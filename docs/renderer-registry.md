# CNA renderer registry

Current as of the final 21-lane reconciliation on 2026-08-09. CNA exposes exactly **41 public
renderer identities**. EasyGL is an internal implementation shared by four public GL profiles and
does not add a public identity. Internal renderer/API choices made by bgfx, Skia, Sokol, Diligent,
LLGL, or another abstraction likewise do not add CNA identities.

## Canonical public identities

| # | Enum | Selector | Compile definition | Implementation / factory | Primary gate |
|---:|---|---|---|---|---|
| 1 | `SdlRenderer` | `SDL_RENDERER` | `CNA_BACKEND_SDL_RENDERER` | SDL Renderer / `SdlGraphicsBackend` | none |
| 2 | `OpenGLES` | `OPENGLES` | `CNA_BACKEND_EASYGL` + `CNA_GL_PROFILE_OPENGLES` | EasyGL / `EasyGLGraphicsBackend` | non-Emscripten |
| 3 | `OpenGL33` | `OPENGL33` | `CNA_BACKEND_EASYGL` + `CNA_GL_PROFILE_OPENGL33` | shared EasyGL factory | non-Emscripten |
| 4 | `WebGL1` | `WEBGL1` | `CNA_BACKEND_EASYGL` + `CNA_GL_PROFILE_WEBGL1` | shared EasyGL factory | Emscripten |
| 5 | `WebGL2` | `WEBGL2` | `CNA_BACKEND_EASYGL` + `CNA_GL_PROFILE_WEBGL2` | shared EasyGL factory | Emscripten |
| 6 | `Bgfx` | `BGFX` | `CNA_BACKEND_BGFX` | bgfx / `BgfxGraphicsBackend` | dependency |
| 7 | `Vulkan` | `VULKAN` | `CNA_BACKEND_VULKAN` | Vulkan / `VulkanGraphicsBackend` | Vulkan SDK/runtime |
| 8 | `WebGPU` | `WEBGPU` | `CNA_BACKEND_WEBGPU` | wgpu-native / `WebGPUGraphicsBackend` | wgpu-native |
| 9 | `Magnum` | `MAGNUM` | `CNA_BACKEND_MAGNUM` | Magnum / `MagnumGraphicsBackend` | non-Emscripten + dependency |
| 10 | `Headless` | `HEADLESS` | `CNA_BACKEND_HEADLESS` | Headless / `HeadlessGraphicsBackend` | none |
| 11 | `Software` | `SOFTWARE` | `CNA_BACKEND_SOFTWARE` | Software / `SoftwareGraphicsBackend` | none |
| 12 | `Stub` | `STUB` | `CNA_BACKEND_STUB` | Stub / `StubGraphicsBackend` | none |
| 13 | `D3D11` | `D3D11` | `CNA_BACKEND_D3D11` | D3D11 / `D3D11GraphicsBackend` | Windows |
| 14 | `D3D12` | `D3D12` | `CNA_BACKEND_D3D12` | D3D12 / `D3D12GraphicsBackend` | Windows |
| 15 | `Direct2D` | `DIRECT2D` | `CNA_BACKEND_DIRECT2D` | Direct2D / `Direct2DGraphicsBackend` | Windows |
| 16 | `Canvas` | `CANVAS` | `CNA_BACKEND_CANVAS` | Canvas / `CanvasGraphicsBackend` | Emscripten |
| 17 | `HtmlDom` | `HTML_DOM` | `CNA_BACKEND_HTML_DOM` | HTML DOM / `HtmlDomGraphicsBackend` | Emscripten |
| 18 | `Skia` | `SKIA` | `CNA_BACKEND_SKIA` | Skia / `SkiaGraphicsBackend` | pinned Skia artifact |
| 19 | `Ascii` | `ASCII` | `CNA_BACKEND_ASCII` | ASCII / `AsciiGraphicsBackend` | none |
| 20 | `FreeDirect` | `FREEDIRECT` | `CNA_BACKEND_FREEDIRECT` | FreeDirect / `FreeDirectGraphicsBackend` | free-direct dependency |
| 21 | `D3D9` | `D3D9` | `CNA_BACKEND_D3D9` | D3D9 / `D3D9GraphicsBackend` | Windows |
| 22 | `Dx1` | `DX1` | `CNA_BACKEND_DX1` | DX1 / `Dx1GraphicsBackend` | Windows |
| 23 | `Dx2` | `DX2` | `CNA_BACKEND_DX2` | DX2 / `Dx2GraphicsBackend` | Windows |
| 24 | `Dx3` | `DX3` | `CNA_BACKEND_DX3` | DX3 / `Dx3GraphicsBackend` | Windows |
| 25 | `Dx5` | `DX5` | `CNA_BACKEND_DX5` | DX5 / `Dx5GraphicsBackend` | Windows |
| 26 | `Dx6` | `DX6` | `CNA_BACKEND_DX6` | DX6 / `Dx6GraphicsBackend` | Windows |
| 27 | `Dx7` | `DX7` | `CNA_BACKEND_DX7` | DX7 / `Dx7GraphicsBackend` | Windows |
| 28 | `Dx8` | `DX8` | `CNA_BACKEND_DX8` | DX8 / `Dx8GraphicsBackend` | Windows |
| 29 | `D3D10` | `D3D10` | `CNA_BACKEND_D3D10` | D3D10 / `D3D10GraphicsBackend` | Windows |
| 30 | `SdlGpu` | `SDL_GPU` | `CNA_BACKEND_SDL_GPU` | SDL GPU / `SdlGpuGraphicsBackend` | SDL GPU runtime |
| 31 | `OpenGLES1` | `OPENGLES1` | `CNA_BACKEND_OPENGLES1` | GLES 1.1 / `OpenGLES1GraphicsBackend` | system GLESv1_CM |
| 32 | `OpenGL4` | `OPENGL4` | `CNA_BACKEND_OPENGL4` | OpenGL 4 / `OpenGL4GraphicsBackend` | system OpenGL |
| 33 | `OpenGL1` | `OPENGL1` | `CNA_BACKEND_OPENGL1` | OpenGL 1 / `OpenGL1GraphicsBackend` | Linux or Windows |
| 34 | `OpenGL2` | `OPENGL2` | `CNA_BACKEND_OPENGL2` | OpenGL 2 / `OpenGL2GraphicsBackend` | system OpenGL |
| 35 | `Wicked` | `WICKED` | `CNA_BACKEND_WICKED` | Wicked / `WickedGraphicsBackend` | non-Emscripten + dependency |
| 36 | `Sokol` | `SOKOL` | `CNA_BACKEND_SOKOL` | Sokol / `SokolGraphicsBackend` | configured native API |
| 37 | `Diligent` | `DILIGENT` | `CNA_BACKEND_DILIGENT` | Diligent / `DiligentGraphicsBackend` | DiligentCore |
| 38 | `Glide` | `GLIDE` | `CNA_BACKEND_GLIDE` | Glide / `GlideGraphicsBackend` | 32-bit Windows |
| 39 | `Gdi` | `GDI` | `CNA_BACKEND_GDI` | GDI / `GdiGraphicsBackend` | Windows |
| 40 | `Llgl` | `LLGL` | `CNA_BACKEND_LLGL` | LLGL / `LlglGraphicsBackend` | LLGL dependency |
| 41 | `Metal` | `METAL` | `CNA_BACKEND_METAL` | Metal / `MetalGraphicsBackend` | macOS/Darwin |

