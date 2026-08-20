# OPENGL2 renderer — native desktop OpenGL 2.1 (compatibility profile)

`-DCNA_GRAPHICS_RENDERER=OPENGL2` · enum `CNA::GraphicsRendererType::OpenGL2` · target
`cna_renderer_opengl2` · sources `src/Graphics/Renderers/OpenGL2/`.

Plan and per-session history: `plans/plan_opengl2.md`.

## Identity

- Requests **OpenGL 2.1, compatibility profile** (`SDL_GL_CONTEXT_PROFILE_COMPATIBILITY`, 2.1);
  runs on whatever compatibility context the driver grants and uses only 2.1-era entry points.
- Shader-based, **GLSL 1.10 throughout** — every program is compiled at runtime from inline
  sources (1.20-only constructs such as `mat3(mat4)` truncation are deliberately avoided);
  attribute locations are bound by name via `glBindAttribLocation` (GLSL 1.10 has no
  `layout(location=N)`).
- **Deliberately independent of EasyGL/easy-gl.** EasyGL requests `SDL_GL_CONTEXT_PROFILE_ES`
  (OpenGL ES 3.0 / WebGL2) and cannot create a desktop 2.1 compatibility context; there is no
  shared code and no routing through the hidden EasyGL identity. Equally distinct from
  `OPENGL1` (fixed-function, no shaders), `OPENGL4` (core profile, GLSL 4.10+) and
  `OPENGLES1` (ES 1.1 Common profile).
- Dependencies: the platform's own GL library (`find_package(OpenGL REQUIRED)`) plus SDL3 for
  window/context management. The 66 post-1.1 entry points (FBOs, buffers, shaders, glTexImage3D,
  glDrawBuffers, queries, …) are resolved at runtime via `SDL_GL_GetProcAddress` — required on
  Windows, where opengl32.dll exports only GL 1.1 — with the instancing pair
  (`glDrawElementsInstancedARB`/`glVertexAttribDivisorARB`) loaded from the optional
  `GL_ARB_draw_instanced`/`GL_ARB_instanced_arrays` extensions. Zero new third-party dependency,
  nothing vendored, nothing downloaded, no generated files.

## Supported

