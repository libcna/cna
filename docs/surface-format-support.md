# SurfaceFormat support — EasyGL

> This page covers the EasyGL renderer family
> (`OPENGLES3`, `OPENGL33`, and `WEBGL2`). Other renderers publish their capability boundary in
> their own renderer documentation and in `graphics-renderer-feature-matrix.md`.

---

## Canonical `SurfaceFormat` enum values

The list, ordinal values, and descriptions below are FNA's `SurfaceFormat.cs`
(`Microsoft.Xna.Framework.Graphics.SurfaceFormat`). Ordinal values are load-bearing — every
renderer does `static_cast<int>(format)` at some point, so CNA's enum declares these 27 values in
exactly this order. `SurfaceFormatTests.cpp` pins every ordinal.

| # | Name | XNA 4.0 original? | Description |
|--:|------|:---:|---|
| 0 | `Color` | ✅ | Unsigned 32-bit ARGB, 8 bits per channel. |
| 1 | `Bgr565` | ✅ | Unsigned 16-bit BGR: 5/6/5 bits. |
| 2 | `Bgra5551` | ✅ | Unsigned 16-bit BGRA: 5/5/5 bits color, 1 bit alpha. |
| 3 | `Bgra4444` | ✅ | Unsigned 16-bit BGRA: 4 bits per channel. |
| 4 | `Dxt1` | ✅ | DXT1 compressed; dimensions must be a multiple of 4. |
| 5 | `Dxt3` | ✅ | DXT3 compressed; dimensions must be a multiple of 4. |
| 6 | `Dxt5` | ✅ | DXT5 compressed; dimensions must be a multiple of 4. |
| 7 | `NormalizedByte2` | ✅ | Signed 16-bit bump-map: 8 bits for u/v. |
| 8 | `NormalizedByte4` | ✅ | Signed 32-bit bump-map: 8 bits per channel. |
| 9 | `Rgba1010102` | ✅ | Unsigned 32-bit RGBA: 10/10/10 bits color, 2 bits alpha. |
| 10 | `Rg32` | ✅ | Unsigned 32-bit RG: 16 bits per channel. |
| 11 | `Rgba64` | ✅ | Unsigned 64-bit RGBA: 16 bits per channel. |
| 12 | `Alpha8` | ✅ | Unsigned 8-bit alpha only. |
| 13 | `Single` | ✅ | IEEE 32-bit float, one channel (red). |
| 14 | `Vector2` | ✅ | IEEE 64-bit float, 32 bits per channel (RG). |
| 15 | `Vector4` | ✅ | IEEE 128-bit float, 32 bits per channel (RGBA). |
| 16 | `HalfSingle` | ✅ | 16-bit half-float, one channel (red). |
| 17 | `HalfVector2` | ✅ | 32-bit half-float, 16 bits per channel (RG). |
| 18 | `HalfVector4` | ✅ | 64-bit half-float, 16 bits per channel (RGBA). |
| 19 | `HdrBlendable` | ✅ | Float format for HDR data (RGBA16F-equivalent). |
| 20 | `ColorBgraEXT` | ❌ FNA ext. | Unsigned 32-bit ABGR, 8 bits per channel (XNA3 legacy). |
| 21 | `ColorSrgbEXT` | ❌ FNA ext. | Unsigned 32-bit ARGB, sRGB-encoded, read as linear in shaders. |
| 22 | `Dxt5SrgbEXT` | ❌ FNA ext. | DXT5 compressed, sRGB-encoded, read as linear in shaders. |
| 23 | `Bc7EXT` | ❌ FNA ext. | BC7 block compressed format. |
| 24 | `Bc7SrgbEXT` | ❌ FNA ext. | BC7 block compressed format, non-linear sRGB. |
| 25 | `ByteEXT` | ❌ FNA ext. | Unsigned 8-bit, one channel (red). |
| 26 | `UShortEXT` | ❌ FNA ext. | Unsigned 16-bit, one channel (red). |

"FNA ext." means the value does not exist in the original Microsoft XNA4 enum but is part of FNA's
`SurfaceFormat.cs`; CNA declares them with FNA's names and ordinals, not as CNA inventions.

---

## Per-format CPU size

FNA's `Texture.cs` ("Static SurfaceFormat Size Methods" region) provides these as static methods on
`Texture`; CNA keeps them there. Both throw `System::ArgumentOutOfRangeException` for an
unrecognized enum value.

