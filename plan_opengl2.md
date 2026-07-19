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

### Verified working
- `cmake/Tests/OpenGL2Tests.cmake` registers four CTests (Xvfb, `SDL_VIDEODRIVER=x11`):
  - `OpenGL2_Smoke` -- window/GL-context lifecycle, VertexBuffer/16-bit/32-bit IndexBuffer
    round-trips, 60 frames of Clear+Present. 7/7 PASS.
  - `OpenGL2_2D` -- real `Texture2D` + `SpriteBatch`, pixel-verified via `ReadBackbuffer`:
    quadrant UV mapping (no flip bug), an opaque draw, `BlendState::AlphaBlend` factor
    correctness, `transformMatrix` translation, `SetSamplerAddressMode` wrap/clamp, rotation/
    both-flip draws, 120 stable frames. 13/13 PASS.
  - `OpenGL2_3D` -- colored3d/textured3d/colored_textured3d + real depth-test occlusion, all
    pixel-verified. 6/6 PASS.
  - `OpenGL2_RenderTarget2D` -- FBO construction, `GetData()` readback, RT-as-Texture2D via
    SpriteBatch, depth occlusion inside the FBO, RT-vs-window sizing. 6/6 PASS.
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
- Depth, stencil, blending, culling, scissor, viewport, wireframe and polygon offset basics.
- `ReadBackbuffer` (pixel readback, enables automated pixel-exact testing).
- RenderTarget2D/FBO (single-sample, single-mip-level; see Follow-up for MSAA/mipmaps).
- No EasyGL dependency.

## Follow-up work
- `GetViewportSize()`/`SetViewport()` do not implement `CnaPresentationMode` scaling
  (Letterbox/Overscan/Stretch/FixedHeightDynamicWidth): the virtual resolution is used verbatim
  as the GL viewport size regardless of the actual window's aspect ratio, so a *resizable* window
  whose live size diverges from the game's requested back-buffer size will not letterbox/scale
  correctly (EasyGL's `getLogicalSize()` is the reference behavior to match). Not a problem for a
  fixed-size window matching its requested resolution (the common case, and what both CTests and
  `examples/demo_2d` use).
- Full vertex declaration support rather than stride inference.
- BasicEffect lighting/fog parity and multiple directional lights.
- AlphaTestEffect and DualTextureEffect.
- RenderTarget2D: MSAA (`multiSampleCount` is accepted but ignored -- no multisampled
  renderbuffer + resolve-on-unbind yet, unlike `EasyGLRenderTargetBackend`'s), mipmap generation
  (`mipMap` is accepted but ignored -- no `glGenerateMipmap` on unbind), and `RenderTargetCube`
  (only `RenderTarget2D` is implemented; `CreateRenderTargetCube` still returns `nullptr`).
- Cube maps (plain `TextureCube`, not just render-target cube faces) and an EnvironmentMapEffect
  subset where supported by OpenGL 2.1/extensions.
- `ApplySamplerState` (the direct-3D-draw sampler path, as opposed to `SpriteBatch`'s own
  `SetSamplerFilter`/`SetSamplerAddressMode` which are now wired) is still unimplemented --
  `GraphicsDevice.SamplerStates[slot]` has no effect on `DrawPrimitives`/`DrawIndexedPrimitives`.
- Robust extension/capability detection.
- Windows GL 2.x entry-point loader validation; current direct prototypes are primarily intended for Linux desktop builds.
- Visual parity tests against other backends (golden-image comparison).
