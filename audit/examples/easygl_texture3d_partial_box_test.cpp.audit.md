# Audit: examples/easygl_texture3d_partial_box_test.cpp

## Metadata

- Source file: `examples/easygl_texture3d_partial_box_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend `Texture3D::SetData` sub-region placement test
- File type: C++ example/integration-test executable (`Texture3DPartialBoxTest : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::Texture3D::SetData` (10-arg box overload,
  `Texture3D.cpp:118-137`), `CNA::Internal::Backends::EasyGL::EasyGLTexture3DBackend::SetData`
  (`EasyGLGraphicsBackend.cpp:110-120`)
- XNA/FNA relevance: `Texture3D.SetData<T>(level,left,top,right,bottom,front,back,data,startIndex,
  elementCount)`, judged against `FNA/src/Graphics/Texture3D.cs`'s identical overload.
- Main related tests: this file (Task 273); its sibling `easygl_texture3d_partial_box_readback_test.cpp`
  (Task 274) covers the equivalent `GetData`-side sub-region behavior.

## Purpose

Verifies `Texture3D::SetData`'s box-region placement with three independent sub-tests, each on its own
freshly-created volume filled entirely Red, with a distinctly-colored box written into a specific sub-region
and the *entire* volume then read back and checked voxel-by-voxel (in vs. out of the written box). The header
explicitly motivates this design against the weaker coverage of the pre-existing `easygl_texture3d_slices_test.cpp`
(Task 173), which only varies boxes along Z. Placement matches the shard convention.

## Executive Verdict

**Healthy** — all three sub-tests use genuinely distinct, asymmetric box shapes/positions (deliberately
different width/height/depth per sub-test, off-origin placement, single-voxel precision, and an exact
far-corner touch), each is verified by checking *every* voxel in the volume (not just the box), and the box
semantics under test were independently confirmed to match both `Texture3D.cpp`'s implementation and FNA's
reference contract.

## Checklist Results

### API / XNA / FNA parity
`SetData(level, left, top, right, bottom, front, back, data, startIndex, elementCount)` (lines 89, 120, 152)
is the real 10-arg XNA overload; `GetData` used for full-volume verification (lines 92, 123, 155) likewise.
Confirmed against `FNA/src/Graphics/Texture3D.cs::SetData<T>` (10-arg overload, lines 105-138) that the
`right-left`/`bottom-top`/`back-front` exclusive-bound box semantics this test relies on for its "in-box"
predicate (e.g. line 99: `(x>=1 && x<3) && (y>=2 && y<5) && (z>=1 && z<3)`) are the actual contract, not a
test-author assumption — FNA computes the same `right-left`/etc. deltas when forwarding to
`FNA3D_SetTextureData3D`.

### Behavioral correctness
**Sub-test A** (lines 78-105): a 4×5×3 volume (deliberately distinct W/H/D, so a transposed axis would
produce a detectably wrong-shaped or wrong-positioned box) filled Red, with a 2×3×2 Blue box written at
`left=1,top=2,front=1` (`right=3,bottom=5,back=3`). All 60 voxels of the full volume are read back and each
is checked against the exact in-box predicate — this would catch a box write that used the wrong offset, the
wrong extent, or a swapped axis, since the resulting Blue region's shape/position would differ from the
predicate in a way that's visible on essentially any wrong voxel, not only ones inside the intended box.

**Sub-test B** (lines 110-136): single-voxel (`1×1×1`) Green box at `(2,1,0)` inside a 3×3×3 Red volume,
verified by checking all 27 voxels are Red except the single target, which must be Green. This is the
tightest possible precision test for `SetData`'s box-position arithmetic — any off-by-one in any axis flips
which single voxel is Green, and the full 27-voxel scan will catch it regardless of which axis is wrong.

**Sub-test C** (lines 141-168): box from `(2,2,2)` to `(4,4,4)` in a 4×4×4 volume — `right==bottom==back==4`,
exactly the volume's real extent on all three axes simultaneously (a stronger version of the readback
sibling's sub-test C, which only touches the far corner on one test, here touching it on all three axes at
once). Confirmed this exercises the exclusive-bound contract at its most extreme case for the `SetData` path.

### Logic
Each sub-test creates its own fresh `Texture3D` (lines 81, 113, 144) rather than reusing one instance across
sub-tests — correct and deliberate, since (unlike the `GetData`-focused sibling, which only needs one shared
volume for multiple *reads*) these sub-tests each need an independently-clean, fully-Red starting volume
before writing their own box, and reusing one instance across sub-tests with different dimensions would not
even be possible (each sub-test uses different W/H/D).

### Memory/resource lifetime
All buffers (`allRed`, `blueBox`, `green1`, `yellowBox`, `rb`) are correctly sized `std::vector<Color>`/local
arrays; no lifetime or dangling-pointer risk.

### C++ correctness
`colourEq()` (lines 48-53) again compares only R/G/B, ignoring Alpha — the same systemic gap shared across
this shard's `Texture3D` test files (see `easygl_texture3d_mip_test.cpp.audit.md` F1); not re-scored here.

### Performance
N/A — single-frame, `Initialize()`-only test; three sub-tests each doing a bounded, small (≤125-voxel)
full-volume verification scan, negligible cost.

### Robustness
No invalid-input path exercised; correct scope for a positive-path box-placement test.

### Testing
This file is itself a test; see Missing or Weak Tests.

## Detailed Findings

No HIGH/CRITICAL findings.

## Cross-File Observations

- Shares the `colourEq()`/Alpha-blind-spot gap with every other `easygl_texture3d_*` file in this batch (see
  `easygl_texture3d_mip_test.cpp.audit.md` F1) — not re-scored per-file.
- This file (Task 273, `SetData` placement) and `easygl_texture3d_partial_box_readback_test.cpp` (Task 274,
  `GetData` placement) form a deliberate write/read pair covering the same class of box shapes from opposite
  directions — legitimate non-redundant coverage, consistent with the sibling report's observation.
- Distinct from `easygl_texture3d_slices_test.cpp` (Task 173) as explicitly documented in this file's own
  header: the older test only varies boxes along Z with full-width/height slices, which this file's header
  correctly identifies as unable to catch an X/Y offset or extent bug — verified this claim is accurate by
  inspecting `easygl_texture3d_slices_test.cpp`, whose `SetData` calls (`tex.SetData(0, 0, 0, kW, kH, se.z,
  se.z + 1, ...)`) indeed always use `left=top=0, right=kW, bottom=kH` unconditionally, confirming this file's
  stated coverage gap in the predecessor test is real and that this file genuinely closes it rather than
  duplicating existing coverage.

## Missing or Weak Tests

- Alpha channel is never verified (shared systemic gap).
- No sub-test writes two *overlapping* boxes in sequence to the same volume (verifying the second write
  correctly overwrites only its own region without leaving stale texels from the first write at the
  overlap boundary) — all three sub-tests here write exactly one box onto a uniformly-Red base, so a
  partial-overwrite/box-boundary-precision bug on a second write is untested by this file (though sub-test C's
  exact-far-corner box is a reasonable partial substitute for boundary precision in the single-write case).
- No sub-test exercises a box write at a non-zero mip level combined with a partial (non-full) box — same gap
  as the `GetData` sibling.

## Positive Findings

- Each sub-test's full-volume (not just in-box) verification is the strongest possible check for a placement
  bug — a box that's shifted, resized, or wrongly-clamped would produce a detectable mismatch on some voxel
  regardless of which axis or direction the bug affects, since literally every voxel in the volume is checked
  against the correct in/out-of-box predicate.
- Sub-test B's single-voxel precision case is a genuinely strong, minimal test for off-by-one box-position
  arithmetic.
- The file's own header correctly and verifiably identifies a real coverage gap in the predecessor test
  (Task 173) that it closes, rather than asserting an unverified claim.

## Final Assessment

A rigorous, evidence-based `SetData` box-placement test whose three sub-tests use genuinely distinct box
shapes/positions and full-volume verification, independently confirmed to exercise real, previously-uncovered
placement-bug classes (asymmetric extents, off-origin offsets, single-voxel precision, and simultaneous
far-corner touch on all three axes) against both the actual `Texture3D`/`EasyGLTexture3DBackend`
implementation and FNA's reference box contract.
