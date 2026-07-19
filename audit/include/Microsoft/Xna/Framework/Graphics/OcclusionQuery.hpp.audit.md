# Audit: include/Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp` (56 lines)
- Audit status: AUDITED (full read)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/OcclusionQuery.cs`
- Main related tests: not independently located in this pass

## Purpose
A GPU query counting visible pixels rendered between `Begin()`/`End()`.

## Executive Verdict
Correct, minimal, matches FNA's real public API surface exactly (`IsComplete`, `PixelCount`,
`Begin()`, `End()`) — this port adds no extra members and drops none.

## Checklist Results
- Doxygen coverage: complete, including the honest platform-behavior note on `PixelCount`
  ("On OpenGL ES 3.0 this returns 0... or 1") — a real, disclosed backend-dependent behavior rather
  than a claimed-uniform guarantee.
- `GetTypeName()` correctly declared `override`, confirmed implemented in the `.cpp`.
- `NOXNA` correctly applied to the destructor and `GetTypeName()` (no FNA equivalent needed for
  either in a GC'd language).

## Detailed Findings
None.

## Cross-File Observations
This is this batch's cleanest 1:1 API match against FNA — a useful positive contrast to the
buffer-class findings elsewhere in this batch.

## Missing or Weak Tests
Not independently located in this pass; a test covering `IsComplete`/`PixelCount`'s behavior
immediately after `Begin()` (before any `End()`) — real XNA's documented undefined/implementation-
specific behavior in that state — was not confirmed either way.

## Positive Findings
Minimal, correct, honestly documents a real backend-specific quirk instead of glossing over it.

## Final Assessment
No findings.
