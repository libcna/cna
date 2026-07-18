# Audit: examples/easygl_bloom_pipeline_test.cpp

## Metadata

- Source file: `examples/easygl_bloom_pipeline_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend integration test
- File type: C++ example/integration-test executable (`EasyGLBloomPipelineTest : Game`, `main()`)
- Related production code: `CNA::Internal::Backends::EasyGLSpriteBatchBackend::FlushBatch`
  (`EasyGLGraphicsBackend.cpp` lines 1082-1171), `EasyGLGraphicsBackend::GetCurrentRenderTarget2DSize`
  (line 1616), `Microsoft::Xna::Framework::Graphics::RenderTarget2D`, `ShaderEffect`.
- XNA/FNA relevance: exercises chained `RenderTarget2D` passes driven by custom `Effect`s inside `SpriteBatch` —
  real XNA 4.0-shaped rendering pattern (multi-pass post-processing). The specific 4-pass composition
  (`Extract → BlurH → BlurV → Combine`) mirrors the Microsoft XNA Game Studio **BloomPostprocess** sample's
  `BloomComponent.cs::Draw()`, not FNA itself.
- FNA reference: N/A for the shader bodies (see the 3 sibling `easygl_bloom_*_test.cpp` reports in this batch for
  the per-shader detail; `BloomSample_4_0` is not present in the local FNA reference tree). This file's most
  significant claim — the Task 1078 viewport-sizing bug/fix — **was** independently verified against the actual
  production diff described below.
- Main related tests: composes `easygl_bloom_extract_test.cpp`, `easygl_bloom_gaussianblur_test.cpp`,
  `easygl_bloom_combine_test.cpp`'s three shaders end-to-end.

## Purpose

Task 946/1078: proves the three individually-ported bloom shaders (Extract/GaussianBlur/Combine) compose correctly
through a real 4-pass `RenderTarget2D` chain, mirroring `BloomComponent.cs::Draw()`'s own pass sequence. The file's
own header (lines 27-35) documents a genuine bug this test found and fixed: `EasyGLSpriteBatchBackend::FlushBatch()`
previously always sized its viewport/orthographic projection to the **window**, never to whichever `RenderTarget2D`
was actually bound, so a custom-effect draw into a half-resolution intermediate RT rendered into the wrong
sub-region instead of filling it. Correctly placed as an `easygl_`-prefixed integration test.

## Executive Verdict

**Healthy**, and unusually valuable among this batch: this is a genuine "test found a real bug, and the fix is
independently verifiable in the shipped code" case, not just a "test that passes." The three-check design (halo
spillover / far-black / centre-brightness) correctly isolates the property most likely to break under refactoring
(RT-chaining correctness) rather than only re-testing the individual shaders' own math (already covered by the
sibling files).

## Checklist Results

### API / XNA / FNA parity
N/A directly (`ShaderEffect`/`RenderTarget2D` chaining pattern is CNA/XNA-shaped, not itself a distinct API surface
beyond what the 3 sibling files already establish).

### Behavioral correctness — the Task 1078 claim, independently verified
Read `EasyGLSpriteBatchBackend::FlushBatch()` (`EasyGLGraphicsBackend.cpp:1082-1171`) directly:
```
if (graphicsBackend_ && graphicsBackend_->GetCurrentRenderTarget2DSize(rtW, rtH) && rtW > 0 && rtH > 0)
{
    device_.set_viewport(0, 0, rtW, rtH);
    logW = rtW; logH = rtH;
}
else if (graphicsBackend_) { /* falls back to window physical/logical size */ }
```
(lines 1109-1123) — this exact `GetCurrentRenderTarget2DSize()`-first, window-size-fallback structure, with the
inline comment "Task 1078: a custom-effect draw into a bound RenderTarget2D must size its viewport and orthographic
projection to that RT, not the window" (lines 1105-1108), corroborates the file's own header claim precisely: the
fix is real, present, and named consistently (same task number) between the test file and the production commit it
describes. `GetCurrentRenderTarget2DSize` (`EasyGLGraphicsBackend.cpp:1616`) exists and is called exactly where the
comment says.

This test's own design directly exercises the scenario the bug required: `rt1`/`rt2` are `kBloomSize×kBloomSize`
(`128/2=64`), genuinely smaller than `sceneRt`/the window (`kSceneSize=128`) — unlike the sibling
`easygl_bloom_gaussianblur_test.cpp`, whose `RenderTarget2D`s happen to match the window size and thus could not
have caught this regression (see that file's own Cross-File Observations).

### Logic
Linear `Draw()` guarded by `done_`, 4 sequential passes exactly matching the header's documented sequence
(Extract → BlurH → BlurV → Combine), each correctly binding its own `RenderTarget2D` via `SetRenderTarget`,
clearing, applying its effect + uniforms, and drawing a full-quad `sb.Draw(prevRt, Rectangle(0,0,dstW,dstH), ...)`
before the next pass. Verified pass 4 (Combine) correctly restores `SetRenderTarget(nullptr)` (line 262) before
drawing to the real backbuffer, and correctly re-uses `sceneRt` (the full-res, pre-blur scene) as the second texture
unit (`combineFx.SetTexture(1, sceneRt)`, line 266) — matching `BloomCombine.fx`'s intended `base` (unblurred scene)
+ `bloom` (blurred extract) composition, not accidentally feeding the blurred RT into both slots.

### Memory/resource lifetime
`sceneRt`, `rt1`, `rt2` are stack-local `RenderTarget2D` values, all destructed at `Draw()`'s scope exit after
`device.SetRenderTarget(nullptr)` has already run (line 262) — no dispose-while-bound risk (the same hazard
`easygl_bound_resource_dispose_test.cpp` in this batch explicitly tests for).

### C++ correctness
`ComputeBlurParameters`'s array-reference parameters (`float (&weights)[kSampleCount], float (&flatOffsets)
[kSampleCount * 2]`, lines 139-140) are correctly, consistently sized against `kSampleCount=15` throughout; no
raw-pointer/length desync risk.

### Performance
N/A — single-frame test; 4 RT passes are appropriate for what's being proven, not gratuitous.

### Architecture
Faithfully mirrors the real sample's own pass structure (Extract at half-res, two separable blur passes at
half-res, Combine back at full-res) rather than a simplified stand-in — a meaningfully accurate architectural port,
consistent with the project's stated intent (header lines 12-16) to scope out the 3D/model-loading machinery while
preserving the actual bloom pipeline shape.

### Robustness
Compile-error guard for all three effects (`!extractFx.IsEffectValid() || !blurFx.IsEffectValid() ||
!combineFx.IsEffectValid()`, line 196) fails loud rather than proceeding with an invalid program.

### Testing
This file is itself a test — its 3-check design (centre/halo/far) is discussed further below.

## Detailed Findings

No MEDIUM-or-higher findings for this file. Two LOW items, both about check tightness:

### F1 — "Halo" check only asserts `>0`, not a meaningful magnitude range

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `haloOk` (line 286: `haloPx.getRProperty() > 0`)
- Evidence: unlike the sibling `easygl_bloom_gaussianblur_test.cpp`'s analogous "adjacent" check (also just `> 0`,
  so this is a consistent shard-wide pattern, not a one-off weakness) and unlike its own "centre" check (`>= 200`,
  a real magnitude floor), the halo check accepts any non-zero value including e.g. `1/255`, which would technically
  "pass" even for a nearly-imperceptible bloom leak that might itself indicate a partially-broken (but not fully
  broken) blur radius or intensity.
- Why it matters: weakens this check's ability to catch a *regression that reduces* bloom spillover without
  eliminating it entirely (e.g. an accidentally-halved blur radius) — it would still register as "halo > 0" and
  pass. The "far == 0" and "centre >= 200" checks remain strong regardless.
- FNA/XNA comparison: N/A.
- Related files: `easygl_bloom_gaussianblur_test.cpp` (same pattern in its own "adjacent" check).
- Suggested future action (not implemented by this audit): tighten to a minimum meaningful floor (e.g. `> 10`) if
  this test is revisited.

### F2 — Shares the `ComputeGaussian`/`ComputeBlurParameters` duplication noted in the GaussianBlur sibling report

- Severity: LOW
- Confidence: HIGH
- Category: maintainability / duplication
- Location/symbol: `ComputeGaussian` (lines 133-137), `ComputeBlurParameters` (lines 139-166)
- Evidence/why it matters: see `easygl_bloom_gaussianblur_test.cpp.audit.md`'s Finding F1 for the full analysis;
  recorded here too since duplication is a property of both files jointly, not attributable to only one.

## Cross-File Observations

- Confirms (independently, via reading production code) the Task 1078 bug/fix this file's header describes is real
  and precisely as documented — a genuine example of a test-authoring process finding and fixing a real backend bug,
  not just documentation embellishment. Worth citing in `AUDIT_CROSS_CUTTING_FINDINGS.md` as a positive example of
  the "write the integration test, find the real bug" workflow this project's task history shows repeatedly.
- Reuses (independently re-implements, not shares via header) `BloomCombine.fx`'s GLSL body and forces
  `BloomSaturation=BaseSaturation=1.0` exactly like `easygl_bloom_combine_test.cpp` — see that file's Finding F1;
  the same `AdjustSaturation()` blind spot applies here too (`combineFx.SetUniformFloat("uBloomSaturation", 1.0f)`,
  line 269).

## Missing or Weak Tests

- See F1 (halo-check tightness).
- See `easygl_bloom_combine_test.cpp.audit.md` F1 — this file's Combine pass also never exercises
  `AdjustSaturation()` at a fractional saturation.

## Positive Findings

- Directly, independently verified: the Task 1078 viewport-sizing bug this file's header claims to have found is
  real, present in the fix, and precisely matches the described mechanism — a rare case in this audit where a test
  file's own historical narrative could be fully corroborated against the actual backend source.
- Correctly designed to expose exactly the failure mode its sibling tests structurally cannot (mismatched RT vs.
  window size) rather than duplicating their coverage.

## Final Assessment

A well-designed, faithfully-verified end-to-end pipeline test whose central historical claim (finding and fixing a
real viewport-sizing bug via this exact test) was independently confirmed against `EasyGLSpriteBatchBackend::
FlushBatch()`. Only minor check-tightness and cross-file duplication notes, both LOW severity, keep this from a
perfect score.
