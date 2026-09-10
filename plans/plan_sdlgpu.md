# SDL GPU Graphics Renderer — classic XNA / EasyGL parity plan and execution log

> **Current verdict (audit opened 2026-09-09): B. CLASSIC SDL GPU ↔ EASYGL PARITY NOT YET
> REACHED.** The current renderer is a substantial, real Vulkan-backed implementation, but live
> source and pixel tests demonstrate ordinary-XNA gaps in texture/render-target formats,
> independent MRT outputs and several effect permutations. Core instancing and multiple streams
> were closed by `SDLGPU-60`. This verdict supersedes
> historical completion banners below; those remain as implementation history, not current truth.

## 2026-09-09 parity audit status

| Item | Current evidence |
|---|---|
| Starting branch / commit | `sdlgpu` / `3a44315fdffe974e02a664cacae4fd388c9728aa` |
| Ending commit | In progress; update after the final adversarial sweep |
| Renderer contract | `modules/graphics/include/CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp` |
| Reference renderer | `modules/renderers/easygl/{include,src,examples}` |
| Renderer under test | `modules/renderers/sdl-gpu/{include,src,tests,examples}` |
| EasyGL / SDL GPU example sources | 246 / 38 `.cpp` files at audit start; 246 / 43 now (source count, not capability count) |
| EasyGL / SDL GPU renderer unit-test sources | 2 / 2 `.cpp` files |
| SDL GPU registered integration tests | 149 CTests (85 baseline plus 64 parity/remediation registrations) |
| Shared EasyGL parity fixtures available | 31 renderer-neutral sources in `modules/graphics/examples/parity` |
| Shared parity fixtures registered for SDL GPU | 18/31 (wireframe; compressed cube; HDR target; four sampler; four vertex-semantic; multi-stream; instancing; and five render-state fixtures) |
| Tasks created by this audit | 30 (`SDLGPU-55`–`SDLGPU-84`) |
| Completed / open / proven unavoidable | 21 / 9 tasks; 2 capability fields (`BlendState.MultiSampleMask` and exact half-rate `PresentInterval::Two`) are proven unavailable in current SDL_gpu and are not separate tasks; `SDLGPU-80` remains a candidate limitation |
| SDL GPU build | Stable `cmake-build-sdlgpu`, Debug, `CNA_GRAPHICS_RENDERER=SDL_GPU`, tests/examples ON |
| EasyGL oracle build | Stable `cmake-build-debug`, Debug, `CNA_GRAPHICS_RENDERER=OPENGL33`, tests/examples ON |
| Runtime driver | SDL 3.5.0 SDL_gpu Vulkan on AMD Radeon 780M / Mesa RADV 25.0.7; Khronos validation layer 1.4.309 present |
| Display constraint | Sandboxed tests cannot access host `:0`; SDL `offscreen` successfully creates the real Vulkan GPU device. Escalated preservation checks can use host `:0` (WebGPU), while EasyGL uses Xvfb `:179`. The SDL GPU test-driver cache setting defaults to `x11` and is locally set to `offscreen`. |
| Validation | Debug mode is enabled in current source. The offscreen 85-test baseline, SDLGPU-58's 14-test state suite, SDLGPU-67's 13-test viewport/scissor suite, SDLGPU-68's 13-test presentation/lifecycle slice, SDLGPU-69's 9-test 2D-format slice, SDLGPU-70's 8-test cube-format slice, SDLGPU-71's 6-test volume slice, SDLGPU-72's 4-test 2D-target-format slice and SDLGPU-73's 9-test cube/depth slice emitted no captured `VUID`, `Validation Error`, or `Validation Warning`; every new parity CTest makes those diagnostics fatal. |
| Focused EasyGL oracle baseline | 34/34 selected tests pass on Xvfb/llvmpipe: the prior 28 stock-effect/state/format/RT/texture/MRT/instancing/query tests plus six presentation/reset/resize/VSync programs. The VSync program's three CNA-forwarding checks pass; its real-vblank half is correctly skipped because raw GL cannot enable VSync under Xvfb. |

The first attempted baseline used the suite's historical hard-coded `SDL_VIDEODRIVER=x11` and
could not reach the inaccessible host display: early programs skipped and 47 tests failed for the
same environment reason. That result is retained as environment evidence, not misreported as a
renderer failure. The renderer-local `CNA_SDLGPU_TEST_VIDEO_DRIVER` cache setting makes the same
suite reproducible on this host without changing its `x11` default elsewhere.

The real offscreen/Vulkan baseline is **77/85 passed**. The eight failures are evidence and are not
hidden:

1. `SdlGpu_ConstructorExceptionSafety`: 234/264; its failure-injection oracle still hard-codes 23
   construction shaders while the live construction enum creates 25 (`SDLGPU-56`). Every observed
   failure remained transactionally balanced.
2. `SdlGpu_SkinnedEffect_Fog`: classic `SkinnedEffect` legs pass; only CNAEXT `PbrEffect` and
   `SkinnedPbrEffect` half-fog expectations fail. This is recorded as an existing out-of-scope
   regression and must not be worsened.
3. `SdlGpu_DepthBias`: the initial 56/57 SpriteBatch positive-bias failure is resolved by
   `SDLGPU-65`; the expanded test is now 67/67.
4. `SdlGpu_StockEffectSamplerContract` and `SdlGpu_DualTextureSlotSamplerContract`: the baseline
   refusal of their valid stride-28 independent-UV declaration and the exception-unwinding abort
   are resolved by `SDLGPU-59`; both complete with their validation-fatal oracles.
5. `SdlGpu_DescriptorCapacityContract`: the apparent 12/27 and 114/256 "exactness" failure was a
   baseline-reading defect, resolved by `SDLGPU-63`. Those are the deliberately Point-like cases;
   the other 15/27 and 142/256 deliberately Linear/Anisotropic cases must interpolate. All 29
   adversarial capacity/lifetime checks pass with zero wrong samples.
6. `SdlGpu_PointSamplingContract`: the initial 145/146 result is resolved by `SDLGPU-62`; the
  3D non-integer 3×3→10×10 PointClamp mapping now matches all 100 EasyGL/XNA texels.
7. `SdlGpu_GraphicsDevice_OrderedClear`: the initial 45/46 result was an oracle defect, resolved by
   `SDLGPU-66`: the supposedly untouched face had undefined new-resource contents. After seeding it
   with a known sentinel, all 46 checks pass and prove that two later face cycles preserve it.

### Definition and boundary

Parity means observable equivalence for the ordinary XNA 4.0 graphics surface that EasyGL
supports, using identical public inputs and exact or narrowly-toleranced readback. An internal
method named `*EXT` is in scope when an ordinary public constructor/property reaches it. Public
CNAEXT graphics (compute/storage/image bindings, indirect draws, GPU timers, modern render passes,
PBR/glTF additions, modern post-processing/shadows/IBL/particles) is `out`; existing support may be
preserved or fixed incidentally but is not expanded here. FNA under
`/rv/data/library/github.com/FNA-XNA/FNA` and existing shared fidelity tests arbitrate cases where
EasyGL itself appears wrong.

Legend: `=` equivalent with discriminating evidence; `~` implementation exists but evidence is
weaker or semantics remain uncertain; `≠` observable difference; `∅` absent; `⛔` demonstrated
underlying limitation with no correct reasonable emulation; `out` excluded modern CNAEXT.

### Feature-family matrix (initial live-source pass)

| Family | EasyGL reference | SDL GPU current state | State / owner |
|---|---|---|---|
| Clear color/depth/stencil overloads | Direct GL clears; ordered-clear corpus | Deferred clear commands and all combinations | `=` — ordered clear/draw/cube-target tests and state suites pass; `SDLGPU-58/66` |
| Present, interval and modes | Runtime swap interval; resize examples | SDL claim/present modes and proxy copy | `=` for Immediate/One/default forwarding, reset, resize and minimize retry; exact half-rate `PresentInterval::Two` is `⛔` because SDL exposes only VSYNC/IMMEDIATE/MAILBOX and no vblank timing primitive; `SDLGPU-68` |
| Backbuffer size/readback/channel order | Direct readback, RGBA contract | Real `backbufferProxy_` download, contrary to old SDLGPU-39 text | `=` — dimension/first-read/range plus reset/resize/minimize-retained readback pass; `SDLGPU-68` |
| Logical resolution/transforms/letterbox | Explicit default viewport and transforms | Explicit physical/logical transforms | `=` — physical rectangle, all modes, HiDPI transforms, zero-size safety, SpriteBatch projection and real resize pass; `SDLGPU-68` |
| Blend factors/functions/BlendFactor | Full separate RGB/A state | Complete immutable pipeline state and per-draw dynamic factor | `=` — all 13 factors in all four roles, all five equations for RGB/A, separate fields and A→B→A are pixel-verified; `SDLGPU-58` |
| ColorWriteChannels | Four slot-aligned masks | Four slot-aligned immutable pipeline masks | `=` — every slot-0 mask value plus distinct four-MRT-slot masks and cache restoration pass; `SDLGPU-58` |
| MultiSampleMask | EasyGL also documents non-default masks as unimplemented | SDL receives the value but cannot apply it | `⛔` — SDL 3.5 requires `sample_mask=0` and `enable_mask=false`; arbitrary-shader emulation cannot preserve sample coverage/depth/stencil side effects; `SDLGPU-58` |
| Depth compare/write | All comparisons | Complete immutable depth state | `=` — shared 8×3 signature matrix, exact EasyGL frame, direct EasyGL source and write-enable oracle pass; `SDLGPU-58` |
| Stencil masks/ops/two-sided/reference | Full front/back state | Complete immutable descriptor and per-draw dynamic reference | `=` — all eight compares and operations, masks, two-sided state and reference pass, including A→B→A; `SDLGPU-58` |
| Cull/scissor/viewport/depth range | Full GL state | Cull/bias immutable; scissor/viewport/range captured per draw | `=` — exact EasyGL sources, exhaustive deferred A→B→A draws, cube/RT2D/backbuffer switches and resize all pass; presentation-mode lifecycle remains separately `SDLGPU-68` |
| FillMode.WireFrame | Renderer-side triangle-edge expansion | Native `SDL_GPU_FILLMODE_LINE` in every ordinary pipeline | `=` — shared asymmetric three-edge fixture passes; `SDLGPU-61` |
| Constant/slope depth bias | GL polygon offset with XNA normalization | Per-format normalized-to-native conversion in every immutable pipeline; SpriteBatch uses projected layer depth | `=` — expanded 67/67 pixel/cache oracle, validation clean; `SDLGPU-65` |
| Sampler filter/address/anisotropy | All stock/effect slots | Every stock, compiled-effect and SpriteBatch command captures the complete state | `=` — shared pixel oracles plus 29/29 descriptor-capacity checks; `SDLGPU-64` |
| MaxMipLevel/LOD bias/AddressW | Applied on ordinary draws | MaxMipLevel/bias pixel-verified on stock 3D and SpriteBatch; W reaches the native descriptor | `=` for 2D/cube state delivery; volume AddressW remains `~` pending the ordinary compiled-effect sampling route in `SDLGPU-79` |
| Texture2D Color/mips/partial/NPOT/readback | Real upload + CPU/GPU read paths | Format-sized upload, shared CPU shadow and authored mips | `=` including 3D PointClamp; deferred lifetime sweep remains `SDLGPU-81` |
| Texture2D ordinary non-Color formats | Packed, DXT native-or-decode, SNORM | Exact packed/BC/SNORM storage where available; lossless packed and DXT decode fallbacks; NormalizedByte2 semantic expansion | `=` for EasyGL's exact nine-format set, including typed transfers, sampling, NPOT/partial DXT blocks and authored mips; remaining classic formats are truthfully refused by both public paths; `SDLGPU-69` |
| Texture3D Color/mips/boxes/readback | Native RGBA8 volume path | Native RGBA8 volume path | `=` — exact EasyGL sources prove slices, asymmetric boxes/readback and authored mips; expanded SDL test proves NPOT, cumulative writes, A→B→A and exact format forwarding; `SDLGPU-71` |
| Texture3D surface format | Public seam permits Color only; concrete EasyGL resource always allocates RGBA8 and ignores its internal argument | Resource-specific classifier supports Color, refuses the other 19 classic formats, and the concrete resource records/rejects its forwarded value | `=` — truthful Color-only reference surface; ordinary compiled-effect volume sampling remains independently `~` in `SDLGPU-79` |
| TextureCube faces/mips/partial/readback | Color and DXT cubes; packed formats are falsely accepted (defect ledger) | Color plus native-BC-or-decoded DXT1/3/5; explicit refusal for every other classic format | `=` for the truthful public surface: six faces, partial rectangles/blocks, authored mips, DDS/content readback and DXT sampling; `SDLGPU-70`, `EASYGL-PARITY-2` |
| RenderTarget2D Color/depth/MSAA/readback/mips | Full classic path | Exact per-target D16/D24-or-D32/D24S8-or-D32S8 storage, real resolve/readback/mips | `~` — depth storage/pipeline identity `=` via `SDLGPU-73`; preserve/zero-MSAA/transitions sweep remains `SDLGPU-74` |
| RenderTarget2D float/half/Rgba64 | Requested storage, runtime probed | Exact native storage, format-aware pipelines/transfers and XNA channel expansion | `=` — all nine EasyGL formats agree at query/construction, render, typed readback and sampling; `SDLGPU-72` |
| RenderTargetCube Color/faces/depth/MSAA/mips | Full classic path | Exact color/depth storage, six-face resolves/readback/sampling and mips | `~` — format/depth/pipeline identity `=` via `SDLGPU-73`; consolidated preserve/discard transitions remain `SDLGPU-74` |
| RenderTargetCube non-Color | Requested storage, runtime probed | Exact native cube storage and cube-specific runtime probe | `=` — all nine EasyGL formats retain six faces; float sampling preserves >1; `SDLGPU-73` |
| MRT binding/clear | Independent fragment outputs verified | attachments bind, but stock output only targets slot 0 | `≠`; `SDLGPU-75` |
| Vertex buffers/declarations/dynamic options | declaration semantic+offset driven | classic single-stream stock inputs use semantic/index/format/offset and declaration-aware pipeline keys | `=` for declaration interpretation; dynamic/range cases remain `~` in `SDLGPU-78` |
| Index buffers 16/32-bit/dynamic | Both sizes/options | Both sizes/options implemented | `~` — offsets/ranges/replacement sweep; `SDLGPU-78` |
| Draw primitive/indexed/user ranges/order | Full public family | ordinary variants and chronological queue exist | `~` — range/topology/base/start adversarial sweep; `SDLGPU-78` |
| Instancing/multiple vertex streams | Real per-instance divisors and streams | semantic multi-stream pipeline layouts; native instance rate with frequency materialization | `~` — core geometry/BasicEffect path has 69 shared pixel/state/lifetime tests plus two byte-identical EasyGL fixtures (`SDLGPU-60`); remaining stock-effect and compiled/custom permutations are `SDLGPU-77/79` |
| SpriteBatch geometry/sort/state | Broad 2D corpus | real deferred/immediate sprite renderer | `~` — shared geometry/state/font fixtures pending; `SDLGPU-76` |
| BasicEffect | broad lighting/fog/option corpus | real shader families | `~`; `SDLGPU-77` |
| AlphaTest/DualTexture effects | broad compare/source/UV corpus | real shaders; independent TEXCOORD0/1 is byte-exact with EasyGL | `=` for vertex input semantics; remaining effect combinations `~` in `SDLGPU-77` |
| EnvironmentMap/Skinned effects | broad light/specular/fresnel/bones corpus | real shaders; baseline classic fog passes | `~`; `SDLGPU-77` |
| Ordinary compiled effects | real compiled runtime | `SdlGpuCompiledEffect` exists | `~` — shared state/resource regression sweep; `SDLGPU-79` |
| Models where renderer participates | hierarchy/multi-mesh/skinning corpus | stock draw paths exist | `~`; `SDLGPU-79` |
| Deferred resource/state lifetime | immediate GL plus registries | shared-state command snapshots and lifetime tests | `~` — mutation/destruction sweep; `SDLGPU-81` |
| OcclusionQuery | native GL query (`any` on GLES, count on desktop) | inherited null creation; no query primitive in vendored SDL_gpu | candidate `⛔`, not final until `SDLGPU-80` closes |
| ShaderEffect/PBR/glTF/compute/storage/indirect/timers/modern passes | EasyGL CNAEXT families | some existing SDL GPU implementations | `out` — preserve, do not expand |

### Ordinary SurfaceFormat matrix (current)

The first twenty enum entries are the XNA-facing formats. `SDL native` means the vendored
`SDL_gpu.h` exposes an exact or semantics-compatible storage format; runtime
`SDL_GPUTextureSupportsFormat` still has to be asked for the intended usage. `EasyGL RT` is
driver-probed where noted. Since `SDLGPU-69`, Texture2D has an explicit renderer answer for all
twenty classic entries: it implements the same nine-format public set as EasyGL and explicitly
refuses the other eleven. `SDLGPU-70` closes the cube column with Color plus DXT1/3/5 and truthful
refusal of the other sixteen. `SDLGPU-71` closes the volume column at the reference renderer's
actual Color-only boundary: both implementations allocate RGBA8 and the public constructor now
obtains a resource-specific verdict instead of letting SDL silently discard its argument.
`SDLGPU-72/73` close both render-target format columns with the same nine-format set EasyGL
actually creates. `SDLGPU-73` additionally gives each target its own exact-or-more-precise
depth/stencil storage instead of substituting the swapchain's combined format.

| SurfaceFormat | EasyGL Texture2D/Cube | EasyGL render target | SDL native candidate | SDL current / task |
|---|---|---|---|---|
| Color | yes | yes | R8G8B8A8_UNORM | Texture2D/cube/volume/RT2D/RTCube `=` |
| Bgr565 | yes (desktop/ES3); cube classifier is a false positive | no | B5G6R5_UNORM | Texture2D `=` native-or-RGBA8; cube `=` truthful refusal (`EASYGL-PARITY-2`); volume `=` refusal |
| Bgra5551 | yes (desktop/ES3); cube classifier is a false positive | no | B5G5R5A1_UNORM | Texture2D `=` native-or-RGBA8; cube `=` truthful refusal (`EASYGL-PARITY-2`); volume `=` refusal |
| Bgra4444 | yes (desktop/ES3); cube classifier is a false positive | no | B4G4R4A4_UNORM | Texture2D `=` native-or-RGBA8; cube `=` truthful refusal (`EASYGL-PARITY-2`); volume `=` refusal |
| Dxt1 | native or renderer decode | no | BC1_RGBA_UNORM | Texture2D/cube `=` native-or-decode; volume `=` refusal |
| Dxt3 | native or renderer decode | no | BC2_RGBA_UNORM | Texture2D/cube `=` native-or-decode; volume `=` refusal |
| Dxt5 | native or renderer decode | no | BC3_RGBA_UNORM | Texture2D/cube `=` native-or-decode; volume `=` refusal |
| NormalizedByte2 | yes (desktop/ES3); cube prohibited by public rule | no | R8G8_SNORM | Texture2D `=` via semantic RGBA8_SNORM expansion; cube/volume `=` refusal |
| NormalizedByte4 | yes (desktop/ES3); cube prohibited by public rule | no | R8G8B8A8_SNORM | Texture2D `=` native; cube/volume `=` refusal |
| Rgba1010102 | public EasyGL path defers/refuses | no | A2R10G10B10/A2B10G10R10_UNORM | Texture2D/cube/volume `=` refusal |
| Rg32 | public EasyGL path defers/refuses | no | R16G16_UNORM | Texture2D/cube/volume `=` refusal |
| Rgba64 | public texture path defers/refuses | desktop RT yes | R16G16B16A16_UNORM | Texture2D/cube/volume `=` refusal; RT2D/RTCube `=` |
| Alpha8 | public EasyGL path defers/refuses | no | A8_UNORM | Texture2D/cube/volume `=` refusal |
| Single | public texture path defers/refuses | driver-probed R32F | R32_FLOAT | Texture2D/cube/volume `=` refusal; RT2D/RTCube `=` |
| Vector2 | public texture path defers/refuses | driver-probed RG32F | R32G32_FLOAT | Texture2D/cube/volume `=` refusal; RT2D/RTCube `=` |
| Vector4 | public texture path defers/refuses | driver-probed RGBA32F | R32G32B32A32_FLOAT | Texture2D/cube/volume `=` refusal; RT2D/RTCube `=` |
| HalfSingle | public texture path defers/refuses | driver-probed R16F | R16_FLOAT | Texture2D/cube/volume `=` refusal; RT2D/RTCube `=` |
| HalfVector2 | public texture path defers/refuses | driver-probed RG16F | R16G16_FLOAT | Texture2D/cube/volume `=` refusal; RT2D/RTCube `=` |
| HalfVector4 | public texture path defers/refuses | driver-probed RGBA16F | R16G16B16A16_FLOAT | Texture2D/cube/volume `=` refusal; RT2D/RTCube `=` |
| HdrBlendable | public texture path defers/refuses | driver-probed RGBA16F | R16G16B16A16_FLOAT | Texture2D/cube/volume `=` refusal; RT2D/RTCube `=` |

The seven `*EXT` enum entries after these twenty belong to modern/CNA-specific planning unless an
ordinary call path proves otherwise. They are not silently counted toward this parity verdict.

### Mechanical renderer-contract audit

This inventory follows the live modular interface, including dangerous defaults. “SDL explicit”
means a concrete override exists; it does not by itself mean parity. Method overloads in a group
were checked individually in the declarations and implementations.

| Contract hooks | EasyGL | SDL GPU | Public/scope result |
|---|---|---|---|
| `IVertexBufferRenderer::{SetData,SetDataWithOptions,SetVertexDeclaration,GetVertexCount}` | explicit | explicit | classic; declaration semantics are consumed faithfully for single-stream stock draws (`SDLGPU-59`); options/ranges remain `SDLGPU-78` |
| `IIndexBufferRenderer::{SetData16,SetData32,*WithOptions,GetIndexCount,IsThirtyTwoBit}` | all explicit | all explicit | classic; verify options/ranges (`SDLGPU-78`) |
| `IOcclusionQueryRenderer::{Begin,End,IsComplete,PixelCount,PixelCountIsPreciseEXT}` and factory | explicit | inherited null, no object | classic; candidate limitation (`SDLGPU-80`) |
| `ITextureRenderer::{GetWidth,GetHeight,UpdatePixels,UpdatePixelsLevel,HasDefinedMipLevel,GetSurfaceFormatEXT,ShareCpuPixels,GetData}` | explicit except default mip query behavior is implemented through EasyGL state | width/height/update/format/GetData explicit; mip query and CPU-share defaults remain appropriate because the XNA layer owns those shadows | classic format behavior `=` through `SDLGPU-69`; deferred lifetime remains `SDLGPU-81` |
| `ITexture3DRenderer::{SetData,GetData,BindGL,GetDimensionsEXT}` | upload/readback/bind explicit; concrete storage is RGBA8 | upload/readback explicit; concrete resource records and guards its Color format; GL/default dimensions irrelevant | classic Color storage/transfers `=` (`SDLGPU-71`) |
| `ITextureCubeRenderer::{SetData,SetCompressedDataEXT,GetData,BindGL,ShareCpuPixels,GetSizeEXT}` | all storage paths explicit | RGBA and DXT Set/Get explicit; exact DXT block shadow; size/cpu-share defaults are unused by ordinary SDL sampling | classic cube storage `=` through `SDLGPU-70`; `BindGL` is EasyGL-specific |
| `IRenderTargetRenderer::{Bind/Unbind,GetData,GetMultiSampleCount,GetAppliedDepthStencilFormatEXT,HasRealDepthBuffer,DepthBufferBitsEXT,HasRealStencilBuffer}` | explicit where native facts differ | bind/read/MSAA and all applied depth/stencil facts explicit from per-target state | classic format/property behavior `=` (`SDLGPU-72/73`); transitions remain `SDLGPU-74` |
| `IRenderTargetCubeRenderer::{GetSize,BindFace,Unbind,GetData,GetMultiSampleCount,depth/stencil facts}` | explicit where native facts differ | size/bind/read/MSAA and all applied depth/stencil facts explicit from per-target state | classic format/property behavior `=` (`SDLGPU-66/73`); transitions remain `SDLGPU-74` |
| `IEffectRenderer` compile/bind/unbind, uniform scalar/vector/matrix/arrays, 2D/cube/3D texture binds | all explicit | compile and scalar/vector/matrix explicit; bind/unbind no-op by design; array and texture defaults are covered by renderer-owned compiled path only | ShaderEffect is CNAEXT; ordinary compiled effect separately in scope (`SDLGPU-79`) |
| `ISpriteBatchRenderer::{Begin,End,SetTransformMatrix,SetCustomEffect,SetSampler*,SetImmediateMode,Draw overloads}` | explicit except default immediate hook | explicit for all public stock paths, including complete sampler state | classic; broad behavior still `~` (`SDLGPU-65/76`) |
| `AcquireThreadContextLeaseEXT`, surface changed/invalidated, context-loss hooks | context/registry explicit | surface change explicit; invalidation/recovery defaults retained | surface change is ordinary lifecycle and verified by `SDLGPU-68`; lease and simulated recovery are EasyGL-specific/CNAEXT, while resource lifetime remains `SDLGPU-81` |
| `Clear`, all six depth/stencil variants, legacy depth/blend/write toggles | explicit | explicit | classic; discriminating sweep (`SDLGPU-58/66`) |
| `Present`, viewport/default viewport, virtual resolution, presentation mode, swap interval, MSAA/application-format facts | explicit including default viewport and runtime facts | present/size/default-physical-viewport/virtual/mode/requested interval explicit; SDL-local applied-interval fact distinguishes fallbacks; applied-format hooks remain inherited | classic presentation behavior verified by `SDLGPU-68`; format facts remain `SDLGPU-69–74` |
| format classifiers, compressed transfer policy, half-float filtering | explicit, runtime/profile aware; volume inherits Color-only framework fallback | Texture2D, TextureCube, Texture3D, RT2D and cube-target classification explicit; half filtering still inherited | classic textures/volume/targets `=` through `SDLGPU-69–73`; remaining filtering evidence is owned by its sampler/effect tasks |
| coordinate transforms and `ReadBackbuffer` | explicit | explicit | classic; readback baseline passes (`SDLGPU-68`) |
| texture/sprite/RT2D/RTCube/Texture3D/TextureCube factories, including format-bearing `*EXT` RT factories | explicit | Every texture and target factory preserves its classified format; cube and 2D targets retain per-resource depth facts | classic format/factory surface `=` (`SDLGPU-69–73`); usage transitions remain `SDLGPU-74` |
| `SetRenderTarget2D`, `SetRenderTargetCubeFace`, `SetRenderTargets` | all explicit | 2D and plural explicit; single cube-face hook inherited and delegates to plural | classic; cube ordering verified, MRT semantics remain `SDLGPU-75` |
| blend/depth/raster/sampler applications; BlendFactor/reference/scissor/viewport | all explicit | all explicit | classic; immutable-key and propagation verification (`SDLGPU-58/63/65`) |
| buffer factories and colored/extended primitive/indexed draw hooks | explicit | explicit | classic; semantic stock dispatch verified by `SDLGPU-59`, ranges remain `SDLGPU-78` |
| `DrawInstancedPrimitivesEx`, `GetMaxVertexStreams`, multistream capability | explicit | explicit stock-effect draw, eight-stream limit and truthful capabilities | classic XNA 4.0; `=` for stock BasicEffect/multi-stream semantics (`SDLGPU-60`), compiled/custom/skinned combinations remain named boundaries for `SDLGPU-77/79` |
| `SupportsDepth*`, `Ensure3DSupported`, unsupported-call behavior, `CanBeginDrawEXT` | runtime-aware | depth/stencil availability is now exposed through explicit capability answers; other defaults retained where semantically applicable | classic capability/profile truthfulness (`SDLGPU-57`) |
| `SupportsCapability`, numeric texture/cube/volume/RT limits, limitations text | explicit runtime answers | explicit exhaustive capability switch, eight stock-effect streams and limitations text; format/device limits remain owned by their dedicated tasks | publicly observable; false promises removed by `SDLGPU-57`, stream claims verified by `SDLGPU-60` |
| compiled-effect factory/runtime/support | explicit | explicit | ordinary Effect bytecode path in scope (`SDLGPU-79`) |
| ShaderEffect dialect/source execution | EasyGL explicit | SDL dialect/compile path explicit | existing CNAEXT, `out` |
| compute/storage/image, indirect draw, barriers, GPU timer, shadow/IBL, display-color-space extensions | EasyGL implements a subset | SDL mostly inherits safe false/no-op defaults | modern CNAEXT, `out` |

