# CNA renderer registry

Status: reconciled with the C++ identity/category/maturity APIs, C ABI, CMake selection and
generated runtime registry on **2026-11-01**.

CNA exposes exactly **10 public renderer identities** over **8 implementation families**. A public
identity is the stable name applications select and query. An implementation family is the code
that serves one or more identities; EasyGL serves three GL profiles, while every other family
currently serves one identity.

Internal API or driver choices do not create CNA identities. Vulkan, Direct3D 12 and Metal are
SDL's backends beneath `SDLGPU`; SDLGPU, Direct3D 11 and OpenGL are FNA3D's runtime drivers; native
WebGPU implementations sit beneath wgpu-native or the browser. CNA calls the public layer a
**renderer** and reserves *backend* or *driver* for those upstream choices.

CNA intentionally keeps a curated set. A new identity requires an explicit owner decision and a
meaningful platform, compatibility or architectural reason; renderer count is not a goal. The 41
retired identities and their permanent numeric reservations are recorded in
[`removed-renderers.md`](removed-renderers.md).

## Canonical public identities

| C ABI | C++ enum | Selector | Implementation family | Category | Maturity |
|---:|---|---|---|---|---|
| 3 | `OpenGLES3` | `OPENGLES3` | EasyGL | Native | Production |
| 4 | `OpenGL33` | `OPENGL33` | EasyGL | Native | Production |
| 6 | `WebGL2` | `WEBGL2` | EasyGL | Web | Supported |
| 9 | `WebGPU` | `WEBGPU` | WebGPU | TranslationLayer | Supported |
| 11 | `Headless` | `HEADLESS` | Headless | Diagnostic | Supported |
| 12 | `Software` | `SOFTWARE` | Software | Software | Experimental |
| 13 | `Stub` | `STUB` | Stub | Diagnostic | Supported |
| 22 | `DirectX9` | `DIRECTX9` | DirectX9 | Native | Production |
| 31 | `SdlGpu` | `SDLGPU` | SdlGpu | TranslationLayer | Supported |
| 43 | `Fna3d` | `FNA3D` | Fna3d | TranslationLayer | Experimental |

The selector is the exact spelling accepted by `CNA_GRAPHICS_RENDERER`,
`CNA_GRAPHICS_RENDERERS` and the case-insensitive runtime name parser. `EASYGL` is not a public
selector: it names the shared implementation. The former `SDL_GPU` spelling was renamed to
`SDLGPU` and is refused rather than kept as an alias.

## Identity numbers and retirement

`CNA::GraphicsRendererType` is a dense C++ enum. Its ordinals are not a stable serialized or ABI
contract: removing an enumerator may move later ordinals. Use the enum symbol and canonical name,
not its underlying integer.

The C ABI identity is deliberately different. `CNA_GraphicsRendererType` is a fixed-width number,
and a published value is never reassigned. `CNA_GRAPHICS_RENDERER_UNKNOWN` is 0 and is report-only;
routes that accept an identity reject it. The live range has permanent retired gaps at 1, 2, 5, 7,
8, 10, 14-21, 23-30, 32-42 and 44-51. The next new identity, if authorized, takes value **52**.

Consequences:

- `CNA_GRAPHICS_RENDERER_MAXIMUM` is 43 (`FNA3D`), not the number of live identities;
- walking 0 through the maximum must expect gaps and validate each value;
- a retired name is refused by every CMake selection route with a retirement diagnostic;
- runtime selection refuses a retired name, and C ABI identity-taking routes refuse its reserved
  numeric value with `CNA_RESULT_INVALID_ARGUMENT`;
- no retired name silently falls back to the host default or aliases a survivor.

## Implementation families and build gates

The generated registry maps the identities above to these eight families:

| Family target | Public identity or identities | Compile definition | Renderer-specific build boundary |
|---|---|---|---|
| `cna_renderer_easygl` | `OPENGLES3`, `OPENGL33`, `WEBGL2` | `CNA_RENDERER_EASYGL` plus one `CNA_GL_PROFILE_*` for the default | sibling `easy-gl`/`meta-gl`; native profiles reject Emscripten, `WEBGL2` requires it |
| `cna_renderer_webgpu` | `WEBGPU` | `CNA_RENDERER_WEBGPU` | pinned wgpu-native package on native targets; Emscripten `emdawnwebgpu` port in browsers |
| `cna_renderer_headless` | `HEADLESS` | `CNA_RENDERER_HEADLESS` | no graphics dependency or window |
| `cna_renderer_software` | `SOFTWARE` | `CNA_RENDERER_SOFTWARE` | CPU rasterizer; presentation is a separate platform service |
| `cna_renderer_stub` | `STUB` | `CNA_RENDERER_STUB` | no graphics dependency, retained pixels or window |
| `cna_renderer_directx9` | `DIRECTX9` | `CNA_RENDERER_DIRECTX9` | Windows target, native or cross-compiled |
| `cna_renderer_sdl_gpu` | `SDLGPU` | `CNA_RENDERER_SDLGPU` | SDL3 and a usable SDL GPU driver; `CNA_ENABLE_SDL=OFF` is refused |
| `cna_renderer_fna3d` | `FNA3D` | `CNA_RENDERER_FNA3D` | SDL3 plus CNA's pinned FNA3D/MojoShader forks |

