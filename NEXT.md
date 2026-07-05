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
- **Current development phase:** Phases 1–39 are complete. **Phase 40 (Viewport, DisplayMode, and
  adapter behavior, `GRAPHICS_TASKS.md` Tasks 341–350) is open** — Tasks 341–348 are all now done,
  **Task 349 is next** (see §8). Full phase history is in `GRAPHICS_TASKS.md`; the most recent
  closed phases have synthesis docs: `docs/depthstencilstate-support.md` (Phase 37),
  `docs/rasterizerstate-support.md` (Phase 38), `docs/rendertarget-support.md` (Phase 39).
  **Phase 40 connects directly to Task 880** (found in Phase 39): `Viewport` has zero GPU backend
  wiring on all 3 backends — Tasks 341–344 were all pure math/unit tests against the `Viewport`
  class in isolation (no `GraphicsDevice` involved), so none of them touched it. **Task 348**
  traced the real window-resize chain end-to-end and confirmed a related, deliberate CNA
  divergence from FNA: `PresentationParameters.BackBufferWidth`/`Height` do NOT follow a real
  window resize (only `Viewport` does, via `PresentationMode::FixedHeightDynamicWidth`'s
  fixed-height/dynamic-width scaling) — this is intentional (an existing source comment explains
  why matching FNA's behavior here would corrupt CNA's virtual-resolution feature), not a bug.
  Task 348 also opened **Task 882**: `PresentationMode`'s other 4 values (`Letterbox`/`Overscan`/
  `Stretch`/`NativeBackBuffer`) aren't distinctly implemented on EasyGL, and Vulkan/Bgfx don't
  implement virtual-resolution scaling at all — tracked, not fixed (needs its own dedicated,
  multi-backend investigation). The still-untouched viewport-reset-after-resize task (Task 349,
  next) will likely re-surface Task 880's GPU-wiring gap from a different angle; worth
  cross-referencing rather than re-diagnosing from scratch. Task 345 audited `GraphicsAdapter`
  against FNA (fixed 4 real bugs, most notably a dangling-reference bug in the old `DefaultAdapter`
  static field) and opened a finding closed by Task 347 (added FNA's missing `this[SurfaceFormat]`
  indexer to `DisplayModeCollection`). Task 346 (skipped one round, then picked up) verified
  `GraphicsAdapter`'s existing headless-CI fallback chain is structurally complete, fixed one
  small leak, and added a genuine (non-mocked) regression test for the "SDL video subsystem not
  initialized" enumeration-failure path.
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

---

## 2. Current status

