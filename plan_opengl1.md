# CNA OPENGL1 Backend Plan

## Scope
`OPENGL1` is a genuinely independent, native legacy desktop OpenGL backend. It MUST NOT depend on EasyGL and MUST NOT route rendering through SDL_Renderer, SDL_GPU, bgfx, or the modern EasyGL backend. SDL is used only for the window and OpenGL context.

Target platforms: Linux and Windows desktop compatibility-profile drivers. The backend intentionally targets the OpenGL 1.x fixed-function programming model and requests an OpenGL 1.1 context. Modern drivers may expose a newer compatibility context while preserving these APIs.

## Implemented foundation
- Independent `CNA_GRAPHICS_BACKEND=OPENGL1` selection and `CNA_BACKEND_OPENGL1` compile definition.
- SDL `SDL_WINDOW_OPENGL` window integration and direct `SDL_GL_CreateContext`/swap path.
- Direct legacy OpenGL API usage; zero EasyGL dependency.
- RGBA8 `Texture2D` creation/update/binding.
- Fixed-function `SpriteBatch` using textured quads, tint, source/destination rectangles, rotation, origin and sprite flipping.
- CPU-backed vertex/index buffers with 16-bit and 32-bit indices.
- Non-indexed/indexed 3D primitive rendering for triangle list/strip, line list/strip and points.
- Known CNA vertex layouts: position+color, position+texture, position+color+texture, position+normal+texture.
- World/View/Projection through the legacy projection/model-view matrix stacks.
- Depth testing/writes, stencil, culling, wireframe, scissor, viewport and polygon offset.
- Basic fixed-function texture mapping, vertex colors, one-light directional lighting, linear fog and coarse alpha testing through `GpuDrawParams`.

## Intentional OpenGL 1.x limitations
- No GLSL/custom `ShaderEffect` pipeline in the strict OPENGL1 backend.
- No PBR shaders, programmable per-pixel lighting, GPU skinning, modern environment-map shaders or arbitrary custom vertex declarations.
- No MRT.
- No instancing.
- No native modern occlusion-query guarantee.
- RenderTarget2D is implemented via `ARB_framebuffer_object`/core (>=3.0), detected at runtime (`OpenGL1Capabilities::framebufferObject`); `CreateRenderTarget2D()` returns nullptr on a driver without it. `EXT_framebuffer_object` (the older, narrower extension) is not supported. No MSAA, no mip-chain auto-generation, no RenderTargetCube yet.
- `DualTextureEffect` is implemented via `ARB_multitexture`/core (>=1.3), detected at runtime (`OpenGL1Capabilities::multitexture`) with the entry points (`glActiveTexture`/`glMultiTexCoord2f`) loaded through `SDL_GL_GetProcAddress`; a strict 1.1 driver silently falls back to texture unit 0 only (`texture1` ignored, matching every other textured draw).
- Blend equations beyond additive blending and constant blend color need later extension/version detection.
- Anisotropic filtering requires `GL_EXT_texture_filter_anisotropic`, detected at runtime (`OpenGL1Capabilities::anisotropicFiltering`, `GraphicsCapability::AnisotropicFiltering`); silently falls back to no anisotropy (clamped to 1.0x) when the driver lacks it.

