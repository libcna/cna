# Graphics renderer feature matrix — established GPU paths and Skia raster 2D

Master, up-to-date cross-renderer feature matrix for CNA's established renderers, written for
Task 451 (Phase 51), extended 2026-07-14 with a `DIRECTX11` column (`../plan_dx.md`, Phase DX11
`DX-96`), then a `DIRECTX12` column (Phase DX12 `DX-115`) the same day, then a `DIRECTX9` column
(`../plan_dx9.md`, `D9-130`) 2026-07-15, once each renderer's feature set was broad enough for a
meaningful row-by-row comparison. The experimental WebGPU renderer is intentionally tracked
separately in [`webgpu-renderer.md`](webgpu-renderer.md) and `../plan_webgpu.md` until its feature
surface is broad enough for meaningful parity columns. The experimental LLGL renderer
([`llgl-renderer.md`](llgl-renderer.md), `../plan_llgl.md`) is tracked separately for the same
reason — its verified surface is a 2D baseline on one of its two renderer modules, which would be
almost entirely ⬜ as a column here.

**A `DIRECTX11`/`DIRECTX12` cell means "verified through Wine+DXVK/vkd3d-proton on this dev machine's real
GPU," not "verified on real Windows hardware"** — see `docs/directx11-renderer.md`/`docs/directx12-renderer.md`'s
own "Known limitations" for the real-Windows gate (`plan_dx.md` `DX-90`/`DX-91` for D3D11, `DX-114`
for D3D12) that still applies on top of every ✅ below. **`DIRECTX12`'s own *routine CTest* checks are all
off-screen** — swap-chain creation and `Present()`/back-buffer rendering are real and proven through a
live window (`plan_dx.md` `DX-102`/`DX-116`), but only via a separate manual diagnostic through a
Proton-managed launch (`scripts/run-proton-vkd3d.sh`), not the routine `DirectX12_Smoke` CTest — see
`docs/directx12-renderer.md`. So a `DIRECTX12` ✅ below means "real off-screen GPU pixel/behavior proof,"
unless the row is specifically about presentation itself. A cell is only marked ✅ if a real,
GPU-facing pixel/behavior check actually exercised it; anything implemented but not independently
pixel-tested is marked 🟨, and anything not attempted/not built at all is marked ⬜ (distinct from
this doc's own ❌, which means "tested and found to genuinely not work").

**The `DIRECTX9` column measures something narrower and stricter than the other columns: pixel-for-pixel
indistinguishability from the real XNA 4.0 runtime**, not just "feature works." A `DIRECTX9` ✅ means the
feature is exercised by `tools/xna-oracle/`'s checked-in scene corpus (`D9-120`, `D3D9_XNA_Diff`
CTest) and its render is byte-identical (`--tolerance 0`) to the real XNA 4.0 runtime's own render of
the same scene — a strictly stronger bar than any other column's ✅. Like `DIRECTX11`/`DIRECTX12`, this is
"verified through Wine+DXVK on this dev machine's real GPU," not real Windows hardware (`D9-140`,
open). See `docs/directx9-renderer.md` and `docs/d3d9-divergence-report.md` for the full detail this
column's cells summarize.

The **Headless** renderer (`CNA_GRAPHICS_RENDERER=HEADLESS`, tracked in `../plan_headless.md`) is deliberately
**not** a column in this matrix: it never renders a single pixel, so none of the below
correctness/parity rows are meaningful for it. It exists for a different purpose entirely — running
game logic headlessly (no window, no GPU, no display server) for fast CI tests — and validates
itself via argument checks, resource-lifecycle tracking, and draw-call/state-change counters instead
of pixel output. See `plan_headless.md` for its own status.

The **Software** renderer (`CNA_GRAPHICS_RENDERER=SOFTWARE`, tracked in `../plan_software.md`) is
also **not yet** a column here, but for a different reason than Headless: unlike Headless, it
*does* render real pixels (a genuine CPU rasterizer), so it could plausibly become a real
pixel-parity comparison column once its feature set is broad enough — v1 only covers `TriangleList`,
a `BasicEffect` subset (no lighting/fog), nearest-neighbor texturing, and a simplified
`Opaque`/`AlphaBlend` distinction, too narrow for a meaningful row-by-row comparison against the
established renderers yet. Worth revisiting as `plan_software.md`'s scope grows. See
`docs/software-renderer.md` for its current capability boundary.

The **Stub** renderer (`CNA_GRAPHICS_RENDERER=STUB`, tracked in `../plan_stub.md`) is, like Headless,
**not** a column in this matrix and for the same reason: it never renders a single pixel. Unlike
Headless, it also keeps no bookkeeping at all (no counters, no validation modes, no trace log) — it
exists purely as the smallest possible complete `IGraphicsRenderer` implementation, a minimal
reference and a dependency-free placeholder, not a pixel-parity or diagnostic tool. See
`docs/stub-renderer.md` for its current capability boundary.

## glTF campaign companion matrix (`GLTF-393`)

The glTF differential campaign has a deliberately narrower executable renderer set than the
project-wide tables below: STUB establishes importer independence, HEADLESS observes the native
draw boundary without rasterising, and OPENGLES3 plus Vulkan provide independent real graphics APIs
and framebuffer evidence. This table records the four renderer-sensitive features named by
`plan_gltf.md`; it does not turn a boundary-only result into pixel support.

Status here is specific: ✅ means a real native draw or framebuffer assertion; ◇ means the complete
value/topology reaches an observable non-rasterising boundary; ❌ means the renderer explicitly has
no such route. The general feature tables remain authoritative for renderers outside this campaign,
while `gltf-renderer-pbr-fallbacks.md` carries the separate 15-PBR-renderer shader inventory.

| Renderer | 32-bit index draw | `POINTS` | line modes | MRT | glTF PBR sRGB contract | Evidence boundary |
|---|---:|---:|---:|---:|---:|---|
| STUB | ❌ | ❌ | ❌ | ❌ | ◇ | Imports and L1–L6 effect capture run, but `SupportsCapability(ThreeD)` is false and no draw is submitted. |
| HEADLESS | ◇ | ◇ | ◇ | ◇ | ◇ | `HeadlessIndexBufferRenderer`, topology/MRT trace and `GpuDrawParams` are observable; no pixel is produced. |
| OPENGLES3 / EasyGL | ✅ | ✅ | ✅ | ✅ | ✅ | Full glTF selection 520/520; `PointListPrimitiveTest`, its interleaved line control, EasyGL MRT tests and `EasyGL_Pbr_SrgbTransfer`. |
| Vulkan | ✅ | ✅ | ✅ | ✅ | ✅ | Full glTF selection 520/520; 15/15 `PointListPrimitiveTest` cases include a 32-bit indexed point and the real PBR point pipeline, assert zero validation messages, and sit beside Vulkan MRT and `Vulkan_Pbr_SrgbTransfer` tests. |

MRT is recorded because a conformance/viewer harness may render diagnostic outputs to several
attachments; core glTF itself has one material colour result and does not require MRT. Likewise,
“sRGB” here means the glTF semantic contract — decode only base-colour/emissive samples, keep
normal/MR/occlusion and factors linear, mix fog in linear space, then optionally encode the final
RGB. It is not a claim that every native texture or swap-chain format is sRGB. The ordinary
`GltfRendererPbrFallbackPolicy.EveryPbrShaderHonorsColorSpaceDeclarations` source audit prevents the
other eleven PBR implementations from silently diverging even when this machine cannot execute
them.

### All-renderer 32-bit index audit (`GLTF-163`)

This is a source-capability matrix, not a claim that all platform APIs were executed on this Linux
host. It covers every one of the **42** renderer directories that implements
`CreateIndexBuffer16`. ✅ means the renderer owns an explicit 32-bit factory and preserves uint32
values to its draw boundary; ◇ means the exact width is observable but that renderer deliberately
does not rasterise; `conditional` means the native device extension decides; ❌ means construction
is rejected clearly because the renderer has no corresponding 3D route.

The shared `IGraphicsRenderer::CreateIndexBuffer32` no longer delegates to
`CreateIndexBuffer16`: it throws `32-bit index buffers are not supported by this renderer`.
Therefore a backend outside the ✅/◇ set cannot return a plausible 16-bit handle and later truncate
a glTF mesh. `GltfRendererIndexWidthPolicy` derives the complete directory inventory and locks the
provider/rejecter partition; adding a 43rd renderer without a disposition fails L0. The public
runtime contract is independently exercised by
`IndexBufferEmptyDataTest.SharedThirtyTwoBitFactoryRejectsInsteadOfDelegatingToSixteenBits`.

