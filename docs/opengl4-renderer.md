# OPENGL4 renderer — desktop OpenGL 4.x core profile

`-DCNA_GRAPHICS_RENDERER=OPENGL4` · enum `CNA::GraphicsRendererType::OpenGL4` · target
`cna_renderer_opengl4` · sources `modules/renderers/opengl4/`.

History: `plans/plan_opengl4.md` (the 2026-07 build-out, `GL4-1` … `GL4-33`) and
`plans/plan_opengl4_modern_graphics.md` (the EasyGL-parity rewrite and its evidence ledger,
`GL4-0001` …). The capability table there (`GL4-0022`) is the authoritative parity record; this page
summarises it.

## Identity

- Requests a desktop **OpenGL 4.1 core** context — the highest core version macOS also provides —
  and runs on whatever newer core context the driver grants. A platform that grants less (a lower
  version, or a compatibility profile) is **refused by name** at construction rather than run as
  some other GL.
- Its own renderer family: its own context request, its own `gl4_*` loader (`GL4Loader.hpp`),
  native `glPolygonMode` wireframe, exact `GL_SAMPLES_PASSED` occlusion counts. It does not use
  easy-gl/meta-gl.
- What decides **what XNA means** is shared with the EasyGL family: the stock-effect GLSL corpus
  (`modules/graphics/include/CNA/Internal/Renderers/Common/GlStockShaderSources.hpp`, compiled here
  as `#version 410 core`) and the presentation transform (`GlPresentationSurfaceState.hpp`). The
  draw, state, sampler, texture and SpriteBatch code carries EasyGL's measured semantics over raw
  desktop GL.
- Platforms: native **X11 (GLX)** and **Wayland (EGL)** with no SDL, and SDL3. Dependencies: the
  platform's GL library only (`find_package(OpenGL)`); MojoShader only with the compiled-effects
  option below.

## Classic XNA surface — all implemented