### EasyGL example-corpus classification

`plans/sdlgpu_easygl_example_classification.csv` contains exactly one sorted row for every live
EasyGL example source. `tools/check_sdlgpu_easygl_example_classification.py` compares it to the
filesystem, rejects duplicates/unknown categories/missing evidence, and currently reports:

| Category | Count |
|---|---:|
| `classic-xna-covered-by-existing-sdlgpu-test` | 116 |
| `classic-xna-direct-parity` | 38 |
| `classic-xna-new-sdlgpu-test-needed` | 28 |
| `classic-xna-feature-missing` | 4 |
| `modern-cnaext-out` | 51 |
| `easygl-specific` | 9 |
| `duplicate` | 0 |
| `easygl-defect` | 0 |
| `unclassified` | **0** |

The `easygl-defect` count describes the 246 files physically under EasyGL's example directory.
The defects below were exposed by shared renderer-neutral evidence and the mechanical cube
contract audit, so they are recorded in the ledger without falsely reclassifying an unrelated
EasyGL example source.

Classification is a triage statement, not proof of parity. The 116 “covered” programs map to
existing SDL GPU family tests or shared graphics regressions; `SDLGPU-81` must register the same
31 renderer-neutral parity sources and confirm their discriminating checks. The 38 direct rows now
include exact EasyGL presentation/lifecycle sources compiled under SDL GPU by `SDLGPU-68`, the
packed/DXT/format-refusal sources from `SDLGPU-69`, cube and volume sources from `SDLGPU-70/71`,
the exact RenderTarget2D properties source from `SDLGPU-72`, and both cube property/depth sources
from `SDLGPU-73`. A row moves whenever implementation evidence disproves its classification.

### EasyGL defects discovered by the SDL GPU parity oracle

| ID | Evidence and disposition |
|---|---|
| `EASYGL-PARITY-1` | `EasyGLRenderer::ApplyBlendState` disables blending whenever both RGB and alpha use One/Zero factors, without also requiring both equations to be Add. The SDLGPU-58 equation oracle demonstrated that ReverseSubtract then copies the source under EasyGL instead of evaluating `0 - source`; SDL GPU now evaluates the public XNA equation correctly. This EasyGL defect is not copied and is not an SDL parity blocker. A future EasyGL-local task should add the same two regression legs and include the blend functions in its opaque shortcut. |
| `EASYGL-PARITY-2` | EasyGL's default cube classifier inherits the Texture2D verdict and therefore reports Bgr565/Bgra5551/Bgra4444 Supported, but `EasyGLTextureCubeRenderer::CreateResources` allocates RGBA8 for every non-DXT cube and the public `TextureCube` exposes no packed-element transfer overload. Construction thus promises a packed cube that cannot be faithfully written or read. SDL GPU deliberately refuses these three formats; copying the false acceptance would reproduce a reference defect, not parity. |
| `EASYGL-PARITY-3` | `EasyGLRenderTargetRenderer::GetData` calls `glReadPixels` without setting `GL_PACK_ALIGNMENT=1`, then treats the destination as tightly packed. An exact 5×3 `HalfSingle` readback therefore asks GL to write 12 physical bytes per 10-byte logical row into the public 30-byte buffer; the first row comparison fails and the overwrite can corrupt the following test. The same oracle passes at a 6-pixel alignment-safe width, while SDL GPU also passes the original 5×3 transfer exactly. SDL retains tight format-sized staging rather than copying this EasyGL defect; a future EasyGL-local fix must save, set and restore pack alignment. |

## Executable EasyGL-parity backlog

Each task below is one commit. Every implementation task includes its test/oracle and plan evidence
in the same commit. `⬜` open, `🟨` in progress or evidenced but not accepted, `✅` accepted, `⛔`
underlying API limitation proven after reasonable emulation analysis.

### SDLGPU-55 — establish a reproducible authoritative baseline ✅

- **Problem/public behavior:** the historical plan cannot reproduce current renderer tests on a
  machine without accessible X11 and contains obsolete capability claims.
- **Evidence:** EasyGL=246 sources, SDL GPU=38 sources/85 CTests; old x11 run is environmental while
  SDL offscreen/Vulkan gives the real 77/85 result above.
- **Location:** this dated plan section, classification CSV/checker, renderer-local example CMake.
- **Acceptance/test:** CMake defaults remain x11; the stable build can select offscreen; checker sees
  246/246 and no unclassified rows; record exact builds, drivers, failures and live source paths.
- **Result (2026-09-09):** accepted. Stable SDL GPU targeted build succeeded; real offscreen/Vulkan
  suite completed 77/85 with all eight failures classified above; focused EasyGL oracle completed
  27/27; the classification checker reports 246 rows and zero unclassified; `git diff --check`
  passes. No clean rebuild was used.

### SDLGPU-56 — repair the constructor exception-safety oracle ✅

- **Problem/public behavior:** the test stops after 23 shaders although construction now owns 25,
  leaving two allocation failure points untested; production transactionality is public lifecycle
  behavior.
- **Evidence:** EasyGL resource-registry tests cover construction/destruction; SDL baseline is
  234/264 with every failure balanced and both newer PBR vertex shaders absent from the oracle.
- **Location:** SDL GPU construction failure enum/test and renderer construction shader list.
- **Acceptance/test:** derive or explicitly share the authoritative count, inject every acquisition,
  prove zero leaked handles and successful recovery, with validation fatal.
- **Result (2026-09-09):** accepted. Added distinct failure points for the PBR vertex-color and
  skinned-PBR vertex-color shader acquisitions (they previously reused earlier points and were
  unreachable by injection), tied production `ConstructionShader::Count` to the public-internal
  contiguous failure-point count with `static_assert`, and made the test derive both its case count
  and success expectation from that value. Incremental target build succeeded;
  `SdlGpu_ConstructorExceptionSafety` passes in 14.93 s on offscreen/Vulkan with validation fatal.

### SDLGPU-57 — make capability and numeric-limit reporting truthful ✅

- **Problem/public behavior:** SDL GPU inherits broad `true`/unbounded defaults for unsupported
  wireframe, instancing, occlusion and device-dependent limits.
- **Evidence:** EasyGL switches every capability from live GL facts; SDL has no override while its
  corresponding factories/draw hooks are null/throw/default.
- **Location:** `SdlGpuRenderer::{SupportsCapability,GetMax*}` and capability tests.
- **Acceptance/test:** every public capability agrees with a successful discriminating operation or
  a truthful refusal; A→feature invocation produces no false-supported state.
- **Result (2026-09-09):** accepted. SDL GPU now enumerates every `GraphicsCapability` instead of
  inheriting catch-all true, runtime-probes 2× color+depth MSAA, reports the current one-stream
  encoder limit, and contributes exact qualitative limitations. MRT reports false until distinct
  outputs exist, while its already-supported multi-attachment bind/clear path remains callable.
  Compiled effects follow the optional build's real `SupportsCompiledEffects()` result. Incremental
  build succeeded; `SdlGpu_Smoke` passes 28/28 capability/lifecycle checks and the relinked
  `SdlGpu_MRT` compatibility test also passes; the non-production SDL ratchet remains at/below its
  existing budget.

### SDLGPU-58 — exhaustively verify immutable blend/depth/stencil/raster keys ✅

- **Problem/public behavior:** partial tests cannot prove every XNA state or cache identity.
- **Evidence:** EasyGL has factor/function/compare/mask/op/two-sided suites and shared equation
  fixtures; SDL descriptors look complete but are not fully discriminated.
- **Location:** pipeline key/creation and shared parity fixtures.
- **Acceptance/test:** all blend factors/functions/separate alpha/BlendFactor/write masks/sample
  mask, all depth compares, stencil masks/ops/two-sided/reference, cull/scissor/bias pass pixel
  oracles including A→B→A, with no validation output.
- **Result (2026-09-10):** accepted, with the one rigorously bounded SDL_gpu limitation called out
  below. Mechanical inspection confirms that every pipeline-static field used by the SDL
  descriptors is also represented in `PipelineCacheKey`: topology; depth enable/write/function;
  active color count/formats; sample count; depth/stencil format; blend enable and all six separate
  factor/equation fields; every active write mask; cull/fill; normalized constant/slope bias; and
  stencil enable, masks, front operations/function, two-sided enable and back operations/function.
  The dynamic blend constant, stencil reference, viewport and scissor are captured per queued draw
  and replayed through SDL_gpu's dynamic commands instead of being incorrectly baked into or read
  late from that key.

  The shared blend oracle now exhausts all 13 `Blend` values independently as color source,
  color destination, alpha source and alpha destination; all five `BlendFunction` values for both
  color and alpha; all 16 slot-0 channel masks; independent alpha/color fields; a nontrivial
  `BlendFactor`; and function A→B→A. This exposed and fixed one real SDL bug: One/Zero factors only
  permit the disabled-blend copy shortcut when both equations are Add. Two SDL-specific pixel legs
  retain ReverseSubtract One/Zero for color and alpha, because the old shortcut would respectively
  return the source color/source alpha instead of zero. The same experiment exposed
  `EASYGL-PARITY-1` above; the EasyGL bug is documented rather than copied.

  The depth fixture distinguishes all eight compares with the unique nearer/equal/farther
  signatures. The new stencil fixture does the corresponding reference-less/equal/greater sweep
  for all eight stencil compares and includes Always→Never→Always restoration. Existing shared and
  verbatim EasyGL sources additionally discriminate all eight stencil operations (including
  wrapping versus saturation at zero), read/write masks, fail/depth-fail/pass operations,
  two-sided front/back state, dynamic reference, depth-write enable, culling, scissor, viewport and
  bias. Four complete shared output frames—blend, depth, stencil operation and stencil compare—are
  byte-identical between EasyGL and SDL GPU (**maximum channel difference 0**).

  `BlendState.MultiSampleMask` is the sole unavoidable field in this family. Vendored SDL 3.5's
  `SDL_gpu.h` lines 1862–1863 mark `SDL_GPUMultisampleState::sample_mask` and `enable_mask` reserved
  for future use and require `0`/`false`. Rewriting arbitrary compiled/stock fragment shaders to
  discard by sample ID would not reproduce coverage before depth/stencil or all sample side
  effects, so there is no correct general renderer-side emulation. EasyGL also explicitly leaves
  non-default masks unimplemented. The all-ones default remains equivalent; non-default masks are
  classified `⛔`, never falsely claimed as working.

  Targeted incremental builds only were used. The SDL state selection passes **14/14 CTests** on
  offscreen Vulkan: five shared state fixtures, eight verbatim EasyGL sources, and the expanded
  20/20 SDL render-state regression. All have validation-error/warning fatal patterns and emitted
  none. `SdlGpu_MRT`, `SdlGpu_ColorWriteChannels` and the 67/67 `SdlGpu_DepthBias` control also pass,
  making the final focused SDL run **17/17**. The five shared controls pass **5/5** on EasyGL/Xvfb.
  The audit classifier remains complete at 246/246 with zero unclassified rows; eight now-direct
  state sources moved to direct parity. This periodic re-audit also moved the completed SDLGPU-59
  declaration and SDLGPU-64 sampler leads, plus the depth-bias source, out of stale gap buckets;
  custom ShaderEffect instancing remains correctly owned by `SDLGPU-79`.

### SDLGPU-59 — resolve stock vertex inputs from VertexDeclaration semantics ✅

- **Problem/public behavior:** valid declarations are selected by byte stride/fixed layouts and
  stride-28 DualTexture TEXCOORD0/1 is rejected.
- **Evidence:** EasyGL binds `(VertexElementUsage,index,offset)`; two SDL baseline tests skip then
  abort on precisely this declaration; shared `StockVertexSemantics.hpp` already encodes fidelity.
- **Location:** SDL stock program/pipeline vertex-input resolution and affected fixtures.
- **Acceptance/test:** reordered/padded/position-normal/lit-color/independent-UV declarations render
  the same pixels as EasyGL; missing required semantics throw precise errors; fixture exceptions
  safely unbind targets.
- **Result (2026-09-10):** accepted. Every classic single-stream stock family now resolves native
  vertex attributes by `(usage, usageIndex)` with the declaration's own format, offset and stride.
  The resolved layout—including optional inputs supplied by the reference-compatible neutral
  `(0,0,0,1)` record—is part of immutable pipeline identity. Program selection likewise uses
  Normal/Color/TexCoord semantics rather than ambiguous record sizes, and DualTexture consumes
  TEXCOORD0 and TEXCOORD1 independently. Lit vertex colour is carried to the shader and gated by
  `VertexColorEnabled`; absent ordinary stock textures reach the existing neutral-white binding.
  A declaration lacking mandatory POSITION0 throws a precise `NotSupportedException`. The two
  previously aborting fixtures now restore their render target and device state even if a future
  draw diagnostic throws.

  Verification uses identical renderer-neutral sources on EasyGL and SDL GPU. Reordered/padded
  vertex semantics, lit vertex colour, position+colour program selection, and independent dual UV
  frames are byte-identical (**maximum channel difference 0** for all four). Those four tests,
  `SdlGpu_3D` (including the missing-POSITION0 rejection), and both previously blocked sampler
  contracts pass **7/7 CTests** on Vulkan with validation diagnostics fatal. Seven broader classic
  effect regressions (Effects, EnvironmentMap, EnvironmentMapEmissive, Skinned,
  SkinnedEffectVertexColor, SkinnedEffect_WorldNormal and Effects_Fog) also pass. The separate
  CNAEXT PBR fixtures remain outside this parity task; their existing current-tree failures were
  reproduced and traced through their unchanged stride-derived queue/issue path rather than hidden
  or misreported as classic evidence. Multi-stream input and ordinary BasicEffect instancing are
  now closed by `SDLGPU-60`, while deeper Skinned declaration coverage remains with `SDLGPU-77`.

### SDLGPU-60 — implement ordinary XNA instancing and multi-stream input ✅

- **Problem/public behavior:** `DrawInstancedPrimitives` and multiple `VertexBufferBinding`s are
  ordinary XNA 4.0 but SDL inherits a throwing draw and false max-stream default.
- **Evidence:** EasyGL uses stream offsets/frequencies/divisors; SDL_gpu exposes multiple vertex
  bindings plus `SDL_GPU_VERTEXINPUTRATE_INSTANCE`.
- **Location:** buffer bindings, SDL vertex input/pipeline keys, deferred draw command.
- **Acceptance/test:** shared split-stream and instanced fixtures pass, including nonzero vertex/
  base/start/instance offsets, per-instance signatures, frequency and lifetime snapshots.
- **Result (2026-09-10):** accepted. The ordinary stock-effect dispatcher now collects every
  bound per-vertex stream, selects the shader family across the complete declaration set and
  resolves each consumed semantic to its declaring stream's own format, offset and stride.
  Pipeline identity includes the resolved stream table and input rate; replay binds the densely
  resolved buffers plus the optional neutral record. Each queued command copies every stream at
  the public draw call, so later rebinding, updates, wrapper destruction and allocator address
  reuse cannot alter an issued draw.

  `DrawInstancedPrimitivesEx` now resolves POSITION0/COLOR0 semantically and the four world-matrix
  columns positionally across any per-instance declarations, matching EasyGL's declaration-order
  rule. SDL_gpu's native `SDL_GPU_VERTEXINPUTRATE_INSTANCE` supplies the step mode. Because its
  descriptor has no divisor field, XNA `InstanceFrequency > 1` is emulated exactly by materializing
  source record `VertexOffset + instance / frequency` once per submitted instance. The native
  indexed draw preserves 16/32-bit indices, `startIndex`, signed `baseVertex` and each stream's own
  element offset. Missing matrix columns, incompatible formats and the still-unimplemented
  compiled/custom-effect instanced combinations raise named exceptions rather than drawing from a
  subset. Capabilities now report multi-stream input and instancing, with an honest eight-stream
  stock resolver limit (SDL_gpu itself exposes 16 slots/attributes). This closes the core
  geometry path; texture, lighting, AlphaTest, DualTexture, EnvironmentMap, Skinned and compiled
  effect permutations remain explicitly tracked by `SDLGPU-77/79` and therefore keep the
  feature-family matrix at `~` rather than overclaiming whole-effect parity.

  Verification is deliberately redundant: `OrdinaryDrawMultiStreamTest` passes **20/20** and
  `InstancedDrawMultiStreamTest` passes **28/28**, covering two per-vertex plus two per-instance
  streams, non-contiguous/dropped bindings, independent offsets, `vertexStart`, `startIndex`,
  `baseVertex`, 16/32-bit indices, frequencies 1/2/3, A→B→A pipeline/binding sequences, dynamic
  replacement, deferred snapshots and wrapper death. The newly enabled diffuse/vertex-color
  instancing suites pass another **21/21** checks, including non-neutral RGB/alpha multiplication,
  enabled/disabled transitions, packed stride 24, a nonidentity effect World composed after the
  instance World, and queued state/data lifetime. The shared
  `multi_stream_split` and `instanced_draw` sources pass under both renderers and their complete
  RGBA8 dumps are byte-identical (SHA-256 respectively
  `bef17637de93bbc0ce6bfd85f342dafb322968cd1f9ea494d5b29e708b6957d4` and
  `cd2d27772b4d72f808185524fbc87fd7f976924911e306bc9da32c4ad4ae2842`). Both SDL CTests make
  validation diagnostics fatal. The construction failure matrix also passes after adding the new
  instanced shader acquisition, proving transactional release at every stage.

### SDLGPU-61 — verify and advertise FillMode.WireFrame ✅

- **Problem/public behavior:** the parity audit initially classified XNA wireframe as absent and
  the capability profile reported false, despite the live renderer already baking the requested
  fill mode into its immutable pipelines.
- **Evidence:** `FillRasterizerState` maps the queued state snapshot to
  `SDL_GPU_FILLMODE_LINE`; SDL 3.5 exposes that native polygon-line mode. EasyGL's implementation
  is a renderer-side edge expansion, so output—not implementation technique—is the contract.
- **Location:** capability profile/report and the shared EasyGL/WebGPU wireframe fixture.
- **Acceptance/test:** the same asymmetric triangle under Solid and WireFrame has a filled versus
  empty interior, all three wire edges have positive coverage, the images are distinct, the
  capability is true, and validation diagnostics are fatal.
- **Result (2026-09-09):** the unchanged native line-fill implementation passes all ten shared
  discriminators on Vulkan: Solid fills 169/169 interior pixels, WireFrame fills 0/169, all three
  wire-edge probes contain exactly nine lit pixels, and the two interiors differ by 246. The
  renderer now reports `WireFrame=true` and no longer lists it as a limitation; the expanded
  30/30 smoke profile agrees. Both tests treat validation diagnostics as fatal.

### SDLGPU-62 — match PointClamp texel-center sampling for 3D draws ✅

- **Problem/public behavior:** a 3×3 texture scaled to 10×10 changes texels at different pixels.
- **Evidence:** shared `SdlGpu_PointSamplingContract` passes 145/146; only non-integer 3D mapping
  differs from its EasyGL/XNA oracle.
- **Location:** stock textured vertex shader UV/pixel-center transform and sampling fixture.
- **Acceptance/test:** exact 100-pixel signature matches the EasyGL control without regressing the
  already exact SpriteBatch and integer-scale cases.
- **Result (2026-09-09):** every queued stock 3D family now post-multiplies its WVP by EasyGL's
  measured 63/64-of-a-pixel Direct3D 9 correction in X and Y, using the exact viewport captured at
  draw time. Multisampled destinations deliberately retain the unshifted matrix, matching EasyGL's
  established coverage rule. `SdlGpu_PointSamplingContract` passes 146/146: the non-integer U2 leg
  moved from 19 mismatches to zero while all SpriteBatch, integer-scale, custom-viewport, address,
  minification and linear-filter controls remain exact. The 3D, Effects, EnvMap, EnvMapEmissive,
  Skinned, BasicEffect fog and DepthBias regressions pass. The MSAA target control exposed an
  unrelated invalid negative-Z depth fixture left latent by `SDLGPU-65`; it is isolated as
  `SDLGPU-84`. Native validation diagnostics are fatal.

### SDLGPU-63 — verify sampler identity at descriptor capacity ✅

- **Problem/public behavior:** determine whether rotating 27 sampler states or 256 live textures
  aliases native sampler/texture bindings or exhausts a hidden descriptor pool.
- **Evidence:** the initial audit incorrectly read 12/27 and 114/256 exact samples as failures. The
  fixture intentionally rotates all nine `TextureFilter` values: five magnify linearly, so 15/27
  sampler-only draws and 142/256 non-uniform texture draws must interpolate; the remaining Point-like
  cases must reproduce byte-encoded identities exactly.
- **Location:** sampler cache key, binding tables, descriptor-capacity rollover and resource cycling.
- **Acceptance/test:** every adversarial sample remains exact across capacity/rollover and A→B→A;
  bounded cache/resource behavior and validation cleanliness are proved.
- **Result (2026-09-09):** all **29/29** checks pass on the real Vulkan SDL_gpu path. The test proves
  1/2/31/32/63/64/65/66/96/128/256 simultaneously-live textures; all 27 filter/address
  combinations; 256 live texture+sampler pairs; Point-vs-Linear native distinctness; repeated
  identical state; 256 create/draw/read/destroy cycles; 65 live render targets; 96 DualTexture,
  70 AlphaTest and 70 EnvironmentMap effect resources; 96 SpriteBatch flushes and one 96-sprite
  batch; indexed/non-indexed/user draws; 320 released-resource cycles; recreation/interleaving; and
  70 live cubes. Every wrong/unreadable counter is zero. `SamplerCacheKeyEXT` compares and hashes
  filter, U/V/W address modes, clamped anisotropy, MaxMipLevel and the exact LOD-bias bit pattern.
  Vulkan validation errors and warnings are now fatal for this contract. No production defect was
  present and no renderer code was changed.

