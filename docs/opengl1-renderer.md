# OPENGL1 — legacy desktop OpenGL 1.x fixed-function graphics renderer

Status: **Historical** — a genuine fixed-function implementation of the XNA-facing graphics
surface on legacy desktop OpenGL, deliberately independent of every shader-based renderer.

## glTF and the wide vertex records

This renderer emits immediate-mode vertices and has cases for the stride-16, 20, 24 and 32 layouts
only. Every wider canonical CNA record — 48, 52, 56, 60, 68, 76 and 80, which is every PBR and
skinned layout the glTF importer produces — is **refused by name**, and so is any draw that came
from `PbrEffect` or `SkinnedPbrEffect`.

**Until 2026-08-18 it drew them instead** (`plan_gltf.md GLTF-477`). `DrawInternal`'s per-vertex
`emit` ended in `else glColor4f(1,1,1,1)`, so a glTF model rendered as untextured, unlit, flat white
geometry and the draw reported success. OpenGL 1.x fixed function has no shader stage of any kind,
so there is nothing here to grow into a metallic-roughness path; refusing is the honest half of the
renderer partition, and it is what `SOKOL`, `TINYGL`, `GLIDE`, `OPENGLES1` and `PORTABLEGL` already
did. The refusal happens before `SetupMatrices`, so no GL state is touched and nothing is submitted.

Verified on a live device: `RendererStrideConformance` 3/3 and 600 of 603 `*Gltf*`/`*OpenGL1*` tests.

## Identity and selection

| Field | Value |
|---|---|
| Enum identity | `CNA::GraphicsRendererType::OpenGL1` |
| Build option | `-DCNA_GRAPHICS_RENDERER=OPENGL1` / `CNA_RENDERER_OPENGL1` |
| Renderer target | `cna_renderer_opengl1` |
| Platforms | desktop Linux and Windows only (enforced at configure time) |
| Plan | `plan_opengl1.md` · test scenarios: `tests/opengl1/README.md` |

## API contract vs. driver-reported version

The renderer requests a **legacy (non-core) OpenGL 1.1 context** (`SDL_GL_CONTEXT_MAJOR_VERSION=1`,
`MINOR=1`, no profile mask) and uses **only the fixed-function pipeline**: immediate-mode vertex
emission (`glBegin`/`glVertex3f`), `GL_MODELVIEW`/`GL_PROJECTION` matrices, `glLight*`,
`glTexEnv*` combiners, `GL_FOG`, `glAlphaFunc`. There is no shader of any kind in the renderer —
no `glCreateShader`, no `glUseProgram`, no generated artifacts.

A modern driver typically grants a **compatibility profile** context reporting its own highest
version (validated: `4.5 (Compatibility Profile) Mesa 25.0.7`, llvmpipe on Xvfb). That reported version
is the host driver's, not the renderer's API level: the renderer's calls are the GL 1.x
fixed-function subset plus runtime-discovered 1.2–1.5-era features. Do not describe this renderer
as creating a "GL 1.1 context" on such hosts, and do not describe it as a GL 4.x renderer either.

Runtime feature discovery (`OpenGL1Capabilities`, phase 1) gates everything newer than 1.1:
`ARB_framebuffer_object` entry points (render targets, mip generation),
`ARB_multitexture`/core-1.3 (`DualTextureEffect`, `EnvironmentMapEffect`),
`ARB_texture_cube_map`/core-1.3, `GL_EXT_texture_filter_anisotropic`, core-1.4 extended blend
(`glBlendColor`/`glBlendFuncSeparate`/`glBlendEquationSeparate`), `ARB_occlusion_query`/core-1.5.
Every entry point is loaded through `SDL_GL_GetProcAddress`; there is **no GL loader library and
no EasyGL/MetaGL dependency**.

## Supported

