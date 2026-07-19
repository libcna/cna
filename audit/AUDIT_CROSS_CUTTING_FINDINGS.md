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

- **Architecture-level, ALL backends: `IGraphicsBackend::ApplySamplerState()`'s signature never carries an
  `AddressW` parameter at all** (`include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp:656-658`:
  `virtual void ApplySamplerState(int slot, int filter, int addressU, int addressV, int maxAnisotropy) {}`) —
  found while auditing `D3D11SamplerCache.cpp`, whose own comment honestly discloses "IGraphicsBackend::
  ApplySamplerState has no addressW parameter (a pre-existing interface limitation, not introduced here)" and
  works around it by reusing `addressV` for the W axis. Since this is the shared interface every backend
  implements, **`Microsoft::Xna::Framework::Graphics::SamplerState::AddressW`** (a real, documented XNA property —
  confirmed present in `SamplerState.hpp` — used for `Texture3D`/volume-texture wrapping behavior along the third
  axis) is silently unenforceable by *any* backend built on this interface, not just D3D11. No test found anywhere
  in this codebase exercising a scenario where `AddressW` differs from `AddressV` in a way that would surface this
  gap (a `grep` across `tests/`/`examples/` for Texture3D-sampler-address content found nothing). This is an
  interface-level defect no individual backend can fix on its own — worth flagging for `IGraphicsBackend.hpp`'s own
  audit and for whichever backend's Texture3D support gets audited first.
  **UPDATE: this is one instance of a broader recurring shape — `IGraphicsBackend`'s `Apply*State()` methods
  consistently omit several fields the real D3D11/XNA state descriptions support**, found while auditing
  `D3D11StateObjectCache.cpp`: `ApplyBlendState()` carries no per-render-target color write mask (every D3D11
  blend state is created with `D3D11_COLOR_WRITE_ENABLE_ALL` regardless of XNA's real, settable
  `BlendState.ColorWriteChannels`), and `ApplyRasterizerState()` carries no `MultiSampleAntiAlias` flag (every
  D3D11 rasterizer state hardcodes `MultisampleEnable = FALSE`, though this is lower-impact since it only affects
  line/point AA algorithm selection, not MSAA render-target sampling itself, which is controlled independently via
  `DXGI_SAMPLE_DESC`). All three gaps (`AddressW`, color-write-mask, `MultiSampleAntiAlias`) are honestly
  self-disclosed in the D3D11 backend's own source comments as **pre-existing `IGraphicsBackend` interface
  limitations, not something this backend introduced or can fix unilaterally** — but since every backend
  implements the same shared interface, `BlendState.ColorWriteChannels`/`SamplerState.AddressW` (both real,
  documented, settable XNA properties) are likely silently unenforceable across *all* backends, not just D3D11.
  Priority check when `IGraphicsBackend.hpp` and `xna-graphics`'s `BlendState`/`SamplerState`/`RasterizerState`
  are directly audited: confirm whether any backend actually threads these fields through some other path this
  audit hasn't yet found, or whether they are genuinely dead XNA-facing properties across the whole project.

