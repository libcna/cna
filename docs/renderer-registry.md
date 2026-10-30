# CNA renderer registry

CNA exposes exactly **10 public renderer identities** over 8 implementation families. EasyGL is an
internal implementation shared by three public GL profiles and does not add a public identity.
Internal renderer/API choices made by an abstraction such as SDLGPU or FNA3D likewise do not add
CNA identities: SDLGPU's Vulkan, Direct3D 12 and Metal backends are SDL's, not CNA's.

CNA intentionally maintains a curated renderer set. A new renderer is added only when it provides
meaningful platform coverage, compatibility value, architectural value, or a capability not
reasonably covered by the existing set; renderer count is not a goal in itself, and forty-one
identities have been retired (`docs/removed-renderers.md`).

The C++ enum is dense. The "C ABI value" column below is intentionally **not** dense: a retired
identity's value is permanently reserved and never reassigned, so the range has gaps at 1, 2, 5,
7, 8, 10, 14–21, 23–30, 32–42 and 44–51. The next new identity takes value 52.

## Canonical public identities

| C ABI value | Enum | Selector | Compile definition | Implementation / factory | Primary gate |
|---:|---|---|---|---|---|
| 3 | `OpenGLES3` | `OPENGLES3` | `CNA_RENDERER_EASYGL` + `CNA_GL_PROFILE_OPENGLES3` | EasyGL / `EasyGLRenderer` | non-Emscripten |
| 4 | `OpenGL33` | `OPENGL33` | `CNA_RENDERER_EASYGL` + `CNA_GL_PROFILE_OPENGL33` | shared EasyGL factory | non-Emscripten |
| 6 | `WebGL2` | `WEBGL2` | `CNA_RENDERER_EASYGL` + `CNA_GL_PROFILE_WEBGL2` | shared EasyGL factory | Emscripten |
| 9 | `WebGPU` | `WEBGPU` | `CNA_RENDERER_WEBGPU` | wgpu-native / `WebGPURenderer` | wgpu-native |
| 11 | `Headless` | `HEADLESS` | `CNA_RENDERER_HEADLESS` | Headless / `HeadlessRenderer` | none |
| 12 | `Software` | `SOFTWARE` | `CNA_RENDERER_SOFTWARE` | Software / `SoftwareRenderer` | none |
| 13 | `Stub` | `STUB` | `CNA_RENDERER_STUB` | Stub / `StubRenderer` | none |
| 22 | `DirectX9` | `DIRECTX9` | `CNA_RENDERER_DIRECTX9` | Direct3D 9 / `DirectX9Renderer` | Windows |
| 31 | `SdlGpu` | `SDLGPU` | `CNA_RENDERER_SDLGPU` | SDL GPU / `SdlGpuRenderer` | SDL3 with a GPU driver (Vulkan, Direct3D 12 or Metal) |
| 43 | `Fna3d` | `FNA3D` | `CNA_RENDERER_FNA3D` | FNA3D / `Fna3dRenderer` | FNA3D dependency |

The three GL profiles share one implementation target, macro, and factory, so 10 public identities
map to 8 concrete implementation factories. Their public contracts remain distinct because the
selected context, shader language/profile, and supported platform differ. `EASYGL` is not
an accepted selector, and neither is any retired name: `cmake/RendererIdentities.cmake` refuses a
retired selector by name, with its reserved value, rather than falling back to a default, and
`GraphicsRendererSelection` refuses one at run time with a message that says it was retired.
The family's own examples and tests keep the `easygl_`/`EasyGL_` prefix: it names the shared
implementation they exercise, whichever of the three profiles builds them (GLB-14).

## Capability classes

- **No renderer:** `STUB` is a no-op; `HEADLESS` is validation/trace-oriented and makes no pixel
  fidelity claim.
- **CPU bounded 3D:** `SOFTWARE`.
- **Programmable/modern, with renderer-specific limits:** `OPENGLES3`, `OPENGL33`, `WEBGL2`,
  `WEBGPU`, `DIRECTX9` and `SDLGPU`.
- **Abstraction layer:** `SDLGPU` is CNA's modern native path: CNA maps XNA 4.0 onto SDL's
  `SDL_GPU` API, and
  SDL picks Vulkan, Direct3D 12 or Metal underneath it. `FNA3D` selects SDLGPU, Direct3D 11 or
  OpenGL at runtime. Neither internal choice is another CNA identity.

These classes are descriptive, not blanket parity claims. `WEBGPU` is validated on Linux x86_64
(native) and in headless Chrome; its Windows, macOS and linux-aarch64 packages are unverified
(`WEBGPU-1`). `SDLGPU`
is validated on Linux (Vulkan) and, cross-compiled, under Wine with vkd3d-proton (Direct3D 12);
native Windows, macOS and iOS qualification is open (`handoff.md` §5.2). A capability query and
each renderer document remain authoritative for the narrower operation-level boundary.

## Registration invariants

`GraphicsRendererType`, its canonical name, CMake selector, compile definition/profile, selected
target, factory branch, and platform/dependency gate must agree. No public selector/name may be
duplicated, and no retired name or C ABI value may be reused. Every accepted selector either
reaches its factory or rejects at its documented gate; every retired one is refused by name at
configure time and at run time. `scripts/check_renderer_identities.py` holds the enum, the CMake
list, the runtime registry, the C ABI table, the retired-value table and the run-time retired-name
list to each other. The per-host default (`cmake/RendererIdentityDefault.cmake`) is `WEBGL2` under Emscripten,
`SDLGPU` on Windows, macOS (`Darwin`) and iOS, and `OPENGLES3` on every other system -- Linux,
Android, the BSDs and any host not named above.

CNA's own graphics code calls a CNA graphics renderer a **renderer**, never a backend:
`GraphicsRenderer*`, `CNA_GRAPHICS_RENDERER*`, `cna_graphics_renderer_*`,
`CNA::Internal::Renderers`. "Backend" stays for other subsystems (audio, input, `ENetBackend`,
device test backends) and for an upstream library's own vocabulary, such as SDLGPU's backends
above. Inside a family, a class that wraps a native API object keeps that API's prefix
(`D3D9EffectRenderer`, `D3D9Buffers`); only the identity class and the selection surface carry
the identity's name (`DirectX9Renderer`, `DIRECTX9`). The same check refuses the old
graphics-backend identifiers in every active CNA-owned file.
