# CNA renderer registry

Current as of the final 21-lane reconciliation on 2026-08-09. CNA exposes exactly **41 public
renderer identities**. EasyGL is an internal implementation shared by four public GL profiles and
does not add a public identity. Internal renderer/API choices made by bgfx, Skia, Sokol, Diligent,
LLGL, or another abstraction likewise do not add CNA identities.

## Canonical public identities

| # | Enum | Selector | Compile definition | Implementation / factory | Primary gate |
|---:|---|---|---|---|---|
| 1 | `SdlRenderer` | `SDL_RENDERER` | `CNA_RENDERER_SDL_RENDERER` | SDL Renderer / `SdlRenderer` | none |
| 2 | `OpenGLES` | `OPENGLES` | `CNA_RENDERER_EASYGL` + `CNA_GL_PROFILE_OPENGLES` | EasyGL / `EasyGLRenderer` | non-Emscripten |
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
| 13 | `D3D11` | `D3D11` | `CNA_RENDERER_D3D11` | D3D11 / `D3D11Renderer` | Windows |
| 14 | `D3D12` | `D3D12` | `CNA_RENDERER_D3D12` | D3D12 / `D3D12Renderer` | Windows |
| 15 | `Direct2D` | `DIRECT2D` | `CNA_RENDERER_DIRECT2D` | Direct2D / `Direct2DRenderer` | Windows |
| 16 | `Canvas` | `CANVAS` | `CNA_RENDERER_CANVAS` | Canvas / `CanvasRenderer` | Emscripten |
| 17 | `HtmlDom` | `HTML_DOM` | `CNA_RENDERER_HTML_DOM` | HTML DOM / `HtmlDomRenderer` | Emscripten |
| 18 | `Skia` | `SKIA` | `CNA_RENDERER_SKIA` | Skia / `SkiaRenderer` | pinned Skia artifact |
| 19 | `Ascii` | `ASCII` | `CNA_RENDERER_ASCII` | ASCII / `AsciiRenderer` | none |
| 20 | `FreeDirect` | `FREEDIRECT` | `CNA_RENDERER_FREEDIRECT` | FreeDirect / `FreeDirectRenderer` | free-direct dependency |
| 21 | `D3D9` | `D3D9` | `CNA_RENDERER_D3D9` | D3D9 / `D3D9Renderer` | Windows |
| 22 | `Dx1` | `DX1` | `CNA_RENDERER_DX1` | DX1 / `Dx1Renderer` | Windows |
| 23 | `Dx2` | `DX2` | `CNA_RENDERER_DX2` | DX2 / `Dx2Renderer` | Windows |
| 24 | `Dx3` | `DX3` | `CNA_RENDERER_DX3` | DX3 / `Dx3Renderer` | Windows |
| 25 | `Dx5` | `DX5` | `CNA_RENDERER_DX5` | DX5 / `Dx5Renderer` | Windows |
| 26 | `Dx6` | `DX6` | `CNA_RENDERER_DX6` | DX6 / `Dx6Renderer` | Windows |
| 27 | `Dx7` | `DX7` | `CNA_RENDERER_DX7` | DX7 / `Dx7Renderer` | Windows |
| 28 | `Dx8` | `DX8` | `CNA_RENDERER_DX8` | DX8 / `Dx8Renderer` | Windows |
| 29 | `D3D10` | `D3D10` | `CNA_RENDERER_D3D10` | D3D10 / `D3D10Renderer` | Windows |
| 30 | `SdlGpu` | `SDL_GPU` | `CNA_RENDERER_SDL_GPU` | SDL GPU / `SdlGpuRenderer` | SDL GPU runtime |
| 31 | `OpenGLES1` | `OPENGLES1` | `CNA_RENDERER_OPENGLES1` | GLES 1.1 / `OpenGLES1Renderer` | system GLESv1_CM |
| 32 | `OpenGL4` | `OPENGL4` | `CNA_RENDERER_OPENGL4` | OpenGL 4 / `OpenGL4Renderer` | system OpenGL |
| 33 | `OpenGL1` | `OPENGL1` | `CNA_RENDERER_OPENGL1` | OpenGL 1 / `OpenGL1Renderer` | Linux or Windows |
| 34 | `OpenGL2` | `OPENGL2` | `CNA_RENDERER_OPENGL2` | OpenGL 2 / `OpenGL2Renderer` | system OpenGL |
| 35 | `Wicked` | `WICKED` | `CNA_RENDERER_WICKED` | Wicked / `WickedRenderer` | non-Emscripten + dependency |
| 36 | `Sokol` | `SOKOL` | `CNA_RENDERER_SOKOL` | Sokol / `SokolRenderer` | configured native API |
| 37 | `Diligent` | `DILIGENT` | `CNA_RENDERER_DILIGENT` | Diligent / `DiligentRenderer` | DiligentCore |
| 38 | `Glide` | `GLIDE` | `CNA_RENDERER_GLIDE` | Glide / `GlideRenderer` | 32-bit Windows |
| 39 | `Gdi` | `GDI` | `CNA_RENDERER_GDI` | GDI / `GdiRenderer` | Windows |
| 40 | `Llgl` | `LLGL` | `CNA_RENDERER_LLGL` | LLGL / `LlglRenderer` | LLGL dependency |
| 41 | `Metal` | `METAL` | `CNA_RENDERER_METAL` | Metal / `MetalRenderer` | macOS/Darwin |

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
- **Programmable/modern, with renderer-specific limits:** `OPENGLES`, `OPENGL33`, `WEBGL1`,
  `WEBGL2`, `BGFX`, `VULKAN`, `WEBGPU`, `MAGNUM`, `D3D9`, `D3D10`, `D3D11`, `D3D12`, `SDL_GPU`,
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
The default is `WEBGL2` under Emscripten, `OPENGLES` on Linux, and `SDL_RENDERER` elsewhere.
