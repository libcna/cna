# Blend2D Graphics Renderer — Implementation Plan

> **Status: v1 baseline, remediated.** `CNA_GRAPHICS_RENDERER=BLEND2D` is a genuine 2D-only
> CPU-raster renderer backed by [Blend2D](https://github.com/blend2d/blend2d) v0.21.2. The
> renderer was first added in a single commit (branch `claude/renderer-blend2d-jallld`) with only
> two smoke-level tests; this plan tracks the subsequent full-implementation audit and
> remediation pass that brought it to a tested, sanitizer-clean state. See
> `docs/blend2d-renderer.md` for the current capability boundary and evidence table — that
> document is the authoritative "what works today" reference; this plan is the task-level history
> and remaining-boundary tracker.

## Non-negotiable rules

Mirrors the project-wide rules already established for `plan_skia.md`/`plan_webgpu.md`, applied
to a CPU-raster 2D-only renderer:

1. **Direct first, safe-resolve second, refusal last.** Implement any XNA `SpriteBatch`/
   `Texture2D`/`RenderTarget2D` behaviour Blend2D genuinely represents. Where Blend2D's own API
   cannot express something in-bounds (e.g. an out-of-range source rectangle), resolve it through
   a bounded, memory-safe CPU path rather than reading past a buffer. Where neither is possible
   (3D, custom shaders, mip/MSAA), refuse explicitly and truthfully.
2. **No silent state loss.** Every `ISpriteBatchRenderer`/`IGraphicsRenderer` optional virtual
   this renderer inherits without overriding must be an explicit, documented decision — either
   the shared default is genuinely correct for a 2D-only CPU rasterizer, or it must be overridden,
   or the corresponding public operation must reject the request. A newly discovered silently-
   inherited default that changes observable behaviour is a defect, not a scope boundary.
3. **No unchecked native memory access.** Every raw pointer walk into a `BLImage`'s pixel buffer
   must be bounds-derived from validated dimensions, never directly from an untrusted public
   `Rectangle`. Every `BLResult`-returning Blend2D call that can genuinely fail must be checked.
4. **Capabilities are measured, not aspirational.** `SupportsCapability()` and the documentation
   table in `docs/blend2d-renderer.md` report only behaviour that has a passing, pixel-level test
   behind it.
5. **Deterministic, headless-capable tests.** Prefer `SDL_VIDEODRIVER=dummy` window-independent
   tests (no real display required) over `SDL_VIDEODRIVER=x11` ones so the renderer's correctness
   suite runs in ordinary CI. Reserve the `x11` display requirement for the one test that
   specifically needs the full `Game`/`GraphicsDeviceManager` integration.

## Task log

### v1 initial addition (pre-remediation baseline)

- **BLEND2D-1.** Add `cmake/ThirdPartyBlend2D.cmake` (pinned `FetchContent` for Blend2D
  `def0d1238c3e5d0983bb848e5676049d829e435b` / AsmJit `b56f4176cb9b0c0501da659ac54d4c5877862c7b`),
  wire `BLEND2D` into `cmake/RendererSelection.cmake`, `modules/core/include/CNA/
  GraphicsRendererType.hpp`, `scripts/check_renderer_identities.py`, `docs/renderer-registry.md`,
  `docs/physical-modules.md`, `FUTURE.md`.
- **BLEND2D-2.** `modules/renderers/blend2d/` module: `Blend2DSurface` (owned `BLImage`+
  `BLContext`), `Blend2DRenderer` (backbuffer, presentation, render-target binding, blend/scissor
  state), `Blend2DSpriteBatchRenderer` (`Draw` overloads via `blit_image`), `Blend2DTextureRenderer`,
  `Blend2DRenderTargetRenderer`, `Blend2DPixelConvert.hpp` (straight RGBA8 <-> premultiplied BGRA).
  Two smoke-level example/tests (`blend2d_smoke_test.cpp`, `blend2d_surface_raster_test.cpp`).

That baseline shipped as a single commit with no independent audit; every remaining task in this
plan is the result of the audit that followed.

### Remediation pass — memory safety / lifetime

- **BLEND2D-3.** `BuildTintedSubImageEXT` (renamed `ResolveSourceImageEXT`) performed raw pointer
  arithmetic into a `BLImage`'s pixel buffer using the caller's `sourceRectangle` directly, with
  no bounds check against the actual texture dimensions — an out-of-bounds CPU read for any
  negative or oversized source rectangle combined with a non-white tint. Fixed: every source pixel
  read now goes through an address-mode-aware coordinate mapper (`AddressedCoordinateEXT`) that
  always returns an in-bounds index; the in-bounds fast path is additionally protected by
  Blend2D's own `blit_image` bounds check (`check_image_area`, `BL_ERROR_INVALID_VALUE`, now
  checked instead of ignored). Verified hostile (negative, oversized, entirely-outside) source
  rectangles, tinted and untinted, clean under ASan/UBSan/LeakSanitizer
  (`Blend2D_Correctness::TestSourceRectangles`).
