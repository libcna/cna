# WEBGL2 renderer

`WEBGL2` is CNA's supported-class browser WebGL 2 identity. It is Emscripten-only and is the
default renderer under that toolchain. The implementation is the shared EasyGL family in
`modules/renderers/easygl/`, which also serves the native `OPENGLES3` and `OPENGL33` identities;
the three are separate public renderer identities, not aliases. [`renderer-registry.md`](renderer-registry.md)
is the canonical identity and selection table, while
[`graphics-renderer-feature-matrix.md`](graphics-renderer-feature-matrix.md) is the cross-renderer
capability summary.

## Selection and validation

A single-renderer C ABI build can be configured with:

```bash
emcmake cmake -S . -B cmake-build-wasm-webgl2 -G Ninja \
  -DCNA_GRAPHICS_RENDERER=WEBGL2 \
  -DCNA_BUILD_C_API=ON \
  -DCNA_BUILD_TESTS=OFF \
  -DCNA_BUILD_EXAMPLES=OFF
cmake --build cmake-build-wasm-webgl2 --parallel 8 \
  --target cna_c_api_browser_probe
```

The final-target helper in `cmake/RendererSelection.cmake` pins both
`MIN_WEBGL_VERSION` and `MAX_WEBGL_VERSION` to 2. There is no WebGL 1 fallback: accepting a WebGL
1 context would contradict the profile's GLSL ES 3.00 contract. CNA's browser application targets
and the `cna_c_api_wasm` library use the same helper; non-WebGL renderer targets do not inherit
these flags. An external final executable must apply the same exact WebGL 2 requirement.

The built probe is a real browser gate, not a Node substitute:

```bash
PLAYWRIGHT_CHROMIUM=/usr/bin/google-chrome \
  node scripts/run_browser_tests.mjs \
  cmake-build-wasm-webgl2/modules/c-api browser_probe.html
```

When `CNA_BUILD_TESTS=ON` and Playwright is resolvable, CMake registers that route as
`CApi_WasmBrowserProbe`, together with the structural link contract and Node module smoke. Node
can validate the ES-module/Wasm artifact, but it has no canvas or browser WebGL implementation and
therefore is not graphics qualification.

## Profile-specific contract

| Area | `WEBGL2` behavior | Difference from the native EasyGL profiles |
|---|---|---|
| Context | Exact browser WebGL 2 context through SDL3/Emscripten | `OPENGLES3` requests native ES 3.0; `OPENGL33` requests desktop GL 3.3 core |
| Stock shaders | Uses the shared GLSL ES 3.00 corpus directly | Desktop GL rewrites it to GLSL 3.30 core |
| CNAEXT `ShaderEffect` | Reports GLSL ES and accepts GLSL ES vertex/fragment packages | Desktop GL reports and accepts desktop GLSL; see [`shader-effect-vs-fx-bytecode.md`](shader-effect-vs-fx-bytecode.md) |
| Compiled XNA effects | Optional `CNA_EASYGL_COMPILED_EFFECTS`; MojoShader uses `glsles3` | Desktop GL uses its `glsl120` adapter profile; the option is off by default |
| Base-vertex indexed draws | Rebases the index pointer/vertex layout in EasyGL | Desktop GL uses `glDrawElementsBaseVertex` |
| Texture channel swizzle | Unavailable; WebGL rejects `GL_TEXTURE_SWIZZLE_*` | Both native profiles expose it |
| Point sprites | Shader `gl_PointSize` works without a desktop enable | Desktop GL enables `GL_PROGRAM_POINT_SIZE` |
| Wireframe | Uses `WEBGL_polygon_mode` only when the browser exposes it; otherwise general wireframe is refused | Desktop GL has core polygon mode; native ES also requires an extension |
| Occlusion count | Core WebGL queries provide a zero/non-zero visibility answer | Desktop GL reports a precise sample count |
| `RasterizerState.MultiSampleAntiAlias` | No equivalent native switch; the state is accepted without changing rasterization | Desktop GL toggles `GL_MULTISAMPLE`; this is separate from allocating and resolving MSAA targets |

