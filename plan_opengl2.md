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

## Status: RenderTargetCube (2026-07-19, session 6 cont'd)

- **`RenderTargetCube` implemented**: one shared FBO whose color attachment is re-pointed at
  whichever face is active (`glFramebufferTexture2D` with `GL_TEXTURE_CUBE_MAP_POSITIVE_X+face`),
  plus one depth/(stencil) renderbuffer shared across all 6 faces (the common convention --
  cleared per-face rather than needing 6 independent depth buffers). Supports mipmaps (same
  pre-allocate-then-`glGenerateMipmap`-on-unbind approach as `RenderTarget`/`TextureCubeBackend`).
  Single-sample only -- MSAA cube render targets are a follow-up item, unlike `RenderTarget2D`'s
  own MSAA support.
  Reused the same `unbindCurrentRenderTarget()`/outgoing-target tracking fix `RenderTarget2D`
  needed (see session 5): `SetRenderTargetCubeFace()` is now an explicit override (the
  `IGraphicsBackend` default just calls `BindAsRenderTargetFace()` directly, bypassing the
  Y-flip/MSAA/mipmap tracking entirely) -- without this, cube-face rendering would have hit the
  exact same "resolve/mipmap regen never runs" bug found and fixed for `RenderTarget2D`.
  `OpenGL2_RenderTargetCube` verifies all 6 faces hold their own distinct color, real depth-test
  occlusion inside a face, and mipmap generation. 5/5 PASS.

## Status: EnvironmentMapEffect (2026-07-19, session 6 cont'd)

- **`EnvironmentMapEffect` implemented**: a dedicated `envMapProgram_` GLSL 1.20 program
  (reflection-mapped shading), matching `EasyGLGraphicsBackend::EnsureEnvMapped3DProgram`'s
  formula exactly -- same per-vertex Fresnel evaluation (`ComputeFresnelFactor`, evaluated at each
  vertex from its own un-interpolated normal/eye vector, then Gouraud-interpolated -- NOT
  recomputed per-fragment, matching real FNA), same `litRGB = lightSum*diffuse+emissive`
  composition, same `mix(baseColor, envSample.rgb*combinedAlpha, blendFactor) +
  envMapSpecular*envSample.a*combinedAlpha` blend. `drawInternal()` now selects this program via a
  new `envMapped` flag (checked before `lit`, since `EnvironmentMapEffect::FillGpuDrawParams()`
  also sets `lightingEnabled=true`), reusing the same `VertexPositionNormalTexture` (stride>=32)
  vertex layout as `litProgram_`.
  Added `ensureDefaultEnvMapTextures()`: a lazily-created 1x1 white `GL_TEXTURE_2D` and 1x1 white
  `GL_TEXTURE_CUBE_MAP` fallback, bound when `Texture`/`EnvironmentMap` are null respectively --
  unlike BasicEffect/AlphaTestEffect/DualTextureEffect (which only select a texturing program when
  `texture0` is actually non-null), `EnvironmentMapEffect::FillGpuDrawParams()` always sets
  `textureEnabled=true` and the fragment shader always samples both `uTex` and `uEnvMap`
  unconditionally, so a real XNA `EnvironmentMapEffect` with either property left unset (a common
  case -- `EnvironmentMap` in particular is easy to forget) must still render sane output rather
  than sampling an unbound texture unit (implementation-defined, typically black/garbage).
  `OpenGL2_EnvironmentMapEffect` verifies: the reflection fully replaces the base and samples the
  geometrically-correct cube face when Fresnel is disabled; the per-vertex (not per-fragment)
  Fresnel evaluation produces the mathematically-derived blend at a symmetric quad's centre, not a
  naive "0 at head-on view" guess; cross-backend consistency against
  `examples/easygl_environmentmapeffect_golden_test.cpp`'s own independently FNA-derived expected
  pixel (151,101,76) reused verbatim; and the null-Texture/null-EnvironmentMap fallback actually
  takes effect (reads back pure white, not black). 5/5 PASS.
  This closes the "one remaining piece for cube-map support" note left in the RenderTargetCube
  status entry above -- `TextureCube`/`RenderTargetCube` now have a real stock-effect consumer.

## Status: SkinnedEffect (2026-07-19, session 6 cont'd)