## Next implementation phases
1. ~~Add runtime GL version/extension discovery~~ **Done**: `OpenGL1Capabilities`/`DetectOpenGL1Capabilities()` (`OpenGL1Capabilities.hpp`/`.cpp`) query `GL_VERSION`/`GL_EXTENSIONS` directly (no loader library, no EasyGL dependency) once per context, right after `SDL_GL_MakeCurrent`, and expose version + `framebufferObject`/`multitexture`/`textureCubeMap`/`generateMipmap`/`anisotropicFiltering` flags for phases 2/3/5/6 below to consult. First real consumer wired end-to-end: `GraphicsCapability::AnisotropicFiltering` now reports the real detected value instead of a hardcoded `false`, and `ApplySamplerState()` actually sets `GL_TEXTURE_MAX_ANISOTROPY_EXT` when `TextureFilter::Anisotropic` is requested and the extension is present (verified against the real driver, including over-cap clamping and reset-to-1.0 on a non-anisotropic filter, by `OpenGL1_Anisotropic_GlState`).
2. ~~Add `ARB_framebuffer_object` RenderTarget2D support~~ **Done**: `OpenGL1RenderTargetBackend` (`OpenGL1RenderTargetBackend.hpp`/`.cpp`) creates a color texture + optional depth/stencil renderbuffer + FBO via ARB/core-3.0 entry points loaded through `SDL_GL_GetProcAddress` (`TryLoadOpenGL1FramebufferObjectFunctions()`); `CreateRenderTarget2D()` returns nullptr when `OpenGL1Capabilities::framebufferObject` is false or the FBO ends up incomplete (strict capability fallback, no partial/broken render target). Found and fixed three real bugs while getting this to round-trip correctly (render into a target, sample it back as a texture) -- see the "Bugs found" section below.
3. ~~Add `ARB_multitexture`/OpenGL 1.3 dual-texture fixed-function path~~ **Done**: `DrawInternal` reproduces FNA's real `DualTextureEffect.fx` formula (`(texture0.rgb*2)*texture1.rgb*diffuseColor.rgb`, alpha `texture0.a*texture1.a*diffuseColor.a`, both units sampling the same per-vertex UV -- DualTextureEffect has no second texcoord channel either) via `GL_COMBINE` texture-environment chaining: unit 0 modulates `texture0` by `GL_PRIMARY_COLOR` with `GL_RGB_SCALE=2`, unit 1 modulates `GL_PREVIOUS` by `texture1`. `SpriteBatch::Begin()` defensively resets unit 1 + unit 0's `GL_TEXTURE_ENV_MODE`, since a prior dual-textured 3D draw's state would otherwise bleed into 2D sprite rendering (`glPushAttrib`/`glPopAttrib` save/restore whatever state existed at `Begin()`/`End()`, they don't reset it to a clean baseline). Verified against the real driver by `OpenGL1_DualTextureEffect_Doubling` (reused from EasyGL's own Task 383 test), including the `*2` doubling factor specifically (a naive `texture0*texture1` multiply without it is a distinct, discriminable wrong answer).
4. ~~Add fixed-function texture environment/combine mappings for the XNA effects that can be represented honestly~~ **Done**: `DrawInternal`'s stride==16 (`VertexPositionColor`) and stride==24 (`VertexPositionColorTexture`) vertex paths used the per-vertex color alone via `glColor4ub`, silently ignoring `GpuDrawParams::diffuseColor` -- any `BasicEffect.VertexColorEnabled` draw with a non-default `DiffuseColor`/`EmissiveColor` (XNA's real formula multiplies vertex color BY the material's `(DiffuseColor+EmissiveColor)*Alpha`, per `BasicEffect::FillGpuDrawParams`) silently dropped the material tint. Fixed with a new `emitVertexColor` lambda that multiplies the unpacked vertex color by `params->diffuseColor` component-wise before calling `glColor4f`, matching the stride==20 fix from the initial bug pass. Verified against the real driver by `OpenGL1_BasicEffect_Combined` (reused verbatim from EasyGL's own Task 370 capstone test), which asserts the exact multiplicative formula `TextureColor x VertexColor x (DiffuseColor+EmissiveColor)` across 4 independently-colored texels -- pixel-exact match within tolerance.
5. ~~Add cube maps where `ARB_texture_cube_map` exists and implement the subset of EnvironmentMapEffect representable by texture coordinate generation/combine~~ **Done**: `OpenGL1TextureCubeBackend` (raw `GL_TEXTURE_CUBE_MAP`/`GL_TEXTURE_CUBE_MAP_POSITIVE_X..NEGATIVE_Z`, no extra function loading needed -- those are core-1.3 enum values, unlike FBO/multitexture's entry points) backs `CreateTextureCube()`, gated on `OpenGL1Capabilities::textureCubeMap` (strict capability fallback: returns nullptr when absent). `DrawInternal` blends it into a `VertexPositionNormalTexture` (stride 32) draw via texture unit 1's `GL_REFLECTION_MAP` texture-coordinate generation (`GL_TEXTURE_GEN_S/T/R`), `GL_INTERPOLATE`d against unit 0's lit/textured result through `GL_COMBINE` with `GL_CONSTANT.a=EnvironmentMapAmount` -- exactly FNA's real `mix(baseColor,envColor,Amount)` blend (see `docs/environmentmapeffect-support.md`'s own Task 394 for that formula). Unit 1's texgen/cube-map-enable state is explicitly reset on every draw that is *not* itself env-mapped (`resetUnit1EnvGen()`), since per-unit GL enables persist across `DrawInternal` calls with no push/pop bracketing them, unlike `SpriteBatch::Begin()`/`End()`. Deliberately NOT attempted: Fresnel edge-weighting and `EnvironmentMapSpecular`'s alpha-scaled specular term, both inherently per-pixel/view-angle-dependent and not expressible with fixed-function texture combiners -- an honest, documented limitation, not a silent wrong answer. Found and fixed one more real bug while wiring this up: the lit (stride 32) path never set `GL_EMISSION`, silently dropping `EnvironmentMapEffect`'s (and `BasicEffect`'s) `EmissiveColor`/ambient-light contribution entirely -- see the "Bugs found" section below. Verified against the real driver by `OpenGL1_EnvironmentMapEffect`, pixel-exact on the first attempt (`Amount=0`/`1`/`0.5` against a solid-color cube map, isolating the blend formula from reflection-vector face selection).
6. Add mipmap generation via available extensions or CPU fallback.
7. ~~Add readback~~ **Done**: `ReadBackbuffer` implemented (`glReadPixels` off `GL_BACK`). Render-target readback is still N/A (blocked on item 2, RenderTarget2D).
8. ~~Add context-loss resource recreation registry, separate from EasyGL~~ **Done**: `OpenGL1ContextRecovery.hpp`/`.cpp` define `IOpenGL1Recoverable`/`OpenGL1ResourceRegistry` -- a from-scratch implementation of the same concept EasyGL's own `::easygl::RecoverableResource`/`ResourceRegistry` establish, but with zero dependency on that library or its `metagl` context-event plumbing. `OpenGL1GraphicsBackend::DebugSimulateContextLoss()`/`DebugRestoreContext()` perform one atomic destroy+recreate cycle (matching every other desktop backend's own semantics -- there is no genuine asynchronous lost/restored pair on desktop the way WebGL has): notify-lost, destroy+recreate the SDL GL context, re-detect capabilities/reload extension entry points exactly as the constructor does, re-establish base GL state, notify-restored. `SetContextRecoveryEnabled(false)` stops future `Create*` calls from registering with the registry, matching the documented `IGraphicsBackend` contract. Two resource types are wired up: `OpenGL1TextureBackend` (via the pre-existing `ITextureBackend::ShareCpuPixels()` hook -- re-uploads from the SAME CPU pixel buffer `Texture2D` itself keeps, no duplicate copy) and `OpenGL1RenderTargetBackend` (rebuilds an empty FBO/color-texture/depth-renderbuffer of the same size/format -- content is GPU-produced, not restorable, matching real XNA/FNA `RenderTarget2D` semantics after a device reset without `RenderTargetUsage.PreserveContents`). `OpenGL1VertexBufferBackend`/`OpenGL1IndexBufferBackend`/`OpenGL1SpriteBatchBackend` need no recovery machinery at all -- they hold zero live GL resources (`DrawInternal`'s immediate-mode `glVertex3f`/`glTexCoord2f`/`glColor4f` calls read straight from CPU-side buffers every draw, there is no VBO/VAO). **Documented, intentional gap**: `OpenGL1TextureCubeBackend` is NOT wired into the registry -- `ITextureCubeBackend` has no `ShareCpuPixels()`-equivalent hook the way `ITextureBackend` does, and adding one would be an `IGraphicsBackend.hpp` interface change affecting every backend, out of proportion for this OPENGL1-scoped task; a `TextureCube` (used by `EnvironmentMapEffect`) will render as garbage/undefined content after a real context loss until this is added. Verified against the real driver by `OpenGL1_ContextLoss`: a `Texture2D`'s content is pixel-exact after `DebugSimulateContextLoss()`, and a `RenderTarget2D` remains bindable/drawable-into afterward without crashing.
9. ~~Add Linux X11/Xvfb smoke tests~~ **Done** (Linux half): `tests/opengl1/README.md`'s 8 priority scenarios are wired to real CTest registrations (`cmake/Tests/OpenGL1Tests.cmake`), 8/8 passing under Xvfb/X11+Mesa llvmpipe. Windows GitHub Actions compile/smoke jobs still open.
10. ~~Add visual golden-image tests shared with Software/EasyGL for the supported fixed-function subset~~ **Done**: reuses the EXACT same checked-in reference PNGs EasyGL's own golden-image suite (`examples/golden/*.png`) uses -- no new images -- via the shared, backend-agnostic `PixelTestGame::CompareGoldenImage()` helper (`examples/common/PixelTestGame.hpp`, real public `Game`/`GraphicsDevice`/`Texture2D` API only), same reuse precedent Vulkan's own golden-image tests already established (`VulkanTests.cmake`). Software's own golden-image suite does not exist yet (only EasyGL/Vulkan do); "shared with Software/EasyGL" is honored in spirit -- OPENGL1 reuses the same PNGs any future Software golden suite would too. 9 of EasyGL's golden scenes reused verbatim, all pixel-matching the checked-in reference on the first attempt despite OPENGL1 being a completely different (legacy fixed-function/immediate-mode) rasterizer from EasyGL's shader-based one: the smoke canary (flat clear), `BasicEffect` (textured+vertex-color+diffuse/emissive), `SpriteBatch` rotation, texture-filter linear blending, `BlendState.Additive`, `DualTextureEffect`, `RasterizerState.CullMode`, `DepthStencilState` write-enable, and `AlphaTestEffect` (`CompareFunction::Greater` with alpha clearly above the reference -- within the "coarse approximation" subset `glAlphaFunc(GL_GEQUAL,...)` can honestly reproduce). Every reused scene was deliberately chosen for a flat, edge-free 8x8 sample region (constant UV, no antialiased boundary in the checked region) -- this is what makes cross-rasterizer reuse safe without a wider tolerance. Deliberately excluded: `PbrEffect`/`SkinnedEffect`/`SkinnedPbrEffect` (GLSL-shader-only, no fixed-function equivalent -- this backend's own design rule) and `EnvironmentMapEffect`'s golden scene (exercises Fresnel/specular, phase 5's own documented unimplemented limitation -- a guaranteed mismatch, not a useful test).
11. ~~Add explicit `GraphicsCapability` reporting so unsupported shader-era features return false instead of over-reporting support~~ **Done** (audit, no source change needed): `GraphicsDevice::SupportsCapability()`'s existing OPENGL1 truth table was already correct after phase 1's anisotropic fix -- `ThreeD`/`DepthStencilBuffer`/`WireFrame` true, `AnisotropicFiltering` tracks the real runtime-detected extension, and `MultiSampleAntiAliasing`/`MultipleRenderTargets`/`OcclusionQuery`/`CustomEffects` correctly report false (none of these are implemented by this backend). Locked in by `OpenGL1_GraphicsCapability`, which cross-checks every value against independent evidence rather than trusting the flag alone: `AnisotropicFiltering` against a direct `glGetString(GL_EXTENSIONS)` scan, `OcclusionQuery=false` against a real `OcclusionQuery` object that never completes, `WireFrame=true` against an actual `glGetIntegerv(GL_POLYGON_MODE)` readback.
12. Audit XNA Reach-profile behavior against what the fixed-function pipeline can reproduce exactly.

## Bugs found while adding test coverage (2026-07-19)

Fixed alongside the initial `void*`/`SDL_GLContext` build error, `ApplyRasterizerState`'s
inverted `GL_FRONT`/`GL_BACK` mapping, and `SpriteBatch`'s double-applied origin offset (all
found and fixed the same day the backend was first run — see git log): the stride==20
(`VertexPositionTexture`) vertex path in `DrawInternal` ignored `GpuDrawParams::diffuseColor`
entirely (hardcoded white), and the window's GL context requested `SDL_GL_STENCIL_SIZE=8` too
late (after `SDL_CreateWindow`) to take effect on X11/GLX, silently producing a 0-bit stencil
buffer and making every `DepthStencilState.StencilEnable` a no-op. See
`tests/opengl1/README.md`'s "Bugs found and fixed" section for the full detail on each.

