# The IGL renderer

`CNA_GRAPHICS_RENDERER=IGL` selects CNA's renderer built on
[facebook/igl](https://github.com/facebook/igl), Meta's "Intermediate Graphics Library", pinned at
`v1.1.1`.

> **Status: experimental, and not yet compiled.** The implementation is complete in shape but has
> never been built; see `plan_igl.md` §1 and §7. Treat every capability below as *intended and
> written*, not *verified*, until the Phase H tasks close.

## What this renderer is

Like `LLGL`, `IGL` names a **portable abstraction, not a native graphics API**. `igl::IDevice`
fronts real OpenGL/OpenGL ES, Vulkan and Metal implementations; CNA compiles the backends it can
(`CNA_IGL_BUILD_BACKEND_OPENGL`, `CNA_IGL_BUILD_BACKEND_VULKAN`) and selects one per process.

## Choosing the backend

```bash
CNA_IGL_BACKEND=auto     # default preference: OpenGL, then Vulkan (compiled-in ones only)
CNA_IGL_BACKEND=opengl   # or gl, glx
CNA_IGL_BACKEND=vulkan   # or vk
```

The choice is **resolved once, cached, and never falls back**. It has to be: `GraphicsDevice` reads
it to decide the platform window's render intent *before the renderer exists*, and a native window
cannot be both OpenGL- and Vulkan-capable. An explicit backend this build does not contain is an
error by name, not a silent substitution.

OpenGL leads the default preference because IGL's OpenGL backend can **adopt** the GL context CNA's
own `CNA::Platform::IPlatformGlContext` already creates for the window
(`igl::opengl::glx::Context`'s adopting constructor). IGL's other Linux GLX constructor opens its
own display connection and leaves the drawable unset, which cannot present to a window.

## Platform support

| Platform | State |
|----------|-------|
| Linux / X11 | Both backends |
| Linux / Wayland | Not wired up — the constructor throws by name |
| Windows, macOS | Not wired up (IGL has WGL and Metal backends; CNA has no bring-up path for them here yet) |

## Capability boundary

| Capability | IGL renderer |
|------------|--------------|
| `ThreeD` | Yes |
| `DepthStencilBuffer` / `StencilBuffer` | Yes, when the surface has a depth/stencil attachment |
| `MultipleRenderTargets` | Yes, 2–4 `RenderTarget2D` slots (`IGL_COLOR_ATTACHMENTS_MAX`) |
| `MultiStreamVertexInput` | Yes — `igl::VertexAttribute::bufferIndex` expresses it natively |
| `Instancing` | Yes — `VertexSampleFunction::Instance` |
| `CustomEffects` | Yes on OpenGL; parameters are refused on Vulkan (see below) |
| `Texture3D` | Yes (real volume storage) |
| `AnisotropicFiltering` | Yes |
| `MultiSampleAntiAliasing` | Yes on render targets; on the back buffer only via the OpenGL visual |
| `WireFrame` | OpenGL only — Vulkan needs `fillModeNonSolid`, which IGL does not request |
| `OcclusionQuery` | **No** — IGL exposes no query object on any backend at `v1.1.1` |
| `AdditiveBlending` | Yes |

## Known gaps, and why

| Gap | Cause | Behaviour |
|-----|-------|-----------|
| Occlusion queries | No IGL API for them | `SupportsCapability` reports false; a query completes immediately with 0 samples rather than spinning forever |
| Back-buffer MSAA on Vulkan | IGL's swap-chain images are single-sample and this renderer adds no resolve pass of its own | `GetMultiSampleCount()` returns 0 — the count that is genuinely in effect |
| Swap interval on Vulkan | Present mode is fixed when the swap chain is created | `SetSwapInterval` returns false |
| Sampler LOD bias | `igl::SamplerStateDesc` has no such field | recorded for diagnostics, not applied |
| Cube render-target MSAA | `igl::FramebufferDesc` cannot express a multisampled cube attachment with a per-face resolve | applied count reported as 1 |
| `ShaderEffect` parameters on Vulkan | Loose (non-block) uniforms do not exist in Vulkan GLSL, and IGL's Vulkan encoder leaves `bindUniform` unimplemented | the draw throws by name; use a std140 block, or `CNA_IGL_BACKEND=opengl` |

## How the frame is built

A **lazily opened render pass**, not a deferred command list:

* `Clear*` ends whatever pass is open and opens a new one whose load actions carry the clear values;
* a draw joins the open pass, opening one that *loads* previous contents when none is;
* `SetRenderTargets` ends the pass and switches framebuffer;
* `Present()` ends the pass, presents the drawable and submits the frame;
* a readback, or an upload into a buffer an already-recorded draw references, calls
  `FlushPendingFrameEXT()` first, so the GPU work that preceded it has genuinely run.

## Coordinate conventions

Both backends present a **bottom-left origin**: IGL's Vulkan encoder binds a negative-height
viewport specifically so its coordinate system matches OpenGL's. Two consequences:

1. **Presentation lives in the projection.** The two backends disagree about what a non-zero
   viewport *origin* means, so this renderer always binds a full-surface viewport and folds XNA's
   `Viewport` rectangle and the letterbox/overscan rectangle into a clip-space scale/offset matrix.
   Clipping is done by the scissor rectangle, whose Y origin *is* converted per backend (OpenGL
   measures it from the bottom, Vulkan from the top).
2. **Off-screen targets render Y-flipped.** That stores their rows top-first, so
   `RenderTarget2D.GetData()` and sampling a target both agree with an uploaded `Texture2D`. The
   pipeline's front-face winding is reversed to match, so `CullClockwiseFace` culls the same
   triangles whether you are drawing to the back buffer or to a target.

Clip depth is corrected with `z' = 2z − w` only when `IDevice::getNormalizedZRange()` reports
`NegOneToOne` (the OpenGL backend); XNA's projections target Direct3D's `[0, w]`.

## Shaders

One **uber-shader** serves every stock effect. Lighting, fog, texturing, alpha test, dual texture,
env mapping, skinning and PBR are uniforms in the `CnaEffect` std140 block (`uFlags.x` is the
feature bitmask), not preprocessor variants. Only two things are real shader variants:

* the **vertex attribute mask** — Vulkan rejects a shader input with no matching vertex attribute;
* the **colour attachment count** — a Vulkan pipeline requires the fragment outputs to match the
  render pass.

The generated GLSL differs per backend in exactly one respect, the resource declarations:

| | OpenGL (4.1 core) | Vulkan |
|---|---|---|
| Samplers | `uniform sampler2D uTexture0;` — the unit comes from `RenderPipelineDesc::fragmentUnitSamplerMap` | `layout(set = 0, binding = 0) uniform sampler2D uTexture0;` |
| Uniform blocks | `layout(std140) uniform CnaEffect {…}` — the binding comes from `uniformBlockBindingMap` | `layout(set = 1, binding = 0, std140) uniform CnaEffect {…}` |

Set 0 for combined image samplers and set 1 for buffers are IGL's own fixed descriptor layout
(`VulkanContext`'s `kBindPoint_CombinedImageSamplers` / `kBindPoint_Buffers`).

### Writing a custom `ShaderEffect` for this renderer

Attribute locations follow the renderer's own slot table:

| Location | Name | Type |
|----------|------|------|
| 0 | `aPosition` | `vec3` (sprite Z carries layer depth) |
| 1 | `aNormal` | `vec3` |
| 2 | `aColor` | `vec4` |
| 3 | `aTexCoord0` | `vec2` |
| 4 | `aTexCoord1` | `vec2` |
| 5 | `aBlendIndices` | `vec4` |
| 6 | `aBlendWeights` | `vec4` |
| 7 | `aTangent` | `vec4` |

The **names** matter on the OpenGL backend — IGL locates a vertex attribute by name there — so a
custom shader must use them, not only the locations.

## Building

See `plan_igl.md` §6. `cmake/ThirdPartyIGL.cmake` deliberately fetches only the dependencies the
four IGL library targets actually include (`glm`, `fmt`, `glslang`, `SPIRV-Headers`, plus `volk` and
`vma` for Vulkan) rather than running IGL's own `deploy_deps.py`, which downloads over a gigabyte of
sample/shell/test dependencies CNA never links.
