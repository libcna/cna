# OpenVG renderer

## Current status

`CNA_GRAPHICS_RENDERER=OPENVG` is a 2D vector-graphics renderer implemented through
[ShivaVG](https://github.com/ileben/ShivaVG) (pinned commit `6122ccb3c4b86f69a326f1a65b0f86bc79f69c50`,
LGPL-2.1), an ANSI C implementation of the Khronos OpenVG 1.1 API on top of fixed-function
(immediate-mode) desktop OpenGL. CNA creates and owns a real desktop OpenGL compatibility-profile
context itself (via SDL, the same "own GL context, no EasyGL" shape as `OPENGL1`/`OPENGL2`), then
attaches ShivaVG's OpenVG context to it with `vgCreateContextSH`. Every draw goes through genuine
`vg*` entry points (`vgClear`, `vgCreateImage`, `vgDrawImage`, `vgDrawPath`, ...), not a
reimplementation of OpenVG's semantics.

    CNA -> OpenVgRenderer -> ShivaVG (real vgCreatePath/vgDrawPath/vgCreateImage/vgDrawImage/...) -> OpenGL

See `openvg-spike/README.md` for the standalone existence-gate proof (a real path drawn and read
back via `glReadPixels`, under Xvfb) that predates this renderer's CNA integration.

OpenVG is a 2D-only vector-graphics API: it has no 3D pipeline, no vertex/index buffer concept, and
no depth/stencil buffer at all. Every inherently-3D `IGraphicsRenderer` pure virtual
(`ClearColorAndDepth` and its siblings, `CreateVertexBuffer`, `CreateIndexBuffer16`,
`DrawColoredPrimitives`, `DrawIndexedColoredPrimitives`, ...) throws
(`Unsupported3DGraphicsCallBehavior::Throw`, the shared default) rather than being silently ignored
or faked.

## Verified capability boundary