Three more found while implementing and round-trip-testing RenderTarget2D (same day):
- `OpenGL1SpriteBatchBackend::Begin()` never disabled `GL_CULL_FACE` -- real XNA `SpriteBatch`
  is never subject to 3D face culling, but a game's `RasterizerState.CullMode` (left over from
  its own 3D rendering) could silently make sprites disappear depending on winding. Latent on
  the default backbuffer (the fixed quad winding happened to survive the default
  `CullCounterClockwiseFace` state) but became directly observable once render-target rendering
  needed a genuinely different code path to render into.
- `GL_CLAMP` (not `GL_CLAMP_TO_EDGE`) was used for texture wrap mode everywhere (texture
  creation, `ApplySamplerState`, the new RT color texture) -- `GL_CLAMP`'s border-color
  blending (toward transparent black) kicks in for `GL_LINEAR`-filtered samples anywhere near a
  texture edge, which for a very small texture (e.g. a 1x1 solid-color texture, a common
  pattern) is effectively everywhere except the exact texel centre. A general texture-fidelity
  bug, not RT-specific, but only surfaced once a test sampled off-centre. Fixed everywhere.
- A render target's color texture is written by the GPU rasterizer, whose row 0 is the BOTTOM
  of what was drawn into it (standard GL framebuffer convention) -- the opposite of every
  CPU-uploaded texture's row 0 (this project's own convention: the TOP, since images upload
  top-down). `OpenGL1RenderTargetBackend::GetData()`'s own row-flip already corrects this for
  CPU readback, but that flip does *not* apply when the render target is instead sampled
  directly as a texture (drawing it via `SpriteBatch`, or binding it as `BasicEffect.Texture`)
  -- confirmed the two paths are genuinely independent by testing them separately. Fixed with a
  V-coordinate swap for `SpriteBatch::Draw()` (checked via `dynamic_cast<const
  IRenderTargetBackend*>`) and an equivalent `GL_TEXTURE` matrix flip in `DrawInternal` for the
  3D path (texcoords there are baked into the vertex buffer, not computed per-draw).

