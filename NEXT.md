# NEXT.md — CNA Project Handoff

---

> ⛔ **WebGPU is forbidden for now** — do not work on any WebGPU task (Phases 56–69, Tasks
> 10001+ in `plan_graphics.md`) until the project owner explicitly lifts this restriction. See
> `CLAUDE.md` ("WebGPU Is Forbidden For Now").

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
  backends** (Tasks 885 + 897 together).
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
  (`cmake-build-bgfx`): all 3 configured, build cleanly. Last rebuilt/re-verified for Task 665
  (Vulkan; EasyGL last verified Task 420, Bgfx untouched/unverified since Task 416).

### Test status (last verified: Task 665 for Vulkan; Task 420 for EasyGL; Task 416 for Bgfx)
- **EasyGL, full `ctest -j1`:** 3625/3628 pass (as of Task 420). 3 pre-existing/documented
  failures (see §5): `EasyGL_MRT_TwoAttachments`, `easy-gl-resource-smoke-tests`,
  `EasyGL_GraphicsDevice_ReferenceStencil`.
- **Vulkan, full `ctest -j1`:** 3544/3556 pass. Same 12 of the 13 documented pre-existing
  failures as Task 664's run, exact-name-match, zero new regressions.
  `Vulkan_RenderTargetCube_SampleAfterUnbind` (Task 876's own already-documented flaky failure)
  again passed this run, consistent with its known non-deterministic nature.
- **Bgfx, full `ctest -j1`:** 3525/3525 pass (as of Task 416) — 100%, no flakes.
- **Caution:** run all 3 backends' full `ctest` suites **sequentially, never concurrently** —
  concurrent runs previously produced transient GPU/driver-contention false failures. If a single
  run shows an anomaly beyond the documented list, re-run that test in isolation before treating it
  as a regression.

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
  INTO a render target works on EasyGL/Vulkan (Task 335), though exact `DepthStencilFormat` isn't
  respected on any backend (Task 877). Mip chains (Task 336) and MSAA (Task 337) are genuinely
  functional on **EasyGL only** — Vulkan/Bgfx report the correct properties but don't back them
  with real GPU resources yet (Tasks 878/879).
- `SetRenderTarget`/`SetRenderTargets` correctly reset `Viewport`/`ScissorRectangle` to the newly
  bound target's size on all 3 backends (Task 338) — `ScissorRectangle`'s reset has a real,
  pixel-verified GPU effect; `Viewport`'s GPU effect is still a no-op everywhere (Task 880).
- `Effect`/`EffectTechnique`/`EffectPass`/`EffectParameterCollection` base-class contract now
  matches FNA closely (Phase 41, Tasks 351–360): technique/pass validation, lookup-by-name/semantic
  semantics, enumeration order, and dispose/lifecycle behavior all verified or fixed to match FNA.
  `Effect::Clone()` still doesn't exist (Task 883).
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
  and real specular remain unimplemented, tracked as Tasks 885/886.
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
  `AlphaTestEffect`/`BasicEffect`'s shared per-stride shaders); both are now fixed. Vulkan/Bgfx
  still have zero fog GPU implementation for any 3D effect (Task 888).
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

### What does NOT work yet
- **Vulkan `BlendState`/`DepthStencilState` support is almost entirely fake** — hardcoded blend
  equations / depth-compare ops / no stencil testing at all, regardless of what's requested.
  Tracked as Task 868/870, confirmed repeatedly via pixel tests, not fixed (large,
  multi-pipeline-site changes).
- `GraphicsDevice.ReferenceStencil`'s independent-override behavior has zero backend connection on
  all 3 backends (Task 872). `GraphicsDevice::Clear` ignores `ClearOptions::Stencil` everywhere
  (Task 871).
- `RenderTarget2D`/`RenderTargetCube`'s mip chains and MSAA don't produce real GPU resources on
  Vulkan/Bgfx (Tasks 878/879 — fixed on EasyGL already).
- `GraphicsDevice.Viewport` has **zero GPU backend wiring on all 3 backends** — every backend
  hardcodes its actual viewport call to the full render-target/window size regardless of what
  `Viewport` is set to. A sub-region viewport (split-screen, atlas-subrect rendering) currently has
  no effect anywhere. Tracked as Task 880, not fixed.
- **`GraphicsDevice`'s default `RasterizerState` (`CullCounterClockwiseFace`, matches FNA) is never
  pushed to any backend's actual GPU state at construction** — Bgfx's own hardcoded default happens
  to be the only one of the 3 that matches FNA's, so it's the only backend that silently culls a
  standard-winding full-screen quad unless `RasterizerState::CullNone` is set explicitly (Task 896;
  found this session under Task 364, previously mis-tracked as part of Task 884, which is now
  closed and covers only the Effect-collection fix below). **Deliberately not fixed** — its
  correct fix has a much larger blast radius than it looks (128 EasyGL/Vulkan example files never
  set `RasterizerState` at all and rely on those backends' incorrect no-culling default), needs a
  scoping decision before it can safely land.
- `SetRenderTargets`'s simultaneous-target cap doesn't match FNA's real `MAX_RENDERTARGET_BINDINGS
  = 4`: EasyGL/Bgfx silently cap at 8, Vulkan has no CNA-level cap at all (Task 881).
- `EasyGL_MRT_TwoAttachments` (Task 145): even a basic, same-size/format 2-target MRT setup doesn't
  render correctly on EasyGL — attachment 1 stays black. Pre-existing, off-limits for opportunistic
  fixing (see §9).
- `Texture3D`/`TextureCube::GetData` is a total silent no-op on Vulkan/Bgfx (Task 865).
  `TextureCube::DDSFromStreamEXT` is a non-functional stub (Task 663).
- `Texture2D::SetData(level>0,...)` is a silent no-op on Vulkan/Bgfx; EasyGL's non-mip-aware
  filters render solid black on mip-incomplete textures (Task 867).
- `SpriteBatch`'s `SamplerState` is a no-op on Bgfx (fixed on Vulkan by Task 665; EasyGL already
  correct).
- `Texture3D` sampling cannot be wired into any shader without an architecture change (Task 863).
- Bgfx: `SpriteBatch::Draw`ing a `RenderTarget2D`/`RenderTargetCube` reads the wrong handle type via
  an invalid `static_cast` — samples wrong/garbage data, doesn't crash (Tasks 873/874).
- Vulkan: `SetRenderTarget(rt); Clear(color); SetRenderTarget(nullptr);` with no draw call never
  gets a render pass recorded (Task 875). Sampling a `RenderTargetCube` via `EnvironmentMapEffect`
  after unbinding renders black instead of actual content, root cause not isolated (Task 876).
- No backend honors the exact requested `DepthStencilFormat` for a render target's depth/stencil
  attachment (Task 877) — the depth-TEST functionality itself works (Task 335), this is format
  fidelity only.
- `BasicEffect::FillGpuDrawParams()` still only forwards `DirectionalLight0` (never `SpecularColor`/
  `SpecularPower`/`DirectionalLight1`/`DirectionalLight2`), and the **lit path** still omits
  `+EmissiveColor` (the disabled-lighting path was fixed in Task 369) — real mismatches vs. FNA,
  invisible in Tasks 364–368 since those cases leave the affected properties at their defaults.
  No specular infrastructure exists anywhere for `BasicEffect` on any backend. Tracked as new
  Tasks 885 (lit-path emissive + multi-light forwarding) and 886 (real specular highlights),
  opened by Task 369's audit — both need their own dedicated task (886 in particular is a new
  feature, not a bug fix; 885's Vulkan half needs a shared push-constant budget change).

---

## 3. Recent changes

Most recent first. Full detail (exact FNA-derived formulas, discriminating-power verification,
per-backend fix shape) is in `plan_graphics.md` — this table is intentionally a one-line-per-task
index, not a duplicate.

| Commit | Task | Summary |
|---|---|---|
| — | 897 | **Real gap found and FIXED — Vulkan half of Task 885, closing the `BasicEffect` lit-path gap on all 3 backends.** Gave the lit-textured (stride-32) pipeline its own dedicated descriptor-set/pipeline-layout/UBO-ring-buffer (`descriptorSetLayoutLitTextured_`/`pipelineLayoutLitTextured3D_`/`litTexturedUBO_`), mirroring `EnvironmentMapEffect`'s own small-UBO pattern exactly, since the shared 128-byte push constant (still used unchanged for strides 20/24/`Instanced3D`) had zero spare room. New `Vulkan_BasicEffect_MultiLightEmissive` test (port of Task 885's EasyGL test) passed on the first attempt with identical expected values; discriminating power confirmed via `git stash` revert-and-refail — pre-fix, identical `(89,13,13)` result to EasyGL/Bgfx's own pre-fix runs. Full serial Vulkan regression 3546/3559 pass, all 13 failures independently reconfirmed pre-existing (identical failures with this task's changes reverted). |
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
Vulkan's `BlendState`/`DepthStencilState` support (Tasks 868/870), `TextureCube::DDSFromStreamEXT`
(Task 663), `Texture3D`/`TextureCube::GetData` on Vulkan/Bgfx (Task 865), `RenderTarget2D`'s
mip/MSAA params accepted but not wired on Vulkan/Bgfx (Tasks 878/879), and `BasicEffect`'s
lit-path missing `+EmissiveColor`/unforwarded specular+extra-lights terms (found Task 366, fixed
for the no-lighting path in Task 369, lit path tracked as Tasks 885/886).
None have a test that currently fails loudly — they're only visible via dedicated pixel tests or
direct code reading.

---

## 5. Known bugs and limitations

| Status | Issue | Tracking |
|---|---|---|
| Confirmed, MASSIVE, not fixed | Vulkan's `BlendState` support is almost entirely fake — hardcodes one blend equation regardless of request. Confirmed 5× via pixel tests. EasyGL fully correct. | Task 868 |
| Confirmed, MASSIVE, not fixed | Vulkan's `DepthStencilState` support is almost entirely fake — `DepthBufferFunction` hardcoded, entire stencil-test parameter set unused. Confirmed 5× via pixel tests. EasyGL fully correct. | Task 870 |
| Confirmed, universal, not fixed | `GraphicsDevice.ReferenceStencil`'s independent-override has zero backend connection on all 3 backends. | Task 872 |
| Confirmed, universal, not fixed | `GraphicsDevice::Clear` ignores `ClearOptions::Stencil` on every backend. | Task 871 |
| Fixed on EasyGL, not fixed on Vulkan/Bgfx | `RenderTarget2D`/`RenderTargetCube`'s `mipMap` produces a real, pixel-verified mip chain on EasyGL (Task 336); Vulkan/Bgfx don't yet allocate/generate real GPU mips. | Task 878 |
| Fixed on EasyGL, not fixed on Vulkan/Bgfx | `RenderTarget2D`/`RenderTargetCube`'s MSAA produces a real, pixel-verified anti-aliased resolve on EasyGL (Task 337); Vulkan/Bgfx honestly report `MultiSampleCount=0`. | Task 879 |
| Confirmed, universal, not fixed | `GraphicsDevice.Viewport` is decorative — no backend actually applies it to the GPU; every backend hardcodes the full render-target/window size instead. | Task 880 |
| Confirmed, severe, silent failure | `TextureCube::DDSFromStreamEXT` ignores its stream argument, always returns a blank 1×1 texture. | Task 663 |
| Confirmed, severe, silent failure | `Texture3D`/`TextureCube::GetData` total no-op on Vulkan/Bgfx. | Task 865 |
| Confirmed, silent failure | `Texture2D::SetData(level>0,...)` no-op on Vulkan/Bgfx; EasyGL renders solid black for mip filters on mip-incomplete textures. | Task 867 |
| Confirmed, architectural, not fixed | `Texture3D`/`TextureCube` can't be sampled in any shader — don't inherit `Texture`. | Task 863 |
| Confirmed, severe, silent failure, not fixed | Bgfx: `SpriteBatch::Draw`ing a `RenderTarget2D` reads a framebuffer handle where a texture handle is expected — samples wrong data, doesn't crash. | Task 873 |
| Confirmed, severe, silent failure, not fixed | Bgfx: same bug shape as Task 873 for `RenderTargetCube` via `EnvironmentMapEffect`. | Task 874 |
| Confirmed, real, not fixed | Vulkan: `SetRenderTarget`+`Clear()` with no draw call never records a render pass — target's image stays `VK_IMAGE_LAYOUT_UNDEFINED` forever. | Task 875 |
| Confirmed, real, not fixed, root cause not isolated | Vulkan: sampling a `RenderTargetCube` via `EnvironmentMapEffect` after unbinding renders black instead of actual content. | Task 876 |
| Confirmed, format-fidelity gap, not fixed | No backend honors the exact requested `DepthStencilFormat` for a render target's depth/stencil attachment. Core depth-test functionality itself works (Task 335). | Task 877 |
| Confirmed, architectural, deliberate | `GraphicsDevice` stores state objects by value, unlike FNA's reference-type aliasing. No game code here relies on FNA's behavior. | Task 869 |
| Confirmed, incomplete | `SpriteBatch`'s `SamplerState` (`Begin()`) is a no-op on Bgfx (fixed on Vulkan by Task 665; EasyGL already correct). | — |
| Confirmed, pre-existing | `EasyGL_MRT_TwoAttachments`: attachment 1 stays black with 2 render targets. | Task 145 |
| Confirmed, minor, not fixed | `SetRenderTargets`'s simultaneous-target cap doesn't match FNA's real `MAX_RENDERTARGET_BINDINGS=4`. | Task 881 |
| Confirmed, incomplete | `PresentationMode::Letterbox`/`Overscan`/`Stretch`/`NativeBackBuffer` aren't distinctly implemented on EasyGL; Vulkan/Bgfx implement no virtual-resolution scaling at all. | Task 882 (not yet a formal `plan_graphics.md` row — referenced inline in Task 348) |
| Confirmed, pre-existing, out-of-repo | `easy-gl-resource-smoke-tests` aborts on an internal assert in the sibling `easy-gl` repo. | — |
| Confirmed, pre-existing | `Vulkan_DepthBias`'s `DepthBias=-1e6` sub-case fails; other sub-cases pass. | — |
| Confirmed, pre-existing, flaky | `Vulkan_FillMode_WireFrame`/`Vulkan_RenderTargetUsage`: order-dependent, only one fails per full-suite run. | — |
| Confirmed, architectural, not fixed | `GraphicsDevice`'s default `RasterizerState` is never pushed to any backend's actual GPU state at construction; Bgfx's hardcoded default happens to be the only one matching FNA's, so it alone silently culls standard-winding quads unless `CullNone` is set explicitly. Fixing this correctly has a large, unaudited blast radius (~128 EasyGL/Vulkan example files never set `RasterizerState` and rely on those backends' incorrect no-culling default) — needs a scoping decision before it can land. | Task 896 |
| Confirmed, architectural, not fixed | `Effect::Clone()` doesn't exist — needs an ownership-model decision plus fixing an `EffectPass::Apply()` owner-aliasing hazard plus `Clone()` overrides in all 7 stock effects. | Task 883 |
| Confirmed, real, not fixed | `BasicEffect::FillGpuDrawParams()` never forwards `SpecularColor`/`SpecularPower` on any backend. No specular infra exists anywhere (`DirectionalLight1`/`DirectionalLight2`/lit-path `EmissiveColor` forwarding fixed on all 3 backends by Tasks 885/897). | Task 886 |
| Confirmed, real, not fixed (empirically verified) | `AlphaTestEffect.VertexColorEnabled` has **zero effect on Vulkan or Bgfx** — their alpha-test pipeline/shader never declares a color vertex attribute at all, and this pipeline is used by default (`AlphaFunction=Greater`/`ReferenceAlpha=0` already trigger it). Correct on EasyGL (reuses `BasicEffect`'s already-fixed stride-24 shader). | Task 887 |
| Confirmed, project-wide, not fixed | **Fog is a total GPU no-op on Vulkan and Bgfx for every 3D effect** — grepped every shader file in both backends for "fog", zero matches anywhere. Affects `BasicEffect` too (its `FillGpuDrawParams()` already forwards fog correctly; only the GPU side is missing). EasyGL already has fog fully working generically (confirmed for `BasicEffect` since Task 195, and `AlphaTestEffect` since Task 378). | Task 888 |
| Confirmed, real, not fixed | `DualTextureEffect.VertexColorEnabled` has **zero effect on all 3 backends** — every backend's dual-texture dispatch is a dedicated shader/pipeline declaring only `position`+`texcoord` inputs (Vulkan explicitly reuses the generic textured-only vertex shader; Bgfx hardcodes `v_color0` to the diffuse uniform, not a real per-vertex attribute). Found while writing Task 389's capstone test — Phase 44 never had a dedicated audit task for this property, unlike `AlphaTestEffect`'s Task 377. | Task 889 |
| Confirmed, real, not fixed | `EnvironmentMapEffect::FillGpuDrawParams()` only forwards `DirectionalLight0` — `DirectionalLight1`/`DirectionalLight2` silently ignored on all 3 backends. Confirmed this effect shares `BasicEffect`'s identical `Lighting.fxh`/`ComputeLights` mechanism in real FNA (same `oneLight` shader-variant optimization), so the same gap and likely the same fix plumbing as Task 885 applies here too. | Task 890 |
| Confirmed, real, not fixed | `EnvironmentMapEffect`'s base cube-map lerp target (`envColor`) is not scaled by combined texture×diffuse alpha on any backend; FNA's real formula (`envmap = SAMPLE_CUBEMAP(...) * color.a`) scales both `envmap.rgb` (base lerp, still unscaled) and `envmap.a` (specular term, fixed by Task 395). Only visible when texture/diffuse alpha is strictly less than 1 — every existing test used opaque textures/diffuse colors. | Task 891 |
| Confirmed, real, worse, not fixed | `BasicEffect`'s lit-textured Bgfx shader (`vs_lit_textured3d.sc`) transforms the vertex normal by the full `World×View×Projection` matrix, not even `World` alone — geometrically meaningless for a direction vector. Invisible in every existing test since all leave `View`/`Projection` at `Identity`. | Task 892 |
| Confirmed, minor, acceptable deviation | `EnvironmentMapEffect`'s Fresnel edge-weighting (Task 396 fix) is computed per-pixel in CNA vs. FNA's real per-vertex (then rasterizer-interpolated) computation — identical on flat/coarse-normal test geometry, but could look subtly different from FNA on sparsely-tessellated curved surfaces at silhouette edges (CNA's per-pixel version is strictly more accurate, not less). | — |
| Confirmed, real, not fixed | `SkinnedEffect`'s `DirectionalLight1`/`DirectionalLight2` are silently ignored by every backend's GPU dispatch, same shape as `BasicEffect`/`EnvironmentMapEffect`'s already-tracked gaps. | Task 893 |
| Confirmed, real, not fixed | `SkinnedEffect`'s `SpecularColor`/`SpecularPower` have zero GPU implementation on any backend — `GpuDrawParams` has no generic specular fields at all, same shape as `BasicEffect`'s already-tracked gap. | Task 894 |
| Confirmed, real, not fixed | `SkinnedEffect.WeightsPerVertex` is a complete GPU no-op on all 3 backends — the skinning shader always sums all 4 bone weights regardless of the property's value (1/2/4), unlike FNA's real shader which only sums the first N. Only visible when unused weight slots hold nonzero data. | Task 895 |
| Confirmed, minor, acceptable deviation | `SkinnedEffect`'s `PreferPerPixelLighting=false` default is effectively a no-op — lighting (`NdotL`) is always computed in the fragment shader on every backend, so CNA always renders at per-pixel quality regardless of this flag (strictly more accurate than FNA's real per-vertex default, never worse). | — |
| Suspected, not reproduced | Vulkan/Bgfx likely have the same mip-allocation bug already fixed on EasyGL's `TextureCube` (Task 276), for `Texture3D`/`TextureCube` on both backends. | Task 864 |
| Needs verification | Whether Bgfx's window actually has a physical stencil buffer has not been checked. | — |
| Incomplete, by design | Stride-keyed vertex layout only supports strides 16/20/24/32/52. Vulkan has no `Tangent`/`Binormal` mapping. `SurfaceFormat` support is Color-only for real GPU formats. `SDL_Renderer` has no 3D at all. | — |
| Risky assumption | `GraphicsDevice`'s user-primitive scratch buffers never shrink — fine for typical use, but memory stays at the high-water mark for the device's lifetime. | — |
| Confirmed, test-harness only, worked around | Bgfx's `GetBackBufferData()` only reliably reflects the *first* read call per rendered frame — reading multiple distinct rectangles from a single frame returns stale/blank data for reads after the first. Every multi-point Bgfx pixel test in this project reads exactly one rectangle per draw+retry pass as a result (Task 406 established a `renderAndRead()`-style per-checkpoint helper for new tests). | — |

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
`EffectPassCollection` dangling-pointer hazard), Task 885, and Task 897 (`BasicEffect` lit-path
`DirectionalLight1`/`2`/`EmissiveColor`, now fixed on **all 3 backends**) are also done. In
priority order, the rest are the accumulated backlog from earlier phases (Tasks 825–828, 863–882,
886, 887–896).

1. **Task 883 — implement `Effect::Clone()`** (needs: C++ ownership-model decision, fixing the
   `EffectPass::Apply()` `owner_`-aliasing hazard on clone, `Clone()` overrides in all 7 stock
   effects). Files: `Effect.hpp`/`.cpp` + all 7 stock-effect pairs.

2. **Task 896 — fix the `RasterizerState`-default GPU-sync gap** (needs: a scoping decision —
   the architecturally correct fix is a one-line `GraphicsDevice` ctor change, but its blast
   radius is ~128 EasyGL/Vulkan example files that never set `RasterizerState` and currently rely
   on those backends' incorrect no-culling default; landing it needs either a winding-order audit
   of those files or an explicit decision on how to sequence that cleanup first). Files:
   `GraphicsDevice.cpp` (ctor), potentially many `examples/*.cpp` files.

3. **Task 886 — implement real specular highlights for `BasicEffect`** (opened by Task 369; a new
   feature, not a bug fix — zero specular infrastructure exists today). Needs world-space-position
   varyings, eye-position uniform (reuse `EnvironmentMapEffect`/`SkinnedEffect`'s
   `Matrix::Invert(view).Translation` technique), half-vector math, and `SpecularColor`/
   `SpecularPower` forwarding, all 3 backends. On Vulkan, the lit-textured pipeline now has its own
   dedicated descriptor-set/UBO (`descriptorSetLayoutLitTextured_`/`litTexturedUBO_`, Task 897) with
   spare room in the same `LitLightParams` UBO shape — extending it for specular data is now a
   much smaller, already-unblocked follow-on.

4. **Task 887 — fix `AlphaTestEffect.VertexColorEnabled` being ignored on Vulkan/Bgfx** (opened by
   Task 377; true by default, not an edge case). Needs unifying Vulkan/Bgfx's alpha-test dispatch
   with their already-correct per-stride textured/colored-textured pipelines (mirror EasyGL's
   architecture) — a large, multi-shader-file (6 files, 2 backends), multi-dispatch-site change.
   Files: `alpha_test3d.vert/frag.glsl` + `colored_textured3d`/`textured3d`/`lit_textured3d`
   (Vulkan); `vs/fs_alpha_test3d.sc` + Bgfx equivalents; both backends' draw-dispatch code.

5. **Task 888 — implement real fog rendering on Vulkan and Bgfx** (opened by Task 378; a
   project-wide gap, not `AlphaTestEffect`-specific — zero shader files in either backend
   implement fog at all, for any effect, though the C++ side already forwards the fields
   correctly for `BasicEffect`). Needs fog uniforms/varyings + blend formula in ~8 shader pairs ×
   2 backends. Likely comparable in size to Task 868/870's Vulkan `BlendState` work.

6. **Task 881 — cap `SetRenderTargets` at FNA's real `MAX_RENDERTARGET_BINDINGS=4`.**
   Files: `GraphicsDevice.cpp` (`SetRenderTargets`). Verification: 5-target call throws, 1–4 work.

7. **Task 880 — wire `GraphicsDevice.Viewport` to a real GPU viewport on all 3 backends.**
   Files: `IGraphicsBackend.hpp`, `GraphicsDevice.cpp`, all 3 backends' graphics-backend `.cpp`.
   Verification: sub-region-viewport pixel test (should fail on all 3 backends today).

8. **Task 878/879 — implement real mip/MSAA render-target support on Vulkan and Bgfx**, mirroring
    Task 336/337's exact EasyGL fix shape. Files: each backend's render-target backend classes.

9. **Task 877 — wire `DepthStencilFormat`'s exact value into render-target depth/stencil
    attachments** on all 3 backends (currently hardcoded/coarse choices).

10. **Task 875/876 — Vulkan render-target bugs**: `Clear()`-only draws never record a render pass
    (875); `RenderTargetCube` via `EnvironmentMapEffect` renders black after unbind, root cause not
    isolated (876, needs isolation before a fix is attempted — see §9).

11. **Task 873/874 — fix Bgfx's wrong-handle-type `static_cast`s** for `RenderTarget2D`/
    `RenderTargetCube` sampling. Files: `BgfxGraphicsBackend.hpp`/`.cpp`.

12. **Task 663 — implement `TextureCube::DDSFromStreamEXT` for real** (build a real DDS cube-map
    test fixture *first*, then implement against it).

13. **Task 865 — implement real Vulkan `GetData` readback for `Texture3D`/`TextureCube`**
    (`vkCmdCopyImageToBuffer` + staging buffer, mirroring the existing upload path in reverse).

14. **Task 864 — reproduce and fix the suspected Vulkan/Bgfx mip-allocation bug** for
    `Texture3D`/`TextureCube` (confirm with a failing test first, per Task 276's methodology).

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
- **No rushed specular implementation for Task 886** — it's a new feature (zero existing
  infrastructure), not a bug fix; needs its own dedicated design pass for world-space-position
  varyings and half-vector math across all 3 backends. Vulkan's lit-textured descriptor-set/UBO
  infrastructure now exists (Task 897) and has room to extend, but the specular math itself is
  still unbuilt on any backend.
- **No further changes to the `GraphicsDevice` user-primitive scratch buffers** without re-running
  the full `DrawUserPrimitives`/`DrawUserIndexedPrimitives` pixel-readback suite.
- **No API renames or namespace moves** — XNA API names and shapes are frozen by the FNA reference.
- **No mass Doxygen or NOXNA cleanup passes** — fix tags only on files you're already touching for
  a real reason.
- **No opportunistic fix for Task 868/870 (Vulkan blend/depth-stencil state)** bundled into an
  unrelated task — both are large, multi-pipeline-site changes; each needs its own dedicated task.
- **No opportunistic fix for Task 871/872 (stencil `Clear`/`ReferenceStencil` backend gaps)** —
  verify with a real test first.
- **No rushed fix for Task 878/879 (Vulkan/Bgfx render-target mip/MSAA)** — mirror Task 336/337's
  exact EasyGL fix shape and verify with the same pixel-differential methodology (ported to
  Vulkan/Bgfx) before declaring it fixed.
- **No rushed fix for Task 873/874 (Bgfx handle-cast bugs)** — verify structurally (extracted handle
  equals the colour-texture handle, not the framebuffer handle) since Bgfx has no pixel readback
  for these two specifically.
- **No fix for Task 875/876 (Vulkan render-target bugs)** without isolating the root cause first —
  Task 876 especially has 2 unisolated candidates (see `plan_graphics.md`).
- **No opportunistic fix for Task 877 (DepthStencilFormat fidelity)** bundled into an unrelated
  task — verify with a dedicated stencil-in-RT pixel test first.
- **No rushed fix for Task 880 (Viewport GPU wiring)** — write the sub-region-viewport pixel test
  FIRST (it should fail on all 3 backends today) before attempting any backend wiring.
- **No opportunistic fix for Task 145 (`EasyGL_MRT_TwoAttachments`)** bundled into Task 881 or any
  MRT-adjacent task — it needs its own dedicated root-cause investigation.
- **No rushed fix for Task 881 (MRT count cap mismatch)** without a real unit test proving both the
  throw-past-4 behavior and that 1–4 targets still work.
- **No implementing `Effect::Clone()` (Task 883) as a side effect of an unrelated Phase 42 task** —
  it needs its own dedicated task given the ownership-model decision and the `owner_`-aliasing fix
  it requires.

---

## 10. Resume prompt

```
Read NEXT.md first. Inspect only the files needed for the first task in §8 (Task 883).
Do not refactor unrelated code. Make one small, verified improvement.
Run the relevant build/test command before declaring the task done.
Update NEXT.md and plan_graphics.md after finishing, then commit AND push (standing
instruction — do not wait to be asked; one task = one commit = one push).

Current status: Phases 1-47 are FULLY COMPLETE (Tasks 664/665 closed Phase 47's own 2 real bugs).
Task 884 (EffectParameterCollection/EffectPassCollection dangling-pointer hazard) is also DONE.
Task 885 + Task 897 together fully fixed BasicEffect's lit-path DirectionalLight1/2 +
EmissiveColor forwarding gap on ALL 3 backends (885 = EasyGL/Bgfx, 897 = Vulkan's new dedicated
descriptor-set/UBO infrastructure for the lit-textured pipeline). Task 896 (RasterizerState-
default GPU-sync gap, split out of Task 884's old bundled title) is OPEN but deliberately not
started -- needs a scoping decision first (see NEXT.md Task 896 note in section 8). Task 883 is
next in the backlog but also needs a decision -- skip it too if still autonomous; Task 886 (real
specular highlights for BasicEffect) is the next well-defined non-decision task if one is wanted
instead. Task 411 (opener) built the mock/recording
ISpriteBatchBackend infrastructure Tasks 412-416 depend on -- audited SpriteBatch's architecture
first (it already has a dedicated ISpriteBatchBackend interface; flushBatch()'s stable_sort by
layerDepth/texture-pointer followed by flushSingle()'s backend_->Draw(s.texture->GetBackend(),...)
is the single dispatch point every sort-mode test needs to observe). FOUND 2 real gaps blocking
any mock-based test, both FIXED as infrastructure prerequisites: (1) SpriteBatch had NO injection
seam -- SpriteBatch(GraphicsDevice&) always requires a real GPU backend, and the existing
SpriteBatch() default ctor leaves backend_ permanently null with no setter. FIXED by adding a new
NOXNA constructor SpriteBatch(unique_ptr<ISpriteBatchBackend>) bypassing GraphicsDevice entirely
(GraphicsResource(nullptr), matching the default ctor's own precedent). (2) flushSingle()
dereferences Texture2D::GetBackend() (*backend_), but the existing
Texture2D::CreateCpuOnlyForTests() leaves backend_ null by design -- a crash the moment any
queued sprite is flushed. FIXED by adding a new NOXNA factory
Texture2D::CreateWithBackendForTests(w, h, shared_ptr<ITextureBackend>), mirroring
CreateCpuOnlyForTests's exact established pattern. Built the actual mock:
tests/Microsoft/Xna/Framework/Graphics/RecordingSpriteBatchBackend.hpp (new shared test-utility
header, mirrors the project's existing *TestAccess.hpp convention) with DummyTextureBackend
(minimal ITextureBackend) and RecordingSpriteBatchBackend (records every Begin/End/Draw call, in
order). Added 5 new tests proving the injection point + mock work end-to-end: construction
doesn't throw, Begin()/End() dispatch correctly, a Draw() call is recorded with byte-exact
deliberately-non-default parameters (rotation/origin/color/layerDepth, so the assertion is
genuinely discriminating), and 2 distinct Texture2D instances produce 2 distinct recorded backend
pointers (the precondition Task 414's SpriteSortMode::Texture grouping assertion depends on).
Deliberately left Tasks 412-416's own per-SpriteSortMode assertions (Immediate flush timing,
Deferred order preservation, Texture grouping, FrontToBack/BackToFront ordering) out of scope --
this task's job was only proving the shared infrastructure itself works. No existing production
behavior changed; only new NOXNA test-only injection points added.

Task 412 fulfilled Task 161's exact ask -- verify-only, zero bugs found. FNA/XNA's contract for
SpriteSortMode::Immediate is that each sprite is submitted the instant Draw() is called, not
queued and flushed at End() like every other mode -- this must be observable BEFORE End() is ever
called. Added ImmediateFlushesInsideDrawBeforeEnd (asserts rec->drawCalls.size()==1 immediately
after Draw(), strictly before End(), then stays at 1 after End() -- no double-flush),
ImmediateFlushesEachDrawSeparatelyInCallOrder (2 distinct textures, count increments by exactly 1
per Draw() call), and DeferredDoesNotFlushBeforeEnd (negative control: proves the assertion
methodology is genuinely discriminating, not spuriously passing regardless of sort mode).
Independently verified discriminating power: temporarily replaced pushSprite()'s
`if (sortMode_ == SpriteSortMode::Immediate)` with `if (false)` and rebuilt -- both Immediate
tests failed exactly as predicted (count read 0 instead of 1 before End()), while the Deferred
negative control correctly kept passing; reverted and reconfirmed all 8 SpriteBatch mock-backend
tests green. No production code changed.

Task 413 fulfilled Task 162's exact ask -- verify-only, zero bugs found. FNA/XNA's contract for
the default SpriteSortMode::Deferred is: no sort at all, sprites delivered in exactly their
original Draw() submission order. Read flushBatch() directly: it stable_sorts for
BackToFront/FrontToBack/Texture only, Deferred deliberately falls through to plain insertion-order
iteration with no else branch at all. Added DeferredPreservesSubmissionOrder: 3 distinct textures
drawn in an order (C, A, B) deliberately chosen not to coincide with any plausible accidental sort
key, asserting the recorded calls arrive in that exact order after End(). Independently verified
discriminating power: temporarily added an unconditional std::reverse(spriteQueue_.begin(),
spriteQueue_.end()) right before the flush loop and rebuilt -- the test failed exactly as
predicted (recorded order came back reversed, B,A,C instead of C,A,B); reverted and reconfirmed
all 9 SpriteBatch mock-backend tests green. No production code changed.

Task 414 fulfilled Task 163's exact ask -- verify-only, zero bugs found. FNA/XNA's contract for
SpriteSortMode::Texture is: sprites grouped by texture (minimize GPU texture-bind changes), sorted
by raw texture reference. flushBatch() implements this as a POINTER comparison
(a.texture < b.texture), so which texture sorts first depends on runtime addresses, not something
a test can predict -- designed the test around only the 2 properties that ARE part of the
contract regardless of address ordering: (1) draws sharing a texture end up adjacent, and (2) they
keep their original relative submission order (stable_sort's stability). Added
TextureGroupsDrawsByTextureAndPreservesGroupOrder: 2 distinct textures A/B drawn interleaved
(A, B, A), asserting the 2 A-entries end up adjacent (regardless of which group sorts first) and
in original order (X=1 before X=3, marker fields unrelated to sorting). Independently verified
discriminating power: temporarily disabled the Texture sort branch and rebuilt -- the test failed
exactly as predicted (the interleaved B-draw stayed between the 2 un-grouped A-draws); reverted
and reconfirmed all 10 SpriteBatch mock-backend tests green. No production code changed. This
run's full Vulkan regression also hit 3 unrelated flakes (CueTest audio-timing,
2 NetworkSessionTest cases) -- all reran individually and passed, confirming pre-existing
flakiness, not a regression from this change.

Task 415 fulfilled Task 164's exact ask -- verify-only, zero bugs found. FNA/XNA's contract for
SpriteSortMode::FrontToBack is: sprites sorted by ASCENDING layerDepth (smaller depth = closer to
camera = drawn first). Added FrontToBackSortsByAscendingLayerDepth: 3 draws at depths 0.5, 0.1,
0.9 (plan_graphics.md's own Task 164 example, deliberately out of order), each with a dest-rect X
marker mirroring its depth*100 (50/10/90); asserts the recorded calls arrive in ascending order
0.1 (X=10), 0.5 (X=50), 0.9 (X=90). Independently verified discriminating power: temporarily
disabled the FrontToBack sort branch and rebuilt -- the test failed exactly as predicted (order
reverted to raw submission order 0.5, 0.1, 0.9); reverted and reconfirmed all 11 SpriteBatch
mock-backend tests green. No production code changed. Vulkan's own 3 extra flakes from Task 414's
run (CueTest audio-timing, 2 NetworkSessionTest cases) did not reproduce this run.

Task 416 fulfilled Task 165's exact ask, CLOSING the per-SpriteSortMode mock-backend test arc
(Tasks 412-416) -- verify-only, zero bugs found. The mirror image of Task 415: FNA/XNA's contract
for SpriteSortMode::BackToFront is sprites sorted by DESCENDING layerDepth (larger depth = farther
from camera = drawn first). Added BackToFrontSortsByDescendingLayerDepth, directly reusing Task
415's exact 3 draws (same depths 0.5, 0.1, 0.9, same dest-rect X markers) with only the sort mode
and expected order reversed, asserting descending order 0.9 (X=90), 0.5 (X=50), 0.1 (X=10).
Independently verified discriminating power: temporarily disabled the BackToFront sort branch and
rebuilt -- the test failed exactly as predicted (order reverted to raw submission order); reverted
and reconfirmed all 12 SpriteBatch mock-backend tests green. No production code changed.

Task 417 (first real GPU pixel test in Phase 47) verified rotation around origin -- verify-only,
zero bugs found. Read FNA's real SpriteBatch.cs GenerateVertexInfo formula directly: origin is
subtracted from each source-space corner BEFORE rotation, so the origin point itself always maps
to exactly (destinationX, destinationY), invariant under rotation -- the defining property of a
true pivot. Read CNA's EasyGL Draw() implementation directly: algebraically identical to FNA's
formula, term-for-term. Designed a discriminating test: 100x100 texture (top-left 20x20=Red
marker, rest=Blue) at destinationRectangle=(200,150,100,100), origin=(100,100) (source's own
bottom-right corner, diagonally opposite the marker), rotated 90 degrees. Hand-derived expected
marker position from FNA's formula: (290,60) -- distinct from both the unrotated position
(110,60) and the origin's own fixed point (200,150). All 3 checks (marker at (290,60)=Red, sprite
interior (250,100)=Blue, outside entirely (50,50)=clear) passed with exact predicted values on
the first attempt. Independently verified discriminating power: temporarily hardcoded
ox=oy=0 in EasyGLGraphicsBackend.cpp (simulating origin-ignored-during-pivot bug) and rebuilt --
2 of 3 checks failed exactly as predicted; reverted and reconfirmed. No production code changed.
Added examples/easygl_spritebatch_rotation_test.cpp. EasyGL-only task, no shared code touched,
Vulkan/Bgfx unaffected/unverified.

Task 418 verified SpriteBatch::Draw's scalar and Vector2 scale overloads -- verify-only, zero
bugs found. Both compute destRect=(position.X, position.Y, sourceRect.Width*scale.X,
sourceRect.Height*scale.Y) (scalar overload forwards Vector2(scale,scale)). Designed a
discriminating test: 20x20 solid-Red texture drawn twice against Green -- scalar overload
(pos=(50,50), scale=3.0 -> expected 60x60, x:[50,110) y:[50,110)) and Vector2 overload with
NON-UNIFORM scale (pos=(200,50), scale=(2,4) -> expected 40x80, x:[200,240) y:[50,130)). Check
points chosen to discriminate 2 axis-mixing bugs: (220,110) is inside the correct 40x80 box but
would read background under a "scale.X for both axes" bug (40x40, edge y=90); (245,70) is
outside the correct box's right edge (240) but would read Red under a "scale.Y for both axes"
bug (80x80, edge x=280). All 5 checks passed with exact predicted values on the first attempt.
Independently verified discriminating power TWICE: temporarily forced dh=dh*scale.X (X-for-both
bug) -- (220,110) failed exactly as predicted; separately forced both dims to scale.Y
(Y-for-both bug) -- (245,70) failed exactly as predicted; reverted both times, net production
diff zero. No production code changed. Added examples/easygl_spritebatch_scale_test.cpp.
EasyGL-only, Vulkan/Bgfx unaffected/unverified.

Task 419 verified SpriteBatch::Draw's sourceRectangle cropping -- verify-only, zero bugs found.
Read EasyGL's UV computation directly: u1=sourceRectangle.X/texW etc -- genuinely used to compute
normalized UV bounds, not discarded. Designed a discriminating test: 20x20 texture, 2x2 grid of
10x10 solid-color cells (Red/Blue/Magenta/Yellow), drawn with sourceRectangle=(10,0,10,10) (Blue
cell only) stretched into a 50x50 destRect. Chose 2 independent check points near destRect's
top-left and bottom-right corners that would each show a DIFFERENT wrong color (Red, Yellow) if
sourceRectangle were ignored and the whole texture stretched instead. All 3 checks passed with
exact predicted values on the first attempt. Independently verified discriminating power:
temporarily hardcoded u1=v1=0,u2=v2=1 (whole-texture UVs) and rebuilt -- both cropping checks
failed exactly as predicted (Red and Yellow instead of Blue); reverted, net production diff zero.
No production code changed. Added examples/easygl_spritebatch_sourcerect_test.cpp. EasyGL-only,
Vulkan/Bgfx unaffected/unverified.

Task 420 closed Phase 47's core SpriteBatch test arc (Tasks 411-420) -- verify-only, zero bugs
found. Proved layerDepth genuinely affects on-screen draw ORDER (which sprite composites on top
when 2 sprites overlap), not just backend dispatch order (already proved by Tasks 415/416's
mock-backend tests). Read EasyGLSpriteBatchBackend::Draw() directly: sprite vertices carry no Z
component at all -- layerDepth is used purely as a CPU-side sort key in flushBatch(), never
written into vertex data -- so with no depth test the sprite drawn LAST in the sorted sequence
wins the overlap via simple painter's algorithm, matching FNA's own default
(DepthStencilState.None) behaviour. Designed a discriminating test: 2 overlapping 60x60 opaque
sprites (Red layerDepth=0.1, Blue layerDepth=0.9) DELIBERATELY SUBMITTED IN THE OPPOSITE ORDER in
code (Draw(Blue) first, Draw(Red) second) from the correct FrontToBack draw order, so the test
genuinely discriminates depth-based sorting from raw submission order. All 3 checks (Red-only,
overlap=Blue, Blue-only) passed with exact predicted values on the first attempt. Independently
verified discriminating power: temporarily disabled the FrontToBack sort branch and rebuilt --
the overlap check failed exactly as predicted (Red instead of Blue); reverted, net production
diff zero. No production code changed. Added examples/easygl_spritebatch_layerdepth_test.cpp.
EasyGL-only, Vulkan/Bgfx unaffected/unverified.

Task 664 FIXED the confirmed Vulkan SpriteBatch multi-Begin/End-per-frame bug -- real, confirmed
bug found and fixed. Root cause isolated via instrumentation/tracing first (per plan_graphics.md's
own instruction, not guess-fixed): 2 compounding mechanisms. (1) VulkanSpriteBatchBackend::Begin()
unconditionally cleared its own vertices_/indices_/draws_ member vectors -- the SAME mutable
storage a prior End() had just finished populating. activeBatches_ tracked entries by RAW POINTER
to that single object, pushed at Begin() time. Since the once-per-frame harvest
(RecordCommandBuffer, driven by Present()) runs strictly after all of a frame's Begin/Draw/End
calls, a 2nd Begin() call destroyed the 1st cycle's already-completed geometry before the harvest
ever got a chance to read it. (2) A second, latent, previously-masked bug: drawSpritesFor's
harvest loop memcpy'd every activeBatches_ entry to a HARDCODED OFFSET 0 in the shared per-frame
spriteVB_/spriteIB_ buffers (unlike the already-correct 3D immediate-draw path, draw3DFor, which
uses a genuine accumulating vbOff/ibOff cursor) -- so even after fixing (1), 2 genuinely distinct
batches targeting the same RT in one frame would still stomp on each other's GPU-visible memory.

FIXED: added VulkanSpriteBatchBackend::BatchSnapshot (owns vertices/indices/draws/
customEffectBackend by value); End() now moves the completed cycle's geometry into a freshly
heap-allocated snapshot and pushes it onto activeBatches_ (changed to
vector<pair<unique_ptr<BatchSnapshot>, VulkanRTSource*>>) -- not Begin() -- so a 2nd Begin() call
only ever resets its own not-yet-used working vectors. Gave drawSpritesFor a genuine accumulating
vbOff/ibOff cursor mirroring draw3DFor's exact pattern (bounds-checked, memcpy'd/bound at the
running offset, advanced by each snapshot's byte size). Removed the now-unnecessary
ConsumeDraws()/GetVertices()/GetIndices()/GetDrawCalls()/GetCustomEffectBackend() accessors.

New regression test examples/vulkan_spritebatch_multi_begin_end_test.cpp: 2 independent
Begin()/Draw()/End() cycles in one Draw(GameTime) (Red left half, Blue right half, matching the
confirmed repro) -- both regions PASS with exact predicted colors on the first attempt.
Independently verified discriminating power: git stash-reverted both changed files and rebuilt --
the pre-fix code reproduced the exact reported symptom (left region = black/clear instead of Red,
right region correctly Blue); git stash pop-restored and reconfirmed both regions pass.

Full Vulkan rebuild + regression: ctest 3543/3555 (12 of the 13 previously-documented pre-existing
failures reproduced exact-name-match; Vulkan_RenderTargetCube_SampleAfterUnbind -- Task 876's own
already-documented flaky failure -- passed this run, reran 5x in isolation and passed every time,
confirming it's unrelated to this fix, not a regression; +1 new test).

Task 665 FIXED the confirmed Vulkan SpriteBatch.Begin() SamplerState no-op -- real, confirmed bug
found and fixed, BOTH predicted root causes confirmed present (not just one). (1)
VulkanSpriteBatchBackend never overrode SetSamplerFilter/SetSamplerAddressMode at all (silent
no-op via ISpriteBatchBackend's default empty bodies); FlushTexture() always used the texture's
own pre-baked, fixed-at-load-time descriptor set (currentTexture_->GetVkDescriptorSet()),
completely bypassing the per-slot VkSampler system (Task 118) entirely. (2) Draw() separately
std::clamp'd the CPU-computed UVs to [0,1] regardless of the actual sourceRectangle extent --
exactly the second bug this task predicted by analogy to Task 269's EasyGL fix -- silently
defeating Wrap/Mirror addressing even if bug (1) alone were fixed, since a clamped UV can never
leave [0,1] for any sampler to wrap/mirror.

FIXED both: added SetSamplerFilter/SetSamplerAddressMode overrides storing pendingFilter_/
pendingAddressU_/pendingAddressV_ (mirroring EasyGLSpriteBatchBackend's exact field
names/defaults); FlushTexture() now calls backend_->ApplySamplerState(0, pendingFilter_,
pendingAddressU_, pendingAddressV_, 1) (Task 118's existing per-slot cache) then builds a fresh
descriptor set via backend_->GetOrCreateTexSamplerDescSet(currentTexture_->GetVkImageView(),
backend_->slotSamplers_[0]) combining the texture's own image view with the CURRENT slot-0
sampler, instead of the texture's fixed one. Removed the erroneous std::clamp(...,0.f,1.f) calls
in Draw()'s UV computation, matching FNA's real unclamped SpriteBatch.cs behavior.

New test examples/vulkan_texture_address_mode_test.cpp -- a direct 1:1 port of Task 269's own
easygl_texture_address_mode_test.cpp (identical 2x1 Red/Blue texture, identical sourceRectangle
2x texture width, identical U~1.25 read-back point, identical PointWrap-vs-PointClamp
comparison): both checks pass with exact predicted colors on the first attempt (PointWrap->Red,
PointClamp->Blue). Independently verified discriminating power: git stash-reverted both changed
files and rebuilt -- the pre-fix code produced the exact predicted symptom, both PointWrap and
PointClamp reading the IDENTICAL blended color (64,0,191) (a bilinear-filtered boundary blend
from the fixed default sampler, proving Wrap addressing genuinely never took effect either way);
git stash pop-restored and reconfirmed both checks pass.

Full Vulkan rebuild + regression: ctest 3544/3556 (same 12 of 13 previously-documented
pre-existing failures as Task 664's run, exact-name match, zero new regressions; +1 new test).

This closes BOTH of Phase 47's originally-scoped "two SpriteBatch bugs found this session"
(Tasks 664/665). Phase 47 is now fully closed. The next NEXT task is Task 883 (implement
Effect::Clone()), the top of the accumulated backlog from earlier phases -- see §8.

Phase 46 ("SkinnedEffect exactness", Tasks 401-410) CLOSED with Task 410
(docs/skinnedeffect-support.md, full synthesis of Tasks 401-409). Summary of what it found/fixed:
Task 401 (opener) audited SkinnedEffect against FNA line-by-line -- all property defaults,
MaxBones=72, bounds-checking, and OnApply()'s shader-index formula matched exactly. FOUND AND
FIXED a real bug: Clone() never preserved SpecularColor/SpecularPower -- the identical
architectural shape Task 392 already fixed for FogColor across 4 stock effects. Opened Tasks 893
(DirectionalLight1/2 unforwarded), 894 (zero specular GPU implementation), and 895
(WeightsPerVertex complete GPU no-op on all 3 backends). Task 402 wrote 52 new
SkinnedEffectTests.cpp unit tests (zero prior coverage existed) plus fixed an unrelated
MaxBones linker gap. Tasks 403/405 were already fully satisfied by Task 402's own coverage --
marked done without new code. Task 404 verified GetBoneTransforms returns a genuinely
independent copy -- zero bugs. Task 406 (phase's first real pixel test) confirmed an identity
bone palette produces zero deformation on all 3 backends -- zero bugs, but surfaced a genuinely
new Bgfx-specific test-harness pitfall (GetBackBufferData() only reliably reflects the first read
per rendered frame), fixed with a renderAndRead() per-checkpoint helper reused by every
subsequent test in the phase. Task 407 formalized the pre-existing (Task 123) EasyGL-only
translation-bone test into all 3 backends -- zero bugs. Task 408 verified genuine 2-bone
weighted blending using a deliberately discriminating bone pair, independently confirmed via a
temporary (1,0)-weight swap -- zero bugs. Task 409 (capstone) combined Tasks 406-408's pieces
into one scene, one bone-palette upload, one draw call covering 3 quads -- zero new bugs, all 3
backends byte-identical across all 3 quads within itself.

Phase 45 ("EnvironmentMapEffect exactness", Tasks 391-400) CLOSED with Task 400
(docs/environmentmapeffect-support.md, full synthesis of Tasks 391-399). Summary of what it
found/fixed: Task 391 (opener) audited EnvironmentMapEffect against FNA -- zero bugs, opened
Task 890 (DirectionalLight1/2 unforwarded). Task 392 found and FIXED a real bug affecting 4
stock effects: Clone() never preserved FogColor (AlphaTestEffect/DualTextureEffect/
EnvironmentMapEffect/SkinnedEffect). Task 393 verified EnvironmentMapAmount=0 -- zero bugs, but
flagged a real formula discrepancy for Task 394 to investigate. Task 394 confirmed and FIXED a
real cube-map blend bug on all 3 backends: additive instead of FNA's real lerp (pre-fix
(228,178,153) vs correct (128,128,128)). Task 395 confirmed and FIXED a second real bug:
EnvironmentMapSpecular was flat-additive instead of alpha-scaled by the cube map's own alpha
channel (pre-fix (202,152,127) vs correct (151,101,76) at alpha=128); opened Task 891 for the
still-unscaled base-lerp envColor term. Task 396 confirmed and FIXED a real missing-feature gap:
no Fresnel edge-weighting existed at all on any backend (pre-fix (128,128,128) vs correct
Fresnel-suppressed (100,50,25) at a head-on camera angle). Task 397 verified EyePosition
correctly drives the reflection vector -- zero bugs, proof by construction using the phase's
first distinct-per-face cube map. Task 398 confirmed and FIXED a real formula bug on 2 of 3
backends: normals transformed by the raw World matrix instead of transpose(inverse(World3x3)),
wrong under non-uniform scale (Vulkan was already correct); opened Task 892 for a worse sibling
bug in BasicEffect's Bgfx lit shader (transforms normals by the full World*View*Projection
matrix). Task 399 (capstone) combined everything Tasks 394-398 fixed into one scene -- all 3
backends produced the exact predicted (151,101,76) on the first attempt, zero new bugs, genuine
cross-backend consistency.

Phase 44 ("DualTextureEffect exactness", Tasks 381-390) CLOSED with Task 390
(docs/dualtextureeffect-support.md, full synthesis of Tasks 381-389). Task 383 found and FIXED A
REAL BUG ON ALL 3 BACKENDS: DualTextureEffect's dual-texture shaders were all missing FNA's
`color.rgb *= 2` doubling factor. Task 387 found and FIXED a real bug on Bgfx: the second texture
slot (Texture2) had no null-fallback at all. Task 388 found and FIXED a real bug on EasyGL: fog
was never forwarded and the dedicated shader had zero fog infra. Task 389 (capstone) found/opened
Task 889 (DualTextureEffect.VertexColorEnabled is a total no-op on all 3 backends).

Phase 43 ("AlphaTestEffect exactness", Tasks 371-380) CLOSED with Task 380
(docs/alphatesteffect-support.md, full synthesis of Tasks 371-379). Task 377 found
AlphaTestEffect.VertexColorEnabled has zero effect on Vulkan/Bgfx by default (opened Task 887).
Task 378 fixed AlphaTestEffect fog forwarding on EasyGL, and discovered fog is a total no-op on
Vulkan/Bgfx for every 3D effect, project-wide (opened Task 888). Task 379 found and FIXED a real,
general Bgfx bug: null-texture draws left the previous draw's texture bound instead of falling
back to white (affected 7 dispatch branches, not just AlphaTestEffect).

Phase 42 closed with docs/basiceffect-support.md (full synthesis) and opened 2 new follow-up
tasks: Task 885 (lit-path EmissiveColor + DirectionalLight1/2 forwarding — Vulkan half needs a
shared push-constant budget expansion, also used by SkinnedEffect) and Task 886 (real specular
highlights — a new feature, zero existing infrastructure). Phase 43 closed and opened 2 more:
Task 887 (Vulkan/Bgfx alpha-test vertex-color unification) and Task 888 (Vulkan/Bgfx project-wide
fog). Phase 44 closed and opened Task 889 (DualTextureEffect.VertexColorEnabled is a no-op on all
3 backends). Phase 45 closed and opened 3 more: Task 890 (EnvironmentMapEffect.
DirectionalLight1/2 unforwarded), Task 891 (EnvironmentMapEffect's base cube-map lerp target
still unscaled by combined texture x diffuse alpha), and Task 892 (BasicEffect's Bgfx
lit-textured shader transforms normals by the full World*View*Projection matrix, a worse sibling
bug found while fixing Task 398). Task 401 opened 3 more: Task 893 (SkinnedEffect.
DirectionalLight1/2 unforwarded), Task 894 (SkinnedEffect.SpecularColor/SpecularPower have zero
GPU implementation on any backend), and Task 895 (SkinnedEffect.WeightsPerVertex is a complete
GPU no-op on all 3 backends). None of these 11 block the current backlog's tasks.

Task 884 fixed a real, confirmed dangling-pointer hazard: EffectParameterCollection and
EffectPassCollection both stored their elements BY VALUE in a std::vector -- the identical bug
shape Task 355 already found and fixed for EffectTechniqueCollection. Switched both to
vector<unique_ptr<T>> with matching custom iterator/const_iterator pairs copied verbatim from
EffectTechniqueCollection.hpp/.cpp; Add() now does elements_.push_back(make_unique<T>(move(x))).
No call-site changes needed anywhere in the codebase -- grepped first and confirmed every existing
usage goes through operator[]/GetParameterBySemantic/range-for, never names the iterator type
directly. Added PointerStableAcrossReallocatingAdd to both collections in EffectCollectionTests.cpp
(take &col[0] after one Add(), force 64 more Add() calls to guarantee reallocation, assert the
original pointer/address is unchanged). Independently verified discriminating power via git stash
revert-and-rebuild on just the 4 production files: pre-fix, EffectParameterCollectionTest failed
cleanly on a pointer-address mismatch, but EffectPassCollectionTest actually SEGFAULTED (a genuine
use-after-free dereferencing the stale pointer) -- strong proof the hazard was real, not
theoretical. Restored the fix, rebuilt, reconfirmed both pass.

While investigating Task 884's original (bundled) scope, discovered the row's own title had
accidentally bundled two unrelated issues together: the Effect-collection fix above, plus a
separate RasterizerState-default GPU-sync gap that Task 364 had originally opened and asked to be
"tracked as new Task 884" -- but by the time a plan_graphics.md row for 884 actually got written,
it only covered the Effect-collection half. Investigated the RasterizerState issue via a research
agent before deciding what to do with it (per the standing "investigate before implementing"
practice): confirmed GraphicsDevice's ctor sets rasterizerState_ to the correct FNA-matching
default (CullCounterClockwiseFace) but never pushes it to any backend's actual GPU state --
EasyGL/Vulkan both start from an effectively-CullNone hardware default, Bgfx is the only one of
the 3 whose hardcoded default happens to match FNA's. The architecturally correct fix is one line
(call setRasterizerStateProperty(rasterizerState_) once in the ctor after backend creation), but
grepping examples/*.cpp found 174 of 208 files never mention RasterizerState at all (128 of those
issue real draw calls) -- since each test's geometry/winding is shared verbatim across its
EasyGL/Vulkan/Bgfx variants, and those variants currently only pass because EasyGL/Vulkan's
INCORRECT no-culling default happens to let the same winding through that Bgfx's CORRECT default
culls, landing the one-line fix would very likely flip many of those 128 EasyGL/Vulkan tests to
black-frame failures. This is a scoping decision (audit-then-fix vs. some other sequencing), not
an implementation task -- split it out into its own Task 896, left unstarted, and updated every
historical plan_graphics.md row that pointed at "Task 884" for this specific bug (Tasks 364, 365,
366, 367, 368, 370, 375, 384, 399) to point at Task 896 instead, so the tracking numbers stay
accurate.

Task 885 fixed BasicEffect's lit-path gap on EasyGL and Bgfx: DirectionalLight1/DirectionalLight2
were completely unforwarded to the GPU (only DirectionalLight0 ever was), and EmissiveColor was
silently dropped whenever LightingEnabled=true (only the disabled-lighting path, Task 369, had
it). Derived the exact FNA formula from Lighting.fxh via a research agent first: EmissiveColor is
added AFTER the ambient+light-sum is multiplied by DiffuseColor, not scaled by it -- CNA folds
AmbientLightColor into the same light-sum multiply rather than FNA's pre-baked "ambient+emissive"
shader uniform (confirmed mathematically equivalent net result by direct expansion of both
formulas). Added GpuDrawParams fields for light1/light2 direction+diffuse (mirroring light0's
existing shape) and reused the pre-existing emissiveColor field (previously EnvironmentMapEffect-
only). Fixed both backends' lit shaders to sum all 3 lights (each light's Enabled state gated the
same way Task 368 gated light0) and add EmissiveColor after the diffuse multiply -- Bgfx's
pre-existing formula needed restructuring (not just extension) since it multiplied the *entire*
lit result by DiffuseColor via a single vec4 multiply, which would have incorrectly scaled
EmissiveColor by DiffuseColor too if added naively. New PointerStable-style discriminating tests,
one per backend (examples/{easygl,bgfx}_basiceffect_multilight_emissive_test.cpp, 3 checks each:
all-3-lights-plus-emissive summed correctly, per-light Enabled gating, per-light independent
Direction field to catch a copy-paste-aliasing hazard). Independently verified discriminating
power on both backends via git stash revert-and-rebuild: pre-fix, both backends produced the
identical (89,13,13) ambient+light0-only result on all 3 checks, exactly as predicted; restored
and reconfirmed all 6 checks (3 per backend) pass with the exact hand-derived expected values.
Vulkan's lit-textured pipeline was investigated too (its own pipelineLayoutExt3D_ push-constant
struct, confirmed via direct code reading to be fully packed at 32/32 floats, shared with strides
20/24 and Instanced3D but NOT literally shared with SkinnedEffect's own separate
pipelineLayoutSkinned3D_/descriptorSetLayoutSkinned_) -- landing the same fix there needs a new,
dedicated descriptor-set/pipeline-layout/UBO-ring-buffer for the lit-textured pipeline specifically
(mirroring EnvironmentMapEffect's own descriptorSetLayoutEnvMap_/pipelineLayoutEnvMap3D_/
envMapUBO_ pattern, not a simple push-constant-widening tweak), so it was split out into its own
Task 897 rather than rushed into the same commit.

Task 897 then implemented exactly that Vulkan infrastructure: descriptorSetLayoutLitTextured_
(set=0: binding0=sampler2D, binding1=UNIFORM_BUFFER_DYNAMIC "LitLightParams" -- 5 padded vec4s
for light1/light2 dir+diffuse and emissiveColor, fragment-stage only), pipelineLayoutLitTextured3D_
(keeping the SAME unchanged 128-byte push constant/FillExtPushConst content -- no change needed
there), pipelinesLitTextured3D_ cache, and a 512-slot per-frame dynamic-offset UBO ring buffer,
all mirroring EnvironmentMapEffect's own EnsureEnvMapResources()/GetOrCreateEnvMapDescSet()/
GetOrCreatePipelineEnvMap3D() line-for-line. DrawPrimitivesEx/DrawIndexedPrimitivesEx gained a
needsLitTextured=(stride==32 && no other special effect) condition -- exactly the pre-existing
condition that already implied "lit-textured shader" via GetOrCreatePipelineExt3D's internal
switch, just given its own explicit flag now -- and useExtParams was updated to exclude it, so
strides 20/24/Instanced3D are completely untouched. lit_textured3d.frag.glsl extended with the
UBO block and the identical 3-light-sum+emissive formula already verified on EasyGL/Bgfx
(emissive added before the texture multiply but after the DiffuseColor multiply, matching FNA).
New Vulkan_BasicEffect_MultiLightEmissive test (port of Task 885's EasyGL test) PASSED ON THE
FIRST ATTEMPT with the exact same expected values -- strong evidence the new pipeline/UBO
plumbing was correct on the first try. Independently verified discriminating power via git stash
revert-and-rebuild of all 4 changed Vulkan files: pre-fix, identical (89,13,13) result to
EasyGL/Bgfx's own pre-fix runs; restored and reconfirmed. Ran a FULL SERIAL (-j1) Vulkan
regression specifically to rule out parallel-execution GPU-context flakiness: 3546/3559 pass, and
further confirmed the 13 failures (5x BlendState, 5x DepthStencilState, GraphicsDevice_
ReferenceStencil, DepthBias, 1 flaky CueTest) are genuinely pre-existing and NOT caused by this
task -- reran the same failing tests with all of Task 897's Vulkan changes git-stash-reverted and
they failed identically without any of this task's code present. This closes the DirectionalLight1/
DirectionalLight2/EmissiveColor lit-path gap on all 3 backends (Tasks 885+897 together).

Last full regression: Task 897 (Vulkan, serial -j1 full ctest after restoring the fix) --
3546/3559 pass (13 pre-existing failures, all independently reconfirmed unrelated to this task
per the revert-and-rerun check above; +1 new test).
EasyGL last verified at Task 885: 3628/3631 pass (3 pre-existing unrelated failures:
EasyGL_MRT_TwoAttachments, EasyGL_GraphicsDevice_ReferenceStencil, easy-gl-resource-smoke-tests,
unchanged from baseline).
Bgfx last verified at Task 885: 3527/3528 pass (1 pre-existing flaky
ENetDiscoveryServiceTest.UnregisterHostStopsAnsweringQueries, reconfirmed passing in isolation).
Caution: run all 3 backends' full ctest suites sequentially, never concurrently (see NEXT.md §2).

For the full history of what each task in Phase 41/42/43/44/45/46/47 found, read plan_graphics.md
directly (Tasks 351-420, 664-665, 884-885, 896-897) rather than this file — this file intentionally
keeps only a one-line summary per task (see §3) to stay a genuinely quick-to-read handoff document.
```
