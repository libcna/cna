# SurfaceFormat Backend Support — CNA

> Generated from source inspection against Tasks 174.
> Covers: EasyGL, Vulkan, Bgfx, SDL_Renderer backends.

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
| **ColorSrgb** | 4 | ❌ | ❌ | ❌ | ❌ | Stored as linear RGBA8; sRGB flag silently ignored; requires GL_SRGB8_ALPHA8 / VK_FORMAT_R8G8B8A8_SRGB |
| Bgr565Srgb | 2 | ❌ | ❌ | ❌ | ❌ | Neither color layout nor sRGB implemented |
| Bgra5551Srgb | 2 | ❌ | ❌ | ❌ | ❌ | Same |
| Bgra4444Srgb | 2 | ❌ | ❌ | ❌ | ❌ | Same |
| Dxt1Srgb | 0.5 | ❌ | ❌ | ❌ | ❌ | CPU decompression path drops sRGB flag |
| Dxt3Srgb | 1 | ❌ | ❌ | ❌ | ❌ | Same |
| Dxt5Srgb | 1 | ❌ | ❌ | ❌ | ❌ | Same |

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
| T176a | ColorSrgb | GL_SRGB8_ALPHA8 / VK_FORMAT_R8G8B8A8_SRGB | Forward `surfaceFormat` to backends; add sRGB branch |
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

- `Bgr565`, `Bgra5551`, `Bgra4444` and their sRGB variants — uncommon in modern use.
- `NormalizedByte2/4` — bump-map formats; rarely used.
- `Dxt1Srgb/Dxt3Srgb/Dxt5Srgb` — follow from fixing DXT native upload + sRGB.

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