| Renderer | Status | 32-bit route / refusal boundary |
|---|---:|---|
| Bgfx | ✅ | Explicit factory; bgfx 32-bit index buffer flag. |
| Blend2D | ❌ | 2D-only; inherits the shared construction-time refusal. |
| Canvas | ❌ | HTML Canvas 2D; inherits the shared refusal. |
| Diligent | ✅ | Explicit factory; declared uint32 buffer format. |
| Direct2D | ❌ | 2D-only; inherits the shared refusal. |
| DirectX 1 | ❌ | No 3D implementation; inherits the shared refusal. |
| DirectX 10 | ✅ | Explicit factory; native 32-bit index format. |
| DirectX 11 | ✅ | Explicit factory; `DXGI_FORMAT_R32_UINT`. |
| DirectX 12 | ✅ | Explicit factory; `DXGI_FORMAT_R32_UINT`. |
| DirectX 2 | ✅ | Exact uint32 CPU index stream consumed by the legacy draw route. |
| DirectX 3 | ✅ | Exact uint32 CPU index stream consumed by the legacy draw route. |
| DirectX 5 | ✅ | Exact uint32 CPU index stream consumed by the legacy draw route. |
| DirectX 6 | ✅ | Exact uint32 CPU index stream consumed by the legacy draw route. |
| DirectX 7 | ✅ | Exact uint32 CPU index stream consumed by the legacy draw route. |
| DirectX 8 | ✅ | Width-locked uint32 stream; draw reads each index as uint32. |
| DirectX 9 | ✅ | Explicit factory; `D3DFMT_INDEX32`. |
| EasyGL | ✅ | Explicit factory; draw selects `GL_UNSIGNED_INT`. |
| FNA3D | ✅ | Explicit factory; FNA3D 32-bit index element size. |
| FreeDirect | ❌ | DirectDraw-only; inherits the shared refusal. |
| GDI | ❌ | Local, explicit `32-bit index buffers` unsupported error. |
| Glide | ✅ | Uint32 values are expanded through the CPU command stream before `grDrawTriangle`. |
| Headless | ◇ | Width-locked uint32 shadow and validated indexed-draw trace; no pixels. |
| HTML DOM | ❌ | 2D DOM renderer; inherits the shared refusal. |
| LLGL | ✅ | Explicit factory; native uint32 index format. |
| Magnum | ✅ | Explicit factory; `MeshIndexType::UnsignedInt`. |
| Metal | ✅ | Explicit factory; `MTLIndexTypeUInt32`. |
| OpenGL 1 | ✅ | Explicit factory; draw selects `GL_UNSIGNED_INT`. |
| OpenGL 2 | ✅ | Explicit factory; draw selects `GL_UNSIGNED_INT`. |
| OpenGL 4 | ✅ | Explicit factory; draw selects `GL_UNSIGNED_INT`. |
| OpenGL ES 1 | conditional | `GL_OES_element_index_uint` → real `GL_UNSIGNED_INT`; absent → shared clear refusal. |
| OpenVG | ❌ | 2D-only; inherits the shared refusal. |
| PortableGL | ✅ | Explicit width-locked factory; real PortableGL `GL_UNSIGNED_INT` draw. |
| SDL_GPU | ✅ | Explicit width-locked factory; `SDL_GPU_INDEXELEMENTSIZE_32BIT`. |
| SDL_Renderer | ❌ | 2D-only; inherits the shared refusal. |
| Skia | ❌ | Local `CreateIndexBuffer32` unsupported-3D error. |
| Software | ✅ | Width-locked uint32 CPU buffer and raster path (2D-only GDI build rejects locally). |
| Sokol | ✅ | Explicit factory; `SG_INDEXTYPE_UINT32`. |
| Stub | ◇ | Explicit width-locked handle accepts uint32 exactly; renderer intentionally draws nothing. |
| SVG DOM | ❌ | 2D DOM renderer; inherits the shared refusal. |
| Vulkan | ✅ | Explicit factory; `VK_INDEX_TYPE_UINT32`. |
| WebGPU | ✅ | Explicit factory; `WGPUIndexFormat_Uint32`. |
| Wicked Engine | ✅ | Explicit factory; native uint32 index-buffer descriptor. |

PortableGL, SDL_GPU and Stub were the three exact-width implementations that previously relied on
the unsafe shared delegation. They now own explicit factories; PortableGL and Stub also reject a
direct renderer-level upload whose width disagrees with the factory declaration. OpenGL ES 1 was
the important unsupported-device case: its pre-existing extension check now reaches the throwing
base method instead of manufacturing a 16-bit buffer.

### Explicit unsupported paths (`GLTF-394`)

An unsupported cell is not permission to submit a different primitive or a narrower index. STUB's
campaign contract ends at `GraphicsCapability::ThreeD == false`, and draw-requiring glTF tests name
that capability when they skip; HEADLESS exposes a trace and never calls it a framebuffer result.
The shared uint32 factory throws the width-specific message above for every renderer without an
explicit 32-bit route.

The Direct3D comparison audit found the remaining silent point-topology defaults. D3D9's five draw
implementations and the D3D10/D3D11 count/native mappers now select their APIs' real point-list
topologies. D3D12 does not pretend: its current pipeline-state cache fixes
`PrimitiveTopologyType` to `TRIANGLE`, so all four draw paths reject `PointListEXT` at entry with
`DirectX12 renderer does not support PrimitiveType::PointListEXT` before target, layout or PSO
validation can obscure the cause. `GltfRendererPointTopologyPolicy` locks those dispositions from
the renderer sources; D3D9, D3D10 and D3D11 additionally participate in the shared point framebuffer
suite. No Direct3D mapper retains an unnamed default that can turn a future topology into a
triangle list.

The **Wicked Engine** renderer (`CNA_GRAPHICS_RENDERER=WICKED`, tracked in `../plan_wicked.md`) is
**not** a column here for the same reason WebGPU is not: its feature surface is a first baseline,
and — more importantly — nothing in it has been executed on real hardware yet, so it has no cell
that could honestly be marked ✅ under this doc's "a real, GPU-facing pixel/behavior check actually
exercised it" bar. It compiles and links against a patched Wicked Engine and its device-independent
logic is unit tested; that is all. Revisit once `plan_wicked.md` `WICKED-18`/`WICKED-74` have been
run on a GPU host. See [`wicked-renderer.md`](wicked-renderer.md) for its current capability boundary.

The experimental **Skia** renderer is also not promoted to a column in the established-renderer
tables below: the selected artifact is a CPU-raster 2D implementation, not a GPU/3D peer. Its
verified subset is nevertheless broad enough to require a precise cross-reference, so the
companion table below maps the same feature families without implying full 3D parity. For this
table, ✅ means complete within the stated 2D contract, ⚠️ means a deliberately bounded direct or
emulated route, and ❌ means direct support was absent and the named emulation was evaluated and
rejected. The full row-per-API inventory is the 249-entry
[`skia-easygl-parity-ledger.md`](skia-easygl-parity-ledger.md).

## Skia CPU-raster 2D companion matrix (SKIA-111)

