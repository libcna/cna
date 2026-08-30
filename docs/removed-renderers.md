# Removed renderers

Renderer identities CNA no longer carries. Each was removed because it duplicated a
rendering strategy CNA keeps, added no platform CNA did not already reach, or could not
satisfy `IGraphicsRenderer` at all — never because the work was bad. Several were
excellent reconnaissance; what is kept is the finding, not the code.

**The code is not gone.** Every removal is one commit, tagged `removed/<family>`, so
`git show removed/<family>` is the whole implementation. What git does *not* preserve is
the third-party project each one wrapped, so every entry below records the exact pinned
dependency. That pairing — CNA's code in git history, the dependency's coordinates here —
is the archive.

**A revert is a reference, not a patch.** `IGraphicsRenderer` changed 65 times in the
three months before these removals. Beyond a few months the removal commit is a
specification to port forward, not a patch to apply. Expect to read it, not `git revert` it.

---

## LLGL

| | |
|---|---|
| Identity | `LLGL` (enum `Llgl`, C ABI `CNA_GRAPHICS_RENDERER_LLGL` = 41) |
| Family | `modules/renderers/llgl` |
| Removed | 2026-08-30, tag `removed/llgl` |
| Size | 22,547 lines (16,448 production, 73 tests, 6,026 examples) |
| Dependency | `https://github.com/LukasBanana/LLGL.git` @ `Release-v0.04b`, commit `1e78d8fa497f5cab76b231ba13f4d6249dac0e7e` (BSD-3-Clause) |
| Build was | `-DCNA_GRAPHICS_RENDERER=LLGL`, optionally `-DCNA_LLGL_ROOT=<checkout>` |

**What it proved.** That a portable abstraction can be driven from CNA at all: the route
`LLGL -> LLGL OpenGL RenderSystem -> native OpenGL/GLX` ran on Linux/X11 x86_64 against a
dedicated Xvfb with Mesa llvmpipe. LLGL's Vulkan module was compiled for coverage but was
never a supported rendering route here — its swapchain needs a WSI surface that a DRI3-less
Xvfb cannot provide, which is a real and reusable finding about testing Vulkan headlessly.

**What it cost.** Four of the seven entries in `known_bugs.md` were LLGL, and a fifth was
LLGL-adjacent — 71% of the open defect list for 1 of 50 renderers. All four were still
open at removal and none was explained:

- 3D pipeline cache ignored `ColorWriteChannels`/blend factors for `DrawPrimitives`.
- `backbuffer_pass_order_test.cpp`'s Contract was stale after LLGL-45; correcting it
  exposed a narrower real gap (V1/V2).
- `Orthographic` + `CreateLookAt` reported geometry off-screen.
- `RasterizerState.SlopeScaleDepthBias`, custom `Viewport.MinDepth`/`MaxDepth`, and
  `RenderTarget2D` depth testing each had a genuine, unexplained defect (LLGL-53).

**Why removed.** CNA is itself a portable graphics abstraction; wrapping another one adds
a translation layer that can only lose fidelity and gain bugs, and teaches its own API
rather than the GPU. LLGL drove OpenGL and Vulkan, both of which CNA reaches natively
(EasyGL, `VULKAN`), so it added no platform. The defect list above is what that
impedance mismatch cost in practice.


---

## SKIA

| | |
|---|---|
| Identity | `SKIA` (enum `Skia`, C ABI `CNA_GRAPHICS_RENDERER_SKIA` = 19) |
| Family | `modules/renderers/skia` |
| Removed | 2026-08-30, tag `removed/skia` |
| Size | 32,563 lines (9,684 production, **0 tests**, 22,879 examples) — plus 34 `docs/skia-*.md`, `plans/plan_skia.md`, `NEXT_skia.md` and 6 `scripts/validate_skia_*.py` |
| Dependency | `https://skia.googlesource.com/skia.git` — **not pinned**; the developer build cloned it at whatever HEAD was, and CMake required `-DCNA_SKIA_ROOT=<checkout>` rather than fetching it |
| Build was | `-DCNA_GRAPHICS_RENDERER=SKIA -DCNA_SKIA_ROOT=<skia checkout>` |

**What it proved.** That an external CPU rasterizer can be driven as a CNA renderer, and —
more usefully — exactly where that stops. The 3D refusal was reasoned out rather than
assumed (`docs/skia-3d-refusal.md`, `docs/skia-3d-emulation-adr.md`), and the GLSL→SkSL
translator contract, the CPU depth/stencil/geometry spikes and the surface-format matrix
are all real findings about the cost of emulating a GPU pipeline on a 2D canvas API.

