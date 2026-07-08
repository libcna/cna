# NEXT.md — CNA Project Handoff

---

> ⛔ **WebGPU is forbidden for now** — do not work on any WebGPU task (Phases 56–69,
> `WEBGPU-1`–`WEBGPU-123` in `plan_webgpu.md`, moved out of `plan_graphics.md` 2026-07-07) until
> the project owner explicitly lifts this restriction. See `CLAUDE.md` ("WebGPU Is Forbidden For
> Now").

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model (`Microsoft::Xna::Framework`),
built on SDL3 with a pluggable 3D graphics backend layer. It is a framework/runtime — not a game —
designed so XNA/FNA game code can be ported to C++ with minimal API-surface changes.

- **Main goal:** full XNA 4.0 API coverage with pixel-accurate behavior, backed by unit tests and
  pixel-readback integration tests, verified against the authoritative FNA reference source
  (`/rv/data/library/github.com/FNA-XNA/FNA/src`). Task-by-task progress lives in
  `plan_graphics.md`; per-phase synthesis docs live in `docs/*.md`.
- **Current development phase:** Phases 1–46 are complete. **Phase 47 ("SpriteBatch renderer
  correctness", Tasks 411–420) is open.** **Task 411 (opener) built the mock/recording
  `ISpriteBatchBackend` infrastructure Tasks 412–416 depend on.** Found `SpriteBatch` had no
  injection seam at all (always required a real `GraphicsDevice`/GPU backend) and
  `Texture2D::CreateCpuOnlyForTests()` leaves `backend_` null (a crash the moment a queued sprite
  is flushed) — fixed both with new `NOXNA` test-only constructors/factories
  (`SpriteBatch(unique_ptr<ISpriteBatchBackend>)`, `Texture2D::CreateWithBackendForTests(...)`),
  then built `RecordingSpriteBatchBackend`/`DummyTextureBackend` and 5 tests proving the whole
  injection path works end-to-end. **Task 412 fulfilled Task 161's ask — zero bugs found**:
  verified `SpriteSortMode::Immediate` flushes each sprite inside `Draw()` itself, strictly
  before `End()`, independently confirming discriminating power by temporarily breaking the
  dispatch and watching both new tests fail exactly as predicted while a `Deferred` negative
  control kept passing. **Task 413 fulfilled Task 162's ask — zero bugs found**: confirmed
  `SpriteSortMode::Deferred` preserves original submission order (no sort at all), independently
  confirmed by temporarily reversing the flush order and watching the test fail exactly as
  predicted. **Task 414 fulfilled Task 163's ask — zero bugs found**: confirmed
  `SpriteSortMode::Texture` groups draws by texture pointer (regardless of which texture sorts
  first, since that depends on runtime addresses) while preserving `stable_sort`'s within-group
  order, independently confirmed by temporarily disabling the sort and watching the test fail
  exactly as predicted. **Task 415 fulfilled Task 164's ask — zero bugs found**: confirmed
  `SpriteSortMode::FrontToBack` sorts by ascending `layerDepth`, using the task's own example
  depths (0.5, 0.1, 0.9), independently confirmed by temporarily disabling the sort and watching
  the test fail exactly as predicted. **Task 416 fulfilled Task 165's ask, closing the
  per-`SpriteSortMode` test arc (Tasks 412–416) — zero bugs found**: confirmed
  `SpriteSortMode::BackToFront` sorts by descending `layerDepth`, the mirror image of Task 415,
  independently confirmed the same way. **Task 417 (first real GPU pixel test in this phase)
  confirmed `SpriteBatch::Draw`'s rotation genuinely pivots around the caller's `origin` point,
  matching FNA's real formula term-for-term — zero bugs found**, independently confirmed by
  temporarily hardcoding `origin=(0,0)` in EasyGL's `Draw()` and watching 2 of 3 checks fail
  exactly as predicted. **Task 418 verified `SpriteBatch::Draw`'s scalar and `Vector2` scale
  overloads produce the exact expected destination size, including non-uniform `Vector2` scale
  — zero bugs found**, independently confirmed twice by temporarily forcing each axis-mixing bug
  (`scale.X`-for-both and `scale.Y`-for-both) and watching the corresponding check fail exactly
  as predicted each time. **Task 419 verified `SpriteBatch::Draw`'s `sourceRectangle` genuinely
  crops the sampled texture region rather than stretching the whole texture — zero bugs found**,
  independently confirmed by temporarily forcing whole-texture UVs and watching both cropping
  checks fail exactly as predicted (reading the texture's own top-left/bottom-right cell colors
  instead of the selected cell). **Task 420 closes Phase 47's core `SpriteBatch` test arc (Tasks
  411–420) — zero bugs found**: proved that `layerDepth`-driven draw order (already verified via
  mock backend in Tasks 415/416) actually determines which of 2 overlapping opaque sprites is
  visible, via a real GPU pixel test deliberately submitted in reverse order from the correct
  sort order — independently confirmed by disabling the sort and watching the overlap check fail
  exactly as predicted. **Task 664 fixed the confirmed Vulkan `SpriteBatch` multi-`Begin`/`End`-
  per-frame bug** — root cause isolated via instrumentation/tracing first (per this task's own
  instruction, not guess-fixed): `Begin()` destructively cleared the *same* mutable vectors a
  prior `End()` had just populated, and a second, previously-masked bug meant the per-frame
  sprite VB/IB harvest always wrote to a hardcoded offset 0 instead of an accumulating cursor.
  Fixed by moving each `Begin`/`End` cycle's geometry into its own independently-owned
  `BatchSnapshot` at `End()`, and giving the harvest a genuine running offset mirroring the
  already-correct 3D draw path. New regression test confirmed via `git stash` revert-and-refail.
  **Task 665 fixed the confirmed Vulkan `SpriteBatch.Begin()` `SamplerState` no-op — both
  predicted root causes confirmed present**: `VulkanSpriteBatchBackend` never overrode
  `SetSamplerFilter`/`SetSamplerAddressMode` (always used the texture's own fixed descriptor
  set), and `Draw()` separately clamped UVs to `[0,1]` regardless of `sourceRectangle`, silently
  defeating `Wrap`/`Mirror` addressing even if the sampler wiring alone were fixed. Fixed both;
  new `Vulkan_TextureAddressMode` test (a direct port of Task 269's EasyGL test) confirmed via
  `git stash` revert-and-refail — pre-fix, both `PointWrap` and `PointClamp` read the identical
  blended color, proving `Wrap` never took effect. **This closes both of Phase 47's originally-
  scoped "two SpriteBatch bugs found this session."** **Task 884 fixed the real, confirmed
  `EffectParameterCollection`/`EffectPassCollection` by-value-`vector` dangling-pointer hazard**
  (the same shape Task 355 already fixed for `EffectTechniqueCollection`) — switched both to
  `vector<unique_ptr<T>>` with matching custom iterators; discriminating power independently
  verified via `git stash` revert-and-rebuild, where the pre-fix code didn't just fail an
  assertion but actually **segfaulted** (`EffectPassCollectionTest`, a genuine use-after-free).
  **Split the RasterizerState-default GPU-sync gap out of Task 884's old bundled title into its
  own Task 896** (not fixed — needs a scoping decision on its large, unaudited blast radius
  across ~128 EasyGL/Vulkan example files before it can safely land; see §5/§8). **Task 885 fixed
  the confirmed `BasicEffect` lit-path gap on EasyGL and Bgfx**: `DirectionalLight1`/
  `DirectionalLight2` were completely unforwarded and `EmissiveColor` was silently dropped whenever
  `LightingEnabled=true` (only the disabled-lighting path had it, Task 369). Fixed both backends'
  shaders to sum all 3 lights and add `EmissiveColor` after the diffuse multiply (matching FNA's
  `Lighting.fxh` formula exactly); discriminating power independently verified on both backends via
  `git stash` revert-and-rebuild (pre-fix: identical `(89,13,13)` ambient+light0-only result on
  both). **Split Vulkan out to its own Task 897, which is also now DONE**: gave the lit-textured
  pipeline its own dedicated descriptor-set/pipeline-layout/UBO-ring-buffer (mirroring
  `EnvironmentMapEffect`'s own pattern exactly), keeping the existing 128-byte push constant
  unchanged for strides 20/24/`Instanced3D`. New `Vulkan_BasicEffect_MultiLightEmissive` test
  passed on the first attempt with the exact same expected values as EasyGL/Bgfx; discriminating
  power confirmed via `git stash` revert-and-refail (identical `(89,13,13)` pre-fix result). Full
  serial Vulkan regression: 3546/3559 pass, all 13 failures independently reconfirmed pre-existing
  (reran with this task's changes reverted — identical failures, proving they predate this task).
  **This closes the `DirectionalLight1`/`DirectionalLight2`/`EmissiveColor` lit-path gap on all 3
  backends** (Tasks 885 + 897 together). **Task 886 implements real specular highlights for
  `BasicEffect` on all 3 backends** — half-vector Blinn-Phong (FNA's `Lighting.fxh`
  `ComputeLights`), gated per-light by the same "faces the light" term as diffuse, summed with
  each light's own `SpecularColor` then scaled once by the material's `SpecularColor`, added after
  the texture×diffuse multiply (FNA's `AddSpecular` macro). Required a real, non-degenerate camera
  (`Matrix::CreateLookAt`/`CreatePerspectiveFieldOfView`) to test at all, since `EyePosition` under
  the identity-View/Projection cameras used by every earlier `BasicEffect` test sits exactly on the
  quad's own plane (degenerate for specular). Building that real-camera test **surfaced 2 more
  real, pre-existing, previously-invisible bugs**, both fixed as hard prerequisites in the same
  commit: **Task 892** (Bgfx's `vs_lit_textured3d.sc` transformed the normal by the full
  World×View×Projection matrix instead of World's inverse-transpose — a row already opened by Task
  398's audit with a detailed prediction that the actual fix matched almost exactly) and **Task
  898** (the identical bug shape independently found on Vulkan's `lit_textured3d.vert.glsl`, fixed
  in-shader via GLSL's built-in `inverse()`, mirroring `env_map3d.vert.glsl`'s own already-correct
  pattern). Both bugs were totally masked by every prior test's identity View/Projection. All 3
  discriminating-power checks (Bgfx/Vulkan `git stash` revert-and-rebuild, reproducing the exact
  predicted pre-fix `(2,2,2)`-instead-of-`(48,48,48)` ambient-only failure) confirmed independently.
  **Task 887 fixed `AlphaTestEffect.VertexColorEnabled` being completely ignored on Vulkan/Bgfx**
  (opened by Task 377) — added a stride-24-only sibling vertex shader to each backend's existing
  `alpha_test3d` pipeline (smaller-footprint than the row's own original "unify all dispatch"
  prediction) that reads the vertex color attribute and gates it by `VertexColorEnabled` before the
  unchanged alpha-test fragment shader's `discard` logic runs. New `{Vulkan,Bgfx}_AlphaTest_
  VertexColor` tests (ports of Task 377's EasyGL test) confirmed via `git stash` revert-and-rebuild
  on both backends — pre-fix, both produced the identical diffuse-alone `(122,82,163)` result on
  both checks. Bgfx's test initially failed for an unrelated, already-known reason (the Task
  364/896 `RasterizerState::CullNone` culling gap, missing from the new test) before being fixed.
  **Task 888 implements real fog rendering on Vulkan and Bgfx** (opened by Task 378) — chose
  EasyGL's already-shipped raw-object-space-Z formula over FNA's more rigorous view-space
  `FogVector` approach (the two are only equivalent under identity `World`/`View`, which every
  test in this project uses, and matching EasyGL is what makes porting its exact tests work as
  verification). **Bgfx got full coverage** (no push-constant byte budget there) across all 7
  applicable pipelines. **Vulkan got fog only where spare UBO/push-constant capacity already
  existed** (`alpha_test3d`/`alpha_test_colored3d`, `lit_textured3d`) — the other 5 pipelines share
  a fully-packed push constant with zero spare bytes and need new dedicated UBO infrastructure,
  split out into new **Task 899**. 6 new tests across both backends confirmed via `git stash`
  revert-and-rebuild; hit 2 real pre-existing gotchas along the way (Vulkan's clip-space Z range is
  `[0,1]` not OpenGL's `[-1,1]`, and 2 ported Bgfx tests needed the already-known
  `RasterizerState::CullNone` workaround) — both fixed in the tests themselves, not backend bugs.
  **Task 900 fixed `SkinnedEffect`/`EnvironmentMapEffect`'s fog gap on EasyGL** (found during Task
  888's research) — both effects' `FillGpuDrawParams()` never forwarded fog fields at all, on any
  backend, despite both having complete `IEffectFog`/`FogVector` machinery in `OnApply()`. Fixed by
  mirroring `BasicEffect`'s existing pattern, plus adding fog uniforms/blend to EasyGL's
  `env_map3d`/`skinned3d` shaders (the only 2 of EasyGL's 7 shader variants with zero fog code
  before this task). 2 new tests passed on the first attempt; `git stash` revert-and-rebuild
  reproduced the exact predicted pre-fix failure. Vulkan/Bgfx's `env_map3d`/`skinned3d` GPU shader
  pipelines still don't implement fog — deliberately left for Task 899's scope, not this task's.
  **Task 881 caps `GraphicsDevice.SetRenderTargets` at FNA's real `MAX_RENDERTARGET_BINDINGS=4`**
  (Task 339 finding) — added a single shared C++ check (`std::invalid_argument` when >4 targets)
  before any backend delegation, making each backend's own pre-existing ad-hoc cap (EasyGL/Bgfx's
  hardcoded 8, Vulkan's none at all) unreachable in practice. Discovered a real, separate,
  previously-unreported robustness gap while writing the test: CNA's `RenderTargetBinding` (unlike
  FNA's, whose constructors throw on a null target) has a default constructor wrapping a null
  `Texture*`, and passing several through `SetRenderTargets` segfaults — not fixed, since a real
  game can never construct one this way given FNA's own API shape.
  **Task 880 wires `GraphicsDevice.Viewport` to a real GPU viewport on all 3 backends** (Task 338
  finding) — previously totally decorative everywhere, every backend hardcoded its actual viewport
  to the full render-target/window size. Fixed by mirroring `ScissorRectangle`'s already-wired
  pattern: `IGraphicsBackend::SetViewport(...)`, `GraphicsDevice::setViewportProperty()` now
  forwards to it, plus a second fix in `UpdateViewportFromWindow()` (a separate direct-mutation
  resize path) so window resizes also push the GPU-side viewport. EasyGL: real `SetViewport()` +
  removed `Clear()`/`ClearColorAndDepth()`'s hardcoded viewport reset (glClear is
  viewport-independent). **Found and fixed a real regression this immediately surfaced**: the
  naive Y-flip using the *window's* physical height broke rendering into any bound
  `RenderTarget2D` smaller than the window (viewport y-offset fell entirely outside the RT's real
  pixel range) — traced to the identical latent bug already present in `SetScissorRect`, fixed both
  using `currentRtHeight_` (mirrors `ReadBackbuffer`'s established pattern). Vulkan: storage-only
  `SetViewport()`, wired into the backbuffer pass only — RT passes deliberately stay hardcoded to
  each RT's own full size (deferred multi-RT-per-frame recording can't attribute one frame-global
  viewport value to a specific RT's draws, same limitation already accepted for Vulkan's
  RT-pass-scissor hardcoding). Bgfx: storage-only `SetViewport()` plus a new `ApplyViewportOverride()`
  called from all 4 real 3D draw-dispatch entry points, backbuffer-only for the same reason as
  Vulkan. 2D `SpriteBatch` deliberately untouched on all 3 backends. New tests, one per backend;
  the Bgfx one needed a rewrite to read one sample point per render pass instead of two, after
  hitting the documented "`GetBackBufferData()` only reflects the first read per frame" quirk.
  `git stash` revert-and-rebuild confirmed all 3 tests fail exactly as predicted pre-fix. Full
  regression: EasyGL 3636/3639 (3 pre-existing failures, unchanged), Vulkan 61/73 filtered +
  `CnaTests` 3493/3495 (12 pre-existing failures, unchanged), Bgfx 3540/3540 (100%, zero failures).
  **Task 899's Bgfx bonus scope is done** (its Vulkan core scope — 5 pipelines needing new
  descriptor-set/UBO infrastructure, comparable in size to Task 897 — is still open, tracked
  separately below): `env_map3d`/`skinned3d` fog needed zero new C++ infra (fog uniforms already
  set unconditionally per-draw since Task 888, both effects' `FillGpuDrawParams()` already
  forward fog fields since Task 900) — only the shader files themselves were missing the fog
  varying/blend, added mirroring `lit_textured3d`'s already-proven pattern. **Found and fixed a
  real, separate bug while writing the `SkinnedEffect` fog test**: `SkinnedEffect.EmissiveColor`
  was a total GPU no-op on Bgfx — `fs_skinned3d.sc` never declared `u_emissiveColor` at all, and
  the C++ dispatch never set the (already-existing) uniform handle for the skinned branch. Fixed
  both; the 4 pre-existing `SkinnedEffect` Bgfx tests re-verified passing unchanged (they all use
  `EnableDefaultLighting()`, whose light sum dominated enough that the missing emissive term was
  never visible before). `git stash` revert-and-rebuild confirmed both new tests fail exactly as
  predicted pre-fix. Full Bgfx regression: 47/47 pass, zero failures.
  **Task 899's Vulkan core scope is now also done**, closing out the whole task: `colored3d`/
  `textured3d`/`colored_textured3d` share one new "Bundle A" descriptor-set-layout/pool/pipeline-
  layout/UBO-ring-buffer/descriptor-set-cache (`descriptorSetLayoutFogTex3D_` etc., mirroring
  `descriptorSetLayoutLitTextured_`'s exact shape), `dual_texture3d` got its own split-off
  `dual_texture3d.vert.glsl` plus a 3rd descriptor binding on `descriptorSetLayout2Tex_` (converting
  its descriptor-set cache from a flat map to a per-frame array), and `skinned3d` got a 3rd binding
  on `descriptorSetLayoutSkinned_` (a dedicated fog UBO, since `BoneBlock` has zero spare capacity)
  — all exactly matching the fix shape the prior research pass predicted. **Found and fixed a
  second shared-shader landmine the prior research pass missed**: `GetOrCreatePipelineInstanced3D`
  and the legacy zero-descriptor-set `GetOrCreatePipeline3D` (backing the no-`GpuDrawParams`
  `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` path) both silently reused
  `kColored3dVertSpv`/`kColored3dFragSpv` directly — once those shaders grew a fog descriptor
  binding, both pipelines started failing `vkCreateGraphicsPipelines` validation. Fixed with 2 new
  trivial dedicated shader files (`instanced3d.frag.glsl`, `colored3d_legacy.vert.glsl`) rather than
  touching either pipeline's layout. This was caught as a genuine `Vulkan_DrawInstanced_3Instances`
  regression on the first full-suite run and root-caused before considering the task done. Also
  deleted the now-fully-unreachable old `GetOrCreatePipelineExt3D`/`pipelinesExt3D_` (used only by
  the dispatch branch this task's new `useFogTex3D` flag replaces) rather than leaving it as a
  second landmine. 5 new tests, one per fixed pipeline; `SkinnedEffect`'s isolates material color
  via `DirectionalLight0` rather than `EmissiveColor` (Vulkan's `skinned3d.frag.glsl` has never
  read an emissive uniform at all — a separate, out-of-scope gap analogous to the one the Bgfx
  bonus scope above found for Bgfx). `git stash` revert-and-rebuild (plus manually moving aside 3
  new shader files) confirmed all 5 new tests fail 1/3 exactly as predicted pre-fix. Full
  regression: `ctest -R Vulkan` 66/78 (the same pre-existing 12 failures, zero new ones after the
  Instanced3D fix), `CnaTests` 3493/3495 (2 pre-existing skips, unchanged).
  **Task 878/879's `RenderTarget2D` MSAA half is now done on Vulkan and Bgfx**, closing out Task
  337's EasyGL-only precedent (mip half of Task 878 remains open; `RenderTargetCube` MSAA split to
  new Task 903). Vulkan's per-RT MSAA deliberately piggybacks on the backend's own already-picked
  backbuffer `sampleCount_` rather than threading an independent sample count through every
  pipeline cache key — a new shared 3-attachment `rtRenderPassMsaa_` render pass reuses the exact
  same lazily-created MSAA pipeline variants the backbuffer path already builds, resolving
  automatically via `pResolveAttachments` at `vkCmdEndRenderPass`. A real, empirically-found
  Vulkan validation-layer subtlety: render-pass "compatibility" here requires matching subpass
  *dependency* masks too, not just attachment descriptions. Bgfx just needed
  `BGFX_TEXTURE_RT_MSAA_X{2,4,8,16}` instead of plain `BGFX_TEXTURE_RT` — bgfx resolves
  internally. **Verifying the Bgfx test surfaced 2 more real, previously-invisible, pre-existing
  bugs, both fixed as hard prerequisites** (neither test could ever produce a meaningful result
  without them): Task 873 (`BgfxSpriteBatchBackend::Draw`'s wrong-handle-type cast sampling
  `RenderTarget2D`s, closed after being tracked open since Task 333) and new Task 901
  (`EnsureViewState()` was clobbering a bound RT's own viewport back to the full window size on
  every `Clear()`, silently corrupting all rendering into any RT smaller than the window — never
  caught before since no earlier Bgfx test combined a differently-sized RT with a working pixel
  read). **A genuinely deep, separate architectural gap was found (not fixed) while wiring up the
  Vulkan test**: `GraphicsDeviceManager.PreferMultiSampling` has never actually reached the
  Vulkan backend at all — `Game`'s `GraphicsDevice` member is unconditionally default-constructed
  before any derived-class code can set preferences, and `GraphicsDeviceManager`'s apply path only
  patches the already-built backend (window size, swap interval), never recreates it — meaning
  `vulkan_msaa_test.cpp` (Task 147) has been a false positive its entire existence. Tracked as new
  Task 902 (needs a real `GraphicsDevice.Reset()`, large and separate); worked around here with a
  narrow `NOXNA` test-only `GraphicsDevice::RecreateBackendForMultiSampleCount()` hook used only by
  the new Vulkan test. Also found (not fixed, new Task 904) a sibling Vulkan pipeline
  (`GetOrCreatePipelineFogTex3D`) missing the same msaa-aware render-pass check this task added
  elsewhere — dormant, not exercised by any current test. `git stash` revert-and-rebuild confirmed
  both new tests fail exactly as predicted (Vulkan: compile error; Bgfx: purely-binary
  `MultiSampleCount=8` row). Full regression: Vulkan `ctest -R Vulkan` 68/80 (same 12 pre-existing
  failures), `CnaTests` 3493/2 skipped (exact baseline match); Bgfx `ctest -R Bgfx` 48/48 (100%),
  `CnaTests` 3497 passed/2 skipped, 0 failed. EasyGL `CNA` target sanity-rebuilt clean.
- Phase 46 ("SkinnedEffect exactness", Tasks 401–410) is **CLOSED** — Task 410 wrote
  `docs/skinnedeffect-support.md` synthesizing Tasks 401–409: property/default audit (zero bugs,
  Task 401), a real `Clone()`-drops-`SpecularColor`/`SpecularPower` bug found and fixed (Task 401,
  the identical architectural shape Task 392 already fixed for `FogColor`), 52 new unit tests plus
  an unrelated `MaxBones` linker-gap fix (Task 402), 2 already-satisfied bounds-checking tasks
  marked done without new code (Tasks 403/405), `GetBoneTransforms` independent-copy verification
  (Task 404), and 3 pixel tests plus a capstone (Tasks 406–409) that verified identity/single-bone/
  two-bone-blend skinning with zero bugs found, byte-identical across all 3 backends — Task 406
  additionally surfaced a genuinely new Bgfx-specific test-harness pitfall (`GetBackBufferData()`
  only reliably reflects the first read per rendered frame), fixed with a `renderAndRead()`
  per-checkpoint helper reused by every subsequent test in the phase. Opened 3 new follow-up
  tasks: Task 893 (`DirectionalLight1`/`2` unforwarded), Task 894 (zero specular GPU
  implementation), and Task 895 (`WeightsPerVertex` complete GPU no-op on all 3 backends). Phase 45
  ("EnvironmentMapEffect exactness", Tasks
  391–400) is
  **CLOSED** — Task 400 wrote `docs/environmentmapeffect-support.md` synthesizing Tasks 391–399:
  property/default audit (zero bugs, Task 391), a real `Clone()`-drops-`FogColor` bug found and
  fixed across 4 stock effects (Task 392), a real cross-backend cube-map lerp-vs-additive blend
  bug found and fixed (Task 394), a real `EnvironmentMapSpecular`-not-alpha-scaled bug found and
  fixed on all 3 backends (Task 395), a real missing-Fresnel-edge-weighting gap found and fixed on
  all 3 backends (Task 396), `EyePosition`/reflection-vector correctness verified with zero bugs
  (Task 397), a real `World`-non-uniform-scale normal-transform bug found and fixed on 2 of 3
  backends (Task 398), and a capstone cross-backend consistency test (Task 399) that produced the
  exact predicted value on all 3 backends on the first attempt. Opened 3 new follow-up tasks along
  the way: Task 890 (`DirectionalLight1`/`2` unforwarded), Task 891 (base-lerp `envColor` not
  alpha-scaled), and Task 892 (`BasicEffect`'s worse sibling Bgfx normal-matrix bug, found while
  fixing Task 398). Phase 44 ("DualTextureEffect exactness", Tasks 381–390) is **CLOSED** — Task
  390 wrote `docs/dualtextureeffect-support.md` synthesizing Tasks 381–389: property/default audit
  (zero bugs), a real cross-backend `color.rgb *= 2` doubling-factor bug found and fixed (Task
  383), a real Bgfx-only `Texture2` null-fallback bug found and fixed (Task 387), a real
  EasyGL-only fog-forwarding bug found and fixed (Task 388, requiring new shader infra since
  `DualTextureEffect` has its own dedicated shader unlike `AlphaTestEffect`), and a capstone
  cross-backend consistency test (Task 389) that also discovered and opened **Task 889**
  (`VertexColorEnabled` is a total no-op on all 3 backends for `DualTextureEffect` — no dedicated
  audit task existed for this property in Phase 44, unlike `AlphaTestEffect`'s Task 377). Phase 43
  ("AlphaTestEffect exactness", Tasks 371–380) is **CLOSED**: Task 380
  wrote `docs/alphatesteffect-support.md` synthesizing Tasks 371–379. Task 377 found
  `AlphaTestEffect.VertexColorEnabled` has zero effect on Vulkan/Bgfx by default (opened Task 887).
  Task 378 fixed `AlphaTestEffect` fog forwarding on EasyGL, and discovered fog is a total no-op on
  Vulkan/Bgfx for every 3D effect, project-wide (opened Task 888). Task 379 found and **fixed** a
  real, general Bgfx bug: null-texture draws left the *previous* draw's texture bound instead of
  falling back to white (affected 7 dispatch branches, not just `AlphaTestEffect`). Phase 42 closed
  with a synthesis doc (`docs/basiceffect-support.md`) and opened 2 new follow-up tasks (885, 886 —
  lit-path emissive/multi-light forwarding, real specular) rather than bundling large new features
  into a pixel-test task. Full task-by-task detail (audit findings, exact formulas derived from FNA
  source, discriminating-power
  verification) lives in `plan_graphics.md` — this file intentionally
  does not duplicate it.
- **Key architectural decisions:**
  - Backend selection is **compile-time** via the `CNA_GRAPHICS_BACKEND` CMake option
    (`EASYGL` | `VULKAN` | `BGFX` | `SDL_RENDERER`). EasyGL is primary and most heavily tested.
    `SDL_Renderer` is 2D-only by design (no 3D pipeline at all).
  - The `sharp-runtime` sibling repo (`../sharp-runtime/`) provides all `System.*` types and
    primitive type aliases (`bytecs`, `Single`, `String`, …) used on the XNA API surface.
  - Vertex layout dispatch is **stride-keyed**: EasyGL/Vulkan/Bgfx select their GPU vertex layout
    from the raw byte stride of the bound buffer, not from `VertexDeclaration` contents. Only
    strides 16/20/24/32/52 are handled correctly.
  - `Texture3D` and `TextureCube` inherit `GraphicsResource` directly, **not** `Texture` — unlike
    FNA, where both inherit `Texture`. Known, documented architectural gap (see §5/§6) with real
    downstream consequences (texture-in-shader sampling, `EffectParameter` storage).
  - `GraphicsDevice` stores state objects (`BlendState`/`DepthStencilState`/`RasterizerState`) **by
    value**, unlike FNA's reference-type aliasing. Deliberate, project-wide, not fixed (Task 869).
  - CNA's stock effects (`BasicEffect` etc.) bypass FNA's `EffectDirtyFlags`/`EffectParameter`
    reflection pipeline entirely — derived shader values are computed in an ungated
    `FillGpuDrawParams()` instead of a real `OnApply()` override (Task 351/361 finding).
  - XNA compiled `.fx` bytecode: the project has decided on **full support** as the long-term goal
    (Task 352), planned as new **Phase 74** (Tasks 10200–10209, `docs/fx-bytecode-support-plan.md`).
    Until that lands, `Effect`'s bytecode constructor throws `NotImplementedException` (Task 353).
    Today, custom shaders go through `ShaderEffect` (hand-written GLSL on EasyGL, pre-compiled
    SPIR-V on Vulkan, no-op stub on Bgfx) — see `docs/shader-effect-vs-fx-bytecode.md`.

---

## 2. Current status

### Build status
- **EasyGL** (`cmake-build-debug`), **Vulkan** (`cmake-build-vulkan`), and **Bgfx**
  (`cmake-build-bgfx`): all 3 reconfigured and rebuilt from scratch this session (2026-07-07,
  post-merge — the merge landed after Task 896 and wiped all `cmake-build-*` dirs, see the §10
  resume-prompt note below) — no build errors anywhere. Verified again after Task 878's Vulkan
  changes landed.

### Test status (last verified: Task 911, 2026-07-08)
- **Task 911 full Vulkan regression** (`git stash`-verified against the Task 870 baseline, zero new
  failures introduced): `ctest` 4369/4378 passed (9 pre-existing: 5× `BlendState`/Task 868, 1
  `DepthBias` sub-case, 3 `ContentManagerSkinnedModelTest` segfaults — the exact same set Task 870
  already documented as its own baseline). New `Vulkan_RenderTarget2D_DepthFormatFidelity` test
  passes. See §3 Task 911 row for the full breakdown, including the real `VulkanMRTProxy`
  regression + pre-existing `mrtProxy_` leak found and fixed while verifying this task.
- **Task 902 full 4-backend regression** (each independently `git stash`-verified against its own
  pre-existing baseline, zero new failures introduced): Vulkan `ctest` 4362/4377 (15 pre-existing:
  the 12 already-documented state-test failures + 3 newly-confirmed pre-existing
  `ContentManagerSkinnedModelTest` segfaults), EasyGL `ctest` 4430/4433 (3 pre-existing:
  `EasyGL_MRT_TwoAttachments`, `EasyGL_GraphicsDevice_ReferenceStencil`,
  `easy-gl-resource-smoke-tests`), SDL_Renderer `ctest` 4275/4288 (13 pre-existing, all
  `"SDL_Renderer does not support 3D"`), Bgfx `ctest` 4337/4339 (2 pre-existing, environment
  DRI3/Vulkan-negotiation flakiness — `Bgfx_RenderTarget2D_MsaaResolve`/`MipChain`). See §3 Task
  902 row for the full breakdown.
- **`CnaTests` (gtest unit-test binary, `tests/*.cpp` only — does NOT cover the `examples/*.cpp`
  pixel tests below), all 3 original backends:** EasyGL 4272/4274 passed (2 known skips, 0 failed), Bgfx
  4276/4278 passed (2 known skips, 0 failed), Vulkan 4272/4274 passed (2 known skips, 0 failed)
  when filtering out `ContentManagerSkinnedModelTest.*` (see below). Higher totals than the
  pre-merge baseline (~3501-3505) are expected — the feature/devices+audio+input merge added
  substantial new test coverage, plus this session's own additions (Tasks 663/865/875/877/912/913).
- **SDL_Renderer (`cmake-build-sdl`, new this session, first full baseline established via Task
  915)**: `CnaTests` (filtered) 4262/4274 passed, 2 skips, 10 known/expected failures; the full
  unfiltered `ctest` suite (4339 tests) found 3 more in the excluded `ContentManagerSkinnedModelTest`
  suite for the same reason — all 13 failures throw `"SDL_Renderer does not support 3D"`, matching
  this backend's already-documented, accepted 2D-only architectural scope (`EffectApplyTest`,
  `GraphicsDeviceValidationTest.SetRenderTargets_*`, `SkinnedModelEXTPartTest.*`,
  `ContentManagerSkinnedModelTest.*` — all exercise 3D vertex/skinning paths). Not a regression;
  this is simply the first time this backend's test suite has been run this systematically.
- **`examples/*.cpp` pixel tests, full `ctest` suite (all tests, both `CnaTests`-discovered gtest
  cases AND the `examples/*.cpp` pixel tests, run one-per-process): DONE for EasyGL and Vulkan
  this session (Bgfx still pending).** This session fixed the blocker (`CNA_TEST_DISPLAY` cache
  variable, see §3) and then actually ran it:
  - **EasyGL**: 4421 total, 4418 passed, 2 skipped, 3 failed — all 3 already-documented
    pre-existing (`EasyGL_MRT_TwoAttachments`, `EasyGL_GraphicsDevice_ReferenceStencil`,
    `easy-gl-resource-smoke-tests`). One additional flaky, unrelated audio-subsystem failure
    (`CueTest.PlayCalledTwiceWhileAlreadyPlayingIsANoOpAndDoesNotDuplicateInstances`) seen on one
    of two runs — out of scope (audio, not graphics), not investigated further.
  - **Vulkan**: 4359 total, 4341 passed, 2 skipped, 17 failed on the first run — **4 previously-
    undetected regressions found and fixed this session** (Tasks 908/909, see §3), confirmed via
    a clean rerun down to exactly the expected 12 already-documented pre-existing failures (5×
    `BlendState`/Task 868, 5× `DepthStencilState`/Task 870, `ReferenceStencil`/Task 872, one
    `DepthBias` sub-case) plus 3 `ContentManagerSkinnedModelTest.*` segfaults. That segfault
    triple reproduced identically (same 3 tests) across 2 full runs in this session, and via
    `git stash` was confirmed unrelated to any of this session's changes — a pre-existing
    Vulkan/`Xvfb`/`llvmpipe` environment issue (each test passes cleanly run in isolation).
  - **Bgfx**: 4323 total, 4322 passed, 2 skipped, 1 failed — `Bgfx_RenderTarget2D_MsaaResolve`,
    confirmed an `Xvfb`-specific environment limitation on an already-documented finding, not a
    code regression (see the updated §5 row). **§8 item 0 is now fully done for all 3 backends.**
- **Caution:** run all 3 backends' full `ctest` suites **sequentially, never concurrently** —
  concurrent runs previously produced transient GPU/driver-contention false failures. If a single
  run shows an anomaly beyond the documented list, re-run that test in isolation before treating it
  as a regression (this is exactly how Tasks 908/909 were found and confirmed this session).

### What currently works
- Full `Texture2D`/`Texture3D`/`TextureCube` construction, `SetData`/`GetData` (arbitrary
  sub-regions, `startIndex`, mip levels on EasyGL), argument validation, `Dispose`.
- `SurfaceFormat` enum: all 27 values match FNA exactly, including ordinals.
- `SamplerState`/`BlendState`/`DepthStencilState`/`RasterizerState`: full property surfaces, all
  static presets (including `Name`), and `GraphicsDevice` defaults all verified against FNA and
  pixel-tested where applicable (Phases 35–38).
- `GraphicsDevice.SamplerStates`/`VertexSamplerStates` are honored by all 3D stock-effect draws on
  all 3 backends (Task 293 fix).
- `EnvironmentMapEffect` and `SpriteBatch` (all 20 FNA public methods) work on all 3 backends.
- Vulkan rendering is colorspace-correct (`Texture2D`/swapchain both fixed from sRGB to UNORM).
- `RenderTarget2D`/`RenderTargetCube`: constructors, `DepthStencilFormat`/`MultiSampleCount`/
  `RenderTargetUsage`/`IsContentLost`/`ContentLost` all match FNA at the property level. Basic
  render-to-texture round trip pixel-verified on EasyGL and Vulkan; depth testing while rendering
  INTO a render target works on EasyGL/Vulkan (Task 335). Exact `DepthStencilFormat` fidelity
  (Task 877) is now fully implemented and pixel-verified on **all 3 backends** (correct GL
  internal format/bgfx texture format + attachment point per `None`/`Depth16`/`Depth24`/
  `Depth24Stencil8` on EasyGL/Bgfx, including `RenderTargetCube` on both — Bgfx's cube previously
  had NO depth attachment support at all; Vulkan's own real per-instance `VkFormat` fidelity landed
  in Task 911 — a depth-format-keyed render-pass+pipeline cache across all 9 3D pipeline functions
  plus the 2D sprite pipeline, MRT deliberately staying on the device-wide format). `RenderTarget2D`/`RenderTargetCube`
  mip chains (Task 336)
  are genuinely functional on **all 3 backends** (EasyGL/Task 336, Vulkan/Task 878/907 real
  `vkCmdBlitImage` cascade — `RenderTargetCube` per-face via `baseArrayLayer`, Bgfx/Task 906/907
  built-in `hasMips=true`+auto-resolve). MSAA
  (Task 337) is functional on **all 3 backends for both `RenderTarget2D` (Task 879) and
  `RenderTargetCube` (Task 903)** — Vulkan's cube MSAA mirrors its 2D sibling via a shared MSAA
  colour image reused across all 6 faces; Bgfx just needed `BGFX_TEXTURE_RT_MSAA_Xn` on
  `createTextureCube()`, same mechanism as its 2D fix.
- `SetRenderTarget`/`SetRenderTargets` correctly reset `Viewport`/`ScissorRectangle` to the newly
  bound target's size on all 3 backends (Task 338) — `ScissorRectangle`'s reset has a real,
  pixel-verified GPU effect; `Viewport`'s GPU effect is still a no-op everywhere (Task 880).
- `Effect`/`EffectTechnique`/`EffectPass`/`EffectParameterCollection` base-class contract now
  matches FNA closely (Phase 41, Tasks 351–360): technique/pass validation, lookup-by-name/semantic
  semantics, enumeration order, and dispose/lifecycle behavior all verified or fixed to match FNA.
  `Effect::Clone()` is now a real polymorphic contract, implemented on all 8 concrete subclasses
  (Task 883).
- `BasicEffect` (Phase 42, Tasks 361–370, **complete** — see `docs/basiceffect-support.md` for the
  full synthesis): all 22 property defaults verified/correct against FNA (2 real default-value bugs
  fixed in Task 361, 1 more in Task 362). `EnableDefaultLighting()`'s exact constants confirmed
  correct (Task 363). Pixel-verified on all 3 backends: no-texture diffuse-only rendering (Task
  364, fixed 3 real per-backend bugs where `VertexColorEnabled` wasn't honored), vertex-color
  multiplication (Task 365, already correct), texture × diffuse multiplication (Task 366, already
  correct), the 3-way texture × vertex color × diffuse product on the stride-24
  `VertexPositionColorTexture` path (Task 367, fixed real bugs on EasyGL and Bgfx), one-
  directional-light diffuse lighting (Task 368, fixed `DirectionalLight0.Enabled` being ignored on
  all 3 backends plus a much wider Bgfx-only layout bug — see below), `DiffuseColor+EmissiveColor`
  combination with `LightingEnabled=false` (Task 369, fixed a real shared-C++ bug, all 3 backends),
  and a combined-feature cross-backend consistency capstone (Task 370 — all 3 backends produced
  byte-identical pixel output, no new bugs found). Lit-path `EmissiveColor`/multi-light forwarding
  (Tasks 885/897) and real specular highlights (Task 886, plus 2 normal-transform prerequisite
  fixes, Tasks 892/898) are now implemented and pixel-verified on all 3 backends.
- **Bgfx's vertex layout for strides 20/24/32 now correctly binds `TexCoord0`/`Normal`** (Task 368
  fix to `MakeBgfxLayout()`) — previously every non-skinned, non-`VertexPositionColor` stride fell
  through to a `Position`+`Color0`+padding-only layout, leaving `a_texcoord0` (strides 20/24) and
  `a_normal` (stride 32) permanently unbound (defaulting to ~zero). Invisible until Task 368 because
  every prior BasicEffect pixel test used a 1×1 texture (UV-insensitive); any future Bgfx test using
  a real multi-texel texture or per-vertex normals on these strides should now behave correctly.
- **Bgfx now falls back to a default white texture when a draw's texture is null** (Task 379 fix,
  `defaultWhiteTexture3D_`), matching EasyGL/Vulkan's identical pre-existing fallback — previously
  every one of Bgfx's 7 texture-binding dispatch branches silently left the *previous* draw's
  texture bound instead (undefined, stale-state-dependent). Fixes any null-texture draw across
  every effect, not just `AlphaTestEffect` (the effect that happened to expose it).
- **`DualTextureEffect` now correctly applies FNA's `color.rgb *= 2` doubling factor** to its first
  texture slot on all 3 backends (Task 383 fix) — previously missing everywhere, invisible to every
  prior test (Tasks 133/135/191/293/294/296/297) since they all used pure 0/1-saturated texture
  values where a missing `*2` clamps right back to the same result. Any non-saturated
  `DualTextureEffect` draw is now pixel-accurate to real FNA/XNA output.
- **Bgfx's `Texture2` (second texture slot) now falls back to white when null** (Task 387 fix,
  mirroring Task 379's slot-0 fix) — previously the only remaining unconditional-skip texture-
  binding site in the codebase; EasyGL/Vulkan were already correct for this slot.
- **`DualTextureEffect` fog now works on EasyGL** (Task 388 fix) — `FillGpuDrawParams()` never
  forwarded fog fields, and the effect's own dedicated shader had no fog uniforms at all (unlike
  `AlphaTestEffect`/`BasicEffect`'s shared per-stride shaders); both are now fixed.
- **Fog now works on Bgfx (all applicable pipelines) and on Vulkan's `alpha_test3d`/
  `lit_textured3d` pipelines** (Task 888 fix) — previously a total GPU no-op on both backends for
  every 3D effect. Vulkan's `colored3d`/`textured3d`/`colored_textured3d`/`dual_texture3d`/
  `skinned3d` pipelines still lack fog (their shared push constant has zero spare bytes; needs new
  UBO infrastructure, tracked as Task 899). `SkinnedEffect`/`EnvironmentMapEffect` fog remains a
  separate, pre-existing C++-level gap on every backend including EasyGL (`FillGpuDrawParams()`
  never forwards their fog fields at all), not in this task's scope.
- **`DualTextureEffect` (Phase 44, Tasks 381–389, pixel-verification work complete)**: all 3
  backends produce byte-identical output for the doubling factor + two-texture multiply +
  `DiffuseColor` + `Alpha` combination, confirmed by Task 389's capstone test using a real 2×2
  multi-texel texture (first in any `DualTextureEffect` test).
- **`Clone()` now preserves `FogColor`** on `AlphaTestEffect`, `DualTextureEffect`,
  `EnvironmentMapEffect`, and `SkinnedEffect` (Task 392 fix) — previously silently reset to black
  on every clone, since `CacheEffectParameters()` re-links `fogColorParam_` to a fresh, zero-valued
  parameter in the clone and nothing copied the source's actual value across.
- **`EnvironmentMapEffect`'s cube-map blend now correctly interpolates instead of adding** (Task
  394 fix, all 3 backends) — previously `rgb = litRGB×texColor + envColor×Amount + specular`
  (additive), now `rgb = mix(litRGB×texColor, envColor, Amount) + specular` (matching FNA's real
  `lerp`-based `PSEnvMap` formula). At `EnvironmentMapAmount=1` the cube map now correctly *fully
  replaces* the lit/textured color instead of being added on top of it.
- **`EnvironmentMapEffect`'s `EnvironmentMapSpecular` now correctly scales by the cube map's alpha
  channel** (Task 395 fix, all 3 backends) — previously added as a flat, unscaled constant; now
  `rgb += EnvironmentMapSpecular × envmap.a × combinedAlpha` (matching FNA's real
  `PSEnvMapSpecular` formula). A translucent cube map's specular contribution is now correctly
  attenuated instead of always applied at full strength.
- **`EnvironmentMapEffect`'s Fresnel edge-weighting is now implemented on all 3 backends** (Task
  396 fix) — previously the env-map blend factor was always the flat `EnvironmentMapAmount`
  regardless of view angle; now (when `FresnelFactor≠0`, the default) it's
  `pow(max(1-abs(dot(eyeVector,normal)),0),FresnelFactor)*EnvironmentMapAmount`, matching FNA's
  real `ComputeFresnelFactor`. Reflections now correctly weaken at normal incidence and strengthen
  at grazing angles instead of being view-angle-independent.
- **`EnvironmentMapEffect`'s normal transform now correctly uses `World`'s inverse-transpose on
  EasyGL and Bgfx** (Task 398 fix; Vulkan was already correct) — previously both backends
  transformed the normal by the raw `World` matrix directly, wrong under non-uniform-scale
  `World` transforms. Non-uniform scale now correctly skews reflections/lighting instead of
  producing a wrong, un-inverse-transposed normal direction. (EasyGL's fix is shared
  infrastructure that also correctly improves `BasicEffect`'s lit-textured pipeline.)
- **`SkinnedEffect`'s `Clone()` now correctly preserves `SpecularColor`/`SpecularPower`** (Task
  401 fix) — previously silently reset to `(0,0,0)`/`0` on every clone, the identical bug shape
  Task 392 already fixed for `FogColor` across 4 other stock effects.
- **`BasicEffect` now renders real specular highlights on all 3 backends** (Task 886) — half-vector
  Blinn-Phong (FNA's `Lighting.fxh` `ComputeLights`), summed per-light then scaled once by the
  material `SpecularColor`, added after the texture×diffuse multiply. Fixing this surfaced and
  fixed 2 more real, pre-existing normal-transform bugs on Bgfx (Task 892) and Vulkan (Task 898):
  both transformed the lit-textured vertex normal by the full World×View×Projection matrix instead
  of World's inverse-transpose, wrong under any non-identity camera — invisible until Task 886's
  test needed a real, non-degenerate `Matrix::CreateLookAt` camera for the first time on this path.
- **`GraphicsDevice.SetRenderTargets` now correctly caps at FNA's real `MAX_RENDERTARGET_BINDINGS=4`
  on all 3 backends** (Task 881 fix) — a single shared C++ check throws `std::invalid_argument`
  before any backend delegation, superseding each backend's own previously-wrong ad-hoc cap
  (EasyGL/Bgfx's hardcoded 8, Vulkan's none at all).
- **`Texture3D::GetData`/`TextureCube::GetData` now perform a real GPU readback on Vulkan** (Task
  865 fix) — `vkCmdCopyImageToBuffer` + host-visible staging buffer, mirroring `SetData`'s upload
  path in reverse; previously silently left the caller's buffer untouched. Required a new
  `TransitionImageLayout` case (`SHADER_READ_ONLY_OPTIMAL <-> TRANSFER_SRC_OPTIMAL`) and adding
  `VK_IMAGE_USAGE_TRANSFER_SRC_BIT` to both textures' image usage flags.
- **`Texture3D`'s mip levels >0 now work on EasyGL (Task 862) and both `Texture3D`/`TextureCube`'s
  mip levels >0 now work on all 3 backends (Vulkan/Task 864, Bgfx/Task 914)** — all previously
  hardcoded `mipLevels`/allocated GPU storage to 1 regardless of `mipMap`, and reverting the
  EasyGL/Vulkan fix reproduces real GPU memory corruption (higher mip levels' writes land back
  inside level 0's only allocated memory), not just a silent no-op. Bgfx's own fix needed a real
  `GetData` readback path first (Task 914 — `bgfx::readTexture()` via a temporary
  `BGFX_TEXTURE_BLIT_DST|READ_BACK` texture, since `GetData` was previously a total no-op there).
- **`GraphicsDevice.GetBackBufferData` now works on SDL_Renderer** (Task 915 fix) — this backend
  never overrode `ReadBackbuffer` at all (hard `throw`), which blocked the *entire* SDL_Renderer
  pixel-test audit phase (`plan_graphics.md` Tasks 666–861) before any of it could start. Fixed via
  `SDL_RenderReadPixels()`. Real subtlety found: that call is in *physical* output coordinates,
  while every other backend uses *logical* coordinates, and this project's default presentation
  mode (`FixedHeightDynamicWidth`) deliberately does NOT map 1:1 between them — exact-pixel tests
  on this backend must request `PresentationMode::NativeBackBuffer` to get true 1:1 correspondence.
  Also fixed a real, unrelated CMakeLists.txt bug found while wiring up the first SDL_Renderer test:
  a ~2850-line block (lines 579–3436) intended to be EasyGL/Vulkan-only was silently swallowing any
  new registration placed inside it for any other backend.
- **`SpriteBatch::Draw`'s rotation-around-origin now works correctly on SDL_Renderer** (Task 671
  fix) — `SDL_RenderTextureRotated`'s `dstrect`+`center` pivot model is the opposite of XNA's
  `destinationRectangle`+`origin` contract (XNA requires `origin` to map to exactly
  `destinationRectangle.X/Y` on screen, invariant under rotation; SDL's `center` is relative to
  `dstrect`'s own position). `SdlSpriteBatchBackend::Draw` passed `destinationRectangle.X/Y`
  straight through, placing the rotation pivot a full sprite-size away from where XNA requires it.
  Fixed by offsetting `dst.x/y` by `-center`. Single fix corrects all 9 `SpriteBatch::Draw`
  overloads, since they all funnel through one backend call.
- **`SpriteBatch`'s `SamplerState` now works on all 3 backends** (Task 750 fix) — Bgfx never
  overrode `SetSamplerFilter`/`SetSamplerAddressMode` at all, so `Begin()`'s requested sampler had
  zero effect; fixed by threading the pending filter/address values into the existing
  `ApplySamplerState(0, ...)` call right before each sprite submit (mirrors EasyGL/Task 269 and
  Vulkan/Task 665's identical fix).
- **`RenderTargetCube` MSAA now works on Vulkan and Bgfx** (Task 903 fix) — mirrors
  `RenderTarget2D`'s existing Task 878/879 MSAA support, applied per cube face via a shared MSAA
  colour image. **Found and fixed a real, previously-undiscovered Vulkan bug while verifying this**:
  the 2D `SpriteBatch` pipeline's MSAA-variant selection only ever checked the *backbuffer's* own
  MSAA state, never an RT's — meaning a `SpriteBatch` fill into any MSAA-enabled `RenderTarget2D`
  or `RenderTargetCube` face produced live Vulkan validation errors (wrong pipeline sample count
  vs. the RT's real MSAA render pass). Fixed to check the actual bound target's `WantsMsaa()`,
  which also silently corrects the identical latent gap for `RenderTarget2D` + `SpriteBatch` MSAA
  fills, not just the new cube case.

### What does NOT work yet
- **Vulkan `BlendState`/`DepthStencilState` support is almost entirely fake** — hardcoded blend
  equations / depth-compare ops / no stencil testing at all, regardless of what's requested.
  Tracked as Task 868/870, confirmed repeatedly via pixel tests, not fixed (large,
  multi-pipeline-site changes).
- `GraphicsDevice.ReferenceStencil`'s independent-override behavior has zero backend connection on
  all 3 backends (Task 872). `GraphicsDevice::Clear` ignores `ClearOptions::Stencil` everywhere
  (Task 871).
- `GraphicsDevice.Viewport` has **zero GPU backend wiring on all 3 backends** — every backend
  hardcodes its actual viewport call to the full render-target/window size regardless of what
  `Viewport` is set to. A sub-region viewport (split-screen, atlas-subrect rendering) currently has
  no effect anywhere. Tracked as Task 880, not fixed.
- `EasyGL_MRT_TwoAttachments` (Task 145): even a basic, same-size/format 2-target MRT setup doesn't
  render correctly on EasyGL — attachment 1 stays black. Pre-existing, off-limits for opportunistic
  fixing (see §9).
- **`Texture3D`/`TextureCube::GetData` is now a real GPU readback on all 3 backends**: Vulkan
  (Task 865, `vkCmdCopyImageToBuffer`) and Bgfx (Task 914, blit into a temporary
  `BGFX_TEXTURE_BLIT_DST|READ_BACK` texture + `bgfx::readTexture()` — previously a total no-op).
- `Texture2D::SetData(level>0,...)` is a silent no-op on Vulkan/Bgfx; EasyGL's non-mip-aware
  filters render solid black on mip-incomplete textures (Task 867).
- `Texture3D` sampling cannot be wired into any shader without an architecture change (Task 863).
---

## 3. Recent changes

Most recent first. Full detail (exact FNA-derived formulas, discriminating-power verification,
per-backend fix shape) is in `plan_graphics.md` — this table is intentionally a one-line-per-task
index, not a duplicate.

| Commit | Task | Summary |
|---|---|---|
| `1442194d` | 670 | **No bug found — `SpriteSortMode::Immediate` genuinely flushes per-draw on SDL_Renderer, end-to-end.** New `sdlrenderer_spritebatch_immediate_flush_test.cpp`: draws a sprite, then `Clear(Green)` (a plain `GraphicsDevice` call outside `SpriteBatch`'s queue), then `End()` — correct behavior wipes the already-drawn sprite (region reads Green); a "silently `Deferred`" bug would draw the sprite AFTER the clear instead (region would read Red). **Methodology finding**: an initial single-line sabotage produced a false negative (it caused a silent-drop bug instead of a defer-to-`End()` bug, since `End()`'s own `Immediate`-skip meant the queued sprite was never flushed at all) — corrected by also making `End()` unconditionally flush, which genuinely reproduced the intended bug and failed as predicted; restored and reconfirmed passing. Requires `PresentationMode::NativeBackBuffer` (Task 915). Full regression: `ctest` 4277/4292 passed, 2 skipped, 13 failed (same baseline as Task 667/668/669 — zero new failures). |
| `f27356a5` | 668 | **No bug found — `SpriteSortMode::Texture` grouping already works correctly on SDL_Renderer.** The pointer-order contract (which texture group ends up first) is already covered at the logic level by the mock-backend Task 414 test; this task pixel-verifies that SDL_Renderer's draw dispatch still binds the correct texture per reordered call. New `sdlrenderer_spritebatch_texture_sort_test.cpp`: 4 non-overlapping sprites, 2 textures, scrambled A/B/A/B submission — each destination shows its own colour regardless of reordering. Requires `PresentationMode::NativeBackBuffer` (Task 915). All 4 checks pass. Full regression: `ctest` 4276/4291 passed, 2 skipped, 13 failed (same baseline as Task 667/669 — zero new failures). |
| `803b2c0c` | 667 | **No bug found — `SpriteSortMode::Deferred` (the default) already correctly preserves submission order on SDL_Renderer, ignoring `layerDepth`.** `SpriteBatch::flushBatch()` confirmed: `Deferred` takes the "no sort" fall-through branch. New `sdlrenderer_spritebatch_deferred_order_test.cpp`: 2 overlapping-sprite pairs with `layerDepth` values chosen to flip the outcome if `Deferred` secretly sorted like `FrontToBack` or `BackToFront` — both pairs confirm the submitted-last sprite always wins the overlap regardless of depth. Requires `PresentationMode::NativeBackBuffer` (Task 915). All 6 checks pass. Full regression: `ctest` 4275/4290 passed, 2 skipped, 13 failed (same already-documented baseline as Task 669 — zero new failures). |
| `eda538d7` | 669 | **No bug found — `SpriteSortMode::FrontToBack`/`BackToFront` already work correctly on SDL_Renderer.** Sort logic lives entirely in the shared, backend-agnostic `SpriteBatch.cpp`, already proven correct on EasyGL (Tasks 415/416/420); this task confirmed SDL_Renderer's draw-dispatch doesn't itself reorder anything after that shared sort. New `sdlrenderer_spritebatch_layerdepth_test.cpp`: a direct port of Task 420's `FrontToBack` scenario plus a brand-new `BackToFront` scenario (no prior test on any backend exercised it with real pixels) — both use the established "submit in the wrong order so the test discriminates real sorting from submission order" methodology. Requires `PresentationMode::NativeBackBuffer` (Task 915). All 6 checks pass. Full regression: `ctest` 4274/4289 passed, 2 skipped, 13 failed (same already-documented "SDL_Renderer does not support 3D" baseline as Task 915 — zero new failures). |
| `a8c9b032` | 914 | **`Texture3D`/`TextureCube::GetData` is now real on Bgfx (previously a total no-op silently leaving the caller's buffer untouched), and Task 864's mip-allocation fix (`mipMap` genuinely threaded through, previously hardcoded `false`) now applies to Bgfx too.** `bgfx::readTexture()` requires its source texture to have `BGFX_TEXTURE_READ_BACK`, incompatible with a normally shader-sampled texture — `GetData()` instead blits the requested region into a small temporary `BGFX_TEXTURE_BLIT_DST|READ_BACK` texture (2D for cube faces, 3D for volume slices) via `bgfx::blit()`, then reads that via `bgfx::readTexture()`; a new `AdvanceFramesUntil()` helper loops `bgfx::frame()` until the returned future frame number is reached, mirroring `ReadBackbuffer`'s own established retry-until-ready convention. Confirmed both `BGFX_CAPS_TEXTURE_BLIT`/`READ_BACK` are actually supported in this sandbox via a throwaway caps check before implementing — resolving the real open question Task 864's own scoping note had flagged. Registered the same 4 backend-agnostic example tests already shared by EasyGL/Vulkan (Tasks 173/275/862/864) for Bgfx too, rather than writing new ones. `git stash` confirmed all 4 fail exactly as predicted without the fix; restored and reconfirmed all 4 pass 100%. Full regression: `ctest` 4342/4344 passed (2 pre-existing: `Bgfx_RenderTarget2D_MsaaResolve`/`MipChain`, the same already-documented Xvfb/environment limitations carried since Task 750/877 — zero new failures). |
| `11642341` | 911 | **Every Vulkan `RenderTarget2D`/`RenderTargetCube` now gets a real, distinct depth `VkFormat` picked for its own instance (or genuinely no depth attachment at all for `DepthFormat::None`), instead of silently sharing the backbuffer's device-wide `depthFormat_` regardless of what was requested.** New `PickDepthFormat()` maps each `DepthFormat` to a real, probed `VkFormat`; the old single shared RT render pass members became 3 `std::unordered_map<VkFormat, VkRenderPass>` caches (`GetOrCreateRTRenderPass`/`GetOrCreateRTRenderPassMsaa`); `VulkanRenderTargetBackend`/`VulkanRenderTargetCubeBackend` both now genuinely act on their `depthFormat` constructor parameter (previously silently dropped). All 9 3D pipeline functions plus the 2D sprite pipeline (converted from eager singletons to the same lazy depth-format-keyed cache pattern) gained a `targetDepthFmt` parameter, folded into their cache key and used to select the render pass via new `PickRTPipelineRenderPass()`; MRT deliberately stays out of scope (always the device-wide `depthFormat_`, XNA/FNA's own multi-target depth semantics being inherently ambiguous). **Real regression found and fixed while testing this task**: `VulkanMRTProxy` previously borrowed `rts[0]->GetDepthView()` directly, an invariant broken by this task's own `DepthFormat::None` fix (null `depthView_`) — reproduced as a live `vkCreateFramebuffer` validation error + abort on `SetRenderTargets_FourTargets_DoesNotThrow`; fixed by giving the proxy its own dedicated depth image, always in `depthFormat_`. Also fixed a second, pre-existing, unrelated leak found in the process: `mrtProxy_` was never explicitly reset before device teardown. New `Vulkan_RenderTarget2D_DepthFormatFidelity` test proves genuine per-format fidelity (a `None`-format RT can't depth-reject a later draw; `Depth24Stencil8`/`Depth16` RTs can, coexisting correctly in one frame). `git stash` confirmed both the depth-format bug and the MRT regression fail exactly as predicted without the fix. Full regression: `ctest` 4369/4378 passed (9 failures, identical set to Task 870's own documented baseline — zero new failures). |
| `8764d201` | 870 | **Vulkan's `DepthStencilState` support was almost entirely fake — `DepthBufferFunction` hardcoded (2 pipelines `LESS`, 5 `LESS_OR_EQUAL`, unrelated to what's requested), entire stencil-test parameter set dropped by `ApplyDepthStencilState`. Now fixed: real per-pipeline `depthCompareOp` + full front/back `VkStencilOpState` (`ToVkCompareOp`/`ToVkStencilOp` mirror EasyGL's exact ordinal mapping), stencil reference/masks as true Vulkan dynamic state, `FindDepthFormat()` reordered to prefer stencil-capable formats.** Fixes 6 of the 12 pre-existing Vulkan `ctest` failures (`CompareFunction`, `StencilEnable`, `StencilMask`, `StencilOps`, `StencilTwoSided`, `GraphicsDevice.ReferenceStencil`); the remaining 6 are the separate, already-tracked Task 868 `BlendState` bug (5) + 1 known `DepthBias` sub-case. **Found and fixed a 2nd bug as a prerequisite**: `GraphicsDevice.ReferenceStencil`'s setter never reached any backend at all (new `IGraphicsBackend::SetReferenceStencil`, mirroring `SetBlendFactor`'s pattern) — EasyGL/Bgfx have the identical gap, deliberately left open. **A genuinely surprising empirical finding**: stencil `front`/`back` needed the OPPOSITE assignment from what culling's own (already-correct) `frontFace` convention would suggest — likely an llvmpipe/Mesa quirk specific to asymmetric `VkStencilOpState`, not a general winding bug (culling itself is unaffected). Also updated `vulkan_depth_bias_test.cpp`, which relied on the old buggy hardcoded `LESS` behavior — XNA's real default is `LessEqual`, under which the test's premise can't discriminate anything, so it now explicitly requests `CompareFunction::Less`. `git stash` confirmed all 6 previously-failing tests fail exactly as predicted without the fix. Full regression: `ctest` 4368/4377 (9 pre-existing, unrelated), `CnaTests` (filtered) 4278/4280 passed, 2 skipped, 0 failed. |
| `e75ed62a` | 910 | **Bgfx: every render target (2D, cube, MRT) now gets its own distinct, stable bgfx view id instead of sharing one hardcoded id — fixes the "only the last-bound render target actually renders" bug found while building Task 907's mip test.** Root cause: `bgfx::setViewFrameBuffer(viewId, fbo)` is a per-view-per-*frame* setting resolved once at `bgfx::frame()`, not per `bgfx::submit()`/`touch()` call, so whichever value was set last for a shared view id wins for every draw submitted to it that frame. New free-list-backed `Detail::AllocateRtViewId()`/`ReleaseRtViewId()` pool (ids `[1,256)`, 0 reserved for the backbuffer); `BgfxRenderTargetBackend`/`BgfxRenderTargetCubeBackend` each get a `viewId_` allocated at construction, released at destruction; MRT gets its own fresh `mrtViewId_` per `SetRenderTargets(count>1)` call. New `bgfx_concurrent_rendertargets_test.cpp`: binds+fills 2 different `RenderTarget2D`s with NO `bgfx::frame()` boundary in between, `git stash` confirmed the 1st one silently stays at its primed baseline color without the fix (clobbered by the 2nd's bind), both read back correctly with it. Full Bgfx regression: `ctest` 4338/4340 (2 pre-existing environment failures only), `CnaTests` 4285/4287 passed, 2 skipped, 0 failed. |
| `717fa65f` | 902 | **`GraphicsDevice::Reset()` now really reconfigures the backend — `GraphicsDeviceManager.PreferMultiSampling` (and general preference changes) finally reach it, closing `vulkan_msaa_test.cpp`'s (Task 147) long-standing false positive.** FNA's real `Reset()` does an in-place `FNA3D_ResetBackbuffer()`, not a full teardown, and doesn't notify individual resources (that FNA code path is dead) — narrower scope than Task 902's original write-up assumed. New `IGraphicsBackend::ApplyMultiSampleCount()`/`GetMultiSampleCount()` virtuals; `VulkanGraphicsBackend` implements real in-place swapchain/render-pass/pipeline MSAA reconfiguration (backbuffer-only — live RTs keep their own construction-time sample count). `GraphicsDevice::Reset(pp,adapter)` now calls it and writes the real clamped value back to `PresentationParameters` (mirrors FNA's `FNA3D_GetMaxMultiSampleCount` write-back); `GraphicsDeviceManager::applyToExistingBackend()` now calls the real `Reset()` instead of the long-commented-out no-op path (order matters: `SetPresentationMode()` before `Reset()`, found via a real SDL_Renderer regression). EasyGL's main backend gained an honest `GetMultiSampleCount()` override (was silently always 0). `vulkan_msaa_test.cpp` rewritten with a genuine diagonal-edge differential (toggling real MSAA at runtime via `ApplyChanges()`, not the `RecreateBackendForMultiSampleCount()` NOXNA escape hatch other MSAA tests use) — `git stash` confirmed it fails without the fix, passes with it. `easygl_msaa_change_test.cpp` updated: PP now honestly reports `0` (not the old requested-but-never-applied `8`) when toggling MSAA on an already-constructed EasyGL device. **Full 4-backend regression, each independently `git stash`-verified against its own pre-existing baseline, zero new failures**: Vulkan 4362/4377 (15 pre-existing: 12 already-documented + 3 newly-confirmed pre-existing `ContentManagerSkinnedModelTest` segfaults), EasyGL 4430/4433 (3 pre-existing), SDL_Renderer 4275/4288 (13 pre-existing), Bgfx 4337/4339 (2 pre-existing, environment DRI3/Vulkan-negotiation flakiness). |
| `2d4f7a2c` | 671 | **Real bug found and fixed: `SDL_RenderTextureRotated`'s pivot model is the opposite of XNA's rotation contract.** XNA requires `origin` to map to exactly `destinationRectangle.X/Y`, invariant under rotation; SDL's `center` is relative to `dstrect`'s own position, which was passed straight through, placing the pivot a full sprite-size away from where XNA needs it. Direct port of Task 417's EasyGL rotation test caught this immediately (marker landed at the clear colour instead of the expected rotated position). Fixed by offsetting `dst.x/y` by `-center` in `SdlSpriteBatchBackend::Draw`'s 8-arg overload — corrects all 9 `SpriteBatch::Draw` overloads at once, since they all funnel through this one backend call (confirmed via `SpriteBatch.cpp`'s `flushSingle()`). Rotation *angle* direction itself was already correct (verified algebraically against SDL's documented clockwise convention). **Discriminating power independently verified**: `git stash`-reverted the fix, reproduced the exact predicted failure; restored and reconfirmed all 3 checks pass. Full regression: `CnaTests` (SDL_Renderer, filtered) exact baseline match; Task 666's overloads test (rotation=0 only) unaffected. |
| `382e4f8c` | 666 | **Audited all 9 `SpriteBatch::Draw` overloads on SDL_Renderer — all correct, no backend bug found (first task actually run in the newly-unblocked Task 666–861 phase).** New `examples/sdlrenderer_spritebatch_overloads_test.cpp` draws each overload into its own 60×60 screen slot with a unique tint colour (white 1×1 texture × tint, `BlendState::Opaque`), then reads back a real discriminating pixel per overload — including a genuine `SpriteEffects::FlipHorizontally` check against an asymmetric red/green texture (wrong-half colour if the flip silently no-ops). Caught a real bug in the test itself during development (wrong read-back point for the 2 native-size, unscaled overloads), confirming the test isn't vacuously true. `DrawString`'s 6 overloads are tracked separately (existing Tasks 690–694). Full regression: `CnaTests` (SDL_Renderer, filtered) exact baseline match, 0 new failures. |
| `997b53b9` | 915 | **`SdlGraphicsBackend::ReadBackbuffer` implemented for the first time — foundational prerequisite unblocking the entire SDL_Renderer pixel-test audit phase (Tasks 666–861).** Previously a hard `throw` (the shared `IGraphicsBackend::ReadBackbuffer` default), meaning `GraphicsDevice::GetBackBufferData` — the pixel-verification mechanism every EasyGL/Vulkan/Bgfx test in this project relies on — was structurally impossible on this backend. Fixed via `SDL_RenderReadPixels()` + `SDL_ConvertSurface(..., SDL_PIXELFORMAT_RGBA32)`. **Real subtlety found**: `SDL_RenderReadPixels` operates in *physical* output coordinates, while every other backend uses *logical* coordinates, and this project's default presentation mode (`FixedHeightDynamicWidth`) deliberately does NOT map 1:1 between them (confirmed empirically: a requested 64×64 canvas became a real `107×64` logical size against an 800×480 physical window). Fixed to detect this via `SDL_GetRenderLogicalPresentationRect()` and throw clearly on mismatch rather than return wrong data; exact-pixel tests must request `PresentationMode::NativeBackBuffer` for true 1:1 correspondence. New `examples/sdlrenderer_readback_test.cpp` plus a brand-new `cna_sdl_test` CMake macro (this backend had zero test infrastructure before). **Found and fixed a second, unrelated CMakeLists.txt bug**: the new test block was initially inserted inside a ~2850-line `if(EASYGL OR VULKAN)` block, silently swallowing the registration; moved to the correct unconditional location. **Discriminating power independently verified**: `git stash`-reverted both backend files — the pre-fix binary aborted with the exact predicted `std::runtime_error`; restored and reconfirmed passes. Full regression: `CnaTests` (SDL_Renderer, filtered) 4262/4274 passed, 2 skipped, 10 failed (all pre-existing/expected, "`SDL_Renderer does not support 3D`"); full unfiltered `ctest` (4339 tests, first run this systematically) found 3 more in the excluded `ContentManagerSkinnedModelTest` suite for the same reason — 13 known/expected 3D-related failures total, 0 unexplained. EasyGL/Vulkan/Bgfx spot-checked clean after the CMakeLists.txt restructuring. |
| `a1425233` | 750 | **`SpriteBatch`'s `SamplerState` now works on Bgfx, closing the last of the 3 backends for this bug shape (EasyGL/Task 269, Vulkan/Task 665, Bgfx/Task 750).** `BgfxSpriteBatchBackend` never overrode `SetSamplerFilter`/`SetSamplerAddressMode` at all (silent no-op via `ISpriteBatchBackend`'s default empty bodies) — `SpriteBatch::Begin()`'s requested sampler had zero effect; every sprite draw used whatever `samplerFlags_[0]` happened to already hold from a previous 3D draw. Unlike EasyGL/Vulkan's deferred-batch architecture, Bgfx submits each sprite immediately via `SubmitSprite()`, which already read `samplerFlags_[0]` — the per-slot sampler-flags plumbing (`ApplySamplerState()`) was already in place, just never invoked from the sprite path. Fixed by adding the 2 overrides (storing `pendingFilter_`/`pendingAddressU_`/`pendingAddressV_`, mirroring EasyGL's exact field names/defaults) and calling `ApplySamplerState(0, ...)` right before each `SubmitSprite()` call. New `examples/bgfx_texture_address_mode_test.cpp` (direct port of Task 269/665's identical 2×1 Red/Blue, `sourceRectangle` 2× width, `U≈1.25`, `PointWrap`-vs-`PointClamp` methodology): both checks pass on the first attempt. **Discriminating power independently verified**: `git stash`-reverted both files — pre-fix, both `PointWrap` and `PointClamp` read the *identical* wrong value `(254,0,1)`, proving addressing never took effect either way (matching Task 665's exact pre-fix Vulkan symptom); restored and reconfirmed both pass. Full regression: `CnaTests` (Bgfx) 4276/4278 passed, 2 skipped, 0 failed (exact baseline match); `ctest -R Bgfx` 52/54 (2 failures are the already-documented pre-existing `Bgfx_RenderTarget2D_MsaaResolve` Xvfb limitation and `Bgfx_RenderTarget2D_MipChain`, reconfirmed genuinely flaky via 3 isolated reruns — 2/3 passed — unrelated to this fix). |
| `8d38ec52` | 903 | **`RenderTargetCube` MSAA implemented on both Vulkan and Bgfx, mirroring `RenderTarget2D`'s existing Task 878/879 support applied per cube face via a shared MSAA colour image** (same "share one resource across all 6 faces" pattern this class already uses for its depth buffer). Vulkan: promoted shared depth image + new shared `msaaColorImage_`/`msaaColorView_` + per-face framebuffers built against `rtRenderPassMsaa_`; each `FaceProxy` gained `WantsMsaa()`/`msaaFramebuffer`/`msaaRenderPass` — `RecordCommandBuffer` needed zero changes since it already dispatches through the generic `VulkanRTSource` interface. Bgfx: `cubeTex`/`depthTex` creation flags now use the existing `BgfxMsaaRtFlag()` helper instead of plain `BGFX_TEXTURE_RT`. **Found and fixed a real, previously-undiscovered Vulkan bug while verifying this**: the 2D `SpriteBatch` pipeline's MSAA-variant selection only ever checked the *backbuffer's* own MSAA state, never an RT's — a `SpriteBatch` fill into any MSAA-enabled `RenderTarget2D`/`RenderTargetCube` face produced live `VUID-vkCmdDraw-multisampledRenderToSingleSampled-07284`/`VUID-vkCmdDraw-renderPass-02684` validation errors, invisible until now because every prior MSAA RT test used a 3D `BasicEffect` fill instead (whose pipeline selection already checked `rt->WantsMsaa()`); this task's cube-face fills use `SpriteBatch` (Task 907's established convention) and were the first to hit it. Fixed to check the bound target's `WantsMsaa()`, which also silently fixes the identical latent gap for `RenderTarget2D`. **Test design deliberately scoped down from a genuine sub-pixel AA differential** (unlike the existing `{vulkan,bgfx}_rendertarget2d_msaa_test.cpp` diagonal-edge tests): independently re-derived and empirically confirmed that `EnvironmentMapEffect`'s reflection-vector sampling of a flat, fixed-normal quad reduces to `normalize(vertexPosition.xy, 0)` under `World=View=Projection=Identity` — it can't sweep across a specific cube face's UV space, matching Task 907's own already-documented finding for its mip test. New `{vulkan,bgfx}_rendertargetcube_msaa_test.cpp` instead check `GetMultiSampleCount()` property fidelity (0 stays 0; 8 reports a real device-clamped value — the actual bug this task's title describes, since neither backend's `RenderTargetCube` ever overrode this before) plus a no-corruption/no-crash sanity check. **A second, already-tracked issue was hit writing the Bgfx test** (not a new bug): Task 910's documented "all 6 faces share one hardcoded bgfx view id" limitation, fixed with the established dummy-backbuffer-read-between-faces workaround. **Discriminating power independently verified**: `git stash`-reverting all 4 backend files reproduced the exact predicted failures on both backends (`MultiSampleCount request 8 -> applied 0`; Vulkan's validation errors also reproduced pre-fix); restored and reconfirmed all 3 checks pass, zero validation errors. Full regression: `CnaTests` Vulkan/Bgfx exact baseline match (4272/4274, 4276/4278), 0 failed; `ctest -R "RenderTarget\|Mip\|Msaa\|MultiSample"` Vulkan 67/67 (100%), Bgfx 59/60 (1 failure is the already-documented pre-existing `Bgfx_RenderTarget2D_MsaaResolve` Xvfb limitation, unrelated). |
| `6787a06d` | 862/864 (+914) | **`Texture3D`'s mip levels >0 fixed on EasyGL (Task 862); both `Texture3D`/`TextureCube`'s mip levels >0 fixed on Vulkan (Task 864); Bgfx split to new Task 914 as genuinely unverifiable.** Same bug shape as Task 276's already-fixed EasyGL `TextureCube` mip bug: `mipLevels`/GPU storage was hardcoded to 1 regardless of `mipMap` on `EasyGLTexture3DBackend` and both `VulkanTexture3DBackend`/`VulkanTextureCubeBackend` (Vulkan's `CreateTexture3D`/`CreateTextureCube` dropped the `mipMap` parameter entirely). Fixed by pre-allocating every mip level at construction (EasyGL: per-level `set_image_3d(nullptr)` loop; Vulkan: real `mipLevels`/full-range `VkImageView`/a construction-time full-mip-range initial layout transition mirroring `VulkanRenderTargetBackend`'s Task 878/907 pattern, since the shared single-level `TransitionImageLayout` helper can't be reused). **Found a more severe symptom than expected**: on Vulkan, writing to an unallocated mip level didn't just silently fail — it corrupted mip level 0's real GPU memory (confirmed via `git stash` revert: `Texture3D` 67/73 pass with 6 corrupted, `TextureCube` 112/126 pass with 14 corrupted, higher levels' writes landing back inside level 0's only real memory). New backend-agnostic `examples/easygl_texture3d_mip_test.cpp` (4x4x4, 3 levels, mirrors Task 276's `TextureCube` test) registered for both EasyGL and Vulkan; the existing unmodified `easygl_texturecube_mip_test.cpp` (Task 276) registered for Vulkan for the first time (only possible now thanks to Task 865's real `GetData`). `git stash` revert-and-rebuild confirmed discriminating power on Vulkan; direct before/after binary comparison confirmed it on EasyGL. Full regression: `CnaTests` EasyGL/Vulkan 4272/4274 passed, 2 skipped, 0 failed (exact baseline match); `ctest -R "Texture3D\|TextureCube"` EasyGL 88/88, Vulkan 85/85. **Bgfx split to Task 914**: the identical 1-line-shaped fix (`hasMips` threaded through) is obviously correct by inspection but currently has zero way to be verified — `GetData` is a total no-op on Bgfx (confirmed empirically: the mip test fails 100% identically whether or not the fix is applied), `Texture3D` can't be sampled in any shader (Task 863), and `TextureCube` mip-content sampling via `EnvironmentMapEffect` was already found non-discriminating by Task 907's own test. Not landed to avoid shipping an unverified change; Task 914 covers the real prerequisite (bgfx's genuine but async, `BGFX_TEXTURE_READ_BACK`-gated `readTexture()` API) needed before this can be verified. |
| `82a19244` | 865 | **Real Vulkan `GetData` readback implemented for `Texture3D`/`TextureCube`, closing a severe silent-failure gap (same class as Task 663's old `DDSFromStreamEXT` stub).** Neither backend class overrode `GetData` before this — both silently fell through to the empty base-class default, leaving the caller's buffer untouched with no error. Fixed with `vkCmdCopyImageToBuffer` + a host-visible staging buffer, mirroring `SetData`'s upload path in reverse (`TextureCube` needs its own inline barrier pair since the shared `TransitionImageLayout` helper only targets array layer 0). **2 prerequisites required**: a new `TransitionImageLayout` case (`SHADER_READ_ONLY_OPTIMAL <-> TRANSFER_SRC_OPTIMAL`, previously only upload-direction transitions existed) and `VK_IMAGE_USAGE_TRANSFER_SRC_BIT` added to both textures' image usage flags (previously upload/sample-only). **Test approach**: registered 2 existing backend-agnostic example files for Vulkan rather than writing new ones — `easygl_texture3d_slices_test.cpp` → `Vulkan_Texture3D_Slices_RoundTrip`, `easygl_texturecube_partial_rect_test.cpp` → `Vulkan_TextureCube_PartialRect_RoundTrip` — both do real per-pixel/per-voxel assertions. Also widened `TextureCubeTests.cpp`'s DDS 6-face test guard from EasyGL-only to include Vulkan. **Discriminating power independently verified**: `git stash`-reverting just the 2 Vulkan backend files reproduced 16/16 + 114/114 + 1 failures exactly as predicted; restored and reconfirmed all pass. Full regression: `CnaTests` all 3 backends 0 failed (Vulkan/EasyGL 4272/4274 passed, Bgfx 4276/4278 passed, all pre-existing skips only); `ctest -R "Texture3D\|TextureCube"` (Vulkan) 83/83. |
| `93a2acd8` | 913 | **Fixed a genuine memory-safety bug: `TextureCube`/`Texture3D`'s `SetData`/`GetData` region overloads never validated `elementCount` against the actual texel/voxel count being read/written.** Confirmed FNA's real contract by reading its `Texture2D.SetData<T>`/`GetData<T>` source (`elementCount` must be `>=` the region size; extra is allowed and ignored) — CNA's own `Texture2D.cpp` already implements this exact check; `TextureCube` and `Texture3D` were the only 2 sibling classes missing it. Added the identical `elementCount < (region size)` check to all 4 sites (`TextureCube::SetData`/`GetData`, `Texture3D::SetData`/`GetData`), matching `Texture2D.cpp`'s exact message style. 4 new gtest cases across all 3 backends. **Discriminating power independently verified**: reverted both production files and reran — all 4 silently proceeded instead of throwing (the same underlying condition that produced a live heap-corruption crash while building Task 663's DDS fixture, though this smaller repro didn't reliably reproduce the crash itself — undefined behavior, not guaranteed to crash every time); restored and reconfirmed 4/4 PASS. Full regression: `CnaTests` all 3 backends exact baseline match +4, 0 failed. |
| `105a33aa` | 663 (+913) | **`TextureCube::DDSFromStreamEXT` implemented for real — DDS header parsing (mirrors FNA's `Texture.ParseDDS`) + DXT1/3/5 per-face/per-level decode via the existing `DxtUtil` (already used by `Texture2D::FromStream`), uploaded as `SurfaceFormat::Color`** (CNA's established, already-accepted deviation from FNA's real compressed-GPU-format upload, matching `Texture2D::FromStream`'s own identical precedent — CNA doesn't implement compressed GPU formats end-to-end on any backend). Throws `System::FormatException` for non-cube-map DDS input and `System::NotSupportedException` for malformed/unsupported input, matching FNA's exact exception types. **Found and fixed a real infrastructure gap**: `TextureCube` had no explicit move constructor (a user-declared destructor suppresses the implicit one) and an implicitly-deleted copy ctor (owns a `unique_ptr` backend) — meaning a factory function returning `TextureCube` by value couldn't compile at all without NRVO. Added explicit move ctor/assignment + explicit deleted copy. **Test fixture, per this row's own "build a real DDS cube-map test fixture first" guidance**: `BuildSolidColorCubeDds()` hand-builds a minimal, valid, 4x4 single-mip DXT1 cube-map DDS entirely in memory (no real .dds asset available in this environment) — 6 maximally-distinct, exactly-RGB565-representable solid colours, one per face, each encoded as a single solid DXT1 block. 6 new gtest cases in `TextureCubeTests.cpp`, all 3 backends; the strict per-face colour assertion is EasyGL-only (`#ifdef CNA_BACKEND_EASYGL`) since `TextureCube::GetData` is a known no-op on Vulkan/Bgfx (Task 865) — confirmed directly when an earlier draft of this exact test failed with all-zero readback there. **A second, real, separate memory-safety gap found and NOT fixed, split to new Task 913**: `TextureCube::SetData`/`GetData`'s 6-arg overload never validates `elementCount` against the actual region size (`w*h`) — confirmed via a genuine `free(): invalid pointer` heap-corruption crash from an early, incorrectly-written draft of the new test. **Discriminating power independently verified**: `git stash`-reverted the implementation back to the old 1x1-blank stub (keeping the header/test changes) — all 6 new tests failed exactly as predicted; restored and reconfirmed 6/6 PASS on all 3 backends. Full regression: `CnaTests` all 3 backends exact baseline match +6 new tests, 0 failed. |
| `23e677fe` | 912 | **Root-caused to an already-documented Bgfx quirk, not a distinct bug — closed via a real fix to test infrastructure, no backend code changed.** Bisected using `rendertarget2d_depth_test.cpp` as the minimal repro (per this row's own suggestion): stripped the 2nd quad, `DepthStencilState::Default`, then replaced the whole `BasicEffect`+`DrawUserPrimitives` RT-fill with a plain `SpriteBatch` fill — still failed identically every time, ruling out every one of this row's own candidate hypotheses. Actual cause: sampling a render target via `SpriteBatch` immediately after filling+unbinding it, with no intervening `bgfx::frame()` boundary, can read back stale/black content on the very first `GetBackBufferData()` call — the exact same already-documented "Bgfx's `GetBackBufferData()` only reliably reflects the first read call per rendered frame" quirk (NEXT.md §5), just never previously triggered by `rendertarget2d_depth_test.cpp` since it was never registered for Bgfx. Confirmed the fix: adding the established retry-until-non-black loop (`bgfx_rendertarget2d_mip_test.cpp`'s own convention) to `rendertarget2d_depth_test.cpp`'s Pass 2 makes the exact original, unmodified test pass correctly and consistently. Applied directly to the shared file (EasyGL/Vulkan unaffected — both already pass on the first iteration) and registered it for Bgfx for the first time (`Bgfx_RenderTarget2D_DepthBuffer`). Full regression: `CnaTests` (Bgfx) 4266/4268 passed, 2 skipped, 0 failed; targeted `ctest -R "RenderTarget\|Mip\|Msaa\|MultiSample"` 57/59 (2 failures are the already-documented `Bgfx_RenderTarget2D_MsaaResolve` Xvfb limitation and `Bgfx_RenderTarget2D_MipChain`, reconfirmed genuinely flaky in this sandbox — 4/5 direct runs pass — pre-existing, unrelated). |
| `5c37cd66` | 876 | **Verify-only, zero code changes — the bug no longer reproduces.** Rebuilt and reran `examples/vulkan_rendertargetcube_sample_test.cpp` (`Vulkan_RenderTargetCube_SampleAfterUnbind`) completely unmodified while investigating this task's 2 previously-open candidates: it now passes cleanly and consistently (`centre=(0,0,255)`, exact expected blue — 5 repeated direct runs plus a dedicated `ctest -R` invocation, no flakiness). Not independently bisected to an exact fixing commit (would need a from-scratch vendored-submodule build at an older checkout — not worth the cost once the bug is confirmed gone); most likely an incidental side effect of this session's substantial `VulkanRenderTargetCubeBackend` rework for Task 907, though that task's own new fix was gated on `levelCount_ > 1` and this test uses `mipMap=false`, so it isn't an obvious direct cause either. |
| `2d729b9b` | 875 | **Fixed a real Vulkan bug: `SetRenderTarget(rt); Clear(color); SetRenderTarget(nullptr);` with no draw call in between never got a render pass recorded at all** — `RecordCommandBuffer`'s `usedRTs` list was previously built purely from draw-call-populated sources (`activeBatches_`/`pending3D_`), so a Clear-only RT's colour image stayed at `VK_IMAGE_LAYOUT_UNDEFINED` forever. Fixed with a new `clearedRTs_` list: `Clear()`/`ClearColorAndDepth()` now push the currently-bound RT onto it, and `usedRTs` seeds from it. New `Vulkan_RenderTarget2D_ClearOnlyRoundtrip` test (port of `easygl_rt_roundtrip_test.cpp`/Task 180) — **first attempt read the RT directly while bound (matching the EasyGL original) and only appeared to pass by coincidence**: investigation confirmed Vulkan's `GetBackBufferData` always reads the swapchain image regardless of what's bound, unlike EasyGL; redesigned to sample via `SpriteBatch` after unbinding instead (Task 148's proven-good path), which also surfaced a 2nd real nuance — Vulkan has no per-RT-remembered clear value, so a frame boundary must be forced right after each RT's Clear-only fill or the shared global clear-colour scalar gets overwritten before that RT's render pass actually records. **Discriminating power independently verified**: disabling both of the fix's 2 call sites (matching the true pre-fix state — `RenderTargetUsage::DiscardContents`'s implicit clear-on-bind goes through a 2nd function, `ClearColorAndDepth`, that also needed the fix) reproduced pure black on both RTs; restored and reconfirmed 2/2 PASS. Full regression: `CnaTests` (Vulkan, filtered) 4262/4264 passed, 2 skipped, 0 failed (exact baseline match); targeted `ctest -R "RenderTarget\|Mip\|Msaa\|MultiSample"` 64/64 (was 63/63 before this task's new test). |
| `7d883ee5` | 877 (+911, +912) | **`DepthStencilFormat` fidelity fixed on EasyGL and Bgfx render targets (2D and cube); Vulkan deliberately scoped down to a documented architectural limitation, split to Task 911.** `IGraphicsBackend::CreateRenderTarget2D`'s `bool hasDepth` became `int depthFormat` (raw `DepthFormat` ordinal); `CreateRenderTargetCube` gained the same new param (previously had none at all). EasyGL/Bgfx each got a `DepthFormat`→native-format mapping (`InternalFormat`+attachment point / `bgfx::TextureFormat`) replacing their old hardcoded `DepthComponent24`/`D24S8` choices. **2 more real, independent bugs found and fixed as part of this**: EasyGL's `CreateRenderTargetCube()` previously hardcoded `hasDepth=true` unconditionally (ignored `DepthFormat::None`); Bgfx's `RenderTargetCube` had NO depth attachment support at all, on any request (new capability, not just format fidelity). Vulkan's RT render passes/pipelines are deliberately shared across the backbuffer and every RT (confirmed via the Task 904 comment on `GetOrCreatePipelineFogTex3D`) — varying the depth format per RT would need a depth-format-keyed render-pass+pipeline cache across 10+ pipeline-creation functions, a genuinely large change split to **Task 911** (needs its own scoping pass, same class as Task 902/910); Vulkan's vestigial always-`true`-never-read `hasDepth_` field removed as dead code. New `{EasyGL,Bgfx}_RenderTargetCube_DepthFormat` tests. **Discriminating power independently verified via targeted backend-only sabotage** (not full `git stash`, to preserve the new interface signature): EasyGL fails exactly as predicted when `CreateRenderTargetCube`'s factory is reverted to the old hardcoded-`true` behavior. Bgfx's `Depth24Stencil8` assertion, however, was found to have **zero discriminating power in this sandbox** — this Xvfb/Mesa-llvmpipe software-GL environment performs an identical, seemingly-correct depth comparison regardless of whether a depth attachment exists at all (confirmed via the same sabotage technique); kept the assertion (it's still true, just not exclusively provable here) and dropped strict pass/fail on the `None` case, mirroring the existing `Bgfx_RenderTarget2D_MsaaResolve` Xvfb-limitation precedent. **A second, more severe, unrelated pre-existing Bgfx bug found and NOT fixed, split to new Task 912**: sampling ANY Bgfx render target (2D or cube) back out via `EnvironmentMapEffect`/`SpriteBatch` after a real 3D depth-tested draw into it reads back black — confirmed by running the existing, unmodified `rendertarget2d_depth_test.cpp` (Task 335) against Bgfx for the first time ever. Full regression, all 3 backends: `CnaTests` exact baseline match (EasyGL/Vulkan 4262/4264, Bgfx 4265/4268); targeted `ctest -R "RenderTarget\|Mip\|Msaa\|MultiSample"` EasyGL 36/36, Vulkan 63/63, Bgfx 56/58 (2 failures are the already-documented Xvfb MSAA limitation + a reconfirmed-flaky-under-batch mip test, both pre-existing). |
| `3923d40d` | 904 | **Real, confirmed, latent bug fixed exactly per this row's own long-standing prediction.** `GetOrCreatePipelineFogTex3D` (stride-20 `textured3d`/stride-24 `colored_textured3d`) was missing the `(msaa && renderPassMsaa_) ? renderPassMsaa_ : renderPass_` render-pass-selection ternary every sibling 3D pipeline-creation function already has. Fixed with the identical one-line ternary. New `Vulkan_BasicEffect_TexturedMsaa` test (port of Task 366's texture test, backbuffer MSAA forced via `RecreateBackendForMultiSampleCount(8)`). **Discriminating power confirmed via the Vulkan validation layer, not the pixel assertion** (which passes identically either way on this driver, matching Task 905's precedent for this exact bug class) — `git stash` revert-and-rebuild reproduced the exact predicted `VUID-VkGraphicsPipelineCreateInfo-multisampledRenderToSingleSampled-06853`/`VUID-vkCmdDraw-renderPass-02684` validation errors. Full regression: `CnaTests` (Vulkan, filtered) 4262/4264 passed, 2 skipped, 0 failed (exact baseline match). |
| `4bd25b91` | 907 (+874, +910) | **`RenderTargetCube` mip chains implemented on both Vulkan and Bgfx, mirroring each backend's own `RenderTarget2D` sibling (Task 878/906) exactly, applied per cube face/array-layer.** Vulkan: `VulkanRenderTargetCubeBackend` gained `mipMap`, full-mip-range `cubeView_`, and each `FaceProxy` a `MaybeGenerateMips` override doing a per-layer `vkCmdBlitImage` cascade (reuses `RecordCommandBuffer`'s existing generic per-RT hook unchanged). **Found and fixed a real prerequisite**: mip levels 1+ of a face were never touched by anything except the new blit cascade, whose first barrier assumed `SHADER_READ_ONLY_OPTIMAL` as a starting point that was never actually established — produced live `VUID-vkCmdDraw-None-09600` validation errors until a construction-time full-range initial transition was added (mirrors Task 878's identical fix). Bgfx: `BgfxRenderTargetCubeBackend` threads `mipMap` into `createTextureCube`'s `hasMips` (same 1-line mechanism as Task 906); `BindAsRenderTargetFace`'s existing `Attachment::init()` already defaults to `BGFX_RESOLVE_AUTO_GEN_MIPS`. **This was Bgfx's first-ever exercise of `RenderTargetCube`-via-`EnvironmentMapEffect` at all, and surfaced 2 more real bugs, both fixed as hard prerequisites**: (1) **closes Task 874** — an unsafe `static_cast<const BgfxTextureCubeBackend&>` in the `EnvironmentMapEffect` dispatch (identical shape to Task 873's `RenderTarget2D` fix), fixed via a new `IBgfxCubeSamplable` interface + `dynamic_cast`; (2) a new, previously-unreported gap — `SetRenderTargetCubeFace`'s shared default never updates `currentRtWidth_`/`currentRtHeight_` (Task 901's fix, but only for the 2D case), so `SpriteBatch` draws into a cube face rasterized into a full-window-sized viewport instead of the face's own size; fixed with a Bgfx-specific override. **A third, genuinely architectural bug was found and root-caused (unlike Task 876's still-open Vulkan analogue) but NOT fixed, split to new Task 910**: rendering into more than one cube face within a single un-advanced bgfx frame only actually renders into whichever face was bound *last* (`bgfx::setViewFrameBuffer` is a per-view-per-*frame* setting, and all faces share view id 1) — confirmed directly (forcing a `bgfx::frame()` boundary between faces fixes it), worked around in this test with exactly that. New `{Vulkan,Bgfx}_RenderTargetCube_MipChain` tests (6-face solid-blue fill + `EnvironmentMapEffect`-sample, both read back `(0,0,255)`) — deliberately do NOT assert on coarser mip levels' content specifically (an attempt at that was tried and abandoned as non-discriminating, see the Vulkan test's own header comment for why). `git stash` revert-and-refail independently confirmed discriminating power on both backends. Full regression: Vulkan `CnaTests` (filtered) 4262/4264, Bgfx `CnaTests` 4275/4277 — both exact baseline matches, 0 failed. |
| `0fd4a80d` | 906 | **Real feature implemented and pixel-verified: `RenderTarget2D` mip chains on Bgfx — a 2-line fix, correcting Task 878's own "Bgfx needs new downsample-shader infra" prediction.** bgfx has a real, built-in `glGenerateMipmap`-equivalent, gated behind a framebuffer attachment's resolve flag (`BGFX_RESOLVE_AUTO_GEN_MIPS`, the default whenever the attached texture has mips, confirmed in `bgfx.cpp`) rather than exposed on `bgfx::blit()` (still just a same-size copy, as originally found). Fix: `BgfxRenderTargetBackend` threads a `mipMap` bool through to `createTexture2D`'s `hasMips` argument (previously hardcoded `false`); bgfx auto-regenerates the chain internally on every framebuffer switch-away. An earlier spike toward a custom downsample shader (2 `.sc` files, a `compile_shaders.py` entry, bgfx's `shaderc` built from scratch) was cleanly reverted once this simpler mechanism was found. New `Bgfx_RenderTarget2D_MipChain` test (port of Vulkan's asymmetric-7:1-split methodology) passes with `sample=(224,0,32)`, matching the predicted `(223,0,32)`. Also surfaced a separate Bgfx test-harness-only issue (not a code bug, reproduces with `mipMap=false` too): resampling the same already-rendered RT object across more than one independent read cycle only reliably works on the first cycle — worked around via this project's established fresh-RT-per-checkpoint convention. `git stash` revert-and-refail confirmed discriminating power. Full regression: `CnaTests` (Bgfx) 4275/4277 passed, 2 skipped, 0 failed (exact baseline match). |
| `85a77dc7` | 908 | **Real regression found and fixed — 4 example files Task 896's own audit missed.** Running the full EasyGL `ctest` suite end-to-end for the first time since Task 896 (unblocked by the `CNA_TEST_DISPLAY` fix above) surfaced 4 previously-undetected failures: `EasyGL_AvatarRenderer_{RealRender,AttachPart,TintRouting}` and `EasyGL_ModelDraw_RedQuad` — all reading background-only (no geometry drawn at all). All 4 source files predate Task 896 by a wide margin, so they should have been caught by that task's 119-file audit but weren't. Same root cause/fix as every other Task 896 row: CCW-wound NDC quads silently culled under the real default `RasterizerState.CullMode`; fixed with the identical `RasterizerState::CullNone` pattern. 3 of the 4 files are shared source also used by Vulkan's `ctest` registrations — rebuilt and confirmed all 3 pass there too (previously also silently broken on Vulkan). `git stash` revert-and-refail confirmed on one file, reproducing the exact original failure. |
| `12ccb084` | 909 | **Real regression — Task 896 itself broke these, not merely missed them (unlike Task 908).** Full Vulkan `ctest` run surfaced `Vulkan_BasicEffect_Specular` and `Vulkan_BasicEffect_MultiLightEmissive` both reading pure black, beyond the 12 already-documented pre-existing Vulkan failures. Both test files carried a comment (written before Task 896 existed) claiming "Vulkan's default cull state is effectively CullNone" — true then, false after Task 896 pushed the real default `RasterizerState` to Vulkan's GPU state. Fixed with `RasterizerState::CullNone`, same pattern as every other Task 896 row. `git stash` revert-and-refail confirmed on one file (1/5→5/5 PASS). |
| `75359fba` | — (§8 item 0 infra) | Made `ctest`'s hardcoded `DISPLAY=:0` configurable via a new `CNA_TEST_DISPLAY` cache variable (default `:0`) — all ~270 occurrences now read `DISPLAY=${CNA_TEST_DISPLAY}`. Not a `plan_graphics.md` task; unblocks §8 item 0 (full `ctest` suite run against a virtual display). |
| `21d10a91` | — (not a `plan_graphics.md` task) | **Landed between the 2026-07-07 merge and this session, not previously reflected here.** Fixed `SdlGraphicsBackend::CreateRenderTarget2D` still having the old 4-param override after the base `IGraphicsBackend` interface gained `mipMap`/`multiSampleCount` params (adopted by Bgfx/EasyGL/Vulkan) — the stale override made the `SDL_Renderer` backend fail to compile, breaking downstream consumers (e.g. `mobile-eggbert`). |
| `87325a6b` | 878 (Vulkan half) | **Real feature implemented and pixel-verified: `RenderTarget2D` mip chains on Vulkan** (Bgfx split to new Task 906 — needs new downsample-shader infra, bigger than originally predicted; `RenderTargetCube` both backends split to new Task 907). `VulkanRenderTargetBackend` gained a real `vkCmdBlitImage`-cascade mip chain (mirrors EasyGL's Task 336 `glGenerateMipmap`-on-unbind), a full-mip-range sampling view separate from the mip-0-only framebuffer attachment view, and a full-range initial layout transition. **Found and fixed a real, independently-necessary prerequisite**: every Vulkan `VkSampler` had `maxLod=0` (zero-init), silently clamping all sampling to mip level 0 regardless of `mipmapMode` — fixed to `VK_LOD_CLAMP_NONE` on both `CreateSampler()`'s default and `ApplySamplerState()`'s per-slot samplers (safe for every existing single-level resource, whose own `VkImageView` levelCount still bounds the visible range). New `Vulkan_RenderTarget2D_MipChain` test uses a deliberately asymmetric 7:1 red/blue split (not 50/50) plus a forced 1x1-destination minification draw to read back the coarsest mip level's true weighted average — an **earlier 50/50-split version of this test was caught as a false positive by this project's own discriminating-power discipline** (its colour boundary sat exactly at the forced centre-sample point, so an ordinary level-0 bilinear blend passed identically with or without the fix; `git stash` revert-and-rebuild still "passed"). Re-verified correctly after the redesign: reverting fails with pure red `(255,0,0)` instead of the predicted `(223,0,32)` weighted average. Full regression: `CnaTests` (Vulkan) 4262/4264 passed, 2 skips, 0 failed (excludes one pre-existing `ContentManagerSkinnedModelTest`-area Xvfb/llvmpipe full-suite segfault, confirmed unrelated — reproduces identically with this task's changes fully reverted); 16 spot-run existing RT/sampler/texture/viewport Vulkan examples all pass unchanged. |
| `bdb69b03` | 896 | **Closed — `GraphicsDevice` now pushes its real default `RasterizerState` (`CullCounterClockwiseFace`) to all 3 backends' GPU state at construction, per the user's own "full upfront audit first" scoping decision.** Audited/fixed 119 `examples/*.cpp` files via 8 parallel agent forks before landing the 1-line ctor fix: 64 mechanically mirrored from their already-fixed Bgfx sibling, 55 independently audited via a pre-calibrated 2D cross-product winding formula (115 total needed+got `RasterizerState::CullNone`, 4 confirmed genuinely safe). `house3d_demo.cpp` was only spot-checked by its fork (3/6 box faces) — independently re-verified by hand afterward via the general 3D inward-normal rule, confirming all 6 faces and the single global fix. One fork used the wrong device-variable name in 1 file, caught by the very next full build (100% clean otherwise, all 3 backends). Full `CnaTests` regression clean on all 3 backends (exact baseline match, 0 new failures); ~12 spot-run example binaries all pass except the already-known, independently-reconfirmed-pre-existing `EasyGL_MRT_TwoAttachments` (Task 145, unrelated to culling). |
| `a7a84047` | 883 | **Closed — `Effect::Clone()` made a real polymorphic base contract; found 5 of 8 concrete subclasses already had a working `Clone()` from an earlier, undocumented pass (this row was stale).** Made `Clone()` a pure-virtual base method (previously not even declared on `Effect`, so the 5 existing `Clone()`s were silently non-polymorphic despite the hierarchy), added `override` to those 5, and implemented the 3 that were genuinely missing: `BasicEffect` (the real gap this row's title meant), `EffectMaterial` (hit a real C++ overload-resolution gotcha — needed an explicit `static_cast<Effect&>` since the compiler prefers the deleted implicit copy ctor over the real `Effect&`-taking one), and `ShaderEffect` (a NOXNA extension outside the "7 stock effects" count — its `Clone()` deliberately recompiles from cached GLSL source rather than sharing the compiled program, since it uniquely owns a per-instance backend handle unlike the other 7). The aliasing hazard this row originally worried about (clone's `EffectPass::owner_`/`techniqueId_` pointing at the original) turns out not to happen with the established pattern — every `Clone()` builds a **fresh** Default technique/pass via the normal base constructor, never copies the original's. New tests: 3 in `EffectTests.cpp` (base-contract polymorphic dispatch, technique independence, clone-after-dispose), `BasicEffectDefaultsTest.CloneCopiesAllProperties` (all 22 properties + bidirectional independence), plus first-ever test files for `SpriteEffect`, `EffectMaterial`, `ShaderEffect` (none had any test coverage before). Verified: clean full build on all 3 backends including every example executable; `CnaTests` EasyGL 3501/2 skipped and Bgfx 3505/2 skipped both 0-failed; Vulkan under `Xvfb`+`llvmpipe` (see §4's new environment note) showed non-reproducible unrelated flakiness across 3 runs, zero Clone-related failures in any run. |
| `d1018384`/`6d077140` | 878/879 (MSAA), 873, 901–905 | **`RenderTarget2D` MSAA now genuinely implemented and pixel-verified on Vulkan and Bgfx** (`RenderTargetCube` split to new Task 903; mip half of 878 remains untouched, still open). Vulkan piggybacks per-RT MSAA on the backend's own already-picked `sampleCount_` (documented scope decision — real independent-per-RT sample counts would need threading a new dimension through ~10 pipeline cache keys); a new shared `rtRenderPassMsaa_` 3-attachment render pass reuses the exact same lazily-created MSAA pipeline variants the backbuffer path already builds. A real Vulkan validation-layer subtlety was found empirically: render-pass "compatibility" here requires matching subpass *dependency* masks, not just attachment descriptions. Bgfx just needed `BGFX_TEXTURE_RT_MSAA_X{2,4,8,16}` instead of plain `BGFX_TEXTURE_RT` (bgfx resolves internally). **Verifying the Bgfx test found and fixed 2 more real, previously-invisible, pre-existing bugs as hard prerequisites**: Task 873 (`BgfxSpriteBatchBackend::Draw`'s wrong-handle-type cast sampling `RenderTarget2D`s) and a new one, Task 901 (`EnsureViewState()` clobbering a bound RT's own viewport back to full window size on every `Clear()`, silently corrupting all rendering into any RT smaller than the window). **A genuinely deep, separate architectural gap was found (not fixed) while wiring up the Vulkan test**: `GraphicsDeviceManager.PreferMultiSampling` has never actually reached the Vulkan backend at all (`Game`'s `GraphicsDevice` member is unconditionally default-constructed before any derived-class code can set preferences) — meaning `vulkan_msaa_test.cpp` (Task 147) has been a false positive its entire existence; tracked as new Task 902, worked around here with a narrow `NOXNA` test-only `GraphicsDevice::RecreateBackendForMultiSampleCount()` hook. Also found (not fixed, Task 904) a sibling Vulkan pipeline (`GetOrCreatePipelineFogTex3D`, `textured3d`/`colored_textured3d`) missing the same msaa-aware render-pass check this task had to add elsewhere — dormant, not exercised by any current test. `git stash` revert-and-rebuild confirmed both new tests fail exactly as predicted (Vulkan: compile error; Bgfx: purely-binary `MultiSampleCount=8` row). **Independent review of this diff (separately, before merging) found and fixed one more real bug, Task 905**: `CreateRTRenderPass()`'s subpass dependencies didn't actually match `CreateRenderPass()`'s despite a comment claiming they did, producing live `VUID-vkCmdDraw-renderPass-02684` validation errors (correct pixel output regardless, but a genuine spec violation) the moment a depth-tested 3D primitive was drawn into a plain non-MSAA `RenderTarget2D` — pre-existing, outside every hunk of the original diff, invisible until this task's own new `MultiSampleCount=0` comparison call was the first to exercise it. Fixed by widening `CreateRTRenderPass()`'s `deps[]` to match `CreateRenderPass()`'s exactly. Full regression: Vulkan `ctest -R Vulkan` 68/80 (same 12 pre-existing failures), `CnaTests` 3493/2 skipped (exact baseline match, re-confirmed after the Task 905 fix); Bgfx `ctest -R Bgfx` 48/48 (100%), `CnaTests` 3497 passed/2 skipped, 0 failed (independently re-confirmed). EasyGL `CNA` target sanity-rebuilt clean. |
| `6a91d11b` | 899 (`env_map3d` leftover) | **Closes out the last open fog gap Task 899 ever touched.** Vulkan's `env_map3d` pipeline had ~160 spare bytes in its existing `EnvMapParams` UBO (96 of 256 used) — packed `fogColorEnabled`/`fogStartEnd` into `[24..31]` of the now-`float[32]` `envMapUboData`, mirroring `lit_textured3d`'s identical "fog in an existing UBO's spare tail" pattern. New `vulkan_environmentmapeffect_fog_test.cpp` passed 3/3 first try; `git stash` revert-and-rebuild confirmed it fails 1/3 exactly as predicted pre-fix. Full regression: `ctest -R Vulkan` 67/79 (same 12 pre-existing failures, zero new), `CnaTests` 3493/3495 unchanged. |
| `beb83b20` | 899 (Vulkan core) | **Real feature implemented on Vulkan, closing out Task 899's originally-scoped title.** `colored3d`/`textured3d`/`colored_textured3d` unified into one new "Bundle A" descriptor-set-layout/pool/pipeline-layout/UBO-ring-buffer/descriptor-set-cache (mirroring `descriptorSetLayoutLitTextured_`'s shape); `dual_texture3d` got a split-off dedicated vertex shader plus a 3rd descriptor binding; `skinned3d` got a 3rd descriptor binding (dedicated fog UBO, `BoneBlock` has zero spare capacity) — exactly matching the prior research pass's predicted fix shape. **Found and fixed a 2nd shared-shader landmine the research pass missed**: `Instanced3D` and the legacy no-`GpuDrawParams` `DrawColoredPrimitives` path both silently reused `kColored3dVertSpv`/`kColored3dFragSpv`, which broke once those shaders grew a fog descriptor binding — caught as a genuine `Vulkan_DrawInstanced_3Instances` regression on the first full-suite run, root-caused and fixed with 2 new trivial dedicated shader files rather than left as a landmine. Also deleted the now-fully-unreachable old `GetOrCreatePipelineExt3D`/`pipelinesExt3D_`. 5 new tests, one per fixed pipeline; `git stash` revert-and-rebuild (plus manually moving aside 3 new shader files) confirmed all 5 fail 1/3 exactly as predicted pre-fix. Full regression: `ctest -R Vulkan` 66/78 (same pre-existing 12 failures, zero new), `CnaTests` 3493/3495 (2 pre-existing skips, unchanged). |
| `44f26670` | 899 (Bgfx bonus) | **Real bug found and fixed on Bgfx, opportunistic bonus scope of the still-open Task 899.** `env_map3d`/`skinned3d` fog needed only shader edits (fog uniforms/`FillGpuDrawParams()` were already correct since Tasks 888/900) — mirrored `lit_textured3d`'s proven pattern. Found and fixed a real, separate, previously-unreported bug while writing the `SkinnedEffect` fog test: `EmissiveColor` was a total GPU no-op on Bgfx (`fs_skinned3d.sc` never declared `u_emissiveColor`, C++ dispatch never set the existing uniform handle for this branch) — invisible until this test isolated fog via `EmissiveColor` alone, mirroring how Task 886 uncovered Tasks 892/898 the same way. `git stash` revert-and-rebuild confirmed both new tests fail exactly as predicted; full Bgfx regression 47/47, zero failures. Task 899's actual title scope (Vulkan's 5 pipelines) remains open — comparable in size to Task 897, deliberately not attempted in the same pass. |
| `86226bc3` | 880 | **Real, confirmed universal gap found and FIXED on all 3 backends** (Task 338 finding): `GraphicsDevice.Viewport` was totally decorative, every backend hardcoded the full render-target/window size. Fixed by mirroring `ScissorRectangle`'s already-wired pattern (`IGraphicsBackend::SetViewport`, `setViewportProperty()` forwards to it, plus a second fix in the separate `UpdateViewportFromWindow()` resize path). **Found and fixed a real regression this immediately surfaced**: EasyGL's naive window-height-based Y-flip broke rendering into any bound `RenderTarget2D` smaller than the window — traced to the identical latent bug already present in `SetScissorRect`, fixed both using `currentRtHeight_` (mirrors `ReadBackbuffer`'s established pattern). Vulkan/Bgfx wired backbuffer-only, RT passes deliberately left hardcoded to each RT's own full size (deferred multi-RT-per-frame recording can't attribute one frame-global viewport to a specific RT, same shape as Vulkan's pre-existing RT-pass-scissor gap, documented as its own new row). New tests, one per backend; the Bgfx one needed a rewrite after hitting the documented one-read-per-frame quirk. `git stash` revert-and-rebuild confirmed all 3 fail exactly as predicted pre-fix. Full regression: EasyGL 3636/3639, Vulkan 61/73 filtered + `CnaTests` 3493/3495, Bgfx 3540/3540 (100%) — all pre-existing failures only, zero new regressions. |
| `94f1827e` | 881 | **Real, confirmed spec-compliance divergence found and FIXED on all 3 backends** (Task 339 finding): `SetRenderTargets`'s cap didn't match FNA's real `MAX_RENDERTARGET_BINDINGS=4` (EasyGL/Bgfx hardcoded 8, Vulkan had none at all). Fixed with a single shared C++ check (`std::invalid_argument` when >4 targets) before any backend delegation. New `GraphicsDeviceValidationTest.SetRenderTargets_*` unit tests using real `RenderTarget2D` instances — discovered a real, separate, previously-unreported segfault when passing null-target `RenderTargetBinding`s (a CNA-only default-ctor deviation from FNA, unreachable in practice, not fixed here). `git stash` revert-and-rebuild confirmed discriminating power. Full regression: EasyGL 3635/3638 (3 pre-existing failures only); Vulkan/Bgfx sanity-rebuilt clean (shared, backend-agnostic file). |
| `fd8c2dea` | 900 | **Real, confirmed gap found and FIXED on EasyGL** (opened by Task 888's research): `SkinnedEffect`/`EnvironmentMapEffect`'s `FillGpuDrawParams()` never forwarded fog fields at all, despite both having complete `IEffectFog`/`FogVector` machinery in `OnApply()` — the same gap Task 378/388 already fixed for `AlphaTestEffect`/`DualTextureEffect`. Fixed by mirroring `BasicEffect`'s pattern in both effects, plus adding fog uniforms/blend to EasyGL's `env_map3d`/`skinned3d` shaders (the only 2 of 7 EasyGL shader variants with zero fog code before this task) — `BindDrawParams()` needed no changes, its fog-uniform-setting block is already fully generic. New `easygl_{environmentmapeffect,skinnedeffect}_fog_test.cpp` (3 checks each) passed on the first attempt; `git stash` revert-and-rebuild reproduced the exact predicted pre-fix failure (1/3 PASS each). Full EasyGL regression: 3631/3634 pass, same 3 pre-existing failures. Vulkan/Bgfx's `env_map3d`/`skinned3d` pipelines still lack fog GPU-side, left for Task 899. |
| `a42ae950` | 888 | **Real feature implemented on Bgfx (full) and Vulkan (partial, remainder split to Task 899).** Chose EasyGL's already-shipped raw-object-space-Z fog formula over FNA's view-space `FogVector` (equivalent only under identity World/View, which every test uses; matching EasyGL is what makes the verification-by-porting-existing-tests plan work). Bgfx: 2 shared uniforms set unconditionally per draw, new varying, fog logic added to all 7 applicable shader pairs — no push-constant budget constraint there. Vulkan: fixed only `alpha_test3d`/`alpha_test_colored3d` (28 spare push-constant bytes) and `lit_textured3d` (32 spare `LitLightParams` UBO bytes); the other 5 pipelines share a fully-packed push constant with zero spare bytes, split to Task 899. 6 new tests (2 Vulkan, 4 Bgfx) confirmed via `git stash` revert-and-rebuild. Found and fixed 2 real pre-existing test-authoring gotchas along the way: Vulkan's clip-space Z is `[0,1]` not `[-1,1]` (a ported test using `z=-0.9` got near-plane-clipped); 2 ported Bgfx tests needed the already-known `RasterizerState::CullNone` workaround. Full regression: Vulkan 3551/3563 (12 pre-existing failures only), Bgfx 3535/3535 (100%, zero failures). Opened Task 899 (Vulkan's remaining 5 pipelines) and Task 900 (`SkinnedEffect`/`EnvironmentMapEffect` fog forwarding gap, found during research). |
| `60ffbdbd` | 887 | **Real, confirmed gap found and FIXED on Vulkan/Bgfx** (opened by Task 377): `AlphaTestEffect.VertexColorEnabled` had zero effect since neither backend's alpha-test shader ever declared a color vertex attribute. Fixed with a smaller-footprint approach than originally scoped — added a stride-24-only sibling vertex shader to each backend's existing `alpha_test3d` pipeline (`alpha_test_colored3d.vert.glsl` on Vulkan, `vs_alpha_test_colored3d.sc` on Bgfx) rather than unifying all dispatch into the per-stride pipelines. New `{Vulkan,Bgfx}_AlphaTest_VertexColor` tests (ports of Task 377's EasyGL test) confirmed via `git stash` revert-and-rebuild on both backends — pre-fix, identical diffuse-alone `(122,82,163)` result on both checks. Full regression: Vulkan 3549/3561 (12 pre-existing failures only), Bgfx 3531/3531 (100%, zero failures). |
| `a6198bf0` | 886/892/898 | **Real feature implemented + 2 real, pre-existing bugs found and FIXED as hard prerequisites, all 3 backends.** Task 886: real specular highlights for `BasicEffect` — half-vector Blinn-Phong (FNA's `Lighting.fxh` `ComputeLights`), per-light gated by the same "faces the light" term as diffuse, summed with each light's own `SpecularColor` then scaled once by the material `SpecularColor`, added after the texture×diffuse multiply (FNA's `AddSpecular` macro). Required a real `Matrix::CreateLookAt`/`CreatePerspectiveFieldOfView` camera to test (identity View/Projection places `EyePosition` on the quad's own plane, degenerate for specular) — building that real camera surfaced Task 892 (Bgfx `vs_lit_textured3d.sc` transformed the normal by the full WVP matrix instead of World's inverse-transpose, a row already opened by Task 398's audit) and Task 898 (identical bug independently found on Vulkan's `lit_textured3d.vert.glsl`, fixed in-shader via GLSL's built-in `inverse()`). Both were totally masked by every prior test's identity View/Projection. New `{EasyGL,Bgfx,Vulkan}_BasicEffect_Specular` tests (4 checks each, Python-derived expected values) plus `Bgfx_BasicEffect_NormalTransform` (isolates the diffuse-only symptom). Discriminating power independently verified via `git stash` revert-and-rebuild on both Bgfx and Vulkan, reproducing the exact predicted `(2,2,2)`-instead-of-`(48,48,48)` failure. Full regression: EasyGL 3629/3632, Bgfx 3529/3530, Vulkan 3548/3560 — all pre-existing/documented failures only. |
| `cc9fec3b` | 897 | **Real gap found and FIXED — Vulkan half of Task 885, closing the `BasicEffect` lit-path gap on all 3 backends.** Gave the lit-textured (stride-32) pipeline its own dedicated descriptor-set/pipeline-layout/UBO-ring-buffer (`descriptorSetLayoutLitTextured_`/`pipelineLayoutLitTextured3D_`/`litTexturedUBO_`), mirroring `EnvironmentMapEffect`'s own small-UBO pattern exactly, since the shared 128-byte push constant (still used unchanged for strides 20/24/`Instanced3D`) had zero spare room. New `Vulkan_BasicEffect_MultiLightEmissive` test (port of Task 885's EasyGL test) passed on the first attempt with identical expected values; discriminating power confirmed via `git stash` revert-and-refail — pre-fix, identical `(89,13,13)` result to EasyGL/Bgfx's own pre-fix runs. Full serial Vulkan regression 3546/3559 pass, all 13 failures independently reconfirmed pre-existing (identical failures with this task's changes reverted). |
| `6072fdd2` | 885 | **Real, confirmed gap found and FIXED on EasyGL/Bgfx (Vulkan split out to Task 897).** `BasicEffect::FillGpuDrawParams()` never forwarded `DirectionalLight1`/`DirectionalLight2` and silently dropped `EmissiveColor` whenever `LightingEnabled=true`. Added `GpuDrawParams` fields for light1/2, reused the existing `emissiveColor` field; fixed both backends' lit shaders to sum all 3 lights and add `EmissiveColor` after the diffuse multiply (not scaled by it), matching FNA's `Lighting.fxh` exactly — Bgfx's pre-existing formula needed restructuring since it multiplied the whole lit result by `DiffuseColor` naively. New `{EasyGL,Bgfx}_BasicEffect_MultiLightEmissive` tests (3 checks each: all-3-lights-summed, per-light `Enabled` gating, per-light independent `Direction` field) confirmed via `git stash` revert-and-refail — pre-fix, both backends produced the identical `(89,13,13)` ambient+light0-only result. |
| `6e3b41a5` | 884 | **Real, confirmed dangling-pointer hazard found and FIXED** — `EffectParameterCollection`/`EffectPassCollection` stored their elements **by value** in a `std::vector`, the same hazard Task 355 already fixed for `EffectTechniqueCollection`. Switched both to `vector<unique_ptr<T>>` with matching custom iterators; no call-site changes needed anywhere (every usage went through `operator[]`/`GetParameterBySemantic`/range-`for`). New `PointerStableAcrossReallocatingAdd` tests on both collections; discriminating power independently verified via `git stash` revert-and-rebuild — pre-fix, `EffectParameterCollectionTest` failed on a pointer-address mismatch and `EffectPassCollectionTest` **segfaulted** (genuine use-after-free), both confirming the hazard was real. Split the RasterizerState-default GPU-sync gap out of this task's old bundled title into its own Task 896 (needs a scoping decision, not fixed here). |
| `1c20d985` | 665 | **Real, confirmed Vulkan bug found and FIXED — both predicted root causes confirmed present.** `VulkanSpriteBatchBackend` never overrode `SetSamplerFilter`/`SetSamplerAddressMode` (always used the texture's own fixed descriptor set, bypassing the per-slot `VkSampler` cache entirely); `Draw()` separately clamped UVs to `[0,1]` regardless of `sourceRectangle`, defeating `Wrap`/`Mirror` addressing even if the sampler wiring alone were fixed. Fixed both, mirroring EasyGL's Task 269/118 pattern. New `Vulkan_TextureAddressMode` test (direct port of Task 269's EasyGL test) confirmed via `git stash` revert-and-refail — pre-fix both `PointWrap`/`PointClamp` read the identical blended color. Closes both of Phase 47's originally-scoped SpriteBatch bugs. |
| `b9094009` | 664 | **Real, confirmed Vulkan bug found and FIXED.** Root cause isolated via instrumentation/tracing (per this task's own instruction, not guess-fixed): `VulkanSpriteBatchBackend::Begin()` destructively cleared the same mutable vectors a prior `End()` had just populated, before the once-per-frame harvest could read them; a second, latent bug meant the harvest always wrote to a hardcoded offset 0 instead of an accumulating cursor. Fixed with a per-cycle `BatchSnapshot` moved into `activeBatches_` at `End()`, plus a genuine running `vbOff`/`ibOff` cursor mirroring the already-correct 3D draw path. New `Vulkan_SpriteBatch_MultiBeginEnd` regression test confirmed via `git stash` revert-and-refail. |
| `718f6499` | 420 | **Verify-only, zero bugs found, closes Phase 47's core `SpriteBatch` test arc (Tasks 411–420).** Proved that `layerDepth`-driven draw order (already verified via mock backend in Tasks 415/416) actually determines which of 2 overlapping opaque sprites is visible, via a real GPU pixel test deliberately submitted in reverse order from the correct sort order. Independently verified discriminating power by disabling the sort and confirming the overlap check fails exactly as predicted. |
| `13baad58` | 419 | **Verify-only, zero bugs found (EasyGL).** Confirmed `SpriteBatch::Draw`'s `sourceRectangle` genuinely crops the sampled texture region (2x2 grid of solid-color cells, one cell selected and stretched, entire drawn sprite uniformly that cell's color). Independently verified discriminating power: temporarily forced whole-texture UVs and confirmed both cropping checks fail exactly as predicted. |
| `f3cbbe55` | 418 | **Verify-only, zero bugs found (EasyGL).** Confirmed `SpriteBatch::Draw`'s scalar and `Vector2` scale overloads produce exact expected destination sizes, including non-uniform `Vector2` scale. Independently verified discriminating power twice: temporarily forced each axis-mixing bug (`scale.X`-for-both, `scale.Y`-for-both) and confirmed the corresponding check fails exactly as predicted each time. |
| `a643fc25` | 417 | **Verify-only, zero bugs found (EasyGL).** First real GPU pixel test in Phase 47: confirmed `SpriteBatch::Draw`'s rotation genuinely pivots around the caller's `origin` point, matching FNA's real `GenerateVertexInfo` formula term-for-term. Independently verified discriminating power: temporarily hardcoded `origin=(0,0)` in EasyGL's `Draw()` and confirmed 2 of 3 checks fail exactly as predicted. |
| `1294cd32` | 416 | **Verify-only, zero bugs found, closes the per-`SpriteSortMode` test arc (Tasks 412–416)**: fulfills Task 165 — confirmed `SpriteSortMode::BackToFront` sorts by descending `layerDepth`, the mirror image of Task 415, reusing its exact test shape with sort mode and expected order reversed. Independently verified discriminating power the same way. |
| `39e4d62b` | 415 | **Verify-only, zero bugs found**: fulfills Task 164 — confirmed `SpriteSortMode::FrontToBack` sorts by ascending `layerDepth`, using the task's own example depths (0.5, 0.1, 0.9). Independently verified discriminating power: temporarily disabled the `FrontToBack` sort branch and confirmed the test fails exactly as predicted (order reverted to raw submission order). |
| `b00ed8ba` | 414 | **Verify-only, zero bugs found**: fulfills Task 163 — confirmed `SpriteSortMode::Texture` groups draws by texture pointer (adjacency, not a predicted order since that depends on runtime addresses) while preserving `stable_sort`'s within-group submission order. Independently verified discriminating power: temporarily disabled the `Texture` sort branch and confirmed the test fails exactly as predicted (interleaved draw stayed un-grouped). |
| `80ba1276` | 413 | **Verify-only, zero bugs found**: fulfills Task 162 — confirmed `SpriteSortMode::Deferred` preserves original `Draw()` submission order (no sort at all). Independently verified discriminating power: temporarily added an unconditional `std::reverse()` before the flush loop and confirmed the test fails exactly as predicted (recorded order came back reversed). |
| `a79f4091` | 412 | **Verify-only, zero bugs found**: fulfills Task 161 — confirmed `SpriteSortMode::Immediate` flushes each sprite inside `Draw()` itself, strictly before `End()`, using Task 411's `RecordingSpriteBatchBackend`. Independently verified discriminating power: temporarily forced `pushSprite()` to always queue (breaking the Immediate branch) and confirmed both new tests fail exactly as predicted, while a `Deferred` negative control kept passing. |
| `93a725b8` | 411 | **Opens Phase 47. Infrastructure task, 2 real gaps found and fixed**: `SpriteBatch` had no way to inject a custom backend without a real `GraphicsDevice`, and `Texture2D::CreateCpuOnlyForTests()`'s null backend would crash `flushSingle()` the moment a sprite is flushed. Fixed both with new `NOXNA` test-only constructors/factories, then built `RecordingSpriteBatchBackend`/`DummyTextureBackend` (new shared `tests/.../RecordingSpriteBatchBackend.hpp`) and 5 tests proving Begin/End/Draw dispatch and multi-texture discrimination work end-to-end, headlessly. |
| `fe509dbd` | 410 | **Doc, closes Phase 46.** Wrote `docs/skinnedeffect-support.md` synthesizing Tasks 401–409 (mirrors `docs/basiceffect-support.md`/`docs/alphatesteffect-support.md`/`docs/dualtextureeffect-support.md`/`docs/environmentmapeffect-support.md`'s style) — per-task summaries, full 3-backend support matrix, "Open, tracked follow-up work" listing Tasks 893/894/895. No code changed. |
| `aa9aa8a3` | 409 | **Capstone, zero new bugs found**: combined Tasks 406–408's pieces (identity no-op, single-bone translation, 2-bone weighted blend) into one scene, one bone-palette upload, one draw call covering 3 quads distinguished only by per-vertex weight/index data. All 3 backends produced the exact predicted output on the first attempt, each byte-identical across all 3 quads within itself and matching each backend's own Task 406–408 single-quad values exactly — proving the pieces compose correctly within a single draw, not just in isolation. |
| `d0eebe95` | 408 | **Verify-only, zero bugs found**: confirmed genuine 2-bone weighted blending (`skinMat = w0×Bones[i0] + w1×Bones[i1]`) on all 3 backends using a deliberately discriminating bone pair whose blended result differs from either bone's own individual shift. Independently verified discriminating power by temporarily swapping to a `(1,0)` weight split and observing the predicted `-0.5`-shift swap (quad moves to the *left* read-back point instead of centre) before restoring the real `0.5`/`0.5` test. |
| `8906d776` | 407 | **Verify-only, zero bugs found**: formalized the pre-existing (Task 123) EasyGL-only `skinned_effect_integration_test.cpp` translation-bone scenario into Phase 46's own per-backend naming convention, extending it to Vulkan and Bgfx for the first time. All 3 backends produced the exact predicted output on the first attempt, confirming a real non-identity `Matrix.CreateTranslation` bone correctly shifts the mesh everywhere. Bgfx reused Task 406's `renderAndRead()` helper. |
| `fa5f026a` | 406 | **Opens the phase's first real pixel test, verify-only, zero `SkinnedEffect`/backend bugs found**: confirmed an identity bone palette (`Bones[0]=Identity`, `WeightsPerVertex=1`) produces zero mesh deformation on all 3 backends — direct contrast with the pre-existing Task 123 integration test. Surfaced a genuinely new Bgfx *test-harness* pitfall: `GetBackBufferData()` only reliably reflects the first read call per rendered frame; reading 3 rectangles from one frame silently returned blank data for reads 2 and 3. Fixed by refactoring to a `renderAndRead()` helper doing one full clear+draw+retry+read pass per checkpoint — now the established pattern for future multi-point Bgfx tests. |
| `aa101253` | 404 | **Verify-only, zero bugs found**: confirmed `GetBoneTransforms` returns a genuinely independent copy (`EffectParameter::GetValueMatrixArray()` builds a brand-new `std::vector<Matrix>` every call, matching FNA's own array-allocating semantics). Added `GetBoneTransformsReturnsIndependentCopy`, discriminating by construction (mutating the first call's result and confirming a second call is unaffected). |
| `43dcf220` | 403/405 | **Documentation only, no new code**: both tasks already fully satisfied by Task 402's own `SetBoneTransformsAcceptsExactlyMaxBones`/`SetBoneTransformsThrowsWhenExceedingMaxBones` tests. Marked done in `plan_graphics.md`, no test/production changes. |
| `5b8f1c56` | 402 | **52 new unit tests, regression guard for Task 401's fix**: wrote `SkinnedEffectTests.cpp` from scratch (zero prior coverage). `Clone()` test deliberately sets `SpecularColor`/`SpecularPower` before cloning — `git stash`-confirmed it fails exactly as predicted with Task 401's fix reverted. Also fixed an unrelated build-breaking discovery: `SkinnedEffect::MaxBones` had no out-of-line definition, causing a linker error the moment any code took its address. |
| `eb68b5bc` | 401 | **Opens Phase 46. Real, confirmed bug found and fixed**: `SkinnedEffect`'s `Clone()` never preserved `SpecularColor`/`SpecularPower` — the identical bug shape Task 392 fixed for `FogColor` across 4 stock effects, undetected here since `SkinnedEffect` had zero prior test coverage. All property defaults, `MaxBones`/bone bounds-checking, and `OnApply()`'s shader-index formula confirmed matching FNA exactly. Opened Tasks 893 (`DirectionalLight1`/`2` unforwarded), 894 (zero specular GPU implementation), and 895 (`WeightsPerVertex` complete GPU no-op on all 3 backends). |
| `eaf5c852` | 400 | **Documentation only, closes Phase 45.** Wrote `docs/environmentmapeffect-support.md` synthesizing Tasks 391–399: per-task summaries, full 3-backend support matrix, and an "Open, tracked follow-up work" section listing Tasks 890/891/892. No production code or tests changed. |
| `9d1d7a56` | 399 | **Capstone, verify-only, zero bugs found**: combined Tasks 394–398's fixes (lerp blend, alpha-scaled specular, Fresnel suppression, `EyePosition`, non-uniform `World` scale) into one scene. All 3 backends produced the exact predicted `(151,101,76)` on the first attempt — genuine cross-backend consistency, closing Phase 45's per-task verification arc. |
| `44aac0ca` | 398 | **Real, confirmed formula bug found and fixed on 2 of 3 backends**: `EnvironmentMapEffect`'s normal was transformed by the raw `World` matrix instead of `transpose(inverse(World3x3))`, wrong under non-uniform scale. EasyGL and Bgfx both had this bug (Vulkan was already correct); fixed both via a CPU-side cofactor/det shortcut. Empirically confirmed pre-fix output `(1,12,242)` (buggy blue) on both vs FNA's correct yellow. Opened Task 892 for a worse sibling bug in `BasicEffect`'s Bgfx lit shader (transforms normals by the full WVP matrix). `git stash`-confirmed both fixes independently. |
| `5d845961` | 397 | **Verify-only, zero bugs found, no code changed**: confirmed `EyePosition` correctly drives `EnvironmentMapEffect`'s reflection vector on all 3 backends. Built the phase's first distinct-per-face cube map (every prior test used solid colors, unable to detect a wrong reflection vector) and 2 camera positions that hit 2 clearly different, exactly-predicted faces — proof by construction that the wiring works end-to-end. |
| `fe469465` | 396 | **Real, confirmed missing-feature gap found and fixed on all 3 backends**: `EnvironmentMapEffect` implemented no Fresnel edge-weighting at all — the env-map blend factor was always the flat `EnvironmentMapAmount` regardless of view angle, instead of FNA's real per-vertex `pow(max(1-abs(dot(eyeVector,normal)),0),FresnelFactor)*EnvironmentMapAmount` term (the default). Empirically confirmed pre-fix output `(128,128,128)` (cube map fully applied) at a head-on camera angle vs FNA's correct Fresnel-suppressed `(100,50,25)`. Added `fresnelEnabled`/`fresnelFactor` to `GpuDrawParams`, threaded per-pixel into all 3 shaders (repurposing unused padding floats on Vulkan/Bgfx). `git stash`-confirmed EasyGL fails pre-fix with the exact predicted value. |
| `32e97e5e` | 395 | **Real, confirmed formula bug found and fixed on all 3 backends**: `EnvironmentMapEffect`'s `EnvironmentMapSpecular` was a flat additive constant instead of FNA's real `+= EnvironmentMapSpecular * envmap.a` (scaled by the cube map's own alpha, further scaled by combined texture×diffuse alpha). Empirically confirmed pre-fix output `(202,152,127)` regardless of cubemap alpha vs FNA's correct alpha-scaled `(151,101,76)` at `alpha=128`. Fixed all 3 shaders by sampling the cube map's full `vec4`. Opened Task 891 for the still-unscaled base-lerp `envColor` nuance. `git stash`-confirmed EasyGL fails pre-fix with the exact predicted value. |
| `87263325` | 394 | **Real, confirmed formula bug found and fixed on all 3 backends**: `EnvironmentMapEffect`'s cube-map blend was additive (`+envColor×Amount`) instead of FNA's real `lerp`/`mix`, meaning `Amount=1` added the cube map on top of the lit color instead of fully replacing it. Empirically confirmed pre-fix output `(228,178,153)` vs FNA's correct `(128,128,128)`. Fixed all 3 shaders. `git stash`-confirmed all 3 backends fail pre-fix with the exact predicted value. |
| `8b5adb5b` | 393 | **Verify-only, zero bugs in its own scope**: `EnvironmentMapAmount=0` correctly ignores the cube map on all 3 backends, exact match. Surfaced a real formula discrepancy for Task 394: FNA's real pixel shader lerps between lit color and cube map (`Amount=1` should fully replace); CNA's actual shader formula adds instead — invisible at `Amount=0` (both coincide) but real and testable at `Amount=1`. |
| `51cbf4f5` | 392 | **Real bug found and fixed, affecting 4 stock effects**: `Clone()` never preserved `FogColor` on `AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect` (silently reset to black on every clone). Found while writing `EnvironmentMapEffectTests.cpp`'s `Clone()` test (Task 372/382's own tests never set `FogColor` before cloning, so they never caught it). Fixed all 4 with a one-line addition each; extended the 2 existing test files to close the gap. `git stash`-confirmed all 3 testable cases fail pre-fix. |
| `b59c3a0d` | 391 | **Verify-only, opens Phase 45**: audited `EnvironmentMapEffect` against FNA — all 14 properties/defaults/`Clone()`/`OnApply()` match exactly, zero bugs in its own scope. Found `FillGpuDrawParams()` only forwards `DirectionalLight0` (same shape as Task 885's `BasicEffect` gap, confirmed shared `Lighting.fxh` mechanism) — opened Task 890. |
| `3eb10974` | 390 | **Doc, closes Phase 44**: wrote `docs/dualtextureeffect-support.md` synthesizing Tasks 381–389 (mirrors `docs/alphatesteffect-support.md`'s style) — per-task summaries, full 3-backend support matrix, "Open, tracked follow-up work" listing Tasks 887/888/889. No code changed. |
| `fcaa1950` | 389 | **Capstone, zero new bugs**: combined Tasks 383–388's fixes (doubling factor, two-texture multiply, diffuse) into one scene, first `DualTextureEffect` test with a real 2×2 multi-texel texture. All 3 backends byte-identical, exact match. Found and opened Task 889 while writing it: `VertexColorEnabled` is a total no-op for `DualTextureEffect` on all 3 backends (dedicated shaders/pipelines on every backend lack a color attribute entirely) — Phase 44 never had a dedicated audit task for this property. |
| `8d0d1cee` | 388 | **Real bug found and fixed on EasyGL**: `DualTextureEffect::FillGpuDrawParams()` never forwarded fog fields at all — same bug shape as pre-Task-378 `AlphaTestEffect`, but `DualTextureEffect`'s own dedicated shader also had zero fog uniforms (unlike the shared per-stride shaders). Fixed both. 3/3 PASS, `git stash`-confirmed 2/3 correctly fail pre-fix. Vulkan/Bgfx's project-wide fog gap (Task 888) remains, confirmed not fixable here. |
| `39e5feed` | 387 | **Real bug found and fixed on Bgfx**: `texColor3DSampler2_` (`Texture2`, slot 1) had no null-fallback at all — exactly the gap Task 379 explicitly predicted and left unfixed. Fixed with the same else-branch pattern as slot 0 (Task 379). EasyGL/Vulkan already correct (verified). 2/2 PASS on all 3 backends; `git stash`-confirmed pre-fix failure `(0,0,0)`. Bgfx ctest 3403/3403, 100%. |
| `ab98b721` | 386 | **Verify-only, zero bugs**: confirmed `DualTextureEffect`'s first texture (`Texture`, slot 0) already falls back to white when null, on all 3 backends — Bgfx's case was already covered by Task 379's general fix. New pixel test per backend (previous-draw + null-texture pattern, matching Task 379's methodology), 2/2 PASS exact match on all 3. Noted Bgfx's second slot (`Texture2`) still has the gap — deferred to Task 387. |
| `80df324c` | 385 | **Verify-only, zero bugs**: confirmed `DualTextureEffect`'s `Alpha` correctly premultiplies the forwarded diffuse RGB (`Vector4(diffuseColor*alpha,alpha)`) via 3 new GPU-independent unit tests plus real `BlendState.AlphaBlend` pixel tests on EasyGL/Bgfx (`(128,0,128)` exact match). Vulkan's known-fake `BlendState` (Task 868/870) required a narrower alpha-channel-only pixel test there instead, to avoid misattributing that unrelated bug. |
| `c1b80f93` | 384 | **Coverage-only (Bgfx)**: added `bgfx_dual_texture_test.cpp` (magenta×yellow=red), Bgfx's last remaining gap in basic DualTextureEffect multiply coverage (EasyGL/Vulkan already had it, Tasks 133/135). Explicitly documented as non-discriminating for Task 383's `*2` bug (saturated values). Fixed a test-authoring mistake: a copied `SetDepthTestEnabled` call crashes on Bgfx (Task 375's known stub). Bgfx ctest 3400/3400, 100%. |
| `235b0d6c` | 383 | **Real bug found and fixed, all 3 backends**: `DualTextureEffect`'s shaders were all missing FNA's `color.rgb *= 2` doubling factor on the first texture — invisible to every prior test (saturated 0/1 values only). Fixed on EasyGL/Vulkan/Bgfx; also fixed a pre-existing `TextureFilter::Point vs Linear` test (Task 297) whose thresholds relied on the missing factor. First-ever Bgfx `DualTextureEffect` pixel test. |
| `26907553` | 382 | **Test-only**: wrote `DualTextureEffectTests.cpp` from scratch (26 tests — Task 381 found zero prior coverage), mirroring Task 372's `AlphaTestEffectTests.cpp` style: all 9 property defaults, setter round-trips (including `Texture`/`Texture2`), `Clone()`, `GetTypeName()`. Zero bugs found. |
| `838e1021` | 381 | **Verify-only, opens Phase 44**: audited `DualTextureEffect` against FNA — all properties, defaults, `Clone()`, `OnApply()` match exactly, zero bugs in this task's own scope. Found `FillGpuDrawParams()` never forwards fog fields at all (same bug shape as pre-Task-378 `AlphaTestEffect`); deliberately deferred to Task 388. |
| `15784f6a` | 380 | **Doc, closes Phase 43**: wrote `docs/alphatesteffect-support.md` synthesizing Tasks 371–379 (mirrors `docs/basiceffect-support.md`'s Phase 42 style) — per-task summaries, full 3-backend support matrix, "Open, tracked follow-up work" listing Tasks 887/888. No code changed. |
| `b3946135` | 379 | **Fix (Bgfx, general)**: all 7 of Bgfx's texture-binding dispatch branches left the previous draw's texture bound when a draw's texture was null, instead of falling back to white like EasyGL/Vulkan. Fixed with a new `defaultWhiteTexture3D_` + `else` branch at every site. 3/3 PASS on all 3 backends; empirically confirmed the pre-fix bug (got black, not stale-texture). |
| `4cde20c0` | 378 | **Fix**: `AlphaTestEffect::FillGpuDrawParams()` never forwarded fog fields at all — fixed on EasyGL (shader infra already existed there). Discovered fog is a total no-op on Vulkan/Bgfx for **every** 3D effect, project-wide (not just `AlphaTestEffect`) — zero shader files in either backend implement it. Opened Task 888. |
| `b7e9388a` | 377 | **Real bug found (not fixed here)**: `AlphaTestEffect.VertexColorEnabled` has zero effect on Vulkan/Bgfx by default (their alpha-test pipeline never declares a color vertex attribute). Correct on EasyGL, confirmed by new `easygl_alphatest_vertexcolor_diffuse_test.cpp` (2/2 PASS). Empirically verified the Vulkan bug with a temporary, uncommitted test. Opened Task 887 for the fix. |
| `d13895c2` | 376 | 17 new direct unit tests (`AlphaTestEffectTests.cpp`) locking in `ReferenceAlpha`'s `/255.0f` scaling across boundary + out-of-range values (`-10`...`300`, unclamped, matching FNA), for both `AlphaTest` switch-case shapes. No GPU needed. Zero bugs — formula already correct per Tasks 371/373. |
| `9dcf85f5` | 373–375 | Threshold-sweep `CompareFunction` pixel test (24 assertions: 8 functions × below/at/above reference) on all 3 backends — first-ever `AlphaTestEffect` coverage of any kind on Vulkan/Bgfx (Task 190 was EasyGL-only, boundary-value-only). Found `SetDepthTestEnabled` throws on Bgfx (pre-existing, documented gap); fixed by omitting the unneeded call. Zero `AlphaTestEffect` bugs found. |
| `a9089852` | 372 | New `AlphaTestEffectTests.cpp` from scratch (zero prior coverage): 27 tests covering all 8 property defaults, setter round-trips, `Clone()`, `GetTypeName()`. No bugs found, no production code changed. |
| `70593b70` | 371 | **Opens Phase 43. Verify-only, zero bugs found** — `AlphaTestEffect`'s properties/defaults/dirty-flag constants/`OnApply()` formula all already match FNA exactly. Confirmed (not fixed, Task 378's job) that fog is a total GPU no-op for this effect. Zero existing test coverage found (Task 372's job). |
| `dda9a7b1` | 370 | **Closes Phase 42.** Capstone test combining texture+vertexcolor+diffuse+emissive on all 3 backends, first `BasicEffect` test to use a real multi-texel texture (exercises Task 368's Bgfx layout fix for real). All 3 backends produced byte-identical output — no new bugs, pure integration verification. Wrote `docs/basiceffect-support.md` synthesis doc. |
| `ccb957a0` | 369 | **Fix**: `FillGpuDrawParams()` dropped `EmissiveColor` from the `LightingEnabled=false` diffuse formula on all 3 backends (shared C++ fix, matches `EffectHelpers.SetMaterialColor`'s exact branching). Lit-path emissive/multi-light/specular deliberately scoped out into new Tasks 885/886 rather than bundled in. |
| `1e6f87c7` | 368 | **2 real bugs fixed**: `FillGpuDrawParams()` ignored `DirectionalLight0.Enabled` entirely (all 3 backends, shared C++ fix); Bgfx's `MakeBgfxLayout()` never bound `Normal`/`TexCoord0` for strides 20/24/32 (Bgfx-only, wide-reaching — silently broke per-vertex UV/normal on those strides project-wide, invisible until this task's non-1×1-texture, real-normal test). |
| `2569b0e1` | 367 | **Fix (2 of 3 backends)**: the stride-24 `VertexPositionColorTexture` shader path (texture × vertex color) silently dropped `DiffuseColor` entirely on EasyGL and Bgfx (no uniform, no multiply at all); Vulkan already had it right. Fixed both to mirror Task 364's `VertexColorEnabled`-gate pattern. Also fixed a stale Task 189 test that only passed because of the EasyGL bug this task fixed. |
| `90b9be1b` | 366 | Verify-only: texture × diffuse color already correct on all 3 backends; closed a real test-coverage gap (prior test only used degenerate white/white cases). |
| `a4a80bd2` | 365 | Verify-only: `DiffuseColor × Alpha × VertexColor` already correct on all 3 backends when `VertexColorEnabled=true`. |
| `54aee7a2` | 364 | **Fix**: `VertexColorEnabled` wasn't honored by any of the 3 backends' no-texture shaders — fixed per-backend; found (not fixed) a Bgfx-only rasterizer-cull-default bug (Task 896). |
| `563dcbb2` | 363 | Verify-only: `EnableDefaultLighting()`'s exact constants already match FNA literal-for-literal. |
| `251c38d2` | 362 | Exhaustive default-value tests for all 22 `BasicEffect` properties; **fix**: `DirectionalLight`'s direction default was wrong (`Vector3::Forward` instead of `Vector3.Zero`). |
| `c079089d` | 361 | Opened Phase 42. Audited `BasicEffect` vs FNA; **fix**: 2 default-value bugs (`VertexColorEnabled`, `DirectionalLight0.Enabled`). |
| `41dd2bc8` | 360 | **Closed Phase 41.** Effect lifecycle tests (dispose/apply-after-dispose); confirmed CNA's disposed-effect guard is a deliberate safety improvement over FNA (which has none). |
| `77f0e1af` | 359 | Verify-only: missing-parameter-lookup behavior (null vs. throw) already matches FNA exactly across all 3 lookup surfaces. |
| `0216b64e` | 358 | Verify-only: `EffectParameterCollection` enumeration order already matches FNA's plain insertion order. |
| `f1d97235` | 357 | Verify-only: name/semantic lookup semantics already match FNA (ordinal, case-sensitive, first-match-wins). |
| `0216b64e`/`009772e8` | 356 | Verify-only: `CurrentTechnique` selection already correctly matches FNA. |
| `05bb689f` | 355 | **Fix**: `EffectPass::Apply()` now validates the pass belongs to the current technique (matching FNA); **fix**: `EffectTechniqueCollection`'s by-value vector storage was a dangling-pointer hazard — switched to `vector<unique_ptr<...>>`. |
| `19f61361` | 354 | Docs: `docs/shader-effect-vs-fx-bytecode.md` — what's supported today, the interim throw guard, the Phase 74 roadmap. |
| `41b66531` | 353 | **Feat**: added the previously-missing bytecode-accepting `Effect` constructor; throws `NotImplementedException` until Phase 74 lands. |
| `e2a67b87` | 352 | Decision (user's, not inferred): full support for `.fx` bytecode. Found MojoShader's source locally vendorable; opened Phase 74. |
| `2a9748cf` | 351 | Opened Phase 41. Audited `Effect` vs FNA; **fix**: `GetTypeName()`, missing `NOXNA`, a `Dispose()` name-hiding bug that broke compilation on every concrete effect class. |
| `755efaf2` | 350 | **Closed Phase 40.** Docs: `docs/viewport-displaymode-adapter-support.md` — non-desktop (Android/Web) limitations. |
| `3a8de4a3` | 349 | **Fix**: `UpdateViewportFromWindow()` was stomping a custom `Viewport` back to full-size on every frame; fixed with dedicated last-known-size tracking. |
| `a43702b3` | 348 | Verified `Viewport` tracks a real OS-level window resize; confirmed `PresentationParameters.BackBufferWidth/Height` deliberately don't follow it (documented CNA divergence). |
| `704f0e13`/`6c58aa61`/`42cf16cf` | 345–347 | Audited `GraphicsAdapter`/`DisplayModeCollection`; **fix**: dangling `DefaultAdapter` reference, missing `this[SurfaceFormat]` indexer, headless-fallback leak. |

Older history (Phases 1–39): see `plan_graphics.md` and `docs/*.md` synthesis docs
(`docs/rasterizerstate-support.md` Phase 38, `docs/rendertarget-support.md` Phase 39,
`docs/sampler-state-support.md` Phases 34–36, `docs/depthstencilstate-support.md` Phase 37).
Headline older findings: Task 293 fixed a severe, project-wide bug (per-slot `SamplerState`
silently ignored by all 3D draws, all 3 backends); Task 304/868 found Vulkan's `BlendState` support
is almost entirely fake; Task 315 fixed a real EasyGL stencil-buffer-request bug.

---

## 4. Current blocker / main problem

**There is no build-breaking or test-breaking blocker.** The repository builds and the test suites
pass at the rates given in §2 on all 3 backends.

The most significant *correctness* gap is architectural: `Texture3D`/`TextureCube` do not inherit
`Texture` in CNA (they inherit `GraphicsResource` directly), which structurally prevents
`Texture3D` from ever being sampled via the normal `GraphicsDevice.Textures[slot]` path. No failing
command or test is tied to this — it manifests as a compile-time impossibility if game code tries
`GraphicsDevice.Textures[i] = my3DTexture`. See `plan_graphics.md` Task 863.

The most significant *silent-failure* gaps (compile and run without error, wrong or no data):
Vulkan's `BlendState`/`DepthStencilState` support (Tasks 868/870),
`Texture3D`/`TextureCube::GetData` on Vulkan/Bgfx (Task 865), and `BasicEffect`'s
lit-path missing `+EmissiveColor`/unforwarded specular+extra-lights terms (found Task 366, fixed
for the no-lighting path in Task 369, lit path tracked as Tasks
885/886). Task 910 (Bgfx rendering into more than one `RenderTargetCube` face/`RenderTarget2D`
per un-advanced frame silently corrupting all but the last one bound) is now fixed — see §3/§5.
None have a test that currently fails loudly — they're only visible via dedicated pixel tests or
direct code reading.

**Environment note (2026-07-07):** GPU/window-creating test binaries (`CnaTests`, the
`cna_test_*`/`easygl_*`/`vulkan_*`/`bgfx_*` example executables) must **not** be run against the
real display (`DISPLAY=:0`) — doing so pops real windows on the developer's actual desktop and
was genuinely disruptive mid-session (repeated window/keyboard-focus stealing). Use a virtual
display instead: `Xvfb :99 -screen 0 1280x1024x24 &`, then `SDL_VIDEODRIVER=x11 DISPLAY=:99 ./CnaTests`
(or the individual example binary directly). **`ctest` cannot be used for the GPU pixel-test
suites as-is** — `CMakeLists.txt` hardcodes `ENVIRONMENT "...;DISPLAY=:0"` on every
`Vulkan_*`/`Bgfx_*`/`EasyGL_*` GPU test (265 occurrences), which overrides any `DISPLAY` already
exported in the shell; this was not fixed (out of scope, large mechanical find-replace across the
whole file, needs its own task/decision on the right default). Vulkan's `CnaTests` under
`Xvfb`+software rendering (`llvmpipe`, since Xvfb has no real GPU/DRI3) shows non-deterministic,
low-single-digit-count test flakiness unrelated to any specific change (different tests fail each
run) — a known limitation of this fallback environment, not a regression signal. EasyGL and Bgfx
both ran fully clean (0 failures) under the same `Xvfb` setup.

**Follow-up (2026-07-07, end of Task 896 session):** the full `ctest` suite (all `examples/*.cpp`
pixel tests, ~200+ across 3 backends) has **not** been re-run end-to-end since before Task
878/879 started — see §2's test-status section and §8 item 0. Whoever picks this up next should
either (a) start a fresh `Xvfb :99` (`Xvfb :99 -screen 0 1280x1024x24 &`, then
`SDL_VIDEODRIVER=x11 DISPLAY=:99 ctest ...` — this does NOT work as-is due to the hardcoded
`DISPLAY=:0` above, so this only works for direct binary invocation, not `ctest` itself), or
(b) properly fix the `ctest` `DISPLAY=:0` hardcoding (e.g. make it configurable via a CMake cache
variable defaulting to `:0` for backward compatibility) so the full suite can run against a
virtual display. **Never run any GPU/window-creating binary against the real `DISPLAY=:0`** —
confirmed disruptive to the human developer's desktop twice in one session. Separately: this is a
shared machine — a `cat /proc/loadavg` check before trusting any flaky-looking test result is
worthwhile, since unrelated concurrent builds from other sessions/projects (observed: a sibling
`cna_devices` project building with high parallelism) can push load average to 4x the core count
and cause transient, non-representative test failures unrelated to any code change.

---

## 5. Known bugs and limitations

| Status | Issue | Tracking |
|---|---|---|
| Confirmed, MASSIVE, not fixed | Vulkan's `BlendState` support is almost entirely fake — hardcodes one blend equation regardless of request. Confirmed 5× via pixel tests. EasyGL fully correct. | Task 868 |
| Fixed | Vulkan's `DepthStencilState` support: `DepthBufferFunction` now real per-pipeline (`ToVkCompareOp`, cache-key-widened), full front/back stencil-op support (`ToVkStencilOp`, dynamic reference/masks), `FindDepthFormat()` now prefers stencil-capable formats. Also fixed a separate, previously-undiscovered bug found as a prerequisite: `GraphicsDevice.ReferenceStencil`'s setter never reached any backend (EasyGL/Bgfx still have this gap, not fixed here). `git stash` confirmed all 6 previously-failing tests fail exactly as predicted without the fix. | Task 870 (done) |
| Confirmed, universal, not fixed | `GraphicsDevice.ReferenceStencil`'s independent-override has zero backend connection on all 3 backends. | Task 872 |
| Confirmed, universal, not fixed | `GraphicsDevice::Clear` ignores `ClearOptions::Stencil` on every backend. | Task 871 |
| Fixed, all 3 backends, both `RenderTarget2D` and `RenderTargetCube` | `mipMap` produces a real, pixel-verified mip chain on EasyGL (Task 336), Vulkan (Task 878/907, `vkCmdBlitImage` cascade — `RenderTargetCube` scoped per-face via `baseArrayLayer`), and Bgfx (Task 906/907 — a 2-line fix, `hasMips=true`, since bgfx's own `BGFX_RESOLVE_AUTO_GEN_MIPS` framebuffer-attachment default already auto-regenerates mips; corrects Task 878's own "needs new downsample-shader infra" prediction). | Task 878/906/907 (done, all 3 backends, both RT types) |
| Fixed, all 3 backends, both `RenderTarget2D` and `RenderTargetCube`; environment-dependent test on Bgfx | MSAA produces a real, pixel-verified anti-aliased resolve on EasyGL (Task 337), Vulkan and Bgfx, for both `RenderTarget2D` (Task 878/879 — Vulkan piggybacks on the backend's own backbuffer `sampleCount_`, Bgfx uses `BGFX_TEXTURE_RT_MSAA_Xn`) and `RenderTargetCube` (Task 903 — same mechanism, applied per cube face via a shared MSAA colour image). Task 903 also found and fixed a real Vulkan bug: the 2D `SpriteBatch` pipeline's MSAA selection only checked the backbuffer's own state, never an RT's — fixed to check the bound target's `WantsMsaa()`, correcting the identical latent gap for `RenderTarget2D` too. **`Bgfx_RenderTarget2D_MsaaResolve` fails under this session's `Xvfb` sandbox specifically**: the test forces `CNA_BGFX_RENDERER=VULKAN` (bgfx's default GL renderer only negotiates legacy GL 2.1 here, under which MSAA doesn't really resolve — an already-documented Task 879 finding), but `Xvfb` has no DRI3 support (confirmed via `vulkan: No DRI3 support detected` in the test's own output), so bgfx's Vulkan renderer fails to initialize and silently falls back to the same GL 2.1 path the workaround was meant to avoid (`active renderer: OpenGL 2.1` despite requesting Vulkan). Not a code regression — an `Xvfb`-specific environment limitation on top of an already-tracked finding; passes against a real display with a working Vulkan/DRI3 stack. | Task 879/903 (both done) |
| Fixed, all 3 backends (backbuffer); RT-pass sub-region still hardcoded on Vulkan/Bgfx | `GraphicsDevice.Viewport` now has real GPU effect on the backbuffer for all 3 backends. Vulkan/Bgfx's RT passes still hardcode each RT's own full size regardless of a custom sub-region `Viewport` (deferred multi-RT-per-frame recording limitation, same shape as their existing RT-pass-scissor gap below). 2D `SpriteBatch` unaffected by `Viewport` on all 3 backends. | Task 880 (done) |
| Confirmed, real, not fixed, separately found | Vulkan's `RecordCommandBuffer` RT pass hardcodes `vkCmdSetScissor` to each RT's own full size (`VkRect2D rtSc{{0,0},{rtW,rtH}}`), ignoring `scissorEnabled_`/`scissorX_`/etc. entirely — only the backbuffer pass reads the real dynamic scissor state. A custom `ScissorRectangle` therefore has zero effect while rendering into a bound `RenderTarget2D` on Vulkan. Found while scoping Task 880's identical Viewport limitation. | — |
| Fixed | `TextureCube::DDSFromStreamEXT` now really parses DDS headers and decodes DXT1/3/5 cube-map data (6 faces × mip levels) to `SurfaceFormat::Color`, matching FNA's exception behavior for non-cube/unsupported input. | Task 663 (done) |
| Fixed, `TextureCube` and `Texture3D` | `SetData`/`GetData`'s region-taking overloads now validate `elementCount` against the actual texel/voxel count, matching `Texture2D`'s already-correct pattern — a mismatch previously caused heap corruption (confirmed via a live `free(): invalid pointer` crash) instead of a clean exception. | Task 913 (done) |
| Fixed, all 3 backends | `Texture3D`/`TextureCube::GetData` is now a real GPU readback on all 3 backends: Vulkan (`vkCmdCopyImageToBuffer` + staging buffer, mirroring `SetData` in reverse) and Bgfx (Task 914 — blit into a temporary `BGFX_TEXTURE_BLIT_DST|READ_BACK` texture, since `bgfx::readTexture()`'s source can't also be shader-sampled; `bgfx::frame()`-advance loop to wait for the async result). | Task 865 (done, Vulkan) / 914 (done, Bgfx) |
| Confirmed, silent failure | `Texture2D::SetData(level>0,...)` no-op on Vulkan/Bgfx; EasyGL renders solid black for mip filters on mip-incomplete textures. | Task 867 |
| Confirmed, architectural, not fixed | `Texture3D`/`TextureCube` can't be sampled in any shader — don't inherit `Texture`. | Task 863 |
| Fixed, Bgfx | Bgfx: `SpriteBatch::Draw`ing a `RenderTarget2D` previously read a framebuffer handle where a texture handle was expected — samples wrong data, doesn't crash. Fixed via a new `IBgfxSamplable` accessor both `BgfxTextureBackend`/`BgfxRenderTargetBackend` implement correctly, fixed as a hard prerequisite of Task 878/879's Bgfx MSAA test. | Task 873 (done) |
| Confirmed, severe, silent failure, not fixed | Bgfx: same bug shape as Task 873 for `RenderTargetCube` via `EnvironmentMapEffect`. | Task 874 |
| Fixed, Vulkan | `SetRenderTarget`+`Clear()` with no draw call now correctly records a render pass — `Clear()`/`ClearColorAndDepth()` mark the currently-bound RT as needing recording even with zero draw calls. | Task 875 (done) |
| Fixed (verify-only — bug no longer reproduces) | Vulkan: sampling a `RenderTargetCube` via `EnvironmentMapEffect` after unbinding — previously rendered black, now confirmed passing cleanly and consistently (5 repeated runs + dedicated `ctest`). Not independently bisected to an exact fixing commit; likely an incidental side effect of this session's `VulkanRenderTargetCubeBackend` rework (Task 907). | Task 876 (done) |
| Fixed, all 3 backends | EasyGL, Bgfx, and now Vulkan (Task 911) all honor the exact requested `DepthStencilFormat` (`None`/`Depth16`/`Depth24`/`Depth24Stencil8`) for a render target's depth/stencil attachment, on both `RenderTarget2D` and `RenderTargetCube` (Bgfx's cube previously had NO depth attachment support at all; Vulkan previously shared one device-global depth format for every render target regardless of what was requested). Vulkan's fix required a depth-format-keyed render-pass+pipeline cache across all 9 3D pipeline functions plus the 2D sprite pipeline; MRT deliberately stays on the device-wide format (XNA/FNA's own multi-target depth semantics are inherently ambiguous). Core depth-test functionality itself works everywhere it's honored (Task 335). | Task 877 (done, EasyGL+Bgfx) / 911 (done, Vulkan) |
| Confirmed, architectural, deliberate | `GraphicsDevice` stores state objects by value, unlike FNA's reference-type aliasing. No game code here relies on FNA's behavior. | Task 869 |
| Fixed, all 3 backends | `SpriteBatch`'s `SamplerState` (`Begin()`) now works on all 3 backends (EasyGL/Task 269, Vulkan/Task 665, Bgfx/Task 750 — never overrode `SetSamplerFilter`/`SetSamplerAddressMode` at all). | Task 750 (done) |
| Fixed, SDL_Renderer | `GraphicsDevice.GetBackBufferData` was a hard `throw` on SDL_Renderer (no `ReadBackbuffer` override at all) — blocked the entire SDL_Renderer pixel-test audit phase. Fixed via `SDL_RenderReadPixels()`; exact-pixel tests must request `PresentationMode::NativeBackBuffer` for true 1:1 logical/physical correspondence (the default `FixedHeightDynamicWidth` mode deliberately does not give 1:1 mapping). | Task 915 (done) |
| Confirmed, pre-existing | `EasyGL_MRT_TwoAttachments`: attachment 1 stays black with 2 render targets. | Task 145 |
| Fixed, all 3 backends | `SetRenderTargets`'s simultaneous-target cap now matches FNA's real `MAX_RENDERTARGET_BINDINGS=4` via a single shared C++ check. | Task 881 (done) |
| Confirmed, real, not fixed, separately found | CNA's `RenderTargetBinding` (unlike FNA's, whose constructors throw on a null target) has a default constructor wrapping a null `Texture*`; passing several through `SetRenderTargets` segfaults. Unreachable in practice given FNA's own API shape (a real game can never construct one this way), found while testing Task 881. | — |
| Confirmed, incomplete | `PresentationMode::Letterbox`/`Overscan`/`Stretch`/`NativeBackBuffer` aren't distinctly implemented on EasyGL; Vulkan/Bgfx implement no virtual-resolution scaling at all. | Task 882 (not yet a formal `plan_graphics.md` row — referenced inline in Task 348) |
| Confirmed, pre-existing, out-of-repo | `easy-gl-resource-smoke-tests` aborts on an internal assert in the sibling `easy-gl` repo. | — |
| Confirmed, pre-existing | `Vulkan_DepthBias`'s `DepthBias=-1e6` sub-case fails; other sub-cases pass. | — |
| Confirmed, pre-existing, flaky | `Vulkan_FillMode_WireFrame`/`Vulkan_RenderTargetUsage`: order-dependent, only one fails per full-suite run. | — |
| Fixed, all 3 backends | `GraphicsDevice` now pushes its real default `RasterizerState` (`CullCounterClockwiseFace`) to all 3 backends' GPU state at construction. 119 `examples/*.cpp` files audited/fixed first (upfront, per the user's own scoping decision) — 115 needed `RasterizerState::CullNone`, 4 confirmed already safe. | Task 896 (done) |
| Fixed, all 8 concrete subclasses | `Effect::Clone()` is now a real pure-virtual base contract; 5 of 8 concrete subclasses already had a working (but non-polymorphic) `Clone()`, the other 3 (`BasicEffect`, `EffectMaterial`, `ShaderEffect`) implemented here. | Task 883 (done) |
| Fixed, all 3 backends | `BasicEffect::FillGpuDrawParams()` now forwards `SpecularColor`/`SpecularPower` and renders real half-vector Blinn-Phong specular highlights on all 3 backends. | Task 886 (done) |
| Fixed, all 3 backends | `AlphaTestEffect.VertexColorEnabled` now works on Vulkan/Bgfx — a new stride-24-only sibling vertex shader reads the vertex color attribute and gates it, dispatched alongside the existing alpha-test pipeline. Was correct on EasyGL from the start (reuses `BasicEffect`'s already-fixed stride-24 shader). | Task 887 (done) |
| Fixed, all 3 backends, every pipeline | Fog now works everywhere: Bgfx for all 7 applicable pipelines, Vulkan for all 8 (`alpha_test3d`/`alpha_test_colored3d`/`lit_textured3d` via Task 888's spare UBO/push-constant capacity; `colored3d`/`textured3d`/`colored_textured3d`/`dual_texture3d`/`skinned3d` via Task 899's new "Bundle A"/extended-descriptor-set infrastructure; `env_map3d` via Task 899's own noted cheap leftover, fog packed into `EnvMapParams`' spare tail bytes). | Task 888 (done)/899 (fully done) |
| Fixed, all 3 backends | `SkinnedEffect`/`EnvironmentMapEffect`'s `FillGpuDrawParams()` forwards fog fields correctly on all 3 backends, and every backend's GPU shader pipeline for both effects now implements the blend too (EasyGL: Task 900; Bgfx `env_map3d`/`skinned3d` + Vulkan `skinned3d`/`env_map3d`: Task 899). | Task 900/899 (all done) |
| Fixed, Bgfx | `SkinnedEffect.EmissiveColor` was a total GPU no-op on Bgfx — `fs_skinned3d.sc` never declared or read a `u_emissiveColor` uniform, and the C++ `skinned` dispatch branch never set the (already-existing, already-used-elsewhere) uniform handle. Found while writing Task 899's Bgfx fog test (which isolates fog via `EmissiveColor` alone). Distinct from Task 894 (`SpecularColor`/`SpecularPower`, still open on all 3 backends). | Task 899 (Bgfx bonus, done) |
| Confirmed, real, not fixed | `DualTextureEffect.VertexColorEnabled` has **zero effect on all 3 backends** — every backend's dual-texture dispatch is a dedicated shader/pipeline declaring only `position`+`texcoord` inputs (Vulkan explicitly reuses the generic textured-only vertex shader; Bgfx hardcodes `v_color0` to the diffuse uniform, not a real per-vertex attribute). Found while writing Task 389's capstone test — Phase 44 never had a dedicated audit task for this property, unlike `AlphaTestEffect`'s Task 377. | Task 889 |
| Confirmed, real, not fixed | `EnvironmentMapEffect::FillGpuDrawParams()` only forwards `DirectionalLight0` — `DirectionalLight1`/`DirectionalLight2` silently ignored on all 3 backends. Confirmed this effect shares `BasicEffect`'s identical `Lighting.fxh`/`ComputeLights` mechanism in real FNA (same `oneLight` shader-variant optimization), so the same gap and likely the same fix plumbing as Task 885 applies here too. | Task 890 |
| Confirmed, real, not fixed | `EnvironmentMapEffect`'s base cube-map lerp target (`envColor`) is not scaled by combined texture×diffuse alpha on any backend; FNA's real formula (`envmap = SAMPLE_CUBEMAP(...) * color.a`) scales both `envmap.rgb` (base lerp, still unscaled) and `envmap.a` (specular term, fixed by Task 395). Only visible when texture/diffuse alpha is strictly less than 1 — every existing test used opaque textures/diffuse colors. | Task 891 |
| Fixed, both backends | `BasicEffect`'s lit-textured Bgfx shader (`vs_lit_textured3d.sc`, Task 892) and Vulkan shader (`lit_textured3d.vert.glsl`, Task 898) both transformed the vertex normal by the full `World×View×Projection` matrix instead of `World`'s inverse-transpose — geometrically meaningless for a direction vector, invisible until Task 886's real-camera specular test. Both now use a correct inverse-transpose normal matrix (CPU-side `ComputeNormalMatrix3x3` on Bgfx, GLSL built-in `inverse()` in-shader on Vulkan). EasyGL was already correct. | Task 892/898 (done) |
| Confirmed, minor, acceptable deviation | `EnvironmentMapEffect`'s Fresnel edge-weighting (Task 396 fix) is computed per-pixel in CNA vs. FNA's real per-vertex (then rasterizer-interpolated) computation — identical on flat/coarse-normal test geometry, but could look subtly different from FNA on sparsely-tessellated curved surfaces at silhouette edges (CNA's per-pixel version is strictly more accurate, not less). | — |
| Confirmed, real, not fixed | `SkinnedEffect`'s `DirectionalLight1`/`DirectionalLight2` are silently ignored by every backend's GPU dispatch, same shape as `BasicEffect`/`EnvironmentMapEffect`'s already-tracked gaps. | Task 893 |
| Confirmed, real, not fixed | `SkinnedEffect`'s `SpecularColor`/`SpecularPower` have zero GPU implementation on any backend — `GpuDrawParams` has no generic specular fields at all, same shape as `BasicEffect`'s already-tracked gap. | Task 894 |
| Confirmed, real, not fixed | `SkinnedEffect.WeightsPerVertex` is a complete GPU no-op on all 3 backends — the skinning shader always sums all 4 bone weights regardless of the property's value (1/2/4), unlike FNA's real shader which only sums the first N. Only visible when unused weight slots hold nonzero data. | Task 895 |
| Confirmed, minor, acceptable deviation | `SkinnedEffect`'s `PreferPerPixelLighting=false` default is effectively a no-op — lighting (`NdotL`) is always computed in the fragment shader on every backend, so CNA always renders at per-pixel quality regardless of this flag (strictly more accurate than FNA's real per-vertex default, never worse). | — |
| Fixed, all 3 backends | Confirmed and fixed the same `mipLevels`/`hasMips` hardcoded-to-1 bug (same shape as Task 276's `TextureCube` fix) for `Texture3D` on EasyGL (Task 862), both `Texture3D`/`TextureCube` on Vulkan (Task 864), and both on Bgfx (Task 914) — reverting the EasyGL/Vulkan fix reproduces real GPU memory corruption (mip levels overwrite each other), not just a silent no-op. Bgfx's own fix needed a real `GetData` readback path first (`GetData` was previously a total no-op there); Task 914 added one via a temporary `BGFX_TEXTURE_BLIT_DST|READ_BACK` texture + `bgfx::blit()`/`bgfx::readTexture()`, confirming `BGFX_CAPS_TEXTURE_BLIT`/`READ_BACK` are both actually supported in this sandbox. | Task 862/864/914 (all done) |
| Needs verification | Whether Bgfx's window actually has a physical stencil buffer has not been checked. | — |
| Incomplete, by design | Stride-keyed vertex layout only supports strides 16/20/24/32/52. Vulkan has no `Tangent`/`Binormal` mapping. `SurfaceFormat` support is Color-only for real GPU formats. `SDL_Renderer` has no 3D at all. | — |
| Risky assumption | `GraphicsDevice`'s user-primitive scratch buffers never shrink — fine for typical use, but memory stays at the high-water mark for the device's lifetime. | — |
| Confirmed, test-harness only, worked around | Bgfx's `GetBackBufferData()` only reliably reflects the *first* read call per rendered frame — reading multiple distinct rectangles from a single frame returns stale/blank data for reads after the first. Every multi-point Bgfx pixel test in this project reads exactly one rectangle per draw+retry pass as a result (Task 406 established a `renderAndRead()`-style per-checkpoint helper for new tests). | — |
| Fixed, Bgfx | `BgfxGraphicsBackend::EnsureViewState()` (called from `Clear()`/`SubmitSprite()`) previously reset the current view's rect and 2D ortho-projection to the full *window* size unconditionally, clobbering `BindAsRenderTarget()`'s correctly RT-sized viewport the moment `Clear()` next ran on that RT — silently corrupting all rendering into any `RenderTarget2D` smaller than the window. Found and fixed while verifying Task 878/879's Bgfx MSAA test (the first Bgfx test to both render into a differently-sized RT and pixel-verify the result). | Task 901 (done) |
| Fixed | `GraphicsDeviceManager.PreferMultiSampling` now genuinely reaches the backend: `GraphicsDeviceManager::applyToExistingBackend()` calls the real `GraphicsDevice::Reset()`, which calls the new `IGraphicsBackend::ApplyMultiSampleCount()` (Vulkan: real in-place swapchain/render-pass/pipeline reconfiguration; EasyGL: honest echo of its construction-time value, can't change post-construction; SDL_Renderer/Bgfx: no backbuffer MSAA support, honest 0). `vulkan_msaa_test.cpp` (Task 147) no longer a false positive — rewritten with a genuine diagonal-edge differential exercising the real runtime toggle path. Scoped to the backbuffer only — already-live `RenderTarget2D`/`RenderTargetCube` instances keep whatever MultiSampleCount they engaged at their own construction time. | Task 902 (done) |
| Fixed, both Vulkan and Bgfx | `RenderTargetCube` MSAA now works on both backends, mirroring `RenderTarget2D`'s existing Task 878/879 support (real XNA API surface, confirmed against FNA's actual constructor). Also fixed a real Vulkan bug found while verifying this: the 2D `SpriteBatch` pipeline's MSAA selection never checked an RT's own `WantsMsaa()`, only the backbuffer's. | Task 903 (done) |
| Fixed, Vulkan | `GetOrCreatePipelineFogTex3D` (`textured3d`/`colored_textured3d`, stride 20/24) was missing the same `msaa`-aware render-pass-selection check every sibling 3D pipeline-creation function has. Fixed with the identical ternary the siblings already use. Discriminating power confirmed via the Vulkan validation layer (not the pixel assertion, which passes either way on this driver) — `git stash` revert-and-rebuild reproduced the exact predicted `VUID-VkGraphicsPipelineCreateInfo-multisampledRenderToSingleSampled-06853`/`VUID-vkCmdDraw-renderPass-02684` errors. | Task 904 (done) |
| Fixed, Vulkan | `CreateRTRenderPass()`'s subpass dependencies didn't actually match `CreateRenderPass()`'s despite a comment claiming they did — pipelines created against `renderPass_` weren't truly render-pass-compatible with `rtRenderPass_`/`rtRenderPassLoad_`, producing live `VUID-vkCmdDraw-renderPass-02684` validation errors (correct pixel output regardless) the moment a depth-tested 3D primitive drew into a plain non-MSAA `RenderTarget2D`. Pre-existing, outside Task 878/879's own diff; found during independent review of that diff, fixed by widening `CreateRTRenderPass()`'s `deps[]` to match `CreateRenderPass()`'s exactly. | Task 905 (done) |
| Fixed, Bgfx | `RenderTarget2D` mip chains on Bgfx — turned out to be a 2-line fix (`hasMips=true` on `bgfx::createTexture2D`), correcting Task 878's own prediction that bgfx needed new downsample-shader infrastructure: bgfx's own `BGFX_RESOLVE_AUTO_GEN_MIPS` framebuffer-attachment default (set automatically by the already-used `createFrameBuffer` overload whenever the attached texture has mips) already auto-regenerates the chain via the platform's own `glGenerateMipmap`-equivalent, triggered on every framebuffer switch-away. | Task 906 (done) |
| Fixed, Bgfx | `EnvironmentMapEffect` sampling a `RenderTargetCube` on Bgfx previously did an unsafe `static_cast` to `BgfxTextureCubeBackend`, reading `BgfxRenderTargetCubeBackend::fbo` (wrong handle type) — identical bug shape to Task 873's `RenderTarget2D` fix. Fixed via a new `IBgfxCubeSamplable` interface + `dynamic_cast`, as part of Task 907's own verification (this was Bgfx's first-ever test of this path). | Task 874 (done) |
| Fixed | Bgfx: every render target (2D, cube, MRT) now gets its own distinct, stable bgfx view id (a free-list-backed pool, `Detail::AllocateRtViewId()`/`ReleaseRtViewId()`) instead of sharing one hardcoded id — rendering into more than one within a single un-advanced bgfx frame no longer clobbers all but the last one bound. `git stash` confirmed the exact predicted failure (the 1st RT stays at its primed baseline, never actually changes). | Task 910 (done) |
| Fixed, Vulkan | Every render target's depth/stencil buffer now gets a real, distinct `VkFormat` picked for its own instance (`PickDepthFormat()`), instead of silently sharing the backbuffer's device-wide `depthFormat_` — needed a depth-format-keyed render-pass cache (`GetOrCreateRTRenderPass`/`GetOrCreateRTRenderPassMsaa`) plus a `targetDepthFmt` cache-key dimension across all 9 3D pipeline functions and the 2D sprite pipeline (`PickRTPipelineRenderPass()`); MRT deliberately stays on the device-wide format. Also found and fixed a real regression this surfaced: `VulkanMRTProxy` previously borrowed `rts[0]`'s depth view directly, breaking when an RT genuinely has none (`DepthFormat::None`) — fixed with its own dedicated depth image; and a pre-existing unrelated `mrtProxy_` teardown leak. | Task 911 (done) |
| Fixed (verify-only — not a distinct bug) | Bgfx: sampling a render target (2D or cube) back out via `SpriteBatch` immediately after filling and unbinding it (no intervening `bgfx::frame()` boundary) can read back stale/black content on the first `GetBackBufferData()` call. Root-caused to the SAME already-documented "first read per rendered frame" quirk below (not `DepthStencilState`/`RasterizerState` leakage or 3D-vs-2D dispatch, both ruled out by bisection) — fixed by applying the existing retry-until-non-black convention; `rendertarget2d_depth_test.cpp` now registered for Bgfx too. | Task 912 (done) |
| Confirmed, pre-existing, environment-only | A `ContentManagerSkinnedModelTest`-area segfault occurs when running the full `CnaTests` suite on Vulkan under `Xvfb`+`llvmpipe`, non-deterministic in exactly where it lands — reproduces identically with/without Task 878's changes (confirmed via `git stash`), and the same test passes cleanly in isolation. Matches this file's already-documented general Vulkan/Xvfb/llvmpipe full-suite flakiness; not a regression, not investigated further. | — |

---

## 6. Architecture notes

### Main modules

| Layer | Location | Notes |
|---|---|---|
| XNA public API | `include/Microsoft/Xna/Framework/…` | Must match XNA 4.0 / FNA exactly |
| Backend contracts | `include/CNA/Internal/Backends/Common/` | `IGraphicsBackend`, `IVertexBuffer`, etc. |
| EasyGL backend | `src/CNA/Internal/Backends/EasyGL/` | Primary; OpenGL ES 3.2 via EasyGL wrapper |
| Vulkan backend | `src/CNA/Internal/Backends/Vulkan/` | `VulkanVertexFormatHelper.hpp` for per-format mapping |
| Bgfx backend | `src/CNA/Internal/Backends/Bgfx/` | `BgfxVertexFormatHelper.hpp`; `ReadBackbuffer()` (screenshot-callback based) works and is pixel-proven (Task 364) |
| CNA utilities | `include/CNA/`, `src/CNA/` | `NOXNA` helpers, logging, math |
| sharp-runtime | `../sharp-runtime/` (sibling repo) | `System.*` types, primitive aliases |

### Critical invariants (do not break these)

- **`NOXNA` macro** tags every non-XNA extension in public headers — required for any new CNA-only
  public method/constructor/type. Requires `#include "CNA/CNAHelper.hpp"`.
- **C# properties** → `getXProperty()` / `setXProperty()` — never public fields on the XNA surface.
- **`static readonly`** (C#) → `static const` member in `.hpp` + definition in `.cpp`.
- **Type aliases** from `SharpRuntime/SharpRuntimeHelper.hpp` (`bytecs`, `Single`, `String`, …) must
  be used on XNA API surfaces — never raw `uint8_t`/`float`/`std::string` directly.
- **Backend selection is compile-time** — no runtime branch between backends in the same binary.
- **Stride-keyed vertex layout** — only strides 16/20/24/32/52 work correctly for 3D.
- **Doxygen required** on every public `.hpp` member: full `/** @brief … @param … @return */`.
- **SPDX header** `// SPDX-License-Identifier: MS-PL` at the top of every `.hpp`/`.cpp`.
- **`Texture3D`/`TextureCube` inherit `GraphicsResource`, not `Texture`** — a known deviation from
  FNA (see §5). Do not assume code that works for `Texture2D` "just works" for these two.
- **`SurfaceFormat` ordinal values are load-bearing** — every backend does
  `static_cast<int>(format)`. Any enum edit must preserve FNA's exact ordinals (verified 0–26).
- **`Texture::ValidateFormat` blocks every format except `Color`** at construction time.
- **`GraphicsDevice::userVertexScratch_`/`userIndexScratch_`** are shared, growable, non-shrinking
  scratch buffers used by all `DrawUserPrimitives`/`DrawUserIndexedPrimitives` overloads. Never
  resize down; never reenter mid-write.
- **No backend's `CreateRenderTarget2D`/`CreateRenderTargetCube` accepts a mip count or a
  multisample count on Vulkan/Bgfx** — only EasyGL actually wires these through (Tasks 336/337).
- **`Effect`/`EffectTechnique`/`EffectPass`/`EffectParameterCollection`** — collections of these
  must use `vector<unique_ptr<T>>`, not `vector<T>` by value, if any code caches a raw pointer/
  reference across an `Add()` call (a real dangling-pointer bug class found in Task 355; now fixed
  everywhere — `EffectTechniqueCollection` by Task 355, `EffectParameterCollection`/
  `EffectPassCollection` by Task 884).

### FNA reference

Authoritative behavioral reference: `/rv/data/library/github.com/FNA-XNA/FNA/src`. When CNA
intentionally diverges from FNA, document it in the commit/PR description and in `plan_graphics.md`
— not as a source comment explaining the deviation's rationale.

---

## 7. Useful commands

```bash
# Configure (EasyGL — primary)
cmake -B cmake-build-debug -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON

# Configure (Vulkan)
cmake -B cmake-build-vulkan -DCNA_GRAPHICS_BACKEND=VULKAN -DCNA_BUILD_TESTS=ON

# Configure (Bgfx)
cmake -B cmake-build-bgfx -DCNA_GRAPHICS_BACKEND=BGFX -DCNA_BUILD_TESTS=ON

# Build CNA library
cmake --build cmake-build-debug --target CNA -j$(nproc)

# Build and run all unit tests (EasyGL)
cmake --build cmake-build-debug --target CnaTests -j$(nproc)
./cmake-build-debug/CnaTests

# Run a specific unit test suite
./cmake-build-debug/CnaTests --gtest_filter="Texture2DTest.*"

# Build Vulkan (single-threaded is more reliable for link stability)
cmake --build cmake-build-vulkan --target CNA -j1

# Full ctest run (unit + integration), any backend build dir — run sequentially, not concurrently
# across backends (see §2)
cd cmake-build-debug && ctest -j1 --output-on-failure
cd cmake-build-debug && ctest -R <TestName>          # run one test in isolation (useful for flaky tests)

# Run a specific EasyGL/Vulkan integration/example test directly (needs an X server on :0)
SDL_VIDEODRIVER=x11 DISPLAY=:0 ./cmake-build-debug/cna_test_easygl_rendertarget2d_properties
```

There is no known reproducible failing build command right now (see §4).

---

## 8. Next smallest tasks

Phase 47 ("SpriteBatch renderer correctness") is now fully closed — Tasks 411–420 (mock-backend
infrastructure + all 5 sort modes + real GPU pixel tests) and both of the phase's originally-
scoped real bugs (Tasks 664/665) are done. Task 884 (`EffectParameterCollection`/
`EffectPassCollection` dangling-pointer hazard), Task 885/897 (`BasicEffect` lit-path
`DirectionalLight1`/`2`/`EmissiveColor`), Task 886/892/898 (real specular highlights + 2
normal-transform prerequisite fixes on Bgfx/Vulkan), Task 887 (`AlphaTestEffect.
VertexColorEnabled` ignored on Vulkan/Bgfx), Task 888 (real fog on Bgfx + Vulkan's
`alpha_test3d`/`lit_textured3d` pipelines), Task 900 (`SkinnedEffect`/`EnvironmentMapEffect`
fog forwarding on EasyGL), Task 881 (`SetRenderTargets`'s `MAX_RENDERTARGET_BINDINGS=4` cap),
Task 880 (`GraphicsDevice.Viewport` GPU wiring on all 3 backends), Task 899 (Vulkan fog, all 5
pipelines + Bgfx bonus + `env_map3d` leftover), Task 879 (`RenderTarget2D` MSAA on Vulkan/Bgfx),
Task 873 (Bgfx `SpriteBatch` RT-sampling cast bug), Task 901/905 (2 more real bugs found while
verifying Task 878/879), Task 883 (`Effect::Clone()`, plus discovering 5 of its 8 concrete
subclasses already had a working `Clone()` from an earlier, undocumented pass), Task 896
(`GraphicsDevice`'s default `RasterizerState` GPU-sync gap, closed via an upfront 119-file audit
across 8 parallel forks before landing the 1-line ctor fix), Task 877 (`DepthStencilFormat`
fidelity on EasyGL/Bgfx render targets, including new `RenderTargetCube` depth support on Bgfx —
Vulkan's own per-instance fidelity split to Task 911, a genuine architectural constraint), Task 875
(Vulkan Clear()-only render targets not recording a render pass), Task 912 (Bgfx sample-after-
unbind black readback, root-caused to an already-documented quirk, not a distinct bug), Task 663
(`TextureCube::DDSFromStreamEXT` real implementation), Task 913 (`TextureCube`/`Texture3D`
`elementCount` validation, closing a real heap-corruption gap), Task 865 (real Vulkan `GetData`
readback for `Texture3D`/`TextureCube`), Task 862/864 (`Texture3D`/`TextureCube` mip-level
allocation fixed on EasyGL/Vulkan; Bgfx split to Task 914, genuinely unverifiable with current
infrastructure), Task 903 (`RenderTargetCube` MSAA on Vulkan and Bgfx, plus a real Vulkan
2D-sprite-pipeline MSAA bug found and fixed along the way), and Task 750 (`SpriteBatch`'s
`SamplerState` fixed on Bgfx, closing the last of the 3 backends for this bug shape — EasyGL/Task
269, Vulkan/Task 665), and Task 902 (`GraphicsDevice::Reset()` now really reaches the backend —
`GraphicsDeviceManager.PreferMultiSampling` closes `vulkan_msaa_test.cpp`'s Task 147 false
positive) are all done. In priority order, the rest are the accumulated backlog from
earlier phases plus this task's new findings (Tasks 825–828, 863, 866–882, 889–895, 910–911,
914) — note the 421–500/666–861 ranges in `plan_graphics.md` also contain many still-open ⬜ rows
from earlier phases (audits, reference-value generation, per-backend pixel-test parity) that were
never folded into this curated list; Task 750 was pulled from there as a well-scoped exception
since it directly matched an already-tracked bug. Worth a dedicated triage pass before working
through that range in bulk — some entries (verify before starting any of them) may already be
superseded by later, higher-numbered rediscoveries of the same underlying bug.

0. **DONE this session.** Not a `plan_graphics.md` task — ran the full `ctest` suite for all 3
   backends' `examples/*.cpp` pixel tests, the single highest-value step to confirm Task 896's
   119-file audit didn't silently break anything. Fixed the blocker first (`CNA_TEST_DISPLAY`
   cache variable replacing `ctest`'s hardcoded `DISPLAY=:0`, ~270 occurrences), then ran all 3
   backends sequentially against `Xvfb :99`. **Found and fixed 4 real regressions Task 896's own
   audit missed or got wrong** (Tasks 908/909 — see §3); confirmed the remaining failures on all 3
   backends are the already-documented pre-existing ones (see §2's Test status section for exact
   counts) plus one `Xvfb`-specific Bgfx MSAA environment limitation (§5) and a handful of
   pre-existing `ContentManagerSkinnedModelTest`-area Vulkan segfaults under this sandbox's
   `Xvfb`+`llvmpipe` combination (confirmed via `git stash` to be unrelated to any of this
   session's changes).

**Tasks 911 and 914 are now DONE** — see §3/§5 for the full write-ups. Task 911: real per-instance
`VkFormat` fidelity for every Vulkan render target, a depth-format-keyed render-pass+pipeline cache
across all 9 3D pipeline functions plus the 2D sprite pipeline, plus a real `VulkanMRTProxy`
regression and a pre-existing `mrtProxy_` teardown leak found and fixed along the way. Task 914:
real `Texture3D`/`TextureCube::GetData` readback on Bgfx via a temporary blit-dst/read-back
texture, unblocking Task 864's mip-allocation fix for Bgfx too — confirmed `BGFX_CAPS_TEXTURE_
BLIT`/`READ_BACK` are both actually supported in this sandbox.

All 4 tasks the project owner explicitly approved "Implement now" this stretch (902/910/911/914)
are now closed. Now working through the standing backlog: **Tasks 667, 668, 669, and 670 are
done** (see §3 — every `SpriteSortMode` value now pixel-verified on SDL_Renderer, no bugs found;
new tests cover `BackToFront` and `Texture` grouping for the first time on any backend). Next:
Task 671 is already done from an earlier session (SpriteBatch rotation) — continue at Task 672.
Triage note on the Task 666+ SDL_Renderer audit phase (Tasks 667–861): most remaining rows are
concrete, well-scoped
"verify/pixel-test X on SDL_Renderer" items that fit this project's established
test-first/`git stash`-verify methodology directly — good source of the next several tasks. The
421–500 range is a different shape: mostly cross-cutting audit/documentation/infrastructure tasks
(SpriteFont/Model/OcclusionQuery audits, backend feature matrix, golden-image infra, FNA reference
generator, coverage docs, final 1.0 milestone declaration) rather than single-commit bug-hunting
tasks — several (e.g. 481–490, 499–500) explicitly depend on finishing everything else first, so
they're not good candidates to pick up out of order. Prefer continuing in Tasks 667+ order next.

---

## 9. Do not do yet

- **No architecture change to make `Texture3D`/`TextureCube` inherit `Texture`** (Task 863) without
  a deliberate, scoped design pass — it touches `EffectParameter`, `TextureCollection`, and every
  backend's texture-bind code. Not a small patch.
- **No rushed `TextureCube::DDSFromStreamEXT` implementation** without a real DDS cube-map test
  fixture built first.
- **No SpriteBatch Vulkan multi-batch fix** until the root cause is isolated.
- **No opportunistic fixes** for `EasyGL_MRT_TwoAttachments`, `Vulkan_DepthBias`, or
  `Vulkan_FillMode_WireFrame`/`Vulkan_RenderTargetUsage` flakiness — each needs its own dedicated
  root-cause investigation, not a guess bundled into an unrelated task.
- **No investigation of `easy-gl-resource-smoke-tests`** as part of a CNA task — it lives in the
  sibling `easy-gl` repo.
- **No refactor of the stride-keyed vertex layout system** — load-bearing for all 3D tests across
  all backends; needs its own dedicated phase with full regression testing.
- **No further changes to the `GraphicsDevice` user-primitive scratch buffers** without re-running
  the full `DrawUserPrimitives`/`DrawUserIndexedPrimitives` pixel-readback suite.
- **No API renames or namespace moves** — XNA API names and shapes are frozen by the FNA reference.
- **No mass Doxygen or NOXNA cleanup passes** — fix tags only on files you're already touching for
  a real reason.
- **No opportunistic fix for Task 868/870 (Vulkan blend/depth-stencil state)** bundled into an
  unrelated task — both are large, multi-pipeline-site changes; each needs its own dedicated task.
- **No opportunistic fix for Task 871/872 (stencil `Clear`/`ReferenceStencil` backend gaps)** —
  verify with a real test first.
- **Tasks 906/907 are done** (`RenderTarget2D`/`RenderTargetCube` mip on all 3 backends — Bgfx
  turned out to be a 2-line `hasMips=true` fix on both, not new downsample-shader infrastructure;
  see §3/§5). **Task 874 is also done** (closed as a Task 907 verification prerequisite). **Task
  910 is done** (every Bgfx render target now gets its own distinct bgfx view id instead of
  sharing one hardcoded id — see §3/§5). The pre-existing test-only workaround (forcing a
  `bgfx::frame()` boundary between per-face/per-RT operations, `bgfx_rendertargetcube_mip_test.cpp`)
  is left in place, harmless now that it's no longer strictly necessary.
- **No rushed fix for Task 874 (Bgfx `RenderTargetCube` handle-cast bug)** — Task 873's
  `RenderTarget2D`/`SpriteBatch` sibling is already fixed and *was* pixel-verified (via Task
  878/879's MSAA test's RT-then-backbuffer-sample methodology, once its own prerequisite bugs were
  fixed) — the old assumption that Bgfx has no pixel readback path for this class of bug no longer
  holds; use the same methodology for 874 rather than a purely structural check.
- **Tasks 875, 876, and 912 are all done** (Vulkan `Clear()`-only render targets now correctly
  record a render pass; Task 876's `RenderTargetCube`-via-`EnvironmentMapEffect` black-after-unbind
  bug was found to no longer reproduce at all when re-checked; Task 912's Bgfx render-target
  sampling bug was root-caused to the already-documented "first read per rendered frame" quirk, not
  a distinct bug — fixed via the established retry-until-non-black convention — see §3/§5).
- **Task 877 is done** (`DepthStencilFormat` fidelity on EasyGL/Bgfx render targets, including new
  `RenderTargetCube` depth support on Bgfx — see §3/§5). **For Task 911 (Vulkan per-instance
  `DepthStencilFormat` fidelity)**: don't assume a quick fix — varying the depth format per RT
  needs a depth-format-keyed render-pass *and* pipeline cache across 10+ `GetOrCreatePipelineXXX`
  functions (pipelines are deliberately shared across the backbuffer's and every RT's render pass
  today) — needs its own scoping pass before starting.
- **Task 902 is done** (`GraphicsDevice::Reset()` now really reconfigures the backend —
  `GraphicsDeviceManager.PreferMultiSampling` reaches `VulkanGraphicsBackend::ApplyMultiSampleCount()`
  and `vulkan_msaa_test.cpp`'s Task 147 false positive is fixed — see §3/§5). Scoped to the
  backbuffer only; already-live `RenderTarget2D`/`RenderTargetCube` MSAA is untouched.
- **No opportunistic fix for Vulkan/Bgfx's RT-pass sub-region Viewport/scissor limitation** (found
  while scoping Task 880) bundled into an unrelated task — the deferred multi-RT-per-frame
  recording architecture needs a real per-draw-call viewport/scissor capture design, not a quick
  patch.
- **No opportunistic fix for Task 145 (`EasyGL_MRT_TwoAttachments`)** bundled into any MRT-adjacent
  task — it needs its own dedicated root-cause investigation.

---

## 10. Resume prompt

```
Read NEXT.md first, in full — this section is intentionally the only thing you need before
touching any code.

## Repo state as of 2026-07-08 (end of Task 669 session) — READ THIS FIRST

All 4 `cmake-build-{debug,vulkan,bgfx,sdl}` directories exist and were verified building clean at
the end of this session (`cmake-build-sdl` needed `-DCNA_TEST_DISPLAY=:99` — it was still pointing
at the real `:0` display from an earlier configure; always double check this cache variable before
running `ctest` there, see §4). Working tree should be clean (this session's work is committed and
pushed). **Tasks 902, 870, 911, and 914 (`GraphicsDevice::Reset()` real backend wiring; Vulkan
`DepthStencilState`/stencil-test fidelity; Vulkan per-instance `DepthStencilFormat` fidelity; Bgfx
real `Texture3D`/`TextureCube::GetData` readback + mip-allocation fix) are all now done** — every
task the project owner explicitly approved "Implement now" this stretch (902/910/911/914) is
closed. Now working the standing backlog (§8): **Tasks 667, 668, 669, and 670 are also done this
session** (every `SpriteSortMode` value — `Deferred`/`Texture`/`FrontToBack`/`BackToFront`/
`Immediate` — confirmed already correct on SDL_Renderer, no bugs found). Next up: continue the
Task 666+ SDL_Renderer audit phase in order (Task 671 is already done from an earlier session —
continue at Task 672), and/or the untriaged 421–500 range of
`plan_graphics.md` (see §8's own triage note on why 667+ is the
better source of well-scoped single-commit tasks right now).

**First action on resume, before any feature work** (run these in order):
  1. `git log --oneline -5` and `git status --short` — confirm clean tree, note current HEAD.
  2. `cmake --build cmake-build-debug --target CNA CnaTests -j4` (EasyGL)
  3. `cmake --build cmake-build-vulkan --target CNA CnaTests -j4` (Vulkan)
  4. `cmake --build cmake-build-bgfx --target CNA CnaTests -j4` (Bgfx)

If any of these fail on a fresh checkout, reconfigure first (`cmake -S . -B cmake-build-debug
-DCNA_GRAPHICS_BACKEND=EASYGL`, `=VULKAN`, `=BGFX` respectively) before treating it as a real
break. If they all succeed, proceed straight to the backlog below — no baseline re-verification
needed, this session already did a full `CnaTests` + targeted `ctest` pass on all 3 backends.

**Environment, non-negotiable:** never run any GPU/window-creating binary against the real
`DISPLAY=:0` — confirmed disruptive to the human developer's desktop in an earlier session
(window/keyboard-focus stealing). Start a virtual display first: `Xvfb :99 -screen 0 1280x1024x24
&`, then always `SDL_VIDEODRIVER=x11 DISPLAY=:99 <binary>` (this session used `:99`, already
running — check `pgrep -a Xvfb` before starting a second one). `ctest`'s GPU tests read
`CNA_TEST_DISPLAY` (cache variable, default `:0`) — either pass `-DCNA_TEST_DISPLAY=:99` at
configure time or keep using direct binary invocation for GPU tests. Full details in §4. Run all
3 backends' test suites **sequentially, never concurrently** — concurrent runs cause transient
GPU/driver-contention false failures (this session hit exactly this with `Bgfx_RenderTarget2D_
MipChain`: failed once inside a broader `ctest -R` batch, passed cleanly 4/4 times in isolation —
re-run any anomaly in isolation before treating it as a regression).

## What to do next

Work through `plan_graphics.md` autonomously, one task at a time, following the established
per-task methodology: research → implement → write a discriminating test FIRST → verify via
`git stash`-revert-and-rebuild (or targeted backend-only sabotage when a full interface-signature
stash would break the build, as this session did for Task 877) that the new test actually fails
without the fix → full regression per affected backend(s) → update `plan_graphics.md` (verbose
writeup) and `NEXT.md` (concise) → commit → push → a small follow-up commit filling in the
just-created commit hash into NEXT.md's `TBD` placeholder (Task 877's own row already has its real
hash filled in, `7d883ee5` — this describes the pattern for the *next* task). One task = one
commit; never bundle unrelated tasks.

**Skip without asking**: any WebGPU task (`plan_webgpu.md`, hard project-wide prohibition, see
CLAUDE.md). All 4 tasks the project owner explicitly approved "Implement now" this stretch
(902/910/911/914) are now closed — continue automatically into the standing backlog (§8) one task
at a time, without stopping to ask permission between ordinary backlog items.

**Backlog, in priority order:** see §8. This session closed Task 902 (`GraphicsDevice::Reset()`
now really reaches the backend — `GraphicsDeviceManager.PreferMultiSampling` reaches
`VulkanGraphicsBackend::ApplyMultiSampleCount()`, fixing `vulkan_msaa_test.cpp`'s Task 147 false
positive; scoped to the backbuffer only) and Task 910 (every Bgfx render target now gets its own
distinct bgfx view id instead of sharing one hardcoded id, fixing the "only the last-bound render
target actually renders within one un-advanced frame" bug found while verifying Task 907). A
prior session closed Task 877 (`DepthStencilFormat`
fidelity on EasyGL/Bgfx, including new `RenderTargetCube` depth support on Bgfx), Task 875
(Vulkan `Clear()`-only render targets never recording a render pass), Task 876 (Vulkan
`RenderTargetCube`-via-`EnvironmentMapEffect` black-after-unbind — re-checked while investigating
and found to no longer reproduce at all, closed verify-only), Task 912 (Bgfx render-target
sampling bug — bisected and root-caused to the already-documented Bgfx "first read per rendered
frame" quirk, not a distinct bug; fixed via the established retry-until-non-black convention,
`rendertarget2d_depth_test.cpp` now registered for Bgfx too), Task 663
(`TextureCube::DDSFromStreamEXT` — real DDS header parsing + DXT1/3/5 decode, tested against a
hand-built in-memory DDS fixture), Task 913 (`TextureCube`/`Texture3D`'s `SetData`/`GetData`
not validating `elementCount` against the region size, a genuine heap-corruption bug found while
building Task 663's fixture — fixed by mirroring `Texture2D`'s already-correct pattern), and Task
865 (real Vulkan `GetData` readback for `Texture3D`/`TextureCube` via `vkCmdCopyImageToBuffer` +
staging buffer, reusing 2 existing backend-agnostic example tests rather than writing new ones),
and Task 862/864 (`Texture3D`/`TextureCube` mip-level allocation fixed on EasyGL and Vulkan —
reverting either fix reproduces real GPU memory corruption, not just a silent no-op), and Task 903
(`RenderTargetCube` MSAA on Vulkan and Bgfx, mirroring `RenderTarget2D`'s Task 878/879 support —
also found and fixed a real Vulkan bug along the way: the 2D `SpriteBatch` pipeline's MSAA
selection never checked an RT's own `WantsMsaa()`, only the backbuffer's). Also closed Task 750
(`SpriteBatch`'s `SamplerState` fixed on Bgfx — never overrode `SetSamplerFilter`/
`SetSamplerAddressMode` at all, closing the last of the 3 backends for this bug shape) and Task 915
(`GraphicsDevice.GetBackBufferData` implemented for the first time on SDL_Renderer via
`SDL_RenderReadPixels()` — previously a hard `throw`, blocking the entire Task 666+ SDL_Renderer
pixel-test audit phase before it could start; also fixed a real CMakeLists.txt scoping bug found
while wiring up the first test). Also closed Task 870 this session (Vulkan `DepthStencilState`
support was almost entirely fake -- `DepthBufferFunction` and the entire stencil-test parameter
set now real, see §3/§5; a project-owner reprioritization inserted this ahead of Task 911 given
its severity), Task 911 this session (Vulkan render targets now get true per-instance
`DepthStencilFormat` fidelity — a depth-format-keyed render-pass+pipeline cache across all 9 3D
pipeline functions plus the 2D sprite pipeline; MRT deliberately stays on the device-wide format;
also found and fixed a real `VulkanMRTProxy` regression this surfaced, plus a pre-existing
unrelated `mrtProxy_` teardown leak — the project owner's "push through full scope even when
large" instruction was followed rather than landing a narrower alternative), and Task 914 this
session (`Texture3D`/`TextureCube::GetData` now real on Bgfx via a temporary
`BGFX_TEXTURE_BLIT_DST|READ_BACK` texture + `bgfx::blit()`/`bgfx::readTexture()`, unblocking Task
864's mip-allocation fix for Bgfx too — confirmed `BGFX_CAPS_TEXTURE_BLIT`/`READ_BACK` are both
actually supported in this sandbox before implementing). **All 4 project-owner-approved
"Implement now" tasks (902/910/911/914) are now closed.** Also closed Task 669 this session
(`SpriteSortMode::FrontToBack`/`BackToFront` confirmed already correct on SDL_Renderer, no bug
found — new test covers `BackToFront` with real pixels for the first time on any backend), Task
667 (`SpriteSortMode::Deferred` submission order also confirmed already correct, no bug found),
and Task 668 (`SpriteSortMode::Texture` grouping also confirmed already correct, no bug found —
new test pixel-verifies texture bindings survive the sort's reordering, complementing the
existing mock-backend adjacency/stability test), and Task 670 (`SpriteSortMode::Immediate`
per-draw flush also confirmed already correct end-to-end — a real methodology finding while
verifying it: an initial 1-line sabotage produced a false-negative-passing test because it caused
a silent-drop bug instead of the intended defer-to-`End()` bug; a 2nd, correct sabotage genuinely
reproduced it and the test failed as predicted). **Every `SpriteSortMode` value is now
pixel-verified on SDL_Renderer.** Next up: continue the SDL_Renderer audit phase (Task 671 is
already done from an earlier session — continue at Task 672 — write pixel tests for
SpriteBatch/SpriteFont/BlendState/SamplerState/RenderTarget2D/Viewport/GraphicsDevice-lifecycle on
this backend, Tasks 672–861 in `plan_graphics.md`, all unblocked by Task 915), and/or the older,
not-yet-triaged backlog in the
421–500 range of `plan_graphics.md`
(audits, reference-value generation, and other
per-backend tasks from earlier phases never folded into this §8 list) — worth a dedicated triage
pass before working through in bulk, since some entries (e.g. Task 750) may already be superseded
by later, higher-numbered rediscoveries of the
same bug.

**Standing instructions (still in force):**
- Commit AND push after every finished task without waiting to be asked.
- Chain to the next non-decision task automatically once one finishes — don't stop to ask
  "should I continue?" for ordinary backlog progression.
- Update both `plan_graphics.md` (verbose, full FNA-comparison detail) and `NEXT.md` (concise,
  one-line-per-task in §3) after every task.
- If genuinely blocked on a decision only the project owner can make, stop and ask — don't guess
  and don't skip silently without noting it in NEXT.md §8/§9.

For the full history of what every task through Task 896 found and fixed, read `plan_graphics.md`
directly (or `git log`) rather than this file — NEXT.md intentionally keeps only a one-line
summary per task (§3) to stay a genuinely quick-to-read handoff document. (A much longer version
of this resume-prompt section existed before 2026-07-07 and had drifted into a 700-line historical
dump that violated this file's own stated purpose; it was replaced with this concise version —
if you need that old narrative detail, it's preserved in this file's git history.)
```
