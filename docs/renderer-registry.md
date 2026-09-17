# CNA renderer registry

CNA exposes exactly **25 public renderer identities** over 21 implementation families. EasyGL is an
internal implementation shared by five public GL profiles and does not add a public identity.
Internal renderer/API choices made by an abstraction such as FNA3D likewise do not add CNA
identities.

CNA intentionally maintains a curated renderer set. A new renderer is added only when it provides
meaningful platform coverage, compatibility value, architectural value, or a capability not
reasonably covered by the existing set; renderer count is not a goal in itself, and twenty-six
identities have been retired (`docs/removed-renderers.md`).

The C++ enum is dense. The "C ABI value" column below is intentionally **not** dense: a retired
identity's value is permanently reserved and never reassigned, so the range has gaps at 7, 10, 19,
20, 23–30, 32, 34–39, 41, 45 and 47–51. The next new identity takes value 52.

## Canonical public identities

| C ABI value | Enum | Selector | Compile definition | Implementation / factory | Primary gate |
|---:|---|---|---|---|---|
| 1 | `SdlRenderer` | `SDL_RENDERER` | `CNA_RENDERER_SDL_RENDERER` | SDL Renderer / `SdlRenderer` | none |
| 2 | `OpenGLES2` | `OPENGLES2` | `CNA_RENDERER_EASYGL` + `CNA_GL_PROFILE_OPENGLES2` | shared EasyGL factory | non-Emscripten |
| 3 | `OpenGLES3` | `OPENGLES3` | `CNA_RENDERER_EASYGL` + `CNA_GL_PROFILE_OPENGLES3` | EasyGL / `EasyGLRenderer` | non-Emscripten |
| 4 | `OpenGL33` | `OPENGL33` | `CNA_RENDERER_EASYGL` + `CNA_GL_PROFILE_OPENGL33` | shared EasyGL factory | non-Emscripten |
| 5 | `WebGL1` | `WEBGL1` | `CNA_RENDERER_EASYGL` + `CNA_GL_PROFILE_WEBGL1` | shared EasyGL factory | Emscripten |
| 6 | `WebGL2` | `WEBGL2` | `CNA_RENDERER_EASYGL` + `CNA_GL_PROFILE_WEBGL2` | shared EasyGL factory | Emscripten |
| 8 | `Vulkan` | `VULKAN` | `CNA_RENDERER_VULKAN` | Vulkan / `VulkanRenderer` | Vulkan SDK/runtime |
| 9 | `WebGPU` | `WEBGPU` | `CNA_RENDERER_WEBGPU` | wgpu-native / `WebGPURenderer` | wgpu-native |
| 11 | `Headless` | `HEADLESS` | `CNA_RENDERER_HEADLESS` | Headless / `HeadlessRenderer` | none |
| 12 | `Software` | `SOFTWARE` | `CNA_RENDERER_SOFTWARE` | Software / `SoftwareRenderer` | none |
| 13 | `Stub` | `STUB` | `CNA_RENDERER_STUB` | Stub / `StubRenderer` | none |
| 14 | `DirectX11` | `DIRECTX11` | `CNA_RENDERER_DIRECTX11` | Direct3D 11 / `DirectX11Renderer` | Windows |
| 15 | `DirectX12` | `DIRECTX12` | `CNA_RENDERER_DIRECTX12` | Direct3D 12 / `DirectX12Renderer` | Windows |
| 16 | `Direct2D` | `DIRECT2D` | `CNA_RENDERER_DIRECT2D` | Direct2D / `Direct2DRenderer` | Windows |
| 17 | `Canvas` | `CANVAS` | `CNA_RENDERER_CANVAS` | Canvas / `CanvasRenderer` | Emscripten |
| 18 | `HtmlDom` | `HTML_DOM` | `CNA_RENDERER_HTML_DOM` | HTML DOM / `HtmlDomRenderer` | Emscripten |
| 21 | `FreeDirect` | `FREEDIRECT` | `CNA_RENDERER_FREEDIRECT` | FreeDirect / `FreeDirectRenderer` | free-direct dependency |
| 22 | `DirectX9` | `DIRECTX9` | `CNA_RENDERER_DIRECTX9` | Direct3D 9 / `DirectX9Renderer` | Windows |
| 31 | `SdlGpu` | `SDL_GPU` | `CNA_RENDERER_SDL_GPU` | SDL GPU / `SdlGpuRenderer` | SDL GPU runtime |
| 33 | `OpenGL4` | `OPENGL4` | `CNA_RENDERER_OPENGL4` | OpenGL 4 / `OpenGL4Renderer` | system OpenGL |
| 40 | `Gdi` | `GDI` | `CNA_RENDERER_GDI` | GDI / `GdiRenderer` | Windows |
| 42 | `Metal` | `METAL` | `CNA_RENDERER_METAL` | Metal / `MetalRenderer` | macOS/Darwin |
| 43 | `Fna3d` | `FNA3D` | `CNA_RENDERER_FNA3D` | FNA3D / `Fna3dRenderer` | FNA3D dependency |
| 44 | `SvgDom` | `SVG_DOM` | `CNA_RENDERER_SVG_DOM` | SVG DOM / `SvgDomRenderer` | Emscripten |
| 46 | `PortableGL` | `PORTABLEGL` | `CNA_RENDERER_PORTABLEGL` | PortableGL / `PortableGLRenderer` | none (CPU-only, fetched header) |

