# Audit: include/Microsoft/Xna/Framework/Input/MouseCursor.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Input/MouseCursor.hpp`
- Audit status: AUDITED (full read, 148 lines)
- Subsystem: `xna-input` shard
- File type: C++ header
- XNA/FNA relevance: N/A — entirely `NOXNA`, a MonoGame-derived extension with no XNA 4.0/FNA
  equivalent type at all (explicitly disclosed)
- Main related tests: not independently located in this pass

## Purpose
Wraps an `SDL_Cursor*`: 12 stock system cursors as lazy process-lifetime singletons, plus
`FromTexture2D()` for a custom cursor image.

## Executive Verdict
Correct. The class-level disclosure ("No `MouseCursor` type exists in XNA 4.0 or FNA") is accurate
and appropriately placed. The stock-cursor singleton design's `Dispose()`-is-a-deliberate-no-op
comment (lines 58-65) correctly explains why: freeing a shared process-lifetime SDL cursor out from
under every other user would be a real bug, and explicitly warns against moving a stock-cursor
reference (only obtain-and-use-in-place is safe).

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
`FromTexture2D()`'s pixel-format/lifetime handling verified correct in the paired `.cpp`.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Clear ownership-model documentation for the stock-cursor singletons, correctly warning against a
real use-after-move hazard.

## Final Assessment
No findings.