### SDLGPU-64 — propagate every ordinary sampler field on every stock path ✅

- **Problem/public behavior:** MaxMipLevel, MipMapLevelOfDetailBias and volume AddressW are accepted
  but not captured by all stock/SpriteBatch commands.
- **Evidence:** EasyGL effect corpus varies these; SDL header explicitly says only compiled effects
  receive extended mip state while stock commands snapshot filter/address/anisotropy.
- **Location:** sampler snapshots in all stock effect and sprite command records, volume sampling.
- **Acceptance/test:** shared filter/max-mip/LOD-bias/sprite-state fixtures plus AddressU/V and
  anisotropy slot tests yield deliberately different mip/edge colors; AddressW reaches the native
  descriptor without aliasing. The three-colour volume observation is retained by the dependent
  volume-sampling task `SDLGPU-79`, because this task does not invent a new public shader API.
- **Result (2026-09-09):** every SDL GPU stock command now snapshots filter, U/V/W address modes,
  anisotropy, MaxMipLevel and LOD bias per texture slot and passes the complete description to the
  already lossless native sampler cache. This includes both `DualTextureEffect` slots, the base and
  cube samplers of `EnvironmentMapEffect`, every built-in textured family, PBR preservation paths,
  and SpriteBatch. `SDL_GPUSamplerCreateInfo::address_mode_w` now uses the captured W mode rather
  than a hard-coded clamp. The shared SpriteBatch contract was also corrected: `Begin` assigns the
  resolved sampler to `GraphicsDevice.SamplerStates[0]` as FNA does, and a new complete common hook
  carries all seven properties instead of silently dropping four. EasyGL and WebGPU adopted the
  hook in the same task to preserve their ordinary-XNA behavior; WebGPU's sprite shader uses its
  existing `textureSampleBias` emulation.

  Verification is discriminating, not construction-only. On the real SDL_gpu Vulkan path the four
  shared sampler fixtures pass, including authored mip colours under natural, +1 and -2 bias and a
  MaxMipLevel clamp; the revised SpriteBatch fixture also compares all seven retained device
  properties. Existing `SdlGpu_SamplerState`, both environment-map cube sampler contracts and the
  29-check descriptor-capacity/lifetime stress all pass (**8/8 CTests total**) with validation
  diagnostics fatal. The same four fixtures pass on EasyGL. WebGPU shader validation and the
  corrected fixture pass on the host GPU, and direct EasyGL↔WebGPU frame comparison is byte-exact
  (max difference 0). `SDLGPU-59` subsequently removed the stock/dual contracts' stride-28
  declaration blocker; both now finish successfully, preserving their sampler evidence. AddressW
  is delivered to the native descriptor, but a three-colour volume pixel oracle cannot run until
  SDL GPU has the ordinary compiled/volume sampling path tracked by `SDLGPU-79`; that row retains
  the final end-to-end proof rather than leaving it unowned.

### SDLGPU-65 — honor SpriteBatch depth and slope bias ✅

- **Problem/public behavior:** SpriteBatch under a positive depth bias fails to move behind the
  reference geometry.
- **Evidence:** EasyGL depth-bias oracle passes; SDL baseline is 56/57 while 3D bias and A→B→A pass.
- **Location:** sprite pipeline key/descriptor depth-bias fields.
- **Acceptance/test:** positive, negative, slope and combined sprite cases pass against coplanar and
  sloped geometry, target-format A→B→A, validation clean.
- **Result (2026-09-09):** accepted for every currently supported depth attachment. FNA3D's own
  SDL_gpu driver showed the missing semantic conversion: XNA supplies normalized depth, while
  SDL_gpu consumes native r-units (`D16: 2^16−1`, `D24: 2^24−1`, `D32F: 2^23−1`). Every SDL GPU
  pipeline now converts against its actual depth format, includes the converted value in immutable
  cache identity, and explicitly enables XNA depth clipping. Sprite vertices now carry their
  projected `layerDepth` (`0.5 - depth/2`) instead of the old hard-coded near-plane Z=0. The
  discriminators use a normalized `0.0005` constant that would be ineffective if passed through,
  and cover positive/negative/combined bias, flat slope, A→B→A, deferred state lifetime and
  nonzero layer depth and positive/negative/combined bias on a public transform-created sloped
  sprite. Incremental build succeeded; `SdlGpu_DepthBias` passes 67/67 on Vulkan/D32F
  with validation enabled. Focused `SdlGpu_Smoke`, `SdlGpu_2D`, `SdlGpu_3D`, and
  `SdlGpu_ShaderEffect` regressions also pass. Cross-format A→B→A remains assigned to the format
  tasks because this baseline exposes only D32F/S8 and SDL GPU does not yet create other requested
  depth formats.

### SDLGPU-66 — isolate ordered clears and draws per cube face ✅

- **Problem/public behavior:** the ordered-clear baseline appeared to show work queued for one cube
  face retroactively affecting a supposedly untouched face.
- **Evidence:** baseline T4 was 45/46 because an unwritten face read as the same yellow used by a
  later face clear. EasyGL returned transparent black, but new render-target contents are undefined;
  neither value proves or disproves isolation. Existing SDL tests already paint all six faces with
  distinct values and preserve face 0 after five later writes.
- **Location:** shared ordered-clear oracle; no renderer behavior was changed.
- **Acceptance/test:** seed the control face with a distinct known value, then issue draw/clear
  sequences against two other faces; all three exact readbacks, six-face tests, repeated switches,
  pass-boundary tests and validation remain clean.
