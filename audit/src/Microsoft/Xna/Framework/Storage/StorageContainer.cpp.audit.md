# Audit: src/Microsoft/Xna/Framework/Storage/StorageContainer.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Storage/StorageContainer.cpp`
- Audit status: AUDITED (full read, 215 lines)
- Subsystem: `xna-storage` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Storage/StorageContainer.cs` (421 lines,
  read in full)
- Main related tests: not independently located in this pass

## Purpose
Implements the constructor (folder-tree creation), `Dispose()`, and all directory/file/OpenFile
operations declared in the header, resolving relative paths against the container's own
`storagePath_` via `std::filesystem`.

## Executive Verdict
Solid, faithful port overall. `ResolvePath()`'s lack of containment checking is confirmed
FNA-faithful (see the paired `.hpp` report). Two minor findings: the constructor's empty-name guard
uses `std::invalid_argument` rather than the project's established `System::ArgumentNullException`
convention (a further instance of the shard-spanning pattern noted elsewhere this session), and the
custom `GlobMatch()` helper is a reasonable-but-unverified approximation of .NET's own
`Directory.GetFiles(path, searchPattern)` wildcard semantics.

## Checklist Results

### LOW: constructor's empty-`displayName` guard uses `std::invalid_argument` instead of the established convention
Lines 48-50: `if (displayName.empty()) throw std::invalid_argument("A title name has to be provided
in parameter displayName.");`. FNA's real constructor throws `ArgumentNullException` for the
identical case (`StorageContainer.cs` lines 91-94). This is a further instance of the
exception-type-inconsistency pattern already confirmed 3 times this session in the
`xna-framework-core` shard (`GameComponentCollection`, `GraphicsDeviceManager` x2, `Game`) — see the
consolidated cross-cutting entry recommending a project-wide grep rather than treating each instance
as an isolated note. Same pattern recurs in every throw site in this file (lines 94, 102, 109, 126,
144, 152, 159, 176, 209) and in `StorageDevice.cpp`.

### LOW (unverified): `GlobMatch()` is a from-scratch wildcard matcher, not verified against .NET's own semantics
Lines 13-35 implement a minimal `*`/`?` glob matcher used by both `GetDirectoryNames(searchPattern)`
and `GetFileNames(searchPattern)`. FNA's equivalent uses .NET's own `Directory.GetDirectories(path,
searchPattern)`/`Directory.GetFiles(path, searchPattern)`, whose wildcard semantics have some
platform/legacy quirks not necessarily identical to a straightforward custom `*`/`?` matcher (e.g.
historical DOS-8.3-pattern edge cases in some .NET implementations). The algorithm itself (a
classic two-pointer glob matcher with backtracking via `starPi`/`starSi`) is correct for standard
`*`/`?` semantics; the deviation risk, if any, is only in the rarer .NET-specific edge cases, which
were not verified in this pass.

## Detailed Findings
1. **[LOW] Constructor's empty-name guard uses `std::invalid_argument`, not
   `System::ArgumentNullException`** — lines 48-50; a further instance of the shard-spanning
   exception-type pattern.
2. **[LOW, unverified] Custom `GlobMatch()` wildcard semantics not verified against .NET's exact
   `Directory.GetFiles`/`GetDirectories` search-pattern behavior** — lines 13-35.

## Cross-File Observations
- `ResolvePath()` (lines 84-87) is the single chokepoint all path-taking methods funnel through —
  a reasonable structure, but confirmed to have no containment check, matching FNA's own
  `StorageContainer.cs` exactly (see the paired `.hpp` report for the full FNA-fidelity analysis).
- Contrast with `StorageDevice::DeleteContainer()` (audited separately): that method has the
  identical missing-containment-check pattern but performs a recursive delete rather than a
  create/open, and FNA's own equivalent is an unimplemented stub rather than a working method — a
  materially more severe combination. See
  `src/Microsoft/Xna/Framework/Storage/StorageDevice.cpp.audit.md`.

## Missing or Weak Tests
Not independently located in this pass. Worth adding: a `GetFileNames("*.sav")`/
`GetDirectoryNames("Player?")`-style test to confirm `GlobMatch()`'s practical behavior matches
expectations for the common cases XNA games actually use.

## Positive Findings
The constructor's folder-tree creation (`Player{N}`/`AllPlayers` naming, `create_directories`
idempotency) is a faithful, careful port of FNA's exact folder-naming convention, including the
1-based player-index-to-folder-name conversion.

## Final Assessment
Two LOW findings (exception-type-convention inconsistency; an unverified but plausible custom glob
matcher). The path-containment gap noted in the header report is confirmed FNA-faithful, not a
CNA-introduced defect.
