# SurfaceFormat Renderer Support — CNA

> Generated from source inspection against Tasks 174, 281.
> Covers: EasyGL, Vulkan, Bgfx, SDL_Renderer renderers.

> Skia successor note (SKIA-134): this historical cross-renderer document does not define the Skia
> implementation routes. The checked, current 27-value Skia contract is
> [`skia-surface-format-matrix.md`](skia-surface-format-matrix.md). SKIA-135–139 have promoted
> `Bgr565`, `Bgra4444`, `Rgba1010102`, `Rg32`, `Rgba64`, `Alpha8`, `ColorBgraEXT`,
> `ColorSrgbEXT`, `ByteEXT`, `UShortEXT`, `Single`, `Vector2`, `Vector4`, `HalfSingle`,
> `HalfVector2`, `HalfVector4`, `HdrBlendable`, `Bgra5551`, `NormalizedByte2`, and
> `NormalizedByte4` for Skia `Texture2D`; the remaining format gates are tracked by SKIA-140–143.

---

## Canonical `SurfaceFormat` enum values (Task 281)

The authoritative list, ordinal values, and descriptions below are taken directly from FNA's
`SurfaceFormat.cs` (`Microsoft.Xna.Framework.Graphics.SurfaceFormat`). Ordinal values are load-bearing
— every renderer does `static_cast<int>(format)` at some point, so CNA's enum must declare these 27
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

**Task 283** ported the remaining two methods from the same FNA region:

- `Texture::GetPixelStoreAlignment(format)` — `min(8, GetFormatSizeEXT(format))`, the OpenGL 2.1
  `GL_PACK_ALIGNMENT`/`GL_UNPACK_ALIGNMENT` cap.
- `Texture::ValidateGetDataFormat(format, elementSizeInBytes)` — throws `std::invalid_argument`
  unless `elementSizeInBytes` evenly divides `GetFormatSizeEXT(format)`; throws `std::out_of_range`
  for an unrecognized format, same as `GetFormatSizeEXT` itself.

