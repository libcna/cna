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
- **Current development phase:** Phases 1–38 are complete. **Phase 39 (RenderTarget2D and
  RenderTargetCube completeness, `GRAPHICS_TASKS.md` Tasks 331–340) is open** — Tasks 331–334 done,
  **Task 335 is next** (see §8). Full phase history is in `GRAPHICS_TASKS.md`; the most recent
  closed phases have synthesis docs: `docs/sampler-state-support.md` (Phase 35),
  `docs/depthstencilstate-support.md` (Phase 37), `docs/rasterizerstate-support.md` (Phase 38).
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
- **EasyGL** (`cmake-build-debug`) and **Vulkan** (`cmake-build-vulkan`): both configured, build
  cleanly, rebuilt and verified in the current session.
- **Bgfx** (`cmake-build-bgfx`): rebuilt and full ctest re-verified this session (Task 333) —
  3223/3223 (100%) pass.

### Test status (last verified this session)
- **EasyGL, full `ctest -j1`:** 3312/3315 pass. 3 pre-existing/documented failures (see §5):
  `EasyGL_MRT_TwoAttachments`, `easy-gl-resource-smoke-tests`, `EasyGL_GraphicsDevice_ReferenceStencil`.
- **Vulkan, full `ctest -j1`:** 3237/3251 pass. 14 documented failures (see §5): the same 13
  pre-existing ones (5× `Vulkan_BlendState_*` Task 868, 5× `Vulkan_DepthStencilState_*` Task 870,
  `Vulkan_GraphicsDevice_ReferenceStencil` Task 872, `Vulkan_DepthBias` one sub-case,
  `Vulkan_RenderTargetUsage`/`Vulkan_FillMode_WireFrame` order-dependent flakiness — only one of
  the two fails per run), **plus one new, correctly-failing test**: `Vulkan_RenderTargetCube_SampleAfterUnbind`
  (Task 334/876 — a genuine confirmed bug, not a false pass; the test is *supposed* to fail until
  Task 876 is fixed).
- **Bgfx, full `ctest -j1`:** 3224/3224 (100%) pass — rebuilt and re-verified this session
  (Tasks 333–334), including the new `Bgfx_RenderTarget2D_SampleAfterUnbind` and
  `Bgfx_RenderTargetCube_SampleAfterUnbind` smoke tests (both pass — Bgfx's Tasks 873/874 bugs are
  silent-wrong-data, not crashes, so they don't fail these smoke tests; they're unverifiable
  pixel-wise since Bgfx has no GPU readback).
- **Caution:** run EasyGL's and Vulkan's full `ctest` suites **sequentially, never concurrently**
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
  render-to-texture round trip pixel-verified on EasyGL and Vulkan.
- `RenderTargetCube`: constructor, `DepthStencilFormat`/`MultiSampleCount`/`RenderTargetUsage`/
  `IsContentLost`/`ContentLost` all match FNA at the property level; `GetTypeName()` now correctly
  reports `"...RenderTargetCube"` instead of the inherited `"...TextureCube"` (Task 332).
  Sampling a `RenderTargetCube` back out via `EnvironmentMapEffect` after unbinding is
  pixel-verified working on **EasyGL only** (Task 334) — see below for Vulkan/Bgfx gaps.

### What does NOT work yet
- **Vulkan `BlendState`/`DepthStencilState` support is almost entirely fake** — hardcoded blend
  equations / depth-compare ops / no stencil testing at all, regardless of what's requested.
  Tracked as Task 868/Task 870, confirmed repeatedly via pixel tests, not fixed (large,
  multi-pipeline-site changes).
- `GraphicsDevice.ReferenceStencil`'s independent-override behavior has zero backend connection on
  all 3 backends (Task 872). `GraphicsDevice::Clear` ignores `ClearOptions::Stencil` everywhere
  (Task 871).
- `RenderTarget2D`'s `mipMap` parameter is silently ignored (level count always 1; no backend
  allocates render-target mip storage) — Task 336. `preferredMultiSampleCount` is stored verbatim
  and never clamped/wired to any backend — Task 337. Both found this session (Task 331), deferred.
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

---

## 3. Recent changes

Most recent first. Full history (including everything before Task 271, and full detail for every
task below) is in `GRAPHICS_TASKS.md` and `git log`.

