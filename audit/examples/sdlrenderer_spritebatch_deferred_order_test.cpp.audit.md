# Audit: examples/sdlrenderer_spritebatch_deferred_order_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_spritebatch_deferred_order_test.cpp` (160 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — `SpriteSortMode::Deferred` submission-order pixel test
- Build/CTest registration: `cna_sdl_test(cna_test_sdl_spritebatch_deferred_order …)` /
  `cna_register_backend_test(NAME SDL_Renderer_SpriteBatch_DeferredOrder …)`,
  `cmake/Tests/SdlRendererTests.cmake:50-52`. Header traces to Task 667.
- XNA/FNA relevance: `SpriteSortMode.Deferred` (no depth sort, submission-order painter's algorithm).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp` (`flushBatch()`, the
  `Deferred`/no-sort fall-through branch), `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp`
  (`SdlSpriteBatchBackend::Draw`).
- FNA reference: `Graphics/SpriteBatch.cs` lines ~1150+ (`sortMode == SpriteSortMode.Deferred` skips the sort
  branch entirely) and the `FrontToBackComparer`/`BackToFrontComparer` classes (lines 1602-1619), confirming
  `Deferred` is the one mode with *no* comparator applied.

## Purpose

`SdlSpriteBatchDeferredOrderTest` draws two overlapping-sprite pairs under `SpriteSortMode::Deferred` with
`layerDepth` values deliberately chosen to flip the visible outcome if `Deferred` were secretly implemented as
`FrontToBack` or `BackToFront` sorting instead of raw submission order. Pair 1: submit Red (`layerDepth=0.9`)
then Blue (`layerDepth=0.1`) — correct submission-order result is Blue on top; a `FrontToBack` (ascending-depth)
mistake would put Red on top instead. Pair 2 is the mirror case (submit Red `0.1` then Blue `0.9`), which would
instead be defeated by a mistaken `BackToFront` implementation. Together the two pairs discriminate both
possible wrong-sort-mode failure modes, not just "did Deferred sort correctly" in one direction.

## Executive Verdict

**Healthy.** The test's own reasoning was independently verified against `SpriteBatch.cpp`'s actual
`flushBatch()` implementation (confirmed: `Deferred` takes no branch in the `if/else if` sort-mode chain, so
`spriteQueue_` is drawn in exact push order) and against FNA's own sort-comparator classes, and the two-pair
design genuinely discriminates both wrong-sort hypotheses rather than just re-confirming one.

## Checklist Results

### API / XNA / FNA parity
`SpriteBatch::Begin(SpriteSortMode::Deferred, BlendState::Opaque, samplerState, nullptr, nullptr, nullptr,
Matrix::getIdentityProperty())` (lines 99-102) and the 8-argument `Draw(texture, destRect, srcRect, color,
rotation, origin, effects, layerDepth)` NOXNA overload (lines 105-114) are used — both signatures match their
declarations in `SpriteBatch.hpp`. `layerDepth` semantics (0=front, 1=back, per the header's own Doxygen at
`SpriteBatch.hpp` line 193) are used consistently with the comment's stated intent.

### Behavioral correctness
Independently re-derived the expected result table (lines 118-126) directly from `flushBatch()`'s logic: since
`Deferred` performs no `std::stable_sort` at all, `spriteQueue_` is drawn in exactly the order `pushSprite()`
was called, and — with no depth test on this backend (SDL_Renderer draws 2D quads with no Z-buffer) — the
sprite drawn *last* wins any pixel overlap via simple painter's-algorithm overwrite. Pair 1: Red pushed first
(destRect `(100,100,60,60)`), Blue pushed second (destRect `(130,100,60,60)`) → overlap region `[130,160)×
[100,160)` must be Blue (Blue drawn last) regardless of the `layerDepth` values attached — matches the test's
own expected table (`{145,130,kBlue,...}`, line 121). Pair 2 pushes Red (`layerDepth=0.1`) first at
`(100,200,60,60)` and Blue (`layerDepth=0.9`) second at `(130,200,60,60)` — the `layerDepth` assignment is the
mirror image of Pair 1's, but submission order is unchanged (Red first, Blue second), so draw order alone still
makes Blue win the overlap; this is exactly the pairing needed to catch a hypothetical `BackToFront`
(descending-depth) mistake, under which Blue (0.9) would be drawn *first* and Red (0.1) last, putting Red on top
instead — matching the test's own expected table (`{145,230,kBlue,...}`, line 124).
Both pairs' geometry (`100..160` for the first sprite, `130..190` for the second, 30px overlap) is internally
consistent and the sample points (`115`, `145`, `175` at each row) sit cleanly inside "A-only," "overlap," and
"B-only" regions respectively, with no edge-adjacency risk.

