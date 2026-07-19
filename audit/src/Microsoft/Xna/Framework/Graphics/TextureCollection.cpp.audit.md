# Audit: src/Microsoft/Xna/Framework/Graphics/TextureCollection.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/TextureCollection.cpp`
- Audit status: AUDITED (full read, 46 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/TextureCollection.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements the fixed-size texture-slot array, its accessor operators, and `RemoveDisposedTexture`.

## Executive Verdict
Correct as far as it goes — the disposed-texture check it does implement correctly uses
`System::ObjectDisposedException` — but missing FNA's real (DEBUG-only) render-target-conflict
check; see the paired `.hpp` report for the full analysis. Index-bounds exceptions use the shard's
recurring raw-`std::` pattern.

## Checklist Results
- `operator[](int)` (lines 16-23) / `operator()(int, Texture*)` (lines 25-36): both bounds-check
  `index` against `[0, MaxTextures)`.
- `operator()`'s disposed-texture check (lines 31-34): correctly throws
  `System::ObjectDisposedException(texture->getNameProperty())`, matching FNA's own
  `ObjectDisposedException(value.GetType().ToString())` in spirit (FNA uses the type name; CNA uses
  the instance's `Name` — a reasonable, if not identical, diagnostic-string choice, not a functional
  difference).
- `RemoveDisposedTexture` (lines 38-45): correctly nulls every slot holding the given pointer,
  matching FNA's own loop-and-null-via-indexer pattern.

## Detailed Findings

### MEDIUM — index-bounds exceptions use raw `std::out_of_range` instead of a `System::` exception
type
Lines 20 and 29 both throw `std::out_of_range("Texture index out of range.")`. FNA's own indexer has
no explicit bounds check (a plain C# array access throws `System.IndexOutOfRangeException`
automatically) — the closer project-convention equivalent here would be `System::ArgumentOutOfRangeException`
(this project's established type for an out-of-range integer parameter, used correctly elsewhere in
this same shard, e.g. `RenderTarget2D`'s `System::InvalidOperationException` usage). Same recurring
cross-cutting pattern as the other files in this batch.

### MEDIUM — missing render-target/sampler conflict check
See the paired `.hpp` report for the full analysis; this `.cpp`'s `operator()` (lines 25-36) has no
equivalent to FNA's DEBUG-only render-target-conflict guard.

## Cross-File Observations
None beyond the paired `.hpp` report.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
`RemoveDisposedTexture`'s loop-and-null pattern is correct and simple, matching FNA's own semantics.

## Final Assessment
Two MEDIUM findings: raw `std::out_of_range` instead of `System::ArgumentOutOfRangeException`, and
the missing render-target/sampler conflict check (shared with the paired `.hpp` report).