| Commit / Task | Change |
|---|---|
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
| Confirmed, not fixed (found Task 331) | `RenderTarget2D`'s `mipMap` is silently ignored — level count always 1; no backend allocates RT mip storage. | Task 336 |
| Confirmed, not fixed (found Task 331) | `RenderTarget2D`'s `preferredMultiSampleCount` is stored verbatim, never clamped/wired to any backend. | Task 337 |
| Confirmed, severe, silent failure | `TextureCube::DDSFromStreamEXT` ignores its stream argument, always returns a blank 1×1 texture. | Task 663 |
| Confirmed, severe, silent failure | `Texture3D`/`TextureCube::GetData` total no-op on Vulkan/Bgfx. | Task 865 |
| Confirmed, silent failure | `Texture2D::SetData(level>0,...)` no-op on Vulkan/Bgfx; EasyGL renders solid black for mip filters on mip-incomplete textures. | Task 867 |
| Confirmed, architectural, not fixed | `Texture3D`/`TextureCube` can't be sampled in any shader — don't inherit `Texture`. | Task 863 |
| Confirmed, severe, silent failure, not fixed | Bgfx: `SpriteBatch::Draw`ing a `RenderTarget2D` reads a framebuffer handle where a texture handle is expected (`static_cast` to an unrelated backend type) — samples wrong data, doesn't crash, can't be pixel-verified (no Bgfx GPU readback). | Task 873 |
| Confirmed, severe, silent failure, not fixed | Bgfx: same bug shape as Task 873 for `RenderTargetCube` sampled via `EnvironmentMapEffect` — reads `BgfxRenderTargetCubeBackend::fbo` where `cubeTex` should be. | Task 874 |
| Confirmed, real, not fixed | Vulkan: `SetRenderTarget`+`Clear()` with no draw call in between never records a render pass — target's image stays `VK_IMAGE_LAYOUT_UNDEFINED` forever. | Task 875 |
| Confirmed, real, not fixed, root cause not isolated | Vulkan: sampling a `RenderTargetCube` via `EnvironmentMapEffect` after unbinding renders black instead of actual content, even with a real draw call per face. | Task 876 |
| Confirmed, architectural, deliberate | `GraphicsDevice` stores state objects by value, unlike FNA's reference-type aliasing. No game code here relies on FNA's behavior. | Task 869 |
| Confirmed bug | `SpriteBatch` with multiple `Begin()`/`End()` per frame on Vulkan: only the last batch renders. | — |
| Confirmed, incomplete | `SpriteBatch`'s `SamplerState` (`Begin()`) is a no-op on Vulkan/Bgfx (EasyGL only). | — |
| Confirmed, pre-existing | `EasyGL_MRT_TwoAttachments`: attachment 1 stays black with 2 render targets. Not caused by recent work. | — |
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

1. **`GRAPHICS_TASKS.md` Task 335 — verify depth buffer creation for render targets**
   - Goal: confirm `RenderTarget2D`/`RenderTargetCube`'s `DepthStencilFormat` actually results in a
     real, usable depth (and stencil, where applicable) buffer on each backend — e.g. draw two
     overlapping quads at different depths into a render target with `DepthFormat::Depth24Stencil8`
     and pixel-verify the nearer one wins, mirroring how depth tests are already pixel-verified for
     the backbuffer (Phase 37).
   - Files: likely a new `examples/*_rendertarget_depth_test.cpp`-style integration test per
     backend; check `EasyGLRenderTargetBackend`/`VulkanRenderTargetBackend`/
     `BgfxRenderTargetBackend`'s constructors for how (or whether) `hasDepth`/`DepthFormat` is
     actually wired to a real depth attachment.
   - Verification: new pixel-readback test on EasyGL and Vulkan (Bgfx: same GPU-readback
     limitation as always — smoke-test bind/draw/unbind without crashing).

2. **`GRAPHICS_TASKS.md` Task 875 — fix Vulkan: `Clear()` alone never records a render pass for a bound RT**
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

3. **`GRAPHICS_TASKS.md` Task 876 — investigate why `RenderTargetCube` sampled via `EnvironmentMapEffect` renders black on Vulkan**
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

4. **`GRAPHICS_TASKS.md` Tasks 873/874 — fix Bgfx's wrong-handle-type casts for `RenderTarget2D`/`RenderTargetCube` sampling**
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

