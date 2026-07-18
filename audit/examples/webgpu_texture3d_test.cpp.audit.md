# Audit: examples/webgpu_texture3d_test.cpp

## Metadata

- Source file: `examples/webgpu_texture3d_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-webgpu` shard — `Texture3D`/`WebGPUTexture3DBackend` end-to-end test
  (WEBGPU-57/112), CTest target `WebGPU_Texture3D`
  (`cna_webgpu_test(cna_test_webgpu_texture3d …)` /
  `cna_register_backend_test(NAME WebGPU_Texture3D …)`, `cmake/Tests/WebGpuTests.cmake:166-168`).
- XNA/FNA relevance: direct — `Microsoft::Xna::Framework::Graphics::Texture3D` (`SetData`/`GetData`
  10-arg overloads, `SurfaceFormat::Color`). Per the file's own header comment (lines 6-9),
  `Texture3D`'s XNA-layer `SetData`/`GetData` have **no** CPU-side pixel shadow (unlike `Texture2D`)
  — every call goes straight through to `ITexture3DBackend::SetData()`/`GetData()` — so this is a
  genuine end-to-end XNA-layer test, not a backend-internal-only one.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/Texture3D.cpp` (constructor lines
  58-72, `SetData`/`GetData` 10-arg overloads lines 118-192),
  `src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp`
  (`WebGPUTexture3DBackend::WebGPUTexture3DBackend()` lines 1256-1282, `SetData()` lines 1289-1315,
  `GetData()` lines 1321-1422, `WebGPUGraphicsBackend::CreateTexture3D()` lines 5874-5878).

## Purpose

Three-check test proving `WebGPUTexture3DBackend` (WEBGPU-57/112) — this backend's first
`Texture3D` (volume texture) support, previously `IGraphicsBackend::CreateTexture3D()`'s safe
nullptr-returning default, meaning every `Texture3D::SetData`/`GetData` call on this backend
silently did nothing before this task: (A) 4 depth slices each filled with their own distinct solid
colour via the 10-arg `SetData(level,left,top,right,bottom,front,back,...)` overload, then a
whole-volume `GetData()` reads back all 4 slices in the correct order; (B) a single 1x1x1 voxel at
a non-origin `(x=1,y=1,z=2)` matches exactly that slice's colour, not slice 0 or a shifted region;
(C) a mip-level round trip (level 1, distinct content from level 0).

## Executive Verdict

**Healthy** — the actual voxel-addressing math (`SetData`/`GetData`'s `level,left,top,right,bottom,
front,back` → backend `level,x,y,z,w,h,depth` translation) and the depth-dimension mip-halving
claim in Check C's own comment were independently traced against the real
`Texture3D.cpp`/`WebGPUTexture3DBackend` source and confirmed correct.

## Checklist Results

### API / XNA / FNA parity

`Texture3D::SetData(int level,int left,int top,int right,int bottom,int front,int back,const
Color* data,int startIndex,int elementCount)` and the matching `GetData` overload (lines 118-137,
169-192) are the real FNA/XNA 10-arg box-region signature; the test's own per-slice loop (lines
74-78) correctly maps `front=z, back=z+1` to select one depth slice at a time, matching FNA's own
`Texture3D.SetData<T>(level,left,top,right,bottom,front,back,data,startIndex,elementCount)`
semantics (region = `[left,right)×[top,bottom)×[front,back)`).

### Behavioral correctness

- **Check A** (lines 80-93): each `SetData(0,0,0,w,h,z,z+1,...)` call writes exactly slice `z`'s own
  colour (`sliceColors[z]`); traced through `Texture3D::SetData` (line 135, `SetDataPointerEXT`)
  into `WebGPUTexture3DBackend::SetData` (lines 1289-1314): `destination.origin =
  {x,y,z}={0,0,z}`, `extent={w,h,depth}={2,2,1}` — a genuinely per-slice
  `wgpuQueueWriteTexture` targeting `WGPUOrigin3D.z=z`, not a single flat write that would silently
  alias slices. The whole-volume `GetData(0,0,0,w,h,0,depth,...)` (line 83) maps to backend
  `GetData(level=0,x=0,y=0,z=0,w=2,h=2,depth=4,...)` (`Texture3D::GetData` line 188-190), and
  `WebGPUTexture3DBackend::GetData`'s inner triple loop (lines 1396-1418) indexes the mapped buffer
  at `mapped + sz*sliceBytes + sy*bytesPerRow + sx*4` for `sz∈[0,4)` — correctly reproducing the
  per-slice write order into `readback[z*w*h+i]`, matching the test's own indexing (line 88).
- **Check B** (lines 97-102): `vol.GetData(0,1,1,2,2,2,3,&voxel,0,1)` → `Texture3D::GetData`
  computes `left=1,top=1,right=2,bottom=2,front=2,back=3` → backend call
  `GetData(level=0,x=1,y=1,z=2,w=1,h=1,depth=1,...)`. Re-traced `WebGPUTexture3DBackend::GetData`'s
  loop for this exact single-voxel case: `slice=0→sz=2`, `row=0→sy=1`, `col=0→sx=1`, all within
  `levelW=levelH=2, levelDepth=4` bounds, reading `mapped[2*sliceBytes + 1*bytesPerRow + 1*4 ...]` —
  exactly slice 2's uniformly-blue data. `voxel == sliceColors[2]` is the correct expectation.
- **Check C** (lines 105-117): the file's own comment states level 1 of a 2x2x4 volume is "1x1x2,
  since w/h halve, depth halves too per wgpu-native's standard 3D mip rules." Independently
  confirmed against `WebGPUTexture3DBackend::MipDim(base,level) = std::max(1,base>>level)` (line
  193-196, shared helper) — `MipDim(depth_=4, level=1) = 4>>1 = 2`, exactly matching the test's own
  `SetData(1,0,0,1,1,0,2,...)` (`front=0,back=2` → `depth=2`) and `GetData(1,...,0,2,...)` calls.
  This is also consistent with `WebGPUTexture3DBackend`'s own mip-level *count* convention (lines
  1263-1268: `while (w>1||h>1) {...}` — depth does NOT participate in the level *count*, mirroring
  `Texture3D.cpp`'s `CalculateMipLevels(width,height)`, lines 24-29 — but each level's *physical*
  depth extent is still independently halved by the WebGPU/wgpu-native runtime per standard 3D-mip
  rules), giving a 2x2x4 volume exactly `mipLevels_=2` (levels 0 and 1), matching the test's use of
  `level=1` as the last valid level.

### Logic

`MipDim`'s reuse across `Texture2D`/`TextureCube`/`Texture3D` backends (confirmed via grep: same
helper function body, single definition at line 193, used at lines 698-699, 898, 1330-1332) is
correctly depth-aware here specifically because `WebGPUTexture3DBackend::GetData()` (lines 1330-
1332) is the one call site that actually passes `depth_` as the `base` argument — the other two
backends have no depth dimension to pass.

### Memory/resource lifetime

Single-frame `Draw()` gate (`frame_++<1`, line 60) with no manual GPU-resource teardown, consistent
with every other file in this shard; `Texture3D vol(...)` is a local stack object whose destructor
(`Dispose(bool)`, `Texture3D.cpp` line 74-78) resets `backend_` before the frame ends.

### C++ correctness

`Color` equality (`readback[...] == sliceColors[z]`, line 88; `voxel == sliceColors[2]`, line 100)
relies on `Color::operator==`, not a raw byte-compare — appropriate here since both sides are real
`Microsoft::Xna::Framework::Color` values reconstructed via `Texture3D::GetData`'s own
`rgbaToColors()` (line 150-155), not raw backend bytes.

### Robustness

Not exercised — see Missing or Weak Tests. `Texture3D::SetData`/`GetData`'s own XNA-layer
validation (`elementCount <= 0`, `startIndex < 0`, `level < 0`, `left>=right`/`top>=bottom`/
`front>=back`, `elementCount` vs. voxel count — lines 121-132, 172-183) is real, present, and
correctly guards every call this test makes, but no check in this file exercises any of those
throwing paths.

### Testing

Three checks, each isolating a genuinely distinct axis (per-slice/depth addressing, non-origin
voxel offset, mip level selection) — proportionate to what WEBGPU-57/112 added, and each backed by
an independent re-derivation in this audit rather than mere internal self-consistency.

## Detailed Findings

No HIGH/CRITICAL findings. No MEDIUM/LOW correctness findings against this file's own three checks
— every claim (including the depth-mip-halving assertion in Check C's own comment, which could
easily have been an unverified guess) was independently confirmed against the real
`MipDim()`/mip-level-count logic.

## Cross-File Observations

- `WebGPUTexture3DBackend::SetData()` (lines 1289-1300) validates `level`/`data`/`w`/`h`/`depth`/
  `dataLength` and throws on violation, but performs **no bounds check that `x+w`/`y+h`/`z+depth`
  stay within the level's actual dimensions** before issuing `wgpuQueueWriteTexture` (unlike
  `TextureCube::SetData`'s XNA-layer own `x+w > levelSize` check, `TextureCube.cpp` line 161) — a
  `Texture3D::SetData` call with a region straddling or exceeding the level's real extent would
  pass CNA's own `Texture3D.cpp`-level checks (which only validate `elementCount` against the
  requested box's *volume*, not the box's position against `width_`/`height_`/`depth_`, lines 118-
  132) and reach the backend un-clamped. This file's own three checks never construct such a case
  (Check A/B/C all stay strictly within bounds), so this is a latent gap rather than something this
  file gets wrong — flagged here as a cross-file observation for whoever eventually audits
  `Texture3D.cpp` itself (out of this batch's scope), since the missing validation lives there, not
  in this test.
- Shares the same staged-`MAP_READ`-buffer/aligned-row/async-poll `GetData()` technique as
  `WebGPUTextureBackend::GetData()` and `WebGPUTextureCubeBackend::GetData()` (both audited
  alongside this file in the same batch) extended to a third (depth) dimension — consistent,
  deliberately-reused implementation pattern across all three texture-kind backends in this file.

## Missing or Weak Tests

- **No out-of-bounds / invalid-parameter coverage.** None of the three checks exercises
  `Texture3D::SetData`/`GetData`'s own exception paths (`elementCount<=0`, `level<0`,
  `left>=right`/`top>=bottom`/`front>=back`, insufficient `elementCount` for the requested region) —
  all real, present validation in `Texture3D.cpp` that this file never drives. Severity: LOW (this
  validation is XNA-layer, shared and generic, and more naturally belongs to a `Texture3D`-focused
  test file rather than this backend-capability test, but its complete absence anywhere in the
  `examples-tests-webgpu` shard is worth noting per the audit's "recurring testing gaps" theme).
- No test constructs a region whose position (not just its size) exceeds `width_`/`height_`/
  `depth_` — the exact `SetData()` gap noted in Cross-File Observations above; a test doing so would
  have surfaced whether `WebGPUTexture3DBackend::SetData()`'s missing bounds check is actually
  reachable and what it does (most likely an out-of-range GPU write or a validation-layer error
  from `wgpu-native`, neither of which is currently observed by any test).
- Depth-only mip-halving (the specific claim Check C's comment makes) is confirmed only for one
  concrete case (`depth=4→2` at level 1); a texture with an odd depth (e.g. 5) is never tested to
  confirm `MipDim`'s `max(1, base>>level)` rounding-down behavior at a depth boundary.

## Positive Findings

- Check A's per-slice distinct-colour design is the right technique to catch a hypothetical
  "z/depth silently aliases into a single 2D copy" bug — a 4-colour, order-checked readback is
  strictly stronger than a single-colour smoke test.
- Check C's own header comment makes a specific, falsifiable, non-obvious technical claim (depth
  halves too, mirroring standard 3D mip rules, unlike the level-*count* convention which is
  width/height-only) rather than a vague "mip works" assertion — and this audit's independent
  tracing confirmed the claim is accurate, not just plausible-sounding.
- Reuses the exact SetData/GetData box-region convention (`level,left,top,right,bottom,front,back`)
  FNA itself uses, giving high confidence the XNA-facing API shape is correct without needing a
  separate parity check file.

## Final Assessment

A precise, three-axis test for `Texture3D`/`WebGPUTexture3DBackend` (WEBGPU-57/112) whose depth-
dimension addressing and mip-halving claims were independently confirmed against the real
production code rather than taken on faith. The most actionable follow-up is not to this file but
to `Texture3D.cpp`/`WebGPUTexture3DBackend::SetData()`, which appears to have no bounds check
against the region's *position* relative to the texture's actual dimensions (only against the
region's volume vs. `elementCount`) — worth a dedicated out-of-bounds test once `Texture3D.cpp`
itself is in scope.
