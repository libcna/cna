# Native Sample Multi-Renderer Qualification Plan

**Started:** 2026-10-04 on CNA branch `samples` and cna-samples branch `develop`.

**Scope:** qualify the existing cna-samples gallery corpus with one executable per sample and CNA's
existing runtime renderer selection.  Linux targets are `OPENGLES3`, `OPENGL33`, `VULKAN`,
`WEBGPU`, `SDL_GPU` and `FNA3D`; `OPENGLES2` has its own truthful capability subset.  Windows and
macOS work in this Linux phase is preparatory and must not be reported as native qualification.

All compilation in this campaign uses at most 12 parallel jobs.  GPU/window execution uses
`tools/platform/run_gpu_tests_private.sh`, never the owner's live display.

## Starting state

- CNA: `b0e97bb1bb876f9b3edd6f4ff1ef3067908ae8ac`
- cna-samples: `5db32e6a2631f216e85082b1c390321548924aa2`
- cna-samples manifest: 89 completed public-gallery executables plus the two completed native-only
  ports `ClientServerSample` and `NetRumble`, for a 91-sample primary corpus.

## Work items

| ID | Status | Work |
|---|---|---|
| MSR-001 | **DONE** | Route `SurfaceFormat::Color` byte/generic `TextureCube` and `Texture3D` transfers through the canonical RGBA8 renderer path. |
| MSR-002 | **DONE** | Restore the `SDL_GPU` + compiled-XNA-effect build and its focused tests (`SMG-0043`). |
| MSR-003 | **DONE** | Restore SpriteBatch's stock vertex-stage inheritance and device texture-slot semantics for compiled effects, exposed by `BloomSample`. |
| MSR-004 | **DONE** | Repair compiled-effect texture-parameter indexing, exposed by `ShadowMapping` on Vulkan and WebGPU. |
| MSR-005 | **DONE** | Classify `LensFlare` occlusion-query limits truthfully for SDL_GPU and FNA3D's selected internal driver; never fabricate a query result. |
| MSR-006 | **IN PROGRESS** | Complete single-renderer and six-renderer Linux CNA qualification. |
| MSR-007 | **IN PROGRESS** | Qualify the representative sample set, then the complete 91-sample corpus. |
| MSR-008 | **IN PROGRESS** | Prepare and cross-build the Windows renderer set; record Wine evidence separately from native qualification. |
| MSR-009 | **OPEN** | Prepare the macOS renderer set and exact Mac mini M4 qualification commands. |
| MSR-010 | **DONE** | Stop Vulkan's stock-layout guards from rejecting compiled-effect multi-stream and instanced draws before their own shader linker runs. |
| MSR-011 | **DONE** | Restore the FNA3D-only `CnaRendererTests` build after pointer-container and namespace API changes. |
| MSR-012 | **DONE** | Correct stale FNA3D compiled-effect test profile, parameter-shape and exception assumptions. |
| MSR-013 | **DONE** | Align the synthetic struct-default test with the pinned FNA/MojoShader contract and preserve the authentic FNA pixel oracle. |
| MSR-014 | **DONE** | Resolve FNA3D compiled-effect device texture slots at draw time, including SpriteBatch's slot-zero override. |
| MSR-015 | **DONE** | Keep focused renderer tests on libcna's single initialized EasyGL/meta-gl runtime copy. |
| MSR-016 | **DONE** | Let Vulkan stock effects ignore declaration channels the active effect does not consume. |
| MSR-017 | **DONE** | Normalize legacy XNB SkinnedEffect declarations before WebGPU's stride-derived layout guard. |
| MSR-018 | **DONE** | Allow WebGPU signed-normalized textures to receive complete authored mip chains. |
| MSR-019 | **DONE** | Promote FNA3D's implemented signed-normalized Texture2D formats through its public format gate. |
| MSR-020 | **DONE** | Remove the obsolete FNA3D lifetime-test archive rescan that duplicated CNA globals at shutdown. |
| MSR-021 | **DONE** | Latch an avatar's Loading-to-Ready transition until its caller can update animation transforms. |
| MSR-022 | **DONE** | Store compiled SpriteBatch projection matrices in the Effect Framework layout expected by every native backend. |
| MSR-023 | **DONE** | Expand missing WebGPU texture channels to the XNA/D3D9 values in stock SpriteBatch sampling. |
| MSR-024 | **DONE** | Keep Vulkan stock descriptor bindings from overwriting a compiled Effect's own pipeline layout. |
| MSR-025 | **DONE** | Capture WebGPU stock 3D colour-write and multisample masks per deferred draw. |
| MSR-026 | **DONE** | Make reused Vulkan occlusion-query results generation-safe and monotonic. |
| MSR-027 | **DONE** | Dispose copied texture wrappers before their owning renderer, including ContentManager/SpriteFont copies. |
| MSR-028 | **DONE** | Keep compiled XNA effects valid under the GLSL ES 1.00 profile used by OPENGLES2. |
| MSR-029 | **DONE** | Re-run and manually inspect the post-fix representative Linux visual matrix. |
| MSR-030 | **DONE** | Repair target-aware dependency discovery, runtime deployment and sharp-runtime portability exposed by the seven-renderer Windows cross-build. |

## MSR-001 — Color byte-transfer routing

`TextureCubeReader` uploads an uncompressed XNB cube as byte arrays, face by face and mip by mip.
The shared `TextureCube::SetTypedDataBytesEXT` route sent even `SurfaceFormat::Color` through the
new optional declared-format hook.  EasyGL and Vulkan implement that hook, while FNA3D, SDL_GPU and
WebGPU correctly implement their established RGBA8 `SetData` route; the sample therefore failed on
those three with “did not store the complete declared-format cube region.”  `Texture3D` had the
same routing defect, and both readback paths had the symmetric problem.

For `SurfaceFormat::Color`, byte/generic set and get operations now use the canonical renderer
`SetData`/`GetData` route.  Other uncompressed formats continue to require the exact
`SetDataBytesEXT`/`GetDataBytesEXT` contract, so the change does not reinterpret packed, float or
normalized formats.

Regression evidence:

- reproduced before the fix with two `TextureCubeTest` and two `Texture3DTest` generic/byte cases;
- those four cases pass after the fix on SDL_GPU and OPENGLES3;
- new `TextureCubeTest.ColorByteTransfersPopulateEveryFaceAndMipLikeTextureCubeReader` exercises
  all six faces and every mip using the XNB reader's byte-array shape; it passes on SDL_GPU and
  OPENGLES3;
