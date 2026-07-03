# SurfaceFormat Backend Support — CNA

> Generated from source inspection against Tasks 174, 281.
> Covers: EasyGL, Vulkan, Bgfx, SDL_Renderer backends.

---

## Canonical `SurfaceFormat` enum values (Task 281)

The authoritative list, ordinal values, and descriptions below are taken directly from FNA's
`SurfaceFormat.cs` (`Microsoft.Xna.Framework.Graphics.SurfaceFormat`). Ordinal values are load-bearing
— every backend does `static_cast<int>(format)` at some point, so CNA's enum must declare these 27
values in exactly this order.

**Task 281 finding, fixed:** values 20–26 previously did **not** match FNA at all. CNA's enum instead
declared 7 invented "Srgb" variants at those ordinals (`ColorSrgb`, `Bgr565Srgb`, `Bgra5551Srgb`,
`Bgra4444Srgb`, `Dxt1Srgb`, `Dxt3Srgb`, `Dxt5Srgb`) that have no FNA equivalent at all, while omitting
FNA's real values entirely. This violated the project's hard rule that enum names must match XNA/FNA
exactly. Fixed in `SurfaceFormat.hpp` to match FNA's actual 27 values below; pinned by 27 new/existing
ordinal-value unit tests in `SurfaceFormatTests.cpp` (0–19 were already correct and tested; 20–26 are
new). Blast radius was small: only one example test (`easygl_surface_format_throws_test.cpp`)
referenced the old invented names, since `Texture::ValidateFormat` only special-cases `Color` and
throws for every other value — the specific identity of the "other" value never mattered to any
validation logic.

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

"FNA ext." means the value doesn't exist in the original Microsoft XNA4 enum but is part of FNA's
real, current `SurfaceFormat.cs` — these are still the authoritative reference for this project (per
project convention: "the authoritative behavioral and API reference is the local FNA source tree"),
not CNA inventions.

---

## Per-format CPU size (Task 282)

Ported directly from FNA's `Texture.cs` ("Static SurfaceFormat Size Methods" region) as two public
static methods on CNA's `Texture` class — real FNA API, so no `SurfaceFormatHelper` class was
invented; both throw `std::out_of_range` for an unrecognized enum value.

| Format | `GetBlockSizeSquaredEXT` | `GetFormatSizeEXT` (bytes) |
|---|:---:|:---:|
| Dxt1 | 16 | 8 |
| Dxt3, Dxt5, Dxt5SrgbEXT, Bc7EXT, Bc7SrgbEXT | 16 | 16 |
| Alpha8, ByteEXT | 1 | 1 |
| Bgr565, Bgra4444, Bgra5551, HalfSingle, NormalizedByte2, UShortEXT | 1 | 2 |
| Color, Single, Rg32, HalfVector2, NormalizedByte4, Rgba1010102, ColorBgraEXT, ColorSrgbEXT | 1 | 4 |
| HalfVector4, Rgba64, Vector2, HdrBlendable | 1 | 8 |
| Vector4 | 1 | 16 |

Not yet ported: FNA's `ValidateGetDataFormat`/`GetPixelStoreAlignment`, which consume
`GetFormatSizeEXT` and are the actual "required for `SetData`/`GetData`" helpers — tracked as
Task 283. Not yet load-bearing in CNA since `Texture::ValidateFormat` still blocks every non-`Color`
format before any of this logic would run.

---

## How format selection works (current state)

`Texture2D` stores the requested `SurfaceFormat` in `format_` but does **not** forward it
to the backend.  The `IGraphicsBackend::CreateTexture(const ImageData&)` contract carries
only pre-decoded RGBA8 pixel bytes — the format enum is invisible to all backends.

`Texture3D` and `TextureCube` receive `surfaceFormat` as an `int` argument in
`CreateTexture3D` / `CreateTextureCube`, but every backend ignores it (`/* surfaceFormat */`).

**Result**: every texture, regardless of the requested `SurfaceFormat`, is stored on the
GPU as RGBA8 unorm (8 bits per channel, linear).

---

## Legend

| Symbol | Meaning |
|--------|---------|
| ✅ | Correct GPU format; SetData / GetData round-trip works |
| ⚠️ | Partially works with caveats documented below |
| ❌ | Not implemented; silently wrong or rejected |
| — | Not applicable (backend has no support for this texture type) |

