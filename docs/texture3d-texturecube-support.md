# Texture3D / TextureCube renderer support — CNA

This page records the EasyGL resource contract. Other renderers document their narrower boundaries
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

EasyGL does not reduce these resources to RGBA8. `TextureCube` supports the classic packed,
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

The cube and volume renderers (`EasyGLTexture3DRenderer` included) allocate every level in their
constructors before `glTexSubImage3D` can write one. `EasyGL_Texture3D_Mip_RoundTrip`, `EasyGL_TextureCube_Mip_RoundTrip`, and the shared
`Texture3DTest`/`TextureCubeTest` mip contracts pin both paths.

---

## `CubeMapFace` validation

`TextureCube::SetData`/`GetData` throw `std::out_of_range` for an out-of-range `CubeMapFace` value,
at the public API layer, on all renderers (the check lives in `TextureCube.cpp`, above the renderer
dispatch). FNA's `TextureCube.cs` never validates this, so this is a CNA safety extra; the
renderers also guard an invalid face internally.

---

## Sampling in shaders

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
2D texture. Unsupported layouts are refused explicitly. The source-layout and portable
per-unit-sampler regressions protect this distinction.

---

## `DDSFromStreamEXT` (`TextureCube` only — no `Texture3D` equivalent in FNA)

It is implemented once, in the shared XNA-layer `TextureCube.cpp`, not per-renderer. It parses the
DDS header mirroring FNA's `Texture.ParseDDS`, refuses a non-cube file with
`System::FormatException("This file does not contain cube data!")` and malformed or out-of-scope
input with `System::NotSupportedException`, and decodes every face and mip level of a
DXT1/DXT3/DXT5 or byte-aligned 24-bit RGB888/BGR888 cube map to RGBA8. The 24-bit channel masks
select the byte order and alpha is filled with 255. DX10 extended headers, other packed layouts,
and HDR variants remain unsupported.

CNA converts to RGBA8 on the CPU and uploads `SurfaceFormat::Color`, where FNA uploads the
compressed blocks to a real compressed GPU format. That is a deliberate deviation matching
`Texture2D::DDSFromStreamEXT`'s stream-decoding contract. Some CNA renderers accept explicit native
compressed-texture transfers, but this portable stream route normalizes its supported source
encodings before upload.

The parsing and decompression live in `CNA::Internal::Graphics::DecodeDdsCube` (`CNBF-113`) — a
pure-CPU component needing no `GraphicsDevice` — and `DDSFromStreamEXT` is a thin consumer that
decodes and uploads. The same decoder lets the headless CNB content compiler produce a
`TextureCube.cnb`.

---

## EasyGL context recovery

Cube and volume textures both register as `easygl::RecoverableResource`s. Context loss discards
their native names without issuing GL calls; restoration creates new objects and uploads every
retained face/mip or volume/mip in its declared format. Partial uploads update those exact CPU
shadows, and disposal removes the resource from the recovery registry.

`EasyGL_Texture3D_ContextLoss_OPENGL33` and `EasyGL_Texture3D_ContextLoss_OPENGLES3` sample the
same volume objects across two real native context destroy/recreate cycles. They cover distinct
voxels and all mips, box updates before and after restoration, odd-width Alpha8, packed Bgr565,
Single float storage, exact CPU readback and disposal before loss. The test compiles a fresh
custom sampling shader for each context, so it isolates texture recovery; custom
`ShaderEffect` programs themselves relink and restore their uniform values after a context loss
(see [`graphics-resource-lifetime.md`](graphics-resource-lifetime.md) §8). These Linux native
tests exercise the shared registry path; they do not establish Android lifecycle or asynchronous
browser event qualification for volume textures.

