# Skia successor contract matrix

Status: active for SKIA-117 and the SKIA-115–170 expansion

This inventory is the routing source for the post-baseline Skia work. `Baseline` describes the
release-gated SKIA-1–114 behavior, not an aspiration. `Successor task` owns the next decision or
implementation. A completed successor may still retain refusal when public Skia cannot reproduce
the CNA/FNA contract exactly; it must never turn a missing route into a silent approximation.

Baseline values are `supported`, `bounded`, `fallback`, `refused`, `transfer-only`, or
`not-selected`. The validator derives every enum row from the current headers and rejects missing,
stale, duplicated, malformed, or unrouted entries.

| ID | Contract | Baseline | Successor task | Existing evidence / required acceptance |
|---|---|---|---|---|
| `FMT-Color` | RGBA8 transfer, sampling and render target. | supported | SKIA-143 | Existing exhaustive transfer, target and XNA-oracle coverage remains the reference. |
| `FMT-Bgr565` | Packed 16-bit BGR transfer, normalized sampling and renderability. | refused | SKIA-135 | Map exact little-endian bits to compatible Skia storage and prove quantization. |
| `FMT-Bgra5551` | Packed 5:5:5:1 transfer and alpha quantization. | refused | SKIA-139 | Requires conversion shadow because pinned Skia has no layout-compatible color type. |
| `FMT-Bgra4444` | Packed 4:4:4:4 transfer, sampling and renderability. | refused | SKIA-135 | Map channel order and prove raw round-trip plus rendered output. |
| `FMT-Dxt1` | BC1 blocks, optional one-bit alpha, mips and sampled pixels. | refused | SKIA-140 | Preserve compressed blocks while decoding bounded sampling images. |
| `FMT-Dxt3` | BC2 explicit-alpha blocks, mips and sampled pixels. | refused | SKIA-140 | Preserve exact blocks and verify four-bit alpha expansion. |
| `FMT-Dxt5` | BC3 interpolated-alpha blocks, mips and sampled pixels. | refused | SKIA-140 | Preserve exact blocks and verify both alpha interpolation modes. |
| `FMT-NormalizedByte2` | Signed normalized two-channel transfer and raw sampling. | refused | SKIA-139 | Explicit signed normalization and missing-channel defaults require byte oracles. |
| `FMT-NormalizedByte4` | Signed normalized four-channel transfer and raw sampling. | refused | SKIA-139 | Explicit signed normalization must avoid color-space conversion. |
| `FMT-Rgba1010102` | Packed 10:10:10:2 transfer, sampling and renderability. | refused | SKIA-135 | Pinned Skia exposes compatible storage; prove endian and alpha rounding. |
| `FMT-Rg32` | Two unsigned 16-bit channels and normalized raw sampling. | refused | SKIA-137 | Use compatible RG16 storage or exact conversion shadow. |
| `FMT-Rgba64` | Four unsigned 16-bit channels and normalized raw sampling. | refused | SKIA-137 | Use compatible RGBA16 storage and preserve original transfer words. |
| `FMT-Alpha8` | One alpha byte with zero RGB sampling semantics. | refused | SKIA-137 | Pinned Alpha8 path needs exact swizzle and target policy evidence. |
| `FMT-Single` | One IEEE float channel and raw sampling. | refused | SKIA-138 | Preserve bit patterns and define NaN, infinity and renderability. |
| `FMT-Vector2` | Two IEEE float channels and raw sampling. | refused | SKIA-138 | Preserve RG float layout without color management. |
| `FMT-Vector4` | Four IEEE float channels, sampling and HDR operations. | refused | SKIA-138 | Use RGBA_F32 where supported and bound precision/readback behavior. |
| `FMT-HalfSingle` | One IEEE half channel and raw sampling. | refused | SKIA-138 | Preserve half bits and explicit R-channel semantics. |
| `FMT-HalfVector2` | Two IEEE half channels and raw sampling. | refused | SKIA-138 | Use R16G16 float storage where compatible. |
| `FMT-HalfVector4` | Four IEEE half channels and HDR sampling. | refused | SKIA-138 | Use RGBA_F16 storage with explicit alpha/color-space policy. |
| `FMT-HdrBlendable` | Blendable HDR half-float surface. | refused | SKIA-138 | Must separately prove transfer, blending and render-target support. |
| `FMT-ColorBgraEXT` | BGRA8 XNA3 transfer layout and ordinary color sampling. | refused | SKIA-136 | Map BGRA storage without red/blue swaps or changed public bytes. |
| `FMT-ColorSrgbEXT` | RGBA8 sRGB transfer with linear shader sampling. | refused | SKIA-136 | Prove one and only one transfer-function conversion. |
| `FMT-Dxt5SrgbEXT` | BC3 blocks with sRGB RGB sampling and linear alpha. | refused | SKIA-140 | Combine exact compressed shadow with explicit sRGB decoded image. |
| `FMT-Bc7EXT` | BC7 block transfer, modes and sampled pixels. | refused | SKIA-141 | Requires a bounded license-compatible conformant decoder. |
| `FMT-Bc7SrgbEXT` | BC7 blocks with sRGB RGB sampling. | refused | SKIA-141 | Decoder and color-space path must be proven together. |
| `FMT-ByteEXT` | One unsigned normalized R byte. | refused | SKIA-137 | Use raw R8 semantics rather than grayscale/color-managed sampling. |
| `FMT-UShortEXT` | One unsigned normalized R 16-bit value. | refused | SKIA-137 | Use raw R16 semantics with exact little-endian transfer. |
| `BLEND-One` | Unit source/destination multiplier. | bounded | SKIA-120 | Public scalar-oracle pixels cover all four selector positions in SKIA-123. |
| `BLEND-Zero` | Zero source/destination multiplier. | bounded | SKIA-120 | Public scalar-oracle pixels cover all four selector positions in SKIA-123. |
| `BLEND-SourceColor` | Per-channel source-color multiplier. | bounded | SKIA-120 | Public generated pixels use the declared premultiplied working convention. |
| `BLEND-InverseSourceColor` | Inverse source-color multiplier. | bounded | SKIA-120 | Public generated pixels use the declared premultiplied working convention. |
| `BLEND-SourceAlpha` | Source-alpha multiplier. | bounded | SKIA-120 | Translucent public pixels distinguish source alpha from RGB in all positions. |
| `BLEND-InverseSourceAlpha` | Inverse source-alpha multiplier. | bounded | SKIA-120 | Translucent public pixels distinguish inverse source alpha in all positions. |
| `BLEND-DestinationColor` | Per-channel destination-color multiplier. | bounded | SKIA-120 | Public corpus reads the active destination in source and destination terms. |
| `BLEND-InverseDestinationColor` | Inverse destination-color multiplier. | bounded | SKIA-120 | Public corpus reads inverse active destination components in all positions. |
| `BLEND-DestinationAlpha` | Destination-alpha multiplier. | bounded | SKIA-120 | Public alpha-position pixels match the premultiplied target convention. |
| `BLEND-InverseDestinationAlpha` | Inverse destination-alpha multiplier. | bounded | SKIA-120 | Public alpha-position pixels match the independent scalar oracle. |
| `BLEND-BlendFactor` | GraphicsDevice blend constant multiplier. | refused | SKIA-121 | Baseline refusal is superseded by the SKIA-124 promoted raster route: baked and live A→B→A constants pass. |
| `BLEND-InverseBlendFactor` | Inverse GraphicsDevice blend constant. | refused | SKIA-121 | Baseline refusal is superseded by the SKIA-124 promoted raster route and independent public oracle coverage. |
| `BLEND-SourceAlphaSaturation` | min(source alpha, inverse destination alpha) RGB factor. | bounded | SKIA-120 | Public corpus covers it in all four positions against the independent oracle. |
| `BLENDFUNC-Add` | Add weighted source and destination. | bounded | SKIA-120 | Public color/alpha scenes cover arbitrary weighted terms. |
| `BLENDFUNC-Subtract` | Subtract destination term from source term. | refused | SKIA-120 | Public color/alpha scenes prove clamping against the scalar oracle. |
| `BLENDFUNC-ReverseSubtract` | Subtract source term from destination term. | refused | SKIA-120 | Public color/alpha scenes prove clamping against the scalar oracle. |
| `BLENDFUNC-Max` | Componentwise maximum of source and destination; EasyGL/OpenGL ignores factors. | refused | SKIA-120 | Public color/alpha scenes prove the factor-independent EasyGL equation. |
| `BLENDFUNC-Min` | Componentwise minimum of source and destination; EasyGL/OpenGL ignores factors. | refused | SKIA-120 | Public color/alpha scenes prove the factor-independent EasyGL equation. |
| `FILTER-Linear` | Bilinear texel sampling without mip selection. | supported | SKIA-129 | Preserve existing exact magnification/minification pixels. |
| `FILTER-Point` | Nearest texel sampling without mip selection. | supported | SKIA-129 | Preserve existing exact coordinate and tile-mode pixels. |
| `FILTER-Anisotropic` | Device anisotropic footprint up to MaxAnisotropy. | fallback | SKIA-165 | Raster stays Linear fallback; Ganesh must prove a distinct device result. |
| `FILTER-LinearMipPoint` | Linear texels and nearest mip level. | refused | SKIA-129 | Requires mutable mip chain plus exact derivative/LOD selection. |
| `FILTER-PointMipLinear` | Point texels and interpolation between mip levels. | refused | SKIA-129 | Requires mutable mip chain plus exact derivative/LOD selection. |
| `FILTER-MinLinearMagPointMipLinear` | Linear minification, point magnification, linear mip blend. | refused | SKIA-129 | Min/mag split and mip blend require separate scale oracles. |
| `FILTER-MinLinearMagPointMipPoint` | Linear minification, point magnification, nearest mip. | refused | SKIA-129 | Min/mag split and nearest mip require separate scale oracles. |
| `FILTER-MinPointMagLinearMipLinear` | Point minification, linear magnification, linear mip blend. | refused | SKIA-129 | Min/mag split and mip blend require separate scale oracles. |
| `FILTER-MinPointMagLinearMipPoint` | Point minification, linear magnification, nearest mip. | refused | SKIA-129 | Min/mag split and nearest mip require separate scale oracles. |
| `CAP-ThreeD` | Complete public 3D renderer. | refused | SKIA-170 | Successor 2D/GPU work does not reopen the accepted 3D ADR implicitly. |
| `CAP-DepthStencilBuffer` | Real target depth/stencil attachment. | refused | SKIA-170 | MSAA/Ganesh does not imply depth/stencil support. |
| `CAP-MultiSampleAntiAliasing` | Real sample count above one. | refused | SKIA-164 | Ganesh probe must control reporting; raster remains false. |
| `CAP-MultipleRenderTargets` | More than one simultaneous target with distinct outputs. | refused | SKIA-166–167 | Distinct locations 0–3 are the required gate. |
| `CAP-AnisotropicFiltering` | Real anisotropic device sampling. | fallback | SKIA-165 | Raster fallback is not capability evidence. |
| `CAP-WireFrame` | Public wireframe rasterization. | refused | SKIA-170 | Remains part of rejected 3D geometry scope. |
| `CAP-OcclusionQuery` | Real samples-passed query. | refused | SKIA-170 | Remains part of accepted query refusal. |
| `CAP-CustomEffects` | Complete custom-effect contract represented by the enum. | bounded | SKIA-152–158 | Explicit fragment SkSL v1 is narrower and currently reports false. |
| `CAP-Texture3D` | Persistent volume storage; sampling is a separate contract. | transfer-only | SKIA-144–151 | Baseline true value stays storage-only until sampling has explicit evidence. |
| `MIP-TEXTURE2D-CONSTRUCTION` | Mipmapped construction, dimensions, count and zero initialization. | refused | SKIA-125–126 | The baseline refusal is superseded by the checked complete-chain construction and property tests. |
| `MIP-TEXTURE2D-TRANSFER` | Full/partial SetData/GetData for each mip. | refused | SKIA-127 | The baseline refusal is superseded by shared range coverage plus direct odd/NPOT level, byte, lifetime and atomic-failure evidence. |
| `MIP-TEXTURE2D-GENERATION` | Dirty descendant generation after upload. | refused | SKIA-128 | Baseline refusal is superseded by exact odd/NPOT straight-RGBA area-box bytes, dirty-generation counts, and explicit-level ownership barriers. |
| `MIP-RENDERTARGET2D` | Render, upload, readback and sample mutable target mips. | refused | SKIA-131–132 | Existing target policy proves atomic refusal and recovery. |
| `MIP-SAMPLING-LOD` | Min/mag and mip-level selection/interpolation. | refused | SKIA-129 | Scale-transform oracle must distinguish every TextureFilter value. |
| `FORMAT-RENDERTARGET` | Per-format renderability independent of texture sampling. | refused | SKIA-142 | Every enum needs direct route or pre-allocation refusal. |
| `FORMAT-CONTENT` | XNB/DDS level and compressed block preservation. | bounded | SKIA-130–141 | Existing Color DDS path and cube content fixtures are reusable evidence. |
| `EFFECT-LANGUAGE` | Untagged EasyGL GLSL versus explicit SkSL identity. | bounded | SKIA-152–155 | Existing v1 marker prevents language guessing. |
| `EFFECT-VERTEX` | Custom 2D vertex attributes, transform and varyings. | refused | SKIA-153–154 | SkMesh must pass interpolation and clipping oracles before promotion. |
| `EFFECT-FRAGMENT` | Single-output programmable fragment color. | bounded | SKIA-154–158 | Explicit SkSL v1 works; broader ABI and translated sources remain. |
| `EFFECT-UNIFORMS` | Scalars, vectors, matrices, arrays and shared stage layout. | bounded | SKIA-154–157 | Existing reflected v1 setters are the lower bound. |
| `EFFECT-TEXTURES` | Typed 2D/cube/volume child bindings and sampler state. | bounded | SKIA-149–157 | Existing weak 2D children must extend without type confusion. |
| `SAMPLING-CUBE` | Direction-to-face/UV, seams, mips and filtering. | refused | SKIA-144–146 | Six-face CPU storage and target snapshots already exist. |
| `SAMPLING-VOLUME` | 3D coordinate, slices, interpolation, mips and addressing. | refused | SKIA-144–148 | CPU voxel/mip storage already exists. |
| `MSAA-BACKBUFFER` | Applied samples, resolve, presentation, resize and readback. | refused | SKIA-159–164 | Raster clamps zero; Ganesh device probe is mandatory. |
| `MSAA-TARGET` | Per-target samples, resolve, sampling and readback. | refused | SKIA-164 | Target properties must report applied count and preserve failure atomicity. |
| `ANISO-RASTER` | True raster anisotropy. | fallback | SKIA-165 | Pinned raster image shader explicitly degrades to Linear/mip fallback. |
| `ANISO-GPU` | Ganesh device anisotropy and clamping. | not-selected | SKIA-159–165 | Requires GPU artifact, mips and a minification oracle. |
| `MRT-DISTINCT` | Shader outputs 0–3 with independent values and masks. | refused | SKIA-166–167 | Baseline proof shows one SkCanvas result cannot recover missing outputs. |
| `MRT-ATOMIC` | Multi-target validation, binding and failure ordering. | supported | SKIA-166–168 | Existing rejection suite is the regression lower bound. |
| `GPU-OWNERSHIP` | SDL GL context, Ganesh context/surface and destruction order. | not-selected | SKIA-159–162 | Surface-mode ADR supplies the six reopening requirements. |
| `GPU-PARITY` | Raster/Ganesh pixels, lifecycle, state and recovery. | not-selected | SKIA-163 | Current XNA oracle and API-contract corpus must run in both modes. |
| `RESOURCE-BUDGET` | Checked storage, compiler and cache limits across successor features. | bounded | SKIA-118 | Existing 16K-axis/256-MiB resource and SkSL limits are the floor. |
| `ORACLE-CROSSFEATURE` | Combined mip/format/blend/effect/sampling/GPU interactions. | refused | SKIA-118–169 | Add minimal discriminating scenes rather than blanket image tolerances. |