Presentation (virtual resolution; NativeBackBuffer, FixedHeightDynamicWidth, Stretch, Letterbox,
Overscan) · backbuffer MSAA with apply/reset · swap interval · all six clears, which ignore the
scissor rectangle and the colour/depth/stencil write masks as XNA's do · `BlendState` (Opaque,
separate factors and equations, blend factor, per-target `ColorWriteChannels`, `MultiSampleMask`) ·
`DepthStencilState` gated by the bound target's real depth format, two-sided stencil on XNA's faces,
the counter-clockwise tuple applied to triangles only (Direct3D 9's rule), reference stencil ·
`RasterizerState` (culling, native wireframe, scissor including zero-extent rectangles, depth bias
converted by depth-format precision, multisample toggle) · XNA pixel centre and Direct3D clip depth ·
samplers with every filter ordinal's mip term, anisotropy, AddressW, MaxMipLevel and LOD bias,
each parameter written only when the slot's sampler object does not already hold it (EasyGL's
GLB-41 rule; without it every draw re-wrote all sixteen slots, `STREETGL4-0001`) ·
Texture2D in every EasyGL format (packed 16-bit, SNORM, RGB10A2, RGBA16, half/float, channel-expanded
formats, DXT native or exactly decoded) · TextureCube and Texture3D with declared-format transfers ·
RenderTarget2D/RenderTargetCube with MSAA, resolve, mip regeneration and bottom-up storage mapped on
readback · up to four render targets (`GL_MAX_DRAW_BUFFERS`/`GL_MAX_COLOR_ATTACHMENTS` queried),
cube faces included · vertex declarations bound by semantic in any order, multi-stream input,
instancing with per-instance streams, 16/32-bit indices, base vertex (negative folded on the CPU),
point lists · every stock effect (BasicEffect per-vertex/per-pixel, AlphaTest, DualTexture,
EnvironmentMap, Skinned) plus PbrEffect/SkinnedPbrEffect · SpriteBatch (device state and viewport,
sort modes, Immediate, 2 048-sprite submissions, custom effect applied at submission) and SpriteFont ·
custom GLSL `ShaderEffect` (desktop dialect; GLSL ES 3.00 sources are adapted, with diagnostics on
the author's own line numbers) · occlusion queries · backbuffer and render-target readback ·
background content loading through the context lease · several GraphicsDevices at once, each device's
work in its own context.

**Compiled XNA Effect bytecode** (`.fxb`, content effects) through MojoShader's GLSL 1.20 route with
`-DCNA_OPENGL4_COMPILED_EFFECTS=ON` (off by default, like EasyGL's option; builds without SDL).

## Not applicable

- **Context-loss recovery** (`SetContextRecoveryEnabled`, `DebugSimulateContextLoss`) exists for
  WebGL and Android, where the context is taken away. A desktop 4.x core context without
  `GL_ARB_robustness` reset notification is not; `CanBeginDrawEXT` keeps the base answer.
- The ES 2.0/WebGL 1 fallbacks of the EasyGL family have no counterpart in a desktop 4.x context.

## Modern (CNAEXT) GPU features

Workstream B of `plans/plan_opengl4_modern_graphics.md` (`GL4-0024` onward) implements the modern
API over the live context. Every answer below is asked of the driver, never assumed, and
`GL4::DiscoverModernCapabilities` keeps the native facts separate from these promises
(`plans/plan_modern.md` `MOD-2260`).

- **Compute** — desktop GLSL 4.30 programs (`#version 430 core` payloads), promised only on a 4.3+
  context. Scalar uniforms go to the program object. Storage-buffer, constant-buffer, sampled-texture
  and image bindings are recorded and installed only for the dispatch, then restored, so a compute
  pass never disturbs XNA draw state. Each dispatch ends in one GPU-side `glMemoryBarrier`, so results
  are visible to whatever follows without caller barriers; nothing waits on the CPU.
- **Storage and constant buffers** — every usage/CPU-access role, ranged transfers and GPU copies.
  Indirect-argument-only buffers exist without compute.
- **Storage textures** — Vulkan's fifteen storage formats, exact-byte transfers, compute image
  binding, and sampling from a `ShaderEffect`. `ByteEXT`/`UShortEXT` are refused by name because
  OpenGL4's `Texture2D` does not store them.
- **Indirect draws and base instance** — `glDraw*Indirect` through the instanced route's stream
  handling; base instance offsets per-instance streams (Vulkan's contract).
- **GPU timers** — `GL_TIMESTAMP` pairs (not `GL_TIME_ELAPSED`, which allows only one active query),
  so timers nest, and 64-bit reads that do not saturate. The timestamp period is 1000 ps.
- **Markers** — `SetStringMarkerEXT` inserts a KHR_debug application marker; compute dispatches are
  debug groups while debug output is on.
- **Shadows and image-based lighting** — the shared stock programs sample directional, cascaded,
  point and spot shadows and PBR IBL; both queries answer true.
- **Limits and format usages** — every `RendererLimit`, and per-format Sampled/Filterable/Blendable/
  Multisample/Storage*/Transfer*/Mipmapped from `glGetInternalformativ`. StorageAtomic is false for
  every format: GLSL image atomics need `r32i`/`r32ui`.
- **Texture arrays** — `GL_TEXTURE_2D_ARRAY` in every `Texture2D` format (Dxt with native S3TC),
  each layer stored and sampled exactly as a `Texture2D` of its format; exact per-layer, per-mip,
  sub-rectangle transfers; `ShaderEffect::SetTextureArrayEXT` binds array unit N to GL texture unit
  N's array target, sampled through XNA sampler slot N (Vulkan's and WebGPU's rule), and the effect
  keeps the array alive until the unit is cleared. `MaxTextureArrayLayers` is
  `GL_MAX_ARRAY_TEXTURE_LAYERS`. Core since GL 3.0, so available on the 4.1 floor too
  (`OpenGL4_Gl41Floor`, a real 4.1 context through Mesa's version override).
- **Not implemented:** scRGB/HDR10 display output (sRGB only, the base answer).

## Diagnostics

With `KHR_debug` available (Debug builds, or `CNA_OPENGL4_DEBUG_OUTPUT=1`) the renderer installs a
synchronous debug callback and logs `[OpenGL4 GL Error] …` for every error, undefined-behaviour or
high-severity message. Every OpenGL4 CTest fails on that line (`cmake/TestHelpers.cmake`).
`CNA_OPENGL4_DEBUG_OUTPUT=verbose` also logs informational messages.

## Validation

Measured on an AMD Radeon 780M (Mesa radeonsi, 4.6 core) through
`tools/platform/run_gpu_tests_private.sh`, never on a live desktop:

| suite | Wayland (EGL) | X11 (GLX) |
|---|---|---|
| `ctest -R '^OpenGL4_'` — OpenGL4's own tests, the 32 shared parity fixtures, 346 EasyGL example sources rebuilt against OpenGL4, the modern stress run and the forced 4.1 floor | 407 / 407 | 406 / 406 (openbox managing the private display) + `OpenGL4_Gl41Floor` |
| `CnaGraphicsTests` | 2 833 / 0 / 57 | 2 800 / 0 / 71 (OpenGL4-only build) |
| `CnaRendererTests` | 338 / 0 / 10 | 232 / 0 / 0 (before `GL4-0037`'s two cases) |
| `CnaGraphicsExtTests` (the modern engine layer) | 960 / 0 / 7 | 956 / 0 / 6 (before `GL4-0037`); `Texture2DArray*` 14 / 14 |
| CNAEXT example oracles | 32 / 1 / 0 | 34 / 1 / 0 (the failure, on every renderer: `CNAEXT_NoPosixSetenv`, a source scan of the Wayland platform) |
| AddressSanitizer + UBSan + LeakSanitizer (`build-asan`): OpenGL4's own tests, `CnaGraphicsExtTests`, 3000-cycle stress | 18 / 0 / 0, 956 / 0 / 6, 6 / 6 | — |

Every remaining skip is classified in the ledger (tests owned by another renderer, refusal legs for
behaviour OpenGL4 has, environment-dependent cases). Windows and macOS remain unvalidated.
