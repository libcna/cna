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

## Recurring testing gaps

- **Documentation rot: header comments describing "known bugs"/"current limitations"/expected-throw assertions
  are not revisited once the underlying code is fixed.** Found repeatedly in the `examples-tests-easygl` batch
  (218 files) — at least 6 distinct files carry stale bug/limitation claims contradicted by since-closed tasks —
  and again in the `examples-tests-sdlrenderer` batch (67 files): `sdlrenderer_clearoptions_audit_test.cpp` and
  `sdlrenderer_rendertarget_depth_decision_test.cpp` both assert an expected-throw behavior for
  `ClearOptions`/`DepthBuffer` combinations that a later FNA-parity fix (commit `90f5db2c`) deliberately changed to
  silently-masked-and-degrade instead — the tests were never updated to match. **This is now confirmed across two
  independent mechanical-batch passes, strengthening the case that this is a systemic gap in this codebase's
  process** (fixing behavior without a corresponding sweep of test/comment claims that describe the old behavior),
  not incidental to any one subsystem. (Vulkan blend state "almost
  entirely fake," `SetReferenceStencil` claimed universally missing, anisotropic filtering bugs claimed open,
  `EnvironmentMapEffect`'s pre-fix shader formula documented instead of the current one, `GetData()` claimed
  unimplemented). None of these are currently-live production bugs — the underlying code was actually fixed in
  each case — but the stale comments actively mislead a future reader (including future audit passes) into
  believing a fixed issue is still open, or vice versa risk under-trusting a test that's actually fine. Recommend
  (not implemented by this audit) a periodic sweep specifically for "Task NNN"/"known bug"/"currently broken"-style
  comments cross-checked against `git log`/current source, independent of any one file's own audit.
- **Tests asserting metadata/capacity instead of actual data content or actual code-path execution**: a recurring
  shape across the EasyGL example-test shard — `easygl_vertexbuffer_setdata_test.cpp` (capacity getters only, never
  checks uploaded bytes), `easygl_dynamic_buffer_stress_test.cpp` (index-buffer half never actually draws
  indexed), `easygl_msaa_test.cpp` (scene can't distinguish MSAA-resolved from never-engaged). Worth watching for
  the same shape in other backends' example-test shards during Pass 2.

## Build-system inconsistencies

_(pending)_
