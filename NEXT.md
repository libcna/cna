# NEXT.md — CNA Project Handoff

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model (`Microsoft::Xna::Framework`),
built on SDL3 with a pluggable 3D graphics backend layer. It is a framework/runtime — not a game —
designed so XNA/FNA game code can be ported to C++ with minimal API-surface changes.

- **Main goal:** full XNA 4.0 API coverage with pixel-accurate behavior, backed by unit tests and
  pixel-readback integration tests, verified against the authoritative FNA reference source
  (`/rv/data/library/github.com/FNA-XNA/FNA/src`). Task-by-task progress lives in
  `GRAPHICS_TASKS.md`; per-phase synthesis docs live in `docs/*.md`.
- **Current development phase:** Phases 1–41 are complete. **Phase 42 ("BasicEffect exactness",
  Tasks 361–370) is open** — Tasks 361–366 are done, **Task 367 is next**. See §8 for the exact
  goal and §3 for what the last 6 tasks found. Full task-by-task detail (audit findings, exact
  formulas derived from FNA source, discriminating-power verification) lives in `GRAPHICS_TASKS.md`
  — this file intentionally does not duplicate it.
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
  (`cmake-build-bgfx`): all 3 configured, build cleanly. Last rebuilt/re-verified for Task 366.

### Test status (last verified: Task 366)
- **EasyGL, full `ctest -j1`:** 3415/3419 pass. 3 pre-existing/documented failures (see §5):
  `EasyGL_MRT_TwoAttachments`, `easy-gl-resource-smoke-tests`, `EasyGL_GraphicsDevice_ReferenceStencil`.
- **Vulkan, full `ctest -j1`:** 3337/3352 pass. 13 documented pre-existing failures (see §5) + 1
  order-dependent `Vulkan_FillMode_WireFrame`/`Vulkan_RenderTargetUsage` flake + 1 flaky `CueTest`.