- **Result (2026-09-09):** T4 now initializes PositiveY to cyan before the NegativeX and PositiveZ
  subject cycles and requires byte-exact cyan afterward. SDL GPU passes all 46 ordered-clear checks;
  EasyGL passes all 49 of its enabled checks. The existing SDL cube target suite also proves six
  distinct faces, and the depthless-cube suite covers all faces plus producer/consumer switching.
  Its depth discriminator was also corrected from invalid identity-projection Z=−0.5 (outside
  XNA/DirectX's 0..W clip volume) to valid near/far depths 0.25/0.75; it passes 7/7 without undoing
  `SDLGPU-65`'s XNA depth clipping. The SDL ordered-clear CTest now makes native validation
  diagnostics fatal.

### SDLGPU-67 — verify viewport/scissor/depth-range capture across target changes ✅

- **Problem/public behavior:** deferred draws must retain the exact viewport, MinDepth/MaxDepth and
  scissor active at issue time and restore full target/backbuffer viewports on switches.
- **Evidence:** many SDL targeted tests pass; EasyGL has subregion/state/RT switch controls but the
  complete resize/logical-resolution sequence is not shared.
- **Location:** draw snapshots, target-pass setup and viewport conversion.
- **Acceptance/test:** adversarial A→B→A viewport/scissor/depth-range sequences across differently
  sized RT2D/cube/backbuffer and resize match EasyGL invariants.
- **Result (2026-09-10):** accepted without a production change: the live draw queue already
  snapshots viewport bounds, MinDepth/MaxDepth, scissor enable and rectangle per public draw. The
  stale SDL contracts in both exhaustive deferred fixtures were corrected to exercise the real
  proxy-backed backbuffer readback; they now complete **39/39** viewport and **47/47** scissor
  checks instead of skipping the backbuffer half. Six missing shared/oracle programs are now
  registered under SDL GPU, including the exact EasyGL `viewport_state`, `viewport_subregion` and
  `scissor` sources. The existing renderer-neutral cube fixture now dirties state and proves the
  exact cube 32×32 → RT2D 19×23 → backbuffer viewport, depth-range and scissor resets. The focused
  SDL GPU suite passes **13/13** with `VUID`/validation diagnostics fatal; the corresponding seven
  EasyGL oracles pass **7/7** on Xvfb. The corpus checker remains 246/246 with zero unclassified and
  promotes all three reused EasyGL sources to direct parity.

### SDLGPU-68 — close presentation, resize, reset and minimize lifecycle parity ✅

- **Problem/public behavior:** GDM/PresentationParameters changes, zero-size windows, VSync modes,
  backbuffer dimensions/readback and logical transforms must remain coherent.
- **Evidence:** before the fix SDL inherited `GetDefaultViewportRect()`, so the common discriminator
  passed only 2/6: Letterbox returned logical `(0,0,400×160)` instead of centred physical
  `(0,80,800×320)`, Overscan returned the same logical rectangle instead of
  `(-200,0,1200×480)`, and Stretch/FixedHeight returned 400×160 instead of 800×480. SDL also
  inherited `OnSurfaceChanged()`, ignored display scale in both coordinate transforms and exposed
  no requested interval fact. `SetVirtualResolution()` additionally overwrote physical drawable
  dimensions while Native mode was active, conflating two separate facts. The live proxy path did
  already provide real readback; this row tested rather than replacing it.
- **Location:** presentation setters, swapchain claim/reclaim, proxy recreation, viewport facts.
- **Acceptance/test:** same public reset/resize/minimize/restore sequences match dimensions,
  transforms, viewport and readback; unsupported present modes report the applied value truthfully.
- **Result (2026-09-10):** SDL GPU now consumes `RendererSurfaceInfo` updates with immutable window
  identity, physical drawable dimensions and normalized display scale; returns the exact physical
  presentation rectangle; keeps virtual and physical dimensions independent; and transforms
  between SDL client coordinates and XNA logical coordinates with HiDPI scale. SpriteBatch now
  snapshots its logical projection extent separately from the mapped physical viewport, so the
  fixed default rectangle does not turn a letterboxed default viewport into a custom sub-viewport
  or let a later resize alter a queued sprite. The common physical-presentation suite moves from
  **2/6 to 6/6**, including pixel-readback coverage of the default letterboxed SpriteBatch viewport.

  The documented successful-null swapchain acquisition path now has a forced real-command-buffer
  oracle: it submits without error, retains the queued clear, renders it on retry and stays usable
  (**3/3**). A deterministic surface/present-mode executable passes **13/13** for Letterbox,
  distinct logical/physical sizes, scaled transforms, bar rejection, zero-size safety, window
  identity and requested-versus-applied intervals. The five pre-existing swapchain/present/resize/
  backbuffer-readback tests remain **5/5**. Six exact EasyGL sources are now compiled under SDL GPU
  and pass **6/6 CTests**: backbuffer resize, reset events, GDM VSync forwarding, MSAA changes,
  PresentationParameters and real OS-window resize. Their EasyGL originals pass **6/6** under
  Xvfb after the stale real-resize fixture explicitly selected the FixedHeightDynamicWidth mode its
  assertions describe. All new SDL tests make `VUID`/validation warnings fatal and emitted none.

  **Exact unavoidable interval difference:** vendored SDL 3.5's `SDL_GPUPresentMode` contains only
  `VSYNC`, `IMMEDIATE` and `MAILBOX`; both synchronized modes present at each vblank, and the API
  exposes neither a half-rate mode nor vblank/presentation-timing control from which the renderer
  could correctly emulate XNA `PresentInterval::Two`. SDL therefore records request `2` while
  truthfully recording applied interval `1`. An Immediate request is applied natively where
  supported and otherwise records its synchronized fallback. No fake timing claim is made.
  Corpus classification remains machine-checked at **246/246** with zero unclassified; all six
  reused lifecycle sources are promoted to direct parity. Registered SDL GPU CTests: **123**.

### SDLGPU-69 — implement ordinary Texture2D surface formats ✅

- **Problem/public behavior:** every non-Color XNA texture format is currently rejected or would be
  stored as RGBA8 despite SDL_gpu exposing native formats.
- **Evidence:** EasyGL packed/DXT/SNORM examples; SDL classifier defaults and texture allocation/
  upload assume four bytes per texel.
- **Location:** format map/classifiers, `SdlGpuTextureRenderer`, transfer/readback conversions and
  compressed-content policy.
- **Acceptance/test:** every ordinary format gets truthful runtime classification; native storage
  is used where exact, renderer conversion where reasonable, and byte/typed SetData/GetData,
  partial rectangles, authored mips, NPOT, compressed blocks and sampling are discriminated.
- **Result (2026-09-10):** accepted. SDL GPU now explicitly classifies all twenty classic
  `SurfaceFormat` values for Texture2D: the exact nine-format EasyGL set (`Color`, the three packed
  16-bit formats, DXT1/3/5 and both signed-normalized formats) is supported, while the other eleven
  are explicitly refused instead of reaching a hard-coded RGBA8 allocation. Packed formats use
  their exact SDL_gpu storage when the device supports it and a lossless logical-to-RGBA8 expansion
  otherwise. DXT uses BC1/2/3 where all required device formats exist and the shared CNA decoder
  otherwise; the content loader now retains compressed assets only when that native-storage fact is
  true. `NormalizedByte4` uses native RGBA8 SNORM. `NormalizedByte2` expands to RGBA8 SNORM with
  missing B/A channels set to signed one, preserving D3D9/XNA sampling semantics rather than SDL's
  native two-channel zero/one defaults.

  Format-sized transfers no longer assume four bytes per texel. Exact DXT block shadows preserve
  caller-authored data across byte readback, block-aligned partial updates, NPOT edge blocks and
  authored sub-4x4 mip levels; GPU uploads use format-aware SDL sizes and tightly packed rows.
  Three exact EasyGL sources now compile unchanged under SDL GPU and pass: DXT sampling (3/3),
  packed sampling (3/3), and format construction/refusal (30/30). Forced packed and DXT fallback
  registrations pass the same pixel oracles. The SDL-specific native/fallback matrix passes 9/9 on
  both paths, including all classifier rows and the `NormalizedByte2` blue-versus-black missing-
  channel discriminator. The unchanged compressed-content loader fixture passes 3/3 both natively
  and under forced decode, covering DDS DXT1, XNB DXT5, four authored mips and SpriteBatch sampling.
  Shared graphics format/constructor tests pass 17/17. All nine registered format CTests make
  validation diagnostics fatal and emit none. Corpus classification remains 246/246 with zero
  unclassified; the three format sources move from feature-missing to direct parity. Registered SDL
  GPU CTests: **132**.

### SDLGPU-70 — implement ordinary TextureCube formats ✅

- **Problem/public behavior:** cube creation ignores `surfaceFormat`, transfers assume RGBA8 and
  compressed cube upload inherits false.
- **Evidence:** EasyGL six-face/mip/partial and DXT paths; SDL_gpu supports cube textures with the
  same native format enum.
- **Location:** cube classifier/resource format/upload/readback/sampling.
- **Acceptance/test:** all legal ordinary cube formats are truthful; six deliberately distinct
  faces, partials, authored mips and DXT blocks round-trip/sample; illegal SNORM cubes refuse.
- **Result (2026-09-10):** accepted. `CreateTextureCube` now preserves `surfaceFormat`; the cube
  classifier explicitly supports Color and DXT1/3/5, explicitly refuses the other 16 classic
  values, and the compressed-transfer predicate is no longer inherited false. DXT cubes allocate
  native BC1/2/3 when the concrete cube usage is supported and otherwise allocate RGBA8 backed by
  the shared CPU decoder; `CNA_SDLGPU_FORCE_DXT_FALLBACK=1` exercises the latter even on this BC
  device. Per-face/per-level exact block shadows reconstruct full logical levels for partial
  updates, preserving untouched NPOT edge blocks identically on both storage paths, while public
  `GetData(Color*)` decodes and crops the requested rectangle. Plain Color and compressed cube mips
  are now explicitly authored: the old full-level-0 `SDL_GenerateMipmapsForGPUTexture` call was an
  observable XNA/EasyGL mismatch because SDL regenerates all six faces and overwrote mips already
  authored on earlier faces. The unchanged EasyGL mip oracle exposed that defect and passes after
  its removal.

  Eight new validation-fatal registrations pass: four exact EasyGL sources (six faces 96/96,
  partial/startIndex 114/114, authored mips 126/126, DDS content load 7/7), the shared compressed
  cube sample/readback fixture on native and forced-decode paths, and the SDL cube format matrix on
  both paths (11/11 each: exact classifier boundary, DXT1/3/5, six distinct faces, NPOT partial
  blocks and a sub-4x4 authored DXT5 mip). The shared sample fixture now supplies its required
  neutral white EnvironmentMapEffect base texture, preventing two blank frames from satisfying its
  agreement check; the rebuilt shared source also passes 4/4 under EasyGL on temporary Xvfb, so
  this harness correction preserves the oracle renderer. The existing SDL Color cube regression
  passes 5/5 with its former implicit-mip
  assertion replaced by the discriminating authored +X mip → later -X level-0 preservation
  sequence. Graphics tests pass 59/59 across every `*TextureCube*` suite, including construction/use
  agreement for each DXT format and the named surface-refusal matrix. The updated Texture2D matrix
  also passes 9/9 twice and proves the process-wide compressed-content policy agrees with physical
  BC support for both 2D and cube resource kinds. All 11 focused CTests emitted no Vulkan validation
  diagnostics. Corpus classification is 246/246 with zero unclassified; the four cube sources move
  from covered to direct parity. Registered SDL GPU CTests: **140**.

### SDLGPU-71 — implement ordinary Texture3D format semantics and repair the public seam ✅

- **Problem/public behavior:** public `Texture3D` unconditionally calls the Color-only framework
  validator and SDL then ignores the forwarded format.
- **Evidence:** EasyGL has a format-bearing volume factory; SDL_gpu exposes 3D textures in the same
  format system, so the public seam—not an `EXT` suffix—makes this in scope.
- **Location:** shared Texture3D classifier call path (minimal common correction) and SDL volume
  storage/transfers.
- **Acceptance/test:** profile+renderer classification mirrors Texture2D; every format in the
  reference volume-storage surface handles dimensions/mips/partial boxes/readback with its exact
  forwarded identity or is truthfully refused; EasyGL plus SDL are regression tested after the
  shared change. End-to-end ordinary compiled-effect volume sampling remains part of the compiled
  effect contract in `SDLGPU-79`, rather than making this format task depend on a separate shader
  execution path.
- **Result (2026-09-10):** accepted at the live reference surface's actual boundary. The deeper
  audit found that neither renderer supports a non-Color ordinary volume today: CNA's public
  `Texture3D` transfer overloads are Color-shaped, the old shared validator permits Color only,
  and `EasyGLTexture3DRenderer` ignores its internal `surfaceFormat` argument while allocating
  every level as RGBA8. Expanding SDL_gpu to packed, BC or float volumes would therefore create a
  new unrepresented public transfer contract rather than EasyGL parity. Instead, the common
  renderer contract now has a resource-specific `ClassifyTexture3DFormatEXT` hook whose safe
  default preserves all other renderers' Color-only behavior. The public constructor applies the
  graphics-profile gate and then that verdict. SDL GPU explicitly supports Color, refuses every
  other one of the 20 classic values with `NotSupportedException`, forwards the accepted ordinal
  into its concrete resource, records it for diagnostics, and defensively rejects any non-Color
  direct factory use. The format argument is no longer accepted and ignored.

  The SDL regression checks the exact 1-supported/19-refused classifier boundary, public named
  refusal for all 19, concrete format forwarding, NPOT dimensions, cumulative independent volume
  writes, authored mips, partial boxes/readback and A→B→A resource sequences. Four new
  validation-fatal CTests compile the verbatim EasyGL slice, asymmetric upload, asymmetric
  readback/startIndex and authored-mip sources under SDL GPU; the focused volume slice passes
  **6/6 CTests** with no captured validation diagnostic. The relevant graphics tests pass **44/44**
  under both SDL GPU and EasyGL (the unsupported-renderer control skips once under each), and the
  EasyGL format contract remains 30/30. The four standalone EasyGL oracles also pass directly on a
  temporary Xvfb display. Volume sampling is not exercised by any classic stock XNA effect; its
  existing compiled-effect resource path remains explicitly owned by `SDLGPU-79`, while the
  CNAEXT ShaderEffect volume program remains out of this task. Corpus classification is 246/246
  with zero unclassified; four volume sources move from covered to direct parity. Registered SDL
  GPU CTests: **144**.

### SDLGPU-72 — preserve requested RenderTarget2D color formats ✅

- **Problem/public behavior:** ordinary `RenderTarget2D(..., SurfaceFormat, ...)` reaches the base
  `CreateRenderTarget2DEXT`, which silently substitutes Color.
- **Evidence:** EasyGL creates/probes Rgba64 and float/half targets; SDL_gpu exposes corresponding
  render-target formats and a usage-support query.
- **Location:** SDL RT format classifier/factory/state/pipeline target-info/readback.
- **Acceptance/test:** each ordinary format is either exact and pixel/byte verified (including >1
  float witnesses) or truthfully refused from a failed runtime probe; pipeline cache separates
  target formats.
- **Result (2026-09-11):** accepted. The renderer now maps the exact nine-format set exposed by
  EasyGL (`Color`, `Rgba64`, `Single`, `Vector2`, `Vector4`, `HalfSingle`, `HalfVector2`,
  `HalfVector4`, `HdrBlendable`) to native SDL_gpu storage and runtime-probes each format for the
  combined color-target and sampler usage required by XNA. The other eleven classic formats are
  explicitly refused. The ordinary format-bearing factory preserves the request; allocation,
  multisample attachment, immutable pipeline target description and typed readback all consume
  the same stored native format/texel size instead of the historical RGBA8/4-byte constants.
  SpriteBatch snapshots the sampled target's public format and applies D3D9/XNA absent-channel
  expansion for one- and two-channel float targets; four-channel textures remain unchanged.

  On the current RADV device all nine exact formats are available. The shared agreement suite
  proves matching query/construction, bind/clear and stock-3D rendering for all nine; typed
  byte-exact readback covers 2-, 4-, 8- and 16-byte texels with deliberately above-one float
  values, and the existing NPOT RGBA32F test remains exact for 13x7, 17x3, 5x11 and 64x1.
  The shared HDR fixture proves above-one half storage, sampling, depth, MSAA and generated mips.
  The channel oracle distinguishes `(R,1,1,1)`, `(R,G,1,1)` and untouched RGBA sampling. The exact
  EasyGL RenderTarget2D properties source now compiles unchanged under SDL GPU and passes all
  18 checks, including Rgba64 typed readback. Four focused SDL CTests pass 4/4 with validation
  warnings/errors fatal; the task-only graphics filter passes with its one correctly skipped MRT
  leg. The broader capability filter retains one pre-existing assertion that expects MRT=true;
  this is explicitly owned by `SDLGPU-75`, not hidden as a target-format failure.

  The new exact numeric source passes under EasyGL at alignment-safe dimensions. Its adversarial
  5x3 HalfSingle predecessor exposed `EASYGL-PARITY-3`; SDL passed that same tight transfer and does
  not reproduce the OpenGL pack-alignment overwrite. Corpus classification remains 246/246 with
  zero unclassified, promotes the exact properties source to direct parity, and registered SDL GPU
  CTests increase to **146**. Only targeted incremental builds/relinks were used.

### SDLGPU-73 — preserve requested RenderTargetCube and depth/stencil formats ✅

- **Problem/public behavior:** cube target color format is dropped and depth/stencil facts partly
  come from generic defaults.
- **Evidence:** EasyGL creates requested cube storage and tests depth formats; SDL uses a combined
  depth/stencil choice without fully reporting the applied public result.
- **Location:** cube EXT factory/state, depth format mapping/facts, pipeline keys.
- **Acceptance/test:** six faces of every supported color format plus Depth16/24/24Stencil8,
  zero/nonzero MSAA, mips/readback/sampling/switching match EasyGL or truthfully refuse.
- **Result (2026-09-11):** accepted. A new cube-specific format classifier now probes the actual
  SDL `TEXTURETYPE_CUBE` color-target-plus-sampler usage instead of assuming a successful 2D probe
  also proves cube support. The format-bearing factory preserves the ordinal, and each cube state
  owns its exact native format and texel size. Allocation, per-face MSAA resolve, mip generation,
  immutable pipeline target descriptions and renderer-local native readback all consume those
  facts. The common Color-shaped `TextureCube::GetData` contract remains RGBA8-only and explicitly
  refuses a typed float cube rather than leaking native bytes through the wrong element type.

  Depth storage is likewise per target for both cube and 2D resources. `None` allocates nothing;
  `Depth16` uses D16; `Depth24` prefers D24 and reasonably falls back to D32 float; and
  `Depth24Stencil8` prefers D24S8 and falls back to D32S8. The applied public XNA semantic remains
  the requested enum, while the actual fixed-point precision or float mantissa (16, 24 or 23 bits)
  drives DepthBias normalization. The live RADV device applies 0/16/23/23 effective bits for
  None/D16/D24/D24S8; its lack of D24 and D24S8 is no longer confused with lack of the ordinary XNA
  capability. Depth-only targets report no stencil plane, and multisample clamping now requires
  both the selected color and depth formats to support the count. Pipeline identity already
  includes the native depth format and now receives each target's real format rather than the
  swapchain's global combined choice.

  The SDL physical matrix passes **40/40**: all 20 classic cube classifier answers, the exact nine
  supported formats, all six faces with distinct format-sized values, half-float MSAA and mip
  values above one, and the four depth facts for both cube and 2D targets. The strengthened native
  cube regression passes **7/7**, including a D16→D24→D24S8→D16 pipeline sequence whose nearer
  geometry wins each time. The ordinary public discriminator fills every half-float face with
  `(4,2,1)`, samples it through `EnvironmentMapEffect` into an RGBA32F target and reads back the
  exact values; the historical RGBA8 substitution would return `(1,1,1)`. It passes identically on
  SDL GPU and EasyGL. The two unchanged EasyGL cube sources compile under SDL GPU and pass 16/16
  constructor/property checks plus the 2/2 depth-versus-no-depth pixel oracle; both also pass
  directly under EasyGL/Xvfb.

  The final focused SDL run passes **9/9 CTests**, including RT2D, cube MSAA+mips, plural binding,
  depthless SpriteBatch compatibility and the 67/67 depth-bias control, with validation diagnostics
  fatal and none emitted. Of 23 selected shared HDR/target/default-contract tests, 22 pass and the
  sole MRT case is expectedly skipped. The classifier remains 246/246 with zero unclassified and promotes both exact EasyGL
  cube sources to direct parity. Registered SDL GPU CTests: **149**. Only targeted incremental
  builds and relinks were used.

### SDLGPU-74 — finish render-target MSAA, mip, preserve/discard and transition semantics ⬜

- **Problem/public behavior:** broad Color-path tests exist, but zero MSAA, preserve/discard,
  consecutive passes, read-before-first-present and mixed target transitions need one oracle.
- **Evidence:** EasyGL and shared RT suites cover these independently; SDL has resolve/proxy/pass
  machinery with historical fixes but no consolidated parity run.
- **Location:** RT state/resolve/mip/pass finalization for 2D and cube.
- **Acceptance/test:** zero stays zero, supported nonzero counts report applied samples, preserve/
  discard obey XNA, authored/rendered mips read and sample correctly across repeated mixed passes.

### SDLGPU-75 — implement independently observable MRT outputs ⬜

- **Problem/public behavior:** binding/clearing multiple attachments is not MRT parity if shaders
  cannot write distinct values to each target.
- **Evidence:** EasyGL `easygl_mrt_test` uses distinct fragment locations; SDL stock comments state
  only attachment zero is written although multiple targets bind.
- **Location:** ordinary compiled-effect pipeline/output reflection and MRT pipeline target info.
- **Acceptance/test:** one ordinary Effect pass writes unique signatures to at least two targets,
  both read back correctly, masks/blend states per slot apply, and A→different format/count→A is
  cache-safe. CNAEXT ShaderEffect alone is not sufficient proof.

### SDLGPU-76 — complete SpriteBatch observable parity ⬜

- **Problem/public behavior:** one smoke path does not establish overload, sorting, transform,
  source/origin/rotation/scale/flip/layer/effect/state/RT/order semantics.
- **Evidence:** EasyGL has a broad sprite/font corpus; SDL implements the surface but has a live
  depth-bias failure and incomplete shared-fixture coverage.
- **Location:** SpriteBatch command capture/sort/geometry and renderer sprite pipelines.
- **Acceptance/test:** reuse shared sprite geometry/state/font/sampler sources, add immediate vs
  deferred and interleaved 3D checks, disposed-resource failures and nontrivial rectangles.

### SDLGPU-77 — exhaustively verify all five classic built-in effects ⬜

- **Problem/public behavior:** smoke triangles do not prove all option combinations of Basic,
  AlphaTest, DualTexture, EnvironmentMap and Skinned effects.
- **Evidence:** 44 corpus rows identify weaker SDL evidence; shared effect fixtures isolate light
  terms, alpha comparisons, UV1, fresnel and bones.
- **Location:** stock shader variants/uniform snapshots/program selection.
- **Acceptance/test:** same sources verify texture/color/diffuse/emissive/alpha/fog, default and
  multiple lights, per-pixel/specular, transforms/nonuniform normals, bones/weights limits and
  environment amount/fresnel/specular with discriminating pixels.

### SDLGPU-78 — close buffer and draw-family edge cases ⬜

- **Problem/public behavior:** all draw overloads must honor primitive count/topology, offsets,
  base/start values, 16/32-bit indices, dynamic options, zero sizes and repeated replacement.
- **Evidence:** EasyGL has dedicated validation/dynamic/range/GetData tests; SDL paths exist but are
  not collectively verified and deferred capture increases risk.
- **Location:** vertex/index backends, public draw parameter packing, queued commands.
- **Acceptance/test:** exact geometry signatures for every draw family/topology and adversarial
  ranges; Discard/NoOverwrite replacement and disposal/lifetime sequences; validation clean.

### SDLGPU-79 — verify Models and ordinary compiled Effect behavior ⬜

- **Problem/public behavior:** Model hierarchy/mesh/effect application and compiled techniques,
  passes, parameters, state and texture lifetimes must not regress.
- **Evidence:** EasyGL model/Effect corpus and SDL compiled runtime both exist, but no complete oracle
  comparison has been recorded.
- **Location:** `SdlGpuCompiledEffect`, stock model draw routing and shared content fixtures.
- **Acceptance/test:** identical compiled fixtures and model sources prove technique/pass changes,
  clone independence, 32-bit meshes, child transforms, multi-mesh effects and skinned playback.

### SDLGPU-80 — prove or close the occlusion-query limitation ⬜

- **Problem/public behavior:** ordinary `OcclusionQuery` works on EasyGL and SDL currently returns
  null while inherited capability reports true.
- **Evidence:** vendored SDL 3.5.0 `SDL_gpu.h` contains fence queries only and no occlusion/query-pool
  primitive. CPU bounds or delayed fake values cannot reproduce samples-after-depth/stencil.
- **Location:** vendored-header evidence, capability reporting and limitation text.
- **Acceptance/test:** re-audit the exact current header/API; investigate a correct render/readback
  emulation and reject it only with measured correctness/cost reasoning. If no reasonable path
  exists, capability false + constructor refusal + exact limitation is `⛔`; never fake results.

### SDLGPU-81 — register and pass the shared EasyGL parity corpus; audit deferred lifetime ⬜

- **Problem/public behavior:** EasyGL/WebGPU build the same 30 sources; SDL GPU does not, and queued
  work must snapshot/retain all referenced state/resources at issue time.
- **Evidence:** existing SDL lifetime tests cover targets and some source textures, not every
  texture/cube/volume/buffer/effect/sampler/viewport/RT mutation sequence.
- **Location:** SDL example CMake and deferred command ownership/snapshots.
- **Acceptance/test:** register `cna_register_parity_fixtures` against SDL GPU, all in-scope fixtures
  pass with validation fatal, direct EasyGL-vs-SDL raw frames agree within each fixture's declared
  tolerance, and destruction/mutation after enqueue cannot retroactively change earlier draws.

### SDLGPU-82 — final adversarial sweep and exact verdict ⬜

- **Problem/public behavior:** known tasks turning green does not prove parity.
- **Evidence:** repeat mechanical searches for inherited defaults, ignored parameters, hard-coded
  RGBA8/state, silent fallbacks, missing keys, throws/nulls/TODOs and unclassified examples.
- **Location:** entire public graphics seam, both modular renderers, tests and this plan.
- **Acceptance/test:** recompute the 246-row manifest with zero unexplained relevant rows; rerun all
  SDL tests, all shared parity/oracle tests and targeted EasyGL controls; record final commits/counts,
  validation status, unavoidable limits and every remaining task. Only then issue verdict A or B.

### SDLGPU-83 — make the requested native viewport authoritative before first acquisition ✅

- **Problem/public behavior:** a first draw at a non-default `NativeBackBuffer` size captured the
  renderer's constructor-time 800×480 viewport even though the public presentation request was
  256×128; lazy swapchain acquisition learned the real size only after the draw had been queued.
- **Evidence:** the shared wireframe fixture rendered its solid triangle in the 800×480-scaled
  upper-left of a 256×128 readback on both offscreen RADV and X11/lavapipe. Readback tracing showed
  `backbuffer=256x128 viewport=800x480` immediately before acquisition; subsequent frames reported
  256×128. EasyGL derives the logical size from the requested virtual dimensions.
- **Location:** `SdlGpuRenderer::SetVirtualResolution`, first-frame smoke regression.
- **Acceptance/test:** the renderer and public `GraphicsDevice.Viewport` both report the exact
  requested 320×240 size on frame one, before any present/readback/acquisition; ordinary rendering
  regressions remain green and native validation output is fatal.
- **Result (2026-09-09):** `SetVirtualResolution` now treats a positive native request as the
  pending swapchain extent immediately; the first real acquisition remains authoritative and
  replaces it if the platform clamps the size. The expanded smoke test passes 30/30 on Vulkan,
  including both first-frame exact-size checks, with validation diagnostics configured as fatal.

### SDLGPU-84 — keep render-target depth oracles inside the XNA clip volume ✅

- **Problem/public behavior:** the SDL-only 2D and multisampled 2D render-target tests call an
  identity-projection vertex at Z=−0.5 “near.” After `SDLGPU-65` correctly enabled the XNA/D3D
  0..W depth clip, that quad is clipped and both tests misleadingly report a depth failure.
- **Evidence:** the equivalent cube fixture had the same issue and passes at valid 0.25/0.75
  depths; FNA3D's SDL_gpu driver explicitly enables depth clipping.
- **Location:** SDL GPU RenderTarget2D and RenderTarget2DMSAA test geometry only.
- **Acceptance/test:** use valid, separated 0.25/0.75 clip depths; nearer green wins under both
  single-sample and MSAA targets, all other assertions remain green, validation clean.
- **Result (2026-09-09):** both fixtures now use near Z=0.25 and far Z=0.75, entirely within the
  XNA/Direct3D clip volume while retaining a large discriminating separation. The ordinary target
  passes 7/7 and the 4x-MSAA resolve target passes 9/9; both CTests now make Vulkan validation
  diagnostics fatal.

---

## Historical SDL GPU implementation plan (preserved evidence; paths/status may be stale)

# SDL GPU Graphics Backend — Implementation Plan

> **Correction (2026-07-15, later the same day): the banner below's "every phase ... is fully
> implemented" claim was wrong when written** — `SDLGPU-17`–`20` (the genuine keyed pipeline cache,
> plus dynamic `BlendState`/`DepthStencilState`/`RasterizerState`) were still `⬜`/🟨 at that point;
> every 3D/sprite pipeline was still hardcoded Opaque/no-cull/Solid/no-stencil regardless of what
> `GraphicsDevice.BlendState`/`DepthStencilState`/`RasterizerState` were assigned. Found on a
> later re-check the same day and closed for real — see execution-order item 10 and each row's own
> Notes column. A genuine bug (`SDL_SetGPUStencilReference()` never called anywhere, so
> `ReferenceStencil` was silently ignored) was found and fixed along the way.
>
> **Status (2026-07-15): every phase currently in this plan (SDLGPU-1 through SDLGPU-10) is fully
> implemented and verified** — `AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/
> `SkinnedEffect` are all real and verified (`EnvironmentMapEffect` landed once Phase `SDLGPU-9`'s
> cube-texture backends existed; `SkinnedEffect` landed after a real empirical spike found and
> worked around a genuine `SDL_gpu` push-uniform size cap — see that row for the full finding), and
> custom `ShaderEffect` support (Phase `SDLGPU-10`) landed last, via a real runtime GLSL→SPIR-V
> compile (`libshaderc` linked into the backend binary itself, not just used at build time) — see
> `SDLGPU-42`'s row for a genuine architectural finding this decision uncovered (Vulkan's own
> `ShaderEffect` support doesn't actually runtime-compile GLSL text at all, despite the class's own
> documented contract).
> `CNA_GRAPHICS_BACKEND=SDL_GPU` configures, builds (`cna_backend_graphics_sdl_gpu`, zero new
> third-party dependencies as predicted), and a real window + real `SDL_GPUDevice` (Vulkan driver)
> clears color+depth+stencil, uploads a real `Texture2D`, draws a real `SpriteBatch` scene (tint,
> alpha, rotation, both flips, Point/Linear + Wrap/Clamp sampling), draws real
> `colored3d`/`textured3d`/`colored_textured3d`/`lit_textured3d` 3D geometry (with a real
> depth-test occlusion proof), and draws real `AlphaTestEffect`/`DualTextureEffect` geometry (real
> per-pixel discard; a real two-sampler multiply with a colour-shift result that could only come
> from two textures actually being sampled) — all via the public `BasicEffect`-family/
> `VertexBuffer`/`GraphicsDevice.DrawPrimitives` API. Verified by `SdlGpu_Smoke` (6/6), `SdlGpu_2D`
> (3/3), `SdlGpu_3D` (6/6), and `SdlGpu_Effects` (3/3 checks, `ctest -R SdlGpu`), all on real GPU
> via Vulkan on this Linux dev machine, each backed by a real screenshot, not just "didn't throw".
> Those screenshots caught and led to fixing a real bug — see `SDLGPU-14`'s row below — and then,
> for the 3D path, *confirmed the fix's own theory* (3D shaders using a real XNA projection matrix
> need no Y-flip, unlike the hand-computed 2D sprite NDC math). `GraphicsBackendCompileDefinitionTests.cpp`'s
> `ExactlyOneGraphicsBackendIsSelected` was updated for the new backend and the full `CnaTests`
> suite (118 tests) still passes. `EnvironmentMapEffect`/`SkinnedEffect` are still ⬜. **Phase
> `SDLGPU-8` (render targets) is now fully done**: `SDLGPU-35` (`RenderTarget2D`), `SDLGPU-36`
> (`RenderTargetCube`, real MSAA + mip regen), `SDLGPU-37` (MRT), `SDLGPU-38` (`RenderTarget2D`
> MSAA), and `SDLGPU-39`'s `RenderTarget2D`/`RenderTargetCube` legs are all done and verified,
> 2026-07-15 — see their own rows for the real multi-pass `EnsureFrameRendered` refactor and
> `DrawTarget` generalization this required. `SDLGPU-39`'s remaining swapchain leg stays 🟨 — hit a
> real, unresolved segfault (see that row); plain `Texture2D::GetData()` is separately out of scope
> (already-accurate CPU shadow, no GPU path needed). Plain `TextureCube::GetData()`'s leg is now
> **also done for real** — `SDLGPU-51` (below) landed a full `SdlGpuTextureCubeBackend` with a real
> transfer-buffer+fence `GetData()`, not a stub, so `SDLGPU-39` is now only blocked on the swapchain
> segfault.
>
> **Real cross-backend interface change made 2026-07-15 to close `SDLGPU-39`'s `RenderTarget2D`
> leg:** `ITextureBackend` (in the common `IGraphicsBackend.hpp` every backend implements) gained a
> new `GetData(level, x, y, w, h, data, dataLength) const` virtual with a safe no-op default
> (mirrors `ITextureCubeBackend::GetData`'s own convention) — purely additive, no other backend's
> code needed to change. `Texture2D::GetData()`'s two entry points now fall back to this only when
> the CPU-side shadow is empty (i.e. only for a `RenderTarget2D`); plain, `SetData()`-populated
> textures are completely unaffected (confirmed via 123/123 passing `Texture2DTest`/
> `TextureCubeTest`/`Texture3DTest`). See `SDLGPU-39`'s own row for the full detail.
>
> **Phase `SDLGPU-9` (`Texture3D` + plain `TextureCube`) fully closed 2026-07-15**: `SDLGPU-40`
> (`Texture3D`) and `SDLGPU-41` (mips) are done and verified — see `SDLGPU-40`'s own row for a real
> bug this task's own byte-exact round-trip test caught and fixed (`SDL_UploadToGPUTexture`'s
> `cycle=true` silently orphaned earlier partial sub-volume writes onto an abandoned GPU resource;
> fixed with `cycle=false` for `Texture3D` specifically). `SDLGPU-51` (plain `TextureCube`) is also
> done and verified — `SdlGpuTextureCubeBackend` reused the exact same per-face `region.layer`
> indexing `SdlGpuRenderTargetCubeBackend::GetData()` already established, and its `SetData` uses
> `cycle=false` from the start (the `SDLGPU-40` bug was checked for and confirmed absent via a
> deliberate "write all 6 faces, then re-read face 0 last" regression check). With both cube-texture
> sources now real, `SDLGPU-33` (`EnvironmentMapEffect`) was implemented and verified the same day —
> see its own row for the new `env_map3d.glsl` shader pair and the dual-backend cube resolve this
> required. Only `SDLGPU-34` (`SkinnedEffect`'s push-uniform-size spike) remains deferred from
> Phase 7.
>
> **Real architectural gotcha found while writing `SDLGPU-37`'s test — FIXED 2026-07-15 (see
> execution-order item 12 below), no longer applies:** a `RenderTarget2D`/`RenderTargetCube`
> destroyed before this backend's deferred `Present()`-time render pass actually executes used to
> be a genuine use-after-free. `SdlGpuRenderTarget2DState`/`SdlGpuRenderTargetCubeState` (new,
> shared_ptr-owned structs holding the actual GPU state, independent of the
> `SdlGpuRenderTargetBackend`/`SdlGpuRenderTargetCubeBackend` wrapper's own C++ lifetime) now keep a
> render target's state alive exactly as long as anything still references it — a short-lived local
> render target destroyed mid-`Draw()` now renders its own pending content correctly AND is safe to
> sample from elsewhere the same frame. Confirmed via a real segfault when `sdlgpu_mrt_test.cpp`'s
> first draft used `Draw()`-local `RenderTarget2D` instances (now fixed, still worth keeping
> targets as member fields by convention, but no longer a crash risk either way).
>
> **Full `CnaTests` suite is NOT currently stable end-to-end under `CNA_GRAPHICS_BACKEND=SDL_GPU`
> in this sandboxed dev environment** (found 2026-07-15 while trying to verify `SDLGPU-35`
> broadly): running the entire ~4300-test suite segfaults partway through
> `ContentManagerSkinnedModelTest` (confirmed reproducible on the unmodified pre-`SDLGPU-35`
> baseline too, and the exact test it crashes on shifts when the first one is filtered out —
> consistent with real-window/GPU-device resource churn across thousands of tests in this sandbox,
> not a deterministic logic bug in one test). This is a pre-existing condition, not something
> `SDLGPU-35` introduced or fixed. This backend's own dedicated CTest suite
> (`SdlGpu_Smoke`/`SdlGpu_2D`/`SdlGpu_3D`/`SdlGpu_Effects`/`SdlGpu_RenderTarget2D`/
> `SdlGpu_RenderTargetCube`/`SdlGpu_MRT`/`SdlGpu_RenderTarget2DMSAA`/`SdlGpu_Texture3D`/
> `SdlGpu_TextureCube`/`SdlGpu_EnvMap`/`SdlGpu_Skinned`/`SdlGpu_ShaderEffect`, `ctest -R SdlGpu`) is the actual validated methodology
> for real-window backends in this project (mirrors
> how Vulkan/D3D11/D3D12 are validated) — don't treat a full unfiltered `CnaTests` run under this
> backend as a meaningful signal without first re-reading this note.
>
> **Known partial gaps in what's landed so far:** `SDL_CreateGPUDevice`'s `debug_mode` is
> hardcoded `false`, not yet wired to a CNA-side debug/validation toggle (minor, deferred).
> "Recoverable handling of a failed/occluded swapchain acquisition" (SDLGPU-11) only covers the
> one case `SDL_gpu` itself documents (a null texture on a minimized window) — there is no
> WebGPU-style surface-loss/outdated-recovery case to handle because `SDL_gpu` does not expose
> one at this API level.
>
> **Real, cross-backend bug found while verifying this phase — fixed elsewhere, noted here for
> history:** `GraphicsDeviceManager::SynchronizeWithVerticalRetrace` +
> `ApplyChanges()`/`applyToExistingBackend()` used to never reach
> `IGraphicsBackend::SetSwapInterval()` for *any* backend (`GraphicsDevice::Reset()` never forwarded
> the presentation interval to the backend). This has since been fixed in `GraphicsDevice::Reset()`
> (merged into `develop` from the D3D9 branch). Fixing it surfaced a real consequence for every
> SDL_GPU test/example in this file: each one's constructor called
> `backend.SetSwapInterval(0)` directly to avoid this environment's ~1s/frame VSync wait on its
> virtual/headless display, but that call ran *before* `Game::DoInitialize()`'s `CreateDevice()` —
> which, now that forwarding works, applies `GraphicsDeviceManager.SynchronizeWithVerticalRetrace`'s
> default (`true`) afterward and silently overwrote the workaround, making every 120-frame test take
> ~2 minutes and time out under CTest's 60s default. Fixed by replacing the direct-backend-poke
> workaround in all 19 SDL_GPU example/test constructors with
> `gdm_->setSynchronizeWithVerticalRetraceProperty(false);`, called before `CreateDevice()` ever
> runs — the correct, property-level way to request Immediate presentation, and no longer a
> workaround now that the forwarding gap itself is closed.
>
> **Real, pre-existing SDL_GPU bugs found while merging into `develop` (2026-07-16), fixed as part
> of that merge:** `SdlGpuVertexBufferBackend::SetDataWithOptions` and
> `SdlGpuIndexBufferBackend::Upload` created the backing `SDL_GPUBuffer` with `size` equal to the
> caller's raw vertex/index byte count, with no floor — a degenerate `vertexCount`/`indexCount` of
> `0` (e.g. an empty part of a skinned model) asked `SDL_CreateGPUBuffer` for a 0-byte buffer, which
> SDL_gpu's Vulkan driver asserts on and crashes (reproduced identically on the pristine,
> pre-merge `feature/sdlgpu` branch, so this was not introduced by the merge — it just had never
> been exercised by a real content-loading test under this backend before). This crashed
> `ContentManagerSkinnedModelTest.PartNameWithUnbalancedEmbeddedBraceParsesCorrectly` and two
> `TextureLoads*` cases with a SEGFAULT. Fixed by clamping the backing allocation to a 4-byte floor
> and skipping the transfer-buffer upload entirely when the logical size is 0 (the public
> `vertexCount_`/`indexCount_`/`shadowData_` still report the real, possibly-zero size).
>
> **Known gap surfaced by the same merge, left open:** `CnjEffectTest.LoadsRealCnjFixture` (a
> `develop`-only test — it doesn't exist on `feature/sdlgpu`, so this is the first time it has run
> under `CNA_GRAPHICS_BACKEND=SDL_GPU`) fails because its fixture's custom `ShaderEffect` GLSL is
> not written in the SPIR-V-compatible dialect SDL_GPU's shader compiler requires (needs an
> explicit `#version 310 es`-or-higher, explicit `location` qualifiers on stage I/O, and uniforms in
> a block rather than loose/non-opaque). This is a real, separately-scoped gap in SDL_GPU's custom
> `ShaderEffect` support (either the fixture's GLSL or SDL_GPU's shader-compat layer needs work),
> not something fixable as a small merge-time patch — left open and tracked here rather than fixed
> under this merge.
>
> **Status legend:** ✅ implemented *and verified against its stated acceptance criteria*;
> 🟨 code or documentation exists but has not met those criteria; ⬜ not implemented.

---

## Why an SDL GPU backend

- **Zero new third-party dependency.** SDL3 is already a vendored/git-cloned dependency of this
  project (`third_party/SDL`), and `SDL_gpu.c` is already compiled into every prebuilt SDL3
  package this repo produces (`.sdl-prebuilt*/`). Unlike WebGPU (`wgpu-native`, a separate
  library) or Vulkan (the Vulkan SDK/loader), adding this backend requires no new external
  package — only new CNA-side code.
- **One API, several native backends.** `SDL_gpu` itself dispatches to Vulkan, D3D12, or Metal
  depending on platform/availability. A working SDL GPU backend gets CNA a second real path to
  Direct3D 12 and Metal without maintaining separate `D3D12`-style or `Metal`-style CNA backend
  code for them — at the cost of losing direct control over backend-specific behavior that the
  dedicated `D3D11`/`D3D12`/`Vulkan` backends have.
- **Consistent with the existing multi-backend architecture.** CNA already supports 9
  `CNA_GRAPHICS_BACKEND` values (`SDL_RENDERER`, `EASYGL`, `BGFX`, `VULKAN`, `WEBGPU`, `HEADLESS`,
  `SOFTWARE`, `D3D11`, `D3D12`) selected at CMake configure time; this is simply the tenth,
  following the exact same `IGraphicsBackend` contract every other backend already implements
  (`include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp`).

This plan does **not** propose retiring any existing backend. See
`Internal (CNA) vs XNA Layer` in `CLAUDE.md` for the backend-selection architecture this plan
extends.

---

## Non-goals and known constraints (read before starting)

- **Not a replacement for any existing backend.** SDL GPU is an *additional* pluggable backend.
- **No occlusion-query support in `SDL_gpu` itself.** As of the vendored `third_party/SDL/include/SDL3/SDL_gpu.h`,
  there is no query-pool/occlusion-query type anywhere in the API (only `SDL_GPUFence` for
  CPU/GPU synchronization). `IOcclusionQueryBackend::CreateOcclusionQuery()` must return
  `nullptr` on this backend, exactly like the `Headless`/`Software` backends already do for
  their own reasons. This is a **permanent limitation of the underlying API**, not a task gap —
  do not open a task to "fix" it unless a future SDL3 version adds real query support.