**Why removed.** It is 2D-only by construction, so it can never satisfy `IGraphicsRenderer`:
CNA's contract has 102 pure-virtual methods covering depth, render targets, MSAA, MRT and
stock 3D effects, and Skia never advertised any of them. That is a category error rather
than unfinished work — no amount of further effort finishes it. Meanwhile EasyGL already
renders CNA's 2D on every platform CNA targets, on the GPU. Skia was also the heaviest
dependency in the tree, and the only one with no pinned revision — the build was not
reproducible across machines or across time.

**Note for anyone restoring it.** Because the dependency was never pinned, this removal
commit does not identify the Skia revision it was written against. Reconstructing that
from `docs/skia-ganesh-artifact.md` and the commit dates is the first task.


---

## SOKOL

| | |
|---|---|
| Identity | `SOKOL` (enum `Sokol`, C ABI `CNA_GRAPHICS_RENDERER_SOKOL` = 37) |
| Family | `modules/renderers/sokol` |
| Removed | 2026-08-30, tag `removed/sokol` |
| Size | 24,597 lines (21,850 production, **0 tests**, 2,747 examples) |
| Dependency | `https://github.com/floooh/sokol.git` @ `27b49604b19be8cee0dcc6b2bbfe803dd9517585` (zlib/libpng) |
| Build was | `-DCNA_GRAPHICS_RENDERER=SOKOL`, native API chosen by `CNA_SOKOL_API` |

**What it proved.** That `sokol_gfx`'s single-header, API-agnostic model maps onto
`IGraphicsRenderer` at all, with the native API (GL / D3D11 / Metal / WebGPU) picked at
configure time rather than by CNA.

**Why removed.** Same reason as LLGL: CNA is already the portable abstraction, and
stacking `sokol_gfx` under it adds a translation layer without adding a platform. The one
real argument for keeping it was Metal coverage without writing Metal — worth recording,
because it is the argument to revisit if Apple ever becomes a target. It did not survive
today's balance: CNA targets no Apple platform, 21,850 production lines carried zero tests
of their own, and `CNA_SOKOL_API` meant the identity `SOKOL` never named one behaviour.


---

## DILIGENT

| | |
|---|---|
| Identity | `DILIGENT` (enum `Diligent`, C ABI `CNA_GRAPHICS_RENDERER_DILIGENT` = 38) |
| Family | `modules/renderers/diligent` |
| Removed | 2026-08-30, tag `removed/diligent` |
| Size | 14,931 lines (7,066 production, 243 tests, 7,622 examples) |
| Dependency | `https://github.com/DiligentGraphics/DiligentCore.git` @ `v2.5.6` (Apache-2.0) |
| Build was | `-DCNA_GRAPHICS_RENDERER=DILIGENT` |

**What it proved.** It was the only CNA renderer that chose its native API at **runtime**
rather than at configure time — DiligentCore is itself an abstraction over
D3D11/D3D12/Vulkan/OpenGL/Metal. That made it the one place where "which backend am I
actually on?" was a runtime question, and the descriptor/selection plumbing it needed is a
genuine finding about CNA's own compile-time assumptions.

**Why removed.** That uniqueness is now duplicated by CNA's own feature: runtime renderer
selection through `CNA::GraphicsRendererSelection` (`docs/runtime-renderer-selection.md`)
does the same job without a third-party abstraction underneath. What is left is the same
translation layer as LLGL and SOKOL, over APIs CNA already reaches natively.


---

## IGL

| | |
|---|---|
| Identity | `IGL` (enum `Igl`, C ABI `CNA_GRAPHICS_RENDERER_IGL` = 48) |
| Family | `modules/renderers/igl` |
| Removed | 2026-08-30, tag `removed/igl` |
| Size | 14,122 lines (9,071 production, 569 tests, 4,482 examples) |
| Dependency | `https://github.com/facebook/igl.git` @ `v1.1.1` (MIT) |
| Build was | `-DCNA_GRAPHICS_RENDERER=IGL`, backend fixed for the process by `CNA_IGL_BACKEND` |

**What it proved.** That a renderer can need its native API decided *before the renderer
exists* — IGL's backend had to be fixed for the process because the platform window's
render intent is chosen first. That constraint is a real fact about CNA's window/renderer
ordering and outlives the renderer.

**Why removed.** IGL drove OpenGL and Vulkan. CNA reaches both natively, through EasyGL and
the `VULKAN` renderer, so it added no platform whatsoever — the weakest case of the three
portable abstractions removed today.

