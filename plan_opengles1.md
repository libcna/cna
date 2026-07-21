# OpenGL ES 1.1 Backend Implementation Plan

> The OpenGLES1 backend was authorized and started on **2026-07-21** as CNA's sixth graphics
> backend: a genuine **OpenGL ES 1.1 fixed-function ("Common"/CM profile)** implementation,
> deliberately independent of the EasyGL backend (EasyGL targets WebGL2/OpenGL ES 3.0, a
> shader-based programmable pipeline, and cannot create an ES 1.1 context at all — there is no
> code sharing between the two).
>
> **Status legend:** ✅ implemented *and verified against its stated acceptance criteria*;
> 🟨 code exists but has not met those criteria (most commonly: correct and builds, but could not
> be runtime-verified on this development host — see below); ⬜ not implemented.
>
> **A real, load-bearing finding from this backend's own bring-up (2026-07-21):** this project's
> Linux development container has no OpenGL ES 1.1 driver capable of actually creating an ES1
> context, despite Mesa's EGL implementation advertising ES1-capable configs. Verified with a
> minimal raw EGL program, independent of SDL3/CNA: `eglChooseConfig()` with
> `EGL_RENDERABLE_TYPE = EGL_OPENGL_ES_BIT` succeeds and reports every config ES1-capable, but
> `eglCreateContext()` on that same config with `EGL_CONTEXT_CLIENT_VERSION = 1` fails with
> `EGL_BAD_CONFIG` on every single config, consistently. The identical program requesting an ES2
> context on the same driver succeeds without incident. See `docs/opengles1-backend.md` for the
> full spike output. This means every runtime-behavior row below is honestly `🟨` (code reviewed
> line-by-line against the ES 1.1 spec and this project's own established cross-backend
> conventions, and confirmed to *compile and link* against real system `GLESv1_CM`/`GLES/gl.h`
> headers/libraries) rather than `✅`, until run on a host with a genuine ES1 driver (embedded
> Linux/Android/PowerVR/Mali hardware, or a desktop vendor driver — proprietary NVIDIA/AMD/Intel,
> or an ES1-capable translation layer — that actually implements it). This is the same
> "can't be runtime-validated on this particular container" situation this repository already
> has for D3D11/D3D12 (Windows-only) and, to a lesser extent, Vulkan (needs a real ICD) — see
> `plan_dx.md`'s own equivalent framing.
>
> **Platform scope:** any host with a real, working OpenGL ES 1.1 (Common profile) driver exposed
> through EGL and reachable via `SDL_GL_CreateContext()` with
> `SDL_GL_CONTEXT_PROFILE_MASK = SDL_GL_CONTEXT_PROFILE_ES`, `major=1`, `minor=1`. Confirmed NOT to
> work on this project's own Mesa/llvmpipe development container (see above) — do not assume any
> given Linux desktop host has a working ES1 driver without checking first (a quick way to check:
> the raw EGL spike program described in `docs/opengles1-backend.md`).

## Design decisions

1. **Genuinely separate from EasyGL, not a variant of it.** EasyGL requests an ES 3.0 context
   (`SDL_GL_CONTEXT_MAJOR_VERSION=3`) and is entirely shader-based (`easygl::Program`, GLSL vertex/
   fragment sources baked into `EasyGLGraphicsBackend.cpp`). OpenGL ES 1.1 has no shader stage at
   all — a completely different rendering model (fixed-function matrix stack, texture
   environment, per-vertex lighting). Sharing code between the two would mean either bolting
   fixed-function emulation onto a shader backend (defeating the point of testing a real ES1
   target) or forcing EasyGL's shader assumptions into a context that cannot run shaders. The two
   backends share zero source files; both independently implement `IGraphicsBackend`.
2. **Real system library, not vendored.** Requires `libgles1`/`libgles-dev` (Debian/Ubuntu; other
   distros/embedded SDKs have equivalents) providing `libGLESv1_CM.so`, `GLES/gl.h`,
   `GLES/glext.h`. `cmake/BackendLibraries.cmake` `find_library`/`find_path`s these and fails with
   a clear `FATAL_ERROR` + install instructions if absent, matching `VULKAN`'s
   `find_package(Vulkan REQUIRED)` precedent — this is a hard system dependency, not something
   CNA fetches or vendors.
