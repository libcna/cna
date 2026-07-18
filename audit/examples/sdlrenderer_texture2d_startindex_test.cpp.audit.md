# Audit: examples/sdlrenderer_texture2d_startindex_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_texture2d_startindex_test.cpp` (146 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — Task 680, `Texture2D::SetData`'s `startIndex`/
  `elementCount` array-slicing arithmetic, verified end-to-end on the real SDL_Renderer GPU texture.
- CMake registration: `cna_sdl_test(cna_test_sdl_texture2d_startindex
  examples/sdlrenderer_texture2d_startindex_test.cpp)` / `SDL_Renderer_Texture2D_StartIndex` — confirmed
  at `cmake/Tests/SdlRendererTests.cmake:121-123`.
- XNA/FNA relevance: direct — `Texture2D.SetData<T>(int level, Rectangle? rect, T[] data, int startIndex,
  int elementCount)`'s `startIndex`/`elementCount` slicing semantics (FNA `Texture2D.cs`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/Texture2D.cpp`
  (`SetData(level, rect, data, startIndex, elementCount)`, lines 245-316, specifically the
  `src = startIndex + row*w + col` formula at line 287).

## Purpose

A direct port of the existing backend-agnostic Task 170A scenario onto SDL_Renderer: a 4x1 texture filled
entirely Red, then a 6-element source array `[Green, Green, Blue, Blue, Green, Green]` sliced with
`startIndex=2, elementCount=2` into a `(1,0,2,1)` rectangle — only `data[2]`/`data[3]` (the two Blue
elements) should actually be read and written; the Green elements bracketing them on both sides must be
skipped, producing `R B B R`. Drawn 4x scaled via `SpriteBatch` (`PointClamp`) and verified against the
real framebuffer. The file's own comment correctly identifies that the *slicing arithmetic itself* is a
pure CPU-side, backend-agnostic concern (already covered by the existing `easygl_texture2d_partial_rect_test.cpp`,
Task 170, via a `GetData`-only round trip) — the value this SDL_Renderer-specific test adds is confirming
the sliced-and-merged result survives the real `SdlTextureBackend::UpdatePixels()` whole-buffer re-upload
correctly.

## Executive Verdict

**Healthy** — the slicing arithmetic claim was independently re-derived by hand against the actual
production formula and matches exactly; the pixel checks correctly distinguish the two untouched Red
texels from the two newly-written Blue ones.

## Checklist Results

### API / XNA / FNA parity
`tex_->SetData(0, &rect2x1, src6, 2, 2)` (line 86) matches FNA's 5-argument `SetData<T>` overload order
(`level, rect, data, startIndex, elementCount`) exactly.

### Behavioral correctness
Independently re-derived the slicing formula: `Texture2D::SetData`'s merge loop
(`Texture2D.cpp` lines 283-294) computes `src = startIndex + row*w + col`. For `rect2x1 = (1,0,2,1)`
(`w=2, h=1`), `startIndex=2`: `row=0, col=0 → src=2` (`src6[2]=kBlue`); `row=0, col=1 → src=3`
(`src6[3]=kBlue`) — both Blue elements are correctly selected, and the `Green` elements at `src6[0,1,4,5]`
are never read at all (the loop bounds are `row<h=1, col<w=2`, so `src` never reaches those indices).
This independently confirms the file's own claim ("only `data[2]`/`data[3]` are read; the Green elements at
either end must be skipped") — not merely trusted, actually re-derived.

### Logic
Destination-pixel expectations (lines 107-112) correctly map the 4x1 source (`texel(0)=Red unwritten,
texel(1)=Blue(data[2]), texel(2)=Blue(data[3]), texel(3)=Red unwritten`) onto the 4x-scaled 16x4
destination: sample x-coordinates `2, 6, 10, 14` are each the horizontal centre of their respective 4px-
wide texel column (`[0,4), [4,8), [8,12), [12,16)`) — correctly avoids sampling at texel boundaries where
point-filtering rounding could be ambiguous.

### C++ correctness
`const Rectangle rect2x1(1, 0, 2, 1)` and `const Color src6[6]` are both correctly-sized, stack-local,
const data with no lifetime concerns spanning the synchronous `SetData` call.

### Memory/resource lifetime
`gdm_`/`sb_`/`tex_` are `unique_ptr` members with standard RAII lifetime.

### Performance / Thread safety
N/A — one-shot CTest executable, single-threaded.

### Architecture
Correctly XNA-facing throughout, only public `Texture2D`/`SpriteBatch`/`GraphicsDevice` API used.

### Maintainability
146 lines, single clear responsibility, ASCII diagram in the header comment aids readability, consistent
with sibling files' style.

### Portability
Correctly requires `PresentationMode::NativeBackBuffer` (line 135), same rationale as every other file in
this batch.

### Robustness
N/A beyond what's covered — positive-path test; the "skip the bracketing Green elements" design is itself
a good negative-adjacent check (it would fail loudly if the slicing arithmetic accidentally read the wrong
source indices, e.g. off-by-`startIndex` or off-by-one).

### Testing
This file is itself a test. See Missing or Weak Tests below.

### Cross-file consistency
Correctly and accurately cross-references both the existing backend-agnostic `easygl_texture2d_partial_rect_test.cpp`
(Task 170, confirmed to exist in the repo at `examples/easygl_texture2d_partial_rect_test.cpp`) as the
already-proven CPU-side arithmetic precedent, and the sibling `sdlrenderer_texture2d_partial_rect_test.cpp`
(Task 679) as sharing an identical "merge into full buffer, always re-upload whole buffer" methodology —
this file mirrors that one's structure closely but targets a different `SetData` sub-feature (array
slicing vs. rectangle sub-region), a sensible non-redundant split.

## Detailed Findings

No HIGH/MEDIUM/LOW correctness findings — this file was independently re-derived and traces cleanly end to
end with no discrepancy found between its comments, its assertions, and the actual production code.

## Cross-File Observations

- Together with `sdlrenderer_texture2d_partial_rect_test.cpp`, this file completes coverage of both
  `SetData`'s sub-region-selection mechanisms (`Rectangle` vs. `startIndex`/`elementCount`) against the real
  SDL_Renderer re-upload path — good complementary, non-overlapping coverage of the same underlying
  `UpdatePixels`-always-re-uploads-everything architecture.

## Missing or Weak Tests

None found — the 4-point check (2 untouched Red boundary texels + 2 newly-written Blue interior texels) is
a minimal but sufficient sample set for this scenario's failure modes.

## Positive Findings

- The slicing-arithmetic claim in the header comment was independently re-derived by this audit (not
  merely trusted) and matches the actual `Texture2D.cpp` formula exactly.
- Correctly distinguishes the CPU-side-arithmetic-already-proven-elsewhere concern from this file's own
  genuinely backend-specific value-add (real re-upload correctness) — a well-reasoned, non-redundant test
  design, consistent with the same discipline seen in this file's sibling tests.

## Final Assessment

A clean, accurately-documented, well-targeted pixel test with no discrepancies found between its stated
claims and the actual production behavior.