Both are `internal` in FNA; made `public static` on CNA's `Texture` instead (documented, intentional
deviation — see `AUDIT.md`), since 3 of the 4 real call sites aren't subclasses of `Texture` in CNA.
Wired `ValidateGetDataFormat` into all 4 of FNA's real call sites: `Texture2D::GetData`,
`Texture3D::GetData`, `TextureCube::GetData`, `GraphicsDevice::GetBackBufferData` — all using
`elementSizeInBytes = 4` (this project's `Color`/RGBA-raw-bytes convention). Renderer-specific
promotion gates now use it for supported non-Color formats, including EasyGL's ES 3-class
`NormalizedByte4` path.

---

## Color format mapping — found and fixed a real Vulkan gamma bug (Task 284)

Task 284 asks to verify RGBA/BGRA channel-order correctness across renderers. Channel order itself
was already correct everywhere (confirmed by the dozens of existing exact-color pixel-readback
tests across EasyGL and Vulkan) — but those tests all use only saturated 0/255 component values
(Red/Green/Blue/White/Black/Magenta/Yellow/Cyan), which cannot reveal a **linear-vs-sRGB colorspace**
bug: both 0 and 255 are fixed points of the sRGB transfer curve, so a wrongly-sRGB-decoded texture
still round-trips correctly at the extremes. Testing with a genuine mid-range value (128) instead
uncovered a real bug on Vulkan.

**Found:** `VulkanTextureRenderer` (backing `Texture2D`) created its `VkImage`/`VkImageView` as
`VK_FORMAT_R8G8B8A8_SRGB` — inconsistent with `Texture3D`/`TextureCube`/`RenderTarget2D`'s
`VK_FORMAT_R8G8B8A8_UNORM`, and wrong regardless: FNA's `SurfaceFormat.Color` is explicitly linear
(there's a separate `SurfaceFormat.ColorSrgbEXT` for the gamma-encoded variant — see the canonical
enum table above). Compounding this, the Vulkan swapchain surface format selection explicitly
searched for and preferred `VK_FORMAT_B8G8R8A8_SRGB`, applying an automatic linear→sRGB gamma
encode to **every** presented pixel, textured or not.

These two bugs partially masked each other for textured content — the wrong sRGB *decode* on
texture sample and the swapchain's sRGB *encode* on present are approximate inverses, so a
textured quad's mid-grey (128) read back as ≈128 (looking correct by coincidence). But **any
non-textured content** (plain vertex colors, `BasicEffect` lighting output, blended colors — i.e.
most of the actual rendering pipeline) only went through the swapchain's encode with nothing to
cancel it, so it came out **badly wrong**: a nominal (128,128,128) vertex color read back as
(188,188,188), a 60-unit error.

**Fixed both:** `VulkanTextureRenderer`'s image/view format to `VK_FORMAT_R8G8B8A8_UNORM`, and the
swapchain preference to `VK_FORMAT_B8G8R8A8_UNORM`. Added
`modules/renderers/vulkan/examples/vulkan_texture_srgb_test.cpp` (`Vulkan_Texture2D_ColorFormat_Linear` ctest): renders a
mid-grey (128,128,128) quad two ways — plain vertex color (no texture) vs. a sampled `Texture2D`
filled with the same value (no vertex-color tint) — and compares the two backbuffer readbacks.
Before the fix: vertex-color path read 188, textured path read 128 (diff 60, confirming both bugs).
After the fix: both read exactly 128 (diff 0). Full Vulkan ctest suite: 1944/1945 (only the
pre-existing, already-documented `Vulkan_DepthBias` failure remains; `Vulkan_FillMode_WireFrame`'s
known order-dependent flakiness is unrelated and unaffected). EasyGL and Bgfx were checked and have
no equivalent bug — neither has any `SRGB`/`Srgb` format reference anywhere in their renderer source.

---

## How format selection works (current state)

Historically `Texture2D` stored the requested `SurfaceFormat` in `format_` but did not forward it
to the renderers covered by this document. `ImageData` now carries the format ordinal. Skia uses
that for its promoted formats; EasyGL uses it for `NormalizedByte4` on OpenGL ES 3/OpenGL 3.3/
WebGL 2 profiles, storing exact signed bytes as `GL_RGBA8_SNORM`.

`Texture3D` and `TextureCube` receive `surfaceFormat` as an `int` argument in
`CreateTexture3D` / `CreateTextureCube`, but every renderer ignores it (`/* surfaceFormat */`).

Other accepted textures in the historical renderers below remain RGBA8 unorm. Skia's broader
current exceptions are documented only in its checked matrix linked above.

---

## Legend

| Symbol | Meaning |
|--------|---------|
| ✅ | Correct GPU format; SetData / GetData round-trip works |
| ⚠️ | Partially works with caveats documented below |
| ❌ | Not implemented; silently wrong or rejected |
| — | Not applicable (renderer has no support for this texture type) |

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
| NormalizedByte4 | 4 | ✅ GL_RGBA8_SNORM (ES 3-class profiles) | ❌ | ❌ | ❌ | Signed bytes upload and sample in [-1,1]; ES 2/WebGL 1 reject clearly |
| Rgba1010102 | 4 | ❌ | ❌ | ❌ | ❌ | 10-bit per channel; requires GL_RGB10_A2 / VK_FORMAT_A2B10G10R10_UNORM_PACK32 |
| Rg32 | 4 | ❌ | ❌ | ❌ | — | 16-bit per channel RG; requires GL_RG16 / VK_FORMAT_R16G16_UNORM |
| Rgba64 | 8 | ❌ | ❌ | ❌ | — | 16-bit per channel RGBA; requires GL_RGBA16 / VK_FORMAT_R16G16B16A16_UNORM |
| Alpha8 | 1 | ❌ | ❌ | ❌ | ❌ | Single alpha channel; requires GL_R8 + swizzle or GL_ALPHA |
| Single | 4 | ❌ | ❌ | ❌ | — | 32-bit float R; requires GL_R32F / VK_FORMAT_R32_SFLOAT |
| Vector2 | 8 | ❌ | ❌ | ❌ | — | 32-bit float RG; requires GL_RG32F |
| Vector4 | 16 | ❌ | ❌ | ❌ | — | 32-bit float RGBA; requires GL_RGBA32F |
| HalfSingle | 2 | ❌ | ❌ | ❌ | — | 16-bit half float R; requires GL_R16F |
| HalfVector2 | 4 | ❌ | ❌ | ❌ | — | 16-bit half float RG; requires GL_RG16F |
| HalfVector4 | 8 | ❌ | ❌ | ❌ | — | 16-bit half float RGBA; requires GL_RGBA16F — key format for HDR (CNAEXT N11) |
| HdrBlendable | 8 | ❌ | ❌ | ❌ | — | Alias for RGBA16F in FNA; same as HalfVector4 |
| ColorBgraEXT | 4 | ❌ | ❌ | ❌ | ❌ | XNA3 legacy ABGR byte order; stored as RGBA8, byte order not respected |
| **ColorSrgbEXT** | 4 | ❌ | ❌ | ❌ | ❌ | Stored as linear RGBA8; sRGB flag silently ignored; requires GL_SRGB8_ALPHA8 / VK_FORMAT_R8G8B8A8_SRGB |
| Dxt5SrgbEXT | 1 | ❌ | ❌ | ❌ | ❌ | CPU decompression path (see Dxt5 above) drops the sRGB flag |
| Bc7EXT | 1 | ❌ | ❌ | ❌ | ❌ | No BC7 decode/upload path at all (unlike Dxt1/3/5's CPU-decompress fallback) |
| Bc7SrgbEXT | 1 | ❌ | ❌ | ❌ | ❌ | Same as Bc7EXT, plus sRGB |
| ByteEXT | 1 | ❌ | ❌ | ❌ | ❌ | Single unsigned byte channel; requires GL_R8 |
| UShortEXT | 2 | ❌ | ❌ | ❌ | ❌ | Single unsigned short channel; requires GL_R16 |

---

## Renderer GPU format summary

| Renderer | Texture2D | Texture3D | TextureCube | RenderTarget (color) | RenderTarget (depth) |
|---------|-----------|-----------|-------------|----------------------|----------------------|
| EasyGL | GL_RGBA8; GL_RGBA8_SNORM for NormalizedByte4 on ES 3-class profiles | GL_RGBA8 | GL_RGBA8 | GL_RGBA8 | GL_DEPTH_COMPONENT24 |
| Vulkan | VK_FORMAT_R8G8B8A8_UNORM | VK_FORMAT_R8G8B8A8_UNORM | VK_FORMAT_R8G8B8A8_UNORM | swapchain format¹ | D32_SFLOAT or D24_UNORM_S8² |
| Bgfx | RGBA8 | RGBA8 | RGBA8 | RGBA8 | D24S8 |
| SDL_Renderer | SDL_PIXELFORMAT_RGBA32 | — | — | SDL_PIXELFORMAT_RGBA32 | — |

¹ Vulkan swapchain prefers `VK_FORMAT_B8G8R8A8_UNORM`; falls back to first format reported by the
  device. Previously preferred `VK_FORMAT_B8G8R8A8_SRGB` — fixed by Task 284 (see the "Color format
  mapping" section above): FNA's `SurfaceFormat.Color` is linear, not sRGB, so an SRGB swapchain
  applied an unwanted gamma encode to every presented pixel.
² Vulkan depth format is selected at init: tries D32_SFLOAT → D32_SFLOAT_S8_UINT → D24_UNORM_S8_UINT.

---

## What needs to be fixed (priority order)

### High priority

| # | Format(s) | Required GL/Vulkan format | Work needed |
|---|-----------|--------------------------|------------|
| T176a | ColorSrgbEXT | GL_SRGB8_ALPHA8 / VK_FORMAT_R8G8B8A8_SRGB | Forward `surfaceFormat` to renderers; add sRGB branch |
| T176b | HalfVector4 / HdrBlendable | GL_RGBA16F / VK_FORMAT_R16G16B16A16_SFLOAT | Required for CNAEXT HDR render targets (N11 EasyGL, N12 other renderers) |
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
- `NormalizedByte2` and `NormalizedByte4` outside EasyGL's ES 3-class Texture2D path — bump-map
  formats.
- `Dxt5SrgbEXT` — follows from fixing DXT native upload + sRGB.
- `Bc7EXT`/`Bc7SrgbEXT` — no existing CPU-decompress fallback (unlike Dxt1/3/5), so this needs a
  real BC7 decoder before it can work at all, even via the CPU-decompress path.
- `ByteEXT`/`UShortEXT` — single-channel formats; rarely used outside custom shaders.

---

## Implementation notes

To properly support non-Color formats, the following refactors are needed:

1. **Pass `SurfaceFormat` through `IGraphicsRenderer::CreateTexture`** — done through
   `ImageData::surfaceFormat`; each renderer must still opt a format into its verified mapping.

2. **Add a format dispatch function in each renderer** — map `SurfaceFormat` enum to
   the native `metagl::InternalFormat` / `VkFormat` / `bgfx::TextureFormat`.

3. **Keep the RGBA8 path as the default fallback** — unknown or unsupported formats
   should throw `std::runtime_error` rather than silently using RGBA8.

4. **DXT native upload (EasyGL/Vulkan)** — requires `GL_EXT_texture_compression_s3tc`
   or `VK_EXT_texture_compression_bc` to be present; must query caps at init.
