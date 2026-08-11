# CNA renderer registry

Current as of the ascii-post-process-effect migration (2026-08): CNA exposes exactly **40 public
renderer identities** — one fewer than the prior 41-identity reconciliation of 2026-08-09, which
still included `ASCII`. `ASCII` was removed as a public renderer identity and its reusable
quantization/glyph-atlas logic migrated to a renderer-neutral post-process effect,
`CNA::Graphics::AsciiPostProcessEffect` (`modules/graphics-ext/`) — see
`docs/ascii-post-process-effect.md`. EasyGL is an internal implementation shared by four public GL
profiles and does not add a public identity. Internal renderer/API choices made by bgfx, Skia,
Sokol, Diligent, LLGL, or another abstraction likewise do not add CNA identities.

## Canonical public identities

| # | Enum | Selector | Compile definition | Implementation / factory | Primary gate |
|---:|---|---|---|---|---|
| 1 | `SdlRenderer` | `SDL_RENDERER` | `CNA_RENDERER_SDL_RENDERER` | SDL Renderer / `SdlRenderer` | none |
| 2 | `OpenGLES3` | `OPENGLES3` | `CNA_RENDERER_EASYGL` + `CNA_GL_PROFILE_OPENGLES3` | EasyGL / `EasyGLRenderer` | non-Emscripten |
| 3 | `OpenGL33` | `OPENGL33` | `CNA_RENDERER_EASYGL` + `CNA_GL_PROFILE_OPENGL33` | shared EasyGL factory | non-Emscripten |
| 4 | `WebGL1` | `WEBGL1` | `CNA_RENDERER_EASYGL` + `CNA_GL_PROFILE_WEBGL1` | shared EasyGL factory | Emscripten |
| 5 | `WebGL2` | `WEBGL2` | `CNA_RENDERER_EASYGL` + `CNA_GL_PROFILE_WEBGL2` | shared EasyGL factory | Emscripten |
| 6 | `Bgfx` | `BGFX` | `CNA_RENDERER_BGFX` | bgfx / `BgfxRenderer` | dependency |
| 7 | `Vulkan` | `VULKAN` | `CNA_RENDERER_VULKAN` | Vulkan / `VulkanRenderer` | Vulkan SDK/runtime |
| 8 | `WebGPU` | `WEBGPU` | `CNA_RENDERER_WEBGPU` | wgpu-native / `WebGPURenderer` | wgpu-native |
| 9 | `Magnum` | `MAGNUM` | `CNA_RENDERER_MAGNUM` | Magnum / `MagnumRenderer` | non-Emscripten + dependency |
| 10 | `Headless` | `HEADLESS` | `CNA_RENDERER_HEADLESS` | Headless / `HeadlessRenderer` | none |
| 11 | `Software` | `SOFTWARE` | `CNA_RENDERER_SOFTWARE` | Software / `SoftwareRenderer` | none |
| 12 | `Stub` | `STUB` | `CNA_RENDERER_STUB` | Stub / `StubRenderer` | none |
| 13 | `DirectX11` | `DIRECTX11` | `CNA_RENDERER_DIRECTX11` | Direct3D 11 / `DirectX11Renderer` | Windows |
| 14 | `DirectX12` | `DIRECTX12` | `CNA_RENDERER_DIRECTX12` | Direct3D 12 / `DirectX12Renderer` | Windows |
| 15 | `Direct2D` | `DIRECT2D` | `CNA_RENDERER_DIRECT2D` | Direct2D / `Direct2DRenderer` | Windows |
| 16 | `Canvas` | `CANVAS` | `CNA_RENDERER_CANVAS` | Canvas / `CanvasRenderer` | Emscripten |
| 17 | `HtmlDom` | `HTML_DOM` | `CNA_RENDERER_HTML_DOM` | HTML DOM / `HtmlDomRenderer` | Emscripten |
| 18 | `Skia` | `SKIA` | `CNA_RENDERER_SKIA` | Skia / `SkiaRenderer` | pinned Skia artifact |
| 19 | `FreeDirect` | `FREEDIRECT` | `CNA_RENDERER_FREEDIRECT` | FreeDirect / `FreeDirectRenderer` | free-direct dependency |
| 20 | `DirectX9` | `DIRECTX9` | `CNA_RENDERER_DIRECTX9` | Direct3D 9 / `DirectX9Renderer` | Windows |
| 21 | `DirectX1` | `DIRECTX1` | `CNA_RENDERER_DIRECTX1` | DIRECTX1 / `DirectX1Renderer` | Windows |
| 22 | `DirectX2` | `DIRECTX2` | `CNA_RENDERER_DIRECTX2` | DIRECTX2 / `DirectX2Renderer` | Windows |
| 23 | `DirectX3` | `DIRECTX3` | `CNA_RENDERER_DIRECTX3` | DIRECTX3 / `DirectX3Renderer` | Windows |
| 24 | `DirectX5` | `DIRECTX5` | `CNA_RENDERER_DIRECTX5` | DIRECTX5 / `DirectX5Renderer` | Windows |
| 25 | `DirectX6` | `DIRECTX6` | `CNA_RENDERER_DIRECTX6` | DIRECTX6 / `DirectX6Renderer` | Windows |
| 26 | `DirectX7` | `DIRECTX7` | `CNA_RENDERER_DIRECTX7` | DIRECTX7 / `DirectX7Renderer` | Windows |
| 27 | `DirectX8` | `DIRECTX8` | `CNA_RENDERER_DIRECTX8` | DIRECTX8 / `DirectX8Renderer` | Windows |
| 28 | `DirectX10` | `DIRECTX10` | `CNA_RENDERER_DIRECTX10` | Direct3D 10 / `DirectX10Renderer` | Windows |
| 29 | `SdlGpu` | `SDL_GPU` | `CNA_RENDERER_SDL_GPU` | SDL GPU / `SdlGpuRenderer` | SDL GPU runtime |
| 30 | `OpenGLES1` | `OPENGLES1` | `CNA_RENDERER_OPENGLES1` | GLES 1.1 / `OpenGLES1Renderer` | system GLESv1_CM |
| 31 | `OpenGL4` | `OPENGL4` | `CNA_RENDERER_OPENGL4` | OpenGL 4 / `OpenGL4Renderer` | system OpenGL |
| 32 | `OpenGL1` | `OPENGL1` | `CNA_RENDERER_OPENGL1` | OpenGL 1 / `OpenGL1Renderer` | Linux or Windows |
| 33 | `OpenGL2` | `OPENGL2` | `CNA_RENDERER_OPENGL2` | OpenGL 2 / `OpenGL2Renderer` | system OpenGL |
| 34 | `Wicked` | `WICKED` | `CNA_RENDERER_WICKED` | Wicked / `WickedRenderer` | non-Emscripten + dependency |
| 35 | `Sokol` | `SOKOL` | `CNA_RENDERER_SOKOL` | Sokol / `SokolRenderer` | configured native API |
| 36 | `Diligent` | `DILIGENT` | `CNA_RENDERER_DILIGENT` | Diligent / `DiligentRenderer` | DiligentCore |
| 37 | `Glide` | `GLIDE` | `CNA_RENDERER_GLIDE` | Glide / `GlideRenderer` | 32-bit Windows |
| 38 | `Gdi` | `GDI` | `CNA_RENDERER_GDI` | GDI / `GdiRenderer` | Windows |
| 39 | `Llgl` | `LLGL` | `CNA_RENDERER_LLGL` | LLGL / `LlglRenderer` | LLGL dependency |
| 40 | `Metal` | `METAL` | `CNA_RENDERER_METAL` | Metal / `MetalRenderer` | macOS/Darwin |

