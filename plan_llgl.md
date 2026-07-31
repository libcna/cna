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
> implemented, nor are render targets, cube and volume textures, custom `ShaderEffect`s and
> occlusion queries.
> Each either reports itself unsupported through `SupportsCapability()`/the shared interface's own
> "no backend" convention, or throws through `NotYetImplemented()`. None of them silently no-ops.

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
| LLGL-22 | Full `CnaTests` regression baseline under `-DCNA_GRAPHICS_BACKEND=LLGL`. | ✅ | **Unchanged by LLGL-25's full scope, texturing through lighting (5635 passed, 45 skipped, 18 failed): the remaining failures need cube textures, MRT, occlusion queries or custom effects, none of which that phase touched.** After LLGL-24: 5635 passed, 45 skipped, 18 failed — the 3D draw path closed four of the original failures and un-skipped 25 vertex-buffer tests. The remaining 18 are `TextureCubeTest` (9) plus four content/fuzz tests that need `CreateTextureCube`, `GraphicsDeviceCapabilityTest` (3: MRT, occlusion queries, custom effects) and the two `Cnj` effect tests. Original baseline for reference: **5606 passed, 70 skipped, 22 failed** of 5698. Every one of the 22 is a test that assumes a capability this backend does not implement yet, and each fails through a clean, documented refusal rather than a crash or a wrong answer: `TextureCubeTest` (9) plus `Texture3DTextureCubeContentTypeReaderTest`, `XnbBuiltInReaderRegistrationTest`, `XnbContainerFuzzTest` and `CnjCapabilityMatrixTest` (4 more) all need `CreateTextureCube`, which returns null; `GraphicsDeviceCapabilityTest` (4) asserts the 3D-capable answers its own header comment says it only builds against; `IndexedDrawDeferredTest`, `NonIndexedDrawRangeTest` and `VertexBufferEmptyDataTest.UploadedVerticesSupportNormalAndIndexedDrawing` (3) reach the 3D draw path; `CnjEffectTest`/`CnjStockEffectTest` (2) need custom effects. Two further failures WERE fixed rather than tolerated, following the same registration-gap precedent as `ASCII-5`/`DX3-86`: `GraphicsDeviceValidationTest`'s two `SetRenderTargets` expectations now express this backend's own (temporary) "no render targets at all" state. The list shrinks as phase LLGL-5 lands. |
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
| LLGL-26 | Render targets (`RenderTarget2D`, `RenderTargetCube`, MRT), cube and volume textures. | ⬜ | `SetRenderTargets()` currently accepts only the "restore the back buffer" request and throws for anything else. |
| LLGL-27 | Custom `ShaderEffect` via `IEffectBackend`. | ⬜ | Needs a runtime GLSL→SPIR-V compile for the Vulkan module, the same problem SDL_GPU solved with libshaderc. |
| LLGL-28 | Occlusion queries (`LLGL::QueryHeap`). | ⬜ | LLGL has the API; CNA returns `nullptr` from `CreateOcclusionQuery()` and `SupportsCapability(OcclusionQuery)` is false. |

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
