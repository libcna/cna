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
or faked, and `Ensure3DSupported()` is overridden so every modern `GraphicsDevice.Draw*`/
`DrawIndexed*`/`DrawInstanced*`/`DrawUser*` entry point is rejected by that guard BEFORE any
vertex/index-buffer validation or sampler-state application runs, not merely once native
submission is reached.

This document was rewritten after an end-to-end correctness audit (presentation/viewport/scissor
math, 3D/render-target guards, ShivaVG context lifetime, texture format, blend modes) added real
pixel-level regression coverage for every claim below -- see "Test status" for the exact test
files. Nothing here is asserted without a corresponding test.

## Presentation model

CNA's virtual-resolution/presentation-mode feature (`CnaPresentationMode`: `Letterbox`, `Overscan`,
`Stretch`, `NativeBackBuffer`, `FixedHeightDynamicWidth`) is fully implemented, not only
`NativeBackBuffer`/`FixedHeightDynamicWidth`. One shared computation,
`OpenVgRenderer::ComputeLogicalViewportEXT()`, is the single source of truth for the mapping from
logical (virtual-resolution) coordinates to the physical framebuffer, and every consumer below is
built on it -- there is no separate, independently-derived math anywhere else in this renderer:

- **`GetViewportSize()`** (`GraphicsDevice.Viewport.Width/Height`) returns the LOGICAL size.
- **`GetDefaultViewportRect()`** returns the PHYSICAL sub-rectangle
  (`GraphicsDevice::UpdateViewportFromWindow()` applies this as the real `glViewport`) --
  identical to `(0, 0, physicalWidth, physicalHeight)` for `NativeBackBuffer`/`Stretch`/
  `FixedHeightDynamicWidth`; centred and shrunk (with bars) for `Letterbox`; centred and grown
  (cropped) for `Overscan`. This mirrors `OpenGL2Renderer`'s own `GetDefaultViewportRect()` --
  the reference implementation this codebase's own base-interface doc comment names.
- **The `GL_PROJECTION` orthographic matrix** (`OpenVgRenderer::applyOrtho()`) is always scoped to
  the CURRENT LOGICAL size, never the physical framebuffer size -- this is the mechanism that
  actually applies presentation scaling to rasterization (P0-1's central defect: the previous
  implementation's per-draw device-flip transform used the physical framebuffer height
  unconditionally, so Letterbox/Overscan/Stretch reported plausible logical numbers but never
  actually placed pixels correctly).
- **`OpenVgSpriteBatchRenderer`'s per-draw device-flip transform** is scoped to
  `OpenVgRenderer::GetLogicalHeightEXT()` (the current logical canvas height), not the physical
  framebuffer height -- sprites are always specified and composed in logical coordinates; the
  ortho + the active `glViewport` rectangle carry the entire logical->physical mapping as a real
  GPU operation, exactly like `OpenGL2Renderer`'s own CPU-computed-NDC model, just expressed
  through `GL_PROJECTION` instead of per-vertex division (ShivaVG has no programmable vertex
  stage to do the division itself).
