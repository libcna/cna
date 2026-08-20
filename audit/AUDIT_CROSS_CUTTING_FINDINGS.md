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
  outcome the project's own `plans/plan_graphics.md`/git log confirm is a still-open, known-failing case (Task 952) —
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
- **HIGH: `ENetBackend.cpp`'s `HandleReceive()` dispatches host-only broadcast messages
  (`ServerWelcome`/`GamerJoinBroadcast`/`GamerLeaveBroadcast`/`StateChangeBroadcast`) with no check that the
  sending peer is this session's authoritative host** -- found while auditing `cna-internal-core`'s Net
  subsystem. `peer` is passed to `HandleClientHello`/`HandleConnect`/`HandleDisconnect`/`HandleAppData` but
  NOT to the four broadcast-only handlers, so any connected `ENetPeer` (i.e. any client, using a
  modified/custom ENet client speaking this fully-inferable wire format -- no MITM or spoofing needed) can
  forge these directly to the host: kick arbitrary other gamers via a forged `GamerLeaveBroadcastMessage`,
  inject fake gamers into the host's own roster via a forged `GamerJoinBroadcastMessage`/`ServerWelcomeMessage`
  (the latter also corrupts the host's own local wire-id assignment), or force an arbitrary
  `NetworkEventType::StateChange` via a forged `StateChangeBroadcastMessage`. This is orthogonal to the
  subsystem's own extensive, separately-tracked `audit_net.md` remediation history (a dozen-plus prior fixes,
  all addressing lifecycle/bookkeeping correctness under honest-client conditions) -- none of that work
  covered an adversarial/modified-client threat model. See `src/CNA/Internal/Net/ENetBackend.cpp.audit.md`
  for the full trace and fix shape. Related, lower-severity: `HandleClientHello` has no per-peer resend
  guard, allowing unbounded fake-gamer injection via repeated `ClientHello` sends from one peer.
- **HIGH (32-bit `size_t` targets only): `AudioTagParser.cpp`'s ID3v2.3 and FLAC-picture-block length
  validation uses `pos + len > bound`-style bounds checks vulnerable to unsigned-integer-overflow wraparound**
  -- found while auditing `cna-internal-core`'s Media subsystem. Safe on all 64-bit desktop builds (the
  primary target), but a maliciously crafted `.mp3`/`.flac` file in the user's Music library could trigger a
  genuine out-of-bounds heap read on a 32-bit `size_t` build (e.g. Android armeabi-v7a). Contrast with
  `XactParser.cpp` (same shard), which was explicitly hardened against this exact overflow class per a cited
  external audit (AUDIO-PARSER-001) -- the two files show different levels of hardening maturity against the
  identical vulnerability class. See `src/CNA/Internal/Media/AudioTagParser.cpp.audit.md`.
- **MEDIUM: `PlaylistParser.cpp` performs no path-containment check on `.m3u`/`.m3u8` playlist entries** --
  found while auditing `cna-internal-core`'s Media subsystem. Accepts absolute paths and `..`-escaping
  relative paths as-is (standard M3U behavior, not unique to this codebase), but inconsistent with this same
  shard's own established untrusted-path defenses (`CnjSourceFile.hpp`'s root-containment check,
  `SavedPictureStore.cpp`'s single-filename-segment sanitization) -- a hostile playlist file could make the
  engine open/decode an arbitrary existing file elsewhere on the filesystem. See
  `src/CNA/Internal/Media/PlaylistParser.cpp.audit.md`.
