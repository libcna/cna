# Audit: examples/bgfx_rasterizerstate_depthbias_test.cpp

## Metadata

- Source file: `examples/bgfx_rasterizerstate_depthbias_test.cpp` (174 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `RasterizerState.DepthBias` pixel test
- File type: standalone `Game`-subclass executable, CTest-registered (`cna_test_bgfx_rasterizerstate_depthbias`
  / `Bgfx_RasterizerState_DepthBias`, `cmake/Tests/BgfxTests.cmake:710-713`)
- XNA/FNA relevance: direct — `RasterizerState.DepthBias`/`SlopeScaleDepthBias`,
  `DepthStencilState.DepthBufferFunction`.
- Related production code: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`
  (`ApplyRasterizerState()` lines 1773-1797, `SetDepthBiasUniform()` lines 2183-2186,
  `ApplyDepthStencilState()` lines 1674-1721), `src/CNA/Internal/Backends/Bgfx/shaders/vs_colored3d.sc`
  (`u_depthBias` Z-offset).

## Purpose

Task 767: verifies `RasterizerState.DepthBias` is genuinely applied on Bgfx via a per-draw
vertex-shader Z-offset (bgfx has no native polygon-offset mechanism, unlike D3D9/Vulkan). Uses a
"coplanar redraw" methodology: force `DepthStencilState.DepthBufferFunction = Less` (XNA's real default
`LessEqual` can't discriminate a coplanar second draw, since an equal-depth redraw always passes
regardless of bias), draw a red triangle A with no bias, then an identically-shaped green triangle B
with the scenario's `RasterizerState`. Check A (no bias) expects the centre to stay RED (B's redraw
fails the strict `Less` test); Check B (`DepthBias=-1e6`) expects GREEN (bias pulls B in front). Check C
(`SlopeScaleDepthBias=-2e3` on a tilted triangle) is explicitly informational-only, since the file's own
header states `SlopeScaleDepthBias` is a deliberately unemulated, project-owner-approved gap
(2026-07-10) on this backend specifically.

## Executive Verdict

**Healthy.** Independently confirmed the `DepthBias` emulation formula
(`kDepthBiasScale = 1/16777215`, i.e. `1/(2^24-1)`, matching the D3D9 24-bit-depth-buffer convention
XNA's `DepthBias` scale is defined against) is real, correctly wired from
`GraphicsDevice::setRasterizerStateProperty` through to the vertex shader's `gl_Position.z +=
u_depthBias.x * gl_Position.w`, and that the `SlopeScaleDepthBias` non-emulation claim is 100% accurate
today (the parameter is explicitly discarded — `float /*slopeScaleDepthBias*/` — not silently dropped
somewhere less visible).

## Checklist Results

### API / XNA / FNA parity
`DepthStencilState`'s default ctor (`src/Microsoft/Xna/Framework/Graphics/DepthStencilState.cpp:10-28`)
sets `depthBufferEnable_=true`, `depthBufferWriteEnable_=true`, `depthBufferFunction_=LessEqual` —
matches FNA's `DepthStencilState.Default`. The test's own `DepthStencilState dss;
dss.setDepthBufferFunctionProperty(CompareFunction::Less);` correctly leaves `DepthBufferWriteEnable`
at its true default (`true`), which this audit confirms is load-bearing: without depth writes enabled
on the first (red) draw, the second (green) draw's depth comparison would have nothing meaningful to
compare against.

### Behavioral correctness
Traced `ApplyDepthStencilState()`'s `depthFunc` switch (`case 2 → BGFX_STATE_DEPTH_TEST_LESS`) — correct
for `CompareFunction::Less` (XNA ordinal 2). Traced `ApplyRasterizerState()`'s `depthBias_ = depthBias;`
(no scaling at this layer) and `SetDepthBiasUniform()`'s `depthBias_ * kDepthBiasScale` — for
`DepthBias=-1000000.0f`, this yields `≈-0.0596`, added directly to `gl_Position.z` (scaled by `w` to
survive perspective divide as an approximately-constant NDC offset) — comfortably large enough to flip a
`Less`-comparison coplanar redraw in a 64×64 test image, consistent with Check B's expectation.
Independently verified `Detail`/`ApplyRasterizerState`'s signature discards `slopeScaleDepthBias`
entirely (`void BgfxGraphicsBackend::ApplyRasterizerState(int cullMode, int fillMode, bool
scissorTestEnable, float depthBias, float /*slopeScaleDepthBias*/)`), confirming Check C's
"informational only, not emulated" framing is accurate, not a stale disclaimer.

### Logic
`RunCheck()`'s own draw order (draw un-biased triangle A first, always with the *default*
`RasterizerState()`, then draw B with the scenario's `RasterizerState`) correctly isolates the variable
under test — A's state is never itself varied across checks, so any check failure is attributable to B's
`RasterizerState` alone.

### C++ correctness
`SetDepthBiasUniform()` is called on every relevant draw path (`DrawColoredPrimitives`,
`DrawIndexedColoredPrimitives`, and the `Ex` overloads) — confirmed this test's `DrawUserPrimitives`
calls route through `DrawColoredPrimitives`, which does call it (line 2289), so the bias is genuinely
live for this test's draw calls, not merely present in an unrelated code path.

### Testing
Two hard pass/fail checks (A, B) plus one clearly-labeled informational-only check (C) that never
affects the exit code (`result_ = (passCount == 2) ? 0 : 1`, using only `okA`/`okB`) — an honestly framed
test that doesn't inflate its own pass count with an assertion it explicitly can't make.

## Detailed Findings

None. No HIGH/CRITICAL/MEDIUM defects found — both the emulation implementation and the test's own
methodology and claims check out.

## Cross-File Observations

- Mirrors `examples/easygl_depth_bias_test.cpp` (Task 767's EasyGL half) and
  `examples/vulkan_depth_bias_test.cpp` (Task 328)'s shared "shadow acne" coplanar-redraw methodology,
  restructured per-check (rather than one multi-point single-frame render) specifically for Bgfx's own
  first-read-per-frame `GetBackBufferData` limitation (Task 406) — consistent with every other file in
  this shard.
- The `SlopeScaleDepthBias` non-emulation gap is consistent with this project's documented precedent for
  other known, deliberately-scoped-out limitations (e.g. `OcclusionQuery.PixelCount()`, cited in the
  file's own header) rather than a silently-dropped feature.

## Missing or Weak Tests

- No test in this file (or, as far as this audit found while researching it, elsewhere in the Bgfx
  shard) exercises a *positive* `DepthBias` value or a magnitude near XNA's typical small usage range
  (XNA doc examples use values like `0.0001f`-scale, not `-1e6`) — the huge magnitude here is
  appropriate for a binary pass/fail discriminator but does not by itself validate the bias *scale*
  factor is XNA-accurate at realistic magnitudes, only that its sign and rough order of magnitude are
  correct. Not a defect in this file, but a gap worth flagging for a future task.

## Positive Findings

- Correctly and deliberately avoids asserting anything about the unemulated `SlopeScaleDepthBias`
  parameter rather than papering over the gap — the file's own "informational only" framing was
  independently confirmed accurate against the current `ApplyRasterizerState` signature.
- The depth-bias scale constant (`1/16777215`) and its application point (vertex shader, scaled by `w`)
  were independently traced and are dimensionally sound for a 24-bit-depth-buffer-style constant bias.

## Final Assessment

A well-designed, honestly-scoped test. Both hard checks and the documented emulation gap were
independently verified against the current production code and hold up.