- the same `ReachGraphicsDemo` executable now starts and remains stable on all six Linux target
  renderers, with the active renderer verified in each process.

## MSR-003 — SpriteBatch compiled-effect inheritance

FNA applies its embedded compiled `SpriteEffect` before a custom SpriteBatch effect.  A custom pass
may consequently replace only the pixel shader and inherit the stock vertex shader plus its
`MatrixTransform`; Bloom's extraction and combine effects use exactly that normal Effect Framework
pattern.  Vulkan, WebGPU and SDL_GPU previously tried to link the custom pass as a complete shader
pair, so the pixel-only pass failed with “no shader pair bound”.

Those three renderers now embed CNA's authentic `SpriteEffect.fxb` whenever their compiled-effect
support is enabled, apply its projection before the custom passes, and feed a three-component
sprite position including `layerDepth` to the inherited shader.  The same repair carries the full
`GpuDrawParams` snapshot into compiled SpriteBatch draws: an effect sampler without its own texture
assignment reads the matching `GraphicsDevice.Textures[n]` slot, while SpriteBatch still overrides
slot 0 with the sprite after `EffectPass::Apply()` as XNA requires.  This fixed Bloom's second
failure, where its combine pass binds the scene render target directly to slot 1.

Regression evidence:

- new shared contracts render and read back pixels for a pixel-only SpriteBatch effect and for a
  sampler reading `GraphicsDevice.Textures[1]`; the contracts are registered for every compiled-
  effect backend;
- the pixel-only, device-slot and existing slot-0 override contracts pass 3/3 on Vulkan, WebGPU and
  SDL_GPU on the Radeon 780M through the private GPU runner;
- the wider WebGPU compiled-effect run passes 49 tests with one intentional Reach-profile skip;
  SDL_GPU passes 45 with one configuration skip;
- the wider Vulkan run initially exposed separate multi-stream and instanced compiled-draw
  failures; MSR-010 repaired them, bringing the run to 26 passes and one Reach-profile skip;
- the same single `BloomSample_cna_samples` executable reaches an automated pass with the requested
  active renderer verified for OPENGLES3, OPENGL33, Vulkan, WebGPU, SDL_GPU and FNA3D.  That result
  was startup/stability evidence only; MSR-022 later added the missing geometry-sensitive contract
  and completed focused visual comparison for the affected backends.

## MSR-004 — Native compiled-effect parameter indices

Vulkan and WebGPU sized their texture-value arrays from the public XNA parameter description.
That description intentionally omits sampler and shader-object parameters, while every
`runtimeIndex` remains an index into MojoShader's complete native parameter table.  Authentic
effects such as ShadowMapping's place a private sampler before a public texture, so the valid
texture index exceeded the compact public count and was rejected during `EffectPass::Apply()`.

Both backends now size native-indexed texture storage from `effectData_->param_count`, matching
the already-correct EasyGL and SDL_GPU implementations.  The shared compiled-effect fixture can
now place a sampler before its texture, and the common texture-binding contract exercises that
shape through the public `EffectParameter::SetValue(Texture2D*)` path.

Regression evidence:

- the new shared contract failed before the fix on Vulkan and WebGPU with each backend's
  “texture parameter index is out of range” exception;
- the focused shared contract passes after the fix on Vulkan, WebGPU and SDL_GPU;
- the same six-renderer `ShadowMapping` executable now remains stable with both Vulkan and WebGPU,
  and each process logs the explicitly requested active renderer;
- the OPENGLES3 launch of that same executable still passes after the framework change.

## MSR-005 — FNA3D occlusion-query capability

FNA3D is both a CNA renderer and a dispatcher for its own internal graphics drivers.  CNA formerly
reported `OcclusionQuery` unconditionally for the FNA3D renderer, even when FNA3D selected its
SDL_GPU driver.  That upstream driver explicitly has no query-pool abstraction and returns null
from `FNA3D_CreateQuery`, so `LensFlare` passed CNA's capability gate and failed only while creating
the supposedly supported resource.

The FNA3D renderer now discovers the selected internal driver and records whether a real query can
be created.  Its known SDL_GPU driver is classified unsupported without invoking its deliberately
unsupported entry point; current non-SDL_GPU drivers are tested with a narrow public FNA3D query
allocation probe.  `SupportsCapability(OcclusionQuery)` and `CreateOcclusionQuery()` share that
result.  No query result is fabricated, and direct CNA SDL_GPU remains truthfully unsupported.

Regression evidence:

- the clean single-renderer FNA3D capability test passes with the default SDL_GPU driver, reporting
  `OcclusionQuery=false` and confirming that resource creation returns null;
- the same test passes with `FNA3D_FORCE_DRIVER=OpenGL`, reporting a real supported query path;
- the same `LensFlare_cna_samples` executable reaches automated passes on OPENGLES3, OPENGL33,
  Vulkan and WebGPU;
- direct SDL_GPU and default FNA3D/SDL_GPU fail at CNA's explicit capability gate and are recorded
  as an upstream API limitation, while the supplementary FNA3D/OpenGL run reaches an automated
  pass.  These automated results still require separate manual visual confirmation.

## MSR-010 — Vulkan compiled-effect stream validation

Vulkan's ordinary indexed, non-indexed and instanced paths selected a stock shader family and
validated the combined declaration against that stock shader before invoking the compiled-effect
linker.  A valid Effect consuming `POSITION0` from one stream and `TEXCOORD0` from another was
therefore rejected according to an unrelated stock input table.  Single-stream effects happened
to avoid the unconditional multi-stream refusal.

The three stock-layout guards now apply only to stock draws.  Compiled draws continue into their
existing pass linker, which validates the actual selected shader against the per-vertex and
per-instance declarations; no capability flag or input semantics changed.

Regression evidence:

- `VulkanCompiledEffectDrawTest.SharedMultiStreamDrawContract` and
  `SharedInstancingDrawContract` both failed before the repair and pass afterward with pixel
  readback on the Radeon 780M;
- the complete non-interactive Vulkan compiled-effect selection passes 26 tests with one
  intentional Reach-profile `Texture3D` skip;