- **Shaders must be precompiled bytecode with a mandatory resource-binding order.**
  `SDL_gpu` does not accept GLSL/HLSL source at runtime. `SDL_CreateGPUShader` takes
  `SDL_GPUShaderFormat`-tagged bytecode (SPIR-V for the Vulkan driver, DXBC/DXIL for the D3D12
  driver, MSL/metallib for the Metal driver), and — per `SDL_gpu.h` (~L2606–2744) — every
  shader stage's samplers/storage-textures/storage-buffers/uniform-buffers must be declared in a
  **fixed HLSL-style order** (`t[n]`/`s[n]` space0, `u[n]` space1, `b[n]` space2 for the vertex
  stage; an analogous space2/space3 split for the fragment stage). This is a different
  convention from the raw `layout(set=, binding=)` numbers the existing `VulkanGraphicsBackend`
  GLSL shaders use (`src/CNA/Internal/Backends/Vulkan/shaders/*.glsl`) — those shaders are a
  useful **algorithmic** reference (the lighting math, the alpha-test logic, the dual-texture
  blend) but their compiled SPIR-V is **not** directly reusable as-is; the binding layout has to
  be re-authored to satisfy `SDL_gpu`'s convention. See Phase `SDLGPU-3` for the decision this
  implies (hand-author SPIR-V for the Vulkan driver first vs. vendoring the official
  `SDL_shadercross` cross-compiler for all formats).
- **Verified target for the first milestone: Linux desktop via `SDL_gpu`'s Vulkan driver.**
  This matches the current dev environment (the only `SDL_gpu` driver compiled into this
  project's prebuilt SDL3 packages on Linux is `vulkan`, per
  `.sdl-prebuilt-Linux-x86_64/SDL/build/.../src/gpu/`). Windows (D3D12 driver) and macOS/iOS
  (Metal driver) remain **code paths only, not validation claims**, until run on that real
  hardware — the same phased-claim discipline already used for `D3D11`/`D3D12` (Wine/DXVK/
  vkd3d-proton verified, real-Windows-hardware tasks marked `needs_human`) and for WebGPU
  (Linux-desktop-verified, Windows/macOS unverified).
- **Pipelines are immutable objects**, exactly like Vulkan/D3D12/WebGPU in this codebase — a
  pipeline cache keyed by (shader pair, vertex format, blend/depth-stencil/rasterizer state,
  sample count, target formats) is required from the start, not an afterthought.

---

## Naming conventions for this backend

| Item | Value |
| --- | --- |
| `CNA_GRAPHICS_BACKEND` value | `SDL_GPU` |
| CMake option | `CNA_BACKEND_SDL_GPU` |
| Compile definition | `CNA_BACKEND_SDL_GPU` |
| Backend directory | `src/CNA/Internal/Backends/SdlGpu/`, `include/CNA/Internal/Backends/SdlGpu/` |
| CMake target | `cna_backend_graphics_sdl_gpu` |
| Main class | `CNA::Internal::Backends::SdlGpuGraphicsBackend` (deliberately distinct from the existing `SdlGraphicsBackend` in `SdlRenderer/`, which wraps the older `SDL_Renderer` 2D API — the two are unrelated APIs and must not share a class name) |
| Task prefix | `SDLGPU-` |
| CTest target | `SdlGpu_Smoke` (plus one CTest binary per capability, mirroring `WebGPU_Colored3D`, `D3D11_Smoke`, etc.) |

---

## Active execution order — do this one task at a time

1. ~~`SDLGPU-1`~~ – ~~`SDLGPU-12`~~ — Phases `SDLGPU-1`/`SDLGPU-2` (infrastructure, device/window/
   swapchain lifecycle, color+depth+stencil clear/present) done and verified 2026-07-15, all ✅
   except two 🟨 rows noted in each row's own Notes column (debug-mode toggle not yet wired;
   `IGraphicsBackend`-family concrete subclasses besides the main backend class don't exist yet).
2. ~~`SDLGPU-13`~~ – ~~`SDLGPU-25`~~ — Phases `SDLGPU-3`/`SDLGPU-4`/`SDLGPU-5` (shader authoring
   decision, sprite2d pipeline, `Texture2D`, vertex/index buffers, `SpriteBatch`) done and
   verified 2026-07-15 — see each row's own Notes column for the handful of 🟨/⬜ sub-items
   (BlendState/DepthStencilState/RasterizerState/ApplySamplerState dynamic mapping, streaming
   hints, a genuine multi-key pipeline cache) that are real 3D-draw-path or polish work, not
   blockers for the 2D milestone itself.
3. ~~`SDLGPU-26`~~ – ~~`SDLGPU-30`~~ — Phase `SDLGPU-6` (`colored3d`/`textured3d`/
   `colored_textured3d`/`lit_textured3d`, i.e. `BasicEffect`, plus real `DrawPrimitivesEx`/
   `DrawIndexedPrimitivesEx` stride dispatch and a real depth-test occlusion proof) done and
   verified 2026-07-15, all ✅. The pipeline-cache/state-mapping generality this phase actually
   needed (`SDLGPU-17`'s `GetOrCreate*` pattern extended to 4 shader families keyed by
   topology+depthTest+depthWrite+depthFunc) landed as part of it; `SDLGPU-18`–`21`'s full
   `BlendState`/`DepthStencilState`/`RasterizerState`/`ApplySamplerState` *dynamic* mapping did
   not (every 3D pipeline is still hardcoded opaque/no-cull/Linear+Clamp) — see those rows' own
   Notes for what's real vs. still open.
4. ~~`SDLGPU-31`~~/~~`SDLGPU-32`~~ — `AlphaTestEffect`/`DualTextureEffect` done and verified
   2026-07-15, all ✅ (real per-pixel discard proof; real two-sampler-multiply proof). `SDLGPU-33`
   (`EnvironmentMapEffect`) and `SDLGPU-34` (`SkinnedEffect`) deliberately deferred at this point —
   both landed later the same session once their own blockers (no `TextureCube` foundation yet; an
   unresolved 72-bone-palette push-size question plus a new stride-52 vertex format) were resolved,
   see items 7/8 below.
5. **Phase `SDLGPU-8` (render targets) is fully done.** `SDLGPU-35` (`RenderTarget2D`), `SDLGPU-36`
   (`RenderTargetCube`, including real MSAA + mip regen), `SDLGPU-37` (MRT), `SDLGPU-38`
   (`RenderTarget2D` MSAA), and `SDLGPU-39`'s `RenderTarget2D`/`RenderTargetCube` `GetData()` legs
   all done and verified 2026-07-15, all ✅ — see their own rows for the real multi-pass
   `EnsureFrameRendered` refactor, the `DrawTarget{rt,cube,face}` generalization, the
   `currentExtraMrtTargets_` MRT mechanism, the `ClampSampleCount`/automatic-resolve MSAA
   mechanism, and the new `ITextureBackend::GetData` cross-backend interface addition this
   required, plus `SDLGPU-37`'s own row for a real cross-cutting resource-lifetime gotcha found
   while testing it (see the status banner's own callout). `SDLGPU-39`'s remaining legs (swapchain/
   plain `Texture2D`/`TextureCube`) stay 🟨: the swapchain leg hit a real, unresolved segfault
   specific to the swapchain texture as a download/copy source (see that row) — do not re-attempt
   without reading it first; plain `Texture2D` needs no fix (already-accurate CPU shadow); plain
   `TextureCube`'s leg is now also done for real (see item 6 below) — so `SDLGPU-39` is now only
   blocked on the swapchain segfault.
6. **Phase `SDLGPU-9` (`Texture3D` + plain `TextureCube`) is fully done.** `SDLGPU-40` (`Texture3D`),
   `SDLGPU-41` (mips), and `SDLGPU-51` (plain `TextureCube`) are all done and verified 2026-07-15,
   all ✅ — see `SDLGPU-40`'s own row for a real `cycle=true` GPU-resource-orphaning bug this
   phase's own byte-exact round-trip tests caught and fixed (confirmed absent for `SDLGPU-51` too
   via its own dedicated regression check).
7. **`SDLGPU-33` (`EnvironmentMapEffect`) done and verified 2026-07-15**, once both cube-texture
   sources existed — see its own row for the new `env_map3d.glsl` shader pair and the dual-backend
   cube resolve this required.
8. **`SDLGPU-34` (`SkinnedEffect`) done and verified 2026-07-15 — Phase `SDLGPU-7` is now fully
   closed.** The bone-palette push-size question was resolved via a real empirical spike: this
   backend's push-uniform-data mechanism has a genuine, undocumented ~4096-byte cap per slot, so
   the 72-bone (4608-byte) palette is uploaded as a real storage buffer
   (`SDL_BindGPUVertexStorageBuffers`) instead of a uniform push — see its own row for the full
   binary-search finding and fix. All of Phases `SDLGPU-1`–`SDLGPU-9` are now done.
