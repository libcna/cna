# AUDIT_CROSS_CUTTING_FINDINGS.md

**Status: SKELETON — populated incrementally during Pass 2 as patterns spanning multiple files emerge, finalized
in Pass 5.**

Each entry references the per-file audit reports that provide evidence rather than restating their detail.
Organize by category as entries accumulate.

## Known pre-existing issue to actively cross-check (from `known_bugs.md`, consulted as secondary context per D-3)

- "Multiple SpriteBatch Begin/End in one frame discards all but the last" — check whether this is still reproducible
  against current `SpriteBatch` source, which backend(s) it affects, and whether it's backend-specific or a shared
  `Microsoft::Xna::Framework::Graphics::SpriteBatch` logic bug. Link the corresponding per-file finding here once
  the `xna-graphics` / `tests-xna-graphics` shards are audited.

## Architecture

- **Silent-default-degradation risk in `IGraphicsBackend`** (see `include/CNA/Internal/Backends/Common/
  IGraphicsBackend.hpp.audit.md` F1): most optional 3D-state/effect-parameter methods default to a silent no-op
  or colored-fallback rather than a negotiable capability, with `SupportsCapability()` defaulting to `true` for
  everything. SdlRenderer's and Dx3's audits both confirm the *good* counter-pattern (every unsupported method
  explicitly overridden to throw); worth checking during Pass 4 whether other backends follow that discipline or
  IGraphicsBackend's riskier default.
- **CONFIRMED LIVE BUG (not just theoretical risk): `IGraphicsBackend::RegisterForWindow`/`windowRegistry()`'s
  register-in-constructor/unregister-in-destructor convention has no protection against a constructor that
  registers early and then throws before completing.** `EasyGLGraphicsBackend`'s own audit (F1) found a concrete,
  reachable instance: `RegisterForWindow` runs before `SDL_GL_CreateContext`, which can throw. The destructor
  (which would unregister) never runs on a failed construction, leaving a dangling pointer that
  `SdlInputBridge`/`Mouse` would dereference on the next input event. **`WebGPUGraphicsBackend` was checked and
  does NOT share this risk** — its constructor wraps every fallible step (`CreateSurface`/`RequestAdapterAndDevice`/
  `ConfigureSurface`) in a `try` block with `RegisterForWindow` called last, and a full `catch (...)` that releases
  every resource before rethrowing — a model example of the correct pattern. **Still need to check `Canvas`/
  `SdlGpu`** (the other two `RegisterForWindow` callers) for the same ordering risk when their shards are audited.
  **Update: all four callers now checked.** `Canvas` (confirmed safe — the only fallible step is a null-check that
  precedes registration, nothing to leak). `SdlGpu` (checked: registration also happens *last*, after 10
  sequential `Create*Resources()` shader/pipeline-creation calls, so it does **not** share EasyGL's
  dangling-registry-entry risk — but see the new, distinct finding immediately below that this same ordering
  creates for `SdlGpu` specifically). **Only `EasyGL` has the dangling-registry-entry bug** — the other three all
  correctly defer registration until construction can no longer fail.
