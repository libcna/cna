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
- Multitexture effects such as `DualTextureEffect` require OpenGL 1.3 or `ARB_multitexture`; strict 1.1 currently falls back to texture unit 0 only.
- Blend equations beyond additive blending and constant blend color need later extension/version detection.
- Anisotropic filtering requires `GL_EXT_texture_filter_anisotropic`, detected at runtime (`OpenGL1Capabilities::anisotropicFiltering`, `GraphicsCapability::AnisotropicFiltering`); silently falls back to no anisotropy (clamped to 1.0x) when the driver lacks it.

## Next implementation phases
1. ~~Add runtime GL version/extension discovery~~ **Done**: `OpenGL1Capabilities`/`DetectOpenGL1Capabilities()` (`OpenGL1Capabilities.hpp`/`.cpp`) query `GL_VERSION`/`GL_EXTENSIONS` directly (no loader library, no EasyGL dependency) once per context, right after `SDL_GL_MakeCurrent`, and expose version + `framebufferObject`/`multitexture`/`textureCubeMap`/`generateMipmap`/`anisotropicFiltering` flags for phases 2/3/5/6 below to consult. First real consumer wired end-to-end: `GraphicsCapability::AnisotropicFiltering` now reports the real detected value instead of a hardcoded `false`, and `ApplySamplerState()` actually sets `GL_TEXTURE_MAX_ANISOTROPY_EXT` when `TextureFilter::Anisotropic` is requested and the extension is present (verified against the real driver, including over-cap clamping and reset-to-1.0 on a non-anisotropic filter, by `OpenGL1_Anisotropic_GlState`).
2. ~~Add `ARB_framebuffer_object` RenderTarget2D support~~ **Done**: `OpenGL1RenderTargetBackend` (`OpenGL1RenderTargetBackend.hpp`/`.cpp`) creates a color texture + optional depth/stencil renderbuffer + FBO via ARB/core-3.0 entry points loaded through `SDL_GL_GetProcAddress` (`TryLoadOpenGL1FramebufferObjectFunctions()`); `CreateRenderTarget2D()` returns nullptr when `OpenGL1Capabilities::framebufferObject` is false or the FBO ends up incomplete (strict capability fallback, no partial/broken render target). Found and fixed three real bugs while getting this to round-trip correctly (render into a target, sample it back as a texture) -- see the "Bugs found" section below.
3. Add `ARB_multitexture`/OpenGL 1.3 dual-texture fixed-function path.
4. Add fixed-function texture environment/combine mappings for the XNA effects that can be represented honestly.
5. Add cube maps where `ARB_texture_cube_map` exists and implement the subset of EnvironmentMapEffect representable by texture coordinate generation/combine.
6. Add mipmap generation via available extensions or CPU fallback.
7. ~~Add readback~~ **Done**: `ReadBackbuffer` implemented (`glReadPixels` off `GL_BACK`). Render-target readback is still N/A (blocked on item 2, RenderTarget2D).
8. Add context-loss resource recreation registry, separate from EasyGL.
9. ~~Add Linux X11/Xvfb smoke tests~~ **Done** (Linux half): `tests/opengl1/README.md`'s 8 priority scenarios are wired to real CTest registrations (`cmake/Tests/OpenGL1Tests.cmake`), 8/8 passing under Xvfb/X11+Mesa llvmpipe. Windows GitHub Actions compile/smoke jobs still open.
10. Add visual golden-image tests shared with Software/EasyGL for the supported fixed-function subset.
11. Add explicit `GraphicsCapability` reporting so unsupported shader-era features return false instead of over-reporting support.
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

## Design rule
Do not turn OPENGL1 into a second modern OpenGL backend. Features should be implemented with true legacy/fixed-function OpenGL or well-defined period-compatible extensions. Shader-dependent XNA/NOXNA features should remain unsupported rather than secretly delegating to EasyGL.
