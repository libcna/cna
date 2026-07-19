# Audit: src/Microsoft/Xna/Framework/Content/ContentReader.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Content/ContentReader.cpp`
- Audit status: AUDITED (full read, 269 lines)
- Subsystem: `xna-content` shard (last file of the shard — 15/15 complete)
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: real FNA's `ReadExternalReference<T>()` uses
  `MonoGame.Utilities.FileHelpers.ResolveRelativePath` with no containment check at all ("FNA just
  lets the OS fail to find an escaping path", per this file's own comment) — CNA's containment
  check is a disclosed, NOXNA hardening addition with no FNA equivalent to compare fidelity against
- Main related tests: not independently located in this pass

## Purpose
Implements `ReadExternalReference<T>()`'s path resolution and containment check,
`InitializeTypeReaders()` (type-reader-table parsing and shared-resource-count validation), and
`ReadSharedResources()`'s two-pass read-then-fixup protocol.

## Executive Verdict
Needs attention. `maxSharedResourceCount` is confirmed genuinely enforced (lines 224-230),
resolving what the `cna-internal-core` shard's `XnbReadLimits.hpp` report could previously only
confirm via grep as "a symbol referenced there." However, `ResolveRelativeAssetPath()`'s
containment check (lines 25-49) — the mechanism backing `ReadExternalReference<T>()`'s documented
"rejected outright" guarantee — has a confirmed HIGH-severity gap: it only rejects a `..`-style
relative escape, not an absolute-path escape, which slips through both this function's own check
and `ContentManager::BuildAssetPath()`'s identical unchecked `fs::path` concatenation downstream.

## Checklist Results

### HIGH: `ResolveRelativeAssetPath()`'s containment check only catches relative `..` escapes, not absolute paths
Lines 25-49:
```cpp
std::string ResolveRelativeAssetPath(const std::string& filePath, const std::string& relativeFile)
{
    ...
    const fs::path base = fs::path(normalizeSeparators(filePath)).parent_path();
    const fs::path combined = base / normalizeSeparators(relativeFile);
    const std::string resolved = combined.lexically_normal().generic_string();

    if (resolved == ".." || resolved.rfind("../", 0) == 0)
    {
        throw ContentLoadException(...);
    }
    return resolved;
}
```
`std::filesystem::path::operator/` has the standard-mandated behavior that if the right-hand
operand `is_absolute()`, the result *is* that right-hand operand — `base` is discarded entirely.
So `ResolveRelativeAssetPath("some/asset", "/etc/passwd")` (or, on a build where the relevant
platform allows it, a Windows drive-rooted path) produces `combined = "/etc/passwd"`, which
`lexically_normal()` does not change, and which does **not** begin with `"../"` and is not exactly
`".."` — so the containment check silently passes it through. The caller,
`ReadExternalReference<T>()` (lines 52-68), then calls `contentManager_->Load<T>(resolved)` with
this absolute path unchanged.

Tracing one level further: `ContentManager::BuildAssetPath()` (audited separately,
`ContentManager.cpp` lines 295-307) does `(fs::path(rootDirectory_) / assetName).string()` — the
exact same absolute-path-discards-base behavior — so an absolute `assetName` reaching `Load<T>()`
is *also* not re-contained there. The end-to-end effect: a crafted `.xnb`/`.cnj` file whose
`Texture2D`/`TextureCube` external reference (the two types this method is explicitly instantiated
for, `ContentReader.cpp` lines 70-71) is an absolute path causes CNA to attempt to open and parse
*that exact absolute path* as a texture, with no containment check anywhere in the call chain —
directly contradicting this method's own doc comment ("a reference that resolves above the content
root's own logical space is rejected outright").

Practical impact is narrower than `StorageDevice::DeleteContainer()`'s (this reads/parses a file as
a texture rather than deleting a directory tree, and only affects the two asset types with a
`ReadExternalReference` instantiation today), but it is a genuine content-root escape via a
documented-as-hardened path, from a crafted asset file — the kind of input a modding/downloaded-content
scenario would plausibly not fully trust.

