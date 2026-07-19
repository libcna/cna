# Audit: src/Microsoft/Xna/Framework/Storage/StorageDevice.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Storage/StorageDevice.cpp`
- Audit status: AUDITED (full read, 264 lines)
- Subsystem: `xna-storage` shard (last file of the shard — 6/6 complete)
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Storage/StorageDevice.cs` (356 lines,
  read in full), especially `DeleteContainer` (lines 349-352), `FreeSpace`/`IsConnected`/
  `TotalSpace` (lines 32-105), and the `NotAsyncLie`/`OpenContainerLie`/`ShowSelectorLie` pattern
  (lines 139-343)
- Main related tests: not independently located in this pass

## Purpose
Implements storage-root resolution (via `SDL_GetPrefPath`, with `XDG_DATA_HOME`/`HOME` fallbacks),
free-space/connectivity queries, the fake-async container-open and device-selector flows, and
`DeleteContainer`.

## Executive Verdict
Needs attention. `getFreeSpaceProperty()`/`getIsConnectedProperty()`/`getTotalSpaceProperty()` and
the fake-async `BeginOpenContainer`/`EndOpenContainer`/`BeginShowSelector`/`EndShowSelector` flows
are faithful, well-matched ports of FNA's real behavior (including the identical
`StorageDeviceNotConnectedException` message text). However, `DeleteContainer()` (lines 194-199)
is a confirmed HIGH-severity finding: it performs a real `fs::remove_all()` recursive delete driven
directly by a caller-supplied `titleName` string, with **no check that the resolved path stays
within the storage root** — and, critically, this is **not a faithful reproduction of any FNA
behavior**, since FNA's own `DeleteContainer` is simply `throw new NotImplementedException();`
(`StorageDevice.cs` lines 349-352). CNA chose to actually implement functionality FNA never
provided, and the implementation is unsafe.

## Checklist Results

### HIGH: `DeleteContainer()` is a CNA-original recursive delete with zero path-containment validation
Lines 194-199:
```cpp
void StorageDevice::DeleteContainer(const std::string& titleName)
{
    if (titleName.empty())
        throw std::invalid_argument("titleName must not be empty.");
    fs::remove_all(fs::path(EnsureStorageRoot()) / titleName);
}
```
`std::filesystem::path::operator/` (used here via `fs::path(...) / titleName`) has the identical
"absolute right-hand operand replaces the left" and "`..` components are not specially rejected"
behavior as .NET's `Path.Combine` (the mechanism already confirmed to have the same gap in
`StorageContainer`'s own methods, verified FNA-faithful there). But here, the consequence is a
**recursive directory deletion** (`fs::remove_all`), not a file/directory create-or-open:

- `DeleteContainer("/")` (or any absolute path the process has permission to modify) resolves to
  that absolute path verbatim — `fs::path(root) / "/some/absolute/path"` discards `root` entirely
  per `std::filesystem` concatenation rules — and recursively deletes it.
- `DeleteContainer("../../../SomeOtherAppData")` resolves outside the storage root via ordinary
  `..` traversal, which `fs::remove_all` does not re-validate before deleting.

Crucially, **FNA's own `DeleteContainer` is an unimplemented stub**:
```csharp
public void DeleteContainer(string titleName)
{
    throw new NotImplementedException();
}
```
(`StorageDevice.cs` lines 349-352). This means the missing-containment-check pattern here is
fundamentally different in kind from every other "confirmed FNA-faithful, therefore acceptable per
project policy" finding elsewhere this session (`TitleContainer::OpenStream()`,
`StorageContainer`'s own directory/file methods): there is no FNA behavior being faithfully
reproduced here at all. CNA affirmatively added new, real functionality beyond what FNA provides,
and that new functionality is a genuine path-traversal-enabled recursive-delete primitive reachable
from a public XNA-surface method (`StorageDevice::DeleteContainer`, called with a caller-supplied
string — e.g. a save-slot name, a mod identifier, or any other value a game might pass through with
insufficient sanitization).

**Fix shape**: resolve `titleName` against the storage root, canonicalize both paths (e.g.
`fs::weakly_canonical`), and verify the resolved path is a strict descendant of the storage root
before calling `fs::remove_all` — reject (throw) otherwise. This is the same containment-check
pattern this project has already implemented correctly elsewhere (e.g. `CnjSourceFile.hpp`'s
path-containment check, praised earlier this session for using `std::filesystem::path` component
iteration rather than string-prefix comparison).

### LOW: exception-type-convention inconsistency (further instance of the shard-spanning pattern)
Lines 184, 196-197, 243: `EndOpenContainer`'s cast-failure guard, `DeleteContainer`'s empty-name
guard, and `EndShowSelector`'s cast-failure guard all throw `std::invalid_argument` rather than a
`System::*Exception` type. FNA doesn't have a directly equivalent bad-cast guard for the two
`EndXxx` cases (an invalid cast in C# would throw an unchecked `NullReferenceException` from the
`as`-cast-then-dereference pattern — CNA's explicit, clearer guard here is arguably an improvement),
but the empty-`titleName` case has no FNA equivalent to compare against either, since
`DeleteContainer` is unimplemented there. Noted for completeness as part of the shard-spanning
exception-type pattern, not as an independent new finding.

### LOW (info): `EnsureStorageRoot()`'s static state is not thread-safe
Lines 68-105: `storageRootInitialized_`/`storageRoot_` are plain static members mutated with no
synchronization. If `StorageDevice` methods were ever called concurrently from multiple threads
before first initialization, this could race. Given XNA's single-threaded-by-convention game-loop
assumption (consistent with this project's own established assumptions elsewhere, e.g.
`FrameworkDispatcher`'s similar single-threaded model), this is unlikely to matter in practice, and
is noted only for completeness.

## Detailed Findings
1. **[HIGH] `DeleteContainer()` performs a real recursive delete via `fs::remove_all` with no
   path-containment check, and is not an FNA-faithful gap — FNA's own equivalent is unimplemented**
   — lines 194-199; cf. FNA `StorageDevice.cs` lines 349-352.
2. **[LOW] Exception-type-convention inconsistency across three throw sites** — lines 184, 196-197,
   243; further instance of the shard-spanning pattern.
3. **[LOW, info] `EnsureStorageRoot()`'s static state has no thread-safety guard** — lines 68-105.

## Cross-File Observations
- This is the most severe finding of the entire `xna-storage` shard and arguably one of the most
  severe of this audit session so far, given it is a genuinely new (non-FNA-faithful) vulnerability
  with a concrete, plausible trigger path (any code passing a lightly-sanitized or
  user-influenced string to `DeleteContainer`).
- Recommend adding this finding to `AUDIT_CROSS_CUTTING_FINDINGS.md` as its own entry (not folded
  into the general "missing path-containment check" pattern note, since the FNA-faithfulness
  angle that makes those other instances "acceptable" does not apply here).

## Missing or Weak Tests
Not independently located in this pass. Highest-priority test for the `tests-*` shard: call
`DeleteContainer` with a `titleName` containing `..` components or an absolute path pointing at a
directory outside the storage root (in a temp-directory-scoped test fixture), and assert the
directory survives / the call is rejected.

## Positive Findings
`getFreeSpaceProperty()`/`getIsConnectedProperty()`/`getTotalSpaceProperty()` are faithful,
well-matched ports (including FNA's exact `StorageDeviceNotConnectedException` message text and the
"null-drive-means-guess-long-max" fallback behavior, adapted correctly to `fs::exists`/`fs::space`).
The fake-async `BeginOpenContainer`/`BeginShowSelector` family correctly mirrors FNA's own
"Private XNA Lies" synchronous-completion pattern, including the exact per-overload
`sizeInBytes`/`directoryCount`-ignoring behavior and `PlayerIndex` propagation.

## Final Assessment
One HIGH finding: `DeleteContainer()` is a CNA-original, unsafe recursive-delete implementation
with zero path-containment validation, where FNA itself provides no equivalent functionality at
all. Two LOW findings (exception-type convention; static-state thread-safety, informational only).
Recommend prioritizing a fix for the HIGH finding given its concrete, plausible exploitability.
