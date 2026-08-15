# CNA renderer registry

Current as of the TINYGL addition (2026-08-13): CNA exposes exactly **47 public renderer
identities**. `TINYGL` (C-Chads/tinygl) is CNA's fixed-function CPU OpenGL renderer -- see
`docs/tinygl-renderer.md` and `plan_tinygl.md`. Before it, as of the eleven-lane renderer
integration (2026-08-11), there were **46**. `ASCII` was removed as a public renderer identity and its reusable
quantization/glyph-atlas logic migrated to a renderer-neutral post-process effect,
`CNA::Graphics::AsciiPostProcessEffect` (`modules/graphics-ext/`) — see
`docs/ascii-post-process-effect.md`. `OPENGLES2`, `BLEND2D`, `FNA3D`, `SVG_DOM`, `OPENVG` and `PORTABLEGL` were added. EasyGL is an internal
implementation shared by five public GL profiles and does not add a public identity. Internal
renderer/API choices made by bgfx, Skia, Sokol, Diligent, LLGL, or another abstraction likewise do
not add CNA identities.

## Canonical public identities

| # | Enum | Selector | Compile definition | Implementation / factory | Primary gate |
|---:|---|---|---|---|---|
| 1 | `SdlRenderer` | `SDL_RENDERER` | `CNA_RENDERER_SDL_RENDERER` | SDL Renderer / `SdlRenderer` | none |
| 2 | `OpenGLES2` | `OPENGLES2` | `CNA_RENDERER_EASYGL` + `CNA_GL_PROFILE_OPENGLES2` | shared EasyGL factory | non-Emscripten |
| 3 | `OpenGLES3` | `OPENGLES3` | `CNA_RENDERER_EASYGL` + `CNA_GL_PROFILE_OPENGLES3` | EasyGL / `EasyGLRenderer` | non-Emscripten |
| 4 | `OpenGL33` | `OPENGL33` | `CNA_RENDERER_EASYGL` + `CNA_GL_PROFILE_OPENGL33` | shared EasyGL factory | non-Emscripten |
| 5 | `WebGL1` | `WEBGL1` | `CNA_RENDERER_EASYGL` + `CNA_GL_PROFILE_WEBGL1` | shared EasyGL factory | Emscripten |
| 6 | `WebGL2` | `WEBGL2` | `CNA_RENDERER_EASYGL` + `CNA_GL_PROFILE_WEBGL2` | shared EasyGL factory | Emscripten |
| 7 | `Bgfx` | `BGFX` | `CNA_RENDERER_BGFX` | bgfx / `BgfxRenderer` | dependency |
| 8 | `Vulkan` | `VULKAN` | `CNA_RENDERER_VULKAN` | Vulkan / `VulkanRenderer` | Vulkan SDK/runtime |
| 9 | `WebGPU` | `WEBGPU` | `CNA_RENDERER_WEBGPU` | wgpu-native / `WebGPURenderer` | wgpu-native |
| 10 | `Magnum` | `MAGNUM` | `CNA_RENDERER_MAGNUM` | Magnum / `MagnumRenderer` | non-Emscripten + dependency |
| 11 | `Headless` | `HEADLESS` | `CNA_RENDERER_HEADLESS` | Headless / `HeadlessRenderer` | none |
| 12 | `Software` | `SOFTWARE` | `CNA_RENDERER_SOFTWARE` | Software / `SoftwareRenderer` | none |
| 13 | `Stub` | `STUB` | `CNA_RENDERER_STUB` | Stub / `StubRenderer` | none |
| 14 | `DirectX11` | `DIRECTX11` | `CNA_RENDERER_DIRECTX11` | Direct3D 11 / `DirectX11Renderer` | Windows |
| 15 | `DirectX12` | `DIRECTX12` | `CNA_RENDERER_DIRECTX12` | Direct3D 12 / `DirectX12Renderer` | Windows |
| 16 | `Direct2D` | `DIRECT2D` | `CNA_RENDERER_DIRECT2D` | Direct2D / `Direct2DRenderer` | Windows |
| 17 | `Canvas` | `CANVAS` | `CNA_RENDERER_CANVAS` | Canvas / `CanvasRenderer` | Emscripten |
| 18 | `HtmlDom` | `HTML_DOM` | `CNA_RENDERER_HTML_DOM` | HTML DOM / `HtmlDomRenderer` | Emscripten |
| 19 | `Skia` | `SKIA` | `CNA_RENDERER_SKIA` | Skia / `SkiaRenderer` | pinned Skia artifact |
| 20 | `Blend2D` | `BLEND2D` | `CNA_RENDERER_BLEND2D` | Blend2D / `Blend2DRenderer` | pinned Blend2D+AsmJit FetchContent |
| 21 | `FreeDirect` | `FREEDIRECT` | `CNA_RENDERER_FREEDIRECT` | FreeDirect / `FreeDirectRenderer` | free-direct dependency |
| 22 | `DirectX9` | `DIRECTX9` | `CNA_RENDERER_DIRECTX9` | Direct3D 9 / `DirectX9Renderer` | Windows |
| 23 | `DirectX1` | `DIRECTX1` | `CNA_RENDERER_DIRECTX1` | DIRECTX1 / `DirectX1Renderer` | Windows |
| 24 | `DirectX2` | `DIRECTX2` | `CNA_RENDERER_DIRECTX2` | DIRECTX2 / `DirectX2Renderer` | Windows |
| 25 | `DirectX3` | `DIRECTX3` | `CNA_RENDERER_DIRECTX3` | DIRECTX3 / `DirectX3Renderer` | Windows |
| 26 | `DirectX5` | `DIRECTX5` | `CNA_RENDERER_DIRECTX5` | DIRECTX5 / `DirectX5Renderer` | Windows |
| 27 | `DirectX6` | `DIRECTX6` | `CNA_RENDERER_DIRECTX6` | DIRECTX6 / `DirectX6Renderer` | Windows |
| 28 | `DirectX7` | `DIRECTX7` | `CNA_RENDERER_DIRECTX7` | DIRECTX7 / `DirectX7Renderer` | Windows |
| 29 | `DirectX8` | `DIRECTX8` | `CNA_RENDERER_DIRECTX8` | DIRECTX8 / `DirectX8Renderer` | Windows |
| 30 | `DirectX10` | `DIRECTX10` | `CNA_RENDERER_DIRECTX10` | Direct3D 10 / `DirectX10Renderer` | Windows |
| 31 | `SdlGpu` | `SDL_GPU` | `CNA_RENDERER_SDL_GPU` | SDL GPU / `SdlGpuRenderer` | SDL GPU runtime |
| 32 | `OpenGLES1` | `OPENGLES1` | `CNA_RENDERER_OPENGLES1` | GLES 1.1 / `OpenGLES1Renderer` | system GLESv1_CM |
| 33 | `OpenGL4` | `OPENGL4` | `CNA_RENDERER_OPENGL4` | OpenGL 4 / `OpenGL4Renderer` | system OpenGL |
| 34 | `OpenGL1` | `OPENGL1` | `CNA_RENDERER_OPENGL1` | OpenGL 1 / `OpenGL1Renderer` | Linux or Windows |
| 35 | `OpenGL2` | `OPENGL2` | `CNA_RENDERER_OPENGL2` | OpenGL 2 / `OpenGL2Renderer` | system OpenGL |
| 36 | `Wicked` | `WICKED` | `CNA_RENDERER_WICKED` | Wicked / `WickedRenderer` | non-Emscripten + dependency |
| 37 | `Sokol` | `SOKOL` | `CNA_RENDERER_SOKOL` | Sokol / `SokolRenderer` | configured native API |
| 38 | `Diligent` | `DILIGENT` | `CNA_RENDERER_DILIGENT` | Diligent / `DiligentRenderer` | DiligentCore |
| 39 | `Glide` | `GLIDE` | `CNA_RENDERER_GLIDE` | Glide / `GlideRenderer` | 32-bit Windows |
| 40 | `Gdi` | `GDI` | `CNA_RENDERER_GDI` | GDI / `GdiRenderer` | Windows |
| 41 | `Llgl` | `LLGL` | `CNA_RENDERER_LLGL` | LLGL / `LlglRenderer` | LLGL dependency |
| 42 | `Metal` | `METAL` | `CNA_RENDERER_METAL` | Metal / `MetalRenderer` | macOS/Darwin |
| 43 | `Fna3d` | `FNA3D` | `CNA_RENDERER_FNA3D` | FNA3D / `Fna3dRenderer` | FNA3D dependency |
| 44 | `SvgDom` | `SVG_DOM` | `CNA_RENDERER_SVG_DOM` | SVG DOM / `SvgDomRenderer` | Emscripten |
| 45 | `OpenVg` | `OPENVG` | `CNA_RENDERER_OPENVG` | ShivaVG / `OpenVgRenderer` | desktop OpenGL (compat profile) |
| 46 | `PortableGL` | `PORTABLEGL` | `CNA_RENDERER_PORTABLEGL` | PortableGL / `PortableGLRenderer` | none (CPU-only, fetched header) |
| 47 | `TinyGL` | `TINYGL` | `CNA_RENDERER_TINYGL` | TinyGL / `TinyGLRenderer` | none (CPU-only, fetched+built source) |