- **BLEND2D-4.** Self-sampling (drawing a `RenderTarget2D` while it is the active render target)
  aliased the live `BLImage` as both source and destination with no snapshot, an unverified
  assumption against Blend2D's pinned-version synchronous-context guarantees. Fixed: detected via
  pointer identity against the active surface's image and routed through the same safe resolved-
  copy path. Verified no corruption for a 1:1 self-sampled draw
  (`Blend2D_Correctness::TestRenderTargets`).
- **BLEND2D-5.** Render-target lifetime (`Blend2DRenderTargetRenderer`'s raw `Blend2DRenderer&`)
  audited against CNA's established `GraphicsResource`/`GraphicsDevice::Dispose()` disposal-order
  contract (every tracked resource is disposed before the owning `IGraphicsRenderer` is torn
  down) — confirmed safe under that contract, matching the codebase-wide raw-backpointer pattern;
  not a Blend2D-specific gap. Documented in `Blend2DRenderer.hpp`'s class comment.
- **BLEND2D-6.** `ScopedContextStateEXT` (RAII `BLContext::save()`/`restore()`) added so an
  exception partway through a draw (a failed `BLResult` check, a resolve allocation failure)
  cannot leave a stale transform/clip attached to a surface's `BLContext`, corrupting subsequent
  draws.

### Remediation pass — silent semantic corruption

- **BLEND2D-7.** `Blend2DTextureRenderer::UpdatePixels` ignored its `stride` parameter and always
  advanced by `width * 4`, silently reading the wrong bytes for any caller-supplied stride wider
  than the tight row pitch. Fixed and verified with a deliberately padded stride and per-row
  distinct colours (`Blend2D_Correctness::TestTextureStride`).
- **BLEND2D-8.** `SpriteEffects` flips were geometrically wrong for a non-zero origin: `scale(-1,1)`
  mirrored about the local coordinate origin rather than the destination rectangle's own center,
  moving a flipped sprite off its requested destination for `origin=(0,0)` and producing
  systematically wrong results otherwise. Re-derived against `SoftwareSpriteBatchRenderer`'s
  per-vertex reference math (FNA/XNA's own flip semantics: mirror the SOURCE sampling, which is
  visually equivalent to mirroring the drawn content about the destination rect's own center) and
  generalized to negative destination width/height (an independent mirror source that XOR-composes
  with `SpriteEffects`). Verified across no-rotation, rotation, non-zero origin, non-uniform
  scale, both flip axes, and negative destination width
  (`Blend2D_Correctness::TestFlipOriginRotation`).
- **BLEND2D-9.** `ApplyBlendState` recognized only two raw tuples and silently routed every other
  `BlendState` (including materially different independent-alpha combinations) through
  `SRC_OVER`. Replaced with an explicit 6-factor match against the four stock XNA presets
  (`Opaque`/`AlphaBlend`/`NonPremultiplied`/`Additive`); any other tuple, including a colour/alpha
  mismatch, throws. Verified with a semi-transparent source over a semi-transparent destination
  for all four presets, not just opaque colours (`Blend2D_Correctness::TestBlendPresets`,
  `TestTintChain`).
- **BLEND2D-10.** `BlendWriteState` (`ColorWriteChannels`/`MultiSampleMask`) was accepted and
  silently discarded. Implemented a bounded whole-surface snapshot/merge for a non-default
  `ColorWriteChannels[0]`; `ColorWriteChannels1-3` (unimplemented MRT slots) and a non-default
  `MultiSampleMask` are rejected outright. Verified RGB-only and alpha-only masks against their
  correct (premultiplied-storage-consistent) results
  (`Blend2D_Correctness::TestColorWriteChannels`).
- **BLEND2D-11.** `SpriteBatch.Begin`'s `SetTransformMatrix`/`SetSamplerFilter`/
  `SetSamplerAddressMode`/`SetCustomEffect`/`SetImmediateMode` were unimplemented (inherited the
  shared `ISpriteBatchRenderer` no-op defaults), so a caller-supplied transform matrix, sampler
  state, or custom effect was silently ignored. All five implemented or given an explicit,
  audited no-op decision:
  - `SetTransformMatrix`: composed via `BLContext::apply_transform` as the outermost transform.
  - `SetSamplerFilter`: mapped to `BLPatternQuality` (Point-family -> Nearest, Linear-family ->
    Bilinear), matching `SdlRenderer`'s established `TextureFilter` decomposition.
  - `SetSamplerAddressMode`: Wrap/Clamp/Mirror implemented exactly via the resolved-copy path.
  - `SetCustomEffect`: rejects any non-null, non-exact-stock `SpriteEffect`.
  - `SetImmediateMode`: explicit no-op — every draw already executes synchronously per call.
- **BLEND2D-12.** `RasterizerState.ScissorTestEnable` had no renderer-side effect at all
  (`ApplyRasterizerState` was never overridden): once any scissor rectangle was set via
  `SetScissorRect`, it stayed applied forever, even after the caller disabled the scissor test,
  and stopped applying entirely after a render-target switch (each `Blend2DSurface` owns its own
  `BLContext`, and the clip had been applied eagerly to whichever one was active at
  `SetScissorRect`-call time). Redesigned: scissor rectangle and enabled flag are recorded only,
  and re-applied fresh on every draw, in target space, on whichever surface is currently active
  (`Blend2D_Correctness::TestScissor`).
- **BLEND2D-13** (found during the second, independent audit pass). `IGraphicsRenderer::
  SetViewport`'s shared no-op default was inherited, so a non-default XNA `Viewport` (e.g.
  split-screen) had no effect at all — every other CNA 2D renderer (Skia, Software) applies the
  viewport as SpriteBatch's coordinate origin and clip rectangle. Implemented to match; verified
  offset and clip behaviour (`Blend2D_Correctness::TestViewport`).
- **BLEND2D-14** (found during the second audit pass). `ApplyDepthStencilState`/
  `SetReferenceStencil` silently accepted any `DepthStencilState`/reference value even though
  `SupportsDepthStencil()`/`SupportsCapability(StencilBuffer)` both correctly report `false`.
  `DepthStencilState.None` (the state every `SpriteBatch.Begin` applies) is accepted; anything
  depth/stencil-enabling now routes through the same `HandleUnsupported3DCall` policy every other
  unsupported-3D entry point in this renderer uses.

### Remediation pass — presentation

- **BLEND2D-15.** `FixedHeightDynamicWidth` mapped to `SDL_LOGICAL_PRESENTATION_LETTERBOX` with
  the requested virtual width used unchanged — not the established CNA behaviour (aspect-derived
  logical width, re-computed as the window/output resizes). Reimplemented against
  `SkiaRenderer::RecreateBackbuffer`/`RefreshDynamicBackbufferIfNeeded`'s exact formula
  (`logicalW = round(outputW * requestedH / outputH)`) and refresh timing (on `Clear`/`Present`/
  `GetViewportSize`, skipped while a render target is bound). A test-only
  `DebugSetPresentationOutputSizeEXT`/`DebugClearPresentationOutputSizeEXT` pair (mirroring
  Skia's own) makes this deterministically testable without a real resizable window. Verified
  the derived width for two different simulated output aspect ratios, and that the bound-target
  suppression + eventual-consistency-after-unbind both hold
  (`Blend2D_Correctness::TestPresentationModes`).

### Remediation pass — unsupported-boundary correctness

- **BLEND2D-16.** `dynamic_cast<const Blend2DNativeImageSourceEXT&>(texture)` (an unchecked
  reference cast) produced an undiagnosable `std::bad_cast` if a foreign renderer's texture/
  render-target crossed the `SpriteBatch::Draw` boundary. Replaced with a checked
  `dynamic_cast<const T*>` + explicit `std::invalid_argument`, thrown before any native memory is
  touched (`Blend2D_Correctness::TestCrossRendererSafety`).
- **BLEND2D-17.** `Blend2DSpriteBatchRenderer::Begin()`/`End()` had no re-entrancy guard (a
  double `Begin()` silently overwrote the "begun" flag). Guarded to match every other CNA
  renderer's own Begin/End contract, and verified that a rejected double-`Begin()`/double-`End()`/
  invalid-sampler-state call does not poison the next valid cycle
  (`Blend2D_Correctness::TestBeginEndStateMachine`).

### Remediation pass — build/dependency

- **BLEND2D-18.** Verified the pinned Blend2D/AsmJit commits against the exact upstream source
  tree fetched by `cmake/ThirdPartyBlend2D.cmake` (`BL_FORMAT_PRGB32` semantics, `check_image_area`
  bounds behaviour, `BLContext` transform composition order, `BLPatternQuality`/`BLExtendMode`
  availability) rather than current upstream `master`. A fresh configure + build (native, and
  `-DCNA_SANITIZE=address,undefined`) both succeed with `CNA_BLEND2D_ROOT`/`CNA_ASMJIT_ROOT` unset
  (network `FetchContent` path).
- **BLEND2D-19.** Added `plan_blend2d.md` (this file) — the branch referenced it from multiple
  source-file comments without it existing.

### Remaining boundaries (explicit, not silently accepted)

These are documented, tested-as-absent boundaries, not oversights:

- **Mip chains and MSAA** are not implemented for `RenderTarget2D`/`Texture2D`; `CreateRenderTarget2D`'s
  `mipMap`/`multiSampleCount` parameters are accepted but produce a single-level, zero-sample
  target. `GraphicsCapability::MultiSampleAntiAliasing` correctly reports `false`.
- **Cube/volume textures** (`TextureCube`, `Texture3D`, `RenderTargetCube`) have no Blend2D
  representation; the shared `IGraphicsRenderer` defaults (`return nullptr`) are the correct,
  established "unsupported" signal this codebase already uses consistently for a renderer with no
  cube/volume concept, and are relied on unchanged.
- **Custom `Effect`/shader pipeline**: rejected outright (see BLEND2D-11). Blend2D has no runtime
  shading language comparable to Skia's bounded SkSL route; building one was judged out of scope
  for a CPU vector rasterizer whose value proposition is 2D raster fidelity, not a general shader
  ABI.
- **`Blend2D_Smoke`'s presentation checks** require a real X11/Wayland display
  (`SDL_VIDEODRIVER=x11`) and were not exercised by this remediation pass in a headless sandbox;
  `Blend2D_Correctness` covers the same construction/Clear/Present/mode-switch paths under
  `SDL_VIDEODRIVER=dummy` instead, which does not require (and cannot fully substitute for) a real
  compositor's presentation path.

## Reference implementations consulted

- **Skia** (`modules/renderers/skia/src/Skia*.cpp`): primary architectural reference for
  presentation (`RecreateBackbuffer`/`RefreshDynamicBackbufferIfNeeded`), the transform/scissor/
  viewport composition order in `SkiaSpriteBatchRenderer::Draw`, and the masked-write
  (`ColorWriteChannels`) precedent. Skia-specific features (SkSL runtime effects, mip-chain
  blending, mesh draws) were deliberately NOT ported — Blend2D has no equivalent primitive for
  any of them, and the task scope is genuine 2D raster correctness, not feature-count parity.
- **Software** (`modules/renderers/software/src/SoftwareSpriteBatch.cpp`): the authoritative
  in-repo reference for XNA/FNA's actual per-vertex `SpriteEffects` flip semantics (UV-coordinate
  swap, not destination-geometry mirroring), used to re-derive Blend2D's context-transform-based
  flip fix (BLEND2D-8) and to confirm the viewport-local coordinate contract (BLEND2D-13).
- **SdlRenderer** (`modules/renderers/sdl-renderer/src/SdlRenderer.cpp`): reference for the
  `TextureFilter` -> single-filter-mode decomposition (BLEND2D-11) and the general "no group-wrap
  for non-GPU renderers" register pattern for render targets.