| Format | `GetBlockSizeSquaredEXT` | `GetFormatSizeEXT` (bytes) |
|---|:---:|:---:|
| Dxt1 | 16 | 8 |
| Dxt3, Dxt5, Dxt5SrgbEXT, Bc7EXT, Bc7SrgbEXT | 16 | 16 |
| Alpha8, ByteEXT | 1 | 1 |
| Bgr565, Bgra4444, Bgra5551, HalfSingle, NormalizedByte2, UShortEXT | 1 | 2 |
| Color, Single, Rg32, HalfVector2, NormalizedByte4, Rgba1010102, ColorBgraEXT, ColorSrgbEXT | 1 | 4 |
| HalfVector4, Rgba64, Vector2, HdrBlendable | 1 | 8 |
| Vector4 | 1 | 16 |

The same FNA region provides two more methods:

- `Texture::GetPixelStoreAlignment(format)` — `min(8, GetFormatSizeEXT(format))`, the OpenGL 2.1
  `GL_PACK_ALIGNMENT`/`GL_UNPACK_ALIGNMENT` cap.
- `Texture::ValidateGetDataFormat(format, elementSizeInBytes)` — throws
  `System::ArgumentException` unless `elementSizeInBytes` evenly divides
  `GetFormatSizeEXT(format)`; an unrecognized format propagates
  `System::ArgumentOutOfRangeException` from `GetFormatSizeEXT`.

