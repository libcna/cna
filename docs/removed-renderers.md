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