- the authentic `InstancedModel_cna_samples` executable reaches an automated pass with Vulkan,
  and the same executable still passes with OPENGLES3.  Manual visual confirmation remains
  separate.

## MSR-011 — FNA3D renderer-test build

The clean FNA3D-only configuration exposed two stale test-source assumptions that incremental and
other renderer builds had not compiled: an effect pass pointer was bound directly to a reference,
and one compiled-effect file used graphics types without importing their namespaces.  The tests
now dereference the pointer-container result and include/import the XNA graphics types explicitly.

The complete `CnaRendererTests` target builds successfully in `cmake-build-qual-fna3d` with 12
parallel jobs.  Its first full FNA3D execution passes 89 tests and skips one Reach-incompatible
volume-texture contract; the 11 runtime failures it exposed are tracked as subsequent renderer or
test-profile qualification work rather than being hidden by this compile repair.

## MSR-012 — FNA3D compiled-effect test contracts

Nine of the first clean suite's failures were stale test assumptions rather than renderer defects.
Tests reading the back buffer now construct an explicit HiDef device instead of the extension
constructor's intentional Reach default.  Clone tests use the `float4` shape that the authentic
`BasicEffect.fxb` reflection exposes for `DiffuseColor`, and the selected-effect disposal test
catches CNA's XNA-compatible `InvalidOperationException` rather than an unrelated standard-library
exception type.

All nine focused tests pass on the default FNA3D/SDL_GPU driver after these corrections.  The two
remaining failures reproduced independently: the synthetic struct-default expectation was
resolved by MSR-013, and MSR-014 repaired the compiled SpriteBatch sampler that should read
`GraphicsDevice.Textures[1]`.

## MSR-013 — FNA/MojoShader struct-default contract

The clean FNA3D suite exposed a synthetic fixture that expected every source struct initializer to
survive the pinned MojoShader parser.  A tentative parser change made that test pass, but also
changed the authentic `CnaConformanceEffect.fxb` pixel from the established FNA-compatible value.
The independent managed-FNA reflection and pixel oracles prove this is not an implementation gap
that CNA may silently reinterpret: FNA 26.5 reads the same compiled bytes with the pinned parser's
zero-element non-array behavior, and the project already documents CNA's one deliberate public
member-offset divergence in `plans/plan_fx.md`.

The parser change was therefore discarded.  The synthetic expectation and its misleading fixture
comment now describe the existing FNA/MojoShader storage contract: `Intensity` is `0.5`, CNA's
padded `Direction` view is `(0.6, 0, 0)`, and its padded `Thresholds` view is `(0, 0)`.  This is a
test repair only; no runtime or authentic effect behavior changed.

Regression evidence:

- the synthetic reflection test, authentic FNA reflection oracle, and authentic FNA pixel oracle
  pass together 3/3 on the clean FNA3D configuration;
- the complete SDL_GPU compiled-effect selection passes 46 tests with one existing configuration
  skip, including its authentic golden-pixel draw;
- the discarded parser experiment was never committed.

## MSR-014 — FNA3D compiled device texture slots

FNA3D applies compiled-effect sampler bindings immediately in `EffectPass::Apply()`.  That native
binding became stale when an application replaced `GraphicsDevice.Textures[n]` between Apply and
Draw, and an effect sampler without its own texture assignment never acquired the public device
slot at all.  The latter is Bloom's normal combine-pass pattern.  SpriteBatch had the same problem
for nonzero slots even though it correctly replaced slot zero with the sprite texture.

FNA3D compiled draws now resolve the current public pixel texture collection immediately before
the native draw, validate that every non-null texture belongs to the same device, and bind it with
the slot's current sampler state.  Compiled SpriteBatch fills the same draw snapshot after each
pass application, resolves every public slot, then deliberately replaces only slot zero with the
sprite texture.  This is renderer-level XNA device-state behavior; no sample-specific branch or
fallback was added.

Regression evidence:

- the existing shared SpriteBatch device-slot contract reproduced the failure as a black pixel
  instead of the expected green one before the repair;
- the shared ordinary sampler contract now additionally assigns red to
  `GraphicsDevice.Textures[1]`, applies the pass, replaces that slot with blue, and requires the
  draw to sample blue;
- both focused FNA3D contracts pass through its default SDL_GPU/Vulkan driver and its supplemental
  OpenGL driver;
- the complete FNA3D renderer selection passes 100 of 101 tests; the only skip is the existing
  Reach-profile volume-texture contract;
- the extended shared sampler contract also passes on OPENGLES3, Vulkan, WebGPU and SDL_GPU.

## MSR-015 — Focused renderer-test runtime identity

`CnaRendererTests` linked the test-support archives before `libcna.so`.  EasyGL tests directly
reference easy-gl/meta-gl helpers, so that order pulled a second static meta-gl copy into the
executable.  ELF executable-symbol preemption then redirected the renderer inside `libcna.so` to
the test executable's uninitialized dispatch table.  Constructing an ordinary `GraphicsDevice`
aborted in `metagl::glGenFramebuffers` before the test could run.

The focused renderer target now follows the existing `CnaTests` VKPAR-0017 rule and links CNA
before test-support archives.  Direct test references resolve to the same initialized runtime copy
used by the renderer, without changing shipping linkage or renderer behavior.

Regression evidence:

- the OPENGLES3 shared sampler contract reproduced the abort twice before the link-order repair;
- its debugger backtrace terminated in the uninitialized executable copy of
  `metagl::glGenFramebuffers` during the renderer's normalized render-target capability probe;
- the same focused executable and test pass afterward on the private Radeon 780M GPU display and
  log `CNA: graphics renderer: OPENGLES3`.

## MSR-016 — Vulkan inactive vertex semantics

The official XNA `baseballbat.xnb` used by `ObjectPlacementOnAvatar` and
`SkinnedModelExtensions` declares `Position0 + Normal0 + TextureCoordinate1` in a 32-byte record.
Its `BasicEffect` has texturing disabled, so the imported UV channel is not consumed by the active
shader.  EasyGL and the shared `GraphicsDevice` contract already distinguish active vertex
semantics with `StockEffectUsesVertexSemantic`; Vulkan's Position+Normal selector instead required
the declaration to contain exactly two elements and rejected both samples before recording a draw.