- **`SetViewport(x, y, w, h, ...)`** is a raw pass-through (only a Y-flip is applied) -- matching
  every sibling renderer's own established convention (`GdiRenderer`/`HtmlDomRenderer`/
  `OpenGL2Renderer`'s own `SetViewport`, all of which forward their arguments as-is without
  presentation-mode-aware rewriting; `GraphicsDevice.cpp` itself does the same). A custom viewport
  therefore behaves exactly like a standard GPU viewport: the CURRENT logical canvas (still sized
  by `ComputeLogicalViewportEXT()`, independent of the custom rectangle's own width/height) is
  mapped into whatever physical rectangle was requested.
- **`SetScissorRect(x, y, w, h)`** is presentation-mapped (through the same
  `ComputeLogicalViewportEXT()`), unlike `SetViewport` -- a deliberate improvement over
  `OpenGL2Renderer`'s own scissor handling (which passes its arguments through raw, an existing,
  separate gap in that renderer outside this task's scope): `GraphicsDevice`'s own scissor-rect
  auto-reset always uses LOGICAL coordinates in the SAME absolute logical-canvas space sprite
  draws use, with no presentation-aware conversion applied anywhere else in the shared layer, so a
  renderer that treats them as already-physical is wrong under any non-1:1 presentation mode.
- **`TransformWindowToLogical`/`TransformLogicalToWindow`** are fully presentation-mode-aware
  (Letterbox bars/Overscan crop correctly excluded/included) and separately account for a
  high-DPI window scale factor: `windowX`/`windowY` are SDL window-POINT coordinates (matching
  SDL's own mouse/touch event convention), scaled to physical pixels via
  `SDL_GetWindowSizeInPixels()`/`SDL_GetWindowSize()` before the presentation-rect math runs --
  `ComputeLogicalViewportEXT()` itself is physical-pixel-based throughout for exactly this reason
  (`OpenGL2Renderer`'s own equivalent conflates window points and physical pixels; this renderer
  deliberately does not).
- **Physical surface resynchronization** (`EnsureSurfaceSizeEXT()`) runs at the top of every entry
  point whose correctness depends on the current physical size (`Clear`, `SetViewport`,
  `SetScissorRect`, every `SpriteBatch` draw, `ReadBackbuffer`) -- not only `Clear()` -- so a
  caller may resize the window and draw with no `Clear()` call at all (P0-4). It re-syncs
  ShivaVG's own internal `context->surfaceWidth/surfaceHeight` (`vgResizeSurfaceSH`, which
  `vgClear`'s own clip-to-window clamp in `shContext.c` depends on) and reapplies this renderer's
  own authoritative ortho/viewport/scissor state on top, since `vgResizeSurfaceSH` itself
  overwrites both as a side effect.
- **`Clear()`** no longer touches the surface size, viewport, or scissor state at all beyond the
  read-only resync above -- it only clears the color buffer (P0-3: a custom `Viewport`/
  `ScissorRectangle` now survives `Clear()`).

## Verified capability boundary

| CNA feature | OpenVG route | Notes |
|---|---|---|
| `Clear`, `Present` | Real `vgSetfv(VG_CLEAR_COLOR,...)` + `vgClear`, `SDL_GL_SwapWindow` | Clear no longer mutates viewport/scissor/surface-size state beyond resynchronizing to the current physical size (P0-3). |
| Presentation modes (`Letterbox`/`Overscan`/`Stretch`/`NativeBackBuffer`/`FixedHeightDynamicWidth`) | Real `GL_PROJECTION` ortho scoped to the logical canvas + real `glViewport` for the physical sub-rectangle | All five modes are implemented and pixel-tested, not only `NativeBackBuffer`/`FixedHeightDynamicWidth` (P0-1). See "Presentation model" above. |
| Custom `Viewport` | Real `glViewport`, raw pass-through (matches every sibling renderer) | Pixel-tested placement; survives `Clear()` and window resize (P0-2/P0-3/P0-4). |
| `ScissorRectangle` / `RasterizerState.ScissorTestEnable` | Real `VG_SCISSOR_RECTS` (presentation-mapped) / `VG_SCISSORING` | The rectangle (`SetScissorRect`) and the enable flag (`ApplyRasterizerState`) are fully independent -- setting a rectangle no longer implicitly enables scissoring (P0-5). |
| `Texture2D` create/update | Real `VGImage` via `vgCreateImage`/`vgImageSubData` | `VG_sABGR_8888` (straight, non-premultiplied alpha) -- **not** the more obviously-named `VG_sRGBA_8888`: ShivaVG maps that to a packed GL upload type that reinterprets memory bytes as A,B,G,R on a little-endian host (found empirically; see `OpenVgTextureRenderer.cpp`'s own comment), while `VG_sABGR_8888` maps to the REV variant that reproduces true R,G,B,A memory order. Uploaded with a plain POSITIVE stride, no row reversal in either direction -- orientation is decided entirely by `OpenVgSpriteBatchRenderer`'s own per-draw device-flip transform (verified: a row flip baked in at upload time only cancels out correctly for a non-rotated draw, since `vgDrawImage`'s texcoord generation is independent of the modelview/rotation matrix). Proven with a decisive asymmetric-texel pixel oracle (P1-3, `openvg_texture_orientation_test.cpp`). |
| `SpriteBatch` draw (all 3 overloads, rotation, origin, source rectangle, tint, `SpriteEffects` flip) | Real `vgDrawImage`, transformed via `VG_MATRIX_IMAGE_USER_TO_SURFACE` in LOGICAL coordinates | A whole-texture `sourceRectangle` draws the texture's own `VGImage` directly; a fully in-bounds partial one copies the requested region into a temporary `VGImage` via `vgCopyImage` (`vgChildImage` is declared by ShivaVG's `openvg.h` but its `src/shImage.c` body is an unconditional `return VG_INVALID_HANDLE` stub -- found empirically). Tint uses `VG_DRAW_IMAGE_MULTIPLY` with a real `VGPaint`. Verified by `openvg_spritebatch_rotation_test.cpp` and `openvg_texture_orientation_test.cpp`. |
| `SpriteBatch.SetTransformMatrix` | Real `vgMultMatrix` | Row-major XNA `Matrix` decomposed to a 2D affine, same `(a,b,c,d,e,f)` convention `CanvasRenderer` uses. |
| Out-of-bounds `sourceRectangle` + `TextureAddressMode.Clamp` | Real edge-clamp: a CPU-side padded `VGImage` sized to the FULL requested rectangle, with out-of-bounds texels repeating their nearest in-bounds edge texel | Previously silently cropped to the in-bounds region instead (P1-4) -- indistinguishable from clamp only when the excess region happened to render nothing. Pixel-tested for all four edges and simultaneous left+top. |
| `TextureAddressMode.Wrap`/`Mirror` combined with an out-of-bounds `sourceRectangle` | **Rejected** (throws) | ShivaVG has no tiled-pattern image-draw path; `Clamp` (the default) and any in-bounds source rectangle work normally. |
| Backbuffer readback | Real `glReadPixels` against the same GL framebuffer ShivaVG rendered into | Physical pixel coordinates, resynchronized to the current physical size before reading (`EnsureSurfaceSizeEXT()`). |
| `BlendState.Opaque` | Real `VG_BLEND_SRC` | ShivaVG implements this for real (blending disabled). Pixel-tested. |
| `BlendState.NonPremultiplied` | Real `VG_BLEND_SRC_OVER` | ShivaVG implements this for real (`glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)`). Pixel-tested against a computed expected composite. |
| `BlendState.AlphaBlend` | Mapped to `VG_BLEND_SRC_OVER` too | **Documented deviation**: XNA's `AlphaBlend` preset (`SourceBlend=One`) technically assumes an already-premultiplied source. This renderer's textures are never premultiplied (see the `Texture2D` row above), so for every texture this renderer can actually produce, `VG_BLEND_SRC_OVER`'s real straight-alpha-over blend func computes the visually correct composite anyway. Diverges from real XNA only if a caller manually uploads already-premultiplied bytes via `Texture2D.SetData`. Pixel-tested. |
| `BlendState.Additive` | **Rejected** (`ApplyBlendState` throws) | `VG_BLEND_ADDITIVE` is declared by the OpenVG 1.1 spec/`openvg.h` but has no case in ShivaVG's own `updateBlendingStateGL` (`src/shPipeline.c`) -- it silently falls through to normal alpha blending. Accepting it would be a false capability claim, so it is rejected deterministically instead. `SupportsCapability(AdditiveBlending)` reports `false`. |
| Arbitrary custom `BlendState` combinations | **Rejected** (throws) | No generic blend-factor/equation model exists on this renderer -- pixel-tested (both Additive and an arbitrary factor combination). |
| `BlendState.ColorWriteChannels` | Real per-draw `glColorMask`, wrapped tightly around each `vgDrawImage` call | Verified safe against ShivaVG's own internal `glColorMask` usage: `vgDrawImage` only ever touches it on its `VG_DRAW_IMAGE_MULTIPLY`-with-a-non-color-paint branch, which this renderer's tint path (always `VG_PAINT_TYPE_COLOR`) never takes (P1-5). Pixel-tested. |
| `BlendState.MultiSampleMask` | Intentionally ignored (not honored, not rejected) | OpenVG never creates a multisample-capable GL context (`GraphicsCapability.MultiSampleAntiAliasing` is `false`), so a coverage mask can never have any observable effect here -- inert bookkeeping, not a dropped feature. |
| `RasterizerState.CullMode` | Accepted for every value (including the device-default `CullCounterClockwise`) | 2D quads are never back-face culled by any CNA renderer regardless of value -- benign for the same reason a 2D renderer's default `RasterizerState` is a 3D preset that SpriteBatch never observably honors. |
| `RasterizerState.FillMode.WireFrame` | **Rejected** (throws) | No unfilled-polygon draw path exists (`vgDrawImage`/`vgDrawPath` always rasterize filled). `SupportsCapability(WireFrame)` reports `false`. |
| `RasterizerState.DepthBias`/`SlopeScaleDepthBias` | **Rejected when non-zero** (throws) | No depth buffer exists for either to bias against. |
| `DepthStencilState` | **`DepthStencilState.None` (both enables `false`) is accepted; any meaningfully-enabled depth or stencil state, or a non-zero `ReferenceStencil`, is rejected** (throws) | `DepthStencilState.None` is exactly what `SpriteBatch.Begin()` applies by default, so ordinary SpriteBatch usage and normal `GraphicsDevice` construction are unaffected (P0-7). `SupportsDepthStencil()` reports `false`. |
| Modern 3D `Draw*`/`DrawIndexed*`/`DrawInstanced*`/`DrawUser*` entry points | **Rejected by `Ensure3DSupported()`** before any vertex/index-buffer validation or sampler-state application (Throw policy) or safely no-op'd (WarnAndStub policy) | Verified through the PUBLIC `GraphicsDevice` API, not only direct renderer calls (P0-6). |
| Render targets (`RenderTarget2D`/`RenderTargetCube`) | **Unsupported** (`CreateRenderTarget2D` returns `nullptr`, the shared default) | ShivaVG has no EGL-VGImage-surface/FBO equivalent to bind an off-screen `VGImage` as a draw target. `GraphicsDevice::SetRenderTarget(RenderTarget2D*)` throws `System::NotSupportedException` TRANSACTIONALLY -- before any native/public state changes -- rather than silently leaving the real backbuffer bound while `GraphicsDevice` believed a render target was active (P0-8; fixed in the SHARED `GraphicsDevice.cpp` layer, not as an OpenVG-specific hack, so every renderer with no real `RenderTarget2D` storage gets the same correct behavior). `SetRenderTargets(vector<>, count > 0)` throws for the identical reason. |
| Custom `Effect` in `SpriteBatch` | **Rejected** (throws) | No programmable shader stage exists on this renderer (ShivaVG is fixed-function GL). |
| `GraphicsCapability.ThreeD`, `DepthStencilBuffer`, `MultipleRenderTargets`, `OcclusionQuery`, `CustomEffects`, `AdditiveBlending`, `Texture3D`, `MultiSampleAntiAliasing`, `AnisotropicFiltering`, `WireFrame`, `Instancing`, `MultiStreamVertexInput`, `StencilBuffer` | All report `false` | Every entry in `CNA::GraphicsCapability` is genuinely false for this renderer -- see `SupportsCapability`'s own comment for the per-entry reasoning. `Texture3D` false matches `Texture3DTests.cpp`'s own dual fixture (`Texture3DTest` self-skips, `Texture3DUnsupportedRendererTest` asserts the constructor throws) -- see "Known cross-renderer test gaps" below for the one shared fixture this leaves unregistered. |
| Swap interval (`GraphicsRendererCreateArgs::swapInterval` / `SetSwapInterval`) | Real `SDL_GL_SetSwapInterval`, with a fallback chain (requested -> 1 -> 0) and the ACTUALLY-applied interval read back via `SDL_GL_GetSwapInterval` | Previously discarded entirely (P1-1). |

## Lifetime and safety

- **Single-live-context rule (P0-9).** The exact pinned ShivaVG revision's `vgCreateContextSH`/
  `vgDestroyContextSH` (`src/shContext.c`) operate on one process-global `static VGContext
  *g_context`: a second `vgCreateContextSH()` call while one is already live silently returns
  `VG_TRUE` and reuses the FIRST context, and `vgDestroyContextSH()` unconditionally frees it --
  so two concurrently-live `OpenVgRenderer` instances would silently alias one ShivaVG context,
  and the first one destroyed would free it out from under the second (a use-after-free on its
  very next OpenVG call). CNA enforces the smallest robust fix instead of a per-context ShivaVG
  redesign: a process-wide `std::atomic<bool>` ownership flag. A second concurrent
  `OpenVgRenderer` construction throws a clear `std::runtime_error` immediately; destroying the
  first releases the slot; sequential construct/destroy/construct cycles are fully supported.
  Tested directly (`openvg_presentation_viewport_scissor_test.cpp`).
- **Exception-safe construction (P0-10).** Every acquired resource (the single-live-context
  ownership flag, the SDL GL context) is released on any constructor failure path -- including
  `SDL_GL_MakeCurrent` failing after `SDL_GL_CreateContext` already succeeded, which previously
  leaked the GL context because nothing freed it before the exception propagated out of the
  not-yet-fully-constructed object (whose own destructor never runs in that case).
- **GL context currentness (P2-2).** The destructor re-establishes this renderer's own SDL GL
  context as current before destroying ShivaVG's context, in case some other GL consumer in the
  same process changed the current context in between -- combined with the single-live-renderer
  rule above, this is the complete, minimal answer to "which GL context does ShivaVG's global
  state belong to" without a more complex multi-context redesign.
- **Texture construction (P1-2).** CPU-side argument validation (pixel-buffer size vs.
  width\*height\*4) now runs BEFORE `vgCreateImage`, not after -- a validation failure used to
  leave an allocated `VGImage` with nothing to free it (a genuine per-failed-upload leak, since
  the object's destructor never runs for a not-yet-constructed instance).
- **VG error reporting (P1-10).** `vgCreateImage` failure is turned into a `std::runtime_error`
  naming the real `VGErrorCode` (via `vgGetError()`), rather than a generic message with no
  diagnostic information.

## Dependency and build

`cmake/ThirdPartyOpenVG.cmake` fetches ShivaVG's source at the pinned commit via `FetchContent` and
compiles its 11 core `.c` files directly (ShivaVG has no CMake of its own -- only a 2010-era
autotools build). Two small from-2010-codebase compatibility gaps against a modern host toolchain
are bridged there with compiler flags/generated files, not by patching ShivaVG's own source -- see
that file's own comments for the exact mechanism (a generated one-line `config.h`, and a
`GLintptr`/`GLsizeiptr` typedef shim ahead of ShivaVG's translation units, applied via the
portable CMake `-include`/`/FI` compiler-option abstraction rather than a hardcoded flag string, so
it resolves correctly for GCC/Clang and MSVC alike). ShivaVG links real `OpenGL::GL`/`OpenGL::GLU`
(`shImage.c`/`shContext.c` call `gluScaleImage`/`gluOrtho2D` for real).

## Supported platforms

Desktop Linux, Windows, and macOS only (`cmake/RendererSelection.cmake`'s own platform gate) --
ShivaVG needs a real fixed-function/immediate-mode OpenGL context, which neither WebGL
(Emscripten) nor a GLES-only mobile target can create. Native validation actually performed in
this environment is **Linux + Xvfb (X11 GLX) only** -- the full OpenVG test suite (13 tests) and
the complete `CnaTests` corpus were both run and passed/skipped-as-expected there (see "Test
status"). Windows (MSVC)/macOS were NOT compiled or run in this environment; their own GL/GLU
linkage and ShivaVG's own upstream-documented build targets are unchanged from before this audit,
but that is not the same as having verified them here -- do not read this document as claiming
otherwise.

## Test status

Real windowed/pixel behavior is covered by `modules/renderers/openvg/examples/` (this project's
established split for GPU/window-creating tests -- pure-function pieces live in
`modules/renderers/openvg/tests/` instead):

| Test | Coverage |
|---|---|
| `openvg_smoke_test` | Vertical slice: Clear, SpriteBatch draw, readback. |
| `openvg_spritebatch_rotation_test` | Decisive rotation/origin/flip geometry oracle (`NativeBackBuffer`). |
| `openvg_unsupported_3d_behavior_test` | Throw/WarnAndStub policy across every inherently-3D entry point. |
| `openvg_presentation_viewport_scissor_test` | All 5 presentation modes (pixel-level), window<->logical transforms + letterbox-bar rejection, custom viewport placement/persistence across `Clear()`, resize without `Clear()`, `RasterizerState`-driven scissor (enable/rectangle independence, presentation-scaled), single-live-context guard + sequential recreation, swap interval. |
| `openvg_texture_orientation_test` | Asymmetric-texel pixel oracle: upload, `UpdatePixels`, partial source rectangle, tint, rotation, both `SpriteEffects` flips, out-of-bounds `Clamp` edge extension (multiple edges + a corner), out-of-bounds `Wrap`/`Mirror` rejection. |
| `openvg_blend_test` | `Opaque`/`NonPremultiplied`/`AlphaBlend` against a computed expected composite, `Additive`/custom-`BlendState` rejection, `ColorWriteChannels` via `glColorMask`. |
| `openvg_modern_state_guard_test` | Every modern 3D `Draw*` entry point through the public `GraphicsDevice` API, meaningful `DepthStencilState`/`RasterizerState` rejection with benign defaults accepted, transactional `RenderTarget2D`-binding rejection. |

Plus `OpenVgBlendStateMapping.*` (`modules/renderers/openvg/tests/`, the pure
`BlendStateToVgBlendMode` mapping function, no window/GL context needed).

**Exact commands run** (this environment): `Xvfb :0 -screen 0 1280x1024x24 &`, then
`DISPLAY=:0 ctest --test-dir cmake-build-openvg -R OpenVg --output-on-failure` (13/13 pass) and
`DISPLAY=:0 ctest --test-dir cmake-build-openvg -j4` (the complete `CnaTests` corpus).

## Known cross-renderer test gaps

Running the full `CnaTests` corpus against `-DCNA_GRAPHICS_RENDERER=OPENVG` surfaces two
pre-existing categories of shared-test gap -- neither is an OPENVG-specific `#ifdef`/skip; both are
generic, renderer-neutral gates or (for the smaller, tractable set this audit directly caused to
surface) fixes to the shared test files themselves:

* **`RenderTarget2D` genuinely unsupported.** `Texture2DCacheReconstructionTests.cpp`'s
  `RenderTargetKeepsItsRendererAndDropsTheShadowOnUpload` and
  `RenderTargetReadbackComesFromTheSurfaceNotAnUploadShadow` assumed every renderer provides real
  `RenderTarget2D` storage -- true of every renderer that existed before OpenVG, including the
  other 2D-only ones (SDL_Renderer, ASCII, Canvas, GDI, ...), which all still have a real 2D
  draw-to-texture path. Fixed with a minimal, isolated `CNA_RENDERER_OPENVG`-gated
  `kRenderTarget2DSupported` constant and `GTEST_SKIP()`, mirroring this file's own existing
  `TextureCube`-style capability-gate precedent. This audit additionally found and fixed three
  MORE tests with the identical unguarded assumption
  (`GraphicsDeviceValidationTests.cpp`'s `TextureCollectionValidationTest.
  ActiveRenderTargetCannotBindToPixelTextureSlot`/`ActiveRenderTargetCannotBindToVertexTextureSlot`/
  `RenderTargetCanBindForSamplingAfterUnbind`) -- these three now gate on the REAL runtime
  behavior of `GraphicsDevice::SetRenderTarget` itself (skip if it throws
  `System::NotSupportedException`) rather than a renderer-name constant, so the gate stays correct
  for any future renderer with the same real gap too. A fourth,
  `GraphicsDeviceDefaultStateTests.cpp`'s `AssigningDepthStencilStatePropagatesReferenceStencil`,
  needed the analogous `GraphicsCapability::StencilBuffer` gate (it exercises a non-zero
  `ReferenceStencil`, which P0-7's fix now correctly rejects on a renderer with no stencil plane
  at all). All four of these were surfaced BY this audit's own correctness fixes (P0-7/P0-8) --
  they used to pass only because the underlying renderer calls they depend on were silent no-ops.

* **Pre-existing "content pipeline assumes a 3D pipeline" gap, not introduced by OpenVG.** Roughly
  90-100 tests across `Model`/`Cnj`/`Gltf`/`SkinnedModel`-loading suites (`XnbBuiltInReaderRegistrationTest`,
  `CnjModelTest`, `GltfToCnjToolTest`, `RuntimeGltfModelTest`, `SkinnedModelEXTPartTest`,
  `ModelContentTypeReaderTest`, `ContentPathContainmentTest`, `VertexBufferBindingValidationTest`,
  `VertexBufferEmptyDataTest`, `InstancedDrawMultiStreamTest`, `OrdinaryDrawMultiStreamTest`,
  `NonIndexedDrawRangeTest`, `SetMorphWeightsEXTTest`, ...) construct a real `VertexBuffer`
  (directly, or transitively through `Model` loading) with no `GraphicsCapability::ThreeD` guard.
  `OpenVgRenderer::CreateVertexBuffer` honestly throws (`HandleUnsupported3DCall`) under the
  default `Throw` policy -- the exact same call every other 2D-only renderer already in this
  registry (SDL_Renderer, ASCII, Canvas, GDI, HTML_DOM, FreeDirect, Headless-adjacent) makes for
  the identical entry point. No prior renderer in this registry combination has apparently
  exercised this exact suite before, so the gap was latent rather than newly caused by OpenVG, and
  this audit's own fixes did not change its shape or size. The one non-3D outlier,
  `MetalResourceHealth`'s `RenderTargetCubeRendererEscapesThroughTextureCubeBaseMove` (a
  "host-portable" test compiled into every renderer's `CnaTests`, not Metal-specific despite its
  module location), asserts a non-null `RenderTargetCube` renderer identity and would fail
  identically for any renderer already in `TextureCubeTests.cpp`'s `kCubeStorageSupported = false`
  list, for the same silent-nullptr construction reason `RenderTargetCube`/`TextureCube` already
  document elsewhere.

  These tests were deliberately left unmodified rather than retrofitted with
  `GraphicsCapability::ThreeD` guards across a dozen content-module files: that is a genuine,
  useful follow-up, but it is a general content-pipeline/2D-renderer-family gap outside this
  renderer's own minimal-shared-change scope, not an OpenVG regression, and outside what this
  audit's own P0/P1 finding list asked for.

* **Unrelated, non-graphics flakiness observed in the same full-corpus run.** A handful of
  `ENetDiscoveryServiceTest`/`TwoProcessLoopbackTest` UDP-networking tests and the
  `ModuleLinkClosure_*` probes (which expect a Makefiles-generator `link.txt` and fail outright
  under this environment's Ninja generator) fail or flake independent of which graphics renderer
  is selected -- confirmed by running representative cases standalone (outside the parallel `-j4`
  full-corpus run) and observing them pass. Not investigated further here: genuinely orthogonal to
  OpenVG.

## Known limitations (upstream, not CNA-specific)

`vgChildImage` is declared but unconditionally returns `VG_INVALID_HANDLE` (not documented in
`TODO`/`README` -- found empirically); this renderer's `SpriteBatch` path works around it with
`vgCopyImage` instead (see the table above). Per ShivaVG's own `TODO`/`README`: masking (`vgMask`), path length queries
(`vgPathLength`/`vgPointAlongPath`), image filters (`vgGaussianBlur`, `vgConvolve`,
`vgColorMatrix`, `vgLookup*`), `vgHardwareQuery`, and warp computation functions are declared but
not implemented. `vgSetColor`/`vgGetColor` are declared in `openvg.h` but have no body in this
revision's `shPaint.c` either -- OpenVgSpriteBatchRenderer's tint path uses the underlying
`vgSetParameterfv(paint, VG_PAINT_COLOR, ...)` primitive instead, which IS implemented. No
multi-threading support (and, per "Lifetime and safety" above, no multi-INSTANCE support in one
process either -- ShivaVG's own global context, not a CNA design choice).

**Per-renderer-lifecycle leak (upstream) -- FIXED via a checked-in patch, not left as a limitation.**
`shContext.c`'s `VGContext_ctor`/`VGContext_dtor` were asymmetric: the constructor `SH_INITOBJ`s
five owned resources (the `paths`/`paints`/`images` arrays' own backing storage plus
`c->defaultPaint`'s `instops`/`stops` arrays and 1D gradient GL texture), but the destructor only
ever freed the individual path/paint/image *objects* those arrays contained -- never the arrays'
own storage, and never `c->defaultPaint` at all (leaking its GL texture object too, not just heap
bytes). A genuine upstream bug (confirmed by reading `VGContext_ctor` against `VGContext_dtor`
side by side), reproducible as a constant 64-byte/5-allocation-plus-one-GL-texture leak on every
`vgCreateContextSH`/`vgDestroyContextSH` (`OpenVgRenderer` construct/destroy) cycle -- fixed size,
confirmed NOT to accumulate per draw/frame/texture, only per renderer lifecycle.

Fixed with `cmake/patches/shivavg-context-dtor-leak.patch` (four `SH_DEINITOBJ` calls added to
`VGContext_dtor`), applied idempotently to the `FetchContent`-fetched source via
`cmake/patches/apply-shivavg-patch.cmake` as `ThirdPartyOpenVG.cmake`'s own `PATCH_COMMAND` --
never by hand-editing generated `_deps` content. Verified: a fresh `FetchContent` checkout gets the
patch applied automatically (`git apply`); re-running the same `PATCH_COMMAND` against an
already-patched checkout detects that via `git apply --reverse --check` and skips cleanly (no
double-apply, no error); a 25-cycle sequential construct/`Clear`/`Present`/destroy regression test
(`openvg_presentation_viewport_scissor_test.cpp`'s `TestRepeatedConstructDestroyCycle`) and the
full `OpenVg`-labelled + `CnaTests` graphics suite are all clean of AddressSanitizer/LeakSanitizer
findings after the patch, where the pre-patch same runs reported this exact leak. The single-live-
context guard and exception-safe construction below are unaffected -- neither touches
`VGContext_ctor`/`_dtor` -- and re-verified clean together with this fix.
