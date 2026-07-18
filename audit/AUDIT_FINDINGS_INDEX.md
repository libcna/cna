# AUDIT_FINDINGS_INDEX.md

**Status: SKELETON — populated incrementally as per-file audits surface findings worth surfacing globally (not
every `INFO`-level note needs an index entry; use judgment — this index is for anything `MEDIUM`+ severity, or
`LOW` if it recurs across many files).**

Recommendations recorded here are for future prioritization only — **no implementation work is performed as part
of this audit** (see `CLAUDE.md`/audit prompt "No-development rule").

## By severity

### CRITICAL
_(none recorded yet)_

### HIGH

- **Vulkan-specific: `SpriteBatch.Begin(transformMatrix)`'s transform is silently dropped —
  `VulkanSpriteBatchBackend` never overrides `SetTransformMatrix()`, confirmed by an exhaustive grep across the
  entire Vulkan backend directory (zero matches).** Every other checked backend (EasyGL, Bgfx, D3D9, D3D11,
  WebGPU, SdlGpu, SdlRenderer, Canvas, Dx3, Software, Headless, and Ascii via delegation) correctly applies it via
  one of two valid mechanisms. Found incidentally while auditing `D3D11SpriteBatch.cpp`, whose own header comment
  claims this as a "deliberate improvement" over Vulkan — independently verified true. No test anywhere exercises
  a non-Identity transform matrix on Vulkan. See `AUDIT_CROSS_CUTTING_FINDINGS.md`.
- **`EasyGL_AvatarRenderer_TintRouting` is a currently-failing CTest, registered with no `WILL_FAIL` annotation —
  independently re-confirmed by direct build+execution during synthesis (not just relayed from the audit
  subagent).** `ctest -R EasyGL_AvatarRenderer_TintRouting`: **Failed**, `left=(81,51,31) right=(41,181,255);
  expected: left=HairColor(40,25,15), right=ShirtColor(20,90,155)`. The sibling `Vulkan_AvatarRenderer_TintRouting`
  passes only by coincidence (a separate, independently-confirmed Vulkan `SkinnedEffect` ambient/emissive-forwarding
  bug cancels out the same miscalibration that fails on EasyGL). See
  [audit report](examples/avatar_tint_routing_integration_test.cpp.audit.md) and `AUDIT_CROSS_CUTTING_FINDINGS.md`
  (CI-masking risk).
- **`SpriteFont::MeasureString`/`SpriteBatch::DrawString` dereference an `unordered_map::end()` iterator with no
  check, reachable via fully public API** — setting `DefaultCharacter` (no validation in `setDefaultCharacterProperty`)
  to a character absent from the font's own map, then measuring/drawing a genuinely-missing glyph, is undefined
  behavior in `SpriteFont.cpp:101-111`/`SpriteBatch.cpp:457-465`. FNA throws `KeyNotFoundException` in the
  equivalent case. See [audit report](examples/sprite_font_test.cpp.audit.md) and
  `AUDIT_CROSS_CUTTING_FINDINGS.md` (Production correctness bugs outside the graphics-backend layer).
