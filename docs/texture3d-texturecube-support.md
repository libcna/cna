# Texture3D / TextureCube renderer support — CNA

This page records the EasyGL resource contract. The original Tasks 271–279 audit found several
real gaps, but its two later follow-ups (Tasks 862/863) are complete: volume mip storage and
cube/volume shader binding are not open work. Other renderers document their narrower boundaries
on their own renderer pages; applications should query the active renderer capability profile
instead of inferring support from this EasyGL record.

---

## Legend

| Symbol | Meaning |
|--------|---------|
| ✅ | Implemented and covered by the named behavior test. |
| ⚠️ | Implemented with the documented boundary. |
| ❌ | Unsupported and refused explicitly. |

---

## Construction (`Texture3D`/`TextureCube` constructors)

| Renderer | `mipMap` respected? | `SurfaceFormat` respected? |
|---------|:---:|:---:|
| EasyGL | ✅ complete cube and volume mip chains | ✅ every format it classifies as supported keeps its declared transfer/storage contract |

EasyGL no longer reduces these resources to RGBA8. `TextureCube` supports the classic packed,
float/half and DXT families its live context can represent; `Texture3D` supports the classic
uncompressed HiDef families and explicitly refuses DXT and the two SNORM volume layouts. The
authoritative decision is `ClassifyTextureCubeFormatEXT` / `ClassifyTexture3DFormatEXT`, including
the device-dependent `Rg32`/`Rgba64` answer. `ClassicTextureFormat` verifies exact normalized,
float, half and compressed transfers and sampling rather than treating construction as proof.

---

## `SetData` — box / rect sub-region uploads

The CNA public API layer (`Texture3D::SetData`, `TextureCube::SetData`) validates arbitrary x/y/z
(or x/y, for `TextureCube`) sub-regions and forwards the exact declared-format byte range. EasyGL
keeps the corresponding exact-format readback shadow (also used to restore cube resources) and
uploads the same region to the GL resource.

| Renderer | Arbitrary sub-region upload | Verified how |
|---------|:---:|---|
| EasyGL | ✅ | `EasyGL_Texture3D_PartialBox_RoundTrip`, `EasyGL_TextureCube_PartialRect_RoundTrip`, and `ClassicTextureFormat` |

---

## `GetData` — readback

`GetData` of a `Texture3D` or `TextureCube` depends on the renderer's
`ITexture3DRenderer`/`ITextureCubeRenderer` contract. EasyGL returns the exact declared bytes from
its resource/shadow path and refuses a request it cannot complete; the shared layer never converts
an untouched scratch buffer into fabricated output.

| Renderer | `GetData` implemented? |
|---------|:---:|
| EasyGL | ✅ — full, partial and mip-level round trips across the supported classic formats (`ClassicTextureFormat`). |

---

## Mip levels (`level` > 0)

| Renderer | `Texture3D` level>0 | `TextureCube` level>0 |
|---------|:---:|:---:|
| EasyGL | ✅ every level pre-allocated and independently round-tripped | ✅ every face/level pre-allocated and independently round-tripped |

Task 276 fixed the original cube-only allocation. Task 862 then applied the same requirement to
`EasyGLTexture3DRenderer`: its constructor allocates every level before `glTexSubImage3D` can write
one. `EasyGL_Texture3D_Mip_RoundTrip`, `EasyGL_TextureCube_Mip_RoundTrip`, and the shared
`Texture3DTest`/`TextureCubeTest` mip contracts pin both paths.

---

## `CubeMapFace` validation

Fixed this session (Task 279): `TextureCube::SetData`/`GetData` now throw `std::out_of_range` for
an out-of-range `CubeMapFace` value, at the public API layer, on all renderers (the check lives in
`TextureCube.cpp`, above the renderer dispatch). FNA itself never validates this — confirmed via
`TextureCube.cs` — so this is a CNA safety extra. The renderers already guarded against an invalid
face internally too (`if (face < 0 || face >= 6) return;`), so this was already memory-safe before
Task 279; the fix only changes silent-no-op into a clear exception.

---

## Sampling in shaders

The Task 277 finding (no `Texture` inheritance and no custom-effect binding) is historical.
`Texture3D` and `TextureCube` derive from `Texture`, and `ShaderEffect::SetTexture` has explicit
2D, cube and volume overloads.

| Resource | EasyGL evidence |
|---|---|
| `TextureCube` | ✅ stock `EnvironmentMapEffect`, `ClassicTextureFormat` cube-sampling probes, and `EasyGL_ShaderEffect_TextureCube` |
| `Texture3D` | ✅ `EasyGL_ShaderEffect_Texture3D` samples distinct slices; `RendererFeature::Texture3DSampling` reports supported |

API presence does not imply that every renderer executes custom shader source. Query the selected
renderer for its shader dialect, `ShaderEffectSourceExecution`, and `Texture3DSampling` answers.

SDLGPU source and portable-bytecode sampler layouts are documented in
[SDLGPU ShaderEffect layout](sdlgpu-shader-effect-layout.md). In particular a native
fragment `sampler3D` at set2/binding0 consumes the effect's volume unit0, not SpriteBatch's
2D texture. Unsupported layouts are refused explicitly. The source-layout and existing
portable per-unit-sampler regressions protect this distinction.

---

## `DDSFromStreamEXT` (`TextureCube` only — no `Texture3D` equivalent in FNA)

**Implemented.** This section described a non-functional 1×1 stub long after Task 663 replaced it;
the stale text is corrected here rather than deleted, because a reader who remembers the stub needs
to see that it went.

It is implemented once, in the shared XNA-layer `TextureCube.cpp`, not per-renderer. It parses the
DDS header mirroring FNA's `Texture.ParseDDS`, refuses a non-cube file with
`System::FormatException("This file does not contain cube data!")` and malformed or out-of-scope
input with `System::NotSupportedException`, and decodes every face and mip level of a
DXT1/DXT3/DXT5 or byte-aligned 24-bit RGB888/BGR888 cube map to RGBA8. The 24-bit channel masks
select the byte order and alpha is filled with 255. DX10 extended headers, other packed layouts,
and HDR variants remain unsupported.

CNA converts to RGBA8 on the CPU and uploads `SurfaceFormat::Color`, where FNA uploads the
compressed blocks to a real compressed GPU format. That is a deliberate deviation matching
`Texture2D::DDSFromStreamEXT`'s stream-decoding contract. Some CNA renderers now accept explicit native
compressed-texture transfers, but this portable stream route still normalizes its supported source
encodings before upload.

Since `CNBF-113`, the parsing and decompression live in
`CNA::Internal::Graphics::DecodeDdsCube` — a pure-CPU component needing no `GraphicsDevice` — and
`DDSFromStreamEXT` is a thin consumer that decodes and uploads. The same decoder is what lets the
headless CNB content compiler produce a `TextureCube.cnb`. Behaviour is unchanged in both
directions; only the location moved.

---

## Current EasyGL limitation

`EasyGLTextureCubeRenderer` is a recoverable resource and recreates its GL object from retained
face/mip data after context loss. `EasyGLTexture3DRenderer` is not yet registered as an
`easygl::RecoverableResource`; after a WebGL 2 or Android GL context loss it retains a dead native
handle. This is the current defect recorded in `handoff.md` §5.1. It is unrelated to ordinary mip
allocation, transfer, readback, or sampling while the context remains alive.

