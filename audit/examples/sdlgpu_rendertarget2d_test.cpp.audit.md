# Audit: examples/sdlgpu_rendertarget2d_test.cpp

## Metadata

- Source file: `examples/sdlgpu_rendertarget2d_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlgpu` shard — `RenderTarget2D` proof for the SDL_GPU backend
- File type: standalone `Game`-subclass executable, CTest-registered (`SdlGpu_RenderTarget2D`,
  `cmake/Tests/SdlGpuTests.cmake:41-43`, `TIMEOUT 60 LABELS "SdlGpu"`)
- XNA/FNA relevance: direct — `RenderTarget2D` constructor overload, `RenderTarget2D.
  MultiSampleCount`, `RenderTarget2D.GetData(level, Rectangle*, T*, startIndex, elementCount)`.
- FNA reference: `Graphics/RenderTarget2D.cs`, `Graphics/Texture2D.cs` (`GetData` overload set).
- Related production code: `src/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.cpp`
  (`SdlGpuRenderTargetBackend::GetData` lines 4377-~4440, `RenderToTarget`'s per-target-pass
  ordering lines 686-697, 809-851), `include/Microsoft/Xna/Framework/Graphics/Texture2D.hpp`
  (`GetData` overload set).

## Purpose

Seven-check, 120-frame test proving a `SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SAMPLER` texture can be
rendered into during its own render pass and sampled during a later pass within the same frame
(Phase SDLGPU-8's per-target multi-pass `EnsureFrameRendered()`). Check A is a Clear()-only fill
(no draw call) — specifically testing whether a target that never receives an actual draw still
gets its own render pass at all; Check B is a real colored3d triangle; Check C is a depth-tested
target (farther Red quad, then nearer Green); Check D is `MultiSampleCount` fidelity for the
non-MSAA case (0 requested → 0 applied, real MSAA round-trip deferred to
`sdlgpu_rendertarget2d_msaa_test.cpp`); Checks E/F/G are real `GetData()` pixel assertions on the
same three targets Checks A-C populate.

## Executive Verdict

**Healthy.** All 7 checks map onto real, independently-traced backend behavior; the file's own
claim that `GetData()` "IS real ... reads its own self-owned colorTexture_, not the swapchain"
(header lines 8-10) was independently confirmed by tracing `SdlGpuRenderTargetBackend::GetData`
and cross-checked against `plans/plan_sdlgpu.md`'s SDLGPU-39 row, which documents this specific
capability (and the reason the swapchain-equivalent remains unimplemented) in detail. No defect
found.

## Checklist Results

### API / XNA / FNA parity

`RenderTarget2D(dev, w, h, mipMap, format, depthFormat, preferredMultiSampleCount, usage)` (lines
171-176) matches FNA's constructor argument order and count.
`RenderTarget2D::GetData(level, Rectangle*, Color*, startIndex, elementCount)` (`ReadCentrePixel`,
lines 76-83, and the inline `RenderTarget2D rtNoMsaa(...)` check at line 227-230) matches FNA's
five-argument `Texture2D.GetData` overload FNA's `RenderTarget2D` inherits.

### Behavioral correctness

- **Check A** (Clear-only): confirmed via `RenderToTarget` (lines 809-851) that a render pass is
  built and executed for *every* target in `usedRenderTargetsThisFrame_` regardless of whether it
  received a draw call — `MarkUsedThisFrame()` (called from `BindAsRenderTarget()`, line 4358) adds
  the target the moment it is bound, independent of any subsequent `DrawPrimitives` call, so
  `rtClearOnly_`'s Clear-only content is not silently dropped by an "only render targets with an
  actual draw get a pass" optimization that would defeat this specific check's own purpose.
- **Check B** (colored3d triangle): `BasicEffect`+`VertexColorEnabled`, drawn via
  `DrawPrimitives(TriangleList, 0, 1)` (line 126) — correctly passes a *primitive* count of 1 for
  a 3-vertex single triangle (`triVerts[3]`, lines 178-182), not a vertex count, avoiding the
  known `DrawPrimitives`-primitiveCount-vs-vertexCount confusion this project's own memory notes
  flag as a recurring authoring mistake elsewhere.
- **Check C** (depth-tested target): farther Red quad (`z=0.5`) drawn before nearer Green
  (`z=-0.5`, lines 140-143), matching the correct draw order to make "nearer wins" a genuine
  depth-test outcome (see Missing or Weak Tests for the same caveat noted in this shard's MSAA
  sibling test).
- **Check D**: constructs a fresh `RenderTarget2D rtNoMsaa(...)` with `preferredMultiSampleCount=0`
  (lines 227-228) and asserts `getMultiSampleCountProperty() == 0` — correctly delegates the *real*
  MSAA-request round-trip (nonzero request → clamped nonzero applied) to
  `sdlgpu_rendertarget2d_msaa_test.cpp`, avoiding duplicate/overlapping test responsibility between
  the two files.
- **Checks E/F/G**: `ReadCentrePixel` (lines 76-83) uses an 8-tolerance `Matches` (lines 68-74) —
  tighter than the 10-tolerance most other checks in this shard use, appropriate since these are
  flat-color-fill assertions (Green/Red/Green) with no blend or lighting gradient to account for.

### Logic

`RenderIntoTargets`/`SampleTargetsToBackbuffer` (lines 111-163) are called both in the frame-1
`try` block (lines 212, 215) and unconditionally in the `else` branch for frames 2-120 (lines
262-263) — the 120-frame repeat genuinely re-exercises the full render/sample cycle every frame,
not just once, matching the file's own "120 frames ... render with no exception" closing check
(line 268).

### Memory/resource lifetime

All three render targets (`rtClearOnly_`, `rtTriangle_`, `rtDepth_`) are members (lines 90-92),
correctly avoiding the local-render-target-use-after-free pattern this shard's
`sdlgpu_rendertarget_lifetime_test.cpp` specifically regression-tests. The Check D
`RenderTarget2D rtNoMsaa(...)` (line 227) is a genuine local variable, but is only ever
constructed and immediately queried for a CPU-side property (`getMultiSampleCountProperty()`) —
never bound, drawn into, or sampled, so it has no queued GPU commands that could reference a
freed handle and is unaffected by the deferred-release mechanism entirely.

### Testing

Comprehensive layering: no-throw smoke coverage (A-D via the `try`/`catch` at lines 208-223), a
dedicated MultiSampleCount property check (D), and real pixel-content assertions (E/F/G) — this
file was the one that closed out SDLGPU-39's `RenderTarget2D::GetData()` leg per `plans/plan_sdlgpu.md`,
and its own row there confirms these three checks were added specifically to replace "the
screenshot-only verification this row previously depended on," which this audit's own reading of
the current file confirms is exactly what E/F/G do.

## Cross-File Observations

- `plans/plan_sdlgpu.md`'s SDLGPU-39 row (secondary context per D-3) confirms this file's own claim
  about `GetData()` being "real" is accurate and not stale: `RenderTarget2D::GetData()` downloads
  from `colorTexture_` (the always-single-sample, already-resolved-if-MSAA sampleable texture)
  using the same transfer-buffer+fence pattern as `RenderTargetCube`/`TextureCube`/`Texture3D`'s
  own legs — independently confirmed via `SdlGpuRenderTargetBackend::GetData`'s
  `owner_->EnsureFrameRendered()` call before the download (forces any pending draws to actually
  render first) and its download source (`region.texture = state_->colorTexture`, never the
  swapchain texture).
- The same plan-doc row explains, in detail, *why* the swapchain-equivalent (`ReadBackbuffer`)
  remains unimplemented: `SDL_gpu.h`'s own doc comment on the swapchain-acquisition function states
  the swapchain texture is "write-only and cannot be used ... for another reading operation" —
  a permanent SDL_GPU API contract, not a driver bug or an unfinished feature. This file's own
  header comment (lines 6-10) accurately reflects this documented, permanent limitation rather than
  describing it as an open TODO — confirmed not stale.
- Fog and skinned-normal-transform cross-cutting bugs are **not applicable**: this file exercises
  only `BasicEffect`+`VertexColorEnabled` with no `FogEnabled`/skinning code path.

## Missing or Weak Tests

- Check C's "nearer Green wins" depth-test proof (farther Red drawn first, nearer Green drawn
  second) cannot distinguish a correctly-functioning depth test from a silently-disabled one, since
  Green is drawn last regardless and would win either way (same caveat independently found in this
  shard's `sdlgpu_rendertarget2d_msaa_test.cpp`, Check C) — not a live defect, since depth testing
  in this backend was already independently confirmed correct via other files in this project's
  wider test suite, but a coverage gap specific to this exact check's own discriminating power.

## Positive Findings

- `GetData()`'s "real, not swapchain-backed" claim was independently traced and confirmed, with the
  swapchain-download limitation's root cause (a genuine, documented, permanent SDL_GPU API
  contract) verified against the project's own git-log-documented investigation rather than taken
  on faith.
- Check A (Clear-only, no draw) is a well-chosen edge case that specifically discriminates whether
  this backend's per-target multi-pass architecture depends on an actual draw call being queued —
  confirmed it does not, via `MarkUsedThisFrame()`'s bind-time (not draw-time) registration.
- Check D correctly avoids duplicating the MSAA round-trip test already owned by a sibling file in
  this same shard, keeping test responsibilities cleanly separated.

## Final Assessment

A well-constructed, appropriately-scoped `RenderTarget2D` test with real pixel-content assertions
backing every claim about rendered content, and an accurate, independently-corroborated
description of this backend's `GetData()`/swapchain-readback capability boundary. No defects found
in this file or the production code paths it exercises.