- **`SkinnedEffect` implemented**: a dedicated `skinnedProgram_` GLSL shader (bone-palette vertex
  skinning), matching `EasyGLGraphicsBackend::EnsureSkinnedProgram`'s formula exactly -- same
  `skinMat=sum(uBones[index]*weight)` over the first `WeightsPerVertex` weight/index pairs, same
  degenerate-blend-normal guard (falls back to the bind-pose normal if the skinned normal's
  length collapses toward zero from near-cancelling bone rotations), same
  `litRGB=lightSum*diffuse+emissive` composition, same vertex-color-modulates-diffuse-and-specular
  ordering. `drawInternal()` selects it via a new `skinned` flag, checked before `envMapped`/`lit`
  (`SkinnedEffect::FillGpuDrawParams()` also sets `lightingEnabled=true`).
  New vertex layout support for `VertexPositionNormalTextureSkinned` (stride 52: position, normal,
  texcoord, `Vector4` blend weight, `Byte4` blend indices) and its stride-56 vertex-color variant
  (matches every other CNA backend's own "stride 52/56" convention). GL 2.1 has no integer vertex
  attributes (`glVertexAttribIPointer` is GL 3.0+), so bone indices are uploaded as an
  unnormalized `GL_UNSIGNED_BYTE`-as-float attribute (exact for byte-range values, no precision
  loss) and rounded back to `int` in the shader; vertex-shader dynamic (non-constant) indexing of
  a uniform array (`uBones[i]`) is valid GLSL back to 1.10, so this is portable GL 2.1 -- no
  `#version` bump needed anywhere in this file. Added two new globally-bound attribute locations
  (4=`aBoneWeight`, 5=`aBoneIndices`, harmless no-ops for every other program, same convention as
  the existing `aNormal` binding) and explicitly disables them in every non-skinned stride branch
  (a previous skinned draw's stale enabled state would otherwise persist into later draws).
  `mat3(mat4)` truncation requires GLSL 1.20 and this file targets 1.10 everywhere else, so the
  skin matrix's rotation part is extracted manually (`mat3(skinMat[0].xyz, skinMat[1].xyz,
  skinMat[2].xyz)`) instead of bumping the shader version.
  Reused `EnvironmentMapEffect`'s `ensureDefaultWhiteTextures()` fallback (renamed from
  `ensureDefaultEnvMapTextures()` now that a second effect needs it) -- `SkinnedEffect`'s fragment
  shader always samples `uTex` unconditionally (no `uTextureEnabled` toggle, matching
  `EnsureSkinnedProgram`'s own shader), so a null `Texture` needs a real bound fallback, same
  reasoning as `EnvironmentMapEffect`.
  `OpenGL2_SkinnedEffect` verifies: per-vertex bone selection and the position transform are both
  real (a quad fully weighted to a translating bone moves off-screen, the same quad fully weighted
  to an identity bone stays put); `WeightsPerVertex` gating genuinely limits how many weight/index
  pairs are summed (derived and confirmed the single-bone-at-partial-weight "W cancels in the
  perspective divide" edge case, and the two-bone weights-sum-to-1 rigid-translation case, by hand
  before running -- both matched on the first real run); per-fragment lighting; and the
  null-Texture fallback. 7/7 PASS on the first run.

## Status: SupportsCapability real values (2026-07-19, session 6 cont'd)

- **`SupportsCapability` implemented**: previously used `IGraphicsBackend`'s blanket
  "everything supported" default. Now reports real, backend-specific values for the 3 capabilities
  that genuinely vary (matching `EasyGLGraphicsBackend`/`VulkanGraphicsBackend`'s own established
  per-capability pattern, not a speculative addition):
  - `WireFrame`: **`true`**, unlike EasyGL/GLES3 (which has no wireframe fill mode at all) --
    desktop GL 2.1's compatibility profile genuinely supports
    `glPolygonMode(GL_FRONT_AND_BACK, GL_LINE)`, already wired in `ApplyRasterizerState()`
    (verified, not just assumed, before writing this override).
  - `MultiSampleAntiAliasing`: queries `GL_MAX_SAMPLES` (`>1` -- real device/driver-dependent
    check, same technique EasyGL uses for its own equivalent GLES3 query).
  - `AnisotropicFiltering`: queries `GL_EXTENSIONS` for `GL_EXT_texture_filter_anisotropic` (GL
    2.1 predates `glGetStringi`-based extension enumeration, so this uses the classic
    space-separated single-string search).
  - Every other capability (`ThreeD`, `DepthStencilBuffer`, `MultipleRenderTargets`,
    `OcclusionQuery`, `CustomEffects`) keeps the default `true`, matching EasyGL's own defaults for
    those (this backend genuinely has real, tested support for each -- see the `OpenGL2_*` CTests
    above).
  This also fixes the one non-fixture-related failure the generic `CnaTests` gtest target had
  under this backend: `GraphicsDeviceCapabilityTest.DoesNotSupportWireFrame` is (per its own
  header comment) an EasyGL-specific assumption baked into that shared test file, not something
  meant to hold for every backend -- OpenGL2 correctly returns `true` there now, same as it always
  should have per the blanket default, and the shared gtest correctly still reports it as a
  mismatch for the reason stated in its own file (out of scope to change that shared,
  intentionally-backend-specific test).
  New `opengl2_graphics_capability_test.cpp` (twin of
  `dx3_graphics_capability_test.cpp`/`sdlrenderer_graphics_capability_test.cpp`/
  `canvas_graphics_capability_test.cpp`'s per-backend dedicated pattern, since OpenGL2 -- like
  Vulkan -- doesn't fit the generic gtest target's EasyGL-specific assumptions): asserts the real
  positive-capability set including `WireFrame=true`, and that the two device-dependent queries
  don't throw. 8/8 PASS.

## Status: ShaderEffect / CreateEffectBackend (2026-07-19, session 6 cont'd)

- **Custom `ShaderEffect` support implemented**: `CreateEffectBackend()` previously returned
  `nullptr` (default), so `ShaderEffect` silently had no backend at all. Added `EffectBackend`
  (an `IEffectBackend` implementation) that compiles a user's runtime GLSL source via the same
  `LinkProgram()` helper every built-in program uses -- which means a custom vertex shader binds
  to the SAME fixed attribute locations as everything else in this file (0=`aPosition`,
  1=`aColor`, 2=`aTexCoord`, 3=`aNormal`, 4=`aBoneWeight`, 5=`aBoneIndices`). This is a real,
  necessary divergence from `EasyGLEffectBackend`'s contract: GLSL 1.10/1.20 (this file's target)
  has no `layout(location=N)` qualifier, so unlike EasyGL's GLES3 custom shaders (which bind
  locations directly in their own source), an OpenGL2 custom shader MUST use those exact
  attribute names to receive vertex data -- documented in `EffectBackend`'s own doc comment.
  `drawInternal()` now checks `GpuDrawParams::customEffectBackend` first, before any built-in
  program selection: binds the custom program, uploads `World`/`View`/`Projection` under those
  exact XNA-HLSL-style uniform names (matching
  `EasyGLGraphicsBackend::BindCustomEffectMatrices`'s identical names), and draws through the same
  vertex layout every built-in program uses. The stride-based vertex-attribute-binding block
  (previously duplicated inline) was extracted into a shared `BindVertexAttributesForStride()`
  free function so both paths stay byte-identical by construction.
  Found and fixed a real bug during testing, not a test-authoring error: GL 2.1 has no
  `glProgramUniform*` (DSA is GL 4.1+), so plain `glUniform*` always targets whichever program is
  CURRENTLY bound via `glUseProgram`, not the program object `glGetUniformLocation` was queried
  against. The initial `SetUniformXxx()` implementations called `glUniform*` without first
  re-binding their own program, so a uniform set before the effect's first `Apply()`/`Bind()`
  call silently landed on whatever OTHER program happened to be current -- reproduced exactly by
  the first CTest run (a `uTint` set before any draw read back as pure black, since GLSL's
  default-zero uniform value multiplied the vertex color to zero). Fixed by having every
  `SetUniformXxx()` call `glUseProgram(program_)` first.
  `OpenGL2_ShaderEffect` verifies: valid/invalid compilation (`IsEffectValid()`/
  `GetCompileError()`); `World`/`View`/`Projection` wiring plus vertex-color pass-through;
  `SetUniformVec4()` actually reaching the shader (a tint uniform provably halves the output);
  and `SetTexture()`/`BindTexture()` actually reaching the shader (a custom shader samples a
  solid-color texture and reads back that exact color). 6/6 PASS.
  `SpriteBatch::SetCustomEffect()` (the 2D sprite-batch custom-shader path) was left unimplemented
  at the end of this task -- see the SpriteBatch::SetCustomEffect status entry below (implemented
  the same session, once the architectural mismatch it required working around was understood).

## Status: SpriteBatch::SetCustomEffect (2026-07-19/20, session 7)

- **`SpriteBatch::Begin(..., Effect*)` implemented**, resolving the deferral noted at the end of
  the previous session: `Sprite::Draw()`'s built-in path pre-transforms every vertex to clip
  space on the CPU (`gl_Position=vec4(aPosition,1.0)`, no projection uniform at all), which
  cannot be reused as-is for a custom effect that expects a `MatrixTransform` uniform (real XNA's
  `SpriteEffect.fx` convention -- any custom SpriteBatch effect ported from real XNA already
  declares a `float4x4 MatrixTransform` parameter; XNA throws if it's missing). Rather than
  restructuring the built-in path (risking the 13/13-passing `OpenGL2_2D` suite), added a
  SEPARATE `DrawWithCustomEffect()` path used only when `SetCustomEffect()` has set a non-null
  effect: vertex positions upload RAW (screen pixels, not pre-transformed), and `transform_`
  (`SpriteBatch.Begin`'s own transform matrix) is folded into the SAME `MatrixTransform` uniform
  as the screen->clip orthographic projection, matching real XNA's `SpriteBatch.SetupMatrix()`
  exactly -- **not** `EasyGLSpriteBatchBackend`'s own lowercase `"projection"` uniform name, which
  is a real deviation from XNA in that backend (flagged, not fixed, out of scope for this branch).
  The shared UV/rotation/flip math (previously inline in `Draw()`) was extracted into
  `ComputeSpriteScreenCorners()` so both paths stay consistent by construction.
  The texture is always bound to unit 0 with no explicit sampler-uniform requirement -- GLSL
  default-initializes an unset sampler uniform to 0, matching HLSL's implicit register-0 default
  that real XNA's `DECLARE_TEXTURE(Texture, 0)` relies on.
  When the custom effect fails to compile, the draw is skipped entirely -- deliberately NOT
  falling back to the built-in shader the way `EasyGLSpriteBatchBackend::FlushBatch()` does,
  since the two vertex conventions here are incompatible (falling back would feed raw
  screen-space positions to a shader that expects already-transformed clip-space ones, producing
  garbage rather than a plausible degraded result).
  `OpenGL2_SpriteBatchCustomEffect` verifies: `MatrixTransform` wiring + vertex-color
  pass-through + texture sampling all reach a custom shader correctly; a subsequent
  `Begin()` without an effect genuinely restores the built-in shader (not sticky). 3/3 PASS on
  the first real run.

## Status: real Letterbox/Overscan/Stretch presentation modes (2026-07-19/20, session 7 cont'd)

- **Real `CnaPresentationMode::Letterbox`/`Overscan`/`Stretch` implemented** for OpenGL2 --
  previously all three fell back to the virtual size verbatim (matching EasyGL's own documented
  gap, not an OpenGL2-specific regression). Algorithm mirrors `SdlGpuGraphicsBackend::
  ComputeLogicalViewport` exactly (the established "this backend actually does it right"
  reference elsewhere in this codebase; EasyGL's own equivalent is a documented no-op fallback,
  not a model to follow): `Letterbox` scales uniformly by `min(physW/virtW, physH/virtH)`
  (shrink-to-fit, centred, bars); `Overscan` scales by `max(...)` (grow-to-cover, centred,
  cropped); `Stretch` fills the window exactly with a non-uniform scale. Added
  `ComputeLogicalViewport()` (new `LogicalViewport{x,y,width,height,logicalWidth,logicalHeight}`
  struct) and rewired `GetViewportSize()`/`TransformWindowToLogical()`/`TransformLogicalToWindow()`
  to use it.
  **Root-caused a real, pre-existing gap in the SHARED (not backend-specific) `GraphicsDevice::
  UpdateViewportFromWindow()`**: it always called `backend_->SetViewport(0, 0, logicalWidth,
  logicalHeight, ...)` -- i.e. the actual GL/GPU viewport was hardcoded to the window ORIGIN using
  the LOGICAL size, with no way for a backend to say "the physical viewport rectangle is
  different" (offset and/or a different size). This meant even a mathematically-correct
  `ComputeLogicalViewport()` had no path to actually reach the real GL viewport -- confirmed by an
  initial implementation attempt that (correctly) computed the letterbox sub-rectangle but (still)
  failed every pixel check, because the ACTUAL `glViewport` call elsewhere in the shared code was
  never told about it. Fixed with a minimal, backward-compatible interface addition:
  `IGraphicsBackend::GetDefaultViewportRect(x,y,width,height)`, defaulting to `(0, 0,
  GetViewportSize())` -- byte-identical to every backend's behavior before this task, so EasyGL/
  Vulkan/Bgfx/SdlGpu/WebGPU/D3D9/11/12/DX3/Software etc. are all completely unaffected unless they
  choose to override it. `UpdateViewportFromWindow()` now calls this to get the PHYSICAL rectangle
  pushed to `SetViewport()`, while the LOGICAL `width`/`height` (exposed via `GraphicsDevice.
  Viewport.Width/Height`) stay untouched -- matches real XNA, where `Viewport.Width` after a
  letterboxed `Reset()` is the game's own virtual resolution, not the physical monitor's pixels.
  Also fixed a second latent bug this surfaced: `UpdateViewportFromWindow()`'s own
  "did anything change" guard only ever compared the LOGICAL size, so a window resize under
  Letterbox/Overscan (whose logical size stays FIXED at the virtual resolution, by design) would
  silently skip re-applying the changed PHYSICAL rectangle -- added `lastKnownViewportPhysX/Y/
  Width/Height_` tracking alongside the pre-existing logical-size tracking.
  Once the real `glViewport` rectangle was correct, `Sprite::Draw()`/`DrawWithCustomEffect()`'s
  CPU-side NDC math (which an earlier attempt within this same task had made letterbox-aware) was
  reverted back to its ORIGINAL simple logical-only form -- GL's own viewport transform now
  performs the logical->physical mapping automatically, for BOTH 2D SpriteBatch content and any
  DIRECT 3D draw (`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`'s standard WVP-transform vertex
  path), a broader/more correct fix achieved with LESS backend-specific code than the reverted
  sprite-only attempt.
  Three new CTests (one process per mode -- `GraphicsDevice::SetPresentationMode` is private,
  reachable only via `GraphicsDeviceManager::ApplyChanges()`, which also re-asserts window size
  from `PresentationParameters`, so mode and an independently-resized window can't both be
  exercised through a single `ApplyChanges()` call within one process; each file's own header
  comment explains this): `OpenGL2_PresentationMode_Letterbox` (centred content, real black bars
  at the physical window edges), `OpenGL2_PresentationMode_Overscan` (content fully covers the
  window, no bars, top/bottom cropped), `OpenGL2_PresentationMode_Stretch` (fills exactly via
  non-uniform scale, no bars, no cropping). 2/2 PASS each.

## Status: PbrEffect / SkinnedPbrEffect (2026-07-19/20, session 7 cont'd)

- **`PbrEffect`/`SkinnedPbrEffect` implemented**: metallic-roughness BRDF, matching
  `EasyGLGraphicsBackend::EnsurePbrProgram`/`EnsurePbrSkinnedProgram`'s formula exactly -- the
  glTF 2.0 spec's own reference BRDF (Appendix B.3.2-B.3.4: GGX/Trowbridge-Reitz normal
  distribution, Smith-Schlick-GGX visibility with direct-lighting `k=(roughness+1)^2/8`, Schlick
  Fresnel), same per-fragment TBN basis built from an interpolated vertex tangent for tangent-space
  normal mapping. Added `pbrProgram_`/`pbrSkinnedProgram_` (the skinned variant shares the SAME
  fragment shader verbatim -- only the vertex shader differs, adding the bone-palette skin
  transform from `SkinnedEffect`). `mat3(uWorld)` truncation (for the tangent's World-space
  transform) hit the same GLSL-1.20-vs-1.10 issue `SkinnedEffect`'s skin matrix did -- same manual
  3x3-extraction workaround.
  New vertex layout support for `VertexPositionNormalTangentTexture` (stride 48) and its
  stride-68 skinned variant, added as new attribute location 6 (`aTangent`), following the same
  "always bind this location globally, harmless no-op for every other program" convention as
  locations 4/5.
  **Found and fixed a real, pre-existing gap this surfaced**: `VertexBuffer` had NO typed
  `SetData`/`GetData` overload at all for either new vertex type (only the generic, non-owning
  `SetDataRaw(const void*, count, stride)` existed) -- meaning a real game constructing a
  `PbrEffect`/`SkinnedPbrEffect` mesh via the standard, idiomatic `VertexBuffer::SetData(array,
  count)` API used by every OTHER vertex type in this codebase would simply fail to compile.
  Confirmed by hitting the exact same wall the first test-writing attempt did. Fixed by adding the
  full 8-method set (`SetData(count)`/`SetData(startIndex,elementCount)`/`GetData(count)`/
  `GetData(startIndex,elementCount)` x2 types) in the SHARED `VertexBuffer.hpp`/`.cpp`, mirroring
  `VertexPositionNormalTextureSkinned`'s own established "pack into a compact GpuVertex struct,
  strip the `IVertexType` vtable pointer" pattern exactly (confirmed via `sizeof()`: the public
  XNA-facing struct is 56/80 bytes due to that vtable pointer + alignment padding, vs. the
  GPU-compact 48/68 bytes every backend's stride dispatch expects -- the SAME reason every other
  typed `VertexBuffer::SetData` overload exists in the first place, not something newly invented
  here). Purely additive (new overloads only) -- every other backend/caller is unaffected.
  Added five default-fallback textures reused for `PbrEffect`'s always-on sampling (matching the
  established `EnvironmentMapEffect`/`SkinnedEffect` precedent): `MetallicRoughnessMap`/
  `EmissiveMap`/`OcclusionMap` default to white ("1.0 when absent", matching
  `GpuDrawParams::pbrMetallicFactor`'s own doc comment), and a NEW default flat-normal texture
  ((128,128,255,255), decodes to tangent-space (0,0,1) -- "no perturbation") for `NormalMap`.
  `OpenGL2_PbrEffect` verifies: per-fragment lighting response; NormalMap perturbation actually
  changes the lit result; EmissiveFactor is additive and lighting-independent; OcclusionMap
  darkens the ambient term; all five maps' null fallbacks work without crashing; and
  `SkinnedPbrEffect`'s bone-palette position skinning combined with the PBR shader. 7/7 PASS.

## Status: Windows GL 2.x entry-point loader (2026-07-19/20, session 7 cont'd)

- **Windows GL entry-point loading implemented and cross-compile verified.** The real, non-theoretical
  problem: `opengl32.dll` on Windows only ever exports the ~350 GL 1.1 entry points (Microsoft
  never updated its own ICD dispatch table past that) -- everything this file calls beyond GL 1.1
  (buffer objects/GL 1.5, shaders/GL 2.0, multitexture/GL 1.3, framebuffer objects/
  `ARB_framebuffer_object`, even `glTexImage3D`/`glTexSubImage3D` despite being spec-core since
  GL 1.2) must be resolved at runtime via `wglGetProcAddress` there, or the Windows link step
  fails outright with unresolved externals -- `GL_GLEXT_PROTOTYPES`'s plain `extern`/`GLAPI`
  declarations (used unchanged on every other platform) only link because Linux/Mesa/GLX (and
  other desktop GL ICDs) export these symbols directly for static linking, a convenience specific
  to those platforms' loader model, not something Windows' `opengl32.dll` provides.
  Fixed with a `#if defined(_WIN32)` branch: ~54 file-local `PFNGLxxxPROC` function-pointer
  variables (the STANDARD Khronos typedefs from `glext.h`/`SDL3/SDL_opengl_glext.h`, not
  hand-rolled signatures) plus `LoadWin32GLExtensions()`, called once via
  `SDL_GL_GetProcAddress()` right after `SDL_GL_MakeCurrent()` in the constructor, before
  `ensurePrograms()`. The ~30 GL 1.1-safe functions this file also uses (`glClear`, `glViewport`,
  `glDrawArrays`, `glTexImage2D`, etc.) are left untouched, linking directly against
  `opengl32.lib` as before -- only genuinely-beyond-1.1 entry points are routed through the
  loader. Every other platform's build is byte-for-byte unaffected (the `#else` branch is the
  exact pre-existing `GL_GLEXT_PROTOTYPES`+direct-link code).
  **This sandbox has no Windows toolchain, but MinGW-w64 (`x86_64-w64-mingw32-g++`/
  `i686-w64-mingw32-g++`) turned out to be installed** -- used it to cross-compile
  `OpenGL2GraphicsBackend.cpp` standalone (real Windows headers via mingw's bundled Win32 SDK,
  real SDL3 headers, real x86_64/i686 COFF object-file output) for BOTH 32- and 64-bit Windows
  targets: zero errors on either. Inspected the resulting object files' symbol tables directly
  (`x86_64-w64-mingw32-nm`) to confirm the fix is structurally real, not just "compiles": GL 1.1
  functions (`glClear`, `glDrawArrays`, `glViewport`, ...) show up as `__imp_glClear`-style DLL
  import references (the standard Windows convention for symbols `opengl32.lib` resolves at link
  time); every extension function (`glGenBuffers`, `glCreateShader`, `glActiveTexture`, ...) shows
  up as a local BSS data symbol in this file's own anonymous namespace instead -- exactly the
  split the fix is supposed to produce, confirmed by direct inspection rather than assumed.
  This is a real, meaningful upgrade over "written carefully, hoped for the best" -- compile- and
  link-boundary-level correctness is now genuinely verified -- but it is still NOT a substitute
  for an actual Windows runtime test: `wglGetProcAddress`'s real behavior, actual GL driver
  entry-point availability, and end-to-end rendering on Windows remain unverified and should be
  the first thing checked on a real Windows machine before relying on this backend there.