Vulkan now selects its existing Position+Normal stage when every additional declaration element is
inactive for the applied BasicEffect.  It still refuses a missing active input: a textured effect
with no `TextureCoordinate0` remains covered by `Vulkan_DeclarationRefusalContract`.  The shared
`BasicEffect_PositionNormal` pixel test adds the actual 32-byte declaration and asserts the same lit
red output on Vulkan and OPENGLES3.

Regression evidence:

- `Vulkan_BasicEffect_PositionNormal` passes, including the new
  `TextureCoordinate1`-while-untextured leg;
- `EasyGL_BasicEffect_PositionNormal` passes unchanged;
- `Vulkan_DeclarationRefusalContract` still passes after its deliberately invalid draw was made to
  request the missing texture input actively;
- the same six-renderer `ObjectPlacementOnAvatar_cna_samples` and
  `SkinnedModelExtensions_cna_samples` executables both report active renderer `VULKAN` and
  `AUTOMATED_PASS` in `matrix-results/msr-020-vulkan-unused-semantics`;
- the full `^Vulkan_` run built and executed 372 tests: the changed refusal test was corrected and
  passes on rerun; the three remaining failures are independent standing/configuration findings --
  the already documented `Vulkan_DrawRangeValidation`, a compiled-effects/HiDef-unaware capability
  snapshot, and `Vulkan_AvatarRenderer_Standard`'s `SkinnedEffect` rendering path.

## MSR-017 — WebGPU legacy XNB skinned declarations

`CPUSkinning`, `SkinnedModelExtensions` and `SkinningSample` use an authentic legacy XNA model
vertex declaration ordered as `Position0 + BlendIndices0 + BlendWeight0 + Normal0 +
TextureCoordinate0 + Tangent0`.  Its 68-byte record is a regular `SkinnedEffect` input, but the
same byte count denotes CNA's canonical skinned-PBR record.  WebGPU selected the draw correctly as
skinned, then ran the generic stride-derived declaration guard before its existing semantic
normalizer; the guard consequently compared `BlendIndices0@12` with PBR's `BlendIndices0@64` and
refused the draw.

WebGPU now sends a non-PBR `SkinnedEffect` draw through its declaration-aware validation and
normalization before any fixed-stride guard.  A canonical declared 52/56-byte stream retains the
resident-buffer fast path; every other declared layout is normalized by semantics, including a
reordered stream whose byte count happens to collide with another CNA layout.  Declaration-less
low-level buffers still accept only the canonical 52/56-byte records, and the existing truthful
single-stream boundary remains enforced.

Regression evidence:

- `WebGPU_Parity_skinned_terms` now contains the real reordered 68-byte declaration in addition to
  the existing stride-64 `Vector4` blend-index case; all assertions pass;
- the five focused WebGPU SkinnedEffect/Skinned3D tests pass 5/5 through the private GPU runner;
- the corresponding EasyGL parity fixture passes unchanged;
- the same multi-renderer executables for `CPUSkinning`, `SkinnedModelExtensions` and
  `SkinningSample` pass 3/3 with active `WEBGPU` and 3/3 with active `OPENGLES3`;
- the full WebGPU gallery result therefore advances from 86 to 89 automated technical passes;
  `NormalMappingEffect` remains a separate signed-normalized mip-generation defect and `Yacht`
  remains the renderer-independent gamer-services environment failure.

## MSR-018 — WebGPU authored signed-normalized mip chains

WebGPU stores `NormalizedByte2` and `NormalizedByte4` natively as `rg8snorm` and `rgba8snorm`.
Those core formats are sampleable and filterable but not render attachments.  CNA consequently
excluded `RenderAttachment` usage correctly, then rejected every mipmapped signed-normalized
texture because its optional automatic mip generator is a render pass.  That refusal also blocked
the distinct and fully supported case where XNA content supplies every level itself.

All three `NormalMappingEffect` normal maps are authentic `NormalizedByte4` XNB textures with
complete mip chains: the two 1024x1024 assets carry 11 levels and the 16x16 eye map carries 5.
WebGPU now allocates the requested chain and accepts explicit `UpdatePixelsLevel` uploads while
continuing not to claim an automatic SNORM downsampler.  A level-zero write does not fabricate or
overwrite authored higher levels.

Regression evidence:

- `NormalizedByteFormat.MipmappedNormalizedByte4SamplesAnAuthoredLevel` uploads three distinct
  levels, forces level 2 with `SamplerState.MaxMipLevel`, and verifies the green level through a
  real GPU sample rather than through `Texture2D`'s CPU shadow;
- all five shared NormalizedByte contracts pass 5/5 on WEBGPU and 5/5 on OPENGLES3;
- the same `NormalMappingEffect_cna_samples` executable reports `AUTOMATED_PASS` with active
  `WEBGPU` and active `OPENGLES3` in `matrix-results/msr-018-normalized-byte-mips`;
- WebGPU's full technical gallery result is now 90/91, with only `Yacht`'s renderer-independent
  gamer-services environment failure remaining.

## MSR-019 — FNA3D signed-normalized Texture2D gate

FNA3D's XNA-identical surface-format enum, `FNA3D_CreateTexture2D` path and SDL_GPU/OpenGL
drivers already implement `NormalizedByte2` and `NormalizedByte4` as signed-normalized storage.
Its format-aware transfer code also sizes two- and four-byte texels correctly.  The public
`Texture2D` constructor nevertheless fell through the shared tri-state default because
`Fna3dRenderer` supplied no `ClassifySurfaceFormatEXT` verdict, so authentic compiled-content
normal maps were refused before the renderer could create them.

The FNA3D classifier now promotes only `Color` and the two signed-normalized formats whose
complete public storage, transfer and sampling contracts are proven.  Invalid ordinals are
unsupported; compressed families still respect the running driver's feature query but defer to
the existing public gate; every other unverified format also continues to defer.  This avoids
turning three green sample cells into an untested claim about FNA3D's whole lower-level enum.

Regression evidence:

- `NormalizedByteFormat` passes 5/5 on FNA3D's default SDL_GPU/Vulkan driver and 5/5 on its
  explicit OpenGL driver, including signed shader sampling and an explicitly authored mip level;
- `Fna3d_Capabilities` asserts both promoted formats and an invalid-ordinal refusal, and passes on
  the default SDL_GPU/Vulkan driver;
- the same multi-renderer `DistortionSample`, `NormalMappingEffect` and `SpriteEffects`
  executables pass 3/3 with active `FNA3D` and 3/3 with active `OPENGLES3` in
  `matrix-results/msr-019b-fna3d-normalized-formats`;