The five GL profiles share one implementation target, macro, and factory, so 25 public identities
map to 21 concrete implementation factories. Their public contracts remain distinct because the
selected context, shader language/profile, and supported platform differ. `FREEDIRECT` is the
renamed free-direct-backed identity (it was called `DIRECTX3` before 2026-08-04). `EASYGL` is not
an accepted selector, and neither is any retired name: `cmake/RendererIdentities.cmake` refuses a
retired selector by name, with its reserved value, rather than falling back to a default.

## Capability classes

- **No renderer:** `STUB` is a no-op; `HEADLESS` is validation/trace-oriented and makes no pixel
  fidelity claim.
- **2D-oriented:** `SDL_RENDERER`, `CANVAS`, `HTML_DOM`, `SVG_DOM`, `FREEDIRECT`, `DIRECT2D`, and
  `GDI`.
- **CPU bounded 3D:** `SOFTWARE`, `PORTABLEGL`.
- **Programmable/modern, with renderer-specific limits:** `OPENGLES2` (deliberately the
  narrowest of the GL family -- shader-based but bounded by core OpenGL ES 2.0, see
  `docs/opengles2-renderer.md`), `OPENGLES3`, `OPENGL33`, `WEBGL1`, `WEBGL2`, `VULKAN`, `WEBGPU`,
  `DIRECTX9`, `DIRECTX11`, `DIRECTX12`, `SDL_GPU`, `OPENGL4`, and `METAL`.
- **Abstraction layer:** `FNA3D` selects SDL_GPU, Direct3D 11 or OpenGL at runtime; that internal
  choice is not another CNA identity.

These classes are descriptive, not blanket parity claims. `WEBGPU` remains experimental. Metal's
adapted native macOS validation remains an external gate. A capability query and each renderer
document remain authoritative for the narrower operation-level boundary.

## Registration invariants

`GraphicsRendererType`, its canonical name, CMake selector, compile definition/profile, selected
target, factory branch, and platform/dependency gate must agree. No public selector/name may be
duplicated, and no retired name or C ABI value may be reused. Every accepted selector either
reaches its factory or rejects at its documented gate; every retired one is refused by name at
configure time. `scripts/check_renderer_identities.py` holds the enum, the CMake list, the runtime
registry, the C ABI table and the retired-value table to each other. The default is `WEBGL2` under
Emscripten, `OPENGLES3` on Linux, and `SDL_RENDERER` elsewhere.
