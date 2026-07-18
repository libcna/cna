# Audit: examples/easygl_clear_overloads_test.cpp

## Metadata

- Source file: `examples/easygl_clear_overloads_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard)
- File type: C++ example/integration test, registered as CTest `EasyGL_ClearOverloads`
  (`cmake/Tests/EasyGLTests.cmake:935-937`, target `cna_test_easygl_clear_overloads`)
- Related production code: `GraphicsDevice::Clear` (4 overloads,
  `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp:264-370`),
  `System::ArgumentOutOfRangeException`
- XNA/FNA relevance: `GraphicsDevice.Clear` (`Graphics/GraphicsDevice.cs:791-841` in FNA)
- Main related tests: none named for `GraphicsDevice::Clear` elsewhere in this batch; this appears to
  be the primary dedicated test for the 4 `Clear` overloads (single-`Color`, `(options,color,depth,
  stencil)`, `(r,g,b,a)` float — not exercised here — and `(color,depth)`).

## Purpose

`ClearOverloadsTest` (Task 207) is a 9-check integration test verifying `GraphicsDevice::Clear`'s
overload set behaves per XNA semantics: single-`Color` clears target+depth+stencil together,
`ClearOptions`-flagged clears only touch what's requested, `Clear(ClearOptions::DepthBuffer, …)` never
changes the color target, and the `depth` argument's `[0,1]` validation is gated specifically on the
`DepthBuffer` flag being present (not unconditional).

## Executive Verdict

**Healthy.** All 9 checks are independently verifiable against the actual `GraphicsDevice::Clear`
implementation and, for the parts that have an FNA equivalent, against the FNA source directly — every
check's expected behavior matches the real code path it exercises. One noteworthy cross-file
observation: the depth-range validation this test exercises (checks 6-8) is a CNA-only addition with no
FNA precedent, undocumented as an intentional deviation at its point of definition — not a defect in
this test file itself, but worth flagging for the file that actually implements it.

## Checklist Results

### API / XNA / FNA parity
Exercises `Clear(Color)`, `Clear(ClearOptions, Color, float, int)`, and `Clear(Color, float)` — all
three are real XNA `GraphicsDevice.Clear` overloads. Confirmed FNA's single-`Color` overload
(`GraphicsDevice.cs:791-799`):
```csharp
public void Clear(Color color) {
    Clear(ClearOptions.Target | ClearOptions.DepthBuffer | ClearOptions.Stencil,
          color.ToVector4(), Viewport.MaxDepth, 0);
}
```
matches CNA's `GraphicsDevice::Clear(const Color&)` (`GraphicsDevice.cpp:264-274`) exactly, including
using the *current viewport's* `MaxDepth` rather than a hardcoded `1.0f` — a subtlety this test's Check
1 doesn't specifically stress (default viewport `MaxDepth` is `1.0`), but the underlying implementation
is correct regardless.

### Behavioral correctness
Verified each of the 9 checks against `GraphicsDevice::Clear(ClearOptions, const Color&, float, int)`
(`GraphicsDevice.cpp:284-346`):
- **Checks 1-5** (color-changing overloads/flag combinations): the implementation dispatches to
  `ClearColorDepthAndStencil`/`ClearColorAndDepth`/etc. based on which of `clearTarget`/`clearDepth`/
  `clearStencil` are set (lines 330-346) — Check 3 (`DepthBuffer`-only leaves color unchanged) is
  correctly predicted: with only `clearDepth=true`, the color-touching branches are never taken.
- **Checks 6-7** (`ArgumentOutOfRangeException` for `depth` outside `[0,1]` with `DepthBuffer` set):
  matches `GraphicsDevice.cpp:291-297` exactly — `if (hasClearFlag(options, ClearOptions::DepthBuffer))
  { if (depth < 0.0f || depth > 1.0f) throw ArgumentOutOfRangeException(...); }`.
- **Check 8** (`Target`-only with `depth=-99` does NOT throw): correctly predicts that the validation
  block above is entered only `if (hasClearFlag(options, ClearOptions::DepthBuffer))` — with
  `ClearOptions::Target` alone, that guard is false, so the exception path is skipped entirely,
  regardless of how invalid `depth` is. This is a well-chosen test: it specifically proves the guard is
  flag-gated, not "the depth argument is always range-checked."
- **Check 9** (`Clear(Color, float)` convenience overload): matches
  `Clear(ClearOptions::Target | ClearOptions::DepthBuffer, color, depth, 0)`
  (`GraphicsDevice.cpp:367-370`) — correctly expects the color to change (Target flag is present).

### Logic
`colorNear()` (lines 39-44) checks R/G/B only with ±2 tolerance, deliberately excluding alpha — a
reasonable choice since `GraphicsDevice::Clear`'s primary observable is a fully-opaque backbuffer where
alpha readback behavior is backend/format-dependent and not the subject of this test. `throwsAOOR()`
(lines 60-65) uses a template lambda-invoker pattern that correctly distinguishes the target exception
type from any other (returns `false` for a non-matching `catch (...)`), so Check 6/7 cannot pass via an
unrelated exception being thrown instead.

### Memory/resource lifetime
No owned GPU resources beyond the implicit backbuffer; `gdm_` is a `unique_ptr`, standard pattern.

### C++ correctness
`readCenter()` (lines 29-36) and the inline Check 8 block correctly use `std::abs` via `<cmath>`
(included, line 23) — no missing-include risk unlike a sibling file flagged elsewhere in the broader
suite (`easygl_dualtexture_test.cpp`'s F2, per a neighboring report). `throwsAOOR`'s templated
`F&& fn` correctly forwards a `[&]` capturing lambda by universal reference.

### Performance / Thread safety
N/A — single-frame test with 9 small sequential clears.

### Architecture
Test stays entirely on the public XNA API surface (`GraphicsDevice`, `ClearOptions`, `Color`,
`System::ArgumentOutOfRangeException`) — no backend-internal symbols.

### Maintainability
Very clearly organized: each of the 9 checks is delimited with a `// ── N. …` banner comment matching
the header's own numbered list (lines 4-13) — a genuinely useful 1:1 map from comment to assertion,
easy to audit and easy to extend.