- the first complete FNA3D gallery run was 85/91; these three focused repairs leave
  `AvatarAnimationBlending`, the truthful `LensFlare` query limit, and the renderer-independent
  `Yacht` gamer-services failure for the next full rerun.

## MSR-020 — FNA3D lifetime-test link ownership

`Fna3d_Device_Lifetime` retained an ELF archive-rescan workaround from the former static-link
layout.  The current test already links the complete shared `libcna.so`, so rescanning
`cna_input`, `cna_graphics_core` and `cna_renderer_fna3d` also copied their global objects into the
executable.  Both copies registered teardown for the interposed `TouchPanel::gestures_` storage,
and the otherwise successful test aborted during process exit with a double free.

The test now uses the same `CNA` link contract as every other FNA3D executable.  Its symbol table
no longer defines a second `TouchPanel::gestures_`; `Fna3d_Device_Lifetime` passes in isolation and
the complete OpenGL-pinned FNA3D renderer suite passes 13/13 in the private GPU environment.

## MSR-021 — Avatar loading-transition frame latch

`AvatarRenderer::Draw` used to poll its asynchronous load and immediately consume the supplied
bone transforms when that poll changed `Loading` to `Ready`.  A game following the normal XNA
pattern — update its animation only after observing `State == Ready`, then always call `Draw` —
could therefore see Loading during Update and be asked for ready-state transforms later in the
same frame.  `AvatarAnimationBlending` exposed the race on FNA3D because its live blend array is
intentionally zero-initialized until its first ready-state update.

A draw that begins in Loading now completes and publishes the background transition but keeps
that one call on the loading/no-op path.  The following Update observes Ready and initializes the
animation before the following Draw.  The decomposability validation remains unchanged once a
draw begins Ready.

Regression evidence:

- `AvatarRendererTest.ALoadCompletingInsideDrawWaitsUntilTheCallerCanUpdateItsAnimation` uses an
  identical-description cache hit to force completion inside Draw without timing assumptions;
- the focused avatar animation/renderer suite passes 51/51 through the private FNA3D GPU lane;
- the unchanged multi-renderer `AvatarAnimationBlending` executable passes with active `FNA3D`
  and active `OPENGLES3` in `matrix-results/msr-021-avatar-loading-latch`.

## MSR-022 — Compiled SpriteBatch matrix storage

The native Vulkan, WebGPU and SDL_GPU compiled SpriteBatch paths wrote their orthographic
projection directly through `ICompiledEffectRuntime::SetParameterValue`.  That low-level entry
point accepts the Effect Framework's stored matrix layout; unlike public
`EffectParameter::SetValue(Matrix)`, it does not transpose a CNA `Matrix` for the caller.  Passing
the untransposed field order produced malformed clip coordinates and clip W.  Full-screen Bloom
passes could consequently render one diagonal triangle or nothing even though a former one-texel
centre-pixel test passed.  The same latent mistake existed in the DirectX 9, 11 and 12 SpriteBatch
implementations.

All six backends now store `Transpose(projection).ToColumnMajor(...)` (DirectX 9 uses its combined
SpriteBatch transform), exactly matching the public Effect parameter path.  The shared pixel-only
SpriteBatch contract now paints an 800-by-480 four-quadrant source, scales it into a 400-by-240
target through the compiled effect and probes all four corners.  This makes both triangles,
interpolation and the inherited `TEXCOORD0` linkage observable; a solid 1-by-1 source sampled at
the centre could not expose malformed geometry.

Regression evidence:

- deliberately restoring the old Vulkan write makes three of the four corner probes fail; the
  corrected write passes them;
- Vulkan's compiled-effect draw suite passes 18 tests with one intentional Reach-profile
  `Texture3D` skip; the corresponding WebGPU, SDL_GPU and FNA3D suites pass 22+1 skip, 28+1
  configuration skip and 17+1 Reach-profile skip respectively through the private GPU runner;
- six relevant OPENGLES3/EasyGL SpriteBatch, orientation, switching and render-target-source
  contracts pass after strengthening the shared test, preserving the reference renderer path;
- the unchanged multi-renderer `BloomSample_cna_samples` executable passes with active Vulkan,
  WebGPU and SDL_GPU in `matrix-results/msr-026-bloom-matrix-fixed`; the captured frames were
  manually compared with the OPENGLES3 reference and the former black/diagonal output is gone;
- the DirectX 9, 11 and 12 renderer libraries, with their respective compiled-effect paths
  enabled, cross-build from Linux.  This also exposed and repaired a pre-existing DirectX 9
  `EffectPass*` member-access compile failure.  This is cross-build evidence only, not native
  Windows GPU qualification.

## MSR-023 — WebGPU sampled-channel expansion

XNA/D3D9 samples channels absent from a stored texture format as one: a `Single` texture yields
`(R, 1, 1, 1)` and a two-channel texture yields `(R, G, 1, 1)`.  WebGPU's native sampled value is
`(R, 0, 0, 1)` or `(R, G, 0, 1)`.  The stock WebGPU SpriteBatch shader used that native value
unchanged, which made ShadowMapping's on-screen `SurfaceFormat::Single` preview red while the
OPENGLES3/OPENGL33 reference was white/cyan.

WebGPU texture and render-target renderer objects now report their logical XNA surface format.
Each queued stock sprite captures it, and the existing 16-byte sampler uniform block carries a
green/blue/alpha mask alongside the LOD bias.  The fragment shader restores absent channels to one
after sampling.  One- and two-channel XNA formats share the same rule as EasyGL, Vulkan, SDL_GPU
and the DirectX SpriteBatch implementations; ordinary four-channel textures keep the identity
mask.  Custom and compiled effects are not special-cased, and no sample source changed.

Regression evidence:

- after enabling WebGPU in the renderer-neutral `SingleChannelExpansionTest`, the two `Single`
  cases failed before the repair with green and blue both zero; `ColorFormatIsLeftAlone` passed;
- all three cases pass after the repair on WebGPU, Vulkan and SDL_GPU through the private GPU
  runner.  FNA3D's selected SDL_GPU driver truthfully skips the two `Single` render-target cases
  because it does not advertise that format, while its four-channel control passes;
