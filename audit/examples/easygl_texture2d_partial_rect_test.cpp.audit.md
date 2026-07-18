# Audit: examples/easygl_texture2d_partial_rect_test.cpp

## Metadata

- Source file: `examples/easygl_texture2d_partial_rect_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — CPU-side round-trip test (no GPU readback, no rendering)
- File type: C++ executable test (`Game` subclass, no gtest), 190 lines
- XNA/FNA relevance: exercises `Texture2D::SetData(int, Rectangle*, Color*, int, int)` and
  `Texture2D::GetData(int, Rectangle*, Color*, int, int)` with a sub-rectangle and non-zero `startIndex`/
  `elementCount` — real XNA 4.0 API surface
- FNA reference: partial-rectangle/`startIndex` semantics for `Texture2D.SetData<T>`/`GetData<T>` are standard
  XNA4 API behavior; CNA's CPU-shadow-buffer implementation is an internal detail, not itself an FNA-source diff
  target
- Production code under test: `src/Microsoft/Xna/Framework/Graphics/Texture2D.cpp:245-316` (`SetData`),
  `:378-455` (`GetData`)
- Naming note: like its sibling in this batch, this file is backend-agnostic — confirmed registered under EasyGL,
  Vulkan, **and** Bgfx (`cmake/Tests/EasyGLTests.cmake:828-832`, `cmake/Tests/VulkanTests.cmake:871-875`,
  `cmake/Tests/BgfxTests.cmake:42-45`)

## Purpose

Tasks 169-170: three sub-tests in one file. Task 169 writes a solid-Red 4×4 texture then overwrites a 2×2 Blue
sub-rectangle at `(1,1)` and verifies the resulting 4×4 read-back has Blue only at the 4 expected interior pixels.
Task 170A writes a 6-element source array with a non-zero `startIndex`/`elementCount` into a sub-rectangle,
verifying only the intended slice of the source array is consumed. Task 170B mirrors this for `GetData`'s own
`startIndex`, verifying the output array's untouched slots retain a sentinel value.

## Executive Verdict

**Healthy.** Independently re-derived the exact pixel/array-index arithmetic for all three sub-tests directly
against `Texture2D::SetData`/`GetData`'s actual index computation and confirmed every expected value in the test
matches the production formula precisely, including the specific source/destination array offsets in the
`startIndex` sub-tests, which are easy to get subtly wrong.

## Checklist Results

### API / XNA / FNA parity
`Texture2D(dev, 4, 4)` / `Texture2D(dev, 4, 1)` (2-arg constructor, defaults to `SurfaceFormat::Color`,
single level) matches `Texture2D.hpp:54`. `SetData(0, nullptr, data, 0, count)`,
`SetData(0, &rect, data, startIndex, count)`, and the matching `GetData` overloads match
`Texture2D.hpp`'s declared signatures exactly.

### Behavioral correctness

**Task 169 (partial rectangle, lines 80-106):** Traced `SetData`'s partial-write loop (`Texture2D.cpp:283-294`):
`dst = ((y+row)*levelW + (x+col))*4`. For `subRect=(1,1,2,2)` on a 4-wide level: `row=0,col=0 → dst
index=(1*4+1)*4=... ` re-expressed as flat pixel index `(y+row)*4+(x+col)`: `(1,1)→5`, `(1,2)→6`, `(2,1)→9`,
`(2,2)→10` — matches the test's own expected `blueIdx = {5,6,9,10}` (line 94) exactly, and matches the ASCII-art
layout in the header comment (lines 8-13).

**Task 170A (`SetData` startIndex, lines 111-136):** Traced the source-index computation
(`Texture2D.cpp:287`, `src = startIndex + row*w + col`) for `rect2x1=(1,0,2,1)`, `startIndex=2`: `row=0,col=0 →
src=2` → `src6[2]=Blue`; `row=0,col=1 → src=3` → `src6[3]=Blue`. Destination indices
(`Texture2D.cpp:288`, `dst=((y+row)*levelW+(x+col))*4`, `levelW=4`): `col=0 → pixel(1,0)`; `col=1 →
pixel(2,0)`. Final 4-pixel array: `[Red, Blue, Blue, Red]` — matches the test's `want4` (line 129) exactly, and
correctly proves the `Green` entries at `src6[0,1,4,5]` are skipped (a bug that ignored `startIndex` and read
from `src6[0]`/`src6[1]` instead would produce `Green` at pixels 1-2, not `Blue`, and this test would catch it).

**Task 170B (`GetData` startIndex, lines 141-167):** Traced `GetData`'s destination-index computation
(`Texture2D.cpp:443-452`, general path since `rect != nullptr`): `dst = startIndex + row*w + col`. For
`rect2x1=(1,0,2,1)`, `startIndex=1`: `col=0 → dst=1` (reads pixel `(1,0)`=Blue); `col=1 → dst=2` (reads pixel
`(2,0)`=Blue). Pre-filled sentinel `Green` at `out[0]`/`out[3]` (line 153) correctly remains untouched since the
loop only ever writes `dst∈{1,2}` — matches the test's expected `wantOut = {Green, Blue, Blue, Green}` (line 160)
exactly. This sentinel-based design is a genuinely effective way to prove `startIndex` is honored on the
**output** side specifically (as opposed to Task 170A, which proves it on the **input/source** side) — a bug that
ignored `GetData`'s `startIndex` and wrote to `out[0]`/`out[1]` instead would leave `out[0]` as `Blue` (not the
`Green` sentinel) and the test would correctly report `[FAIL]`.

### Logic
All three sub-tests are correctly isolated in their own scope block (`{ ... }`, lines 80-106, 111-136, 141-167)
with fresh local `Texture2D` instances — Task 170B explicitly re-creates and re-populates its own texture instance
"in same state" (line 143) rather than relying on Task 170A's texture still being in scope, avoiding any
cross-sub-test ordering dependency.

### Memory/resource lifetime
All `Texture2D tex` instances are locals scoped to their respective `{ }` blocks, safely destructed at each
block's end; `gdm_` (member) correctly outlives `Initialize()`.

### Testing
Uses the same accumulate-all-failures `check()` pattern as `easygl_texture2d_mip_test.cpp` (audited in this same
batch) — every one of the 16 (Task 169) + 4 (Task 170A) + 4 (Task 170B) = 24 individual pixel/slot checks is
independently reported, not short-circuited on first failure.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — Task 170A and 170B both use a rectangle aligned to the buffer's start row; no test in this file exercises a partial rect combined with a non-trivial row stride (i.e. a sub-rectangle narrower than the full level width on a texture taller than 1 row)

- Severity: LOW
- Confidence: MEDIUM
- Category: test-coverage
- Location/symbol: `PartialRectTest::Initialize`, Task 170A/170B blocks (lines 111-167); the `SetData`/`GetData`
  row-stride computation (`Texture2D.cpp:288`, `:447`: `(y+row)*levelW + (x+col)`)
- Evidence: Task 169 does use a genuine 2D sub-rectangle on a 4×4 (multi-row) texture, correctly exercising the
  `(y+row)*levelW` term with `row` taking values 0 and 1 — so the row-stride term itself **is** exercised
  elsewhere in this same file. Tasks 170A/170B specifically use a `4×1` (single-row) texture, meaning their
  `(y+row)*levelW` term is always `0*4=0` regardless of `levelW`'s actual value — the `startIndex`-specific
  arithmetic these two sub-tests target is therefore proven only for the degenerate single-row case.
- Why it matters: this is a narrow, low-risk gap since Task 169 already independently proves the row-stride term
  works correctly for a 2D sub-rectangle (just without a non-zero `startIndex`); the untested combination is
  specifically "`startIndex` + multi-row sub-rectangle" together, which shares the same underlying formula
  (`dst/src = startIndex + row*w + col`) already proven correct in each dimension separately. Confidence is
  MEDIUM rather than HIGH because no code-level evidence suggests these two terms interact incorrectly — this is
  a coverage-completeness note, not a suspected defect.
- FNA/XNA comparison: N/A.
- Suggested action (not implemented by this audit): consider a 4th sub-test combining a multi-row sub-rectangle
  with a non-zero `startIndex`/`elementCount`, to directly prove the combination rather than relying on two
  separate single-dimension proofs.

## Cross-File Observations

- Shares the "pure CPU shadow buffer, no framebuffer readback" scope, the accumulate-all-failures `check()`
  pattern, and the multi-backend (EasyGL/Vulkan/Bgfx) CMake registration with `easygl_texture2d_mip_test.cpp`
  (audited in this same batch) — the two files are clearly designed as a matched pair covering complementary
  `SetData`/`GetData` dimensions (mip-level targeting here; sub-rectangle/`startIndex` in this file).

## Missing or Weak Tests

- See F1 — no single sub-test combines a non-zero `startIndex` with a genuinely multi-row sub-rectangle.
- As with `easygl_texture2d_mip_test.cpp`, no sub-test here combines a non-zero mip level with a partial
  rectangle — the "partial rect on a non-zero mip level" combination remains untested across both files in this
  batch.

## Positive Findings

- Every expected pixel/array value in all three sub-tests was independently re-derived from the actual
  `SetData`/`GetData` index arithmetic in `Texture2D.cpp` and found to match exactly, including the specific
  source-vs-destination offset distinction between Task 170A (`startIndex` on the input array) and Task 170B
  (`startIndex` on the output array) — a genuinely precise, non-trivial pair of assertions, not a coincidental
  pass.
- The sentinel-value technique in Task 170B (pre-filling `out` with a color that appears nowhere else in the
  expected result) is a good, deliberate technique for proving "untouched slots stay untouched," not just "touched
  slots get the right value."
- Each sub-test is correctly isolated in its own scope with a fresh `Texture2D`, avoiding inter-sub-test ordering
  dependencies.

## Final Assessment

A precise, well-verified test of `Texture2D::SetData`/`GetData`'s partial-rectangle and `startIndex`/
`elementCount` semantics. Every expected value was independently traced against the production index-arithmetic
and confirmed correct, including the input-side-vs-output-side `startIndex` distinction between its two
sub-tests. The only real gap is a narrow, low-risk combination (non-zero `startIndex` together with a
multi-row sub-rectangle) that is not directly proven, though each contributing dimension is proven separately
elsewhere in this same file.
