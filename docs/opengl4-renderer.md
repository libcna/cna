# OPENGL4 renderer — real desktop OpenGL 4.x core profile

`-DCNA_GRAPHICS_RENDERER=OPENGL4` · enum `CNA::GraphicsRendererType::OpenGL4` · target
`cna_renderer_opengl4` · sources `src/Graphics/Renderers/OpenGL4/`.

Plan and per-task history: `plan_opengl4.md` (GL4-1 … GL4-33).

## Identity

- Requests **OpenGL 4.1 core profile minimum** (`SDL_GL_CONTEXT_PROFILE_CORE`, 4.1 being the
  highest core version macOS also provides); runs on whatever newer core context the driver
  grants — validated on Mesa llvmpipe's **4.5 (Core Profile)**, GLSL 4.50.
- **Deliberately independent of EasyGL/easy-gl.** EasyGL requests
  `SDL_GL_CONTEXT_PROFILE_ES` (OpenGL ES 3.0 / WebGL2) and cannot create a desktop core-profile
  context at all; there is no shared code and no routing through the hidden EasyGL identity.
- Dependencies: the platform's own GL library (`find_package(OpenGL REQUIRED)` —
  libGL/opengl32/OpenGL.framework) plus SDL3 for window/context management. GL entry points past
  1.1 are resolved by the renderer's own small hand-rolled loader (`GL4Loader.hpp/.cpp`) — zero
  new third-party dependency, nothing vendored, nothing downloaded.
- Shaders: **GLSL 410 core**, compiled at runtime from inline sources; compilation failure throws
  `std::runtime_error` carrying the driver's own info log. No generated files.

## Supported

Window/context lifecycle · `Clear` (all six clear variants) · `Present` · viewport · scissor ·
dynamic `BlendState` (presets + custom + `SetBlendFactor`, separate alpha, blend equations) ·
per-MRT-slot `ColorWriteChannels` via `glColorMaski` · `DepthStencilState` incl. real two-sided
stencil · `RasterizerState` culling + **real WireFrame** (`glPolygonMode`, shared-pixel-oracle
verified) · dynamic `SamplerState` (filter/address; anisotropy when the driver grants the
extension) · `Texture2D` incl. real per-level mip upload and mip-aware filters · `Texture3D` and
`TextureCube` with real readback (per-slice / per-face+level FBO `glReadPixels`) ·
`RenderTarget2D` (FBO, depth formats, MSAA resolve, mip regen, `GetData`) · `RenderTargetCube`
(per-face FBO, `SetData`/`GetData`) · **real MRT** (up to 8 targets, `glDrawBuffers`) ·
backbuffer **MSAA** (manual multisample FBO + blit resolve) · vertex/index buffers (**16- and
32-bit**) · non-indexed/indexed draws with `vertexStart`/`startIndex`/**`baseVertex`**
(`glDrawElementsBaseVertex`) · all five stock effects (`BasicEffect` textured/lit/vertex-lit via
`PreferPerPixelLighting`, `AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`,
`SkinnedEffect`) · `PbrEffect`/`SkinnedPbrEffect` (glTF 2.0 metallic-roughness BRDF) · fog on all
stride-dispatched programs (FNA fog-vector form, REMED-GFX-010) · real **occlusion queries**
(`GL_SAMPLES_PASSED`, exact pixel counts) · custom GLSL `ShaderEffect` for 3D draws and
`SpriteBatch::SetCustomEffect` · **hardware instancing** (`glDrawElementsInstanced` +
`glVertexAttribDivisor`, unified vertex-stream transport, custom-effect driven) ·
`TransformWindowToLogical`/`TransformLogicalToWindow` · resize/reset · disposal.

The unrecognised-stride fallback renders the applied effect's `DiffuseColor`, gated by its
`VertexColorEnabled` (`plan_gltf.md GLTF-475`). It used to reach that fallback through the
params-free colour route, which discarded `GpuDrawParams` and left the program painting attribute
location 1 -- the `NORMAL` on every record from stride 32 upward -- as the surface colour. The
stride-16 `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` entry points are unchanged: they
have no effect to read and state white with the attribute enabled, which is the old formula.

## Unsupported — every one rejects or reports truthfully, none silently

- **Multi-stream vertex input** (`GraphicsCapability::MultiStreamVertexInput` = `false`):
  `ApplyLayout` binds one `GL_ARRAY_BUFFER` and reads every attribute from it at stride offsets.
  `GraphicsDevice` refuses the binding shape up front, and every `Draw*Ex` route additionally
  throws `System::NotSupportedException` if a multi-stream draw reaches the renderer directly.
- **Declarations the stride dispatch cannot represent faithfully** are refused at draw time by
  `RequireFaithfulDeclarationEXT` (REMED-GFX-DECL-GUARD; asymmetric — only what the caller
  declared is checked). Custom-`ShaderEffect` draws bind attributes generically from the
  declaration itself and are exempt by construction.
- **Anisotropic filtering** is answered from the queried driver ceiling
  (`GL_MAX_TEXTURE_MAX_ANISOTROPY`); the extension is core only in GL 4.6, so a driver without it
  gets a truthful `false` and the sampler parameter is never uploaded.
- `BlendState.MultiSampleMask` is accepted but only the all-ones default is implemented — the
  same documented gap EasyGL records.
- `ApplyMultiSampleCount` after construction is not overridden (inherited no-op) — MSAA is fixed
  at renderer construction, EasyGL's own documented limitation.
- Cube faces inside a **multi-target** `SetRenderTargets` set throw `std::runtime_error`
  (single-face binds route to the real cube-face setter). MRT sets carry no depth attachment
  (EasyGL's same documented gap).
- Context-loss recovery was explicitly deferred by the 2026-07-22 project-owner scoping decision
  (`plan_opengl4.md`); `SetContextRecoveryEnabled` keeps the base no-op.
- Windows/macOS validation is environment-blocked in this dev loop and remains open in
  `plan_opengl4.md`'s remaining work.

## Capability report

`SupportsCapability` answers **all ten** current `GraphicsCapability` members explicitly — nine
`true` (each backed by a named real implementation), `MultiStreamVertexInput` `false`,
`AnisotropicFiltering` from the driver. The switch has no default case, so a future enum member
surfaces as a compiler warning rather than an inherited wrong answer.

## Validation

25 dedicated pixel-readback CTest suites (label `OpenGL4`, `ctest -R OpenGL4_`), all passing
against a real `OpenGL 4.5 (Core Profile) Mesa` context under Xvfb, plus the full `CnaTests`
corpus under `CNA_GRAPHICS_RENDERER=OPENGL4` and an ASan/UBSan run of the representative renderer
suites. The integration-time record lives in `integration/lanes/opengl4.md` on the planning
branch.
