# Skia successor contract matrix

Status: active for SKIA-117 and the SKIA-115–170 expansion

This inventory is the routing source for the post-baseline Skia work. `Baseline` describes the
release-gated SKIA-1–114 behavior, not an aspiration. `Successor task` owns the next decision or
implementation. A completed successor may still retain refusal when public Skia cannot reproduce
the CNA/FNA contract exactly; it must never turn a missing route into a silent approximation.

Baseline values are `supported`, `bounded`, `fallback`, `refused`, `transfer-only`, or
`not-selected`. The validator derives every enum row from the current headers and rejects missing,
stale, duplicated, malformed, or unrouted entries.

SKIA-134's exact byte, sampling, pinned-raster and renderability decisions are maintained in the
checked [`skia-surface-format-matrix.md`](skia-surface-format-matrix.md). This routing table remains
the compact successor index; the format matrix is the normative input for SKIA-135–143.

| ID | Contract | Baseline | Successor task | Existing evidence / required acceptance |
|---|---|---|---|---|
| `FMT-Color` | RGBA8 transfer, sampling and render target. | supported | SKIA-143 | Existing exhaustive transfer, target and XNA-oracle coverage remains the reference. |
| `FMT-Bgr565` | Packed 16-bit BGR transfer, normalized sampling and renderability. | refused | SKIA-135 | Promoted for Texture2D: exact little-endian `kRGB_565` words, native mips, sampling and quantization pass; targets stay refused. |
| `FMT-Bgra5551` | Packed 5:5:5:1 transfer and alpha quantization. | refused | SKIA-139 | Promoted for Texture2D through an exact LE A:R:G:B shadow, RGBA32F working image, native-component mips and sampled alpha; targets stay refused. |
| `FMT-Bgra4444` | Packed 4:4:4:4 transfer, sampling and renderability. | refused | SKIA-135 | Promoted for Texture2D through exact A:R:G:B shadow conversion, native mips and sampled pixels; Skia `kARGB_4444` is never mislabelled and targets stay refused. |
| `FMT-Dxt1` | BC1 blocks, optional one-bit alpha, mips and sampled pixels. | refused | SKIA-140 | Preserve compressed blocks while decoding bounded sampling images. |
| `FMT-Dxt3` | BC2 explicit-alpha blocks, mips and sampled pixels. | refused | SKIA-140 | Preserve exact blocks and verify four-bit alpha expansion. |
| `FMT-Dxt5` | BC3 interpolated-alpha blocks, mips and sampled pixels. | refused | SKIA-140 | Preserve exact blocks and verify both alpha interpolation modes. |
| `FMT-NormalizedByte2` | Signed normalized two-channel transfer and raw sampling. | refused | SKIA-139 | Promoted for Texture2D through exact bytes and an opaque RGBA32F view; raw -128/-127 gather as -1, missing B/A are 0/1, and targets stay refused. |
| `FMT-NormalizedByte4` | Signed normalized four-channel transfer and raw sampling. | refused | SKIA-139 | Promoted for Texture2D through exact bytes and a colour-space-free RGBA32F view; canonical signed-integer mips and sampled endpoints pass while targets stay refused. |
| `FMT-Rgba1010102` | Packed 10:10:10:2 transfer, sampling and renderability. | refused | SKIA-135 | Promoted for Texture2D: compatible pinned storage passes exact endian, native mip, sampled colour and two-bit-alpha evidence; SKIA-142 promotes a matching native `kRGBA_1010102` RenderTarget2D. |
| `FMT-Rg32` | Two unsigned 16-bit channels and normalized raw sampling. | refused | SKIA-137 | Promoted for Texture2D: exact LE RG16 transfers/mips use `kR16G16_unorm` and sample B=0/A=1; SKIA-142 promotes a matching native RenderTarget2D. |
| `FMT-Rgba64` | Four unsigned 16-bit channels and normalized raw sampling. | refused | SKIA-137 | Promoted for Texture2D: exact LE RGBA16 transfers/mips use `kR16G16B16A16_unorm`; SKIA-142 promotes a matching native RenderTarget2D. |
| `FMT-Alpha8` | One alpha byte with zero RGB sampling semantics. | refused | SKIA-137 | Promoted for Texture2D: exact A8 storage uses pinned `kAlpha_8`, whose gather supplies zero RGB; targets stay refused. |
| `FMT-Single` | One IEEE float channel and raw sampling. | refused | SKIA-138 | Promoted for Texture2D: exact LE binary32 shadow expands to opaque `kRGBA_F32`; exceptional bits round-trip. SKIA-142 promotes a RenderTarget2D that widens to the same native `kRGBA_F32` surface (no native 1-channel 32-bit-float colour type exists), extracting R at the surface boundary. |
| `FMT-Vector2` | Two IEEE float channels and raw sampling. | refused | SKIA-138 | Promoted for Texture2D: exact LE RG binary32 shadow expands to opaque `kRGBA_F32` with B=0. SKIA-142 promotes a RenderTarget2D on the same widened `kRGBA_F32` surface, extracting R,G at the surface boundary. |
| `FMT-Vector4` | Four IEEE float channels, sampling and HDR operations. | refused | SKIA-138 | Promoted for Texture2D through direct `kRGBA_F32`, exact typed transfers and deterministic float mips; SKIA-142 promotes a matching native RenderTarget2D. |
| `FMT-HalfSingle` | One IEEE half channel and raw sampling. | refused | SKIA-138 | Promoted for Texture2D through direct opaque `kR16_float`, exact half words and explicit exceptional-value mips. |
| `FMT-HalfVector2` | Two IEEE half channels and raw sampling. | refused | SKIA-138 | Promoted for Texture2D through direct opaque `kR16G16_float`; B=0/A=1 sampling and exact transfers pass. |
| `FMT-HalfVector4` | Four IEEE half channels and HDR sampling. | refused | SKIA-138 | Promoted for Texture2D through direct `kRGBA_F16`, exact typed transfers and extended-range sampling; SKIA-142 promotes a matching native RenderTarget2D. |
| `FMT-HdrBlendable` | Blendable HDR half-float surface. | refused | SKIA-138 | Promoted for Texture2D through `kRGBA_F16`; unclamped sampling and bounded public blending pass. SKIA-142 promotes a matching native RenderTarget2D. |
| `FMT-ColorBgraEXT` | BGRA8 XNA3 transfer layout and ordinary color sampling. | refused | SKIA-136 | Promoted for Texture2D: exact B/G/R/A transfer bytes use `kBGRA_8888`, sample with the required B/R mapping, and generate byte-domain mips; targets stay refused. |
| `FMT-ColorSrgbEXT` | RGBA8 sRGB transfer with linear shader sampling. | refused | SKIA-136 | Promoted for Texture2D: exact encoded bytes, linear-light RGB mips, linear alpha, and explicit linear/sRGB destinations prove one decode and at most one re-encode; SKIA-142 promotes a matching native `kSRGBA_8888` RenderTarget2D. |
| `FMT-Dxt5SrgbEXT` | BC3 blocks with sRGB RGB sampling and linear alpha. | refused | SKIA-140 | Combine exact compressed shadow with explicit sRGB decoded image. |
| `FMT-Bc7EXT` | BC7 block transfer, modes and sampled pixels. | refused | SKIA-141 | Requires a bounded license-compatible conformant decoder. |
| `FMT-Bc7SrgbEXT` | BC7 blocks with sRGB RGB sampling. | refused | SKIA-141 | Decoder and color-space path must be proven together. |
| `FMT-ByteEXT` | One unsigned normalized R byte. | refused | SKIA-137 | Promoted for Texture2D: exact R8 transfer/mips use `kR8_unorm`, which supplies zero GB and opaque alpha; SKIA-142 promotes a matching native RenderTarget2D. |
| `FMT-UShortEXT` | One unsigned normalized R 16-bit value. | refused | SKIA-137 | Promoted for Texture2D: exact LE R16 transfer/mips use `kR16_unorm`, which supplies zero GB and opaque alpha; SKIA-142 promotes a matching native RenderTarget2D. |
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
| `FILTER-Linear` | Bilinear texel and linear mip sampling. | supported | SKIA-129 | Exact old magnification/minification pixels remain; a real chain now brackets fractional LOD. |
| `FILTER-Point` | Nearest texel and nearest mip sampling. | supported | SKIA-129 | Exact old coordinate/tile pixels remain; a real chain now selects the nearest level. |
| `FILTER-Anisotropic` | Device anisotropic footprint up to MaxAnisotropy. | fallback | SKIA-165 | Raster stays Linear fallback; Ganesh must prove a distinct device result. |
| `FILTER-LinearMipPoint` | Linear texels and nearest mip level. | supported | SKIA-129 | Affine LOD, exact integer-level, half-tie and public pixel evidence pass. |
| `FILTER-PointMipLinear` | Point texels and interpolation between mip levels. | supported | SKIA-129 | Bounded two-level interpolation and ±1 byte public evidence pass. |
| `FILTER-MinLinearMagPointMipLinear` | Linear minification, point magnification, linear mip blend. | supported | SKIA-129 | Independent min/mag and fractional-LOD public oracles pass. |
| `FILTER-MinLinearMagPointMipPoint` | Linear minification, point magnification, nearest mip. | supported | SKIA-129 | Independent min/mag and nearest-level public oracles pass. |
| `FILTER-MinPointMagLinearMipLinear` | Point minification, linear magnification, linear mip blend. | supported | SKIA-129 | Independent min/mag and fractional-LOD public oracles pass. |
| `FILTER-MinPointMagLinearMipPoint` | Point minification, linear magnification, nearest mip. | supported | SKIA-129 | Independent min/mag and nearest-level public oracles pass. |
| `CAP-ThreeD` | Complete public 3D renderer. | refused | SKIA-170 | Successor 2D/GPU work does not reopen the accepted 3D ADR implicitly. |
| `CAP-DepthStencilBuffer` | Real target depth/stencil attachment. | refused | SKIA-170 | MSAA/Ganesh does not imply depth/stencil support. |
| `CAP-MultiSampleAntiAliasing` | Real sample count above one. | refused | SKIA-164 | Ganesh probe must control reporting; raster remains false. |
| `CAP-MultipleRenderTargets` | More than one simultaneous target with distinct outputs. | refused | SKIA-166–167 | Distinct locations 0–3 are the required gate. |
| `CAP-AnisotropicFiltering` | Real anisotropic device sampling. | fallback | SKIA-165 | Raster fallback is not capability evidence. |
| `CAP-WireFrame` | Public wireframe rasterization. | refused | SKIA-170 | Remains part of rejected 3D geometry scope. |
| `CAP-OcclusionQuery` | Real samples-passed query. | refused | SKIA-170 | Remains part of accepted query refusal. |
| `CAP-CustomEffects` | Complete custom-effect contract represented by the enum. | bounded | SKIA-152–158 | Explicit fragment SkSL v1 is narrower and currently reports false. |
| `CAP-Texture3D` | Persistent volume storage; sampling is a separate contract. | transfer-only | SKIA-144–151 | Baseline true value stays storage-only by design, even though sampling now has explicit evidence (see `SAMPLING-CUBE`/`SAMPLING-VOLUME` below) -- the two remain deliberately separate contracts, not a promotion trigger for this enum. |
| `MIP-TEXTURE2D-CONSTRUCTION` | Mipmapped construction, dimensions, count and zero initialization. | supported | SKIA-125–126 | Checked complete-chain construction and property tests pass. |
| `MIP-TEXTURE2D-TRANSFER` | Full/partial SetData/GetData for each mip. | supported | SKIA-127 | Shared range coverage plus direct odd/NPOT level, byte, lifetime and atomic-failure evidence pass. |
| `MIP-TEXTURE2D-GENERATION` | Dirty descendant generation after upload. | supported | SKIA-128 | Exact odd/NPOT straight-RGBA area-box bytes, dirty-generation counts and explicit-level ownership barriers pass. |
| `MIP-RENDERTARGET2D` | Render, upload, readback and sample mutable target mips. | supported | SKIA-131–132 | Stable surfaces and exact shadows back every level; parent uploads and level-zero passes deterministically regenerate their dirty suffix once at upload or resolve barriers. |
| `MIP-SAMPLING-LOD` | Min/mag and mip-level selection/interpolation. | supported | SKIA-129 | All-nine decomposition, affine rho, NPOT, source/crop, transform, addressing and interpolation evidence pass. |
| `FORMAT-RENDERTARGET` | Per-format renderability independent of texture sampling. | refused | SKIA-142 | The thirteen non-`Color` formats FNA itself reports renderable each construct a real native-format `SkSurface`; every other format refuses transactionally before allocation. |
| `FORMAT-CONTENT` | XNB/DDS level and compressed block preservation. | bounded | SKIA-130–141 | SKIA-130 preserves complete DDS DXT1/DXT3/DXT5 and XNB Color/DXT5 chains as canonical RGBA8, rejects malformed boundaries, and keeps single-level assets single-level; native wider public formats remain SKIA-134–141. |
| `EFFECT-LANGUAGE` | Untagged EasyGL GLSL versus explicit SkSL identity. | bounded | SKIA-152–155 | Existing v1 marker prevents language guessing. |
| `EFFECT-VERTEX` | Custom 2D vertex attributes, transform and varyings. | refused | SKIA-153–154 | SkMesh must pass interpolation and clipping oracles before promotion. |
| `EFFECT-FRAGMENT` | Single-output programmable fragment color. | bounded | SKIA-154–158 | Explicit SkSL v1 works; broader ABI and translated sources remain. |
| `EFFECT-UNIFORMS` | Scalars, vectors, matrices, arrays and shared stage layout. | bounded | SKIA-154–157 | Existing reflected v1 setters are the lower bound. |
| `EFFECT-TEXTURES` | Typed 2D/cube/volume child bindings and sampler state. | bounded | SKIA-150–157 | Cube/volume weak-lifetime children are implemented without type confusion (SKIA-149, reserved `cnaCubeFace0`–`5`/`cnaVolumeAtlas0` children orthogonal to `cnaTexture0`–`7`); the broader ABI (vertex children, wider sampler-state coverage) remains SKIA-150–157. |
| `SAMPLING-CUBE` | Direction-to-face/UV, seams, mips and filtering. | bounded | SKIA-144–151 | Bounded fragment-only direction-to-face sampling with dominant-axis table, seam policy, mip selection and Point/Linear filtering is implemented and oracle-proven end to end through the public `SetTexture(1, TextureCube)` API (SKIA-145/146/149/150, `Skia_CubeVolume_Effect_Binding`, `Skia_CubeVolume_Sampling_Oracle`). |
| `SAMPLING-VOLUME` | 3D coordinate, slices, interpolation, mips and addressing. | bounded | SKIA-144–151 | Bounded fragment-only trilinear interpolation, mip selection, per-axis Clamp/Wrap/Mirror addressing and the 256 MiB atlas resource budget are implemented and oracle-proven end to end through the public `SetTexture(1, Texture3D)` API (SKIA-147/148/149/150, `Skia_CubeVolume_Effect_Binding`, `Skia_CubeVolume_Sampling_Oracle`). |
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
