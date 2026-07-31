# LLGL Graphics Backend — Implementation Plan

> **Status (2026-07-31): the 2D baseline is implemented and verified against real GPU pixels on
> BOTH renderer modules.** `CNA_GRAPHICS_BACKEND=LLGL` configures and builds
> (`cna_backend_graphics_llgl`), and on this environment's virtual display (Xvfb + Mesa lavapipe
> for Vulkan, llvmpipe for OpenGL) a real SDL window, a real `LLGL::RenderSystem`, and a real swap
> chain clear and present 60 frames, upload a real `Texture2D`, and draw a real `SpriteBatch` scene
> whose pixels are read back and asserted: quadrant orientation, tint multiplication,
> `SpriteEffects::FlipHorizontally`, and `BlendState::NonPremultiplied` alpha blending. Four CTests,
> all green: `Llgl_Smoke` and `Llgl_Smoke_OpenGL` (8/8 each), `Llgl_2D` and `Llgl_2D_OpenGL`
> (10/10 pixel checks each).
>
> **`LLGL-17` — "the OpenGL module clears but draws nothing" — is fixed** (2026-07-31, same day it
> was filed). The cause was CNA's own shader-language selection, not LLGL: a modern OpenGL module
> reports *both* GLSL and SPIR-V (desktop GL ingests SPIR-V through `GL_ARB_gl_spirv`), the
> selection checked SPIR-V first, and GL accepted the Vulkan-targeted SPIR-V far enough to
> rasterize geometry from the position attribute while every other attribute and the uniform block
> read as zero. GLSL is now preferred wherever a module offers it. The regression that let this
> survive is closed too: the OpenGL module now has its own CTest registrations rather than being
> exercised only by whatever the default preference happened to pick.
>
> **The colour-only 3D path is implemented and pixel-verified too** (`LLGL-24`, 2026-07-31):
> `VertexDeclaration` translation, per-layout vertex shaders, a keyed pipeline cache, real vertex
> and index buffer draws with `vertexStart`/`startIndex`/`baseVertex` honoured, depth test and
> depth write, cull mode, and fill mode — `Llgl_3D` and `Llgl_3D_OpenGL`, 12/12 checks each. The
> **`BasicEffect`'s textured, tinted, fogged, alpha-tested AND LIT paths are all done** (`LLGL-25`,
> closed): one texture, `DiffuseColor`, `Alpha`, vertex-colour modulation, fog, `AlphaTestEffect`,
> and per-pixel directional lighting (ambient, up to three lights, specular, `EmissiveColor`) —
> `Llgl_BasicEffect`/`Llgl_Lighting` and their `_OpenGL` twins, 13/13 and 8/8 checks each.
> `DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect` and `PbrEffect` are still not
> implemented, nor are cube and volume textures or custom `ShaderEffect`s.
> Each either reports itself unsupported through `SupportsCapability()`/the shared interface's own
> "no backend" convention, or throws through `NotYetImplemented()`. None of them silently no-ops.
>
> **`RenderTarget2D` is implemented too** (`LLGL-26`, 2026-07-31): draw into it, unbind back to the
> swap chain, sample it back onto the screen with `SpriteBatch`, draw the 3D path (`BasicEffect` +
> depth test) into it, and `GetData()` straight off the colour attachment — `Llgl_RenderTarget`/
> `_OpenGL`, 9/9 on both modules. `RenderTargetCube`, MRT and MSAA/mip-mapped render targets are
> still not implemented.
>
> **Occlusion queries are implemented too** (`LLGL-28`, 2026-07-31): `LLGL::QueryHeap`-backed,
> answering synchronously (a submit-and-wait forced on first `IsComplete()`/`PixelCount()` call,
> not genuine async polling) — `Llgl_OcclusionQuery`/`_OpenGL`, 6/6 on both modules.
>
> **Custom `ShaderEffect`s are implemented too** (`LLGL-27`, 2026-07-31), scoped to `SpriteBatch`
> draws (the fixed sprite vertex layout, not an arbitrary `VertexDeclaration`) — a real runtime
> GLSL→SPIR-V compile via `libshaderc` when the Vulkan module is loaded, GLSL handed to LLGL
> directly on the OpenGL module — `Llgl_ShaderEffect`/`_OpenGL`, 6/6 on both modules. Every item in
> `plan_llgl.md`'s original phase-5 scope (`LLGL-26`/`27`/`28`) is now implemented in at least its
> initial, documented cut; `RenderTargetCube`, MRT and MSAA/mip-mapped render targets remain the
> concrete open follow-ups.

---

## Why an LLGL backend

Every other CNA backend names a native graphics API (`VULKAN`, `D3D11`, `EASYGL`, …) or a
platform-provided abstraction (`SDL_GPU`, `BGFX`, `WEBGPU`). LLGL is a thin, hand-written C++
abstraction over OpenGL / Vulkan / Direct3D 11 / Direct3D 12 / Metal, with an API shaped very much
like modern explicit APIs (command buffers, pipeline state objects, pipeline layouts, resource
heaps) but far smaller than bgfx.

