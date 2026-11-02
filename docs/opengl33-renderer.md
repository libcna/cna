# OPENGL33 renderer

Status: current profile contract, reconciled with the renderer registry, live capability queries,
EasyGL implementation and platform qualification on **2026-10-31**, with runtime multisample-state
selection verified on **2026-11-02**.

`OPENGL33` is CNA's production-class desktop OpenGL identity. It requests an OpenGL 3.3 core
context and is implemented by the shared EasyGL family in `modules/renderers/easygl/`. The same
family also serves `OPENGLES3` and `WEBGL2`; they are separate public renderer identities, not
aliases. [`renderer-registry.md`](renderer-registry.md) is the canonical identity and selection
table, while [`graphics-renderer-feature-matrix.md`](graphics-renderer-feature-matrix.md) is the
cross-renderer capability summary.

## Selection and validation

A single-renderer build selects the profile with:

```bash
cmake -S . -B cmake-build-opengl33 -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCNA_GRAPHICS_RENDERER=OPENGL33 \
  -DCNA_BUILD_TESTS=ON \
  -DCNA_BUILD_EXAMPLES=ON
cmake --build cmake-build-opengl33 --parallel 8
```

EasyGL consumes the sibling `easy-gl` and `meta-gl` checkouts described in
[`development-environment.md`](development-environment.md). On Linux, run every window/GPU test
through the private display runner, never on the owner's desktop:

```bash
tools/platform/run_gpu_tests_private.sh cmake-build-opengl33 \
  -R '^EasyGL_' --output-on-failure -j6
```

The registered suite keeps the `EasyGL_` prefix because it tests the shared implementation. There
is no separate `OPENGL33` CTest label: the build's default renderer identity determines whether the
native EasyGL suite uses the desktop or ES profile. On Linux, the multisample state and pixel
contracts also register explicit runtime legs for compiled nondefault native GL profiles. In a
multi-renderer build, `OPENGL33` can be
selected before device creation through `GraphicsRendererSelection` or the
`CNA_GRAPHICS_RENDERER=OPENGL33` environment variable; see
[`runtime-renderer-selection.md`](runtime-renderer-selection.md).

## Profile-specific contract

| Area | `OPENGL33` behavior | Difference from the other EasyGL profiles |
|---|---|---|
| Context | SDL platform service requests OpenGL 3.3 core | `OPENGLES3` requests ES 3.0; `WEBGL2` uses the browser context |
| Stock shaders | One GLSL ES 3.00 corpus is rewritten to `#version 330 core`; the ES precision declaration is removed | ES/Web use the stored source directly |
| CNAEXT `ShaderEffect` | Reports desktop GLSL and accepts desktop-GLSL vertex/fragment packages | ES/Web report and accept GLSL ES; see [`shader-effect-vs-fx-bytecode.md`](shader-effect-vs-fx-bytecode.md) |
| Compiled XNA effects | Optional `CNA_EASYGL_COMPILED_EFFECTS`; MojoShader uses its `glsl120` adapter profile | ES/Web use `glsles3`; the option is off by default |
| Base-vertex indexed draws | Uses desktop `glDrawElementsBaseVertex` | ES 3.0/WebGL use the EasyGL rebase fallback |
| Texture channel swizzle | Available in core GL 3.3 | WebGL forbids it; native ES 3 exposes it |
| Point sprites | Enables `GL_PROGRAM_POINT_SIZE`, so shader `gl_PointSize` drives rasterization | ES/Web honor shader point size without that desktop switch |
| Wireframe | Uses desktop `glPolygonMode` when the live context exposes it | ES/Web require a native polygon-mode extension; otherwise general wireframe is refused |
| Occlusion count | Uses `GL_SAMPLES_PASSED` and reports a precise count | ES/Web may expose only a zero/non-zero result |
| `RasterizerState.MultiSampleAntiAlias` | Toggles `GL_MULTISAMPLE` according to the device's runtime profile | ES/Web have no equivalent switch and receive no desktop-only state change |

The requested version is a minimum contract, not a promise that `GL_VERSION` will equal 3.3. Mesa
may supply a later core context and qualified Apple hardware supplies OpenGL 4.1; EasyGL continues
to use the `OPENGL33` feature boundary.

## Shared EasyGL surface

`OPENGL33` uses the complete EasyGL 2D and 3D routes: `SpriteBatch`/`SpriteFont`, the five classic
stock effects, `PbrEffect`/`SkinnedPbrEffect`, indexed and instanced drawing, multiple vertex
streams, `Texture2D`/`TextureCube`/`Texture3D`, render-target cubes, MRT, MSAA and occlusion
queries. This statement does not promise every format or every combination on every driver.
Before choosing a non-`Color` format, sample count, anisotropy level or numeric limit, query
`GraphicsDevice::GetRendererCapabilityProfileEXT()` and the per-format support functions.

The focused sources of truth are:

- [`surface-format-support.md`](surface-format-support.md),
  [`rendertarget-support.md`](rendertarget-support.md) and
  [`texture3d-texturecube-support.md`](texture3d-texturecube-support.md) for storage, transfer and
  target boundaries;
- [`vertex-format-support.md`](vertex-format-support.md) for declarations, formats and base
  vertex behavior;
- [`rasterizerstate-support.md`](rasterizerstate-support.md),
  [`sampler-state-support.md`](sampler-state-support.md) and
  [`occlusionquery-support.md`](occlusionquery-support.md) for live state and query semantics;
- [`fx-compiled-effects.md`](fx-compiled-effects.md) for the optional compiled-effect runtime.

One device owns one GL context, and that context can be current on only one thread at a time.
EasyGL's renderer-context lease lets resource construction, transfer and disposal run on another
thread between frames. Waiting inside `LoadContent`, `Update` or `Draw` for a worker that itself
needs the context can deadlock because the game thread still owns the lease.

The native profiles render the logical back buffer into an offscreen framebuffer and present by
blitting it to the window surface. Clear, readback, resizing, presentation modes and MSAA resolve
therefore use CNA's back-buffer image rather than relying on transient window-surface contents.

## Qualification and current boundaries

- Linux x86_64 is the routine qualification environment. Current tests run against a real Mesa
  core context through the private Weston/Xwayland runner.
- macOS arm64 was qualified on the Mac mini M4 campaign, including the full tree, samples and the
  optional compiled-effect path. Apple's depth storage and anisotropy behavior have the exact
  qualifications recorded in [`apple-platforms.md`](apple-platforms.md).
- No current real-Windows `OPENGL33` qualification is recorded. Configuration support is not a
  substitute for observed pixels on Windows drivers.
- `OPENGL33` is native-only. Use `WEBGL2` for an Emscripten/browser build.
- MSAA availability, render-target formats, native wireframe and numeric limits are properties of
  the live context. The adapter-level promise made before context creation is deliberately
  conservative.

Active evidence includes the stock-effect golden/contract tests, profile-aware
`ShaderEffect` tests, `EasyGL_ShipGame_Particle_Shader`, `EasyGL_MsaaFragmentContract`, the
`EasyGLMultiSampleState` unit test and the nondefault-profile `EasyGL_RuntimeMultiSampleState_*` /
`EasyGL_RuntimeMsaaFragmentContract_*` registrations,
rasterizer and wireframe contracts, render-target/MSAA/MRT/cube/volume tests, exact desktop
occlusion queries, presentation/reset tests and the optional compiled-effect suite. Historical
campaign counts and closed failures belong in Git history, not in this current contract.