Both are `internal` in FNA; they are `public static` on CNA's `Texture` because the C API exports
them (`modules/c-api/spec/Texture.toml`).
`ValidateGetDataFormat` is wired into all 4 of FNA's real call sites: `Texture2D::GetData`,
`Texture3D::GetData`, `TextureCube::GetData`, `GraphicsDevice::GetBackBufferData` — all using
`elementSizeInBytes = 4` (this project's `Color`/RGBA-raw-bytes convention). Renderer-specific
promotion gates use it for supported non-Color formats, including EasyGL's ES 3-class
`NormalizedByte4` path.

---

## Current EasyGL contract

Format support is selected in three layers, in this order:

1. XNA profile and resource-kind rules reject combinations that XNA does not permit. In
   particular, volume textures are HiDef-only, volumes cannot use DXT or signed-normalized formats,
   and cubes cannot use `NormalizedByte2` or `NormalizedByte4`.
2. The active renderer classifies the exact resource kind. Texture2D, TextureCube, Texture3D, and
   render-target capabilities are deliberately separate questions.
3. A `Defer` verdict reaches the framework's conservative default, which accepts only `Color`.
   Unknown formats therefore do not silently become RGBA8 textures.

Render targets follow XNA/FNA preferred-format selection: if the preferred format is not allowed or
not renderable, the public resource is created as `Color` and reports that substitution. Texture
resources do not substitute an unsupported requested format; construction throws a named
`System` exception.

### Classic XNA formats (ordinals 0–19)

`Exact` below means that full, partial, mip-level, and generic typed `SetData`/`GetData` transfers
preserve the format's declared bytes. It does not require the GL allocation to have the identical
channel count: EasyGL may widen one- and two-channel data while retaining exact public transfers
and format-correct shader values. `Probed` means support is decided on the live GL context before
the renderer advertises it.

| Format group | Texture2D | TextureCube | Texture3D | Color render target |
|---|---|---|---|---|
| `Color` | Exact, RGBA8 | Exact, RGBA8 | Exact, RGBA8 | RGBA8 |
| `Bgr565`, `Bgra5551`, `Bgra4444` | Exact packed storage and sampling | Exact | Exact | Preferred format falls back to `Color` |
| `Dxt1`, `Dxt3`, `Dxt5` | Exact block transfers; native S3TC or exact decode fallback | Exact block transfers and sampling | Rejected by XNA volume rules | Rejected by XNA render-target rules; preferred format falls back to `Color` |
| `NormalizedByte2`, `NormalizedByte4` | Exact signed-normalized storage and sampling | Rejected by XNA cube rules | Rejected by XNA volume rules | Preferred format falls back to `Color` |
| `Rgba1010102` | Exact packed storage and sampling | Exact | Exact | Probed RGB10_A2 |
| `Rg32`, `Rgba64` | Exact when texture-norm16 is available | Exact when texture-norm16 is available | Exact when texture-norm16 is available | Probed RG16/RGBA16; otherwise `Color` fallback |
| `Alpha8` | Exact alpha semantics using format-correct widening | Exact | Exact | Not renderable; preferred format falls back to `Color` |
| `Single`, `Vector2`, `Vector4` | Exact float storage/sampling, with widening where needed | Exact | Exact | Probed R32F/RG32F/RGBA32F |
| `HalfSingle`, `HalfVector2`, `HalfVector4`, `HdrBlendable` | Exact half-float storage/sampling, with widening where needed | Exact | Exact | Probed R16F/RG16F/RGBA16F; `HdrBlendable` uses RGBA16F |

The Reach profile still rejects its eleven HiDef-only classic Texture2D formats before EasyGL is
asked. The renderer's broader storage support does not bypass that public XNA restriction.

### FNA extension formats (ordinals 20–26)

`ColorBgraEXT`, `ColorSrgbEXT`, `Dxt5SrgbEXT`, `Bc7EXT`, `Bc7SrgbEXT`, `ByteEXT`, and
`UShortEXT` have correct enum values and CPU size calculations, but they are not promoted by
EasyGL's resource classifiers. Texture construction is refused with a named `System` exception; a
preferred render-target request follows the normal `Color` substitution rule. In particular,
`ColorSrgbEXT` must not be described as linear RGBA8 storage: it is refused rather than silently
losing its transfer function.

This is separate from CNA's sRGB math used by PBR and content processing. Those shader/CPU transfer
functions do not imply support for the `ColorSrgbEXT`, `Dxt5SrgbEXT`, or `Bc7SrgbEXT` resource
formats.

## Storage, transfer, and sampling details

- `ImageData::surfaceFormat`, `CreateTextureCube`, and `CreateTexture3D` all carry the requested
  format to EasyGL. Every accepted classic format has an explicit mapping; there is no generic
  non-Color-to-RGBA8 fallback.
- Public DXT transfers remain DXT blocks. EasyGL keeps native compressed storage when S3TC is
  available and otherwise decodes renderer-side into RGBA8 for sampling. The public block bytes,
  partial-block updates, faces, and mip levels remain exact in either route.
- Single- and two-channel classic formats may use four-channel physical storage on ordinary
  textures. Missing sampled channels are supplied with XNA-compatible defaults; readback returns
  only the declared format bytes.
- `ClassifyColorTransferFormatEXT` keeps `Color`-element convenience transfers distinct from raw
  typed transfers. A non-Color texture is not reinterpreted through `Color` merely because both
  element types happen to occupy four bytes.
- Point sampling is covered across every promoted Texture2D classic format. Cube coverage includes
  EnvironmentMap sampling for normalized, float/half, and DXT data; Texture3D coverage includes
  shader sampling as well as slice, mip, and partial-box transfers.

## Render-target boundary

EasyGL's exact color-attachment mappings are:

| Requested format | GL storage |
|---|---|
| `Color` | RGBA8 |
| `Rgba1010102` | RGB10_A2 |
| `Rg32` | RG16 |
| `Rgba64` | RGBA16 |
| `Single`, `Vector2`, `Vector4` | R32F, RG32F, RGBA32F |
| `HalfSingle`, `HalfVector2`, `HalfVector4`, `HdrBlendable` | R16F, RG16F, RGBA16F |

Every non-Color attachment above is runtime-probed for framebuffer completeness and, where
relevant, multisample resolve support. `Rg32` and `Rgba64` additionally require texture-norm16.
Capability queries, preferred-format selection, 2D/cube construction, binding, clearing, typed
readback, mip generation, MSAA resolve, MRT, and sampling are tested against the same classifier.

Depth/stencil support is documented separately in `depthstencilstate-support.md` and
`rendertarget-support.md`; it is not fixed to a single GL depth format.

## Remaining limitations

- EasyGL does not currently promote any of the seven FNA extension resource formats (20–26).
- EasyGL has no native `Alpha8` render-target storage; preferred `Alpha8` targets become `Color`.
  SOFTWARE stores the format itself (L22).
- `Rg32` and `Rgba64` ordinary textures and render targets depend on the live context's normalized
  16-bit texture support. A context that cannot prove it rejects the format or selects the defined
  render-target fallback.
- WebGL cannot apply the texture swizzle used to make absent channels of one- and two-channel render
  targets sample as one. On that profile those absent channels read as GL zero; this is a recorded
  platform divergence, not a claim of full D3D channel-default parity.

## Verification and sources of truth

The implementation boundary is defined by the four EasyGL classifier methods and their matching
allocation paths in `EasyGLRenderer.cpp`, plus the XNA gates in `Texture.cpp`. The focused regression
suites are:

- `ClassicTextureFormat.*` — classic Texture2D/cube/volume transfers and sampling;
- `NormalizedRenderTargetRoundTrip.*` and `HdrRenderTargetRoundTripTest.*` — normalized and float
  render targets;
- `RenderTargetFormatAgreement.*` — query/construction/use agreement for every enum value;
- `SurfaceFormatRefusalSurface.*` — every resource kind either succeeds honestly or throws a named
  exception;
- `EasyGL_DxtFormat`, `EasyGL_DxtTextureCube`, `EasyGL_ShaderEffect_Texture3D`, and
  `EasyGL_Pbr_SrgbTransfer` — standalone renderer integration coverage.

For a cross-renderer view, use `graphics-renderer-feature-matrix.md`; current defects are in
`KNOWN_ISSUES.md`.
