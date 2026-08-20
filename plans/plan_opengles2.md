# OPENGLES2 Renderer Profile Implementation Plan


> **Renderer selection.** This document describes the renderer as a compile-time choice
> (`-DCNA_GRAPHICS_RENDERER=...`), which remains the default and recommended mode. Since
> `plan_runtimerenderer.md`, CNA can also be built with several renderers and choose between
> them at runtime — see `docs/runtime-renderer-selection.md`. Nothing below changes in
> single-renderer mode.

> The OPENGLES2 renderer profile was authorized as the first entry of the FUTURE.md Phase-2
> renderer expansion and implemented on **2026-08-10**, on the dedicated branch
> `feature/renderer-opengles2` rooted at the pre-expansion normalization endpoint (`4c93f185c`).
> It is CNA's 42nd public renderer identity (41 → 42) and the **fifth public GL-family profile**:
> unlike OPENGLES1/OPENGL4/OPENGL1/OPENGL2 (each an independent renderer implementation), it
> shares the EasyGL implementation family (`modules/renderers/easygl/`, sibling `easy-gl` →
> `meta-gl`) with OPENGLES3/OPENGL33/WEBGL1/WEBGL2, selected by `CNA_GL_PROFILE_OPENGLES2`.
> The implementation-family count stays 38; only the public identity count grows.
>
> Design shape (verified against the sibling repositories before implementation, easy-gl
> `develop` @ `0b46d35c`, meta-gl `develop` @ `571d3a62`): meta-gl's loader validates
> "OpenGL ES 2.0" contexts against the ES 2.0 core entry-point baseline only, and easy-gl's
> `Device::initialize` requires the VertexArrayObject feature (`GL_OES_vertex_array_object` on
> ES < 3.0) — so a genuine native ES2 route exists and OPENGLES2 is
> `WEBGL1's GLSL ES 1.00 dialect + OPENGLES3's native context path + ES 2.0-only GL mechanics`.
> See `docs/opengles2-renderer.md` for the capability boundary and runtime-extension gates.
>
> **Status legend:** ✅ implemented and verified; 🟨 code exists but not fully verified;
> ⬜ not done.

## Tasks

- ✅ **OPENGLES2-1 — Sibling-repository ES2 verification (read-only).** meta-gl: ES 2.0 is the
  documented baseline (`OpenGL_ES.md`), `detect_version()` maps "OpenGL ES 2.0" → `Gles20`,
  `required_version_loaded()` then demands only `minimum_loaded()` (the ES 2.0 core set); entry
  points are loaded by exact core name (no OES-suffix fallback) and a null pointer terminates at
  call time — so ES 3.0+ calls must never be reached under this profile. easy-gl:
  `Capabilities::detect_common_features()` recognizes ES2 (`VertexArrayObject` =
  `is_at_least(3,0) || GL_OES_vertex_array_object`), and `Device::initialize` hard-requires
  VAO/Shader/Program/Buffer/BasicRendering. Neither sibling was modified.
- ✅ **OPENGLES2-2 — Public identity registration.** `GraphicsRendererType::OpenGLES2` (enum
  position 2, before `OpenGLES3`; `Metal` stays the count anchor), `"OPENGLES2"` name,
  `CNA_GRAPHICS_RENDERER=OPENGLES2` selector + `CNA_RENDERER_OPENGLES2` option,
  `CNA_RENDERER_EASYGL` + `CNA_GL_PROFILE_OPENGLES2` compile definitions (the family's
  established convention — deliberately NOT a distinct `CNA_RENDERER_OPENGLES2` compile
  definition), EasyGL family gate + non-Emscripten `FATAL_ERROR` + Linux-primary warning,
  `scripts/check_renderer_identities.py` = 42, `GraphicsRendererTypeTests` = 42.
- ✅ **OPENGLES2-3 — Context request.** `SDL_GL_CONTEXT_PROFILE_ES` major 2 minor 0 through the
  native path (shared attribute branch with WEBGL1; reached via desktop SDL, not Emscripten).
- ✅ **OPENGLES2-4 — GLSL ES 1.00 shader route.** `AdaptGlslEs300ForActiveProfile`'s WEBGL1
  branch widened to `WEBGL1 || OPENGLES2` (shared `TransformGlslEs300BodyToEs100` rewrite), plus
  both attribute-location rebind sites (`CompileAndLink`, SpriteBatch `InitializeResources`) and
  the Byte4 float-read mode (`DescribeVertexElementFormat` + the fixed-stride 52/56/68 skinned
  layouts via the new `SetBoneIndicesAttributePointer` helper).