The four GL profiles share one implementation target, macro, and factory, so 41 public identities
map to 38 concrete implementation factories. Their public contracts remain distinct because the
selected context, shader language/profile, and supported platform differ. `FREEDIRECT` is the
renamed free-direct-backed identity; current `DX3` is the genuine DirectX 3 implementation.
`EASYGL` and the temporary `DX30` are not accepted selectors or compatibility aliases.

## Capability classes

- **No renderer:** `STUB` is a no-op; `HEADLESS` is validation/trace-oriented and makes no pixel
  fidelity claim.
- **2D-oriented:** `SDL_RENDERER`, `CANVAS`, `HTML_DOM`, `SKIA`, `ASCII`, `FREEDIRECT`, `DX1`,
  `DIRECT2D`, and `GDI`.
- **CPU bounded 3D:** `SOFTWARE`.
- **Legacy or fixed-function bounded 3D:** `OPENGLES1`, `OPENGL1`, `DX2`, `DX3`, `DX5`, `DX6`,
  `DX7`, `DX8`, and `GLIDE`.
- **Programmable/modern, with backend-specific limits:** `OPENGLES`, `OPENGL33`, `WEBGL1`,
  `WEBGL2`, `BGFX`, `VULKAN`, `WEBGPU`, `MAGNUM`, `D3D9`, `D3D10`, `D3D11`, `D3D12`, `SDL_GPU`,
  `OPENGL4`, `OPENGL2`, `WICKED`, `SOKOL`, `DILIGENT`, `LLGL`, and `METAL`.

These classes are descriptive, not blanket parity claims. `WEBGPU` remains experimental. The
accepted Sokol route is GLCORE; the accepted LLGL runtime is OpenGL on Linux/X11/x86_64; Diligent's
internal native API is not another CNA identity; Metal's adapted native macOS validation remains an
external gate. A capability query and each backend document remain authoritative for the narrower
operation-level boundary.

## Registration invariants

`GraphicsBackendType`, its canonical name, CMake selector, compile definition/profile, selected
target, factory branch, and platform/dependency gate must agree. No public selector/name may be
duplicated. Every accepted selector either reaches its factory or rejects at its documented gate.
The default is `WEBGL2` under Emscripten, `OPENGLES` on Linux, and `SDL_RENDERER` elsewhere.
