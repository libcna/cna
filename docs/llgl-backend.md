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
  textured or untextured, plus **`AlphaTestEffect`** and **`DualTextureEffect`**. Lighting still
  requires vertex colours when no texture is bound -- a lit, untextured, colourless draw is refused
  by name rather than silently dropping the light;
* **`RenderTarget2D`**: draw into it (both `SpriteBatch` and the 3D path), unbind back to the
  swap chain, sample it back onto the screen, and `GetData()` straight off the colour attachment.
  See "Render targets" below for what this does and does not cover;
* **occlusion queries**: real `LLGL::QueryHeap`-backed `OcclusionQuery`. See "Occlusion queries"
  below for how `IsComplete()`/`PixelCount()` behave on this backend;
* **custom `ShaderEffect`s**, scoped to `SpriteBatch` draws. See "Custom effects" below for the
  runtime compile path and the uniform contract;
* **a real window resize** through `GraphicsDeviceManager.ApplyChanges()`, and **MSAA on the back
  buffer**, construction-time only and module-dependent — see the `MultiSampleAntiAliasing` row in
  "Capability boundary" below;
* **cube textures** (`TextureCube`): create, upload and read back all 6 faces and every mip level.
  Not yet sampled from a 3D shader for reflections -- that is `EnvironmentMapEffect`, still open.
* **volume textures** (`Texture3D`): create, box-region upload and box-region read back. Not yet
  sampled from a 3D shader.

**Not implemented:** `EnvironmentMapEffect`, `SkinnedEffect`, `PbrEffect`,
`RenderTargetCube`, multiple render targets (MRT), and MSAA/mip-mapped render targets.
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
# LLGL-27: runtime GLSL->SPIR-V compile for custom ShaderEffects on the Vulkan module. Only the
# runtime .so is needed (no -dev package, no headers -- CMake hand-declares the small ABI subset
# it calls); libshaderc-dev also satisfies the same find_library() if it happens to be installed.
sudo apt-get install -y libshaderc1
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
  lit_colored3d.vert.glsl                            lit, untextured-but-coloured (LLGL-31)
  untextured3d.frag.glsl  textured3d.frag.glsl      3D fragment shaders (alpha test + fog)
  lit_textured3d.frag.glsl                           (+ the lighting equation, .gl. too)
  lit_untextured3d.frag.glsl                         lit, untextured-but-coloured (LLGL-31)
  dual_textured3d.frag.glsl                          DualTextureEffect (reuses the plain
                                                       textured/colored_textured vertex shader --
                                                       no dedicated vertex shader of its own)
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

Every unlit 3D shader (vertex AND fragment) declares the SAME 144-byte `Transform` uniform block,
even the ones that never read every field in it: OpenGL requires an identically named and laid-out
uniform block across every stage linked into one program, so a shader that is ever paired with
another one that reads `vertexColorEnabledPad` (offset 128) must declare that field too, whether or
not it uses it. The lit shaders don't need a separate field for the same flag -- they reuse
`ambientColorLighting.w`, since offset 128 becomes `worldMatrix` for them.

`GraphicsDevice.DrawUserPrimitives()`'s typed overloads (`VertexPositionColor`,
`VertexPositionTexture`, `VertexPositionColorTexture`, `VertexPositionNormalTexture`) work too
(`LLGL-32`): they route through `IGraphicsBackend::CreateVertexBuffer(int)` (count-only, no
`VertexDeclaration`) and a raw byte `SetData`, so `LlglVertexBufferBackend::ResolveVertexAttributes()`
infers the vertex layout from the upload stride instead -- 16/20/24/32 bytes are each a distinct,
unambiguous size among `GraphicsDevice.cpp`'s own GPU-packed stream structs, the same technique the
Vulkan backend's own `MakeExt3DKey()` already uses for these exact stream sizes. Skinned/tangent
streams (52/68/48 bytes) are not recognised -- `SkinnedEffect` is not implemented on this backend
at all yet.

## Render targets

`RenderTarget2D` draws into an off-screen colour (and always-allocated depth/stencil) attachment,
which is then either sampled back with `SpriteBatch`/the 3D path like any other `Texture2D`, or
read back directly with `GetData()`. `RenderTargetCube`, multiple simultaneous render targets
(MRT), and MSAA/mip-mapped render targets are not implemented and fail by name.

Two implementation choices are worth knowing if you are debugging a render-target frame:

* **One render pass per distinct target, not per bind.** LLGL's public Vulkan API has no way to
  re-enter a render pass with `Load` semantics, so a frame that interleaves draws to the back
  buffer and one or more render targets is replayed as one pass per distinct target IDENTITY, in
  first-appearance order — every command for a given target is grouped together, not replayed in
  original interleaved order. `RenderTargetUsage.PreserveContents` is not honoured across separate
  binds of the same target within one frame as a result.