Window/context lifecycle · `Clear` (all six variants) · `Present` · viewport · scissor · dynamic
`BlendState` (presets + custom, separate alpha, blend equations, `BlendFactor` via
`glBlendColor`) · slot-0 `ColorWriteChannels` (`glColorMask`; distinct per-slot masks with an
active MRT set are refused — `glColorMaski` is GL 3.0+) · `DepthStencilState` incl. two-sided
stencil (`glStencilFuncSeparate`, GL 2.0 core) and standalone `SetReferenceStencil` ·
`RasterizerState` culling + **real WireFrame** (`glPolygonMode`, shared-pixel-oracle verified) ·
depth bias · dynamic `SamplerState` (filter/address per slot; anisotropy when
`GL_EXT_texture_filter_anisotropic` is granted) · `Texture2D` incl. real per-level mip upload and
mip-aware filters · `Texture3D` (real `GL_TEXTURE_3D` storage incl. mip levels) · `TextureCube` ·
`RenderTarget2D` (FBO, depth formats, **MSAA** + blit resolve, mip regen, `GetData`) ·
`RenderTargetCube` (per-face FBO rendering, `GetData` at any level) · **real MRT** (up to 8
targets, `glDrawBuffers`, with real per-set depth/stencil and MSAA resolve) · backbuffer scaling
through the real presentation modes (`Letterbox`/`Overscan`/`Stretch`/`FixedHeightDynamicWidth`
via the physical-viewport-rect contract this lane introduced) · vertex/index buffers (**16- and
32-bit**, `SetDataOptions::Discard`/`NoOverwrite` orphan/sub-data upload paths) ·
non-indexed/indexed draws with `vertexStart`/`startIndex`/**`baseVertex`** (software base-vertex:
attribute pointers re-based `baseVertex*stride` bytes — GL 2.1 has no
`glDrawElementsBaseVertex`) · all five stock effects (`BasicEffect` incl. 3-light specular +
emissive + per-vertex color, `AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`,
`SkinnedEffect`) · `PbrEffect`/`SkinnedPbrEffect` (tangent-space normal mapping, secondary PBR
texture units with real sampler states) · fog on all six program families (FNA fog-vector form,
REMED-GFX-010, skinned programs dotting the post-skin position) · real **occlusion queries**
(`GL_SAMPLES_PASSED`, exact pixel counts, core 1.5) · custom GLSL 1.10 `ShaderEffect` for 3D
draws and `SpriteBatch` (`SetCustomEffect`) · **full custom `VertexDeclaration` support** — a
declaration that is not the built-in layout its stride implies is bound name-driven from its own
elements, reading exactly the declared bytes (fidelity by translation; a declaration without a
Color element pins `aColor` to constant white) · **hardware instancing** through the unified
vertex-stream transport when the driver grants the ARB extension pair (custom-`ShaderEffect`
draws; per-instance attributes bound by name, divisor = the stream's own `InstanceFrequency`,
per-stream `VertexOffset` honored) · `SpriteBatch` (rotation/origin/scale/source-rect/
layer-depth/effects, custom effects) · virtual resolution + window/logical transforms ·
runtime `SetSwapInterval` · `ReadBackbuffer` · context-loss recovery (registry-based re-create,
re-binds the render target/cube face active at loss) · disposal.

## Unsupported — rejects or reports truthfully, never silently

- **Multi-stream vertex input** — capability `false`, `GraphicsDevice` refusal, plus a
  renderer-level `System::NotSupportedException` backstop on all three `Draw*Ex` routes.
- **Instancing without the ARB extensions** — `SupportsCapability(Instancing)` answers from the
  resolved proc addresses; an instanced draw outside the custom-effect scope (or without the
  extensions) is the shared base class's deterministic refusal. `Instancing` is this renderer's
  own `GraphicsCapability` member: unlike GL 3.3+/D3D11-class renderers, instancing here is an
  optional driver extension, so a game can ask before relying on it.
- **Cube faces in a multi-target set** — descriptor route throws (single cube-face bindings route
  to the real cube-face setter).
- **Distinct per-MRT-slot `ColorWriteChannels`** — refused with an active MRT set
  (`glColorMaski` is GL 3.0+; EasyGL's same GLES3 refusal).
- **`BlendState.MultiSampleMask`** beyond all-ones — documented gap (`GL_SAMPLE_MASK` is
  GL 3.2+; EasyGL's and OPENGL1's same gap).
- **MSAA cube render targets** — `multiSampleCount` accepted and ignored on the cube factory
  (RenderTarget2D MSAA is real); CPU upload into an RT-cube face is the inherited interface
  refusal asserted by the shared contract test.
- **`HalfVector2`/`HalfVector4` vertex formats** on drivers without `ARB_half_float_vertex` —
  accepted for table completeness (GL_HALF_FLOAT is core 3.0); real 2.1-only hardware without
  the extension cannot render them.
- Windows/macOS validation — environment-blocked here (Linux sandbox); the Windows GL 1.1
  entry-point loading path exists and compiles but has not been executed on a real Windows GL
  driver.

## Capability answers

Exhaustive eleven-member switch, no default case: `ThreeD`/`DepthStencilBuffer`/`CustomEffects`/
`WireFrame` structural `true`; `MultipleRenderTargets`/`OcclusionQuery`/`Texture3D` from their
runtime-resolved core entry points; `MultiSampleAntiAliasing` (`GL_MAX_SAMPLES`),
`AnisotropicFiltering` (extension string) and `Instancing` (ARB proc pair) runtime-detected;
`MultiStreamVertexInput` `false`.