- twelve WebGPU shader-validation, SpriteBatch, render-target, viewport, blend, draw-order and
  cross-renderer parity CTests pass after relinking against the repaired renderer;
- the unchanged multi-renderer `ShadowMapping_cna_samples` executable now produces
  `(255,255,255)` at the sampled preview probes on WebGPU, matching OPENGLES3; before the repair
  those same WebGPU probes were `(255,0,0)`.  The full scene and its cast shadow remain visible;
- the same rebuilt executable passes and retains the reference preview pixels with active
  OPENGLES3.  Evidence is in `matrix-results/msr-028-shadowmapping-{webgpu-fixed,gles3-regression}`.

## MSR-024 — Vulkan compiled-effect descriptor ownership

Vulkan chooses a provisional stock shader family from the vertex declaration before a compiled
Effect supplies its own shaders and descriptor layout.  ShadowMapping's 32-byte
Position/Normal/Texture grid consequently populated the stock lit-textured shadow set even though
its compiled Effect owned a four-set pipeline.  Replay first bound the compiled Effect's sets 1–3,
then rebound set 1 through the unrelated two-set stock layout.  Vulkan invalidated the Effect's
pixel-uniform set 3, emitted `VUID-vkCmdDrawIndexed-None-08600`, and rendered the grid black.  The
character used a different declaration stride and therefore did not trigger the leaked stock
family.

The stock shadow binding now runs only when the draw actually owns the stock descriptor layout.
Compiled and custom effects retain their own bindings, with no renderer branch or workaround in
the sample.

Regression evidence:

- the new shared `RunCompiledEffectStockLayoutIsolationContract` deliberately uses the same
  32-byte Position/Normal/Texture declaration with a compiled shader that consumes only position
  and texture coordinates.  Before the repair it reproduced the validation error and returned
  alpha zero; afterward its red-pixel oracle passes;
- the contract is registered for Vulkan, EasyGL, WebGPU, SDL_GPU, FNA3D and DirectX 9/11/12.  It
  passes on the five Linux backends exercised so far; DirectX execution remains part of native
  Windows qualification rather than being inferred from registration or cross-compilation;
- the complete Vulkan compiled-effect selection is 28 passes plus one intentional Reach-profile
  `Texture3D` skip, with no Vulkan validation message;
- OPENGLES3 passes the new contract and all three sampled-channel regression cases; WebGPU,
  SDL_GPU and FNA3D pass the same new shared contract through the private GPU runner;
- the rebuilt, unchanged multi-renderer `ShadowMapping_cna_samples` executable reaches an
  automated pass with active Vulkan.  Manual inspection confirms the checkerboard grid, character,
  cast shadow and white shadow-map preview; the captured frame is byte-identical to the fixed
  WebGPU frame.  Evidence is in `matrix-results/msr-029-shadowmapping-vulkan-fixed`.

## MSR-025 — WebGPU deferred write-mask ownership

WebGPU queues every stock 3D draw and records the native render pass later.  Its draw commands
captured blend factors and functions, but the pipeline builder still read `ColorWriteChannels` and
`MultiSampleMask` from renderer-global state at replay time.  A later state change therefore
retroactively altered earlier draws in the same pass.  `LensFlare` exposed the defect by queuing a
100-by-100 BasicEffect polygon with colour writes disabled for an occlusion query, then starting a
normal SpriteBatch before replay.  SpriteBatch restored an all-channel mask, so the supposedly
invisible query polygon appeared as an opaque square.

The existing per-draw WebGPU blend snapshot now also carries the write and coverage masks.  All
twelve stock 3D pipeline families use those captured values for both pipeline identity and native
pipeline construction.  SpriteBatch, custom effects and compiled effects retain their own existing
state snapshots; no sample source changed.

Regression evidence:

- the renderer-neutral GFX-077 test now mutates the device blend state after a draw is queued but
  before its render-target switch flushes it.  Before the repair both delayed checks failed: a
  `ColorWriteChannels::None` draw and a `MultiSampleMask=0` draw each rendered with the later all-on
  state.  After the repair the WebGPU test passes 11/11;
- the strengthened shared test passes 8/8 on SDL_GPU, preserving the other deferred renderer path;
- seven focused WebGPU BasicEffect, graphics-state, occlusion-query, SpriteBatch-blend, colour-write
  and draw-order CTests pass through the private GPU runner;
- the rebuilt, unchanged multi-renderer `LensFlare_cna_samples` executable passes with active
  WebGPU and OPENGLES3.  Manual inspection confirms that the query square is gone while the glow,
  flare chain and terrain remain intact.  The fixed WebGPU capture differs from the earlier
  OPENGL33 reference by normalized MAE `0.000282384`; evidence is in
  `matrix-results/msr-030-lensflare-webgpu-fixed`.

## MSR-026 — Vulkan occlusion-query generation ownership

Vulkan resets a reused one-slot query pool inside the next submitted command buffer.  Until that
queued reset executes, `vkGetQueryPoolResults` can still expose the preceding generation as
available.  `LensFlare` intermittently observed that old availability in `IsComplete`, then found
the slot unavailable when `PixelCount` polled it again and terminated with “The occlusion query has
not completed”.  A stale count was equally possible even when both calls succeeded.

Each recorded query now owns the frame fence and monotonically increasing submission generation
that contain its reset/begin/end commands.  Polling is refused until that exact submission has
completed, the Vulkan availability word must be set, and the count is cached until the next
`Begin`, so a successful `IsComplete` remains true through the following `PixelCount`.  The
render-target-only synchronous path is recorded as complete directly.  This follows the existing
Vulkan GPU-timer ownership model and does not change the public XNA lifecycle state machine.

Regression evidence:

- the new `Vulkan_OcclusionQuery_ReuseGeneration` test reuses one object for 32 alternating
  full-frame and half-frame exact queries.  Before the repair it failed at cycle 16 with the old
  half-frame count `2048` where `4096` was required; afterward it passes 20 consecutive runs;
- `Vulkan_OcclusionQuery_{Precision,ReuseGeneration,PixelCount,Cycle}` pass 4/4.  The precision
  test now polls through ordinary game frames instead of recursively submitting frames from one
  `Draw` callback, which became observable once completion was no longer falsely reported early;
- the shared profile, lifecycle and pixel-count contracts pass 3/3 on Vulkan, while the unchanged
  OPENGLES3 contracts pass 5/5;