| CNA feature | OpenVG route | Notes |
|---|---|---|
| `Clear`, `Present` | Real `vgSetfv(VG_CLEAR_COLOR,...)` + `vgClear`, `SDL_GL_SwapWindow` | |
| `Texture2D` create/update | Real `VGImage` via `vgCreateImage`/`vgImageSubData` | `VG_sABGR_8888` (straight, non-premultiplied alpha) -- **not** the more obviously-named `VG_sRGBA_8888`: ShivaVG maps that to a packed GL upload type that reinterprets memory bytes as A,B,G,R on a little-endian host (found empirically; see `OpenVgTextureRenderer.cpp`'s own comment), while `VG_sABGR_8888` maps to the REV variant that reproduces true R,G,B,A memory order. Uploads also use OpenVG's documented negative-`dataStride` trick to reconcile `ImageData`'s top-row-first convention with OpenVG's Y-up image space -- no CPU-side row flip. |
| `SpriteBatch` draw (all 3 overloads, rotation, origin, source rectangle, tint, `SpriteEffects` flip) | Real `vgDrawImage`, transformed via `VG_MATRIX_IMAGE_USER_TO_SURFACE`. A whole-texture `sourceRectangle` draws the texture's own `VGImage` directly; a partial one copies the requested region into a temporary `VGImage` via `vgCopyImage` first (`vgChildImage` is declared by ShivaVG's `openvg.h` but its `src/shImage.c` body is an unconditional `return VG_INVALID_HANDLE` stub -- found empirically, see `OpenVgSpriteBatchRenderer.cpp`'s own comment) | Tint uses `VG_DRAW_IMAGE_MULTIPLY` with a real `VGPaint`. Verified by `openvg_spritebatch_rotation_test.cpp`, a direct pixel-oracle port of the SDL_Renderer/EasyGL rotation-around-origin test. |
| `SpriteBatch.SetTransformMatrix` | Real `vgMultMatrix` | Row-major XNA `Matrix` decomposed to a 2D affine, same `(a,b,c,d,e,f)` convention `CanvasRenderer` uses. |
| Scissor rect | Real `VG_SCISSORING`/`VG_SCISSOR_RECTS` | |
| Viewport | Real `glViewport` (OpenVG has no separate viewport concept from its GL-backed surface) | |
| Backbuffer readback | Real `glReadPixels` against the same GL framebuffer ShivaVG rendered into | |
| `BlendState.Opaque` | Real `VG_BLEND_SRC` | ShivaVG implements this for real (blending disabled). |
| `BlendState.NonPremultiplied` | Real `VG_BLEND_SRC_OVER` | ShivaVG implements this for real (`glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)`). |
| `BlendState.AlphaBlend` | Mapped to `VG_BLEND_SRC_OVER` too | **Documented deviation**: XNA's `AlphaBlend` preset (`SourceBlend=One`) technically assumes an already-premultiplied source. This renderer's textures are never premultiplied (see the `Texture2D` row above), so for every texture this renderer can actually produce, `VG_BLEND_SRC_OVER`'s real straight-alpha-over blend func computes the visually correct composite anyway. Diverges from real XNA only if a caller manually uploads already-premultiplied bytes via `Texture2D.SetData`. |
| `BlendState.Additive` | **Rejected** (`ApplyBlendState` throws) | `VG_BLEND_ADDITIVE` is declared by the OpenVG 1.1 spec/`openvg.h` but has no case in ShivaVG's own `updateBlendingStateGL` (`src/shPipeline.c`) -- it silently falls through to normal alpha blending. Accepting it would be a false capability claim, so it is rejected deterministically instead. `SupportsCapability(AdditiveBlending)` reports `false`. |
| Render targets (`RenderTarget2D`/`RenderTargetCube`) | **Unsupported** (`CreateRenderTarget2D` returns `nullptr`, the shared default) | ShivaVG has no EGL-VGImage-surface/FBO equivalent to bind an off-screen `VGImage` as a draw target. `SetRenderTargets(count > 0)` throws. |
| `TextureAddressMode.Wrap`/`Mirror` combined with an out-of-bounds `sourceRectangle` | **Rejected** (throws) | ShivaVG has no tiled-pattern image-draw path; `Clamp` (the default) and any in-bounds source rectangle work normally. |
| Custom `Effect` in `SpriteBatch` | **Rejected** (throws) | No programmable shader stage exists on this renderer (ShivaVG is fixed-function GL). |
| `GraphicsCapability.ThreeD`, `DepthStencilBuffer`, `MultipleRenderTargets`, `OcclusionQuery`, `CustomEffects`, `AdditiveBlending`, `Texture3D` | All report `false` | See `SupportsCapability`/`SupportsDepthStencil` overrides in `OpenVgRenderer`. `Texture3D` false matches `Texture3DTests.cpp`'s own dual fixture (`Texture3DTest` self-skips, `Texture3DUnsupportedRendererTest` asserts the constructor throws) -- see "Known cross-renderer test gaps" below for the one shared fixture this leaves unregistered. |
| Every 3D entry point (vertex/index buffers, `DrawColoredPrimitives`, depth/stencil clears, `SetDepthTestEnabled`, ...) | **Rejected** (throws via the shared `HandleUnsupported3DCall`) | OpenVG has no 3D pipeline at all. |

## Dependency and build

`cmake/ThirdPartyOpenVG.cmake` fetches ShivaVG's source at the pinned commit via `FetchContent` and
compiles its 11 core `.c` files directly (ShivaVG has no CMake of its own -- only a 2010-era
autotools build). Two small from-2010-codebase compatibility gaps against a modern host toolchain
are bridged there with compiler flags/generated files, not by patching ShivaVG's own source -- see
that file's own comments for the exact mechanism (a generated one-line `config.h`, and a
`GLintptr`/`GLsizeiptr` typedef shim ahead of ShivaVG's translation units). ShivaVG links real
`OpenGL::GL`/`OpenGL::GLU` (`shImage.c`/`shContext.c` call `gluScaleImage`/`gluOrtho2D` for real).

## Supported platforms

Desktop Linux, Windows, and macOS only (`cmake/RendererSelection.cmake`'s own platform gate) --
ShivaVG needs a real fixed-function/immediate-mode OpenGL context, which neither WebGL
(Emscripten) nor a GLES-only mobile target can create. Native validation in this project's CI
environment is Linux + Xvfb (X11 GLX); Windows/macOS are the same upstream-documented ShivaVG
build targets but have not been run in this environment.

## Known cross-renderer test gaps

Running the full `CnaTests` corpus against `-DCNA_GRAPHICS_RENDERER=OPENVG` for the first time (no
prior CNA renderer was both this consistently 2D-only *and* this honest about it -- see
`GraphicsCapability` table above) surfaced two categories of pre-existing shared-test gap:

* **`RenderTarget2D` genuinely unsupported.** `Texture2DCacheReconstructionTests.cpp`'s
  `RenderTargetKeepsItsRendererAndDropsTheShadowOnUpload` and
  `RenderTargetReadbackComesFromTheSurfaceNotAnUploadShadow` assumed every renderer provides real
  `RenderTarget2D` storage -- true of every renderer that existed before OpenVG, including the
  other 2D-only ones (SDL_Renderer, ASCII, Canvas, GDI, ...), which all still have a real 2D
  draw-to-texture path. OpenVG does not: ShivaVG's non-EGL context extension binds the OpenVG
  pipeline to one real window surface only, with no pbuffer/FBO-backed off-screen `VGImage`
  surface to bind as a draw target. Fixed with a minimal, isolated `CNA_RENDERER_OPENVG`-gated
  `kRenderTarget2DSupported` constant and `GTEST_SKIP()` in both tests, mirroring this file's own
  existing `TextureCube`-style capability-gate precedent.

* **Pre-existing "content pipeline assumes a 3D pipeline" gap, not introduced by OpenVG.** ~85
  tests across `Model`/`Cnj`/`Gltf`/`SkinnedModel`-loading suites (`XnbBuiltInReaderRegistrationTest`,
  `CnjModelTest`, `GltfToCnjToolTest`, `RuntimeGltfModelTest`, `SkinnedModelEXTPartTest`,
  `ModelContentTypeReaderTest`, `ContentPathContainmentTest`, ...) and the direct
  `VertexBuffer`/`IndexBuffer`/`Effect`/instanced-draw suites construct a real `VertexBuffer`
  (directly, or transitively through `Model` loading) with no `GraphicsCapability::ThreeD` guard.
  `OpenVgRenderer::CreateVertexBuffer` honestly throws (`HandleUnsupported3DCall`) under the
  default `Throw` policy -- the exact same call every other 2D-only renderer already in this
  registry (SDL_Renderer, ASCII, Canvas, GDI, HTML_DOM, FreeDirect, Headless-adjacent) makes for
  the identical entry point (verified by reading `SdlRenderer.cpp`/`AsciiRenderer.cpp`; both call
  `HandleUnsupported3DCall(..., "CreateVertexBuffer")` too). No prior renderer in this registry
  combination has apparently exercised this exact suite before, so the gap was latent rather than
  newly caused by OpenVG. The one non-3D outlier, `MetalResourceHealth`'s
  `RenderTargetCubeRendererEscapesThroughTextureCubeBaseMove` (a "host-portable" test compiled into
  every renderer's `CnaTests`, not Metal-specific despite its module location), asserts a non-null
  `RenderTargetCube` renderer identity and would fail identically for any renderer already in
  `TextureCubeTests.cpp`'s `kCubeStorageSupported = false` list, for the same silent-nullptr
  construction reason `RenderTargetCube`/`TextureCube` already document elsewhere.

  These ~85 tests were deliberately left unmodified rather than retrofitted with
  `GraphicsCapability::ThreeD` guards across a dozen content-module files: that is a genuine,
  useful follow-up, but it is a general content-pipeline/2D-renderer-family gap outside this
  renderer's own minimal-shared-change scope, not an OpenVG regression.

## Known limitations (upstream, not CNA-specific)

`vgChildImage` is declared but unconditionally returns `VG_INVALID_HANDLE` (not documented in
`TODO`/`README` -- found empirically); this renderer's `SpriteBatch` path works around it with
`vgCopyImage` instead (see the table above). Per ShivaVG's own `TODO`/`README`: masking (`vgMask`), path length queries
(`vgPathLength`/`vgPointAlongPath`), image filters (`vgGaussianBlur`, `vgConvolve`,
`vgColorMatrix`, `vgLookup*`), `vgHardwareQuery`, and warp computation functions are declared but
not implemented. `vgSetColor`/`vgGetColor` are declared in `openvg.h` but have no body in this
revision's `shPaint.c` either -- OpenVgSpriteBatchRenderer's tint path uses the underlying
`vgSetParameterfv(paint, VG_PAINT_COLOR, ...)` primitive instead, which IS implemented. No
multi-threading support.

**Fixed-size leak per renderer lifecycle (AddressSanitizer/LeakSanitizer, upstream).** Running the
`OpenVg`-labelled examples and the graphics test suites under `-DCNA_SANITIZE=address,undefined`
reports a constant 64-byte / 5-allocation leak for every `OpenVgRenderer` construct/destroy cycle
(`vgCreateContextSH` / `vgDestroyContextSH`) -- e.g. 5088 bytes / 390 allocations across a ~78-cycle
graphics test run, i.e. exactly 64/5 bytes-per-cycle throughout, never growing per draw call, per
texture, or per frame. Traced to `shContext.c`'s `VGContext_ctor`/`VGContext_dtor`: the constructor
calls `SH_INITOBJ` on five dynamic arrays/objects (`c->scissor`, `c->strokeDashPattern`,
`c->paths`, `c->paints`, `c->images`), but the destructor only frees the *elements inside*
`paths`/`paints`/`images` (`SH_DELETEOBJ` in a loop) and calls `SH_DEINITOBJ` on `scissor` and
`strokeDashPattern` only -- it never deinits the three backing `SHPathArray`/`SHPaintArray`/
`SHImageArray` structures themselves, nor `c->defaultPaint`. This is a genuine upstream ShivaVG
bug (a ctor/dtor asymmetry inside the vendored library itself), not CNA glue code: `OpenVgRenderer`
calls `vgCreateContextSH` and `vgDestroyContextSH` symmetrically (`OpenVgRenderer.cpp`'s
constructor/destructor), and per this project's own `cmake/ThirdPartyOpenVG.cmake` convention,
upstream gaps are bridged from CNA's side rather than by patching ShivaVG's vendored source. No
other AddressSanitizer/UndefinedBehaviorSanitizer defect (heap/stack overflow, use-after-free,
undefined-behavior runtime error) was observed anywhere in this renderer's own tests or in the
broader graphics/SpriteBatch/Texture2D/BlendState/Viewport suite run against it.