- **NEW, SdlGpu-specific: constructor resource leak on any of 10 sequential fallible resource-creation calls.**
  `SdlGpuGraphicsBackend`'s constructor (`SdlGpuGraphicsBackend.cpp` ~line 487-543) creates the SDL GPU device and
  claims the window (with correct, explicit cleanup on `SDL_ClaimWindowForGPUDevice` failure specifically), then
  calls `SetSwapInterval`/`QueryDepthStencilFormat`/`CreateSpriteResources`/`CreateColoredResources`/
  `CreateTexturedResources`/`CreateLitTexturedResources`/`CreateAlphaTestResources`/`CreateDualTextureResources`/
  `CreateEnvMapResources`/`CreateSkinnedResources`/`CreatePbrResources` in sequence, entirely unwrapped by any
  try/catch. If ANY of these ten calls throws (plausible — they compile SPIR-V shaders and create GPU pipeline
  objects, and the constructor's own comment notes non-Linux platforms' shader-format support is still
  incomplete/deferred, a real reachable failure mode there), the destructor (which does a complete, correct
  teardown of exactly these resources, verified by direct comparison) never runs, since a constructor that throws
  leaves the object never-fully-constructed. Result: the SDL GPU device, the claimed window, and any GPU
  pipelines/shaders successfully created by earlier calls in the sequence all leak. **Contrast with WebGPU's
  constructor (this audit's model example of correct exception safety) which wraps the equivalent sequence in
  exactly the try/catch+cleanup-then-rethrow pattern this file is missing.** Not yet written up as a full per-file
  finding — `backend-sdlgpu`'s own direct audit (queued, 27 files, not yet started) should record this formally.
- **Recurring shape: device/object state is mutated to reflect a requested change *before* the call that can
  reject/throw for that change, leaving stale/inconsistent tracked state on failure.** Three confirmed instances
  now, in unrelated subsystems: (1) `IGraphicsBackend`'s window registry (EasyGL F1, above); (2)
  `SpriteBatch::Begin()` sets `begun_=true` before backend calls that can throw, permanently wedging the object
  if one does (found via `sdlrenderer_custom_effect_throws_test.cpp`'s audit); (3) `GraphicsDevice::SetRenderTargets`
  mutates `currentRenderTargets_`/`renderTargetBound_` to the rejected MRT bindings before the backend call that
  actually throws for MRT-unsupported backends (found via `sdlrenderer_rendertargets_mrt_throws_test.cpp`'s
  audit). **This looks like a genuine, repeated authoring pattern in this codebase** (mutate optimistically, only
  discover the operation was invalid via a later exception) rather than three independent coincidences — worth
  actively watching for in every subsequent state-mutating method audited, not just these three.

## Duplicated backend logic

_(pending — revisit once more backends are audited)_

## Recurring memory/resource risk patterns

_(pending)_

## Recurring performance risk patterns

_(pending)_

## Systematic FNA parity gaps

- **CONFIRMED IN 3+ BACKENDS: the pre-Task-1111 fog formula (proven wrong by this project's own XNA-oracle diff,
  commit `74ad3bae`) was fixed in EasyGL but never ported to Bgfx or Vulkan.** EasyGL's fog formula
  (`vFogFactor=(aPos.z+uFogEnd)/(uFogEnd-uFogStart)`) matches FNA's real `SetFogVector`/`ComputeFogFactor`;
  Bgfx's and Vulkan's shared shaders instead use `(FogEnd-z)/(FogEnd-FogStart)` — the **mirror-image** formula
  this project's own commit history already proved incorrect. Confirmed in **6 separate test-file audits across
  2 backends**: Bgfx (`bgfx_alphatest_fog_test.cpp`, `bgfx_basiceffect_fog_test.cpp`,
  `bgfx_basiceffect_lit_fog_test.cpp` — 3 distinct shaders: `vs_alpha_test3d.sc`, `vs_colored3d.sc`,
  `vs_lit_textured3d.sc`) and Vulkan (`vulkan_basiceffect_fog_test.cpp`, `vulkan_basiceffect_textured3d_fog_test.cpp`,
  `vulkan_environmentmapeffect_fog_test.cpp` — `textured3d.vert.glsl`, `env_map3d.vert.glsl`, and by extension
  likely every other Vulkan 3D fog-capable shader). Each affected test's own expected values assert the *wrong*
  (mirrored) fog behavior, matching the buggy shader rather than real FNA — meaning these tests would need their
  expected values corrected, not just the shaders, once fixed. **This is now this audit's most widely-confirmed
  single defect** (2 backends, 6 shader variants, all traced to the same root formula) — high priority for the
  Pass 3 systematic FNA parity sweep and Pass 4 backend matrix to determine its true full extent (D3D9/D3D11/D3D12/
  SdlGpu/WebGPU/Software/SdlRenderer/Dx3/Canvas/Ascii/Headless not yet checked for the same formula).
  **UPDATE (direct source read of the shared `D3DCommon` shaders ahead of the `backend-d3d11`/`backend-d3d12`
  shard audits): `src/CNA/Internal/Backends/D3DCommon/shaders/skinned3d.vert.hlsl` — compiled into BOTH D3D11 and
  D3D12 — has the identical mirrored formula, `(FogStartEnd.y - input.Position.z) / max(FogStartEnd.y -
  FogStartEnd.x, 1e-6)` (algebraically `(FogEnd-z)/(FogEnd-FogStart)`).** This file's own header comment claims
  the formula "matches EasyGL/Bgfx's established SkinnedEffect fog formula exactly" — a **false claim**: EasyGL's
  real formula is the corrected, post-Task-1111 one; only Bgfx's is the mirrored/wrong one this comment actually
  matches. This is revealing as a likely propagation mechanism: a later port copied whichever prior instance was
  most convenient (Bgfx's) while believing, incorrectly, that it agreed with EasyGL's (since-fixed) version,
  rather than re-deriving the formula from FNA. **Raises the confirmed count to 3 backend-groups at the
  shader-source level: Bgfx, Vulkan, and D3D11+D3D12 (shared D3DCommon source).**
- **CONFIRMED IN 3 BACKENDS: skinned-effect shaders skip the WorldInverseTranspose normal transform** (EasyGL,
  WebGPU — see below — and now **Vulkan**: `skinned3d.vert.glsl`/`skinned3d_vertexlit.vert.glsl` compute the lit
  normal as `mat3(skinMat)*aNormal` with no World-space composition, per `vulkan_skinnedeffect_preferperpixellighting_test.cpp`'s
  audit). Same root cause, same "invisible because every test uses World=Identity" masking. See below for the
  full EasyGL/WebGPU writeup — this note just adds Vulkan as a third confirmed instance.
  **D3D9 adds a nuanced 4th data point**: its *vendored* stock-effect shaders (SkinnedEffect.fx, byte-for-byte
  from FNA, exempt from audit per D-5) do NOT have this bug — confirmed via `d3d9_drawex_test.cpp`'s audit, which
  explicitly checked and found D3D9 shares neither the fog-formula nor the normal-transform defect for its stock
  effects. **However, D3D9's own CNA-original (non-vendored) `PbrSkinned3D.hlsl` custom shader DOES have it** —
  confirmed via `d3d9_pbr_test.cpp`'s audit (raw World instead of `WorldInverseTranspose` for the skinned
  normal/tangent transform, masked by that test's own `World=Identity` scene) — meaning the defect isn't confined
  to a single copy-pasted shader family; it recurs independently in D3D9's own hand-written PBR-skinning shader
  too, suggesting a shared conceptual mistake (skinning-then-forgetting-the-outer-normal-matrix) rather than one
  line of source propagating verbatim across every instance.
  **UPDATE: now confirmed in 2 more backends via direct source reads, both explicitly self-documented as ported
  from an existing (buggy) instance rather than independently reintroduced:**
  (a) **SdlGpu** — `skinned3d.vert.glsl`/`skinned_colored3d.vert.glsl` (found via `sdlgpu_skinned_test.cpp`,
  `sdlgpu_skinnedeffect_vertexcolor_test.cpp`, `sdlgpu_smoke_test.cpp`'s audits) transform the normal by the
  bone-skin matrix alone with **no world-space contribution at all**, and the shader's own comment "explicitly
  acknowledges the omission was ported from Vulkan" (per the `sdlgpu_smoke_test.cpp` audit); SdlGpu's
  `pbr_skinned3d.vert.glsl` (via `sdlgpu_skinnedpbreffect_test.cpp`) has the narrower "raw `mat3(World)` instead of
  inverse-transpose" variant, inconsistent with its own correct non-skinned sibling `pbr3d.vert.glsl`.
  (b) **D3D11 + D3D12 (shared `D3DCommon` source)** — found via direct source reading ahead of those shards'
  own full audits: `src/CNA/Internal/Backends/D3DCommon/shaders/skinned3d.vert.hlsl` (`output.Normal =
  normalize(mul(input.Normal, (float3x3)skinMat))`, no `World` composed in at all) carries an explicit header
  comment stating it was **"Ported line-by-line from `src/CNA/Internal/Backends/Vulkan/shaders/skinned3d.vert.glsl`"**
  — the clearest, most explicit first-hand confirmation yet of the Vulkan→D3DCommon porting chain (mirroring the
  already-confirmed EasyGL→WebGPU chain below). The sibling `pbr_skinned3d.vert.hlsl` has the narrower "raw
  `World`, not inverse-transpose" variant (`output.Normal = normalize(mul(mul(input.Normal, skinNormalMat),
  (float3x3)World))`), **self-documented in its own comment**: "plain World (NOT the inverse-transpose
  pbr3d.vert.hlsl's unskinned sibling uses)" — i.e. the author of this shader already knew the correct convention
  (visible one file away) and used the wrong one anyway for the skinned variant. `skinned3d_vertexlit.vert.hlsl`
  and both `.frag.hlsl` siblings in the same directory have not yet been fully read to confirm/rule out the same
  pattern — queued for the `backend-d3dcommon`/`backend-d3d11`/`backend-d3d12` shard audits.
  **This raises the confirmed-at-shader-source-level count to 5 of 14 backends: EasyGL, WebGPU, Vulkan, SdlGpu,
  D3D11+D3D12 (shared D3DCommon)** — only Bgfx's *own* skinned shader source (as opposed to its already-audited
  *test* files, which only infer the bug from masked test behavior) remains unconfirmed at the direct-source-read
  level among backends with a SkinnedEffect implementation.
- **NEW: a *second*, distinct fog defect — "object-space-only fog" (ignores World/View for the Z used in the fog
  calculation), separate from the Task-1111 mirrored-formula bug above.** Confirmed in D3D9's own custom shaders:
  `SkinnedVertexColor3D.hlsl` (via `d3d9_skinnedvertexcolor_test.cpp`'s audit) and, per that same report, also
  `Pbr3D.hlsl`/`PbrSkinned3D.hlsl` — all compute fog from raw local-space vertex Z, never transforming it by
  World/View first, unlike this same backend's own correct `ComputeFogVectorEXT()` path used for every vendored
  stock effect. This matches a previously-recorded EasyGL memory note (`feedback_easygl_fog_object_space_only`)
  about the identical class of mistake in that backend — worth checking whether EasyGL's own non-stock shaders
  have the same issue, and treating "object-space-only fog in a CNA-original (non-vendored) shader" as its own
  distinct pattern to watch for, separate from the vendored/ported stock-effect fog-formula bug.
- **NEW, Vulkan-specific: `SkinnedEffect::FillGpuDrawParams()` never sets `ambientColor`, and Vulkan's skinned
  shaders never consume `emissiveColor`** — so `AmbientLightColor`/`EmissiveColor` are silently no-ops for skinned
  models on Vulkan specifically (EasyGL forwards them correctly). Confirmed across 4 test files
  (`vulkan_skinnedeffect_combined_test.cpp`, `_preferperpixellighting_test.cpp`, `_specular_test.cpp`,
  `_vertexcolor_test.cpp` — the last of which explicitly identifies the defect in its own header comment and
  deliberately routes around it by setting `AmbientLightColor=0`, per that file's audit).
- **NEW, Vulkan-specific: `env_map3d.vert.glsl` lacks the Y-flip present in every other core Vulkan 3D vertex
  shader**, causing `EnvironmentMapEffect` scenes to render vertically mirrored on Vulkan. Confirmed across 4 test
  files (`vulkan_env_map_test.cpp`, `_amount_one_test.cpp`, `_amount_zero_test.cpp`, `_combined_test.cpp`,
  `_eyeposition_test.cpp`) — all masked because their scenes are symmetric enough (identity View, centered camera,
  center-pixel-only sampling) that a vertical mirror is invisible to the specific pixel each test checks.
  **UPDATE: a 5th masked instance found** in the `examples-tests-generic` batch —
  `environmentmapeffect_alphascaledlerp_test.cpp` (a shared cross-backend test file, registered on Vulkan among
  others) exercises this exact shader and is masked for the identical reason (identity View, center-pixel-only
  sampling).
- **CONFIRMED IN 4 BACKENDS (Bgfx, WebGPU, Vulkan, SdlGpu): `EnvironmentMapEffect`'s fragment shader
  re-multiplies `EmissiveColor` by `DiffuseColor`** instead of adding it unscaled (FNA's `Lighting.fxh` convention,
  explicitly confirmed by this project's own `EnvironmentMapEffect.cpp` comment stating the unscaled-add is
  required to "match FNA"). Confirmed across 5 Bgfx test files (`bgfx_environmentmapeffect_eyeposition_test.cpp`,
  `_fresnel_test.cpp`, `_multilight_test.cpp`, `_specular_test.cpp`, `_worldtransform_test.cpp`), **WebGPU**
  (`webgpu_envmap3d_test.cpp`'s audit, directly reading `WebGPUGraphicsBackend::CreateEnvMapResources()`'s
  fragment shader: `litRGB=(emissiveAmount+lightSum)*diffuseColor`), **Vulkan** (previously only suspected from
  test-file phrasing; now independently confirmed via the `examples-tests-generic` batch's direct read of Vulkan's
  own `env_map3d.frag.glsl` while auditing `environmentmapeffect_alphascaledlerp_test.cpp` — resolving the prior
  "unconfirmed" note), and now **SdlGpu** (`src/CNA/Internal/Backends/SdlGpu/shaders/env_map3d.frag.glsl`, found
  via `sdlgpu_envmap_test.cpp`'s and `sdlgpu_smoke_test.cpp`'s audits: `litRGB = (emissiveAmount + lightSum) *
  DiffuseColor`, byte-for-byte the same formula shape) — all masked because no test in any family varies
  `DiffuseColor` away from its default white or `EmissiveColor`/`AmbientLightColor` away from black. **A third
  systemic, multi-backend defect for this audit, alongside the fog-formula and skinned-normal-transform bugs, now
  the 4-backend-widest of the three** — remaining unchecked: D3D9/D3D11/D3D12/Software/SdlRenderer/Dx3/Canvas/
  Ascii/Headless's own `EnvironmentMapEffect` shaders.
- **NEW, WebGPU-specific: `SpriteBatch`'s clip-space mapping is always backbuffer-relative, never
  render-target-relative.** `WebGPUGraphicsBackend::QueueSprite()` derives its clip-space viewport exclusively
  from the backbuffer's physical/virtual size via `ComputeLogicalViewport()`, never from the currently-bound
  `RenderTarget2D`/`RenderTargetCube` face — so `SpriteBatch.Draw()` into an off-screen target of a different size
  mis-maps its destination rectangle. Confirmed via `webgpu_rendertargetcube_test.cpp`'s audit, which found the
  test file's own Check-C comment already self-discloses this exact defect (empirically observed, then
  independently re-verified against production source) — a pre-existing, backend-wide gap currently uncovered by
  any regression test.
- **NEW, Bgfx-specific: `BgfxGraphicsBackend::EnsureViewState()` unconditionally clears color+depth+stencil on
  every `Clear*()` call regardless of the requested `ClearOptions`** — a stencil-only clear silently wipes color
  and depth too. Confirmed via `bgfx_graphicsdevice_clear_stencil_test.cpp`'s audit.

- **CONFIRMED SYSTEMIC, MULTI-BACKEND: skinned-effect shaders skip the WorldInverseTranspose normal transform.**
  First surfaced incidentally by 3 EasyGL example-test audits (`examples-tests-easygl` shard, all using
  `World=Identity` so unable to prove it), then independently confirmed by direct reading of
  `EasyGLGraphicsBackend.cpp`: `EnsureSkinnedProgram`/`EnsureSkinnedVertexLitProgram` never register or use a
  `uNormalMatrix` uniform at all (normal transformed only by the bone-skin matrix), and `EnsurePbrSkinnedProgram`
  uses the raw `uWorld` matrix instead of the correct inverse-transpose. **Then confirmed to recur in the WebGPU
  backend too** (`backend-webgpu` direct audit): `CreateSkinnedResources()`'s WGSL shader does
  `output.worldNormal = normalize(skinMat3 * input.normal)` — same bug, exactly — and the surrounding code
  comment (`WebGPUGraphicsBackend.cpp` ~line 7436) explicitly states this shader was **"ported from
  EasyGLGraphicsBackend::EnsureSkinnedProgram()'s GLSL shader line-for-line,"** meaning the bug was deliberately
  and knowingly propagated as part of a "match the reference backend's rendered output" porting discipline, not
  independently reintroduced by accident. **This makes it very likely every other backend with its own
  SkinnedEffect implementation (Vulkan, Bgfx, D3D9, D3D11, D3D12, SdlGpu — all confirmed via the
  `examples-tests-*` shards to have `skinnedeffect_*` tests) has the identical defect, each also probably ported
  from the same EasyGL reference.** **Priority check for every remaining backend audit**: does its own
  SkinnedEffect/SkinnedPbrEffect shader compose the object's world-space normal matrix with the per-vertex
  bone-skin matrix, or only apply the bone matrix? See `AUDIT_FINDINGS_INDEX.md` HIGH/MEDIUM sections,
  `audit/src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp.audit.md` F2/F3, and
  `audit/src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp.audit.md` for full detail.

## CI-masking risk: known-failing tests registered without an expected-failure annotation

- `bgfx_rendertargetcube_depthformat_test.cpp` (`Bgfx_RenderTargetCube_DepthFormat` CTest target) asserts an
  outcome the project's own `plan_graphics.md`/git log confirm is a still-open, known-failing case (Task 952) —
  registered with no `WILL_FAIL`/skip annotation, meaning CI either already shows this red (masked among other
  noise) or something else is suppressing it.
- `bgfx_skinnedeffect_weightspervertex_test.cpp` (`Bgfx_SkinnedEffect_WeightsPerVertex`) is confirmed via git
  history to have been a pre-existing CTest failure since before commit `0cb4a591` (2026-07-16), never fixed or
  root-caused.
- **Recommend a full CTest-registration sweep (Pass 6) to enumerate every currently-failing/expected-to-fail test
  across all backends** and confirm each either passes, is properly marked `WILL_FAIL`, or is tracked as a known
  open issue — this pair suggests there may be more.
- **INDEPENDENTLY RE-VERIFIED BY DIRECT BUILD+EXECUTION (twice: once by the auditing subagent, then re-confirmed
  first-hand during synthesis): `EasyGL_AvatarRenderer_TintRouting` is a currently-failing CTest, registered with
  no `WILL_FAIL`/skip annotation** (`examples/avatar_tint_routing_integration_test.cpp`, `examples-tests-generic`
  shard). Configured a scoped `EASYGL`-backend debug build, built only `cna_test_avatar_tint_routing`, and ran
  `ctest -R EasyGL_AvatarRenderer_TintRouting` directly: **`Failed`, 0/1 passed**, actual output:
  `[FAIL] AvatarTintRoutingIntegration: left=(81,51,31) right=(41,181,255); expected: left=HairColor(40,25,15),
  right=ShirtColor(20,90,155)`. The deltas are large (up to 41 on the red channel, 100 on the blue channel) — bigger
  than a merely-mistuned tolerance alone would suggest, so while the subagent's root-cause analysis (the test's own
  `±20` tolerance never re-tuned for the real `(Ambient+lightSum)*Diffuse` FNA formula given this scene's
  fully-saturated `Ambient`+`Light0` choice) may be *a* contributing factor, the magnitude here warrants a closer
  look during Pass 6/the `xna-gamerservices` shard audit rather than treating "tolerance-only" as fully settled.
  **Notably, the sibling `Vulkan_AvatarRenderer_TintRouting` variant currently *passes* — but only by
  coincidence**: a separately-confirmed, independent defect (`SkinnedEffect::FillGpuDrawParams` never sets
  `ambientColor`; Vulkan's `FillExtPushConst` has no `emissiveColor` slot at all — see the Vulkan-specific
  `AmbientLightColor`/`EmissiveColor` no-op entry above) silently drops the same ambient term that's
  over-tolerating the EasyGL failure, and the two errors happen to cancel out on Vulkan specifically. **This is a
  third, independent confirmation of the "documentation/test rot" pattern above, but more severe**: unlike the
  other instances (stale comments describing already-fixed behavior), this one is an actually-red, currently-
  registered CTest that a normal `ctest` run shows failing today — raising the priority of the Pass 6
  CTest-registration sweep from "recommended" to "should specifically re-run this exact test name first."

## API design: bare public fields instead of the project's own get/set convention

- `BasicEffect::VertexColorEnabled` is a bare public field with no `getXProperty()`/`setXProperty()` wrapper at
  all, unlike every other property on the class — a direct violation of this project's own explicit C# property
  convention (`CLAUDE.md`). Confirmed via both `bgfx_basiceffect_texture_vertexcolor_enabled_test.cpp` and
  `vulkan_basiceffect_vertexcolor_enabled_test.cpp`'s audits (independently discovered in two different backend
  test batches, exercising the same production `BasicEffect.hpp`/`.cpp`, not a backend-specific issue), and now a
  **3rd time** via `examples/basic_effect_test.cpp` (`examples-tests-generic` shard, `fx.VertexColorEnabled =
  true` used directly as a bare field). Worth a priority check when the `xna-graphics` shard reaches `BasicEffect`
  for whether this is the only such lapse.

## Recurring testing gaps

- **Documentation rot: header comments describing "known bugs"/"current limitations"/expected-throw assertions
  are not revisited once the underlying code is fixed.** Found repeatedly in the `examples-tests-easygl` batch
  (218 files) — at least 6 distinct files carry stale bug/limitation claims contradicted by since-closed tasks —
  and again in the `examples-tests-sdlrenderer` batch (67 files): `sdlrenderer_clearoptions_audit_test.cpp` and
  `sdlrenderer_rendertarget_depth_decision_test.cpp` both assert an expected-throw behavior for
  `ClearOptions`/`DepthBuffer` combinations that a later FNA-parity fix (commit `90f5db2c`) deliberately changed to
  silently-masked-and-degrade instead — the tests were never updated to match. **Now confirmed across four
  independent mechanical-batch passes** (EasyGL, SdlRenderer, Bgfx, Vulkan), strengthening the case that this is a
  systemic gap in this codebase's process (fixing behavior without a corresponding sweep of test/comment claims
  that describe the old behavior), not incidental to any one subsystem. Prior EasyGL-batch instances: Vulkan
  blend state "almost entirely fake," `SetReferenceStencil` claimed universally missing, anisotropic filtering
  bugs claimed open, `EnvironmentMapEffect`'s pre-fix shader formula documented instead of the current one,
  `GetData()` claimed unimplemented. Bgfx/Vulkan batches added: `bgfx_basiceffect_specular_test.cpp`'s stale
  pre-Task-1104 constant has **zero disclosure comment** (unlike its EasyGL sibling, which does disclose the
  identical situation — inconsistent even in how the same underlying staleness is handled across ports);
  `bgfx_render_target_cube_sample_test.cpp`/`_render_target_sample_test.cpp` describe already-fixed unsafe-cast
  bugs (Task 873/874) as still unfixed; `bgfx_basiceffect_vertexcolor_enabled_test.cpp` makes a cull-state claim
  superseded the day after the file was authored (Task 896); `vulkan_dualtextureeffect_alpha_test.cpp` repeats the
  same stale "Vulkan BlendState almost entirely fake" (Task 868) claim; `vulkan_rendertarget2d_msaa_test.cpp`/
  `_rendertargetcube_msaa_test.cpp` claim `PreferMultiSampling` never reaches Vulkan, fixed by Task 902 the same
  day as the test's only commit. None of these are currently-live production bugs — the underlying code was
  actually fixed in each case — but the stale comments actively mislead a future reader (including future audit
  passes) into believing a fixed issue is still open. Recommend (not implemented by this audit) a periodic sweep
  specifically for "Task NNN"/"known bug"/"currently broken"-style comments cross-checked against `git log`/
  current source, independent of any one file's own audit.
- **Tests asserting metadata/capacity instead of actual data content or actual code-path execution**: a recurring
  shape across the EasyGL example-test shard — `easygl_vertexbuffer_setdata_test.cpp` (capacity getters only, never
  checks uploaded bytes), `easygl_dynamic_buffer_stress_test.cpp` (index-buffer half never actually draws
  indexed), `easygl_msaa_test.cpp` (scene can't distinguish MSAA-resolved from never-engaged). **Now also
  confirmed in Bgfx**: `bgfx_vertex_format_test.cpp`'s `UploadAndCheck()` never actually calls `SetData`, so all 4
  "stride 16/20/24/32" cases silently construct the same hardcoded stride-16 layout regardless of which
  declaration is nominally under test — and more importantly, the actual production functions this whole file
  exists to test (`BgfxVertexFormatHelper.hpp`'s `VertexElementFormatToBgfx`/`VertexElementUsageToBgfxAttrib`) are
  **never called anywhere** by `BgfxGraphicsBackend.cpp`'s real `MakeBgfxLayout()`, which dispatches purely on
  hardcoded byte-size instead — the test's entire subject may be dead code. Also `bgfx_render_target_usage_test.cpp`
  (never reads back a pixel to verify Discard vs. Preserve contents) and
  `bgfx_blendstate_separate_functions_test.cpp` (never reads the alpha channel, so `AlphaBlendFunction`'s
  independence is inferred, not observed). Worth watching for the same shape in every remaining backend's
  example-test shard.

## Build-system inconsistencies

_(pending)_

## Production correctness bugs outside the graphics-backend layer

- **HIGH: `SpriteFont::MeasureString`/`SpriteBatch::DrawString` dereference an `unordered_map::end()` iterator with
  no check, reachable via fully public API.** Found while auditing `examples/sprite_font_test.cpp`
  (`examples-tests-generic` shard): the test sets `DefaultCharacter` (via `setDefaultCharacterProperty`, which
  performs no validation) to a character not present in the font's own character map, one call short of exercising
  the bug. Tracing the production code (`SpriteFont.cpp:101-111`, `SpriteBatch.cpp:457-465`) confirmed both
  methods' `DefaultCharacter`-fallback lookup path dereferences the map iterator unconditionally, so a caller who
  sets a bad `DefaultCharacter` and then measures/draws a genuinely-missing glyph hits undefined behavior. **FNA's
  real behavior is to throw `KeyNotFoundException`** — this is a real, non-backend-specific FNA-parity gap in
  `Microsoft::Xna::Framework::Graphics::SpriteFont`/`SpriteBatch` themselves (not a rendering-backend bug), and
  should be flagged prominently when the `xna-graphics` shard reaches these two files.