The four GL profiles share one implementation target, macro, and factory, so 40 public identities
map to 37 concrete implementation factories. Their public contracts remain distinct because the
selected context, shader language/profile, and supported platform differ. `FREEDIRECT` is the
renamed free-direct-backed identity; current `DIRECTX3` is the genuine DirectX 3 implementation.
`EASYGL` and the temporary `DX30` are not accepted selectors or compatibility aliases.

## Capability classes

- **No renderer:** `STUB` is a no-op; `HEADLESS` is validation/trace-oriented and makes no pixel
  fidelity claim.
- **2D-oriented:** `SDL_RENDERER`, `CANVAS`, `HTML_DOM`, `SKIA`, `FREEDIRECT`, `DIRECTX1`,
  `DIRECT2D`, and `GDI`.
- **CPU bounded 3D:** `SOFTWARE`.
- **Legacy or fixed-function bounded 3D:** `OPENGLES1`, `OPENGL1`, `DIRECTX2`, `DIRECTX3`, `DIRECTX5`, `DIRECTX6`,
  `DIRECTX7`, `DIRECTX8`, and `GLIDE`.
- **Programmable/modern, with renderer-specific limits:** `OPENGLES3`, `OPENGL33`, `WEBGL1`,
  `WEBGL2`, `BGFX`, `VULKAN`, `WEBGPU`, `MAGNUM`, `DIRECTX9`, `DIRECTX10`, `DIRECTX11`, `DIRECTX12`, `SDL_GPU`,
  `OPENGL4`, `OPENGL2`, `WICKED`, `SOKOL`, `DILIGENT`, `LLGL`, and `METAL`.

These classes are descriptive, not blanket parity claims. `WEBGPU` remains experimental. The
accepted Sokol route is GLCORE; the accepted LLGL runtime is OpenGL on Linux/X11/x86_64; Diligent's
internal native API is not another CNA identity; Metal's adapted native macOS validation remains an
external gate. A capability query and each renderer document remain authoritative for the narrower
operation-level boundary.

## Registration invariants

`GraphicsRendererType`, its canonical name, CMake selector, compile definition/profile, selected
target, factory branch, and platform/dependency gate must agree. No public selector/name may be
duplicated. Every accepted selector either reaches its factory or rejects at its documented gate.
The default is `WEBGL2` under Emscripten, `OPENGLES3` on Linux, and `SDL_RENDERER` elsewhere.