- **MEDIUM: `VideoDecoder.cpp`'s `ConvertFrameToRGBA()`/YUV-conversion helpers index a decoded frame using
  stale cached `width_`/`height_` rather than the frame's own dimensions** -- found while auditing
  `cna-internal-core`'s Media subsystem. A potential OOB read if a video stream changes resolution mid-decode
  (low likelihood for this project's authored-cutscene use case, but currently unguarded). Notable because
  this file otherwise has the densest prior-review-fix documentation of any file in the whole audit
  (18+ cited `plans/plan_media.md` findings) yet doesn't address this specific case. See
  `src/CNA/Internal/Media/VideoDecoder.cpp.audit.md`.

- **HIGH: `TextureCubeContentTypeReader.cpp` is missing the byte-count-vs-pixel-count validation both of
  its sibling readers (`Texture2DContentTypeReader.cpp`, `Texture3DContentTypeReader.cpp`) correctly have**
  -- found while auditing `cna-internal-core`'s Xnb subsystem. For an uncompressed `SurfaceFormat::Color`
  face/level, the file's own claimed `byteCount` (fully attacker-controlled, independent of `faceSize`) is
  used as-is to size `bytes`, then indexed via unchecked `std::vector::operator[]` in the pixel-unpacking
  loop with no `bytes.size() != pixelCount*4` guard beforehand -- a crafted `.xnb` TextureCube asset with an
  undersized declared byte count triggers a genuine out-of-bounds heap read (crash risk, or heap-memory
  disclosure via pixels uploaded to the GPU). The compressed DXT path is unaffected (`DxtUtil`'s own
  established bounds checks guarantee a fixed decompressed size or a clean throw). A clear porting/
  refactoring omission, not an intentional scope difference — comparing the three sibling readers side by
  side makes the gap obvious. See `src/CNA/Internal/Xnb/TextureCubeContentTypeReader.cpp.audit.md`.

- **MEDIUM: `Color::PackFromVector4()` (`xna-framework-core` shard) uses an unclamped
  `static_cast<bytecs>(float)` conversion -- undefined behavior in C++ for out-of-range/NaN input** --
  found while auditing `xna-framework-core`. Every other float-to-component conversion path in the same
  file (the `Vector4`/`Vector3`/float constructors) correctly clamps via `MathHelper::Clamp` first via a
  shared `ToByteFromUnitClamped()` helper; the `IPackedVector::PackFromVector4` override skips this clamp
  entirely. A `Vector4` component outside roughly [0,1] (plausible from unclamped procedural/HDR-ish color
  math, or a stray `NaN`/`Infinity`) triggers real UB, not just an XNA-style "unspecified" result. See
  `src/Microsoft/Xna/Framework/Color.cpp.audit.md`. Worth checking whether other `IPackedVector`
  implementations share this pattern when that area (`Microsoft::Xna::Framework::Graphics::PackedVector`)
  is audited.

- **MEDIUM, CONFIRMED WIDESPREAD: a signed-integer-overflow-UB fix applied to `Vector2::GetHashCode()` was
  never propagated to at least 4 structurally-identical sibling files in the same `xna-framework-core`
  shard** -- found while auditing `Vector3.cpp`, then confirmed via direct grep across the shard's other
  math types. `Vector2::GetHashCode()` sums two `FloatHash()`-derived `int` values via an explicit
  unsigned-wraparound cast, with a comment citing a specific prior fix (INPUT-BUILD-006) for exactly this
  signed-overflow-UB class. **None of the following files received the same fix**, despite sharing the
  identical `FloatHash()`-sum pattern: `Vector3::GetHashCode()` (3 terms, confirmed via full audit --
  MEDIUM finding recorded in that file's own report), `Vector4::GetHashCode()` (4 terms, `FloatHash(W) +
  FloatHash(X) + FloatHash(Y) + FloatHash(Z)`), `Quaternion::GetHashCode()` (4 terms), and
  `Matrix::GetHashCode()` (**16 terms** -- summing all 16 matrix elements' `FloatHash()` values with plain
  signed `+`, the highest-risk instance of this pattern given how many arbitrary-range terms are summed).
  `Point::GetHashCode()` (`X ^ Y`) and `Rectangle::GetHashCode()` (`X ^ Y ^ Width ^ Height`) use XOR
  combining instead and are **not** affected; `Plane::GetHashCode()` (`Normal.GetHashCode() ^
  std::hash<float>{}(D)`) also XOR-combines at its own level but transitively inherits `Vector3`'s bug via
  the `Normal.GetHashCode()` call. Each affected file needs the identical fix
  `Vector2::GetHashCode()` already has (sum via `static_cast<unsigned>` then cast back to `int`) --
  Vector4/Quaternion/Matrix's own instances are noted here in advance of those files' own full audit passes
  landing (only `Vector2`/`Vector3` have been fully audited as of this note); update each file's own report
  and mark this resolved as each is confirmed fixed or accepted.

- **CONFIRMED FNA-FAITHFUL (not a port defect): `BoundingSphere::Contains(BoundingFrustum)` can never
  return `ContainmentType::Disjoint`** -- found while auditing `xna-framework-core`, verified directly
  against `/rv/data/library/github.com/FNA-XNA/FNA/src/BoundingSphere.cs` (lines 218-248). FNA's own source
  has the identical dead-code shape (`double dmin = 0; // TODO : calcul dmin; if (dmin <= Radius*Radius)
  return Intersects;`) with FNA's own comment admitting the per-axis distance calculation was never written
  for this one overload -- a real, known, upstream-incomplete method in XNA/FNA itself. Per this project's
  own FNA-fidelity policy, faithfully preserving this is correct and should NOT be independently "fixed" --
  but the CNA port is missing FNA's own explanatory `// TODO` comment at this spot, unlike this project's
  established practice of flagging known-incomplete-but-faithfully-preserved FNA behavior elsewhere
  (`LzxDecoder.cpp`'s Intel E8 handling, `VideoDecoder.cpp`'s several similar notes). Recommended fix is
  documentation-only (add a comment citing this exact FNA source location), not a behavior change. See
  `src/Microsoft/Xna/Framework/BoundingSphere.cpp.audit.md`. Worth checking whether `BoundingBox`/
  `BoundingFrustum`'s own `Contains`/`Intersects` overloads against each other have any similar
  FNA-upstream-incomplete siblings when those files (and `BoundingFrustum.cpp`, not yet audited) are
  reviewed.

- **MEDIUM: `Matrix::Invert()` computes every intermediate cofactor determinant in single-precision
  `float`, unlike FNA's own deliberate double-precision implementation** -- found while auditing
  `xna-framework-core`, verified directly against `/rv/data/library/github.com/FNA-XNA/FNA/src/Matrix.cs`
  (`Invert`, starting line 1836). FNA's real implementation promotes every multiplication/subtraction to
  `double` before rounding back to `float`, for every one of the ~20 intermediate cofactor terms -- a
  deliberate, consistently-applied precision choice for a classically numerically-sensitive operation
  (4x4 matrix inversion via cofactor expansion). The CNA port's own source comment acknowledges this
  ("FNA casts each operand to double... CNA uses plain float arithmetic for simplicity") but asserts "no
  observable difference in practice" without demonstrating it via any cited test or numerical comparison --
  unlike this project's own established standard elsewhere (e.g. `SoundEffectContentTypeReader.cpp`'s
  fixture-verified claims) for this kind of precision/behavior assertion. Given the project's explicit
  FNA-fidelity policy, this is a testable, currently-unverified claim: construct a poorly-conditioned or
  widely-varying-magnitude test matrix, invert both ways, and compare against a high-precision reference.
  See `src/Microsoft/Xna/Framework/Matrix.cpp.audit.md`.

- **Recurring pattern in `xna-framework-core`: several spots that look like defects in isolation are
  confirmed-faithful FNA reproductions once checked against the FNA reference source, and in each case the
  port omits the kind of explanatory comment this codebase otherwise uses well elsewhere.** Three confirmed
  instances so far, all found by directly reading `/rv/data/library/github.com/FNA-XNA/FNA/src/`:
  (1) `BoundingSphere::Contains(BoundingFrustum)` can never return `Disjoint` (FNA's own `// TODO : calcul
  dmin`, never implemented); (2) `BoundingFrustum::Intersects(Ray)`'s general case throws
  `NotImplementedException` (identical in FNA, though the CNA port's message is clearer than FNA's bare
  exception); (3) `Curve::ComputeTangent()`'s `Smooth`-tangent branches use inconsistent near-zero epsilon
  thresholds for `TangentIn` vs. `TangentOut` (FNA's own code has the identical asymmetry, stemming from
  a well-known easy-to-misuse C# API -- `float.Epsilon` is the smallest representable float, not a
  tolerance value -- that this port's `std::numeric_limits<float>::denorm_min()` correctly-but-confusingly
  mirrors). None of the three is a port-introduced regression; all three are correct per this project's own
  FNA-fidelity policy. Recommended as a documentation-only pass once the `xna-framework-core` shard (and
  any other shard where this pattern recurs) is fully audited: add a short comment at each spot citing the
  FNA source location, so a future maintainer doesn't mistake faithful-but-surprising behavior for an
  accidental regression and "fix" it in a way that would silently diverge from FNA.

- **`GameWindow::EndScreenDeviceChange` never centers or repositions the window onto the display named
  by its `screenDeviceName` parameter, and the orientation model substitutes an unconditional
  window-aspect-ratio heuristic for FNA's real, mobile-gated SDL-display-orientation-event mechanism.**
  Both confirmed by direct comparison against FNA's actual SDL3 platform implementation (not just its
  abstract `GameWindow.cs` declaration): `/rv/data/library/github.com/FNA-XNA/FNA/src/FNAPlatform/
  SDL3_FNAPlatform.cs`, `ApplyWindowChanges` (lines 471-575, with an explicit "Window always gets
  centered on changes, per XNA behavior" comment at line 535) and `INTERNAL_HandleOrientationChange`/
  `INTERNAL_ConvertOrientation`/orientation-support gating (lines 237-270, 814-865), plus
  `FNAWindow.SetSupportedOrientations` (`FNAWindow.cs` lines 159-174), which FNA deliberately makes a
  no-op on desktop ("we can't support that reliably across multiple mobile platforms... this method is
  essentially a no-op") where CNA's version performs real cascading-fallback state mutation instead.
  Neither deviation is disclosed anywhere in `GameWindow.hpp`'s doc comments, and the header's own
  `@param screenDeviceName` doc (line 158, "The name of the display adapter") describes exactly the
  placement behavior the implementation omits. Concretely: calling `EndScreenDeviceChange` with a
  different display's adapter name has no effect on window position in CNA, and simply resizing a
  desktop game window narrow/tall flips `CurrentOrientation` to `Portrait` with no FNA/XNA counterpart
  for that behavior. See `include/Microsoft/Xna/Framework/GameWindow.hpp.audit.md` and
  `src/Microsoft/Xna/Framework/GameWindow.cpp.audit.md`.

- **`GraphicsDeviceManager` never subscribes to its own `Graphics::GraphicsDevice`'s
  `DeviceResetting`/`DeviceReset`/`Disposing` events, unlike FNA's real
  `IGraphicsDeviceManager.CreateDevice()`, which wires `graphicsDevice.DeviceResetting +=
  OnDeviceResetting; graphicsDevice.DeviceReset += OnDeviceReset;` (`GraphicsDeviceManager.cs` lines
  556-557) so that ANY device reset — however triggered — reaches `IGraphicsDeviceService`
  listeners.** CNA's `GraphicsDeviceManager::ApplyChanges()`/`CreateDevice()` instead raise their own
  separate copies of `DeviceResetting`/`DeviceReset` manually, only around their own call into
  `applyToExistingBackend()`. This "happens to work" for preference-change resets routed through
  `GraphicsDeviceManager` itself, but a confirmed, real, independent path exists that bypasses it
  entirely: `Graphics::GraphicsDevice.cpp`'s `createBackend()` installs a `deviceEventCallback`
  (lines 1459-1478, cited by its own comment as "plans/plan_dx9.md D9-34: forward a REAL, backend-detected
  device-lost/reset event... Nine of the ten backends never call this") that raises
  `GraphicsDevice`'s *own* `DeviceResetting`/`DeviceReset` directly for a genuine backend-detected
  device-lost recovery (e.g. a D3D9-class alt-tab/display-mode-change scenario) — completely outside
  any `GraphicsDeviceManager` call. Since `GraphicsDeviceManager` never forwards `GraphicsDevice`'s
  events, this real device-lost-then-reset cycle silently never reaches `IGraphicsDeviceService`
  listeners (the conventional surface resource-reload code subscribes to), even though
  `GraphicsDevice` itself correctly tracked and raised it. See
  `include/Microsoft/Xna/Framework/GraphicsDeviceManager.hpp.audit.md` and
  `src/Microsoft/Xna/Framework/GraphicsDeviceManager.cpp.audit.md` for full detail; flag again when
  the one backend implementing this callback is formally audited under `xna-graphics`.

- **`GraphicsDeviceManager.cpp`'s own `platformSupportsOrientations()` (correctly gating orientation
  support to iOS/Android only, matching FNA) directly contradicts `GameWindow.cpp`'s unconditional,
  every-platform window-aspect-ratio orientation heuristic noted above** — this is not merely a CNA
  deviation from FNA, it is an internal inconsistency: the project already correctly implements FNA's
  real orientation-support gate in one sibling file but omits it in another within the same
  subsystem. Also independently confirmed: `GraphicsDeviceManager` genuinely calls
  `EndScreenDeviceChange` with the real adapter device name (`ApplyChanges()`/`CreateDevice()`,
  passing `gdi.getAdapterProperty()->getDeviceNameProperty()`), so the `GameWindow` finding about
  `EndScreenDeviceChange` never repositioning the window is concretely reachable through normal
  `GraphicsDeviceManager` operation, not a theoretical gap in an unused parameter.

- **`Game::UnloadContent()` is a dead virtual lifecycle hook — declared and given an empty default
  body exactly like FNA's, but never invoked anywhere by the framework.** FNA's real `Initialize()`
  (`Game.cs` lines 649-662) subscribes `graphicsDeviceService.DeviceDisposing += (o, e) =>
  UnloadContent();`, guaranteeing any override gets a chance to release GPU-dependent resources
  whenever the device is disposing. CNA's `Initialize()` (`Game.cpp` lines 513-529) never performs
  this subscription (confirmed via a whole-repository grep for `UnloadContent` -- exactly two hits,
  the declaration and the empty default body, no call site). A game overriding `UnloadContent()` per
  the documented XNA lifecycle contract will silently never have it called by CNA, under any
  circumstance. Compounds with the `GraphicsDeviceManager` event-forwarding gap noted above: even a
  fix to that gap wouldn't help here, since `Game::Initialize()` isn't listening for
  `DeviceDisposing` at all. See `include/Microsoft/Xna/Framework/Game.hpp.audit.md` and
  `src/Microsoft/Xna/Framework/Game.cpp.audit.md`.

- **`Game::PollEvents()` omits four real FNA SDL3 event reactions with concrete, observable
  consequences**, confirmed absent via grep: `SDL_EVENT_WINDOW_MOVED` (FNA detects a
  cross-display move and calls `GraphicsDevice.Reset()` with the new adapter -- real multi-monitor
  support CNA lacks), `SDL_EVENT_WINDOW_EXPOSED` (FNA calls `RedrawWindow()` to keep rendering
  during a blocking resize-drag; CNA has no such call anywhere), `SDL_EVENT_WINDOW_ENTER_FULLSCREEN`/
  `LEAVE_FULLSCREEN` (FNA syncs `GraphicsDeviceManager.IsFullScreen` back when the OS/window manager
  toggles fullscreen outside the app's own request; CNA has no such sync path), and
  `SDL_EVENT_WINDOW_MOUSE_ENTER`/`MOUSE_LEAVE` (FNA toggles the screensaver on mouse enter/leave;
  CNA disables it unconditionally from an unrelated call site in `Guide.cpp` with no matching
  toggle). Also confirms the previously-noted orientation-model gap extends to the Game loop level:
  no `SDL_EVENT_DISPLAY_ORIENTATION` handling exists in `PollEvents()` either. See
  `src/Microsoft/Xna/Framework/Game.cpp.audit.md`.

- **Recurring pattern, now confirmed 3 independent times within the `xna-framework-core` shard
  alone: a project-provided, widely-used sharp-runtime exception type is available but a raw
  `std::` exception is thrown instead, diverging from both FNA's own exception type for the same
  case and this codebase's own established convention elsewhere.** Instances: (1)
  `GameComponentCollection::SetItem()` throws `std::logic_error` with a comment claiming
  `System::NotSupportedException` "isn't available yet" -- it is, and is used by 16 other files; (2)
  `GraphicsDeviceManager`'s constructor and `registerServices()` throw `std::invalid_argument` for
  null-game and already-registered guards where FNA throws `ArgumentNullException`/`ArgumentException`
  respectively, and `System::ArgumentException` is already used elsewhere in this exact directory
  (`BoundingBox.cpp`); (3) `Game::AssertNotDisposed()` throws `std::runtime_error` where FNA throws
  `ObjectDisposedException`, and `System::ObjectDisposedException` is already used by 28 other CNA
  files. None of the three is individually severe, but three independent confirmations within one
  shard suggests this is worth a project-wide grep pass (raw `std::invalid_argument` /
  `std::runtime_error` / `std::logic_error` / `std::out_of_range` throws that shadow an
  already-available, already-used `System::*Exception` counterpart) rather than treating each as an
  isolated one-off finding.

- **`StorageDevice::DeleteContainer()` performs a real, unchecked recursive filesystem delete
  (`fs::remove_all`) driven directly by a caller-supplied `titleName` string, with zero
  path-containment validation -- and, unlike every other "missing containment check" finding this
  session, this is NOT a faithful reproduction of any FNA behavior.** FNA's own `DeleteContainer`
  (`src/Storage/StorageDevice.cs` lines 349-352) is an unimplemented stub: `throw new
  NotImplementedException();`. CNA chose to actually implement the method -- a reasonable
  enhancement over FNA's stub in principle -- but `fs::path(root) / titleName` has the same
  absolute-path-escapes/`..`-traversal behavior already confirmed elsewhere (`StorageContainer`,
  `TitleContainer::OpenStream()`), and here the consequence is a recursive **delete** rather than a
  create/open/read. `DeleteContainer("../../../SomeOtherAppData")` or an absolute path resolves
  outside the storage root and is deleted with no re-validation. This is a genuine, CNA-introduced
  path-traversal-enabled data-loss vulnerability, reachable from a public XNA-surface method, and
  arguably the most severe finding of this audit session so far. See
  `include/Microsoft/Xna/Framework/Storage/StorageDevice.hpp.audit.md` and
  `src/Microsoft/Xna/Framework/Storage/StorageDevice.cpp.audit.md`.

- **`StorageContainer`'s directory/file/OpenFile methods have no path-containment check on their
  relative-path arguments, but this is confirmed FNA-faithful** (FNA's own `StorageContainer.cs`
  uses unchecked `Path.Combine` for every one of the same methods) -- correct per this project's
  FNA-fidelity policy, joining the `TitleContainer::OpenStream()`/`BoundingSphere`/
  `BoundingFrustum`/`Curve` pattern of "looks like a bug but is a verified-faithful FNA
  reproduction," undisclosed as intentional in the CNA source. See
  `include/Microsoft/Xna/Framework/Storage/StorageContainer.hpp.audit.md`.

- **All three file pairs in the `xna-storage` shard show an intra-pair SPDX license mismatch**:
  each `.hpp` uses `// SPDX-License-Identifier: MS-PL` while its paired `.cpp` uses `// SPDX-License-Identifier:
  MIT` plus an explicit `// Copyright (c) Robert Vokac and contributors` line. This is a stronger
  version of the licensing inconsistency already noted for the `CNA::Internal::Net` subsystem
  (there it was a whole-subsystem MIT-vs-MS-PL split; here the mismatch is *within* each file pair
  itself). Now confirmed in two independent subsystems -- worth a project-wide grep for `.hpp`/`.cpp`
  pairs with disagreeing SPDX headers as part of Pass 5.

- **`ContentReader::ReadExternalReference<T>()`'s documented "rejected outright" containment
  guarantee has a real, concrete absolute-path bypass -- the third confirmed instance this session
  of the same `std::filesystem::path`-concatenation pitfall** (`base / rhs` silently discards `base`
  whenever `rhs.is_absolute()`, on top of the already-known `..`-traversal gap). `ResolveRelativeAssetPath()`
  (`ContentReader.cpp` lines 25-49) only rejects a resolved path that is exactly `".."` or begins
  with `"../"` -- an absolute-path external reference (e.g. `/etc/passwd`) in a crafted
  `.xnb`/`.cnj` file's `Texture2D`/`TextureCube` reference sails through unchanged, and
  `ContentManager::BuildAssetPath()` doesn't re-contain it either downstream. Unlike the
  `StorageContainer` findings above, this is NOT FNA-faithful -- FNA's own `ReadExternalReference`
  has no containment check at all, so CNA's check is a disclosed addition that happens to be
  incomplete, directly contradicting its own doc comment. Combined with `StorageDevice::DeleteContainer()`'s
  HIGH finding above, this makes three independent confirmations of the identical C++-specific
  pitfall across two subsystems (`xna-storage`, `xna-content`) this session -- recommend a
  project-wide grep for `fs::path(...) / <caller-supplied string>` call sites lacking a preceding
  `is_absolute()` rejection, as a dedicated item in Pass 5. See
  `include/Microsoft/Xna/Framework/Content/ContentReader.hpp.audit.md` and
  `src/Microsoft/Xna/Framework/Content/ContentReader.cpp.audit.md`.

- **`ContentLoadException` derives from `std::runtime_error` instead of this project's established
  `System::Exception` (used by 7+ other direct-`Exception`-derived XNA exception types), and is
  missing the default (parameterless) constructor FNA's real three-constructor class has.** The
  base-class choice has a concrete consequence: the message+inner constructor flattens the inner
  exception into `.what()` text (.NET `ToString()`-style "---> " concatenation) but cannot preserve
  the actual inner exception object, unlike `System::Exception`'s real `getInnerExceptionProperty()`
  -- a capability this exact codebase already correctly implements elsewhere in the same session
  (`StorageDeviceNotConnectedException`). See
  `include/Microsoft/Xna/Framework/Content/ContentLoadException.hpp.audit.md`.

- **`xna-audio` shard note: this is the most thoroughly self-audited subsystem encountered so far.**
  All 31 files were reviewed (headers in full; the largest .cpp files -- `Cue.cpp` 1398 lines,
  `SoundEffectInstance.cpp` 1233 lines, `SoundEffect.cpp` 774 lines, `DynamicSoundEffectInstance.cpp`
  648 lines, `WaveBank.cpp` 417 lines -- at a thorough-but-not-exhaustive depth, explicitly noted in
  each report). Only one new finding surfaced: `SoundBank::GetCue()` returns a raw owning `Cue*`
  instead of `std::unique_ptr<Cue>`, inconsistent with this codebase's own established
  `std::unique_ptr`-based ownership-transfer convention used elsewhere for the identical pattern
  (`StorageDevice::EndOpenContainer()`/`EndShowSelector()`) -- LOW severity, functionally fine since
  the header explicitly documents the ownership contract. Everything else reviewed was already
  correct, and the shard's own comments cite a remarkable number of real, previously-fixed defects
  from prior review cycles -- several explicitly ASan/TSAN-confirmed (a real use-after-free in
  `DynamicSoundEffectInstance::SubmitBuffer`, `AUD-15-006`; a real data race on `isFloat_`,
  fixed via `std::atomic<bool>`) and several mathematically corrected against FAudio's real
  behavior after a prior "verified as a faithful port" conclusion turned out to be incomplete (the
  `Apply3D` distance-attenuation formula, `P9-3D-003`; the weighted-lottery variation-selection
  boundary check, `P11-XACT-002`/`P11-XACT-004`). See `include/Microsoft/Xna/Framework/Audio/SoundBank.hpp.audit.md`
  for the one new finding, and the shard's other `.audit.md` reports for the full list of confirmed
  prior fixes cited by file.

- **`xna-input` shard note: an exceptionally strong, consistent track record.** All 44 files
  (`Microsoft.Xna.Framework.Input` and `.Touch`) were reviewed. Several exhaustive/automated
  spot-checks against FNA found zero discrepancies: `Buttons` (all 32 bit values, hex-for-hex),
  `Keys` (all 160 key names+values, via an automated diff), `GamePadDPad`/`GamePadThumbSticks`/
  `GamePadTriggers`'s dead-zone and clamp formulas (verified against FNA's real `GamePad.cs`/
  `GamePadThumbSticks.cs`/`GamePadTriggers.cs` line-for-line), and `TouchLocation`'s
  `TryGetPreviousLocation`/`Equals`/`GetHashCode`/`ToString`. The only findings are two
  functionally-inconsequential, LOW-severity documentation-framing notes: `GamePadState`'s and
  `MouseState`'s `GetHashCode()` implementation comments read as if preserving an original FNA
  formula ("avoids signed-overflow UB... INPUT-BUILD-006"), but FNA's real `GetHashCode()` for
  both types is simply `base.GetHashCode()` (an opaque CLR default with no portable equivalent) --
  the custom formulas here are necessary CNA inventions, not preserved ports, though they correctly
  satisfy `GetHashCode()`'s actual contract (Equals-consistency). Contrast with `GamePadDPad`/
  `GamePadThumbSticks`/`GamePadTriggers`/`KeyboardState`/`TouchLocation` in the same shard, which
  all have genuine, portable FNA formulas correctly preserved via the same overflow-safe rewrite
  pattern. See `include/Microsoft/Xna/Framework/Input/GamePadState.hpp.audit.md` and
  `include/Microsoft/Xna/Framework/Input/MouseState.hpp.audit.md`.

- **`xna-media` shard note: FNA is a complete (or near-complete) stub for a large fraction of this
  namespace -- confirmed by direct source inspection, not assumption.** `MediaLibrary`, `Album`,
  `Artist`, `Genre`, `AlbumCollection`, `ArtistCollection`, `GenreCollection`, `Picture`,
  `PictureAlbum`, `PictureCollection`, `PictureAlbumCollection`, `Playlist`, `PlaylistCollection`,
  and `MediaSource` all have every (or nearly every) member throw `NotImplementedException` in
  real FNA source (verified via `grep -c NotImplementedException` against each file). This means
  FNA cannot serve as a behavioral reference for this whole family -- CNA provides a genuine,
  from-scratch, real implementation of documented XNA 4.0 API surface FNA itself never implements
  on desktop. This is consistent with, and now considerably extends, this project's own established
  feedback that "FNA is NOT authoritative for API surface" (previously established for
  `Song.Album`/`Artist`/`Genre`/`ToString`, confirmed again directly in this pass: FNA's real
  `Song.cs` genuinely has none of those four members). By contrast, `Song`, `SongCollection`,
  `MediaPlayer`, `MediaQueue`, `Video`, `VideoPlayer`, and `VisualizationData` ARE genuinely
  implemented in FNA and were directly, successfully diffed against it in this pass (`MediaPlayer::NextSong()`'s
  repeat/shuffle/clamp logic verified line-for-line identical). Future audit/maintenance work on
  this namespace should consult real XNA 4.0 documentation (or the xn65 reference), not FNA source,
  for the stub-only types' correct behavior.

- **`MediaLibrary::SavePicture(name, Stream* source)` assumes a single `Read()` call fills the
  whole buffer, violating this project's own `System::IO::Stream::Read()` interface, which
  explicitly documents a single call may return fewer bytes than requested.** The return value is
  discarded entirely; for any `Stream` subclass that legitimately returns a partial read (a real,
  interface-permitted case), the trailing portion of the buffer stays zeroed and is silently saved
  as if it were the complete image -- a genuine, confirmed defect, not an FNA-parity gap (FNA has no
  equivalent method to compare against, since `MediaLibrary` is a stub there). See
  `include/Microsoft/Xna/Framework/Media/MediaLibrary.hpp.audit.md` and
  `src/Microsoft/Xna/Framework/Media/MediaLibrary.cpp.audit.md`.

## Licensing/header-convention inconsistencies

- **The entire `CNA::Internal::Net` subsystem (`ENetLibrary`, `ENetHostHandle`, `ENetDiscoveryService`,
  `NetDiscoveryProtocol`, `NetPacketCodec`, `ENetBackend` -- 12 files) uses `// SPDX-License-Identifier: MIT`
  plus an explicit `// Copyright (c) Robert Vokac and contributors` line**, diverging from every other
  CNA-original NOXNA file audited elsewhere (`Json.hpp`, the Media subsystem, `GltfImportCore`,
  `PbrMaterial.hpp`, etc.), all of which use only `// SPDX-License-Identifier: MS-PL` with no separate
  copyright line. `include/CNA/Misc.hpp` (also audited, `cna-root-utilities` shard) has no SPDX header at
  all -- a third variant. May be intentional (ENet itself is MIT-licensed), but is currently an unrecorded
  inconsistency across three different conventions in the codebase's own NOXNA code.

## Per-shard notes: `xna-net`

- **`xna-net` shard note: FNA has ZERO reference material at all for this namespace -- not merely a
  stub family like Media, a total absence.** Confirmed via `find` against the local FNA reference tree
  (`/rv/data/library/github.com/FNA-XNA/FNA/src`): no `Net`/`GamerServices` directory, no files, nothing.
  This is a stronger and categorically different situation than `xna-media`'s "FNA implements this as a
  complete stub" pattern (where at least a `NotImplementedException`-throwing file exists to confirm
  against) -- here there is no file to confirm anything against, for any of the 42 files in this shard.
  Several source comments in this shard make specific, sometimes startlingly precise claims about FNA's
  own internal comments/bugs verbatim (e.g. `NetworkSessionProperties.cpp`'s claimed "TODO: Expand list to
  index size?"; `QualityOfService.hpp`'s claimed "TODO: Everything below"; `PacketReader`/`PacketWriter`'s
  claimed Color read/write byte-count asymmetry; `LocalNetworkGamer::ReceiveData(PacketReader&, ...)`'s
  claimed "declares a length variable that is never updated"; several `BeginJoin`/`BeginJoinInvited`
  call sites' claimed hardcoded `NetworkSessionType.PlayerMatch`/null-`SessionProperties` FIXMEs). None of
  these are verifiable against this project's own designated FNA reference, but several are independently
  corroborable as real, well-documented XNA quirks from general domain knowledge (the `SendDataOptions`
  `[Flags]`-but-not-bitwise-composable enum shape; the `PacketReader`/`PacketWriter` Color asymmetry) --
  treated as plausible-and-preserved rather than flagged as unverifiable-therefore-suspect. Future
  maintenance on this namespace should consult real XNA 4.0 documentation (or the xn65 reference), not
  FNA source, the same conclusion already reached for `xna-media`'s stub-only types.
- **Positive finding, independently significant: the previously-known critical `NetworkSession::Dispose()`
  ASan-confirmed heap-buffer-overflow use-after-free (this project's own prior `audit_net.md`, "Critical
  finding 1") is confirmed genuinely fixed in the code as it stands today.** `NetworkSession::Dispose()`
  (`NetworkSession.cpp` lines 297-337) now early-returns on a second call via an `isDisposed_` guard
  (Task 12.1), with defense-in-depth clearing of all four gamer collections
  (`localGamers_`/`remoteGamers_`/`allGamers_`/`previousGamers_`) independent of that guard, so even a
  single `Dispose()` call cannot leave a caller-visible dangling pointer into a gamer `ownedGamers_.clear()`
  just freed. See `src/Microsoft/Xna/Framework/Net/NetworkSession.cpp.audit.md` for the full verification,
  which also confirms at least seven other distinct, specifically-tracked defects in this same file
  (Task 2.1-2.5, 2.15, 3.1-3.3, 6.1, 12) are genuinely fixed, not merely claimed.
- **MEDIUM finding: `NetworkSessionProperties::Insert(int)`/`RemoveAt(int)` perform unchecked
  `properties_.begin() + index` iterator arithmetic, unlike every other index-taking member in the same
  file** (`operator[]` via `.at()`; `CopyTo` via explicit `ArgumentOutOfRangeException`/`ArgumentException`
  checks). A negative or past-the-end `index` is undefined behavior (invalid iterator construction), not a
  catchable exception, for a type reachable from the public `NetworkSession::Create`/`Find` API surface.
  See `src/Microsoft/Xna/Framework/Net/NetworkSessionProperties.cpp.audit.md` for the full analysis and a
  suggested fix shape (report-only, no source changes made).
- All 42 files in this shard are otherwise correct; no other new defects found. The shard is notable for
  unusually thorough self-documentation: nearly every non-trivial member across `NetworkSession`,
  `NetworkGamer`, `LocalNetworkGamer`, and `AvailableNetworkSession` cites a specific `plans/plan_net.md` task ID
  and/or a sibling-repo `DEFERRED.md` item number for every claimed FNA-stub-versus-restored-behavior
  distinction, rather than an unverifiable bare assertion -- the strongest practical substitute available
  given the total absence of an FNA reference for this namespace.

## Per-shard notes: `xna-gamerservices`

- **`xna-gamerservices` shard note: FNA has zero reference material here too**, the identical situation to
  `xna-net` (confirmed via `find` -- no `Achievement`/`Leaderboard`/`Avatar`/`Guide`/`GamerServices` files
  anywhere in the local FNA tree). All 89 files (`Achievement`/`Leaderboard` family, `Avatar` family, `Gamer`/
  `GamerCollection`/`GamerPresence`/`GamerPrivilege*`/`GamerProfile` core, `SignedInGamer`/`FriendGamer`
  family, `GamerServicesComponent`/`GamerServicesDispatcher` infrastructure, exception types, and `Guide` +
  misc enums) were audited across six parallel passes. Four MEDIUM findings surfaced; no HIGH/CRITICAL.
- **MEDIUM: `GamerPresence.cpp`'s `presenceModeStrings_` display-string table is sorted alphabetically, not
  indexed to `GamerPresenceMode`'s declared enum ordinals -- `setPresenceModeProperty()` indexes it by raw
  enum value, so 59 of the 60 modes resolve to the wrong display string** (e.g. `None` -> "Arcade Mode",
  `CornflowerBlue` -> "Won the Game" -- verified programmatically, not just by inspection). Currently
  dormant/no observable effect (no public getter exposes the resolved string, and its only consumer,
  `SetPresenceModeStringEXT`, is a permanent no-op) but would become a live, silent, wrong-everywhere bug
  the instant either half gets a real implementation. See
  `src/Microsoft/Xna/Framework/GamerServices/GamerPresence.cpp.audit.md`.
- **MEDIUM: `GamerServicesComponent` (a concrete `System::Object`/`GameComponent`-derived class) does not
  override `GetTypeName()`** -- confirmed it silently reports `GameComponent`'s own literal
  `"Microsoft.Xna.Framework.GameComponent"` instead of
  `"Microsoft.Xna.Framework.GamerServices.GamerServicesComponent"`. Confirmed not a project-wide gap: the
  sibling `DrawableGameComponent` correctly overrides it, making this a specific, isolated miss on this one
  type. See `include/Microsoft/Xna/Framework/GamerServices/GamerServicesComponent.hpp.audit.md`.
- **MEDIUM: `GuideAlreadyVisibleException` is fully implemented and unit-tested but is dead code in
  production** -- the real "a Guide UI is already pending" guards inside `Guide::BeginShowMessageBox`/
  `BeginShowKeyboardInput` throw a generic `System::InvalidOperationException` instead of this dedicated,
  purpose-built exception type (confirmed via grep: never constructed/thrown anywhere outside its own
  declaration and its own test file). See `include/Microsoft/Xna/Framework/GamerServices/Guide.hpp.audit.md`.
- **MEDIUM: `PropertyDictionary`'s entire read-accessor surface (both `operator[]` overloads plus all eight
  `GetValueXxx` methods -- nine methods total) throws raw `std::out_of_range`/`std::bad_any_cast` instead of
  this project's own `System::Collections::Generic::KeyNotFoundException`/`System::InvalidCastException`.**
  The single largest-blast-radius instance yet of this audit's recurring "raw `std::` exception instead of
  the project's own sharp-runtime exception type" cross-cutting pattern (see the Systematic FNA parity
  gaps / general findings above) -- one file, nine methods, all sharing the identical gap. See
  `include/Microsoft/Xna/Framework/GamerServices/PropertyDictionary.hpp.audit.md`.