The five GL profiles share one implementation target, macro, and factory, so 47 public identities
map to 43 concrete implementation factories. Their public contracts remain distinct because the
selected context, shader language/profile, and supported platform differ. `FREEDIRECT` is the
renamed free-direct-backed identity; current `DIRECTX3` is the genuine DirectX 3 implementation.
`EASYGL` and the temporary `DX30` are not accepted selectors or compatibility aliases.

## Capability classes

- **No renderer:** `STUB` is a no-op; `HEADLESS` is validation/trace-oriented and makes no pixel
  fidelity claim.
- **2D-oriented:** `SDL_RENDERER`, `CANVAS`, `HTML_DOM`, `SKIA`, `BLEND2D`, `FREEDIRECT`,
  `DIRECTX1`, `DIRECT2D`, and `GDI`.
- **CPU bounded 3D:** `SOFTWARE`, `PORTABLEGL`, `TINYGL`.
- **Legacy or fixed-function bounded 3D:** `OPENGLES1`, `OPENGL1`, `TINYGL` (also CPU, above), `DIRECTX2`, `DIRECTX3`, `DIRECTX5`, `DIRECTX6`,
  `DIRECTX7`, `DIRECTX8`, and `GLIDE`.
- **Programmable/modern, with renderer-specific limits:** `OPENGLES2` (deliberately the
  narrowest of the GL family -- shader-based but bounded by core OpenGL ES 2.0, see
  `docs/opengles2-renderer.md`), `OPENGLES3`, `OPENGL33`, `WEBGL1`,
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
