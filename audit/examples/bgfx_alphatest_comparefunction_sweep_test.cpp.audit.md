# Audit: examples/bgfx_alphatest_comparefunction_sweep_test.cpp

## Metadata

- Source file: `examples/bgfx_alphatest_comparefunction_sweep_test.cpp` (165 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `AlphaTestEffect.AlphaFunction` all-8-`CompareFunction` sweep
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_alphatest_comparefunction_sweep …)` /
  `cna_register_backend_test(NAME Bgfx_AlphaTest_CompareFunctionSweep …)`, `cmake/Tests/BgfxTests.cmake:377-380`).
- XNA/FNA relevance: direct — `AlphaTestEffect.AlphaFunction`/`ReferenceAlpha`, all 8 `CompareFunction` enum
  values.
- FNA reference: `src/Graphics/Effect/StockEffects/AlphaTestEffect.cs` (`OnApply()`'s `alphaTest` vector
  encoding switch), `HLSL/Common.fxh`/`AlphaTestEffect.fx` (`clip((a < x) ? z : w)` / `clip((abs(a-x)<y) ? z
  : w)` shader-side gates).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/AlphaTestEffect.cpp`
  (`FillGpuDrawParams()` lines 339-379, mirrors `OnApply()`'s CPU-side `alphaTest` vector encoding exactly),
  `src/CNA/Internal/Backends/Bgfx/shaders/fs_alpha_test3d.sc` (the `u_alphaTest`-consuming discard logic).

## Purpose

Sweeps all 8 `CompareFunction` values (`Always`, `Never`, `Less`, `LessEqual`, `Equal`, `NotEqual`,
`GreaterEqual`, `Greater`) against 3 alpha levels relative to a fixed `ReferenceAlpha=128`: 64/255 (below),
128/255 (at), 192/255 (above) — 24 total sub-checks — proving the Bgfx alpha-test shader's discard logic
implements every compare mode correctly, not just the default `Greater`. This is explicitly the first
`CompareFunction` sweep test on Bgfx (`Task 375`; the file's own header notes Bgfx never had one before,
only EasyGL did via Task 190).

## Checklist Results

### API / XNA / FNA parity

`kCases` (lines 50-59) encodes the expected pass/fail truth table for all 8 `CompareFunction` values. I
independently re-derived every row against `AlphaTestEffect.cs`'s real `switch (alphaFunction)` semantics
(shader evaluates `clip((a < x) ? z : w)`, with `x`/threshold set per-mode) rather than trusting the table:

| Mode | below(64) | at(128) | above(192) | File's row | Correct? |
|---|---|---|---|---|---|
| Always | draw | draw | draw | true,true,true | ✓ |
| Never | discard | discard | discard | false,false,false | ✓ |
| Less (a<ref) | draw | discard | discard | true,false,false | ✓ |
| LessEqual (a≤ref) | draw | draw | discard | true,true,false | ✓ |
| Equal (a==ref) | discard | draw | discard | false,true,false | ✓ |
| NotEqual (a≠ref) | draw | discard | draw | true,false,true | ✓ |
| GreaterEqual (a≥ref) | discard | draw | draw | false,true,true | ✓ |
| Greater (a>ref) | discard | discard | draw | false,false,true | ✓ |

All 8 rows are correct against FNA semantics. This is a genuine, non-boilerplate re-derivation, not a
restatement of the file's own comment.

### Behavioral correctness

`runOne()` (lines 67-90) sets `AlphaTestEffect` with a fixed white 1×1 texture and `DiffuseColor` left at
its FNA default (`Vector3::One`), so the shader's combined pixel alpha equals exactly the material `Alpha`
set via `setAlphaProperty(alpha)` (texture alpha=1, no vertex color path used here) — this is the correct
way to isolate the alpha-test gate from any other alpha contributor. Drawn/discarded is inferred from
`got.getRProperty() > 50` after an opaque-blended draw over a black clear: at `alpha∈{64,128,192}/255`, a
drawn pixel's R channel equals ~`alpha*255` (since RGB = texture(1,1,1) × diffuseColor(1,1,1)*alpha), i.e.
{64,128,192} — all cleanly above the 50 threshold — while a discarded pixel stays at the clear color's R=0.
The 50 threshold has comfortable margin on both sides (nearest case is 64 vs 0, not e.g. 51 vs 49), so this
detection heuristic is robust for the specific alpha values chosen.

### Logic

`fx.setReferenceAlphaProperty(kReference)` (128) and `fx.setAlphaFunctionProperty(func)` are set inside the
loop body before `fx.Apply()`, correctly forcing the effect's `DirtyAlphaTest`/`DirtyShaderIndex` flags to
recompute per iteration (`AlphaTestEffect::setAlphaFunctionProperty`/`setReferenceAlphaProperty`,
`AlphaTestEffect.cpp` lines 172-184). `RasterizerState::CullNone` (line 79) is required per the file's own
correctly-attributed note (Task 364/884): Bgfx's default `RasterizerState` cull state
(`BGFX_STATE_CULL_CCW`) is the only one of CNA's three main backends whose default actually matches FNA's
real `RasterizerState.CullMode = CullCounterClockwiseFace` default (confirmed against
`/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/States/RasterizerState.cs:127`) — EasyGL/Vulkan
default to no culling, so only Bgfx would silently cull this test's NDC quad winding without the explicit
override. This is accurately self-described as a still-open, tracked gap (Task 884), not overclaimed as
fixed.

### Robustness / Testing

Unlike every other file in this shard (`bgfx_alphatest_fog_test.cpp`, `bgfx_alphatest_null_texture_test.cpp`,
`bgfx_alphatest_vertexcolor_test.cpp`, all 3 `bgfx_basiceffect_*` fog/combined files), this file's `runOne()`
does **not** use the established "retry `Clear()`+`Draw()`+`GetBackBufferData()` up to 20 times, breaking on
first non-black read" pattern that every sibling test's own comment attributes to a specific Bgfx quirk
("`GetBackBufferData` only reliably reflects the first read per frame — Task 406 finding"). See F1.

## Detailed Findings

### F1 — Missing the sibling-established readback retry workaround; the file's own 24 single-shot draw+read cycles are more retry-workaround surface than any sibling test, yet the only one without it

- Severity: MEDIUM
- Confidence: LOW-MEDIUM (the underlying Bgfx readback quirk this workaround compensates for was not
  independently reproduced by this audit — Bgfx was not built/run in this sandbox — so this is a
  cross-file consistency/robustness observation, not a confirmed live flake)
- Category: robustness / test-flakiness-risk
- Location/symbol: `runOne()` (lines 67-90) — single `dev.Clear()` + `fx.Apply()` +
  `dev.DrawUserPrimitives()` + `dev.GetBackBufferData()` sequence, no retry loop
- Evidence: every other file in this batch (`bgfx_alphatest_fog_test.cpp` line 113-123,
  `bgfx_alphatest_null_texture_test.cpp` line 96-107, `bgfx_alphatest_vertexcolor_test.cpp` line 98-108,
  `bgfx_basiceffect_combined_test.cpp` line 128-140, `bgfx_basiceffect_emissive_test.cpp` line 111-123,
  `bgfx_basiceffect_fog_test.cpp` line 94-105, `bgfx_basiceffect_lit_fog_test.cpp` line 109-119) wraps its
  render+readback in a `for (i<20)` retry loop specifically because, per their shared comment, "Bgfx's
  `GetBackBufferData` only reliably reflects the first read per frame." This exact comment/pattern appears
  42 times across the Bgfx test corpus (`git grep` count), i.e. it is an established, project-wide,
  deliberately-adopted defensive idiom for this exact API — not a one-off. This file runs the *same*
  `dev.Clear()+Apply()+Draw()+GetBackBufferData()` sequence 24 times (8 functions × 3 alpha levels) with
  zero retries, meaning it has 24 independent opportunities to hit whatever readback timing issue motivated
  the other 7 files' workaround, yet is the one file in the batch not defended against it.
- Why it matters: if the underlying quirk is real (as its 42 independent occurrences across the codebase
  suggest), any of this test's 24 iterations could intermittently read stale/black data and register a
  false `[FAIL]` (a flaky CI test), without the file being able to distinguish "real regression" from
  "readback raced ahead of the draw." This is a robustness gap in test authoring, not a production-code
  defect — the underlying `AlphaTestEffect` compare-function logic itself was independently re-derived
  as correct in the API/FNA-parity section above.
- FNA/XNA comparison: N/A — test-infrastructure consistency issue, not an XNA/FNA behavior question.
- Related files: the 7 sibling files listed above (for the established pattern this file omits).
- Suggested future action (not implemented by this audit): adopt the same bounded retry-until-nonblack loop
  in `runOne()`, or determine (by actually running the Bgfx suite under CI-representative conditions) that
  a single-shot read is in fact reliable and update the 7 siblings' now-inconsistent comments instead.

## Cross-File Observations

- This is the correct, independently-verified truth table for `CompareFunction` against `AlphaTestEffect`'s
  real FNA encoding (see API/FNA parity above) — a useful reference point for any future test in this family
  that needs to reason about a specific compare mode's expected pass/fail behavior.
- Task 364/884's cull-state finding is repeated verbatim (and accurately) across all 8 files in this batch;
  it remains an honestly-tracked, not-yet-fixed gap in Bgfx's `RasterizerState` default, not something this
  audit needed to re-litigate per file.

## Missing or Weak Tests

- No boundary case at `ReferenceAlpha=0` or `255` (the actual clamp extremes) is exercised — only
  `kReference=128` is used throughout. Given `AlphaTestEffect`'s real default `ReferenceAlpha=0`, a
  regression specific to the zero-reference case (e.g. an off-by-one in the `threshold=0.5/255` epsilon at
  the boundary) would not be caught here. Not a defect in this file, just an uncovered edge worth flagging
  per the checklist's "boundary/error/parity coverage" guidance.

## Positive Findings

- The `kCases` truth table is precise and was independently confirmed correct for all 8 `CompareFunction`
  values against real FNA semantics, not just internally self-consistent.
- Correctly isolates the alpha-test gate from every other alpha contributor (white texture, default
  `DiffuseColor=One`, no vertex color) so the only quantity under test is the compare-function logic itself.
- Accurately and consistently attributes the `RasterizerState::CullNone` requirement to a real, still-open,
  correctly-cross-referenced tracked defect (Task 364/884) rather than silently working around it without
  comment.

## Final Assessment

The actual `CompareFunction` logic under test is correct and was independently re-verified against FNA's
real `AlphaTestEffect.cs` semantics for all 8 modes. The one weakness is procedural rather than logical:
this file is the sole holdout in its own test family that doesn't use the established readback-retry
defense the other 7 sibling files adopted for the same GPU API calls, which is worth reconciling one way
or the other (add the retry, or prove it's unnecessary and simplify the other 7).