9. **`SDLGPU-42`/`SDLGPU-43` (custom `ShaderEffect`) done and verified 2026-07-15 — Phase
   `SDLGPU-10` is now fully closed.** User chose a
   real runtime `libshaderc` GLSL→SPIR-V compile over deferring — see `SDLGPU-42`'s own row for a
   genuine finding this decision uncovered along the way (Vulkan's own `ShaderEffect` support
   doesn't actually runtime-compile GLSL text at all, unlike what the class's own documented
   contract says). `SdlGpuEffectBackend` + full `SpriteBatch` custom-effect wiring
   (`SetCustomEffect`/per-`SpriteCommand` uniform snapshot at `Draw()`-call time/`RenderSprites()`
   pipeline selection) verified end-to-end via `SdlGpu_ShaderEffect`, 3/3 checks, through the real
   public `ShaderEffect`/`SpriteBatch.Begin(effect)` API. **Correction**: this item's own prior text
   claimed "every phase currently in this plan is done" — that was wrong; `SDLGPU-17`–`20`
   (dynamic `BlendState`/`DepthStencilState`/`RasterizerState`, and the genuine keyed pipeline
   cache their combination needs) were still `⬜`/🟨 at this point, found on a later re-check and
   closed as item 10 below. `GraphicsDeviceManager`'s vsync-forwarding gap (listed here as still
   open) was also fixed separately the same day on `fix/graphicsdevicemanager-vsync` (backend-
   independent, not SDL_GPU-specific — see that branch's own commit).
10. **`SDLGPU-17`–`20` (real dynamic `BlendState`/`DepthStencilState`/`RasterizerState`, plus the
    hash-keyed pipeline cache all three together need) done and verified 2026-07-15.** Every
    pipeline family (sprite + all 7 3D shader families) previously hardcoded Opaque blending,
    `CullMode::None`, `FillMode::Solid`, and no stencil test at all — `GraphicsDevice.BlendState`/
    `DepthStencilState`/`RasterizerState` assignments never reached this backend's own pipeline-
    baked state. A real bug was found and fixed along the way, not just new plumbing added: SDL_gpu
    exposes `ReferenceStencil` as a genuine per-draw *dynamic* value (`SDL_SetGPUStencilReference`),
    not pipeline-baked, and this call was missing from every draw path — a real per-pixel stencil
    test could never actually use a non-zero reference. New `SdlGpu_RenderState` test, 13/13 checks,
    all real `RenderTarget2D::GetData()` pixel assertions (not "didn't throw"): `BlendState.Additive`/
    `AlphaBlend`, a real stencil-mask-then-masked-draw proof (temporarily reverting the
    `SDL_SetGPUStencilReference()` fix reproduced the original bug exactly, confirming the test
    catches it), a `CullMode` differential (`CullCounterClockwise`/`CullClockwise`/`None`), a
    `FillMode` differential (`Solid`/`WireFrame`), and a `ScissorTestEnable`+`ScissorRectangle`
    clip proof. `RasterizerState.DepthBias`/`SlopeScaleDepthBias` are captured but deliberately not
    applied (no SDL_gpu per-draw dynamic-depth-bias equivalent exists at all, unlike Vulkan's
    `vkCmdSetDepthBias`); `TwoSidedStencilMode`'s CCW fields are wired but not separately empirically
    differentiated by a dedicated front/back test in this pass — both documented, deliberate scope
    boundaries, not silent gaps. Full `ctest -R "SdlGpu"` re-run: **14/14 passing** (stable across
    repeated runs; this real-display GPU suite is occasionally flaky run-to-run on an unrelated
    test — a transient segfault on a different test each time, gone on immediate rerun, reproduces
    on unmodified code too — not something this task introduced). Full `CnaTests` (4367 of 4375,
    excluding 8 pre-existing-crash `ContentManagerSkinnedModelTest` cases confirmed to segfault
    identically on unmodified `feature/sdlgpu` HEAD): 4364 passed, 2 pre-existing hardware-sensor
    skips, 1 pre-existing failure (`ContextRecoveryTest.GetDataThrowsAfterFullUploadWithRecoveryDisabled`,
    also confirmed to fail identically on unmodified `feature/sdlgpu` HEAD) — zero regressions from
    this task's own changes.
11. **`SDLGPU-21` (`ApplySamplerState` for direct 3D draws) done and verified 2026-07-15.** The
    sampler-object cache itself already existed and was already verified via `SpriteBatch`; this
    closed the missing piece — a new `samplerSlots_[16]` array set by `ApplySamplerState()` and
    read directly into each `DrawCommand`'s pre-existing `textureFilter`/`addressU`/`addressV`
    fields at `Queue*Draw()` time (slot 0 for every single-texture family; `DualTextureEffect`
    genuinely needed independent slots 0 and 1 for its two texture units, so its own
    `DualTextureDrawCommand` gained separate `texture0*`/`texture1*` fields and
    `RenderDualTextureDraws` now binds two distinct samplers instead of one shared one). Also fixed
    a latent, stale default: the 5 single-texture `DrawCommand` structs hardcoded `addressU=1`
    (Clamp) — corrected to `0` (Wrap), matching `SamplerState.LinearWrap`'s real `GraphicsDevice`
    default (`SpriteCommand`'s own separate Clamp default is deliberately unchanged — XNA's real
    `SpriteBatch` default genuinely is `SamplerState.LinearClamp`, not the same thing).
    New `SdlGpu_SamplerState` test, 7/7 real pixel checks (`TextureAddressMode` Wrap-vs-Clamp past
    `U=1`; `TextureFilter` Point-vs-Linear at a colour boundary, matching the hand-derived blend
    weight exactly; `DualTextureEffect`'s two texture units resolving independently) —
    temporarily reverting `ApplySamplerState()` to a no-op reproduced the exact predicted failure
    pattern on all 3 checks, confirming the test and fix both genuinely matter. Full
    `ctest -R "SdlGpu"`: **15/15 passing**. Full `CnaTests` (same exclusions as item 10 above):
    4364 passed, 2 pre-existing skips, 1 pre-existing failure — identical to item 10's own numbers,
    zero regressions.

12. **Render-target-destroyed-before-flush use-after-free fixed for real, 2026-07-15.** Asked the
    user which architectural approach to pursue (a simpler "defer the GPU texture-handle release"
    fix that only guarantees no crash, vs. a fuller redesign that also guarantees correct content)
    — user chose the fuller redesign. New `SdlGpuRenderTarget2DState`/`SdlGpuRenderTargetCubeState`
    structs hold the actual `SDL_GPUTexture*` handles + clear-pending state, owned via
    `std::shared_ptr` instead of directly by `SdlGpuRenderTargetBackend`/
    `SdlGpuRenderTargetCubeBackend`. `usedRenderTargetsThisFrame_`/
    `usedRenderTargetCubeFacesThisFrame_` (and every queued `DrawTarget`) hold/reference that
    shared_ptr, so a render target's own pending `Clear()`/draws still execute correctly and its GPU
    texture handles stay valid for anything still sampling them, even if the
    `SdlGpuRenderTargetBackend` wrapper itself is destroyed mid-`Draw()`, before `Present()` ever
    runs. `RenderToTarget`/`RenderToTargetCubeFace` now take the state struct directly (not the
    wrapper); every existing wrapper-level accessor (`ColorTexture()`, `GetWidth()`, etc.) still
    works unchanged for callers that resolve it while the wrapper is alive (e.g. `SpriteBatch::Draw`'s
    own dual-backend resolve), just delegating to the new struct internally — no public/external API
    changed. `SdlGpuRenderTargetBackend`'s own destructor is now nearly empty (just clears
    `currentRenderTarget_` if it was the active one); the actual GPU release is deferred inside
    `SdlGpuRenderTarget2DState`'s destructor via the existing `QueueTextureRelease` mechanism
    (`SDLGPU-19`'s row), which only runs once every reference (the wrapper's own, plus
    `usedRenderTargetsThisFrame_`'s, plus any queued `DrawCommand`'s) has released it.
    `currentExtraMrtTargets_` (MRT's own secondary-target tracking) was deliberately NOT converted —
    a real, separate, pre-existing dangling-pointer risk if an MRT secondary target is destroyed
    while still the active MRT binding, narrower and rarer than the sampled-as-texture-input case
    this task fixed; left as a documented, out-of-scope finding, not fixed here.
    New `SdlGpu_RenderTargetLifetime` test (3/3 checks): a `RenderTarget2D` created, drawn into,
    sampled by a still-pending `SpriteBatch` draw, and destroyed — all as a local variable inside
    one `Draw()` call, before `Present()` ever runs. Confirmed via `git stash`-style before/after
    testing: reverting just the deferred-release piece reproduces the original segfault reliably
    (5/5 runs); with the fix, the test not only avoids the crash but reads back the *correct*
    sampled content (proving the fuller redesign's real value over the simpler crash-only fix, which
    was verified along the way to leave the sampled content undefined/wrong even though it no longer
    crashes). **Also found and fixed a real, unrelated pre-existing regression while chasing this
    down**: `sdlgpu_envmap_test.cpp`'s own quad was wound CCW under the identity view/projection it
    uses — harmless while `SDLGPU-18/19/20`'s `RasterizerState.CullMode` was still hardcoded to
    `None`, but once real dynamic culling landed, the project's own default
    `RasterizerState.CullCounterClockwise` started genuinely culling it, silently regressing the
    test (confirmed via `git stash` to the `SDLGPU-21` commit, reproducing the failure
    deterministically 5/5 times — this had gone undetected in that task's own "14/14 passing"/
    "15/15 passing" claims, an honest correction, not a new gap). Fixed by flipping its winding to
    match this project's own established CW convention for identity-view/projection tests (same
    fix already applied to `sdlgpu_renderstate_test.cpp`/`sdlgpu_samplerstate_test.cpp` earlier the
    same day). Full `ctest -R "SdlGpu"`: **16/16 passing**, stable across 3 repeated runs. Full
    `CnaTests` (same exclusions as items 10/11): 4364/4367 passed, identical numbers to those
    items' own — zero regressions.

The documented open findings (swapchain-readback segfault, full-`CnaTests` in-process instability
under this backend in this environment, `currentExtraMrtTargets_`'s own narrower dangling-pointer
risk if an MRT secondary target is destroyed while still bound) remain the only unresolved items in
this plan.

---

## Phase SDLGPU-1 — Infrastructure and CMake integration

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-1 | Add `CNA_GRAPHICS_BACKEND=SDL_GPU` as a tenth valid value: extend the `CACHE STRING`/`set_property(... STRINGS ...)` list (`CMakeLists.txt` ~L94–95), the `option(CNA_BACKEND_SDL_GPU ...)` declaration and its inclusion in the mutual-exclusion `_cna_enabled_backends` list (~L97–147). | ✅ | Done 2026-07-15. Also updated `tests/.../GraphicsBackendCompileDefinitionTests.cpp`'s `ExactlyOneGraphicsBackendIsSelected` counter, which would otherwise fail under this backend. |
| SDLGPU-2 | Add the `elseif(CNA_GRAPHICS_BACKEND STREQUAL "SDL_GPU")` branch (~L184–255) setting `BACKEND_DIR`, `BACKEND_TARGET`, `add_compile_definitions(CNA_BACKEND_SDL_GPU)`. | ✅ | Done 2026-07-15, mirroring `HEADLESS`/`SOFTWARE` — no `find_package` call needed. |
| SDLGPU-3 | Add the matching backend-library-target block (~L301+) and, if the backend needs test-only source files, its own `CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "SDL_GPU"` guard (mirroring `HEADLESS`/`SOFTWARE`/`D3D11`/`D3D12` at ~L6497–6799). | ✅ | Link block done 2026-07-15 (`SDL3::SDL3` only). The `CNA_BUILD_TESTS` guard was added directly as `SdlGpu_Smoke`'s own registration (see SDLGPU-12), not as an empty placeholder. **Follow-up fix 2026-07-15 (adversarial-review finding):** the default `cmake --build cmake-build-sdlgpu` failed linking `cna_reference_dump`/`cna_demo_xact` with "undefined reference to `CNA::Logger::Warn`" — `SdlGpuGraphicsBackend.cpp` calls this CNA-defined symbol for its own non-fatal capability-gap warnings, hitting the exact same single-pass static-archive-scanning problem under GNU ld that D3D11/D3D12 already had a fix for (`target_link_libraries(${BACKEND_TARGET} PRIVATE CNA)`, ~L420–432), but that fix's `if()` condition was never extended to cover `SDL_GPU`. Fixed by adding `SDL_GPU` to that condition. Verified: `cna_reference_dump` now links cleanly; `cna_demo_xact` still fails, but only on its unrelated post-build Content-directory copy step (missing `examples/demo_xact/Content`, a pre-existing XACT-audio-demo asset gap, confirmed unrelated to graphics/linking). |
| SDLGPU-4 | Create `include/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.hpp` — class skeleton declaring every `IGraphicsBackend`-family interface (`IVertexBufferBackend`, `IIndexBufferBackend`, `ITextureBackend`, `ITextureCubeBackend`, `ITexture3DBackend`, `IRenderTargetBackend`, `IRenderTargetCubeBackend`, `IEffectBackend`, `ISpriteBatchBackend`, `IOcclusionQueryBackend`, `IGraphicsBackend`). | ✅ | **Row corrected 2026-07-16 — was stale.** This row was still marked 🟨 ("concrete subclasses don't exist yet") even though every one of these interfaces has had a real concrete implementation for a long time: `SdlGpuTextureBackend`, `SdlGpuTexture3DBackend`, `SdlGpuRenderTargetBackend`, `SdlGpuRenderTargetCubeBackend`, `SdlGpuTextureCubeBackend`, `SdlGpuVertexBufferBackend`, `SdlGpuIndexBufferBackend`, `SdlGpuEffectBackend`, `SdlGpuSpriteBatchBackend` all exist in this same header. Only `IOcclusionQueryBackend` has no concrete subclass — but that is `SDLGPU-44`'s own documented, permanent non-gap (no occlusion-query API exists in `SDL_gpu` at all), not something this row was ever waiting on. This row was simply never updated when those classes were built out phase by phase. |
| SDLGPU-5 | Create `src/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.cpp` — constructor/destructor scaffolding, explicit `ThrowUnsupported...`-style paths for every not-yet-implemented method, so the backend compiles and links from task 1. | ✅ | Done 2026-07-15 (`ThrowNotImplemented()`); `CreateTexture`/`CreateSpriteBatch`/`CreateVertexBuffer`/`CreateIndexBuffer16`/`DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` all throw, verified by `SdlGpu_Smoke`'s own throw checks. |

---

## Phase SDLGPU-2 — Device, window and swapchain lifecycle

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-6 | `SDL_CreateGPUDevice` — request `SDL_GPU_SHADERFORMAT_SPIRV` first (Linux/Vulkan driver target), debug-mode flag wired to a CNA-side validation/debug toggle. | ✅ | 2026-07-16. `debug_mode` now mirrors `D3D11GraphicsBackend`'s own `#ifndef NDEBUG` CNA-side toggle (design decision 12's "debug layer is a debug-build convenience, never a hard requirement" rationale) — a debug build requests `SDL_gpu`'s own validation layer, a release build does not. New `debugModeEnabled_` field + `IsDebugModeEnabledEXT()` accessor (mirrors `D3D11GraphicsBackend::IsDebugLayerEnabledEXT()`), logged at backend-init time. `SDL_GPUSupportsShaderFormats`-gated startup error not added — `SDL_CreateGPUDevice` returning null already throws with `SDL_GetError()`, judged sufficient for now. **Real, previously-silent bugs found and fixed via turning this on for real, not just a cosmetic wiring change:** enabling `debug_mode` for the first time surfaced 2 genuine, pre-existing `SDL_gpu` API-contract violations that had always been silently tolerated with `debug_mode` hardcoded `false` — both caused a genuine unkillable-by-SIGTERM hang under Vulkan validation, not just a warning. (1) `RenderTargetCube`'s own MSAA design (`SDLGPU-36`) used a 6-layer `SDL_GPU_TEXTURETYPE_2D_ARRAY` texture with `sample_count>1` — `SDL_gpu` explicitly forbids `sample_count>1` on any array texture ("For array textures: sample_count must be SDL_GPU_SAMPLECOUNT_1"). Fixed by switching to a single-layer `SDL_GPU_TEXTURETYPE_2D` texture shared across whichever face is currently the active render target (same convention `depthTexture` already uses), with `SDL_GPUColorTargetInfo.cycle=true` when writing into it (required per `SDL_gpu.h`'s own doc comment on reusing a bound resource across passes without cycling — a second, related hazard this fix also had to address) — `cubeTexture` itself (whether as the direct target or the resolve target) must never cycle, since that would wipe every other face already written this frame. (2) `Texture3D`/`TextureCube`'s mipmap generation (`SDLGPU-40`/`41`/`51`) called `SDL_GenerateMipmapsForGPUTexture` on textures created with `SAMPLER` usage only — `SDL_gpu` requires `SAMPLER | COLOR_TARGET` for that call. Fixed by widening `createInfo.usage` to include `COLOR_TARGET` when `mipMap` is requested (unchanged for the non-mipmap case). Verified: `SdlGpu_RenderTargetCube` (7/7), `SdlGpu_Texture3D` (6/6), `SdlGpu_TextureCube` (5/5) all pass cleanly with `debug_mode` genuinely on, no hang, full `ctest`/`CnaTests` sweep 4387/4387 zero regressions. **Two further, separate, non-blocking issues found in the same investigation, deliberately NOT fixed here (reported, not silently expanded into):** a Vulkan validation warning (`VUID-vkCmdDraw-renderPass-02684`, depth-stencil attachment incompatible between two render passes) appears during `SdlGpu_RenderTargetCube`'s depth-tested check but does not affect its correctness readback; and `Texture3D`'s own mip-chain auto-generation produces `vkCmdBlitImage`/`vkCreateImageView` validation errors specifically for the depth dimension (likely `SDL_gpu`'s blit-based generator assuming standard per-level depth-halving, which doesn't hold for this backend's own FNA-matching "depth does not participate in the mip-level count" convention) — `SdlGpu_Texture3D`'s existing mipmap check uses a uniform test color that cannot actually discriminate correct vs. corrupted output here, so this may be a real, still-undetected correctness gap in `Texture3D`'s generated-mipmap path specifically (authored, i.e. explicit per-level `SetData`, mipmaps are unaffected). |
| SDLGPU-7 | `SDL_ClaimWindowForGPUDevice` + present-mode negotiation (`SDL_WindowSupportsGPUPresentMode`, `SDL_SetGPUSwapchainParameters`) mapped from XNA's `PresentationParameters`/vsync equivalent. | ✅ | Done 2026-07-15. Verified both paths: VSync (default) and Immediate (every SDL_GPU example/test sets `gdm_->setSynchronizeWithVerticalRetraceProperty(false)` before first device creation — see this file's top-of-file note; this replaced an earlier direct-backend-poke workaround once `GraphicsDeviceManager`'s forwarding gap was fixed in `develop`). |
| SDLGPU-8 | Per-frame `SDL_AcquireGPUCommandBuffer` + `SDL_WaitAndAcquireGPUSwapchainTexture` (or `SDL_AcquireGPUSwapchainTexture` for the non-blocking variant), including the documented zero-size/minimized-window null-texture case. | ✅ | Done 2026-07-15. The null-texture case is handled (submit the empty command buffer, skip the frame) per `SDL_gpu.h`'s own documented contract, but not independently exercised by a real minimized-window test yet — code-path only for that specific branch. |
| SDLGPU-9 | Main render pass (`SDL_BeginGPURenderPass`/`SDL_EndGPURenderPass`) with color + depth + stencil `SDL_GPUColorTargetInfo`/`SDL_GPUDepthStencilTargetInfo` attachments. | ✅ | Done 2026-07-15, including real depth+stencil texture creation/recreation-on-resize (`EnsureDepthStencilTexture`, D24_UNORM_S8_UINT preferred, D32_FLOAT_S8_UINT fallback, per-device `SDL_GPUTextureSupportsFormat` query). Verified via `SdlGpu_Smoke`'s combined `Target\|DepthBuffer\|Stencil` clear, not yet by an actual depth-tested draw (no draw path exists yet). |
| SDLGPU-10 | Clear semantics — map XNA's `ClearOptions` combinations (`Target`/`DepthBuffer`/`Stencil` and their unions, i.e. `Clear`/`ClearColorAndDepth`/`ClearDepth`/`ClearStencil`/`ClearDepthAndStencil`/`ClearColorAndStencil`/`ClearColorDepthAndStencil`) onto `SDL_GPULoadOp::CLEAR` vs `LOAD` per attachment. | ✅ | Done 2026-07-15, all 7 `IGraphicsBackend` clear methods implemented; `SdlGpu_Smoke` exercises the combined color+depth+stencil path every frame. |
| SDLGPU-11 | Present via `SDL_SubmitGPUCommandBuffer` (or `SDL_SubmitGPUCommandBufferAndAcquireFence` where a fence is needed for readback), with recoverable handling of a failed/occluded swapchain acquisition. | ✅ | Done 2026-07-15 for the one recoverable case `SDL_gpu` itself documents (null swapchain texture, e.g. minimized window — submit and skip, no throw). There is no WebGPU-style surface-loss/outdated/suboptimal recovery case to add here: `SDL_gpu`'s own API doesn't expose one at this level. **Hard-acquisition-failure recovery proven for real 2026-07-16**: a genuine `SDL_WaitAndAcquireGPUSwapchainTexture` failure (as opposed to the already-handled null-texture/minimized-window case) is hard to trigger via real hardware/driver-loss events, but `SDL_ReleaseWindowFromGPUDevice`/`SDL_ClaimWindowForGPUDevice` gives a safe, controlled way to force the exact same failure — un-claiming the window makes the next acquisition attempt fail for real, with a real `SDL_GetError()` message, through the identical code path a genuine device-loss event would take. New `SdlGpu_SwapchainRecovery` test, 5/5 real checks: normal frames render fine beforehand; releasing the window then forcing `Present()` throws as documented; re-claiming the window succeeds; a subsequent `Present()` succeeds with no exception (the backend recovers cleanly — `framePending_` correctly stays set across the failed attempt, so the same frame's queued `Clear()` isn't lost); many further frames afterward continue to render correctly, proving the backend stays fully usable, not just "didn't crash immediately". |
| SDLGPU-12 | First end-to-end proof: a minimal native window that initializes the backend, clears, presents at least 60 frames, then exits cleanly. | ✅ | Verified 2026-07-15: `examples/sdlgpu_smoke_test.cpp` + `SdlGpu_Smoke` CTest, real window + real `SDL_GPUDevice` (Vulkan driver) on this Linux dev machine, 60 frames of combined color+depth+stencil `Clear()`+`Present()`, plus `GetWindowInternal`/`GetRendererInternal`/`GetViewportSize`/`CreateVertexBuffer`/`CreateIndexBuffer16` checks — 6/6 checks, `ctest -R SdlGpu_Smoke` passes in ~2s (with VSync off; VSync itself was also confirmed working, at ~1s/frame on this environment's virtual display, before switching the smoke test to Immediate for speed). |

---

## Phase SDLGPU-3 — Shader authoring and resource-binding strategy

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-13 | **Decision task.** Choose the shader pipeline: (a) hand-author SPIR-V for the Vulkan driver by extending the existing `compile_shaders.py`/`libshaderc` runtime-compile pattern with `SDL_gpu`'s mandatory binding order, deferring DXBC/DXIL/MSL authoring; or (b) vendor the official [`SDL_shadercross`](https://github.com/libsdl-org/SDL_shadercross) tool to compile a single HLSL source to all four formats. | ✅ | Decided 2026-07-15: (a). `src/CNA/Internal/Backends/SdlGpu/shaders/compile_shaders.py` (adapted from the Vulkan backend's own script) compiles GLSL → SPIR-V at build time via `libshaderc`, following `SDL_gpu`'s *graphics-pipeline* binding convention specifically (vertex-stage textures=set0/UBOs=set1, fragment-stage textures=set2/UBOs=set3 — a different, narrower convention than the compute-pipeline one also documented in `SDL_gpu.h`; do not confuse the two when adding more shaders). `SDL_shadercross` remains unvendored; revisit (b) only when Windows/macOS driver work actually starts. |
| SDLGPU-14 | `sprite2d` SDL-GPU shader pair (vertex + fragment), compiled to SPIR-V, satisfying `SDL_GPUShaderCreateInfo`'s explicit `num_samplers`/`num_storage_textures`/`num_storage_buffers`/`num_uniform_buffers` counts. | ✅ | Done and runtime-verified 2026-07-15 (`SdlGpu_2D` CTest + a real screenshot, see Phase `SDLGPU-5` below). **Found and fixed a real bug via the screenshot, not just the "no exception" CTest**: `SDL_gpu`'s Vulkan driver flips clip-space Y internally (for cross-backend NDC consistency) — the vertex shader's original `gl_Position = vec4(ndc, 0, 1)` silently rendered every sprite upside down (a texture's top row appeared at the bottom). Fixed by negating Y (`vec4(ndc.x, -ndc.y, 0, 1)`), confirmed by an isolated single-sprite diagnostic (`examples/sdlgpu_diag_single_sprite.cpp`, kept as a permanent manual diagnostic tool, same precedent as `cna_diag_d3d12_swapchain`) before and after the fix. |
| SDLGPU-15 | `SDL_GPUVertexInputState`/`SDL_GPUVertexAttribute`/`SDL_GPUVertexElementFormat` mapping for the four stock vertex strides: `VertexPositionColor` (16), `VertexPositionTexture` (20), `VertexPositionColorTexture` (24), `VertexPositionNormalTexture` (32). | ✅ | **Row corrected 2026-07-15 — was stale.** This row was still marked ⬜ ("not started") even though Phase `SDLGPU-6` (`SDLGPU-26`–`29`, all ✅, done and verified 2026-07-15) actually built exactly this: `colored3d` (stride 16), `textured3d` (stride 20), `colored_textured3d` (stride 24), and `lit_textured3d` (stride 32, `VertexPositionNormalTexture`) each have their own real `SDL_GPUVertexInputState`/`SDL_GPUVertexAttribute`/`SDL_GPUVertexElementFormat` mapping, all runtime-verified via screenshots and CTest. This row was simply never updated when that phase closed. |
| SDLGPU-16 | Per-draw uniform delivery via `SDL_PushGPUVertexUniformData`/`SDL_PushGPUFragmentUniformData` (world/view/projection, `DiffuseColor`, alpha-test params, light params), respecting the documented std140 alignment rule (`vec3`/`vec4` fields 16-byte aligned). | ✅ | **Row corrected 2026-07-15 — was stale.** This row was still marked 🟨 ("3D-specific payloads not implemented") even though Phase `SDLGPU-6`/`SDLGPU-7` (all ✅, done and verified 2026-07-15) actually built exactly this: world/view/projection + `DiffuseColor` push in `textured3d`/`colored_textured3d` (`SDLGPU-27`/`28`), full `ComputeLights()` light params in `lit_textured3d` (`SDLGPU-29`), alpha-test params in `AlphaTestEffect` (`SDLGPU-31`), and per-bone skin-matrix delivery in `SkinnedEffect` (`SDLGPU-34`, which also empirically found and documented the real ~4096-byte `SDL_PushGPUVertexUniformData` per-slot cap this row asked to "measure empirically" — worked around via a storage buffer instead). This row was simply never updated when those phases closed. |

---

## Phase SDLGPU-4 — Pipelines and render state

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-17 | `SDL_GPUGraphicsPipelineCreateInfo` construction plus a pipeline cache keyed by (shader pair, vertex format, blend state, depth/stencil state, rasterizer state, sample count, color/depth target formats). | ✅ | 2026-07-15 (closed alongside `SDLGPU-18`–`20`). `PipelineCacheKey()` now folds topology/depthTest/depthWrite/depthFunc/colorFormat plus the *full* `RenderStateSnapshot` (blend, cull, fill mode, stencil) via a boost-style `HashCombine()` chain, replacing the old 5-field hand-packed `int` key — a hash was chosen specifically to avoid `VulkanGraphicsBackend`'s own documented bit-packing budget problem once every render-state dimension is folded in. Disabled dimensions (blend off, stencil off, two-sided off) always collapse their own sub-fields out of the hash regardless of value, mirroring Vulkan's own `PackBlendBits`'s "collapse to 0 when disabled" rule, so irrelevant values don't fragment the cache with duplicate pipelines. All 12 cache maps (sprite + 7 3D families, `AlphaTest3D`'s stride-24 map included) changed from `unordered_map<int, ...>` to `unordered_map<size_t, ...>`; the custom `ShaderEffect` pipeline cache (`SdlGpuEffectBackend::pipelines_`) deliberately kept as `int` since its own pipeline stays out of this task's scope (see `SDLGPU-19`/`20`'s own notes). |
| SDLGPU-18 | `BlendState` mapping — XNA `BlendState` → `SDL_GPUColorTargetBlendState` (`SDL_GPUBlendFactor`/`SDL_GPUBlendOp`). | ✅ | 2026-07-15. `ApplyBlendState()` overridden; `ToBlendFactor`/`ToBlendOp` mirror `VulkanGraphicsBackend`'s own XNA-ordinal tables exactly. A new shared `FillBlendState()` fills every pipeline family's (sprite + all 7 3D shader families) own `SDL_GPUColorTargetBlendState` from a captured `RenderStateSnapshot` instead of a hardcoded Opaque/no-op struct. Verified: `BlendState.Additive` over a Red background reads back `(255,128,0)` (real colour-add, not a stuck-Opaque overwrite — note XNA's own `Color.Green` is `(0,128,0)`, not lime); `BlendState.AlphaBlend` (`One`/`InverseSourceAlpha`) with a half-alpha source over White reads back mid-grey. |
| SDLGPU-19 | `DepthStencilState` mapping — `SDL_GPUDepthStencilState` (`SDL_GPUCompareOp`, `SDL_GPUStencilOpState`), covering `DepthBufferEnable`/`DepthBufferWriteEnable`/`StencilEnable` and all three XNA stencil-op fields (`StencilFail`/`StencilDepthBufferFail`/`StencilPass`). | ✅ | 2026-07-15. `ApplyDepthStencilState()` overridden, forwarding into the same `depthTestEnabled_`/`depthWriteEnabled_`/`depthCompareFunction_` fields the pre-existing `SetDepthTestEnabled`-style shortcuts already used (whichever setter runs last wins, by design), plus a new `stencilParams_`/`stencilReadMask_`/`stencilWriteMask_`. A new shared `FillDepthStencilState()` bakes `StencilFail`/`StencilPass`/`StencilDepthBufferFail`/`StencilFunction` into every pipeline family's front/back `SDL_GPUStencilOpState` (`TwoSidedStencilMode=false` copies front into back, matching this project's own EasyGL/Vulkan fallback convention — **not** empirically differentiated front-vs-back in this pass; a dedicated two-sided CW/CCW differential test was not written, a documented scope boundary, not a silent gap). **Real bug found and fixed along the way:** `GraphicsDevice.ReferenceStencil`/`DepthStencilState.ReferenceStencil` were captured into `referenceStencil_` but never actually reached the GPU — SDL_gpu exposes stencil reference as a genuine *per-draw dynamic* value (`SDL_SetGPUStencilReference`), not a pipeline-baked one, and this call was missing entirely from every draw path. Fixed by adding `stencilReference` to `RenderStateSnapshot` (captured per-`DrawCommand`/`SpriteCommand` at queue time, like the rest of the snapshot) and calling `SDL_SetGPUStencilReference()` in all 8 `Render*Draws`/`RenderSprites` call sites. Verified via a real per-pixel stencil-mask proof (`StencilFunction=Always`/`StencilPass=Replace`/`ReferenceStencil=1` writes a mask over the left half only; a second full-screen `StencilFunction=Equal`/`ReferenceStencil=1` draw shows its new colour ONLY on the left, leaving the right half's background untouched) — **temporarily disabling the new `SDL_SetGPUStencilReference()` calls reproduced the original bug exactly** (right half wrongly passed too), confirming the test and the fix both genuinely matter. **Second real bug found and fixed 2026-07-15 (adversarial-review finding):** `stencilReadMask_`/`stencilWriteMask_` (`DepthStencilState.StencilMask`/`StencilWriteMask`) were captured by `ApplyDepthStencilState()` but were genuinely dead code — `FillDepthStencilState()` always baked `compare_mask`/`write_mask` as hardcoded `0xFF` regardless of their value, and `PipelineCacheKey()` didn't hash them at all. Fixed by moving both into `StencilKeyParams` (`readMask`/`writeMask`, default `0xFF` matching real XNA's `0x7FFFFFFF` truncated to SDL_gpu's `Uint8` fields), hashing them (truncated to `Uint8`) into the pipeline cache key, and having `FillDepthStencilState()` apply the real values instead of the hardcoded constant. New `SdlGpu_RenderState` checks G/H (write-mask: only the half of a render target with a non-zero `StencilWriteMask` actually receives a stencil write, verified via a subsequent `StencilFunction=Equal` reveal pass; read-mask: a deliberately mismatched `StencilFunction=Equal`/`ReferenceStencil=0` compare still passes everywhere because `StencilMask=0x00` zeroes both sides of the compare) bring the suite to 16/16. Git-stash-verified: reverting `FillDepthStencilState()`'s two lines back to hardcoded `0xFF` reproduced exactly the predicted G/H failures; restoring the fix returned 16/16. |
| SDLGPU-20 | `RasterizerState` mapping — `SDL_GPURasterizerState` (`SDL_GPUFillMode`, `SDL_GPUCullMode`, `SDL_GPUFrontFace`), covering `CullMode.CullClockwiseFace`/`CullCounterClockwiseFace`/`None` and `FillMode.WireFrame`/`Solid`. | ✅ | 2026-07-15. `ApplyRasterizerState()` overridden; `ToCullMode` mirrors this project's own EasyGL backend's real, hardware-validated cull mapping (`CullClockwiseFace`→`SDL_GPU_CULLMODE_BACK`, `CullCounterClockwiseFace`→`SDL_GPU_CULLMODE_FRONT`, `front_face` hardcoded `COUNTER_CLOCKWISE` everywhere — **not** Vulkan's own documented Task-870 front/back swap, since this backend's 3D shaders need no NDC Y-flip like EasyGL's). A new shared `FillRasterizerState()` bakes `cull_mode`/`fill_mode` into every pipeline family from the captured snapshot. `SetScissorRect()`/`ApplyScissorForPass()` also added (`SDL_SetGPUScissor`, applied once per render pass using the live scissor state as of that pass's own flush — **not** re-snapshotted per queued draw like blend/cull/stencil, a deliberate, documented simplification; real XNA games rarely change `ScissorRectangle` differently across different render targets within one frame). Depth bias (`RasterizerState.DepthBias`/`SlopeScaleDepthBias`) is captured but **not applied** — SDL_gpu has no dynamic per-draw depth-bias equivalent to Vulkan's `vkCmdSetDepthBias` at all, so this would require folding floats into the pipeline cache key; a genuine SDL_gpu API-surface limitation, not a silent drop. Verified: the same CW-wound quad shows visible under `CullCounterClockwise` (this project's own default) and `CullNone`, culled under `CullClockwise`; the same triangle's centroid pixel is filled under `FillMode.Solid` but stays background under `FillMode.WireFrame`; a full-screen draw with `ScissorTestEnable=true` + a left-half `ScissorRectangle` only affects the left half. |
| SDLGPU-21 | `SamplerState` mapping — `SDL_GPUSamplerCreateInfo` (`SDL_GPUFilter`, `SDL_GPUSamplerMipmapMode`, `SDL_GPUSamplerAddressMode`) for per-slot Wrap/Clamp/Mirror + Point/Linear/Anisotropic. | ✅ | 2026-07-15. `ApplySamplerState(slot, filter, addressU, addressV, maxAnisotropy)` overridden — stores into a new `samplerSlots_[16]` array (matching `SamplerStateCollection::MaxSamplers`), read directly into each `DrawCommand`'s own pre-existing `textureFilter`/`addressU`/`addressV` fields at `Queue*Draw()` time (not via `RenderStateSnapshot`, since these fields already existed and were just hardcoded before). Every direct-3D-draw family (`Textured`/`LitTextured`/`AlphaTest`/`EnvMap`(diffuse texture0 only — the env map itself stays fixed Linear+Clamp, unchanged)/`Skinned`) now reads slot 0's live sampler state instead of a hardcoded Linear+Clamp. **`DualTextureEffect` genuinely needed two independent slots**, not one: its `DualTextureDrawCommand` gained separate `texture0Filter/AddressU/AddressV` (slot 0) and `texture1Filter/AddressU/AddressV` (slot 1) fields, and `RenderDualTextureDraws` now binds two distinct `SDL_GPUSampler` objects instead of reusing one shared sampler for both textures — real XNA lets `GraphicsDevice.SamplerStates[0]`/`[1]` differ for these two texture units. Also corrected the 5 single-texture `DrawCommand` structs' stale hardcoded default (`addressU=1`/`addressV=1`, i.e. Clamp) to `0`/`0` (Wrap), matching `SamplerState.LinearWrap` — the real XNA `GraphicsDevice.SamplerStates[slot]` default (`SpriteCommand`'s own separate default is deliberately left at Clamp, since XNA's real `SpriteBatch` default is `SamplerState.LinearClamp`, a distinct and correct behavioral difference, not an inconsistency to fix). `maxAnisotropy` is stored but not applied — `GetOrCreateSampler()`'s cache has no anisotropic-filtering dimension at all, a pre-existing limitation shared with `SpriteBatch`'s own sampler path, not introduced here. New `SdlGpu_SamplerState` test, 7/7 real `RenderTarget2D::GetData()` pixel checks: `TextureAddressMode` (`PointWrap` reads a wrapped/repeated texel past `U=1`, `PointClamp` reads the held edge texel instead); `TextureFilter` (`PointClamp` snaps to a sharp texel at a colour boundary, `LinearClamp` reads a real, distinctly different blended value — matches the hand-derived blend weight exactly); and `DualTextureEffect`'s two texture units resolving independently (`texture0=Wrap`/`texture1=Clamp` at the same shared UV multiplies to White; either texture using the *other's* address mode would multiply to Black instead — temporarily reverting `ApplySamplerState()` to a no-op reproduced exactly this failure pattern, confirming the test and fix both genuinely matter). Full `ctest -R "SdlGpu"`: **15/15 passing**, zero regressions. |
| SDLGPU-52 | Draw-call ordering — queued 3D/sprite draws must render in the game's real chronological `Draw()`/`SpriteBatch.Draw()` issue order within one render pass, not grouped by shader family, so alpha-blend/transparency layering between interleaved 3D and 2D draws is correct. | ✅ | 2026-07-15 (adversarial-review finding #4). The backend always rendered all 7 3D shader families first, then every sprite last, regardless of real call order — a game doing background `SpriteBatch` → 3D model → HUD `SpriteBatch` got both sprite batches rendered after the 3D draw, breaking layering. Fixed with a new `drawOrder_` (`vector<QueuedDrawRef{kind,index}>`) appended to by every `Queue*Draw()`/`QueueSprite()` call right after its own family `push_back` — no sort needed, since append-at-call-time already is real chronological order. The old fixed `RenderColoredDraws(); RenderTexturedDraws(); ... RenderSprites();` sequence (3 call sites: `EnsureFrameRendered`/`RenderToTarget`/`RenderToTargetCubeFace`) is replaced by one `RenderQueuedDraws()` that replays `drawOrder_` once, dispatching each ref to a new small per-kind `Issue*Draw()` function (mechanical extraction of each old family loop's body, same readiness/target-filter rules, just callable per-index). `boundPipeline` is now tracked globally across every kind (previously only sprites had this rebind-skip). New `SdlGpu_DrawOrder` test, 2/2 real `RenderTarget2D::GetData()` checks: an opaque 3D quad and an opaque sprite drawn into the same target with depth testing off, in both orders (sprite-then-3D and 3D-then-sprite) — whichever was issued LAST correctly wins the final pixel each time. Git-stash-verified: temporarily recreating the old "all 3D first, sprites last" behavior (two passes over `drawOrder_` instead of one) reproduced exactly the predicted failure (the sprite-then-3D check incorrectly read back the sprite's color); restoring the fix returned 2/2. Full `ctest -R "SdlGpu"` + `CnaTests`: 17/17 + 4386/4386, zero regressions. |

---

## Phase SDLGPU-5 — 2D vertical slice: Texture2D + buffers + SpriteBatch

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-22 | `SdlGpuTexture2DBackend` — `SDL_CreateGPUTexture` + upload via `SDL_CreateGPUTransferBuffer`/`SDL_MapGPUTransferBuffer`/`SDL_UnmapGPUTransferBuffer`/`SDL_BeginGPUCopyPass`/`SDL_UploadToGPUTexture`/`SDL_EndGPUCopyPass`. | ✅ | Done and runtime-verified 2026-07-15 (`SdlGpu_2D` CTest + screenshot). `R8G8B8A8_UNORM`, `SAMPLER` usage only, single mip level — no render-target usage, no mip chain, no 3D/cube variants (later phases). |
| SDLGPU-23 | `SdlGpuVertexBufferBackend`/`SdlGpuIndexBufferBackend` — `SDL_CreateGPUBuffer` (`VERTEX`/`INDEX` usage) + upload via the same transfer-buffer/copy-pass pattern; `SetDataWithOptions` `Discard`/`NoOverwrite` streaming hints. | ✅ | `SetData`/`SetData16`/`SetData32` done and runtime-verified (`SdlGpu_Smoke`'s round-trip checks); every upload recreates the transfer buffer and grows the GPU buffer only when capacity is exceeded. **`SetDataWithOptions`/`SetData16WithOptions`/`SetData32WithOptions` now real overrides, done 2026-07-16** — maps directly to `SDL_UploadToGPUBuffer`'s own `cycle` flag, mirroring `EasyGLVertexBufferBackend`'s established `Discard`→orphan/`NoOverwrite`→in-place convention: `Discard`/`None` cycle the buffer to a fresh backing resource (`SDL_gpu.h`'s own documented `cycle=true` behavior, avoiding a GPU stall on any in-flight read of the old data — this was ALREADY the plain `SetData()`/`SetData16()`/`SetData32()` path's hardcoded behavior, unchanged); `NoOverwrite` does not cycle (`cycle=false`, the caller's own promise that no in-flight GPU read is being overwritten). **Verification note, stated honestly:** a full-buffer overwrite's correctness is identical either way — `cycle` only affects whether the GPU stalls on in-flight reads of the old backing memory, not what ends up readable afterward, since this backend always replaces the buffer's entire contents in one call (never a partial sub-range) — so `SdlGpu_Smoke`'s new checks (byte-exact round-trip after `Discard` then `NoOverwrite`, for both vertex and index buffers) prove the overrides are real and no longer silently ignored (the previous `IGraphicsBackend` default behavior), not a visually-distinguishing behavior difference, since none exists for this call pattern. |
| SDLGPU-24 | `SdlGpuSpriteBatchBackend` — batch quads, bind the `sprite2d` pipeline, bind texture+sampler via `SDL_BindGPUFragmentSamplers`, draw via `SDL_DrawGPUIndexedPrimitives`. Must cover source rectangles, tint/alpha, rotation, both flips, and Linear/Point + Clamp/Wrap/Mirror sampling. | ✅ | Done and runtime-verified 2026-07-15 — uses `SDL_DrawGPUPrimitives` (non-indexed, 6 vertices/sprite, one draw call per sprite into a shared per-frame vertex buffer) rather than `SDL_DrawGPUIndexedPrimitives`; behaviorally equivalent for this milestone, not yet batched into fewer draw calls. `SdlGpu_2D` CTest covers source rects, tint, alpha, rotation, both flips, and Point+Wrap/Linear+Wrap sampling; all confirmed visually correct via screenshot (see `SDLGPU-25`). |
| SDLGPU-25 | **First milestone gate.** A demo/smoke harness renders textured, tinted, rotated, flipped sprites and survives a resize through the SDL GPU backend with no validation error, device loss, or loader failure. | ✅ | Verified 2026-07-15: `examples/sdlgpu_2d_test.cpp` (`SdlGpu_2D` CTest, 3/3 checks) plus a real screenshot (captured via `import -window`, not just "didn't throw") of a 6-sprite scene — opaque quadrant texture, alpha-blended tint, 45°-rotated quadrant, combined horizontal+vertical flip (colors correctly permuted: white/blue/green/red corners), and Point+Wrap/Linear+Wrap sampling with a source rect exceeding the texture bounds (correctly tiles 2×2, unlike the Clamp-default sprites). The screenshot caught and led to fixing `SDLGPU-14`'s Y-flip bug — this is exactly the kind of defect a "did it throw" CTest alone cannot catch, and the reason this milestone is gated on a real visual check, not source inspection. Window-resize survival specifically was not re-tested this session (already covered by `SDLGPU-8`'s swapchain-acquisition handling) — no code path differs between this test and the resize-tested `SdlGpu_Smoke` test. |

---

## Phase SDLGPU-6 — Core 3D vertex formats and BasicEffect

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-26 | `colored3d` pipeline + shader + `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` dispatch (stride 16), with real depth-test verification. | ✅ | Done and runtime-verified 2026-07-15 (`SdlGpu_3D` CTest + screenshot). **Confirmed empirically that, unlike `sprite2d.vert.glsl`, this shader needs NO Y-flip**: `gl_Position = pc.mvp * vec4(inPos,1)` with no negation renders a red/green/blue triangle in the exact expected orientation — SDL_gpu's Vulkan-driver Y-flip only affected the hand-computed pixel-space NDC math in the sprite shader, not a real XNA projection matrix (which already encodes the correct convention). Real depth test verified via a genuine occlusion proof (see SDLGPU-30's note), not just "didn't throw". |
| SDLGPU-27 | `textured3d` (stride 20) — real `Texture2D` sampling, `DiffuseColor` genuinely multiplying it. | ✅ | Done and runtime-verified 2026-07-15 — screenshot confirms the exact quadrant-texture colors/orientation. Fragment shader receives `pc` via its own `SDL_PushGPUFragmentUniformData` push (set 3, binding 0) — a full second push of the same 128 bytes already pushed to the vertex stage (set 1, binding 0), simpler than threading a `textureEnabled` varying through, at the cost of one extra push call per draw. |
| SDLGPU-28 | `colored_textured3d` (stride 24) — vertex-color mixing combined with texture sampling. | ✅ | Done and runtime-verified 2026-07-15 — shares `textured3d`'s fragment shader (`Shaders::kTextured3dFragSpv`) unchanged, only the vertex shader/vertex-input-state differ. The screenshot's green-tinted quad looked unexpectedly dark at first glance — turned out to be **correct**: XNA's `Color::Green` is `(0,128,0)`, not lime `(0,255,0)` (the same real behavioral fact this project's Software backend caught independently, 2026-07-13), so a green-tinted quadrant texture multiplies red/blue channels to black — exactly what rendered. |
| SDLGPU-29 | `lit_textured3d` (stride 32, `VertexPositionNormalTexture`) — FNA's `Lighting.fxh` `ComputeLights()` (3 directional lights, ambient, Blinn-Phong specular, emissive). | ✅ | Done and runtime-verified 2026-07-15 via `BasicEffect.EnableDefaultLighting()`'s real 3-light rig — screenshot shows a visibly different (lit/washed) appearance vs. the plain `textured3d` quad, proving the lighting math genuinely runs. Ported the algorithm from `VulkanGraphicsBackend`'s `lit_textured3d.{vert,frag}.glsl` with the safe-normalize guard from the WebGPU port's own `WEBGPU-22`/`33` finding (a disabled light's zero direction vector). **Did not need WebGPU's CPU-precomputed-normal-matrix workaround** — GLSL has a built-in `inverse()` (unlike WGSL), so the vertex shader computes `transpose(inverse(mat3(world)))` directly, matching `VulkanGraphicsBackend` exactly; the second UBO (`LitLightParams`, vertex+fragment slot 1) is 224 bytes/56 floats here, not WebGPU's 272/68 (no normal-matrix slots needed). |
| SDLGPU-30 | `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` `GpuDrawParams` dispatch by vertex stride, matching the dispatch-by-stride convention every other 3D-capable backend in this codebase already uses. | ✅ | Done and runtime-verified 2026-07-15. Simpler than `WebGPUGraphicsBackend`'s own dispatch: `AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect`-specific `GpuDrawParams` fields are not yet checked (later phases), so dispatch is purely stride+`texture0`-based. **Real depth-test proof**: a nearer (blue) and farther (red) quad, same size/position, drawn in that order with `SetDepthTestEnabled(true)`; the screenshot shows solid, unmixed blue with zero red bleed-through — the farther quad's fragments were genuinely rejected by the depth buffer, not just silently painted-over (which would show red instead, since it drew second). |

---

## Phase SDLGPU-7 — Remaining stock effects

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-31 | `AlphaTestEffect` (per-pixel discard) — strides 20/24/32. | ✅ | Done and runtime-verified 2026-07-15 (`SdlGpu_Effects` CTest + screenshot: `AlphaFunction=Greater`/`ReferenceAlpha=128` on a texture with an opaque top half and a fully-transparent bottom half genuinely discards the bottom half — the `CornflowerBlue` background shows through with a hard edge, not a translucent blend, proving real `discard`, not alpha blending). Strides 20/32 share `alpha_test3d.vert.glsl`/one shader but need distinct pipelines (different vertex-input-state) — the pipeline cache key folds in the stride explicitly for this one shader family, unlike every other family here which is already stride-specific by construction. |
| SDLGPU-32 | `DualTextureEffect` (two-sampler multiply, `tex1.rgb*=2.0; result=tex1*tex2*tint`) — strides 20/24. | ✅ | Done and runtime-verified 2026-07-15 — screenshot shows the exact predicted result of multiplying a red/green/blue/white quadrant texture by a uniform yellow second texture: red/green pass through unchanged, blue becomes black (yellow has no blue channel), white becomes yellow. An unambiguous proof the real two-sampler multiply runs, not just one texture being shown. |
| SDLGPU-33 | `EnvironmentMapEffect` (cubemap reflection) — needs `SDL_GPU_TEXTURETYPE_CUBE` texture creation + `SDL_GPUCubeMapFace`-indexed per-face upload. | ✅ | 2026-07-15. New `env_map3d.vert.glsl`/`env_map3d.frag.glsl` (stride-32 `VertexPositionNormalTexture`, identical vertex layout to `lit_textured3d.glsl`), compiled via `compile_shaders.py`. Mirrors `VulkanGraphicsBackend`'s own `env_map3d.vert/frag.glsl` technique field-for-field (reflect(-E,N) off a world-space normal, Fresnel-weighted or flat blend factor, FNA's real `mix(baseColor, envSample*combinedAlpha, blendFactor) + envMapSpecular*envSample.a*combinedAlpha` lerp semantics) — minus fog, deliberately deferred the same way `lit_textured3d.glsl` already defers it for this backend. A new `SdlGpuTextureCubeBackend::Texture()`/`SdlGpuRenderTargetCubeBackend::CubeTexture()` dual-backend resolve (mirrors `SpriteBatch::Draw`'s own `SdlGpuTextureBackend`-vs-`SdlGpuRenderTargetBackend` resolve) lets `GpuDrawParams::envMap` come from either a plain `TextureCube` (`SDLGPU-51`) or a `RenderTargetCube` (`SDLGPU-36`) — both now real backends. Full `EnvMapDrawCommand`/`CreateEnvMapResources`/`DestroyEnvMapResources`/`GetOrCreatePipelineEnvMap3D`/`QueueEnvMapDraw`/`RenderEnvMapDraws` plumbing added, mirroring `lit_textured3d`'s own family shape exactly; wired into `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`'s dispatch with the same precedence `VulkanGraphicsBackend`/`WebGPUGraphicsBackend` already established (alpha test > dual-texture > env-map > plain lit/textured, since env-map and lit-textured share stride 32); added to all 3 `UploadSceneDrawData`/`RenderEnvMapDraws`(swapchain + 2 render-target passes)/`ReleaseSceneDrawBuffers` call sites. Verified via a new `SdlGpu_EnvMap`, 3/3 checks, directly ported from this project's own existing `vulkan_environmentmapeffect_amount_one_test.cpp` (same values/tolerance, `RenderTarget2D::GetData()` readback instead of `GetBackBufferData()` since this backend's swapchain-download path segfaults — see `SDLGPU-39`'s row): `EnvironmentMapAmount=1` with a solid white cubemap fully replaces the lit/textured color (FNA's real lerp semantics, not an additive contribution); the same with a solid gray cubemap reads back gray, not white or the diffuse texture's own color (proves the cube sample is genuinely being read, not a hardcoded/leftover value); `EnvironmentMapAmount=0` reads back the plain emissive-lit result, nowhere near either cubemap color (proves the blend amount is real, not always-on). `EnvironmentMapEffect::FresnelEnabled` has no public setter in this codebase (defaults permanently `true`, matching real XNA) — all 3 checks exercise the Fresnel-enabled code path by construction, not a separate untested branch. Full `ctest -R "SdlGpu"` re-run: **11/11 passing**, zero regressions. |
| SDLGPU-34 | `SkinnedEffect` (bone-matrix palette). | ✅ | 2026-07-15. New `skinned3d.vert.glsl` (stride-52 `VertexPositionNormalTextureSkinned`: pos/normal/uv + `vec4` blend weight + `SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4` blend indices, read as `uvec4` — ported directly from `VulkanGraphicsBackend`'s own `skinned3d.vert.glsl`, including its established "skin matrix alone transforms the normal, no separate World normal-matrix contribution" simplification). Its fragment stage reuses `litTexturedFragmentShader_` unchanged — byte-identical varying interface and `SkinnedLightParams`/`LitLightParams` UBO layout, so no separate skinned fragment shader exists at all. **Real empirical spike result, found via this task's own byte-exact-position pixel test (binary-searching bone indices 0→71): `SDL_PushGPUVertexUniformData` has a real ~4096-byte cap per slot on this Vulkan-backed environment** — bone indices 0–63 (byte offsets within the first 4096 bytes) read back correctly, but 64–71 (bytes 4096–4608) silently did not update, producing a degenerate skin matrix (all-zero) that discarded the geometry entirely. This cap is **not documented anywhere in `SDL_gpu.h`** — found only by writing the real test the plan's own row anticipated needing. Fixed via the plan's own anticipated fallback: the 72-bone palette (4608 bytes) is uploaded as a real `SDL_GPUBuffer` (`SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ`, uploaded through the same copy-pass mechanism as vertex/index data) and bound via `SDL_BindGPUVertexStorageBuffers`, not pushed as a uniform — a storage buffer has no such per-slot size cap. Per `SDL_CreateGPUShader`'s own binding convention, vertex-stage storage buffers live at `set=0` (same set as sampled/storage textures, ahead of uniform buffers at `set=1`) — the shader declares `layout(std430, set=0, binding=0) readonly buffer BoneBlock { mat4 bones[72]; }`. `PC`/`SkinnedLightParams` still push normally via `SDL_PushGPUVertexUniformData` at vertex slots 0/1 (208 bytes total, nowhere near the 4096-byte cap). Verified: new `SdlGpu_Skinned`, 3/3 checks, Checks A/B directly ported from this project's own existing `vulkan_skinnedeffect_translation_bone_test.cpp`/`vulkan_skinnedeffect_twobone_blend_test.cpp` (same geometry/bone values, `RenderTarget2D::GetData()` readback instead of `GetBackBufferData()` since this backend's swapchain-download path segfaults — see `SDLGPU-39`'s row): a single translation bone (index 0) and a two-bone 50/50 weighted blend (indices 0,1) both shift a quad by the expected amount. Check C is the spike proof itself: a translation bone at index 71 (the LAST slot of the full 72-matrix palette, all others Identity) produces the exact same shift — proving the full 4608-byte palette is transmitted intact and indexable at the far end via the storage-buffer path. This closes Phase `SDLGPU-7` entirely. |

---

## Phase SDLGPU-8 — Render targets

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-35 | `RenderTarget2D` — a `SDL_GPU_TEXTUREUSAGE_COLOR_TARGET \| SDL_GPU_TEXTUREUSAGE_SAMPLER` texture, bound as `SDL_GPUColorTargetInfo.texture` in one pass, sampled in a later pass. | ✅ | 2026-07-15. Required a real architectural refactor, not just a new class: every pipeline cache (`GetOrCreatePipelineColored3D`/`Textured3D`/`ColoredTextured3D`/`LitTextured3D`/`AlphaTest3D`/`DualTexture3D`/sprite) now takes a `colorFormat` param folded into its cache key (previously hardcoded `SDL_GetGPUSwapchainTextureFormat`), and `EnsureFrameRendered()` is now genuinely multi-pass: every distinct `SdlGpuRenderTargetBackend` used this frame gets its own render pass (in first-bind order, via `RenderToTarget()`), all before the swapchain's own pass — this is what makes "bound in one pass, sampled in a later pass" real rather than aspirational. Render targets standardize on `SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM` regardless of the swapchain's native format (XNA's `CreateRenderTarget2D` interface has no format parameter anyway). `Clear()`/`ClearDepth()`/`ClearStencil()` now route to whichever `SdlGpuRenderTargetBackend` is currently bound (each RT owns its own pending-clear state) instead of unconditionally hitting the swapchain's. `SpriteBatch::Draw()` had to learn to resolve either `SdlGpuTextureBackend` (plain `Texture2D`) or `SdlGpuRenderTargetBackend` (`RenderTarget2D`) to a raw `SDL_GPUTexture*`, since the two are unrelated concrete classes. `mipMap` is supported for real (`SDL_GenerateMipmapsForGPUTexture` after that target's pass ends); `multiSampleCount > 0` was a documented throw at the time this row was first closed (`SDLGPU-38` not yet implemented) — real MSAA landed the same session, see `SDLGPU-38`'s own row. Verified via `SdlGpu_RenderTarget2D` (4/4 checks: Clear-only fill, a real colored3d triangle, a depth-tested pair of overlapping quads each with this target's own dedicated depth texture, and a `MultiSampleCount` property-fidelity check) plus a real screenshot (see below) — not just "didn't throw". |
| SDLGPU-36 | `RenderTargetCube` — 6-face color-target texture, including MSAA and mip levels beyond face 0. | ✅ | 2026-07-15. `SdlGpuRenderTargetCubeBackend` owns one `SDL_GPU_TEXTURETYPE_CUBE` texture (6 layers); only one face is ever the active target at a time (shared depth texture across faces, matching D3D11/D3D12's own convention), with per-face clear state. MSAA needed no manual resolve step at all (unlike D3D11/D3D12's `ResolveSubresource`) — `SDL_gpu` has no multisampled cube type, so the real render target is a plain 6-layer `2D_ARRAY` MSAA texture, and `SDL_GPUColorTargetInfo.resolve_texture`/`resolve_layer` resolves it directly into the cube texture's active face automatically at render-pass end. `SDL_GenerateMipmapsForGPUTexture` has no per-layer control (unlike D3D12's manual per-face blit), so it regenerates all 6 faces' chains whenever any face of a mip-enabled cube was used in a frame — harmless for untouched faces (same source data, same result). `multiSampleCount` is clamped via a new `SDL_GPUTextureSupportsSampleCount`-based `ClampSampleCount` helper (mirrors `D3D12RenderTargetCubeBackend::ClampMultiSampleCount`), unlike `RenderTarget2D` which still throws for any nonzero request. Required generalizing every queued draw command's `target` field from a single `RenderTarget2D` pointer to a small `DrawTarget{rt, cube, face}` struct, since a draw can now target the swapchain, a 2D RT, or one cube face. **Verified with real per-face GPU readback** (`SdlGpuRenderTargetCubeBackend::GetData`, pulled forward from `SDLGPU-39` — see that row) rather than `EnvironmentMapEffect` reflection sampling (not implemented in this backend, and Vulkan's own equivalent test documents that technique as unable to discriminate between individual faces anyway): `SdlGpu_RenderTargetCube`, 7/7 checks — all 6 faces filled with distinct colors and read back individually, a real colored3d triangle, real per-face depth-test occlusion, `MultiSampleCount` property fidelity + a genuine MSAA fill/resolve/readback round-trip, and mip level 1 of a uniform-color fill reading back correctly. |
| SDLGPU-37 | Multiple Render Targets (MRT) — `SDL_GPUGraphicsPipelineTargetInfo.color_target_descriptions` array + several `SDL_GPUColorTargetInfo` entries in one render pass. | ✅ | 2026-07-15. `SetRenderTargets(rts, count)` binds `rts[0]` exactly like `SetRenderTarget2D(rts[0])` (the real, single draw target); `rts[1..count-1]` are registered via a new `SdlGpuRenderTargetBackend::MarkUsedThisFrame()` (so each still gets its own real pass this frame) and tracked in a new `currentExtraMrtTargets_` list, but never become `currentRenderTarget_` — **draws remain single-target**, matching the exact same honest scope boundary this project's D3D11/D3D12 MRT support already established (no shader in this codebase declares more than one fragment output). `Clear()`/`ClearDepth()`/`ClearStencil()` propagate to every target in `currentExtraMrtTargets_` too, so a single `Clear()` call while MRT is bound really does clear all of them simultaneously, not just the primary. Verified via `SdlGpu_MRT` (3/3 checks: simultaneous MRT clear, a colored3d draw while MRT is bound, `ClearColorAndDepth`+`SetRenderTargets(nullptr,0)` restore, all with no exception) plus a real screenshot confirming both halves of the scope boundary at once — the primary target turned green (the draw succeeded) while the two secondary targets stayed exactly magenta (the shared `Clear()` reached them, and the draw did not). **Real, non-test-specific finding from writing this test:** a `RenderTarget2D`/`RenderTargetCube` that goes out of scope (destructor runs) before this backend's deferred `Present()`-time render pass actually executes is a real use-after-free — the destructor releases the real `SDL_GPUTexture*` immediately, but any sprite/draw commands already queued against it (not yet rendered) still hold that now-freed handle. Caught via a genuine segfault when this test's first draft used `Draw()`-local `RenderTarget2D` instances instead of members; `SdlGpu_RenderTargetCube`'s own test happened to avoid this by forcing an eager flush inside `GetData()` itself. **Fixed for real 2026-07-15 — see execution-order item 12** (a `shared_ptr`-owned GPU-state redesign, not just a deferred-release patch); `currentExtraMrtTargets_` above still has its own narrower, separate version of this risk (not converted, documented as out of scope in that same item). **Real MRT built 2026-07-15 (adversarial-review finding #2 — this row's own ✅ previously overclaimed "several `SDL_GPUColorTargetInfo` entries in one render pass" when only 1 was ever real):** `rts[0]`'s `SdlGpuRenderTarget2DState` gained `mrtSiblings` (populated by `SetRenderTargets(rts, count>1)`) and every other bound target gained `isMrtSibling=true`; `EnsureFrameRendered`'s per-target loop now skips `isMrtSibling` targets (no longer their own separate pass), and `RenderToTarget()` builds `1+mrtSiblings.size()` real `SDL_GPUColorTargetInfo` entries in ONE `SDL_BeginGPURenderPass` call — the task's own literal wording, now true. Stock (single fragment-output) draw families are unaffected and still only ever write attachment 0 (matching D3D11/D3D12's identical scope boundary — no stock shader in this codebase has more than one output); a genuinely new capability was added instead for the one kind of shader that CAN have more than one output: `SdlGpuEffectBackend::GetOrCreatePipeline()` gained a `colorTargetCount` param, building that many `color_target_descriptions` (all sharing this backend's one `R8G8B8A8_UNORM` `RenderTarget2D` format) and folding it into its own pipeline-cache key. **Real proof, not just "didn't throw"**: new `SdlGpu_MRT` Checks D/E — a single `SpriteBatch` draw with a real runtime-compiled 2-output GLSL fragment shader (`layout(location=0) out vec4`/`layout(location=1) out vec4`, the second a channel-swapped derivative of the first) while 2 fresh targets are bound writes two objectively different, independently-verified `RenderTarget2D::GetData()` values — proving one draw call genuinely reaches two simultaneous attachments. Git-stash-verified: temporarily forcing the render pass back to 1 attachment (and the custom pipeline back to `colorTargetCount=1`) reproduced exactly the predicted failure (target A still correct at attachment 0; target B read back untouched black, never written) — restoring the fix returned 5/5. Full `ctest -R "SdlGpu"` + `CnaTests`: 16/16 + 4385/4385, zero regressions. |
| SDLGPU-38 | MSAA — `SDL_GPUSampleCount` texture creation + `SDL_GPUColorTargetInfo.resolve_texture` automatic resolve-on-render-pass-end. | ✅ | 2026-07-15. Closes `RenderTarget2D`'s own MSAA leg (cube MSAA was already done as part of `SDLGPU-36`), using the exact same mechanism: a separate `msaaTexture_` (`SDL_GPU_TEXTURETYPE_2D`, `COLOR_TARGET`-only usage, real `sample_count`) is the actual render target; `SDL_GPUColorTargetInfo.resolve_texture = colorTexture_` + `store_op = SDL_GPU_STOREOP_RESOLVE` resolves it into the sampleable `colorTexture_` automatically at render-pass end — no manual resolve call. `multiSampleCount` is clamped via the `ClampSampleCount`/`SampleCountToInt` helpers `SDLGPU-36` added (`SDL_GPUTextureSupportsSampleCount`-based); MSAA and `mipMap` remain mutually exclusive on the same attachment (same rationale as the cube). The depth texture's `sample_count` is set to match the color attachment's when MSAA is requested. `CreateRenderTarget2D` no longer throws for any `multiSampleCount` value — the `SDLGPU-35`-era throw is gone. Verified via a new dedicated `SdlGpu_RenderTarget2DMSAA` (4/4 checks: `MultiSampleCount` property fidelity — request 4 applies a real device-clamped value >1; a real colored3d quad drawn into the MSAA target and resolved with no exception; a depth-tested MSAA target with its own MSAA depth texture; sustained multi-frame rendering) plus a real screenshot (a full-screen green quad — both the plain MSAA fill and the depth-tested MSAA target's nearer-quad-wins result render as solid green with no visible corruption or resolve artifacts). `sdlgpu_rendertarget2d_test.cpp`'s own former "MSAA throws" check (Check D) was updated in the same commit to a `MultiSampleCount==0` property-fidelity check instead, since the throw it asserted no longer applies. **Real gap found and fixed 2026-07-15 (adversarial-review finding #1):** despite the real MSAA texture creation above, every pipeline-creation function (`GetOrCreatePipelineColored3D`/`Textured3D`/`ColoredTextured3D`/`LitTextured3D`/`AlphaTest3D`/`DualTexture3D`/`EnvMap3D`/`Skinned3D`, the sprite pipeline, and the custom `ShaderEffect` pipeline) hardcoded `pipelineInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1` regardless of the real render pass's attachment sample count, and `PipelineCacheKey()` had no sample-count dimension at all — two draws into the same colorFormat/state but different MSAA levels would have shared one (wrongly-declared) pipeline. Fixed by threading a real `SDL_GPUSampleCount sampleCount` parameter through every `Render*Draws`/`RenderSprites`/`GetOrCreatePipeline*` function (sourced from a new `sampleCount` field on `SdlGpuRenderTarget2DState`/`SdlGpuRenderTargetCubeState`, set at texture-creation time; the swapchain path always passes `SDL_GPU_SAMPLECOUNT_1` since the swapchain is never MSAA), hashing it into `PipelineCacheKey()`, and folding it into `SdlGpuEffectBackend::pipelines_`'s own `int` key (`colorFormat << 4 | sampleCountInt`). **Honest verification note:** unlike this session's other pipeline-state bugs, a git-stash-style revert of just this fix did **not** reproduce any visible pixel-readback difference on this dev environment's Vulkan driver (`SdlGpu_RenderTarget2DMSAA`'s new Checks D/E, added specifically to try to catch this, still read back the correct resolved color with the fix reverted) — matching the adversarial review's own observation that `SDL_gpu`/this driver silently tolerates a pipeline/render-pass sample-count mismatch rather than hard-crashing or visibly corrupting output. This fix's correctness rests on matching `SDL_gpu`'s own documented pipeline/render-pass contract (verified by code inspection and zero regressions across the full 16/16 `SdlGpu` suite + 4385/4385 `CnaTests`), not on a reproduced visual failure — flagged here rather than silently claimed as empirically proven, since it wasn't. |
| SDLGPU-39 | `GetData()` readback — `SDL_DownloadFromGPUTexture` via a transfer buffer + fence wait (`SDL_SubmitGPUCommandBufferAndAcquireFence`/`SDL_WaitForGPUFences`), for `Texture2D`/`TextureCube`/`RenderTarget2D`/`RenderTargetCube`. | 🟨 | **`RenderTargetCube::GetData()` done for real, 2026-07-15** (pulled forward as part of `SDLGPU-36`): `SdlGpuRenderTargetCubeBackend::GetData()` downloads directly from the self-owned cube texture, exercised by all 7 `SdlGpu_RenderTargetCube` checks. **`RenderTarget2D::GetData()` also done for real, 2026-07-15** — this required a genuine cross-backend interface addition: `ITextureBackend` (in `IGraphicsBackend.hpp`, the common interface every backend implements) gained a new `GetData(level, x, y, w, h, data, dataLength) const` virtual with a safe no-op default (mirrors `ITextureCubeBackend::GetData`'s own existing convention), and `Texture2D::GetData()`'s two entry-point overloads (the flat `(Color*, startIndex, elementCount)` form and the `(level, rect, Color*, startIndex, elementCount)` form) now fall back to `backend_->GetData(...)` **only** when the CPU-side `cpuPixels_`/mip shadow is empty (i.e. for a `RenderTarget2D`, whose content comes from GPU rendering, never `SetData()`) — plain, `SetData()`-populated `Texture2D` instances are completely unaffected (still served entirely from the CPU shadow, zero behavior change, confirmed via the full `Texture2DTest`/`TextureCubeTest`/`Texture3DTest` suites, 123/123 passing). `SdlGpuRenderTargetBackend::GetData()` downloads from `colorTexture_` (the always-single-sample, already-resolved-if-MSAA sampleable texture) using the identical transfer-buffer+fence pattern as the cube leg — safe from the swapchain segfault below since it's a self-owned texture, not the swapchain's. Verified: `sdlgpu_rendertarget2d_test.cpp` gained 3 new real pixel-assertion checks (Clear-only fill reads back exact green, a colored3d triangle reads back exact red, a depth-tested target's nearer-quad-wins reads back exact green) alongside its existing no-exception checks, 7/7 passing — genuinely stronger than the screenshot-only verification this row previously depended on. **Root cause of the swapchain-readback blocker found for real 2026-07-16 — this is a documented, permanent SDL_gpu API contract, not a driver bug or an environment limitation.** `SDL_gpu.h`'s own doc comment on `SDL_WaitAndAcquireGPUSwapchainTexture` (the exact function this backend already calls) states explicitly: *"The swapchain texture is write-only and cannot be used as a sampler or for another reading operation."* `SDL_DownloadFromGPUTexture`, `SDL_CopyGPUTextureToTexture` (source), and `SDL_BlitGPUTexture` (source) are all "reading operations" — so **none of them can ever read the swapchain texture, on any driver, by design**; the earlier segfaults were this contract being violated, not a fixable bug, and trying `SDL_BlitGPUTexture` as an alternative would hit the identical wall (confirmed via documentation before attempting, since the doc text is unambiguous and already matches the observed failure pattern exactly). Plain (non-render-target) `Texture2D::GetData()` needs no fix at all -- it already reads an always-accurate CPU shadow, so a GPU path there would be pure redundant scope. **`TextureCube::GetData()` (plain, non-render-target) also done for real, 2026-07-15** (as part of `SDLGPU-51`): `SdlGpuTextureCubeBackend::GetData()` downloads from the self-owned cube texture using the identical transfer-buffer+fence pattern as the `RenderTargetCube`/`RenderTarget2D`/`Texture3D` legs, indexed via `region.layer` for the face — exercised by all 5 `SdlGpu_TextureCube` checks. **The only real fix for `ReadBackbuffer`/`GetBackBufferData`**: stop rendering the swapchain pass directly into the acquired swapchain texture at all — render every frame into a self-owned proxy texture (`SAMPLER | COLOR_TARGET`, swapchain-sized, exactly like `RenderTarget2D` already is) instead, then one `SDL_BlitGPUTexture` (proxy → the real swapchain texture) right before submit for presentation; `ReadBackbuffer` would then read the proxy via the same transfer-buffer+fence mechanism already proven for `RenderTarget2D`/`RenderTargetCube`/`TextureCube`. **Deliberately not implemented 2026-07-16** — this is a genuine, permanent per-frame cost (one extra texture + one extra blit *every* frame regardless of whether the game ever calls `ReadBackbuffer`, since the proxy must be in place before the frame's draws happen, not allocated lazily on first read request), and the user chose to document this rather than pay that cost. Do not re-attempt `SDL_DownloadFromGPUTexture`/`SDL_CopyGPUTextureToTexture`/`SDL_BlitGPUTexture` directly against the swapchain texture — re-read this note first; the proxy-texture redesign above is the only viable path if this is revisited. |
| SDLGPU-53 | Depth-stencil render-pass compatibility warning found during `SDLGPU-6`'s `debug_mode` investigation (2026-07-16). | ⬜ | With `debug_mode` genuinely on for the first time, `SdlGpu_RenderTargetCube`'s depth-tested check (`SDLGPU-36`) triggers a real Vulkan validation warning: `VUID-vkCmdDraw-renderPass-02684` — "pSubpasses[0].pDepthStencilAttachment->attachment is incompatible between [render pass A] and [render pass B]... the first is `VK_ATTACHMENT_UNUSED` while the second is 1", i.e. a pipeline created assuming a depth attachment is bound in a render pass that (at that specific point) has none, or vice versa. Does **not** affect that check's own correctness readback — `SdlGpu_RenderTargetCube` still passes 7/7 with this warning present. Root cause not yet investigated; likely somewhere in how per-face pipeline creation/caching accounts (or doesn't) for whether a depth attachment is bound for that specific face's pass. Deliberately not investigated further in the same session that found it — reported for its own separate scoping/triage, per this plan's own established practice of not silently expanding an already-agreed task's scope. |

---

## Phase SDLGPU-9 — Texture3D and remaining texture surface

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-40 | `Texture3D` — `SDL_GPU_TEXTURETYPE_3D`, sub-volume upload/readback. | ✅ | 2026-07-15. `SdlGpuTexture3DBackend : ITexture3DBackend` — a single `SDL_GPU_TEXTURETYPE_3D` texture, `SAMPLER` usage only (never a render target, so no `EnsureFrameRendered()` flush needed before `GetData()`, unlike the render-target `GetData()` overrides — a plain texture's content only ever changes via its own immediate, synchronous `SetData()` uploads). `SetData`/`GetData` both carry the mip `level` straight through to `SDL_GPUTextureRegion.mip_level`/`region.z`/`region.d`, matching `Texture3D.cpp`'s own already-correct, unconditional `backend_->SetData/GetData(...)` dispatch (no XNA-layer change needed at all — `Texture3D::GetData()` already called through to the backend the same way `TextureCube::GetData()` does, confirming this codebase's established "plain non-render-target textures always ask the backend directly" convention). Wired into `CreateTexture3D()`, no longer the inherited `nullptr` default. **Real bug found and fixed via this task's own byte-exact round-trip test**: `SDL_UploadToGPUTexture`'s `cycle=true` (copied from `SdlGpuTextureBackend::UpdatePixels`'s own single-full-replace pattern) silently cycles the texture to a **fresh, separate underlying GPU resource** each time it's called on an already-"bound" texture — fine for a single full-texture replace, but `Texture3D` content is built up via multiple independent sub-volume/per-level `SetData()` calls that must all land on the SAME resource; with `cycle=true`, an earlier partial write (e.g. a sub-volume at Z=0) was silently orphaned onto an abandoned resource the moment a second write (Z=1) followed it, reading back as zero/uninitialized instead of its real content. Fixed by passing `cycle=false` for `Texture3D` uploads specifically (`SdlGpuTextureBackend::UpdatePixels`'s own single-full-replace `cycle=true` is unaffected and correct as-is). **Real proof** (matches the byte-exact bar `D3D12Texture3DBackend`/`DX-122` already met): `SdlGpu_Texture3D`, 6/6 checks — a deliberately off-center 2×2×2 sub-volume within a 4×4×2 texture, with a *different* solid color per Z slice, round-trips byte-exact (this is the exact check that caught the `cycle` bug above); a full-volume round-trip; and a check that level 0 remains intact after a later, separate level-1 `SetData()` call (genuinely proves cumulative writes, not just "the last write is readable"). |
| SDLGPU-41 | Mipmaps — allocate the FNA-compatible authored chain and address each level explicitly through `SDL_GPUTextureRegion.mip_level`. | ✅ | Corrected by `REMED-GFX-099` on 2026-07-25. The original 2026-07-15 implementation correctly supported authored per-level data, but also invented a plain-Texture3D automatic-generation behavior: full level-0 `SetData` called `SDL_GenerateMipmapsForGPUTexture` and widened the texture to `SAMPLER | COLOR_TARGET`. FNA and CNA's Vulkan/EasyGL/WebGPU/Bgfx backends allocate authored levels and never make that implicit call. The extension exposed invalid SDL Vulkan 3D views/blits and its uniform test could not prove depth handling. Automatic generation is removed; Texture3D is `SAMPLER`-only and every mip is populated explicitly. `SdlGpu_Texture3D` is now 30/30 across level 0/nonzero mips, repeated/order/state cycles, distinct depth planes, partial volume, NPOT, and two resources, with zero validation. |
| SDLGPU-54 | `Texture3D` generated-mipmap depth-dimension correctness/validity gap found during `SDLGPU-6`'s `debug_mode` investigation. | ✅ | **Closed by `REMED-GFX-099` on 2026-07-25.** The exact prefix was 32 errors: 12 image-view messages and 20 blit messages. For an 8×8×2 four-level 3D image, SDL's Vulkan backend created `VK_IMAGE_VIEW_TYPE_2D` target views for every base depth plane at every mip, so plane 1 was out of range at mips 1–3; its generator likewise blitted base plane 1 through every shrunk mip. A create-only native SDL reproducer proved the first bad event occurs during texture creation. The installed SDL 3.5.0 API inputs were valid and a newer local `origin/main` retains both loops, but CNA should never have selected this non-FNA path. Restoring authored-only mips removes `COLOR_TARGET`, all target views, and all generator blits. Final dedicated 31/31 checks and full SDL_GPU 36/36 are globally validation-clean. |
| SDLGPU-51 | Plain, non-render-target `TextureCube` — `CreateTextureCube()` currently returns `IGraphicsBackend`'s default `nullptr`; needs a real `SdlGpuTextureCubeBackend : ITextureCubeBackend` (upload via `SDL_GPU_TEXTURETYPE_CUBE` + `SDL_GPUCubeMapFace`-indexed per-face `SetData`/`GetData`), analogous to `SdlGpuTextureBackend` for plain `Texture2D`. | ✅ | 2026-07-15. `SdlGpuTextureCubeBackend : ITextureCubeBackend` — a single `SDL_GPU_TEXTURETYPE_CUBE` texture (6 layers), `SAMPLER` usage only (never a render target). Each face maps to one array layer; `SetData`/`GetData` carry `face` straight through to `SDL_GPUTextureRegion.layer` (the same convention `SdlGpuRenderTargetCubeBackend::GetData` already established) and `level` through to `mip_level`, matching `Texture3D`'s pattern. `TextureCube.cpp`'s XNA layer was already correctly wired to call `backend_->SetData/GetData(...)` unconditionally, so no XNA-layer change was needed, only the backend class itself. Wired into `CreateTextureCube()`, no longer the inherited `nullptr`. **Uploads use `cycle=false` from the start** — the exact bug `SDLGPU-40` found and fixed for `Texture3D` (`SDL_UploadToGPUTexture`'s `cycle=true` silently orphaning earlier writes onto an abandoned GPU resource) applies equally here, since a real cube map is built up via multiple independent per-face `SetData` calls that must all land on the same resource; this was checked for and confirmed absent via a deliberate regression check (all 6 faces written with distinct colors, then face 0 re-read *last*, after the other 5 writes). The original implementation regenerated the whole 6-face mip chain after one face's level-0 upload because SDL exposes no per-layer generator; **SDLGPU-70 supersedes that historical choice** after the EasyGL oracle proved it overwrote mip levels already authored on other faces. Plain TextureCube mips are now explicitly authored, matching XNA/EasyGL, and the five-check regression was revised to distinguish cross-face preservation. This also fully unblocks `SDLGPU-33` (`EnvironmentMapEffect`) on the texture-source side — see that row. |

---

## Phase SDLGPU-10 — Custom ShaderEffect (`IEffectBackend`)

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-42 | **Decision task.** `SDL_gpu` only accepts precompiled bytecode, so an arbitrary user-authored `ShaderEffect` needs either (a) a runtime GLSL→SPIR-V compile via `libshaderc` for the Vulkan-driver case only, or (b) deferring custom-`ShaderEffect` support on this backend until `SDLGPU-13`'s `SDL_shadercross` alternative (if chosen) is vendored. Document the choice and its exact platform scope — do not silently support it on Linux only while implying full parity with `D3D11`'s runtime `D3DCompile()` path. | ✅ | **Decided 2026-07-15 (user choice): (a), real runtime `libshaderc` compile — Linux/Vulkan-driver scope only, same as this backend's entire current scope (no Windows/macOS driver work has started).** A real architectural finding changed the framing of this decision before it was made: `VulkanGraphicsBackend`'s own `VulkanEffectBackend::CompileProgram` does **not** runtime-compile GLSL text at all — despite `ShaderEffect`'s own documented contract ("Contents of the GLSL vertex shader source, not a file path"), Vulkan's implementation expects the caller to already hand it precompiled SPIR-V bytes (its own doc comment literally says "`vertSpv` and `fragSpv` contain raw SPIR-V bytecode"), and `libshaderc` is used only by this project's own build-time `compile_shaders.py` scripts, never at runtime in any backend's actual C++ code before this task. `EasyGLEffectBackend`, by contrast, genuinely compiles raw GLSL text at runtime since the OpenGL driver does that natively. So the real choice was between mirroring Vulkan's own (narrower, arguably contract-violating) precompiled-bytes scope, or building the first-ever backend in this project whose `ShaderEffect` support matches the class's own documented contract for real. User chose the latter. This environment has no `libshaderc-dev` package (no unversioned `.so` symlink, no headers) — `CMakeLists.txt`'s `SDL_GPU` branch does a `find_library()` first (works if a dev package exists elsewhere/CI), falling back to globbing standard multiarch library directories for the runtime-only versioned `libshaderc.so.1`, linked by its exact path (mirrors how this project's own `.sdl-prebuilt-*` SDL3 libraries are already linked by full versioned path elsewhere in this file). Confirmed via `ldd` on a real built test binary that `libshaderc.so.1` is a genuine, resolved runtime dependency, not merely compiled-against. |
| SDLGPU-43 | Implement `SdlGpuEffectBackend::CompileProgram`/`Bind`/`Unbind`/`IsValid`/`GetCompileError` per the `SDLGPU-42` decision. | ✅ | 2026-07-15. `SdlGpuEffectBackend : IEffectBackend`. `CompileProgram(vertSrc, fragSrc)` hand-declares the small `extern "C"` shaderc-C-API subset this project's own `compile_shaders.py` (ctypes) already proves correct against the identical shared library — no `shaderc.h` exists in this environment, so these prototypes are declared locally rather than pulled from a header (opaque handles all `void*`, matching `ctypes.c_void_p`). Compiles both stages independently to SPIR-V, then `SDL_CreateGPUShader`s each; failure at any step populates `GetCompileError()` and leaves `IsValid()` false — matches `VulkanGraphicsBackend`/`D3D11GraphicsBackend`'s own "report via the backend object, don't throw" convention. `Bind()`/`Unbind()` are real no-ops (documented why): this backend defers ALL actual GPU state changes to `Present()` time, so there is nothing for an immediate `Bind()` call to usefully do — the real "bind" is `RenderSprites()` selecting this object's own compiled pipeline over the stock sprite one, driven by a per-`SpriteCommand` pointer set at `Draw()`-call time (see below), not by a live "current effect" side-channel like `VulkanGraphicsBackend::activeCustomEffect_` uses. Fixed vertex contract (`SpriteVertex`-shaped, 32 bytes) and fixed 128-byte uniform layout mirror `D3D11EffectBackend`'s own byte-for-byte convention exactly (`[0..15]`=vpSize, `[16..79]`=matrix, `[80..95]`=color, `[96..99]`=float/int slot 0); per `SDL_CreateGPUShader`'s own binding-convention doc comment, the custom GLSL must declare its uniform block at vertex `set=1`/`binding=0` and fragment `set=3`/`binding=0`, and its one sampler at fragment `set=2`/`binding=0` — documented in the class's own header comment, not silently left implicit. `ISpriteBatchBackend::SetCustomEffect` no longer throws for a non-null effect (previously an honest "not implemented yet" stub) — stores the `Effect*`; `SdlGpuSpriteBatchBackend::Draw()` resolves `effect->GetEffectBackendPtr()` (the same clean accessor `D3D11SpriteBatchBackend` already uses, simpler than Vulkan's own side-channel) via `dynamic_cast<SdlGpuEffectBackend*>` and **snapshots its current 128-byte uniform state into the `SpriteCommand` right there at Draw()-call time** — not at `Present()` time, since a game may reasonably change uniforms between individual `Draw()` calls within one `Begin`/`End` cycle using the same effect object, and this backend's rendering is deferred until `Present()`, potentially long after those later `SetUniform*` calls would otherwise retroactively (and wrongly) apply to an already-queued sprite. `RenderSprites()` now checks each queued sprite's own `customEffect` pointer and binds that object's own cached pipeline (compiled per-`colorFormat` on first use, mirroring every other per-format pipeline cache in this backend) instead of the stock sprite pipeline, re-stamping only the render-time-known `vpSize` into the snapshot before pushing. Verified via a new `SdlGpu_ShaderEffect`, 3/3 checks, through the real public API (`ShaderEffect` → `SpriteBatch.Begin(effect)` → `Draw()` → `End()`, `RenderTarget2D::GetData()` readback since this backend's swapchain-download path segfaults, `SDLGPU-39`): `IsEffectValid()` true after a real runtime compile; a white 1×1 texture drawn through the custom shader with `SetUniformVec4` reads back the *exact* tint color (byte-exact, no blend artifacts — proves the compiled GLSL's own uniform is genuinely read, not the stock vertex-color path); the identical draw with `SetCustomEffect(nullptr)` reads back plain white, not the tint (the real discriminator that Check B's result came from genuine custom-shader binding, not a no-op that always "wins" regardless). Full `ctest -R "SdlGpu"` re-run: **13/13 passing**, zero regressions; the default `EASYGL`-backend debug build was also rebuilt to confirm zero cross-backend impact (all changes were SDL_GPU-backend-local). This closes Phase `SDLGPU-10` and every row currently in `plan_sdlgpu.md`. |

---

## Phase SDLGPU-11 — Known permanent limitation: occlusion queries

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-44 | Confirm and document that `SDL_gpu` has no occlusion-query API in the vendored SDL3 (`third_party/SDL/include/SDL3/SDL_gpu.h`, checked 2026-07-15 — no query-pool type exists). `CreateOcclusionQuery()` returns `nullptr`, matching `IGraphicsBackend`'s own documented default and the posture `Headless`/`Software` already take. | ⬜ | Not a gap to close by writing code — only re-check this row if a future SDL3 upgrade adds real query support. |

---

## Phase SDLGPU-12 — Testing and CI

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-45 | `SdlGpu_Smoke` CTest target (mirroring `D3D11_Smoke`/`D3D12_Smoke`/`WebGPU_Native2D_Smoke`), reporting SKIPPED with a clear reason when no `DISPLAY`/`WAYLAND_DISPLAY` is present. | ✅ | Fixed 2026-07-15 (adversarial-review finding). All 16 `sdlgpu_*.cpp` test/example files ran their real test logic unconditionally — none of them implemented this project's own established `SKIP_RETURN_CODE 77` convention (Task 470), so `ctest -R SdlGpu` reported hard FAILED, not SKIPPED, on any machine without a real X11/Wayland display, unlike every other backend's tests. Fixed by adding a shared `CNA::Examples::ProbeGpuDisplayAvailable()` free function to `examples/common/PixelTestGame.hpp` (same `SDL_InitSubSystem(SDL_INIT_VIDEO)`/`SDL_QuitSubSystem` probe `RunPixelTest<TGame>()` already uses internally, exposed standalone for these hand-rolled multi-frame `Game` subclasses that don't fit `PixelTestGame`'s single-shot shape), and calling it at the top of `main()` in all 17 `sdlgpu_*.cpp` files, returning `kSkipExitCode` (77) before constructing the real `Game` if no display is available. Verified both directions: `ctest -R "SdlGpu"` still 16/16 PASS with a real display; running a test binary directly with `DISPLAY`/`WAYLAND_DISPLAY` unset and an invalid `SDL_VIDEODRIVER` prints `[SKIP] ...` and exits 77 as expected. |
| SDLGPU-46 | One CTest binary per capability, matching this codebase's existing per-backend test structure: `SdlGpu_Colored3D`, `SdlGpu_Textured3D`, `SdlGpu_ColoredTextured3D`, `SdlGpu_LitTextured3D`, `SdlGpu_AlphaTest3D`, `SdlGpu_DualTexture3D`, `SdlGpu_EnvMap`, `SdlGpu_Skinned`, `SdlGpu_RenderTarget2D`, `SdlGpu_RenderTargetCube`, `SdlGpu_MRT`, `SdlGpu_MSAA`, `SdlGpu_Texture3D`. | ⬜ | Every public method/operator/constant this backend newly exposes still needs its own test per `CLAUDE.md`'s testing rules — this row is the backend-specific CTest scaffolding, not a substitute for per-class unit tests. |
| SDLGPU-47 | Cross-backend diagnostic — compare SDL GPU output against an existing verified backend (Vulkan or Software) pixel-for-pixel for at least one shared scene. | ⬜ | `CNA_GRAPHICS_BACKEND` is a compile-time choice, so this needs two separate builds compared offline, exactly as documented in `CMakeLists.txt` (~L6799) for the existing Software-backend cross-backend diagnostic (Phase S9). |

---

## Phase SDLGPU-13 — Platform expansion (deferred until the Linux/Vulkan-driver path is fully verified)

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-48 | Windows validation via `SDL_gpu`'s D3D12 driver. | ⬜ | Code paths only until run on real (or Wine/DXVK-class) Windows infrastructure, same caveat structure already used for `D3D11`/`D3D12`/WebGPU. |
| SDLGPU-49 | macOS/iOS validation via `SDL_gpu`'s Metal driver. | ⬜ | `needs_human` — no Apple hardware in this dev environment, same class of gate as `DX-90`/`DX-91`/`DX-114`. |
| SDLGPU-50 | Android validation via `SDL_gpu`'s Vulkan driver. | ⬜ | The Android prebuilt SDL3 package already exists (`.sdl-prebuilt-Android-aarch64/`) and already compiles `SDL_gpu.c` — check whether its Vulkan GPU driver was also compiled in before assuming this is free. |

---

## Closing notes

- Follow `CHECKLIST.md`'s per-file checklist for every new `.hpp`/`.cpp` pair this plan produces
  (SPDX header, Doxygen on every public member, `GetTypeName()` override, etc.) — this plan file
  only tracks *what* to build, not the per-file compliance checklist, which is authoritative on
  its own.
- One task = one commit, per `CLAUDE.md`'s git-commit rules — do not bundle multiple `SDLGPU-`
  rows into a single commit, and update this file's status column in the same commit that closes
  a task.
- Update the phase-level summary at the top of this file once the Phase `SDLGPU-5` 2D vertical
  slice is verified, the same way `plan_webgpu.md`'s header was rewritten once its own 2D
  baseline landed — do not let the header go stale relative to the task table below it.