| Feature family | Skia | Direct/emulation decision | Verification evidence |
|---|---|---|---|
| `SpriteBatch` overloads, sorting, rotation/flip/scale/crop | ✅ | Direct `SkCanvas` 2D route; no renderer-local public-overload shortcuts. | [SKIA-31–40](../plan_skia.md), [2D registrations](skia-2d-easygl-registration.md), [XNA oracle](skia-xna-oracle.md) |
| Custom `Effect` in `SpriteBatch.Begin` | ⚠️ | Exact stock `SpriteEffect` plus explicit `CNA_SKIA_SKSL_V1` fragment SkSL for ordinary sprite quads, and a separate bounded `CNA_SKIA_SKSL_MESH_V1` per-vertex mesh ABI reached via `SpriteBatch.DrawMeshEXT` (`SkVertices`-based, `SpriteSortMode.Immediate` only) that renders `DualTextureEffect`'s core formula both hand-written and through a restricted GLSL-to-SkSL translator; arbitrary EasyGL GLSL, custom vertex attributes beyond position/UV/colour, and stock 3D effects reject. | [SKIA-89–94, SKIA-144–158](../plan_skia.md), [effects boundary](skia-effects.md), [mesh contract](skia-vertices-2d-effect-contract.md), `Skia_SkSL_UniformTexture`, `Skia_MeshEffect_PublicApi` |
| `SpriteFont` properties, glyph layout, fallback, newline and effects | ✅ | Shared font-atlas layout drawn through the direct sprite route; Skia text APIs are deliberately not substituted. | [SKIA-38, SKIA-109](../plan_skia.md), `Skia_Contract_SpriteFontProperties`, five `Skia_SpriteFont_*` tests |
| Clamp/Wrap/Mirror U/V and point/linear filtering | ✅ | Direct Skia tile/filter modes, independently selected per axis. | [SKIA-43–46, SKIA-106, SKIA-108](../plan_skia.md), `Skia_TextureAddressAxes` |
| `RenderTarget2D` colour/readback/sampling/usage | ✅ | Direct level-0 `SkSurface`; target switching and lifetime are checked. Mipmapped targets additionally own stable per-level surfaces, exact transfer shadows and one-shot dirty descendant resolve. | [SKIA-61–75, SKIA-131–132](../plan_skia.md), `Skia_RenderTarget2D_Golden`, `Skia_RenderTarget2D_MipStorage`, `Skia_RenderTarget2D_MipGeneration` |
| `RenderTargetCube` face rendering/readback/usage/mips | ⚠️ | Six independent 2D raster surfaces with CPU-generated box mips; no cube sampler, depth or MSAA claim. | [SKIA-85–86](../plan_skia.md), `Skia_RenderTargetCube_Policy` and four shared cube contracts |
| Multiple render targets | ❌ | `SkCanvas` has one colour result; replay emulation was evaluated and cannot reproduce distinct outputs for slots 0–3. Empty or one-target plural binding works. | [SKIA-87–88](../plan_skia.md), `Skia_MRT_Rejection` |
| MSAA backbuffer/targets | ❌ | Selected raster `SkSurface` owns zero physical samples. Backbuffer requests from zero through oversized values clamp to 0; real `RenderTarget2D` requests reject. A fake resolve or multisample mask is not exposed. | [surface-mode ADR](skia-surface-mode-adr.md), [SKIA-56, SKIA-76–77](../plan_skia.md), `Skia_RenderTarget2D_MsaaPolicy` |
| Mutable `RenderTarget2D` mip chain | ✅ | CNA-owned stable per-level surfaces and canonical shadows implement exact transfer and sampling. Canvas writes or parent uploads dirty their suffix; unbind/readback/sampling resolves every dirty descendant exactly once with deterministic odd/NPOT area boxes, matching the EasyGL resolve lifecycle. | [SKIA-70, SKIA-131–132](../plan_skia.md), `Skia_RenderTarget2D_MipStorage`, `Skia_RenderTarget2D_MipGeneration`, `Skia_EasyGL_RenderTarget2D_MipComplete` |
| `Texture2D` level-0 Set/Get/partial/NPOT sampling | ✅ | Exact CPU shadow plus direct raster image path; transfer and validation contracts share sources with EasyGL. | [SKIA-22–30, SKIA-106, SKIA-109](../plan_skia.md), [API contract comparison](skia-api-contract-comparison.md) |
| `Texture2D` mip levels above zero | ✅ | Public `mipMap=true` construction owns a complete checked CNA chain; transfer is exact, changed parents area-box-generate only unauthored descendants, all nine TextureFilter ordinals select/interpolate the real levels, and DDS/XNB loaders preserve every declared complete-chain level without fabricating absent mips. | [SKIA-125–130](../plan_skia.md), [resource policy](skia-successor-resource-oracles.md), `Skia_Texture2D_MipGeneration`, `Skia_MipSampling_Raster`, `Skia_Sampler_MipmapFilterPolicy`, `Skia_Texture2D_ContentMips` |
| `TextureCube`/`Texture3D` Set/Get including mips | ⚠️ | Exact bounded CPU face/voxel transfer storage; this capability does not imply shader sampling (see the separate bounded sampling row below). | [SKIA-80–84](../plan_skia.md), [texture storage policy](skia-texture-storage.md), shared 56-check contracts |
| Cube/volume texture sampling | ⚠️ | Bounded direction/coordinate sampling through a dedicated fragment-only SkSL extension (`cnaSampleCubeEXT`/`cnaSampleVolumeEXT`, reachable via `SetTexture(1, TextureCube)` or `SetTexture(1, Texture3D)`); no general `samplerCUBE`/`sampler3D` GLSL translation and no stock 3D effect route -- CPU transfer storage still cannot emulate geometry-driven directional/volume sampling. | [SKIA-144–151](../plan_skia.md), [cube/volume sampling contract](skia-cube-volume-sampling-contract.md), `Skia_CubeVolume_Effect_Binding`, `Skia_CubeVolume_Sampling_Oracle` |
| Non-`Color` surface formats | ⚠️ | Texture2D directly stores/samples packed, BGRA, alpha-only, R8/R16, RG16 and RGBA16 UNORM routes; `Bgra4444` retains exact words with a bounded RGBA conversion, while `ColorSrgbEXT` retains encoded bytes and decodes once into linear-sRGB. The other 16 formats, all non-Color targets, and cube/volume non-Color storage remain refused. | [SKIA-25, SKIA-109, SKIA-134–137](../plan_skia.md), `Skia_Texture2D_PackedFormats`, `Skia_Texture2D_ColourFormats`, `Skia_Texture2D_UnormFormats`, `Skia_Contract_SurfaceFormat` |
| Blend presets/custom state and colour-write masks | ✅ | All 714,025 valid selector tuples (13 factors⁴ × 5 functions²), independent RGB/alpha equations, live blend constants, and all target-0 masks use established or generated runtime-blender routes. Invalid raw selectors, target-1/2/3 masks, and non-default multisample masks reject atomically. | [SKIA-47–57, SKIA-108, SKIA-119–124](../plan_skia.md), [generated blender](skia-generated-blender.md), `Skia_BlendMapping_Raster`, `Skia_GeneratedBlend_PublicCorpus` |
| Depth/stencil state and clears | ❌ | Disabled `None` is valid 2D state, but there is no attachment. CPU depth/stencil prototypes passed in isolation; product emulation was rejected as a separate software renderer. | [SKIA-67, SKIA-97–102](../plan_skia.md), [3D ADR](skia-3d-emulation-adr.md), `Skia_3D_Refusal` |
| Rasterizer state | ⚠️ | Solid fill and scissor are meaningful in 2D. Wireframe, culling/depth bias as 3D features reject or stay unadvertised after geometry-emulation evaluation. | [SKIA-41–42, SKIA-58, SKIA-99–102](../plan_skia.md), `Skia_RasterizerState_Policy` |
| Sampler slots/anisotropy/mip filtering | ⚠️ | Sprite Texture2D sampling implements all nine min/mag/mip combinations plus Clamp/Wrap/Mirror. Anisotropic is an exact complete-Linear fallback with capability false; cube/volume sampler binding is now a bounded SkSL-only extension (SKIA-144–151, see the cube/volume texture sampling row above) and general 3D sampler binding remains unavailable. | [surface-mode ADR](skia-surface-mode-adr.md), [SKIA-43–46, SKIA-70, SKIA-78–79, SKIA-102, SKIA-129, SKIA-144–151](../plan_skia.md), `Skia_MipSampling_Raster`, `Skia_Sampler_MipmapFilterPolicy` |
| `ReferenceStencil` and stencil-only clear | ❌ | Zero is accepted only with disabled state; nonzero/active stencil rejects atomically. No attachment exists, and CPU-stencil product emulation was rejected by the 3D ADR. | [SKIA-67, SKIA-98, SKIA-101–102](../plan_skia.md), `Skia_3D_Refusal` |
| `OcclusionQuery` | ❌ | Raster final pixels cannot distinguish positive from zero coverage. Framebuffer diff, replay and hidden-GPU routes were evaluated and rejected. | [SKIA-104–105](../plan_skia.md), [feasibility report](skia-occlusion-query-feasibility.md) |
| 3D primitive/model rendering and stock 3D effects | ❌ | Direct selected-raster support is absent. SkVertices plus CPU depth/stencil/geometry/effect prototypes were evaluated; completing them would duplicate a software renderer. | [SKIA-95–103](../plan_skia.md), [3D ADR](skia-3d-emulation-adr.md), `Skia_3D_Refusal` |
| Presentation recovery and resource lifetime | ✅ | Presenter-only reconstruction retains CPU surfaces/resources and emits Resetting→Reset; 64 repeated cycles are sanitizer-checked. No GPU `DeviceLost` event is fabricated. | [SKIA-16, SKIA-28, SKIA-65, SKIA-74, SKIA-110](../plan_skia.md), [sanitizer validation](skia-sanitizer-validation.md) |

**Supersedes `docs/coverage.md`**, which is dated 2026-06-21
and predates almost this entire session's work
(dozens of real bugs fixed across all 4 renderers, an entire SDL_Renderer 2D-only audit phase,
Model/OcclusionQuery correctness phases) and never covered SDL_Renderer at all. `docs/coverage.md`
is kept for its still-accurate non-Graphics namespace estimates (Audio/Media/Content/Net/
GamerServices); this doc is Graphics-only and current.

Status legend: ✅ correct and verified · ⚠️ partial/emulated/environment-limited · ❌ known gap, not
fixed · ⛔ BLOCKED, needs a project-owner architecture decision · 🟨 implemented but not
independently verified (D3D9/D3D11/D3D12 columns only, see above) · ⬜ not attempted this session
(D3D9/D3D11/D3D12 columns only).

## 2D SpriteBatch / SpriteFont

