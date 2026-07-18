# Audit: examples/webgpu_rendertarget2d_test.cpp

## Metadata

- Source file: `examples/webgpu_rendertarget2d_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-webgpu` shard — `RenderTarget2D` support test (WEBGPU-53/54)
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_webgpu_test(cna_test_webgpu_rendertarget2d examples/webgpu_rendertarget2d_test.cpp)` /
  `cna_register_backend_test(NAME WebGPU_RenderTarget2D …)`, `cmake/Tests/WebGpuTests.cmake:131-132`).
- XNA/FNA relevance: direct — `GraphicsDevice.SetRenderTarget()`, `RenderTarget2D` constructor overloads
  (`mipMap`, `DepthFormat`, `multiSampleCount`, `RenderTargetUsage`), `RenderTarget2D.GetData()`,
  `ClearOptions` (`Target|DepthBuffer|Stencil`).
- Related production code: `src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp`
  (`WebGPURenderTargetBackend` constructor lines 1424-1559, `CreateRenderTarget2D()` lines 5788-5813,
  `SetRenderTarget2D()` lines 5815-5849, `FlushCurrentRenderTarget()`/`RenderPendingDrawsToRenderTarget()`
  around lines 5330-5459).

## Purpose

Eight-check test proving the WebGPU backend's first real `RenderTarget2D` support (before WEBGPU-53/54,
`CreateRenderTarget2D`/`SetRenderTarget2D` were `IGraphicsBackend`'s own safe no-op defaults — a null backend
handle or silent draw-to-backbuffer): (A) clear-only fill read back via `GetData()`, (B) a real `BasicEffect`
quad drawn into a target, (C) a depth+stencil-tested target where a nearer quad must win over a farther one
(exercising a genuine `Depth24Stencil8` attachment and a real in-pass stencil clear), (D) sampling 3 render
targets back onto the backbuffer via `SpriteBatch` without exception, (E) the architecture-critical check —
an intervening render-target-targeted `Clear()` must not leak into the backbuffer's own render pass, (F)
`MultiSampleCount` honesty when the backend's global MSAA is disabled, (G) `mipMap=true` must throw rather
than silently under-deliver.

## Executive Verdict

**Mostly healthy** — every check's core assertion was independently confirmed against the actual backend
source (the "one deferred render pass per logical frame, flushed on target switch" architecture, the
unconditional-mirror MSAA design, the `mipMap=true` throw). One LOW-severity test-hygiene finding: the
`ApplyBasicEffect()` helper deliberately leaks a heap-allocated `BasicEffect` (see F1) — harmless in a
short-lived CTest process but a pattern worth not propagating.

## Checklist Results

### API / XNA / FNA parity

`RenderTarget2D`'s 7-argument constructor (`width, height, mipMap, format, depthFormat, multiSampleCount,
usage`) and `GetData(level, rect, data, start, count)` overload used here match FNA's own signatures.
`ClearOptions::Target | DepthBuffer | Stencil` combined-flag usage (Check C) matches FNA's `[Flags]` enum
semantics.

### Behavioral correctness

- Check E's architecture claim (an intervening RT-targeted `Clear()` must not leak into the backbuffer's own
  pass) was traced end-to-end: `SetRenderTarget2D()` (lines 5815-5849) calls `FlushCurrentRenderTarget()`
  *before* reassigning `currentRenderTarget_`/`currentRenderTargetCubeFace_` on every target switch, which
  flushes whatever was previously bound (backbuffer or another RT) into its own dedicated
  `WGPURenderPassEncoder` — confirming target separation is structural, not incidental.
- Check F's claim (`RenderTarget2D` requesting `multiSampleCount=4` still reports 0 when the backend's own
  global MSAA is disabled) matches `WebGPURenderTargetBackend`'s constructor exactly: `msaaDescriptor.
  sampleCount = static_cast<uint32_t>(owner_->sampleCount_)` (line 1527) and `appliedMultiSampleCount_ =
  owner_->sampleCount_` (line 1556) — the constructor's own `multiSampleCount` parameter is read only for
  intent/validation, not for what gets applied; confirmed by this same file's own comment ("unconditionally
  mirror the backend's CURRENT global sampleCount_") and cross-verified against `webgpu_msaa_test.cpp`'s
  Check D, which proves the same mirroring in the *opposite* direction (MSAA actually enabled).
- Check G's claim (`CreateRenderTarget2D(mipMap=true)` throws) matches `CreateRenderTarget2D()` lines
  5799-5801 exactly (`if (mipMap) throw std::runtime_error(...)`).

### Logic

Check D's trailing comment (lines 246-253) about forcing the frame to flush via a throwaway
`ReadBackbufferCentre()` call, specifically to avoid contaminating Check E's own centre-pixel read with
still-pending `SpriteBatch` draws, is a subtle but correct precaution given this backend's deferred-pass
architecture — verified consistent with `EnsureFrameRendered()`'s lazy-flush model referenced elsewhere in
the backend.

### C++ correctness

No issues in the test file itself.

### Memory/resource lifetime

**F1** — see Detailed Findings: `ApplyBasicEffect()`'s function-local `static BasicEffect* fx = nullptr;`
allocated once via `new` and never `delete`d.

### Performance

N/A — one-shot test.

### Architecture

Correctly exercises the "one deferred render pass per frame, flushed on target switch" design this backend
uses instead of Vulkan's per-draw target-tag/replay model (per the header's own architecture note, verified
against the backend's `SetRenderTarget2D()` comment) — Checks D/E specifically stress the switch-flush
boundary this design depends on for correctness.

### Maintainability

The final gate uses `passCount_ == 8` (line 312) against exactly 8 `check()` calls (A, B, C, D, E×2, F, G) —
currently correct; same LOW hardcoded-literal observation as the other non-self-tallying files in this
batch.

### Portability

N/A.

### Robustness

Check D wraps the `SpriteBatch` sampling round-trip in a `try`/`catch` and asserts non-throw explicitly
rather than letting an uncaught exception abort the whole test opaquely — a good practice that gives a
useful diagnostic (`e.what()`) on failure rather than a bare crash.

### Testing

Comprehensive for the feature surface in scope (clear, draw, depth/stencil, sampling, target-isolation, MSAA
honesty, mip scope-cut) — no meaningful gap identified for `RenderTarget2D` itself. `RenderTargetCube`'s
distinct behavior is correctly left to its own sibling file.

## Detailed Findings

### F1 — `ApplyBasicEffect()`'s function-local static `BasicEffect*` is allocated once via `new` and never freed

- Severity: LOW
- Confidence: HIGH (directly visible in source)
- Category: memory-lifetime (test-code only, not production)
- Location/symbol: anonymous-namespace `ApplyBasicEffect(GraphicsDevice&)`, lines 115-124:
  `static BasicEffect* fx = nullptr; if (fx == nullptr) fx = new BasicEffect(dev);`
- Evidence: the pointer is captured in a function-local `static` and never deleted anywhere in this
  translation unit; the process exits via `Exit()`/`return game.getResult();` without an explicit cleanup
  path for it.
- Why it matters: harmless in practice for a short-lived, single-`Draw()`-call CTest executable (the OS
  reclaims the allocation at process exit, and `BasicEffect`'s destructor doing GPU-resource cleanup before
  device teardown is not required here since the whole `GraphicsDevice` is torn down moments later anyway).
  Flagged only because it is a `new` with no matching `delete` in first-party test code, and because the
  identical pattern recurs verbatim in `webgpu_rendertargetcube_test.cpp` (this same batch) — a copy-paste
  origin, not two independent authors making the same choice.
- FNA/XNA comparison: N/A (test-authoring style, not an XNA/FNA behavior question).
- Related files: `examples/webgpu_rendertargetcube_test.cpp` (identical helper, identical pattern).
- Suggested future action (not implemented by this audit): use a stack-allocated `BasicEffect` or a
  function-static *value* (not a `new`'d pointer) since nothing here requires deferred/optional construction
  beyond the trivial "construct once" memoization a plain function-static object already provides.

## Cross-File Observations

- Shares its `ApplyBasicEffect()`/`DrawFullScreenQuad()` helper shape and the identical F1 leak pattern with
  `webgpu_rendertargetcube_test.cpp` in this same batch — worth fixing both together if ever addressed.
- Corroborates `webgpu_msaa_test.cpp`'s own MSAA-mirroring claim from the opposite direction (MSAA disabled
  here vs. enabled there), strengthening confidence that `WebGPURenderTargetBackend`'s unconditional-mirror
  design is genuinely unconditional in both directions, not asymmetric.

## Missing or Weak Tests

None identified beyond F1's minor cleanup suggestion.

## Positive Findings

- Check E is a well-designed, deliberately gamma-invariant test (pure 0/255 channel colours, explicitly
  reasoned about in its own comment re: sRGB swapchain encoding) — isolates render-target separation from an
  unrelated, already-known open question about exact Clear-colour byte fidelity.
- Check C is a genuine stress test of a real Depth24Stencil8 attachment with an actual in-pass stencil clear
  (not merely depth), explicitly called out as exercising a "previously-unexercised stencil-attachment gap."

## Final Assessment

A solid, architecturally-aware `RenderTarget2D` test with accurate claims about the backend's deferred-pass
design, unconditional MSAA mirroring, and mip scope-cut, marred only by a harmless but avoidable `new`-without-
`delete` test-helper pattern shared with its `RenderTargetCube` sibling.
