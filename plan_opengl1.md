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
- RenderTarget2D/cube requires an optional FBO extension path and is not part of the strict 1.1 baseline yet.
- Multitexture effects such as `DualTextureEffect` require OpenGL 1.3 or `ARB_multitexture`; strict 1.1 currently falls back to texture unit 0 only.
- Blend equations beyond additive blending and constant blend color need later extension/version detection.
- Anisotropic filtering requires `GL_EXT_texture_filter_anisotropic`, detected at runtime (`OpenGL1Capabilities::anisotropicFiltering`, `GraphicsCapability::AnisotropicFiltering`); silently falls back to no anisotropy (clamped to 1.0x) when the driver lacks it.

## Next implementation phases
1. ~~Add runtime GL version/extension discovery~~ **Done**: `OpenGL1Capabilities`/`DetectOpenGL1Capabilities()` (`OpenGL1Capabilities.hpp`/`.cpp`) query `GL_VERSION`/`GL_EXTENSIONS` directly (no loader library, no EasyGL dependency) once per context, right after `SDL_GL_MakeCurrent`, and expose version + `framebufferObject`/`multitexture`/`textureCubeMap`/`generateMipmap`/`anisotropicFiltering` flags for phases 2/3/5/6 below to consult. First real consumer wired end-to-end: `GraphicsCapability::AnisotropicFiltering` now reports the real detected value instead of a hardcoded `false`, and `ApplySamplerState()` actually sets `GL_TEXTURE_MAX_ANISOTROPY_EXT` when `TextureFilter::Anisotropic` is requested and the extension is present (verified against the real driver, including over-cap clamping and reset-to-1.0 on a non-anisotropic filter, by `OpenGL1_Anisotropic_GlState`).
2. Add `EXT_framebuffer_object` / `ARB_framebuffer_object` RenderTarget2D support when available, preserving a strict capability fallback.
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

## Design rule
Do not turn OPENGL1 into a second modern OpenGL backend. Features should be implemented with true legacy/fixed-function OpenGL or well-defined period-compatible extensions. Shader-dependent XNA/NOXNA features should remain unsupported rather than secretly delegating to EasyGL.