- **Bgfx, full `ctest -j1`:** 3321/3322 pass. 1 flaky, unrelated `CueTest`/`NetworkSessionTest`
  failure (not a regression — passes cleanly in isolation).
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
- `BasicEffect` (Phase 42, Tasks 361–366 so far): all 22 property defaults now verified/correct
  against FNA (2 real default-value bugs fixed in Task 361, 1 more in Task 362).
  `EnableDefaultLighting()`'s exact constants confirmed correct (Task 363). Pixel-verified on all 3
  backends: no-texture diffuse-only rendering (Task 364, fixed 3 real per-backend bugs where
  `VertexColorEnabled` wasn't honored), vertex-color multiplication (Task 365, already correct),
  and texture × diffuse multiplication (Task 366, already correct).

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
- `BasicEffect::FillGpuDrawParams()` only forwards `DirectionalLight0` (never `SpecularColor`/
  `SpecularPower`/`DirectionalLight1`/`DirectionalLight2`) and omits `+EmissiveColor` from the
  disabled-lighting diffuse formula — both real mismatches vs. FNA, invisible in Tasks 364–366
  since those cases leave the affected properties at their defaults. Squarely Tasks 368/369's scope.

---

## 3. Recent changes

Most recent first. Full detail (exact FNA-derived formulas, discriminating-power verification,
per-backend fix shape) is in `GRAPHICS_TASKS.md` — this table is intentionally a one-line-per-task
index, not a duplicate.

| Commit | Task | Summary |
|---|---|---|
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

Older history (Phases 1–39): see `GRAPHICS_TASKS.md` and `docs/*.md` synthesis docs
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
`GraphicsDevice.Textures[i] = my3DTexture`. See `GRAPHICS_TASKS.md` Task 863.

The most significant *silent-failure* gaps (compile and run without error, wrong or no data):
Vulkan's `BlendState`/`DepthStencilState` support (Tasks 868/870), `TextureCube::DDSFromStreamEXT`
(Task 663), `Texture3D`/`TextureCube::GetData` on Vulkan/Bgfx (Task 865), `RenderTarget2D`'s
mip/MSAA params accepted but not wired on Vulkan/Bgfx (Tasks 878/879), and `BasicEffect`'s
missing `+EmissiveColor`/unforwarded specular+extra-lights terms (found Task 366, Task 369's job).
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
| Confirmed, incomplete | `PresentationMode::Letterbox`/`Overscan`/`Stretch`/`NativeBackBuffer` aren't distinctly implemented on EasyGL; Vulkan/Bgfx implement no virtual-resolution scaling at all. | Task 882 (not yet a formal `GRAPHICS_TASKS.md` row — referenced inline in Task 348) |
| Confirmed, pre-existing, out-of-repo | `easy-gl-resource-smoke-tests` aborts on an internal assert in the sibling `easy-gl` repo. | — |
| Confirmed, pre-existing | `Vulkan_DepthBias`'s `DepthBias=-1e6` sub-case fails; other sub-cases pass. | — |
| Confirmed, pre-existing, flaky | `Vulkan_FillMode_WireFrame`/`Vulkan_RenderTargetUsage`: order-dependent, only one fails per full-suite run. | — |
| Confirmed, architectural, not fixed | `GraphicsDevice`'s default `RasterizerState` is never pushed to any backend's actual GPU state at construction; Bgfx's hardcoded default happens to be the only one matching FNA's, so it alone silently culls standard-winding quads unless `CullNone` is set explicitly. | Task 884 (also covers the `EffectTechniqueCollection`/`EffectParameterCollection`/`EffectPassCollection` dangling-vector hazard class — Techniques fixed by Task 355, Parameters/Pass not yet exercised) |
| Confirmed, architectural, not fixed | `Effect::Clone()` doesn't exist — needs an ownership-model decision plus fixing an `EffectPass::Apply()` owner-aliasing hazard plus `Clone()` overrides in all 7 stock effects. | Task 883 |
| Confirmed, real, not fixed | `BasicEffect::FillGpuDrawParams()` only forwards `DirectionalLight0` (never `SpecularColor`/`SpecularPower`/`DirectionalLight1`/`DirectionalLight2`) and omits `+EmissiveColor` from the disabled-lighting diffuse formula. | Tasks 368/369 |
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
intentionally diverges from FNA, document it in the commit/PR description and in `GRAPHICS_TASKS.md`
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

In priority order — the first 4 are the rest of Phase 42, already scoped by `GRAPHICS_TASKS.md`;
the rest are the accumulated backlog from earlier phases (Tasks 863–884).

1. **Task 367 — pixel test: `TextureEnabled=true` AND `VertexColorEnabled=true` (texture × vertex color)**
   - Goal: derive the 3-way formula from FNA's `VSBasicTxVcNoFog`/`PSBasicTxNoFog` (texture sample ×
     vertex color × `DiffuseColor`), then pixel-verify on all 3 backends. This is the stride-24
     `VertexPositionColorTexture` path — distinct from the stride-16/20 paths Tasks 364–366 covered.
   - Files: `examples/{easygl,vulkan,bgfx}_basiceffect_texture_vertexcolor_enabled_test.cpp` (new).
   - Verification: `GetBackBufferData` center-pixel readback with a 3-way-distinguishable numeric
     triple (texel/vertex-color/`DiffuseColor` chosen so the correct product differs from any
     2-of-3 partial product). Remember the Bgfx `RasterizerState::CullNone` workaround (Task 884).

2. **Task 368 — pixel test: one directional light enabled**
   - Goal: verify `BasicEffect`'s lighting math (normal-dependent output) against FNA's per-vertex
     lighting formula when `LightingEnabled=true` with a single `DirectionalLight` on.
   - Files: new pixel test per backend; likely touches `BasicEffect.cpp`'s `FillGpuDrawParams()`
     if the Task 361 finding (`DirectionalLight1`/`2`/specular not forwarded) turns out to matter here.

3. **Task 369 — pixel test: ambient + emissive + specular combination**
   - Goal: harder reference case; this is where Task 366's deferred `+EmissiveColor` finding and
     Task 361's "only `DirectionalLight0` forwarded" finding need to actually be fixed — confirm
     both against FNA's `EffectHelpers.SetMaterialColor`/`BasicEffect.fx` first.
   - Files: `BasicEffect.cpp` (`FillGpuDrawParams()`), new pixel test per backend.

4. **Task 370 — cross-backend `BasicEffect` image comparison suite**
   - Goal: same scene rendered on EasyGL/Vulkan/Bgfx, compared for consistency — likely the natural
     place to close out Phase 42 with a synthesis doc (mirroring Task 340/350's precedent).

5. **Task 883 — implement `Effect::Clone()`** (needs: C++ ownership-model decision, fixing the
   `EffectPass::Apply()` `owner_`-aliasing hazard on clone, `Clone()` overrides in all 7 stock
   effects). Files: `Effect.hpp`/`.cpp` + all 7 stock-effect pairs.

6. **Task 884 — fix the `RasterizerState`-default GPU-sync gap** and the remaining
   `EffectParameterCollection`/`EffectPassCollection` by-value-vector dangling-pointer hazard.
   Files: each backend's device-construction path; `EffectParameterCollection.hpp`/
   `EffectPassCollection.hpp`.

7. **Task 881 — cap `SetRenderTargets` at FNA's real `MAX_RENDERTARGET_BINDINGS=4`.**
   Files: `GraphicsDevice.cpp` (`SetRenderTargets`). Verification: 5-target call throws, 1–4 work.

8. **Task 880 — wire `GraphicsDevice.Viewport` to a real GPU viewport on all 3 backends.**
   Files: `IGraphicsBackend.hpp`, `GraphicsDevice.cpp`, all 3 backends' graphics-backend `.cpp`.
   Verification: sub-region-viewport pixel test (should fail on all 3 backends today).

9. **Task 878/879 — implement real mip/MSAA render-target support on Vulkan and Bgfx**, mirroring
   Task 336/337's exact EasyGL fix shape. Files: each backend's render-target backend classes.

10. **Task 877 — wire `DepthStencilFormat`'s exact value into render-target depth/stencil
    attachments** on all 3 backends (currently hardcoded/coarse choices).

11. **Task 875/876 — Vulkan render-target bugs**: `Clear()`-only draws never record a render pass
    (875); `RenderTargetCube` via `EnvironmentMapEffect` renders black after unbind, root cause not
    isolated (876, needs isolation before a fix is attempted — see §9).

12. **Task 873/874 — fix Bgfx's wrong-handle-type `static_cast`s** for `RenderTarget2D`/
    `RenderTargetCube` sampling. Files: `BgfxGraphicsBackend.hpp`/`.cpp`.

13. **Task 663 — implement `TextureCube::DDSFromStreamEXT` for real** (build a real DDS cube-map
    test fixture *first*, then implement against it).

14. **Task 865 — implement real Vulkan `GetData` readback for `Texture3D`/`TextureCube`**
    (`vkCmdCopyImageToBuffer` + staging buffer, mirroring the existing upload path in reverse).

15. **Task 864 — reproduce and fix the suspected Vulkan/Bgfx mip-allocation bug** for
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
  Task 876 especially has 2 unisolated candidates (see `GRAPHICS_TASKS.md`).
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
Read NEXT.md first. Inspect only the files needed for the first task in §8 (Task 367).
Do not refactor unrelated code. Make one small, verified improvement.
Run the relevant build/test command before declaring the task done.
Update NEXT.md and GRAPHICS_TASKS.md after finishing, then commit AND push (standing
instruction — do not wait to be asked; one task = one commit = one push).

Current status: Phases 1-41 are FULLY COMPLETE. Phase 42 ("BasicEffect exactness",
GRAPHICS_TASKS.md Tasks 361-370) is open: Tasks 361-366 are DONE, Task 367 is NEXT (pixel test:
TextureEnabled=true AND VertexColorEnabled=true - texture x vertex color - EasyGL/Vulkan/Bgfx).

Last full 3-backend regression (Task 366, verify-only — no bug found):
EasyGL 3415/3419 pass (3 documented pre-existing failures).
Vulkan 3337/3352 pass (13 documented pre-existing failures + 1 order-dependent flake + 1 flaky CueTest).
Bgfx 3321/3322 pass (1 flaky, unrelated CueTest/NetworkSessionTest failure).
Caution: run all 3 backends' full ctest suites sequentially, never concurrently (see NEXT.md §2).

For the full history of what each task in Phase 41/42 found and fixed, read GRAPHICS_TASKS.md
directly (Tasks 351-366) rather than this file — this file intentionally keeps only a one-line
summary per task (see §3) to stay a genuinely quick-to-read handoff document.
```
