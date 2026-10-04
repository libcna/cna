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
  active renderer verified for OPENGLES3, OPENGL33, Vulkan, WebGPU, SDL_GPU and FNA3D.  Manual
  visual confirmation remains required and is not implied by that automated result.

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
remaining failures reproduce independently: nested struct/array parameter storage and a compiled
SpriteBatch sampler that should read `GraphicsDevice.Textures[1]`.

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
