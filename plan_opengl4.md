# OpenGL 4 Graphics Backend — Implementation Plan

> **Status (2026-07-21): Phase 1 (`GL4-1`–`GL4-13`) landed and verified.** `CNA_GRAPHICS_BACKEND=OPENGL4`
> configures, builds (`cna_backend_graphics_opengl4`), and a real window with a real desktop
> `SDL_GL_CONTEXT_PROFILE_CORE` context (confirmed via `glGetString(GL_VERSION)` reporting
> `4.5 (Core Profile) Mesa 25.2.8` on this dev machine's llvmpipe/Mesa driver under Xvfb) clears
> color/depth/stencil, uploads a real `Texture2D`, draws a real `SpriteBatch` scene (tint, alpha,
> rotation/flip, source-rectangle cropping, and all three sampler address modes — Wrap/Clamp/
> Mirror), and draws real 3D geometry through `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`'s
> stride-keyed dispatch: `colored3d` (`VertexPositionColor`/`BasicEffect.VertexColorEnabled`,
> with a genuine depth-test occlusion proof verified two ways/draw-order-independent),
> `textured3d` (`VertexPositionTexture`/`BasicEffect.TextureEnabled`, real texture sampling, not a
> diffuse-only fallback), `colored_textured3d` (`VertexPositionColorTexture`, vertex-color tint
> multiplying the sampled texture), and `lit_textured3d` (`VertexPositionNormalTexture`/
> `BasicEffect.EnableDefaultLighting()`, real ambient+Lambertian-diffuse+Blinn-Phong-specular
> lighting, proven to actually differ from the unlit render). Verified by `OpenGL4_Smoke` (8/8),
> `OpenGL4_Readback` (10/10), `OpenGL4_3D` (4/4), and `OpenGL4_Textured3D` (5/5) — all real
> pixel-readback assertions via `GraphicsDevice.GetBackBufferData()`, not just "didn't throw" —
> `ctest -R OpenGL4`, all pass. Two real bugs were found and fixed while getting there — see
> `GL4-7`/`GL4-9`'s rows below.
>
> **Status (2026-07-22): `GL4-14` (`RenderTarget2D`, real FBO) landed and verified.**
> `OpenGL4RenderTargetBackend` is a real FBO with a colour texture attachment, an optional
> depth/stencil renderbuffer (`Depth16`/`Depth24`/`Depth24Stencil8`, exactly the format
> requested — `DepthFormat::None` omits the attachment entirely), an optional multisampled colour
> (and depth) renderbuffer resolved into the single-sample colour texture via
> `glBlitFramebuffer` on unbind, and an optional mip chain regenerated via `glGenerateMipmap` on
> unbind — modeled directly on `EasyGLRenderTargetBackend`'s own resource shape but using raw
> `GL4Loader` calls instead of the `easygl::` wrapper types this backend deliberately avoids.
> `RenderTarget2D::GetData()` is real (`OpenGL4RenderTargetBackend::GetData()`, via a throwaway
> per-level read FBO), not the EasyGL-style gap the "Remaining work" section used to note for
> this backend's peers. Two real, non-hypothetical bugs were found and fixed while wiring this
> up, both from the exact same root cause (code that assumed "the currently bound target is
> always the real backbuffer"): `SetViewport`'s bottom-left-to-top-left Y flip was hardcoded to
> the window's physical height, which is wrong once an FBO smaller than the window is bound (now
> keyed off a new `currentRtHeight_` member, mirroring `EasyGLGraphicsBackend`'s identical
> pattern); and `OpenGL4SpriteBatchBackend::FlushBatch`'s viewport/ortho sizing had the same
> window-size-only assumption, which would have silently broken any `SpriteBatch::Draw()` issued
> while a render target was bound (now checks a new `GetCurrentRenderTarget2DSize()` accessor
> first). Verified by `OpenGL4_RenderTarget2D` (12/12: Clear-only/colored3d/depth-tested draws
> sampled back via `SpriteBatch`, `MultiSampleCount` property fidelity, real `GetData()` pixel
> reads on all three, a mipMap round-trip, a real MSAA round-trip through the
> `glBlitFramebuffer` resolve path, and a `SpriteBatch::Draw()`-into-a-bound-RT check that
> specifically exercises the `FlushBatch` fix) plus a full re-run of `OpenGL4_Smoke` (8/8),
> `OpenGL4_Readback` (10/10), `OpenGL4_3D` (4/4), and `OpenGL4_Textured3D` (5/5) confirming no
> regression from the shared `SetViewport`/`FlushBatch` changes. `RenderTargetCube`/MRT are not
> part of this task — see "Remaining work" below.
>
> **Status (2026-07-22): `GL4-15` (`RenderTargetCube` + real MRT) landed and verified.**
> `OpenGL4RenderTargetCubeBackend` is one shared cube-map texture with a single FBO re-attaching
> whichever face (`0`=+X..`5`=-Z) is currently bound, the same depth/stencil-renderbuffer/MSAA-
> resolve/mip-regen machinery as `GL4-14`'s 2D target, and a real `GetData()` per face+level via a
> throwaway read FBO — modeled directly on `EasyGLRenderTargetCubeBackend`. `SetRenderTargets`
> (plural) is real MRT: a lazily-created, persistent FBO with one `glFramebufferTexture2D`
> attachment per target at `GL_COLOR_ATTACHMENT0+i` and a real `glDrawBuffers` call — not the
> inherited single-target-only default. No depth attachment for MRT (same accepted, documented gap
> `EasyGLGraphicsBackend`'s own MRT FBO already has), and no multi-output shader variant exists
> yet (this backend's `colored3d`/`textured3d`/etc. programs all declare a single `fragColor`
> output, so only `COLOR_ATTACHMENT0` receives a draw under MRT — verified explicitly, not glossed
> over, by `OpenGL4_RenderTargetCube_MRT`'s own Check H). Verified by
> `OpenGL4_RenderTargetCube_MRT` (13/13: two independent cube faces proven not to alias each
> other, a real colored3d draw into a face, a depth-tested face, `MultiSampleCount` fidelity, a
> mipMap round-trip, a real MSAA round-trip, and the MRT slot-0-vs-slot-1 independence proof) plus
> a full re-run of `OpenGL4_Smoke`/`OpenGL4_Readback`/`OpenGL4_3D`/`OpenGL4_Textured3D`/
> `OpenGL4_RenderTarget2D` confirming no regression from `SetRenderTarget2D`'s new
> `currentRtCube_` unbind check.
>
> **Status (2026-07-22): `GL4-16` (dynamic `BlendState`/`DepthStencilState`/`RasterizerState`)
> landed and verified.** `ApplyBlendState`/`ApplyDepthStencilState`/`ApplyRasterizerState`/
> `SetBlendFactor`/`SetScissorRect` are real now — blend factors/equations
> (`glBlendFuncSeparate`/`glBlendEquationSeparate`), `glBlendColor`, real depth+stencil test state
> including two-sided stencil (`glStencilFuncSeparate`/`glStencilOpSeparate`/
> `glStencilMaskSeparate`, `GL4Loader` gained these 3 GL-2.0 entry points), cull mode
> (`glCullFace`/`glFrontFace`), scissor test, and wireframe fill mode (`glPolygonMode`, a real
> desktop-GL capability EasyGL's ES target has to fake by re-expanding triangles into
> `GL_LINES` at draw time — this backend doesn't need that workaround). Since every
> `GraphicsDevice` applies its own default `RasterizerState`/`BlendState`/`DepthStencilState`
> automatically at construction (matching real XNA, not just when a game explicitly assigns one),
> turning this on for real immediately exposed that `GL4-9`/`GL4-13`'s own pre-existing test files
> (`opengl4_3d_test.cpp`, `opengl4_textured3d_test.cpp`) had never been exercised under real
> culling and used quad windings that XNA's actual default (`CullCounterClockwiseFace`) culls —
> both fixed with an explicit `RasterizerState::CullNone` opt-out, the same established idiom
> already used by e.g. `bgfx_basiceffect_texture_enabled_test.cpp` for the identical reason (not a
> new pattern invented here). The new test itself needed two of its own genuine authoring
> corrections before it reflected real behavior: `BlendState::AlphaBlend` assumes an
> already-premultiplied source colour (`One`/`InverseSourceAlpha`) and was misused for a
> straight-alpha blend check (fixed by switching to `BlendState::NonPremultiplied`,
> `SourceAlpha`/`InverseSourceAlpha`), and a wrong assumption that this codebase's `Color::Green`
> is `(0,255,0)` rather than real XNA's `(0,128,0)` (`Lime` is the pure-green one) skewed an
> expected blend result. Verified by the new `OpenGL4_RenderState` CTest (12/12: `BlendState`
> preset/custom/`SetBlendFactor` checks, a `DepthStencilState`-object depth-test check, a real
> 2-pass stencil-buffer check, `CullMode` checked against
> `easygl_rasterizerstate_cullmode_test.cpp`'s own already-empirically-verified triangle winding,
> scissor-rect gating, and wireframe fill mode), plus a full re-run of the other 6 OpenGL4 CTest
> suites confirming everything (including the 2 fixed pre-existing test files) is green.
>
> **Status (2026-07-22): `GL4-17` (real backbuffer MSAA) landed and verified.**
> `GraphicsBackendCreateArgs::multiSampleCount` is honored now, via a manually-managed multisample
> FBO (`msaaFbo_`/`msaaColorRbo_`/`msaaDepthRbo_`, a real `GL_DEPTH24_STENCIL8` combined depth+
> stencil attachment so `GL4-16`'s real stencil test doesn't silently break under backbuffer MSAA)
> resolved into FBO 0 via `glBlitFramebuffer` before `Present()`/`ReadBackbuffer()` — the same
> `CreateMsaaBuffers`/`ResolveMsaa`/`BindDefaultFramebuffer` shape `EasyGLGraphicsBackend` already
> uses for its own backbuffer MSAA, deliberately chosen over
> `SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, ...)` (a window pixel-format request this
> backend could not resolve through its own controlled `glBlitFramebuffer` call, and which would
> fight the existing Y-flip/`ReadBackbuffer` conventions). Fixed at backend-construction time only
> — `ApplyMultiSampleCount` is not overridden, the same documented "no way to change after
> construction" limitation `EasyGLGraphicsBackend` already has. `GetMultiSampleCount()` (the
> top-level `IGraphicsBackend` one) now reports the real, `GL_MAX_SAMPLES`-clamped value. Verified
> by the new `OpenGL4_MSAA` CTest (3/3): a diagonal-edge-triangle differential test (matching this
> project's own established MSAA methodology — a solid-fill readback alone cannot distinguish
> "MSAA happened" from "MSAA was silently ignored") shows a hard binary transition with MSAA off
> and genuinely blended intermediate pixels for the identical geometry after
> `GraphicsDevice::RecreateBackendForMultiSampleCount(8)` (the same NOXNA test-only escape hatch
> Vulkan's own pre-Task-902 MSAA tests used, since `GraphicsDeviceManager.PreferMultiSampling` set
> in a `Game` subclass's constructor never reaches the *first* backend construction at all — a
> documented, codebase-wide `GraphicsDevice` architectural constraint, not an OpenGL4-specific
> gap). Plus a full re-run of the other 7 OpenGL4 CTest suites confirming no regression.
>
> **Status (2026-07-22): `GL4-18` (real `Texture2D` mip levels) landed and verified.**
> `OpenGL4TextureBackend::UpdatePixelsLevel()` is real now — `Texture2D::SetData(level, ...)` for
> `level>0` previously reached an unoverridden no-op, silently discarding every mip level beyond
> 0. `GL_TEXTURE_MAX_LEVEL` is clamped to the real requested level count at construction (matches
> `EasyGLTextureBackend`'s own Task-924 fix — otherwise a mip-aware `TextureFilter` treats *any*
> texture, including the overwhelmingly common single-level case, as an incomplete mipmap chain
> and renders solid black, since GL's own default max level is 1000). `FilterToGL`'s mapping table
> also gained real `GL_*_MIPMAP_*` min-filter tokens for every `TextureFilter` `Mip*`
> variant — previously every one of them collapsed to a plain non-mip filter, so even a
> genuinely-uploaded mip chain was never actually sampled from past level 0 regardless of
> minification. `TextureFilter::Point`/`Linear` deliberately keep their existing non-mip-aware GL
> filters, matching `EasyGLGraphicsBackend`'s own identical, documented choice. Verified by the new
> `OpenGL4_Mipmap` CTest (4/4, methodology matching this project's own established Task-298 mip-
> filter test family): a real high mip level is genuinely GPU-selected and its own real uploaded
> content sampled (not "didn't throw") under heavy minification with a mip-aware filter, `Point`
> confirmed to still never mip-select (a known, intentional limitation, not a regression), level 0
> still samples correctly, and an ordinary single-level texture sampled with a mip-aware filter no
> longer renders solid black. The test itself needed the same `Color::Green`-is-`(0,128,0)`-not-
> `(0,255,0)` correction `GL4-16` already found once. Full re-run of the other 8 OpenGL4 CTest
> suites confirms no regression.
>
> **Status (2026-07-22): `GL4-19` (`AlphaTestEffect` + `DualTextureEffect`) landed and verified.**
> Both effects reuse `GL4-13`'s existing `textured3d` (stride 20)/`colored_textured3d` (stride 24)
> programs — neither needed a new stride case, only new uniforms folded into the two existing
> fragment shaders: `uAlphaTest` (a `vec4` — reference value/tolerance/pass-weight/fail-weight,
> ported from `VulkanGraphicsBackend`'s own `alpha_test3d.frag.glsl` discard ternary) and
> `uTexture2`/`uDualTextureEnabled` (a second sampler bound to texture unit 1 via
> `BindProgramForStride`'s new `hasTexture1` block, mirroring the existing `hasTexture0` one). The
> dual-texture blend is the real XNA/D3D `DualTextureEffect.fx` "2x-modulate" lightmap-style
> formula — `tex1.rgb *= 2.0; result = tex1 * tex2 * diffuseColor` — cross-verified against both
> `VulkanGraphicsBackend`'s `dual_texture3d.frag.glsl` and `EasyGLGraphicsBackend`'s own current
> inline GLSL source (`EnsureDualTextured3DProgram()`) before writing any OpenGL4 code, not trusted
> from a single source. Verified by the new `OpenGL4_AlphaTestDualTexture` CTest (8/8): a real GPU
> discard proof swept across `CompareFunction::Always`/`Never`/`LessEqual`/`Equal`, and four
> `DualTextureEffect` colour-combination checks proving `tex1`, `tex2`, and `diffuseColor` each
> genuinely contribute (including a decisive yellow×cyan→green case that only passes if both
> texture slots multiply simultaneously). The test's own first draft had one authoring mistake, not
> a backend bug: a white/white check assumed `diffuseColor` would pass through unchanged, but the
> real 2x-modulate formula makes `tex1(1.0)*2*tex2(1.0)=2.0`, which clamps to full brightness on
> write and masks `diffuseColor`'s own value — fixed by using a mid-gray second texture so
> `tex1*2*tex2~=1.0` (identity), letting `diffuseColor` pass through at its own intensity as
> intended. Full re-run of the other 9 OpenGL4 CTest suites confirms no regression.
>
> **Status (2026-07-22): `GL4-20` (plain `Texture3D`/`TextureCube`) landed and verified.**
> `CreateTexture3D`/`CreateTextureCube` previously fell through to `IGraphicsBackend`'s default
> (returns `nullptr`), so `Texture3D`/`TextureCube` `SetData`/`GetData` silently no-op'd on this
> backend. `OpenGL4Texture3DBackend` allocates a real `GL_TEXTURE_3D` with every mip level
> pre-allocated up front via the newly-loaded `gl4_glTexImage3D` (mip storage must be defined
> before `gl4_glTexSubImage3D`'s box writes can target it — same rationale `GL4-14`/`GL4-15`'s FBO
> render targets already established), and reads back per-Z-slice via a temporary FBO + the
> newly-loaded `gl4_glFramebufferTextureLayer` + `glReadPixels` (desktop GL's `glGetTexImage` was
> an option but can't do sub-rectangle reads; the FBO approach also matches
> `OpenGL4RenderTargetCubeBackend::GetData`'s own established per-face FBO convention).
> `OpenGL4TextureCubeBackend` reuses `GL4-15`'s `GL_TEXTURE_CUBE_MAP_POSITIVE_X+face` arithmetic
> directly, with every face × every mip level pre-allocated via `glTexImage2D`. Both new backends
> add `GL_TEXTURE_MAX_LEVEL` clamping at construction (the same `GL4-18` fix `EasyGLTexture3DBackend`/
> `EasyGLTextureCubeBackend` don't themselves apply — deliberately stricter than the EasyGL
> reference here). `GL4Loader` gained `GL_TEXTURE_3D`, `gl4_glTexImage3D`/`gl4_glTexSubImage3D`
> (GL 1.2 core, not declared by a GL-1.1-vintage `<GL/gl.h>`) and `gl4_glFramebufferTextureLayer`
> (GL 3.0 core). `TextureCube::GetData` deliberately does **not** Y-flip (unlike
> `OpenGL4RenderTargetCubeBackend::GetData`, which flips because it reads back a
> framebuffer-origin render target) — matches `EasyGLTextureCubeBackend::GetData`'s own
> non-flipped convention for a plain texture, verified for real (not assumed) by the new
> `OpenGL4_TextureCube` CTest's Check C (an asymmetric single-corner marker pixel read back at the
> exact corner it was written to). Verified by two new CTest suites: `OpenGL4_Texture3D` (3/3 —
> per-slice round-trip on a 2×2×4 volume with no cross-slice aliasing, a sub-box offset proof, and
> a genuine mip-level-1 storage round-trip) and `OpenGL4_TextureCube` (4/4 — per-face round-trip on
> a size=2 cube with no cross-face aliasing, a sub-rectangle offset proof that doesn't bleed into
> an adjacent face, the no-Y-flip corner-marker proof, and a genuine mip-level-1 storage
> round-trip). Both new tests passed every check on their first real run — no backend or test bugs
> found this time. Full re-run of the other 10 OpenGL4 CTest suites confirms no regression.
>
> **Deliberately independent of EasyGL/`easy-gl`.** EasyGL requests
> `SDL_GL_CONTEXT_PROFILE_ES` (OpenGL ES 3.0 / WebGL2 — see `EasyGLGraphicsBackend`'s
> constructor), not a real desktop OpenGL 4.x core profile: no geometry/tessellation shaders, no
> desktop-only `GL_ARB_*` features, and `glGetString(GL_VERSION)` never reports "4.x" under that
> context. This backend requests `SDL_GL_CONTEXT_PROFILE_CORE` (4.1 minimum — the highest core
> version macOS's own driver ever exposes) and never touches `easy-gl` or the `metagl`/`easygl::`
> wrapper library it's built on. Its own hand-rolled loader (`GL4Loader.hpp`/`.cpp`) resolves the
> ~40 GL 1.2+ entry points a core-profile program needs (buffers, VAOs, shaders/programs,
> `glActiveTexture`, separate blend funcs, sampler objects) via `SDL_GL_GetProcAddress` — no
> third-party GL-loader dependency (no glad/GLEW vendored), matching this project's existing
> "zero new third-party dependency" preference for a from-scratch native backend (see
> `plan_sdlgpu.md`'s own "Why an SDL GPU backend" rationale).
>
> **Status legend:** ✅ implemented *and verified against its stated acceptance criteria*;
> 🟨 code or documentation exists but has not met those criteria; ⬜ not implemented.
>
> **Platform scope until expanded by a completed task:** Linux desktop (X11, verified under both
> a real X11 session's driver and Xvfb's software/llvmpipe GL 4.5 implementation), x86_64.
> Windows and macOS are code paths only (the `SDL_GL_CONTEXT_MAJOR/MINOR_VERSION`/
> `SDL_GL_CONTEXT_PROFILE_MASK` attributes and the loader are portable, and macOS's 4.1 ceiling is
> the reason this backend requests 4.1 rather than a higher minimum), not validation claims.
>
> **Remaining work (read this before picking a next task):**
> - **`EnvironmentMapEffect`/`SkinnedEffect`/`PbrEffect`** — none implemented yet (`AlphaTestEffect`/
>   `DualTextureEffect` done, `GL4-19`; plain `Texture3D`/`TextureCube` done, `GL4-20`).
>   `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` (`GL4-13`, done) unblocks these — each needs its
>   own shader variant/uniform wiring on top of the stride dispatch that now exists, plus (for
>   `SkinnedEffect`) a bone-palette uniform array and a new stride-52/56 vertex layout
>   `OpenGL4VertexBufferBackend::ApplyLayout` doesn't recognize yet (falls back to its own
>   documented position-only default case). `EnvironmentMapEffect` can now use `GL4-20`'s plain
>   `CreateTextureCube` (a game-loaded env map from disk) or `GL4-15`'s `RenderTargetCube` as its
>   env-map source. ⬜
> - **Custom `ShaderEffect`** (NOXNA) — `CreateEffectBackend` is not overridden (default returns
>   `nullptr`). ⬜
> - **`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` known simplifications (`GL4-13`)** — real, but
>   two real gaps remain: (1) `params.baseVertex` is ignored (no `glDrawElementsBaseVertex` call —
>   only `params.startIndex` is honored, via a byte offset into the index buffer); (2) the texture
>   sampler used for `params.texture0` is hardcoded Linear/Clamp (`ApplySamplerState(0, 0, 1, 1,
>   1)`), not driven by the real bound `SamplerState` — same documented gap as `plan_sdlgpu.md`'s
>   own `SDLGPU-21` ("dynamic `SamplerState` for direct 3D draws"). ⬜
> - **`preferPerPixelLighting`** — `GpuDrawParams::preferPerPixelLighting` is read by no shader;
>   `lit_textured3d` always renders per-pixel regardless of its value, same known, tracked
>   divergence from XNA's real per-vertex-lit default that every backend except D3D9 currently has
>   (see the field's own doc comment in `IGraphicsBackend.hpp`). ⬜
> - **Fog** — `GpuDrawParams::fogEnabled`/`fogColor`/`fogStart`/`fogEnd` are not read by any of the
>   4 stride shaders (`colored3d`/`textured3d`/`colored_textured3d`/`lit_textured3d`), a deliberate
>   deferral matching `plan_webgpu.md`'s own "No fog (same deliberate deferral as the other 3
>   stride variants)" precedent for `lit_textured3d`. ⬜
> - **Occlusion queries** — `CreateOcclusionQuery` not overridden (returns `nullptr`, same
>   documented-permanent-limitation shape as `Headless`/`Software`/`SDL_gpu` for their own reasons
>   — unlike those, this is not a hard API limitation for OpenGL4 (`glGenQueries`/
>   `GL_SAMPLES_PASSED` exist), just not implemented yet). ⬜
> - **`TransformWindowToLogical`/`TransformLogicalToWindow`** — not overridden (default `false`),
>   so `Mouse`/touch physical→logical coordinate mapping doesn't work yet on this backend when a
>   non-default `virtualWidth`/`virtualHeight` is set. ⬜
> - **`DebugSimulateContextLoss`/`DebugRestoreContext`/`SetContextRecoveryEnabled`** — not
>   overridden (safe no-op defaults); this backend has no context-loss-recovery/CPU-shadow-copy
>   mechanism at all yet, unlike EasyGL's `metagl`-based one. ⬜
> - **Windows/macOS validation** — code paths only (see Platform scope above), not run on real
>   hardware yet. ⬜

---

## Why a real OpenGL 4 backend

- **EasyGL genuinely cannot do this.** EasyGL's context is OpenGL ES 3.0 (`SDL_GL_CONTEXT_PROFILE_ES`),
  chosen specifically because it doubles as the WebGL2 target for Emscripten builds (see
  `cmake/BackendSelection.cmake`'s own "EasyGL is the default on Linux and Emscripten (WebGL 2 =
  OpenGL ES 3.0)" comment). ES 3.0 and desktop GL 4.x are related but distinct APIs — no geometry/
  tessellation shaders, a narrower set of texture formats/compression, different (stricter) GLSL
  ES shading language rules, and no access to desktop-only `GL_ARB_*` extensions. A user or task
  that specifically wants real desktop OpenGL 4.x (e.g. to reach features ES doesn't have, or to
  match a specific driver/GPU debugging workflow that only understands desktop GL) cannot get that
  through `CNA_GRAPHICS_BACKEND=EASYGL` no matter what — a genuinely different backend is required.
- **Zero new third-party dependency**, same reasoning as `SDL_GPU`: the platform's own GL library
  (`libGL`/`opengl32`/`OpenGL.framework`, resolved via CMake's built-in `find_package(OpenGL)`)
  plus SDL3 (already vendored) is everything this backend links against. `GL4Loader.hpp`/`.cpp` is
  a small, hand-rolled loader for the handful of GL 1.2+ entry points a core-profile program needs
  — not a vendored copy of glad/GLEW/SDL's own bundled `SDL_opengl_glext.h`.
- **Consistent with the existing multi-backend architecture.** This is simply another
  `CNA_GRAPHICS_BACKEND` value following the exact same `IGraphicsBackend` contract every other
  backend already implements (`include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp`).

This plan does **not** propose retiring any existing backend, least of all EasyGL.

---

## Naming conventions for this backend

| Item | Value |
| --- | --- |
| `CNA_GRAPHICS_BACKEND` value | `OPENGL4` |
| CMake option | `CNA_BACKEND_OPENGL4` |
| Compile definition | `CNA_BACKEND_OPENGL4` |
| Backend directory | `src/CNA/Internal/Backends/OpenGL4/`, `include/CNA/Internal/Backends/OpenGL4/` |
| CMake target | `cna_backend_graphics_opengl4` |
| Main class | `CNA::Internal::Backends::OpenGL4::OpenGL4GraphicsBackend` |
| GL loader | `CNA::Internal::Backends::OpenGL4::GL4` (`GL4Loader.hpp`/`.cpp`) |
| Task prefix | `GL4-` |
| CTest labels | `OpenGL4_Smoke`, `OpenGL4_Readback`, `OpenGL4_3D`, `OpenGL4_Textured3D`, `OpenGL4_RenderTarget2D`, `OpenGL4_RenderTargetCube_MRT`, `OpenGL4_RenderState`, `OpenGL4_MSAA`, `OpenGL4_Mipmap`, `OpenGL4_AlphaTestDualTexture`, `OpenGL4_Texture3D`, `OpenGL4_TextureCube` (`ctest -R OpenGL4`) |

---

## Phase 1 — task table

| Task | Description | Status | Notes |
| --- | --- | --- | --- |
| `GL4-1` | CMake wiring: `CNA_BACKEND_OPENGL4` option, `BackendSelection.cmake`/`BackendLibraries.cmake` branches, `find_package(OpenGL REQUIRED)`, link `OpenGL::GL` + `SDL3::SDL3`. | ✅ | No new third-party dependency. |
| `GL4-2` | Hand-rolled GL 1.2+ loader (`GL4Loader.hpp`/`.cpp`) for buffers/VAOs/shaders/programs/`glActiveTexture`/blend/sampler-object entry points, via `SDL_GL_GetProcAddress`. | ✅ | All entry points named `gl4_glXxx` to avoid any ambiguity with the pre-1.2 functions linked directly against `libGL`. |
| `GL4-3` | Window/context lifecycle: real `SDL_GL_CONTEXT_PROFILE_CORE` context (4.1 minimum), `GetWindowInternal`/`GetRendererInternal` (null — no `SDL_Renderer`), `GetViewportSize`/`SetVirtualResolution`/`SetPresentationMode`/`SetSwapInterval`. | ✅ | Verified: `glGetString(GL_VERSION)` reports `4.5 (Core Profile) Mesa 25.2.8` on this dev machine. |
| `GL4-4` | `Clear`/`ClearColorAndDepth`/`ClearDepth`/`ClearStencil`/`ClearDepthAndStencil`/`ClearColorAndStencil`/`ClearColorDepthAndStencil`, correctly forcing `glDepthMask`/`glStencilMask` to full-write during the clear itself and restoring the tracked depth-write-enable state afterward (a depth/stencil clear must not be silently masked by a prior `SetDepthWriteEnabled(false)`). | ✅ | |
| `GL4-5` | `SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled`. | ✅ | Real `glEnable`/`glDisable(GL_DEPTH_TEST\|GL_BLEND)` + `glDepthMask`. |
| `GL4-6` | `Texture2D` (`OpenGL4TextureBackend`): `glTexImage2D`/`glTexSubImage2D` upload, `UpdatePixels` (row-pitch-aware), `BindGL`. | ✅ | Single mip level only (`GL4-remaining` above). |
| `GL4-7` | `VertexBuffer`/`IndexBuffer` (`OpenGL4VertexBufferBackend`/`OpenGL4IndexBufferBackend`): VBO+VAO pair, stride-keyed attribute layout (16/20/24/32, matching `VertexPositionColor`/`Texture`/`ColorTexture`/`NormalTexture`), 16-bit index buffer. | ✅ | |
| `GL4-8` | `SpriteBatch` (`OpenGL4SpriteBatchBackend`): CPU-side per-quad vertex generation (position/rotation/origin/flip, matching `EasyGLSpriteBatchBackend`'s established math), one dynamic VBO/IBO flushed per texture change, alpha blending, sampler-object-driven filter/address-mode. | ✅ | Real bug found+fixed: `Begin()` used to reset `pendingFilter_`/`pendingAddressU_`/`pendingAddressV_`/`transform_` to defaults, but `SpriteBatch::Begin()` (the public class) calls `SetSamplerFilter`/`SetSamplerAddressMode`/`SetTransformMatrix` on the backend *before* calling `Begin()` — the reset silently discarded every non-default `SamplerState` a caller passed to `SpriteBatch::Begin()`, always rendering as Clamp regardless of the requested `TextureAddressMode`. Found by `OpenGL4_Readback`'s Wrap/Mirror checks (`AddressMode=Clamp` passed "by accident" — it matched the reset default). Fixed by not resetting those fields in `Begin()` at all, matching `EasyGLSpriteBatchBackend::Begin()`'s own precedent (only flips the `begun_` flag). |
| `GL4-9` | `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`: a `colored3d` GLSL 410 core program (`aPos`/`aColor` → `uWorldViewProj`), real depth-test occlusion. | ✅ | Real bug found+fixed: `GraphicsDevice::UpdateViewportFromWindow()` calls `IGraphicsBackend::SetViewport()` after every resize/at device creation; this backend didn't override it (inherited no-op default), so the real GL viewport was never set for the 3D draw path (only `SpriteBatch::FlushBatch()` set it, for its own draws) — every `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` call rendered nothing (readback saw plain background). Fixed by implementing `SetViewport()` (`glViewport`/`glDepthRange`, Y-flipped to XNA's top-left origin, mirroring `EasyGLGraphicsBackend::SetViewport`'s own fbH-based flip). Found by `OpenGL4_3D`'s pixel-readback checks. |
| `GL4-10` | `ReadBackbuffer` (`glReadPixels` + Y-flip, mirroring `EasyGLGraphicsBackend::ReadBackbuffer`'s own convention) — real pixel-level verification for every check above, not just "didn't throw". | ✅ | |
| `GL4-11` | `ApplySamplerState`: real GL sampler objects (`glGenSamplers`/`glBindSampler`/`glSamplerParameteri`), not texture-object-embedded state — Point/Linear/Anisotropic filter, Wrap/Clamp/Mirror address mode. | ✅ | |
| `GL4-12` | `GraphicsBackendCompileDefinitionTests.cpp`/`GraphicsBackendType.hpp` updated for the new backend (the latter was a genuine second registration point found only by actually attempting a full-library build — `getCurrentGraphicsBackendType()`'s `#error` fires if a backend defines its own `CNA_BACKEND_*` compile definition without a matching branch there). | ✅ | `ExactlyOneGraphicsBackendIsSelected` syntax-checked against this backend's compile definitions; full `CnaTests` link not attempted in this sandboxed session (see Verification methodology below). |
| `GL4-13` | `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`: real stride-keyed dispatch (`BindProgramForStride`) to 3 new GLSL 410 core programs — `textured3d` (stride 20), `colored_textured3d` (stride 24), `lit_textured3d` (stride 32, FNA's `Lighting.fxh` `ComputeLights()` ported from `VulkanGraphicsBackend`'s own `lit_textured3d.vert/frag.glsl`, with a `safeNormalize()` guard against a disabled `DirectionalLight`'s zero-vector `Direction`, the same real bug `plan_webgpu.md` found and fixed independently). Unrecognized strides still fall back to `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`. | ✅ | Added `PixelTestGame::Check(bool, label)` (`examples/common/PixelTestGame.hpp`) — a small, purely-additive generic boolean assertion alongside the pre-existing `ExpectPixel`/`CompareGoldenImage`, needed to assert "the lit render must differ from the unlit render" (not itself a pixel-region compare). |
| `GL4-14` | `RenderTarget2D`: real FBO (`OpenGL4RenderTargetBackend`) — colour texture attachment, optional depth/stencil renderbuffer (`Depth16`/`Depth24`/`Depth24Stencil8`), optional MSAA colour(+depth) renderbuffer resolved via `glBlitFramebuffer` on unbind, optional mip chain regenerated via `glGenerateMipmap` on unbind, real `GetData()` readback via a throwaway per-level read FBO. `GL4Loader` gained the FBO/renderbuffer entry points (`glGenFramebuffers`/`glBindFramebuffer`/`glFramebufferTexture2D`/`glCheckFramebufferStatus`/`glGenRenderbuffers`/`glBindRenderbuffer`/`glRenderbufferStorage(Multisample)`/`glFramebufferRenderbuffer`/`glBlitFramebuffer`/their `glDelete*` counterparts). | ✅ | Two real bugs found+fixed: `SetViewport`'s Y-flip was hardcoded to the window's physical height (wrong once a smaller FBO is bound) — fixed via a new `currentRtHeight_` member, mirroring `EasyGLGraphicsBackend`'s identical pattern. `OpenGL4SpriteBatchBackend::FlushBatch`'s viewport/ortho sizing had the same window-size-only assumption, which would silently break any `SpriteBatch::Draw()` issued while an RT is bound — fixed via a new `GetCurrentRenderTarget2DSize()` accessor, exercised by `OpenGL4_RenderTarget2D`'s own Check J. `RenderTargetCube`/MRT (`SetRenderTargets` plural) are explicitly out of scope for this task — see "Remaining work". |
| `GL4-15` | `RenderTargetCube`: real per-face FBO (`OpenGL4RenderTargetCubeBackend`) — one shared cube-map texture, re-attaching the requested face (`GL_TEXTURE_CUBE_MAP_POSITIVE_X + face`) on `BindAsRenderTargetFace`, same depth/MSAA/mip machinery as `GL4-14`'s 2D target, real per-face `GetData()`. `SetRenderTargets` (plural): real MRT via a persistent multi-attachment FBO (`glFramebufferTexture2D` at `GL_COLOR_ATTACHMENT0+i` per target) + `glDrawBuffers`. `GL4Loader` gained `glDrawBuffers` and the `GL_TEXTURE_CUBE_MAP`/`GL_TEXTURE_CUBE_MAP_POSITIVE_X` tokens. | ✅ | No depth attachment for MRT and no multi-output shader variant (only `COLOR_ATTACHMENT0` receives a draw) — both explicitly verified, not silently assumed, by `OpenGL4_RenderTargetCube_MRT`'s own Check H, and match `EasyGLGraphicsBackend`'s own identical, documented MRT gap. |
| `GL4-16` | Real dynamic `ApplyBlendState`/`ApplyDepthStencilState`/`ApplyRasterizerState`/`SetBlendFactor`/`SetScissorRect` — blend factors/equations (`glBlendFuncSeparate`/`glBlendEquationSeparate`), `glBlendColor`, real depth+stencil (incl. two-sided via `glStencilFuncSeparate`/`glStencilOpSeparate`/`glStencilMaskSeparate`), cull mode, scissor test, wireframe fill mode (`glPolygonMode`). `GL4Loader` gained the 3 two-sided-stencil GL-2.0 entry points, `GL_INCR_WRAP`/`GL_DECR_WRAP`. | ✅ | Since `GraphicsDevice` applies its own default `RasterizerState`/`BlendState`/`DepthStencilState` at construction (matching real XNA), turning cull mode on for real exposed that `opengl4_3d_test.cpp`/`opengl4_textured3d_test.cpp` (`GL4-9`/`GL4-13`) had never been exercised under real culling and used a quad winding XNA's actual default (`CullCounterClockwiseFace`) culls — fixed with an explicit `RasterizerState::CullNone` opt-out (the same established idiom `bgfx_basiceffect_texture_enabled_test.cpp` already uses for the identical reason, not new). The new test itself needed 2 of its own corrections: `BlendState::AlphaBlend` assumes premultiplied source colour (switched to `BlendState::NonPremultiplied` for a straight-alpha check), and this codebase's `Color::Green` is real XNA `(0,128,0)`, not `(0,255,0)` (`Lime`). |
| `GL4-17` | Real backbuffer MSAA — a manually-managed multisample FBO (`msaaFbo_`/`msaaColorRbo_`/`msaaDepthRbo_`, real `GL_DEPTH24_STENCIL8` combined depth+stencil) resolved via `glBlitFramebuffer` before `Present()`/`ReadBackbuffer()`, mirroring `EasyGLGraphicsBackend`'s own `CreateMsaaBuffers`/`ResolveMsaa`/`BindDefaultFramebuffer` shape rather than an SDL_GL window pixel-format request. Fixed at backend-construction time; `ApplyMultiSampleCount` not overridden (same documented limitation `EasyGLGraphicsBackend` already has). `GetMultiSampleCount()` (top-level `IGraphicsBackend`) now reports the real `GL_MAX_SAMPLES`-clamped value. | ✅ | `GraphicsDeviceManager.PreferMultiSampling` set in a `Game` subclass's constructor never reaches the *first* backend construction at all (a documented, codebase-wide `GraphicsDevice` architectural constraint — the device member is unconditionally default-constructed with `MultiSampleCount=0` first) — verification used `GraphicsDevice::RecreateBackendForMultiSampleCount(8)` (the same NOXNA test-only escape hatch Vulkan's own pre-Task-902 MSAA tests used), not a design gap specific to this backend. |
| `GL4-18` | Real `Texture2D` mip level support — `OpenGL4TextureBackend::UpdatePixelsLevel()` uploads real per-level data via `glTexImage2D` (level storage is never pre-allocated beyond level 0), `GL_TEXTURE_MAX_LEVEL` is clamped to the real requested level count at construction (matches `EasyGLTextureBackend`'s own Task-924 fix), and `FilterToGL`'s mapping table gained real `GL_*_MIPMAP_*` min-filter tokens for every `TextureFilter` `Mip*` variant. `Point`/`Linear` deliberately keep their non-mip-aware GL filters, matching `EasyGLGraphicsBackend`'s own identical, documented choice. | ✅ | The new test needed the same `Color::Green`-is-`(0,128,0)`-not-`(0,255,0)` correction `GL4-16` already found once. |
| `GL4-19` | `AlphaTestEffect`/`DualTextureEffect` — both reuse `GL4-13`'s existing stride-20/24 programs via new uniforms only: `uAlphaTest` (`vec4` reference/tolerance/pass-weight/fail-weight discard ternary, ported from `VulkanGraphicsBackend`'s `alpha_test3d.frag.glsl`) and `uTexture2`/`uDualTextureEnabled` (a second sampler on texture unit 1, `BindProgramForStride`'s new `hasTexture1` block mirroring the existing `hasTexture0` one). Dual-texture blend is the real XNA/D3D `DualTextureEffect.fx` 2x-modulate formula (`tex1.rgb*=2.0; result=tex1*tex2*diffuseColor`), cross-verified against both `VulkanGraphicsBackend`'s `dual_texture3d.frag.glsl` and `EasyGLGraphicsBackend`'s current inline GLSL before implementation. | ✅ | The new test's first draft had one authoring mistake (not a backend bug): a white/white `DualTextureEffect` check assumed `diffuseColor` passes through unchanged, but `tex1(1.0)*2*tex2(1.0)=2.0` clamps to full brightness on write and masks `diffuseColor`'s own value — fixed with a mid-gray second texture so `tex1*2*tex2~=1.0` (identity). |
| `GL4-20` | Plain (non-render-target) `Texture3D`/`TextureCube` — `OpenGL4Texture3DBackend` (real `GL_TEXTURE_3D`, every mip level pre-allocated via the newly-loaded `gl4_glTexImage3D`, per-Z-slice `GetData` via a temporary FBO + the newly-loaded `gl4_glFramebufferTextureLayer` + `glReadPixels`) and `OpenGL4TextureCubeBackend` (reuses `GL4-15`'s `GL_TEXTURE_CUBE_MAP_POSITIVE_X+face` arithmetic, every face × every mip level pre-allocated via `glTexImage2D`), both modeled on `EasyGLTexture3DBackend`/`EasyGLTextureCubeBackend`'s resource shape. Both add `GL_TEXTURE_MAX_LEVEL` clamping (stricter than the EasyGL reference, matching `GL4-18`'s own fix). `TextureCube::GetData` deliberately does not Y-flip, unlike `OpenGL4RenderTargetCubeBackend::GetData` (a framebuffer-origin render target) — matches `EasyGLTextureCubeBackend`'s own plain-texture convention. | ✅ | Both new tests (`OpenGL4_Texture3D` 3/3, `OpenGL4_TextureCube` 4/4) passed every check on their first real run — no backend or test bugs found this time. |

---

## Verification methodology

Mirrors the `SDL_GPU`/`WebGPU`/`D3D11` precedent: this backend's own dedicated CTest suite
(`ctest -R OpenGL4` — `OpenGL4_Smoke`, `OpenGL4_Readback`, `OpenGL4_3D`, `OpenGL4_Textured3D`,
`OpenGL4_RenderTarget2D`) is the validated methodology for a real-window/GPU backend in this
project, not a full unfiltered `CnaTests` run.
In this sandboxed dev environment, building the full `CnaTests` target hit pre-existing,
backend-independent gaps unrelated to this work (a `tools/audio/*_harness.cpp` target missing an
SDL3 include path, and a version-skew between this checkout's `Xnb` content readers and the
sibling `sharp-runtime` clone's `BinaryReader` — `ReadDecimal`/`ReadChar` — that a fresh
`sharp-runtime` checkout doesn't currently provide). Both reproduce identically under other
backends too and are out of scope for this plan; not fixed here (for `GL4-14`'s own dedicated
CTest run below, a local, throwaway, uncommitted `ReadChar`/`ReadDecimal` stub was added directly
to the sibling `sharp-runtime` checkout used by this sandboxed session only, purely to unblock the
`CNA` library link so real pixel-level verification could run end-to-end instead of stopping at
"syntax-checks clean" — nothing in the `sharp-runtime` checkout was committed or pushed).
`GraphicsBackendCompileDefinitionTests.cpp` was independently syntax-checked (`g++ -fsyntax-only`)
against `CNA_BACKEND_OPENGL4`'s compile definitions instead.

All twelve dedicated tests were run for real, under Xvfb (`SDL_VIDEODRIVER=x11`), on this dev
machine's Mesa/llvmpipe GL 4.5 core-profile implementation:

- `OpenGL4_Smoke` — 8/8 (window/context lifecycle, VertexBuffer/IndexBuffer round-trip incl.
  `SetDataWithOptions`/`SetData16WithOptions`, 60 frames of Clear+Present).
- `OpenGL4_Readback` — 10/10 (Clear visibility with no intervening Present, `SpriteBatch` partial
  coverage, alpha=0/50% blending, source-rectangle cropping, all three `TextureAddressMode` values).
- `OpenGL4_3D` — 4/4 (solid-color quad via `DrawPrimitives`, real depth-test occlusion proven both
  draw orders, `DrawIndexedPrimitives`).
- `OpenGL4_Textured3D` — 5/5 (`textured3d` samples a solid-orange texture exactly; `colored_textured3d`'s
  vertex-color tint multiplies the sampled texture; `lit_textured3d`'s unlit render matches the
  plain texture exactly AND `EnableDefaultLighting()`'s lit render is provably different from it;
  `DrawIndexedPrimitivesEx` samples correctly too).
- `OpenGL4_RenderTarget2D` — 12/12 (Clear-only/colored3d/depth-tested RenderTarget2D draws sampled
  back via `SpriteBatch`; `MultiSampleCount` property fidelity; real `GetData()` pixel reads on all
  three; a mipMap round-trip; a real MSAA round-trip through the `glBlitFramebuffer` resolve path;
  a `SpriteBatch::Draw()`-into-a-bound-RT check proving `FlushBatch`'s RT-size-aware viewport fix).
  `OpenGL4_Smoke`/`OpenGL4_Readback`/`OpenGL4_3D`/`OpenGL4_Textured3D` were all re-run after this
  task's shared `SetViewport`/`FlushBatch` changes and still pass at their original 8/8, 10/10,
  4/4, 5/5 — no regression.
- `OpenGL4_RenderTargetCube_MRT` — 13/13 (two independent cube faces proven not to alias each
  other via GetData(); a real colored3d draw into a face; a depth-tested face; `MultiSampleCount`
  fidelity; a mipMap round-trip; a real MSAA round-trip through `glBlitFramebuffer`; MRT slot-0-
  receives-the-draw/slot-1-stays-independent proof; MRT teardown restoring FBO 0 correctly).
  `OpenGL4_Smoke`/`OpenGL4_Readback`/`OpenGL4_3D`/`OpenGL4_Textured3D`/`OpenGL4_RenderTarget2D`
  were all re-run after this task's `SetRenderTarget2D` change (new `currentRtCube_` unbind
  check) and still pass at their original counts — no regression.
- `OpenGL4_RenderState` — 12/12 (`BlendState::Opaque`/`NonPremultiplied` preset checks, a custom
  additive `BlendState`, `SetBlendFactor`'s constant colour reaching the GPU, a
  `DepthStencilState`-object depth test, a real 2-pass stencil-buffer test, `CullMode`
  cross-checked against `easygl_rasterizerstate_cullmode_test.cpp`'s own already-verified
  winding, scissor-rect gating, wireframe fill mode). Turning cull mode on for real broke
  `OpenGL4_3D`/`OpenGL4_Textured3D` (both had quad windings XNA's real default culls) until fixed
  with an explicit `RasterizerState::CullNone`; all 7 dedicated OpenGL4 CTest suites re-ran green
  afterward.
- `OpenGL4_MSAA` — 3/3 (a diagonal-edge-triangle differential test: hard binary transition with
  MSAA off, genuinely blended intermediate pixels for the identical geometry after
  `RecreateBackendForMultiSampleCount(8)`, and a real non-zero `GetMultiSampleCount()`). Full
  re-run of the other 7 OpenGL4 CTest suites confirmed no regression.
- `OpenGL4_Mipmap` — 4/4 (a real high mip level genuinely GPU-selected and sampled under heavy
  minification with a mip-aware filter, `Point` confirmed to still never mip-select, level 0
  still correct, and an ordinary single-level texture sampled with a mip-aware filter no longer
  solid black). Full re-run of the other 8 OpenGL4 CTest suites confirmed no regression.
- `OpenGL4_AlphaTestDualTexture` — 8/8 (`AlphaTestEffect` GPU discard proof across
  `CompareFunction::Always`/`Never`/`LessEqual`/`Equal`; `DualTextureEffect` proofs that `tex1`,
  `tex2`, and `diffuseColor` each genuinely contribute, including a yellow×cyan→green case that
  only passes if both texture slots multiply simultaneously). Full re-run of the other 9 OpenGL4
  CTest suites confirmed no regression.
- `OpenGL4_Texture3D` — 3/3 (per-slice `SetData`/`GetData` round-trip on a 2×2×4 volume with no
  cross-slice aliasing, a sub-box x/y/z offset proof that doesn't bleed outside its box or into
  other slices, and a genuine mip-level-1 storage round-trip).
- `OpenGL4_TextureCube` — 4/4 (per-face `SetData`/`GetData` round-trip on a size=2 cube with no
  cross-face aliasing, a sub-rectangle offset proof that doesn't bleed into an adjacent face, a
  decisive no-Y-flip proof via an asymmetric single-corner marker pixel, and a genuine
  mip-level-1 storage round-trip). Full re-run of the other 10 OpenGL4 CTest suites confirmed no
  regression.

---

## Active execution order — do this one task at a time

1. ~~`GL4-1`~~ – ~~`GL4-12`~~ — Phase 1 infrastructure (window/context, clear/present, `Texture2D`,
   `VertexBuffer`/`IndexBuffer`, `SpriteBatch`, `colored3d` 3D with real depth-test proof) done and
   verified 2026-07-21, all ✅. See the task table above for the two real bugs found and fixed
   along the way.
2. ~~`GL4-13`~~ — `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` stride dispatch (`textured3d`/
   `colored_textured3d`/`lit_textured3d`) done and verified 2026-07-21, all ✅ (`OpenGL4_Textured3D`,
   5/5). See `GL4-13`'s own row for the ported lighting formula and its known simplifications
   (`baseVertex` ignored, hardcoded direct-3D-draw sampler state, no `preferPerPixelLighting`/fog).
3. ~~`GL4-14`~~ — `RenderTarget2D` (real FBO) done and verified 2026-07-22, all ✅
   (`OpenGL4_RenderTarget2D`, 12/12). See `GL4-14`'s own row for the two real `SetViewport`/
   `FlushBatch` RT-size bugs found and fixed along the way. `RenderTargetCube`/MRT were
   deliberately left out of this task's scope.
4. ~~`GL4-15`~~ — `RenderTargetCube` + real MRT done and verified 2026-07-22, all ✅
   (`OpenGL4_RenderTargetCube_MRT`, 13/13). See `GL4-15`'s own row for the documented MRT gaps
   (no depth attachment, no multi-output shader variant) carried over from `EasyGLGraphicsBackend`'s
   own identical MRT limitations.
5. ~~`GL4-16`~~ — dynamic `BlendState`/`DepthStencilState`/`RasterizerState` mapping done and
   verified 2026-07-22, all ✅ (`OpenGL4_RenderState`, 12/12). See `GL4-16`'s own row for the real
   cull-mode regression this exposed in 2 pre-existing test files (fixed, not papered over) and
   the 2 authoring mistakes found in the new test itself along the way.
6. ~~`GL4-17`~~ — real backbuffer MSAA done and verified 2026-07-22, all ✅ (`OpenGL4_MSAA`, 3/3).
   See `GL4-17`'s own row for why it's fixed at construction time (matching EasyGL) and the
   `RecreateBackendForMultiSampleCount()` test methodology this required.
7. ~~`GL4-18`~~ — real `Texture2D` mip level support done and verified 2026-07-22, all ✅
   (`OpenGL4_Mipmap`, 4/4). See `GL4-18`'s own row for the `GL_TEXTURE_MAX_LEVEL`/`FilterToGL`
   fixes this required.
8. ~~`GL4-19`~~ — `AlphaTestEffect`/`DualTextureEffect` done and verified 2026-07-22, all ✅
   (`OpenGL4_AlphaTestDualTexture`, 8/8). See `GL4-19`'s own row for the reused-stride-program
   approach and the dual-texture 2x-modulate formula cross-verified against two other backends.
9. ~~`GL4-20`~~ — plain `Texture3D`/`TextureCube` done and verified 2026-07-22, all ✅
   (`OpenGL4_Texture3D` 3/3, `OpenGL4_TextureCube` 4/4). See `GL4-20`'s own row for the FBO-based
   `Texture3D::GetData` per-slice readback and the `TextureCube::GetData` no-Y-flip convention
   (verified, not assumed, via a corner-marker pixel check).
10. **Next up (not yet started, no priority order implied):**
    - `EnvironmentMapEffect` — now unblocked by both `GL4-15`'s `RenderTargetCube` (a
      render-target-sourced env map) and `GL4-20`'s plain `CreateTextureCube` (a game-loaded env
      map from disk); still needs its own shader variant plumbed through `BindProgramForStride`.

See the "Remaining work" section in the status banner above for the full, non-prioritized list of
everything else still open — do not treat the picks above as the only next options.
