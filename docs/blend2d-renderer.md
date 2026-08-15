# Blend2D renderer

## Current status

`CNA_GRAPHICS_RENDERER=BLEND2D` is a genuine, 2D-only CPU-raster renderer backed by
[Blend2D](https://github.com/blend2d/blend2d), a real 2D vector graphics engine (Zlib license)
powered by an AsmJit-generated (also Zlib) JIT pipeline compiler. It owns a premultiplied
`BLImage`/`BLContext` backbuffer, clears and draws through real Blend2D rasterization, reads the
completed frame back, and hands it to the platform surface presenter -- the same "CPU raster +
platform presentation" shape the SKIA renderer already established (`docs/skia-renderer.md`). The
renderer depends only on `IPlatformSurfacePresenter`; the selected platform owns the native upload,
scaling and swap. No platform backend executes a Blend2D draw command.

See `plan_blend2d.md` for the task-level remediation history and remaining boundaries.

The implemented surface is a real 2D vertical slice: `Clear`, `Present`, backbuffer/render-target
readback, virtual-resolution resize, all five presentation modes, scissor clipping gated by
`RasterizerState.ScissorTestEnable`, the XNA `Viewport` rectangle, `SpriteBatch.Begin`'s transform
matrix/sampler filter/sampler address mode, `Texture2D` upload/readback (with the caller's actual
row stride honored), `RenderTarget2D` render/unbind/sample, and every `SpriteBatch.Draw` overload
(position, rotation, origin, non-uniform scale including a negative-scale mirror, source
rectangle, flips, tint colour) -- including out-of-range source rectangles and self-sampling a
render target, both handled safely rather than left as an out-of-bounds read. The 3D pipeline
(vertex/index buffers as real GPU storage, `DrawColoredPrimitives`, depth/stencil, occlusion
queries, cube/volume textures, custom `Effect` compilation) has no Blend2D equivalent at all and is
truthfully refused rather than silently no-opped: every 3D draw call and every
depth/stencil-enabling `DepthStencilState` throws through `Ensure3DSupported`/
`HandleUnsupported3DCall`, and `SupportsCapability(GraphicsCapability::ThreeD)` reports `false`.

## Verified capability boundary

Every row below is exercised by `Blend2D_Correctness`
(`modules/renderers/blend2d/examples/blend2d_renderer_correctness_test.cpp`, 333 pixel-level
checks run under `SDL_VIDEODRIVER=dummy` -- no real display required -- and clean under
AddressSanitizer/UndefinedBehaviorSanitizer/LeakSanitizer) or by `Blend2D_RenderTargetProperty`
(`blend2d_rendertarget_property_test.cpp`, driving the real public `RenderTarget2D`/`SpriteBatch`
API through a `Game`/`GraphicsDeviceManager`, also SDL_VIDEODRIVER=dummy-safe) unless noted
otherwise.

| CNA feature | Blend2D route | Direct/emulation decision | Evidence |
|---|---|---|---|
| Clear, Present, resize, all five presentation modes, backbuffer readback | Direct CPU raster + platform presentation | Real `BLContext::fill_all`/`blit_image`; `IPlatformSurfacePresenter` only displays the finished RGBA8 frame. | `Blend2D_Surface_Raster`, `Blend2D_Smoke`, `Blend2D_Correctness` (`TestPresentationModes`) |
| `FixedHeightDynamicWidth` presentation mode | Direct, aspect-derived | Logical width = `round(outputWidth * requestedHeight / outputHeight)`, re-derived on every `Clear`/`Present`/`GetViewportSize` while no render target is bound, exactly matching `SkiaRenderer::RecreateBackbuffer`/`RefreshDynamicBackbufferIfNeeded`'s formula and timing. | `Blend2D_Correctness` (`TestPresentationModes`, including a simulated output-size change via `DebugSetPresentationOutputSizeEXT`) |
| `SpriteBatch` position/dest-rect/source-rect/rotation/origin/scale/flip/tint overloads | Direct 2D path | `BLContext` transform stack (`apply_transform`/`translate`/`rotate`/`scale`) plus `blit_image`. Flips (`SpriteEffects` and/or a negative destination scale) mirror the drawn content about the destination rectangle's own local center, matching FNA/XNA's per-vertex geometry -- not a naive `scale(-1,1)` about the coordinate origin, which used to move a flipped sprite off its requested destination rectangle. A negative destination scale and `SpriteEffects` compose by XOR (both together cancel out), matching `SoftwareSpriteBatchRenderer`'s reference derivation. A non-white tint (or an out-of-bounds/self-sampling source, see below) resolves a genuine CPU-multiplied, address-mode-aware premultiplied sub-image, since Blend2D's stock blit has no per-draw colour modulation of its own. | `Blend2D_Smoke`, `Blend2D_Correctness` (`TestFlipOriginRotation`, `TestTintChain`) |
| Source rectangle safety (negative origin, oversized, entirely outside the texture, self-sampling) | Direct blit when fully in-bounds; safe resolved-copy otherwise | Every source pixel read for the tint/resolve path is bounds-mapped through the active `TextureAddressMode` (Clamp/Wrap/Mirror) before it is read, so no source rectangle can produce an out-of-bounds read; the in-bounds fast path is additionally protected by Blend2D's own `blit_image` bounds check (`BL_ERROR_INVALID_VALUE`, checked). Drawing a render target while it is the active target snapshots an independent copy first rather than assuming Blend2D permits self-referential source/destination blits. | `Blend2D_Correctness` (`TestSourceRectangles`, `TestRenderTargets`'s self-sampling checks), clean under ASan/UBSan/LeakSanitizer |
| Sampler filter (`SpriteBatch.Begin`'s `SamplerState.Filter`) | Direct `BLContext::set_pattern_quality` | All 9 `TextureFilter` values decompose to their magnification component (Blend2D has no separate min/mag/mip control or mip chain, so no `Mip*` suffix is ever observable), matching `SdlRenderer`'s own established decomposition: `Point`-family -> `BL_PATTERN_QUALITY_NEAREST`, `Linear`-family -> `BL_PATTERN_QUALITY_BILINEAR`. Point and Linear are proven to actually differ (not just "does not throw") by minifying a two-texel red/green source into one destination pixel: `NEAREST` selects one pure stored texel, `BILINEAR` interpolates both, for all 9 enum values. | `Blend2D_Correctness` (`TestSamplerFilter`) |
| Sampler address mode (`SamplerState.AddressU`/`AddressV`) | Direct, per-pixel | Wrap/Clamp/Mirror are implemented exactly (not approximated) for the resolved-copy source path described above; independent per-axis modes are supported. | `Blend2D_Correctness` (`TestSamplerAddressModes`) |
| `SpriteBatch.Begin`'s transform matrix | Direct `BLContext::apply_transform` | Composed as the outermost transform around each sprite's own placement, matching `SkiaSpriteBatchRenderer`/`SoftwareSpriteBatchRenderer`'s identical ordering. | `Blend2D_Correctness` (`TestTransformMatrix`, `TestScissor`'s target-space check) |
| Custom `Effect` | Rejected (not `Opaque`/discarded) | Blend2D has no shader/effect pipeline; only `null` or the exact stock `SpriteEffect` (which owns no renderer program) is accepted. A real custom `Effect` throws instead of silently rendering through the built-in sprite path. | `Blend2D_Correctness` (`TestCustomEffectRejection`) |
| `Texture2D` upload/readback, including a caller row stride wider than `width * 4` | Direct premultiplied `BLImage` path | Straight RGBA8 CNA bytes convert to/from Blend2D's premultiplied, channel-swapped (`BGRA`) native storage on every transfer (`Blend2DPixelConvert.hpp`) using the caller's actual stride, never a hardcoded `width * 4` assumption. | `Blend2D_Smoke`, `Blend2D_Correctness` (`TestTextureStride`) |
| `RenderTarget2D` render/unbind/switch/self-sample/destruction | Direct bindable `BLImage`/`BLContext` target | Each target owns its own `Blend2DSurface`; `BindAsRenderTarget`/`UnbindAsRenderTarget` switch the renderer's single tracked active surface, matching XNA's "operations apply to the current target" contract. Destroying a bound target clears the active pointer; destroying an unbound one is a no-op. Mip chains and MSAA are not implemented (see below). | `Blend2D_Smoke`, `Blend2D_Correctness` (`TestRenderTargets`) |
| Scissor clipping | Direct `clip_to_rect`, gated by `ScissorTestEnable` | `SetScissorRect`/`ApplyRasterizerState` only record state; `Blend2DSpriteBatchRenderer` re-applies the clip fresh on every draw, in target space (before the transform matrix or any per-sprite transform), on whichever `BLContext` is currently active -- so it cannot leak across a render-target switch or an intervening `ScissorTestEnable` toggle the way an eagerly-applied, persistent clip would. | `Blend2D_Correctness` (`TestScissor`) |
| `Viewport` | Direct `clip_to_rect` + translate | SpriteBatch coordinates are viewport-local, matching every other CNA 2D renderer's contract: a non-default viewport (e.g. split-screen) clips to and offsets by the viewport rectangle, applied fresh on every draw in target space, before the transform matrix. | `Blend2D_Correctness` (`TestViewport`) |
| `ColorWriteChannels` (render-target slot 0 only) | Direct fast path when unmasked; bounded whole-surface merge otherwise | Blend2D has no native per-channel colour write mask. The common (`ColorWriteChannels == All`) case uses the ordinary native-comp-op blit with no extra cost; a non-default mask snapshots the whole active surface's premultiplied bytes before the draw, lets Blend2D's real rasterizer perform the composite, then merges the two premultiplied buffers per channel (matching `SkiaRenderer`'s own masked-write precedent of merging in its native premultiplied representation). `ColorWriteChannels1/2/3` (unimplemented MRT slots) and a non-default `MultiSampleMask` are rejected outright. | `Blend2D_Correctness` (`TestColorWriteChannels`, `TestBlendPresets`'s rejection checks) |
| `Opaque`/`AlphaBlend`/`NonPremultiplied`/`Additive` blend presets | `Opaque`/`AlphaBlend` direct exact `BLCompOp`; `NonPremultiplied`/`Additive` native `BLCompOp` as a staging draw only, then a bounded CPU-assisted full colour+alpha recompute | `Opaque` -> `BL_COMP_OP_SRC_COPY` (a literal copy, source alpha included -- it does not force full opacity, matching real XNA). `AlphaBlend` -> `BL_COMP_OP_SRC_OVER`, exact in BOTH channels: XNA's `Dca'=Sca+Dca*(1-Sa)`, `Da'=Sa+Da*(1-Sa)` is byte-identical to native Porter-Duff over. `NonPremultiplied`/`Additive` set BOTH `ColorSourceBlend` and `AlphaSourceBlend` to `SourceAlpha`, so XNA evaluates colour and alpha from the SAME straight-space equation pair -- the semantic oracle is `SkiaRenderer.cpp`'s `MaskedBlendEffect()` runtime blender, NOT `BLCompOp` behaviour: recover straight destination colour `dstColor=Dca/Da`, then `Cout=saturate(Sca+dstColor*(1-Sa))` / `Aout=saturate(Sa*Sa+Da*(1-Sa))` for `NonPremultiplied`, `Cout=saturate(Sca+dstColor)` / `Aout=saturate(Sa*Sa+Da)` for `Additive`, and store `(Cout*Aout, Aout)`. Critically `Cout` is premultiplied against the NEW `Aout`, not the OLD `Da` native `BL_COMP_OP_SRC_OVER`/`PLUS` would use -- since `Aout != Da` whenever `Sa` is neither 0 nor 1, NEITHER preset's colour bytes are identical to what the native op alone would produce, even though native compositing's colour formula happens to share the same `Sca` (source premultiplied contribution) term. The native `BLCompOp` blit is therefore only a staging draw for these two presets: a second bounded pass renders the identical draw onto a transparent scratch canvas via `SRC_OVER` (which collapses to exactly the source's own premultiplied contribution `Sca` and effective alpha `Sa`, antialiased edge coverage/sampling/rotation/clipping all included) and combines it with a whole-surface pre-draw snapshot to compute and write back BOTH the colour and alpha bytes (`Blend2DSpriteBatchRenderer.cpp`'s `ApplyIndependentBlendCorrectionEXT`). `Cout` is saturated to `[0,1]` BEFORE the `Cout*Aout` repremultiply (matching `MaskedBlendEffect`'s own `saturate(...)` placement), which guarantees `stored.rgb <= stored.a` by construction for every touched pixel -- `Additive`'s colour weights (`Sa + 1`) do not sum to 1, so an unsaturated `Cout` can genuinely exceed 1.0 (a real, expected additive "blow out" XNA/FNA also clip). `Blend2DPixelConvert.hpp`'s unpremultiply clamp is therefore defensive hardening only, never load-bearing for this renderer's own output, verified directly against Blend2D's raw native bytes (not just the straight-RGBA readback) across a broad source-alpha/destination-alpha sweep and at a partial-coverage antialiased edge. All four presets verified against a semi-transparent source over a semi-transparent destination, with alpha checked in isolation from the full colour+alpha result, not just opaque colours. | `Blend2D_Correctness` (`TestBlendPresets`, `TestPrgb32Invariant`, `TestBlendCorrectionEdgeCoverage`, `TestTintChain`) |
| Any other `BlendState` (independent alpha factors/function, or a colour factor/function combination outside the four stock presets) | Rejected | Blend2D's `BLCompOp` set is Porter-Duff/blend-mode based, not the generic per-factor equation XNA's `BlendState` exposes, so only the four exact stock tuples above have a real Blend2D operator. Every other combination throws rather than silently falling back to `SRC_OVER`. | `Blend2D_Correctness` (`TestBlendPresets`'s rejection checks) |
| Cross-renderer resource safety | Rejected with a clear exception | A `Texture2D`/`RenderTarget2D` created by a different renderer throws `std::invalid_argument` before any native memory is touched, not an unchecked `dynamic_cast` producing an undiagnosable `std::bad_cast`. | `Blend2D_Correctness` (`TestCrossRendererSafety`) |
| `Begin`/`End` state machine | Guarded | A double `Begin()` or a stray `End()` throws without corrupting the renderer for the next, valid `Begin()`/`End()` cycle; a rejected `SetSamplerFilter`/`SetSamplerAddressMode`/`SetCustomEffect` during `Begin()`-adjacent setup does not poison subsequent batches. | `Blend2D_Correctness` (`TestBeginEndStateMachine`) |
| `DepthStencilState` | `None` accepted; anything depth/stencil-enabling rejected | Matches `SupportsDepthStencil() == false` / `SupportsCapability(StencilBuffer) == false`: `ApplyDepthStencilState`/`SetReferenceStencil` now reject a real depth-test/write/stencil request (found during a second, independent audit pass) instead of silently accepting state that could never actually apply. | -- |
| 3D pipeline (vertex/index buffers as GPU storage, `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`, occlusion queries, `Texture3D`/`TextureCube`, custom `Effect` compilation, instancing, MRT, wireframe) | Unsupported | Blend2D has no 3D or programmable-shader concept at all. Vertex/index buffer *handles* exist (bookkeeping only, matching `IVertexBufferRenderer`/`IIndexBufferRenderer`'s required interface) but are never consumed by a real draw; every 3D draw call and `SupportsCapability` entry reports the honest `false`/throw. | `Blend2D_Smoke` (3D `DrawPrimitives` throw check) |
| `RenderTarget2D.DepthStencilFormat`, mip chains, MSAA, cube/volume sampling | Unsupported, and truthfully reported as such through the PUBLIC `RenderTarget2D` properties | Blend2D never allocates a real depth/stencil plane regardless of the requested `DepthFormat`: `Blend2DRenderTargetRenderer::GetAppliedDepthStencilFormatEXT` always returns `DepthFormat::None`'s raw ordinal, so `RenderTarget2D.DepthStencilFormat` (populated from that method by `RenderTarget2D.cpp`, not from the separate `HasRealDepthBuffer`/`HasRealStencilBuffer` boolean queries -- both also `false`) never claims an attachment that was never created. `CreateRenderTarget2D`'s `multiSampleCount` is accepted but not honored: `GetMultiSampleCount()` stays the inherited default of `0`, so `RenderTarget2D.MultiSampleCount` truthfully clamps to `0` even when a large count is requested. `mipMap` is accepted and `RenderTarget2D.LevelCount` echoes the requested mip-chain length as metadata (the shared cross-renderer convention every renderer follows), but no level beyond 0 is ever real storage: `Blend2DRenderTargetRenderer::GetData` returns `false` for `level != 0`, which the shared `Texture2D::GetData` turns into a `System::NotSupportedException` rather than fabricated pixel data. | `Blend2D_RenderTargetProperty` |

## Memory safety and error handling

Every `BLContext`/`BLImage` call that can genuinely fail (a non-finite transform, an
out-of-bounds image area, an allocation failure) is routed through `Blend2DCheckEXT`
(`Blend2DCheckedCallEXT.hpp`), which throws with Blend2D's own error code rather than silently
continuing after a discarded `BLResult`. `BLContext::save()`/`restore()` around each draw's
transform/clip state uses an RAII guard (`ScopedContextStateEXT`) so an exception partway through
a draw cannot leave a stale transform or clip attached to the surface's `BLContext`, corrupting
every subsequent draw on it.

`BL_FORMAT_PRGB32`'s in-memory byte order (`BGRA` on a little-endian host) is host-endian
dependent by Blend2D's own documentation (it is defined as the Cairo `CAIRO_FORMAT_ARGB32` / Qt
`QImage::Format_ARGB32_Premultiplied` equivalent). CNA has no big-endian target anywhere in this
codebase, so rather than leave that assumption undocumented, `Blend2DPixelConvert.hpp` fails the
build with a truthful `static_assert` on a big-endian host instead of silently producing
channel-swapped pixels.

Hostile inputs (negative/oversized source rectangles, self-sampling) are covered by
`Blend2D_Correctness` and pass clean under AddressSanitizer, UndefinedBehaviorSanitizer, and
LeakSanitizer (`-DCNA_SANITIZE=address,undefined`).

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

For a sanitizer run:

```sh
cmake -S . -B cmake-build-blend2d-asan \
  -DCNA_GRAPHICS_RENDERER=BLEND2D \
  -DCNA_BUILD_TESTS=ON \
  -DCNA_SANITIZE=address,undefined
cmake --build cmake-build-blend2d-asan --parallel 4
```

## Pixel format and premultiplication

Blend2D's `BL_FORMAT_PRGB32` native storage is premultiplied alpha with `BGRA` in-memory byte
order (the byte layout of its packed `0xAARRGGBB` value on a little-endian host -- see "Memory
safety and error handling" above). CNA's own transfer contract (`ImageData`,
`ITextureRenderer::GetData`/`UpdatePixels`) is always straight (non-premultiplied) top-row-first
`RGBA8`, matching every other renderer in this codebase. Every transfer into or out of a
Blend2D-owned `BLImage` -- texture upload, texture readback, backbuffer/render-target readback, and
the resolved sub-image `SpriteBatch` builds for a non-white draw colour or an out-of-bounds/
self-sampling source -- converts explicitly through `Blend2DPixelConvert.hpp`; there is no raw byte
copy anywhere in this renderer.

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
- `Blend2D_Correctness`
  (`modules/renderers/blend2d/examples/blend2d_renderer_correctness_test.cpp`): 112 deterministic
  pixel-level checks driving `Blend2DRenderer`/`Blend2DSpriteBatchRenderer` directly through an
  `SdlTestSurfacePresenter` under `SDL_VIDEODRIVER=dummy` (no real display required, so this runs
  in headless CI). This is test infrastructure; production Blend2D code is SDL-free. Covers every
  row of the capability table above, including hostile source rectangles and self-sampling under
  sanitizers.

All three are registered as CTests via `cna_register_renderer_test()` and follow the repository's
`SKIP_RETURN_CODE 77` headless-safe convention (`cna_apply_skip_convention()`).