WebGL 2 also has no geometry or tessellation shader stage. CNA's public XNA effect path does not
require either stage, so their absence is a profile boundary rather than a missing stock-effect
feature.

## Shared EasyGL surface and live capabilities

`WEBGL2` uses EasyGL's complete 2D and 3D routes: `SpriteBatch`/`SpriteFont`, the five classic
stock effects, `PbrEffect`/`SkinnedPbrEffect`, indexed and instanced drawing, multiple vertex
streams, `Texture2D`/`TextureCube`/`Texture3D`, render-target cubes, MRT, MSAA and occlusion
queries. This statement does not promise every storage format, sample count or extension on every
browser/GPU pair.

Format support is not limited to `SurfaceFormat::Color`. The renderer classifies the live WebGL
implementation and its extensions, including floating-point targets, normalized 16-bit textures,
compressed formats and anisotropic filtering. Before choosing a non-`Color` format, sample count,
anisotropy level or numeric limit, query `GraphicsDevice::GetRendererCapabilityProfileEXT()` and
the per-format support functions. The focused sources of truth are
[`surface-format-support.md`](surface-format-support.md),
[`rendertarget-support.md`](rendertarget-support.md),
[`texture3d-texturecube-support.md`](texture3d-texturecube-support.md),
[`rasterizerstate-support.md`](rasterizerstate-support.md),
[`sampler-state-support.md`](sampler-state-support.md) and
[`occlusionquery-support.md`](occlusionquery-support.md).

Browser canvas sizing, the application loop, optional pthread ABI, storage and WebGL context loss
belong to the Emscripten platform rather than this renderer identity. Their current contract is in
[`web-emscripten-graphics-limitations.md`](web-emscripten-graphics-limitations.md); the separate
browser `WEBGPU` route is in [`webgpu-renderer.md`](webgpu-renderer.md).

## Qualification and current boundaries

- The reproducible browser gate uses headless Chrome. The C ABI probe fetches and instantiates
  the module over HTTP, verifies the `WEBGL2` identity and a real `WebGL2RenderingContext`, drives
  the shared Game tick through animation frames, clears/presents, exercises a stock-effect MRT
  draw with readback, and checks clean lifecycle and teardown.
- Real sample runs additionally cover touch input, compiled XNB model loading, resource creation
  on a loading pthread and foreground WebGL context loss/restoration. These are integration
  evidence, not an exhaustive browser/GPU/device matrix.
- The Emscripten multi-renderer workflow proves that a `WEBGL2;WEBGPU` bundle builds, links and
  publishes the correct registry. That workflow does not itself run the bundle in a browser;
  browser runtime evidence comes from the dedicated harnesses.
- WebGL format, compression, anisotropy, polygon-mode and numeric-limit availability remains
  browser/GPU dependent and must follow the live profile rather than a hard-coded browser name.
- `CApi_WasmFullscreenProbe` checks the actual presented pixels and logical viewport across
  repeated DOM fullscreen transitions with portrait/landscape back buffers and Chrome DPR 1/2.
  Logical resize requests preserve the browser-owned fullscreen canvas size. The default
  standard-resolution canvas, other-browser and pthread qualifications are described in
  [the web canvas contract](web-emscripten-graphics-limitations.md#canvas-and-display-model).
- `Texture3D` joins the shared EasyGL recovery registry and restores every mip from its CPU
  shadow. The [volume recovery regressions](texture3d-texturecube-support.md#easygl-context-recovery)
  run on native OpenGLES3/OpenGL33; a volume-specific asynchronous browser recovery run is
  unqualified.
- The shared EasyGL One/Zero blend path keeps independent colour/alpha equations (CNA-FIX-D24).
  Its ordinary-RGBA witness against measured XNA runs on native OpenGLES3/OpenGL33, not in the
  browser harness.