That makes it interesting to CNA for two distinct reasons:

1. **A second, independent multi-API abstraction** to compare against bgfx and SDL_gpu. Where those
   two hide their backend choice almost entirely, LLGL exposes it as a named module loaded at
   runtime, which lets one CNA build genuinely switch native API without recompiling.
2. **Reach on platforms CNA does not otherwise cover well** — LLGL's Metal and Direct3D 12 modules
   are first-class, so this backend is the natural future home for a macOS/iOS target that does not
   go through MoltenVK.

The cost is that CNA now depends on someone else's abstraction of an abstraction: a defect can live
in CNA, in LLGL, or in the native driver, and telling them apart takes a spike outside CNA (this
plan's own `LLGL-17` investigation did exactly that).

---

## Design decisions

| # | Decision | Rationale |
| --- | --- | --- |
| 1 | **The renderer module is chosen at runtime, not at CMake time.** `Detail::ResolveRendererModule()` walks a preference list and loads the first module that works, caching the answer for the process. | This is the one thing LLGL offers that no other CNA backend does. Making it a compile-time choice would throw it away. |
| 2 | **Default preference is Vulkan, then OpenGL.** Overridable with `CNA_LLGL_RENDERER=auto\|opengl\|vulkan\|null`. | Chosen by the project owner. Note that LLGL itself marks its Vulkan module experimental while its OpenGL module is the mature one — the preference deliberately does not follow LLGL's own maturity ranking. |
| 3 | **The Null module is never selected automatically.** It is compiled in and reachable only through an explicit `CNA_LLGL_RENDERER=null`. | A renderer that accepts every command and draws nothing would turn "no usable GPU" into a silent black screen — the exact class of fabricated success this project's backends must never produce. |
| 4 | **CNA keeps owning the window; LLGL renders into it.** `LlglSdlSurface` adapts the existing SDL3 window to `LLGL::Surface`; LLGL's own `Window`/`Display` layer is never used to create one. | The window belongs to `GraphicsDevice`, is shared with SDL input/event handling, and exists before any backend does. |
| 5 | **Only the OpenGL module gets `SDL_WINDOW_OPENGL`.** The Vulkan module builds its surface from the native window handle and needs no SDL flag. | SDL refuses to create a window that is both `SDL_WINDOW_OPENGL` and `SDL_WINDOW_VULKAN`, so the flag has to follow the runtime module decision — which is why `ResolveRendererModule()` caches: `GraphicsDevice` asks before the window exists and the backend asks after. |
| 6 | **X11 only, for now.** A Wayland SDL window is refused with a clear error naming `SDL_VIDEODRIVER=x11`. | LLGL 0.04b compiles Wayland support only when explicitly enabled, and this integration does not enable it. Refusing beats handing LLGL a handle it cannot present to. |
| 7 | **The X11 visual is reported to LLGL, not left for LLGL to choose.** | A GLX context created for a visual other than the drawable's cannot be made current. The SDL window already committed to a visual when it was created, so it is the only one that can work. |
| 8 | **Both shader flavours are checked in, and the choice is made from the module's reported shading language, GLSL first.** Vulkan gets SPIR-V words, OpenGL gets GLSL source. | A build needs no shader toolchain — same discipline as the Bgfx and SDL_GPU backends' generated headers. The GLSL-first order is load-bearing, not cosmetic: a modern OpenGL module reports SPIR-V too, and accepting it there silently breaks every binding (see `LLGL-17`). |
| 9 | **The whole frame is buffered on the CPU and recorded at `Present()`.** Clears and sprites go into one ordered command list; vertex data accumulates in one array. | LLGL forbids buffer uploads inside a render pass. Deferring everything means uploads happen with no command buffer open at all, and submission order matches call order without any pass-splitting. |
| 10 | **Sprite geometry is baked into window pixels on the CPU; the GPU viewport stays at the full window.** Letterboxing lives in the geometry, XNA's sub-`Viewport` clipping in the scissor. | Keeps one projection constant for a whole frame, which is what makes decision 9 cheap. |
| 11 | **Clip space is treated as Y-up on every module.** | LLGL submits Vulkan viewports with a negated height, flipping Vulkan's natively Y-down clip space to match OpenGL's. `RenderingCapabilities::screenOrigin` describes viewport/scissor *rectangle* space, not clip space — keying the projection's Y sign off it renders the scene upside down (found by reading back real pixels, see `LLGL-13`). |
| 12 | **`LLGL_ENABLE_EXCEPTIONS=ON`.** | LLGL's `LLGL_TRAP` aborts the process outright when exceptions are off. CNA reports backend failures as exceptions everywhere else, so an abort would replace a reportable failure with a crash. |
| 13 | **`CXX_EXTENSIONS ON` for LLGL's own targets only.** | LLGL's `LLGL_VA_ARGS` macro is built on the `, ## __VA_ARGS__` GNU extension; with CNA's `CMAKE_CXX_EXTENSIONS OFF` inherited, LLGL does not compile at all. |
| 14 | **The LLGL archives are linked as a link group.** | The core archive and its modules genuinely reference each other, and CMake orders the modules ahead of the core. |

---

## Naming conventions for this backend

| Thing | Convention |
| --- | --- |
| Task prefix | `LLGL-` |
| CMake option value | `CNA_GRAPHICS_BACKEND=LLGL` |
| Compile definition | `CNA_BACKEND_LLGL` (plus `CNA_LLGL_HAS_OPENGL`/`_VULKAN`/`_NULL`) |
| Backend target | `cna_backend_graphics_llgl` |
| Source directory | `src/CNA/Internal/Backends/Llgl/` |
| C++ namespace | `CNA::Internal::Backends::Llgl` |
| Class prefix | `Llgl` (e.g. `LlglTextureBackend`) |
| Build directory | `cmake-build-llgl/` |

---

## Phase LLGL-1 — Infrastructure and CMake integration

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| LLGL-1 | Add `LLGL` as a valid `CNA_GRAPHICS_BACKEND` value: the `CACHE STRING`/`STRINGS` list, the `option(CNA_BACKEND_LLGL ...)` declaration, and the mutual-exclusion list. | ✅ | Done 2026-07-31. `tests/.../GraphicsBackendCompileDefinitionTests.cpp`'s `ExactlyOneGraphicsBackendIsSelected` counter and `CNA::GraphicsBackendType` were updated in the same change — both would otherwise be wrong under this backend. |
| LLGL-2 | Add the selection branch setting `BACKEND_DIR`/`BACKEND_TARGET`/`CNA_BACKEND_LLGL`, and `cmake/ThirdPartyLLGL.cmake` with `cna_configure_llgl()`. | ✅ | Done 2026-07-31. Pinned FetchContent tag `Release-v0.04b`, with `CNA_LLGL_ROOT` for an existing checkout. Vulkan module defaults to whatever `find_package(Vulkan QUIET)` reports, so a host without a Vulkan SDK still configures. |
| LLGL-3 | Link the backend target against the LLGL module archives. | ✅ | Done 2026-07-31 — see design decision 14. Found empirically as "undefined reference to `LLGL::ModuleOpenGL::AllocRenderSystem`". |
| LLGL-4 | Runtime renderer selection (`LlglRendererSelection.hpp/.cpp`): module enum, name mapping, compiled-in query, env override parsing, preference resolution, cached probe. | ✅ | Done 2026-07-31. Covered by 5 unit tests in `GraphicsBackendCompileDefinitionTests.cpp` (default preference never contains Null, override parsing, invalid-value rejection, module names, which module needs a GL window). |
| LLGL-5 | `GraphicsDevice::getBackendWindowFlags()` follows the runtime module decision. | ✅ | Done 2026-07-31 — see design decision 5. |

---

## Phase LLGL-2 — Surface, device and swap chain

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| LLGL-6 | `LlglSdlSurface`: SDL3 window → `LLGL::Surface`, including the real X11 visual and a clear error for a non-X11 driver. | ✅ | Done 2026-07-31. Xlib is confined to that one translation unit and its macros (`None`, `Status`, …) undefined immediately, so no XNA header ever sees them. |
| LLGL-7 | Load the render system, create the swap chain (24-bit depth, 8-bit stencil, requested sample count), command buffer and queue. | ✅ | Done 2026-07-31. `CNA_LLGL_DEBUG=1` additionally turns on LLGL's own debug layer — which is what found `LLGL-12`'s bind-flag defect within seconds. |
| LLGL-8 | Presentation: virtual resolution, all five `CnaPresentationMode` policies, swap interval, window↔logical coordinate transforms, resize. | ✅ | Done 2026-07-31. Verified for `FixedHeightDynamicWidth` (the default) by `Llgl_Smoke`; the other four modes share one code path and are not separately pixel-verified yet. |
| LLGL-9 | First end-to-end proof: real window, 60 frames of clear + present, clean exit. | ✅ | Verified 2026-07-31 — `examples/llgl_smoke_test.cpp` / `Llgl_Smoke`, 8/8 checks on Vulkan (lavapipe) under Xvfb. |

---

## Phase LLGL-3 — 2D vertical slice

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| LLGL-10 | Sprite shaders in both flavours plus `compile_shaders.py`, which compiles the Vulkan flavour to SPIR-V and embeds the OpenGL flavour verbatim into one generated header. | ✅ | Done 2026-07-31. `--check` mode fails if the checked-in header is stale. |
| LLGL-11 | `Texture2D`: creation from `ImageData`, `SetData` per level, GPU readback for `GetData`. | ✅ | Done 2026-07-31. Readback verified indirectly through the back-buffer path; a direct `Texture2D::GetData()` round-trip test is still owed (`LLGL-19`). |
| LLGL-12 | `SpriteBatch`: pipeline layout, per-blend-state pipeline cache, per-sampler-state sampler cache, quad building with rotation/origin/flip, frame recording and submission. | ✅ | Done 2026-07-31. Two real defects were found by asserting pixels rather than absence of exceptions: the readback texture lacked `CopyDst`/`CopySrc` (so the "cleared to black" check was passing against a zero-initialised buffer, not against the frame), and the staging texture must take the swap chain's own colour format or a B8G8R8A8 swap chain hands back byte-swapped pixels. |
| LLGL-13 | Correct orientation. | ✅ | Done 2026-07-31 — design decision 11. The first implementation keyed the projection's Y sign off `screenOrigin` and rendered every sprite vertically mirrored; caught by the quadrant check in `Llgl_2D`, not by inspection. |
| LLGL-14 | Blend state, blend factor, sampler state (complete min/mag/mip triples for all nine `TextureFilter` values), scissor, viewport. | ✅ | Done 2026-07-31. `SetBlendFactor` is only emitted when the blend state actually uses `BlendFactor`/`InverseBlendFactor` — see `LLGL-18`. |
| LLGL-15 | Back-buffer readback (`ReadBackbuffer`), used by `GraphicsDevice::GetBackBufferData` and every pixel test. | ✅ | Done 2026-07-31. The whole back buffer is captured once per frame and every region served from that capture: the swap chain's render pass loads its colour attachment as `Undefined`, so re-entering it for a second copy would read discarded content. |
| LLGL-16 | Pixel-asserted 2D proof. | ✅ | Verified 2026-07-31 — `examples/llgl_2d_test.cpp` / `Llgl_2D`, 10/10 checks on Vulkan (lavapipe) under Xvfb. |

---

## Phase LLGL-4 — Known gaps and open questions

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| LLGL-17 | **The OpenGL module clears but draws nothing.** | ✅ | Filed and fixed 2026-07-31. **The cause was CNA's, not LLGL's.** A standalone LLGL-only spike narrowed it down step by step: a quad with an identity matrix rendered in the right place (so position, pipeline and render pass were fine) while `texCoord`, `color` and the uniform block all read as zero; explicit `layout(location=)`/`layout(binding=)` qualifiers changed nothing; a `ResourceHeap` instead of individual `SetResource` bindings changed nothing; and a fragment shader hardcoded to output magenta still rendered black — which is what finally ruled out the binding path entirely. Instrumenting LLGL's own `GLLegacyShader::CompileShaderSource` showed it was never called: LLGL's GL core profile advertises `ShadingLanguage::SPIRV` (`GL_ARB_gl_spirv`) alongside GLSL, and this backend's selection checked SPIR-V first, so the OpenGL module was being handed SPIR-V compiled for Vulkan's binding model. Fixed by preferring GLSL wherever a module offers it. Both flavours are now pixel-verified: `Llgl_2D_OpenGL` 10/10, `Llgl_2D` (Vulkan) 10/10. |
| LLGL-18 | `SetBlendFactor` on OpenGL hits `ErrUnsupportedGLProc: glBlendColor` on this environment's context. | ✅ | Fixed 2026-07-31 by requesting dynamic blend-factor state, and emitting the call, only when the blend state genuinely references `Blend::BlendFactor`/`InverseBlendFactor` — correct, cheaper, and it keeps the overwhelming majority of blend states off a proc some GL tables genuinely lack. **A first pass documented this workaround before implementing it**; the gap surfaced immediately once the OpenGL module actually drew and hit the unimplemented path. A game that really uses `Blend::BlendFactor` on such a driver still fails loudly, with LLGL's own error. |
| LLGL-19 | Byte-exact texture upload/readback round-trip test. | ✅ | Done 2026-07-31 — `examples/llgl_texture_readback_test.cpp`, `Llgl_TextureReadback` (+ `_OpenGL`), 6/6 on both modules. Deliberately drives `ITextureBackend` directly rather than `Texture2D::GetData()`: the public API answers from its own CPU pixel shadow whenever it has one, so a test written against it would pass without the GPU being asked anything. Covers full-surface and sub-rectangle reads, `UpdatePixels`, per-level `UpdatePixelsLevel` (level 0 keeps its own different content), and the two refusals — an undersized destination buffer and a level the texture does not have — because a backend that half-filled a buffer and returned true would hand the shared layer fabricated pixels. |
| LLGL-20 | Pixel-verify all five presentation modes. | ✅ | Done 2026-07-31 — `examples/llgl_presentation_test.cpp`, `Llgl_Presentation` (+ `_OpenGL`), 6/6 on both modules. An 800x480 window with a 100x100 canvas (a deliberately different aspect ratio, since a matching one makes Letterbox, Overscan and Stretch indistinguishable): each mode draws a full-canvas sprite and the test asserts both where it appears and where it does not — a letterbox bar still at the clear colour is what separates "fitted" from "stretched over everything". Also covers the logical↔window transform round-trip. **This test found and closed a real limitation**: `ReadBackbuffer` used to throw outright for any non-1:1 presentation, so no pixel test could run under a scaled canvas at all. It now resolves a scaled logical pixel to the window pixel at the centre of the block it covers — nearest-neighbour, deliberately not an average, so every value returned is a colour the frame genuinely contained. A real window resize is still unproven (`LLGL-29`). |
| LLGL-21 | `BlendState.MultiSampleMask` and the per-MRT colour write masks for slots 1..3. | ⬜ | Deliberately not applied: this backend renders to a single attachment, and LLGL's sample mask lives in the blend descriptor and would multiply the pipeline cache with no 2D use. Documented, not lost. |
| LLGL-22 | Full `CnaTests` regression baseline under `-DCNA_GRAPHICS_BACKEND=LLGL`. | ✅ | **Unchanged by LLGL-25's full scope, texturing through lighting (5635 passed, 45 skipped, 18 failed): the remaining failures need cube textures, MRT, occlusion queries or custom effects, none of which that phase touched.** After LLGL-24: 5635 passed, 45 skipped, 18 failed — the 3D draw path closed four of the original failures and un-skipped 25 vertex-buffer tests. The remaining 18 are `TextureCubeTest` (9) plus four content/fuzz tests that need `CreateTextureCube`, `GraphicsDeviceCapabilityTest` (3: MRT, occlusion queries, custom effects) and the two `Cnj` effect tests. Original baseline for reference: **5606 passed, 70 skipped, 22 failed** of 5698. Every one of the 22 is a test that assumes a capability this backend does not implement yet, and each fails through a clean, documented refusal rather than a crash or a wrong answer: `TextureCubeTest` (9) plus `Texture3DTextureCubeContentTypeReaderTest`, `XnbBuiltInReaderRegistrationTest`, `XnbContainerFuzzTest` and `CnjCapabilityMatrixTest` (4 more) all need `CreateTextureCube`, which returns null; `GraphicsDeviceCapabilityTest` (4) asserts the 3D-capable answers its own header comment says it only builds against; `IndexedDrawDeferredTest`, `NonIndexedDrawRangeTest` and `VertexBufferEmptyDataTest.UploadedVerticesSupportNormalAndIndexedDrawing` (3) reach the 3D draw path; `CnjEffectTest`/`CnjStockEffectTest` (2) need custom effects. Two further failures WERE fixed rather than tolerated, following the same registration-gap precedent as `ASCII-5`/`DX3-86`: `GraphicsDeviceValidationTest`'s two `SetRenderTargets` expectations expressed this backend's own (temporary) "no render targets at all" state — since superseded again by `LLGL-26`: `SetRenderTargets_OneTarget_DoesNotThrow` now genuinely does not throw, and `SetRenderTargets_FourTargets_DoesNotThrow` throws `std::runtime_error` (this backend's own single-target limitation) alongside the other single-target backends instead of the shared layer's null-backend `NotSupportedException`. The list shrinks as phase LLGL-5 lands. |
| LLGL-31 | Lighting without a texture. | ⬜ | Refused by name (`NotYetImplemented`) rather than silently dropping the light or rendering unlit -- deliberately bounded scope for `LLGL-25`, not an oversight. Needs a third fragment-shader variant (lit, untextured) and its own pipeline layout (no texture/sampler bindings). |
| LLGL-30 | `FillMode::WireFrame` on the Vulkan module. | ⬜ | Works and is pixel-verified on the OpenGL module; LLGL's Vulkan module does not enable the device feature a line polygon mode needs and draws nothing at all, so the backend refuses the request there rather than presenting an empty frame. `SupportsCapability(WireFrame)` answers per module, which made this the first capability in the project whose answer is a runtime fact -- `GraphicsDeviceCapabilityTest.DoesNotSupportWireFrame` was adjusted accordingly. |
| LLGL-29 | Real window resize (`ResizeBuffers` and the presentation rect following it). | ⬜ | The code path exists (`UpdateSwapChainResolution` runs every `Present()`); nothing drives a real resize and reads pixels afterwards. |
| LLGL-23 | MSAA back buffer. | ⬜ | `multiSampleCount` is forwarded to the swap chain and `GetMultiSampleCount()` reports what LLGL actually applied, but no test asserts an antialiased edge, and `ReadBackbuffer` from a multisampled swap chain is untested. |

