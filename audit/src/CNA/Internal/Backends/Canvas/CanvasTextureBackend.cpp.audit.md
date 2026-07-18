# Audit: src/CNA/Internal/Backends/Canvas/CanvasTextureBackend.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/Canvas/CanvasTextureBackend.cpp`
- Audit status: AUDITED
- Subsystem: `backend-canvas` shard
- File type: C++ implementation (121 lines)
- Related header/implementation: `include/CNA/Internal/Backends/Canvas/CanvasTextureBackend.hpp` (same shard)
- XNA/FNA relevance: implements `Texture2D`'s backend-side pixel storage/upload
- Graphics backend relevance: texture backing for the Canvas backend
- FNA reference: N/A
- Main related tests: `examples-tests-canvas` (2 files, not yet audited)

## Purpose

Creates a private off-screen `<canvas>`/`CanvasRenderingContext2D` pair per texture instance (via `OffscreenCanvas`
where available, else a detached DOM `<canvas>` element), registered by integer id for `SpriteBatch`'s
`drawImage()` calls to source from.

## Executive Verdict

**Healthy.** Correct, minimal implementation; mip-level rejection matches the same "no native mip chain" honesty
already established in SdlRenderer/Dx3.

## Checklist Results

### Behavioral correctness / Logic
`UpdatePixelsLevel` (lines 110-120) correctly throws for any `level != 0` rather than silently discarding the
upload, then delegates `level == 0` to the ordinary `UpdatePixels` path — matches
`SdlTextureBackend::UpdatePixelsLevel`'s equivalent, already-audited behavior exactly, down to citing the same
Task 681 precedent in its own comment.

### Memory/resource lifetime
`NextCanvasId()` (lines 68-72) is a function-local static `int` incremented with no synchronization — safe given
Emscripten's default single-threaded execution model (consistent with every other backend's thread-safety
posture in this audit); would be a real race if this backend were ever built with Emscripten's pthreads support
and textures created from multiple threads, which is not this backend's documented use case.

### C++ correctness / Performance / Thread safety / Portability / Architecture / Maintainability / Robustness / Testing
No issues found; `CNA_Canvas2D_UpdatePixels`'s mirror-tile cache invalidation (lines 45-56) is a correct,
proactive fix for a real potential staleness bug (a `SetData()` call after a Mirror-addressed draw had already
cached a pre-tiled mirror pattern would otherwise show stale pixels on the next Mirror-addressed draw).

## Detailed Findings

None.

## Cross-File Observations

The mirror-tile cache invalidation here and in `CanvasRenderTargetBackend.cpp`'s `BindAsRenderTarget` (both
delete `Module['cnaMirrorTiles'][id]`) are consistent, correctly-applied instances of the same cache-invalidation
discipline — worth confirming no third code path that could also stale this cache (e.g. a hypothetical future
direct-pixel-write path) was missed, though none was found in this audit's reading of all 4 `.cpp` files in this
shard.

## Missing or Weak Tests

Not independently assessed (queued for `examples-tests-canvas`).

## Positive Findings

Proactive, correct cache-invalidation discipline for the mirror-tile optimization.

## Final Assessment

No issues found.