- **`env_map3d.frag`'s `EmissiveColor`-re-multiply bug now confirmed in 5 backends: Bgfx, WebGPU, Vulkan, SdlGpu,
  and D3D11+D3D12** (shared `D3DCommon/shaders/env_map3d.frag.hlsl`, also explicitly "ported line-by-line from
  Vulkan") — same `(emissiveAmount+lightSum)*DiffuseColor` shape everywhere. See
  [audit report](examples/sdlgpu_envmap_test.cpp.audit.md), [audit report](examples/sdlgpu_smoke_test.cpp.audit.md),
  and `AUDIT_CROSS_CUTTING_FINDINGS.md`.
- **The skinned-normal-transform bug (missing world-space normal-matrix contribution) is now confirmed at the
  shader-source level in 5 of 14 backends: EasyGL, WebGPU, Vulkan, SdlGpu, and D3D11+D3D12 (shared `D3DCommon`
  source) — and within D3DCommon specifically, confirmed in ALL 5 of its skinned vertex shaders (`skinned3d`,
  `skinned3d_vertexlit`, `skinned_colored3d`, `skinned_colored3d_vertexlit`, `pbr_skinned3d`), with zero
  exceptions.** The shared `D3DCommon/shaders/skinned3d.vert.hlsl` (compiled into both D3D11 and D3D12) carries an
  explicit header comment stating it was **"Ported line-by-line from
  `src/CNA/Internal/Backends/Vulkan/shaders/skinned3d.vert.glsl,"`** the clearest direct evidence yet of the
  cross-backend porting chain that propagated this bug (alongside the already-confirmed EasyGL→WebGPU chain).
  SdlGpu's own shader comment "explicitly acknowledges the omission was ported from Vulkan" too (per
  `sdlgpu_smoke_test.cpp`'s audit). The related but distinct "raw World instead of inverse-transpose" variant
  (rather than a complete omission) is separately confirmed in `pbr_skinned3d.vert.hlsl` (D3DCommon, shared
  D3D11/D3D12), `pbr_skinned3d.vert.glsl` (SdlGpu), `EnsurePbrSkinnedProgram` (EasyGL), and D3D9's own
  `PbrSkinned3D.hlsl`. **D3DCommon's own 3 unskinned lit vertex shaders (`lit_textured3d`, `pbr3d`,
  `lit_textured3d_vertexlit`) correctly use the inverse-transpose convention** — clean proof this is a
  skinning-specific oversight, not general unfamiliarity with the correct math. Also confirmed while reading this
  directory: **D3D11/D3D12 do NOT share Vulkan's `EnvironmentMapEffect` Y-flip bug** — a genuine, deliberate,
  well-documented backend difference (D3D's clip-space convention already matches XNA's, unlike Vulkan's), not an
  oversight. Only Bgfx's *own* skinned shader source remains unconfirmed at the direct-source-read level
  (only inferred so far from masked test behavior). See `AUDIT_CROSS_CUTTING_FINDINGS.md` (Systematic FNA parity
  gaps) for full detail and every originating test/source reference.
- **EasyGL backend: a constructor failure after `RegisterForWindow()` but before construction completes leaves a
  dangling entry in `IGraphicsBackend`'s static window registry.** Independently discovered via direct production
  code reading (not from the test batch). `EasyGLGraphicsBackend`'s constructor calls `RegisterForWindow(window,
  this)` early, then `SDL_GL_CreateContext` can throw shortly after (a real, reachable failure mode — unsupported
  GLES3 context, headless/CI environment, driver issue). The registry entry is never cleaned up since the
  destructor never runs on a failed construction. `SdlInputBridge.cpp`/`Mouse.cpp` both dereference
  `GetForWindow()`'s result unconditionally — a subsequent mouse/input event on that window would be a
  use-after-free. **The most severe confirmed finding in this audit so far.** See
  [audit report](src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp.audit.md) F1.
- **CONFIRMED IN 3 BACKEND-GROUPS — the pre-Task-1111 fog formula (already proven wrong by this project's own
  XNA-oracle diff and fixed in EasyGL) was never ported to Bgfx, Vulkan, or D3D11/D3D12.** Bgfx's
  `vs_alpha_test3d.sc`/`vs_colored3d.sc`/`vs_lit_textured3d.sc`, Vulkan's `textured3d.vert.glsl`/
  `env_map3d.vert.glsl`, and — confirmed by an exhaustive direct read of every fog-capable shader in the shared
  `D3DCommon` directory — **all 15 of D3DCommon's 15 fog-capable vertex shaders** (compiled into both D3D11 and
  D3D12) all use the mirror-image `(FogEnd-z)/(FogEnd-FogStart)` formula instead of the FNA-correct
  `(z+FogEnd)/(FogEnd-FogStart)`. **D3D11/D3D12 is the widest single instance of this bug found in the audit so
  far — not a handful of shaders, but literally every fog-capable shader in the shared source.** This is this
  audit's single most widely-confirmed defect. Full detail and all originating reports in
  `AUDIT_CROSS_CUTTING_FINDINGS.md` (Systematic FNA parity gaps).
- *(superseded by the 5-backend skinned-normal-transform entry above)* Vulkan's `skinned3d.vert.glsl`/
  `skinned3d_vertexlit.vert.glsl` share the same missing-world-space-normal-transform defect already confirmed in
  EasyGL and WebGPU. See `examples/vulkan_skinnedeffect_preferperpixellighting_test.cpp.audit.md`.
- **Vulkan-specific: `SkinnedEffect::FillGpuDrawParams()` never sets `ambientColor`, and Vulkan's skinned shaders
  never consume `emissiveColor`** — silently drops `AmbientLightColor`/`EmissiveColor` for skinned models on
  Vulkan only. Confirmed across 4 test files; this is also the reason `Vulkan_AvatarRenderer_TintRouting`
  coincidentally passes despite the EasyGL sibling failing (see the currently-failing-CTest entry above).
  **A narrower variant now confirmed in D3D11+D3D12 too**: the shared `D3DCommon` `SkinnedEffect` fragment
  shaders (`skinned3d.frag.hlsl` and its 3 siblings) have no `EmissiveColor` cbuffer field at all, unlike their
  unskinned siblings which do — `AmbientColor` IS correctly present/consumed here, unlike Vulkan, so only the
  `EmissiveColor` half of the defect transfers. See `AUDIT_CROSS_CUTTING_FINDINGS.md`.
- **Vulkan-specific: `env_map3d.vert.glsl` lacks the Y-flip every other core Vulkan 3D vertex shader has**,
  rendering `EnvironmentMapEffect` scenes vertically mirrored. Confirmed across 5 test files (a 5th masked instance
  since found via `environmentmapeffect_alphascaledlerp_test.cpp`); see `AUDIT_CROSS_CUTTING_FINDINGS.md`.
- *(superseded by the 4-backend entry above)* `EnvironmentMapEffect`'s fragment shader re-multiplies `EmissiveColor`
  by `DiffuseColor` instead of adding it unscaled (FNA's real `Lighting.fxh` convention) — now confirmed in Bgfx,
  WebGPU, Vulkan (resolving the prior "suspected"), and SdlGpu. See `AUDIT_CROSS_CUTTING_FINDINGS.md`.
- **WebGPU-specific: `SpriteBatch`'s clip-space mapping is always backbuffer-relative, never render-target-relative**
  — drawing into an off-screen target of a different size mis-maps sprite placement. See
  [audit report](examples/webgpu_rendertargetcube_test.cpp.audit.md) and `AUDIT_CROSS_CUTTING_FINDINGS.md`.
- **D3D9's own custom (non-vendored) `PbrSkinned3D.hlsl` shares the skinned-normal-transform bug** (4th confirmed
  instance) — its *vendored* stock effects do not. See
  [audit report](examples/d3d9_pbr_test.cpp.audit.md).
- **NEW pattern: "object-space-only fog" in D3D9's own custom shaders** (`SkinnedVertexColor3D.hlsl`, `Pbr3D.hlsl`,
  `PbrSkinned3D.hlsl`) — fog computed from raw local-space Z, ignoring World/View, distinct from the Task-1111
  mirrored-formula bug. See [audit report](examples/d3d9_skinnedvertexcolor_test.cpp.audit.md) and
  `AUDIT_CROSS_CUTTING_FINDINGS.md`.
- **Bgfx-specific: `BgfxGraphicsBackend::EnsureViewState()` unconditionally clears color+depth+stencil on every
  `Clear*()` call regardless of the requested `ClearOptions`** — a stencil-only clear silently wipes color and
  depth too. See [audit report](examples/bgfx_graphicsdevice_clear_stencil_test.cpp.audit.md).
- **Bgfx-specific: `bgfx_vertex_format_test.cpp`'s entire subject (`BgfxVertexFormatHelper.hpp`'s
  `VertexElementFormatToBgfx`/`VertexElementUsageToBgfxAttrib`) is never called by production code** — real
  `MakeBgfxLayout()` dispatches on hardcoded byte-size instead, and the test's own `UploadAndCheck()` never calls
  `SetData`, so all 4 stride cases silently test the same hardcoded stride-16 layout. See
  [audit report](examples/bgfx_vertex_format_test.cpp.audit.md).
- **`SpriteBatch::Begin()` sets `begun_=true` before backend calls that can throw (`SetCustomEffect`/
  `SetTransformMatrix`/`SetSamplerFilter`/`Begin`); if one throws, the object is permanently stuck reporting
  "Begin has been called before calling End" on every subsequent `Begin()`, with no documented recovery besides
  an undocumented explicit `End()` call.** A general `SpriteBatch.cpp` defect, not backend-specific — discovered
  while auditing `sdlrenderer_custom_effect_throws_test.cpp` (whose own custom-Effect-throws scenario deliberately
  triggers it, though the test itself doesn't check the stuck-state consequence). See
  [audit report](examples/sdlrenderer_custom_effect_throws_test.cpp.audit.md).
- **`GraphicsDevice::SetRenderTargets` mutates `currentRenderTargets_`/`renderTargetBound_` to the requested
  (rejected) MRT bindings *before* `backend_->SetRenderTargets` can throw** — a caller that doesn't manually
  restore the render target after catching the MRT-unsupported exception is left with device-tracked state that
  doesn't match reality. Same "mutate before the fallible call" shape as the SpriteBatch::Begin() finding above.
  See [audit report](examples/sdlrenderer_rendertargets_mrt_throws_test.cpp.audit.md).
- **CONFIRMED IN 2 BACKENDS: `SkinnedEffect`'s shaders never apply the object's World transform to lighting
  normals at all.** `EnsureSkinnedProgram()`/`EnsureSkinnedVertexLitProgram()` (EasyGL) transform the normal with
  `mat3(skinMat)*aNormal` only; neither registers a `uNormalMatrix` uniform, unlike every non-skinned lit shader
  in the same file (which correctly uses the inverse-transpose `uNormalMatrix`, a documented prior fix, Task 398).
  **`CreateSkinnedResources()` (WebGPU) has the byte-for-byte identical defect**, and its own surrounding comment
  explicitly states the shader was "ported from `EasyGLGraphicsBackend::EnsureSkinnedProgram()`'s GLSL shader
  line-for-line" — i.e. the bug was knowingly propagated as part of a deliberate cross-backend-consistency porting
  practice. This makes it very likely every remaining backend with its own SkinnedEffect (Vulkan, Bgfx, D3D9,
  D3D11, D3D12, SdlGpu) has the same defect. Any rotated skinned model's lighting is wrong; invisible to every
  existing test because they all use `World=Identity`. See
  [EasyGL audit report](src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp.audit.md) F2,
  [WebGPU audit report](src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp.audit.md) F1, and the
  originating test reports:
  [easygl_skinnedeffect_preferperpixellighting_test.cpp](examples/easygl_skinnedeffect_preferperpixellighting_test.cpp.audit.md),
  [easygl_skinnedeffect_specular_test.cpp](examples/easygl_skinnedeffect_specular_test.cpp.audit.md),
  [easygl_skinnedpbreffect_golden_test.cpp](examples/easygl_skinnedpbreffect_golden_test.cpp.audit.md).
- **`GraphicsDevice::DrawUserPrimitives(void*, VertexDeclaration&)` never propagates the declaration to the
  backend** — no `SetVertexDeclaration` call before `SetData`, so EasyGL silently falls back to a hardcoded
  stride-keyed layout table. A genuinely custom (non-matching-stride or reordered-field) vertex layout would
  silently render wrong; the existing test can't detect this because its vertex struct happens to alias the
  fallback layout. See
  [easygl_draw_user_primitives_custom_test.cpp](examples/easygl_draw_user_primitives_custom_test.cpp.audit.md).
- **`easygl_msaa_test.cpp` cannot actually verify MSAA.** Two compounding issues: (1) the test/CTest name and
  header claim "4×" MSAA, but `GraphicsDeviceManager`'s real default for `PreferMultiSampling=true` (with
  `MultiSampleCount` left at 0) is **8**, not 4 — confirmed in `GraphicsDeviceManager.cpp` and admitted by the
  file's own constructor comment; (2) the test's solid full-viewport quad produces an identical center pixel
  whether or not the MSAA resolve pipeline ever actually ran, so the assertion cannot fail in the way its header
  describes. See [easygl_msaa_test.cpp](examples/easygl_msaa_test.cpp.audit.md).
- **`easygl_dynamic_buffer_stress_test.cpp`'s index-buffer half never calls `SetIndexBuffer`/
  `DrawIndexedPrimitives`** despite its header comment claiming pixel-readback verification of dynamic index-buffer
  streaming — reduces to a static capacity assertion that would pass even if `SetData` were a no-op. See
  [easygl_dynamic_buffer_stress_test.cpp](examples/easygl_dynamic_buffer_stress_test.cpp.audit.md).

### MEDIUM (SdlGpu, preliminary — full per-file audit not yet done)

- **SdlGpu backend: constructor resource leak if any of 10 sequential shader/pipeline-creation calls throws.**
  Unlike WebGPU's model-example try/catch+cleanup, `SdlGpuGraphicsBackend`'s constructor has no exception-safety
  wrapper around `CreateSpriteResources` through `CreatePbrResources` — a failure leaks the SDL GPU device, claimed
  window, and any already-created pipelines, since the destructor (which correctly tears all of this down) never
  runs on a failed construction. See `AUDIT_CROSS_CUTTING_FINDINGS.md`.

### MEDIUM (continued, Bgfx/Vulkan batch)

- **Two known-failing CTest targets registered with no `WILL_FAIL`/skip annotation**: `Bgfx_RenderTargetCube_DepthFormat`
  (Task 952, still open) and `Bgfx_SkinnedEffect_WeightsPerVertex` (pre-existing failure since before commit
  `0cb4a591`). See `AUDIT_CROSS_CUTTING_FINDINGS.md` (CI-masking risk).
- **`BasicEffect::VertexColorEnabled` is a bare public field with no property wrapper**, violating this project's
  own C# property convention — confirmed via both Bgfx and Vulkan test audits exercising the same production
  code. See `AUDIT_CROSS_CUTTING_FINDINGS.md`.
- **Vulkan hardcodes full-target scissor for render-target passes**, ignoring `ScissorRectangle`/
  `ScissorTestEnable` when a render target is bound. See
  [audit report](examples/vulkan_scissor_test.cpp.audit.md).

### MEDIUM (continued)

- **Two SDL_Renderer tests (`sdlrenderer_clearoptions_audit_test.cpp`, `sdlrenderer_rendertarget_depth_decision_test.cpp`)
  have stale expected-throw assertions superseded by a real FNA-parity fix.** Commit `90f5db2c` made
  `GraphicsDevice::Clear(ClearOptions,...)` mask `DepthBuffer`/`Stencil` out and degrade silently on backends with
  no real depth/stencil buffer, instead of throwing — the *production* behavior now matches FNA
  (`plan_graphics.md` Task 1113, still open, tracks this); the *tests* still assert the old throwing behavior and
  would fail if run today. Production code is correct; tests need updating. See
  [audit report](examples/sdlrenderer_clearoptions_audit_test.cpp.audit.md) and
  [audit report](examples/sdlrenderer_rendertarget_depth_decision_test.cpp.audit.md).
- **SpriteFont's `DrawString` flip-lookup tables are sized 3, not FNA's 4** — combined `SpriteEffects` flips are
  unsupported, and a raw enum cast forcing an out-of-range combination would read past the end of a `constexpr`
  array (undefined behavior). See
  [audit report](examples/sdlrenderer_spritefont_effects_test.cpp.audit.md).

### MEDIUM

- **Headless backend: `HeadlessStatistics::primitiveCount` undercounts instanced draws by a factor of
  `instanceCount`.** `src/CNA/Internal/Backends/Headless/HeadlessGraphicsBackend.cpp` `DrawInstancedPrimitivesEx`
  (lines 819-830) corrects `drawCallCount` for instancing but not `primitiveCount`. See
  [audit report](src/CNA/Internal/Backends/Headless/HeadlessGraphicsBackend.cpp.audit.md) F1.
- **Software backend: `DepthBufferWriteEnable`/`SetDepthWriteEnabled` have no effect — depth is always written
  when the test passes.** `src/CNA/Internal/Backends/Software/SoftwareGraphicsBackend.cpp` `ApplyDepthStencilState`
  (lines 1095-1099) never stores the write-enable flag; both rasterizer cores write depth unconditionally. See
  [audit report](src/CNA/Internal/Backends/Software/SoftwareGraphicsBackend.cpp.audit.md) F1.
- **Software backend: `DepthStencilState.DepthBufferFunction` is ignored — depth test is hardcoded to
  LessEqual.** Same file, same method; the `depthFunc` parameter is discarded and the rasterizer always does
  `reject if depth > stored`. See
  [audit report](src/CNA/Internal/Backends/Software/SoftwareGraphicsBackend.cpp.audit.md) F2.
- **Dx3 backend: a failed resize (`SetVirtualResolution`) destroys the working primary/backbuffer surfaces before
  confirming the replacement succeeds, leaving the backend permanently unusable.**
  `Dx3GraphicsBackend::Impl::CreateSurfaces` releases the old DirectDraw surfaces unconditionally before
  attempting to create new ones; on failure, every subsequent `Clear`/`Present`/`ReadBackbuffer` call dereferences
  a null surface pointer. See
  [audit report](src/CNA/Internal/Backends/Dx3/Dx3GraphicsBackend.cpp.audit.md) F1.
- **EasyGL backend: `SkinnedPbrEffect`'s shader uses the raw `uWorld` matrix instead of the inverse-transpose
  normal matrix** for its normal/tangent transform — correct only for rotation/uniform-scale World transforms,
  wrong for non-uniform scale. A narrower-scope sibling of the HIGH finding above. See
  [audit report](src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp.audit.md) F3.

### LOW / recurring test-authoring patterns (from the `examples-tests-easygl` mechanical batch, 218 files)

- **Stale "known bug" documentation comments contradicted by later fixes**, found repeatedly across this shard:
  `easygl_blendstate_additive_test.cpp`/`_nonpremultiplied_test.cpp`/`_separate_factors_test.cpp`/
  `_separate_functions_test.cpp` all carry stale claims that Vulkan's blend state is "almost entirely fake" —
  contradicted by Task 868's since-recorded closure (confirmed via `plan_graphics.md` and current Vulkan source).
  `easygl_graphicsdevice_reference_stencil_test.cpp` falsely claims `SetReferenceStencil` doesn't exist project-wide
  when it's implemented on 6 of 14 backends. `easygl_texture_anisotropic_effect_test.cpp` describes Task 867/918
  anisotropic bugs as open when both are confirmed fixed. `easygl_env_map_test.cpp`'s header documents the
  pre-fix (buggy) shader formula, not the current one — masked because its specific test parameters happen to
  make both formulas agree. `easygl_buffer_usage_test.cpp` claims `GetData()` is unimplemented; Task 930 added it.
  **Pattern for `AUDIT_CROSS_CUTTING_FINDINGS.md`: this codebase's header comments document point-in-time bug
  investigations accurately at the time, but are not being systematically revisited when the underlying code is
  later fixed — a recurring documentation-rot risk, not a single mistake.**
- **Tests that assert only metadata/capacity, not actual data content**: `easygl_vertexbuffer_setdata_test.cpp`
  only asserts capacity/metadata getters that `SetData` never touches — no scenario verifies uploaded content
  landed at the correct offset, despite that being the file's stated purpose.
- **Weak/incomplete enum coverage**: `easygl_depthstencilstate_compare_function_test.cpp` tests only 5 of 8
  `CompareFunction` values (Equal/GreaterEqual/NotEqual untested).
- Full per-file detail for all 218 files, including ~15 additional "Needs attention" verdicts not severe enough
  for this index, lives under `audit/examples/easygl_*.audit.md`.

## By subsystem
_(index rebuilt from the severity table above once populated)_

## By category
_(correctness / FNA-parity / architecture / performance / memory / portability / testing — rebuilt once populated)_
