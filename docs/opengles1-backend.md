# OpenGL ES 1.1 graphics backend

## Status

The OpenGLES1 backend was added on **2026-07-21** as CNA's sixth graphics backend: a genuine
**OpenGL ES 1.1 fixed-function ("Common", CM profile)** implementation, deliberately independent
of the EasyGL backend. EasyGL targets WebGL2/OpenGL ES 3.0 (a shader-based, programmable
pipeline) and cannot create an ES 1.1 context at all — there is no shared code between the two.

Select it with:

```bash
cmake -S . -B cmake-build-opengles1 \
  -DCNA_GRAPHICS_BACKEND=OPENGLES1 \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-opengles1 -j
```

This requires a **real system OpenGL ES 1.1 library and Khronos headers** — e.g. on Debian/
Ubuntu:

```bash
sudo apt-get install libgles1 libgles-dev
```

which provides `libGLESv1_CM.so` plus `GLES/gl.h`/`GLES/glext.h`. `cmake/BackendLibraries.cmake`
`find_library`/`find_path`s these at configure time and fails with a clear `FATAL_ERROR` (same
shape as Vulkan's `find_package(Vulkan REQUIRED)`) if they are absent — this is a hard system
dependency, not a vendored/fetched one.

## A real, empirically-found limitation: not every EGL driver implements ES1

During this backend's own bring-up, a genuine OpenGL ES 1.1 context could **not** be created on
this project's Linux development container, whose only GL driver is Mesa's software rasterizer
(llvmpipe) via EGL. This was verified independently of CNA/SDL3 with a minimal raw EGL program:

- `eglChooseConfig()` with `EGL_RENDERABLE_TYPE = EGL_OPENGL_ES_BIT` **succeeds** and reports
  every enumerated config as ES1-capable (`EGL_CONFORMANT` includes the ES1 bit).
- `eglCreateContext()` on that same config with `EGL_CONTEXT_CLIENT_VERSION = 1` **fails** with
  `EGL_BAD_CONFIG` (`0x3003`), on every config, consistently.
- The identical program requesting an ES2 context (`EGL_CONTEXT_CLIENT_VERSION = 2`,
  `EGL_RENDERABLE_TYPE = EGL_OPENGL_ES2_BIT`) **succeeds** on the same driver.

In other words: this specific Mesa/llvmpipe build advertises ES1-capable EGL configs but does not
actually implement ES1 context creation — a real, known category of Mesa limitation (desktop
Linux software rendering dropped genuine "Common profile" ES1 support in a way ES2/ES3 was not),
not a CNA or SDL3 bug. Building and running `cna_test_opengles1_clear_readback` end-to-end on
this same container reaches the identical conclusion through the real backend/SDL3 stack (which
routes an `SDL_WINDOW_OPENGL` window through GLX on X11, a distinct code path from the raw EGL
spike above, yet fails for the same underlying reason):

```
[WindowDebug] after SDL_CreateWindow: flags=0x622 borderless=false fullscreen=false
terminate called after throwing an instance of 'std::runtime_error'
  what():  OpenGLES1: SDL_GL_CreateContext failed (no OpenGL ES 1.1 driver available on this
  system): Could not create GL context: BadAlloc (insufficient resources for operation)
```

`OpenGLES1GraphicsBackend`'s `CreateGLContext()` uses the exact same
`SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES)` +
`SDL_GL_CreateContext()` pattern EasyGL already uses for ES3 (just requesting major=1, minor=1
instead), so it fails identically and throws a clear `std::runtime_error` identifying the missing
driver, rather than silently falling back to something else.

**Practical implication:** this backend cannot be runtime-verified (context creation, Clear/
Present, any pixel-level test) on a plain desktop Linux container using stock Mesa. It targets:

- embedded/mobile Linux targets with a vendor ES1 driver (PowerVR, Mali, VideoCore/Broadcom,
  older Android devices via the NDK's `libGLESv1_CM.so`),
- desktop hosts whose GL vendor driver (proprietary NVIDIA/AMD/Intel, or a translation layer such
  as ANGLE) genuinely implements ES1 CM,
- CI/dev environments with a real GPU and driver stack, as opposed to Mesa's software renderer.

This mirrors this project's own existing precedent for D3D11/D3D12 (Windows-only, cannot be
validated on a Linux container either) and Vulkan (requires a real Vulkan ICD) — a backend whose
correctness this repository can assert by code review and compile-time verification, with runtime
pixel verification left to a host that actually has the driver. `cmake/Tests/OpenGLES1Tests.cmake`
registers `OpenGLES1_Clear_Readback` unconditionally (no automatic skip) — on a host without a
real ES1 driver it fails fast with the `SDL_GL_CreateContext failed` message above, same as a
Vulkan test would fail without an ICD.

## Implemented baseline

- Window/context lifecycle: real ES 1.1 context creation via SDL3, `Clear()`/`Present()`,
  `SetVirtualResolution()`/`SetPresentationMode()`, `TransformWindowToLogical()`/
  `TransformLogicalToWindow()` (same `FixedHeightDynamicWidth` scaling math as every other
  backend), `DebugSimulateContextLoss()`/`DebugRestoreContext()` (destroy + recreate).
- `Texture2D` creation and level-0 upload (`glTexImage2D`), sampler filter/wrap state
  (`glTexParameteri`).
- Client-side vertex/16-bit index buffers (`OpenGLES1VertexBufferBackend`/
  `OpenGLES1IndexBufferBackend`) — ES 1.1 core does not mandate GPU buffer objects
  (`GL_OES_vertex_buffer_object` is an optional extension), so every draw uploads directly from a
  CPU-side byte array via `glVertexPointer`/`glColorPointer`/`glTexCoordPointer`/
  `glNormalPointer`, which the fixed-function pipeline always supports without any extension.
- `SpriteBatch` (2D): an orthographic `glOrthof` projection matching XNA's top-left-origin pixel
  convention, `GL_MODULATE` texture environment, per-vertex color, rotation/origin/flip/layer
  handling identical to every other backend's `SpriteBatch` math.
- `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` (`BasicEffect` with
  `VertexColorEnabled=true`, no lighting/texture): loads `GL_PROJECTION`/`GL_MODELVIEW` directly
  from XNA's `Matrix::ToColumnMajor()`.
- `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`: dispatches by vertex stride (16/20/24/32, the same
  convention as every other backend) and translates `GpuDrawParams` onto fixed-function state:
  - texture (`glTexEnvi(GL_MODULATE)`),
  - up to 3 directional lights (`GL_LIGHT0..2`, `glLightfv`/`glMaterialfv`/`glMaterialf`), applied
    under a view-only `GL_MODELVIEW` so world-space light directions land correctly in eye space,
  - fog (`glFog*`, `GL_LINEAR` mode, matching `BasicEffect`'s eye-space start/end convention),
  - a best-effort `glAlphaFunc` mapping of `AlphaTestEffect`'s 4-way tolerance-band test.
- Render state: `ApplyBlendState` (`GL_OES_blend_func_separate`/`GL_OES_blend_subtract` resolved
  at runtime via `SDL_GL_GetProcAddress`, with a documented fallback when absent),
  `ApplyDepthStencilState`, `ApplyRasterizerState` (cull mode, scissor), `ApplySamplerState`,
  `SetScissorRect`, `SetViewport`.
- `ReadBackbuffer()` (`glReadPixels` + row-flip, used by `GraphicsDevice::GetBackBufferData()`).

## Important limitations

This backend has **no programmable shader path at all** — `CreateEffectBackend()` keeps
`IGraphicsBackend`'s base `nullptr` default. The following are **permanent, not "not yet
implemented"**, gaps for a fixed-function ES 1.1 pipeline:

- **Custom `ShaderEffect` / GLSL shaders** — no shader compiler exists in ES 1.1 at all.
- **`SkinnedEffect`/`SkinnedPbrEffect`** — no vertex skinning without the rare
  `GL_OES_matrix_palette` extension, not assumed present.
- **`PbrEffect`** — metallic-roughness BRDF math has no fixed-function equivalent.
- **`EnvironmentMapEffect`** — cube-map sampling has no fixed-function equivalent in ES 1.1 core
  (`GL_OES_texture_cube_map` is an optional, uncommon extension on CM implementations); `envMap`
  is accepted by `GpuDrawParams` but ignored, falling back to the plain colored path.
- **`DualTextureEffect`** — ES 1.1 does support real multitexturing (`GL_MAX_TEXTURE_UNITS` ≥ 2 on
  a conformant implementation), so this is technically feasible on this pipeline, but is not
  implemented in this baseline (`dualTexture` falls back to the plain colored path). Left as
  future work, tracked in `plan_opengles1.md`.
- **Instancing** (`DrawInstancedPrimitivesEx`) — no fixed-function instancing mechanism exists;
  falls back to the plain colored path.
- **`RenderTarget2D`/`RenderTargetCube`/`Texture3D`/`TextureCube`/`OcclusionQuery`** — ES 1.1 core
  has none of these; `GL_OES_framebuffer_object` (a common but optional extension) could back a
  future `RenderTarget2D`, tracked as future work in `plan_opengles1.md`. The base
  `IGraphicsBackend` defaults (`return nullptr`) apply unchanged.
- **`ApplyBlendState`'s `BlendFactor`/`InverseBlendFactor`** — ES 1.1 core has no `glBlendColor`/
  `GL_CONSTANT_COLOR`; these two `Blend` ordinals fall back to `SourceAlpha`/`InverseSourceAlpha`.
- **`BlendFunction::Max`/`Min`** — `GL_OES_blend_subtract` only defines Add/Subtract/
  ReverseSubtract; Max/Min fall back to Add.
- **Two-sided stencil** — ES 1.1 core has no separate front/back stencil functions; only the
  clockwise (front) face's stencil state is applied.
- **`RasterizerState.FillMode = WireFrame`** and **`DepthBias`/`SlopeScaleDepthBias`** — ES 1.1 has
  neither `glPolygonMode` nor `glPolygonOffset`; both are silently ignored (`SupportsCapability`
  reports `WireFrame` as unsupported).
- **MSAA** — not implemented in this baseline (`SupportsCapability(MultiSampleAntiAliasing)`
  returns `false`).
- **Combining per-vertex `Color` with `BasicEffect.DiffuseColor` simultaneously** — the
  fixed-function pipeline has exactly one "current color" input to `GL_MODULATE`; when
  `VertexColorEnabled` is true this backend uses the per-vertex color array and does **not**
  additionally multiply in `DiffuseColor` (no multiply-both-inputs stage without extra
  `GL_COMBINE` setup this baseline does not implement). When `VertexColorEnabled` is false,
  `DiffuseColor` is used as the flat constant color instead.
- **Compressed texture formats (DXT/BC)** — not implemented; same cross-backend gap every other
  CNA backend currently has.

`GraphicsCapability::CustomEffects`, `MultipleRenderTargets`, `AnisotropicFiltering`, `WireFrame`,
`OcclusionQuery`, and `MultiSampleAntiAliasing` all report `false` from `SupportsCapability()` —
query before relying on the corresponding feature, per that method's own documented contract.

## Architecture notes

Unlike every other CNA graphics backend, there is no shader compilation, no bind groups/descriptor
sets, and no pipeline objects — the entire draw path is GL's global matrix stack
(`glMatrixMode`/`glLoadMatrixf`), texture environment stage (`glTexEnv*`), and per-vertex
lighting/fog fixed-function state, reconfigured on every draw call. Vertex/index data lives in
plain CPU-side `std::vector`s and is re-submitted via client-array pointers on every draw
(`glVertexPointer`/`glDrawArrays`/`glDrawElements`) rather than uploaded once to a GPU buffer —
this is simpler and always spec-legal on ES 1.1 (real GPU buffer objects are the optional
`GL_OES_vertex_buffer_object` extension), at the cost of re-uploading the same data every frame if
a game does not change it — a reasonable baseline trade-off for a first working implementation of
a legacy, fixed-function-only target.

See `plan_opengles1.md` for task-level status and the remaining work.
