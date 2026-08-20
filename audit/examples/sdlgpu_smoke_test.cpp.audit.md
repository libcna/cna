# Audit: examples/sdlgpu_smoke_test.cpp

## Metadata

- Source file: `examples/sdlgpu_smoke_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlgpu` shard — end-to-end backend lifecycle/device/window/swapchain
  smoke test, this backend's first proof gate (plans/plan_sdlgpu.md SDLGPU-6..12)
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_sdlgpu_test(cna_test_sdlgpu_smoke …)` / `cna_register_backend_test(NAME SdlGpu_Smoke …)`,
  `cmake/Tests/SdlGpuTests.cmake:18-20`, `TIMEOUT 60`).
- XNA/FNA relevance: indirect for most checks (`GraphicsDevice.Clear`, `VertexBuffer`/`IndexBuffer`
  raw-data upload are XNA-facing concepts) but the checks themselves exercise the CNA-internal
  `IGraphicsBackend`/`IVertexBufferBackend`/`IIndexBufferBackend` seam directly, not the XNA-facing
  `VertexBuffer`/`IndexBuffer` classes.
- Related production code: `include/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.hpp`,
  `src/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.cpp` (constructor lines 487-543,
  `EnsureFrameRendered()` lines 646-785, `SdlGpuVertexBufferBackend::SetDataWithOptions` lines
  4658-4728, `SdlGpuIndexBufferBackend::Upload` lines 4756-4825), and — per this batch's required
  cross-backend check — `src/CNA/Internal/Backends/SdlGpu/shaders/env_map3d.frag.glsl` and
  `skinned3d.vert.glsl`.

## Purpose

Six-check smoke test proving the backend's basic device/window/swapchain lifecycle actually works
end-to-end with a real `SDL_Window`/`SDL_GPUDevice`: (A) `GetWindowInternal()` returns a real
window, (B) `GetRendererInternal()` is null (this backend never creates an `SDL_Renderer`), (C)
`GetViewportSize()` reports a real positive size, (D) a real `SdlGpuVertexBufferBackend`/
`SdlGpuIndexBufferBackend` round-trips `SetData()`/`SetData16()` count, (E) 60 frames of
`Clear(Target|DepthBuffer|Stencil, …)` + the automatic post-`Draw()` `Present()` complete with no
exception, (F/G) `SetDataWithOptions()`/`SetData16WithOptions()` with `Discard` and `NoOverwrite`
hints are real overrides (SDLGPU-23), not silently-ignored defaults.

## Executive Verdict

**Mostly healthy.** Every check genuinely exercises the code path its label claims, and the file's
own claims about `SdlGpuGraphicsBackend`'s construction order, cycle-flag mapping, and
`RegisterForWindow` safety were all independently confirmed against the current production source.
One real test-quality gap (F1: Check F/G's own header comment over-claims "round-trips the exact
byte data," but the assertion never reads the data back). Separately, this audit's required
cross-backend check of this backend's own shaders (per `AUDIT_CROSS_CUTTING_FINDINGS.md`) found two
HIGH-severity, already-confirmed-in-other-backends defects genuinely present in this backend's own
`EnvironmentMapEffect`/`SkinnedEffect` shaders (F2, F3) — neither is exercised by this file (which
never touches those effects), but both are real, current, and worth surfacing here since this file
is this shard's whole-backend "first proof gate."

## Checklist Results

### API / XNA / FNA parity

N/A directly — every symbol under test in this file (`GetWindowInternal`, `GetRendererInternal`,
`GetViewportSize`, `CreateVertexBuffer`/`CreateIndexBuffer16`'s returned `IVertexBufferBackend`/
`IIndexBufferBackend`) is a CNA-internal backend seam (`include/CNA/Internal/Backends/Common/
IGraphicsBackend.hpp`), not part of the XNA-facing `Microsoft::Xna` API surface. `Color::
CornflowerBlue` and `ClearOptions::Target|DepthBuffer|Stencil` (line 109-110) are the correct XNA
symbols for a full clear.

### Behavioral correctness

- Check A/B/C (lines 75-81): `GetWindowInternal()`/`GetRendererInternal()` are trivial accessors
  (`SdlGpuGraphicsBackend.hpp:980,982`) — confirmed `window_` is set at construction (never null,
  guarded by an `invalid_argument` throw at construction, line 494-495) and `GetRendererInternal()`
  is a hardcoded `return nullptr;` (this backend never creates an `SDL_Renderer`, matching its own
  `SDL_GPUDevice`-only design). `GetViewportSize()` (line 1052-1057) derives from
  `ComputeLogicalViewport()`, which returns `physicalWidth_`/`physicalHeight_` directly when
  `presentationMode_ == NativeBackBuffer` (the default here, since the test never calls
  `SetVirtualResolution`/`SetPresentationMode`) — `physicalWidth_`/`physicalHeight_` are set from a
  real `SDL_GetWindowSizeInPixels()` call at construction time (lines 533-537), so Check C's
  `width > 0 && height > 0` genuinely exercises a real window query, not a hardcoded default.
- Check D (lines 83-92): `CreateVertexBuffer(3)`/`CreateIndexBuffer16(3)` return real
  `std::unique_ptr<IVertexBufferBackend>`/`<IIndexBufferBackend>` wrapping
  `SdlGpuVertexBufferBackend`/`SdlGpuIndexBufferBackend` (`CreateVertexBuffer`/`CreateIndexBuffer16`,
  lines 1245-1253) — `SetData()`/`SetData16()` genuinely upload via a transfer buffer + copy pass
  (traced `SetDataWithOptions` lines 4658-4728, `Upload` lines 4756-4825) before `vertexCount_`/
  `indexCount_` are updated, so `GetVertexCount()==3`/`GetIndexCount()==3` reflects a real completed
  upload, not an early-set field.
- Check E (60-frame loop, lines 109-121): every frame calls `dev.Clear(...)`; the automatic
  post-`Draw()` `Present()` (documented at lines 112-113, confirmed at `GraphicsDevice`'s own
  `Tick()`/`EndDraw()` call site — not re-verified line-by-line here as it is outside this backend
  and outside this batch's scope) routes to `SdlGpuGraphicsBackend::Present()` →
  `EnsureFrameRendered()` (line 1008-1013), which genuinely acquires a command buffer, waits for and
  acquires the real swapchain texture, issues a render pass, and submits — an uncaught exception at
  any point during 60 real frames would terminate the process before this check's own `Exit()` call,
  so "no crash" here is a meaningful proof, not a triviality.
- Check F/G (lines 94-106): see F1 — the assertions genuinely exercise `SetDataWithOptions`/
  `SetData16WithOptions`'s real `Discard`/`NoOverwrite` code paths (confirmed non-default overrides,
  not `IGraphicsBackend`'s ignore-and-delegate default — `SdlGpuGraphicsBackend.hpp:360-361,
  394-397`), but the assertion itself is weaker than the header comment claims.

### Logic

`SdlGpuVertexBufferBackend::SetDataWithOptions` (line 4661): `const bool cycle = (options !=
SetDataOptions::NoOverwrite);` — `Discard`/`None` both map to `cycle=true` (orphan to a fresh
backing resource), `NoOverwrite` maps to `cycle=false` (in-place), passed straight to
`SDL_UploadToGPUBuffer(copyPass, &source, &destRegion, cycle)` (line 4716). This is exactly what the
file's own header comment (lines 17-24) and the class's own Doxygen comment
(`SdlGpuGraphicsBackend.hpp:351-359`) claim — independently confirmed correct, not merely
internally self-consistent.

### C++ correctness

`destRegion`/`destination` in both `SetDataWithOptions` and `Upload` are default-constructed
(`SDL_GPUBufferRegion destRegion{};`) with no explicit `.offset` field set, so every upload — cycle
or not — always targets byte offset 0 of the buffer (a full-buffer replace). This matches the
`IVertexBufferBackend`/`IIndexBufferBackend` interface contract (`IGraphicsBackend.hpp:64-131`),
which has no offset parameter at all — not a gap introduced by this backend.

### Robustness

The constructor throws `std::invalid_argument` for a null window (line 494-495) and
`std::runtime_error` with `SDL_GetError()` context for every fallible SDL_gpu call in the
construction sequence (`SDL_CreateGPUDevice` line 509-511, `SDL_ClaimWindowForGPUDevice` line
513-519, with proper `SDL_DestroyGPUDevice` cleanup on the latter's failure) — this file's Check A/B
implicitly rely on construction having already succeeded by the time `Draw()` runs, which is a
reasonable assumption for a `Game`-subclass test (a construction failure would abort before `Run()`
even starts the frame loop).

### Testing

Strong for what it explicitly claims to prove (device/window/viewport plumbing, real 60-frame
Clear+Present stability, and that the `Discard`/`NoOverwrite` overrides are wired up as real, not
default, code paths). Weak on Check F/G specifically — see F1.

## Detailed Findings

### F1 — Check F/G's header-comment claim ("round-trip the exact byte data either way") is not what the assertion verifies; only vertex/index *count* is checked

- Severity: MEDIUM
- Confidence: HIGH (read both the test's assertion and the production class it targets)
- Category: test-coverage / correctness-of-test
- Location/symbol: `sdlgpu_smoke_test.cpp:94-106` (the `check(vb->GetVertexCount() == 3, …)`/
  `check(ib->GetIndexCount() == 3, …)` calls after the `Discard`/`NoOverwrite` `SetDataWithOptions`/
  `SetData16WithOptions` calls); header comment lines 17-24 ("round-trip the exact byte data either
  way").
- Evidence: `vb`/`ib` are `std::unique_ptr<IVertexBufferBackend>`/`<IIndexBufferBackend>` (the
  interface types returned by `CreateVertexBuffer`/`CreateIndexBuffer16`,
  `IGraphicsBackend.hpp:744,749`), which expose only `GetVertexCount()`/`GetIndexCount()` — the
  concrete `SdlGpuVertexBufferBackend`/`SdlGpuIndexBufferBackend` classes each additionally expose a
  `NOXNA ShadowData()` accessor holding "a CPU-side copy of the most recent upload"
  (`SdlGpuGraphicsBackend.hpp:368-373,403-404`) specifically for a caller to verify uploaded bytes —
  but the test never `static_cast`s to the concrete type to call it (unlike line 71's own
  `static_cast<SdlGpuGraphicsBackend&>(dev.GetBackend())` for the backend object itself, which shows
  the file's author was aware this pattern is available). The actual assertions
  (`vb->GetVertexCount() == 3`, `ib->GetIndexCount() == 3`) only prove the vertex/index *count*
  survived three sequential uploads (`Discard` then `NoOverwrite`) — they say nothing about whether
  `vertsDiscard`'s or `vertsNoOverwrite`'s actual float payload landed correctly in the GPU buffer.
- Why it matters: a regression that flipped the `cycle` boolean's sense (e.g., accidentally cycling
  on `NoOverwrite` and not on `Discard`), or one that computed `sizeBytes`/`allocSizeBytes` wrong for
  one of the two paths, or one that left `destRegion.size` stale from a prior call, would not be
  caught by this check at all, since neither Discard nor NoOverwrite changes vertex/index *count* —
  only content. This is the SdlGpu-shard instance of the same recurring pattern
  `AUDIT_CROSS_CUTTING_FINDINGS.md` already documents for `easygl_vertexbuffer_setdata_test.cpp`
  (capacity getters only, never checks uploaded bytes) and `bgfx_vertex_format_test.cpp`
  ("`UploadAndCheck()` never actually calls `SetData`").
- FNA/XNA comparison: N/A — this is a CNA-internal backend-seam test-authoring gap, not an XNA/FNA
  behavioral question.
- Related files: none outside this file — `ShadowData()` (the fix ingredient) already exists in the
  production header, so no production change would be needed to strengthen this check.
- Suggested future action (not implemented by this audit): after each `SetDataWithOptions`/
  `SetData16WithOptions` call, `static_cast` to the concrete backend type and compare `ShadowData()`
  byte-for-byte against the source array, the same way `sdlgpu_texture3d_test.cpp`/
  `sdlgpu_texturecube_test.cpp` in this same shard already do real byte-exact comparisons for
  textures.

### F2 — [Backend-wide, not exercised by this file] `env_map3d.frag.glsl` re-multiplies the already-combined `EmissiveColor+AmbientLightColor*DiffuseColor` term (and its baked-in `Alpha`) by `DiffuseColor` a second time

- Severity: HIGH
- Confidence: HIGH (independently re-derived the correct FNA formula from
  `Lighting.fxh`/`EffectHelpers.SetMaterialColor` and traced both the CNA-side CPU pre-combination
  and the GLSL compositing line)
- Category: correctness / FNA-parity (found via this audit's required cross-backend shader check,
  not via this file's own checks)
- Location/symbol: `src/CNA/Internal/Backends/SdlGpu/shaders/env_map3d.frag.glsl:53`
  (`vec3 litRGB = (pc.emissiveAmount.xyz + lightSum) * fragTint.rgb;`); CPU-side value supplied by
  `Microsoft::Xna::Framework::Graphics::EnvironmentMapEffect::FillGpuDrawParams()`
  (`EnvironmentMapEffect.cpp:418-426`) and forwarded verbatim by
  `FillEnvMapUniforms()` (`SdlGpuGraphicsBackend.cpp:373-380`).
- Evidence: FNA's real formula (`Lighting.fxh`'s `ComputeLights`: `result.Diffuse =
  mul(diffuse, lightDiffuse) * DiffuseColor.rgb + EmissiveColor;`, where `EmissiveColor` here is the
  *already* CPU-pre-combined `(EmissiveColor + AmbientLightColor*DiffuseColor)*Alpha` from
  `EffectHelpers.SetMaterialColor`) only multiplies the **directional-light sum** by `DiffuseColor`;
  the emissive/ambient term is added **unscaled**. CNA's own `EnvironmentMapEffect::
  FillGpuDrawParams()` correctly reproduces this pre-combination
  (`p.emissiveColor[i] = (emissiveColor_[i] + ambientLightColor_[i]*diffuseColor_[i]) * alpha_;`,
  with its own comment confirming "matches FNA") — and `FillEnvMapUniforms`'s own comment even
  states *"no extra alpha handling needed"* on the C++ side, correctly expecting the shader to use
  `emissiveAmount` unscaled. But `env_map3d.frag.glsl`'s compositing line instead computes
  `litRGB = (emissiveAmount + lightSum) * fragTint.rgb` (`fragTint.rgb == pc.diffuseColor.rgb ==
  diffuseColor_*alpha_`) — i.e. `(emissive+ambient*diffuse)*alpha + lightSum` is multiplied by
  `diffuse*alpha` a **second** time, producing `emissive*diffuse*alpha²` and
  `ambient*diffuse²*alpha²` terms that should not exist at all (emissive should never be scaled by
  diffuse; ambient*diffuse should not be squared; alpha should not be squared).
- Why it matters: this is the SdlGpu instance of the cross-cutting "`EnvironmentMapEffect`'s
  fragment shader re-multiplies `EmissiveColor` by `DiffuseColor`" bug
  `AUDIT_CROSS_CUTTING_FINDINGS.md` already confirmed in Bgfx (5 test files) and suspected in Vulkan
  — SdlGpu is now a third confirmed instance, and uniquely also squares the alpha term (a variant
  not previously documented for Bgfx). It is masked in this backend's own CTest-registered
  `sdlgpu_envmap_test.cpp` for the identical documented reason: that file sets
  `EmissiveColor=(0.5,0.5,0.5)` (line 112) but never sets `DiffuseColor` away from its XNA default of
  `Vector3.One`/`Alpha=1` — multiplying by 1 (even squared) has zero observable effect, so the bug is
  fully invisible to that test despite it deliberately varying `EmissiveColor`.
- FNA/XNA comparison: confirmed wrong against `FNA/src/Graphics/Effect/StockEffects/HLSL/
  Lighting.fxh` and `EffectHelpers.cs`'s `SetMaterialColor` (both read directly for this audit).
- Related files: `src/CNA/Internal/Backends/Bgfx/…` (already-confirmed sibling instance per
  `AUDIT_CROSS_CUTTING_FINDINGS.md`), `examples/sdlgpu_envmap_test.cpp` (the CTest that should catch
  this but currently cannot, given its own `DiffuseColor`/`Alpha` defaults).
  **Also flagged for reconciliation**: `audit/examples/sdlgpu_rendertargetcube_test.cpp.audit.md`
  (an existing sibling report in this same shard) states *"This backend has no
  `EnvironmentMapEffect` yet (SDLGPU-33, deferred per the file's own header comment, confirmed still
  true)"* — this is factually incorrect for the current tree: `env_map3d.vert.glsl`/`.frag.glsl`,
  `SdlGpuGraphicsBackend::QueueEnvMapDraw()`/`CreateEnvMapResources()`, and a fully CTest-registered
  `examples/sdlgpu_envmap_test.cpp` (`SdlGpu_EnvMap`, `cmake/Tests/SdlGpuTests.cmake:70-73`) all
  exist and are wired up (confirmed via `git log`: `feat(plans/plan_sdlgpu.md): close SDLGPU-33 -- SDL_GPU
  EnvironmentMapEffect`, commit `7a078d06`/`3fe53c05`, 2026-07-15 — the same day as, and
  chronologically after, that report's own `SDLGPU-36` RenderTargetCube commit
  `3d248aa7`/`68c6fd33`). Worth a synthesis-pass correction to that report.
- Suggested future action (not implemented by this audit): change line 53 of
  `env_map3d.frag.glsl` to `vec3 litRGB = lightSum * fragTint.rgb + pc.emissiveAmount.xyz;` (add the
  emissive/ambient term unscaled, matching `Lighting.fxh` exactly), and add an
  `sdlgpu_envmap_test.cpp` check that varies `DiffuseColor`/`AmbientLightColor` away from their
  defaults so a regression would actually be observable.

### F3 — [Backend-wide, not exercised by this file] `skinned3d.vert.glsl` transforms the vertex normal by the bone-skin matrix alone, with no `WorldInverseTranspose` contribution

- Severity: HIGH
- Confidence: HIGH (matches an already-independently-confirmed systemic pattern; the shader's own
  comment explicitly acknowledges the omission)
- Category: correctness / FNA-parity (found via this audit's required cross-backend shader check)
- Location/symbol: `src/CNA/Internal/Backends/SdlGpu/shaders/skinned3d.vert.glsl:71-75`
  (`fragNormal = normalize(mat3(skinMat) * inNormal);`)
- Evidence: the shader's own comment states verbatim: *"The normal is transformed by the skin
  matrix alone, with no additional World normal-matrix contribution -- mirrors
  `VulkanGraphicsBackend`'s own `skinned3d.vert.glsl` exactly (an established simplification already
  shared by every backend implementing `SkinnedEffect` in this codebase, not something introduced
  here)."* Contrast with this same backend's own `lit_textured3d.vert.glsl:48`
  (`mat3 normalMatrix = transpose(inverse(mat3(lp.world))); fragNormal = normalize(normalMatrix *
  inNormal);`), which correctly composes the object's World-space normal matrix for `BasicEffect`'s
  own lit path — `SkinnedEffect`'s vertex shader in this same backend does not do the analogous
  composition with the skin matrix at all.
- Why it matters: this is now a fourth/fifth confirmed backend instance (EasyGL, WebGPU, Vulkan per
  `AUDIT_CROSS_CUTTING_FINDINGS.md`, now SdlGpu) of the same systemic `SkinnedEffect` defect — any
  skinned model rendered with a non-identity/non-uniform-scale `World` transform will receive
  incorrectly-lit normals (rotation/scale from `World` never applied to the normal, only the
  per-bone skin transform). Invisible in every test that uses `World=Identity`, which is the
  established masking mechanism this same cross-cutting entry already documents for the other three
  backends.
- FNA/XNA comparison: `SkinnedEffect.fx`'s real lighting path routes through the same
  `Lighting.fxh`/`WorldInverseTranspose` mechanism as `BasicEffect`; the correct normal is
  `normalize(mul(skinnedNormal, WorldInverseTranspose))`, not just the bone-skin-transformed normal.
- Related files: `EasyGLGraphicsBackend.cpp` (`EnsureSkinnedProgram`), `WebGPUGraphicsBackend.cpp`
  (`CreateSkinnedResources()`), Vulkan's `skinned3d.vert.glsl`/`skinned3d_vertexlit.vert.glsl` — all
  already cited in `AUDIT_CROSS_CUTTING_FINDINGS.md` as the same defect.
- Suggested future action (not implemented by this audit): compose a world-space normal matrix
  (forwarded from CPU, since GLSL's `inverse()` — already used by `lit_textured3d.vert.glsl` in this
  same backend — makes this straightforward here, unlike WebGPU's WGSL workaround) with the
  per-vertex skin matrix, e.g. `normalize(mat3(lp.world) * inverse-transpose applied per-bone or
  precomputed WorldInverseTranspose) * mat3(skinMat) * inNormal` depending on how the project wants
  to define "world" for a skinned mesh whose bones already carry their own transforms.

## Cross-File Observations

- **`RegisterForWindow`/constructor-ordering risk (`AUDIT_CROSS_CUTTING_FINDINGS.md`'s open
  "still need to check Canvas/SdlGpu" item) — SdlGpu does NOT share the EasyGL-confirmed risk.**
  `SdlGpuGraphicsBackend`'s constructor (lines 487-543) calls
  `IGraphicsBackend::RegisterForWindow(window_, this)` at line 539 — strictly the last fallible-order
  step, after `SDL_CreateGPUDevice`, `SDL_ClaimWindowForGPUDevice`, and all nine
  `Create*Resources()` calls (any of which can throw on shader-compile/pipeline-creation failure).
  If any of those throw, `RegisterForWindow` is never reached, so no dangling registration is left
  for `SdlInputBridge`/`Mouse` to dereference on a subsequent input event — this closes that
  cross-cutting checklist item for SdlGpu (Canvas remains unchecked).
- Fog: this backend implements **no fog at all** yet (`SdlGpuGraphicsBackend.cpp:326-327,355,383`,
  each explicitly commented "minus fog, deliberately deferred") — so the separately-confirmed
  EasyGL-fixed/Bgfx-Vulkan-still-wrong fog-formula bug does not apply to SdlGpu; there is no formula
  here to be right or wrong about yet (N/A, not a clean bill of health).
- This file and `sdlgpu_2d_test.cpp` are this shard's two "whole-backend plumbing" proofs (as
  opposed to the per-feature files); F1's weak byte-content check is worth checking against whether
  `sdlgpu_2d_test.cpp`'s own `Texture2D`/`SpriteBatch` proof has the equivalent gap.

## Missing or Weak Tests

- See F1 — Check F/G should compare `ShadowData()` byte-for-byte, not just vertex/index count.
- No check in this file (nor, per F2, in `sdlgpu_envmap_test.cpp`) varies `DiffuseColor`/`Alpha`
  away from their defaults for any lit/emissive effect on this backend — the specific gap that masks
  F2 from ever being caught by this backend's own CTest suite as currently written.

## Positive Findings

- Every one of Checks A-E genuinely traces to a real, distinct backend code path — this is not a
  "constructs an object and checks it's non-null" smoke test dressed up as more.
- `SetDataWithOptions`/`SetData16WithOptions`'s `Discard`/`NoOverwrite` → `cycle` mapping was
  independently verified correct against `SDL_UploadToGPUBuffer`'s real parameter, matching both the
  file's header comment and the production Doxygen comment precisely.
- The constructor's `RegisterForWindow` ordering is provably safe against the exact
  register-then-throw dangling-pointer risk this audit's cross-cutting findings flagged as open for
  this backend.
- 60 real frames of `Clear()`+automatic `Present()` completing with no exception is a meaningful,
  non-trivial proof given the backend's real SDL_gpu command-buffer/copy-pass/render-pass machinery
  underneath.

## Final Assessment

A solid, accurate smoke test with one real but modest test-quality gap (F1). The more consequential
findings in this report (F2, F3) are not defects in this file itself but genuine, currently-live
production bugs in the backend it exercises, surfaced by this audit's required cross-backend shader
check — both should be tracked against the backend's `EnvironmentMapEffect`/`SkinnedEffect` shader
files rather than against this smoke test.
