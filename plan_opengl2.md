# OPENGL2 Backend Plan

Native desktop OpenGL 2.1 backend. It deliberately does not use EasyGL and does not include the separately developed METAL, OPENGL1, or WEBGL1 backends.

## Status: 2D baseline verified (2026-07-19)

The backend as originally committed did not compile at all: `ApplyDepthStencilState`/
`ApplyRasterizerState` were declared with signatures that did not match
`IGraphicsBackend`'s pure/default virtuals (missing/extra/reordered parameters), so `override`
failed to resolve. Beyond that, review + a real Xvfb bring-up found and fixed:

- **Compile-blocking signature mismatches** on `ApplyDepthStencilState` (11 params declared vs.
  16 in `IGraphicsBackend`) and `ApplyRasterizerState` (6 params, wrong order/extra
  `multiSampleAntiAlias` vs. 5 in the interface). Fixed to match the interface exactly.
- **Wrong SpriteBatch blend factors**: hardcoded `GL_SRC_ALPHA`/`GL_ONE_MINUS_SRC_ALPHA`
  (non-premultiplied) inside every sprite draw, unconditionally overriding whatever `BlendState`
  the game actually requested. `SpriteBatch.Begin()`'s default `BlendState::AlphaBlend` is
  `Blend::One`/`Blend::InverseSourceAlpha` (premultiplied-style factors) -- the old hardcoded pair
  produced visibly wrong compositing for any non-opaque texture (verified with a pixel-exact
  regression test, `OpenGL2_2D`'s Check D). Fixed by implementing a real
  `ApplyBlendState(colorSrc, alphaSrc, colorDst, alphaDst, colorFunc, alphaFunc)` (full XNA
  `Blend`/`BlendFunction` enum -> GL mapping, mirroring `EasyGLGraphicsBackend`'s own) and no
  longer touching blend state at all inside `Sprite::Draw` -- it now relies on
  `GraphicsDevice::setBlendStateProperty()` having already reached `ApplyBlendState()` before the
  draw, exactly like `EasyGLSpriteBatchBackend`.
- **`ApplyBlendState` previously ignored all six parameters**, always forcing
  `glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)` regardless of the requested `BlendState`
  (Opaque/Additive/NonPremultiplied/custom all behaved identically). Now a real mapping.
  `DepthStencilState`/`RasterizerState` enum mapping is still only partial (see Follow-up).
- **No `ReadBackbuffer` override** -- `GraphicsDevice::GetBackBufferData()` threw on this backend.
  Added (`glReadPixels` + the same bottom-up-to-top-down row flip `EasyGLGraphicsBackend` uses),
  which is what made pixel-exact automated verification of the fixes above possible at all.
- **Viewport corruption on every non-default resolution** (found empirically, not by inspection):
  `GraphicsDevice::Reset()` calls `applyPresentationParametersToWindow()` (which calls
  `SDL_SetWindowSize()`) and then immediately `UpdateViewportFromWindow()` -> `SetViewport()` in
  the same call stack, with no event pump in between. SDL3 documents `SDL_SetWindowSize()` as
  asynchronous on some windowing systems (confirmed here under a bare Xvfb with no window
  manager); `SDL_GetWindowSize()`/`SDL_GetWindowSizeInPixels()` kept reporting the OLD window size
  at that point, so the backend's Y-flip math baked in a wrong `glViewport()` offset for the rest
  of the session (nothing re-triggers it later: `GraphicsDeviceManager::INTERNAL_OnClientSizeChanged`
  keys its "did anything change" check off the LOGICAL/virtual viewport size, which does not
  change across this resize). This is **shared `GraphicsDevice.cpp` code, not OpenGL2-specific**
  -- fixed by adding `SDL_SyncWindow(window_)` right after `SDL_SetWindowSize()` in
  `applyPresentationParametersToWindow()`, matching SDL3's own documented fix for this exact race.
  Sanity-rebuilt the EasyGL backend after this change (compiles clean); did not re-run EasyGL's
  full test suite.
- Also fixed while rewriting into normal multi-line formatting (was previously a handful of
  extremely dense one-liner functions, inconsistent with every other backend file in this repo):
  a stray/misleading comment claiming desktop GL has no `GL_UNSIGNED_INT` element indices (it does
  -- only GLES without an extension needs one; the draw path already used it correctly), and a
  function-local `static GLuint` sprite shader/VBO pair that would go stale across
  `DebugSimulateContextLoss()` or multiple `GraphicsDevice` instances in one process (now owned
  per-`Sprite`-instance, mirroring `EasyGLSpriteBatchBackend`'s own per-instance GL resources).

## Status: SpriteBatch feature completeness + 3D pixel-exact coverage (2026-07-19, session 2)

Continued past the 2D baseline:

- **`ApplyDepthStencilState` now fully wired**: added `CompareFunction`/`StencilOperation` -> GL
  enum mapping (mirroring `EasyGLGraphicsBackend`'s own tables) and real
  `glDepthFunc`/`glStencilFunc(Separate)`/`glStencilOp(Separate)`/`glStencilMask(Separate)` calls,
  including two-sided stencil. Previously only `depthEnable`/`depthWriteEnable`/`stencilEnable`
  were honored; compare functions and stencil ops were silently dropped.
- **`SpriteBatch.Begin(..., transformMatrix)` is now honored**: `Sprite::SetTransformMatrix()` was
  previously the `ISpriteBatchBackend` no-op default. Each sprite corner's screen-space position
  is now transformed by the matrix's 2D affine part before the screen->clip mapping (row-vector
  order, matching `EasyGLSpriteBatchBackend`'s `transform_ * orthoM` combined-matrix semantics).
  Verified pixel-exact (`OpenGL2_2D` Check E: a translation-only matrix moves a fixed-rect sprite
  to the transformed screen position, and nothing draws at the untransformed one).
- **`SetSamplerFilter`/`SetSamplerAddressMode` are now honored**: previously accepted and
  silently ignored (built-in `GL_LINEAR`/`GL_CLAMP_TO_EDGE` only, from `Tex`'s constructor).
  Added `TextureFilter`/`TextureAddressMode` -> GL mapping (the filter mapping mirrors
  `SdlGraphicsBackend::SetSamplerFilter`'s magnification-dominant reasoning, since this backend
  has no mipmaps to give the minification component separate meaning) and apply them via
  `glTexParameteri` on the bound texture right before each sprite draw (GL 2.1 has no separate
  sampler objects -- those are GL 3.3+). Verified pixel-exact (`OpenGL2_2D` Check F:
  `SamplerState::PointWrap` vs `PointClamp` read back different, correct texels for the same
  out-of-[0,1) UV).
- **New `OpenGL2_3D` CTest**: pixel-exact coverage for `DrawPrimitives`/`BasicEffect` through
  `VertexPositionColor` (colored3d), `VertexPositionTexture` (textured3d), and
  `VertexPositionColorTexture` (colored_textured3d), plus a real depth-test occlusion proof (a
  nearer quad wins regardless of draw order -- proves the depth buffer is real, not draw-order
  luck). BasicEffect lighting (`VertexPositionNormalTexture`/`EnableDefaultLighting`) is
  deliberately NOT covered -- this backend's 3D shaders are still unlit (see Follow-up), so a
  lit-scene pixel assertion would not be meaningful yet. 6/6 PASS.

## Status: RenderTarget2D/FBO support (2026-07-19, session 3)

- **`CreateRenderTarget2D`/`SetRenderTarget2D` implemented**: a single-sample, single-mip-level
  FBO (color texture + optional depth/`Depth24Stencil8` renderbuffer, mapped from `DepthFormat`
  the same way `EasyGLGraphicsBackend`'s `MapDepthFormat` does). No MSAA or mipmap generation yet
  (see Follow-up) -- `CreateRenderTarget2D`'s `mipMap`/`multiSampleCount` params are accepted
  (matching the interface signature) but currently ignored.
- **RT-aware Y-flip**: `SetViewport`/`SetScissorRect`/`ReadBackbuffer` previously always flipped
  against the window's height, which is wrong whenever an FBO smaller/larger than the window is
  bound. Added `currentRtHeight_` tracking (mirrors `EasyGLGraphicsBackend`'s identical pattern)
  so all three use the bound render target's own height instead when one is active.
- **Real bug found and fixed while building the RT test**: `Sprite::Draw` used to
  `dynamic_cast<const Tex*>(&texture)` and silently no-op if that failed -- meaning
  `SpriteBatch.Draw()` on a `RenderTarget2D` (a *very* common pattern: render an effect to a
  target, then blit it to the screen) always drew nothing at all on this backend. `RenderTarget`
  is a sibling `ITextureBackend` implementation, not a `Tex` subclass, so it always failed that
  cast. Fixed by using the `ITextureBackend&` parameter directly (`BindGL()` is already virtual
  on the common interface -- the downcast was never actually necessary).
- **RT-aware SpriteBatch sizing** (Task-1078-equivalent to `EasyGLGraphicsBackend`'s own fix of
  the identical bug class): `Sprite::Draw`'s screen->clip mapping used to always divide by the
  backend's window/virtual size. A `SpriteBatch` draw issued while a differently-sized
  `RenderTarget2D` is bound would size itself to the *window*, not the target, corrupting
  anything drawn into an off-screen target sized differently from the window (e.g. a shadow map,
  a UI composited at a fixed resolution). Added `GetCurrentRenderTarget2DSize()` (a
  backend-internal, non-`IGraphicsBackend` helper `Sprite` calls through its concrete
  `OpenGL2GraphicsBackend*` pointer, exactly mirroring how `EasyGLSpriteBatchBackend` reaches its
  own backend's equivalent method) so `Sprite::Draw` sizes itself to the bound RT when one exists.
- **New `OpenGL2_RenderTarget2D` CTest**: construction, `RenderTarget2D::GetData()` readback,
  sampling a just-rendered RT as a plain `Texture2D` via `SpriteBatch` (the bug above), a real
  depth-test occlusion proof *inside* the FBO's own depth/stencil renderbuffer, and the
  RT-vs-window sizing fix, all pixel-verified. 6/6 PASS. (One iteration finding along the way,
  not a backend bug: the depth-occlusion check initially failed because `SpriteBatch.Begin()`
  earlier in the same test left `GraphicsDevice.DepthStencilState` at its own default of `None` --
  real, documented XNA/FNA `SpriteBatch` behavior, not an OpenGL2 defect. Fixed by having the test
  restore `DepthStencilState::Default` before its 3D draws, exactly as a real game must.)

## Status: ApplySamplerState + AlphaTest/DualTexture/lighting/fog (2026-07-19, session 4)

Working toward EasyGL feature parity (user request: "at least EasyGL's capabilities, within the
possibilities of OpenGL 2"):

- **`ApplySamplerState` implemented** for direct 3D draws (`GraphicsDevice.SamplerStates[slot]`
  previously had zero effect on `DrawPrimitives`/`DrawIndexedPrimitives`). GL 2.1 has no sampler
  objects, so the requested filter/wrap is cached per slot and applied via `glTexParameteri` at
  the point `drawInternal()` actually binds a texture to that unit -- same approach as
  `Sprite::Draw`'s pre-existing per-draw sampler application.
- **AlphaTestEffect**: folded directly into the existing textured/dual-texture/lit fragment
  shaders (no separate compiled program needed) via the exact discard formula documented on
  `GpuDrawParams::alphaTest` and independently confirmed against `AlphaTestEffect::
  FillGpuDrawParams()`'s own comment. Defaults to a no-op (`{0,0,1,1}` = always pass) so every
  existing textured draw is unaffected when alpha testing isn't in use.
- **DualTextureEffect**: new `dualTextureProgram_` (two samplers at the same texcoord,
  `base*2.0` then modulated by the second texture -- the classic lightmap technique, matching
  `EasyGLGraphicsBackend::EnsureDualTextured3DProgram`'s identical formula).
- **BasicEffect lighting**: new `litProgram_`, per-pixel Blinn-Phong with 3 directional lights
  (ambient + diffuse + specular + emissive), selected when `GpuDrawParams::lightingEnabled` and
  the vertex format carries a normal (`VertexPositionNormalTexture`, stride>=32 -- CNA has no
  vertex format that pairs a normal with per-vertex color, so lit draws always use the
  material-level `DiffuseColor`, never a vertex color, matching `EasyGLGraphicsBackend::
  EnsureLit3DProgram`'s own formula exactly). Always per-pixel regardless of
  `GpuDrawParams::preferPerPixelLighting` -- matches that field's own documented, accepted
  project-wide convention (every backend but D3D9 ignores it). `uNormalMatrix` uses the raw World
  upper-3x3 (no inverse-transpose) -- correct for translation/rotation/uniform-scale World
  matrices; a documented simplification for non-uniform scale (see Follow-up).
- **BasicEffect fog**: added to every shader (colored/textured/dual-texture/lit) uniformly, using
  the exact same object-space-vertex-Z formula as `EasyGLGraphicsBackend` (a known, documented
  simplification -- see this project's own `feedback_easygl_fog_object_space_only` note --
  reads raw local vertex Z, not a real eye-space distance; matched here for consistency rather
  than implemented "more correctly" in isolation).
- **New `OpenGL2_Effects` CTest**: AlphaTestEffect discard (opaque survives, alpha-32-vs-
  reference-128 is discarded), DualTextureEffect's `*2.0` lightmap formula (half-intensity base
  reads back full-intensity, not half), BasicEffect lighting (a light facing the surface reads
  near-white; one facing away reads dim ambient-only), and BasicEffect fog (a quad at FogStart
  reads the fog color exactly; one at FogEnd reads its own unfogged color exactly). All
  pixel-verified. 8/8 PASS. (Iteration finding, not a backend bug: the first fog attempt used
  Z=-5/Z=5 with an Identity projection, which put the geometry outside the clip-space [-1,1]
  range and got near/far-clipped entirely -- fixed by keeping FogStart/FogEnd/Z within that range.)

## Status: FixedHeightDynamicWidth presentation-mode scaling (2026-07-19, session 4 cont'd)

- **`GetViewportSize()` now implements `CnaPresentationMode::FixedHeightDynamicWidth`** (the
  default mode): the virtual height stays fixed but the logical width is derived from the
  window's live aspect ratio, exactly matching `EasyGLGraphicsBackend::getLogicalSize()`'s own
  formula. Previously the virtual resolution was used verbatim regardless of window size, so a
  resized window never revealed more content or adapted at all. Every other mode (Letterbox/
  Overscan/Stretch/NativeBackBuffer) still falls back to the virtual size verbatim -- EasyGL's
  own `GetViewportSize` doesn't differentiate those either, so this isn't a new gap relative to
  it.
- **`TransformWindowToLogical`/`TransformLogicalToWindow` fixed to match**: these previously used
  a separate X scale derived from the fixed `virtualWidth_`, which became inconsistent with the
  now-adaptive logical width. Switched to a pure height-derived uniform scale (no offset), same
  formula and reasoning as `EasyGLGraphicsBackend`'s identical methods -- exact for
  FixedHeightDynamicWidth specifically, since that mode has no letterbox bars needing an offset.
- **New `OpenGL2_Presentation` CTest**: resizes the real SDL window from 320x240 to 640x240 via
  `SDL_SetWindowSize`/`SDL_SyncWindow` mid-run and verifies `GetViewportSize()` adapts to 640x240
  (not the original 320x240), and that a `SpriteBatch` draw near the new right edge is actually
  visible after readback -- proving the real GL viewport was re-issued, not just C++-side
  bookkeeping. 4/4 PASS.

## Status: RenderTarget2D MSAA + mipmap generation (2026-07-19, session 5)

- **MSAA render targets**: a multisample color renderbuffer (`glRenderbufferStorageMultisample`,
  clamped to `GL_MAX_SAMPLES`) plus a separate single-sample resolve FBO, resolved via
  `glBlitFramebuffer` when the target stops being bound -- mirrors
  `EasyGLRenderTargetBackend`'s identical multisample-renderbuffer + resolve-FBO shape. The
  depth/stencil renderbuffer (when requested) is multisampled too, attached to the same MSAA FBO.
- **Mipmap generation**: `mipMap=true` pre-allocates the full mip chain (empty, all levels) at
  construction, then `glGenerateMipmap()` regenerates it from the just-rendered level 0 when the
  target is unbound -- same resolve-then-mipmap order as `EasyGLRenderTargetBackend`'s own
  `UnbindAsRenderTarget()` (matches FNA3D's `OPENGL_ResolveTarget` behavior).
- **Real bug found while testing this**: `SetRenderTarget2D(nullptr)` previously just bound the
  default framebuffer directly -- it never called the OUTGOING target's own
  `UnbindAsRenderTarget()` first, so the MSAA resolve blit and mip regeneration above were
  entirely dead code in practice (only ever reachable via the RT's own destructor path, which
  doesn't call it either). Fixed by tracking the currently-bound `IRenderTargetBackend*` and
  unbinding it properly before switching targets.
- **Second real bug found while testing this** (in the new CTest itself, not the backend): the
  first MSAA differential check used a diagonal-split triangle with the wrong winding order,
  which this backend's default `RasterizerState.CullCounterClockwise` silently backface-culled in
  *both* the MSAA and single-sample targets -- an all-background render, which the single-sample
  assertion ("is the pixel a flat, unblended color") didn't catch since a pure background color is
  trivially "flat" too. A separate full-coverage quad draw (already proven correct elsewhere)
  confirmed MSAA rendering itself worked before the triangle's winding was the culprit; fixed the
  test's vertex order to match this project's established CW convention.
- **`OpenGL2_RenderTarget2D` extended** with a mipmap check (level-1 mip matches the level-0
  drawn color) and an MSAA check (a hard diagonal edge resolves to a genuine blended color, e.g.
  `(128,0,128)` for a red/blue split, vs. a flat, unblended color on the equivalent single-sample
  target). 9/9 PASS.

## Status: normal-matrix fix + occlusion queries (2026-07-19, session 6)

- **Real inverse-transpose normal matrix**: `uNormalMatrix` previously used the raw World
  upper-3x3, correct only under translation/rotation/uniform scale. Switched to
  `Matrix::Transpose(Matrix::Invert(world))` (both already public XNA API, no new 3x3-inverse
  routine needed). Verified with a differential CTest: `World=Scale(3,1,1)` + a 45-degree-tilted
  normal reads back `(242,242,242)` (matching the precomputed correct N.(-L)~=0.9487) instead of
  the old, wrong `~(81,81,81)` (N.(-L)~=0.3162).
- **Occlusion queries implemented**: real `GL_SAMPLES_PASSED` (`ARB_occlusion_query`, core since
  GL 1.5) -- an exact pixel count, actually more precise than
  `EasyGLGraphicsBackend`'s own GLES3 `GL_ANY_SAMPLES_PASSED` (which can only report 0 or 1).
  New `OpenGL2_OcclusionQuery` CTest: a fully visible full-screen quad reports the exact
  backbuffer pixel count (`76800` for 320x240); a quad drawn entirely behind a nearer opaque one
  (real depth-test occlusion) reports exactly `0`. 4/4 PASS.

## Status: TextureCube (2026-07-19, session 6 cont'd)

- **Plain `TextureCube` implemented**: `GL_TEXTURE_CUBE_MAP` (`ARB_texture_cube_map`, core since
  GL 1.3). `CubeMapFace`'s own ordinals (`PositiveX=0`..`NegativeZ=5`) already match
  `GL_TEXTURE_CUBE_MAP_POSITIVE_X`..`NEGATIVE_Z`'s consecutive enum values in the same order, so
  `GL_TEXTURE_CUBE_MAP_POSITIVE_X + face` needs no separate mapping table. Supports mipmaps (full
  chain pre-allocated per face at construction, matching `RenderTarget`'s own approach).
  No stock effect samples a cube map yet on this backend (`EnvironmentMapEffect` is still a
  follow-up item), so `OpenGL2_TextureCube` verifies the storage/upload/readback round-trip
  directly through `SetData()`/`GetData()` rather than a rendered pixel: all 6 faces hold their
  own distinct color after a round-trip (proving the face-ordinal mapping is really per-face, not
  all 6 landing on the same target), and a `mipMap=true` cube's level-0 round-trip still works.
  3/3 PASS.

## Status: Texture3D (2026-07-19, session 6 cont'd)

- **`Texture3D` implemented**: `GL_TEXTURE_3D` (core desktop GL since 1.2, no extension needed --
  unlike `GLES`). `OpenGL2_Texture3D` verifies a full-volume `SetData()`/`GetData()` round-trip
  (a distinct solid color per depth slice, proving the Z dimension is real and slices don't
  overwrite each other) and a sub-volume round-trip at a non-zero `(x,y,z)` offset. Only level 0
  is allocated -- `mipMap` is accepted (matching `CreateTexture3D`'s interface signature) but not
  yet generated/stored, consistent with `TextureCube`'s own already-implemented mip chain being
  the more complete precedent to eventually extend this to. 3/3 PASS.

### Verified working
- `cmake/Tests/OpenGL2Tests.cmake` registers nine CTests (Xvfb, `SDL_VIDEODRIVER=x11`):
  - `OpenGL2_Smoke` -- window/GL-context lifecycle, VertexBuffer/16-bit/32-bit IndexBuffer
    round-trips, 60 frames of Clear+Present. 7/7 PASS.
  - `OpenGL2_2D` -- real `Texture2D` + `SpriteBatch`, pixel-verified via `ReadBackbuffer`:
    quadrant UV mapping (no flip bug), an opaque draw, `BlendState::AlphaBlend` factor
    correctness, `transformMatrix` translation, `SetSamplerAddressMode` wrap/clamp, rotation/
    both-flip draws, 120 stable frames. 13/13 PASS.
  - `OpenGL2_3D` -- colored3d/textured3d/colored_textured3d + real depth-test occlusion, all
    pixel-verified. 6/6 PASS.
  - `OpenGL2_RenderTarget2D` -- FBO construction, `GetData()` readback, RT-as-Texture2D via
    SpriteBatch, depth occlusion inside the FBO, RT-vs-window sizing, mipmap generation, MSAA
    resolve. 9/9 PASS.
  - `OpenGL2_Effects` -- AlphaTestEffect, DualTextureEffect, BasicEffect lighting and fog, all
    pixel-verified. 8/8 PASS.
  - `OpenGL2_Presentation` -- FixedHeightDynamicWidth adapts to a real window resize. 4/4 PASS.
  - `OpenGL2_OcclusionQuery` -- exact GL_SAMPLES_PASSED pixel counts, visible and fully occluded. 4/4 PASS.
  - `OpenGL2_TextureCube` -- SetData/GetData round-trip for all 6 faces + mipmap construction. 3/3 PASS.
  - `OpenGL2_Texture3D` -- SetData/GetData round-trip across depth slices + sub-volume offset. 3/3 PASS.
- The pre-existing `examples/demo_2d` app (`cna_demo_2d`, window title "CNA 2D Demo") builds and
  runs end-to-end against this backend: real PNG texture load, ~50-100 animated rotating/scaling
  alpha-blended sprites, audio, `--smoke N` clean exit. Screenshot captured via a temporary
  backbuffer dump confirms correct rendering (clean sprite edges, no blend-fringing artifacts).
- Full `CNA` + `CnaTests` + example suite builds clean under `-DCNA_GRAPHICS_BACKEND=OPENGL2`
  except `cna_demo_xact`, which fails only at its Content-copy POST_BUILD step because
  `examples/demo_xact/Content/` does not exist in this checkout at all (never committed to git) --
  pre-existing, unrelated to this backend or this branch.

## Implemented foundation
- Dedicated `CNA_GRAPHICS_BACKEND=OPENGL2` selection.
- SDL-created OpenGL 2.1 compatibility context. SDL is only window/context glue; rendering is direct OpenGL.
- GLSL 1.20 shader compilation/linking.
- 2D SpriteBatch path (verified correct, see above).
- Basic colored/textured 3D using World/View/Projection matrices (stride-inferred vertex layout;
  pixel-verified, see `OpenGL2_3D` above).
- VBOs and 16/32-bit index buffers.
- Depth, stencil (including two-sided), blending, culling, scissor, viewport, wireframe and
  polygon offset, all with real XNA enum -> GL mappings (`Blend`/`BlendFunction`/
  `CompareFunction`/`StencilOperation`).
- `ApplySamplerState` (direct 3D draws) and `SpriteBatch`'s own `SetSamplerFilter`/
  `SetSamplerAddressMode` -- both wired to real `glTexParameteri` calls.
- `ReadBackbuffer` (pixel readback, enables automated pixel-exact testing).
- Occlusion queries (real `GL_SAMPLES_PASSED` pixel counts).
- `TextureCube` (`GL_TEXTURE_CUBE_MAP`, see `OpenGL2_TextureCube` above); not yet sampled by any
  stock effect (`EnvironmentMapEffect` remains a follow-up item).
- `Texture3D` (`GL_TEXTURE_3D`, see `OpenGL2_Texture3D` above); level 0 only, no mip chain yet.
- RenderTarget2D/FBO, including MSAA (resolve-on-unbind) and mipmap generation.
- AlphaTestEffect, DualTextureEffect, and BasicEffect lighting (3 directional lights, ambient,
  specular, emissive) + fog (see `OpenGL2_Effects` above).
- No EasyGL dependency.

## Follow-up work (toward EasyGL feature parity, within OpenGL 2.1's real capabilities)
- `CnaPresentationMode::Letterbox`/`Overscan`/`Stretch` still fall back to the virtual size
  verbatim rather than real letterbox-bar/crop/non-uniform-stretch scaling -- matches EasyGL's
  own current behavior (not a new OpenGL2-specific gap), but neither backend actually implements
  those three modes' real semantics yet.
- Full vertex declaration support rather than stride inference (blocks any vertex format beyond
  the 4 already recognized by stride, e.g. a custom `VertexDeclaration` with a different
  attribute order/extra streams).
- SkinnedEffect (bone palette skinning) -- EasyGL has this; would need a new vertex format
  (blend indices/weights) and a skinned variant of the lit shader.
- PbrEffect/SkinnedPbrEffect (metallic-roughness BRDF) -- EasyGL has this; a large, separate
  effort (its own shader family + texture set), likely lower priority than the items above for a
  "no EasyGL dependency" OpenGL 2.1 backend.
- `RenderTargetCube` -- only `RenderTarget2D` is implemented; `CreateRenderTargetCube` still
  returns `nullptr` (default).
- EnvironmentMapEffect subset (now that plain `TextureCube` exists, this is the remaining piece --
  a reflection-mapping shader sampling `samplerCube`, plus `RenderTargetCube` above for
  render-to-cube-map scenarios).
- Custom `ShaderEffect`/`CreateEffectBackend` (runtime-compiled user GLSL) -- still returns
  `nullptr` (default). Would let games write their own OpenGL2 shaders through CNA's `Effect`
  API, mirroring `EasyGLEffectBackend`.
- Robust extension/capability detection (`SupportsCapability` still uses `IGraphicsBackend`'s
  blanket "everything is supported" default -- no real `GraphicsCapability` gating for anything
  above, e.g. reporting cube-map/3D-texture/occlusion-query support accurately once implemented).
- Windows GL 2.x entry-point loader validation; current direct prototypes are primarily intended for Linux desktop builds.
- Visual parity tests against other backends (golden-image comparison).
