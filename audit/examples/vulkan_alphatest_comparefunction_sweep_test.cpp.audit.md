# Audit: examples/vulkan_alphatest_comparefunction_sweep_test.cpp

## Metadata

- Source file: `examples/vulkan_alphatest_comparefunction_sweep_test.cpp` (158 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `AlphaTestEffect` all-`CompareFunction` threshold sweep
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_alphatest_comparefunction_sweep …)` /
  `cna_register_backend_test(NAME Vulkan_AlphaTest_CompareFunctionSweep …)`, `cmake/Tests/VulkanTests.cmake:570-572`).
- XNA/FNA relevance: direct — `AlphaTestEffect.AlphaFunction`/`ReferenceAlpha`, all 8 `CompareFunction`
  enum values.
- FNA reference: `Graphics/Effect/StockEffects/AlphaTestEffect.cs` (`OnApply()`'s `alphaTest` vec4
  encoding, lines 331-416), `HLSL/AlphaTestEffect.fx` (`PSAlphaTestLtGt`/`PSAlphaTestEqNe`'s `clip()`
  expressions).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/AlphaTestEffect.cpp`
  (`FillGpuDrawParams()` lines 313-380), `src/CNA/Internal/Backends/Vulkan/shaders/alpha_test3d.frag.glsl`
  (the actual GPU-side discard logic), `VulkanGraphicsBackend.cpp:7366-7367` (`needsAlphaTest` dispatch).
- git corroboration: `3cc13137`/`9dcf85f5` "test(Tasks 373-375): AlphaTestEffect CompareFunction
  threshold sweep, all 3 backends" and `b6a00bc6`/`bdb69b03` "fix(Task 896): push GraphicsDevice's real
  default RasterizerState to all 3 backends" — both match this file's own header/inline comments exactly.

## Purpose

Exercises all 8 `CompareFunction` values (`Always`, `Never`, `Less`, `LessEqual`, `Equal`, `NotEqual`,
`GreaterEqual`, `Greater`) against three alpha inputs (64/255 "below", 128/255 "at", 192/255 "above" a
fixed `ReferenceAlpha=128`), for 24 total pass/fail assertions. `kCases[]` (lines 44-53) hard-codes the
expected keep/discard outcome for each `(function, alpha-tier)` combination; `runOne()` draws a full-
screen quad with that function/alpha, reads back the centre pixel, and treats `R>50` as "drawn" vs.
"discarded" (the quad is opaque white on a black-cleared background, so this threshold cannically
separates the two outcomes).

## Executive Verdict

**Healthy** — this audit independently re-derived the GPU-side `clip()` semantics for all 8
`CompareFunction` values directly from `AlphaTestEffect::FillGpuDrawParams()`'s comment-documented
formula (lines 339-340: `if ((y>0) ? (|a-x|<y) : (a<x)) ? z : w < 0 → discard`) and confirmed every
single one of the file's 24 `(function, tier) → keep/discard` expectations in `kCases[]` matches
exactly, with no discrepancies. The GLSL fragment shader (`alpha_test3d.frag.glsl`) implements the
identical encoding, so the test is validated against both the C++ parameter-packing layer and the
actual GPU shader that consumes it.

## Checklist Results

### API / XNA / FNA parity
`setTextureProperty`, `setAlphaProperty`, `setReferenceAlphaProperty`, `setAlphaFunctionProperty` (lines
67-70) all map correctly to FNA's `AlphaTestEffect` public surface (`Texture`/`Alpha`/`ReferenceAlpha`/
`AlphaFunction`). `CompareFunction` enum values used in `kCases[]` are the complete FNA/D3D9 8-value set —
no omissions, no invented values.

### Behavioral correctness
Independently re-derived the discard condition per `AlphaTestEffect.cpp:341-379` and the GLSL shader's
`alpha_test3d.frag.glsl:36-43` (`passTest = (alphaTol>0) ? |alpha-alphaRef|<alphaTol : alpha<alphaRef;
w = passTest ? passW : failW; discard if w<0`):

| Function | x (ref±thresh) | z,w | Below (a=64/255) | At (a=128/255≈ref) | Above (a=192/255) |
|---|---|---|---|---|---|
| Always | n/a | 1,1 | keep | keep | keep |
| Never | n/a | -1,-1 | discard | discard | discard |
| Less | ref-thr | 1,-1 | a<x → keep | a≮x → discard | discard |
| LessEqual | ref+thr | 1,-1 | keep | keep | a≮x → discard |
| Equal | ref,thr | 1,-1 | \|64-128\|≥thr → discard | \|128-128\|<thr → keep | discard |
| NotEqual | ref,thr | -1,1 | not-equal → keep | equal → discard | keep |
| GreaterEqual | ref-thr | -1,1 | a<x → discard | a≥x → keep | keep |
| Greater | ref+thr | -1,1 | discard | a<x → discard | a≥x → keep |

Every row matches `kCases[]` (lines 44-53) exactly: e.g. `Less={true,false,false}`,
`GreaterEqual={false,true,true}`, `NotEqual={true,false,true}`. This is a complete, correct truth table,
not merely internally self-consistent — it was checked against the actual shader arithmetic, which in
turn was checked against `AlphaTestEffect.cpp`'s `FillGpuDrawParams()`, which was checked against FNA's
`OnApply()` (`AlphaTestEffect.cs:331-416`) — all three layers agree.

