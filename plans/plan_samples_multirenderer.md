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
| MSR-008 | **OPEN** | Prepare and cross-build the Windows renderer set; record Wine evidence separately from native qualification. |
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