## Status: cross-backend golden-image visual parity (2026-07-19/20, session 7 cont'd)

- **A first genuine cross-backend pixel-level visual-parity test added.** Every prior
  cross-backend check in this branch (e.g. `OpenGL2_EnvironmentMapEffect`'s Check C) compared a
  single centre pixel against an independently-DERIVED numeric expectation, not another backend's
  actual rendered output. `opengl2_environmentmapeffect_golden_test.cpp` instead reuses
  `examples/easygl_environmentmapeffect_golden_test.cpp`'s own combined EnvironmentMapEffect scene
  VERBATIM (identical texture, cube map, matrices, effect parameters) and calls
  `PixelTestGame::CompareGoldenImage()` against THAT test's own checked-in golden PNG
  (`examples/golden/easygl_environmentmapeffect_golden_test.png`, originally captured from the
  EasyGL/GLES3 backend) -- an 8x8 pixel region, tolerance=20 (matching the golden PNG's own
  established Task 462 project-wide finding: exact pixel-perfect reproduction across this
  project's backends/drivers isn't realistic). PASS: this software-rasterizer (llvmpipe), direct
  GL 2.1 shader render genuinely matches a completely different backend's (GLES3, EasyGL's own
  shader family) rendered pixels for the same scene, not just the same derived number.
  Needed `WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"` on the CTest registration (missing from
  every other OpenGL2 test so far since none of them load a file from a relative path) -- found by
  checking how `EasyGLTests.cmake` itself registers the same golden-image pattern, since ctest's
  default working directory is the build tree, not the source tree the relative golden-PNG path
  assumes.
  Scoped deliberately to this one scene at first -- then, since the pattern proved out cleanly,
  extended to three more of this project's existing `*_golden_test.cpp`/PNG pairs, reused
  verbatim the same way: `opengl2_basiceffect_golden_test.cpp` (`TextureEnabled`+
  `VertexColorEnabled`, `DiffuseColor`+`EmissiveColor`, constant-UV top-left-texel sample,
  tolerance=8), `opengl2_alphatesteffect_golden_test.cpp` (`CompareFunction::Greater` above
  reference -> drawn not discarded, tolerance=20), and `opengl2_skinnedeffect_golden_test.cpp`
  (bone-palette identity-transform quad + `EnableDefaultLighting()`'s real Phong rig,
  tolerance=40). All three PASS -- both their own independently-derived/observed `ExpectPixel`
  cross-check AND the actual golden-PNG region comparison, on the first real run. A genuinely
  broader sweep across this project's dozens of other `*_golden_test.cpp` pairs (BasicEffect
  lighting variants, fog, DualTextureEffect, PbrEffect's 4 golden images, ...) remains real,
  further, separate effort -- not attempted beyond these four representative scenes (one per
  major already-implemented effect family: reflection mapping, unlit texture/vertex-color
  compositing, alpha discard, and skinning+lighting).

