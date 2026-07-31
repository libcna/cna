# LLGL graphics backend

## Status

An **experimental** CNA graphics backend built on [LLGL](https://github.com/LukasBanana/LLGL),
added 2026-07-31. Unlike every other backend in this project it does not name a native graphics
API: LLGL is itself an abstraction layer, and the module it drives is selected when the process
starts.

What is implemented and verified today is the **2D pipeline, on both the Vulkan and the OpenGL
module**:

* real SDL window, `LLGL::RenderSystem`, swap chain (24-bit depth, 8-bit stencil), clear and
  present;
* virtual-resolution presentation (all five `CnaPresentationMode` policies share one code path;
  only `FixedHeightDynamicWidth` is pixel-verified) and window↔logical coordinate transforms;
* `Texture2D` upload per mip level and GPU readback;
* `SpriteBatch` with a per-blend-state pipeline cache and a per-sampler-state sampler cache,
  rotation, origin, both flips, tint, and the complete min/mag/mip triple for all nine XNA
  `TextureFilter` values;
* back-buffer readback, so `GraphicsDevice.GetBackBufferData` and the project's pixel tests work;
* the **3D path**: `VertexDeclaration` translation, real vertex and index buffer draws (with
  `vertexStart`, `startIndex` and `baseVertex` honoured), depth test and depth write, cull mode,
  and fill mode;
* **`BasicEffect` with one texture**, `DiffuseColor`, `Alpha`, vertex-colour modulation, fog,
  and **per-pixel directional lighting** (ambient, up to three lights, specular, `EmissiveColor`),
  plus **`AlphaTestEffect`**. Lighting currently requires a texture to also be bound -- a lit,
  untextured draw is refused by name rather than silently dropping the light.

**Not implemented:** `DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect`, `PbrEffect`,
render targets, cube and volume textures, custom `ShaderEffect`s, occlusion queries.
Each either reports itself unsupported through `GraphicsDevice.SupportsCapability()` or throws —
none of them silently does nothing.

Both modules are covered by their own CTests, so neither can break unnoticed because the default
preference happened to select the other one.

## Building

```bash
cmake -S . -B cmake-build-llgl -DCNA_GRAPHICS_BACKEND=LLGL -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-llgl -j4
```

CMake fetches LLGL at the pinned tag `Release-v0.04b`. For an offline or reproducible build, point
it at an existing checkout instead:

```bash
git clone https://github.com/LukasBanana/LLGL.git ~/deps/LLGL
git -C ~/deps/LLGL checkout Release-v0.04b
cmake -S . -B cmake-build-llgl -DCNA_GRAPHICS_BACKEND=LLGL -DCNA_LLGL_ROOT=$HOME/deps/LLGL
```

Which renderer modules get built:

| Option | Default | Notes |
| --- | --- | --- |
| `CNA_LLGL_BUILD_RENDERER_VULKAN` | `ON` when `find_package(Vulkan)` succeeds | LLGL marks its own Vulkan module experimental. |
| `CNA_LLGL_BUILD_RENDERER_OPENGL` | `ON` | Needs the usual GL/X11 development packages. |
| `CNA_LLGL_BUILD_RENDERER_NULL` | `ON` | Renders nothing; only reachable by explicit request. |

A module that was not built can never be selected, and configuring with neither OpenGL nor Vulkan
is a hard error.

### System packages (Debian/Ubuntu)

```bash
sudo apt-get install -y libgl1-mesa-dev libx11-dev libxrandr-dev libxext-dev libvulkan-dev
# to regenerate the shader header (not needed for a normal build):
sudo apt-get install -y glslang-tools
```

## Choosing the renderer at runtime

```bash
CNA_LLGL_RENDERER=vulkan  ./your_game   # force one module, no fallback
CNA_LLGL_RENDERER=opengl  ./your_game
CNA_LLGL_RENDERER=null    ./your_game   # accepts every command, renders nothing (diagnostics only)
CNA_LLGL_RENDERER=auto    ./your_game   # the default: Vulkan, then OpenGL
```

An unknown value, or one naming a module this build does not contain, is rejected with an error
rather than silently falling back. The Null module is never part of the automatic chain.

The choice is probed once per process by actually loading the module, and the answer is cached:
`GraphicsDevice` needs it before the window exists (only the OpenGL module requires an
`SDL_WINDOW_OPENGL` window) and the backend needs it again afterwards, and the two must agree.

`CNA_LLGL_DEBUG=1` additionally enables LLGL's own debug layer and routes its reports to stdout.
It validates every command and is far too costly to leave on, but it is the fastest way to find out
why a draw produced nothing.

## Platform support

X11 only. A Wayland SDL window is refused with an error naming `SDL_VIDEODRIVER=x11`: LLGL 0.04b
compiles Wayland support only when explicitly enabled and this integration does not enable it.
Handing LLGL a handle it cannot present to would be worse than saying so.

## Shaders

Both flavours are checked in and no shader toolchain is needed to build:

```text
src/CNA/Internal/Backends/Llgl/shaders/
  sprite2d.vert.glsl      sprite2d.frag.glsl        Vulkan flavour, compiled to SPIR-V
  sprite2d.gl.vert.glsl   sprite2d.gl.frag.glsl     OpenGL flavour, embedded as source
  colored3d.vert.glsl     textured3d.vert.glsl      3D vertex shaders, one per vertex layout
  colored_textured3d.vert.glsl                       (plus lit_*.vert.glsl for lighting,
  lit_textured3d.vert.glsl                            and a .gl. flavour of each)
  lit_colored_textured3d.vert.glsl
  untextured3d.frag.glsl  textured3d.frag.glsl      3D fragment shaders (alpha test + fog)
  lit_textured3d.frag.glsl                           (+ the lighting equation, .gl. too)
  effect3d_common.glsl.inc                           the uniform block they all share
  compile_shaders.py                                regenerates llgl_shaders.hpp
  llgl_shaders.hpp                                  generated; do not edit
```

After changing any shader:

```bash
python3 src/CNA/Internal/Backends/Llgl/shaders/compile_shaders.py
python3 src/CNA/Internal/Backends/Llgl/shaders/compile_shaders.py --check   # verifies freshness
```

Which flavour is used is decided from the shading language the loaded module reports, not from the
module's name — **and GLSL is preferred wherever a module offers it**. That order matters: a modern
OpenGL module reports SPIR-V as well (desktop GL ingests it through `GL_ARB_gl_spirv`), but the
SPIR-V here is compiled for Vulkan's binding model, and GL accepts it far enough to rasterize
geometry while silently zeroing every other attribute and the uniform block. That was `LLGL-17`.

## Tests

```bash
ctest --test-dir cmake-build-llgl -R Llgl --output-on-failure
```

`Llgl_Smoke` covers the device/window/swap-chain lifecycle, buffer round-trips, and 60 frames of
clear and present. `Llgl_2D` asserts real pixels read back from the GPU: quadrant orientation
(where a Y-flip mistake shows up immediately), tint multiplication, `SpriteEffects` flipping, and
`NonPremultiplied` alpha blending. `Llgl_TextureReadback` round-trips texture uploads byte-exactly,
`Llgl_Presentation` covers the five presentation policies, `Llgl_3D` covers the 3D draw path
(vertex colours, depth ordering, indexed draws, cull mode, wireframe), `Llgl_BasicEffect` covers
textures, tinting, alpha, fog and the alpha test, and `Llgl_Lighting` covers ambient/directional/
specular/emissive lighting. Every one of them is registered a second time pinned to the OpenGL
module through `CNA_LLGL_RENDERER`, which also exercises the selection path itself. All fourteen
need a display; on a machine without one they report SKIPPED
rather than FAILED. On a headless machine a virtual display works:

```bash
Xvfb :99 -screen 0 1280x1024x24 &
ctest --test-dir cmake-build-llgl -R Llgl --output-on-failure   # configure with -DCNA_TEST_DISPLAY=:99
```

## Capability boundary

`GraphicsDevice.SupportsCapability()` answers honestly for this backend:

| Capability | Supported | Why |
| --- | --- | --- |
| `DepthStencilBuffer` | yes | The swap chain really has both attachments and all seven clear paths work. |
| `MultiSampleAntiAliasing` | yes | Forwarded to the swap chain; not yet pixel-verified (`LLGL-22`). |
| `AnisotropicFiltering` | device-dependent | From LLGL's reported `limits.maxAnisotropy`. |
| `ThreeD` | yes | Draws with depth, cull and fill state, one texture, fog, the alpha test, and per-pixel lighting (textured draws only); the remaining stock effects are not implemented. |
| `WireFrame` | module-dependent | Real on the OpenGL module; the Vulkan module cannot, and refuses rather than drawing an empty frame. |
| `MultipleRenderTargets`, `OcclusionQuery`, `CustomEffects`, `Texture3D` | no | Not implemented — see `plan_llgl.md` phase LLGL-5. |
