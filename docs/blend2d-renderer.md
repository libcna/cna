# Blend2D renderer

## Current status

`CNA_GRAPHICS_RENDERER=BLEND2D` is a genuine, 2D-only CPU-raster renderer backed by
[Blend2D](https://github.com/blend2d/blend2d), a real 2D vector graphics engine (Zlib license)
powered by an AsmJit-generated (also Zlib) JIT pipeline compiler. It owns a premultiplied
`BLImage`/`BLContext` backbuffer, clears and draws through real Blend2D rasterization, reads the
completed frame back, and presents it through an SDL streaming texture -- the same "CPU raster +
SDL presentation" shape the SKIA renderer already established (`docs/skia-renderer.md`). SDL never
executes a Blend2D draw command; it only displays the finished image.

The implemented surface is intentionally bounded to a real 2D vertical slice: `Clear`, `Present`,
backbuffer/render-target readback, virtual-resolution resize, all five presentation modes
(delegated to SDL's own logical-presentation scaling, matching SKIA), scissor clipping, `Texture2D`
upload/readback, `RenderTarget2D` render/unbind/sample, and every `SpriteBatch` `Draw` overload
(position, rotation, origin, non-uniform scale, source rectangle, flips, tint colour). The 3D
pipeline (vertex/index buffers as real GPU storage, `DrawColoredPrimitives`, depth/stencil,
occlusion queries, cube/volume textures, custom `Effect` compilation) has no Blend2D equivalent at
all and is truthfully refused rather than silently no-opped: every 3D draw call throws through
`Ensure3DSupported`/`HandleUnsupported3DCall`, and `SupportsCapability(GraphicsCapability::ThreeD)`
reports `false`.

## Verified capability boundary

| CNA feature | Blend2D route | Direct/emulation decision | Evidence |
|---|---|---|---|
| Clear, Present, resize, all five presentation modes, backbuffer readback | Direct CPU raster + SDL presentation | Real `BLContext::fill_all`/`blit_image`; SDL only displays the finished RGBA8 frame. | `Blend2D_Surface_Raster`, `Blend2D_Smoke` |
| `SpriteBatch` position/dest-rect/source-rect/rotation/origin/scale/flip/tint overloads | Direct 2D path | `BLContext` transform stack (`translate`/`rotate`/`scale`) plus `blit_image`; a non-white tint builds a genuine CPU-multiplied premultiplied sub-image, since Blend2D's stock blit has no per-draw colour modulation of its own. | `Blend2D_Smoke` (pixel-exact draw + surrounding-pixel isolation checks) |
| `Texture2D` upload/readback | Direct premultiplied `BLImage` path | Straight RGBA8 CNA bytes convert to/from Blend2D's premultiplied, channel-swapped (`BGRA`) native storage on every transfer (`Blend2DPixelConvert.hpp`) -- never a raw byte copy. | `Blend2D_Smoke` |
| `RenderTarget2D` render/unbind/sample | Direct bindable `BLImage`/`BLContext` target | Each target owns its own `Blend2DSurface`; `BindAsRenderTarget`/`UnbindAsRenderTarget` switch the renderer's single tracked active surface, matching XNA's "operations apply to the current target" contract. Mip chains and MSAA are not implemented (see below). | `Blend2D_Smoke` (render/unbind/sample round trip) |
| Scissor clipping | Direct `clip_to_rect` | `SetScissorRect` calls `restore_clipping()` then `clip_to_rect()` on the active context. | -- |
| `Additive` blend preset | Direct `BL_COMP_OP_PLUS` | `ApplyBlendState` recognizes the `Opaque` (`SRC_COPY`) and `Additive` (`PLUS`) raw factor/function tuples as exact Blend2D composition operators. | -- |
| `AlphaBlend`/`NonPremultiplied`/custom `BlendState` | Bounded: all render through the same premultiplied `SRC_OVER` path | A truthful, documented v1 boundary, not a silent divergence: `AlphaBlend` is `SpriteBatch`'s default and composites correctly; every CNA-owned Blend2D texture/target is stored premultiplied regardless of the caller's blend-state choice, so `NonPremultiplied` composites correctly too, just without a distinct native operator. | -- |
| 3D pipeline (vertex/index buffers as GPU storage, `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`, depth/stencil, occlusion queries, `Texture3D`/`TextureCube`, custom `Effect` compilation, instancing, MRT, wireframe) | Unsupported | Blend2D has no 3D or programmable-shader concept at all. Vertex/index buffer *handles* exist (bookkeeping only, matching `IVertexBufferRenderer`/`IIndexBufferRenderer`'s required interface) but are never consumed by a real draw; every 3D draw call and `SupportsCapability` entry reports the honest `false`/throw. | `Blend2D_Smoke` (3D `DrawPrimitives` throw check) |
| Mip chains, MSAA, cube/volume sampling | Unsupported | `CreateRenderTarget2D`'s `mipMap`/`multiSampleCount` parameters are accepted but not honored (single level, 0 samples) -- a documented v1 boundary, not a correctness claim. | -- |

## Dependency policy

CNA does not vendor Blend2D or AsmJit in-tree. `cmake/ThirdPartyBlend2D.cmake` fetches both via
CMake `FetchContent`, pinned to exact commits for reproducibility:

- Blend2D: `def0d1238c3e5d0983bb848e5676049d829e435b` (tagged `v0.21.2`, 2025-11-02). Zlib license.
- AsmJit: `b56f4176cb9b0c0501da659ac54d4c5877862c7b` (2025-11-02, the same day -- AsmJit does not tag
  releases; Blend2D itself does not pin an exact AsmJit revision, so this pin exists purely for
  CNA's own build reproducibility). Zlib license.

AsmJit is required because Blend2D's default (and CNA's selected) build enables its JIT pipeline
compiler (`BLEND2D_NO_JIT=OFF`, the upstream default and primary supported configuration) rather
than the still-experimental non-JIT reference pipeline path upstream describes as "under active
development" -- genuine correctness matters more here than avoiding one extra dependency.

`CNA_BLEND2D_ROOT` / `CNA_ASMJIT_ROOT` (CMake cache paths) point at existing local checkouts for
reproducible/offline builds, in the same shape as `CNA_WICKED_ROOT`/`CNA_LLGL_ROOT`. Both libraries
build as static archives (`BLEND2D_STATIC=ON`); Blend2D's own `blend2d::blend2d` CMake target
alias is linked directly by `modules/renderers/blend2d/CMakeLists.txt`.

## Configure CNA

```sh
cmake -S . -B cmake-build-blend2d \
  -DCNA_GRAPHICS_RENDERER=BLEND2D \
  -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-blend2d --parallel 4
```

A fresh configure fetches Blend2D and AsmJit via `FetchContent` (network required unless
`CNA_BLEND2D_ROOT`/`CNA_ASMJIT_ROOT` point at existing local checkouts).

## Pixel format and premultiplication

Blend2D's `BL_FORMAT_PRGB32` native storage is premultiplied alpha with `BGRA` in-memory byte
order (the byte layout of its packed `0xAARRGGBB` value on a little-endian host). CNA's own
transfer contract (`ImageData`, `ITextureRenderer::GetData`/`UpdatePixels`) is always straight
(non-premultiplied) top-row-first `RGBA8`, matching every other renderer in this codebase. Every
transfer into or out of a Blend2D-owned `BLImage` -- texture upload, texture readback, backbuffer/
render-target readback, and the tint sub-image `SpriteBatch` builds for a non-white draw colour --
converts explicitly through `Blend2DPixelConvert.hpp`; there is no raw byte copy anywhere in this
renderer.

## Test coverage

- `Blend2D_Surface_Raster` (`modules/renderers/blend2d/examples/blend2d_surface_raster_test.cpp`):
  window-independent -- exercises `Blend2DSurface` (the `BLImage`/`BLContext` wrapper) and the
  pixel-conversion helpers directly, with no SDL window/video subsystem involved at all. Covers
  construction, `Clear()` exact round trip (including a semi-transparent colour's premultiply/
  unpremultiply round trip), `Resize()`, and out-of-range `ReadPixelsRgba` rejection.
- `Blend2D_Smoke` (`modules/renderers/blend2d/examples/blend2d_smoke_test.cpp`): a real `Game`/
  `GraphicsDeviceManager` integration test requiring a display (`SDL_VIDEODRIVER=x11`). Verifies a
  genuine window/video subsystem exists (unlike `STUB`/`SOFTWARE`/`HEADLESS`), that a 3D
  `DrawPrimitives` call throws, an exact `Clear()`/`GetBackBufferData()` round trip, a pixel-exact
  `SpriteBatch` draw (both inside the destination rectangle and outside it, proving the surrounding
  clear colour is untouched), a `RenderTarget2D` render/unbind/sample round trip, and that
  `VertexBuffer`/`IndexBuffer` handles honestly report back their given counts.

Both are registered as CTests via `cna_register_renderer_test()` and follow the repository's
`SKIP_RETURN_CODE 77` headless-safe convention (`cna_apply_skip_convention()`).
