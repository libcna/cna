# Audit: examples/easygl_texture3d_partial_box_readback_test.cpp

## Metadata

- Source file: `examples/easygl_texture3d_partial_box_readback_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend `Texture3D::GetData` sub-region readback test
- File type: C++ example/integration-test executable (`Texture3DPartialBoxReadbackTest : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::Texture3D::GetData` (10-arg box overload,
  `Texture3D.cpp:169-192`), `CNA::Internal::Backends::EasyGL::EasyGLTexture3DBackend::GetData`
  (`EasyGLGraphicsBackend.cpp:176-203`)
- XNA/FNA relevance: `Texture3D.GetData<T>(level,left,top,right,bottom,front,back,data,startIndex,
  elementCount)`, judged against `FNA/src/Graphics/Texture3D.cs`'s identical overload and its box-validation
  rules.
- Main related tests: this file (Task 274); its sibling `easygl_texture3d_partial_box_test.cpp` (Task 273)
  covers the `SetData`-side placement of the same class of box.

## Purpose

Verifies `Texture3D::GetData`'s box-region readback with three sub-tests, all against a 4×3×5 volume filled
with a per-voxel-unique color formula (`R=20+40x, G=20+60y, B=20+40z`) rather than a binary color split, so
that an axis-swap or off-by-one bug in the read path is detectable even for boxes that don't cross a
symmetric color boundary. The header explicitly motivates this design against the weaker coverage of a
hypothetical binary-split test. Placement matches the shard convention.

## Executive Verdict

**Healthy** — the per-voxel formula is genuinely collision-free within this test's dimensions, all three
sub-tests exercise real, distinct box-shape/position classes (asymmetric off-origin, non-zero `startIndex`
with sentinel guards on both sides, and the exact far-corner boundary), and cross-checking against
`Texture3D.cpp`'s validation and FNA's `Texture3D.cs` confirms the box semantics under test (exclusive
right/bottom/back) are exactly the real contract, not a test-author assumption.

## Checklist Results

### API / XNA / FNA parity
`GetData(level, left, top, right, bottom, front, back, data, startIndex, elementCount)` (used at lines 96-97,
120-121, 146) is the real 10-arg XNA overload. Verified against `FNA/src/Graphics/Texture3D.cs::GetData<T>`
(10-arg overload, lines 229-277): FNA's own box validation (`(left<0||left>=right)||(top<0||top>=bottom)||
(front<0||front>=back)`) matches `Texture3D.cpp`'s check (`Texture3D.cpp:180`) — confirming the box's
right/bottom/back are exclusive bounds in both, exactly as this test's sub-test C exploits (a box exactly
touching `right==width`, `bottom==height`, `back==depth`).

### Behavioral correctness
Verified the per-voxel formula (`voxelColour`, lines 51-54) cannot collide within this test's actual ranges:
`R=20+40x` for `x∈[0,3]` → values `{20,60,100,140}`; `G=20+60y` for `y∈[0,2]` → `{20,80,140}`; `B=20+40z` for
`z∈[0,4]` → `{20,60,100,140,180}`. All values stay within `[0,255]` (no `Color` byte-clamping collisions), and
each axis maps injectively to its own channel — so any box read that samples the wrong voxel (transposed
axis, off-by-one offset, wrong stride) produces a detectable mismatch, not a coincidental match, which is
exactly the stated design goal of using a per-voxel-unique formula instead of a binary split.

**Sub-test A** (lines 90-109): box `left=1,top=1,front=1`, size `2×2×3` (`right=3,bottom=3,back=4`) inside the
`4×3×5` volume — every one of the 12 read-back voxels is checked against `voxelColour(left+bx, top+by,
front+bz)`, correctly reconstructing the source coordinate from the box-local index.

**Sub-test B** (lines 114-135): reads a `2×1×2` box (`elementCount=4`) into `out[1..4]` of a 6-element buffer
pre-filled with `kSentinel(1,2,3,255)`, then explicitly asserts `out[0]` and `out[5]` are untouched. `kSentinel`
values (1,2,3) are below the formula's minimum output (20), so no accidental collision with real voxel data is
possible — a correct choice of sentinel. This is a genuine boundary-touch test: it would catch a `GetData`
implementation that ignored `startIndex` and wrote starting at `data[0]` (which would corrupt `out[0]` and
leave `out[4..5]` unwritten/sentinel where data was expected), not just "does the function accept a
`startIndex` parameter."

**Sub-test C** (lines 140-158): box `(2,1,3)` to `(4,3,5)` — `right==kW(4)`, `bottom==kH(3)`, `back==kD(5)`,
i.e., touching the far corner exactly at the volume's real extent. Confirmed via `EasyGLTexture3DBackend`
that `glReadPixels`/FBO-layer-attach based readback (`EasyGLGraphicsBackend.cpp:176-203`) has no dependency on
whether the box's far edge equals the actual texture dimension (it just reads `w×h` pixels per requested
slice starting at `x,y`), so this sub-test genuinely exercises the exclusive-bound edge case rather than
relying on an accidental pass.

### Logic
All three sub-tests reuse the same `tex` instance created once (line 77) and filled once via a full-volume
`SetData` (line 85) before any of the three reads — correct, since these are read-path (not write-path)
sub-tests and don't need independent volumes per sub-test the way the sibling `_partial_box_test.cpp`
(write-path) does.

### Memory/resource lifetime
All buffers are `std::vector<Color>` with explicit, correctly-computed sizes (`kW*kH*kD`, `bw*bh*bd`, `6`); no
dangling-pointer or lifetime concern.

### C++ correctness
`colourEq()` (lines 43-48) again compares only R/G/B, silently ignoring Alpha — same systemic gap as every
sibling `easygl_texture3d_*` file in this batch (see the mip test's F1; not re-scored here to avoid
duplicate-counting the same shared gap across files, but noted for completeness).

### Performance
N/A — single-frame, `Initialize()`-only test.

### Robustness
No invalid-input path exercised; deliberate, in this case correctly so, since this is a positive-path
box-readback test.

### Testing
This file is itself a test; see Missing or Weak Tests.

## Detailed Findings

No HIGH/CRITICAL findings. No new findings beyond the systemic Alpha-blind-spot already recorded once for
this shard (see `easygl_texture3d_mip_test.cpp.audit.md`, Finding F1) — repeated here as a Cross-File
Observation rather than a duplicate Detailed Finding.

## Cross-File Observations

- Shares the exact `colourEq()`/Alpha-blind-spot gap with every other `easygl_texture3d_*` file in this batch
  (see `easygl_texture3d_mip_test.cpp.audit.md` F1).
- This file (Task 274, `GetData`-focused) and `easygl_texture3d_partial_box_test.cpp` (Task 273,
  `SetData`-focused) are a deliberately paired split — same box-shape design (asymmetric off-origin,
  single/small box, far-corner) applied to opposite data directions. This is legitimate, non-redundant
  coverage (a `SetData` bug and a `GetData` bug are independent failure classes even though both round-trip
  through the same `Texture3D` object), not duplicated test content, despite superficially similar structure.
- Independently confirmed FNA's `GetData<T>` box-validation matches `Texture3D.cpp`'s check nearly verbatim
  (see Behavioral correctness above) — this test's sub-test C specifically exercises the exclusive-bound
  contract both implementations share.

## Missing or Weak Tests

- Alpha channel is never verified (shared systemic gap, see above).
- No sub-test exercises a box at a non-zero mip level (`level>0`) combined with a partial (non-full) box —
  this file and its `SetData` sibling both use `level=0` throughout; the mip-specific box behavior is only
  ever tested with full-level boxes (`easygl_texture3d_mip_test.cpp`), never a partial box at `level>0`. A
  combined partial-box-at-mip-level test would close a real, currently-untested interaction between two
  features each individually well covered.
- No negative-path test (`GetData` with a box exceeding the volume's real dimensions, which per the
  Cross-File Observations above passes validation in both FNA and CNA today) — consistent with the whole
  shard's positive-path-only scope, not unique to this file.

## Positive Findings

- The per-voxel-unique color formula is genuinely, arithmetically collision-free within this test's actual
  dimensions (independently verified above) — a real, deliberate improvement over a binary-split design, not
  just an assertion in the header comment.
- Sub-test B's sentinel-guard-on-both-sides pattern (`out[0]` and `out[5]` both explicitly checked) is a
  strong test for `startIndex` handling — it would catch both an "ignores `startIndex`" bug and an
  "overwrites past the requested `elementCount`" bug in a single sub-test.
- Sub-test C's use of the box's real far edge (rather than an arbitrarily smaller box away from the boundary)
  is a genuine edge-case test, not a coincidental in-bounds case.

## Final Assessment

A well-designed, evidence-based `GetData` sub-region test whose per-voxel color formula, sentinel-guard
`startIndex` check, and far-corner boundary case were all independently verified to be collision-free and to
exercise real, distinct failure classes against the actual `Texture3D`/`EasyGLTexture3DBackend` implementation
and FNA's reference validation rules. The only gaps are the shared, low-severity Alpha blind spot and the
untested partial-box-at-mip-level interaction.