**Fix shape**: after `lexically_normal()`, additionally reject (or re-root) a `combined`/`resolved`
path that `is_absolute()`, not just one beginning with `"../"` — e.g. check
`fs::path(normalizeSeparators(relativeFile)).is_absolute()` up front and reject immediately,
mirroring the same fix needed for `ContentManager::BuildAssetPath()`/`StorageContainer::ResolvePath()`/
`StorageDevice::DeleteContainer()` (all sharing this exact `fs::path` concatenation footgun,
confirmed across two different shards this session).

### Positive (confirms a prior grep-only finding): `maxSharedResourceCount` is genuinely enforced
Lines 224-230: `sharedResourceCount_ = Read7BitEncodedInt(); if (sharedResourceCount_ < 0 ||
sharedResourceCount_ > limits_.maxSharedResourceCount) { throw ContentLoadException(...); }` — a
real, correct bounds check, confirming what `XnbReadLimits.hpp`'s own audit report (`cna-internal-core`
shard) could previously only note as "a symbol referenced there" via grep.

### Positive: two-pass shared-resource read-then-fixup order matches FNA exactly
Lines 251-268 read every shared resource first, then run every queued fixup — matching FNA's own
explicit comment ("we have to read _all_ the objects first, BEFORE doing fixups") and avoiding a
forward-reference ordering bug a naive single-pass implementation would hit.

## Detailed Findings
1. **[HIGH] `ResolveRelativeAssetPath()`'s containment check only rejects relative `..` escapes,
   not absolute-path escapes, contradicting its own "rejected outright" documentation** — lines
   25-49; end-to-end chain confirmed via `ContentManager::BuildAssetPath()`'s identical unchecked
   concatenation.

## Cross-File Observations
- This is the same underlying `std::filesystem::path`-concatenation footgun already confirmed in
  `StorageContainer::ResolvePath()` (FNA-faithful there) and
  `StorageDevice::DeleteContainer()` (NOT FNA-faithful, HIGH finding) from the `xna-storage` shard
  audited earlier this session — three independent confirmations of the same C++-specific pitfall
  (`base / rhs` silently discards `base` when `rhs.is_absolute()`) across two different subsystems.
  Worth a project-wide grep for `fs::path(...) / <caller-supplied string>` call sites without a
  preceding `is_absolute()` rejection, as part of Pass 5.
- Unlike the `StorageContainer` case, this one is NOT FNA-faithful in the sense of "FNA has the
  identical gap" — FNA's own `ReadExternalReference` genuinely has no containment check *at all*
  ("FNA just lets the OS fail to find an escaping path", this file's own comment, lines 39-40), so
  CNA's containment check is a disclosed *addition* beyond FNA that happens to be incomplete, not a
  faithfully-reproduced FNA gap.

## Missing or Weak Tests
Not independently located in this pass. Highest-priority test for the `tests-*` shard: a `.xnb`
fixture whose `Texture2D`/`TextureCube` external reference is an absolute path (in a
temp-directory-scoped fixture), asserting the load is rejected rather than attempting to open the
absolute path.

## Positive Findings
`maxSharedResourceCount` enforcement and the two-pass shared-resource read/fixup ordering are both
correct and well-verified against FNA/the project's own declared design.

## Final Assessment
One HIGH finding: a documented containment guarantee with a real, concrete absolute-path bypass,
the third confirmed instance this session of the same `std::filesystem::path`-concatenation
pitfall. Recommend adding to `AUDIT_CROSS_CUTTING_FINDINGS.md` as a consolidated
"absolute-path-escapes-containment-check" pattern alongside the `StorageDevice::DeleteContainer()`
finding, and recommend a project-wide grep sweep in Pass 5.
