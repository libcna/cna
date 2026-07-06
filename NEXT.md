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
- **Current development phase:** Phases 1–44 are complete. **Phase 45 ("EnvironmentMapEffect
  exactness", Tasks 391–400) is open** — Task 391 (opener) audited `EnvironmentMapEffect` against
  FNA: all 14 properties/defaults/`Clone()`/`OnApply()` match exactly, **zero bugs in its own
  scope**, including a subtle detail (`FresnelFactor`'s constructor-driven `fresnelEnabled_=true`
  post-construction state) that was easy to get wrong but wasn't. Found `FillGpuDrawParams()` only
  forwards `DirectionalLight0` — `DirectionalLight1`/`DirectionalLight2` are silently ignored,
  confirmed via `EnvironmentMapEffect.fx` sharing `BasicEffect`'s identical `Lighting.fxh`
  mechanism — opened as new **Task 890** (same shape as Task 885's `BasicEffect` finding, likely
  shares its fix plumbing) rather than folded into Task 885, since it's a distinct effect with its
  own dispatch. **Task 392 found and fixed a real bug affecting 4 stock effects**
  (`AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect`): none of their
  `Clone()` copy constructors preserved `FogColor` — `CacheEffectParameters()` re-links
  `fogColorParam_` to a fresh, zero-valued parameter in the clone, and nothing copied the source's
  actual value across. Tasks 372/382's own `Clone()` tests never caught this because neither set
  `FogColor` before cloning; extended both to close the gap. Fixed all 4 effects with a one-line
  `fogColorParam_->SetValue(src.getFogColorProperty())` addition each. **Task 393 verified
  `EnvironmentMapAmount=0` correctly ignores the cube map** — zero bugs in its own scope, exact
  match on all 3 backends — but surfaced a real formula-level discrepancy for Task 394 to
  investigate: FNA's real pixel shader **lerps** between the lit/textured color and the cube map
  (`EnvironmentMapAmount=1` should *fully replace* the lit color), while CNA's actual shader
  formula **adds** the cube map on top instead — a difference invisible at `Amount=0` (where both
  formulas coincide) but real and testable at `Amount=1`, which every pre-existing test happened to
  never expose since they all zeroed the lit/textured term in their `Amount=1` sub-cases. **Task
  394 confirmed and FIXED this real formula bug on all 3 backends**: empirically measured CNA's
  pre-fix output as `(228,178,153)` (exact match to the additive-formula prediction) against FNA's
  correct `(128,128,128)` lerp result — fixed all 3 shaders (`EasyGL`/`Vulkan`/`Bgfx`) by changing
  the additive blend to a proper `mix()`. `EnvironmentMapSpecular`'s own separate `×envmap.a`
  formula nuance (Task 395) and Fresnel edge-weighting (Task 396) remain deliberately unimplemented
  — neither affects this task's own `Amount=1` exact-replacement assertion. Phase 44 ("DualTextureEffect
  exactness", Tasks 381–390) is **CLOSED** — Task 390 wrote `docs/dualtextureeffect-support.md`
  synthesizing Tasks 381–389: property/default audit (zero bugs), a real cross-backend `color.rgb
  *= 2` doubling-factor bug found and fixed (Task 383), a real Bgfx-only `Texture2` null-fallback
  bug found and fixed (Task 387), a real EasyGL-only fog-forwarding bug found and fixed (Task 388,
  requiring new shader infra since `DualTextureEffect` has its own dedicated shader unlike
  `AlphaTestEffect`), and a capstone cross-backend consistency test (Task 389) that also
  discovered and opened **Task 889** (`VertexColorEnabled` is a total no-op on all 3 backends for
  `DualTextureEffect` — no dedicated audit task existed for this property in Phase 44, unlike
  `AlphaTestEffect`'s Task 377). Phase 43
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
  (`cmake-build-bgfx`): all 3 configured, build cleanly. Last rebuilt/re-verified for Task 394.

### Test status (last verified: Task 394)
- **EasyGL, full `ctest -j1`:** 3547/3550 pass. 3 pre-existing/documented failures (see §5):
  `EasyGL_MRT_TwoAttachments`, `easy-gl-resource-smoke-tests`, `EasyGL_GraphicsDevice_ReferenceStencil`.
- **Vulkan, full `ctest -j1`:** 3467/3480 pass. 13 documented pre-existing failures (see §5),
  exact-name match, no flakes this run.
- **Bgfx, full `ctest -j1`:** 3451/3451 pass — 100%, no flakes this run.
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
  standard-winding full-screen quad unless `RasterizerState::CullNone` is set explicitly (Task 884,
  found this session).
- `SetRenderTargets`'s simultaneous-target cap doesn't match FNA's real `MAX_RENDERTARGET_BINDINGS
  = 4`: EasyGL/Bgfx silently cap at 8, Vulkan has no CNA-level cap at all (Task 881).
- `EasyGL_MRT_TwoAttachments` (Task 145): even a basic, same-size/format 2-target MRT setup doesn't
  render correctly on EasyGL — attachment 1 stays black. Pre-existing, off-limits for opportunistic
  fixing (see §9).
- `Texture3D`/`TextureCube::GetData` is a total silent no-op on Vulkan/Bgfx (Task 865).
  `TextureCube::DDSFromStreamEXT` is a non-functional stub (Task 663).
- `Texture2D::SetData(level>0,...)` is a silent no-op on Vulkan/Bgfx; EasyGL's non-mip-aware
  filters render solid black on mip-incomplete textures (Task 867).
- `SpriteBatch`'s `SamplerState` is a no-op on Vulkan/Bgfx (EasyGL only). Multiple
  `SpriteBatch::Begin()`/`End()` per frame on Vulkan: only the last batch renders.
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
| — | 394 | **Real, confirmed formula bug found and fixed on all 3 backends**: `EnvironmentMapEffect`'s cube-map blend was additive (`+envColor×Amount`) instead of FNA's real `lerp`/`mix`, meaning `Amount=1` added the cube map on top of the lit color instead of fully replacing it. Empirically confirmed pre-fix output `(228,178,153)` vs FNA's correct `(128,128,128)`. Fixed all 3 shaders. `git stash`-confirmed all 3 backends fail pre-fix with the exact predicted value. |
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
| `54aee7a2` | 364 | **Fix**: `VertexColorEnabled` wasn't honored by any of the 3 backends' no-texture shaders — fixed per-backend; found (not fixed) a Bgfx-only rasterizer-cull-default bug (Task 884). |
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
| Confirmed bug | `SpriteBatch` with multiple `Begin()`/`End()` per frame on Vulkan: only the last batch renders. | — |
| Confirmed, incomplete | `SpriteBatch`'s `SamplerState` (`Begin()`) is a no-op on Vulkan/Bgfx (EasyGL only). | — |
| Confirmed, pre-existing | `EasyGL_MRT_TwoAttachments`: attachment 1 stays black with 2 render targets. | Task 145 |
| Confirmed, minor, not fixed | `SetRenderTargets`'s simultaneous-target cap doesn't match FNA's real `MAX_RENDERTARGET_BINDINGS=4`. | Task 881 |
| Confirmed, incomplete | `PresentationMode::Letterbox`/`Overscan`/`Stretch`/`NativeBackBuffer` aren't distinctly implemented on EasyGL; Vulkan/Bgfx implement no virtual-resolution scaling at all. | Task 882 (not yet a formal `plan_graphics.md` row — referenced inline in Task 348) |
| Confirmed, pre-existing, out-of-repo | `easy-gl-resource-smoke-tests` aborts on an internal assert in the sibling `easy-gl` repo. | — |
| Confirmed, pre-existing | `Vulkan_DepthBias`'s `DepthBias=-1e6` sub-case fails; other sub-cases pass. | — |
| Confirmed, pre-existing, flaky | `Vulkan_FillMode_WireFrame`/`Vulkan_RenderTargetUsage`: order-dependent, only one fails per full-suite run. | — |
| Confirmed, architectural, not fixed | `GraphicsDevice`'s default `RasterizerState` is never pushed to any backend's actual GPU state at construction; Bgfx's hardcoded default happens to be the only one matching FNA's, so it alone silently culls standard-winding quads unless `CullNone` is set explicitly. | Task 884 (also covers the `EffectTechniqueCollection`/`EffectParameterCollection`/`EffectPassCollection` dangling-vector hazard class — Techniques fixed by Task 355, Parameters/Pass not yet exercised) |
| Confirmed, architectural, not fixed | `Effect::Clone()` doesn't exist — needs an ownership-model decision plus fixing an `EffectPass::Apply()` owner-aliasing hazard plus `Clone()` overrides in all 7 stock effects. | Task 883 |
| Confirmed, real, not fixed | `BasicEffect::FillGpuDrawParams()` only forwards `DirectionalLight0` (never `SpecularColor`/`SpecularPower`/`DirectionalLight1`/`DirectionalLight2`); lit path still omits `+EmissiveColor` (disabled-lighting path fixed Task 369). No specular infra exists anywhere. | Tasks 885/886 |
| Confirmed, real, not fixed (empirically verified) | `AlphaTestEffect.VertexColorEnabled` has **zero effect on Vulkan or Bgfx** — their alpha-test pipeline/shader never declares a color vertex attribute at all, and this pipeline is used by default (`AlphaFunction=Greater`/`ReferenceAlpha=0` already trigger it). Correct on EasyGL (reuses `BasicEffect`'s already-fixed stride-24 shader). | Task 887 |
| Confirmed, project-wide, not fixed | **Fog is a total GPU no-op on Vulkan and Bgfx for every 3D effect** — grepped every shader file in both backends for "fog", zero matches anywhere. Affects `BasicEffect` too (its `FillGpuDrawParams()` already forwards fog correctly; only the GPU side is missing). EasyGL already has fog fully working generically (confirmed for `BasicEffect` since Task 195, and `AlphaTestEffect` since Task 378). | Task 888 |
| Confirmed, real, not fixed | `DualTextureEffect.VertexColorEnabled` has **zero effect on all 3 backends** — every backend's dual-texture dispatch is a dedicated shader/pipeline declaring only `position`+`texcoord` inputs (Vulkan explicitly reuses the generic textured-only vertex shader; Bgfx hardcodes `v_color0` to the diffuse uniform, not a real per-vertex attribute). Found while writing Task 389's capstone test — Phase 44 never had a dedicated audit task for this property, unlike `AlphaTestEffect`'s Task 377. | Task 889 |
| Confirmed, real, not fixed | `EnvironmentMapEffect::FillGpuDrawParams()` only forwards `DirectionalLight0` — `DirectionalLight1`/`DirectionalLight2` silently ignored on all 3 backends. Confirmed this effect shares `BasicEffect`'s identical `Lighting.fxh`/`ComputeLights` mechanism in real FNA (same `oneLight` shader-variant optimization), so the same gap and likely the same fix plumbing as Task 885 applies here too. | Task 890 |
| Confirmed, real, not fixed | `EnvironmentMapEffect`'s `EnvironmentMapSpecular` is a flat additive constant on all 3 backends; FNA's real formula multiplies it by the cube map's **alpha** channel (`+= EnvironmentMapSpecular * envmap.a`) instead. Every existing test's cube maps used `alpha=255` throughout, so this was never exercised with a non-1 alpha. | Task 395 |
| Confirmed, real, not fixed | `EnvironmentMapEffect`'s Fresnel edge-weighting (`FresnelFactor`, enabled by default in real FNA) is **not implemented at all** — no Fresnel uniform exists in any of the 3 backends' env-map shaders; the blend factor is always the flat `EnvironmentMapAmount` regardless of view angle. | Task 396 |
| Suspected, not reproduced | Vulkan/Bgfx likely have the same mip-allocation bug already fixed on EasyGL's `TextureCube` (Task 276), for `Texture3D`/`TextureCube` on both backends. | Task 864 |
| Needs verification | Whether Bgfx's window actually has a physical stencil buffer has not been checked. | — |
| Incomplete, by design | Stride-keyed vertex layout only supports strides 16/20/24/32/52. Vulkan has no `Tangent`/`Binormal` mapping. `SurfaceFormat` support is Color-only for real GPU formats. `SDL_Renderer` has no 3D at all. | — |
| Risky assumption | `GraphicsDevice`'s user-primitive scratch buffers never shrink — fine for typical use, but memory stays at the high-water mark for the device's lifetime. | — |

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
  reference across an `Add()` call (a real dangling-pointer bug class found in Task 355; see
  Task 884 for the collections not yet converted).

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

In priority order — the first continues Phase 45 (Task 395 fully scoped in `plan_graphics.md`);
the rest are the accumulated backlog from earlier phases (Tasks 863–890).

1. **Task 395 — pixel test for `EnvironmentMapSpecular`**
   - Goal: verify `EnvironmentMapSpecular`'s contribution. **Important**: Task 394's investigation
     found FNA's real formula multiplies `EnvironmentMapSpecular` by the cube map's **alpha**
     channel (`color.rgb += EnvironmentMapSpecular * envmap.a`), while CNA currently treats it as a
     flat additive constant that ignores cube-map alpha entirely. Every existing test's cube maps
     used `alpha=255` throughout, so design this test with a cube map using a non-1 alpha value to
     determine whether this is a real bug (matching Task 394's exact discovery methodology) and fix
     if confirmed.
   - Files: new `examples/{easygl,vulkan,bgfx}_environmentmapeffect_specular_test.cpp`, likely
     `src/CNA/Internal/Backends/{EasyGL,Vulkan,Bgfx}/...` shader changes if confirmed.

2. **Task 883 — implement `Effect::Clone()`** (needs: C++ ownership-model decision, fixing the
   `EffectPass::Apply()` `owner_`-aliasing hazard on clone, `Clone()` overrides in all 7 stock
   effects). Files: `Effect.hpp`/`.cpp` + all 7 stock-effect pairs.

3. **Task 884 — fix the `RasterizerState`-default GPU-sync gap** and the remaining
   `EffectParameterCollection`/`EffectPassCollection` by-value-vector dangling-pointer hazard.
   Files: each backend's device-construction path; `EffectParameterCollection.hpp`/
   `EffectPassCollection.hpp`.

4. **Task 885 — forward `EmissiveColor` on `BasicEffect`'s lit path + `DirectionalLight1`/`2`**
   (opened by Task 369). Needs a new uniform on EasyGL/Bgfx's lit shaders (straightforward) plus
   expanding Vulkan's shared 128-byte `pipelineLayoutExt3D_` push-constant budget (also used by
   `SkinnedEffect` — coordinate testing across both). Files: `BasicEffect.cpp`, each backend's lit
   shader, `GpuDrawParams`.

5. **Task 886 — implement real specular highlights for `BasicEffect`** (opened by Task 369; a new
   feature, not a bug fix — zero specular infrastructure exists today). Needs world-space-position
   varyings, eye-position uniform (reuse `EnvironmentMapEffect`/`SkinnedEffect`'s
   `Matrix::Invert(view).Translation` technique), half-vector math, and `SpecularColor`/
   `SpecularPower` forwarding, all 3 backends. Likely shares Task 885's Vulkan push-constant work.

6. **Task 887 — fix `AlphaTestEffect.VertexColorEnabled` being ignored on Vulkan/Bgfx** (opened by
   Task 377; true by default, not an edge case). Needs unifying Vulkan/Bgfx's alpha-test dispatch
   with their already-correct per-stride textured/colored-textured pipelines (mirror EasyGL's
   architecture) — a large, multi-shader-file (6 files, 2 backends), multi-dispatch-site change.
   Files: `alpha_test3d.vert/frag.glsl` + `colored_textured3d`/`textured3d`/`lit_textured3d`
   (Vulkan); `vs/fs_alpha_test3d.sc` + Bgfx equivalents; both backends' draw-dispatch code.

7. **Task 888 — implement real fog rendering on Vulkan and Bgfx** (opened by Task 378; a
   project-wide gap, not `AlphaTestEffect`-specific — zero shader files in either backend
   implement fog at all, for any effect, though the C++ side already forwards the fields
   correctly for `BasicEffect`). Needs fog uniforms/varyings + blend formula in ~8 shader pairs ×
   2 backends. Likely comparable in size to Task 868/870's Vulkan `BlendState` work.

8. **Task 881 — cap `SetRenderTargets` at FNA's real `MAX_RENDERTARGET_BINDINGS=4`.**
   Files: `GraphicsDevice.cpp` (`SetRenderTargets`). Verification: 5-target call throws, 1–4 work.

9. **Task 880 — wire `GraphicsDevice.Viewport` to a real GPU viewport on all 3 backends.**
   Files: `IGraphicsBackend.hpp`, `GraphicsDevice.cpp`, all 3 backends' graphics-backend `.cpp`.
   Verification: sub-region-viewport pixel test (should fail on all 3 backends today).

10. **Task 878/879 — implement real mip/MSAA render-target support on Vulkan and Bgfx**, mirroring
    Task 336/337's exact EasyGL fix shape. Files: each backend's render-target backend classes.

11. **Task 877 — wire `DepthStencilFormat`'s exact value into render-target depth/stencil
    attachments** on all 3 backends (currently hardcoded/coarse choices).

12. **Task 875/876 — Vulkan render-target bugs**: `Clear()`-only draws never record a render pass
    (875); `RenderTargetCube` via `EnvironmentMapEffect` renders black after unbind, root cause not
    isolated (876, needs isolation before a fix is attempted — see §9).

13. **Task 873/874 — fix Bgfx's wrong-handle-type `static_cast`s** for `RenderTarget2D`/
    `RenderTargetCube` sampling. Files: `BgfxGraphicsBackend.hpp`/`.cpp`.

14. **Task 663 — implement `TextureCube::DDSFromStreamEXT` for real** (build a real DDS cube-map
    test fixture *first*, then implement against it).

15. **Task 865 — implement real Vulkan `GetData` readback for `Texture3D`/`TextureCube`**
    (`vkCmdCopyImageToBuffer` + staging buffer, mirroring the existing upload path in reverse).

16. **Task 864 — reproduce and fix the suspected Vulkan/Bgfx mip-allocation bug** for
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
- **No opportunistic fix for Task 885 (lit-path EmissiveColor/multi-light)** bundled into an
  unrelated task — the Vulkan half specifically requires expanding a push-constant budget shared
  with `SkinnedEffect` (`FillExtPushConst()`'s `float[32]`), which needs coordinated re-verification
  across both effect classes, not a quick Vulkan-shader-only tweak.
- **No rushed specular implementation for Task 886** — it's a new feature (zero existing
  infrastructure), not a bug fix; needs its own dedicated design pass for world-space-position
  varyings and half-vector math across all 3 backends, likely bundled with Task 885's Vulkan
  push-constant work.
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
Read NEXT.md first. Inspect only the files needed for the first task in §8 (Task 395).
Do not refactor unrelated code. Make one small, verified improvement.
Run the relevant build/test command before declaring the task done.
Update NEXT.md and plan_graphics.md after finishing, then commit AND push (standing
instruction — do not wait to be asked; one task = one commit = one push).

Current status: Phases 1-44 are FULLY COMPLETE. Phase 45 ("EnvironmentMapEffect exactness",
plan_graphics.md Tasks 391-400) is OPEN: Task 391 (opener) audited EnvironmentMapEffect against
FNA line-by-line — all 14 properties/defaults/Clone()/OnApply() match exactly, ZERO bugs in its
own scope. Found FillGpuDrawParams() only forwards DirectionalLight0 (DirectionalLight1/2 silently
ignored) — confirmed this effect shares BasicEffect's identical Lighting.fxh/ComputeLights
mechanism, same gap as Task 885 — opened as new Task 890. Task 392 wrote 42 new
EnvironmentMapEffectTests.cpp unit tests (zero prior coverage existed) and FOUND AND FIXED A REAL
BUG AFFECTING 4 STOCK EFFECTS: Clone() never preserved FogColor on AlphaTestEffect/
DualTextureEffect/EnvironmentMapEffect/SkinnedEffect — CacheEffectParameters() re-links
fogColorParam_ to a fresh, zero-valued parameter in the clone, and nothing copied the source's
actual value across. Tasks 372/382's own Clone() tests never caught this because neither set
FogColor before cloning; extended both existing test files to close the gap. Fixed all 4 effects
with a one-line addition each. git stash-confirmed all 3 testable cases fail pre-fix. Task 393
verified EnvironmentMapAmount=0 correctly ignores the cube map — zero bugs in its own scope, exact
match on all 3 backends — but surfaced a REAL FORMULA DISCREPANCY for Task 394: FNA's real pixel
shader lerps between the lit/textured color and the cube map (Amount=1 should fully REPLACE the
lit color), while CNA's actual shader formula ADDED the cube map on top instead. Task 394
confirmed this empirically (measured pre-fix (228,178,153) vs FNA's correct (128,128,128) using a
deliberately non-saturated gray cubemap after a white-cubemap sub-case failed to discriminate the
two formulas at all — both clamp to 255) and FIXED IT ON ALL 3 BACKENDS: changed
`rgb = litRGB*texColor + envColor*Amount + specular` to
`rgb = mix(litRGB*texColor, envColor, Amount) + specular` in EasyGL's GLSL, Vulkan's
env_map3d.frag.glsl (+ regenerated spirv_shaders.hpp), and Bgfx's fs_env_map3d.sc (+ regenerated
bgfx_shaders.hpp, using the correct `.../bgfx/src` compile_shaders.py path this time). git
stash-confirmed all 3 backends fail pre-fix with the exact predicted additive value. Deliberately
deferred 2 nuances surfaced during the investigation, each already scoped as its own task: Task
395 (NEXT) — EnvironmentMapSpecular should multiply by the cube map's alpha channel per FNA's real
`+= EnvironmentMapSpecular * envmap.a`, but CNA treats it as a flat additive constant ignoring
alpha entirely; every existing test used alpha=255 cubemaps so this was never exercised — and Task
396 — CNA implements no Fresnel edge-weighting at all (no Fresnel uniform exists in any of the 3
backends' shaders), while FNA's default FresnelFactor=1 means real XNA content relies on
edge-weighted blending between the lit color and the cube map rather than a flat Amount everywhere.

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
3 backends). Task 391 opened one more: Task 890 (EnvironmentMapEffect.DirectionalLight1/2 are
completely unforwarded, same shape as Task 885, likely shares its fix). None of these 6 block
Phase 45's remaining tasks.

Last full 3-backend regression (Task 394 — real production shader fix on all 3 backends, plus 3
new test files):
EasyGL 3547/3550 pass (3 documented pre-existing failures, no flakes this run).
Vulkan 3467/3480 pass (13 documented pre-existing failures, exact-name match, no flakes this run).
Bgfx 3451/3451 pass (100%, no flakes this run).
Caution: run all 3 backends' full ctest suites sequentially, never concurrently (see NEXT.md §2).

For the full history of what each task in Phase 41/42/43/44/45 found, read plan_graphics.md
directly (Tasks 351-394) rather than this file — this file intentionally keeps only a one-line
summary per task (see §3) to stay a genuinely quick-to-read handoff document.
```