- **Positive, independently significant: `GamerCollection<T>` (the shared collection template underlying
  `NetworkSession`'s/`NetworkMachine`'s gamer lists, already consumed by the `xna-net` shard) does NOT share
  the sibling `xna-net` shard's `NetworkSessionProperties::Insert`/`RemoveAt` unchecked-iterator-arithmetic
  bug.** Specifically checked given that prior finding: `operator[]` uses
  `ArgumentOutOfRangeException::ThrowIfNegative`/`ThrowIfGreaterThanOrEqual`, its enumerator's `getCurrent()`/
  `MoveNext()` both guard against null/out-of-range/post-`Dispose()` access, and `CopyTo` is likewise
  checked -- every index-taking member is safe. `AchievementCollection`'s own `Insert`/`RemoveAt`/`operator[]`/
  `CopyTo` are similarly fully and correctly bounds-checked with proper sharp-runtime exception types. Both
  are recorded as positive counter-examples showing the `NetworkSessionProperties` bug is an isolated lapse,
  not a systemic collection-type pattern in this codebase.
- **Positive: `GamerServicesDispatcher::UpdateAsync()`'s "permanent no-op once initialized" claim -- the
  load-bearing assumption behind the `xna-net` shard's `NetworkSessionAction` synchronous-completion fix --
  is independently CONFIRMED true by direct source reading**, not merely plausible: `Update()`'s body is
  genuinely empty, and `UpdateAsync()` is `if (isInitialized_) Update(); return isInitialized_;` with no
  reset path once `GamerServicesComponent::Initialize()` sets `isInitialized_ = true` (FNA's own
  `ProcessExit` reset hook is explicitly noted as intentionally omitted). This validates that the `xna-net`
  shard's `NetworkSession::Create`/`Find`/`Join` polling-loop fix was solving a real, otherwise-infinite hang,
  not a hypothetical one.
- One LOW finding worth surfacing: `SignedInGamerCollection::operator[](PlayerIndex)` checks only the upper
  bound (`id >= collection_.size() -> nullptr`), not the lower bound, before indexing -- reachable only via a
  deliberately-misused explicit negative `static_cast` to `PlayerIndex` (the enum itself has no negative
  named values), which is why this is LOW rather than MEDIUM despite superficially resembling the
  `NetworkSessionProperties` shape.
- Two more confirmed instances of the "async `Begin*` stores a callback but never invokes it" bug class
  already fixed for `NetworkSession` in the `xna-net` shard (Task 12) were found already independently fixed
  here too: `SignedInGamer::BeginAwardAchievement`/`BeginGetAchievements`. `Guide`'s own
  `BeginShowMessageBox`/`BeginShowKeyboardInput` completion paths were also directly verified to NOT share
  that bug, explicitly citing the `xna-net` fix as their own precedent for a capture-then-clear,
  reentrancy-safe invocation order.
- `AvatarRenderer`'s resource ownership was directly verified clean (non-owning `GraphicsDevice*`, a
  `shared_ptr<SkinnedModelEXT>` for the shared mesh asset, and a `unique_ptr<SkinnedEffect>` released in
  `Dispose()`), with three previously-tracked defect fixes (Task 1.5, 1.6, 11.6) confirmed present. Note for
  a future pass: the actual joint-weight/bone-skinning math this project's memory records a multi-round
  remediation history for (the "infinite slab" weight-blend bug, the reverted flat-cap garment redesign)
  lives in `Graphics::SkinnedModelEXT::ComputeBoneTransformsEXT()` -- a different file, under the
  `xna-graphics` shard (not yet started), not this shard's thin XNA-facing `AvatarRenderer` wrapper.

## Per-shard notes: `xna-graphics`

- **`xna-graphics` shard note: unlike `xna-net`/`xna-gamerservices`, this namespace HAS real, extensive FNA
  reference material** (`src/Graphics/**/*.cs`), so every finding below is a genuine, directly-verified
  divergence from real FNA behavior, not an unverifiable claim. All 191 files audited across 9 parallel
  passes (8 forks by theme + this shard's `SpriteBatch`/`SpriteFont`/`SpriteSortMode`/`SpriteEffects` quartet
  audited directly, given a prior-session flag on `SpriteFont.cpp`/`SpriteBatch.cpp` for a known
  `unordered_map::end()` UB risk). This is the largest, most consequential shard audited so far in this
  project — the findings below span real memory-safety bugs in the most heavily-used rendering API
  (`SpriteBatch`), a resolved long-standing cross-cutting hypothesis, and the widest single-file instance of
  this audit's recurring exception-type pattern yet found.

### HIGH findings

- **`SpriteFont::MeasureString()` and `SpriteBatch::DrawString()` can both dereference an invalid
  `std::unordered_map::end()` iterator — real undefined behavior, not a graceful failure.** Both functions'
  identical "character not found, fall back to `defaultCharacter_`" logic does a second
  `characterIndexMap_.find()` with no check that it also succeeded before dereferencing `it->second`. FNA's
  real equivalent (`characterIndexMap[DefaultCharacter.Value]`, a C# `Dictionary` indexer) throws a clean,
  catchable `KeyNotFoundException` in the same edge case — so this is a genuine, confirmed defect relative to
  FNA's own safe behavior, not a preserved-as-is quirk. Reachable whenever a `SpriteFont` is constructed with
  a `defaultCharacter` not itself present in `characters` (nothing validates this invariant anywhere) and an
  unresolvable character is then measured/drawn. See
  `src/Microsoft/Xna/Framework/Graphics/SpriteFont.cpp.audit.md` and
  `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp.audit.md`.
- **`SpriteBatch::DrawString()`'s axis-direction lookup tables are sized for only 3 entries, but real XNA's
  `SpriteEffects` is a composable `[Flags]` enum with a valid 4th (combined `FlipHorizontally|FlipVertically`)
  value — an out-of-bounds stack read.** FNA's own `SpriteBatch.cs` declares the equivalent tables with 4
  entries specifically to handle this combination. CNA's `SpriteEffects` enum is missing the `operator|`
  overload the codebase's own convention provides for other flag enums (e.g. `GestureType`), but this doesn't
  prevent the combined value from being constructed — this exact codebase already does so elsewhere via a
  manual `static_cast` workaround (`examples/sdlgpu_2d_test.cpp:126`). If that combined value ever reaches
  `DrawString`, `effIdx=3` reads past the end of all four 3-element tables. See
  `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp.audit.md` and
  `include/Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp.audit.md` (missing operator, MEDIUM).
- **`EffectParameter`'s Matrix Get/Set/Transpose semantics are inverted relative to FNA across all 8
  Matrix-related methods.** FNA's real `GetValueMatrix()`/`SetValue(Matrix)` apply a column-major transpose
  (HLSL's default non-`row_major` layout); this port's plain (non-`Transpose`) variants do the untransposed
  read/write instead — exactly what FNA's own `*Transpose` variants do, and vice versa, for
  `GetValueMatrix`/`GetValueMatrixArray`/`GetValueMatrixTranspose`/`GetValueMatrixTransposeArray`/
  `SetValue(Matrix)`/`SetValue(vector<Matrix>)`/`SetValueTranspose(Matrix)`/`SetValueTranspose(vector<Matrix>)`.
  Cross-validated with high confidence: the sibling `EffectAnnotation::GetValueMatrix()` (audited in the same
  pass) implements the identical FNA formula **correctly**, proving the right convention was known and
  applied elsewhere in this same codebase — this looks like a specific transcription slip in
  `EffectParameter.cpp`, not a deliberate design choice. Real-world rendering impact depends on whether
  `Effect.cpp`/each stock effect's own draw path happens to compensate downstream via `FillGpuDrawParams()`
  (which was separately confirmed, in the sibling stock-effects pass, NOT to route through
  `EffectParameter`'s generic accessors at all for the built-in stock effects) — the practical exposure is
  therefore custom/user-authored `Effect`s that use `EffectParameter`'s generic Matrix accessors directly, a
  real and supported XNA usage pattern. See `src/Microsoft/Xna/Framework/Graphics/EffectParameter.cpp.audit.md`.
- **`EffectParameter::Elements`/`StructureMembers` are permanently empty** — confirmed via repo-wide grep that
  nothing anywhere populates `elements_`/`members_` after construction. Any array- or struct-typed custom
  effect parameter silently reports zero sub-elements regardless of the shader's actual declaration. See
  `include/Microsoft/Xna/Framework/Graphics/EffectParameter.hpp.audit.md`.
- **`BasicEffect` never populates its own `Effect::Parameters` collection at all** — unlike every sibling
  stock effect (`AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect`, `PbrEffect`,
  `SkinnedPbrEffect`), it has no `EffectParameter*` members, no `CacheEffectParameters()`, and `OnApply()` is a
  literal no-op. Rendering itself is unaffected (the real draw path uses `FillGpuDrawParams()` directly), but
  the standard, XNA-documented `effect.Parameters["DiffuseColor"]`-style generic access — which works
  correctly on all 6 sibling effects — silently returns nothing for `BasicEffect`, the single most commonly
  used stock effect in the entire API. See `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp.audit.md`.
- **`GraphicsDevice.cpp` has ~27 raw `std::runtime_error`/`std::invalid_argument` throws** (mostly "no vertex
  buffer bound"/"no effect applied" guards across every `Draw*` overload, plus `GetBackBufferData`/
  `SetRenderTargets` validation), inconsistent with the same file's own correct use of
  `System::InvalidOperationException`/`ArgumentOutOfRangeException`/`ObjectDisposedException`/
  `NotSupportedException` at 13 other sites. The largest single-file instance of this audit's recurring
  exception-type pattern found so far, in the framework's single most central class. See
  `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp.audit.md`.
- **`VertexBuffer`/`IndexBuffer` have no destination-byte-offset concept anywhere in their public
  `SetData`/`SetDataWithOptions` API — confirmed as the root cause of the already-documented
  `IVertexBufferBackend`/`IIndexBufferBackend::SetDataWithOptions()` backend-interface gap** (see the
  "Architecture" section above). The gap does not originate at the backend-interface layer at all — it
  originates here: real FNA's `VertexBuffer.cs`/`IndexBuffer.cs` both expose a genuine `offsetInBytes`
  destination parameter as their most-general `SetData` overload, and this port dropped that overload
  entirely. A real XNA streaming pattern (ring-buffer dynamic vertex data across multiple draws per frame
  using `NoOverwrite`) is architecturally impossible to express in this port, not merely suboptimal. See
  `include/Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp.audit.md` and
  `include/Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp.audit.md`.

### Resolved: the `SkinnedEffect` `ambientColor`/`emissiveColor` open hypothesis (see "Systematic FNA parity
gaps" above)

**CONFIRMED, direct evidence found**: `SkinnedEffect::FillGpuDrawParams()` computes
`emissiveColor[i] = (emissiveColor_.i + ambientLightColor_.i * diffuseColor_.i) * alpha_` — byte-for-byte
FNA's real `EffectHelpers.SetMaterialColor()` lighting-enabled formula — and never writes `p.ambientColor`
anywhere; `EnvironmentMapEffect.cpp` does the identical thing. This resolves the standing open question: the
upstream C++ value is correct and deliberately routed through `emissiveColor`; any backend whose skinned
shader reads `ambientColor` instead (Vulkan) or lacks an `emissiveColor` slot for skinned draws (D3D11/D3D12)
is the one misconsuming an already-correct value, not the C++ layer. `SkinnedPbrEffect`, by contrast,
correctly populates both fields *separately* by design (the real glTF PBR BRDF needs both terms
independently). See `src/Microsoft/Xna/Framework/Graphics/SkinnedEffect.cpp.audit.md`.

### Resolved: the `GraphicsDeviceManager`/`GraphicsDevice` device-events open question (see "Architecture"
section above, `xna-framework-core` shard)

**Resolved in `GraphicsDevice`'s favor**: `GraphicsDevice` itself correctly raises
`DeviceResetting`/`DeviceReset`/`DeviceLost`/`Disposing` at the right points (confirmed in `Reset()`,
`Dispose()`, and the backend `deviceEventCallback`). The previously-documented HIGH finding
("`GraphicsDeviceManager` never subscribes to these") is confirmed to be purely a subscriber-side gap —
`GraphicsDevice` has no gap in when/whether it raises them. See
`src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp.audit.md`.

### Resolved: `SamplerState.AddressW`/`BlendState.ColorWriteChannels`/`RasterizerState` state-object questions
(see "Architecture" section above)

**All confirmed correctly implemented at the XNA-facing-class level**, with zero gap in `SamplerState`,
`BlendState`, `RasterizerState`, or `DepthStencilState` themselves: `SamplerState.AddressW` is fully real and
independently settable (default `Wrap`, matches FNA); `BlendState.ColorWriteChannels` (all 4 MRT targets) is
fully real; `RasterizerState.MultiSampleAntiAlias`/`ScissorTestEnable`/`DepthBias`/`SlopeScaleDepthBias` and
`DepthStencilState`'s full 16-property field set are all fully real and correct. Every already-documented gap
in this area (`IGraphicsBackend::ApplySamplerState()` missing a parameter, D3D12's non-functional
stencil/scissor) is 100% confined to the backend layer, confirmed by tracing the call sites in
`GraphicsDevice.cpp`. See `include/Microsoft/Xna/Framework/Graphics/SamplerState.hpp.audit.md` and siblings.

### Resolved: cube-face mip-regeneration question (see "Architecture" section above)

**Resolved: not this shard's fault.** `TextureCube`'s own XNA-facing `SetData`/`GetData` API is correctly,
unambiguously per-face throughout (matches FNA exactly) — the previously-found "regenerates mips for all 6
faces" defect (SdlGpu, D3D11) is purely a backend-level issue, not caused by this XNA-facing class or its
documentation. See `include/Microsoft/Xna/Framework/Graphics/TextureCube.hpp.audit.md`.

### Resolved: `SkinnedModelEXT` bone-weight-blend "infinite slab" question (persistent project memory)

**The defect cannot originate in `SkinnedModelEXT.cpp`.** `ComputeBoneTransformsEXT()` computes exactly one
world-space matrix *per bone* via single-parent-chain composition — structurally identical to
`AnimationPlayer::RecomputeTransforms` — and performs **no per-vertex multi-bone weight blending at all** (no
`BlendWeight`/`BlendIndices` consumption anywhere in this file). The previously-recorded "infinite slab"
joint-weight-blend defect must therefore live in per-backend skinned-vertex-shader code (where this project's
own cross-cutting findings already document a related, separate skinned-normal-transform bug across all 14
backends) or in the content-import weight-population pipeline — neither of which is in this file. Six
previously-tracked defect fixes (Task 11.1-11.5, 11.21) were independently confirmed present and correct. See
`src/Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.cpp.audit.md`.

### MEDIUM findings

- **`Texture2D::GetTypeName()` returns bare `"Texture2D"` instead of the fully-qualified `.NET` name** every
  sibling (`Texture3D`, `TextureCube`, `RenderTarget2D`, `RenderTargetCube`) correctly returns — an isolated
  regression on the single most commonly-used XNA type in the whole API. See
  `src/Microsoft/Xna/Framework/Graphics/Texture2D.cpp.audit.md`.
- **`RenderTargetCube` lacks `RenderTarget2D`'s own Task 717 fix**: no `Dispose(bool)` override at all, so it
  has neither the "still bound to device" guard nor the dangling-pointer clear `RenderTarget2D` already had to
  add — a real use-after-free risk in the structurally identical pointer pattern. See
  `include/Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp.audit.md`.
- **`RenderTargetBinding`'s two-argument constructors have zero validation** (FNA's real constructors throw
  `ArgumentNullException` for a null texture and `ArgumentOutOfRangeException` for an invalid `CubeMapFace`),
  and it carries an undisclosed, non-`NOXNA`-tagged `arraySlice` extension with no FNA equivalent. See
  `include/Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp.audit.md`.
- **`TextureCollection` is missing FNA's real render-target/sampler-conflict check** (binding a texture that's
  simultaneously an active render target should throw `InvalidOperationException`; currently silently
  allowed). See `include/Microsoft/Xna/Framework/Graphics/TextureCollection.hpp.audit.md`.
- **`DynamicVertexBuffer`/`DynamicIndexBuffer` don't override `GetTypeName()`** — same defect shape as
  `GamerServicesComponent` (sibling `xna-gamerservices` shard) and `Texture2D` (above): each silently reports
  its base class's name instead of its own.
- **`VertexBufferBinding.VertexOffset` is modeled as a vertex-count offset, not FNA's real byte offset**
  (`VertexBufferBinding.cs` explicitly documents bytes) — a genuine unit-semantics divergence, flagged for
  cross-check against `GraphicsDevice`'s actual consumption of this field.
- **`ModelMeshPartCollection::operator[](int)` and `ModelEffectCollection::operator[](int)` both perform
  unchecked `std::vector::operator[]` indexing** where FNA's real equivalents (`ReadOnlyCollection<T>`) always
  bounds-check. The same shape as the confirmed `xna-net` shard's `NetworkSessionProperties::Insert`/
  `RemoveAt` bug. Contrast: `ModelBoneCollection`/`ModelMeshCollection` in this same family correctly use
  `.at()`.
- **`Model.cpp`: five throw sites use raw `std::out_of_range`/`std::runtime_error`** instead of
  `System::ArgumentOutOfRangeException`/`System::InvalidOperationException`, confirmed against FNA's real
  `Model.cs`, which documents exactly those `System.*` exception types.
