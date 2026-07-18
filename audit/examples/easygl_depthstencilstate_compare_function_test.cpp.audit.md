# Audit: examples/easygl_depthstencilstate_compare_function_test.cpp

## Metadata

- Source file: `examples/easygl_depthstencilstate_compare_function_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard)
- File type: C++ example/integration test, registered as CTest
  `EasyGL_DepthStencilState_CompareFunction`
  (`cmake/Tests/EasyGLTests.cmake:1387-1389`, target
  `cna_test_easygl_depthstencilstate_compare_function`)
- Related production code: `EasyGLGraphicsBackend::ApplyDepthStencilState`
  (`EasyGLGraphicsBackend.cpp:1924-1974`), `ToEasyGLCompareFunc` (lines 1839-1852),
  `CompareFunction` enum (`include/Microsoft/Xna/Framework/Graphics/CompareFunction.hpp`)
- XNA/FNA relevance: `DepthStencilState.DepthBufferFunction`, `CompareFunction` enum (8 XNA values:
  `Always`, `Never`, `Less`, `LessEqual`, `Equal`, `GreaterEqual`, `Greater`, `NotEqual`)
- Main related tests: `examples/bgfx_depthstencilstate_compare_function_test.cpp` — a later, explicitly
  more complete adaptation of this exact file (see Detailed Findings, F1)

## Purpose

`DepthStencilStateCompareFunctionTest` (Task 314) verifies that `DepthStencilState.
DepthBufferFunction` genuinely changes depth-test pass/fail outcomes, not just that setting it doesn't
crash. It draws 5 side-by-side columns, each with a red reference quad A at depth 0.5 (writing depth),
then a green quad B at a function-specific depth chosen to give an unambiguous, direction-sensitive
expected result for `Always`/`Never`/`Less`/`LessEqual`/`Greater`.

## Executive Verdict

**Needs attention on test coverage completeness** — the 5 `CompareFunction` values this file actually
tests are each correctly and rigorously verified (confirmed against the real GL mapping in
`ApplyDepthStencilState`), but the file only covers 5 of `CompareFunction`'s 8 real XNA values,
omitting `Equal`, `GreaterEqual`, and `NotEqual` entirely — a gap this audit confirmed is real by
cross-referencing the sibling `bgfx_depthstencilstate_compare_function_test.cpp`, whose own header
comment explicitly states this exact file "only covers 5 of the 8 real XNA CompareFunction values."

## Checklist Results

### API / XNA / FNA parity
`CompareFunction` (`CompareFunction.hpp`) declares 8 values: `Always`, `Never`, `Less`, `LessEqual`,
`Equal`, `GreaterEqual`, `Greater`, `NotEqual` — confirmed by reading the header directly. This test's
`checks[5]` array (lines 110-116) covers `Always`, `Never`, `Less`, `LessEqual`, `Greater` only. See
Finding F1.

### Behavioral correctness
For the 5 values it does cover, the test's expected outcomes are correctly derived and confirmed
against the actual EasyGL implementation:
- `ApplyDepthStencilState` (`EasyGLGraphicsBackend.cpp:1935-1938`): `device.set_depth_test_enabled
  (depthEnable); ... if (depthEnable) device.set_depth_func(ToEasyGLCompareFunc(depthFunc));` — the
  real depth function is genuinely forwarded per-draw, not hardcoded, confirming this test's premise
  that `DepthBufferFunction` is a live, backend-respected setting on EasyGL (unlike the header comment's
  own note, lines 24-29, that this exact scenario is a **known, tracked failure on Vulkan**, where
  `VulkanGraphicsBackend::ApplyDepthStencilState` hardcodes `depthCompareOp` per pipeline regardless of
  what's requested — correctly disclosed as pre-existing and out of scope for this EasyGL-specific file).
- `ToEasyGLCompareFunc` (lines 1839-1852) maps `CompareFunction` ordinals to `::easygl::CompareFunc`
  correctly for all 8 values (`Less→Less`, `LessEqual→Lequal`, `Greater→Greater`, `Always→Always`
  (`default` case, ordinal 0), `Never→Never`) — the mapping itself is complete even though this test
  doesn't exercise all of it.
- Check-by-check: `Always` at `B.depth=0.9` (worse than A's 0.5) still passes (Always ignores the
  compare) → GREEN, correct. `Never` at `B.depth=0.1` (better than A) still fails → RED, correct.
  `Less` at `0.3 < 0.5` → passes → GREEN, correct. `LessEqual` at `0.5 == 0.5` → passes (inclusive) →
  GREEN, correctly distinguishing inclusive-equality from strict `Less`. `Greater` at `0.7 > 0.5` →
  passes → GREEN, correctly using the opposite depth direction from `Less` to catch a
  `Less`/`Greater` mapping swap (e.g. if `ToEasyGLCompareFunc`'s switch cases 2/6 were transposed).

### Logic
`DrawQuad()` (lines 54-68) explicitly sets `RasterizerState::CullNone` (line 66) with a comment
attributing this necessity to a prior finding ("Task 896 finding: this quad's winding is CCW/back-facing
under CNA's real default RasterizerState") — a concretely-cited, traceable fix for a real winding issue
rather than an unexplained workaround. `MakeCompareState()` (lines 70-77) correctly sets both
`DepthBufferEnable=true` and `DepthBufferWriteEnable=true` alongside the function under test — a
disabled/write-off depth state would make the whole per-column comparison meaningless, so this is the
right minimal state.

### Memory/resource lifetime
No dynamically-owned GPU resources beyond `BasicEffect fx(dev)` (stack-local, line 99); no lifetime
concerns.

### C++ correctness
No unusual casts; the `Check` aggregate (line 109) and `checks[5]` array are straightforward, correctly
initialized designated-style aggregates.

### Performance / Thread safety
N/A — single-frame, 5-column test.

### Architecture
Correct XNA API surface only (`DepthStencilState`, `CompareFunction`, `BasicEffect`,
`GraphicsDevice::DrawUserPrimitives`).

### Maintainability
Header comment (lines 1-31) is thorough, including an explicit, dated cross-reference to the known
Vulkan `DepthBufferFunction`-ignoring bug (Task 870/313) and an explicit instruction not to reintroduce
negative Z values (with a pointer to a sibling test file's fuller explanation) — good practice for
avoiding regressions from future edits.

### Portability
No platform-specific code in the test itself; the Vulkan caveat documented in the header is specific to
that other backend, correctly scoped as informational here.

### Robustness
N/A (test file); prints a per-check `[PASS]`/`[FAIL]` line (lines 149-153) plus a final `passCount==5`
gate.

### Testing
**This is the central finding of this report.** `CompareFunction` has 8 values; this file tests 5.
Confirmed via the sibling `bgfx_depthstencilstate_compare_function_test.cpp`'s own header comment
(quoted verbatim: "that file only covers 5 of the 8 real XNA CompareFunction values (missing
Equal/GreaterEqual/NotEqual, this row's own ask is 'all 8')") that this specific gap was already
identified by a later task (Task 760, Phase 72 Bgfx gap closure) and fixed for Bgfx, but this EasyGL
file was never brought up to the same 8-value standard. Per `CLAUDE.md`'s testing rules ("every method
overload must be covered... enum/switch completeness" per `AUDIT_CHECKLIST.md` §13) and the project's
own established precedent (the Bgfx sibling explicitly closing this exact gap for its own backend),
this is a genuine, actionable coverage gap for the EasyGL backend specifically.

### Cross-file consistency
The Bgfx sibling test is a near-total superset of this file's methodology (same "reference quad A +
comparison quad B, unambiguous per-function expected depth" technique) extended to all 8 values and
restructured for a Bgfx-specific readback constraint — a clean template this EasyGL file could adopt
directly if extended.

## Detailed Findings

### F1 — Only 5 of 8 `CompareFunction` values are tested; `Equal`/`GreaterEqual`/`NotEqual` have zero
EasyGL coverage

- Severity: MEDIUM
- Confidence: HIGH
- Category: test-coverage completeness
- Location/symbol: `checks[5]` array, lines 110-116; `CompareFunction` enum,
  `include/Microsoft/Xna/Framework/Graphics/CompareFunction.hpp` (8 values: `Always`, `Never`, `Less`,
  `LessEqual`, `Equal`, `GreaterEqual`, `Greater`, `NotEqual`)
- Evidence: direct enumeration of `checks[5]`'s 5 entries (`Always`, `Never`, `Less`, `LessEqual`,
  `Greater`) against the enum's 8 declared values leaves `Equal`, `GreaterEqual`, `NotEqual` completely
  unexercised by this file — and this is independently corroborated by the sibling
  `bgfx_depthstencilstate_compare_function_test.cpp`'s own header comment explicitly stating this exact
  gap and that closing it was that later task's specific goal, for Bgfx only.
- Why it matters: `ToEasyGLCompareFunc` (`EasyGLGraphicsBackend.cpp:1839-1852`) does implement mappings
  for `Equal`(4)→`Equal`, `GreaterEqual`(5)→`Gequal`, `NotEqual`(7)→`Notequal` — but none of those three
  switch cases are exercised by any test in this shard as far as this file's own name/scope would
  suggest coverage for. A transposition bug in any of those three specific `case` labels (e.g. swapping
  `Gequal`/`Notequal`) would currently go undetected by this file, and this audit found no other EasyGL
  test in the shard that covers them (the `easygl_depthstencilstate_stencil_enable_test.cpp` file in
  this same batch tests `StencilFunction::Equal`/`Always` for the *stencil* compare, not the *depth*
  compare — a different code path, `ToEasyGLCompareFunc` is shared by both but only through different
  call sites with different arguments).
- FNA/XNA comparison: `CompareFunction` is a real, full 8-value XNA enum
  (`Microsoft.Xna.Framework.Graphics.CompareFunction`); all 8 values are equally part of the public API
  surface this project commits to supporting.
- Related files: `examples/bgfx_depthstencilstate_compare_function_test.cpp` (the already-fixed sibling
  for a different backend); `EasyGLGraphicsBackend.cpp:1839-1852` (`ToEasyGLCompareFunc`, the mapping
  function whose 3 untested branches this finding concerns).
- Suggested action (not implemented by this audit): extend this file's `checks` array to all 8 values
  following the Bgfx sibling's already-established per-function expected-depth design (`Equal`/
  `GreaterEqual` at `B.depth==0.5`, `NotEqual` at `B.depth!=0.5`), the same way Task 760 did for Bgfx.

## Cross-File Observations

- This exact gap (5/8 `CompareFunction` values) appears to be backend-specific technical debt: it was
  identified and fixed for Bgfx (Task 760) but the EasyGL file that presumably motivated noticing the
  gap (this one, an earlier task, 314) was never itself updated to match. Worth checking whether other
  backends' `depthstencilstate_compare_function_test.cpp` variants (if any exist for SdlGpu, D3D11,
  D3D12, WebGPU, etc.) have the same gap, as a cross-cutting finding for
  `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