- the complete `^Vulkan_` renderer run passes 370/373.  The only failures are the same three
  independent standing findings already recorded under MSR-016: draw-range forwarding,
  compiled-effects-aware capability snapshot data and AvatarRenderer's SkinnedEffect path;
- the rebuilt, unchanged multi-renderer `LensFlare_cna_samples` executable passes with active
  Vulkan and OPENGLES3.  Manual inspection confirms correct terrain, depth, glow and flare-chain
  rendering with no query polygon; its time-matched Vulkan capture differs from the OPENGL33
  reference by normalized MAE `0.0046027`.  Evidence is in
  `matrix-results/msr-031-lensflare-vulkan-fixed`.

## MSR-027 — Copied-texture device lifetime ownership

`Texture2D` and `TextureCube` are copyable CNA C++ wrappers around shared native texture objects.
Their base `GraphicsResource` copy intentionally does not register a second wrapper, because the
graphics-state classes use that base operation before joining one shared managed identity.  The
texture classes had inherited that unregistered behaviour accidentally.  `ContentManager` caches
assets through `std::any` copies and `SpriteFont` owns its atlas by value, so a copied atlas could
be the last native-texture owner while remaining absent from `GraphicsDevice`'s disposal list.

`RolePlayingGame` exposed both consequences at process shutdown.  WebGPU reached
`Texture::Dispose` from a function-static `SpriteFont` after its raw device pointer was dead and
segfaulted in `TextureCollection::RemoveDisposedTexture`.  SDL_GPU corrupted the heap on the same
path.  A focused SDL_GPU ASan reproduction showed the earlier and more fundamental failure: the
late `SdlGpuSampledTextureState` destructor dereferenced the already-freed `SdlGpuRenderer`, while
Vulkan validation reported its image and views still alive at `vkDestroyDevice`.  A guard around
the later texture-collection cleanup would therefore only have hidden the second symptom.

Texture copy construction now registers every independent wrapper with its live owning device.
Copy assignment retains one registration on the same device, transfers it between different
devices, restores it when assigning into a disposed wrapper, and detaches old sampler bindings
before leaving the old device.  The shared native texture and existing copy-on-write pixel
semantics are unchanged.  Device disposal now resets every wrapper's backend while the renderer
and its required graphics context are still alive; no sample-specific shutdown code was added.

Regression evidence:

- `GraphicsResourceTest.CopiedTexturesAreTrackedAndCanOutliveOwningDevice` covers both copyable
  texture families, asserts all four wrappers are tracked, lets the device die first, then proves
  the surviving wrappers were disposed and are safe to destroy;
- `GraphicsResourceTest.CopyAssignedTexturesTransferDeviceTracking` covers cross-device transfer,
  old sampler detachment, same-device reassignment without duplicate registration and assigning
  into an explicitly disposed wrapper;
- before the repair, the first test produced a deterministic SDL_GPU heap-use-after-free under
  ASan in `SdlGpuSampledTextureState::~SdlGpuSampledTextureState`; afterward both tests pass 2/2
  under the same ASan build with no recurrence of that memory finding or the Vulkan
  object-lifetime errors.  The build's pre-existing UBSan static-initialization warning in
  `Color.hpp` still prints before Google Test starts and is tracked separately rather than being
  attributed to this repair;
- the broader resource and texture-copy selection passes 18/18 on WebGPU, SDL_GPU and the
  OPENGLES3 EasyGL regression baseline;
- the unchanged, single multi-renderer `RolePlayingGame_cna_samples` executable now shuts down
  cleanly with both active `WEBGPU` and active `SDL_GPU` in
  `matrix-results/msr-036-roleplaying-lifetime-fixed`;
- a four-renderer capture rerun passes with active OPENGLES3, OPENGL33, WebGPU and SDL_GPU.  Manual
  inspection finds the complete menu, text, alpha and textures correct, and all four 1280x720 PNGs
  are byte-identical (SHA-256 `c263f1096e10ac6ed5e4e58eaa76293be75bf3654ee71eed3e419c40a0c24d8a`);
  evidence is in `matrix-results/msr-037-roleplaying-visual-parity`.

## MSR-028 — GLSL ES 1.00 compiled-effect centroid declarations

The first complete OPENGLES2 capability probe passed only 14/91 samples.  Seventy-six of its 77
failures stopped while compiling an authentic XNA effect because MojoShader emitted `centroid
varying` into a `#version 100` shader.  GLSL ES 1.00 has no centroid interpolation qualifier, so
this was a common compiled-effect translation defect rather than an intended GLES2 capability
limit.  The failure affected both explicit Shader Model centroid inputs and XNA's implicit
centroid treatment of colour inputs.

CNA's managed MojoShader patches now keep the dedicated vertex/pixel varying name and interface
pair under GLSL ES 1.00 but omit its unsupported `centroid` keyword.  That is semantically safe for
the OPENGLES2 profile because it exposes no multisampling; without multiple samples, centroid and
ordinary interpolation select the same sole sample.  GLSL ES 3 keeps `centroid in`/`centroid out`,
and desktop GLSL keeps `centroid varying`, so the full EasyGL profiles lose no behaviour.

Regression evidence:

- `cna_mojoshader_effect_probe` now includes the authentic `SpriteEffect.fxb`, separately parses
  every contained D3D9 shader through the GLSL ES 1.00 profile and rejects either parser errors or
  any generated `centroid` qualifier.  Its 46 shader-object run passes;
- `EasyGLCompiledEffectTest.EveryCommittedStockEffectParses` passes on the private GPU display with
  active `OPENGLES2`, covering SpriteEffect, BasicEffect, AlphaTestEffect, DualTextureEffect,
  EnvironmentMapEffect and SkinnedEffect;
- the unchanged `AccelerometerSample` and `Graphics3D` executables now pass with active
  `OPENGLES2`; `BloomSample` and `ShadowMapping` advance past effect compilation and are refused at
  the already-truthful GLES2 render-target-sampling boundary;
- the complete OPENGLES2 rerun records 80 automated passes and 11 render failures.  Ten are the
  intended profile boundary: five compiled-effect render-target-sampling users, three textures
  using unsupported `NormalizedByte2`/`NormalizedByte4`, one hardware-instancing user and one
  occlusion-query user.  `Yacht` is the same renderer-independent missing gamer-services failure
  present in every full-gallery run.  Automated evidence is in
  `matrix-results/msr-044-full-opengles2-current`;