5. **`GRAPHICS_TASKS.md` Task 663 — implement `TextureCube::DDSFromStreamEXT` for real**
   - Goal: replace the current stub with a real DDS cube-map parser (header parsing incl. `isCube`
     flag, reuse `Texture2D.cpp`'s DXT decode helpers, 6×`levelCount` `SetData` calls).
   - Files: `src/Microsoft/Xna/Framework/Graphics/TextureCube.cpp`, `TextureCubeTests.cpp`.
   - Verification: build a real/hand-built DDS cube-map test fixture **first**, then implement
     against it — do not mark done on "compiles and doesn't throw" alone (see §9).

6. **`GRAPHICS_TASKS.md` Task 865 — implement real Vulkan `GetData` readback for `Texture3D`/`TextureCube`**
   - Goal: `vkCmdCopyImageToBuffer` + host-visible staging buffer, mirroring the existing upload
     path's staging-buffer pattern in reverse.
   - Files: `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp`
     (`VulkanTexture3DBackend`/`VulkanTextureCubeBackend::GetData`).
   - Verification: new Vulkan pixel-readback test analogous to the EasyGL ones in
     `easygl_texture3d_partial_box_readback_test.cpp`.

7. **`GRAPHICS_TASKS.md` Task 864 — reproduce and fix the suspected Vulkan/Bgfx mip-allocation bug**
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
- **No opportunistic fix for Task 871/872 (stencil `Clear`/`ReferenceStencil` backend gaps) or
  Tasks 336/337 (RenderTarget2D mip/multisample gaps)** — verify with a real test first, same
  discipline as every other tracked bug.
- **No rushed fix for Task 873/874 (Bgfx handle-cast bugs)** bundled into an unrelated task — fix
  with their own dedicated task, and verify structurally (extracted handle equals the colour
  texture handle, not the framebuffer handle) since no pixel readback exists on Bgfx to confirm
  visually.
- **No fix for Task 875 (Vulkan Clear-only RT gap) or Task 876 (Vulkan RenderTargetCube-via-
  EnvironmentMapEffect renders black)** without isolating the root cause first (Task 876
  especially — two unisolated candidates, see §8) — a guessed fix risks masking the real bug
  instead of fixing it.

---

## 10. Resume prompt

```
Read NEXT.md first. Inspect only the files needed for the first task in §8.
Do not refactor unrelated code. Make one small, verified improvement.
Run the relevant build/test command before declaring the task done.
Update NEXT.md after finishing.

Current status: Phases 1-38 are fully complete. Phase 39 (RenderTarget2D and RenderTargetCube
completeness, GRAPHICS_TASKS.md Tasks 331-340) is open, Tasks 331-334 done, Task 335 next. EasyGL:
3312/3315 pass (3 documented pre-existing failures). Vulkan: 3237/3251 pass (13 pre-existing +
1 new correctly-failing test, Task 876). Bgfx: 3224/3224 pass (100%). Caution: run all 3 backends'
full ctest suites sequentially, never concurrently (see NEXT.md §2); if a single run shows an
anomaly beyond the documented list, re-run in isolation before treating it as a regression.

Task 334 (just done) verified RenderTargetCube can be sampled as TextureCube after unbinding via
EnvironmentMapEffect, on all 3 backends. No existing test covered this (Task 142's
vulkan_rtcube_test.cpp renders into cube faces but never samples back). Results: EasyGL PASSES
(architecturally sound, virtual BindGL() dispatch, exact pixel match). Vulkan FAILS with TWO
distinct real bugs: (1) Task 875 - Clear()-only into a bound RT (no draw call) never gets a render
pass recorded on Vulkan, image stays VK_IMAGE_LAYOUT_UNDEFINED forever (VulkanGraphicsBackend::Clear()
only sets a global clear-colour scalar, doesn't register the RT as "used"). (2) Task 876 - even
after switching to a real SpriteBatch draw per face (workaround for #1), sampling the RT cube back
via EnvironmentMapEffect renders black instead of the actual content - root cause NOT isolated
(candidates: SpriteBatch-into-cube-face correctness itself was never verified before, or
GetOrCreateEnvMapDescSet's per-frame descriptor cache). A render-pass-compatibility validation
warning also appears but is a confirmed red herring (present in Task 142's own already-passing
test too, reconfirmed by rerunning it unmodified). Bgfx: same unsafe-cast bug shape as Task 873,
confirmed by layout analysis - tracked as Task 874, doesn't crash (new smoke test confirms), can't
be pixel-verified. New tests: easygl_rendertargetcube_sample_test.cpp,
vulkan_rendertargetcube_sample_test.cpp (correctly fails - this is expected until Task 876 lands),
bgfx_render_target_cube_sample_test.cpp.

Next task: GRAPHICS_TASKS.md Task 335 - verify depth buffer creation for render targets. Confirm
RenderTarget2D/RenderTargetCube's DepthStencilFormat actually produces a real, usable depth (and
stencil where applicable) buffer on each backend - e.g. draw two overlapping quads at different
depths into a render target and pixel-verify the nearer one wins. Check each backend's RT
constructor for how (or whether) hasDepth/DepthFormat is wired to a real depth attachment first.
Update GRAPHICS_TASKS.md and NEXT.md after finishing.
```
