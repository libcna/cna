# Skia `SurfaceFormat` contract matrix

Status: normative SKIA-134 contract; SKIA-135–137 Texture2D routes promoted

This matrix fixes the byte and sampling contract for every public format.
It is based on CNA's `SurfaceFormat`, `Texture::GetBlockSizeSquaredEXT` and
`Texture::GetFormatSizeEXT`, the CNA packed-vector implementations, and FNA/FNA3D's OpenGL
transfer mappings. `Skia representation` names the pinned CPU-raster building block. SKIA-135–137
now enable `Bgr565`, `Bgra4444`, `Rgba1010102`, `Rg32`, `Rgba64`, `Alpha8`, `ColorBgraEXT`,
`ColorSrgbEXT`, `ByteEXT`, and `UShortEXT` for Skia `Texture2D` only after their transfer and pixel
gates passed. Every other non-`Color` texture row remains refused until its owner passes;
non-`Color` render targets remain independently refused pending SKIA-142.

All multi-byte words and IEEE values below use the little-endian host layout required by the
pinned Skia raster artifact. A future big-endian build must add explicit byte conversion or refuse
these routes; it may not reinterpret the words. Missing sampled colour channels are exactly zero
and missing alpha is exactly one unless a row says otherwise. UNORM maps the full unsigned integer
range to `[0, 1]`; SNORM maps signed bytes to `[-1, 1]` with the format-specific endpoint policy
locked by SKIA-139. sRGB conversion applies to RGB only; alpha remains linear.

`FNA/Skia RT decision` distinguishes FNA texture semantics from this backend's deliberate target
policy. FNA makes the listed formats renderable and coerces the others to `Color`; Skia instead
will reject a non-renderable requested format before allocation in SKIA-142, because silently
changing `RenderTarget2D::Format` would make exact transfer/readback impossible.