- **Architecture-level, likely multi-backend: `IVertexBufferBackend`/`IIndexBufferBackend::SetDataWithOptions()`
  has no destination-offset parameter, so every backend's `SetDataOptions::NoOverwrite` path always overwrites
  the exact same `[0, byteCount)` region a prior `Discard`/`None` write already used** — found while auditing
  `D3D11Buffers.cpp`, then independently confirmed the identical shape in `EasyGLGraphicsBackend.cpp`'s own
  `uploadWithOptions()` (`NoOverwrite` also calls `set_sub_data(..., data, byte_count, 0)`, offset 0, same as
  every other path). Real streaming (writing new data into an as-yet-unused portion of a larger buffer while the
  GPU still consumes an earlier portion, the scenario `NoOverwrite` exists for) is architecturally impossible at
  this interface level — every call replaces the same bytes. This makes D3D11's specific implementation a
  **plausible (not confirmed-reproduced) synchronization risk**: `D3D11_MAP_WRITE_NO_OVERWRITE` is a hard promise
  to the driver that the mapped range isn't being read by any pending GPU work; if a caller issues
  `SetDataWithOptions(..., NoOverwrite)` and a prior draw using the same buffer's identical byte range hasn't yet
  completed on the GPU, the CPU write could race a still-in-flight read, since D3D11 (unlike `Discard`) will not
  rename the backing resource to avoid this. Not independently reproduced or traced to a concrete call site in
  this pass — flagged as a priority check for whichever consumer(s) actually call `SetDataWithOptions` with
  `NoOverwrite` in a same-frame multi-draw streaming pattern (e.g. `SpriteBatch`'s dynamic vertex buffer, if it
  uses this path) when `xna-graphics`/`tests-xna-graphics` are audited.
  **UPDATE — 3rd confirmed instance, found while auditing `backend-d3d9`: `D3D9VertexBufferBackend::Upload()`
  calls `buffer_->Lock(0, byteCount, &locked, LockFlagsFor(options))` — hardcoded offset 0, identical shape to
  D3D11/EasyGL.** `D3DLOCK_NOOVERWRITE` carries the same driver contract as D3D11's `D3D11_MAP_WRITE_NO_OVERWRITE`
  (a promise the mapped range isn't being read by pending GPU work), so the same plausible-but-unreproduced
  synchronization risk applies here too if any caller streams into this buffer across multiple same-frame draws.
  3 independent backends (D3D11, EasyGL, D3D9) now share this exact interface-level gap — strong evidence this
  is a project-wide `IVertexBufferBackend`/`IIndexBufferBackend` interface limitation, not a per-backend
  implementation oversight; every future backend audit should check this same call chain by default.

- **Recurring shape across 2 backends, 2 resource types: mip regeneration for a cube resource always touches
  ALL 6 faces, even when only one face's content actually changed.** First found in SdlGpu's own
  `TextureCube::SetData()` path (`sdlgpu_texturecube_test.cpp`'s audit: "any full-level-0 `SetData()` on one face
  ... regenerates mip chains for all 6 faces"). **Now confirmed in a second backend and a second resource type**:
  `D3D11RenderTargetCubeBackend::UnbindAsRenderTarget()` unconditionally calls `GenerateMips(srv_.Get())` on the
  whole cube SRV whenever `mipMap_` is true, after rendering to only the single face `BindAsRenderTargetFace()`
  most recently bound — regenerating mip chains for the other 5 faces from whatever content they currently hold
  (potentially still-uninitialized data, if not every face has been rendered to yet in a typical
  render-one-face-at-a-time cube-map-generation workflow). Not a hard crash/correctness bug in the single-face
  case this project's own tests exercise (each test only ever fully populates one face before checking it), but a
  real risk for any genuine multi-face cube-map-generation workflow, and a needless performance cost (5/6 of the
  `GenerateMips()` work is wasted) even when correctness isn't at stake. Worth checking every other backend's
  `RenderTargetCube`/`TextureCube` mip-regeneration trigger for the same "whole-resource, not per-face" shape.
  **UPDATE — genuine positive counter-example found: `D3D12RenderTargetCubeBackend::GenerateMipsEXT()` correctly
  regenerates mips for ONLY the actually-drawn-to face** (`face = activeFace_`, correct per-face subresource
  indexing `level + face*levelCount_`), despite D3D12 otherwise sharing almost every other cross-backend finding
  with its sibling D3D11. A useful reminder that even closely-related sibling backends can diverge on specific
  implementation details — don't assume a finding transfers without checking.

- **Vulkan-specific: `VulkanSpriteBatchBackend` never overrides `SetTransformMatrix()` at all** (confirmed by an
  exhaustive `grep` across every `.hpp`/`.cpp` file in `src/CNA/Internal/Backends/Vulkan/` and
  `include/CNA/Internal/Backends/Vulkan/` — zero matches), so it falls through to
  `IGraphicsBackend::SetTransformMatrix()`'s shared no-op default. Found while auditing D3D11's own
  `D3D11SpriteBatchBackend`, whose header comment explicitly claims this as "one real, deliberate improvement"
  over "VulkanSpriteBatchBackend... leaves it a silent no-op" — independently verified true. **Practical impact:
  any game calling `SpriteBatch.Begin(transformMatrix: someMatrix)` (the standard XNA idiom for camera-relative 2D
  rendering, e.g. scrolling a 2D world) has that transform silently discarded on Vulkan specifically** — sprites
  render as if `transformMatrix` were always Identity. **Every other backend checked correctly applies it**, via
  one of two different (both valid) mechanisms: a stateful `SetTransformMatrix()` override consumed at flush time
  (EasyGL, Bgfx, D3D9, D3D11, Canvas, Dx3, Software, Headless), or the transform threaded directly as a `Draw()`/
  `QueueSprite()` parameter instead of a separate stateful call (WebGPU, SdlGpu, and SdlRenderer — the last of
  which explicitly documents, in its own Task 675 comment, that this exact gap was found and fixed on that
  backend previously). Ascii delegates its entire `SpriteBatch` to a wrapped `SdlRenderer::SdlGraphicsBackend`
  instance, correctly inheriting that backend's already-fixed behavior. **D3D12 not yet checked** (likely mirrors
  D3D11's correct design, given the pattern established elsewhere in this shard, but not confirmed).
  No test found anywhere in this codebase exercising a non-Identity `SpriteBatch.Begin(transformMatrix)` on
  Vulkan specifically that would have caught this.

- **HIGH, D3D12-specific: `StencilState` (all fields) and `RasterizerState.ScissorTestEnable`/`DepthBias`/
  `SlopeScaleDepthBias` are completely non-functional — accepted by the public API, silently discarded before
  ever reaching a PSO.** `D3D12GraphicsBackend::ApplyDepthStencilState()` receives all 11 stencil-related
  parameters as literally-named commented-out unused parameters (`bool /*stencilEnable*/, int /*stencilFunc*/,
  ...`) and never stores or forwards any of them; `ApplyRasterizerState()` does the identical thing for
  `scissorTestEnable`/`depthBias`/`slopeScaleDepthBias`. `D3D12PipelineStateCache.cpp` confirms why: every PSO is
  built with `ds.StencilEnable = FALSE` hardcoded (line 99) and `RasterizerState.ScissorEnable` left at its
  zero-initialized `FALSE` default (never set anywhere) — so even if the C++ layer *did* track these values,
  D3D12 semantics require the bound PSO's own `ScissorEnable`/`StencilEnable` flags to gate whether the GPU
  applies scissor/stencil testing at all, and those flags are always off. `RSSetScissorRects()` **is** called at
  several sites in `D3D12GraphicsBackend.cpp` — but since the PSO's `ScissorEnable` is always `FALSE`, setting the
  rectangle has no visible effect; the call is necessary but not sufficient in D3D12's model.
  **This is a real, currently-active regression relative to `D3D11`**, whose own `D3D11DepthStencilStateCache`/
  `D3D11RasterizerStateCache` (already audited, `backend-d3d11` shard) correctly implement full dynamic stencil
  (including two-sided mode) and scissor support. Any XNA feature relying on stencil-buffer techniques (mirrors,
  decals, shadow volumes, outline effects, per-pixel clipping via the stencil buffer) or `ScissorRectangle`-based
  clipping (a very common technique for UI/viewport masking) silently does nothing on this D3D12 backend.
  **Honestly, if quietly, disclosed** in both files' own comments as a deliberate first-implementation scope cut
  (DX-107/DX-118's own plan rows), not a hidden defect — but the practical severity (2 real, commonly-used XNA
  features completely non-functional) makes this one of the most significant single-backend findings in this
  audit. No test found exercising `ScissorRectangle`/`StencilState` on D3D12 that would surface this (unsurprising
  given this codebase currently has no Windows-native CI for D3D12 per D-P4).
  **UPDATE — positive cross-backend contrast confirmed: SdlGpu does NOT share this gap.**
  `SdlGpuGraphicsBackend::ApplyDepthStencilState()`/`ApplyRasterizerState()` correctly track all stencil fields
  and `scissorTestEnable`, and both are confirmed genuinely consumed — `scissorEnabled_` gates a real
  `SDL_SetGPUScissor()` call, and the full stencil parameter set is threaded into `CaptureRenderState()`'s
  `RenderStateSnapshot`, which drives real pipeline selection. SdlGpu's only narrower, honestly-disclosed gap in
  this area is `DepthBias`/`SlopeScaleDepthBias` ("stored but deliberately not yet applied," since SDL_GPU has no
  per-draw-dynamic depth-bias equivalent to Vulkan's `vkCmdSetDepthBias`) — a real but much narrower gap than
  D3D12's complete Stencil+Scissor non-functionality.

- **MEDIUM-HIGH, D3D12-specific: `OcclusionQuery` only captures the LAST draw call when multiple draws occur
  between `Begin()`/`End()`, not the cumulative/combined total XNA's real semantics require.**
  `D3D12GraphicsBackend`'s draw-recording methods (`DrawColoredPrimitives`, `DrawIndexedColoredPrimitives`,
  `DrawPrimitivesExImpl`, `DrawInstancedPrimitivesEx` — confirmed via grep, all 4) each independently wrap their
  own single command-list submission in its own `BeginQuery`/`EndQuery` pair on the same query-heap slot
  (index 0) whenever an occlusion query is active. Since a D3D12 query-heap slot holds only one result at a time,
  issuing a 2nd draw's `BeginQuery` before the 1st draw's result is resolved **overwrites** the 1st draw's
  captured samples — so a real XNA usage pattern (multiple draws between one `OcclusionQuery.Begin()`/`.End()`
  pair, expecting the combined/total visible-sample count across all of them) silently returns only the last
  draw's count on this backend. `D3D12OcclusionQueryBackend.cpp`'s own `Begin()` comment self-discloses this is
  "correct for exactly one draw call between Begin()/End()" and refers the reader to "this class's own header doc
  comment for the multi-draw gap" — **but the referenced header (`D3D12OcclusionQueryBackend.hpp`) does not
  actually document this gap anywhere** (confirmed via grep — zero matches for "multi-draw" or similar in that
  file), a documentation-cross-reference inconsistency on top of the real limitation itself. The same `.cpp`
  comment also discloses a genuinely useful empirical finding: `BeginQuery`/`EndQuery` **must** be recorded within
  the same command-list submission as the draw(s) they bracket (a Vulkan/vkd3d-proton requirement) — confirmed via
  the comment's own account of reproducing a real bug (`PixelCount()` reporting 0 for a visible full-viewport
  triangle) before this constraint was understood and fixed.

- **HIGH, SdlGpu-specific: fog is completely unimplemented across all 10 stock-effect shader families — not a
  wrong formula, a total absence.** Confirmed exhaustively: `grep`ing every one of the 23 `.glsl` files in
  `src/CNA/Internal/Backends/SdlGpu/shaders/` for real fog identifiers (`fogFactor`/`FogColor`/`fogEnabled`/
  `fogStart`/`fogEnd`) returns **zero matches** — not even in `skinned3d.vert.glsl`, the shader most likely to
  carry it given every other backend's equivalent does. `colored3d.vert.glsl`'s own header comment confirms this
  is deliberate: "No fog (deliberately deferred, same as this codebase's WebGPU backend's own initial 3D vertical
  slice)." Cross-checked `SdlGpuGraphicsBackend.cpp` itself for any `fogColor`/`fogEnabled`/`fogStart`/`fogEnd`
  reference — also zero matches, confirming the gap runs top-to-bottom (the C++ layer never even attempts to
  send fog parameters, let alone the shader consuming them incorrectly). **This makes SdlGpu a categorically
  different (and arguably more severe) instance than the already-confirmed mirrored-fog-formula bug shared by
  Bgfx/Vulkan/D3D11+D3D12** — those backends at least attempt fog, just with the wrong formula (so `FogEnabled`
  visibly does *something*, just the wrong distance falloff); on SdlGpu, `GraphicsDevice.FogEnable`/
  `BasicEffect.FogEnabled`/`FogColor`/`FogStart`/`FogEnd` have **zero visible effect at all** — any XNA game
  relying on fog (distance culling, atmospheric effects, a common technique) renders identically whether fog is
  enabled or not. No test found anywhere in `examples-tests-sdlgpu` (already audited this session) that would
  have caught this, since none of those 22 files exercise fog on this backend.

