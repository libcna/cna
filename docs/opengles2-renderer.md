# OPENGLES2 (native OpenGL ES 2.0, GLSL ES 1.00) Renderer — Status

`OPENGLES2` is the fifth public GL-family `CNA_GRAPHICS_RENDERER` value — it shares its entire
implementation with `OPENGLES3`/`OPENGL33`/`WEBGL1`/`WEBGL2`
(`modules/renderers/easygl/`, on top of the sibling `easy-gl` → `meta-gl` chain), distinguished at
compile time by the `CNA_GL_PROFILE_OPENGLES2` definition. Conceptually it is the native-desktop/
mobile twin of `WEBGL1`: the same GLES 2.0-shaped context request (`SDL_GL_CONTEXT_PROFILE_ES`,
major 2, minor 0) and the same GLSL ES 1.00 shader rewrite, reached through the native
(EGL/GLX) context path `OPENGLES3` uses instead of Emscripten/WebGL. It was added as the first
entry of the FUTURE.md Phase-2 renderer expansion (identity count 41 → 42), on the reserved name
`docs/RendererNamingMigration.md` §3 planned for it.

**Status legend:** ✅ implemented and verified; 🟨 code exists but not fully verified; ⬜ not done.

## Support route (verified)

```
CNA OPENGLES2 (public identity)
  -> EasyGL renderer (modules/renderers/easygl, CNA_RENDERER_EASYGL + CNA_GL_PROFILE_OPENGLES2)
    -> sibling easy-gl (OOP GL wrapper; Device::initialize requires VertexArrayObject/Shader/
       Program/Buffer feature support -- on an ES 2.0 context that means the
       GL_OES_vertex_array_object extension, or initialization throws)
      -> sibling meta-gl (function loading + typed API; "OpenGL ES 2.0" version strings map to
         its Gles20 level, validated against the ES 2.0 core entry-point baseline only)
        -> native OpenGL ES 2.x driver (SDL_GL_CreateContext with an ES-profile request)
```

- The context request is exactly `SDL_GL_CONTEXT_PROFILE_ES`, major 2, minor 0 (plus the family's
  usual 8-bit stencil request). Per EGL/GLX semantics the driver may return any ES context
  **backward-compatible** with 2.0 (Mesa typically reports "OpenGL ES 3.2"); that is the same
  version-floor semantic every other GL profile's request has. What makes the profile genuinely
  ES 2.0 is that its compiled code never *uses* anything past ES 2.0: shaders are GLSL ES 1.00,
  every ES 3.0+ entry point is compiled out or replaced on every reachable path, and the
  capability report states ES 2.0 truth regardless of what the runtime context could additionally
  do (see the pinned constructor limits and `SupportsCapability` in `EasyGLRenderer.cpp`).
- Verified live in this repository's own test environment: the EasyGL example/pixel suite
  registers and runs under `-DCNA_GRAPHICS_RENDERER=OPENGLES2` (the first GLSL ES 1.00 execution
  against a real driver in this project — `WEBGL1`'s own dialect verification stopped at
  standalone string-transform checks, see `docs/webgl1-renderer.md`), and the profile was
  additionally exercised with `MESA_GLES_VERSION_OVERRIDE=2.0` forcing a strictly
  ES 2.0-validating context. Five example tests exist solely to exercise ES 3.0-level features
  this profile truthfully refuses (MSAA resolve ×3, occlusion queries ×2) and are deliberately
  not registered for it; the hardware-instancing gtest oracles skip themselves on the reported
  `Instancing = false` capability.

## What's real today

- ✅ **Registration** — enum/name/selector/profile define/factory wiring, registry gate
  (`scripts/check_renderer_identities.py` = 42) and docs (`docs/renderer-registry.md`).
- ✅ **Context creation** — native GLES 2.0 request through the same SDL3 path as `OPENGLES3`;
  desktop/mobile only (Emscripten selection is a configure-time `FATAL_ERROR` naming
  WEBGL1/WEBGL2 instead).