### Portability
No platform-specific code.

### Robustness
`check()` (lines 53-57) accumulates pass/fail counts and prints per-check `[PASS]`/`[FAIL]` lines plus
a final summary rather than aborting at the first failure — lets a single regression be pinpointed
without re-running with different flags, a good pattern replicated consistently across this whole
integration-test suite.

### Testing
This file *is* the test for `GraphicsDevice::Clear`. Coverage is strong for the overloads and flag
combinations it targets. Not tested: `Clear(float r, float g, float b, float a)` (the raw-float-channel
overload, `GraphicsDevice.cpp:276-282`) is present in the production code but has no corresponding
check here — a real, if minor, coverage gap for one of the 4 documented overloads. Also not tested:
`ClearOptions::Stencil`-only (isolated, without `Target`/`DepthBuffer`) — Check 5 combines all three
flags, but no check isolates stencil-only clearing the way Check 3 isolates depth-only.

### Cross-file consistency
Consistent with FNA's `GraphicsDevice.Clear` overload set and semantics (see API/FNA parity above).
One cross-file note: `GraphicsDevice.cpp:291-297`'s depth-range validation that Checks 6-8 depend on has
**no FNA equivalent at all** — grepping FNA's `GraphicsDevice.cs` `Clear` methods shows no
`ArgumentOutOfRangeException` check on `depth` anywhere; FNA forwards `depth` straight to
`FNA3D_Clear` unconditionally. This makes the behavior under test in Checks 6-8 a CNA-only addition,
not an FNA-parity requirement — the test itself is internally consistent and correctly verifies the
current CNA implementation, but the implementation being tested (`GraphicsDevice.cpp`) does not carry a
`//` comment documenting this as an intentional deviation from FNA, which `CLAUDE.md`'s own "Behavior
Fidelity"/"Porting Workflow" rules ask for on every intentional deviation. This is a finding for
`GraphicsDevice.cpp`'s own audit (outside this shard), surfaced here since this test is where the
behavior is actually exercised.

## Detailed Findings

No HIGH/MEDIUM findings in this file itself.

### F1 — `Clear(float r, float g, float b, float a)` overload is untested

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `GraphicsDevice::Clear(float, float, float, float)` (`GraphicsDevice.cpp:276-282`)
  vs. this file's 9 checks (none construct or call this overload)
- Evidence: grepped this file for any 4-`float` `Clear` call — none found; the file's own header
  comment (lines 4-13) lists 9 verified behaviors, none referencing the raw-float overload.
- Why it matters: per `CLAUDE.md`'s testing rules, "every method overload must be covered by at least
  one test case" — this 4th `Clear` overload has zero coverage in what otherwise reads as the
  dedicated `Clear`-overloads test file.
- Suggested action (not implemented by this audit): add a 10th check calling
  `dev.Clear(r, g, b, a)` directly and confirming the resulting backbuffer color, mirroring Check 1's
  own `colorNear` pattern.

## Cross-File Observations

- See "Cross-file consistency" above: the `ArgumentOutOfRangeException` depth-range validation this
  test's Checks 6-8 depend on is a CNA-only behavior not present in FNA at all — worth flagging in
  `GraphicsDevice.cpp`'s own audit report as an undocumented intentional deviation (not a defect in
  behavior, since the validation is reasonable and self-consistent, just unflagged per the project's
  own documentation convention).

## Missing or Weak Tests

- See F1 (missing `Clear(r,g,b,a)` coverage).
- No isolated `ClearOptions::Stencil`-only check (only tested in combination with `Target`+`DepthBuffer`
  in Check 5).

## Positive Findings

- Excellent, tightly-scoped negative test (Check 8) that specifically proves the depth-range guard is
  flag-gated rather than unconditional — the kind of check that actually distinguishes correct gating
  logic from a naive "always validate" implementation.
- Clear 1:1 mapping between the header comment's numbered behavior list and the in-code check banners.
- Correctly excludes alpha from `colorNear`'s comparison rather than asserting on a channel this test
  isn't actually trying to control.

## Final Assessment

A well-organized, behaviorally-accurate test for `GraphicsDevice::Clear`'s flag-gating and
depth-validation semantics, correctly verified against both the CNA implementation and FNA's reference
source; its only real gaps are one untested overload (F1) and no isolated stencil-only check, both
minor coverage gaps rather than incorrect assertions.