**What its removal uncovered.** `GraphicsBackendCategoryTests` and
`GraphicsBackendMaturityTests` derived their loop bound from `GraphicsRendererType::Igl`,
with `static_assert(kPublicRendererCount == 48)`. `Igl` had not been the last enumerator
since `PIXIJS` and `NANOVG` were added, so both tests silently stopped short and never
classified those two identities. Both now derive the bound from the real last enumerator.


---

## WICKED

| | |
|---|---|
| Identity | `WICKED` (enum `Wicked`, C ABI `CNA_GRAPHICS_RENDERER_WICKED` = 36) |
| Family | `modules/renderers/wicked` |
| Removed | 2026-08-30, tag `removed/wicked` |
| Size | 6,978 lines (5,935 production, 1,043 tests, 0 examples) |
| Dependency | `https://github.com/turanszkij/WickedEngine.git` @ `27c0df160d738925474a2181d3f88bfd59edaefe` (MIT) — **plus a CNA-authored patch written against exactly that revision** |
| Build was | `-DCNA_GRAPHICS_RENDERER=WICKED` |

**What it proved.** That `wi::graphics`, a game engine's own RHI, can be driven from
outside that engine at all — which required a patch against a pinned WickedEngine commit,
because the RHI is not designed to be consumed independently.

**Why removed.** It was the most inverted of the layering mistakes: wrapping a whole game
engine's render hardware interface *inside* a game framework. It added no platform, and
the patch requirement made the pin load-bearing in a way none of the other dependencies
were — a different WickedEngine revision would not apply. It also shipped zero examples,
so nothing demonstrated it end to end.


---

## MAGNUM

| | |
|---|---|
| Identity | `MAGNUM` (enum `Magnum`, C ABI `CNA_GRAPHICS_RENDERER_MAGNUM` = 10) |
| Family | `modules/renderers/magnum` |
| Removed | 2026-08-30, tag `removed/magnum` |
| Size | 9,472 lines (6,866 production, 1,065 tests, 1,541 examples) |
| Dependency | `https://github.com/mosra/magnum.git` @ `5a7424643bfd4621fbcff8c361d37795502cf890` and `https://github.com/mosra/corrade.git` @ `783e4e4807536ec52c352986fc9317db986ace96` (MIT) — plus system GL/X11 headers, and `libegl1-mesa-dev` under `-DCNA_MAGNUM_USE_EGL=ON` |
| Build was | `-DCNA_GRAPHICS_RENDERER=MAGNUM` |

**What it proved.** Where a third-party GL wrapper's own context ownership collides with
CNA's. Magnum's `Platform::GLContext` takes its entry points from exactly one of Magnum's
four platform context libraries (GLX/EGL/WGL/CGL), none of which exists for Emscripten —
there the loader is baked into `EmscriptenApplication`, which owns the window and event
loop CNA already owns through SDL3. That is why the identity carried a hard configure-time
gate refusing Emscripten, and it is a reusable finding about adopting any library that
expects to own the window.

**Why removed.** It reached desktop OpenGL only, which EasyGL already covers on every
platform CNA targets, so it added nothing. It also pulled two pinned repositories plus
system GL/X11 (and optionally EGL) development headers, making it one of the more
expensive dependencies for the least unique coverage.

---

## BLEND2D

| | |
|---|---|
| Identity | `BLEND2D` (enum `Blend2D`, C ABI `CNA_GRAPHICS_RENDERER_BLEND2D` = 20) |
| Family | `modules/renderers/blend2d` |
| Removed | 2026-08-30, tag `removed/blend2d` |
| Size | 3,974 lines (2,055 production, **0 tests**, 1,919 examples) |
| Dependency | `https://github.com/blend2d/blend2d.git` @ `def0d1238c3e5d0983bb848e5676049d829e435b` and `asmjit` @ `b56f4176cb9b0c0501da659ac54d4c5877862c7b` (zlib) |
| Build was | `-DCNA_GRAPHICS_RENDERER=BLEND2D` |

**What it proved.** The "CPU raster + SDL presentation" shape — the same one SKIA used —
works, and a JIT-compiled 2D rasterizer can sit behind `SpriteBatch`.

**Why removed.** 2D-only, so it can never satisfy `IGraphicsRenderer`, and EasyGL already
renders CNA's 2D on the GPU everywhere CNA runs. Its capability arms were also honest about
being unmeasured: `BLContext` has no polygon fill mode and no vertex route at all, so
`WireFrame`, MRT and occlusion queries were structural refusals rather than gaps —
documented intent, never a measurement, because the renderer was not buildable in this
environment.

**Note.** `.github/workflows/platform-ci.yml` used `BLEND2D` for its "TERMINAL platform +
NULL audio" matrix leg. That leg now uses `SOFTWARE`, which has the same shape (CPU raster,
no display) and is maintained.
