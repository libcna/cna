# Audit: examples/easygl_depthstencilstate_stencil_enable_test.cpp

## Metadata

- Source file: `examples/easygl_depthstencilstate_stencil_enable_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard)
- File type: C++ example/integration test, registered as CTest
  `EasyGL_DepthStencilState_StencilEnable`
  (`cmake/Tests/EasyGLTests.cmake:1393-1395`, target
  `cna_test_easygl_depthstencilstate_stencil_enable`)
- Related production code: `EasyGLGraphicsBackend::ApplyDepthStencilState`
  (`EasyGLGraphicsBackend.cpp:1924-1974`), `EasyGLGraphicsBackend` constructor's
  `SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8)` (line 1302)
- XNA/FNA relevance: `DepthStencilState.StencilEnable`, `StencilFunction`, `StencilOperation`,
  `ReferenceStencil`
- Main related tests: `easygl_depth_format_test.cpp` (this batch) — that file's own header comment
  explicitly defers real `DepthFormat`-behavioral verification to this file

## Purpose

`DepthStencilStateStencilEnableTest` (Task 315) verifies that `DepthStencilState.StencilEnable`
genuinely gates the stencil test, not just that it's a settable field. Using a two-column differential
method (stamp stencil values with real draws, then test with `StencilFunction::Equal`), it checks that
`StencilEnable=true` produces a spatially-differentiated result (left half passes, right half doesn't)
while `StencilEnable=false` makes the stencil test a no-op (both halves pass) — matching real XNA/FNA
semantics for a disabled stencil test.

## Executive Verdict

**Healthy.** The test correctly identifies and works around two real, separately-documented
prerequisites (a `Depth24Stencil8` surface format requirement, and `GraphicsDevice::Clear`'s inability
to actually clear the stencil aspect) rather than silently assuming they work, and its "stamp with a
real draw, not Clear()" methodology is independently confirmed correct by this audit against both the
production `Clear()` implementation and the EasyGL `ApplyDepthStencilState` mapping.

## Checklist Results

### API / XNA / FNA parity
`setDepthBufferEnableProperty`, `setStencilEnableProperty`, `setStencilFunctionProperty`,
`setStencilPassProperty`/`setStencilFailProperty`, `setReferenceStencilProperty` are all correct
XNA-style `DepthStencilState` property setters. `StencilOperation::Replace`/`Keep` and
`CompareFunction::Always`/`Equal` are correct XNA enum members used in their standard sense (a
"stamp" pass with `Always`+`Replace` unconditionally writes the reference value, matching common
stencil-masking technique used in real XNA/DirectX content).

### Behavioral correctness
Verified the constructor's explicit `DepthFormat::Depth24Stencil8` request (lines 200-208) against the
EasyGL backend's own stencil-bit requirement: `SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8)`
(`EasyGLGraphicsBackend.cpp:1302`, with an explicit comment: "Without this, no window ever gets stencil
bits... making DepthStencilState.StencilEnable a permanent no-op regardless of what's requested") — this
confirms the test's own header-comment claim (lines 6-9) that requesting a real stencil-capable format
is not optional boilerplate but a genuine prerequisite for this test to be meaningful at all.

Confirmed `ApplyDepthStencilState`'s stencil path (`EasyGLGraphicsBackend.cpp:1940-1973`):
`device.set_stencil_test_enabled(stencilEnable)` is called unconditionally with the real
`stencilEnable` value — this is the actual gate this test is checking, and it genuinely is a real,
backend-respected boolean (not hardcoded true/false), confirming the test's premise is sound for
EasyGL specifically (the header's own note, lines 34-40, correctly discloses this same property is
**not** functional on Vulkan, a separate, already-tracked bug, not conflated with this file's own
EasyGL-specific claim).

Verified the "don't use `Clear()` to establish the stencil baseline" design choice (header lines 14-18)
against the actual `GraphicsDevice::Clear` implementation: confirmed (during this audit's review of the
sibling `easygl_clear_overloads_test.cpp`) that `GraphicsDevice::Clear`'s dispatch logic
(`GraphicsDevice.cpp:330-346`) only ever calls `ClearColorDepthAndStencil`/`ClearColorAndDepth`/etc.
based on `ClearOptions` flags — `ClearOptions::Stencil` handling was added later (Task 871, per that
file's own comment) but this test was written to not depend on it regardless, using an explicit
`Always`/`Replace` full-screen "stamp" draw instead — a robust, not-overfit-to-implementation-detail
design that would keep working even if `Clear`'s stencil handling regressed.

Traced the differential logic by hand:
1. Full-screen stamp with `ReferenceStencil=0` → stencil=0 everywhere.
2. Per-column left-half stamp with `ReferenceStencil=1` → stencil=1 on left, still 0 on right.
3. Full-column green draw with `StencilFunction::Equal, ReferenceStencil=1`:
   - Column A (`StencilEnable=true`): left (stencil==1) matches → passes → GREEN; right (stencil==0)
     doesn't match → fails → stays BACKGROUND. Matches `IsGreen(aLeft) && IsBackground(aRight)` (line
     177).
   - Column B (`StencilEnable=false`): stencil test is skipped entirely per XNA/FNA semantics (a
     disabled stencil test always passes) → both halves GREEN. Matches `IsGreen(bLeft) &&
     IsGreen(bRight)` (line 178).
This is a correct, real differential test — not a same-expected-result-either-way test that would pass
coincidentally regardless of whether stencil gating actually works.

### Logic
`MakeStampState()`/`MakeTestState()` (lines 88-109) correctly disable depth testing
(`setDepthBufferEnableProperty(false)`) throughout, explicitly to isolate stencil behavior from the
already-tracked `DepthBufferFunction` bug (header lines 20-21) — good test isolation discipline,
avoiding a compound failure that would be ambiguous to diagnose.

### Memory/resource lifetime
No dynamically-owned GPU resources beyond `gdm_` (`unique_ptr`); stack-local `BasicEffect`.

### C++ correctness
`DrawQuad()` (lines 72-86) explicitly sets `RasterizerState::CullNone` with the same "Task 896 finding"
attribution as the sibling `CompareFunction` test in this batch — consistent, correctly-cited fix,
not a duplicated unexplained workaround.

### Performance / Thread safety
N/A — single-frame test.

### Architecture
Correct XNA API surface only.

### Maintainability
Header comment (lines 1-42) is exceptionally thorough: explains the `Depth24Stencil8` prerequisite, the
reason `Clear()` can't be used for the stencil baseline (with a forward-reference to the newly-tracked
Task 871), the depth-isolation rationale, and the already-known Vulkan limitation with an explicit
warning not to misread a coincidental pass there as evidence stencil testing works — a genuinely
well-engineered piece of test documentation.

### Portability
No platform-specific code in the test; the `SDL_GL_STENCIL_SIZE` requirement is correctly handled
inside the EasyGL backend itself, not duplicated here.

### Robustness
`Draw()` prints an `[INFO]` diagnostic hint (lines 189-193) specifically when Check A fails, pointing
the reader at the most likely root cause ("the stencil test is not actually gating fragments") — a
nice touch for CI triage that goes beyond the suite's usual `[PASS]`/`[FAIL]` banner pattern.

### Testing
This is the dedicated, and only, test in this shard for `StencilEnable`'s gating behavior. Coverage is
narrow but appropriate to its stated goal: it does not test other `StencilFunction` values
(`NotEqual`, `Greater`, etc. — those are depth-*compare*-function values tested separately, and this
file specifically targets the enable/disable boolean, not the full stencil comparison space), nor
`twoSidedStencilMode`, nor `StencilMask`/`StencilWriteMask` masking — all reasonably out of scope for a
file whose stated purpose is exactly "does the boolean gate the test."

### Cross-file consistency
Consistent with `easygl_depth_format_test.cpp`'s own disclosed scope boundary (that file explicitly
defers real behavioral `DepthFormat` verification to this one) and with the sibling
`easygl_depthstencilstate_compare_function_test.cpp`'s shared `CullNone`/Task-896 fix and shared
Vulkan-limitation disclosure style.

## Detailed Findings

No HIGH/MEDIUM/LOW findings — this file is a well-designed, correctly-verified test with no gaps this
audit could substantiate beyond the intentionally narrow (and clearly stated) scope described above.

## Cross-File Observations

- Forms a matched pair with `easygl_depth_format_test.cpp`: that file tests the field/round-trip
  contract for `DepthFormat`, this file is the one that actually proves a `DepthFormat` choice
  (`Depth24Stencil8`) has a real, observable behavioral consequence — read together, they fully cover
  what a single "DepthFormat" test might otherwise be expected to cover alone.
- The `GraphicsDevice::Clear` stencil-ignoring issue this file works around (Task 871) was independently
  confirmed by this audit while reviewing the sibling `easygl_clear_overloads_test.cpp` — worth noting
  in a cross-cutting summary that both files independently corroborate the same underlying fact about
  `Clear()`'s stencil handling from two different angles.

## Missing or Weak Tests

- No test of `twoSidedStencilMode`/CCW-specific stencil function differing from the CW one (a
  legitimately separate feature from plain `StencilEnable` gating, reasonable to leave to a dedicated
  future test rather than folding into this file's scope).

## Positive Findings

- Rigorous, genuinely differential test design (not a same-result-either-way false positive risk).
- Explicitly identifies and works around two separate real production gaps (the `Depth24Stencil8`
  requirement, and `Clear()`'s stencil-ignoring bug) rather than silently assuming they're non-issues.
- Exceptionally clear, complete header documentation, including a specific warning against
  misinterpreting a coincidental Vulkan pass.
- Helpful `[INFO]` triage hint on failure, beyond the suite's usual pass/fail banner.

## Final Assessment

A rigorously designed and correctly verified test for `DepthStencilState.StencilEnable`'s real gating
behavior on EasyGL, with unusually thorough documentation of its own prerequisites and known
cross-backend limitations; no defects found in this file.
