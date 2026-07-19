# Audit: tests/Microsoft/Xna/Framework/Graphics/SpriteBatchTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Graphics/SpriteBatchTests.cpp` (751 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-graphics` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `SpriteBatch.hpp`/`.cpp`, `SpriteSortMode.hpp`, `SpriteEffects.hpp`
- Main related tests: N/A (this IS a test file)

## Purpose
Exercises `SpriteSortMode`/`SpriteEffects` enum values, `SpriteBatch`'s Begin/End/Draw guard
behavior (no-backend construction), and — via a `RecordingSpriteBatchBackend` test double — the
real batching/sort-mode/flush-timing contracts for `Deferred`/`Immediate`/`Texture`/
`FrontToBack`/`BackToFront`.

## Executive Verdict
The sort-mode behavioral tests (`SpriteBatchSortModeTest.*`) are excellent — each one is
specifically designed to distinguish the mode under test from a plausible incorrect
implementation (e.g. a negative control proving `Deferred` doesn't eagerly flush, an
interleaved-submission-order test for `Texture` grouping). However, **this file misses both of the
two already-confirmed HIGH-severity `SpriteBatch::DrawString` defects**, because it contains no
`DrawString`-with-a-real-font, non-guard-only test at all.

## Checklist Results
- **Item 1 cross-check (`unordered_map::end()` UB via `DrawString`)**: every `DrawString` test in
  this file (`DrawStringStdStringBeforeBeginThrows`, `DrawStringScalarScaleBeforeBeginThrows`, etc.)
  uses `makeEmptyFont()` (line 155-166: empty `characters`, empty `glyphBounds`, `defaultChar =
  std::nullopt`) purely to exercise the "called before `Begin()`" guard — the guard throws
  `std::runtime_error` before any character lookup ever happens. **No test in this file calls
  `DrawString` after `Begin()` with a real, non-empty font at all** — so the default-character
  fallback path inside `DrawString` (distinct from, but structurally identical to, the same defect
  in `SpriteFont::MeasureString`) is entirely untested. **Verdict: MISSES.**
- **Item 2 cross-check (`SpriteEffects` combined-flags array-bounds OOB in `DrawString`)**: no test
  in this file calls `DrawString` with any `SpriteEffects` value at all (every `DrawString` call
  uses the guard-only, before-`Begin()` overloads); the only `SpriteEffects`-bearing calls are to
  `Draw` (e.g. `DrawIsRecordedWithExactParameters` uses `SpriteEffects::FlipHorizontally` alone,
  never combined with `FlipVertically`). **Verdict: MISSES** — no test exercises the combined value
  through either `Draw` or `DrawString`.
- The `RecordingSpriteBatchBackend`-based tests (Tasks 411-416) are a strong, deliberate design:
  `ImmediateFlushesInsideDrawBeforeEnd` places its critical assertion *between* `Draw()` and `End()`
  specifically to catch an implementation that only flushes at `End()` regardless of sort mode — the
  file's own comment explicitly explains why this placement matters, a sign of careful test design
  rather than copy-pasted boilerplate.
- `TextureGroupsDrawsByTextureAndPreservesGroupOrder` correctly avoids asserting on
  pointer-address-dependent sort order (acknowledging `std::stable_sort`'s comparator is a raw
  pointer comparison) while still verifying the two properties that *are* part of the real contract
  (grouping and within-group submission-order stability) — a mature, well-reasoned test design that
  avoids a flaky, implementation-detail-dependent assertion.

## Detailed Findings
None beyond the two assigned cross-check misses noted above.

## Cross-File Observations
See `SpriteFontTests.cpp`'s own report for the `MeasureString` half of the Item 1 defect (also
missed, for the same underlying reason — no test exercises a default character absent from the
font's own character set). See `SpriteEffectTests.cpp`'s own report — it also has no test of the
combined-flags value.

## Missing or Weak Tests
- A `DrawString(font, text, ...)` test using a real (non-empty) font, called after `Begin()`, with
  a character not in the font's `characters` and a `defaultCharacter` also not in `characters`,
  would catch Item 1's `DrawString` half.
- A `DrawString(..., static_cast<SpriteEffects>(3), ...)` test (mirroring the exact pattern already
  used elsewhere in this codebase's own `examples/sdlgpu_2d_test.cpp`) would catch Item 2, and
  should assert on a recorded backend call's glyph positions rather than merely "doesn't crash",
  since the underlying array-bounds read might not crash deterministically without ASan/UBSan.

## Positive Findings
The sort-mode test suite (Tasks 412-416) is one of the strongest, most deliberately-designed test
groups encountered in this audit pass — each test is built around a specific, named contract
property (eager-flush timing, submission-order preservation, grouping-without-address-dependence,
ascending/descending depth sort) rather than a generic "looks right" smoke check.

## Final Assessment
Two confirmed misses (Items 1 and 2 — both HIGH-severity `DrawString` defects), entirely due to the
complete absence of any post-`Begin()` `DrawString` test with a real font in this file. The
non-`DrawString` test suite (sort modes, `Draw` guard/recording behavior) is otherwise excellent.
