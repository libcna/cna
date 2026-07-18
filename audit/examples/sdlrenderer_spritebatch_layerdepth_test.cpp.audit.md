# Audit: examples/sdlrenderer_spritebatch_layerdepth_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_spritebatch_layerdepth_test.cpp` (172 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — `SpriteSortMode::FrontToBack`/`BackToFront` layerDepth pixel
  test
- Build/CTest registration: `cna_sdl_test(cna_test_sdl_spritebatch_layerdepth …)` /
  `cna_register_backend_test(NAME SDL_Renderer_SpriteBatch_LayerDepth …)`,
  `cmake/Tests/SdlRendererTests.cmake:44-46`. Header traces to Task 669, explicitly a port of Task 420's EasyGL
  test (`examples/easygl_spritebatch_layerdepth_test.cpp`) for the `FrontToBack` half, with a new `BackToFront`
  scenario added ("no existing test in this project, on any backend, exercises `SpriteSortMode::BackToFront`
  with a real pixel verification" — a claim checked below).
- XNA/FNA relevance: `SpriteSortMode.FrontToBack`/`.BackToFront` (`layerDepth`-based draw-order sort).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp` (`flushBatch()`'s
  `FrontToBack`/`BackToFront` `std::stable_sort` branches).
- FNA reference: `Graphics/SpriteBatch.cs` `FrontToBackComparer`/`BackToFrontComparer` (lines 1602-1619):
  `FrontToBackComparer.Compare` returns `p1.depth.CompareTo(p2.depth)` (ascending); `BackToFrontComparer.Compare`
  returns `p2.depth.CompareTo(p1.depth)` (descending).

## Purpose

Two overlapping opaque-sprite pairs, each deliberately submitted in the *opposite* order from that scenario's
own correct final draw order, so each pair genuinely discriminates "did the sort actually run" from "did
submission order happen to look right anyway." FrontToBack (top row): submit Blue (`layerDepth=0.9`) then Red
(`layerDepth=0.1`) — correct ascending-depth sort draws Red (0.1) first, Blue (0.9) last, so Blue must win the
overlap. BackToFront (bottom row): submit Blue (`layerDepth=0.1`) then Red (`layerDepth=0.9`) — correct
descending-depth sort draws Red (0.9) first, Blue (0.1) last, so Blue must again win the overlap, this time via
the *opposite* sort direction.

## Executive Verdict

**Healthy.** Both scenarios were independently re-derived against `SpriteBatch.cpp`'s actual `std::stable_sort`
predicates and cross-checked against FNA's own `FrontToBackComparer`/`BackToFrontComparer` semantics — the
ascending/descending direction, submission order, and expected overlap winner all check out. The header's claim
that no prior test in this project pixel-verifies `BackToFront` was independently spot-checked (see Cross-file
consistency) and found plausible, not disproven.

## Checklist Results

### API / XNA / FNA parity
`SpriteBatch::Begin(SpriteSortMode::FrontToBack, ...)` and `Begin(SpriteSortMode::BackToFront, ...)` (lines 109,
120) match `SpriteBatch.hpp`'s declared enum values and `Begin` overload. `SpriteSortMode` itself (`Deferred`,
`Immediate`, `Texture`, `BackToFront`, `FrontToBack`) matches FNA's enum member-for-member (confirmed via
`SpriteSortMode.hpp`'s presence and prior cross-file knowledge from the sibling `deferred_order_test` report in
this same batch).

### Behavioral correctness
Independently re-derived both scenarios against `SpriteBatch.cpp`'s actual comparator lambdas:
```
FrontToBack: std::stable_sort(..., [](a,b){ return a.layerDepth < b.layerDepth; });   // ascending
BackToFront: std::stable_sort(..., [](a,b){ return a.layerDepth > b.layerDepth; });   // descending
```
— matching FNA's `FrontToBackComparer` (ascending `p1.depth.CompareTo(p2.depth)`) and `BackToFrontComparer`
(descending `p2.depth.CompareTo(p1.depth)`) exactly, direction-for-direction.

- FrontToBack (lines 113-116): pushed Blue(`0.9`) then Red(`0.1`); after ascending sort, draw order becomes
  Red(0.1) then Blue(0.9) → Blue drawn last → overlap `[130,160)×[100,160)` reads Blue. Matches the test's own
  expected table (`{145,130,kBlue,"...B drawn last"}`, line 133). If `FrontToBack` were mistakenly left
  unsorted (raw submission order Blue-then-Red), Red would win the overlap instead — a real, catchable failure
  mode.
- BackToFront (lines 124-127): pushed Blue(`0.1`) then Red(`0.9`); after descending sort, draw order becomes
  Red(0.9) then Blue(0.1) → Blue drawn last → overlap reads Blue again, matching `{145,230,kBlue,"...D drawn
  last"}` (line 136). If `BackToFront` were mistakenly implemented as ascending (i.e., swapped with
  `FrontToBack`'s own comparator), Red would instead be drawn last and the overlap would read Red — again a
  real, catchable failure mode, and specifically the "did the two sort modes get swapped with each other"
  mistake this pairing is built to catch.

Both pairs use `PointClamp` sampling and `BlendState::Opaque` (lines 106, 111, 122), so there is no
partial-alpha ambiguity in the overlap region — the winning sprite fully replaces the losing one, matching a
plain painter's-algorithm 2D backend with no depth buffer.

### Logic
No stray branching; both `Begin`/two-`Draw`/`End` sequences are structurally identical apart from sort mode and
which color gets which depth — a deliberate, easy-to-audit symmetry.

### Memory/resource lifetime
`redTex_`/`blueTex_`/`sb_` `unique_ptr`-owned, constructed once, consistent with the shard.

### C++ correctness
No unsafe casts; `colourMatch` tolerance (`tol=60`, line 63) discriminates red from blue decisively.

### Performance
N/A — single-frame test with four 60×60 draws across two Begin/End sessions.

### Thread safety
N/A.

### Architecture
Correctly requires `PresentationMode::NativeBackBuffer` (line 161), consistent with the rest of this batch.

### Maintainability
172 lines; the `Check` struct + loop idiom is shared across the shard, keeping six assertions compact.

### Portability
N/A — SDL_Renderer-specific, CMake-gated.

### Robustness
The "opposite of correct order" submission design for both scenarios is the right technique — it specifically
rules out the two most likely implementation bugs (no sort at all; the two directional comparators swapped)
rather than merely confirming *a* plausible-looking result.

### Testing
Correctly split from `sdlrenderer_spritebatch_deferred_order_test.cpp` (same batch): that file proves
`Deferred` ignores `layerDepth` entirely; this file proves `FrontToBack`/`BackToFront` genuinely honor it in
their respective directions. `SpriteSortMode::Texture` is not covered by either file (or, as far as this batch
shows, anywhere in this shard) — a real but modest gap, noted under Missing or Weak Tests rather than as a
Detailed Finding since `Texture` sort has no XNA-documented deterministic ordering contract to violate (FNA's
own `TextureComparer` groups by texture-pointer hash purely as a batching optimization, not a pixel-order
guarantee), so there is no equivalent "did the sort direction get swapped" pixel assertion to even construct.

### Cross-file consistency
The header's claim that this is the first pixel-verified `BackToFront` test in the project was spot-checked via
`grep` across `cmake/Tests/*.cmake` for `spritebatch_layerdepth`/`BackToFront` test registrations in other
backends (EasyGL, Bgfx, Vulkan) — all of the sibling backend layerdepth tests found
(`easygl_spritebatch_layerdepth_test.cpp`, `bgfx_spritebatch_layerdepth_test.cpp`) are registered under names
suggesting `FrontToBack`-style tests only (`*_LayerDepthOrder`/`*_LayerDepth`), consistent with — though not
conclusive proof of — the header's claim; full confirmation would require opening those sibling files, which
are out of scope for this batch.

## Detailed Findings

None. No CRITICAL/HIGH/MEDIUM/LOW findings in this file.

## Cross-File Observations

- Complements `sdlrenderer_spritebatch_deferred_order_test.cpp` in this same batch — together the two files
  give complete pixel-level coverage of `Deferred`, `FrontToBack`, and `BackToFront`, leaving only `Immediate`
  (covered by `sdlrenderer_spritebatch_immediate_flush_test.cpp`, also this batch) and `Texture` (not
  pixel-order-testable, per above) among the five `SpriteSortMode` values.
- Same `PresentationMode::NativeBackBuffer` / two-Begin/End-sessions-per-frame idiom as
  `sdlrenderer_spritebatch_deferred_order_test.cpp`.

## Missing or Weak Tests

`SpriteSortMode::Texture` has no pixel-order test anywhere identified in this batch — reasonable, since FNA's
own `TextureComparer` is a batching optimization with no pixel-visible ordering guarantee to assert against, but
worth a one-line note that "all 5 sort modes" is not literally true of this batch's combined coverage.

## Positive Findings

- Both scenarios were independently re-derived, direction-for-direction, against the actual current
  `SpriteBatch.cpp` sort predicates and FNA's own comparator classes, and both check out exactly.
- The deliberately-opposite submission-order design for both pairs is a strong, well-reasoned test structure
  that specifically targets the two most likely sort-direction implementation mistakes.
- The header's provenance claim (ported from Task 420's EasyGL test, `BackToFront` newly added here) was
  spot-checked against sibling backend test registrations and found consistent.

## Final Assessment

A correct, carefully-designed test whose two scenarios both hold up under independent re-derivation against the
production sort implementation and the FNA reference comparator semantics; the only gap (`Texture` sort mode
lacking any pixel-order test) is inherent to that mode's own lack of an ordering contract, not a defect in this
file.