### Build status
- **EasyGL** (`cmake-build-debug`), **Vulkan** (`cmake-build-vulkan`), and **Bgfx**
  (`cmake-build-bgfx`): all 3 configured, build cleanly, rebuilt and fully re-verified this
  session (Task 348 only adds a new EasyGL-only test + an inert `CMakeLists.txt` change — no
  shared production code touched — but all 3 backends were still reconfigured/rebuilt/tested per
  the project's standing regression discipline).

### Test status (last verified this session)
- **EasyGL, full `ctest -j1`:** 3351/3355 pass. 3 pre-existing/documented failures (see §5):
  `EasyGL_MRT_TwoAttachments`, `easy-gl-resource-smoke-tests`, `EasyGL_GraphicsDevice_ReferenceStencil`
  + 1 reconfirmed-flaky, unrelated `CueTest` failure.
- **Vulkan, full `ctest -j1`:** 3274/3288 pass. 13 documented failures (see §5) + 1 reconfirmed-flaky,
  unrelated `CueTest` failure (no order-dependent `Vulkan_RenderTargetUsage`/`Vulkan_FillMode_WireFrame`
  flake this run): 5× `Vulkan_BlendState_*` (Task 868), 5× `Vulkan_DepthStencilState_*` (Task 870),
  `Vulkan_GraphicsDevice_ReferenceStencil` (Task 872), `Vulkan_DepthBias` (one sub-case),
  `Vulkan_RenderTargetCube_SampleAfterUnbind` (Task 876 — genuine confirmed bug, supposed to fail
  until fixed).
- **Bgfx, full `ctest -j1`:** 3259/3259 pass — 100%, no flakes this run.
- **Caution:** run all 3 backends' full `ctest` suites **sequentially, never concurrently**
  — concurrent runs previously produced transient GPU/driver-contention false failures. If a
  single run shows an anomaly beyond the documented list, re-run that test in isolation before
  treating it as a regression.

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
- `RenderTarget2D`: constructors, `DepthStencilFormat`/`MultiSampleCount`/`RenderTargetUsage`, and
  now `IsContentLost`/`ContentLost` (Task 331) all match FNA at the property level. Basic
  render-to-texture round trip pixel-verified on EasyGL and Vulkan. Depth testing while rendering
  INTO a `RenderTarget2D` is genuinely functional on EasyGL and Vulkan, not just a stored property
  (Task 335) — though the exact `DepthStencilFormat` value isn't respected on any backend, only a
  coarse always/never (Vulkan) or hardcoded-format (EasyGL/Bgfx) choice (Task 877).
- `RenderTargetCube`: constructor, `DepthStencilFormat`/`MultiSampleCount`/`RenderTargetUsage`/
  `IsContentLost`/`ContentLost` all match FNA at the property level; `GetTypeName()` now correctly
  reports `"...RenderTargetCube"` instead of the inherited `"...TextureCube"` (Task 332).
  Sampling a `RenderTargetCube` back out via `EnvironmentMapEffect` after unbinding is
  pixel-verified working on **EasyGL only** (Task 334) — see below for Vulkan/Bgfx gaps.
- `RenderTarget2D`/`RenderTargetCube`'s `mipMap` is genuinely functional **on EasyGL**: `LevelCount`
  correctly reflects the full mip chain, GPU storage is pre-allocated for every level, and the
  chain is auto-regenerated from level 0 on unbind (mirroring FNA3D's `OPENGL_ResolveTarget`),
  pixel-verified via a mip-completeness probe (Task 336). Vulkan/Bgfx report the same correct
  `LevelCount` (shared computation) but don't yet back it with real GPU mips (Task 878).
- `RenderTarget2D`/`RenderTargetCube`'s MSAA (`preferredMultiSampleCount`) is genuinely functional
  **on EasyGL**: `MultiSampleCount` reflects the real, device-capability-clamped value (mirroring
  FNA's `ClosestMSAAPower`+`FNA3D_GetMaxMultiSampleCount`), a real multisampled renderbuffer is
  created and resolved via `glBlitFramebuffer` on unbind, pixel-verified with a genuine
  differential anti-aliasing proof (Task 337). Vulkan/Bgfx honestly report `MultiSampleCount=0`
  (not yet implemented, Task 879) rather than a fake pass-through.
- `SetRenderTarget`/`SetRenderTargets` correctly reset `Viewport`/`ScissorRectangle` to the newly
  bound target's size (or the backbuffer's, when unbinding), matching FNA exactly, on all 3
  backends (shared `GraphicsDevice.cpp` code) — `ScissorRectangle`'s reset has a real,
  pixel-verified GPU effect (Task 338); `Viewport`'s GPU effect is still a no-op everywhere
  (Task 880, see below).

### What does NOT work yet
- **Vulkan `BlendState`/`DepthStencilState` support is almost entirely fake** — hardcoded blend
  equations / depth-compare ops / no stencil testing at all, regardless of what's requested.
  Tracked as Task 868/Task 870, confirmed repeatedly via pixel tests, not fixed (large,
  multi-pipeline-site changes).
- `GraphicsDevice.ReferenceStencil`'s independent-override behavior has zero backend connection on
  all 3 backends (Task 872). `GraphicsDevice::Clear` ignores `ClearOptions::Stencil` everywhere
  (Task 871).
- `RenderTarget2D`/`RenderTargetCube`'s `mipMap` still doesn't produce real GPU mips on Vulkan/Bgfx
  (fixed on EasyGL, Task 336; Vulkan/Bgfx tracked as Task 878). Same shape for MSAA (fixed on
  EasyGL, Task 337; Vulkan/Bgfx honestly report `MultiSampleCount=0`, tracked as Task 879).
- `GraphicsDevice.Viewport` has **zero GPU backend wiring on all 3 backends** — every backend
  hardcodes its actual viewport call to the full render-target/window size regardless of what
  `Viewport` is set to (confirmed via code reading, found while doing Task 338). A sub-region
  viewport (split-screen, atlas-subrect rendering) currently has no effect anywhere. Tracked as
  Task 880, not fixed.
- `SetRenderTargets`'s simultaneous-target cap doesn't match FNA's real `MAX_RENDERTARGET_BINDINGS
  = 4`: EasyGL/Bgfx silently cap at 8 (no error, just truncates beyond that), Vulkan has no
  CNA-level cap at all (relies on the raw GPU's own limit). Found while auditing Task 339 — no
  real game code here is affected (no test exercises >2 targets), tracked as Task 881, not fixed.
- `EasyGL_MRT_TwoAttachments` (Task 145): even a basic, same-size/format 2-target MRT setup
  doesn't render correctly on EasyGL — attachment 1 stays black. Pre-existing, off-limits for
  opportunistic fixing (needs its own dedicated root-cause investigation, see §9).
- `Texture3D`/`TextureCube::GetData` is a total silent no-op on Vulkan/Bgfx (Task 865).
  `TextureCube::DDSFromStreamEXT` is a non-functional stub (Task 663).
- `Texture2D::SetData(level>0,...)` is a silent no-op on Vulkan/Bgfx; EasyGL's non-mip-aware
  filters render solid black on mip-incomplete textures (Task 867).
- `SpriteBatch`'s `SamplerState` is a no-op on Vulkan/Bgfx (EasyGL only). Multiple
  `SpriteBatch::Begin()`/`End()` per frame on Vulkan: only the last batch renders.
- `Texture3D` sampling cannot be wired into any shader without an architecture change (Task 863).
- On Bgfx, `SpriteBatch::Draw`ing a `RenderTarget2D` as a texture reads the wrong handle type
  (`BgfxRenderTargetBackend::fbo` instead of `::colorTex`, via an invalid `static_cast` to the
  unrelated `BgfxTextureBackend` type) — confirmed by layout analysis, doesn't crash but samples
  wrong/garbage data (Task 873).
- Same bug shape on Bgfx for `RenderTargetCube` sampled via `EnvironmentMapEffect` — reads
  `BgfxRenderTargetCubeBackend::fbo` instead of `::cubeTex` (Task 874, found this session).
- On Vulkan, `SetRenderTarget(rt); Clear(color); SetRenderTarget(nullptr);` with **no draw call**
  in between never gets a render pass recorded — the RT's image stays `VK_IMAGE_LAYOUT_UNDEFINED`
  forever (Task 875, found this session).
- On Vulkan, sampling a `RenderTargetCube` via `EnvironmentMapEffect` after unbinding renders
  black instead of its actual rendered content, even with a real draw call into each face —
  root cause not yet isolated (Task 876, found this session).
- No backend honors the exact requested `DepthStencilFormat` for a render target's depth/stencil
  attachment: EasyGL always allocates depth-only `DepthComponent24` (no stencil bits, ever);
  Vulkan always allocates a depth buffer regardless of `hasDepth`, using the device-global depth
  format; Bgfx always uses `D24S8` regardless of the exact format requested (most correct of the
  three, but still not format-exact). The core depth-TEST functionality itself does work on
  EasyGL/Vulkan (Task 335) — this is specifically about format fidelity (Task 877, found this
  session).

---

## 3. Recent changes

Most recent first. Full history (including everything before Task 271, and full detail for every
task below) is in `GRAPHICS_TASKS.md` and `git log`.

| Commit / Task | Change |
|---|---|
| Task 348 | Opened Phase 40's third sub-area. Traced the real window-resize chain end-to-end: `SDL_EVENT_WINDOW_RESIZED` → `Game`'s event loop → `GameWindow::updateFromSDL()` → `GameWindow.ClientSizeChanged` → `GraphicsDeviceManager::INTERNAL_OnClientSizeChanged` → `GraphicsDevice::UpdateViewportFromWindow()`. **Confirmed a real, deliberate FNA divergence, correctly not "fixed"**: FNA's `INTERNAL_OnClientSizeChanged` forwards the new window size into `PresentationParameters.BackBufferWidth`/`Height` (full device Reset on every resize); CNA's doesn't, by design (an existing accurate comment explains why — would corrupt `FixedHeightDynamicWidth`'s virtual-resolution scaling). **Genuine discovery while verifying the new test's discriminating power**: `Viewport` tracking doesn't actually depend on the `ClientSizeChanged` event at all — `GraphicsDevice::Present()` unconditionally refreshes it every frame, a stronger guarantee than FNA's — so the event chain itself needed its own separately-discriminating check. **Closed a real test-coverage gap**: Task 227's existing test only covers the GDM-API-driven resize path; new `examples/easygl_real_window_resize_test.cpp` (`EasyGL_RealWindowResize`) calls `SDL_SetWindowSize()` directly on the live window and verifies 4 things: Viewport height pinned, Viewport width changes, `BackBufferWidth`/`Height` unchanged (regression marker), and `ClientSizeChanged` fires. EasyGL-only (matches Task 227's precedent — Vulkan/Bgfx have no virtual-resolution scaling to pin). **New findings deferred to Task 882**: `PresentationMode::Letterbox`/`Overscan`/`Stretch`/`NativeBackBuffer` aren't distinctly implemented on EasyGL (only `FixedHeightDynamicWidth` has real logic); Vulkan/Bgfx implement no virtual-resolution scaling at all. Full 3-backend rebuild + regression (new EasyGL-only test, inert `CMakeLists.txt` change): EasyGL 3351/3355 (3 pre-existing + 1 reconfirmed-flaky `CueTest`). Vulkan 3274/3288 (13 pre-existing + 1 reconfirmed-flaky `CueTest`, no order-dependent flake this run). Bgfx 3259/3259 (100%). |
| Task 346 | Verified the headless-CI fallback chain already present in `GraphicsAdapter.cpp` is structurally complete: `AdaptersChanged()` falls back to a synthetic 800×480 "Default Display" adapter when `SDL_GetDisplays()` fails; `queryDisplayModes()`/`queryCurrentDisplayMode()` fall back similarly for a display that enumerates but whose modes/current-mode queries fail (including the all-null-pointers edge case, already correctly handled). **Fixed one real small bug**: the no-displays branch never called `SDL_free(displays)` before returning — a latent leak when SDL returns a non-null array with `count<=0` — fixed with an unconditional (documented-safe-on-null) `SDL_free(displays)`. **Added a genuine, non-fabricated regression test**: confirmed via a standalone probe that `SDL_GetDisplays()` reliably fails with "Video subsystem has not been initialized" whenever `SDL_INIT_VIDEO` isn't initialized — the real headless-CI scenario, not a mocked one. New `HeadlessFallback_NoVideoSubsystemProducesSingleSyntheticAdapter` test: skips if something else left video initialized (every other SDL-touching test in this project balances its own init/quit calls, so this reliably holds), otherwise calls `AdaptersChanged()` directly and verifies the synthetic adapter's exact values, then restores a real enumeration afterward. Verified it exercises the real path both standalone and interleaved with `GameWindowTests.cpp`. Full 3-backend rebuild + regression: EasyGL 3351/3354 (3 pre-existing, unchanged). Vulkan 3274/3288 (13 pre-existing + 1 reconfirmed order-dependent flake — `Vulkan_FillMode_WireFrame` this run). Bgfx 3258/3259 (1 reconfirmed-flaky `CueTest`). |
| Task 347 | Closes the finding from Task 345's `GraphicsAdapter` audit. Compared `DisplayModeCollection` against FNA's `Graphics/DisplayModeCollection.cs` line-by-line: enumeration (`begin()`/`end()`) and integer indexing (`operator[](int)`, out-of-range throwing) already matched and were fully tested. **Fixed the real gap**: added FNA's missing `this[SurfaceFormat]` indexer as a second `operator[](SurfaceFormat)` overload (C++ overloads `operator[]` by parameter type — coexists cleanly with the int overload). Also `NOXNA`-wrapped `getCountProperty()`/integer `operator[]`, which don't exist in FNA's API at all (a `CLAUDE.md` violation on their own, matching `begin()`/`end()`'s existing correct treatment — `NOXNA` is a no-op marker, zero behavior change). New tests: `IndexBySurfaceFormatReturnsOnlyMatchingModesInOriginalOrder`, `IndexBySurfaceFormatReturnsEmptyWhenNoModeMatches`, `IndexBySurfaceFormatOnEmptyCollectionReturnsEmpty`. Verified discriminating power by temporarily making the new indexer return every mode unconditionally and confirming the 2 filter-correctness tests fail, before reverting. Full 3-backend rebuild + regression (header-only addition, widely included): EasyGL 3349/3353 (3 pre-existing + 1 reconfirmed-flaky `CueTest`). Vulkan 3274/3287 (13 pre-existing, unchanged, no flakes this run). Bgfx 3258/3258 (100%). |
| Task 345 | Audited `GraphicsAdapter` against FNA's `GraphicsAdapter.cs` + `SDL3_FNAPlatform.cs`. Fixed 4 real bugs: (1) `AdaptersChanged()` swapped `DeviceName`/`Description` — FNA gives them different values (`DeviceName`=synthetic `\\.\DISPLAYn`, `Description`=real display name). (2) `queryDisplayModes()` didn't dedupe modes differing only by refresh rate and iterated forward instead of FNA's reverse order — fixed to match exactly. (3) **Most severe**: `static GraphicsAdapter& DefaultAdapter` was a raw reference bound once at static-init time; since `AdaptersChanged()` destroys and recreates every adapter, any later call (an intended, FNA-documented usage) left it — and 2 production call sites in `GraphicsDeviceManager.cpp` — dangling. Removed the field entirely (FNA's own `DefaultAdapter` is a property, always re-evaluated) and repointed both call sites at the existing, always-fresh `getDefaultAdapterProperty()`. (4) Stale Doxygen comments on `getDeviceIdProperty()`/`getVendorIdProperty()` claimed "not implemented" but both are real (Linux PCI sysfs query) — corrected. Documented (not reverted) an intentional deviation: FNA throws `NotImplementedException` for `DeviceId`/`Revision`/`SubSystemId`/`VendorId`; CNA provides real values for the first two. Removed a dead `friend class GraphicsAdapterFactory` (class doesn't exist). Closed a real test-coverage gap — zero `GraphicsAdapter` tests existed despite a stale comment claiming otherwise — with new `GraphicsAdapterTests.cpp` (23 tests). Verified discriminating power for both the swap and dedup fixes by reverting each and confirming the corresponding test fails. **New finding deferred to Task 347**: FNA's `DisplayModeCollection` has a `this[SurfaceFormat]` indexer CNA lacks entirely, while CNA's extra `Count`/integer-`operator[]` (not in FNA) aren't `NOXNA`-wrapped. Full 3-backend rebuild + regression (touches shared `GraphicsDeviceManager.cpp`): EasyGL 3347/3350 (3 pre-existing, unchanged). Vulkan 3269/3284 (13 pre-existing + 2 reconfirmed-flaky `CueTest`; one `Vulkan_DepthBias` 0/4 anomaly reconfirmed as the normal 3/4 pattern in isolation). Bgfx 3254/3255 (1 reconfirmed-flaky `CueTest`). |
| Task 344 | Confirmed by reading FNA's `Viewport.cs` (Task 341's audit) that `MinDepth`/`MaxDepth` setters have zero validation/clamping, and `Project`/`Unproject` use them in unguarded arithmetic. Added 3 new `ViewportTests.cpp` tests: `ProjectWithMinDepthGreaterThanMaxDepthProducesInvertedZWithoutThrowing`, `ProjectUnprojectRoundTripWithInvertedMinMaxDepth` (MinDepth=1,MaxDepth=0 — confirms no reordering/clamping), and `UnprojectWithEqualMinMaxDepthProducesNonFiniteResult` (MinDepth==MaxDepth — confirms genuine IEEE-754 NaN propagation via `std::isnan`, not a guarded fallback). **Verified genuine discriminating power**: temporarily added a "protective" min/max swap to `Project` and a divide-by-zero guard to `Unproject` (the exact kind of FNA-deviating fix a well-intentioned future change might introduce) and confirmed all 3 new tests fail, before reverting both back to the committed Task 341 state. Test-file-only change: EasyGL ctest 3323/3327 (3 pre-existing + 1 reconfirmed-flaky `CueTest`, confirmed passing in isolation). Vulkan ctest 3247/3261 (13 pre-existing + 1 reconfirmed order-dependent flake — `Vulkan_FillMode_WireFrame` this run). Bgfx ctest 3231/3232 (1 reconfirmed-flaky `CueTest`). |
| Tasks 342-343 | Task 341's audit found every pre-existing `Project`/`Unproject` test used identity matrices only, so the perspective-divide branch (`if (!MathHelper::WithinEpsilon(a, 1.0f))`) in both methods was never actually exercised. Added 4 new `ViewportTests.cpp` tests using a real `Matrix::CreatePerspectiveFieldOfView` + `Matrix::CreateLookAt`: `ProjectWithNonIdentityPerspectiveMatrix`/`ProjectWithNonIdentityViewAndProjectionMatrices` (Task 342, hand-derived expected values from FNA's own formulas), `UnprojectRecoversOriginalPointThroughNonIdentityPerspectiveMatrix`/`ProjectUnprojectRoundTripWithNonIdentityViewAndProjectionMatrices` (Task 343). **Verified genuine discriminating power**: temporarily forced each method's own perspective-divide condition to `false` in turn and confirmed exactly the tests that depend on that method's divide branch fail (Project's 2 fail when Project's divide is disabled; Unproject's 2 fail when Unproject's divide is disabled, independently) before reverting both back to the committed Task 341 state. Test-file-only change (no production code touched): EasyGL ctest 3321/3324 (3 pre-existing, unchanged). Vulkan ctest 3242/3258 (13 pre-existing + 1 reconfirmed-flaky `CueTest` + 1 reconfirmed order-dependent `Vulkan_RenderTargetUsage` flake + 1 one-off `DynamicSoundEffectInstanceTest` timing flake, confirmed passing in isolation). Bgfx ctest 3229/3229 (100%). |
| Task 341 | **Opens Phase 40.** Audited `Viewport` against FNA's `Graphics/Viewport.cs` line-by-line: `Project`/`Unproject` math, `AspectRatio`, `Bounds`, `TitleSafeArea`, `ToString()` all already matched FNA exactly — no math bug. **Fixed a real CLAUDE.md convention violation**: `X`/`Y`/`MinDepth`/`MaxDepth` were public raw fields with no getter/setter, while `Height`/`Width` in the same class correctly used `getXProperty()`/`setXProperty()` — converted all 4 to the same `DEF_PROP`-backed pattern (private `X_`/`Y_`/`MinDepth_`/`MaxDepth_`, member order now matches FNA's C# order: Height, MaxDepth, MinDepth, Width, Y, X). Updated the only 3 call sites touching the raw fields (`GraphicsDevice::UpdateViewportFromWindow()`, `ViewportTests.cpp`, `easygl_viewport_state_test.cpp`) and added a new `SettersUpdateEachFieldIndependently` test (previously only constructors were tested, never the setters). Full 3-backend rebuild + regression (header change touches everything including `Viewport.hpp`): EasyGL 3316/3320 (3 pre-existing + 1 reconfirmed-flaky `CueTest`). Vulkan 3239/3254 (13 pre-existing + 2 reconfirmed-flaky `CueTest`). Bgfx 3224/3225 (1 reconfirmed-flaky `CueTest`). |
| Task 340 | **Closes Phase 39.** Wrote `docs/rendertarget-support.md`, a full Phase 39 synthesis (mirroring `docs/rasterizerstate-support.md`'s Phase 38 closer) covering Tasks 331–340: 2 real EasyGL fixes shipped (Task 336 mip support, Task 337 MSAA support), 1 shared-code fix (Task 338's Viewport/ScissorRectangle reset on RT switch), and the per-backend MRT limits table (EasyGL/Bgfx cap at 8, Vulkan uncapped at the CNA level, none matching FNA's real 4-target limit — Task 881). Docs-only task, no code changed, no regression risk. Phase 40 (Viewport, DisplayMode, adapter behavior, Tasks 341–350) is already planned in `GRAPHICS_TASKS.md` and opens next — directly relevant to Task 880 (Viewport has zero GPU wiring). |
| Task 339 | Audit-only closure (no code changed, mirrors Task 330's precedent) — read FNA's actual `SetRenderTargets` source and confirmed it does **zero explicit validation** of format/size/count mismatches between MRT targets; it computes dimensions from `renderTargets[0]` only and delegates everything to the native driver. "Reject invalid combinations" simply isn't an XNA-level behavior. Confirmed CNA's own 3 backends behave the same way (no CNA-level validation, delegate to GL/Vulkan/bgfx) — consistent with FNA, not a divergence. **Found one real, minor, new divergence**: FNA's actual MRT cap is `MAX_RENDERTARGET_BINDINGS=4` (implicit, via a fixed-size array that would throw past 4); CNA's EasyGL/Bgfx silently cap at 8 (no throw), Vulkan has no CNA-level cap at all. Tracked as new **Task 881**, not fixed (no test exercises >2 targets, low priority). Correctly did NOT touch the already-tracked, off-limits `EasyGL_MRT_TwoAttachments` (Task 145) bug — even the basic same-size 2-target MRT case is already known-broken on EasyGL, which blocks any *meaningful* deeper mismatched-format verification; re-diagnosing it here would violate the project's own "each needs its own dedicated investigation" rule. No new test — nothing in FNA to assert against, and the one concrete regression-worthy angle (Task 881's cap divergence) has no existing >2-target test infrastructure to extend. No regression risk, no code changed. |
| Task 338 | Verified `SetRenderTarget(nullptr)`/`SetRenderTargets({})` return to the backbuffer — the core routing was already extensively proven by dozens of existing tests. **Found and fixed a real gap while auditing FNA's actual `SetRenderTargets` source**: FNA *always* resets `Viewport`/`ScissorRectangle` to `(0,0,newWidth,newHeight)` on every render-target switch (new target's size when binding, backbuffer's when unbinding) — confirmed CNA's `SetRenderTarget`/`SetRenderTargets` never touched either property at all. Added `GraphicsDevice::ResetViewportAndScissorForRenderTarget`, wired into all 3 `SetRenderTarget*`/`SetRenderTargets` overloads, matching FNA's exact placement. New `examples/rendertarget_viewport_scissor_reset_test.cpp` (EasyGL + Vulkan) proves both the property values AND a real GPU-level effect (a stale scissor rect from before an RT switch no longer incorrectly clips draws afterward). **Found and deliberately deferred a separate, much bigger gap discovered along the way**: `GraphicsDevice.Viewport` has **zero GPU wiring on any of the 3 backends** — every backend hardcodes its actual viewport to the full target size, ignoring `Viewport` entirely; a sub-region viewport currently has no effect anywhere. Tracked as new **Task 880**, not fixed here (unrelated in scope to render-target switching specifically, needs its own dedicated 3-backend task). Full regression, all 3 backends: EasyGL ctest 3316/3319 (3 pre-existing, unchanged). Vulkan ctest 3238/3253 (13 pre-existing + 1 reconfirmed-flaky unrelated `CueTest`). Bgfx ctest 3223/3224 (1 reconfirmed-flaky unrelated `CueTest`). |
| Task 337 | Confirmed and **actually fixed** MSAA render target support on EasyGL, reusing Task 336's exact resolve-on-unbind mechanism and fix shape. Following FNA's real mechanism (`ClosestMSAAPower` + `FNA3D_GetMaxMultiSampleCount` clamp, then a real multisampled renderbuffer resolved via `glBlitFramebuffer` when the target is unbound — the same `OPENGL_ResolveTarget` function Task 336 already touched for mips). Added `ClosestMSAAPower` to `RenderTarget2D.cpp`/`RenderTargetCube.cpp`; threaded `multiSampleCount` through `IGraphicsBackend::CreateRenderTarget2D`/`CreateRenderTargetCube` (all 3 backends); added `GetMultiSampleCount()` to both render-target backend interfaces so the XNA layer queries the backend's REAL clamped value post-construction, rather than reporting the raw request. **EasyGL**: creates a real multisampled color (+depth) renderbuffer, resolves it into the sampleable texture on unbind (same call site as Task 336's mip regen, correctly ordered — resolve then mip-regenerate). RenderTargetCube reuses one shared multisample renderbuffer across all 6 faces (matching FNA's single `glColorBuffer`), tracking the last-bound face for the resolve. **Rigorously pixel-verified with a genuine anti-aliasing proof** (not just solid-fill plumbing, which even a non-MSAA target passes trivially): new `easygl_rendertarget2d_msaa_test.cpp` renders a diagonal-edged triangle into `MultiSampleCount=0` and `=8` RTs, and confirms the `0` case is purely binary (hard aliased edge) while the `8` case has genuinely intermediate (partially-covered) pixel values. Updated Task 331/332's property tests: unlike `LevelCount` (backend-agnostic), `MultiSampleCount` is legitimately backend/device-capability-dependent even in real FNA, so the tests now accept either EasyGL's real clamped value or Vulkan/Bgfx's honest `0`, while still catching a blind pass-through (literal `9999`) as a failure on any backend. Also fixed a documentation typo from the Task 336 session (several comments said "Task 877" instead of "Task 878" for the Vulkan/Bgfx mip gap). **Vulkan/Bgfx**: accept-and-ignore `multiSampleCount`, report `0` — tracked as new **Task 879**. Full 3-backend rebuild + regression: EasyGL 3315/3318 (3 pre-existing, unchanged). Vulkan 3239/3252 (13 pre-existing, unchanged). Bgfx 3224/3224 (100%). |
| Task 336 | Confirmed and **actually fixed** render target mipmap support on EasyGL, following FNA3D's real native-source mechanism (`OPENGL_ResolveTarget`: mips auto-regenerate from level 0 via `glGenerateMipmap` when a mipmapped RT stops being the active target). Threaded `mipMap` through `IGraphicsBackend::CreateRenderTarget2D`/`CreateRenderTargetCube` (all 3 backends' signatures updated); `RenderTarget2D.cpp`/`RenderTargetCube.cpp` now compute real `LevelCount` (matching `Texture2D`/`TextureCube`'s own pattern, previously a `mipMap ? 1 : 1` no-op). **EasyGL**: pre-allocates every mip level's GPU storage at RT construction; discovered `IRenderTargetBackend`/`IRenderTargetCubeBackend::UnbindAsRenderTarget()` were **completely dead code** (never called anywhere) — added `currentRt2D_`/`currentRtCube_` tracking to `EasyGLGraphicsBackend` so switching away from a bound RT/cube-face now actually calls it, which regenerates mips when needed. Pixel-verified with a new mip-completeness probe (`TextureFilter::Anisotropic` renders solid black on GL-incomplete mip chains — reused Task 867/299's established diagnostic signature); verified the test genuinely discriminates by temporarily forcing a failure case. Updated Task 331/332's property tests' pinned "known gap" assertions to the new correct values (`LevelCount==7`). **Vulkan/Bgfx**: accept-and-ignore `mipMap` (no functional change) — tracked as new **Task 878**, matching the project's existing Task 867 precedent (property correct everywhere, GPU support lags per-backend). Full 3-backend rebuild + regression pass (interface change touched all 3): EasyGL 3312/3317 (3 pre-existing + 2 reconfirmed-flaky unrelated `CueTest` failures). Vulkan 3239/3252 (13 pre-existing, unchanged). Bgfx 3224/3224 (100%). |
| Task 335 | Verified depth buffer creation for render targets is functional, not just a stored property. New backend-agnostic test `examples/rendertarget2d_depth_test.cpp` (`EasyGL_RenderTarget2D_DepthBuffer`/`Vulkan_RenderTarget2D_DepthBuffer`): draws a near GREEN quad then a far RED quad into a `RenderTarget2D` with `DepthFormat::Depth24Stencil8` and `DepthStencilState::Default`, then samples the RT back via `SpriteBatch` (the already-proven sampling-after-unbind path). **PASSES on both EasyGL and Vulkan** — depth testing genuinely works inside render targets, not just the backbuffer. **3 real, scoped format-fidelity gaps found** (not fixed here, tracked as new **Task 877**): EasyGL's `EasyGLRenderTargetBackend`/`EasyGLRenderTargetCubeBackend` both hardcode `DepthComponent24` — a `Depth24Stencil8` request silently gets zero stencil bits; Vulkan's `VulkanRenderTargetBackend` drops its `hasDepth` parameter entirely — every RT gets a depth buffer regardless of request, using the device-global depth format rather than the requested one; Bgfx (code-reading + Task 179's existing smoke coverage only) is the most correct of the three — respects `hasDepth`, uses `D24S8` (has stencil) — but still doesn't differentiate exact `DepthFormat` values. EasyGL ctest: 3313/3316 (3 pre-existing/documented, unchanged). Vulkan ctest: 3239/3252 (13 documented, unchanged, new test passes). |
| Task 334 | Verified `RenderTargetCube` can be sampled as `TextureCube` after unbinding, via `EnvironmentMapEffect` — no existing test covered this at all (Task 142's `vulkan_rtcube_test.cpp` renders into all 6 faces but never samples the cube back out). Wrote new tests on all 3 backends. **EasyGL: PASSES**, exact blue match, architecturally sound (virtual `BindGL()` dispatch, no unsafe cast). **Vulkan: FAILS, two distinct real bugs found**: (1) a `Clear()`-only version of the test showed every cube face stuck at `VK_IMAGE_LAYOUT_UNDEFINED` — `VulkanGraphicsBackend::Clear()` only sets a global clear-colour scalar and never registers the bound RT as "used" (only an actual draw call does) — tracked as **Task 875**. (2) After switching to a real `SpriteBatch` draw per face (working around #1), the test still renders black instead of blue — root cause not isolated (candidates: `SpriteBatch`-into-cube-face correctness was never itself pixel-verified before now, or `EnvironmentMapEffect`'s descriptor-set caching) — tracked as **Task 876**. A render-pass-compatibility validation warning also appears but is a confirmed red herring (present in Task 142's own already-passing test too). **Bgfx: same unsafe-cast bug shape as Task 873**, confirmed by layout analysis (`static_cast<BgfxTextureCubeBackend&>` on a `BgfxRenderTargetCubeBackend` reads `fbo` where `handle` should be) — tracked as **Task 874**, doesn't crash (new smoke test confirms), can't be pixel-verified. EasyGL ctest: 3312/3315 (unchanged, 3 documented). Vulkan ctest: 3237/3251 (13 documented + 1 new correctly-failing test). Bgfx ctest: 3224/3224 (100%). |
| Task 333 | Verified `RenderTarget2D` can be sampled as `Texture2D` after unbinding. EasyGL (Task 87) and Vulkan (Task 148) already had passing pixel tests doing exactly this — reconfirmed, no change needed. **Found and confirmed a new, severe, previously-only-suspected Bgfx bug** (Task 179's test had an informal comment guessing at it): `BgfxSpriteBatchBackend::Draw` casts a `RenderTarget2D`'s backend (`BgfxRenderTargetBackend`) to the unrelated `BgfxTextureBackend` type via `static_cast`, reading its `fbo` (framebuffer handle) where `textureHandle` should be — confirmed by direct memory-layout analysis (both are `struct { uint16_t idx; }`, so it compiles and doesn't crash, but samples a framebuffer-pool handle as if it were a texture-pool handle). New `bgfx_render_target_sample_test.cpp` (`Bgfx_RenderTarget2D_SampleAfterUnbind`) confirms no crash (consistent with silent wrong-data sampling, not an error). Tracked as Task 873, not fixed here (needs a scoped Bgfx-only fix plus non-visual verification, since Bgfx has no pixel readback). Rebuilt and fully re-verified Bgfx this session: 3223/3223 (100%), first full run in several sessions. EasyGL: 3311/3314 (unchanged). Vulkan: 3237/3250 (unchanged). |
| Task 332 | Audited `RenderTargetCube` against FNA's `RenderTargetCube.cs` line-by-line — same shape as Task 331, one class over. Most of it already matched FNA (unlike `RenderTarget2D`, `RenderTargetCube` already had `IsContentLost`/`ContentLost`). **Fixed**: `GetTypeName()` was never overridden, so a `RenderTargetCube` reported itself as `"...TextureCube"` — added the override. **Confirmed the known lead** (`mipMap`/`MultiSampleCount` silently ignored) is the same shape as Tasks 336/337, already covered by those general tasks, not new. **Found and deliberately did NOT fix** (architecture-blocked): tried to add `RenderTarget2D`'s `Dispose(bool)` "still bound" guard, but it doesn't compile — `RenderTargetBinding` only stores `Texture*`, and `RenderTargetCube` doesn't inherit `Texture` (Task 863). Also confirmed `GraphicsDevice::SetRenderTarget(RenderTargetCube*, CubeMapFace)` never records the binding at all, so `GetRenderTargets()` can never see a bound cube face — a direct consequence of Task 863, not a new independent bug. New test `examples/easygl_rendertargetcube_properties_test.cpp` (`EasyGL_RenderTargetCube_Properties`/`Vulkan_RenderTargetCube_Properties`, 15/15 both backends). EasyGL ctest: 3311/3314. Vulkan ctest: 3237/3250 (both: only documented pre-existing failures). |
| `3fdb6c6` Task 331 | **Opens Phase 39.** Audited `RenderTarget2D` against FNA line-by-line. Fixed a real gap: added missing `IsContentLost`/`ContentLost` (mirroring `RenderTargetCube`). Found and deliberately deferred two gaps to dedicated tasks: `mipMap` ignored (Task 336), `MultiSampleCount` not clamped/wired (Task 337). New pixel-free property test on both backends (15/15 pass each). |
| `e81d443` Task 330 | **Closes Phase 38.** Confirmed (no bug) `RasterizerState` has no freeze/immutability enforcement, matching FNA. Wrote `docs/rasterizerstate-support.md` synthesizing Phase 38 — found **no new tracked bugs**, only test-coverage gaps. |
| `4ab72c7` Task 326 | Registered the existing backend-agnostic `FillMode` pixel test for EasyGL too (previously Vulkan-only). No bug found. |
| `14e58da` Tasks 323–325 | One `CullMode` pixel test (contrast-checked across `None`/`CullClockwiseFace`/`CullCounterClockwiseFace`, 6/6 both backends) satisfies all 3 tasks. Found (not fixed, out of scope) Task 318's quad-naming was backwards. |
| `b61aee8` Task 322 | Extended `GraphicsDevice`'s default-`RasterizerState` test to the full 6-property surface. No bug. |
| `c18b0f3` Task 321 | **Opens Phase 38.** Fixed the last portion of Task 866 (preset `Name` gap) — closes Task 866 entirely across all 4 state classes. |
| `ba6011e` Task 320 | **Closes Phase 37.** `docs/depthstencilstate-support.md` synthesis. |
| `6652573` Tasks 318–319 | 5th reconfirmation of Task 870 (Vulkan stencil fake). Fixed a `ReferenceStencil`-propagation bug (Task 309-shaped); found a 2nd universal bug — `ReferenceStencil` has zero backend connection anywhere (new Task 872). |
| `95abf99`/`d86c1f4`/`c1d8e74`/`65d3d21`/`eccbb9e` Tasks 313–317 | Per-property `DepthStencilState` pixel tests; Task 313 discovered Task 870 (Vulkan depth/stencil almost entirely fake), reconfirmed 4 more times; Task 315 found and **fixed** a real bug (`SDL_GL_STENCIL_SIZE` never requested on EasyGL); found Task 871 (`Clear` ignores stencil). |
| `a1bcf20` Tasks 311–312 | **Opens Phase 37.** Fixed `DepthStencilState`'s preset `Name` gap; fixed `GraphicsDevice`'s default `DepthStencilState`/`RasterizerState` never actually copying their FNA-specified presets. |

Older history (Phases 34–36, Tasks 271–310): see `GRAPHICS_TASKS.md` and
`docs/sampler-state-support.md`. Headline: Task 293 fixed a severe, project-wide bug (per-slot
`SamplerState` silently ignored by all 3D draws, all 3 backends); Task 304 found Vulkan's
`BlendState` support is almost entirely fake (Task 868, not fixed, confirmed 5×).

---

## 4. Current blocker / main problem

**There is no build-breaking or test-breaking blocker.** The repository builds and the test suites
pass at the rates given in §2 on EasyGL and Vulkan (Bgfx unverified this session, last known-good).

The most significant *correctness* gap is architectural, not a build/test failure: `Texture3D`/
`TextureCube` do not inherit `Texture` in CNA (they inherit `GraphicsResource` directly), which
structurally prevents `Texture3D` from ever being sampled via the normal
`GraphicsDevice.Textures[slot]` path. No failing command or test is tied to this — it manifests as
a compile-time impossibility if game code tries `GraphicsDevice.Textures[i] = my3DTexture` the way
real XNA/FNA code would. See `GRAPHICS_TASKS.md` Task 863.

The most significant *silent-failure* gaps (compile and run without error, wrong or no data):
Vulkan's `BlendState`/`DepthStencilState` support (Tasks 868/870), `TextureCube::DDSFromStreamEXT`
(Task 663), `Texture3D`/`TextureCube::GetData` on Vulkan/Bgfx (Task 865), and `RenderTarget2D`'s
`mipMap`/`MultiSampleCount` params being accepted but not actually wired to any backend
(Tasks 336/337, found this session). None have a test that currently fails loudly — they're only
visible via dedicated pixel tests or direct code reading.

---

## 5. Known bugs and limitations

| Status | Issue | Tracking |
|---|---|---|
| Confirmed, MASSIVE, not fixed | Vulkan's `BlendState` support is almost entirely fake — hardcodes one blend equation regardless of request. Confirmed 5× via pixel tests. EasyGL fully correct. | Task 868 |
| Confirmed, MASSIVE, not fixed | Vulkan's `DepthStencilState` support is almost entirely fake — `DepthBufferFunction` hardcoded, entire stencil-test parameter set unused. Confirmed 5× via pixel tests. EasyGL fully correct. | Task 870 |
| Confirmed, universal, not fixed | `GraphicsDevice.ReferenceStencil`'s independent-override has zero backend connection on all 3 backends. | Task 872 |
| Confirmed, universal, not fixed | `GraphicsDevice::Clear` ignores `ClearOptions::Stencil` on every backend. | Task 871 |
| Fixed on EasyGL, not fixed on Vulkan/Bgfx | `RenderTarget2D`/`RenderTargetCube`'s `mipMap` produces a real, pixel-verified mip chain on EasyGL (Task 336); Vulkan/Bgfx report the correct `LevelCount` but don't yet allocate/generate real GPU mips. | Task 878 |
| Fixed on EasyGL, not fixed on Vulkan/Bgfx | `RenderTarget2D`/`RenderTargetCube`'s MSAA produces a real, pixel-verified anti-aliased resolve on EasyGL (Task 337); Vulkan/Bgfx honestly report `MultiSampleCount=0` (not a fake pass-through) rather than implementing real multisample attachments. | Task 879 |
| Confirmed, universal, not fixed | `GraphicsDevice.Viewport` is decorative — no backend actually applies it to the GPU; every backend hardcodes the full render-target/window size instead. `SetRenderTarget`'s new reset-to-target-size behavior (Task 338) is correct at the property level but has no GPU-visible effect until this lands. | Task 880 |
| Confirmed, not fixed (found Task 331) | `RenderTarget2D`'s `preferredMultiSampleCount` is stored verbatim, never clamped/wired to any backend. | Task 337 |
| Confirmed, severe, silent failure | `TextureCube::DDSFromStreamEXT` ignores its stream argument, always returns a blank 1×1 texture. | Task 663 |
| Confirmed, severe, silent failure | `Texture3D`/`TextureCube::GetData` total no-op on Vulkan/Bgfx. | Task 865 |
| Confirmed, silent failure | `Texture2D::SetData(level>0,...)` no-op on Vulkan/Bgfx; EasyGL renders solid black for mip filters on mip-incomplete textures. | Task 867 |
| Confirmed, architectural, not fixed | `Texture3D`/`TextureCube` can't be sampled in any shader — don't inherit `Texture`. | Task 863 |
| Confirmed, severe, silent failure, not fixed | Bgfx: `SpriteBatch::Draw`ing a `RenderTarget2D` reads a framebuffer handle where a texture handle is expected (`static_cast` to an unrelated backend type) — samples wrong data, doesn't crash, can't be pixel-verified (no Bgfx GPU readback). | Task 873 |
| Confirmed, severe, silent failure, not fixed | Bgfx: same bug shape as Task 873 for `RenderTargetCube` sampled via `EnvironmentMapEffect` — reads `BgfxRenderTargetCubeBackend::fbo` where `cubeTex` should be. | Task 874 |
| Confirmed, real, not fixed | Vulkan: `SetRenderTarget`+`Clear()` with no draw call in between never records a render pass — target's image stays `VK_IMAGE_LAYOUT_UNDEFINED` forever. | Task 875 |
| Confirmed, real, not fixed, root cause not isolated | Vulkan: sampling a `RenderTargetCube` via `EnvironmentMapEffect` after unbinding renders black instead of actual content, even with a real draw call per face. | Task 876 |
| Confirmed, format-fidelity gap, not fixed | No backend honors the exact requested `DepthStencilFormat` for a render target's depth/stencil attachment (EasyGL: no stencil bits ever; Vulkan: `hasDepth` ignored; Bgfx: format not exact). Core depth-test functionality itself works (Task 335). | Task 877 |
| Confirmed, architectural, deliberate | `GraphicsDevice` stores state objects by value, unlike FNA's reference-type aliasing. No game code here relies on FNA's behavior. | Task 869 |
| Confirmed bug | `SpriteBatch` with multiple `Begin()`/`End()` per frame on Vulkan: only the last batch renders. | — |
| Confirmed, incomplete | `SpriteBatch`'s `SamplerState` (`Begin()`) is a no-op on Vulkan/Bgfx (EasyGL only). | — |
| Confirmed, pre-existing | `EasyGL_MRT_TwoAttachments`: attachment 1 stays black with 2 render targets. Not caused by recent work. | Task 145 |
| Confirmed, minor, not fixed | `SetRenderTargets`'s simultaneous-target cap doesn't match FNA's real `MAX_RENDERTARGET_BINDINGS=4` (EasyGL/Bgfx cap at 8, Vulkan uncapped at the CNA level). No test exercises >2 targets. | Task 881 |
| Confirmed, incomplete (found Task 348) | `PresentationMode::Letterbox`/`Overscan`/`Stretch`/`NativeBackBuffer` aren't distinctly implemented in `EasyGLGraphicsBackend::getLogicalSize()` — only `FixedHeightDynamicWidth` has real logic; every other mode silently behaves the same, contradicting each one's documented behavior. Vulkan/Bgfx implement no virtual-resolution/presentation-mode scaling at all in `GetViewportSize()` (always raw physical window size); Vulkan's `SetVirtualResolution()` instead triggers `RecreateSwapchain()`, a materially different mechanism needing its own investigation. | Task 882 |
| Confirmed, pre-existing, out-of-repo | `easy-gl-resource-smoke-tests` aborts on an internal assert in the sibling `easy-gl` repo. | — |
| Confirmed, pre-existing | `Vulkan_DepthBias`'s `DepthBias=-1e6` sub-case fails; other sub-cases pass. | — |
| Confirmed, pre-existing, flaky | `Vulkan_FillMode_WireFrame`/`Vulkan_RenderTargetUsage`: order-dependent, only one fails per full-suite run. | — |
| Suspected, not reproduced | Vulkan/Bgfx likely have the same mip-allocation bug already fixed on EasyGL's `TextureCube` (Task 276), for `Texture3D`/`TextureCube` on both backends. | Task 864 |
| Needs verification | Whether Bgfx's window actually has a physical stencil buffer (the same class of gap just found/fixed on EasyGL) has not been checked. | — |
| Incomplete, by design | Stride-keyed vertex layout only supports strides 16/20/24/32/52. Vulkan has no `Tangent`/`Binormal` mapping. `SurfaceFormat` support is Color-only for real GPU formats. `SDL_Renderer` has no 3D at all. Bgfx has no GPU pixel-readback API. | — |
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
| Bgfx backend | `src/CNA/Internal/Backends/Bgfx/` | `BgfxVertexFormatHelper.hpp`; no readback API |
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
  multisample count** — `RenderTarget2D`/`RenderTargetCube`'s `mipMap`/`multiSampleCount`
  constructor parameters are currently accepted but not wired through (Tasks 336/337/332).

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

In priority order:

1. **`GRAPHICS_TASKS.md` Task 349 — verify viewport reset after backbuffer resize (EasyGL/Vulkan)**
   - Goal: closes Phase 40 (the last remaining task besides Task 350's docs-only closer). Task 348
     confirmed the real window-resize chain (`SDL_EVENT_WINDOW_RESIZED` → `Game`'s event loop →
     `GameWindow::updateFromSDL()` → `ClientSizeChanged` → `INTERNAL_OnClientSizeChanged` →
     `UpdateViewportFromWindow()`) and — separately — that `GraphicsDevice::Present()`
     unconditionally refreshes `Viewport` every frame regardless of that event chain. Task 349
     should verify this holds specifically for the **backbuffer resize** case (i.e. an explicit
     `GraphicsDeviceManager.ApplyChanges()`-driven resize, Task 227's own tested path) — does
     `Viewport` reset to the *new* backbuffer's full size correctly on both EasyGL and Vulkan,
     matching FNA's real behavior of resetting `Viewport`/`ScissorRectangle` to `(0,0,newW,newH)`?
     This is the same reset behavior Task 338 already implemented for `SetRenderTarget`-driven
     resizes (`GraphicsDevice::ResetViewportAndScissorForRenderTarget`) — check whether backbuffer
     resize (not render-target switching) goes through the same reset path or a separate one, and
     whether `ScissorRectangle` (not just `Viewport`) is included.
   - **Directly connects to Task 880**: `Viewport` has zero real GPU wiring on any backend — a
     "reset" of a property with no GPU effect is only half the story. Cross-reference
     `docs/rendertarget-support.md` §9 rather than re-diagnosing from scratch.
   - Files: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp` (`UpdateViewportFromWindow`,
     `SetPresentationParameters`, `applyToExistingBackend` equivalents),
     `examples/easygl_backbuffer_resize_test.cpp` (Task 227, existing coverage to extend, not
     duplicate), `examples/easygl_real_window_resize_test.cpp` (Task 348, new this session).
   - Verification: check whether `ScissorRectangle` resets alongside `Viewport` on backbuffer
     resize (Task 227's test doesn't check this); extend to Vulkan if EasyGL-only today.

2. **`GRAPHICS_TASKS.md` Task 881 — cap `SetRenderTargets` at FNA's real `MAX_RENDERTARGET_BINDINGS=4`**
   - Goal: found this session (Task 339) — FNA's real MRT limit is 4 simultaneous targets
     (implicitly enforced via a fixed-size array that throws past 4); CNA's EasyGL/Bgfx silently
     cap at 8 with no error, Vulkan has no CNA-level cap at all.
   - Fix shape: add an explicit check in `GraphicsDevice::SetRenderTargets` (shared C++, before
     delegating to any backend) that throws when `renderTargets.size() > 4`, matching FNA's real
     limit — this removes the need for each backend's own ad-hoc cap.
   - Files: `GraphicsDevice.cpp` (`SetRenderTargets`).
   - Verification: new unit test constructing 5 `RenderTargetBinding`s and asserting
     `SetRenderTargets` throws, plus confirming 1–4 still work.

3. **`GRAPHICS_TASKS.md` Task 880 — wire `GraphicsDevice.Viewport` to a real GPU viewport on all 3 backends**
   - Goal: found this session (Task 338) — `GraphicsDevice.setViewportProperty()` has zero backend
     wiring; every backend hardcodes its actual viewport to the full render-target/window size,
     ignoring `Viewport` entirely. A sub-region viewport (split-screen, atlas-subrect rendering)
     currently has no effect anywhere.
   - Fix shape: add `virtual void SetViewport(int x, int y, int w, int h, float minDepth, float
     maxDepth) {}` to `IGraphicsBackend`, call it from `GraphicsDevice::setViewportProperty()`
     (mirroring `setScissorRectangleProperty()`'s existing pattern exactly). EasyGL needs a real
     `glViewport(x,y,w,h)` (+ `glDepthRangef` for `MinDepth`/`MaxDepth`) instead of the hardcoded
     full-size call; Vulkan needs the `VkViewport` dynamic state set per-draw (audit existing
     backbuffer-resize viewport machinery first); Bgfx needs `bgfx::setViewRect`-adjacent state.
   - Files: `IGraphicsBackend.hpp`, `GraphicsDevice.cpp` (`setViewportProperty`),
     `EasyGLGraphicsBackend.cpp` (all the hardcoded `set_viewport(0,0,...)` call sites),
     `VulkanGraphicsBackend.cpp`, `BgfxGraphicsBackend.cpp`.
   - Verification: new sub-region-viewport pixel test — bind a viewport smaller than the full
     target, draw a full-screen quad, confirm pixels outside the viewport rect stay
     background-colored (this would almost certainly FAIL on all 3 backends today, confirming
     the gap precisely before fixing it).

4. **`GRAPHICS_TASKS.md` Task 878 — implement `RenderTarget2D`/`RenderTargetCube` mip support on Vulkan and Bgfx**
   - Goal: found in Task 336 — EasyGL now has a real, pixel-verified mip chain for render targets
     (pre-allocated storage + auto-`glGenerateMipmap`-on-unbind); Vulkan/Bgfx accept-and-ignore the
     `mipMap` parameter. `LevelCount` is already correct everywhere (shared C++ computation) —
     this task is purely about making Vulkan's/Bgfx's GPU resources match what the property
     already claims.
   - Fix shape: Vulkan needs `VkImageCreateInfo::mipLevels` set to the real level count (currently
     hardcoded `1`), a mip-aware `VkImageView`, and a `vkCmdBlitImage` cascade (blit level N→N+1)
     issued when the RT stops being active — mirroring where EasyGL's fix calls
     `generate_mipmap()`. Bgfx needs `hasMips=true` passed to `bgfx::createTexture2D`/
     `createTextureCube` (same shape as Task 864's `Texture3D`/`TextureCube` finding).
   - Files: `VulkanGraphicsBackend.cpp`/`.hpp` (`VulkanRenderTargetBackend`/
     `VulkanRenderTargetCubeBackend`), `BgfxGraphicsBackend.cpp`/`.hpp` (same).
   - Verification: port `easygl_rendertarget2d_mip_test.cpp`'s `TextureFilter::Anisotropic`
     black/blue methodology to Vulkan.

5. **`GRAPHICS_TASKS.md` Task 879 — implement `RenderTarget2D`/`RenderTargetCube` MSAA support on Vulkan and Bgfx**
   - Goal: found this session (Task 337) — EasyGL now has real MSAA-for-RT (multisampled
     renderbuffer + `glBlitFramebuffer` resolve-on-unbind, pixel-verified via a genuine
     anti-aliasing differential test); Vulkan/Bgfx accept-and-ignore `multiSampleCount` and
     honestly report `MultiSampleCount=0` (correct-but-incomplete, not a lie — unlike `LevelCount`,
     this property is legitimately device-capability-dependent, so `0` is an honest "not
     implemented" answer, not a shortcut).
   - Fix shape: Vulkan needs the color (+depth) image created with a real `VkSampleCountFlagBits`
     (queried via `VkPhysicalDeviceLimits::framebufferColorSampleCounts`, mirroring
     `OPENGL_GetMaxMultiSampleCount`'s capability query) and a `vkCmdResolveImage`/resolve
     attachment when the RT stops being active. Bgfx needs a `BGFX_TEXTURE_RT_MSAA_X{2,4,8,16}`
     flag on the color texture's creation (currently plain `BGFX_TEXTURE_RT`) — bgfx may
     auto-resolve at `bgfx::frame()` time, needs verification once wired. Override
     `GetMultiSampleCount()` on both backends' render-target classes once implemented.
   - Files: `VulkanGraphicsBackend.cpp`/`.hpp`, `BgfxGraphicsBackend.cpp`/`.hpp` (render-target
     backend constructors + factory methods).
   - Verification: port `easygl_rendertarget2d_msaa_test.cpp`'s diagonal-edge differential
     anti-aliasing methodology to Vulkan.

6. **`GRAPHICS_TASKS.md` Task 877 — wire `DepthStencilFormat`'s exact value into render-target depth/stencil attachments**
   - Goal: found this session (Task 335) — all 3 backends allocate a render target's depth/stencil
     attachment with a hardcoded/coarse choice instead of the actual requested `DepthFormat`:
     EasyGL always uses `DepthComponent24` (no stencil bits, ever); Vulkan ignores `hasDepth`
     entirely (always allocates, using the device-global depth format); Bgfx always uses `D24S8`
     (closest to correct, but not format-exact).
   - Fix shape: thread the real `DepthFormat` enum (not just a `hasDepth` boolable) through
     `IGraphicsBackend::CreateRenderTarget2D`/`CreateRenderTargetCube`'s signatures to each
     backend's actual attachment-format selection.
   - Files: `EasyGLGraphicsBackend.cpp`/`.hpp`, `VulkanGraphicsBackend.cpp`/`.hpp`,
     `BgfxGraphicsBackend.cpp`/`.hpp` (render-target backend constructors + factory methods).
   - Verification: a stencil-specific pixel test on EasyGL proving `Depth24Stencil8` actually
     gates a stencil-enabled draw inside a render target (currently would fail — no stencil bits
     exist there today).

7. **`GRAPHICS_TASKS.md` Task 875 — fix Vulkan: `Clear()` alone never records a render pass for a bound RT**
   - Goal: `VulkanGraphicsBackend::Clear()` only records a global clear-colour scalar and never
     registers the currently-bound RT in `RecordCommandBuffer`'s `usedRTs` list — only an actual
     draw call does. A `SetRenderTarget(rt); Clear(color); SetRenderTarget(nullptr);` pattern with
     no draw call silently never gets a render pass recorded; the RT's image stays
     `VK_IMAGE_LAYOUT_UNDEFINED` forever (found this session, Task 334, see NEXT.md §5).
   - Fix shape: either mark the currently-bound RT as "used" at `Clear()` time too (not just at
     draw time), or record a minimal begin+clear+end render pass for Clear-only RTs during
     `RecordCommandBuffer`.
   - Files: `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp`
     (`Clear()`, `RecordCommandBuffer()`'s `usedRTs` construction).
   - Verification: port `easygl_rt_roundtrip_test.cpp` (Task 180, EasyGL-only, Clear-only pattern)
     to Vulkan as a new regression test.

8. **`GRAPHICS_TASKS.md` Task 876 — investigate why `RenderTargetCube` sampled via `EnvironmentMapEffect` renders black on Vulkan**
   - Goal: even with a real `SpriteBatch` draw into each of a `RenderTargetCube`'s 6 faces (working
     around Task 875), sampling it back via `EnvironmentMapEffect` renders black instead of the
     actual rendered colour (found this session, Task 334, see NEXT.md §5). The sampling path
     itself (`dynamic_cast<IVulkanCubeSamplable*>` + `GetVkCubeImageView()`) is architecturally
     sound — something in the data chain is wrong.
   - Two unisolated candidates: (a) `SpriteBatch`-into-cube-face pixel correctness was never
     itself verified before this session (Task 142 only checks the backbuffer isn't corrupted,
     not that the faces got the right colour); (b) `GetOrCreateEnvMapDescSet`'s per-frame
     descriptor-set cache/write could be stale or wrong specifically for a `RenderTargetCube`'s
     `cubeView_`.
   - Files: `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp`
     (`GetOrCreateEnvMapDescSet`, the `needsEnvMap` draw-recording branch,
     `VulkanRenderTargetCubeBackend`).
   - Verification: first isolate which half is broken — e.g. add a temporary Vulkan-side debug
     readback of the RT cube face content immediately after Phase 1's draw, independent of
     `EnvironmentMapEffect`, before attempting a fix. See `examples/vulkan_rendertargetcube_sample_test.cpp`
     for the existing failing repro.

9. **`GRAPHICS_TASKS.md` Tasks 873/874 — fix Bgfx's wrong-handle-type casts for `RenderTarget2D`/`RenderTargetCube` sampling**
   - Goal: `BgfxSpriteBatchBackend::Draw` and `BgfxGraphicsBackend`'s `envMapping` branch each cast
     any `ITextureBackend`/`ITextureCubeBackend` to the plain-texture concrete type via
     `static_cast`, but `RenderTarget2D`/`RenderTargetCube`'s backends are unrelated sibling
     classes (`BgfxRenderTargetBackend`/`BgfxRenderTargetCubeBackend`) — this reads the framebuffer
     handle (`fbo`) where the texture handle (`textureHandle`/`handle`) should be, silently
     sampling the wrong data (confirmed Task 333/334, see NEXT.md §5). Worth fixing both together
     in one pass since the fix shape is identical.
   - Fix shape: add a virtual accessor to `ITextureBackend`/`ITextureCubeBackend` for "the
     `bgfx::TextureHandle` to sample", implemented by the plain-texture backends (return their own
     handle) and the render-target backends (return their colour texture handle — `colorTex`/
     `cubeTex`); use it instead of the blind `static_cast`s.
   - Files: `include/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.hpp`,
     `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`.
   - Verification: no pixel readback available on Bgfx — verify structurally instead (assert the
     extracted handle's `.idx` equals the render-target backend's colour-texture handle, not its
     framebuffer handle, after the fix). See `examples/bgfx_render_target_sample_test.cpp`/
     `bgfx_render_target_cube_sample_test.cpp` for the existing doesn't-crash smoke tests to extend.

10. **`GRAPHICS_TASKS.md` Task 663 — implement `TextureCube::DDSFromStreamEXT` for real**
   - Goal: replace the current stub with a real DDS cube-map parser (header parsing incl. `isCube`
     flag, reuse `Texture2D.cpp`'s DXT decode helpers, 6×`levelCount` `SetData` calls).
   - Files: `src/Microsoft/Xna/Framework/Graphics/TextureCube.cpp`, `TextureCubeTests.cpp`.
   - Verification: build a real/hand-built DDS cube-map test fixture **first**, then implement
     against it — do not mark done on "compiles and doesn't throw" alone (see §9).

11. **`GRAPHICS_TASKS.md` Task 865 — implement real Vulkan `GetData` readback for `Texture3D`/`TextureCube`**
   - Goal: `vkCmdCopyImageToBuffer` + host-visible staging buffer, mirroring the existing upload
     path's staging-buffer pattern in reverse.
   - Files: `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp`
     (`VulkanTexture3DBackend`/`VulkanTextureCubeBackend::GetData`).
   - Verification: new Vulkan pixel-readback test analogous to the EasyGL ones in
     `easygl_texture3d_partial_box_readback_test.cpp`.

12. **`GRAPHICS_TASKS.md` Task 864 — reproduce and fix the suspected Vulkan/Bgfx mip-allocation bug**
   - Goal: confirm (via a failing test first, matching the Task 276 methodology) that `Texture3D`/
     `TextureCube` mip levels >0 silently fail on Vulkan and Bgfx, then fix by pre-allocating every
     mip level at image/texture creation time.
   - Files: `VulkanGraphicsBackend.cpp` (`VkImageCreateInfo::mipLevels`),
     `BgfxGraphicsBackend.cpp` (`hasMips` parameter to `bgfx::createTexture3D`/`createTextureCube`).
   - Verification: new mip round-trip test per backend, mirroring
     `examples/easygl_texturecube_mip_test.cpp`.

---

## 9. Do not do yet

- **No architecture change to make `Texture3D`/`TextureCube` inherit `Texture`** (Task 863) without
  a deliberate, scoped design pass — it touches `EffectParameter`, `TextureCollection`, and every
  backend's texture-bind code. Not a small patch.
- **No rushed `TextureCube::DDSFromStreamEXT` implementation** without a real DDS cube-map test
  fixture built first — a "looks plausible" parser that isn't verified against real data just
  trades one silent-failure stub for a differently-silent one.
- **No SpriteBatch Vulkan multi-batch fix** until the root cause is isolated — a wrong fix could
  silently break single-batch rendering.
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
- **No opportunistic fix for Task 868 (Vulkan blend state) or Task 870 (Vulkan
  `DepthBufferFunction`/stencil testing)** bundled into an unrelated task — both are large,
  multi-pipeline-site changes confirmed across many tests; each needs its own dedicated task and
  full regression pass.
- **No opportunistic fix for Task 871/872 (stencil `Clear`/`ReferenceStencil` backend gaps)** —
  verify with a real test first, same discipline as every other tracked bug.
- **No rushed fix for Task 878 (Vulkan/Bgfx render-target mip support)** — mirror Task 336's exact
  EasyGL fix shape (real storage pre-allocation + resolve-time generation), and verify with the
  same `TextureFilter::Anisotropic` black/blue methodology (ported to Vulkan) before declaring it
  fixed — don't just bump `LevelCount` without also making the GPU resource genuinely mip-complete.
- **No rushed fix for Task 879 (Vulkan/Bgfx render-target MSAA support)** — mirror Task 337's exact
  EasyGL fix shape (real multisample attachment + resolve-time blit/resolve), and verify with the
  same diagonal-edge differential anti-aliasing methodology (ported to Vulkan) before declaring it
  fixed — a solid-fill-only test proves nothing here (see Task 337's own writeup for why).
- **No rushed fix for Task 873/874 (Bgfx handle-cast bugs)** bundled into an unrelated task — fix
  with their own dedicated task, and verify structurally (extracted handle equals the colour
  texture handle, not the framebuffer handle) since no pixel readback exists on Bgfx to confirm
  visually.
- **No fix for Task 875 (Vulkan Clear-only RT gap) or Task 876 (Vulkan RenderTargetCube-via-
  EnvironmentMapEffect renders black)** without isolating the root cause first (Task 876
  especially — two unisolated candidates, see §8) — a guessed fix risks masking the real bug
  instead of fixing it.
- **No opportunistic fix for Task 877 (DepthStencilFormat format-fidelity gap)** bundled into an
  unrelated task — verify with a dedicated stencil-in-RT pixel test first, same discipline as
  every other tracked bug; the core depth-test functionality already works (Task 335), so this is
  specifically about exact format fidelity, not a functional blocker.
- **No rushed fix for Task 880 (Viewport has zero GPU wiring)** — this is a large, pre-existing,
  three-backend-wide gap unrelated specifically to render-target switching (found while doing
  Task 338, but the gap predates it and affects Viewport everywhere, not just after
  SetRenderTarget); write the sub-region-viewport pixel test FIRST (it should fail on all 3
  backends today) before attempting any backend wiring, same discipline as every other tracked
  multi-backend gap.
- **No opportunistic fix for Task 145 (`EasyGL_MRT_TwoAttachments`)** bundled into a Task 881 or
  Task 340 pass — it needs its own dedicated root-cause investigation (already stated in this
  section above); do not let MRT-adjacent work in later tasks turn into an accidental fix attempt.
- **No rushed fix for Task 881 (MRT count cap mismatch)** without a real unit test proving both
  the throw-past-4 behavior and that 1–4 targets still work — low priority, no existing game code
  here is affected.

---

## 10. Resume prompt

```
Read NEXT.md first. Inspect only the files needed for the first task in §8.
Do not refactor unrelated code. Make one small, verified improvement.
Run the relevant build/test command before declaring the task done.
Update NEXT.md after finishing.

Current status: Phases 1-39 are FULLY COMPLETE. Phase 40 (Viewport, DisplayMode, and adapter
behavior, GRAPHICS_TASKS.md Tasks 341-350) is open, Tasks 341-348 are ALL DONE, Task 349 next
(the last remaining task besides Task 350's docs-only closer). EasyGL: 3351/3355 pass (3
documented pre-existing failures + 1 reconfirmed-flaky unrelated CueTest). Vulkan: 3274/3288 pass
(13 documented failures + 1 reconfirmed-flaky unrelated CueTest, no order-dependent flake this
run). Bgfx: 3259/3259 pass (100%, no flakes this run). Caution: run all 3 backends' full ctest
suites sequentially, never concurrently (see NEXT.md §2); if a single run shows an anomaly beyond
the documented list, re-run in isolation before treating it as a regression.

Tasks 341-344 (Viewport sub-area) and 345-347 (adapter/DisplayMode sub-area) of Phase 40 are fully
closed - see GRAPHICS_TASKS.md for detail.

Task 348 (just done) traced the real window-resize chain end-to-end: SDL_EVENT_WINDOW_RESIZED ->
Game's event loop -> GameWindow::updateFromSDL() -> GameWindow.ClientSizeChanged ->
GraphicsDeviceManager::INTERNAL_OnClientSizeChanged -> GraphicsDevice::UpdateViewportFromWindow().
CONFIRMED a real, deliberate FNA divergence, correctly NOT fixed: FNA forwards the new window size
into PresentationParameters.BackBufferWidth/Height on every resize (full device Reset); CNA
doesn't, by design (an existing accurate source comment explains why - would corrupt
FixedHeightDynamicWidth's virtual-resolution scaling). GENUINE DISCOVERY made while verifying the
new test's discriminating power (temporarily disabling the ClientSizeChanged subscription, then
separately breaking GameWindow::OnClientSizeChanged()'s Raise() call): Viewport tracking does NOT
actually depend on the ClientSizeChanged event at all - GraphicsDevice::Present() unconditionally
refreshes it every frame, a STRONGER guarantee than FNA's - so the event chain itself needed its
own, separately-discriminating check (added as check 4 in the new test). Closed a real
test-coverage gap: Task 227's easygl_backbuffer_resize_test.cpp only exercises the GDM-API-driven
resize path; new examples/easygl_real_window_resize_test.cpp (EasyGL_RealWindowResize) calls
SDL_SetWindowSize() directly on the live window and checks 4 things: Viewport height pinned,
Viewport width changes, BackBufferWidth/Height unchanged (regression marker), ClientSizeChanged
fires. EasyGL-only (Vulkan/Bgfx have no virtual-resolution scaling to pin). NEW FINDINGS DEFERRED
TO TASK 882: PresentationMode::Letterbox/Overscan/Stretch/NativeBackBuffer aren't distinctly
implemented on EasyGL (only FixedHeightDynamicWidth has real logic); Vulkan/Bgfx implement no
virtual-resolution scaling at all in GetViewportSize() (always raw physical window size); Vulkan's
SetVirtualResolution() instead triggers RecreateSwapchain(), a materially different mechanism
needing its own investigation. No production code changed this task (new test + CMakeLists.txt
registration only).

Tasks 341-348 (Viewport + adapter/DisplayMode + backbuffer-resize sub-areas of Phase 40) are now
ALL closed.

Next task: GRAPHICS_TASKS.md Task 349 - verify viewport reset after backbuffer resize
(EasyGL/Vulkan). Closes Phase 40 except for Task 350's docs-only closer. Task 348 confirmed the
real window-resize chain and that GraphicsDevice::Present() unconditionally refreshes Viewport
every frame regardless of the ClientSizeChanged event. Task 349 should verify this holds
specifically for the BACKBUFFER RESIZE case (an explicit GraphicsDeviceManager.ApplyChanges()-
driven resize, Task 227's own tested path) - does Viewport reset to the new backbuffer's full size
correctly on both EasyGL and Vulkan, matching FNA's real behavior of resetting
Viewport/ScissorRectangle to (0,0,newW,newH)? This is the same reset behavior Task 338 already
implemented for SetRenderTarget-driven resizes (GraphicsDevice::ResetViewportAndScissorForRenderTarget)
- check whether backbuffer resize goes through the same reset path or a separate one, and whether
ScissorRectangle (not just Viewport) is included - Task 227's existing test doesn't check
ScissorRectangle at all. DIRECTLY CONNECTS TO TASK 880 (Viewport has zero real GPU wiring on any
backend) - cross-reference docs/rendertarget-support.md §9 rather than re-diagnosing from scratch.
Files: src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp (UpdateViewportFromWindow,
SetPresentationParameters), examples/easygl_backbuffer_resize_test.cpp (Task 227, extend don't
duplicate), examples/easygl_real_window_resize_test.cpp (Task 348, new this session).
Update GRAPHICS_TASKS.md and NEXT.md after finishing.
```