Device/window/context lifecycle (GLX visual attributes — depth 24, stencil 8, optional MSAA —
requested before `SDL_CreateWindow`, which is where GLX fixes them) · `Clear` (all variants) ·
`Present` · viewport/scissor (render-target-aware Y-flip) · blend state incl. constant blend
colour, blend equations beyond add and separate alpha factors (core 1.4) · slot-0
`ColorWriteChannels` via `glColorMask` (REMED-GFX-077) · depth/stencil state · culling ·
**WireFrame** via real `glPolygonMode(GL_LINE)` (pixel-oracle-proven in the shared suite) ·
depth bias (`glPolygonOffset`) · **fog** — the FNA fog-vector contract recovered exactly by
inversion (see below) · fixed-function transforms and lighting: three directional lights,
specular (`GL_SEPARATE_SPECULAR_COLOR`, local viewer), emissive, vertex-colour × diffuse
modulation · alpha test (coarse `glAlphaFunc(GL_GEQUAL)` approximation, documented) ·
`Texture2D` incl. three-tier automatic mip generation, mip-aware filters, anisotropy, Mirror
wrap · `TextureCube` + `EnvironmentMapEffect` reflection subset (no Fresnel, no
EnvironmentMapSpecular — fixed-function-inexpressible, documented) · `DualTextureEffect`
(GL_COMBINE modulate2x) · CPU-side vertex/index buffers (16- and 32-bit indices), stride
dispatch 16/20/24/32 with the REMED-GFX-DECL-GUARD declaration-fidelity guard on all four draw
routes · non-indexed and indexed draws · `DrawUser*` · `SpriteBatch` (own ortho, wrap modes,
render-target sampling flip) · `RenderTarget2D` (FBO) incl. mip-chain regeneration on unbind,
**MSAA** (multisample renderbuffers + blit resolve) and `GetData` readback · `RenderTargetCube`
(one re-pointed FBO, readback, context-loss aware) · backbuffer **MSAA** (driver-granted,
honestly read back) · **occlusion queries** (`GL_SAMPLES_PASSED`, exact counts) · virtual
resolution / presentation modes (FixedHeightDynamicWidth recomputation) · runtime
`SetSwapInterval` · `ReadBackbuffer` · context-loss recovery registry (Texture2D, TextureCube,
RenderTarget2D/Cube; re-binds the target that was active at loss time) · disposal.

## Fog: the FNA fog-vector inversion

`GpuDrawParams` carries FNA's fog **vector** (REMED-GFX-010); shader renderers dot it against the
object-space position. A fixed-function pipeline has no such dot product — `glFog` needs the
scalars back. `ApplyFogFromVector()` recovers them **exactly** by projecting `fogVector.xyz`
onto the eye-Z row of the same `world*view` matrix the draw just loaded into `GL_MODELVIEW`:
the projection yields the scale, the `w` term then yields `FogStart`/`FogEnd`. All-zero is
honoured as FNA's "fog disabled" encoding; `{0,0,0,1}` (the degenerate `FogStart == FogEnd`)
lands on a fully-fogged ramp. Pinned by a three-pair pixel oracle in
`modules/renderers/opengl1/examples/opengl1_fog_alphatest_test.cpp` — before-ramp, exact mid-ramp and degenerate, each
with its own expected result.

## Unsupported — rejects or reports truthfully, never silently

- custom `ShaderEffect`/GLSL, `SkinnedEffect`, `PbrEffect`, `SkinnedPbrEffect` —
  `CreateEffectRenderer` keeps the interface `nullptr` default; `CustomEffects` reports false
- multiple render targets — capability false; a descriptor set with more than one target throws
  `System::NotSupportedException` (never silently reduced to the first)
- `Texture3D` — capability false; the shared unsupported-renderer suite runs here
- hardware instancing and multi-stream vertex input — capability false
- vertex declarations the stride dispatch cannot represent faithfully — refused at draw time by
  the guard, device stays usable
- CPU upload into a render-target cube face (`RenderTargetCube::SetData`) — inherited interface
  refusal, asserted by the shared contract test
- Fresnel / `EnvironmentMapSpecular` terms of `EnvironmentMapEffect` — always behave as
  `FresnelEnabled=false` / `EnvironmentMapSpecular=0`
- exact XNA `AlphaTestEffect` `CompareFunction` semantics — single `GL_GEQUAL` approximation
- `BlendState.MultiSampleMask` — no fixed-function equivalent (`GL_SAMPLE_MASK` is GL 3.2+)

`SupportsCapability` is an exhaustive ten-member switch with no default case: `ThreeD`,
`DepthStencilBuffer`, `WireFrame` true; `AnisotropicFiltering`, `OcclusionQuery` from runtime
detection; `MultiSampleAntiAliasing` from what the driver genuinely granted;
`MultipleRenderTargets`, `CustomEffects`, `Texture3D`, `MultiStreamVertexInput` false.

## Dependencies

The platform's own OpenGL library via `find_package(OpenGL REQUIRED)` (`OpenGL::GL`) plus the
project's existing SDL3. Nothing vendored, nothing downloaded, no loader library, no absolute
local paths, no EasyGL/MetaGL.

## Validation

All 38 dedicated `OpenGL1_*` CTest suites pass (serial, real context: Mesa / llvmpipe /
`4.5 (Compatibility Profile) Mesa 25.0.7`). Full `CnaTests` under `CNA_GRAPHICS_RENDERER=OPENGL1`:
**5737 run · 5692 passed · 44 skipped · 1 failed** — the 44 skips are the positive `Texture3DTest`
suite (this renderer truthfully reports no `Texture3D`; its unsupported-renderer mirror runs), four
sensor-hardware skips, and two companions; the 1 failure is the known pre-existing two-process
networking flake, unrelated to graphics. The fog inversion is pinned by a three-pair oracle
(before-ramp / exact mid-ramp / degenerate), the WireFrame claim by the shared pixel oracle, and
the capability table by independent cross-checks (`GL_POLYGON_MODE` readback, extension scans, an
occlusion query counting exactly its viewport's pixels).
