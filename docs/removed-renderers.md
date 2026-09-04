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