### Logic
`runOne()` (lines 61-85) correctly re-creates a fresh `AlphaTestEffect` per case (avoiding stale-state
leakage across the 24 sub-tests) and clears to black before each draw, so a "discarded" pixel reliably
reads back as `(0,0,0)` rather than a previous case's leftover color.

### C++ correctness
`got.getRProperty() > 50` (line 84) is a reasonable binary threshold given the two only possible outcomes
are pure black (0) or pure white (255) — no intermediate blending path exists in this scene (no fog, no
partial alpha blend state — `BlendState::Opaque` is set once at line 107, outside the per-case loop, and
is never touched by `runOne()`, so it persists correctly across all 24 draws).

### Robustness
The 3-way alpha sweep (below/at/above) per function is the right technique to distinguish strict (`Less`/
`Greater`) from inclusive (`LessEqual`/`GreaterEqual`) comparisons and to isolate the narrow equality
band (`Equal`/`NotEqual`) — a coarser 2-point sweep could not have told `Less` from `LessEqual` apart.

### Testing
24/24 assertions are genuine discriminating pixel checks, not "compiles and doesn't crash" placeholders.
Coverage is complete for the `CompareFunction` enum's realistic pixel-visible effects; the only untested
dimension is `ReferenceAlpha` itself varying (always held at 128 here) — reasonable, since that's already
covered by the "at" tier's exact-boundary behavior.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings. Two minor observations below.

### F1 — `RasterizerState::CullNone` workaround (line 74) is a documented cross-file characteristic, not a defect in this file
- Severity: INFO
- Confidence: HIGH
- Category: cross-file-consistency
- Location: line 74, comment lines 71-73
- Evidence: `git log` confirms `b6a00bc6`/`bdb69b03` "fix(Task 896): push GraphicsDevice's real default
  RasterizerState to all 3 backends" landed after this file's original authoring (`3cc13137`/`9dcf85f5`,
  Tasks 373-375) — this file was later updated to add the `CullNone` line, consistent with the comment's
  own claim. Not re-verified independently in this batch whether the specific NDC quad winding really is
  back-facing under CNA's true default `RasterizerState.CullCounterClockwiseFace` (that would require a
  runtime A/B toggle of the line, out of this audit's static-analysis scope per D-P4), but the claim is
  corroborated by matching, dated commits rather than a stale/unverifiable assertion.
- Why it matters: purely informational — recorded so a future reviewer doesn't need to re-derive the
  same cross-file corroboration.

### F2 — No test isolates `ReferenceAlpha` itself as a variable (fixed at 128 throughout)
- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location: `kReference = 128` (line 32), used unconditionally by every `runOne()` call
- Evidence: `fx.setReferenceAlphaProperty(kReference)` (line 69) is never varied; only `alpha` (the drawn
  fragment's own alpha) and `func` vary across the 24 cases.
- Why it matters: a regression that hard-coded `AlphaTest.x` to a wrong scale (e.g. treating
  `ReferenceAlpha` as already-normalized 0-1 instead of dividing by 255) would still pass this suite by
  accident if the bug happened to cancel out at exactly 128/255 — a sibling test with a second
  `ReferenceAlpha` value (e.g. 64) would close this gap. Low severity because the reference-alpha-to-
  float conversion (`referenceAlpha_/255.0f`) is simple, shared, and already covered indirectly by other
  files in this shard that use non-default `ReferenceAlpha` values (e.g. `vulkan_alphatest_vertexcolor_test.cpp`'s
  100/180).

## Cross-File Observations

- This file's truth table is architecturally identical to the corresponding EasyGL/Bgfx siblings
  mentioned in its own header comment (`easygl_alphatest_comparefunction_sweep_test.cpp`, "Task 190"),
  and the Vulkan-side GPU encoding (`alpha_test3d.frag.glsl`) uses the exact same `x`/`y`/`z`/`w` packed-
  vec4 convention as the D3D9 HLSL stock effect (`AlphaTestEffect.fx`'s `clip((color.a < AlphaTest.x) ?
  AlphaTest.z : AlphaTest.w)`), confirming the abstraction is faithfully preserved end-to-end from the
  original XNA shader convention into a completely different graphics API.
- `AlphaTestEffect::FillGpuDrawParams()` (shared C++ code) is the single source of truth this test
  ultimately validates for the Vulkan backend; because it is shared with other backends, a regression
  here would likely be caught by the sibling EasyGL/Bgfx tests too — but this file is still the only one
  in the tree that actually proves the Vulkan GLSL shader consumes that shared encoding correctly.

## Missing or Weak Tests

See F2 (no `ReferenceAlpha` variation) — low priority, already substantially covered elsewhere in this
shard.

## Positive Findings

- Complete, correct, independently-verified 8-function × 3-tier truth table — no gaps, no wrong
  expectations found anywhere in `kCases[]`.
- Clean per-case effect reconstruction (fresh `AlphaTestEffect` instance and full black-clear each call)
  eliminates the state-leakage risk that would otherwise undermine the discriminating power of the 24
  sub-assertions.
- Concise, single-purpose file with no dead code, no unexplained magic numbers (every constant is a named
  `kBelow`/`kAt`/`kAbove`/`kReference`), and a clear PASS/FAIL/summary printout matching the format used
  consistently across this shard.

## Final Assessment

A strong, fully-verified conformance test. Its 24-assertion truth table for `CompareFunction` was checked
line-by-line against both the C++ parameter-packing code and the actual GLSL fragment shader, and both
agree with the FNA reference semantics exactly. No behavioral defects found in this file or in the
production code paths it exercises.
