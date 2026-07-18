# Audit: examples/bgfx_spritebatch_layerdepth_test.cpp

## Metadata

- Source file: `examples/bgfx_spritebatch_layerdepth_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `SpriteSortMode::FrontToBack` draw-order pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_spritebatch_layerdepth …)` /
  `cna_register_backend_test(NAME Bgfx_SpriteBatch_LayerDepthOrder …)`, `cmake/Tests/BgfxTests.cmake:756-758`).
- XNA/FNA relevance: direct — `SpriteSortMode.FrontToBack` and `SpriteBatch.Draw`'s `layerDepth` parameter.
- FNA reference: `Graphics/SpriteBatch.cs` sort comparers (ascending-depth comparer for `FrontToBack`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp` (`flushBatch`, lines 185-214),
  `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` (`SubmitSprite`, `(void)layerDepth;` at line 1430).

## Purpose

3-check pixel test (Task 803, a Bgfx port of Task 420's EasyGL test) proving that `SpriteSortMode::FrontToBack`
actually reorders overlapping sprites on screen by `layerDepth`, not just by raw submission order. Two 60×60
opaque sprites overlap: Sprite A (Red, depth 0.1) and Sprite B (Blue, depth 0.9), submitted in the *wrong* order
in code (B first, A second) so that a correct depth-sort (ascending for `FrontToBack`, drawing A then B) must
still put B on top of the overlap region via simple painter's-algorithm overdraw — a naive
"draw-in-submission-order" bug would put A on top instead. The file's own comment explicitly scopes this
narrowly: CPU-side sort-order-per-mode correctness for all 4 `SpriteSortMode` values was already proven
backend-agnostically by Tasks 415/416 via a mock backend; this test's only job is the backend-specific
question of whether the resulting draw order is actually reflected on screen with no depth-test interference.

## Executive Verdict

**Healthy** — every claim in the file's own header comment was independently verified against the real
production code: `SpriteBatch::flushBatch()`'s `FrontToBack` branch really does `stable_sort` ascending on
`layerDepth` (`a.layerDepth < b.layerDepth`, ascending, ", so A(0.1) is drawn first), and
`BgfxGraphicsBackend::SubmitSprite` really does discard `layerDepth` entirely (`(void)layerDepth;`, line 1430)
— vertices carry no Z, confirming the "layerDepth is CPU-side sort key only" claim exactly.

## Checklist Results

### API / XNA / FNA parity
`SpriteSortMode::FrontToBack` combined with `SpriteBatch::Draw`'s `layerDepth` float parameter matches FNA's
API surface exactly (`SpriteBatch.Draw(..., float layerDepth)`).

### Behavioral correctness
Traced the full pipeline:
1. `SpriteBatch::Begin(SpriteSortMode::FrontToBack, …)` sets `sortMode_` (line 108 of `SpriteBatch.cpp`).
2. `Draw(*blueTex_, …, 0.9f)` then `Draw(*redTex_, …, 0.1f)` — submitted in the "wrong" order deliberately,
   pushed into `spriteQueue_` via `pushSprite` (queue order preserved, no sort yet since `sortMode_ !=
   Immediate`).
3. `End()` calls `flushBatch()`, which for `FrontToBack` does
   `std::stable_sort(…, [](a,b){ return a.layerDepth < b.layerDepth; })` (`SpriteBatch.cpp:196-201`) —
   ascending, so post-sort order is [Red(0.1), Blue(0.9)] regardless of submission order.
4. Each sorted entry is drawn via `flushSingle` → `backend_->Draw(...)` in that order; `BgfxSpriteBatchBackend`
   forwards straight to `SubmitSprite`, which never reads `layerDepth` and never touches an NDC Z (confirmed —
   `SpriteVertex` in the Bgfx sprite path carries only `{x,y,u,v,abgr}`, no Z field beyond whatever
   `gl_Position`/gl_default Z bgfx assigns for a 2D ortho projection, which is identical for both sprites).
5. With `BlendState::Opaque` and no depth test configured for the sprite pass, drawing Blue last after Red in
   the overlap region simply overwrites those pixels — the intended "painter's algorithm" outcome.
- Reproduced the file's own geometry math: A=`(100,100,60,60)`, B=`(130,100,60,60)` ⇒ overlap
  x:[130,160)×y:[100,160); check points (115,130)=A-only, (145,130)=overlap, (175,130)=B-only all fall inside
  their claimed regions (A spans x:[100,160), B spans x:[130,190); 115<130 is A-only, 175>160 is B-only,
  145 is inside both).

### Logic
The deliberate wrong-order submission (`Draw(B)` then `Draw(A)`) is the correct technique to discriminate
depth-based sorting from raw submission order — a backend or `SpriteBatch` bug that silently degraded
`FrontToBack` to `Deferred` (no sort) would flip the overlap-region expectation to Red instead of Blue, and
this test would genuinely catch that (not just "compiles and doesn't crash").

### Cross-file consistency
Matches its own stated precedent (`easygl_spritebatch_layerdepth_test.cpp`, Task 420) exactly in scene layout,
only differing in the required per-check `RunCheck` restructuring for Bgfx's readback quirk — verified the
restructuring introduces no behavioral difference (each `RunCheck` call redoes the identical
`Clear`+`Begin`/`Draw`/`Draw`/`End` sequence).

## Detailed Findings

No CRITICAL/HIGH/MEDIUM/LOW defects found in this file.

## Missing or Weak Tests

- Only `SpriteSortMode::FrontToBack` is exercised on this backend (matching Task 420's own established scope);
  `BackToFront` is the mirror-image mode and is not separately pixel-tested on Bgfx specifically, though its
  CPU-side comparer is covered by the backend-agnostic Tasks 415/416. This is a proportionate, explicitly
  documented scope decision rather than an oversight — not raised as a numbered finding.

## Positive Findings

- The file's own scope-narrowing comment (deferring to Tasks 415/416 for CPU-side sort-order correctness) is
  accurate and avoids duplicating coverage that already exists elsewhere — this audit confirmed by reading
  `SpriteBatch.cpp`'s `flushBatch` directly that the sort key and comparator direction are exactly as claimed.
- The claim that "CNA's sprite vertices carry no Z component... layerDepth is used only as a CPU-side sort key"
  was independently verified against `BgfxGraphicsBackend::SubmitSprite`'s actual parameter handling
  (`(void)layerDepth;`), not merely repeated from the comment.
- Check-point geometry is carefully chosen to be robust against off-by-one rasterization edges (15-30px inside
  each claimed region, not touching a boundary).

## Final Assessment

A precise, well-scoped backend-specific regression test; its narrow claim (draw order is correctly reflected
on screen, with no depth-test surprise) was independently verified end-to-end against the real `SpriteBatch`
sort implementation and the real Bgfx submission code, with no discrepancy found.