- **`PackedVector/Byte4.hpp`, `Short2.hpp`, `Short4.hpp` all truncate instead of round** in `Pack()`
  (`static_cast<uint32_t>(clamp(x,0,255))` vs. FNA's `(uint)Math.Round(...)`) — a systematic off-by-up-to-1
  error for any non-integer input, not just boundary ties. Contrast: `NormalizedByte2/4`/`NormalizedShort2/4`
  in the same directory correctly use `std::lroundf`.
  - **Root cause of why this went undetected, found via `tools-fna-reference` shard audit**:
    `tools/fna-reference/PackedVectorReference.cs`'s `DumpByte4()`/`DumpShort2()`/`DumpShort4()` use
    exclusively integer test inputs (`255`, `32767`, `-32768`, ...) — unlike all 14 other `Dump*()`
    functions in the same file, which deliberately include a fractional value (`0.25`/`0.5`) because
    their formulas are rounding-sensitive. For an exact-integer input, `Round(x) == Truncate(x)`, so
    CNA's truncating `Pack()` and FNA's real rounding `Pack()` produce byte-identical output — this
    project's own FNA-vs-CNA comparison harness (Task 479) was structurally incapable of catching this
    defect, not merely unlucky. The tool itself correctly calls real FNA's own types (not an
    independent C# reimplementation of the pack formula), so the reference data it does produce is
    trustworthy — the gap is purely in which test inputs were chosen. See
    `audit/tools/fna-reference/PackedVectorReference.cs.audit.md` for the full writeup.
- **`VertexPositionColor.hpp` does not implement `IVertexType` at all** — unlike real FNA's
  `VertexPositionColor : IVertexType` and unlike every other concrete vertex type in this shard.
- **`GraphicsDevice::Dispose()` disposes owned resources *before* raising `Disposing`**, inverted from FNA's
  real order (event first, then resource teardown) — a `Disposing` handler here can never observe a
  still-valid resource, unlike real XNA.
- **`DisplayMode` is missing `TitleSafeArea`, `GetHashCode()`, and `ToString()`** — all three real, documented
  FNA members.
- **`DeviceLostException`/`DeviceNotResetException`/`NoSuitableGraphicsDeviceException` all derive from
  `std::runtime_error` instead of `System::Exception`**, and are all missing the `(message, innerException)`
  constructor FNA's real types have.
- **Four `EffectParameterCollection`/`EffectPassCollection`/`EffectTechniqueCollection`/
  `EffectAnnotationCollection` `operator[](int)` implementations each throw raw `std::out_of_range`** instead
  of `System::ArgumentOutOfRangeException` — bounds-checking itself is present and correct in all four; only
  the exception type deviates.
- Exception-type convention violated in `SkinnedEffect.cpp` (4 sites), `EnvironmentMapEffect.cpp` (1),
  `PbrEffect.cpp` (1), `SkinnedPbrEffect.cpp` (4, mirroring `SkinnedEffect` almost verbatim),
  `SamplerStateCollection.cpp` (both `operator[]` overloads), and pervasively across `Texture`/`Texture2D`/
  `Texture3D`/`TextureCube`/`TextureCollection` (`Texture2D.cpp` alone has ~15+ raw-`std::`-exception sites,
  the widest single-file instance in the `Texture*` family).

### LOW findings and other notes

- `SamplerStateCollection` has no per-slot dirty-tracking (unlike FNA's real `modifiedSamplers`); traced the
  consequence into `GraphicsDevice::applySamplerStatesToBackend()`, which unconditionally re-applies all 16
  sampler slots every call — a confirmed real performance-architecture divergence from FNA, no correctness
  impact.
- `GraphicsResource` has no way to reassign `graphicsDevice_` after construction, unlike FNA's real `internal
  set`-backed property (used by `VertexDeclaration` to move between devices) — flagged for follow-up.
- `VertexDeclaration`'s auto-stride constructor doesn't validate for an empty element list, unlike FNA's real
  constructor (`ArgumentNullException`/`ArgumentException`).
- A shared, narrow rounding-tie divergence (round-half-up vs. FNA's `Math.Round` banker's-rounding) affects
  `Alpha8`, `Bgr565`, `Bgra4444`, `Bgra5551`, `Rg32`, `Rgba1010102`, `Rgba64`.
- `EffectMaterial::Clone()` preserves type identity, unlike FNA's own `EffectMaterial` (no `Clone()` override
  there, which would slice to a plain `Effect`) — likely an intentional improvement, flagged for visibility,
  not treated as a defect.
- `VertexBuffer`/`IndexBuffer`'s plain `SetData` has zero bounds validation in any build config, but this
  matches FNA's own dominant Release-mode behavior exactly (FNA's equivalent check is
  `[Conditional("DEBUG")]`) — parity, not regression.
- `BlendFunction.hpp`'s Doxygen comments for `Max`/`Min` are self-consistent with the enum names, whereas
  FNA's own doc comments for those two values are internally swapped/contradictory — this port correctly did
  not copy that FNA documentation bug verbatim.

## Recurring pattern: `NetworkSession*` `Dispose()`d but never `delete`d

- **CONFIRMED, 10 files across two categories: 8 example demos (`demo_qos_probe`,
  `demo_session_lifecycle_events`, `demo_gamer_roster_hud`, `demo_session_browser`,
  `demo_simulated_network_conditions`, `demo_net_client_server_arena`,
  `demo_gamerservices_dispatcher_watchdog`, `demo_net_avatar_sync`) AND 2 test/tool regression
  harnesses (`tools/net/gamerservices_dispatcher_harness.cpp`, `tools/net/net_two_process_harness.cpp`
  — the latter alone has roughly a dozen separate call sites) all call `session->Dispose()` but
  never `delete session`.** This is now confirmed as a genuinely systemic, project-wide gap around
  this one class's ownership contract specifically — not confined to throwaway example code, since
  it also appears in the actual regression-test infrastructure spawned by real GTest suites
  (`TwoProcessLoopbackTest.cpp`, `GamerServicesDispatcherHangRegressionTest.cpp`).
  `NetworkSession`'s own class-level doc comment (`xna-net` shard, audited this session) explicitly
  documents that the caller must `delete` the pointer separately once done with it, since
  `Dispose()` deliberately does not `delete this`. Practically harmless in every instance found
  (each process exits immediately afterward, so the OS reclaims the leaked allocation), but a
  consistently-repeated gap worth fixing if any of these files is ever used as a copy-paste
  template for a longer-running application. **Positive counter-example found in the same
  session**: `demo_gamer_profile_privileges`'s `ProfileGame` correctly `Dispose()`s **and**
  `delete`s its owned `GamerProfile*` in both its destructor and its gamer-switching logic, proving
  the correct pattern is well understood and achievable in this codebase — the `NetworkSession` gap
  looks like a specific, repeated omission around that one class, not a project-wide habit that
  extends to every disposable type. See each file's own `.audit.md` report
  (`examples/demo_*/src/*.audit.md`, `audit/tools/net/*.audit.md`) for the individual instances.

## Per-shard notes: `microsoft-devices`

- **`microsoft-devices` shard note: FNA has zero reference material here too**, the same situation as
  `xna-net`/`xna-gamerservices` (confirmed via `find` -- no Sensor/Accelerometer/Compass/Gyroscope/Motion/
  Vibrate files anywhere in the local FNA tree; FNA is desktop-only and never implemented WP7-specific
  sensor/vibration APIs). All 54 files (`Accelerometer`/`Compass` + shared `SensorBase<T>` infrastructure,
  `Gyroscope`/`Motion` + backend interfaces/diagnostics, Android-specific backends + `VibrateController` +
  shared lifecycle primitives) were audited across three parallel passes. This is, by a wide margin, **the
  most thoroughly self-audited subsystem found in this entire project audit to date**: nearly every
  non-trivial design decision across all 54 files cites a specific prior task ID tied to a real,
  previously-found-and-fixed defect from at least two prior "external audit" rounds (dated 2026-07-17/18),
  several confirmed via actual ThreadSanitizer runs.
- **MEDIUM finding, confirmed in all four sensor classes (`Accelerometer`, `Compass`, `Gyroscope`,
  `Motion`): `Dispose(bool disposing)` is re-declared `public` in every one of them, even though the base
  class (`SensorBase<T>`) correctly declares it `protected`.** This breaks the standard C++/.NET
  `Dispose(bool)` idiom -- any external caller can call e.g. `accel.Dispose(false)` directly, and each
  class's own `!disposing` early-return branch marks the object `disposed_ = true` **without** running any
  real cleanup (no `Stop()`, no instance-count decrement, no SDL-subsystem-hold release for
  `Accelerometer`/`Gyroscope`; no `control_->owner` nulling for `Compass`/`Motion`). Result: a real,
  externally-reachable resource leak plus a permanently-broken object (every subsequent call throws
  `ObjectDisposedException`). Originally confirmed in `Accelerometer`/`Compass` by one fork; the
  `Gyroscope`/`Motion` sibling fork's own completed audit did not flag this pattern despite a thorough
  thread-safety review, so it was independently re-verified directly (grepping all four headers' `Dispose
  (bool)` access-specifier placement against the base class's) and confirmed present in all four -- the
  per-file reports for `Gyroscope.hpp`/`Motion.hpp` have been updated accordingly. See
  `include/Microsoft/Devices/Sensors/Accelerometer.hpp.audit.md` (and the sibling `Compass`/`Gyroscope`/
  `Motion` reports) for the full failure-scenario writeup.
- **Positive, independently significant: `SensorBase<T>`'s polling-thread/event-callback design is
  exceptionally mature** -- every mutable field shared between the game thread and a backend callback
  thread is correctly mutex-guarded, the lock is consistently released before any event `Raise()` call
  (avoiding reentrant-handler deadlock), and the `ClaimDisposalOnce()`/`WaitForDisposalToComplete()`/
  `DisposalTerminalStateGuard` disposal-race design correctly closes both a double-cleanup race and a
  "losing thread waits forever if the winning thread's cleanup throws" gap.
- **Positive: this subsystem does NOT share the confirmed `FileDialog`/`MessageBox` mutex-scoping
  use-after-free bug** (see "Recurring memory/resource risk patterns" above) -- specifically checked given
  the structural similarity (a swappable backend behind a mutex): `VibrateController::Start`/`Stop`/
  `getIsSupportedProperty`/`getDeviceNameProperty`/`StartLeftRight` all correctly hold `backendMutex_` for
  the full duration of the `backend_->...()` call, never releasing the lock before dereferencing.
- **Positive: `AndroidCompassMath.hpp`/`AndroidMotionMath.hpp`'s coordinate-system math was independently
  re-derived from first principles (standard Hamilton quaternion convention) and found fully correct** --
  no sign/axis/transpose-direction error, the same class of bug confirmed elsewhere this session in
  `EffectParameter`'s Matrix transpose inversion (`xna-graphics` shard). `AndroidMotionMath.hpp`'s own doc
  comment additionally, honestly flags a genuine, unfixed, out-of-scope observation:
  `ConvertAndroidPortraitToXnaLandscape()`'s landscape remap is a reflection (determinant -1), which no
  quaternion can represent -- correctly scoped as outside its own task's mandate rather than silently
  absorbed or hidden.
- **Positive: `AndroidSensorBridge.cpp`'s native-JNI-callback boundary (the deepest concurrency code in this
  shard) was hand-traced end-to-end and found correct** -- `workerThreadId_` captured once under lock (not
  re-derived from `get_id()`), a single-claimant pattern for concurrent `Stop()` calls, bounded waits with an
  `abandoned_` fallback for a genuinely wedged native call, and every callback wrapped in `try/catch` to
  prevent `std::terminate()`.
- No other MEDIUM+ findings across the shard's remaining 52 files -- everything else (readings, event-args,
  exceptions, enums, `DevicesShutdownCoordinator`, `SdlSubsystemMutex`, `SdlHapticVibrateBackend`,
  `SdlSensorSubsystem`) is correct, several files notably citing specific archived MSDN page numbers to
  justify NOXNA-extension claims rather than asserting parity without evidence.

## Positive pattern: `cmake/Tests/*.cmake` consistently discloses known backend limitations rather than hiding them

- **`build-cmake-tests` shard (14 files, all audited): a recurring, strong positive pattern across
  multiple backends** of registering a test that is known to fail (or partially fail) against a
  specific, already-tracked defect, with the CMake registration's own comment stating precisely
  which check fails and why, rather than quietly excluding the test or deleting it:
  - `WebGpuTests.cmake`'s `WebGPU_Msaa` (WEBGPU-58): left registered and failing on purpose — "3 of
    6 checks in this test currently FAIL because genuine multisample-resolved rendering does not
    yet work end-to-end," directly consistent with CLAUDE.md's own instruction not to overclaim
    WebGPU parity.
  - `VulkanTests.cmake` Tasks 305-309 (Task 868, `ApplyBlendState` hardcoding a single blend
    equation): five consecutive registrations each carry a specific per-check pass/fail prediction
    ("expect Check A (Subtract) to fail per Task 868 — Vulkan always hardcodes
    `VK_BLEND_OP_ADD`"), a notably precise level of documented known-limitation disclosure.
  - `BgfxTests.cmake` Task 923 (BlendState alpha-factor independence) and Task 879 (RenderTarget2D
    MSAA): both empirically prove a failure is an environment/sandbox limitation (confirmed via
    3 independent probes; confirmed by the same test passing cleanly under bgfx's Vulkan renderer)
    rather than a real CNA defect, following this project's own established precedent for that
    class of finding (Task 448's occlusion query).
  - `Dx3Tests.cmake` confirms the already-known `Dx3_SpriteBatch` test (2/10 checks failing per this
    session's own prior finding) is a genuine, live CTest registration, not silently excluded.
  This is a genuinely strong project-wide discipline worth preserving: known gaps stay visible as
  red CTest results with a documented reason, rather than being hidden.

## Repo-hygiene finding: root `.gitignore`'s `build*` pattern silently ignores this audit's own manifest files

- **MEDIUM, discovered while committing the `build-cmake`/`build-cmake-tests` manifest shard
  files**: the root `.gitignore`'s first line is a bare `build*` (line 1), evidently intended to
  ignore a generic ad-hoc `build/` directory (all the project's real CMake build directories are
  separately, explicitly listed as `cmake-build-<backend>/*`, which do NOT start with `build` and
  are unaffected). Because gitignore patterns without a leading `/` match at any path depth against
  the basename, `build*` also matches `audit/manifest/build-ci.md`, `audit/manifest/build-cmake.md`,
  `audit/manifest/build-cmake-tests.md`, and `audit/manifest/build-root.md` — the four manifest
  shard files this very audit created to track the `build-ci`/`build-cmake`/`build-cmake-tests`/
  `build-root` shards. `build-ci.md`/`build-root.md` happen to already be tracked (added with
  `git add -f`, or added before this ignore line existed) and so continue to update normally once
  tracked (gitignore has no effect on already-tracked paths), but `build-cmake.md`/
  `build-cmake-tests.md` were never yet committed and are silently invisible to plain `git add`/
  `git status` — confirmed via `git status --porcelain --ignored=matching audit/`, which lists
  exactly these two files with `!!`. Required `git add -f` to stage them. This is a real,
  currently-live repo-hygiene hazard: any future file or directory anywhere in this repository
  whose name happens to start with `build` (not just inside `audit/`) is silently untracked by
  default with no warning from a plain `git add .`/`git status`. Not fixed here (`.gitignore` is
  outside this audit's `audit/**/*.md`-only scope) — flagging for the project owner to narrow the
  pattern (e.g. `/build/` or `/build*/`, matching the already-precise `cmake-build-*/*` entries
  immediately below it) rather than the current unanchored, extension-less `build*`.

## Per-shard notes: `tests-xna-framework-core` (46 files) + `tests-cna-input` (5 files) + `tests-misc` (1 file)

- **HIGH finding: `GameTests.cpp` and `GraphicsDeviceManagerTests.cpp` both have zero real test
  coverage** (each is a 2-line file with only a comment explaining that `Game`/`GraphicsDeviceManager`
  require a live SDL window). This leaves the two confirmed production HIGH bugs from the
  `xna-framework-core` production shard -- `Game::UnloadContent()`'s dead-hook behavior
  (`include/Microsoft/Xna/Framework/Game.hpp.audit.md`) and `GraphicsDeviceManager`'s device-event-
  forwarding gap (`GraphicsDeviceManager.hpp.audit.md`) -- completely untested, so neither bug would be
  caught by CI today. `GameWindowTests.cpp` (147 lines) demonstrates the correct alternative pattern
  already used elsewhere in this same test directory: attempt a real SDL window and `GTEST_SKIP()` when
  no display is available, rather than shipping an empty file. Recommend both `GameTests.cpp` and
  `GraphicsDeviceManagerTests.cpp` be rewritten to follow the `GameWindowTests.cpp` pattern.
- **MEDIUM finding: `GameCrashTest.cpp` is a dead test file** -- all 24 lines are commented out behind
  an `#ifdef XNA5` gate referencing `setTargetElapsedTimeProperty(nullptr)`, which appears to reference a
  stale/different API shape than the current `Game` class exposes. Contributes no coverage and should
  either be revived against the current API or removed.
- **No contradiction found (verified, not a defect): `RayTests.cpp`'s "not yet implemented" comment for
  `BoundingFrustum::Intersects(Ray)` and `BoundingFrustumTests.cpp`'s passing `Intersects(ray)` calls are
  NOT in conflict.** Direct read of `src/Microsoft/Xna/Framework/BoundingFrustum.cpp` (lines 225-265)
  confirms the implementation fully handles the `Contains`/`Disjoint` cases (which both test files
  exercise) and only throws `System::NotImplementedException` for the general boundary-crossing
  `Intersects` case -- itself a faithful, already-documented match of FNA's own identical limitation.
  Both test files correctly test only the implemented branches; no staleness or test-authoring bug here.
- Further confirmed instances of the already-tracked raw-`std::`-exception-instead-of-`System::`
  -exception-type cross-cutting pattern, this time in test assertions rather than production code
  (`EXPECT_THROW(..., std::invalid_argument)` etc.): `MatrixTests.cpp` (`CreatePerspective*` validation
  tests), `BoundingBoxTests.cpp`, `BoundingSphereTests.cpp`, `CurveKeyCollectionTests.cpp`,
  `CurveTests.cpp`, `GameComponentCollectionTests.cpp`, `GameServiceContainerTests.cpp`,
  `TitleContainerTests.cpp`.
- Positive: `ColorTests.cpp`'s `SizeIsLargerThanFourBytesVtablePresent`/
  `ConstructedFromRawRgbaBytesYieldsCorrectComponents` and `FrameworkDispatcherTests.cpp`'s
  `UpdateDoesNotDeadlockWhenBufferNeededDisposesTheInstance` are both genuine, well-targeted regression
  tests tied to real, previously-fixed bugs (a vtable/raw-pointer-cast bug and a dispatcher-deadlock task,
  respectively) rather than incidental coverage.
- Positive: `tests/PackedVectorGolden.md`'s golden bit-packing values were independently derived via a
  separate Python implementation of FNA's packing formulas rather than copied from the C++ implementation
  under test -- the correct approach to avoid a shared-bug blind spot between test and implementation.
- `tests-cna-input` (5 files: `ClipboardTests.cpp`, `InputDevicesHotplugTests.cpp`,
  `InputDevicesTests.cpp`, `PowerTests.cpp`, `SensorsTests.cpp`) -- all clean, consistently using
  dependency-injected fake backends (`SetSystemDeviceBackendForTests`/`SetSystemPowerBackendForTests`/
  `SetSystemSensorBackendForTests`) to get deterministic coverage of OS-facing NOXNA singletons without
  relying on CI having predictable real hardware.

## Test-coverage gaps for already-confirmed production defects

- **`ContentReaderExternalReferenceTests.cpp` has no test for the confirmed HIGH
  `ContentReader::ReadExternalReference<T>()` absolute-path-escape gap** (`ContentReader.hpp`/`.cpp`,
  `xna-content` shard) -- every existing test constructs a relative (`..`-style) escape attempt, none
  an absolute-path one, so the test suite would not have caught this finding on its own. A sibling
  resolver, `CnjSourceFileSafetyTests.cpp`, already has a working, tested containment pattern that
  could inform a fix.
- **`tests/CNA/Devices/FileDialogTests.cpp` and `MessageBoxTests.cpp` both lack a concurrent-
  backend-swap test for the confirmed use-after-free** (`FileDialog.cpp`/`MessageBox.cpp`, "Recurring
  memory/resource risk patterns" above) -- neither file exercises `SetBackendForTesting()` racing a
  live call the way the confirmed bug requires, consistent with that bug being found via direct code
  reading in this audit rather than by the project's own existing test suite.
- **CONFIRMED, two of the `xna-graphics` shard's HIGH findings are not merely untested by
  `tests-xna-graphics` but actively BAKED IN as the asserted-correct expected behavior:**
  - **`EffectParameterTests.cpp`'s `SetValueTransposeRawLayoutDiffersFromSetValue`** asserts the
    exact inverse of real FNA's `SetValue`/`SetValueTranspose` storage convention as correct --
    independently re-verified against FNA's actual `EffectParameter.cs` source. The sibling
    `EffectAnnotationTests.cpp`'s `GetValueMatrixRoundTrip` gets the identical convention right,
    confirming (as the production-code audit already concluded) that the right convention was known
    and correctly applied elsewhere in this same codebase -- this looks like a transcription slip
    that then got its own matching (wrong) test written against it, not a deliberate design choice.
  - **`GraphicsExceptionTests.cpp`** has 6 tests explicitly asserting `DeviceLostException`/
    `DeviceNotResetException`/`NoSuitableGraphicsDeviceException` inherit/catch as
    `std::runtime_error`, contradicting the already-confirmed finding that these three should derive
    from `System::Exception` instead. Fixing either production bug now requires updating the
    corresponding tests in the same change, not just the implementation.
  - Both are now added to this audit's "test suite bakes in the bug" pattern, alongside the earlier
    `GamerServicesDataTests.cpp` (`PropertyDictionary` raw-`std::`-exception assertions, `tests-xna-
    gamerservices`/`tests-xna-net` shard) instance -- a recurring shape worth watching for
    specifically whenever a production exception-type or math-convention finding is cross-checked
    against its own test file.
- **Confirmed clean, by exhaustive design intent**: 8 of the 10 `xna-graphics` production findings
  are simply *missed* by `tests-xna-graphics` (no test constructs the triggering scenario at all)
  rather than actively baked in -- including the `SpriteFont`/`SpriteBatch` default-character UB, the
  `SpriteEffects` combined-flags OOB read (confirmed the shared `RecordingSpriteBatchBackend.hpp`
  test double would faithfully capture such a call if one existed -- a missing test case, not a
  tooling gap), `EffectParameter.Elements`/`StructureMembers`, `BasicEffect.Parameters`,
  `VertexBuffer`/`IndexBuffer`'s missing destination offset, `PackedVector` truncate-vs-round (only
  exact-integer/zero test inputs used for `Byte4`/`Short2`/`Short4`), `VertexPositionColor`'s missing
  `IVertexType`, and `RenderTargetCube`'s missing `Dispose(bool)` fix. `Texture2DTests.cpp` (1085
  lines, the shard's largest/most rigorous file) also has zero test for `GetTypeName()`, confirming
  by total absence the already-flagged production defect there, while sibling `Texture3DTests.cpp`/
  `TextureCubeTests.cpp` both correctly test it.

## Per-shard notes: `tests-cna-internal` (65 files)

Exceptional-quality test engineering throughout -- real fixtures over hand-crafted data,
mechanism-level regression tests rather than symptom-only checks, and several files that document
catching their own prior test-quality failures. Only two findings, both MEDIUM or below:

- **MEDIUM**: `PictureLibraryIndexTests.cpp` has no symlink-cycle or permission-denied-subdirectory
  test for the picture scanner, unlike the equivalent music-scanner tests in the same shard --
  unverified whether the underlying scanner code shares the same risk, but the test-coverage
  asymmetry between two structurally similar scanners is itself worth noting.
- **LOW**: `DecimalDateTimeContentTypeReaderTests.cpp`'s MSVC-only `DecimalReader`
  test/registration exclusion (`#if !defined(_MSC_VER)`) has no stated rationale anywhere in the
  file, unlike every other platform-conditional test in this shard, which document their reasons.

Two files stand out as reference-quality for the whole project: `ENetBackendTests.cpp` (2083 lines,
exhaustive pending-send-queue state-machine coverage) and `SoundEffectContentTypeReaderTests.cpp`
(1105 lines, precise mechanism-level format-parsing verification).

`LzxDecoderFuzzTests.cpp` and `XnbContainerFuzzTests.cpp` are both genuinely adversarial fuzz
harnesses: real-fixture mutation (not synthetic byte generation), deterministic non-wall-clock
seeds, and precisely-reasoned exception allowlists. `XnbContainerFuzzTests.cpp` specifically
hard-fails on `std::bad_alloc`, treating an allocation-bomb near-miss as a real bug rather than an
acceptable rejection -- one of the sharpest fuzz-harness design choices found in this entire audit.

This shard directly confirms two standing cross-cutting questions:
- **`GamerServicesDispatcherHangRegressionTest.cpp`** substantively reproduces the confirmed
  `GamerServicesDispatcher::UpdateAsync()` permanent-no-op-once-initialized hang (see
  `xna-gamerservices` per-shard notes above) via a genuinely separate, watchdog-monitored OS
  process -- plus two related bugs (a `GetAchievements` hang and a gamer-leak).
- **`TwoProcessLoopbackTest.cpp`**'s fresh-process architecture is genuinely necessary, not
  incidental engineering overhead: only two independent OS processes can each hold their own
  `NetworkSession` given the project's documented one-session-per-process constraint. Its 3-process
  host-migration test carefully eliminates races via a "lowest remaining wire id" deterministic
  promotion rule, matching the identical technique already confirmed in `tools/net/
  net_two_process_harness.cpp` (`tools-net` shard).

## Per-shard notes: `tools-avatar-builder` (15 files)

`tools/avatar_builder/generate_body.py`'s `fix_automatic_weights()` (lines 169-199) is the
confirmed fix location for the long-standing, multi-session "infinite slab" bone-weight-blending
defect (Pants weighted to Shoulders): the original bend-joint blend tested only axial distance from
a joint, which for a laterally-pointing bone (shoulder) describes an infinite slab perpendicular to
the bone axis, sweeping through the torso/hips/legs. The fix adds a `perpendicular = (offset - axis
* signed_dist).length` check, bounding the region to a cylinder around the joint axis. Every
downstream consumer calls this same shared function, so no other file in the pipeline could have
silently regressed it.

`tools/avatar_builder/generate_animations.py`'s own top-of-file docstring is stale, describing only
5 of the file's actual 31 built animation clips (`_GENERIC_BUILDERS` alone has 11 entries,
`Stand0`-`Stand7` plus `Wave`/`Clap`/`Celebrate`, plus 10 `_FEMALE_BUILDERS`/10 `_MALE_BUILDERS`) --
LOW, the same class of stale-scope-note finding already seen in `demo_achievement_showcase.hpp`.
This directly resolves `validate_gltf.py`'s own audit-flagged ambiguity: `REQUIRED_ANIMATIONS`'
8-name `Stand0`-`Stand7` requirement is correct and matches `generate_animations.py`'s real,
current behavior -- the project's README, not the validation gate, is the stale artifact.

`_raise_upper_arm`/`_fold_lower_arm`'s doc comments in `generate_animations.py` document a
genuinely non-obvious, empirically-discovered Blender rigging fact worth calling out as exemplary
engineering documentation: a child bone's local rotation composes with its parent's *current*
(possibly already-rotated) world transform, not its rest transform, so the same local angle can
require an opposite sign depending on whether the parent has already moved -- explicitly flagged as
something to re-verify empirically per call site rather than assumed to generalize to unaudited
bone pairs (e.g. `UpperLeg`/`LowerLeg`).

## Standing investigation resolved: `Dx3_SpriteBatch` 2/10-failing checks

Two independent forks auditing `examples/dx3_spritebatch_test.cpp` (`examples-tests-dx3` shard)
both confirm, via static analysis (this audit-only pass had no DX3/Wine build environment to
re-run the actual binary), the two halves of this project's own persistent-memory-tracked
investigation are of genuinely different natures and should be triaged separately:

- **Check D (zero-alpha blend) -- CONFIRMED test-authoring bug, not a DX3 backend defect.** The
  fixture constructs a **non-premultiplied** `Color(255, 0, 0, 0)` texel and asserts the
  destination is left completely untouched after drawing under `BlendState::AlphaBlend`. Real
  XNA/FNA's `AlphaBlend` preset uses **premultiplied**-alpha blend factors
  (`ColorSourceBlend = One`, `ColorDestinationBlend = InverseSourceAlpha`) -- one fork hand-derived
  the actual resulting formula and got `(255, 6, 7)`, not the asserted `(5, 6, 7)` "destination
  untouched." A faithful DX3 implementation of the real blend equation would legitimately fail
  this check while being behaviorally correct. Fix: construct the fixture with a genuinely
  premultiplied `Color(0, 0, 0, 0)` texel instead (or explicitly document straight-alpha semantics
  as an intentional CNA deviation, if that's ever the actual intent).
- **Check G (180° rotation about center) -- independently re-derived and confirmed sound; a
  failure here means a real DX3 backend defect, not a test bug.** Both forks independently
  re-derived XNA's real origin-scaled corner-rotation formula from scratch and it exactly matches
  the test's own math and pixel assertions (`ReadPixel` bottom-right = Red, top-left = Yellow for
  a point-reflection about the sprite's center). If this check fails in CI, look for a real pivot/
  origin-scaling or rotation-sign defect in DX3's own drawing path.

This resolves the open question from `[[project_dx3_spritebatch_test_failure]]` (persistent
memory): Check G is likely a real rotation-math bug, Check D is likely a test-authoring bug around
premultiplied-alpha semantics -- both hypotheses independently confirmed correct by static
analysis, not merely repeated.

## Test blind spot: identity-matrix fog tests can't distinguish object-space from view-space fog (D3D11/D3D12)

`examples/d3d11_smoke_test.cpp`/`examples/d3d12_smoke_test.cpp`'s fog checks set
`World = View = Projection = Identity` throughout, which makes object-space and view-space vertex
Z coordinates numerically identical -- these tests cannot distinguish a correct view-space fog
implementation from the already-confirmed EasyGL bug where fog reads a raw object-space vertex
attribute (see the standing `[[feedback_easygl_fog_object_space_only]]` memory: "fog shader reads
raw local vertex Z, ignores World/View entirely"). D3D12's own test is an explicit, stated reuse of
D3D11's fixture, so it inherits the identical blind spot rather than independently re-deriving it.
This doesn't confirm D3D11/D3D12 share EasyGL's fog bug -- it confirms the test suite has no way to
tell either way, for either backend.

## `docs/` shard: staleness findings from cross-checking against this session's own confirmed production findings

Three parallel forks audited all 72 `docs/*.md` files, adapting the report structure to check
whether each doc's claims still match the current, actual code/backend/test state this session has
already independently confirmed. Selected findings (full detail lives under each
`audit/docs/<file>.md.audit.md`):

- **MEDIUM** -- `docs/coverage.md` claims ".xnb binary support entirely absent," directly
  contradicted by this session's own confirmed real, substantial `.xnb` reader pipeline
  (`SoundEffectContentTypeReader.cpp` and 10+ sibling readers, all AUDITED clean in
  `tests-cna-internal`/`xna-content` this session).
- **MEDIUM** -- `docs/d3d9-backend.md` and `docs/cnatests-mingw-setenv-proposal.md` directly
  contradict each other under the *same* task ID (`D9-123`) on whether `CnaTests` builds under
  D3D9 -- the setenv-proposal doc's detailed implementation record is far more credible;
  `d3d9-backend.md`'s bullet is almost certainly a stale holdover.
- **MEDIUM** -- `docs/cna_audio_deep_audit_2026-07-17.md` is a genuinely rigorous independent audit
  (confirmed via git log to have triggered 80+ `AUD-XX` remediation commits) but carries no status
  banner noting its flagship P0 finding (XNB format narrowness) is now fixed -- contradicted
  directly by this session's own clean audit of the current `SoundEffectContentTypeReader.cpp`.
- **MEDIUM** -- `docs/dx3-backend.md` claims SpriteBatch "fully verified," contradicted by the
  `Dx3_SpriteBatch` 2/10-failing-checks investigation above (a real rotation-math bug + a
  test-authoring bug) -- stale.
- **MEDIUM** -- `docs/easygl_bugs.md`'s fog-bug row claims fog is computed "after the WVP
  transform (clip-space Z)"; direct source read shows the current shader uses `aPos.z`, a raw
  object-space vertex attribute, with an adjacent comment explicitly saying "object-space" --
  contradicts the doc's own characterization, exactly the kind of drift its own 2026-07-11
  staleness banner already anticipated.
- **MEDIUM** -- `docs/gdm-coverage.md`'s event table shows all `GraphicsDeviceManager` events
  "supported" but never mentions the confirmed HIGH finding (this session's own
  `GraphicsDeviceManager.cpp` audit, `xna-graphics` shard) that it never forwards `GraphicsDevice`'s
  own backend-triggered lifecycle events.
- **MEDIUM** -- `docs/graphics-resource-lifetime.md` and `docs/graphicsresource-fna-audit.md`
  directly contradict each other on whether a `GraphicsDevice` resource-tracking list exists (one
  says yes with detail, the other says "Gap 1: no list exists") -- resolved in favor of the latter
  being stale.
- **LOW** -- `docs/devices-api-coverage.md`'s "Cross-cutting members" table doesn't flag the
  confirmed `Dispose(bool)` public-vs-protected mismatch across all 4 `Microsoft::Devices::Sensors`
  classes (`microsoft-devices` shard).
- **LOW** -- `docs/graphicsdevice-fna-audit.md` (dated 2026-06-26) independently re-verified via
  direct grep against current source: all 7 "missing NOXNA tag" claims and 3 "missing API" claims
  are still accurate today -- the best-aged doc in the batch, flagged only for one easy-to-miss
  inconsistency (`Indices()`/`Indices(const IndexBuffer*)` live inside the NOXNA-helpers section but
  aren't themselves NOXNA-tagged).
- **LOW** -- `docs/README.md` predates several newer docs, most notably the audio deep-audit.

Standouts for quality (no findings, exemplary self-correction discipline): `docs/d3d12-backend.md`,
`docs/basiceffect-support.md`, `docs/input-fna-fidelity.md` (540 lines, includes a rare case of
*correctly declining* to replicate a bug present in FNA itself), `docs/input-public-api-frozen.md`
(compile-time-enforced, cannot drift undetected), `docs/graphics-backend-feature-matrix.md`
(visibly self-correcting, the most trustworthy status doc in the shard).

**Pattern for this section**: this project's own status/coverage docs are, overwhelmingly, honest
and carefully written at the time they're authored -- nearly every finding above is the doc simply
not being revisited after the underlying code/finding changed, the identical
"documentation-rot, not fabrication" pattern already flagged for `cmake/Tests/*.cmake` header
comments and the `examples-tests-easygl` batch elsewhere in this document.

## `examples-demo_devices` shard: vendored-Android-Java scope question resolved

11 `org/libsdl/app/*.java` files (SDL3's standard Android Activity/HID-device/audio-manager Java
glue) live under `examples/demo_devices/android/.../app/src/main/java/org/libsdl/app/` -- not under
`third_party/**`/`vendor/**` -- so per `AUDIT_SCOPE.md`'s own classification rule 2, they were
correctly scoped as AUDIT-eligible rather than `third-party-vendored` EXEMPT, even though their
content is, in fact, unmodified upstream SDL3 Android glue (confirmed via `grep -il
"openeggbert|devicesdemo|CNA"` across all 11 files: zero matches). Audited with an appropriately
lighter-touch structural pass given this. No findings.

## Additional test-coverage/robustness gap: `headless_resource_backends_test.cpp`

Checks A/B (`TextureCube`/`Texture3D` round-trip) are unconditional `check(true, ...)` calls with no
`try`/`catch`, unlike every other check in this file's own shard -- a real regression in either path
would crash the test process outright instead of reporting a clean `FAIL`. LOW; matches the general
"a test's own robustness against the very regression it's meant to catch" class of finding already
noted for other shards this session.

## Pass 3: Systematic API-surface completeness sweep (Microsoft.Xna.Framework.Graphics, vs. real xn65 XML reference)

Per this project's own standing finding that FNA is not authoritative for API *surface* (member
existence) -- FNA itself sometimes omits real XNA 4.0 members the original Microsoft assemblies
had -- this sweep used the actual Microsoft-shipped Windows XNA 4.0 reference documentation at
`/rv/data/library/github.com/borgesdan/xn65/references/Windows/Microsoft.Xna.Framework.Graphics.xml`
(extracted XML doc comments from the real, shipped `Microsoft.Xna.Framework.Graphics.dll`) as ground
truth for "does this member exist in real XNA 4.0," cross-referenced against CNA's actual declared
surface in `include/Microsoft/Xna/Framework/Graphics/*.hpp`.

**Methodology and scale**: 781 raw XML member entries, resolving to 95 real top-level types (99 minus
4 nested `.Enumerator` helper types) and 635 individually-checked property/method/field/event members,
plus a separate pass enumerating every named value of the 27 pure-enum types in this namespace (their
values are documented as nested `<param>` tags under the enum's own `<member name="T:...">` entry, a
different XML shape than class members, requiring a second extraction pass to cover correctly).

**Result: zero types entirely missing (95/95 real types have a corresponding CNA header) and zero
missing enum values (27/27 enums, every named value present).** At the member level, an initial naive
name-match pass flagged 28 types with "possibly missing" members, but manual verification resolved all
but 2 real items as false positives caused by systematic, expected C#-to-C++ idiom translation, not
actual gaps:
- **`Dispose()` on 7 classes** (`BlendState`, `DepthStencilState`, `RasterizerState`, `SamplerState`,
  `OcclusionQuery`, `SpriteBatch`, `VertexDeclaration`) -- false positive. All 7 correctly inherit
  `Dispose()` from `GraphicsResource` rather than redeclaring it, exactly matching real XNA's own
  inheritance-based design.
- **`Finalize()` on `GraphicsDevice`/`GraphicsResource`** -- false positive. `.NET`'s GC finalizer has
  no C++ equivalent; `GraphicsResource`'s real C++ destructor (`~GraphicsResource() override`) calls
  `Dispose(false)` directly, the correct idiomatic translation of the same concept (confirmed via the
  class's own doc comment: "disposing false = called from finalizer").
- **`GetEnumerator()`/non-generic `IEnumerable.GetEnumerator()`/`Item` indexer on 11 collection
  classes** (`DisplayModeCollection`, `EffectAnnotationCollection`, `EffectParameterCollection`,
  `EffectPassCollection`, `EffectTechniqueCollection`, `ModelBoneCollection`, `ModelEffectCollection`,
  `ModelMeshCollection`, `ModelMeshPartCollection`, `SamplerStateCollection`, `TextureCollection`) --
  false positive. All 11 have `operator[]` (the `Item` indexer's correct C++ translation); the 9 that
  are genuinely `IEnumerable` in real XNA also have `begin()`/`end()` (confirmed
  `SamplerStateCollection`/`TextureCollection` are indexer-only in real XNA too, per the XML itself --
  correctly not needing iterator support).
- **Explicit-interface members** `IEffectLights.LightingEnabled` (`EnvironmentMapEffect`,
  `SkinnedEffect`) and `IVertexType.VertexDeclaration` (3 of 4 vertex structs) -- false positive for
  these specific cases. C#'s explicit-interface-implementation member names (which include the
  interface's own namespace, e.g. `Microsoft#Xna#Framework#Graphics#IEffectLights#LightingEnabled`)
  are correctly translated to plain `getLightingEnabledProperty()`/`setLightingEnabledProperty()
  override` and `getVertexDeclarationProperty() const override` in CNA, not literal name matches.
- **`op_Equality`/`op_Inequality` on `VertexElement` + 4 vertex structs** -- false positive.
  `operator==`/`operator!=` are correctly implemented under their real C++ operator-overload spelling.

**Two genuine findings survived verification:**

- **NEW, MEDIUM -- `DisplayMode.TitleSafeArea` and `DisplayMode.ToString()` are both real XNA 4.0
  members, present in FNA with trivial one-line implementations, and entirely absent from CNA's
  `DisplayMode.hpp`.** FNA's `TitleSafeArea` is `new Rectangle(0, 0, Width, Height)` and its
  `ToString()` is a simple `"{{Width:...Height:...Format:...}}"` formatter (`FNA/src/Graphics/
  DisplayMode.cs` lines 52-58, 105-113) -- both are FNA-confirmed-real, trivially portable, and
  simply never added to CNA's `DisplayMode` class, which otherwise correctly implements `Width`/
  `Height`/`Format`/`AspectRatio`/`operator==`/`operator!=`/`GetTypeName()`. Not a behavioral bug in
  anything existing, but a real, clean completeness gap with no known workaround for the missing
  property (title-safe-area-aware UI layout code has no CNA equivalent to call).
- **RE-CONFIRMED (not new) -- `VertexPositionColor` is missing `IVertexType`.** Independently
  re-derived via this systematic sweep: `VertexPositionColor.hpp` declares `struct
  VertexPositionColor` with no base class and only a `getVertexDeclarationStatic()` static helper,
  while its 3 siblings (`VertexPositionColorTexture`, `VertexPositionNormalTexture`,
  `VertexPositionTexture`) all correctly declare `: public IVertexType` and implement
  `getVertexDeclarationProperty() const override`. This matches and independently corroborates the
  `xna-graphics` shard's own already-recorded finding (listed among the "8 of 10 production findings
  simply missed by `tests-xna-graphics`" in the Test-coverage-gaps section above) via a completely
  different method (XML-reference cross-check vs. incidental per-file review) -- a genuine second
  confirmation of the same defect, not a duplicate report of it.

**Confidence assessment**: this sweep is genuinely thorough for `Microsoft.Xna.Framework.Graphics`
specifically -- every real member in the reference XML was individually checked, not sampled, and
every flagged discrepancy was manually resolved (not left as an unverified false-positive list). It
does NOT cover the other 8 `Microsoft.Xna.Framework.*` namespaces (`Audio`, `Content`, `GamerServices`,
`Input`, `Media`, `Net`, `Storage`, plus the top-level `Microsoft.Xna.Framework` namespace itself) --
the `xn65` reference tree has separate XML files for each
(`Microsoft.Xna.Framework.xml`, `.GamerServices.xml`, `.Net.xml`, `.Storage.xml`, `.Video.xml`,
`.Xact.xml`, `.Input.Touch.xml`, plus `.Avatar.xml` and `.Game.xml`) that this pass did not have
scope to process. Extending this exact method to those namespaces is the natural continuation of
Pass 3 and would very likely be similarly high-signal, given how clean this result was for the
Graphics namespace specifically (2 genuine gaps out of 635 members checked).

## Pass 3 continued: Systematic API-surface completeness sweep (Net / GamerServices / Xact-Audio)

Same method as above (real xn65 Windows XNA 4.0 XML reference vs. CNA's actual declared headers),
extended to three more namespaces.

**`Microsoft.Xna.Framework.Net`** (180 raw XML entries -> 23 real types, ~120 members checked):
**23/23 types present, all checked members present**, including every `NetworkSession` async
Begin/End overload, `NetworkSessionProperties`' full `IList<int?>` explicit-interface surface,
`AvailableNetworkSessionCollection`'s indexing/enumeration correctly inherited from its
`ReadOnlyCollection<T>` base (not missing), and all 5 enum types' values matching exactly
(`NetworkSessionEndReason`, `NetworkSessionJoinError`, `NetworkSessionState`, `NetworkSessionType`,
`SendDataOptions`). One LOW convention finding: `NetworkSession::MaxSupportedGamers` (=31) and
`MaxPreviousGamers` (=100) are both real XNA 4.0 fields (confirmed in the XML:
`F:...NetworkSession.MaxSupportedGamers`/`.MaxPreviousGamers`) but are tagged `NOXNA` in CNA's
header -- per this project's own `CLAUDE.md` convention, `NOXNA` should wrap only functionality
that is NOT part of the real XNA 4.0 API, so tagging genuine XNA members this way is a
mistagging, not a functional bug (the values themselves could not be cross-checked against FNA,
which has no `Net` implementation at all -- consistent with this project's own standing note that
FNA omits this entire namespace).

**`Microsoft.Xna.Framework.GamerServices`** (272 raw XML entries -> 37 real types, ~200 members
checked): **37/37 types present, zero gaps found at any level checked** -- the cleanest result of
any namespace swept so far in Pass 3. Every `Gamer`/`GamerProfile`/`GamerPrivileges`/`Guide`/
`SignedInGamer`/`LeaderboardReader`/`LeaderboardWriter`/`Achievement`/`AchievementCollection`/
`PropertyDictionary`/`GamerServicesDispatcher`/`FriendGamer`/`FriendCollection`/`GameDefaults`/
`LeaderboardIdentity`/`GamerPresence`/`GamerCollection<T>` member checked was present and
correctly idiom-translated, including `AchievementCollection`'s dual `operator[](int)`/
`operator[](const std::string&)` indexer overloads (real XNA's `Item(int)`/`Item(string)`),
`SignedInGamerCollection::operator[](PlayerIndex)`, `SignedInGamer`'s `SignedIn`/`SignedOut`
static events, and `GamerServicesDispatcher::InstallingTitleUpdate`. All 8 enum types checked
(`GamerZone`, `MessageBoxIcon`, `NotificationPosition`, `RacingCameraAngle`, `GameDifficulty`,
`ControllerSensitivity`, `GamerPrivilegeSetting`, plus a partial spot-check of the much larger
`GamerPresenceMode`) have matching value sets.

**`Microsoft.Xna.Framework.Audio`** (the XACT-specific subset only -- `Microsoft.Xna.Framework.
Xact.xml` covers `AudioCategory`/`AudioEngine`/`AudioStopOptions`/`Cue`/`RendererDetail`/
`SoundBank`/`WaveBank`, 75 raw entries -> 7 types, ~60 members checked; the much larger
`SoundEffect`/`SoundEffectInstance`/`Microphone`/`DynamicSoundEffectInstance`/etc. surface lives
in the main `Microsoft.Xna.Framework.xml` file, NOT swept in this pass -- a real scope gap, noted
below): **7/7 types present.** `AudioEngine.ContentVersion` (=46), `Disposing` events on all 4
disposable classes, and `Cue.Apply3D` all confirmed present and correct. **One genuine gap found,
of a new kind not yet seen in this sweep**: `AudioCategory.ToString()` is a real XNA 4.0 member
(confirmed in the XML) **missing from both CNA and FNA** -- unlike the earlier `DisplayMode.
ToString()`/`TitleSafeArea` finding (present in FNA, simply never ported to CNA), this is a
genuinely FNA-inherited gap: FNA's own `AudioCategory.cs` has no `ToString()` override either
(only `Equals`/`GetHashCode`), so CNA correctly mirrors FNA's own incompleteness here rather than
diverging from it. LOW severity -- `ToString()` omissions are rarely load-bearing, but worth
recording as a distinct sub-pattern: "real XNA member, absent from FNA too" vs. "real XNA member,
present in FNA, absent from CNA" are different root causes needing different fixes (the former
needs new code written from the XNA spec directly, since there's no FNA implementation to port).

**Confidence and remaining scope**: Net and GamerServices sweeps are as thorough as the original
Graphics sweep (every member individually checked, not sampled). The Audio sweep only covers the
XACT-specific subset (7 of the namespace's real types) -- `SoundEffect`, `SoundEffectInstance`,
`DynamicSoundEffectInstance`, `Microphone`, `AudioListener`, `AudioEmitter`, and the top-level
`Microsoft.Xna.Framework` namespace itself (`Game`, `GraphicsDeviceManager`-adjacent types, math
types, etc.) all remain unswept by this method, along with `Content`, `Input` (Touch-specific XML
exists separately from the already-audited `Input`), `Media`/`.Video.xml`, `Storage`, and
`.Avatar.xml` (CNA's Avatar-related headers live under the `GamerServices` directory by
organizational choice, but the real XNA Avatar API is documented in a separate assembly/XML --
not cross-checked against that separate reference in this pass). Given how consistently clean
these three sweeps have been (3 genuine gaps across ~965 combined members checked), extending
further is very likely still worthwhile but has clearly diminishing urgency compared to the
per-file behavioral findings already documented elsewhere in this file.

## Pass 6: Build/test sanitizer evidence sweep (opportunistic)

Built `CnaTests` for the EasyGL backend (`cmake --build cmake-build-debug -j4`, this project's own
Linux default backend) and ran the full CTest suite twice: once at `-j8` (229+ tests reported
failed/not-run, including an aborted-subprocess partway through -- clearly parallelism-contention
noise, confirmed by re-running individual "failing" tests like `SongTest.ConstructorDoesNotThrowFor
ExistingFile` and the entire `MediaPlayerTest`/`SongTest`/`VideoPlayerTest`/`VideoTest` group (84
tests) in a single filtered process, where they all passed cleanly), then at `-j2` for 375.8s
(**5754 total tests, 96% passed, 229 failed**) to get a reliable baseline. Only 3 backends were
attempted (EasyGL was built and fully tested; Bgfx/Vulkan/SdlGpu/D3D9/D3D11/D3D12/Dx3 were not
attempted in this pass due to time -- a natural continuation for a future opportunistic pass, not
skipped for any environmental reason).

### Reconciliation note: this is a THIRD, previously-undocumented reason `ctest` is unreliable here, distinct from the project's own two already-known reasons

Before detailing the finding below: this project's own `CMakePresets.json` ("tests" preset
description) and `plans/plan_audio20260717.md` (task `P9-BUILD-007`, consulted as secondary context per
D-3 -- not authoritative, but relevant here) already document that running the general suite via
`ctest` (rather than the `CnaTests` binary directly) is unreliable, for two *specific*, different,
already-disclosed reasons: (a) several tests share hardcoded `/tmp/cna_*_test/` scratch paths,
safe under this project's own single-process full-suite convention but racy under `ctest`'s
per-process parallelism (explicitly accepted as an undertaken, deliberately-not-fixed limitation,
per P9-BUILD-007's own account), and (b) `ctest` spuriously reports "unbuilt display-dependent
graphics smoke-test executables" as failed. **Neither of those two already-known reasons is the
`WORKING_DIRECTORY` mechanism below.** The `WORKING_DIRECTORY` gap is deterministic (reproduces at
any parallelism level, including `-j1`, since it's a wrong-directory bug, not a race), affects a
different, much larger set of tests (Media/Audio-tag-parsing/Xnb/ENet/Lzx -- not the `/tmp`-path
tests P9-BUILD-007 describes), and has never been named or root-caused in any pre-existing
project documentation this audit has read. The project's own general advice ("use the binary
directly, not `ctest`") happens to also work around this third, undocumented cause -- which may be
exactly why it was never separately diagnosed: the known workaround silently papers over an
additional, unrelated bug.

### ROOT CAUSE, HIGH severity: `gtest_discover_tests(CnaTests DISCOVERY_MODE PRE_TEST)` has no `WORKING_DIRECTORY` override -- breaks every fixture-file-loading test, invisibly to CI

`cmake/UnitTests.cmake` (line 215) calls `gtest_discover_tests(CnaTests DISCOVERY_MODE PRE_TEST)`
with no `WORKING_DIRECTORY` argument. CMake's own default for this is the target's own runtime
output directory -- confirmed directly in the generated
`cmake-build-debug/CnaTests[1]_tests.cmake`: **every one of the 5507 individually-discovered
`CnaTests` cases** has `WORKING_DIRECTORY /rv/.../cnaaudit/cmake-build-debug` baked in, not the
repo root where `tests/assets/**` actually lives (confirmed: `cmake-build-debug/tests/assets/media/
music/` does not exist). Any test that loads a fixture file by a repo-root-relative path (e.g.
`tests/assets/media/music/Artist One/Album Alpha/01 - Sunrise.ogg`) throws a real
`FileNotFoundException` when run via `ctest`, even though the exact same test passes cleanly when
manually invoked with the repo root as its working directory (confirmed side-by-side for
`SongTest.ConstructorDoesNotThrowForExistingFile`). This single registration gap fully explains
essentially all 229 `-j2` failures: `AudioTagParserTest`, `MediaLibraryIndexTest`,
`PictureLibraryIndexTest`, `PlaylistParserTest`, `SavedPictureStoreTest`, `ThumbnailGeneratorTest`,
`VideoDecoderTest` (real video/audio fixture files), `ENetBackendTest`/`ENetDiscoveryServiceTest`
(network-fixture-dependent subset), `LzxDecoderTest`/`LzxDecoderDifferentialTest`/
`LzxDecoderFuzzTest`, every `*ContentTypeReaderTest`/`XnbBuiltInReaderRegistrationTest`/
`XnbContainerFuzzTest` that loads a real `.xnb`/MonoGame fixture, `ContentManager*XnbTest`,
`ContentReaderExternalReferenceTest`/`ContentReaderTest`, `DynamicSoundEffectInstanceTest`, and the
entire `MediaLibraryTestFixture`/`SongCollectionTest`/`SongTest`/`VideoPlayerTest`/`VideoTest`
group -- every one of these references a real fixture file under `tests/assets/**` by
repo-root-relative path.

**This is invisible to every existing CI workflow, not just unflagged.** All 3 GitHub Actions
workflows in this repo (`build-ci` shard, already audited: `d3d-windows-ci.yml`,
`devices-tests.yml`, `input-ci.yml`) invoke `ctest --test-dir build -L <label>` with a `-L`
label filter (`D3D9`/`D3D11`/`D3D12`, devices-specific, or `input`) -- **none of the three ever
runs the general/default `CnaTests` set this bug affects.** `--test-dir build` only changes where
`ctest` looks for its own `CTestTestfile.cmake`; it does not override each individual test's own
baked-in `WORKING_DIRECTORY` property, so even a hypothetical future unlabeled CI run would still
hit this bug. Net effect: ~220 real unit tests covering Media/Audio-tag-parsing/Xnb-content-pipeline/
ENet-networking/Lzx-decompression have likely **never once passed in any CI run this project has
ever had**, not because they're known-broken and excluded, but because no CI workflow's label
filter happens to include them and the underlying registration bug makes a full unfiltered run
fail loudly enough that it may never have been attempted. Distinct from (though same family as) the
already-documented "3+ confirmed currently-failing CTests with no `WILL_FAIL` annotation"
CI-masking-risk pattern -- this is a structural gap in test *discovery*, not a single mis-registered
test.

**Suggested fix (report-only, no source changed per this audit's scope)**: add
`WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"` to the `gtest_discover_tests(CnaTests ...)` call in
`cmake/UnitTests.cmake`, matching the pattern already correctly applied to individual
`add_test()`-registered golden-image tests in `cmake/Tests/EasyGLTests.cmake`/`VulkanTests.cmake`
(already audited, confirmed correct) for the identical reason.

**Independently confirmed by a second, separately-dispatched pass at this same investigation**:
running `./cmake-build-debug/CnaTests` directly (no `ctest` involved at all, CWD = repo root) gives
**5503 PASSED / 4 SKIPPED (the hardware-dependent Accelerometer/Gyroscope cases below, expected) / 0
FAILED out of 5507** -- a completely clean run, consistent with this project's own historical memory
of a clean `4911 tests / 0 failed` result from an earlier session on a different branch. This
crisply confirms the `ctest`-reported 96%/229-failed number is entirely a CTest-invocation artifact:
the same binary, same tests, same fixture files, 100% clean when CWD is right. That second pass also
found, via `grep -rn "WILL_FAIL" cmake/Tests/*.cmake cmake/UnitTests.cmake`, that **this project has
never used CTest's `WILL_FAIL` mechanism anywhere, for any backend** -- the only related mechanism
in use is `SKIP_REGULAR_EXPRESSION` (WebGPU's own environment-conditional MSAA test, a genuine
environment-based skip, not an expected-failure marker). This means every "currently failing, no
`WILL_FAIL` annotation" finding in this audit (`EasyGL_AvatarRenderer_TintRouting`,
`Bgfx_RenderTargetCube_DepthFormat`, `Bgfx_SkinnedEffect_WeightsPerVertex`,
`EasyGL_GraphicsDevice_ReferenceStencil`, `EasyGL_MRT_TwoAttachments`) is not five independent
oversights -- the project has simply never adopted this CTest feature at all, for any of its
several-dozen already-known-and-disclosed-in-source limitations project-wide. Adopting `WILL_FAIL`
(or an equivalent convention) as a project-wide practice would be a higher-leverage fix than
annotating each already-found instance one at a time.

### Derived finding, MEDIUM severity: `MediaLibraryTestFixture.ObjectGraphIsInternallyConsistent` SEGFAULTs rather than failing cleanly

A genuinely separate robustness gap, currently only reachable because of the `WORKING_DIRECTORY`
bug above: when the picture-library scan silently finds nothing (its expected root directory not
resolving to a real path), `MediaLibraryTestFixture.RootHasTwoChildAlbums` fails cleanly
(`root != nullptr` assertion, `root` is `NULL`) -- but the sibling test
`ObjectGraphIsInternallyConsistent` **crashes with a real SIGSEGV** in the same fixture, confirmed
reproducible via CTest's own `Exception: SegFault` classification. This means some code path
downstream of the same null/empty scan result dereferences it without a null check, rather than
failing as cleanly as its sibling test does. Worth a defensive-`nullptr`-check pass in whichever
`MediaLibraryTestFixture`-adjacent helper walks the object graph, independent of fixing the
root-cause `WORKING_DIRECTORY` bug (a genuinely malformed/incomplete real media library on a user's
machine should also not crash the process).

### NEW, HIGH severity: EasyGL `SetRenderTargets` with 2 attachments only draws to the first one

`EasyGL_MRT_TwoAttachments` (Task 145: `SetRenderTargets({rt0,rt1})`, draw green to attachment0,
verify left=green/right=blue) **fails reproducibly in complete isolation** (re-run alone via
`ctest -R "^EasyGL_MRT_TwoAttachments$"`, not a parallelism artifact): `left=(0,255,0)` [correct,
green] but `right=(0,0,0)` [expected blue, got black] -- the second render-target attachment never
receives its draw output at all. This is a real, previously-undisclosed defect in EasyGL's
multiple-render-target support (`GraphicsDevice.SetRenderTargets` with more than one binding), not
merely an untested edge case -- MRT is a real, documented XNA 4.0 feature and this test was already
registered and presumably passing at some point per its Task 145 comment, so this may be a
regression rather than a day-one gap (not determined which, given this audit is static/point-in-time
only, but `git log` on this file/the underlying `EasyGLGraphicsBackend.cpp` MRT code would settle
it easily for whoever picks this up).

### Confirmed, extends the existing CI-masking-risk list: `EasyGL_GraphicsDevice_ReferenceStencil`

Already-known per its own in-source comment (Task 319/872: "confirmed a universal, not-Vulkan-
specific gap; registered as a documented known failure") -- confirmed here to still genuinely fail
(reproducible), and confirmed to have **no `WILL_FAIL` CTest property**, exactly the same
undisclosed-to-CTest-despite-disclosed-in-comment pattern already documented for
`EasyGL_AvatarRenderer_TintRouting`/`Bgfx_RenderTargetCube_DepthFormat`/
`Bgfx_SkinnedEffect_WeightsPerVertex`. This raises the confirmed count of that specific pattern to
4.

### Out of scope, noted for context only (D-6): `easy-gl-resource-smoke-tests` subprocess abort

A real assertion failure (`g_state.last_active_texture == 0x84C0`) in
`/rv/data/development/github.com/openeggbert/easy-gl/tests/smoke/SmokeResourceTests.cpp` -- this is
the sibling `easy-gl` repository's own test suite, explicitly out of scope per D-6 (external
sibling-repo dependency, reference-only). Noted here only because it caused CTest to report a
"Subprocess aborted" for this one entry; not a CNA finding.

### Benign, expected: 4 hardware-gated sensor tests skipped

`AccelerometerTests.FailedEventWatchRegistrationRollsBackAndReportsFailure`/
`GetCurrentValuePropertyDoesNotThrowWhenSupported` and the equivalent 2 `GyroscopeTests` are
reported "Skipped," consistent with this project's own established pattern of gracefully skipping
hardware-dependent sensor tests when no real accelerometer/gyroscope device is present in the test
environment (already confirmed elsewhere in this audit, `tests-cna-input`/`microsoft-devices`
shards) -- not a finding.

### What this pass did NOT do

Did not build/test Bgfx, Vulkan, SdlGpu, D3D9, D3D11, D3D12, Dx3, WebGPU, SdlRenderer, Software,
Ascii, or Canvas in this sitting -- EasyGL only. Did not run any sanitizer (ASan/UBSan/TSan) build
at all -- every test run here was a plain, non-instrumented Debug build. Did not attempt to
determine whether the `EasyGL_MRT_TwoAttachments` failure is a recent regression or a long-standing
gap (no `git bisect`/blame performed, out of scope for a single opportunistic pass). Both are
natural continuations of Pass 6 for a future session.

## Pass 3 continued: remaining `Microsoft.Xna.Framework.*` namespaces swept against the real xn65 XML reference

Extends the earlier Graphics/Net/GamerServices/XACT-Audio-subset sweep with every other real XNA
4.0 namespace, using the identical method (real Microsoft XNA 4.0 Windows reference XML doc-comments
vs. CNA's actual declared headers, FNA cross-checked for severity triage on any flagged gap).
**Scope-mapping discovery**: several C# namespaces are physically bundled inside the single
`Microsoft.Xna.Framework.xml` file rather than having their own dedicated XML (Audio's non-XACT
types, `Content`, `Design`, `Input` [GamePad/Keyboard/Mouse, distinct from `.Input.Touch`], `Media`,
and `Graphics.PackedVector` are all documented there, not in their own namespace's XML) -- worth
recording so a future continuation doesn't waste time hunting for a separate file that doesn't
exist.

**Combined result across every namespace now swept**: root `Microsoft.Xna.Framework` (42 types, ~900
members) + `.Input.Touch`/`.Storage`/`.Video` (13 types, ~118 members) + `.GamerServices.Avatar*` (12
types, ~90 members) + rest-of-`.Audio` (12 types, ~55 members) + `.Graphics.PackedVector` (17 types,
~215 raw entries) + `.Content` (11 types) + `.Input` GamePad/Keyboard/Mouse (17 types) + `.Media` (21
types) -- **every real `Microsoft.Xna.Framework.*` namespace with runtime-relevant surface is now
swept** (only `.Content.Pipeline` and `.Design`, both confirmed genuinely out-of-scope build-time/
WinForms-tooling namespaces, remain formally unswept member-by-member, and both were positively
scope-confirmed rather than skipped -- see below).

### Root `Microsoft.Xna.Framework` + `.Game`: 42/42 types present, 1 LOW gap

Full member-by-member verification for `Vector2` (78 XML entries) and `Matrix` (108 XML entries, 65
unique leaf names) -- both 100% present including every overload. Automated name-level pass covered
all 42 types; every flagged discrepancy manually resolved (11 false positives from the automated
pass's own constructor-detection limitation, `GetEnumerator`->`begin()`/`end()`, `Finalize`->
destructor -- all confirmed present via direct grep). All 7 enum types in scope have every one of
their 22 combined named values present.

- **NEW, LOW -- `GraphicsDeviceInformation` is missing `Equals(object)`/`GetHashCode()`.** Both are
  real XNA 4.0 members per the reference XML, but FNA itself also never implements them (confirmed
  via direct FNA source read) -- an FNA-inherited gap, not an independent CNA oversight, consistent
  with this project's standing "FNA is not authoritative for API surface" caveat. Kept LOW (not
  MEDIUM, unlike the `DisplayMode` finding from the earlier Graphics sweep) given no evident
  behavioral dependency on value-equality for this class.

### `.Input.Touch` / `.Storage` / `.Video`: 13/13 types, ZERO genuine gaps -- the cleanest Pass 3 result

All 8 Touch types (`GestureSample`, `GestureType` [11-value `[Flags]` enum, `operator|`/`&`/`|=`/`&=`
all correctly present -- unlike the already-known `SpriteEffects` combined-flags gap in
`xna-graphics`], `TouchCollection`, `TouchLocation`, `TouchLocationState`, `TouchPanel`,
`TouchPanelCapabilities`), all 3 Storage types, and `Video`/`VideoPlayer`/`VideoSoundtrackType` are
fully present with every real member accounted for. Corroborates this session's already-clean
`VideoPlayerTest`/`VideoTest` behavioral audit results -- full convergence between the two
independent methods. New false-positive pattern recorded for future sweeps: a C++ value-type
struct correctly has no separate `Equals(object)` overload (no boxing/`System.Object` equivalent to
override against) -- `Equals(T)` alone is the correct translation, confirmed via `Vector2` too.

### `.GamerServices.Avatar*` (the real XNA Avatar API): 12/12 types, ZERO gaps -- and a genuinely reassuring positive finding

The xn65 `Avatar.xml` file is organizational naming only -- every one of these 12 types' real C#
namespace is `Microsoft.Xna.Framework.GamerServices`, matching CNA's own header placement exactly.
**A real risk going in was that CNA's extensive avatar-rendering system might be an entirely
parallel, non-XNA-compatible design merely reusing familiar class names -- verified false.**
`AvatarRenderer` (18/18 real members present) and `AvatarDescription` (10/10 present) are both
faithful to the real API surface, and CNA's own comments honestly disclose that the real `Draw()`
overloads are permanent no-ops (real XNA Avatar rendering depended on Xbox LIVE asset streaming CNA
has no server for), while a clearly `NOXNA`-tagged `DrawRealEXT()` family provides this project's
actual rendering -- the correct way to handle a real API that cannot be faithfully ported, not a
design flaw or a silent gap.

### Rest of `.Audio` (non-XACT): 12/12 types, ZERO gaps

`SoundEffect`/`SoundEffectInstance`/`DynamicSoundEffectInstance`/`Microphone`/`AudioEmitter`/
`AudioListener` all fully member-complete (~55 members checked). Combined with the earlier
XACT-subset sweep, the **full `Audio` namespace is now completely swept: 19/19 types present, one
LOW gap total** (`AudioCategory.ToString()`, already recorded).

### `.Content.Pipeline` and `.Design`: both confirmed correctly out of scope (not skipped)

`.Content.Pipeline` (120 real types: `ContentImporter<T>`, `ContentProcessor<T,U>`,
`ContentBuildLogger`, etc.) is build-time content-pipeline tooling with zero matching
`ContentImporter`/`ContentProcessor`-named files anywhere in CNA (confirmed via repo-wide search) --
CNA is a runtime, not a content-build tool, so this entire namespace is legitimately and entirely
out of scope. `.Design` (66 real members: `BoundingBoxConverter`/`ColorConverter`/`MatrixConverter`/
etc. -- WinForms `System.ComponentModel.TypeConverter` subclasses for property-grid editing support)
is similarly confirmed out of scope: zero matches for `TypeConverter` anywhere in CNA's `include/`
or `src/` trees. Both scope exclusions are correct and expected for a runtime, not gaps.

### `.Graphics.PackedVector`: 17/17 types present, but a NEW systemic MEDIUM-HIGH finding

**All 16 concrete `PackedVector` types (`Byte4`, `Short2`, `Alpha8`, `Bgr565`, `HalfVector4`, etc.)
entirely lack `Equals()`, `GetHashCode()`, and `ToString()`.** Confirmed absent by direct read of 4
representative files (`Byte4.hpp`/`Alpha8.hpp`/`HalfVector4.hpp`/`Rgba1010102.hpp`, 0 matches for any
of the three) and confirmed this is a genuine CNA gap, not FNA-inherited or a project-wide
convention: FNA's own `Byte4.cs` implements all three non-trivially (`GetHashCode()` returning
`packedValue.GetHashCode()`, `ToString()` returning `packedValue.ToString("X")`), and this session's
own Input-namespace sweep confirms `GamePadState`/`MouseState`/`KeyboardState` all correctly
implement the same three members in CNA, as do `Vector2`/`Color`/`Rectangle` per the earlier Graphics
sweep -- making PackedVector's complete omission an isolated gap in one type family, not a
deliberate scope decision or missed project-wide pattern. Practical impact: `operator==`/`!=` cover
direct comparisons, so nothing is silently *wrong*, but any code trying to put a `Byte4`/`Short2`/
etc. in a `std::unordered_map`, or debug-print one via `.ToString()`, simply won't compile. Distinct
from (adjacent to) the already-confirmed HIGH `Byte4`/`Short2`/`Short4.Pack()` truncate-vs-round
bug -- that's 3 types' packing *arithmetic*; this is all 16 types' missing object-contract
*members* entirely.

### `.Content` (runtime): 6/11 types present and complete; 5/11 absent, likely architecturally correct

`ContentManager`/`ContentReader`/`ContentTypeReader`/`ContentTypeReaderManager`/
`ResourceContentManager`/`ContentLoadException` all fully present (including `ContentReader`'s
`ReadSingle()`/`ReadDouble()`, correctly inherited from `sharp-runtime`'s `BinaryReader` base class,
mirroring FNA's own inheritance). The 5 absent types (`ContentSerializer*Attribute` family) are C#
custom attributes consumed by the Content Pipeline's build-time reflection-driven serializer, with
no C++ equivalent to C# attribute-based reflection -- given CNA's own hand-written
`ContentTypeReader<T>`-registration design (rather than reflection-driven generic serialization) and
`.Content.Pipeline` already being correctly out of scope, this is very likely a deliberate
architectural consequence, not an independent oversight. Flagged LOW-MEDIUM for the record, not as
an actionable "please implement" item.

### `.Input` (GamePad/Keyboard/Mouse, distinct from `.Input.Touch`): 17/17 types, 1 LOW tagging nit

All 17 real types present, plus 2 correct `NOXNA` extras (`MouseCursor`, `TextInputEXT`). All 3 most
complex/stateful types (`GamePadState`/`MouseState`/`KeyboardState`) correctly implement `Equals`/
`GetHashCode`/`ToString` -- a useful positive contrast to the PackedVector finding above. One LOW
nit: `KeyboardState::ToString()` is marked `NOXNA` despite being a genuine real XNA 4.0 member
(`MouseState`/`GamePadState`'s own `ToString()` are correctly NOT tagged `NOXNA`) -- a minor tagging
inconsistency per this project's own `CLAUDE.md` convention.

### `.Media` (full namespace, excluding `VideoPlayer`/`Video` already swept separately): 21/21 types, ZERO gaps

`Album`/`Artist`/`Genre`/`MediaLibrary`/`MediaPlayer`/`MediaQueue`/`MediaSource`/`Picture`/
`PictureAlbum`/`Playlist`/`Song`/their collections, all fully present. Spot-checked `Song` in detail
given this project's own persistent-memory-recorded historical note that FNA itself once omitted
real `Song.Album`/`Artist`/`Genre`/`ToString()` -- **confirmed already resolved**: CNA's `Song.hpp`
has all of them correctly present, plus a documented, intentional `GetHashCode()` improvement
(content-based, not identity-based, unlike FNA's own choice).

### Pass 3 running total

Across all sweeps to date (Graphics 781 members/2 gaps, Net ~120/1, GamerServices ~200/0,
XACT-Audio-subset ~60/1, Framework-core+Game ~900/1, Touch+Storage+Video ~118/0, Avatar+rest-Audio
~145/0, PackedVector+Content+Input+Media ~215+/2 (1 systemic)): **roughly 2700+ combined members
individually checked across every real `Microsoft.Xna.Framework.*` namespace, 7 genuine gaps found**
(2 MEDIUM: `DisplayMode.TitleSafeArea`/`ToString()`, PackedVector's systemic missing
`Equals`/`GetHashCode`/`ToString`; 1 re-confirmation of an already-known finding via a different
method: `VertexPositionColor` missing `IVertexType`; 4 LOW: `GraphicsDeviceInformation`
`Equals`/`GetHashCode`, `AudioCategory.ToString()`, `NetworkSession.MaxSupportedGamers`/
`MaxPreviousGamers` NOXNA-mistagging, `KeyboardState::ToString()` NOXNA-mistagging; 1 LOW-MEDIUM
architectural note: `.Content`'s 5 absent `ContentSerializer*Attribute` types). This project's real
XNA 4.0 API *surface* is confirmed overwhelmingly complete -- almost every defect this entire audit
has found is *behavioral* (wrong formulas, dropped events, missing null checks), not a missing
member. Pass 3 is now considered complete for every namespace with runtime-relevant surface.

## Pass 6 continued: build/test verification for every remaining feasible backend

Extends the EasyGL-only Pass 6 sweep above to 7 more backends, each built and runtime-tested (not
just statically reviewed), all via dedicated per-backend build directories to avoid cross-contamination.
The remaining backends (Bgfx, SdlRenderer, D3D12, Software, Ascii, Headless) are covered in a
follow-up continuation once their builds finish.

### HEADLINE, CRITICAL/HIGH: a real, security-relevant memory-safety crash confirmed on 2 of 7 backends so far, from malformed Texture2D content -- the single most severe finding of this entire audit

`XnbContainerFuzzTest.MutatedRealTexture2DFixtureNeverCrashesAndOnlyFailsCleanly` -- a test whose
entire stated purpose is guaranteeing 1500 rounds of mutated/corrupted `.xnb` content never crashes
the process -- **genuinely crashes the process on both Vulkan and WebGPU**, confirmed reproducible
in complete isolation on each, via two structurally different but conceptually identical root
causes:

- **Vulkan**: `*** stack smashing detected ***: terminated`. `VulkanTextureBackend`'s constructor
  takes XNB-decoded `width`/`height`/`mipLevels` with no validation against the device's own image
  limits before computing a staging-buffer size and driving `vkCreateImage`. Vulkan's own validation
  layer flags the resulting grossly out-of-spec parameters (`extent.width=16777217`,
  `mipLevels=25` against a 15-level maximum) as advisory warnings, but the RADV driver proceeds
  anyway, corrupting the stack downstream.
- **WebGPU**: a fatal, non-catchable Rust panic across the wgpu-native FFI boundary
  (`"thread '<unnamed>' panicked... panic in a function that cannot unwind... aborting"`).
  `GenerateMipsForLayer()` does check `wgpuTextureCreateView()`'s synchronous return value for
  `nullptr`, but wgpu-native validates the view's `baseMipLevel` *lazily*, at `wgpuQueueSubmit()`
  time, not at view-creation time -- a mismatched mip level (derived from the same class of
  corrupted/fuzzed dimension data) slips past the existing defensive check entirely and only
  surfaces as a fatal panic several calls downstream.

**Confirmed NOT present on EasyGL** (this session's own earlier EasyGL Pass 6 run reported a clean
5503/5507 with 0 failures for the full suite, which necessarily includes this test) — this is a
genuine, backend-specific class of defect, not a universal one, and not present in the
project's own default/most-mature backend. **Both instances share the same underlying shape**: XNB-
decoded texture dimensions/mip-counts are trusted and passed directly into a native GPU API
(`vkCreateImage`/`wgpuQueueSubmit`) with no sanity check against real device/format limits before
use, and the native API's own advisory-only validation (Vulkan) or lazily-timed validation (WebGPU)
does not substitute for an explicit CNA-side bounds check. **Impact**: any CNA application on either
backend that loads a corrupted, truncated, or maliciously-crafted `Texture2D` `.xnb` asset -- a
realistic scenario for any game loading content from disk, mods, or network transfer -- can be
crashed outright (denial of service), contradicting this exact test's own name and the clean,
catchable-exception behavior every other tested backend provides. **Suggested fix (report-only)**:
validate decoded `width`/`height`/`mipLevels` for sanity (positive, non-degenerate, within the
device's real reported limits) immediately after XNB decode and before any backend-specific texture
creation, throwing the same allowlisted `ContentLoadException` every other malformed-input case in
this test already expects -- ideally in shared `Texture2DContentTypeReader`/`Texture2D` construction
code rather than duplicated per-backend, so a single fix closes it everywhere at once. Whether D3D9/
D3D11/D3D12/Bgfx/SdlGpu/SdlRenderer/Software/Ascii/Headless/Canvas share this exact crash is not yet
fully determined -- D3D9/D3D11/Dx3/SdlGpu's own Pass 6 runs did not specifically isolate this exact
test case; a targeted follow-up re-running `XnbContainerFuzzTest.MutatedRealTexture2DFixtureNeverCrashesAndOnlyFailsCleanly`
in isolation on every remaining backend would settle the true blast radius precisely.

### Vulkan: build+test complete, 5495/5507 pass; corrects a previously-documented finding to FIXED

Real Vulkan 1.4.309 (AMD Radeon 780M, RADV). Beyond the crash above:

- **Task 868 (Vulkan `BlendState` hardcoding) is CONFIRMED FIXED, correcting this audit's own
  earlier static-review finding.** `AUDIT_GRAPHICS_BACKEND_MATRIX.md`/`cmake/Tests/VulkanTests.cmake`
  document this as an open defect with precise per-check failure predictions for 5 blend-state
  tests -- **all 6 blend-state test executables now pass cleanly**, including every case the
  documented predictions said should genuinely fail. `ApplyBlendState`'s own source comment confirms
  the fix landed since this audit's static review: "Task 868: previously every one of these 6 real
  values was discarded... Now stored for real use." `AUDIT_GRAPHICS_BACKEND_MATRIX.md`'s Stencil+
  Scissor+DepthBias row and `VulkanTests.cmake`'s own per-check predictions both need updating to
  reflect this.
- **NEW, MEDIUM-HIGH**: SkinnedEffect+Fog renders completely black on all 3 checks, *including the
  fog-disabled control case* -- not the already-documented mirrored-fog-formula bug (which produces
  a wrong-but-non-black color), but an apparent silent pipeline-creation or descriptor-binding
  failure in the skinned3d+fog pipeline specifically (which needed a 3rd descriptor-set binding
  added to carry fog data). Not root-caused further; even once the black-screen issue is fixed, the
  test's own expected values still encode the already-known wrong mirrored fog formula and would
  need correcting too.
- **NEW, LOW**: `GraphicsDeviceCapabilityTest.DoesNotSupportWireFrame` is a test-authoring bug, not
  a Vulkan defect -- Vulkan genuinely supports wireframe fill mode, but the test asserts universally
  that no backend does, when this was only ever true for EasyGL's underlying GLES3. (Independently
  re-confirmed by the SdlGpu sweep below, which hits the identical test with the identical
  root cause on a third backend.)
- Environment gap, not a CNA defect: `glslc`/`glslangValidator` CLI tools are missing from this
  sandbox (only the shared `libshaderc1` library is installed), blocking 2 CNJ/custom-GLSL-effect
  tests -- needs `sudo apt-get install glslang-tools` to close, unavailable to this pass.
  Fog-formula/scissor/environment-map-normal-transform findings already on record were all
  empirically re-verified consistent with their existing characterization (no new information).

### D3D11: build+test complete via real Wine+DXVK (not a WineD3D fallback), 5/7 pass; 2 new runtime-confirmed defects

Confirmed genuine via real DXVK log lines ("DXVK: Using 16 compiler threads"), not a silent
fallback. `CnaTests.exe` correctly never attempted (documented, pre-existing cross-compile
limitation, `.github/workflows/d3d-windows-ci.yml`'s own header comment). Fog-at-boundary tests
passing is expected and does not contradict the already-confirmed mirrored-formula bug (both
formulas saturate identically at the exact `Z=FogEnd` boundary this smoke test samples).

- **NEW**: `D3D11_Smoke` (152/153 individual checks) -- Blinn-Phong specular fails for non-skinned
  `lit_textured3d` at a geometry chosen so `dot(H,N)=1` exactly (should produce a full-white
  highlight with diffuse/ambient/emissive all zero), while the structurally identical `skinned3d`
  specular check at the same geometry passes. A genuine asymmetry, not yet root-caused to a specific
  shader line.
- **NEW**: `D3D11_Pbr_VertexColor` fails -- `skinned3dvertexlitcolored-black-vertexcolor` produces
  green `(0,255,0,255)` instead of the expected black; a vertex explicitly colored black is not
  correctly applied for the skinned+vertex-lit+vertex-color effect combination specifically.

### Dx3: build+test complete natively (no Wine needed -- `free-direct` is an SDL3-based reimplementation, not real DirectDraw); CLOSES this project's long-standing `Dx3_SpriteBatch` investigation

**Both of this audit's static predictions for `Dx3_SpriteBatch`'s 2 historically-failing checks are
now empirically confirmed for the first time**, exactly matching the persistent "2/10 failing"
record: Check D (zero-alpha `AlphaBlend`) fails, confirming the test-authoring-bug hypothesis
(non-premultiplied fixture color under a premultiplied-convention blend preset); Check G (180°
rotation) fails, confirming the real-backend-defect hypothesis. **New lead on Check G**: the
rotation formula is a byte-for-byte verbatim port of `SoftwareGraphicsBackend.cpp`'s identical code
-- if Software's own equivalent rotation test also fails the same way (see below), this reframes the
bug as a shared defect inherited from Software's original code, not independently introduced in
Dx3. Every other Dx3 test executable is 100% clean: 59/61 checks across all 8 executables (96.7%),
both failures now fully explained.

### D3D9: build+test complete via real Wine+DXVK, 100% clean (280+ checks across 18 executables)

Zero real test failures, after the investigating fork caught and corrected its own initial
methodology error (using the wrong Wine prefix for the shader-compiler-oracle test specifically,
per `scripts/run-wine-dxvk9.sh`'s own documented prefix-selection rule -- corrected before
reporting, a good example of self-caught rigor). Corroborates this backend's stock-effect-fidelity
and Reach/HiDef-profile-enforcement claims with no regression. The already-known object-space-only
fog defect in the 3 CNA-custom shaders was not independently re-exercised (none of the 18
executables specifically test `FogEnabled=true` on those shaders) -- still only statically
confirmed, a natural follow-up target.

### SdlGpu: build+test complete, 5500/5507 pass on the general suite, 21/21 on its own dedicated suite; 3 new findings

- **NEW**: 2 CNJ/custom-`ShaderEffect` tests (`CnjEffectTest.LoadsRealCnjFixture`,
  `CnjStockEffectTest.CustomGlslEffectStillWorks`) fail -- SdlGpu's SPIR-V-based shader pipeline
  rejects a real, already-committed test-fixture GLSL shader that works on EasyGL's more permissive
  raw-OpenGL GLSL (errors: missing `#version 310 es`+, missing explicit `location` qualifiers,
  non-block uniforms). A real cross-backend compatibility gap for user-authored custom-effect
  content, not merely a test-fixture issue -- whether the fix belongs in SdlGpu (auto-upgrade older
  GLSL), documentation (a stricter dialect requirement disclosed to effect authors), or the fixture
  itself is a judgment call for the project owner.
- **NEW, confirms the Vulkan finding above on a second/third backend**:
  `GraphicsDeviceCapabilityTest.DoesNotSupportWireFrame` fails identically -- SdlGpu also genuinely
  supports wireframe, the same universal-assertion test-authoring bug.
- SdlGpu's own dedicated 21-test suite is 100% green, but (as this fork itself was careful to note)
  this doesn't confirm or deny any of the 6 already-known static findings for this backend (fog
  unimplemented, skinned-normal-transform, EnvironmentMapEffect emissive bug, constructor-resource-
  leak, DepthBias-stored-not-applied, whole-cube-mip-regen) since none of them have any direct test
  coverage in either direction -- genuinely untested territory, not silently confirmed-clean.

### Canvas (Emscripten): buildable after all -- corrects an earlier "unavailable" assumption, plus a new build-config finding shared across 3 toolchains

**Emscripten IS available in this sandbox** (a full `emsdk` install at `~/emsdk`, not on `PATH` by
default) -- corrects this pass's own earlier quick `which emcc` check. Configure succeeds; the core
`libCNA.a` and the Canvas backend library itself build with zero Canvas-specific errors across 64%+
of the full target graph. `CnaTests` itself cannot finish building, but the cause is unrelated to
Canvas: **`cmake/Harnesses.cmake`'s two mixer-destroy-voice harnesses
(`cna_audio_mixer_destroy_active_static_voice_harness`/`..._dynamic_voice_harness`) omit an explicit
`SDL3::SDL3` link**, relying on fragile transitive propagation from `CNA` that fails specifically
under this Emscripten toolchain (`fatal error: 'SDL3/SDL.h' file not found`) -- **the same exact
finding independently rediscovered by the D3D9 and D3D11 MinGW cross-compile passes above (3 of 3
non-native-GCC toolchains hit it)**, confirming this is a real, reproducible, toolchain-general
build-config gap in those two specific harness registrations (missing `SDL3::SDL3` in their
`target_link_libraries()` calls), not a one-off. LOW/MEDIUM severity -- harmless on every native
desktop build this project's existing CI already exercises, but a real gap once cross-compiling to
anything else. No new Canvas-backend source-level defect found; existing static findings re-read
and not contradicted.

### Recurring build-config finding, now confirmed under 3 independent toolchains: `cmake/Harnesses.cmake`'s 2 mixer-destroy harnesses need an explicit `SDL3::SDL3` link

Confirmed independently by the Canvas (Emscripten), D3D11 (MinGW), and D3D9 (MinGW) passes above --
all three hit the identical `fatal error: SDL3/SDL.h: No such file or directory` for the exact same
2 targets. Suggested fix (report-only): add `SDL3::SDL3` to both harnesses' `target_link_libraries()`
calls in `cmake/Harnesses.cmake`, matching the pattern other `AudioMixer.hpp`-consuming targets
already use.

### Bgfx: build+test complete, 5504/5511 general + 110/114 backend-specific pass; finds the real root cause behind the "already-known, unrelated `cna_demo_xact` failure" every single Pass 6 fork this session has dismissed

**NEW, backend-agnostic build defect, previously never actually root-caused**: `cmake/Examples.cmake`
registers an unconditional `POST_BUILD` step on `cna_demo_xact` that copies
`examples/demo_xact/Content` -- **which does not exist anywhere in this repository at all**
(`examples/demo_xact/` only has a `src/` subdirectory). This fails the copy command and aborts the
entire top-level `cmake --build` invocation for the WHOLE PROJECT, on every backend, since this
target is backend-agnostic -- confirmed to have **silently also broken this session's own earlier
EasyGL build**, unnoticed because that run's `cmake --build ... | tail -60` pipeline reports the
pipe's exit code (always 0), not the real build's, and `CnaTests` itself doesn't depend on
`cna_demo_xact` so it still built successfully despite the overall command actually failing. Every
Pass 6 fork this session correctly identified this as "already known, pre-existing, unrelated" (it
is not backend-specific), but none had previously traced the exact mechanism -- this is the first
precise root-cause identification. Suggested fix (report-only): either add a real
`examples/demo_xact/Content` directory (or confirm `XactFileGen.hpp` already generates this demo's
XACT content at runtime, making the copy step unnecessary), or guard the `add_custom_command` with
`if(EXISTS ...)`.

**Test results**: direct-binary `CnaTests` run (5511 tests): 5504 passed, 4 skipped (expected), 3
failed -- 2 are the already-documented, disclosed Bgfx architectural limitation (no runtime GLSL
compilation for custom `ShaderEffect`s, per `docs/shader-effect-vs-fx-bytecode.md`) with no
`WILL_FAIL` annotation, 2 more concrete instances of the systemic WILL_FAIL-never-used finding
above. The 3rd, `GraphicsDeviceCapabilityTest.DoesNotSupportWireFrame`, is now **confirmed identical
on a 3rd backend** (Vulkan, SdlGpu, and now Bgfx all hit the exact same universal-assertion
test-authoring bug -- the test hardcodes an EasyGL/GLES3-specific wireframe limitation as if it were
a universal backend truth; Bgfx's desktop OpenGL 2.1 path genuinely supports wireframe fill mode).

Bgfx's own dedicated suite (`ctest -R "^Bgfx_"`): 110/114 pass. 2 already-known failures re-confirmed
(`Bgfx_SkinnedEffect_WeightsPerVertex`, `Bgfx_RenderTargetCube_DepthFormat`). **NEW, real backend
defect, well-evidenced**: `Bgfx_RasterizerState_CullMode_Camera` and
`Bgfx_RasterizerState_CullMode_IndexedBasicEffect` both fail identically -- XNA's real default cull
mode, `CullCounterClockwiseFace`, fails to cull anything on Bgfx (a CCW-wound triangle that should
be culled stays visible), while `None` and `CullClockwiseFace` both work correctly in the same two
tests. Two independent test-file confirmations of the identical failure rules out coincidence.
**Root-cause hypothesis (not fully proven)**: `ApplyRasterizerState()`'s own comment claims
`BGFX_STATE_CULL_CW`/`CCW` map directly to raw winding direction, unaffected by `glFrontFace` -- but
a nearby comment in the same file (Task 763, already-fixed for `bgfx::setStencil`'s front/back
split) documents that bgfx's own default `glFrontFace` is `GL_CW`, opposite of EasyGL's effective
convention, and that this DOES affect stencil face-relativity. If cull-mode is similarly
`glFrontFace`-relative (not the absolute raw-winding direction `ApplyRasterizerState()` assumes),
this default-cull-mode failure is a natural, unfixed sibling of the already-fixed stencil issue --
a concrete, actionable lead for a source-level follow-up, not yet conclusively proven.

### D3D12: genuinely runtime-verified via real vkd3d-proton, not static-only; 220/220 checks pass; confirms the known swapchain crash is still current; finds a real testing-coverage gap

Built via MinGW cross-compile + Wine, exercised through both a direct
`scripts/run-wine-vkd3d.sh` invocation and the official `ctest -L D3D12` registration. **vkd3d-proton
genuinely engaged** (log line `vkd3d-proton - applicationVersion: 3.1.0`, not a silent fallback).
`D3D12_Smoke`'s 220 checks all pass, and the test itself is exceptionally rigorous -- real,
exact byte-level GPU readback across NPOT texture alignment, mip-targeted upload/readback, all 4
`DepthFormat` values, stencil-plane clear+readback with depth-test-gated proof of distinct values
(not a fixed default), full mip-chain regeneration on both `RenderTarget2D` and `RenderTargetCube`
including a non-zero cube face, MSAA creation+resolve with exact color readback, a genuinely
windowless `HeadlessEXT` device construction, pixel-exact `SpriteBatch.DrawString` glyph placement,
`Model::Draw()`'s full bone-transform pipeline, a non-zero-root-bone-index Model proof, exact
`Texture2D.SaveAsPng`/`FromStream` round-trip, and a runtime-compiled custom HLSL `ShaderEffect`.

**NEW, real testing-coverage gap**: `cmake/Tests/D3D12Tests.cmake` registers exactly ONE CTest for
this entire backend (`D3D12_Smoke`) -- meaning this audit's two most significant D3D12 HIGH findings
from static review (Stencil/Scissor/DepthBias completely non-functional; `OcclusionQuery`
multi-draw-overwrite) have **no dedicated test at all** to empirically confirm or refute at runtime.
`D3D12_Smoke`'s own stencil/depth checks only exercise the direct clear-value path, not
`DepthStencilState`/`RasterizerState`'s own settable properties, so they don't actually reach the
bug being described. This is a real gap distinct from the bugs themselves -- two of this backend's
most consequential findings are currently unverifiable by this project's own test suite, one way or
the other.

**Known swapchain-creation crash confirmed still-current, not historical**: reproduced live via the
standard `run-wine-vkd3d.sh` (system-Wine) path -- the process crashes with a Wine backtrace exactly
as `examples/d3d12_swapchain_diag.cpp`'s own header comment already documents (a dxgi.dll ABI
mismatch under system Wine, deliberately not registered as a CTest to avoid permanent noise).
**Could not exercise the documented working fix path** (`scripts/run-proton-vkd3d.sh`, a genuine
`S_OK` swapchain result per this project's own 2026-07-14 record) because no Steam/Proton
installation exists in this sandbox -- a genuine environmental limitation of this specific
environment, explicitly marked as unavailable rather than silently skipped, not a defect in the
script itself (already confirmed correct in its own audit report).

Incidental, unrelated build finding: `cna_xnb_audio_metadata_dump.exe` (a `tools/` utility, not
D3D12-specific) fails to link under this MinGW cross-compile configuration with an undefined
reference to `Video::Video`'s constructor/vtable -- `Video.cpp`'s translation unit apparently isn't
linked into this specific tool target under `CNA_BUILD_EXAMPLES=OFF`. Did not block `D3D12_Smoke`.

### SdlRenderer, Software, Ascii, Headless: all 4 built+tested -- Pass 6 is now COMPLETE across every one of the 14 real graphics backends

This closes out Pass 6: every backend (EasyGL, Canvas, D3D9, D3D11, D3D12, Dx3, WebGPU, Vulkan,
SdlGpu, Bgfx, SdlRenderer, Software, Ascii, Headless) has now been built AND runtime-tested this
session, not merely statically reviewed.

**`cna_demo_xact`'s Content-copy build failure is now confirmed universal, on all 5 backends
independently built and tested this session (EasyGL, Bgfx, SdlRenderer, Software, Ascii, Headless --
6 counting the earlier root-cause identification against Bgfx specifically)**: `examples/demo_xact/
Content` genuinely does not exist anywhere in the source tree. Already root-caused precisely in the
Bgfx section above (an unconditional `POST_BUILD` copy step in `cmake/Examples.cmake`) -- this
further confirms it as a universal, backend-independent build-system defect, not specific to any
one backend.

**NEW, real robustness gap**: `SDL_Renderer_FullscreenToggle` crashes the whole test process with an
uncaught `std::runtime_error` ("`ReadBackbuffer`: physical/logical size mismatch (letterbox or
stretch scaling active) -- exact-pixel readback unsupported") during a fullscreen toggle, instead of
failing as a clean, catchable test assertion -- `CTest` reports "Subprocess aborted." Either the
test needs to avoid calling `ReadBackbuffer` while a letterbox/stretch-scaling mismatch is active, or
(if verifying behavior *during* that state is the actual intent) production code needs to surface
this via the project's own exception-safe check pattern rather than an uncaught exception that
terminates the process.

**NEW, likely shared CPU-side defect, confirmed on 2 (possibly 3) independent backends**: Texture3D
content-reader round-trip returns all-zero/garbage data instead of the real uploaded pixel values --
reproduces identically on Software (`Texture3DTextureCubeContentTypeReaderTest.
Texture3DReaderParsesHandConstructedBytesMatchingFnaByteOrder`, `CnjTexture3DTest.LoadsRealCnjFixture`)
and Headless (same 2, plus `TextureCubeReaderLoadsRealMonoGameFixtureEndToEnd`) -- and SdlRenderer's
own general-suite failure list includes the same content-type-reader test too, though conflated
there with its broader, expected 2D-only-backend failure set. Reproducing identically on a CPU
rasterizer AND a no-op/no-GPU backend strongly suggests shared CPU-side code (the XNB/CNJ Texture3D
content reader, or `Texture3D`'s own generic `GetData`/`SetData` path), not either backend's own
GPU-texture implementation -- worth checking on EasyGL/Vulkan/other backends too, since none of the
earlier per-backend Pass 6 passes specifically isolated this exact test case.

**NEW, likely shared default-capability-flag issue, confirmed on 5 backends now (Vulkan, SdlGpu,
Bgfx, Software, Headless)**: `GraphicsDeviceCapabilityTest.DoesNotSupportWireFrame` fails
identically across all 5, though for what may be two different underlying reasons depending on the
backend -- for Vulkan/SdlGpu/Bgfx, the backend genuinely and correctly supports wireframe fill mode
(a real GPU capability the test wrongly assumes doesn't exist anywhere, since it was only ever true
for EasyGL's GLES3). For Software (a CPU rasterizer that could plausibly implement wireframe
trivially) and especially Headless (which does nothing GPU-side at all, yet reports `true` for this
capability), the direction is less clear -- Headless claiming wireframe support while doing zero
real rendering suggests the `WireFrame` capability flag may default to `true` somewhere shared (e.g.
a base `IGraphicsBackend` default only some backends override to `false`) rather than being
independently computed per backend. Worth checking `SupportsCapability`'s default implementation
directly rather than assuming this is purely a test-authoring bug in every instance.

**`MediaLibraryTestFixture.ObjectGraphIsInternallyConsistent` SEGFAULTs identically on all 4 of
these backends too** -- now confirmed backend-independent across at least 6 backends this session
(EasyGL, WebGPU's own crash aside, SdlRenderer, Software, Ascii, Headless), strongly reinforcing this
as a genuine, universal robustness bug in shared CPU-side scan/object-graph code, not an
EasyGL-specific artifact.

**Ambiguous, flagged not resolved**: `SDL_Renderer_RenderTarget_DepthDecision` fails with framing
that implies binding a `RenderTarget2D` with an explicit `DepthFormat` should unlock some real
per-target depth capability during `Clear()` -- production code doesn't honor this. Unclear whether
this is a second, un-updated instance of the same stale-throw-expectation pattern as the
already-known `SDL_Renderer_ClearOptions_Audit` finding, or a genuinely distinct, never-implemented
feature -- needs reconciling against `docs/sdl-renderer-2d-completeness.md`'s own depth-support row.

Software/Ascii/Headless's own dedicated CTest suites are all 100% green (6/6, 6/6, 7/7
respectively); SdlRenderer's is 65/68 (the 3 failures above). Every other general-suite failure on
the 2D-only backends (SdlRenderer's 56, Ascii's 52) is confirmed expected methodology noise --
`CnaTests`' shared Model/glTF/CNJ content tests assume 3D capability and aren't written
backend-conditionally, not a defect in either backend (both correctly throw for every 3D-only API
entry point per their own clean `ThrowNo3D` dedicated-suite results).

### Pass 6 final tally: every one of this audit's 14 real graphics backends built and runtime-tested

EasyGL, Canvas, D3D9, D3D11, D3D12, Dx3, WebGPU, Vulkan, SdlGpu, Bgfx, SdlRenderer, Software, Ascii,
Headless -- all 14. Headline findings across the whole sweep: a CRITICAL/HIGH cross-backend
security-relevant crash (malformed Texture2D content crashes Vulkan and WebGPU, clean on every other
tested backend); a project-wide `WORKING_DIRECTORY` CTest registration gap invisible to every CI
workflow; a project-wide "never adopted `WILL_FAIL`" systemic gap (now 6+ confirmed instances); a
universal `cna_demo_xact` build defect, precisely root-caused; the `Dx3_SpriteBatch` investigation
closed empirically; Task 868 (Vulkan BlendState) corrected from open to fixed; the `WebGPU_Msaa`
"intentionally failing" note corrected to fixed; several new per-backend rendering/robustness
defects (D3D11 specular asymmetry and vertex-color bug, Bgfx cull-mode bug, SdlGpu's stricter GLSL
dialect, a shared Texture3D round-trip bug, a shared WireFrame-capability-flag ambiguity, and the
`SDL_Renderer_FullscreenToggle` uncaught-exception crash); and a `MediaLibraryTestFixture` SEGFAULT
now confirmed universal across essentially every backend tested.