One more found while implementing `EnvironmentMapEffect`'s cube-map support (same day):
- `DrawInternal`'s lit (stride 32, `VertexPositionNormalTexture`) path enabled `GL_COLOR_MATERIAL`
  with `GL_AMBIENT_AND_DIFFUSE` but never set `GL_EMISSION` at all -- any lit draw's
  `EmissiveColor` (`BasicEffect`) or combined `EmissiveColor`+`AmbientLightColor*DiffuseColor`
  (`EnvironmentMapEffect`, which does not populate `GpuDrawParams::ambientColor` the way
  `BasicEffect` does -- it pre-combines everything into `emissiveColor` instead) was silently
  dropped from the rendered color entirely. Real XNA/GL fixed-function lighting sums material
  emission flatly regardless of any light being on or off. Fixed with
  `glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,...)` from `GpuDrawParams::emissiveColor` alongside
  the existing ambient/diffuse material setup. Caught while building `OpenGL1_EnvironmentMapEffect`
  -- without this fix the test's "pure base color" (`Amount=0`) case would have read back as black
  instead of the expected `AmbientLightColor*DiffuseColor`.

## Design rule
Do not turn OPENGL1 into a second modern OpenGL backend. Features should be implemented with true legacy/fixed-function OpenGL or well-defined period-compatible extensions. Shader-dependent XNA/NOXNA features should remain unsupported rather than secretly delegating to EasyGL.