| Feature | EasyGL | Vulkan | Bgfx | SDL_Renderer | D3D9 | D3D11 | D3D12 |
|---|---|---|---|---|---|---|---|
| All `Draw` overloads, sort modes, rotation/flip/scale/crop | ✅ | not separately re-audited (Task 861) | not separately re-audited | ✅ (2 real bugs fixed: rotation pivot, `transformMatrix`) | ✅ real Microsoft `SpriteEffect.fx`, oracle-verified 0-divergence for rotation (`sprite_rotated_quad`), flip (`sprite_flipped_quad`), multi-texture batching (`sprite_multitexture_quad`), and 3 of 5 `SpriteSortMode` values (`Deferred`/`BackToFront`/`FrontToBack`, `D9-93`); `.Immediate`/`.Texture` confirmed not oracle-viable, not a coverage gap — see `docs/directx9-renderer.md` | ✅ destination-rect placement + `SpriteEffects::FlipHorizontally` (`DX-70`) plus rotation/scale/source-crop-rect, real geometrically-derived pixel proof (`DX-131`, Checks Y2–Y4); `SpriteSortMode` deliberately has no dedicated test — its ordering logic lives entirely in shared, renderer-agnostic `SpriteBatch.cpp` | ✅ same rotation/scale/crop proof, off-screen (`DX-131`, Checks R4–R6) — needed widening the fixture to 2 texels per color to account for D3D12's default bilinear sampler; `SpriteSortMode` same shared-code reasoning as D3D11 |
| Custom `Effect` via `Begin(effect)` | ✅ | ✅ | ✅ | ❌ throws by design (no shader stage, 2D-only renderer) | ✅ **row corrected 2026-07-16 — was stale.** Real color-inversion custom-GLSL shader via `D3D9EffectRenderer` (runtime `D3DCompile()`), through the real public `SpriteBatch.Begin(effect)` API (`D9-110`/`D9-111`/`D9-112`, Phase D9-11 closed 2026-07-15) — this cell previously said "not started," written before that phase landed | ✅ real color-inversion custom-HLSL pixel test (`DX-71`) | ✅ same color-inversion methodology (byte-for-byte identical HLSL contract), through the real public XNA API off-screen via a windowless `GraphicsDevice` (`DX-121`, Checks NN0/NN1) |
| SpriteFont — glyph placement/spacing/newline/fallback/flip | ✅ pixel-verified (Tasks 424-429) | not separately re-audited (Task 861) | not separately re-audited | ✅ (1 real cross-renderer bug found and fixed, Task 694) | ⬜ not attempted this renderer | ✅ exact glyph placement (checked inside + all 4 edge midpoints), per-glyph advance, newline line-drop+x-reset, `FlipVertically` via an asymmetric glyph — real pixel proof through the back buffer (`DX-127`) | ✅ same proof, through a windowless `GraphicsDevice` (`PresentationParameters::HeadlessEXT`) + a real `RenderTarget2D` (`DX-132`) |
| `TextureAddressMode::Wrap`/`Mirror` via SpriteBatch | ✅ | ✅ | ✅ | ⛔ **BLOCKED** (Tasks 686/687) | ✅ real `D3DSAMP_ADDRESSU`/`V`, oracle-verified: distinct 4-band tiling (Wrap) vs. symmetric mirroring pattern (Mirror), proven genuinely different from each other, not just "some non-Clamp behavior" (`D9-92`, `sprite_wrap_quad`/`sprite_mirror_quad`) | ✅ real `D3D11SamplerCache`-backed Wrap/Mirror with discriminating probe pixels (`DX-72`) | ✅ real `D3D12SamplerCache`-backed Wrap/Mirror, same probe-pixel methodology as D3D11 (`DX-133`) — found and fixed a real bug along the way: `SpriteBatch` draws previously silently used whatever sampler an unrelated prior 3D draw left bound, `SetSamplerFilter`/`SetSamplerAddressMode` were stored but inert |