---

## Phase LLGL-5 — 3D pipeline (not started)

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| LLGL-24 | Vertex/index buffer draw path: translate `VertexDeclaration` into LLGL vertex attributes, per-layout pipelines, `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`. | ✅ | Done 2026-07-31 — `examples/llgl_3d_test.cpp`, `Llgl_3D` (+ `_OpenGL`), 12/12 on both modules through the public `BasicEffect`/`VertexBuffer`/`DrawPrimitives` API. The declaration translation feeds both the OpenGL vertex array and the Vulkan pipeline's input layout from one source, so the two cannot drift; attribute locations are assigned by usage, not declaration order. `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` are overridden specifically to honour `vertexStart`/`startIndex`/`baseVertex`, which the shared interface's default silently drops (a defect filed by name against other backends here). An effect asking for anything past vertex colours fails by name. **Four real findings, each caught by pixels rather than review:** a 3D draw refers to the caller's GPU buffers, so a `VertexBuffer` destroyed in the same frame left the recorded frame pointing at freed memory — buffer release is now deferred until the frame is submitted; XNA's `CreateOrthographicOffCenter` is right-handed, so visible geometry has negative z and the first version of the test clipped everything away; the winding the rasterizer sees is screen winding, because LLGL's Y-up clip space and its negated Vulkan viewport height cancel out; and depth testing against a never-cleared depth buffer produced a triangle drawn everywhere except near its apex. |
| LLGL-25 | `BasicEffect` family (`AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect`), depth/stencil state, cull and fill modes. | 🟨 | **Textures, `DiffuseColor`, `Alpha`, vertex-colour modulation, fog, `AlphaTestEffect` and lighting all done 2026-07-31** — `examples/llgl_basiceffect_test.cpp` + `examples/llgl_lighting_test.cpp`, `Llgl_BasicEffect`/`Llgl_Lighting` (+ `_OpenGL`), 13/13 and 8/8 on both modules. All 3D shaders share one 400-byte uniform block (documented in `shaders/effect3d_common.glsl.inc`) -- the unlit shaders declare only the first 128 bytes of it, the lit ones the full block, so growing it for lighting needed no change to any existing shader's own declaration. The vertex-shader variant is chosen from what the vertex LAYOUT carries, not from what the effect asked for -- a shader declaring an input the buffer does not supply reads undefined data on Vulkan -- and a textured/lit effect drawn from a layout missing the attribute it needs is refused by name. Lighting is per-pixel only, matching the documented, accepted deviation every established CNA backend except D3D9 already has (`GpuDrawParams::preferPerPixelLighting`'s own comment). **Bounded scope, by design: lighting only works when a texture is also bound** (`LLGL-31`) -- the overwhelmingly common real case, and a lit-but-untextured draw fails by name rather than silently dropping the light. Depth/stencil state, cull and fill modes were done in `LLGL-24`. **Still open: `DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect`, `PbrEffect`** -- each fails through `NotYetImplemented()` naming itself. |
| LLGL-26 | Render targets (`RenderTarget2D`, `RenderTargetCube`, MRT), cube and volume textures. | 🟨 | **`RenderTarget2D` done 2026-07-31** — `examples/llgl_rendertarget_test.cpp`, `Llgl_RenderTarget` (+ `_OpenGL`), 8/8 on both modules: construction, drawing into the target, unbinding back to the swap chain (its own independent clear), sampling the target back onto the screen with `SpriteBatch` (the `RenderTarget2D`-vs-plain-`Texture2D` cross-cast `ITextureBackend` accepts), and `RenderTarget2D::GetData()` reading the colour attachment directly. **Architecture**: LLGL's public Vulkan render-pass API has no way to re-enter a render pass with `Load` semantics (`BeginRenderPass()` always opens the "primary", `Undefined`/`DONT_CARE`-load pass — confirmed by reading `VKSwapChain.cpp`/`VKCommandBuffer.cpp`/`VKRenderTarget.cpp`), so a frame's queued commands are grouped by target IDENTITY into one contiguous render pass per distinct target (`GroupFrameCommandsByTargetEXT`/`FrameCommandBucket`), in first-appearance order — not replayed in original interleaved order. `RenderTargetUsage.PreserveContents` is not honoured across separate binds in this cut. The colour attachment always takes the swap chain's own colour format and a depth/stencil attachment matching the swap chain's own format is always allocated (regardless of the requested `DepthFormat`, which only changes what `HasRealDepthBuffer()` reports) — confirmed via a standalone read of LLGL's `VKPipelineState`/`VKGraphicsPSO` that a `LLGL::PipelineState` bakes in no `VkRenderPass` handle at bind time, only the attachment formats/sample count checked at creation, so every cached sprite/primitive pipeline built against the swap chain's render pass is reusable as-is against a render target's own render pass as long as the attachment signature matches — avoiding a second, render-target-keyed pipeline cache entirely. **One real bug found before it could reach a test**: `RenderTarget2D::GetBackend()` returns an `LlglRenderTargetBackend*`, not `LlglTextureBackend*` — `SpriteBatch::Draw`/`QueuePrimitives`' effect texture now resolve either concrete backend through a shared `ResolveSampledTexture()` helper instead of a hard `dynamic_cast<LlglTextureBackend*>` that would have silently failed to sample a render target. **Three more real bugs found by running the tests**: (1) sprites queued while a render target was bound read the SWAP CHAIN's shared pixel-to-clip-space projection (sized for the window, e.g. 800x480) instead of the target's own (e.g. 64x64) — the whole draw collapsed into a sliver of the target's clip space instead of filling it. Fixed by giving each `LlglRenderTargetBackend` its own fixed projection buffer, built once at construction (a target's resolution never changes, unlike the swap chain's), referenced per `FrameCommand` instead of the frame-global buffer. (2) destroying a `RenderTarget2D` before `Present()` — a perfectly ordinary pattern XNA allows — segfaulted `RecordAndSubmitFrame` on a freed `LLGL::RenderTarget*` still referenced by `frameCommands_`; fixed the same way `VertexBuffer`/`IndexBuffer` destruction mid-frame already was, by deferring the actual release (`ScheduleRenderTargetReleaseEXT`) until the frame that may reference it is submitted. (3) `RenderTarget2D::GetData()` called with no intervening back-buffer read returned stale/undefined pixels, because unlike back-buffer reads (which flush through `CaptureBackbuffer()`) nothing forced the target's queued draws to actually reach the GPU first; fixed with a new `FlushPendingFrameEXT()` that `GetData()` calls before reading. **3D draws into a render target are pixel-verified too** (Check H: `BasicEffect` + `VertexBuffer` + depth test, a nearer quad drawn first survives a farther one drawn second, read back with `GetData()`) — confirming the render target's own depth/stencil attachment and the reused 3D pipeline cache both genuinely work, not just `SpriteBatch`. **Still open**: `RenderTargetCube`, MRT (`SetRenderTargets` refuses more than one target by name), cube/volume textures, MSAA render targets, mip-mapped render targets. |
| LLGL-27 | Custom `ShaderEffect` via `IEffectBackend`. | ✅ | Done 2026-07-31 — `examples/llgl_shadereffect_test.cpp`, `Llgl_ShaderEffect` (+ `_OpenGL`), 6/6 on both modules: a hand-authored GLSL tint shader compiles, binds, and genuinely tints a drawn sprite by its own uniform (verified against a stock-shader control case that must NOT show the tint). **Scoped to `SpriteBatch` draws only** — the vertex layout is the fixed sprite `position/texCoord/color` stream, not an arbitrary `VertexDeclaration` — mirroring the native Vulkan backend's own `VulkanEffectBackend` precedent exactly, not a new limitation invented here. `vertSrc`/`fragSrc` are always real GLSL text (unlike the Vulkan backend's own convention of expecting pre-compiled SPIR-V bytes, documented in `docs/shader-effect-vs-fx-bytecode.md`): compiled directly when the loaded module accepts GLSL (OpenGL), or through a genuine runtime GLSL→SPIR-V compile via `libshaderc` when it does not (Vulkan) — the same problem `SDL_GPU`'s own effect backend already solved, ported over almost verbatim (`CompileGlslToSpirv`, the same hand-declared `extern "C"` shaderc ABI subset, the same `find_library`-then-glob CMake fallback for this environment's `libshaderc1`-only, no-`-dev`-package install). Named-uniform setters (`SetUniformMat4`/`Vec4`/`Vec3`/`Vec2`/`Float`/`Int`) do not do real name-based reflection -- LLGL exposes none for a raw GLSL/SPIR-V module -- they map onto the exact same fixed 32-float (128-byte) staging block `VulkanEffectBackend::pushConst_` already documents (`[0..1]=vpSize`, `[4..19]=uMatrix`, `[20..23]=uColor`, `[24]=uFloat0`), uploaded to a real constant buffer instead of a Vulkan push constant; `name` is accepted but not consulted, matching that same precedent rather than inventing new semantics. Every custom effect shares one `LLGL::PipelineLayout` (`customEffectLayout_`, built lazily on the first `ShaderEffect`); only the shader modules and each effect's own per-blend-state `LLGL::PipelineState` cache differ. A custom-effect sprite draw gets its own per-draw uniform buffer snapshot (`customEffectUniformBuffers_`/`customEffectUniformData_`, pooled exactly like the 3D path's `transformBuffers_`/`transformData_`) rather than one shared buffer overwritten in place, since `SetUniformX()` can legitimately change between two `Draw()` calls inside one `Begin()`/`End()` block. `LlglSpriteBatchBackend::SetCustomEffect()` (previously the shared interface's silent no-op default -- a real, if inert, gap this closes) is the actual wiring point `SpriteBatch::Begin()`/`End()` call, calling `effect->Apply()` to trigger `IEffectBackend::Bind()` exactly like the native Vulkan backend's own `VulkanSpriteBatchBackend::End()` does. Same deferred-release treatment as render targets/query heaps applies to a destroyed `ShaderEffect`'s shader modules and cached pipelines (`ScheduleEffectResourceReleaseEXT`) -- and this pass also fixed a real, separate gap found while adding it: `pendingRenderTargetReleases_`/`pendingTextureReleases_`/`pendingQueryHeapReleases_` (LLGL-26/28) were drained only by `ReleasePendingBuffers()` (called after a frame submit), never by `~LlglGraphicsBackend()` itself, so a render target/query destroyed mid-frame with the backend torn down before the next submit would leak; the destructor now calls `ReleasePendingBuffers()` up front. **One correctness point caught by reasoning, not yet covered by a dedicated test**: a custom effect's `vpSize` uniform must be the PHYSICAL swap-chain/render-target extent (matching what the stock shader's own per-frame projection divides by), not `GetActiveDrawRect()`'s letterboxed destination rect -- the two only coincide for an unscaled presentation or a render target (which has no letterboxing at all); `QueueSpriteEXT` already branches correctly, but no test combines a scaled `CnaPresentationMode` with a custom effect the way `Llgl_Presentation` alone or `Llgl_ShaderEffect` alone does. `CnjEffectTest.LoadsRealCnjFixture`/`CnjStockEffectTest.CustomGlslEffectStillWorks` remain in `CnaTests`' known-failure list under `-DCNA_GRAPHICS_BACKEND=LLGL` -- not a regression, and not this task's scope: those fixtures' GLSL was authored for EasyGL's GLES dialect and shaderc rejects it outright for SPIR-V (`ES shaders for SPIR-V require version 310 or higher`), a fixture-content mismatch, not a `LlglEffectBackend` defect (confirmed by running them directly: they now reach real compilation and fail with a specific shaderc diagnostic, instead of failing earlier for an unrelated reason). `GraphicsDeviceCapabilityTest.SupportsCustomEffects` now passes (18 failures at LLGL-26 -> 17 after LLGL-28 -> 16 after this task, in the full `CnaTests` sweep). |
| LLGL-28 | Occlusion queries (`LLGL::QueryHeap`). | ✅ | Done 2026-07-31 — `examples/llgl_occlusionquery_test.cpp`, `Llgl_OcclusionQuery` (+ `_OpenGL`), 6/6 on both modules (adapted from `examples/vulkan_occlusionquery_pixelcount_test.cpp`'s three scenarios: a fully visible quad reports a positive `PixelCount()`; a nearer opaque occluder reduces it to exactly 0 via the real depth test; two non-overlapping half-quads inside one `Begin()`/`End()` sum their contributions rather than only the last draw counting). `Begin()`/`End()` are queued into the deferred frame exactly like `Clear`/`Sprite`/`Primitives` (`FrameCommand::Kind::QueryBegin`/`QueryEnd`, replayed as `LLGL::CommandBuffer::BeginQuery`/`EndQuery` -- LLGL requires both inside an open render pass, which this backend only opens at submit time). **A fresh `LLGL::QueryHeap` is created for every `Begin()`, never reused**: reading LLGL 0.04b's own vendored Vulkan source found `VKCommandBuffer::ResetQueryPoolsInFlight` -- the call that would reset a query pool for reuse -- `#if 0`'d out, so a second `vkCmdBeginQuery` on the same query index without an external reset (which LLGL exposes no public API for) is undefined behaviour by the Vulkan spec's own query-reset rule; a fresh pool sidesteps the gap entirely rather than working around a reset LLGL does not offer. `IsComplete()`/`PixelCount()` answer synchronously -- the first call forces a full submit-and-wait (`FlushPendingFrameEXT()`, new, also used by `RenderTarget2D::GetData()`) rather than genuinely polling across frames like real hardware queries are meant to be used; a documented, deliberate trade of async performance for an answer that is always immediately correct. Query heap lifetime follows the same deferred-release pattern as buffers/render targets (`ScheduleQueryHeapReleaseEXT`). `GraphicsDeviceCapabilityTest.SupportsOcclusionQuery` now passes under `-DCNA_GRAPHICS_BACKEND=LLGL` (18 failures -> 17 in the full `CnaTests` sweep). |

---

## Closing notes

The 2D baseline is real and pixel-verified on both renderer modules — but on one platform, and
against a software rasterizer rather than a real GPU. That is the honest scope. Before this backend
is offered as a general alternative to `VULKAN` or `EASYGL` it still needs a run on real hardware,
and phase LLGL-5 for anything beyond 2D.

`LLGL-17` is worth remembering for more than its fix: every symptom pointed at resource binding,
and every experiment aimed there was wasted. What actually settled it was a shader that could not
possibly produce black, and then instrumenting the dependency to see whether it compiled the source
at all. When a component's own behaviour contradicts what it was configured with, check that it
received what you think you gave it before theorising about what it does with it.
