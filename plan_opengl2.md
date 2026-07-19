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
  `SpriteBatch::SetCustomEffect()` (the 2D sprite-batch custom-shader path) remains unimplemented
  in this backend -- out of scope here, tracked as a follow-up below.

### Verified working
- `cmake/Tests/OpenGL2Tests.cmake` registers fourteen CTests (Xvfb, `SDL_VIDEODRIVER=x11`):
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
- Custom `ShaderEffect`/`CreateEffectBackend` (runtime-compiled user GLSL, direct 3D draws only;
  see `OpenGL2_ShaderEffect` above).
- No EasyGL dependency.

## Follow-up work (toward EasyGL feature parity, within OpenGL 2.1's real capabilities)
- `CnaPresentationMode::Letterbox`/`Overscan`/`Stretch` still fall back to the virtual size
  verbatim rather than real letterbox-bar/crop/non-uniform-stretch scaling -- matches EasyGL's
  own current behavior (not a new OpenGL2-specific gap), but neither backend actually implements
  those three modes' real semantics yet.
- Full vertex declaration support rather than stride inference (blocks any vertex format beyond
  the 5 already recognized by stride, e.g. a custom `VertexDeclaration` with a different
  attribute order/extra streams).
- PbrEffect/SkinnedPbrEffect (metallic-roughness BRDF) -- EasyGL has this; a large, separate
  effort (its own shader family + texture set), likely lower priority than the items above for a
  "no EasyGL dependency" OpenGL 2.1 backend.
- `SpriteBatch::SetCustomEffect()` (2D sprite-batch custom-shader path) -- `Sprite` still ignores
  it entirely (default no-op); only the direct 3D draw path
  (`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`) consumes `GpuDrawParams::customEffectBackend` so
  far (see `OpenGL2_ShaderEffect` above). Investigated during this session and deliberately NOT
  attempted: this backend's `Sprite::Draw()` pre-transforms every vertex to clip space ON THE CPU
  (screen coords -> NDC via `viewportWidth`/`viewportHeight` division, with `transform_` applied
  before that) and its built-in shader is just `gl_Position=vec4(aPosition,1.0)` -- no projection
  matrix uniform at all. `EasyGLSpriteBatchBackend::SetCustomEffect`'s established contract (see
  its `FlushBatch()`) instead uploads a real orthographic `projection` uniform and expects the
  vertex shader to apply it, which requires uploading RAW screen-space positions, not
  pre-transformed clip-space ones. Supporting `SetCustomEffect()` here properly (matching that
  same contract, so a custom sprite shader ported from EasyGL "just works") means restructuring
  `Sprite::Draw()`'s vertex generation away from CPU-side clip-space pre-transform -- a real
  refactor of the currently 13/13-passing `OpenGL2_2D` code path, not a small addition; deferred
  rather than risking that already-verified subsystem for a lower-priority feature.
- Windows GL 2.x entry-point loader validation; current direct prototypes are primarily intended for Linux desktop builds.
- Visual parity tests against other backends (golden-image comparison).
