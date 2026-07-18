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

- **EasyGL backend: a constructor failure after `RegisterForWindow()` but before construction completes leaves a
  dangling entry in `IGraphicsBackend`'s static window registry.** Independently discovered via direct production
  code reading (not from the test batch). `EasyGLGraphicsBackend`'s constructor calls `RegisterForWindow(window,
  this)` early, then `SDL_GL_CreateContext` can throw shortly after (a real, reachable failure mode — unsupported
  GLES3 context, headless/CI environment, driver issue). The registry entry is never cleaned up since the
  destructor never runs on a failed construction. `SdlInputBridge.cpp`/`Mouse.cpp` both dereference
  `GetForWindow()`'s result unconditionally — a subsequent mouse/input event on that window would be a
  use-after-free. **The most severe confirmed finding in this audit so far.** See
  [audit report](src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp.audit.md) F1.
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
- **EasyGL backend: `SkinnedEffect`'s shaders never apply the object's World transform to lighting normals at all
  — CONFIRMED via direct source reading** (production-code root-cause of the finding three independent test-file
  audits surfaced from the test side — see those reports below). `EnsureSkinnedProgram()`/
  `EnsureSkinnedVertexLitProgram()` transform the normal with `mat3(skinMat)*aNormal` only; neither program even
  registers a `uNormalMatrix` uniform location, unlike every non-skinned lit shader in the same file (which
  correctly uses the inverse-transpose `uNormalMatrix`, itself a documented prior fix, Task 398). Any rotated
  skinned model's lighting is wrong; invisible to every existing test because they all use `World=Identity`. See
  [audit report](src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp.audit.md) F2, and the originating test
  reports:
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