- ✅ **OPENGLES2-5 — ES 2.0-only GL mechanics.** Sampler objects → per-texture-object sampling
  state with slot records and mip-term demotion (no `GL_TEXTURE_MAX_LEVEL` in ES 2.0; a GL-name →
  level-count registry replaces the Task 924/REMED-GFX-174 clamp); `glDrawElementsBaseVertex`
  (ES 3.2) → enabled-attribute pointer re-offset emulation (core ES 2.0 queries/calls only);
  `GL_READ_FRAMEBUFFER`/`glReadBuffer` → combined `GL_FRAMEBUFFER` readback with binding
  restore; sized `RGBA8` allocations → unsized `GL_RGBA`; `Depth24Stencil8` → separate
  DEPTH+STENCIL attach (`GL_OES_packed_depth_stencil`); startup limits pinned
  (`sampleCount_`/`maxMrtTargets_`/`supportsIndexedColorMasks_`) so a generously
  higher-versioned runtime context cannot leak ES 3.0 behavior into the profile.
- ✅ **OPENGLES2-6 — Truthful capability report.** MultiSampleAntiAliasing/MultipleRenderTargets/
  OcclusionQuery/Texture3D/Instancing/MultiStreamVertexInput report false;
  `DrawInstancedPrimitivesEx` takes the shared base-class refusal (OPENGLES1's own route);
  `SetRenderTargets(count>1)` throws the family's over-the-ceiling refusal at the pinned
  1-target ceiling; WireFrame reports the live `GL_OES_element_index_uint` presence. See
  `docs/opengles2-renderer.md`'s diff table.
- ✅ **OPENGLES2-7 — Test registration.** The full EasyGL example/pixel suite
  (`modules/renderers/easygl/examples/CMakeLists.txt`) and the family-gated demo registrations
  (graphics/net/gamer-services house3d/net/avatar demos) accept OPENGLES2; the CNAEXT
  DepthEffect/CRTEffect demos deliberately stay OPENGLES3/OPENGL33-only (raw GLSL ES 3.00
  `ShaderEffect` sources).
- ✅ **OPENGLES2-8 — Live verification.** Full CTest suite under
  `-DCNA_GRAPHICS_RENDERER=OPENGLES2` on this repository's Xvfb/Mesa environment (the first live
  GLSL ES 1.00 driver execution in this project): **6548/6549 passed** -- the sole failure,
  `EasyGL_GraphicsDevice_ReferenceStencil`, is a pre-existing cross-profile gap
  (`IGraphicsRenderer::SetReferenceStencil` is a no-op default EasyGL never overrode) that fails
  identically on the OPENGLES3 baseline. Strict-ES2 retake with
  `MESA_GLES_VERSION_OVERRIDE=2.0` (context reports and validates as "OpenGL ES 2.0 Mesa"):
  16/16 representative tests pass (SpriteBatch, BasicEffect/SkinnedEffect goldens, render
  targets + depth formats incl. the split depth+stencil attach, stencil ops, baseVertex
  draw-range validation, mip-level contract, cube readback, surface-format sweep). Controls:
  OPENGLES3 **6563/6564** (same sole pre-existing failure), OPENGL33 **6562/6564** (same, plus
  one two-process network-loopback timing flake that passes on retry) -- OPENGLES3/OPENGL33 are
  not weakened.
- ✅ **OPENGLES2-9 — Registry/docs.** `docs/renderer-registry.md` (42 rows),
  `docs/opengles2-renderer.md` (capability boundary), README/CLAUDE.md/NEXT.md/FUTURE.md counts
  and family lists.
- 🟨 **OPENGLES2-10 — Real ES 2.0-only hardware/driver validation.** The Mesa environment
  returns ES 3.x-capable contexts for the 2.0 request (legal version-floor semantics; the
  `MESA_GLES_VERSION_OVERRIDE=2.0` retake forces strict ES 2.0 *validation*, which is the
  strongest check this host can express). Validation on a genuinely ES 2.0-only driver
  (old mobile GPU / ANGLE ES2) remains open, same category of external gate as WEBGL1's
  real-browser validation and OPENGLES1's `-Dgles1=enabled` Mesa note.