### Logic
`flushBatch()`'s `Deferred` fall-through (no sort branch matched) was independently confirmed by reading the
`if (BackToFront) ... else if (FrontToBack) ... else if (Texture) ...` chain in `SpriteBatch.cpp` — `Deferred`
falls through all three conditions to the trailing `// Deferred: no sort, submission order` comment and the
plain `for (const SpriteInfo& s : spriteQueue_) flushSingle(s);` loop, exactly matching the test's own claimed
mechanism.

### Memory/resource lifetime
`redTex_`/`blueTex_`/`sb_` are `unique_ptr`-owned, constructed once in `Initialize()`; consistent with every
other file in this batch.

### C++ correctness
No unsafe casts; `colourMatch`'s `tol=60` (line 52) is generous enough to absorb any minor SDL blit rounding
while still discriminating red (255,0,0) from blue (0,0,255) decisively (255 vs 0 differs by far more than 60
on both the R and B channels for a mismatch).

### Performance
N/A — single-frame test with four 60×60 draws.

### Thread safety
N/A.

### Architecture
Correctly requires `PresentationMode::NativeBackBuffer` (line 149) for the same physical/logical
coordinate-mapping reason established in the sibling `sdlrenderer_readback_test.cpp` report — independently
re-verified against the same `IGraphicsBackend.hpp`/`GraphicsDeviceManager.hpp` `NativeBackBuffer = 3` enum
value and `SdlGraphicsBackend.cpp`'s `ReadBackbuffer` physical/logical guard.

### Maintainability
160 lines; the `Check` struct + loop pattern (lines 118-138) is the same idiom used consistently across this
shard, keeping six assertions compact and readable.

### Portability
N/A — SDL_Renderer-specific, CMake-gated.

### Robustness
The two-pair design is the specific strength here: a single overlapping pair could only prove "Deferred isn't
X" for one wrong hypothesis at a time; testing both orderings (high-depth-first vs low-depth-first submission)
in the same file closes both failure modes without needing two separate test executables.

### Testing
This file is the correct, sufficient owner of the "Deferred = submission order, not depth order" claim for this
backend; layerDepth-actually-affects-order (`FrontToBack`/`BackToFront`) is separately covered by
`sdlrenderer_spritebatch_layerdepth_test.cpp` in this same batch — a sensible split, not a duplication or gap.

### Cross-file consistency
Test's stated mechanism (`flushBatch()`'s "no sort, submission order" fall-through) matches the actual current
`SpriteBatch.cpp` source exactly; no stale-comment or drifted-behavior issue found.

## Detailed Findings

None. No CRITICAL/HIGH/MEDIUM/LOW findings in this file.

## Cross-File Observations

- Shares the exact overlapping-pair-pixel-check methodology with `sdlrenderer_spritebatch_layerdepth_test.cpp`
  (same batch) — the two files are complementary (this one proves Deferred ignores depth; the other proves
  FrontToBack/BackToFront honor it), not redundant.
- Same `PresentationMode::NativeBackBuffer` / `GetBackBufferData` idiom as every other pixel-verification file
  in this batch and the sibling `sdlrenderer_readback_test.cpp`.

## Missing or Weak Tests

None identified for this file's stated scope.

## Positive Findings

- The two-pair (opposite-order) design is a genuinely well-thought-out test structure that discriminates both
  possible wrong-sort-mode failures, not just one.
- The header comment's technical claim about `flushBatch()`'s fall-through behavior was independently confirmed
  against the current production source, not merely asserted.

## Final Assessment

A correct, well-designed test whose claims about `SpriteSortMode::Deferred`'s submission-order semantics hold
up against independent re-derivation from both the production `SpriteBatch.cpp` source and the FNA reference
comparator classes.