## Duplicated backend logic

_(pending — revisit once more backends are audited)_

## Recurring memory/resource risk patterns

- **CONFIRMED, 2 instances: `FileDialog.cpp` and `MessageBox.cpp` (both in `cna-devices`) share an identical
  mutex-scoping mistake that creates a real use-after-free window.** Both files implement the exact same
  "swappable global backend, for test injection" pattern via a private `GetBackend()` helper:
  ```cpp
  IFileDialogBackend* GetBackend() {
      std::lock_guard<std::mutex> lock(BackendMutex());
      return BackendStorage().get();   // <-- lock released HERE, raw pointer returned
  }
  ```
  Every public entry point then calls `GetBackend()->ShowOpenFile(...)` (or `->Show(...)`/`->ShowSimple(...)`
  for `MessageBox`) — the mutex is released the instant `GetBackend()` returns, **before** the returned raw
  pointer is actually dereferenced and used. If `SetBackendForTesting()` runs on another thread between the
  pointer's retrieval and its use, the old backend object (owned by a `unique_ptr` that `SetBackendForTesting()`
  just reassigned, destroying the previous object) is deleted while the first thread is still calling through
  the now-dangling pointer — a genuine use-after-free, not just a theoretical data race. The mutex correctly
  protects the `unique_ptr`'s own read/write, but not the pointee's lifetime across the subsequent virtual
  call. Both classes' own doc comments frame `SetBackendForTesting()` as test-only, single-threaded-setup
  usage (mitigating real-world likelihood), but the synchronization as written does not actually guarantee
  that usage pattern — a test suite that runs cases in parallel (or any future concurrent production use)
  could trigger it. The fix shape (not applied, per this audit's no-development rule) would be either holding
  the lock for the duration of the actual backend call, or making `BackendStorage()` a `shared_ptr` and
  returning/holding a local copy across the call so the object's lifetime is extended past the lock's own
  scope. **`SystemTray`/`Camera` do NOT share this bug** — both use per-instance constructor-injected backends
  (no global swappable state), a structurally different and safer design for the same "inject a fake for
  testing" goal.
- **Related, LOWER-severity pattern found while auditing `cna-internal-core`'s `CNA::Internal::Input`
  subsystem: every `System*Backend`/`Sdl*Backend` seam (8 confirmed instances — `SystemDeviceBackend`,
  `SystemKeyboardBackend`, `SystemMouseBackend`, `SystemPowerBackend`, `SystemSensorBackend`,
  `SdlGamepadBackend`, `SdlHapticBackend`, `SdlJoystickBackend`) uses a plain, entirely unsynchronized
  global raw-pointer swap for its own test-injection hook** (`g_currentBackend = backend ? backend :
  &g_realBackend;`, no mutex at all). This is a distinct pattern from the `FileDialog`/`MessageBox` bug
  above, and arguably lower-risk in practice: there is no mutex creating a false sense of protection —
  the code is honestly, visibly unsynchronized by inspection, consistent with the documented intent (a
  test-setup-only call, never expected to race a real read). Not flagged as a defect on its own, but
  recorded because it's the same *shape* of "global swappable test backend" as the confirmed bug above,
  just missing the ingredient (a broken-but-present mutex) that makes that one actively dangerous.

## Recurring performance risk patterns

- **D3D12-specific: every `SetData`/`SetDataWithOptions()` call on a vertex/index buffer performs a full
  synchronous GPU stall (create an UPLOAD-heap staging buffer, copy, submit, wait on a fence), regardless of the
  `SetDataOptions` hint.** `D3D12VertexBufferBackend::SetDataWithOptions()` takes `SetDataOptions /*options*/` as
  a literally-unused parameter — `Discard`/`NoOverwrite`/`None` are all treated identically. Unlike D3D11/EasyGL
  (which at least attempt a `Map`/`Unmap`-based no-stall path even though the destination-offset architecture gap
  limits its real benefit — see the `NoOverwrite` entry above), D3D12 never even attempts to avoid the stall.
  Correctness is unaffected (the uploaded data is always right), but this is a real, confirmed performance
  regression relative to every other backend for per-frame dynamic-buffer-heavy workloads (e.g. `SpriteBatch`,
  particle systems). A reasonable, disclosed first-implementation simplification (matches this backend's other
  "everything is synchronous for now" choices, e.g. `D3D12OcclusionQueryBackend`'s own explicit rationale), not a
  silently-introduced defect — but worth flagging for a future performance-focused pass on this backend.

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
  **UPDATE (direct, exhaustive source read of every fog-capable shader in the shared `D3DCommon` directory —
  compiled into BOTH D3D11 and D3D12 — ahead of the `backend-d3d11`/`backend-d3d12`/`backend-d3dcommon` shard
  audits): ALL 15 of D3DCommon's 15 fog-capable vertex shaders share the identical mirrored formula**,
  `(FogEnd - z) / (FogEnd - FogStart)` in one of two equivalent spellings (`(FogStartEnd.y - input.Position.z) /
  max(FogStartEnd.y - FogStartEnd.x, 1e-6)` or, for the two alpha-test shaders, `(FogEnd - input.Position.z) /
  max(FogEnd - FogStart, 1e-6)`) — confirmed via `grep` across every `*.vert.hlsl` file in
  `src/CNA/Internal/Backends/D3DCommon/shaders/`: `colored3d`, `colored_textured3d`, `textured3d`, `dual_texture3d`,
  `lit_textured3d`, `lit_textured3d_vertexlit`, `env_map3d`, `pbr3d`, `skinned3d`, `skinned3d_vertexlit`,
  `skinned_colored3d`, `skinned_colored3d_vertexlit`, `pbr_skinned3d`, `alpha_test3d`, `alpha_test_colored3d` (only
  `sprite2d`/`instanced3d` have no fog term at all — consistent with FNA, which doesn't fog sprites). **This makes
  D3D11+D3D12 the *most completely affected* backend-group found so far for this bug — not a handful of shaders
  like Bgfx/Vulkan's partial coverage, but literally every fog-capable shader in the shared source.**
  `skinned3d.vert.hlsl`'s own header comment claims the formula "matches EasyGL/Bgfx's established SkinnedEffect
  fog formula exactly" and `alpha_test_colored3d.vert.hlsl`/`env_map3d.vert.hlsl` similarly cite "the established
  Task 888 formula" — **both claims are false**: EasyGL's real formula is the corrected, post-Task-1111 one; only
  Bgfx's (and apparently whatever Task 888 originally established) is the mirrored/wrong one these comments
  actually match. This is revealing as a likely propagation mechanism: a later port copied whichever prior
  instance was most convenient while believing, incorrectly, that it agreed with EasyGL's (since-fixed) version,
  rather than re-deriving the formula from FNA. **Raises the confirmed count to 3 backend-groups at the
  shader-source level: Bgfx, Vulkan, and D3D11+D3D12 (shared D3DCommon source) — with D3D11+D3D12 now the
  widest/most complete instance of the three.**
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
  (visible one file away) and used the wrong one anyway for the skinned variant.
  **UPDATE (exhaustive read of every D3DCommon vertex shader that computes a lighting normal): `skinned3d_vertexlit.vert.hlsl`
  DOES share the identical complete-omission bug** (`float3 N = normalize(mul(input.Normal, (float3x3)skinMat))`,
  line 75 — plus it independently shares the same mirrored fog formula, see above). Two more sibling skinned
  shaders confirmed the same way: `skinned_colored3d.vert.hlsl` (`output.Normal = normalize(mul(input.Normal,
  (float3x3)skinMat))`) and `skinned_colored3d_vertexlit.vert.hlsl` (`float3 N = normalize(mul(input.Normal,
  (float3x3)skinMat))`) — **so all 4 of D3DCommon's non-PBR skinned vertex shaders share the complete-omission
  variant, and the 1 PBR-skinned shader shares the raw-World variant: 5 for 5, no exceptions.** Crucially, this
  same directory contains the control group proving the D3DCommon author knew the correct convention and only got
  skinning wrong: all 3 **unskinned** lit vertex shaders — `lit_textured3d.vert.hlsl`, `pbr3d.vert.hlsl`,
  `lit_textured3d_vertexlit.vert.hlsl` — correctly compute `float3x3 normalMatrix =
  InverseTranspose3x3((float3x3)World)` and use it. This is the cleanest evidence yet in this audit that the bug
  is specifically "skinning code forgets to compose the outer world-space normal matrix," not a general
  unfamiliarity with the inverse-transpose convention.
  **This raised the confirmed-at-shader-source-level count to 5 of 14 backends: EasyGL, WebGPU, Vulkan, SdlGpu,
  D3D11+D3D12 (shared D3DCommon)** — only Bgfx's *own* skinned shader source remained unconfirmed at that time.
  **RESOLVED — Bgfx's `vs_skinned3d.sc` directly confirmed the pattern**: line 27-29,
  `v_normal = normalize(skinMat[0].xyz * a_normal.x + skinMat[1].xyz * a_normal.y + skinMat[2].xyz * a_normal.z)`
  — literally `mat3(skinMat) * a_normal` spelled out component-wise (BGFX shading language has no built-in
  `mat3`-times-`vec3` shorthand the way GLSL/HLSL do) — the normal is transformed by the skin matrix alone, with
  no `u_world` contribution anywhere. **This makes Bgfx the 6th and FINAL backend confirmed at the direct
  shader-source level, meaning EVERY ONE of the 14 backends in this audit with a `SkinnedEffect` implementation
  now shares this exact defect — a complete, no-exceptions sweep.** `vs_pbr_skinned3d.sc` additionally confirms
  the narrower "raw World, not inverse-transpose" variant for `SkinnedPbrEffect`
  (`v_normal = normalize(mul(u_world, vec4(skinnedNormal, 0.0)).xyz)`, line 39) — the 6th confirmed instance of
  that variant too (after EasyGL, WebGPU, D3D9, D3D11/D3D12, SdlGpu). **`vs_pbr_skinned3d.sc`'s own header
  comment is a third, independent piece of direct evidence for this bug's cross-backend propagation mechanism**:
  it explicitly states this file "applies an EXTRA World-space normal/tangent transform after skinning (unlike
  `vs_skinned3d.sc`'s plain `mat3(skinMat)` multiply) — an intentional divergence... matching
  `EnsurePbrSkinnedProgram()`'s own documented behavior" — i.e. the author of this file was fully aware that the
  plain `SkinnedEffect` shader omits `World` entirely and made a deliberate (if still incorrect — raw `World`
  instead of inverse-transpose) choice to add *some* world-space contribution for the PBR case specifically,
  mirroring EasyGL's own already-confirmed `EnsurePbrSkinnedProgram()` bug precisely (alongside the D3D11
  "ported line-by-line from Vulkan" comment and SdlGpu's "mirrors VulkanGraphicsBackend's own skinned3d.vert.glsl
  exactly" comment, this is the third explicit, self-documented instance of deliberate cross-backend porting that
  propagated this bug family). This is now this audit's single most exhaustively-confirmed defect, alongside the
  fog-formula bug.
  **Also confirmed while reading these files: D3D's own `env_map3d.vert.hlsl` — also "ported line-by-line from
  Vulkan" per its header comment — correctly and deliberately omits Vulkan's Y-flip**, a genuine, positive,
  non-bug backend difference: a family-wide, well-documented convention across every D3DCommon 3D vertex shader
  (`colored3d.vert.hlsl`'s comment explains it most fully) states D3D's clip space already matches XNA's own
  convention, unlike Vulkan's inverted-Y NDC, so the Vulkan-specific Y-flip fix must NOT be carried over. Verified
  this holds for `env_map3d` specifically too (`textured3d`/`sprite2d`/`colored3d` were the ones whose comments
  discuss it, not actual flip code — double-checked to avoid a false read). **D3D11/D3D12 do NOT share Vulkan's
  `EnvironmentMapEffect` Y-flip bug** — worth recording precisely because it shows the porting process *can*
  correctly localize a backend-specific fix when done right, making the skinned-normal/fog-formula omissions look
  more like genuine oversights than an inability to handle backend nuance in general.
- **NEW: a *second*, distinct fog defect — "object-space-only fog" (ignores World/View for the Z used in the fog
  calculation), separate from the Task-1111 mirrored-formula bug above.** Confirmed in D3D9's own custom shaders:
  `SkinnedVertexColor3D.hlsl` (via `d3d9_skinnedvertexcolor_test.cpp`'s audit) and, per that same report, also
  `Pbr3D.hlsl`/`PbrSkinned3D.hlsl` — all compute fog from raw local-space vertex Z, never transforming it by
  World/View first, unlike this same backend's own correct `ComputeFogVectorEXT()` path used for every vendored
  stock effect. This matches a previously-recorded EasyGL memory note (`feedback_easygl_fog_object_space_only`)
  about the identical class of mistake in that backend — worth checking whether EasyGL's own non-stock shaders
  have the same issue, and treating "object-space-only fog in a CNA-original (non-vendored) shader" as its own
  distinct pattern to watch for, separate from the vendored/ported stock-effect fog-formula bug.
  **UPDATE — mechanism confirmed via direct read of `D3D9EffectDraw.cpp`: `ComputeFogVectorEXT()` (line 149) is
  a faithful, correct port of FNA's real `EffectHelpers.SetFogVector`** — it builds a per-vertex dot-product fog
  vector from the combined `World*View` matrix's own Z-row/column elements (`worldView.M13/M23/M33/M43`), a
  materially more sophisticated and CORRECT mechanism than the simple scalar `(z+FogEnd)/(FogEnd-FogStart)`
  formula the 3 CNA-custom shaders use with raw, untransformed local-space `Position.z`. This is not a case of
  the same formula fed a wrong input — it's two structurally different fog algorithms coexisting in one
  backend: the vendored stock effects get real FNA fidelity for free (byte-identical vendored bytecode + a
  faithfully-ported constant-computation helper), while every CNA-original effect (`Instanced3D` has no fog at
  all; `Pbr3D`/`PbrSkinned3D`/`SkinnedVertexColor3D` all use the simpler, object-space-only formula) diverges
  from that same backend's own established-correct convention. The simpler formula's own *arithmetic shape* is
  actually right (`(z+FogEnd)/(FogEnd-FogStart)`, matching FNA's scalar-formula convention used elsewhere in
  this project, e.g. EasyGL) — only the missing World/View transform of the Z value feeding it is wrong. Net
  effect: fog will look visibly inconsistent between stock-effect meshes and PBR/skinned-color meshes in the
  same D3D9-rendered scene, especially under camera rotation (object-space Z stays fixed to the mesh's own local
  orientation regardless of which way the camera is facing, while the real FNA algorithm correctly tracks
  camera-relative depth).
- **UPDATE — root-cause locus confirmed while auditing `backend-vulkan` directly: `VulkanGraphicsBackend::
  FillExtPushConst()` (the backend-side push-constant fill, shared by ext/lit-textured/skinned/pbr draws)
  faithfully copies `p.ambientColor` from the already-populated `GpuDrawParams` into the push constant —
  it does NOT itself drop the field.** This confirms the bug's root cause is entirely upstream, in
  `SkinnedEffect::FillGpuDrawParams()` (the XNA-facing effect code that populates `GpuDrawParams` before it
  ever reaches this backend file, not yet audited as of this shard — tracked under the `xna-graphics` shard,
  Task #4) leaving `ambientColor` at its zero-initialized default for the skinned path specifically. The
  `emissiveColor` half is structural, not just an unset value: every Vulkan skinned-shader UBO (`skinned3d`/
  `skinned3d_color`/`skinned3d_vertexlit`/`skinned3d_vertexlit_color`'s `FogParams` block) has no
  `emissiveColor` field declared at all — confirmed via full read of all 4 — so there is nowhere for the value
  to go even if the effect side did compute it.
  **UPDATE — REVISED hypothesis, now backed by a 4th independent source (`backend-d3d9`'s own
  `D3D9SkinnedVertexColorDraw.cpp`): `SkinnedEffect::FillGpuDrawParams()` likely does NOT leave `ambientColor`
  unset by oversight — it appears to deliberately pre-fold `AmbientLightColor` into `params.emissiveColor`
  for the skinned path specifically, by design, and route it through the `EmissiveColor` register/uniform
  instead of a separate `AmbientColor` one.** `D3D9SkinnedVertexColorDraw.cpp` (line 150) uploads
  `params.emissiveColor` to the `EmissiveColor` pixel-shader register with the explicit comment
  "`SkinnedEffect::FillGpuDrawParams()` already pre-folds ambient into emissiveColor (matches
  `DrawSkinnedEffectEXT`'s own identical upload of `params.emissiveColor` as `EmissiveColor`)" — i.e. D3D9's
  own REAL, vendored stock `SkinnedEffect.fx` draw path (`DrawSkinnedEffectEXT`) does the identical thing. This
  is the same convention Bgfx's own Task-899 fix comment independently described ("C++'s
  `FillGpuDrawParams()` already pre-combines `AmbientLightColor*DiffuseColor` into `emissiveColor`, matching
  EasyGL's already-working formula") and that SdlGpu's reused-lit-fragment-shader mechanism structurally
  implements. **4 independent backends (EasyGL, Bgfx, SdlGpu, D3D9) now corroborate the same "ambient is
  pre-folded into `emissiveColor` for skinned draws" convention** — which reframes Vulkan's and D3D11/D3D12's
  bugs more precisely: it is very likely NOT that the upstream `SkinnedEffect::FillGpuDrawParams()` fails to
  compute the right value at all, but that these 2 backend-groups' own skinned-shader consumption code reads
  the WRONG field (a separate, likely-always-zero-for-skinned-draws `ambientColor`) instead of the RIGHT,
  already-correctly-computed one (`emissiveColor`) — and, in Vulkan's/D3D11's/D3D12's case, additionally lack
  any shader-side slot to receive `emissiveColor` even if they read it. This should be confirmed directly once
  `SkinnedEffect.cpp`/`FillGpuDrawParams()` itself is audited (`xna-graphics` shard, Task #4) — but the balance
  of cross-backend evidence now points at "2 backends misconsume an already-correct upstream value" rather
  than "the upstream value itself is wrong."
- **NEW, Vulkan-specific: `SkinnedEffect::FillGpuDrawParams()` never sets `ambientColor`, and Vulkan's skinned
  shaders never consume `emissiveColor`** — so `AmbientLightColor`/`EmissiveColor` are silently no-ops for skinned
  models on Vulkan specifically (EasyGL forwards them correctly). Confirmed across 4 test files
  (`vulkan_skinnedeffect_combined_test.cpp`, `_preferperpixellighting_test.cpp`, `_specular_test.cpp`,
  `_vertexcolor_test.cpp` — the last of which explicitly identifies the defect in its own header comment and
  deliberately routes around it by setting `AmbientLightColor=0`, per that file's audit).
  **UPDATE, D3D11+D3D12-specific (narrower variant, found via direct source reading): the shared `D3DCommon`
  `SkinnedEffect` fragment shaders (`skinned3d.frag.hlsl`, `skinned3d_vertexlit.frag.hlsl`,
  `skinned_colored3d.frag.hlsl`, `skinned_colored3d_vertexlit.frag.hlsl`) have no `EmissiveColor` cbuffer field at
  all** — compare their `PerDraw`/`FogParams` cbuffer layouts (no `Emissive*` member anywhere) against their
  *unskinned* siblings `lit_textured3d.frag.hlsl`/`lit_textured3d_vertexlit.frag.hlsl`, which both carry an
  explicit `EmissiveColorPad` field consumed in the lit formula (`lit = lightSum * Tint.rgb +
  EmissiveColorPad.xyz`). Unlike the Vulkan case, `AmbientColor` **is** present and consumed correctly in all 4
  D3DCommon skinned shaders (`litRGB = (AmbientColor + lightSum) * DiffuseColor.rgb`) — so only the
  `EmissiveColor` half of this defect transfers to D3D11/D3D12, not the `AmbientColor` half. This is a 2nd,
  independent backend-group confirmation that `SkinnedEffect`'s `EmissiveColor` support tends to get dropped
  during porting — worth checking the C++ side (`SkinnedEffect::FillGpuDrawParams` and each backend's
  draw-param-fill code) to see whether the value is computed and discarded, or never plumbed through at all. Not
  shared by `SkinnedPbrEffect`'s `pbr_skinned3d.frag.hlsl`, whose cbuffer layout is copied wholesale from the
  already-correct unskinned `pbr3d.frag.hlsl` and does carry `EmissiveRoughness.xyz`.
  **UPDATE — genuine positive counter-example: SdlGpu does NOT share this gap.** Confirmed via direct source
  reading: `skinned3d.vert.glsl`'s fragment stage explicitly reuses `lit_textured3d.frag.glsl` **unchanged**
  (verified — both declare a byte-identical `SkinnedLightParams`/`LitLightParams` uniform block, including
  `emissiveColor_pad`), and the C++ fill code confirms this at the call-site level:
  `SdlGpuGraphicsBackend.cpp`'s `FillSkinnedLightUniforms()` calls `FillLitLightUniforms()` directly (adding only
  the `WeightsPerVertex` packing on top) — the exact same function that fills `EmissiveColor` for the unskinned
  lit path. `AmbientColor` is likewise correctly forwarded via the shared primary uniform block
  (`FillExtUniforms()`). **SdlGpu's SkinnedEffect correctly forwards both `AmbientColor` and `EmissiveColor`** —
  its "reuse one identical uniform layout for both Skinned and unskinned lit paths" architecture choice happens
  to structurally prevent the bug that affects D3D11/D3D12 (whose separate, incomplete
  `D3DSkinnedExtraConstants` struct is what actually drops the field) and Vulkan (which drops both fields).
- **NEW, Vulkan-specific, confirmed via direct source read: `GraphicsDevice.ScissorRectangle` is completely
  non-functional whenever a `RenderTarget2D`/`RenderTargetCube` is bound — it only works against the
  backbuffer.** `VulkanGraphicsBackend::RecordCommandBuffer()`'s RT-pass loop (Phase 1, iterating every used
  render target) hardcodes `VkRect2D rtSc{ {0, 0}, { rtW, rtH } }` unconditionally before every
  `vkCmdSetScissor` call for an RT pass — `scissorEnabled_`/`scissorX_`/`scissorY_`/`scissorW_`/`scissorH_`
  (correctly captured by `SetScissorRect()`/`ApplyRasterizerState()`) are never read anywhere in that loop. The
  backbuffer pass (Phase 2), by contrast, correctly checks `scissorEnabled_` and applies the real rect. A game
  that renders to an off-screen `RenderTarget2D` while relying on `ScissorRectangle`-based clipping (a common
  pattern — UI clip regions, split-screen-to-texture, etc.) gets silently unclipped output on Vulkan specifically,
  with no error or warning. Unlike the paired `Viewport`-when-RT-bound limitation (which IS explicitly disclosed
  in `SetViewport()`'s own header comment: "RT passes stay hardcoded to each RT's own full size... cannot recover
  what Viewport was active"), **the Scissor gap has no equivalent disclosure anywhere near the scissor code** —
  a silent gap, not a documented scope cut. No test found anywhere in this audit exercising `ScissorRectangle`
  together with a bound `RenderTarget2D` on any backend, so this specific combination may be broadly under-tested
  project-wide, not just unfixed on Vulkan.
- **NEW, Vulkan-specific: `env_map3d.vert.glsl` lacks the Y-flip present in every other core Vulkan 3D vertex
  shader**, causing `EnvironmentMapEffect` scenes to render vertically mirrored on Vulkan. Confirmed across 4 test
  files (`vulkan_env_map_test.cpp`, `_amount_one_test.cpp`, `_amount_zero_test.cpp`, `_combined_test.cpp`,
  `_eyeposition_test.cpp`) — all masked because their scenes are symmetric enough (identity View, centered camera,
  center-pixel-only sampling) that a vertical mirror is invisible to the specific pixel each test checks.
  **UPDATE: a 5th masked instance found** in the `examples-tests-generic` batch —
  `environmentmapeffect_alphascaledlerp_test.cpp` (a shared cross-backend test file, registered on Vulkan among
  others) exercises this exact shader and is masked for the identical reason (identity View, center-pixel-only
  sampling).
  **MAJOR UPDATE — the missing-Y-flip bug is NOT limited to `EnvironmentMapEffect`: `pbr3d.vert.glsl`,
  `pbr3d_skinned.vert.glsl`, and `instanced3d.vert.glsl` all independently confirmed to share it, and one of
  them contains a demonstrably FALSE justifying comment.** Confirmed via full source read + exact grep sweep of
  every Vulkan `.vert.glsl` file for the flip pattern: 14 shaders correctly flip
  (`colored3d`, `colored3d_legacy`, `colored_textured3d`, `textured3d`, `dual_texture3d`,
  `dual_texture_colored3d`, `alpha_test3d`, `alpha_test_colored3d`, `lit_textured3d`,
  `lit_textured3d_vertexlit`, `skinned3d`, `skinned3d_color`, `skinned3d_vertexlit`,
  `skinned3d_vertexlit_color`), while 4 do not: `env_map3d` (already recorded above), `pbr3d`, `pbr3d_skinned`,
  and `instanced3d`. `sprite2d.vert.glsl` also lacks the flip but is a genuine, verified non-bug: it computes
  NDC directly from pixel-space (`(inPos / viewportSize) * 2.0 - 1.0`) with its own explicit "Y-down to match
  XNA" comment — a self-contained 2D mapping that needs no post-hoc flip, unlike every 3D shader which shares
  one `wvp`/`vp` input built the identical way (`world * view * projection` or `view * projection`, confirmed at
  the C++ call sites in `VulkanGraphicsBackend.cpp`: `DrawPrimitivesEx`'s `wvp` and `FillInstancedPushConst`'s
  `vp`, neither of which bakes in any flip — the flip is a pure per-shader convention, not something the
  C++ side or the caller-supplied `projection` matrix supplies).
  **`pbr3d.vert.glsl`'s own comment claims the omission is deliberate**: "No Y-flip here (unlike
  `lit_textured3d.vert.glsl` et al.) -- kept consistent with `pbr3d_skinned.vert.glsl`'s own convention... so
  PbrEffect and SkinnedPbrEffect render an identical scene identically oriented." This reasoning only checks
  *internal* consistency between the two PBR shaders — it ignores that every *other* 3D effect type
  (BasicEffect/SkinnedEffect/DualTextureEffect/AlphaTestEffect/lit-textured) sharing the identical `mvp` input
  (all filled via the same `FillExtPushConst()` C++ function) DOES flip, meaning PBR/SkinnedPbr scenes render
  vertically mirrored relative to every other effect type in the same Vulkan-rendered frame — the exact same
  bug class as `env_map3d`, just with a plausible-looking (but incomplete) justification attached.
  **`pbr3d_skinned.vert.glsl`'s own comment is worse — it is factually FALSE, not just incomplete**: "Task
  899-family precedent: `skinned3d.vert.glsl` never Y-flips (unlike `lit_textured3d.vert.glsl` et al.) -- this
  shader is a direct extension of that exact skinning transform, so it mirrors that convention exactly." This is
  directly contradicted by `skinned3d.vert.glsl` itself, which — verified by direct read — DOES flip, at line 59:
  `gl_Position.y = -gl_Position.y; // Vulkan NDC Y is inverted vs OpenGL (matches textured3d.vert.glsl)`. Whoever
  wrote `pbr3d_skinned.vert.glsl`'s comment either misremembered or never actually checked `skinned3d.vert.glsl`'s
  content before citing it as precedent — the justification is not merely a design trade-off but a confidently
  wrong claim about a sibling file, which makes this instance more dangerous than a silent omission: a future
  maintainer reading the comment would conclude the current behavior is intentional and verified, when it is
  neither. Net effect: **`PbrEffect`, `SkinnedPbrEffect`, and `InstancedEffect` all render vertically mirrored on
  Vulkan**, in addition to the already-known `EnvironmentMapEffect` — 4 of Vulkan's effect-shader families
  affected in total, one via a plausible-but-incomplete rationale, one via a demonstrably false one, and one
  (`instanced3d`) with no comment or rationale at all (a likely simple oversight — `DrawInstancedPrimitivesEx`'s
  `FillInstancedPushConst` was possibly added/ported at a different time than the Y-flip convention was
  established for the rest of the file). No corresponding test file was found this checks a non-center,
  asymmetric pixel for any of these three effect types on Vulkan — consistent with the same test-masking pattern
  already recorded for `env_map3d`.
- **CONFIRMED IN 5 BACKENDS (Bgfx, WebGPU, Vulkan, SdlGpu, D3D11+D3D12): `EnvironmentMapEffect`'s fragment shader
  re-multiplies `EmissiveColor` by `DiffuseColor`** instead of adding it unscaled (FNA's `Lighting.fxh` convention,
  explicitly confirmed by this project's own `EnvironmentMapEffect.cpp` comment stating the unscaled-add is
  required to "match FNA"). Confirmed across 5 Bgfx test files (`bgfx_environmentmapeffect_eyeposition_test.cpp`,
  `_fresnel_test.cpp`, `_multilight_test.cpp`, `_specular_test.cpp`, `_worldtransform_test.cpp`), **WebGPU**
  (`webgpu_envmap3d_test.cpp`'s audit, directly reading `WebGPUGraphicsBackend::CreateEnvMapResources()`'s
  fragment shader: `litRGB=(emissiveAmount+lightSum)*diffuseColor`), **Vulkan** (previously only suspected from
  test-file phrasing; now independently confirmed via the `examples-tests-generic` batch's direct read of Vulkan's
  own `env_map3d.frag.glsl` while auditing `environmentmapeffect_alphascaledlerp_test.cpp` — resolving the prior
  "unconfirmed" note), **SdlGpu** (`src/CNA/Internal/Backends/SdlGpu/shaders/env_map3d.frag.glsl`, found via
  `sdlgpu_envmap_test.cpp`'s and `sdlgpu_smoke_test.cpp`'s audits: `litRGB = (emissiveAmount + lightSum) *
  DiffuseColor`, byte-for-byte the same formula shape), and now **D3D11+D3D12** (direct read of the shared
  `src/CNA/Internal/Backends/D3DCommon/shaders/env_map3d.frag.hlsl`, also explicitly "ported line-by-line from
  Vulkan" per its own header comment: `float3 litRGB = (EmissiveEm.xyz + lightSum) * DiffuseColor.rgb;`) — all
  masked because no test in any family varies `DiffuseColor` away from its default white or
  `EmissiveColor`/`AmbientLightColor` away from black. **A third systemic, multi-backend defect for this audit,
  alongside the fog-formula and skinned-normal-transform bugs, now the widest of the three at 5 backend-groups**
  — remaining unchecked: D3D9/Software/SdlRenderer/Dx3/Canvas/Ascii/Headless's own `EnvironmentMapEffect` shaders
  (Headless/Ascii/Software/Canvas/Dx3/SdlRenderer likely don't implement `EnvironmentMapEffect`'s reflection math
  at all given their simpler rendering models — worth a quick capability check rather than assuming parity).
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

## Duplicate NOXNA-extension API surfaces across CNA::Input and CNA::Devices

- **NEW, found while auditing `cna-devices`: `CNA::Input` (gated implicitly, always compiled) and
  `CNA::Devices` (gated behind the separate `CNA_DEVICES` CMake option, default OFF, independent of
  `CNA_NOXNA`) contain two entirely independent, redundant implementations of the same features, wrapping
  the identical underlying SDL3 calls.** Confirmed for **Clipboard**: `CNA::Input::Clipboard`
  (`GetTextEXT()`/`SetTextEXT()`/`HasTextEXT()`, the `EXT`-suffix NOXNA convention) and
  `CNA::Devices::Clipboard` (`getTextProperty()`/`setTextProperty()`/`getHasTextProperty()`, this project's
  own documented C# property convention) both wrap `SDL_GetClipboardText`/`SDL_SetClipboardText`/
  `SDL_HasClipboardText` independently — both individually correct (both correctly `SDL_free()` the
  heap-allocated `SDL_GetClipboardText()` result), but genuinely duplicated: a bug fix or behavior change
  applied to one has no reason to also reach the other, and a project enabling `CNA_DEVICES` ends up with 2
  clipboard APIs with different naming conventions and a minor behavioral difference (`Devices::
  setTextProperty` returns `SDL_SetClipboardText`'s own success/failure `bool`; `Input::SetTextEXT` is `void`
  and discards it). **Confirmed for Power/PowerState too**: `CNA::Input::PowerStateEXT`/`Power` and
  `CNA::Devices::PowerState`/`PowerInfo` are likewise 2 independent wrappers around the same
  `SDL_GetPowerInfo()` — both, independently, correctly use an explicit switch (not a risky raw cast) to
  convert `SDL_PowerState`'s own ordinals (which do NOT numerically align with either CNA enum's own
  0-based-sequential ordinals — see the already-recorded ordinal-mismatch finding) — so both are individually
  safe, but again duplicated with 2 near-identical enum definitions and 2 near-identical conversion
  functions. Worth checking whether `Camera`/`Sensors`/`Haptics`/`Joysticks`/`InputDevices` have a similar
  latent duplicate anywhere else in the codebase (not found so far — `cna-input`'s own 31 files had no
  `Camera`/`DisplayInfo`/`FileDialog`/`Locale`/`MessageBox`/`SystemTray`/`UrlLauncher`/`SystemInfo`
  equivalents, so those `cna-devices`-only features are NOT duplicated). This looks like 2 separate NOXNA
  extension efforts (perhaps at different times, by different conventions) that grew the same 2 features
  independently rather than sharing one implementation behind two thin naming-convention facades.

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
- **`D3DConstantBuffers.hpp`'s `D3DLightingConstants`/`D3DBoneConstants` doc comments claim "NOT YET WIRED into
  any draw call"** (referencing DX-60/DX-60a, ahead of DX-63/DX-67 landing), **but both are directly confirmed
  actively used today** in both `D3D11GraphicsBackend.cpp` (lines 1655, 1531/1577) and
  `D3D12GraphicsBackend.cpp` (lines 2001, 1865/1926) — found while auditing the `backend-d3dcommon` shard. A
  5th-6th instance of this audit's recurring documentation-rot pattern.
- **`D3D12RootSignatureCache.hpp`'s class-level doc comment claims every root signature uses
  `D3D12_STATIC_SAMPLER_DESC` static samplers ("fixed at shader-authoring time... this project's own
  D3D11SamplerCache-driven dynamic sampler binding is a D3D11-specific convenience, not an XNA-level requirement
  D3D12 must match")** — **stale**: `D3D12RootSignatureCache.cpp`'s actual current implementation (per its own
  "DX-119" comment) was later upgraded to `NumStaticSamplers = 0` + real per-slot `D3D12_ROOT_PARAMETER_TYPE_
  DESCRIPTOR_TABLE` sampler descriptor tables, populated dynamically at draw time from a genuine
  `D3D12SamplerCache` — i.e. D3D12 DOES now have full dynamic `SamplerState` support, matching D3D11, but the
  header's own class-level comment was never updated to reflect the DX-119 upgrade. Not a functional bug (the
  `.cpp` is correct) — a 7th documentation-rot instance, found while auditing `backend-d3d12`.
- **`D3D12Textures.hpp`'s own header comment claims "Cube/3D texture variants... are deliberately NOT implemented
  in this pass"** (referencing DX-109) — **stale**: `D3D12TextureCube.hpp`/`.cpp` (66/260 lines) and
  `D3D12Texture3D.hpp`/`.cpp` (56/290 lines) both exist as real, substantial implementations in the same shard,
  added by later tasks without this comment being revisited. An 8th documentation-rot instance.
- **`RenderPipelineSettings.hpp`'s own class doc comment (NOXNA `cna-graphics` shard) claims "Construct via
  `GraphicsDevice::GetRenderPipelineSettings()` or standalone"** — **`GraphicsDevice::GetRenderPipelineSettings()`
  does not exist anywhere in the codebase**, confirmed via exhaustive grep (zero matches outside this one
  doc-comment reference). A 9th documentation-rot instance — either planned and never implemented, or removed
  without updating this comment. Lower real-world impact than the other 8 instances since this entire shard is
  gated behind the `CNA_NOXNA` CMake option (default OFF) and has zero production consumers of any kind yet
  (every setting in this shard is an honestly-disclosed forward-looking scaffold, per the shard's own comments).
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
- **NEW, now a confirmed 2-backend pattern: a correct, well-mapped generic `VertexElementFormat` -> native-format
  helper header that is entirely dead code in production.** After Bgfx's `BgfxVertexFormatHelper.hpp` (above),
  Vulkan's own `VulkanVertexFormatHelper.hpp` (`VertexElementFormatToVk()`/`VertexElementFormatSize()`) is
  confirmed, via exhaustive grep of the entire Vulkan backend directory, to have **zero call sites in
  `VulkanGraphicsBackend.cpp`** — the real per-pipeline `VkVertexInputAttributeDescription` arrays are all
  hardcoded per-stride/per-shader instead, exactly mirroring Bgfx's own `MakeBgfxLayout()` hardcoded-stride
  dispatch. Unlike Bgfx's version, **Vulkan's own test (`vulkan_vertex_format_test.cpp`) is well-designed**: it
  directly unit-tests the mapping functions themselves (asserting `VertexElementFormatToVk`/
  `VertexElementFormatSize` against an explicit expected-value table for every enumerator), not just inferring
  behavior indirectly via `SetDataRaw()` — so the mapping logic itself is genuinely, directly verified correct,
  unlike Bgfx's equivalent test which silently exercises the same hardcoded layout regardless of declaration.
  The shared defect across both backends is purely architectural (a parallel, unused implementation path was
  built and correctly tested, but never wired into the real dispatch) — worth checking every remaining backend's
  own vertex-format-helper-equivalent header (if any) for the same "correct but dead" shape.

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
- **HIGH: `CNA::Logger::ToSDLPriority()` (`src/CNA/Logger.cpp`) mistags every `Fatal`/`Error`/`Warn` log call
  with `SDL_LOG_PRIORITY_INFO`, not their real SDL priorities — found while auditing `cna-root-utilities`.**
  The switch's `FATAL`/`ERROR`/`WARN`/`INFO` cases are all commented out with a literal `//todo` marker left in
  place, so all 4 fall through to `default: return SDL_LOG_PRIORITY_INFO;` (only `DEBUG`/`TRACE`/`EXPERIMENT`
  have real, uncommented cases, mapping to `SDL_LOG_PRIORITY_DEBUG`). This is unambiguously incomplete,
  abandoned work — the correct implementation is visible, commented out, immediately above the bug. Impact is
  two-fold: (1) every individual `Logger::Fatal()`/`Error()`/`Warn()` (and their `*If()` variants) call reports
  the wrong SDL priority to any consumer that inspects it (a custom `SDL_LogOutputFunction`, SDL's own
  priority-based coloring); (2) `Logger::SetMinimumLevel()` routes through this SAME function to call
  `SDL_SetLogPriorities()`, so setting the minimum level to `WARN` (intending "show WARN and more severe")
  actually sets SDL's own native threshold to `INFO` instead. Unlike every other finding in this audit so far,
  `Logger` is foundational, always-compiled (not gated behind `CNA_NOXNA` or any backend selection) code used
  project-wide — this is not a rendering-backend-specific bug, but a core-infrastructure one with the widest
  possible blast radius of any single bug found in this audit.
- **`CNA::Runtime` (`include/CNA/Misc.hpp`) is a fully public, Doxygen-documented class with ZERO
  implementation anywhere in the codebase** — found while auditing `cna-root-utilities`. Its 5 declared
  methods (`Initialize`/`Shutdown`/`IsGraphicsEnabled`/`IsAudioEnabled`/`IsInputEnabled`) have no `.cpp`
  definition anywhere (confirmed: no `Misc.cpp` exists at all), and the class has zero consumers anywhere in
  the repository either. Any code that instantiates `CNA::Runtime` and calls any of its methods would fail to
  link. Distinct from (and more severe than) the `cna-graphics` NOXNA shard's own "implemented but unconsumed"
  scaffold pattern — this one isn't even implemented.
- **PENDING VERIFICATION (flagged while auditing `cna-input`, to be confirmed once `cna-internal-core`/
  `cna-devices` are audited): `PowerStateEXT`'s ordinals do not numerically align with real `SDL_PowerState`'s
  own ordinals** (SDL: `Error=-1, Unknown=0, OnBattery=1, NoBattery=2, Charging=3, Charged=4` — confirmed
  against the real SDL3 header via the `planetblupi` sibling repo's vendored copy; `PowerStateEXT`: 0-based
  sequential, `Error=0, Unknown=1, OnBattery=2, NoBattery=3, Charging=4, Charged=5` — every value offset by
  +1). `SensorTypeEXT` has a related, narrower gap: its non-negative values DO align with `SDL_SensorType`'s
  own non-negative values 1:1, but there is no `SensorTypeEXT` entry at all for `SDL_SENSOR_INVALID` (-1). A
  raw numeric cast between either SDL enum and its CNA counterpart would silently misclassify values (in
  `PowerState`'s case, every single one). **Confirmed SAFE in `cna-input`'s own consumer**: `Power.cpp`'s
  `to_power_state_ext()` uses an explicit, exhaustive switch, not a cast. **UPDATE — 2nd confirmation while
  auditing `cna-devices`: `CNA::Devices::PowerInfo.cpp`'s own `ConvertSdlPowerState()` independently uses the
  identical safe pattern** (an explicit, exhaustive switch, byte-for-byte the same shape as `Input::Power.cpp`'s
  own function) — both of this codebase's 2 independent `PowerState` implementations get this right.
  **RESOLVED, fully confirmed SAFE while auditing `cna-internal-core`**: `SdlInputBridge.cpp`'s
  `sdl_power_state_to_ext()` (populating `JoystickCapabilitiesEXT::powerState`) and
  `SystemSensorBackend.cpp`'s `sdl_sensor_type_to_ext()` (populating `Sensors::GetSensorsEXT()`'s results)
  BOTH independently use explicit, exhaustive switches — no raw cast anywhere. This is the 3rd confirmed-safe
  `PowerState` conversion site (after `Input::Power.cpp`, `Devices::PowerInfo.cpp`) and the 2nd confirmed-safe
  `SensorType` conversion site (the only one — `Sensors.cpp` itself just delegates). **Every mapping site for
  both enums across the entire codebase is now confirmed to correctly avoid the ordinal-mismatch trap** — a
  clean resolution, not a defect. `SDL_SENSOR_INVALID`/unrecognized values correctly fall through to
  `SensorTypeEXT::Unknown` via the switch's `default` case, not undefined behavior.