* **Every render target shares the swap chain's own attachment formats.** The colour attachment
  always takes the swap chain's colour format, and a depth/stencil attachment matching the swap
  chain's own format is always allocated regardless of the requested `DepthFormat` (which only
  changes what `HasRealDepthBuffer()` reports). This is what lets every cached sprite/primitive
  pipeline — built once against the swap chain's render pass — be reused as-is for a render-target
  pass, instead of needing a second, render-target-keyed pipeline cache: Vulkan's render-pass-
  compatibility rule only requires matching attachment formats and sample counts, not the same
  `VkRenderPass` object. The 3D path (`BasicEffect`, depth-tested `VertexBuffer` draws) works into
  a render target too, not just `SpriteBatch` — both share the same reused pipelines.

Destroying a `RenderTarget2D` before `Present()` (create it, draw into it, sample it, let it go out
of scope, all within one `Draw()`) is safe: like `VertexBuffer`/`IndexBuffer`, the underlying LLGL
objects are released only once the frame that may still reference them has actually been
submitted. `RenderTarget2D::GetData()` also forces any of its own still-queued draws to be
submitted first — its content, unlike a plain `Texture2D`'s, only exists once they are.

## Occlusion queries

`OcclusionQuery.Begin()`/`End()` queue `LLGL::CommandBuffer::BeginQuery()`/`EndQuery()` into the
same deferred frame as everything else this backend draws — LLGL requires both to be issued
inside an open render pass, which this backend only opens at submit time.

Two things are worth knowing:

* **A fresh `LLGL::QueryHeap` is created for every `Begin()`, never reused.** LLGL 0.04b's own
  vendored Vulkan module never issues the `vkCmdResetQueryPool` a query needs before a second
  `vkCmdBeginQuery` on the same query index (the call exists in its source but is `#if 0`'d out),
  and reusing a query pool without an external reset LLGL does not expose would be undefined
  behaviour by the Vulkan spec's own query-reset rule. A fresh query pool is always in the valid
  "unavailable" state for its first use, so this sidesteps the gap entirely.
* **`IsComplete()`/`PixelCount()` answer synchronously.** The first call after `End()` forces a
  full submit-and-wait (the same `FlushPendingFrameEXT()` `RenderTarget2D::GetData()` uses) rather
  than genuinely polling across frames the way real hardware occlusion queries are meant to be
  used to avoid a CPU stall. This is a deliberate, documented trade of that performance
  characteristic for a result that is always immediately correct.

## Custom effects

`Microsoft::Xna::Framework::Graphics::ShaderEffect` (`NOXNA`) compiles hand-authored GLSL vertex
and fragment source and draws through it. It is **scoped to `SpriteBatch` draws only** -- the
vertex shader is bound to the fixed sprite `position`/`texCoord`/`color` layout, not an arbitrary
`VertexDeclaration` a 3D draw might use, mirroring the native `VULKAN` backend's own
`VulkanEffectBackend` scope exactly rather than inventing a new limitation:

```cpp
ShaderEffect fx(device, vertexGlslSource, fragmentGlslSource);   // always real GLSL text
fx.SetUniformVec4("uColor", 1.0f, 0.0f, 0.0f, 1.0f);              // name accepted, not consulted
spriteBatch.Begin(SpriteSortMode::Immediate, BlendState::Opaque, nullptr, nullptr, nullptr, &fx);
```

Unlike the `VULKAN` backend (which expects the caller to hand it pre-compiled SPIR-V, since it
names one fixed native API -- see `docs/shader-effect-vs-fx-bytecode.md`), `vertSrc`/`fragSrc` are
**always real GLSL text** here: this backend picks its module at runtime, so the game has no
reliable way to know in advance which form to hand over. `CompileProgram()` (via the public
`ShaderEffect` constructor) hands the GLSL to LLGL directly when the loaded module accepts it
(OpenGL), or compiles it to SPIR-V first through a real runtime `libshaderc` call when it does not
(Vulkan) -- the same problem this project's `SDL_GPU` backend already solved the same way.

Named-uniform setters (`SetUniformMat4`/`Vec4`/`Vec3`/`Vec2`/`Float`/`Int`) do **not** do real
name-based reflection -- LLGL exposes none for a raw GLSL/SPIR-V module, and adding one would need
a new dependency (SPIRV-Cross or similar). They map onto a fixed 32-float (128-byte) uniform block,
identical to the native Vulkan backend's own documented `VulkanEffectBackend::pushConst_` layout,
uploaded to a real constant buffer at binding 1 instead of a Vulkan push constant:

```glsl
layout(std140, binding = 1) uniform PC {
    vec4 vpSize_pad;  // xy = viewport/target size in pixels; set automatically, not by the game
    mat4 uMatrix;     // SetUniformMat4
    vec4 uColor;      // SetUniformVec4 / Vec3 (leaves w) / Vec2 (leaves z, w)
    vec4 uFloats;     // uFloats.x only -- SetUniformFloat / Int
} pc;
```

`name` is accepted (matching the shared `IEffectBackend` signature every backend implements) but
not consulted -- matching the same established precedent rather than inventing new semantics.
`colorMap`/`samplerState` (binding 2/3) sample the sprite's own texture, exactly as the stock
sprite shader does; there is no way yet to bind a second texture unit to a custom effect on this
backend. See `examples/llgl_shadereffect_test.cpp` for a complete worked example, including the
vertex shader's own pixel-to-NDC technique.

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
textures, tinting, alpha, fog and the alpha test, `Llgl_Lighting` covers ambient/directional/
specular/emissive lighting both with and without a texture bound, and `Llgl_RenderTarget` covers
drawing into a `RenderTarget2D`, unbinding back to the swap chain, sampling the target back onto
the screen, and `GetData()`.
`Llgl_OcclusionQuery` covers a fully visible quad, a fully occluded one (real depth test), and two
draws inside one `Begin()`/`End()` summing their contributions. `Llgl_ShaderEffect` covers a
custom GLSL shader genuinely tinting a sprite by its own uniform, against a stock-shader control
case that must not show the tint. `Llgl_Resize` covers a real window resize driven through
`GraphicsDeviceManager.ApplyChanges()` (growing, shrinking, and a `Letterbox` presentation rect
recomputing from the resized window), settled with `SDL_SyncWindow()` before each post-resize read
since `SDL_SetWindowSize()` is not guaranteed synchronous under X11. `Llgl_Msaa` covers a genuinely
antialiased diagonal edge against a hard, unblended one with MSAA off, using two raw
`GraphicsDevice` objects constructed directly (MSAA is construction-time only on this backend, so
neither `Game`'s eagerly-constructed device nor `ApplyChanges()` can reach it) — the sample-count-
dependent checks report `[SKIP]` on a module that does not apply MSAA to the default framebuffer at
all (the OpenGL module, on this project's own test environment) rather than failing. `Llgl_DualTexture`
covers `DualTextureEffect`'s `VertexColorEnabled`-gated tint and proves the two textures sample
independently (a white base plus a red overlay must read back red, not white).
`Llgl_DualTextureEffect_VertexColor` and `Llgl_GraphicsDevice_DefaultStateOcclusion` are pre-existing,
cross-backend shared sources (already registered on EasyGL/Vulkan/Bgfx) reused verbatim once
`LLGL-32` made `DrawUserPrimitives()` work on this backend, exercising it through two of its four
recognised upload strides.
Every one of them is registered a second time pinned to the OpenGL
module through `CNA_LLGL_RENDERER`, which also exercises the selection path itself. All thirty
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
| `MultiSampleAntiAliasing` | module-dependent | `MultiSampleCount` is honoured only at swap-chain CONSTRUCTION time (no way to enable it after the fact via `GraphicsDeviceManager.ApplyChanges()` — a `Game`'s eagerly-constructed device is always built with `MultiSampleCount=0`). On this project's own test environment the Vulkan module (lavapipe) applies it and produces a genuinely antialiased edge; the OpenGL module (llvmpipe/GLX) does not apply it at any sample count. Pixel-verified, including the module-dependent behaviour itself, by `Llgl_Msaa` (`LLGL-23`). |
| `AnisotropicFiltering` | device-dependent | From LLGL's reported `limits.maxAnisotropy`. |
| `ThreeD` | yes | Draws with depth, cull and fill state, one texture, fog, the alpha test, and per-pixel lighting (textured or untextured-with-vertex-colours); the remaining stock effects are not implemented. |
| `WireFrame` | module-dependent | Real on the OpenGL module; the Vulkan module cannot, and refuses rather than drawing an empty frame. |
| `OcclusionQuery` | yes | Real `LLGL::QueryHeap`-backed queries — see "Occlusion queries" above for how `IsComplete()`/`PixelCount()` behave on this backend. |
| `CustomEffects` | yes | Real `ShaderEffect`, scoped to `SpriteBatch` draws — see "Custom effects" above. |
| `Texture3D` | yes | Real `LLGL::TextureType::Texture3D` storage — `CreateTexture3D`/box-region `SetData`/`GetData`. Nothing samples a volume texture from a 3D shader yet. |
| `MultipleRenderTargets` | no | Not implemented — see `plan_llgl.md` phase LLGL-26. |

There is no standalone `SupportsCapability` flag for single-target `RenderTarget2D` support (XNA
has none either) — `CreateRenderTarget2D` returning a real backend instead of null is the signal,
and `SetRenderTargets` accepts exactly one `RenderTarget2D` slot, matching `MultipleRenderTargets`
above being false.
