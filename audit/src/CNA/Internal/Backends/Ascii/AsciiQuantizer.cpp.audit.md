# Audit: src/CNA/Internal/Backends/Ascii/AsciiQuantizer.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/Ascii/AsciiQuantizer.cpp`
- Audit status: AUDITED
- Subsystem: `backend-ascii` shard
- File type: C++ implementation (98 lines)
- Related header/implementation: `include/CNA/Internal/Backends/Ascii/AsciiQuantizer.hpp` (same shard)
- XNA/FNA relevance: N/A (NOXNA presentation utility)
- Graphics backend relevance: the actual pixel-block-averaging/luminance-quantization core `AsciiGraphicsBackend`
  calls every `Present()`.
- FNA reference: N/A
- Main related tests: `examples-tests-ascii` (6 files, not yet audited)

## Purpose

Implements `QuantizeFrameToGrid()`: averages each `cellWidth × cellHeight` block of a source RGBA8 image into one
representative color, derives its luminance (standard Rec. 601 luma weights `0.299R+0.587G+0.114B`), and picks a
glyph index by luminance rank. Also implements `ParseAsciiModeFromEnvironment()` (`CNA_ASCII_MODE` env var parsing,
mirroring `HeadlessGraphicsBackend`'s own `ParseHeadlessModeFromEnvironment()` pattern, per its own doc comment).

## Executive Verdict

**Healthy.** Correct averaging, correct rounding-safe edge handling for non-exact-multiple grid dimensions, no
defects found.

## Checklist Results

### Behavioral correctness / Logic
`grid.columns`/`grid.rows` use ceiling division (`(srcWidth + cellWidth - 1) / cellWidth`) so a source dimension
that isn't an exact multiple of the cell size still gets a final, partial cell rather than truncating remainder
pixels — and each cell's `x1`/`y1` is correctly clamped to `srcWidth`/`srcHeight` (lines 49, 53), so an edge cell
averages only the real remaining pixels, never reading past the source buffer (matches the header's own doc
comment claim exactly, verified by direct derivation rather than taken on faith). `PickGlyphIndex`'s
round-to-nearest-index formula (`t * (rampLength-1) + 0.5`, then clamped) is a standard, correct luminance-to-rank
mapping.

### C++ correctness
The `count ? sumR/count : 0` guard (lines 70-72) against a zero-pixel cell is defensive but not reachable in
practice given `cellWidth`/`cellHeight` > 0 is enforced by `AsciiGraphicsBackend::SetCellSize`'s own validation and
the ceiling-division grid sizing guarantees `x0 < srcWidth`/`y0 < srcHeight` for every valid column/row index —
harmless, not a bug, just defensive redundancy.

### Performance
O(srcWidth × srcHeight) unavoidable per-pixel averaging cost, inherent to the feature (see `.cpp` backend report's
own equivalent note) — not a defect.

### Robustness
`ParseAsciiModeFromEnvironment()` correctly defaults to `Color` for both an unset and an unrecognized
`CNA_ASCII_MODE` value (lines 13-26), matching its own doc comment.

## Detailed Findings

None.

## Cross-File Observations

Mirrors `HeadlessGraphicsBackend`'s `ParseHeadlessModeFromEnvironment()` pattern closely (both explicitly cite
each other in comments) — a good example of intentional, consistent cross-backend design reuse for
environment-variable-driven debug/testing modes.

## Missing or Weak Tests

Not independently assessed (queued for `examples-tests-ascii`).

## Positive Findings

Correct, careful edge-case handling for non-exact-multiple grid dimensions, verified by direct derivation rather
than assumed from the doc comment's claim.

## Final Assessment

No issues found; small, correct, well-scoped utility.