3. **Client-side vertex/index arrays, not GPU buffer objects.** ES 1.1 core does not mandate real
   buffer objects (`GL_OES_vertex_buffer_object` is an optional extension, not assumed present).
   `OpenGLES1VertexBufferBackend`/`OpenGLES1IndexBufferBackend` simply hold a CPU-side
   `std::vector<uint8_t>`/`std::vector<uint16_t>`, and every draw call re-submits it directly via
   `glVertexPointer`/`glColorPointer`/`glTexCoordPointer`/`glNormalPointer` + `glDrawArrays`/
   `glDrawElements` client-array pointers — always spec-legal on any ES 1.1 implementation, at the
   cost of re-uploading unchanged data every frame. A future task could add real VBO support behind
   a runtime `GL_OES_vertex_buffer_object` extension check (see Phase 2 below).
4. **`GpuDrawParams` → fixed-function state translation, not a fallback-only stub.**
   `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` dispatch on vertex stride (16/20/24/32, the same
   convention every other CNA backend already uses) and translate what fixed-function ES 1.1 can
   actually express: texture (`GL_MODULATE`), up to 3 directional lights (`GL_LIGHT0..2`), fog,
   and a best-effort `glAlphaFunc` mapping of `AlphaTestEffect`'s tolerance-band test. Fields with
   no fixed-function equivalent (skinning, PBR, cube-map environment mapping, custom shader
   pointer, instancing) fall back to the plain `DrawColoredPrimitives` path rather than silently
   producing wrong output — see `docs/opengles1-backend.md`'s "Important limitations" for the full
   list and why each one is a genuine, permanent gap rather than a "not yet implemented" one.
5. **Lights are applied under a view-only `MODELVIEW`, not world×view.** `glLightfv(..., GL_POSITION, ...)`
   bakes in whatever `GL_MODELVIEW` matrix is current at the moment it's called — the classic
   fixed-function idiom for world-space lights is to load just the camera (view) matrix before
   setting light state, then load the full world×view matrix afterward for the actual vertex
   transform. Verified against the ES 1.1 spec's own description of light-position transformation,
   not assumed.
6. **No programmable shaders, ever — `CreateEffectBackend()` keeps the base `nullptr` default.**
   Distinct from "not yet implemented": ES 1.1 fundamentally has no shader compiler. Custom
   `ShaderEffect`, `PbrEffect`, `SkinnedEffect`/`SkinnedPbrEffect`, and `EnvironmentMapEffect` are
   therefore permanent, not future-phase, gaps for this backend (unless a later phase adds
   `GL_OES_matrix_palette`/`GL_OES_texture_cube_map` extension-gated support — see Phase 2).

## Active execution order — do this one task at a time

1. `OPENGLES1-1` – `OPENGLES1-20` (baseline device/context/clear/present/viewport) — ✅ code
   complete 2026-07-21, 🟨 runtime-unverifiable on this container (see the finding above).
2. `OPENGLES1-21` – `OPENGLES1-35` (`Texture2D`, vertex/index buffers, `SpriteBatch`) — same
   ✅ code / 🟨 runtime status.
3. `OPENGLES1-36` – `OPENGLES1-55` (`DrawColoredPrimitives`/`DrawPrimitivesEx` fixed-function
   dispatch: texture, lighting, fog, alpha test) — same ✅ code / 🟨 runtime status.
4. `OPENGLES1-56` – `OPENGLES1-70` (render state: blend/depth/stencil/rasterizer/sampler/scissor/
   viewport, `ReadBackbuffer`) — same ✅ code / 🟨 runtime status.
5. `OPENGLES1-71` onward (Phase 2 — see below) — ⬜ not started, pick up next.

---

## Phase 1 — Baseline (device, 2D, fixed-function 3D colored/textured/lit draw)