- `Equal`, `GreaterEqual`, `NotEqual` `CompareFunction` values — zero EasyGL coverage (F1).
- No test of `DepthBufferWriteEnable=false` interacting with `DepthBufferFunction` (a write-disabled
  depth test can still gate visibility without updating the depth buffer — a legitimately different,
  untested state combination), though this is arguably a separate concern from this file's stated scope.

## Positive Findings

- The 5 values that *are* tested are rigorously, correctly verified with real discriminating power
  (e.g., `LessEqual` specifically distinguishes inclusive-equality from strict `Less`; `Greater` uses
  the opposite depth direction from `Less` to catch a direction-swap bug) — the quality of what's
  covered is high, only the breadth is short of complete.
- Explicit, well-cited disclosure of the known Vulkan `DepthBufferFunction`-ignoring bug, correctly
  scoped as "not a new finding, a reconfirmation," avoiding false-alarm duplicate bug reports.
- Correctly-cited fix for a real quad-winding issue (Task 896) rather than an unexplained `CullNone`.

## Final Assessment

A rigorously correct test for the 5 `CompareFunction` values it covers, but with a real, independently
corroborated coverage gap (3 of 8 enum values untested) that a sibling backend's test suite has already
identified and fixed for itself — this EasyGL file should be brought up to the same 8-value standard.
