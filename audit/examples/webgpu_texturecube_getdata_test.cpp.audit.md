# Audit: examples/webgpu_texturecube_getdata_test.cpp

## Metadata

- Source file: `examples/webgpu_texturecube_getdata_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-webgpu` shard — `WebGPUTextureCubeBackend::GetData()` end-to-end test
  (WEBGPU-113), CTest target `WebGPU_TextureCube_GetData`
  (`cna_webgpu_test(cna_test_webgpu_texturecube_getdata …)` /
  `cna_register_backend_test(NAME WebGPU_TextureCube_GetData …)`,
  `cmake/Tests/WebGpuTests.cmake:160-162`).
- XNA/FNA relevance: direct — `Microsoft::Xna::Framework::Graphics::TextureCube` (`SetData`/
  `GetData`, `CubeMapFace` enum). Per the file's own header comment (lines 5-8), unlike
  `Texture2D::GetData()`, `TextureCube::GetData()` has **no** CPU-side pixel shadow of its own and
  always calls straight through to `ITextureCubeBackend::GetData()` — so this is a genuine
  end-to-end XNA-layer test, not a backend-only one (the opposite framing from
  `webgpu_texture2d_getdata_test.cpp`, audited in the same batch, which must deliberately bypass
  `Texture2D` because *that* class does have a CPU shadow).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/TextureCube.cpp` (constructor
  lines 58-70, `SetData`/`GetData` 6-arg overloads lines 144-212),
  `src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp`
  (`WebGPUTextureCubeBackend::WebGPUTextureCubeBackend()` lines 787-809, `SetData()` lines 846-883,
  `GetData()` lines 888-979, `WebGPUGraphicsBackend::CreateTextureCube()` lines 5858-5862),
  `include/Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp` (enum ordinals).

## Purpose

Three-check test proving `WebGPUTextureCubeBackend::GetData()` (WEBGPU-113) — closing this class's
former no-op `ITextureCubeBackend::GetData()` default (WEBGPU-56/74 only added `SetData()`,
sufficient for `EnvironmentMapEffect.EnvironmentMap` sampling but not for reading a `TextureCube`
back): (A) each of the 6 faces uploaded a distinct solid colour, `GetData()` on each face reads back
exactly that face's own colour — proves `origin.z = face` addressing is correct and faces do not
alias; (B) a 2x2 sub-rectangle at a non-origin offset `(1,1)` on `PositiveX` matches exactly the
source pixels at that offset; (C) a mip-level round trip (level 1 of `PositiveX`, distinct content
from level 0).

## Executive Verdict

**Healthy** — the face-to-array-layer mapping (`CubeMapFace` ordinal = `WGPUOrigin3D.z`), the
sub-rectangle offset math, and the mip-level dimension logic were all independently traced against
the real production code and confirmed correct, including the specific ordinal-alignment
coincidence that makes the test's `kFaces[f]`/`kFaceColors[f]` parallel-array indexing safe.

## Checklist Results

### API / XNA / FNA parity

`CubeMapFace` (`include/Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp`) declares
`PositiveX=0, NegativeX=1, PositiveY=2, NegativeY=3, PositiveZ=4, NegativeZ=5` — the real FNA/XNA
ordinal ordering. `TextureCube::SetData`/`GetData` (6-arg, `face,level,rect,data,startIndex,
elementCount`, `TextureCube.cpp` lines 144-212) is the genuine FNA signature; `TextureCube::GetData`
correctly derives `x,y,w,h` from an optional `Rectangle*` (defaulting to the whole level, lines 199-
200) exactly as this test's Check A (no rect, whole face) and Check B (`Rectangle rect(1,1,2,2)`)
both rely on.

### Behavioral correctness

- **Check A** (lines 96-107): for each of the 6 faces, `cube.SetData(kFaces[f], faceData.data(),
  ...)` (the 3-arg overload, delegating to the 6-arg one with `level=0, rect=nullptr`, `TextureCube.cpp`
  lines 125-135) then `cube.GetData(kFaces[f], readback.data(), ...)`. Traced into
  `WebGPUTextureCubeBackend::SetData` (line 846: `origin={0,0,face}`) and `GetData` (line 888:
  `origin={0,0,face}`) — since `kFaces[f]` and the array index `f` coincide numerically with
  `CubeMapFace`'s own ordinals (`PositiveX=0` stored at `kFaces[0]`, etc.), `static_cast<int>(face)`
  always equals `f`, so this test's parallel `kFaceColors[f]` indexing genuinely tracks the same
  face being written and read — not an accidental pass from two independently-wrong-but-matching
  off-by-ones. `wgpuCommandEncoderCopyTextureToBuffer`'s `source.origin.z = face` selects the
  correct array layer of the underlying `WGPUTextureDimension_2D` 6-layer texture (constructor
  comment, lines 782-786, confirms the array-layer-per-face design), so no two faces can alias.
- **Check B** (lines 109-131): uploads a genuinely per-pixel-distinct pattern (`Color(x*60,y*60,128,
  255)`, not a flat colour — the right technique to catch a shifted/garbled read) via the 6-arg
  `SetData(PositiveX, 0, nullptr, pattern.data(), 0, ...)`, then reads back `Rectangle(1,1,2,2)`.
  `WebGPUTextureCubeBackend::GetData`'s inner loop (lines 959-975) for `x=y=1,w=h=2`: `sx=1+col∈
  {1,2}, sy=1+row∈{1,2}`, both within the 4x4 face bounds, reading
  `mapped[sy*bytesPerRow+sx*4...]` — the test's own expected-index formula `pattern[(1+row)*
  kFaceSize+(1+col)]` (line 126) matches this exactly (the CPU-side `pattern` vector is tightly
  packed at `kFaceSize*4=16` bytes/row, distinct from the GPU readback's aligned `bytesPerRow` —
  same two-strides-both-handled-correctly situation independently confirmed in
  `webgpu_texture2d_getdata_test.cpp`'s audit).
- **Check C** (lines 133-148): level 1 (2x2, `MipDim(4,1)=2`) uploaded with content
  (`Color(10,200,30,255)`) distinct from level 0's pattern above, via `SetData(PositiveX, 1,
  nullptr, level1.data(), 0, 4)`. `TextureCube::SetData`'s own `levelSize=mipDim(size_,level)=2`
  (line 158) correctly sizes the implicit whole-level rectangle when `rect=nullptr`; `GetData(1,...)`
  reads it back at the same level. Confirmed `WebGPUTextureCubeBackend`'s own `mipLevels_`
  construction (lines 793-798: `size_=4` → `s=2,levels=2; s=1,levels=3` → `mipLevels_=3`) matches
  the test's own comment ("mipMap=true -> 4->2->1, 3 levels") — `level=1` is a valid, mid-chain
  level, not an edge case.

### Logic

`IsValidCubeMapFace()` (`TextureCube.cpp` lines 137-142) bounds `face∈[PositiveX,NegativeZ]`
before either `SetData`/`GetData` reaches the backend — this test only ever passes valid faces, so
that guard is present but unexercised here (see Missing or Weak Tests).

### Memory/resource lifetime

Single-frame `Draw()` gate (`frame_++<1`, line 81), no manual GPU-resource teardown, consistent with
the shard's convention; `TextureCube cube(...)` is a local stack object.

### C++ correctness

`Color::operator==` comparisons throughout (not raw byte-compares) — appropriate since both sides
are reconstructed `Color` values via `TextureCube::GetData`'s own `rgbaToColors()` (line 118-123),
not raw backend bytes. `(std::string(...) + kFaceNames[f] + "...").c_str()` (line 105-106)
constructs a temporary `std::string` whose `.c_str()` is passed directly into `check(bool,const
char*)` as a function argument — the temporary's lifetime extends to the end of the full
expression (the `check()` call itself), so the pointer remains valid for the duration `check()`
uses it (it only `printf`s it synchronously, does not retain it) — no dangling-pointer bug, but a
fragile pattern that would break if `check()` were ever changed to store the label past the call.

### Robustness

Not exercised — see Missing or Weak Tests. `TextureCube::SetData`/`GetData`'s own
`IsValidCubeMapFace()` guard and rectangle-bounds check (`x+w>levelSize||y+h>levelSize`, lines 161,
201) are real, present validation this file never drives into a throwing path.

### Testing

Three checks covering three genuinely distinct axes (per-face addressing across all 6 faces,
non-origin sub-rectangle offset, mip-level selection) — the per-face loop in Check A (rather than
spot-checking one or two faces) is a stronger check than most cube-map tests in other backends'
shards that only verify one or two faces.

## Detailed Findings

No HIGH/CRITICAL findings. No MEDIUM/LOW correctness findings against this file's own checks — the
face-ordinal-to-array-layer mapping (the one place a subtle indexing bug could plausibly hide) was
independently confirmed correct, not merely assumed from the test's own internal consistency.

## Cross-File Observations

- Shares the identical "silently zero-fill instead of throw on an undersized/null destination
  buffer" convention (`WebGPUTextureCubeBackend::GetData()` lines 953-956) with
  `WebGPUTextureBackend::GetData()` and `WebGPUTexture3DBackend::GetData()` (both audited in this
  same batch) — a consistent, deliberate design choice across all three texture-kind backends
  within this one file, not an isolated shortcut.
- `TextureCube::SetData`/`GetData`'s XNA-layer rectangle-bounds check (`x+w>levelSize||
  y+h>levelSize`, `TextureCube.cpp` lines 161, 201) is present and would catch an out-of-bounds
  `Rectangle` before it ever reaches `WebGPUTextureCubeBackend` — unlike the `Texture3D` case
  audited alongside this file, where the equivalent XNA-layer check is missing (only the region's
  *volume* vs. `elementCount` is validated, not its position against `width_`/`height_`/`depth_`).
  `TextureCube` is the more defensively-written of the two siblings on this specific axis.

## Missing or Weak Tests

- **No invalid-face / invalid-level / out-of-bounds-rectangle coverage.** None of the three checks
  drives `IsValidCubeMapFace()`'s throw path (an out-of-range `CubeMapFace` cast), a negative/
  out-of-range `level`, or a `Rectangle` exceeding the level's bounds — all real, present validation
  in `TextureCube.cpp` (lines 147-156, 187-196, 161, 201) that this file never exercises. Severity:
  LOW (consistent with the same gap noted in this batch's other two `_getdata_` audits — a
  systemic, not file-specific, absence of negative-path testing across this shard).
- No test checks `TextureCube::getSizeProperty()`/`GetTypeName()` (trivial, but per
  `AUDIT_CHECKLIST.md`'s "every public method... must have at least one unit test" standard, these
  are technically uncovered by this specific file — likely covered elsewhere in the broader
  `TextureCube`-focused test set, not verified as part of this batch).

## Positive Findings

- Check A's per-face loop (all 6 faces, not a sampled subset) combined with `kFaceColors`' 6
  genuinely distinct colours is a strong design — a face-aliasing bug affecting even one face pair
  would be caught, unlike a 2-face spot-check.
- Check B's use of a genuinely per-pixel-varying pattern (not a flat colour) for the sub-rectangle
  check is the correct technique to catch a shifted/garbled read, matching the same good practice
  independently observed in this batch's `Texture2D`/`Texture3D` sibling tests.
- The file's own header comment correctly and specifically contrasts this test's "genuine
  end-to-end XNA-layer test" framing against `Texture2D::GetData()`'s CPU-shadow-based design (which
  requires the *opposite* framing in that sibling test) — showing real understanding of the two
  classes' actually-different architectures rather than a copy-pasted boilerplate comment.

## Final Assessment

A thorough, correctly-targeted end-to-end test for `TextureCube::GetData()`/
`WebGPUTextureCubeBackend::GetData()` (WEBGPU-113); the face-ordinal-to-array-layer mapping that
could most plausibly hide a subtle bug was independently confirmed correct rather than taken on
faith. The only actionable gap is the same negative/out-of-bounds testing absence found across this
whole batch's `_getdata_` files, not a defect in what the file does test.