| # | Task | Status | Acceptance criteria |
| --- | --- | --- | --- |
| OPENGLES1-1 | New backend directory `include/CNA/Internal/Backends/OpenGLES1/` + `src/CNA/Internal/Backends/OpenGLES1/`, `OpenGLES1GraphicsBackend.hpp`/`.cpp` | ✅ | Files exist, compile against real `GLES/gl.h`/`GLES/glext.h`. |
| OPENGLES1-2 | `cmake/BackendSelection.cmake`: add `OPENGLES1` to the `CNA_GRAPHICS_BACKEND` `STRINGS` list, `option(CNA_BACKEND_OPENGLES1 ...)`, `elseif` block setting `BACKEND_DIR`/`BACKEND_TARGET`/`CNA_BACKEND_DEFINE` | ✅ | `cmake -DCNA_GRAPHICS_BACKEND=OPENGLES1` selects the new backend dir/target. |
| OPENGLES1-3 | `cmake/BackendLibraries.cmake`: `find_library(GLESv1_CM)` + `find_path(GLES/gl.h)`, `FATAL_ERROR` with install instructions if missing, link `SDL3::SDL3` + the found library | ✅ | Configure fails clearly without `libgles1`/`libgles-dev`; succeeds and links correctly with them installed (confirmed on this container after installing the packages). |
| OPENGLES1-4 | `CMakeLists.txt`: `include(cmake/Tests/OpenGLES1Tests.cmake)` | ✅ | |
| OPENGLES1-5 | Real ES 1.1 context creation via `SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION=1, MINOR=1, PROFILE_MASK=ES)` + `SDL_GL_CreateContext()`, depth/stencil buffer attributes, clear `std::runtime_error` on failure | ✅ code / 🟨 runtime | Same pattern as `EasyGLGraphicsBackend`'s own ES3 context creation (major/minor/profile changed). Cannot be runtime-verified on this container — see finding above; will throw its own descriptive error rather than segfault/hang if a host's driver can't create the context. |
| OPENGLES1-6 | `RegisterForWindow`/`UnregisterForWindow` (mouse/touch coordinate bridge) | ✅ | Matches every other backend's identical registration calls in constructor/destructor. |
| OPENGLES1-7 | `Clear`/`ClearColorAndDepth`/`ClearDepth`/`ClearStencil`/`ClearDepthAndStencil`/`ClearColorAndStencil`/`ClearColorDepthAndStencil` | ✅ code / 🟨 runtime | Forces `glDepthMask`/`glStencilMask` writable before each clear (matches `EasyGLGraphicsBackend`'s identical convention) so the requested clear value always reaches the buffer regardless of the last-applied `DepthStencilState`. |
| OPENGLES1-8 | `Present()` (`SDL_GL_SwapWindow`) | ✅ code / 🟨 runtime | |
| OPENGLES1-9 | `GetViewportSize`/`SetVirtualResolution`/`SetPresentationMode`/`SetSwapInterval` | ✅ code / 🟨 runtime | Same `FixedHeightDynamicWidth` logical-size math as `EasyGLGraphicsBackend`. |
| OPENGLES1-10 | `TransformWindowToLogical`/`TransformLogicalToWindow` | ✅ code / 🟨 runtime | Same pure-uniform-scale math (no letterbox offset under the default presentation mode) as `EasyGLGraphicsBackend`'s identical methods. |
| OPENGLES1-11 | `DebugSimulateContextLoss`/`DebugRestoreContext` (destroy + recreate context, reload extension entry points) | ✅ code / 🟨 runtime | |
| OPENGLES1-12 | `ReadBackbuffer` (`glReadPixels` + row flip) | ✅ code / 🟨 runtime | Y-flip matches every other GL-based backend's top-left-origin convention. |
| OPENGLES1-13 | `SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled` | ✅ code / 🟨 runtime | |
| OPENGLES1-14 | `CreateTexture`/`OpenGLES1TextureBackend` (level-0 RGBA8 upload, default linear filter/repeat wrap) | ✅ code / 🟨 runtime | `GetNativeTexture()` returns `nullptr` (no `SDL_Renderer` involved), matching `EasyGL`'s identical convention for GL-based backends. |
| OPENGLES1-15 | `CreateVertexBuffer`/`OpenGLES1VertexBufferBackend` (client-side byte array, `SetData`) | ✅ code / 🟨 runtime | See design decision 3. |
| OPENGLES1-16 | `CreateIndexBuffer16`/`OpenGLES1IndexBufferBackend` (client-side `uint16_t` array, `SetData16`) | ✅ code / 🟨 runtime | 32-bit indices fall back to the base class's `CreateIndexBuffer16` delegation (unimplemented on this backend, matches several other backends' current state). |
| OPENGLES1-17 | `CreateSpriteBatch`/`OpenGLES1SpriteBatchBackend`: quad batching, texture/rotation/origin/flip/layer math (ported from the same well-established formula every CNA backend's `SpriteBatch` uses) | ✅ code / 🟨 runtime | `glOrthof` top-left-origin projection, `GL_MODULATE` texture environment, per-vertex color. |
| OPENGLES1-18 | `SpriteBatch::SetTransformMatrix`/`SetSamplerFilter`/`SetSamplerAddressMode` | ✅ code / 🟨 runtime | |
| OPENGLES1-19 | `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` (`BasicEffect` with `VertexColorEnabled=true`, no texture/lighting) | ✅ code / 🟨 runtime | Loads `GL_PROJECTION`/`GL_MODELVIEW` directly from `Matrix::ToColumnMajor()`. |
| OPENGLES1-20 | `LoadExtensionEntryPoints()`: resolve `glBlendFuncSeparateOES`/`glBlendEquationOES` via `SDL_GL_GetProcAddress`, null-safe fallback | ✅ code / 🟨 runtime | Confirmed these two symbols exist in the real system `GLES/glext.h` on this container (`GL_OES_blend_func_separate`/`GL_OES_blend_subtract`). |
| OPENGLES1-21 | `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`: stride-based dispatch (16/20/24/32), texture (`GL_MODULATE`), fallback to colored path for skinning/PBR/env-map/dual-texture/instancing/custom-effect | ✅ code / 🟨 runtime | See design decision 4. |
| OPENGLES1-22 | Lighting: up to 3 directional lights (`GL_LIGHT0..2`), material diffuse/specular/emission/shininess, ambient via `GL_LIGHT_MODEL_AMBIENT`, applied under a view-only `MODELVIEW` | ✅ code / 🟨 runtime | See design decision 5. |
| OPENGLES1-23 | Fog (`GL_FOG`, `GL_LINEAR` mode, `fogStart`/`fogEnd`/`fogColor`) | ✅ code / 🟨 runtime | Matches `BasicEffect`'s eye-space start/end convention (same semantics as every shader-based backend's fog implementation, just via fixed-function `glFog*` instead of a shader uniform). |
| OPENGLES1-24 | Alpha test: best-effort `glAlphaFunc` mapping of `GpuDrawParams::alphaTest`'s 4-way tolerance-band test | ✅ code / 🟨 runtime | Documented deviation: fixed-function has exactly one comparison function, not a 4-way weighted band — see `docs/opengles1-backend.md`. |
| OPENGLES1-25 | `ApplyBlendState` (`glBlendFunc`/`glBlendFuncSeparateOES`, `glBlendEquationOES` where available) | ✅ code / 🟨 runtime | `BlendFactor`/`InverseBlendFactor` fall back to `SourceAlpha`/`InverseSourceAlpha` (no `glBlendColor` in ES 1.1 core); `BlendFunction::Max`/`Min` fall back to `Add` (no ES1.1 equivalent). |
| OPENGLES1-26 | `ApplyDepthStencilState` (depth func/write, single-sided stencil func/op/mask) | ✅ code / 🟨 runtime | Two-sided stencil not supported (documented deviation — no separate front/back stencil functions in ES 1.1 core). |
| OPENGLES1-27 | `ApplyRasterizerState` (cull mode, scissor enable) | ✅ code / 🟨 runtime | `FillMode::WireFrame`/`DepthBias`/`SlopeScaleDepthBias` silently ignored (no `glPolygonMode`/`glPolygonOffset` in ES 1.1) — `SupportsCapability(WireFrame)` reports `false`. |
| OPENGLES1-28 | `ApplySamplerState` (filter/wrap per the bound texture unit) | ✅ code / 🟨 runtime | Single texture unit only in this baseline. |
| OPENGLES1-29 | `SetScissorRect`/`SetViewport` | ✅ code / 🟨 runtime | |
| OPENGLES1-30 | `SupportsCapability()` overrides: `MultiSampleAntiAliasing`/`MultipleRenderTargets`/`AnisotropicFiltering`/`WireFrame`/`OcclusionQuery`/`CustomEffects` all `false` | ✅ | Matches this backend's actually-implemented feature set exactly — no capability is claimed that the code doesn't back. |
| OPENGLES1-31 | `docs/opengles1-backend.md`: status, build instructions, the EGL/Mesa ES1 finding, implemented baseline, important limitations, architecture notes | ✅ | |
| OPENGLES1-32 | `examples/opengles1_clear_readback_test.cpp` + `cmake/Tests/OpenGLES1Tests.cmake` (`OpenGLES1_Clear_Readback` CTest: `Clear`, `SpriteBatch` quad, `DrawUserPrimitives(VertexPositionColor)`, all via `GetBackBufferData()` pixel assertions) | 🟨 | Compiles and links against the real backend; cannot execute to a PASS/FAIL result on this container (context creation throws — see the finding above). No automatic CTest skip is registered (mirrors `VulkanTests.cmake`'s own no-skip precedent — a missing driver is a real, visible test failure, not silently swallowed). |

---

## Phase 2 — Future work (not started, pick up next)

None of the rows below are required for a working baseline; they extend coverage closer to the
established EasyGL/Vulkan/WebGPU backends' feature set, within what ES 1.1 fixed-function can
actually express.

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| OPENGLES1-71 | `DualTextureEffect` real dispatch via genuine ES 1.1 multitexturing (`glActiveTexture`/`glClientActiveTexture`, unit 0 `GL_MODULATE`, unit 1 combined per `DualTextureEffect`'s multiply-overlay formula) | ⬜ | Technically feasible on this pipeline (unlike skinning/PBR/env-map) — see docs' limitations list. |
| OPENGLES1-72 | `RenderTarget2D` via `GL_OES_framebuffer_object` (runtime extension check, `FATAL`-free graceful "not supported" when absent) | ⬜ | Common but optional extension — needs a runtime capability check, not a hard `find_library` gate like the core library itself. |
| OPENGLES1-73 | Real `GL_OES_vertex_buffer_object`-backed vertex/index buffers when the extension is present at runtime, falling back to the existing client-array path otherwise | ⬜ | Perf-only; current client-array approach is always correct, just re-uploads unchanged data every frame. |
| OPENGLES1-74 | `EnvironmentMapEffect` via `GL_OES_texture_cube_map` (runtime extension check) | ⬜ | Uncommon extension on real CM implementations; verify availability before committing further design here. |
| OPENGLES1-75 | `OcclusionQuery` via a countable equivalent, if any real ES1 CM driver exposes one (likely none do — core ES 1.1 has no occlusion query mechanism at all, unlike ES3's `GL_ANY_SAMPLES_PASSED`) | ⬜ | May turn out to be a permanent gap on further research, not merely deferred. |
| OPENGLES1-76 | Wireframe emulation (re-expand triangles into `GL_LINES` at draw time, the same technique `EasyGLGraphicsBackend::DrawWireframe` already uses for its own ES3/no-`glPolygonMode` target) | ⬜ | |
| OPENGLES1-77 | Runtime verification on an actual ES1-capable host (embedded Linux/Android device, or a desktop vendor driver confirmed to support ES1 CM) — flip every `🟨` row above to `✅` once confirmed | ⬜ | Blocked on hardware/environment access, not on code — see the finding at the top of this file. |
| OPENGLES1-78 | Cross-backend pixel-parity test (same scene rendered on OPENGLES1 vs. an already-verified backend) | ⬜ | Same shape as `plan_webgpu.md`'s own `WEBGPU-123`; needs OPENGLES1-77 first. |

---

## Table of intentional deviations from XNA/FNA behavior (ES 1.1 fixed-function constraints)

| Area | XNA/FNA behavior | This backend's behavior | Why |
| --- | --- | --- | --- |
| `AlphaTestEffect` alpha test | 4-way weighted tolerance band (`GpuDrawParams::alphaTest`) | Best-effort single `glAlphaFunc` comparison | ES 1.1 fixed-function alpha test is a single comparison function; no tolerance-band concept exists. |
| `BlendState.ColorSourceBlend/DestinationBlend = BlendFactor/InverseBlendFactor` | Constant blend color via `GraphicsDevice.BlendFactor` | Falls back to `SourceAlpha`/`InverseSourceAlpha` | No `glBlendColor`/`GL_CONSTANT_COLOR` in ES 1.1 core. |
| `BlendState.ColorBlendFunction/AlphaBlendFunction = Max/Min` | GPU min/max blend equation | Falls back to `Add` | `GL_OES_blend_subtract` only defines Add/Subtract/ReverseSubtract. |
| `DepthStencilState` two-sided stencil | Independent CW/CCW stencil func/op | Only the CW (front) face's state applied | ES 1.1 core has no two-sided stencil API. |
| `RasterizerState.FillMode = WireFrame` | Real wireframe rasterization | Ignored (solid fill) | No `glPolygonMode` in ES 1.1; `SupportsCapability(WireFrame)` reports `false`. Phase 2 tracks a line-re-expansion emulation. |
| `RasterizerState.DepthBias`/`SlopeScaleDepthBias` | GPU depth bias | Ignored | No `glPolygonOffset` in ES 1.1. |
| Simultaneous per-vertex `Color` + `BasicEffect.DiffuseColor` | Both multiply together per pixel | Only one is applied (vertex color array takes precedence when `VertexColorEnabled`) | Fixed-function has exactly one "current color" input to `GL_MODULATE`; combining both needs `GL_COMBINE` multi-stage setup not implemented in this baseline. |
| `SkinnedEffect`/`SkinnedPbrEffect`, `PbrEffect`, `EnvironmentMapEffect`, custom `ShaderEffect`, instancing | Full GPU dispatch | Falls back to the plain colored path | No fixed-function equivalent (or, for env-map, only via an uncommon optional extension not yet implemented — Phase 2). Permanent gap, not "not yet implemented", except where Phase 2 explicitly tracks an extension-gated future implementation. |