## Status: full custom VertexDeclaration support (2026-07-19/20, session 7 cont'd)

- **Full custom `VertexDeclaration` support implemented -- this backend's own follow-up note
  claiming this "means widening `IGraphicsBackend`'s shared interface" turned out to be WRONG**:
  re-reading `IVertexBufferBackend` closely (while re-checking the follow-up list before declaring
  it done) found `SetVertexDeclaration(const std::vector<VertexElement>&)` already exists there
  (Task 1080, default no-op), called by `VertexBuffer::SetDataRaw()` before every upload -- no
  interface change was actually needed, only an OpenGL2-side implementation. `EasyGLGraphicsBackend`
  already has a real one (confirmed by reading it): binds `location = element's own index in the
  declaration list`, which works there because GLES3 lets ANY shader (built-in or a custom
  `ShaderEffect`) declare `layout(location=N)` directly in its own source, independent of
  attribute name.
  GLSL 1.10/1.20 (this file's target throughout) has no `layout(location=N)` qualifier, so that
  exact scheme doesn't transfer -- instead, `BindVertexAttributesForDeclaration()` derives a
  conventional attribute NAME from each element's `VertexElementUsage`/`UsageIndex` (Position->
  `aPosition`, Normal->`aNormal`, TextureCoordinate->`aTexCoord`(N), Color->`aColor`,
  BlendWeight->`aBoneWeight`, BlendIndices->`aBoneIndices`, Tangent->`aTangent`, ... -- the SAME
  names `LinkProgram()` already globally binds to fixed locations 0-6 for every built-in program)
  and looks up WHERE the currently-bound program (built-in or a custom `ShaderEffect`, reached via
  a `dynamic_cast<EffectBackend*>` + new `GetProgramHandle()` accessor, same reasoning as
  `EasyGLSpriteBatchBackend::FlushBatch()`'s own identical downcast) actually put that name via
  `glGetAttribLocation()` -- an element whose derived name a given program doesn't declare is
  silently skipped (matches `glGetAttribLocation` returning -1 for an unused attribute, not an
  error). `VertexElementFormat`'s `Byte4` case reads as an unnormalized unsigned-byte-to-float
  attribute (not a true integer one -- `glVertexAttribIPointer` is GL 3.0+), same convention
  already established for `aBoneIndices` throughout this file.
  Both `drawInternal()` call sites (the built-in-program path and the `customEffectBackend` path)
  now check `vb->declaration.empty()` first and dispatch to this generic path instead of the
  fixed-stride one when a real declaration was supplied -- falling back to the pre-existing,
  unchanged stride dispatch otherwise, so every existing test/scene keeps working exactly as
  before. Added `glGetAttribLocation` to the Windows entry-point loader (missed in the original
  pass -- this is the first place in the file that calls it) and re-verified the MinGW-w64
  cross-compile still succeeds with the addition.
  `OpenGL2_CustomVertexDeclaration` verifies a deliberately REORDERED layout (Color first, then
  Position, then TextureCoordinate -- same total 24-byte size as the built-in
  `VertexPositionColorTexture` stride, a deliberate differentiator: if the declaration path were
  accidentally bypassed in favour of the old fixed-offset stride-24 dispatch, it would misread the
  Color bytes as Position floats and produce degenerate/absent geometry instead of a correct
  quad), combined with a custom `ShaderEffect`, renders correctly. 2/2 PASS on the first real run.

### Verified working
- `cmake/Tests/OpenGL2Tests.cmake` registers twenty-four CTests (Xvfb, `SDL_VIDEODRIVER=x11`):
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
  - `OpenGL2_RenderTargetCube` -- per-face FBO, depth occlusion, mipmap generation. 5/5 PASS.
  - `OpenGL2_EnvironmentMapEffect` -- reflection blend, per-vertex Fresnel suppression, correct
    cube face sampled, cross-backend consistency vs the EasyGL golden scene, default-white
    sampler fallback. 5/5 PASS.
  - `OpenGL2_SkinnedEffect` -- bone-palette position/normal skinning, `WeightsPerVertex` gating,
    per-fragment lighting, default-white sampler fallback. 7/7 PASS.
  - `OpenGL2_GraphicsCapability` -- real `SupportsCapability` values (`WireFrame=true`,
    device-dependent queries don't throw). 8/8 PASS.
  - `OpenGL2_ShaderEffect` -- custom runtime-compiled GLSL: valid/invalid compilation,
    World/View/Projection wiring, uniform-by-name binding, texture-unit binding. 6/6 PASS.
  - `OpenGL2_SpriteBatchCustomEffect` -- custom sprite shader via `MatrixTransform`, texture-unit-0
    binding, built-in shader restored after a plain `Begin()`. 3/3 PASS.
  - `OpenGL2_PresentationMode_Letterbox`/`_Overscan`/`_Stretch` -- real physical-viewport scaling
    per mode (bars / full-coverage-cropped / non-uniform-stretch). 2/2 PASS each.
  - `OpenGL2_PbrEffect` -- metallic-roughness BRDF, tangent-space normal mapping,
    emissive/occlusion maps, SkinnedPbrEffect bone-palette skinning. 7/7 PASS.
  - `OpenGL2_EnvironmentMapEffect_Golden`/`_BasicEffect_Golden`/`_AlphaTestEffect_Golden`/
    `_SkinnedEffect_Golden` -- genuine cross-backend pixel comparisons against EasyGL's own
    checked-in golden PNGs for each identical scene. PASS x4.
  - `OpenGL2_CustomVertexDeclaration` -- a reordered, non-fixed-stride vertex layout bound
    generically by attribute name, combined with a custom `ShaderEffect`. 2/2 PASS.
- The pre-existing `examples/demo_2d` app (`cna_demo_2d`, window title "CNA 2D Demo") builds and
  runs end-to-end against this backend: real PNG texture load, ~50-100 animated rotating/scaling
  alpha-blended sprites, audio, `--smoke N` clean exit. Screenshot captured via a temporary
  backbuffer dump confirms correct rendering (clean sprite edges, no blend-fringing artifacts).
- Full `CNA` + `CnaTests` + example suite builds clean under `-DCNA_GRAPHICS_BACKEND=OPENGL2`
  except `cna_demo_xact`, which fails only at its Content-copy POST_BUILD step because
  `examples/demo_xact/Content/` does not exist in this checkout at all (never committed to git) --
  pre-existing, unrelated to this backend or this branch.
- The generic (backend-agnostic) `CnaTests` gtest target run in full under this build (5521
  cases) has 224 pre-existing failures, none caused by this backend: ~220 are missing local test
  fixture files (media/audio/video/XNB content assets absent from this checkout, e.g.
  `tests/assets/xnb/monogame/windows/uncompressed/audio/tone_mono_44khz_16bit`) that fail
  identically regardless of graphics backend, and
  `GraphicsDeviceCapabilityTest.DoesNotSupportWireFrame` is a pre-existing EasyGL-specific
  assumption baked into that test file's own header comment ("this test target only ever builds
  against a fully 3D-capable backend (EasyGL by default on Linux)") -- it fails because OpenGL2's
  `SupportsCapability` still uses the blanket "everything supported" default (see the
  already-tracked follow-up item below), not because of any regression introduced here.

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
- `TextureCube` (`GL_TEXTURE_CUBE_MAP`, see `OpenGL2_TextureCube` above), now sampled by
  `EnvironmentMapEffect` (see below).
- `Texture3D` (`GL_TEXTURE_3D`, see `OpenGL2_Texture3D` above); level 0 only, no mip chain yet.
- `RenderTargetCube` (see `OpenGL2_RenderTargetCube` above); single-sample only, no MSAA. Usable
  as an `EnvironmentMapEffect` source too (`IRenderTargetCubeBackend` extends
  `ITextureCubeBackend`, so a dynamically-rendered reflection is a real, tested code path).
- RenderTarget2D/FBO, including MSAA (resolve-on-unbind) and mipmap generation.
- AlphaTestEffect, DualTextureEffect, and BasicEffect lighting (3 directional lights, ambient,
  specular, emissive) + fog (see `OpenGL2_Effects` above).
- EnvironmentMapEffect (reflection mapping, per-vertex Fresnel, specular tint; see
  `OpenGL2_EnvironmentMapEffect` above).
- SkinnedEffect (bone-palette vertex skinning, `WeightsPerVertex` gating; see
  `OpenGL2_SkinnedEffect` above).
- Real `SupportsCapability` values for `WireFrame`/`MultiSampleAntiAliasing`/
  `AnisotropicFiltering` (see `OpenGL2_GraphicsCapability` above).
- Custom `ShaderEffect`/`CreateEffectBackend` (runtime-compiled user GLSL) for both the direct 3D
  draw path (see `OpenGL2_ShaderEffect` above) and `SpriteBatch::Begin(..., Effect*)` (see
  `OpenGL2_SpriteBatchCustomEffect` above).
- Real `CnaPresentationMode::Letterbox`/`Overscan`/`Stretch` (physical viewport scaling/bars/crop,
  not just a virtual-size fallback; see `OpenGL2_PresentationMode_*` above). This surfaced and
  fixed a real gap in the SHARED `GraphicsDevice::UpdateViewportFromWindow()` too (see that status
  entry above) via the new `IGraphicsBackend::GetDefaultViewportRect()` method -- every other
  backend is unaffected (its default reproduces their exact prior behavior) unless it chooses to
  override it for its own real letterboxing.
- PbrEffect/SkinnedPbrEffect (metallic-roughness BRDF, tangent-space normal mapping; see
  `OpenGL2_PbrEffect` above). Also added the previously-missing `VertexBuffer::SetData`/`GetData`
  typed overloads for `VertexPositionNormalTangentTexture(Skinned)` (a real, shared gap this
  surfaced -- see that status entry above), so this is usable through the standard `VertexBuffer`
  API like every other vertex type, not just via the raw/untyped path.
- Windows GL entry-point loading (`wglGetProcAddress` via `SDL_GL_GetProcAddress`, ~55 functions
  beyond GL 1.1); MinGW-w64 cross-compile-verified, not yet Windows-runtime-tested (see the status
  entry above).
- Full custom `VertexDeclaration` support (attribute-name-driven generic binding via
  `glGetAttribLocation`, not just the fixed byte-stride table; see `OpenGL2_CustomVertexDeclaration`
  above) alongside the pre-existing stride-inferred fast path, which every existing test/scene
  still uses unchanged.
- No EasyGL dependency.

## Follow-up work (toward EasyGL feature parity, within OpenGL 2.1's real capabilities)
- Windows GL 2.x entry-point loader (see the status entry above) is implemented and
  MinGW-w64 cross-compile-verified (both 32- and 64-bit targets, symbol-table-inspected), but
  still NOT tested on an actual Windows runtime -- `wglGetProcAddress`'s real behavior, actual GL
  driver entry-point availability, and end-to-end rendering on Windows remain to be checked on a
  real machine.
- Visual parity tests against other backends (golden-image comparison) beyond the four scenes now
  covered (`OpenGL2_EnvironmentMapEffect_Golden`/`_BasicEffect_Golden`/`_AlphaTestEffect_Golden`/
  `_SkinnedEffect_Golden`, see the status entry above) -- this project has dozens of
  `*_golden_test.cpp`/checked-in PNG pairs across its other backends (more `BasicEffect` lighting
  variants, fog, DualTextureEffect, `BlendState`/`RasterizerState`/`DepthStencilState` scenes,
  PbrEffect's 4 golden images, texture-filter/sampler-state scenes, ...). Each additional scene is
  mechanically similar to the four already ported (reuse the existing golden PNG + its exact scene
  setup verbatim) but real, incremental effort per scene, not a single remaining task.