---

## Format table

| SurfaceFormat | Bytes/px | EasyGL | Vulkan | Bgfx | SDL_Renderer | Notes |
|---|---|---|---|---|---|---|
| **Color** | 4 | ✅ GL_RGBA8 | ✅ VK_FORMAT_R8G8B8A8_UNORM | ✅ RGBA8 | ✅ RGBA32 | RGBA byte order; only fully supported format |
| Bgr565 | 2 | ❌ | ❌ | ❌ | ❌ | Format not forwarded; RGBA8 used instead |
| Bgra5551 | 2 | ❌ | ❌ | ❌ | ❌ | Same |
| Bgra4444 | 2 | ❌ | ❌ | ❌ | ❌ | Same |
| **Dxt1** | 0.5 | ⚠️ | ⚠️ | ⚠️ | ⚠️ | `FromStream` (.DDS): CPU-decompressed to RGBA8 before upload — works, but 8× VRAM overhead. Direct `SetData` with compressed blocks: ❌ misinterpreted as RGBA8. No native GPU DXT path. |
| **Dxt3** | 1 | ⚠️ | ⚠️ | ⚠️ | ⚠️ | Same as Dxt1 |
| **Dxt5** | 1 | ⚠️ | ⚠️ | ⚠️ | ⚠️ | Same as Dxt1 |
| NormalizedByte2 | 2 | ❌ | ❌ | ❌ | ❌ | Signed 8-bit per channel; no signed GL/Vk format used |
| NormalizedByte4 | 4 | ❌ | ❌ | ❌ | ❌ | Same |
| Rgba1010102 | 4 | ❌ | ❌ | ❌ | ❌ | 10-bit per channel; requires GL_RGB10_A2 / VK_FORMAT_A2B10G10R10_UNORM_PACK32 |
| Rg32 | 4 | ❌ | ❌ | ❌ | — | 16-bit per channel RG; requires GL_RG16 / VK_FORMAT_R16G16_UNORM |
| Rgba64 | 8 | ❌ | ❌ | ❌ | — | 16-bit per channel RGBA; requires GL_RGBA16 / VK_FORMAT_R16G16B16A16_UNORM |
| Alpha8 | 1 | ❌ | ❌ | ❌ | ❌ | Single alpha channel; requires GL_R8 + swizzle or GL_ALPHA |
| Single | 4 | ❌ | ❌ | ❌ | — | 32-bit float R; requires GL_R32F / VK_FORMAT_R32_SFLOAT |
| Vector2 | 8 | ❌ | ❌ | ❌ | — | 32-bit float RG; requires GL_RG32F |
| Vector4 | 16 | ❌ | ❌ | ❌ | — | 32-bit float RGBA; requires GL_RGBA32F |
| HalfSingle | 2 | ❌ | ❌ | ❌ | — | 16-bit half float R; requires GL_R16F |
| HalfVector2 | 4 | ❌ | ❌ | ❌ | — | 16-bit half float RG; requires GL_RG16F |
| HalfVector4 | 8 | ❌ | ❌ | ❌ | — | 16-bit half float RGBA; requires GL_RGBA16F — key format for HDR (NOXNA N20) |
| HdrBlendable | 8 | ❌ | ❌ | ❌ | — | Alias for RGBA16F in FNA; same as HalfVector4 |
| ColorBgraEXT | 4 | ❌ | ❌ | ❌ | ❌ | XNA3 legacy ABGR byte order; stored as RGBA8, byte order not respected |
| **ColorSrgbEXT** | 4 | ❌ | ❌ | ❌ | ❌ | Stored as linear RGBA8; sRGB flag silently ignored; requires GL_SRGB8_ALPHA8 / VK_FORMAT_R8G8B8A8_SRGB |
| Dxt5SrgbEXT | 1 | ❌ | ❌ | ❌ | ❌ | CPU decompression path (see Dxt5 above) drops the sRGB flag |
| Bc7EXT | 1 | ❌ | ❌ | ❌ | ❌ | No BC7 decode/upload path at all (unlike Dxt1/3/5's CPU-decompress fallback) |
| Bc7SrgbEXT | 1 | ❌ | ❌ | ❌ | ❌ | Same as Bc7EXT, plus sRGB |
| ByteEXT | 1 | ❌ | ❌ | ❌ | ❌ | Single unsigned byte channel; requires GL_R8 |
| UShortEXT | 2 | ❌ | ❌ | ❌ | ❌ | Single unsigned short channel; requires GL_R16 |

---

## Backend GPU format summary

| Backend | Texture2D | Texture3D | TextureCube | RenderTarget (color) | RenderTarget (depth) |
|---------|-----------|-----------|-------------|----------------------|----------------------|
| EasyGL | GL_RGBA8 | GL_RGBA8 | GL_RGBA8 | GL_RGBA8 | GL_DEPTH_COMPONENT24 |
| Vulkan | VK_FORMAT_R8G8B8A8_UNORM | VK_FORMAT_R8G8B8A8_UNORM | VK_FORMAT_R8G8B8A8_UNORM | swapchain format¹ | D32_SFLOAT or D24_UNORM_S8² |
| Bgfx | RGBA8 | RGBA8 | RGBA8 | RGBA8 | D24S8 |
| SDL_Renderer | SDL_PIXELFORMAT_RGBA32 | — | — | SDL_PIXELFORMAT_RGBA32 | — |

¹ Vulkan swapchain prefers `VK_FORMAT_B8G8R8A8_SRGB`; falls back to first format reported by the device.
² Vulkan depth format is selected at init: tries D32_SFLOAT → D32_SFLOAT_S8_UINT → D24_UNORM_S8_UINT.

---

## What needs to be fixed (priority order)

### High priority

| # | Format(s) | Required GL/Vulkan format | Work needed |
|---|-----------|--------------------------|------------|
| T176a | ColorSrgbEXT | GL_SRGB8_ALPHA8 / VK_FORMAT_R8G8B8A8_SRGB | Forward `surfaceFormat` to backends; add sRGB branch |
| T176b | HalfVector4 / HdrBlendable | GL_RGBA16F / VK_FORMAT_R16G16B16A16_SFLOAT | Required for NOXNA HDR render targets (N20) |
| T176c | Dxt1/3/5 via SetData | GL_COMPRESSED_RGBA_S3TC_DXT1/3/5_EXT | Native GPU compressed upload path |

### Medium priority

| # | Format(s) | Required GL/Vulkan format |
|---|-----------|--------------------------|
| — | Alpha8 | GL_R8 + `GL_TEXTURE_SWIZZLE_RGBA = {0,0,0,R}` |
| — | Single / HalfSingle | GL_R32F / GL_R16F |
| — | Vector2 / HalfVector2 | GL_RG32F / GL_RG16F |
| — | Vector4 / HalfVector4 | GL_RGBA32F / GL_RGBA16F |
| — | Rg32 / Rgba64 | GL_RG16 / GL_RGBA16 |
| — | Rgba1010102 | GL_RGB10_A2 |

### Low priority / unlikely needed

- `Bgr565`, `Bgra5551`, `Bgra4444`, `ColorBgraEXT` — uncommon in modern use.
- `NormalizedByte2/4` — bump-map formats; rarely used.
- `Dxt5SrgbEXT` — follows from fixing DXT native upload + sRGB.
- `Bc7EXT`/`Bc7SrgbEXT` — no existing CPU-decompress fallback (unlike Dxt1/3/5), so this needs a
  real BC7 decoder before it can work at all, even via the CPU-decompress path.
- `ByteEXT`/`UShortEXT` — single-channel formats; rarely used outside custom shaders.

---

## Implementation notes

To properly support non-Color formats, the following refactors are needed:

1. **Pass `SurfaceFormat` through `IGraphicsBackend::CreateTexture`** — add it to
   `ImageData` or add a second overload `CreateTexture2D(w, h, format, mipMap)`.

2. **Add a format dispatch function in each backend** — map `SurfaceFormat` enum to
   the native `metagl::InternalFormat` / `VkFormat` / `bgfx::TextureFormat`.

3. **Keep the RGBA8 path as the default fallback** — unknown or unsupported formats
   should throw `std::runtime_error` rather than silently using RGBA8.

4. **DXT native upload (EasyGL/Vulkan)** — requires `GL_EXT_texture_compression_s3tc`
   or `VK_EXT_texture_compression_bc` to be present; must query caps at init.