| Ordinal | SurfaceFormat | Block texels | Payload bytes | Exact CNA transfer layout | FNA/EasyGL sampled value | Pinned Skia representation | FNA/Skia RT decision | Promotion route | Owner |
|---:|---|---:|---:|---|---|---|---|---|---|
| 0 | `Color` | 1 | 4 | bytes R8, G8, B8, A8 | RGBA UNORM | `kRGBA_8888`, canonical straight-byte shadow and declared-alpha views | renderable; direct raster surface | supported | SKIA-143 |
| 1 | `Bgr565` | 1 | 2 | LE u16 R15:11, G10:5, B4:0 | RGB UNORM, A=1 | promoted Texture2D `kRGB_565` exact word | FNA coerces to Color; Skia refuses target | direct | SKIA-135 |
| 2 | `Bgra5551` | 1 | 2 | LE u16 A15, R14:10, G9:5, B4:0 | RGBA UNORM | raw u16 shadow plus decoded RGBA working image | FNA coerces to Color; Skia refuses target | conversion-shadow | SKIA-139 |
| 3 | `Bgra4444` | 1 | 2 | LE u16 A15:12, R11:8, G7:4, B3:0 | RGBA UNORM | promoted Texture2D raw u16 shadow plus decoded RGBA working image; `kARGB_4444` is not layout-compatible | FNA coerces to Color; Skia refuses target | conversion-shadow | SKIA-135 |
| 4 | `Dxt1` | 16 | 8 | one BC1 block per 4x4 texels | RGB UNORM or BC1 one-bit-alpha RGBA | exact block shadow plus bounded RGBA8 decoder | FNA coerces to Color; Skia refuses target | compressed-shadow | SKIA-140 |
| 5 | `Dxt3` | 16 | 16 | one BC2 block per 4x4 texels | RGBA UNORM with explicit 4-bit alpha | exact block shadow plus bounded RGBA8 decoder | FNA coerces to Color; Skia refuses target | compressed-shadow | SKIA-140 |
| 6 | `Dxt5` | 16 | 16 | one BC3 block per 4x4 texels | RGBA UNORM with interpolated alpha | exact block shadow plus bounded RGBA8 decoder | FNA coerces to Color; Skia refuses target | compressed-shadow | SKIA-140 |
| 7 | `NormalizedByte2` | 1 | 2 | bytes X s8, Y s8 | RG SNORM, B=0, A=1 | exact byte shadow plus decoded `kRGBA_F32` working image | FNA coerces to Color; Skia refuses target | conversion-shadow | SKIA-139 |
| 8 | `NormalizedByte4` | 1 | 4 | bytes X s8, Y s8, Z s8, W s8 | RGBA SNORM | exact byte shadow plus decoded `kRGBA_F32` working image | FNA coerces to Color; Skia refuses target | conversion-shadow | SKIA-139 |
| 9 | `Rgba1010102` | 1 | 4 | LE u32 A31:30, B29:20, G19:10, R9:0 | RGBA UNORM | promoted Texture2D `kRGBA_1010102` exact word | renderable; Skia target remains refused pending SKIA-142 | direct | SKIA-135 |
| 10 | `Rg32` | 1 | 4 | LE u16 R, then LE u16 G | RG UNORM, B=0, A=1 | promoted Texture2D `kR16G16_unorm` exact words | renderable; Skia target remains refused pending SKIA-142 | direct | SKIA-137 |
| 11 | `Rgba64` | 1 | 8 | LE u16 R, G, B, A | RGBA UNORM | promoted Texture2D `kR16G16B16A16_unorm` exact words | renderable; Skia target remains refused pending SKIA-142 | direct | SKIA-137 |
| 12 | `Alpha8` | 1 | 1 | one byte A8 | R=0, G=0, B=0, A UNORM | promoted Texture2D `kAlpha_8` exact byte | FNA coerces to Color; Skia refuses target | direct-texture-only | SKIA-137 |
| 13 | `Single` | 1 | 4 | LE IEEE binary32 R | R float, G=0, B=0, A=1 | exact bit shadow plus expanded `kRGBA_F32` working image | renderable; conversion raster candidate | conversion-shadow | SKIA-138 |
| 14 | `Vector2` | 1 | 8 | LE IEEE binary32 R, G | RG float, B=0, A=1 | exact bit shadow plus expanded `kRGBA_F32` working image | renderable; conversion raster candidate | conversion-shadow | SKIA-138 |
| 15 | `Vector4` | 1 | 16 | LE IEEE binary32 R, G, B, A | RGBA float | `kRGBA_F32` exact words | renderable; direct raster candidate | direct | SKIA-138 |
| 16 | `HalfSingle` | 1 | 2 | LE IEEE binary16 R | R float, G=0, B=0, A=1 | `kR16_float` exact word | renderable; direct raster candidate | direct | SKIA-138 |
| 17 | `HalfVector2` | 1 | 4 | LE IEEE binary16 R, G | RG float, B=0, A=1 | `kR16G16_float` exact words | renderable; direct raster candidate | direct | SKIA-138 |
| 18 | `HalfVector4` | 1 | 8 | LE IEEE binary16 R, G, B, A | RGBA float | `kRGBA_F16` exact words | renderable; direct raster candidate | direct | SKIA-138 |
| 19 | `HdrBlendable` | 1 | 8 | LE IEEE binary16 R, G, B, A | RGBA float with HDR blending | `kRGBA_F16` exact words | renderable; direct raster candidate with blend oracle | direct | SKIA-138 |
| 20 | `ColorBgraEXT` | 1 | 4 | bytes B8, G8, R8, A8 | RGBA UNORM after B/R mapping | promoted Texture2D `kBGRA_8888` exact bytes | FNA coerces to Color; Skia refuses target | direct-texture-only | SKIA-136 |
| 21 | `ColorSrgbEXT` | 1 | 4 | bytes sR8, sG8, sB8, A8 | linear RGB after one sRGB decode, A UNORM | promoted Texture2D `kSRGBA_8888` plus linear-sRGB working metadata | FNA conditionally renderable; Skia target remains refused pending SKIA-142 | colour-space | SKIA-136 |
| 22 | `Dxt5SrgbEXT` | 16 | 16 | one BC3 block per 4x4 texels with sRGB RGB | linear RGB after BC3 decode and one sRGB decode, A UNORM | exact block shadow plus bounded RGBA8 sRGB decoder | FNA coerces to Color; Skia refuses target | compressed-shadow | SKIA-140 |
| 23 | `Bc7EXT` | 16 | 16 | one BC7 block per 4x4 texels | RGBA UNORM | exact block shadow plus license-compatible bounded decoder | FNA coerces to Color; Skia refuses target | decoder-required | SKIA-141 |
| 24 | `Bc7SrgbEXT` | 16 | 16 | one BC7 block per 4x4 texels with sRGB RGB | linear RGB after BC7 decode and one sRGB decode, A UNORM | exact block shadow plus license-compatible bounded sRGB decoder | FNA coerces to Color; Skia refuses target | decoder-required | SKIA-141 |
| 25 | `ByteEXT` | 1 | 1 | one byte R8 | R UNORM, G=0, B=0, A=1 | promoted Texture2D `kR8_unorm` exact byte | renderable; Skia target remains refused pending SKIA-142 | direct | SKIA-137 |
| 26 | `UShortEXT` | 1 | 2 | LE u16 R | R UNORM, G=0, B=0, A=1 | promoted Texture2D `kR16_unorm` exact word | renderable; Skia target remains refused pending SKIA-142 | direct | SKIA-137 |

## Decisions fixed by SKIA-134

- Public `SetData`/`GetData` always preserve the exact transfer payload, including compressed
  blocks and NaN payload bits. A decoded or renderable working image never replaces that truth
  unless the render target itself has produced new pixels.
- `Bgra4444` requires conversion despite the similarly named Skia type: CNA's alpha nibble is the
  most significant nibble, while `kARGB_4444` places alpha in the least significant nibble.
- `Bgra5551`, both SNORM formats, `Single`, and `Vector2` also need shadows because the pinned Skia
  public colour types have no exact bit layout and sampled-channel semantics simultaneously.
- Compressed formats are texture-only. NPOT dimensions still use `ceil(width/4) * ceil(height/4)`
  blocks per level; the old enum comment's multiple-of-four wording does not justify dropping edge
  texels or crossing mip boundaries.
- BC7 stays refused unless SKIA-141 selects a bounded, license-compatible decoder. No other codec,
  zero fill, or stale RGBA8 content is an acceptable fallback.
- Format promotion is per route. Texture sampling support never implies renderability, and
  renderability never implies backbuffer-format support.
- Pinned `kSRGBA_8888` itself decodes encoded RGB while gathering. Its Skia colour-space metadata
  therefore describes the post-gather linear-sRGB working values, not the encoded storage. A
  linear destination performs no second decode; an explicit sRGB destination encodes exactly once.
- Pinned alpha/R/RG UNORM colour types provide the required missing-channel constants directly:
  Alpha8 gathers zero RGB, while R8/R16/RG16 gather zero absent colour channels and opaque alpha.
  SKIA-137 therefore needs no shader swizzle or expanded conversion shadow for these Texture2D
  routes; typed transfers still serialize every multi-byte word explicitly little-endian.