- the same rebuilt multi-renderer executables pass 4/4 across OPENGLES3 and OPENGL33 for
  `AccelerometerSample` and `ShatterEffect`, preserving the EasyGL full-profile baseline.  Evidence
  is in `matrix-results/msr-045-easygl-centroid-regression`.

## MSR-029 — Post-fix representative visual matrix

The ten-sample high-discrimination set was captured again after the framework repairs, using one
multi-renderer executable per sample and a private 1280x720 GPU display.  The runner verified the
requested active identity before every capture.  Manual review compared each sample across
OPENGLES3, OPENGL33, Vulkan, WebGPU, SDL_GPU and FNA3D, with OPENGL33 and OPENGLES3 as the EasyGL
references; normalized image MAE was used only as supporting evidence because camera/model and
particle animation make several frames intentionally time-dependent.

Fifty-seven of the 58 captured frames are manual visual passes.  They preserve the expected model
geometry, textures, stock/custom effects, bloom and render-target orientation, normal mapping,
instanced geometry, particle sprites and smoke, shadow map and projected character shadow,
SpriteSheet transparency, text and Reach graphics.  Representative static comparisons are very
close: Graphics3D is byte-identical between OPENGL33 and OPENGLES3 and differs by normalized MAE
`0.00000554` on Vulkan/SDL_GPU; ShatterEffect ranges from `0.0010` to `0.0034`.  Larger differences
in Bloom, InstancedModel and Particles3D correspond to visibly correct but differently advanced
animation.

The one captured mismatch is the already-documented GLES3 occlusion-query precision boundary:
`LensFlare` is stable with active OPENGLES3, but its query can return only the real boolean value
`1`; the authentic XNA sample divides the value by its 100x100 query area and the flare chain
becomes effectively invisible.  OPENGL33, Vulkan and WebGPU render the expected terrain, sun,
glow and flare chain.  This is recorded as an upstream API limitation/visual mismatch, not a pass;
CNA does not fabricate a pixel count.  Direct SDL_GPU and FNA3D's default SDL_GPU driver correctly
refuse query creation before capture.  A supplementary FNA3D run forced its real OpenGL driver and
visually passed LensFlare, differing from the OPENGL33 reference by normalized MAE `0.00408196`.

Primary evidence is in `matrix-results/msr-046-postfix-visual-representative`; the FNA3D/OpenGL
supplement is in `matrix-results/msr-047-fna3d-opengl-lensflare-visual`.  Primary manual totals are
9 PASS + 1 upstream visual mismatch on OPENGLES3, 10 PASS each on OPENGL33/Vulkan/WebGPU, and
9 PASS + 1 upstream missing capability each on SDL_GPU and default FNA3D.

## MSR-030 — Windows cross-build portability and runtime deployment

The clean MinGW-w64 Release configuration compiles one executable per sample with `DIRECTX11` as
the default and `DIRECTX9;DIRECTX11;DIRECTX12;VULKAN;WEBGPU;SDL_GPU;FNA3D` in the runtime-selected
set.  The build enables each selected renderer's applicable compiled-XNA-effect implementation.
The authoritative 89 gallery + 2 native-only corpus is complete; the wider repository build also
produces 15 non-primary support, partial, server, test, retained-training and experimental
executables.  This is Linux cross-build evidence only, not native Windows GPU qualification.

The build exposed four general CNA integration defects.  An explicit wgpu-native package root was
incorrectly re-rooted into the target sysroot; WebGPU discovery now bypasses find-root rewriting
for that already-resolved package.  Cross builds looked for nlohmann JSON through the target
sysroot even though it is a portable header-only dependency; the existing isolated-header route
now covers every cross toolchain.  SDL imported targets created below CNA were not visible to the
parent cna-samples directory, so runtime deployment now exports the resolved DLL paths rather than
depending on directory-scoped target visibility.  Finally, CNA's optional Opus probe could import
the host's `-lopus` into a target link; automatic host pkg-config probes are now disabled whenever
`CMAKE_CROSSCOMPILING` is true.  Windows `Xml.Serialization` is enabled again because the old
sharp-runtime `<poll.h>` blocker no longer exists.

The same build exposed target-portability defects in sharp-runtime: pointer-sized unsigned
Winsock handles were narrowed in `ServiceHost`, Winsock was not initialized, Socket teardown used
the POSIX shutdown constant, and non-trivial inline TLS definitions in `Thread` and `CultureInfo`
produced duplicate MinGW initializer symbols.  Those general fixes are committed in sharp-runtime
as `21c06910` (`fix(MSR-030): repair Windows consumer portability`).

Regression and integration evidence:

- `WebGPU_CrossRootPackageDiscovery`, `CnaHostPkgConfigPolicy`, `CnaCrossJsonHeaders` and
  `CnaSdlRuntimeDeployment` pass 4/4; the last test builds a consumer and verifies all three fake
  SDL DLLs were copied beside it;
- six existing XNA XML serialization contracts pass after making the Windows component
  unconditional;
- sharp-runtime's warning-free build and unchanged Yacht SOAP fixture pass 18,123/18,123 tests in
  41 executables with zero failures/skips, at that repository's stricter two-job ceiling;
- all 106 built `*_cna_samples.exe` outputs have `wgpu_native.dll`, SDL3/SDL3_image/SDL3_mixer and
  the three required MinGW C++ runtime DLLs beside the executable;
- the full 220-step final Windows relink completes successfully.  Native Windows execution
  and visual qualification remain open, and no DirectX renderer is reported as natively passed.

## Representative automated matrix

This is startup/active-renderer/stability evidence, not a manual visual pass.  Before `MSR-001`,
the ten-sample matrix was:

| Renderer | Automated pass | Render fail |
|---|---:|---:|
| OPENGLES3 | 10 | 0 |
| OPENGL33 | 10 | 0 |
| VULKAN | 8 | 2 |
| WEBGPU | 7 | 3 |
| SDL_GPU | 7 | 3 |
| FNA3D | 8 | 2 |

After `MSR-001`, `ReachGraphicsDemo` moves from failure to automated pass on FNA3D, WebGPU and
SDL_GPU.  The full ten-sample matrix will be regenerated after the remaining compiled-effect
repairs so intermediate evidence is not mistaken for final qualification.