Note: several per-effect `docs/*-support.md` files (e.g. `basiceffect-support.md`) predate Tasks
885-900's fog/lighting/specular fixes on Vulkan/Bgfx and still show some of these rows as gaps —
this matrix reflects the current, post-fix state; those individual docs are due for a refresh but
not rewritten here (out of this task's own scope).

## RenderTarget / MSAA / mip / depth

| Feature | EasyGL | Vulkan | Bgfx | SDL_Renderer | D3D9 | D3D11 | D3D12 |
|---|---|---|---|---|---|---|---|
| `RenderTarget2D`/`RenderTargetCube`/MRT construction | ✅ | ✅ | ✅ | ✅ (MRT count > 1 throws by design, Task 709) | ✅ **row corrected 2026-07-16 — the crash below is fixed.** `MaxRenderTargets` `GraphicsProfile` ceiling (Reach=1/HiDef=4) real and mutation-verified (`D9-103`); full construction/bind/clear/readback pixel-verified (`DirectX9_Smoke` Checks S/T/U). The reproducible, uncaught DXVK crash this cell used to describe (sampling a `RenderTarget2D` as an ordinary effect texture) was root-caused and fixed 2026-07-16 — a real CNA-side type-confusion bug (`static_cast` between the unrelated sibling classes `D3D9TextureRenderer`/`D3D9RenderTargetRenderer`), not a DXVK/environment limitation (see `NEXT.md` §4). No `D9-A5` oracle scene exercises this path yet, though — that remains open, separately from the crash | ✅ `RenderTarget2D` full bind+clear+readback+unbind-restores-backbuffer pixel-verified; real 2-target MRT bind+independent-clear verified via one `OMSetRenderTargets` call (`plan_dx.md` `DX-43`/`DX-46`); `RenderTargetCube` also has the full bind+clear+readback+unbind proof plus a per-face independence check (`DX-129`) | ✅ **stale row, corrected 2026-07-14** — a real public `D3D12RenderTargetRenderer`/`D3D12RenderTargetCubeRenderer` landed in Phase DX13 (`plan_dx.md` `DX-117`): real `RenderTarget2D` bind+clear+readback+unbind, real 2-target MRT, `RenderTargetCube` face-0 bind+clear+readback+unbind, all pixel-verified. This row previously described a pre-Phase-DX13 state and had not been updated since |
| MSAA (both RT types) | ✅ | ✅ | ✅ (`Bgfx_RenderTarget2D_MsaaResolve` fails only under this session's Xvfb/no-DRI3 sandbox, not a code bug) | N/A (2D-only, no AA needed) | ⬜ not attempted — the render-target-as-texture crash that used to block this is fixed (2026-07-16, see the row above), but no oracle scene exercises `RenderTarget2D`/`RenderTargetCube` MSAA yet | ✅ `RenderTarget2D` 4x MSAA clear+resolve pixel-verified, device-queried via `CheckMultisampleQualityLevels` (`DX-45`); `RenderTargetCube` MSAA now also real, face-scoped resolve via a plain (non-cube) MSAA `Texture2DMSArray` + a separate cube-flagged resolve texture (`DX-152`) | ✅ `RenderTarget2D` 4x MSAA clear+resolve pixel-verified, device-queried via `CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS)`, resolved via real `ResolveSubresource()` on unbind (`DX-117` follow-up, Checks OO0/OO1); `RenderTargetCube` MSAA now also real, same face-scoped resolve design (`DX-152`, Checks SS0/SS1) |
| Mip chains (both RT types) | ✅ | ✅ | ✅ | N/A | ⬜ not attempted — same as MSAA above: no longer crash-blocked (2026-07-16), just not yet oracle-covered | ✅ `RenderTarget2D` real (driver `GenerateMips()`, box-filtered content pixel-verified per level) + `RenderTargetCube` real for face 0 (`DX-144`) and a non-zero face (`DX-153`, confirming the whole-resource `GenerateMips()` call genuinely regenerates whichever face was just drawn to) | ✅ `RenderTarget2D` real (CPU box-filter downsample cascade on unbind, box-filtered content pixel-verified per level) + `RenderTargetCube` real for face 0 (`DX-144`) and a non-zero face (`DX-153`, confirming the `activeFace_`-scoped cascade and its subresource math generalize past face 0) |
| Per-instance `DepthStencilFormat` fidelity | ✅ | ✅ (Task 911) | ✅ | ⚠️ emulated (echoes the requested format back, no real backing storage) | ⬜ not attempted — same as MSAA/mip above: no longer crash-blocked (2026-07-16), just not yet oracle-covered | ✅ real GPU-resource-level format introspection — `None` creates no depth resource at all, `Depth16` genuinely lands on `D16_UNORM` (not silently upgraded), `Depth24`/`Depth24Stencil8` both genuinely share the same `D24_UNORM_S8_UINT` fallback (`DX-145`) | ✅ same real resource-level introspection, off-screen (`DX-145`, Checks II0–II3) |

### Bgfx MRT attachment limits (Task 775)

`GraphicsDevice::SetRenderTargets` throws `std::invalid_argument` above **4** targets in shared C++
code (`MAX_RENDERTARGET_BINDINGS`, Task 881) — mirroring FNA's own real
`internal const int MAX_RENDERTARGET_BINDINGS = 4` cap — before any renderer ever sees the call, so
this is the practical, enforced limit on Bgfx (and every other renderer) regardless of what the
underlying device itself could support. `BgfxRenderer::SetRenderTargets`'s own MRT
framebuffer-construction path (`BgfxRenderer.cpp`) separately caps at a local
`kMaxAttachments = 8`, matching bgfx's own `BGFX_CONFIG_MAX_FRAME_BUFFER_ATTACHMENTS` compile-time
default — unreachable in practice today since the shared 4-target gate rejects anything larger
first, kept only as defense-in-depth (same reasoning as Task 881's own EasyGL/Bgfx ad-hoc-cap
notes). The real device capability, `bgfx::getCaps()->limits.maxFBAttachments`, is logged at
startup (Task 456) and is typically 8 on desktop GL/Vulkan hardware — always ≥ the FNA-mandated 4,
so it has never been the binding constraint in this project.

## Texture2D / Texture3D / TextureCube

| Feature | EasyGL | Vulkan | Bgfx | SDL_Renderer | D3D9 | D3D11 | D3D12 |
|---|---|---|---|---|---|---|---|
| Texture2D `SetData`/`GetData`/`FromStream`/`SaveAsPng`/NPOT | ✅ | ✅ | ✅ | ✅ (4 real bugs found and fixed) | ✅ real `D3DPOOL_MANAGED` upload, exercised (and oracle-verified byte-exact) by every textured scene in the corpus (`texturepixel=` used 64×); `FromStream`/`SaveAsPng`/NPOT-on-`Reach` not separately tested | ✅ `SetData`/`GetData` byte-exact GPU round-trip verified (`DX-40`); NPOT (a genuinely non-power-of-two 5×3 texture) and `FromStream`/`SaveAsPng` (real PNG encode/decode round-trip, every pixel of a varying pattern) both real (`DX-140`) | ✅ same coverage, off-screen — NPOT also exercises the `D3D12_TEXTURE_DATA_PITCH_ALIGNMENT` row-pitch edge case a 5×3 width creates; `FromStream`/`SaveAsPng` needed `PresentationParameters::HeadlessEXT` (a windowless `GraphicsDevice`) to reach at all (`DX-140`, Checks GG0–GG2/KK7–KK9) |
| Texture2D mip-level `SetData` (level > 0) | ✅ | ❌ silent no-op (Task 867) | ❌ silent no-op (Task 867) | ❌ throws by design (Task 681) | ⬜ not attempted — no oracle scene exercises mip level > 0 | ✅ real, deterministic direct-GPU-readback proof — `UpdatePixelsLevel(1,...)` round-trips exact bytes, and level 0's own content is independently confirmed unaffected (`DX-126`) | ✅ same direct-readback proof, off-screen (`DX-141`, Checks HH0–HH2) |
| Texture3D/TextureCube `SetData`/`GetData`, incl. mip | ✅ | ✅ | ✅ (needed a new `GetData` readback path, Task 914) | ⛔ **BLOCKED** — construction succeeds silently with a null renderer, 94-test blast radius (Task 725) | 🟨 split: `TextureCube` `SetData` real, oracle-verified byte-exact via `envmap_quad`/`envmap_fresnel_quad`; `Texture3D` has only its `GraphicsProfile` size-ceiling mutation-verified (`D9-103`) — no oracle scene or GPU round-trip test exists for `Texture3D` at all | ✅ byte-exact GPU round-trip verified for both types at mip 0 (`DX-41`/`DX-42`) | ✅ real `D3D12Texture3DRenderer` (`DX-122`, byte-exact off-center sub-volume round-trip); real `D3D12TextureCubeRenderer::GetData()` readback (`DX-123`, no longer a no-op — proven per-face-distinct and genuinely reads live GPU content, not CPU poison values) |
| Texture3D/TextureCube sampled in shaders | ❌ don't inherit `Texture` (Task 863, architectural) | ❌ | ❌ | N/A | 🟨 `TextureCube` sampled and oracle-verified byte-exact via `EnvironmentMapEffect` (`envmap_quad`/`envmap_fresnel_quad`); `Texture3D` has no consuming shader variant tested | 🟨 `TextureCube` sampled and pixel-verified via `env_map3d` (`DX-66`); `Texture3D` has no consuming shader variant, unverified in a shader | 🟨 `TextureCube` sampled and pixel-verified via `env_map3d` (`DX-111`, Check U1); `Texture3D` has a real renderer (`DX-122`) but still no consuming shader variant, unverified in a shader |
| Non-`Color` `SurfaceFormat` for real GPU texture data | ⛔ **BLOCKED** (Task 732) | same shared-code limitation | same | same | same shared-code limitation | same shared-code limitation | same shared-code limitation |

## GraphicsDevice state objects

| Feature | EasyGL | Vulkan | Bgfx | SDL_Renderer | D3D9 | D3D11 | D3D12 |
|---|---|---|---|---|---|---|---|
| `BlendState` (all presets + custom factors/equations) | ✅ | ✅ **FIXED (Task 868, 2026-07-09)** — real per-`Blend`/`BlendFunction` mapping across all 9 3D pipeline-creation sites; was "almost entirely fake" (hardcoded one blend equation regardless of request, confirmed 5× via pixel tests) before this fix | ✅ | ✅ (2 real bugs fixed) | ✅ real `D3DRS_SRCBLEND`/`DESTBLEND` per-preset dispatch, `Opaque`/`AlphaBlend` pixel-behavior-verified (`DirectX9_BlendState_Opaque`/`_AlphaBlend`) and separately mutation-verified (src/dst swap caught, `D9-122`) | ✅ real cached `ID3D11BlendState`, `Opaque`/`AlphaBlend` pixel-behavior-verified (`DX-50`/`DX-82`) | ✅ real runtime-settable `BlendState`→PSO-description-key mapping, replacing the old hardcoded PSO defaults (`DX-118`); a real `ApplyBlendState(One,One,Add)` genuinely additive-blends a second draw, and reverting to `Opaque` genuinely restores it, off-screen (Checks X1–X3) |
| `DepthStencilState` (compare func + full stencil ops) | ✅ | ✅ (Task 870 — real per-pipeline compare-op + stencil) | not separately re-confirmed this pass | ✅ never throws (deliberate no-op, matches FNA's renderer-agnostic-until-drawn model) | ✅ real `D3DRS_STENCILENABLE`/compare-func dispatch, pixel-verified (`DirectX9_DepthStencilState_StencilEnable`) and mutation-verified (forced-false caught, `D9-122`) | ✅ real cached `ID3D11DepthStencilState`, stencil-enable gating pixel-verified (`DX-51`/`DX-82`) | ✅ real runtime-settable `DepthStencilState`→PSO mapping (`DX-118`), replacing the old hardcoded `depthEnable=false`; a real per-pixel `LessEqual` depth comparison proven both draw orders (near-then-far rejects far, far-then-near lets near overwrite) plus a depth-disabled control (Checks Y2–Y4); combo `Clear*`/stencil-plane behavior separately proven (`DX-146`) |
| `RasterizerState` | ✅ | not separately re-confirmed this pass | not separately re-confirmed this pass | ✅ never throws | ✅ real `D3DRS_CULLMODE` winding-order dispatch, pixel-verified (`DirectX9_RasterizerState_CullMode`) and mutation-verified (forced-none caught, `D9-122`); depth-bias not separately tested — see this doc's own "Remaining genuine D3D9 limitations" section for `D9-62`'s own outstanding depth-bias oracle-proof attempt (investigated 2026-07-16, not closed: no observable effect at any tested magnitude, suspected environment/driver limitation shared with `Vulkan_DepthBias`) | ✅ real cached `ID3D11RasterizerState`, `CullMode` winding-order pixel-verified (`DX-52`/`DX-82`); depth-bias unit convention (float→rounded `INT`) documented, not itself pixel-tested | ✅ real runtime-settable `RasterizerState`→PSO mapping (`DX-118`), replacing the old hardcoded `cullMode=None`; `CullCounterClockwiseFace` genuinely culls, `CullMode::None` genuinely draws the same triangle, off-screen (Checks X4/X5) |
| Per-slot `SamplerState` (16 slots) | ✅ | ✅ | ✅ | ✅ (1 real bug fixed) | 🟨 real `D3DSAMP_ADDRESSU`/`V`/`MAGFILTER`/`MINFILTER` wiring, Wrap/Mirror pixel-behavior oracle-verified via SpriteBatch (`D9-92`); not tested across all 16 slots simultaneously | ✅ real cache with identity/distinctness proof + Wrap/Mirror pixel-verified via SpriteBatch (`DX-44`/`DX-72`), plus all 16 slots proven simultaneously independent (`DX-142`) | ✅ real dynamic per-slot samplers (`D3D12SamplerCache`, `DX-119`, replacing the old single hardcoded static sampler), Wrap-vs-Clamp discriminating pixel proof + cache identity/distinctness (Checks Z1–Z4), plus all 16 slots proven simultaneously independent (`DX-154`, Checks UU0/UU1) |
| `GraphicsDevice.ReferenceStencil` | ❌ **no renderer connection** (Task 872, open) | ✅ **FIXED** — connected via `vkCmdSetStencilReference`, an undocumented side effect of Task 870 (corrected 2026-07-09) | ❌ **no renderer connection** (Task 872, open) | N/A | ⬜ not attempted — no D3D9 test exercises `SetReferenceStencil()` | ✅ real `OMSetDepthStencilState` re-bind on `SetReferenceStencil()`, verified (`DX-52`) | ⬜ not applicable — no depth-stencil-state object exists to carry a reference value |
| `Clear` honors `ClearOptions::Stencil` | ❌ **ignored, all 3** (Task 871, open) | ❌ | ❌ | ⚠️ emulated | ⬜ not attempted — `DirectX9_Smoke` covers plain `Clear(r,g,b,a)` only, not the `ClearOptions` combo variants | 🟨 real `ClearDepthStencilView` calls implemented for all 5 combo variants (`DX-25`); only plain `Clear(r,g,b,a)` has a dedicated round-trip pixel test | ⬜ not attempted — no DSV is bound in any current off-screen test (`depthEnable=false` throughout), so depth/stencil `Clear` combos have no target to exercise against yet |

### Vulkan optional device-feature gating (Task 454)

Investigated whether Vulkan's `VkPhysicalDeviceFeatures`-gated optional capabilities are requested
safely (a device that doesn't support a requested optional feature makes `vkCreateDevice` fail
outright, unlike GL/bgfx's more forgiving capability model). Confirmed the device-creation code
(`VulkanRenderer`'s constructor) only ever requests the 2 optional features CNA actually
uses — `fillModeNonSolid` (`FillMode::WireFrame`) and `samplerAnisotropy` (anisotropic texture
filtering) — and both are correctly gated behind a real `vkGetPhysicalDeviceFeatures` query first
(`if (supported.fillModeNonSolid) { feat.fillModeNonSolid = VK_TRUE; fillModeNonSolidSupported_ =
true; }`, same shape for `samplerAnisotropy`). Neither is ever unconditionally requested. Downstream
usage sites correctly gate on the resulting `fillModeNonSolidSupported_`/`anisotropySupported_`
flags (e.g. `fillModeWireframe_ = (fillMode == 1) && fillModeNonSolidSupported_` — a device without
`fillModeNonSolid` silently falls back to solid fill rather than requesting an invalid pipeline
state), and `maxSamplerAnisotropy_` is read from real `VkPhysicalDeviceLimits` and used to clamp any
requested anisotropy level. MSAA sample-count selection (`PickSampleCount`) also respects the
device's real `framebufferColorSampleCounts` limit, picking the best available count ≤ the
requested one rather than assuming an arbitrary count is always supported. **No gap found** — this
was already correctly implemented, just not previously documented anywhere; recorded here per Task
454's own "throw or document fallback behavior" framing (this renderer's own answer is "gracefully
falls back," which is the idiomatic Vulkan pattern for optional features, not a bug needing a fix).

## OcclusionQuery (Phase 50, closed this session — see `docs/occlusionquery-support.md` for full detail)

| Feature | EasyGL | Vulkan | Bgfx | SDL_Renderer | D3D9 | D3D11 | D3D12 |
|---|---|---|---|---|---|---|---|
| Wired to real GPU work (`Begin`/`End`) | ✅ | ✅ **FIXED (Task 447, 2026-07-10)** — real per-draw-call query correlation via `Pending3DDraw::occlusionQuery` tagging + `vkCmdBeginQuery`/`vkCmdEndQuery` recording in `RecordCommandBuffer()` | ✅ (Task 448) | N/A — throws at construction (Task 727) | ⬜ **not built at all** — `plan_dx9.md`'s task list has no `IDirect3DQuery9`-based occlusion-query task; `CreateOcclusionQuery()` falls through to `IGraphicsRenderer`'s own silent `nullptr` default, same gap as D3D12 | ✅ real `ID3D11Query(D3D11_QUERY_OCCLUSION)`, `Begin`/`End`/`GetData` wired (`DX-47`) | ✅ real `D3D12OcclusionQueryRenderer` (`ID3D12QueryHeap`+readback buffer, `DX-120`) — a full-viewport visible triangle reports a real positive `PixelCount()`, the SAME query object reused around off-screen/clipped geometry reports exactly 0, a genuine visible-vs-invisible discrimination, off-screen (Checks AA1–AA4) |
| Pixel/query correctness (visible vs. occluded) | ✅ verified both directions (Tasks 445/446) | ✅ verified both directions, plus a multi-draw-span check (Task 854) — this sandbox's software Vulkan driver (Mesa Lavapipe) reports fully accurate, discriminating pixel counts (4096 visible / 0 occluded on a 64×64 quad) | ⚠️ can't verify in this sandbox's software GL2.1 driver; dedicated-view architecture gap open (Task 917) | N/A | N/A — no query support exists | ✅ **stale row, corrected 2026-07-15** — `plan_dx.md` `DX-147` closed this: the same `ID3D11Query` object reused around a genuinely visible (viewport-covering) draw reports a real positive `PixelCount()`, then around a fully off-screen draw reports exactly 0, both-directions discriminating (`DirectX11_Smoke` 91→93/93). This row previously described the pre-`DX-147` state | ✅ **stale row, corrected 2026-07-15** — real, and closed earlier than the row above: `DX-120`'s own landing already included this exact both-directions proof (Checks `AA3`/`AA4`, `plan_dx.md` `DX-147`'s own row explicitly credits `DX-120` for closing the D3D12 half) — this row's "no query support exists" contradicted the very next table up (`OcclusionQuery` "wired to real GPU work" row, same file), which already correctly described `D3D12OcclusionQueryRenderer` as real |

## Model (Phase 49, closed this session — see `docs/model-content-pipeline-support.md` for full detail)

| Feature | Status | D3D9 | D3D11 | D3D12 |
|---|---|---|---|---|
| Runtime API (`Model`/`ModelMesh`/`ModelMeshPart`/`ModelBone`) | ✅ fully audited/FNA-faithful, several real bugs found and fixed (Tasks 431-439) | ⬜ not separately tested against this renderer | ✅ a real 2-bone hierarchy drives `Model::Draw()` → `ModelMesh::Draw()`'s genuine orchestration (bone transform, `SetVertexBuffer`/`setIndices`/`DrawIndexedPrimitives`/`EffectPass::Apply`), pixel-verified through the back buffer (`DX-128`) | ✅ same orchestration proof, off-screen through a windowless `GraphicsDevice` (`DX-148`) — found and fixed a real crash bug along the way: `SetDepthTestEnabled`/`SetDepthWriteEnabled`/`SetBlendEnabled` were still unimplemented-throw stubs, so any game driving this shared code path against D3D12 would have crashed |
| Content-pipeline loading (`ModelTypeReader`) | ⚠️ real gaps — no bone hierarchy, no `ParentBone` wiring, no `BoundingSphere`/`Tag`, custom `.model.json` format is not `.xnb`-compatible (Task 440); zero test coverage of the loader itself | ⬜ not separately tested against this renderer | ⬜ not separately tested against this renderer | ⬜ not separately tested against this renderer |
| `Model` constructor root-bone-index flexibility | ✅ fixed (Task 916, 2026-07-09) | ⬜ not separately tested against this renderer | ✅ real proof (`DX-155`) — `getRootProperty()` genuinely returns the non-zero-indexed bone, plus a full 5-argument-constructor draw through the real renderer | ✅ same proof, off-screen (`DX-155`, Checks VV0/VV1) |

Note: `cna_reference_dump`/`cna_demo_2d` (both `Model`-adjacent example binaries) fail to *link*
under `DIRECTX11` (`undefined reference to Effect::Apply()`) — found during `plan_dx.md` `DX-81`'s
coverage audit and confirmed via `git stash` to pre-date the D3D11 renderer work entirely (fails
identically on the base commit); a real, pre-existing, unrelated gap, not caused or fixed by this
renderer. See `docs/directx11-renderer.md`. The same link failure applies equally under `DIRECTX12` (same
root cause, same example binaries, same shared code) — not independently re-confirmed but expected
identical, see `docs/directx12-renderer.md`.

## Every currently-BLOCKED task (⛔)

| Task | Renderer | One-line reason |
|---|---|---|
| 686 | SDL_Renderer | `TextureAddressMode::Wrap` via `SpriteBatch` — no native support in the `Draw()` path used; 3 options (throw / rewrite to `SDL_RenderGeometry` / hybrid), none picked |
| 687 | SDL_Renderer | Same underlying constraint as 686, for `Mirror` — resolving 686 resolves this too |
| 725 | SDL_Renderer | `Texture3D`/`TextureCube` construction succeeds silently with a null renderer; 94 existing tests rely on that silent-success behavior, so fixing needs a blast-radius-aware architecture decision |
| 732 | EasyGL | Real `SurfaceFormat` GPU forwarding conflicts with an already-shipped, already-tested `Texture::ValidateFormat` contract (Task 176) plus the public `SetData`/`GetData` API being `Color*`-only |

## Known pre-existing test-failure baseline, per renderer

Confirmed most recently by this session's own regression runs (always run sequentially per
renderer, never concurrently — concurrent runs have previously produced transient GPU/driver-
contention false failures):

- **EasyGL**: 3 — `EasyGL_MRT_TwoAttachments`, `EasyGL_GraphicsDevice_ReferenceStencil`,
  `easy-gl-resource-smoke-tests`. Reconfirmed as recently as Task 449's own regression (4510/4513).
- **Bgfx**: **current baseline per `NEXT.md` (verified 2026-07-11): `CnaTests` 4375/4377 (2
  hardware skips), `ctest` 103/105 — 2 remaining failures**: `Bgfx_RenderTarget2D_MsaaResolve`
  (this sandbox's Xvfb has no DRI3 support — an environment limitation, not a code bug) and
  `Bgfx_RenderTargetCube_DepthFormat` (Task 952, **DEFERRED** — a `Depth24Stencil8`-attached
  `RenderTargetCube` face produces no colour output; investigated 3 times, root cause not yet
  found). Task 951 (closed 2026-07-11) fixed 5 of the 6 pre-existing `RenderTarget2D`/
  `RenderTargetCube` `glReadPixels`/Xvfb crashes that used to be counted here (`DepthBuffer`,
  `MipChain` ×2, plus others) via a dedicated highest-id "flush" view — see `NEXT.md` §3/§5 for the
  full root-cause writeup. `Bgfx_ModelJsonReader_Quad` (Task 927/948) is also fixed and passes 2/2.
- **Vulkan**: **current baseline per `NEXT.md` (verified 2026-07-11): `CnaTests` 4371/4373 (2
  hardware skips), `ctest` 126/127 — 1 remaining failure, `Vulkan_DepthBias`.** Both the 5×
  `BlendState` failures (Task 868, fixed 2026-07-09, commit `459a0e37`) and the 3
  `ContentManagerSkinnedModelTest.*` segfaults (Task 953, fixed 2026-07-11) that used to make up
  this baseline are gone — no exclusions needed anymore. **Historical correction (2026-07-09,
  Task 861):** this row previously said "12" pre-existing failures and additionally claimed
  "several `DepthStencilState`-adjacent" ones — both wrong; `DepthStencilState`'s own
  compare-op/stencil-op tests all pass (Task 870 fixed this).
- **SDL_Renderer**: 13 known pre-existing, all throwing `"SDL_Renderer does not support 3D"` —
  matches this renderer's accepted 2D-only architectural scope exactly (`EffectApplyTest`,
  `GraphicsDeviceValidationTest.SetRenderTargets_*`, `SkinnedModelEXTPartTest.*`,
  `ContentManagerSkinnedModelTest.*`). Confirmed via Task 915's own systematic full-suite run.

## Remaining genuine Vulkan limitations (Task 861, 2026-07-09)

Phase 73 (Tasks 664-665, 825-861) was written as a checklist of individual Vulkan pixel-test tasks,
but Tasks 825-860 were never checked off — later, higher-numbered work (Tasks 484/495/499/500,
plus the fog/lighting/effect fixes at 885-900) independently established most of the same ground,
superseding the original per-row checklist without formally closing each row. This section is
Task 861's real deliverable: the actual current state, confirmed by spot-checking a representative
sample of Tasks 825-860 against real test coverage rather than re-verifying all 36 rows from
scratch (that would be Task 738-scale work, out of this task's own scope).

**Genuinely already covered by real, current Vulkan tests** (confirmed via
`ctest --test-dir cmake-build-vulkan -N -R "^Vulkan_"`, 93 real tests): `TextureAddressMode`
(Clamp/Mirror), `TextureFilter` (Point vs. Linear), anisotropic filtering, all 7 `BlendState`
presets, all 6 `DepthStencilState` aspects, `CullMode`, `Viewport`, render-target lifecycle
(sample-after-unbind, MSAA, mip chains, depth-format fidelity, MRT-adjacent), and all 5 stock
effects including fog and several per-effect sub-features (specular, Fresnel, eye position, bone
blending) — this maps directly onto Tasks 825-849's own topics. **Confirmed genuine bugs found by
this later work**: `BlendState` (Task 868, **fixed 2026-07-09**), one isolated
`RasterizerState.DepthBias` sub-case (still open) — these are the real content behind Tasks 831-833
and 839's own topics, not clean passes.

**A real, previously-undocumented gap found by this spot-check**: unlike EasyGL and SDL_Renderer,
Vulkan has **no dedicated pixel test** for `SpriteBatch`'s sort-mode ordering, rotation/scale/
source-rectangle-cropping/`SpriteEffects` flip (Task 851/850's own topics), `SpriteFont` glyph
placement (Task 852), or `Model` multi-mesh hierarchy transform propagation (Task 853) —
confirmed via `grep`/`ctest -N` finding zero `Vulkan_SpriteFont*`/`Vulkan_Model*`/
`Vulkan_SpriteSortMode*` test names, despite `Vulkan_SpriteBatch_MultiBeginEnd` and
`Vulkan_Demo2D_SmokeTest` confirming basic `SpriteBatch` drawing works. Corrected the feature
matrix's own "2D SpriteBatch/SpriteFont" table above, which previously (incorrectly) rated Vulkan
✅ for this without a backing test, to "not separately re-audited" — matching the honest phrasing
already used for the adjacent SpriteFont row. This is a **test-coverage gap, not a confirmed
behavioral bug** — the underlying `SpriteBatch`/`SpriteFont`/`Model` code is renderer-agnostic C++
already pixel-verified on EasyGL/SDL_Renderer, so a regression specifically on Vulkan is unlikely,
but it is genuinely unverified there. Not opened as a new numbered task here (that's Task 738-scale
triage work); flagging it in this matrix is this task's own real scope.

**`OcclusionQuery` visible-vs-occluded pixel test on Vulkan (Task 854's own topic)**: **FIXED,
2026-07-10** — was Task 447's ⛔ BLOCKED status (Vulkan's deferred-draw architecture couldn't
correlate a query's Begin/End span with a draw at all); now resolved via real per-draw-call query
tagging and `vkCmdBeginQuery`/`vkCmdEndQuery` recording, see the `OcclusionQuery` table above.

**Bottom line**: Vulkan's real, current, confirmed-open limitation is exactly 1 — the isolated
`RasterizerState.DepthBias` sub-case. `BlendState` (Task 868) is fixed as of 2026-07-09, and
`ReferenceStencil` is fixed on Vulkan specifically (an undocumented side effect of Task 870); the
`ReferenceStencil` gap (Task 872) remains open only on EasyGL and Bgfx. The 2D
SpriteBatch/SpriteFont/Model-hierarchy test-coverage gap above is real but distinct in kind
(untested, not un-implemented or known-broken). `OcclusionQuery` (Task 447/854) is no longer on this
list — fixed in full.

## Remaining genuine Bgfx limitations (Task 824, 2026-07-10)

Phase 72 (Bgfx full 2D+3D pixel-verified parity, Tasks 740-824) is now closed in full: of the
original 38 confirmed real gaps found in a first-ever complete row-by-row triage, 37 are ✅ closed
this session and exactly 1 remains open, explicitly flagged (not silently skipped). Three genuine,
confirmed limitations survive this closure — none of them a code bug in this project, each already
root-caused rather than merely observed:

- **Constant `DepthBias` (Task 767): FIXED, 2026-07-10.** Project-owner decision received: bgfx's
  high-level state API has zero depth-bias/polygon-offset mechanism anywhere (confirmed via
  `bgfx/defines.h` — no `BIAS`/`OFFSET` flag exists — and the vendored `renderer_gl.cpp` — no
  `glPolygonOffset` call in the whole file), so constant `DepthBias` is now emulated via a per-draw
  vertex-shader Z-offset (`BgfxRenderer::SetDepthBiasUniform`, a new `u_depthBias` uniform
  added to every 3D vertex shader, scaled by `kDepthBiasScale` to roughly match the visual magnitude
  a real GL/Vulkan polygon-offset implementation would produce). New `Bgfx_RasterizerState_DepthBias`
  test confirms both the zero-bias baseline (stays RED) and a large negative bias (pulls a coplanar
  redraw in front, turns GREEN); verified via `git stash` revert-and-rebuild.
  **`SlopeScaleDepthBias` remains an intentionally undone gap** (project-owner decision, not
  attempted): a true per-fragment screen-space-slope computation would force every 3D shader off the
  early-Z path, even at `DepthBias=0`, unless duplicate shader variants were added — not worth the
  cost for this one property. EasyGL, by contrast, needed no shader emulation at all: it already had
  real `glPolygonOffset` support in the vendored `easy-gl` library, just never wired up — fixed with
  a native call, covering both constant and slope-scale bias in one shot (see Task 767's own
  `plan_graphics.md` entry). Only Vulkan implements real hardware depth bias (`vkCmdSetDepthBias`,
  dynamic state, including real slope-scale).
- ~~**`RenderTarget2D`/`RenderTargetCube` `glReadPixels` crashes under this sandbox's software GL
  driver**~~ (`Bgfx_RenderTarget2D_DepthBuffer`/`MsaaResolve`/`MipChain`,
  `Bgfx_RenderTargetCube_MipChain`/`MsaaResolve`/`DepthFormat` — 6 tests, all pre-existing at the
  time this section was written) — **5 of these 6 fixed by Task 951 (closed 2026-07-11)**, root
  cause: bgfx processes views in ascending id order each frame, so any render-target view was
  always last-processed and still GL-bound when `glReadPixels()` fired; fixed via a dedicated,
  always-last-processed "flush" view touched right before every screenshot request. Only
  `Bgfx_RenderTargetCube_DepthFormat` remains open (Task 952, **DEFERRED** — a genuinely different,
  still-unsolved root cause: a `Depth24Stencil8`-attached `RenderTargetCube` face produces no colour
  output at all, not a crash). `Bgfx_RenderTarget2D_MsaaResolve` also remains, but as a real
  environment ceiling (no DRI3 in this sandbox), not part of Task 951's crash class. See `NEXT.md`
  §5 for the current, authoritative 2-failure baseline.
- **`OcclusionQuery.PixelCount()` doesn't discriminate visible from occluded geometry in this
  sandbox** (Tasks 814/815): a dedicated scratch probe confirmed the exact same numeric value is
  returned regardless of scene content, extending Task 448's own already-documented finding
  (`IsComplete()`/`PixelCount()` can't distinguish a wired-up query from a never-submitted one) to
  the actual pixel-count magnitude too — the underlying rendering/depth-occlusion behavior each
  scenario depends on IS reliably pixel-verified instead (`Bgfx_OcclusionQuery_PixelCount`'s own 2
  real, sabotage-verified checks). Same software-renderer ceiling as above, not a CNA defect.

**FIXED, 2026-07-10 (Task 927/948): `BgfxRenderer` never overrode `DrawIndexedPrimitivesEx`**
— was a **real CNA gap**, not an environment limitation, first flagged as an "adjacent, out-of-scope
discovery" by Task 766 and concretely reproduced by `Bgfx_ModelJsonReader_Quad` (previously a
documented 1/2 known failure). Any indexed, `Effect`-bound draw with a vertex format lacking a
`Color` attribute (`VertexPositionNormalTexture`/`VertexPositionTexture` — i.e. any
`Content.Load<Model>()`-loaded mesh) silently fell back to the base `IGraphicsRenderer`'s default
`DrawIndexedPrimitivesEx`, which discarded `GpuDrawParams` entirely and rendered via the `colored3D`
pipeline instead — reading an unbound `a_color0` attribute (GL default `(0,0,0,1)`), so the mesh
rendered solid black regardless of its real `DiffuseColor`/texture/lighting. Fixed by adding a real
`BgfxRenderer::DrawIndexedPrimitivesEx` override mirroring `DrawPrimitivesEx`'s own full
`GpuDrawParams` dispatch; `Bgfx_ModelJsonReader_Quad` now passes 2/2.

**Bottom line**: Bgfx's only remaining code-level gap is Task 767 (depth bias), already flagged for
a project-owner decision; the other 2 items are sandbox/environment ceilings, already root-caused,
that would need a real GPU-backed test environment to resolve or re-confirm — not further Bgfx
renderer code work.

## New tracked follow-up tasks opened this session

- ~~**Task 916**~~ — **fixed, 2026-07-09** (same day it was opened): `Model`'s constructor used to
  auto-default `Root` to `bones[0]` with no way to specify a different root bone index; an optional
  `rootBoneIndex` parameter now covers it (low-risk, purely-additive fix).
- **Task 917** — Bgfx occlusion queries share a view/depth buffer with other same-frame geometry
  instead of using bgfx's own dedicated-measurement-view pattern; needed for true scene-depth
  query correctness (deferred, not blocked, can't be verified in this sandbox anyway).

## Remaining genuine D3D9 limitations (`D9-130`, 2026-07-15)

Unlike the other 6 columns, `DIRECTX9`'s ✅ cells above are not "implemented and plausible" — they are
each backed by a byte-identical (`--tolerance 0`) match against the real XNA 4.0 runtime for a
checked-in oracle scene (`D9-120`, `D3D9_XNA_Diff`). The corpus has grown to 36/36 scenes (0
diverge) as of Task 1101 (2026-07-16), up from 31 at `D9-130`'s original 2026-07-15 writing.
**Three of this section's original six gaps are now fixed, corrected below rather than silently
removed** — the genuine, currently-open gaps are:

- **`Texture3D` has no GPU round-trip or oracle coverage at all** — only its `GraphicsProfile`
  size-ceiling is tested; no scene in the corpus samples a `Texture3D` in a shader.
- **`OcclusionQuery` is not built** for this renderer (same gap as D3D12).
- **Not verified on real Windows hardware** (`D9-140`) — every result here is Wine+DXVK-on-this-
  machine, same caveat as `DIRECTX11`/`DIRECTX12`.

Fixed since this section was originally written (2026-07-15):

- ~~Render targets cannot be sampled as textures at all — a reproducible, uncaught DXVK crash~~ —
  **fixed 2026-07-16**: a real type-confusion bug (`static_cast` between the unrelated sibling
  classes `D3D9TextureRenderer`/`D3D9RenderTargetRenderer`, both implementing `ITextureRenderer`),
  not a DXVK/environment limitation. See `NEXT.md` §4 and `src/Graphics/Renderers/D3D9/
  D3D9EffectDraw.cpp`'s `ResolveD3D9TextureEXT`/`ResolveD3D9TextureCubeEXT`.
- ~~`PreferPerPixelLighting` and real specular highlights are structurally unreachable~~ — **fixed
  2026-07-16** (`plan_graphics.md` Phase 80, Task 1101): `GpuDrawParams::preferPerPixelLighting`/
  `specularEnabled` are now real fields (Task 1100), read by this renderer's dispatch instead of
  hardcoded `false`. 4 of the 5 `PixelLighting` bytecode variants `D9-73` had flagged as diverging
  from Microsoft's own shipped bytes are now oracle-proven pixel-perfect; the 5th (untextured
  `VSBasicPixelLighting`) remains permanently unreachable, blocked by the same missing Position-only
  vertex layout as the untextured vertex-lit bucket — unrelated to this fix. This was also the
  project-wide "Divergence 1" every other CNA renderer shared; `plan_graphics.md` Phase 80 has since
  fixed EasyGL/Vulkan/Bgfx/WebGPU(`BasicEffect`-only)/D3D11/D3D12 too — only the `Software` renderer
  remains, deferred (no lighting engine of any kind exists there yet, a larger and differently-shaped
  gap, see Phase 80's own Task 1108 row).
- ~~`CnaTests` does not build under D3D9~~ — **fixed** (`D9-123`, 2026-07-15, same session `D9-130`
  was written in but not yet reflected here): all POSIX `::setenv()`/`::unsetenv()` call sites
  replaced with `System::Environment::SetEnvironmentVariable`; `CnaTests` compiles and the
  `gtest_discover_tests`/`CROSSCOMPILING_EMULATOR` wiring works under D3D9.

See `docs/directx9-renderer.md` for the full writeup and `docs/d3d9-divergence-report.md` for the raw
measurement this section summarizes.

## See also

- `docs/coverage.md` — non-Graphics namespace estimates (Audio/Media/Content/Net/GamerServices),
  still broadly accurate; its own Graphics section is superseded by this document.
- `docs/sdl-renderer-2d-completeness.md` — SDL_Renderer's own full Phase 70 audit in verbose detail.
- `docs/model-content-pipeline-support.md`, `docs/occlusionquery-support.md` — full detail for
  those 2 systems, summarized above.
- `docs/directx9-renderer.md`, `docs/d3d9-divergence-report.md` — the `DIRECTX9` column's full detail: what
  it proves, the oracle harness, and the honest DXVK-authenticity caveat every result inherits.
- Per-effect docs (`docs/basiceffect-support.md` etc.) — largely predate Tasks 885-900's
  fog/lighting/specular fixes on Vulkan/Bgfx; this matrix reflects the current state, those
  individual docs have not been refreshed (out of this task's own scope).