The EasyGL family has three descriptor accessors, one per public GL identity, so `OPENGLES3` and
`OPENGL33` can coexist in a native multi-renderer binary while sharing one archive. `WEBGL2` is in
the Emscripten platform partition and cannot coexist with a Windows-only identity in one target.

Additional platform gates are intentional and fail during configuration:

- `DIRECTX9` targets Windows only;
- `OPENGLES3` and `OPENGL33` reject Emscripten, while `WEBGL2` requires it;
- the `TERMINAL` platform permits only `SOFTWARE`, `HEADLESS` and `STUB`;
- the normal iOS allow-list contains `SOFTWARE` and `SDLGPU`; other attempts require the explicit
  unsupported experimentation override.

Passing a build gate means the family can be compiled for that target. It does not prove that a
specific native driver, adapter or platform has been qualified. Current platform evidence belongs
in each renderer's document and current gaps belong in
[KNOWN_ISSUES.md](../KNOWN_ISSUES.md).

## Categories and maturity

Category and maturity are orthogonal public classifications:

- `GraphicsRendererCategory` describes implementation technology: `Native`,
  `TranslationLayer`, `Software`, `Web` or `Diagnostic`.
- `GraphicsRendererMaturity` describes recommendation confidence: `Production`, `Supported`,
  `Experimental`, `Historical` or `Deprecated`. No current renderer uses the last two values.

The C++ queries are `getGraphicsRendererCategory()` and `getGraphicsRendererMaturity()`; each
accepts any public identity, whether or not it is compiled into the current binary. Their
`getCurrent*` variants describe the build's compile-time default. In a multi-renderer process,
classify `GraphicsRendererSelection::GetActive()` when the question is about the renderer that
actually created the device.

The C ABI exposes the same model through `cna_graphics_renderer_get_category`,
`cna_graphics_renderer_get_maturity` and their current/name-query variants in `core_ext.h`.

Neither classification is a feature promise. A Production renderer can still have a
platform-specific qualification gap, and a Supported or Experimental renderer may implement a
particular feature completely. For operation-level decisions, use the live queries documented in
[`renderer-capability-profiles.md`](renderer-capability-profiles.md); use
[`graphics-renderer-feature-matrix.md`](graphics-renderer-feature-matrix.md) only as the maintained
cross-renderer overview.

## Selection and generated runtime registry

`CNA_GRAPHICS_RENDERER` names the build default. With no explicit value, the default is:

- `WEBGL2` under Emscripten;
- `SDLGPU` on Windows, macOS (`Darwin`) and iOS;
- `OPENGLES3` on Linux, Android, the BSDs and every other target.

The older per-identity `CNA_RENDERER_<IDENTITY>=ON` switches remain selection inputs, but exactly
one may be enabled. `CNA_GRAPHICS_RENDERERS` opts into a binary containing several identities; its
default must be a member of the set and is moved to the first position without changing the
caller's selected value.

`cmake/RendererRegistry.cmake` maps each identity to a family namespace and descriptor accessor,
then generates `CnaRendererRegistry.generated.cpp` for the configured set. An explicit table is
used instead of static self-registration so renderer objects cannot disappear when their static
archives are linked. A single-renderer build has one entry; a multi-renderer build has one entry
per compiled identity, with the build default first.

At runtime, availability means an identity is compiled in; it does not guarantee that device or
surface creation will succeed on the current machine. Selection precedence, the post-success
latch, opt-in fallback, active identity and fallback history are documented in
[`runtime-renderer-selection.md`](runtime-renderer-selection.md).

## Registration invariants and verification

For every public identity, the C++ enum/name, CMake selector, implementation map, descriptor,
factory, compile definition, category, maturity and C ABI value must agree. A new identity is not
complete until every layer is updated and its build/platform gate is explicit.

The principal checks are:

- `scripts/check_renderer_identities.py`: public set, eight implementation families, descriptor
  accessors/factories, C ABI values, retired names/values, current documented counts and renderer
  vocabulary;
- `scripts/check_runtime_renderer_discipline.py`: descriptor/virtual ownership of runtime choices;
- `scripts/check_renderer_combinations.py`: configure-time combination rules and their public
  documentation;
- `GraphicsRendererCategoryTest`, `GraphicsRendererMaturityTest` and
  `GraphicsRendererRegistryTest`: classifications and the compiled descriptor table;
- `CnaRendererDefaultSelection_*` and `CnaRendererRetired_*`: default membership plus every
  retired/renamed/unknown selection route.

The full public identity table is intentionally repeated here for readers, but it is mechanically
checked. The authoritative machine inputs are `CNA_RENDERER_PUBLIC_IDENTITIES` and
`CNA_RENDERER_RETIRED_IDENTITIES` in `cmake/RendererIdentities.cmake`, the dense enum in
`modules/core/include/CNA/GraphicsRendererType.hpp`, the implementation map in
`cmake/RendererRegistry.cmake` and the fixed C constants in `modules/c-api/include/CNA/C/graphics.h`.