- ✅ **Shader route** — every stock shader is authored once against GLSL ES 3.00 and rewritten at
  first use by `AdaptGlslEs300ForActiveProfile()`'s shared `WEBGL1 || OPENGLES2` branch
  (`TransformGlslEs300BodyToEs100()`): `#version 100`, `attribute`/`varying`, `gl_FragColor`,
  `texture2D()`/`textureCube()`, float-encoded bone indices. C++-side attribute locations are
  rebound via `Program::bind_attrib_location()` (GLSL ES 1.00 has no `layout(location=N)`).
- ✅ **SpriteBatch** — the 2D pipeline (16-bit indices, glBufferData uploads) is ES 2.0-clean.
- ✅ **Texture2D** — uploads already used unsized `GL_RGBA` (ES 2.0's required form). Sampling
  state is applied per **texture object** (ES 2.0 has no sampler objects — see below).
- ✅ **Render targets (2D + cube)** — FBO + color texture (+ depth/stencil renderbuffer), with
  three ES 2.0 deltas: unsized `GL_RGBA` color storage, `Depth24Stencil8` attached to the DEPTH
  and STENCIL points separately (`GL_OES_packed_depth_stencil`; ES 2.0 has no
  `GL_DEPTH_STENCIL_ATTACHMENT`), and readback through the combined `GL_FRAMEBUFFER` binding
  (no `GL_READ_FRAMEBUFFER`/`glReadBuffer` in ES 2.0) with the previous binding restored.
- ✅ **Sampler states** — ES 2.0 keeps sampling state on the texture object, so
  `ApplySamplerState` records the request per slot and writes it onto the texture(s) bound to
  that unit, re-applied at every later bind on that unit (covers both call orders CNA uses).
  Mipmap completeness (no `GL_TEXTURE_MAX_LEVEL` in ES 2.0): a texture that allocated a full
  chain keeps its mip filter term; a single-level texture has the mip term demoted
  (`Linear*Mipmap*` → `Linear` etc.) — the same *effective* filtering `MAX_LEVEL` clamping
  produces on the ES 3.0 profiles.
- ✅ **Draw calls** — `DrawPrimitives`/`DrawIndexedPrimitives` (16/32-bit indices);
  `baseVertex != 0` is implemented with core ES 2.0 calls only, by re-offsetting every enabled
  attribute pointer by `baseVertex` elements of its own stride for the draw and restoring it
  after (FNA3D's own no-base-vertex GL fallback shape) — `glDrawElementsBaseVertex` is ES 3.2.
- ✅ **Wireframe emulation** — the family's GL_LINES re-expansion works, including its
  `baseVertex` case via the pointer-shift path; its 32-bit line indices make the `WireFrame`
  capability report conditional on `GL_OES_element_index_uint` (see the runtime-extension table).
- ✅ **BasicEffect family** — the stock effects run through the GLSL ES 1.00 rewrite; verified by
  the same golden/pixel tests `OPENGLES3` uses, executed under this profile.
- ✅ **Custom `ShaderEffect`s** — the mechanism works (`CustomEffects` reports true), but shader
  *source* is the game's own: it must be GLSL ES 1.00 under this profile, exactly as it must be
  WebGL-compatible under `WEBGL1`. A game supplying `#version 300 es` source gets a truthful
  compile error, not silent breakage. This includes CNA's own CNAEXT `DepthEffect`/`CRTEffect`
  demos (raw GLSL ES 3.00 `ShaderEffect`s): they are **not available** under `OPENGLES2` and
  their demo registrations remain gated to `OPENGLES3`/`OPENGL33`.

## Capability report — the concrete diffs from OPENGLES3

`SupportsCapability` answers, `OPENGLES2` vs `OPENGLES3` (everything not listed is identical and
true on both):

| GraphicsCapability | OPENGLES3 | OPENGLES2 | Why |
|---|---|---|---|
| `MultiSampleAntiAliasing` | live `GL_MAX_SAMPLES` query | **false** (pinned) | no multisample renderbuffers/blit in ES 2.0; backbuffer and render-target sample requests degrade to 1 (`GetMultiSampleCount()` reports 0) |
| `MultipleRenderTargets` | true (up to 4) | **false**; `maxMrtTargets_` pinned to 1, so `SetRenderTargets(count>1)` throws the family's own over-the-ceiling `std::runtime_error` (the MRT boundary the family's lifecycle/diagnostic tests record) | no `glDrawBuffers` in core ES 2.0 |
| `OcclusionQuery` | true | **false** | no query objects in ES 2.0 |
| `Texture3D` | true | **false** | no 3D textures in ES 2.0 (`Texture3D` construction is refused at the XNA layer) |
| `Instancing` | true | **false**; `DrawInstancedPrimitivesEx` takes the shared base-class refusal (the same `std::runtime_error` route OPENGLES1 keeps) | no `glDrawElementsInstanced`/`glVertexAttribDivisor` in core ES 2.0 |
| `MultiStreamVertexInput` | true | **false** (rejected before native submission) | same attrib-divisor-era plumbing gap as WEBGL1 |
| `WireFrame` | true | `GL_OES_element_index_uint` present (true on Mesa and essentially all real ES 2.0 drivers) | the line re-expansion uses 32-bit indices, an extension in ES 2.0 |
| `AnisotropicFiltering` | extension query | extension query (unchanged rule; applied per texture object instead of per sampler object) | `GL_EXT_texture_filter_anisotropic` |

Also pinned to ES 2.0 truth regardless of the runtime context: `supportsIndexedColorMasks_` =
false (`glColorMaski` is ES 3.2; per-MRT-slot `ColorWriteChannels` cannot differ — moot with MRT
itself refused).

## Runtime requirements (extensions on a strict ES 2.0 driver)

| Requirement | Needed for | Notes |
|---|---|---|
| `GL_OES_vertex_array_object` | everything (easy-gl `Device::initialize` throws without VAO support) | plus a driver whose loader resolves the unsuffixed core VAO names (`glGenVertexArrays` etc.) — meta-gl loads exact core names with no `OES`-suffix fallback; Mesa aliases them, and the ES 3.0-capable contexts Mesa actually returns carry them core |
| `GL_OES_element_index_uint` | 32-bit index buffers, wireframe emulation | universal in practice; `WireFrame` capability reports its live presence |
| `GL_OES_packed_depth_stencil` | `DepthFormat::Depth24Stencil8` render targets | `GL_OES_depth24` similarly backs `DepthFormat::Depth24` |
| `GL_OES_fbo_render_mipmap` | `RenderTarget2D/Cube::GetData` of mip levels above 0 | core ES 2.0 restricts FBO attachment to level 0 |
| `GL_EXT_texture_filter_anisotropic` | anisotropic filtering (optional; report follows presence) | |

These are advertised by Mesa (this repository's test environment) and by virtually all real
GLES 2.0 hardware of the ES 2.0 era; a driver missing one fails at its documented gate
(initialization throw, capability false, or refused operation) rather than silently misrendering.

## Known deliberate boundaries

- ⬜ **No MSAA, MRT, occlusion queries, Texture3D, instancing, multi-stream input** — see the
  capability table; these are ES 3.0-level features the profile truthfully refuses rather than
  emulating or silently accepting on a generously higher-versioned runtime context.
- 🟨 **Per-texture (not per-slot) sampling state** — inherent to ES 2.0: two slots sampling the
  SAME texture with different `SamplerState`s in one draw resolve to the last-applied state.
  XNA content doing this is rare; the ES 3.0 profiles are unaffected.
- 🟨 **Context recovery** — the desktop debug loss/recreate path works as on `OPENGLES3` (the
  ES 2.0 texture-level registry is rebuilt across the loss); non-recoverable resources behave as
  on every other GL profile.
- ⬜ **Android/EGL-native device validation** — like `OPENGLES3`, primarily tested on Linux
  (configure warns elsewhere); no separate mobile-device validation was performed for this
  profile.
