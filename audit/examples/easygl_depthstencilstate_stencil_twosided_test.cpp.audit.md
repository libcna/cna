# Audit: examples/easygl_depthstencilstate_stencil_twosided_test.cpp

## Metadata

- Source file: `examples/easygl_depthstencilstate_stencil_twosided_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard), Task 318
- File type: standalone `Game` subclass / pixel-readback integration test
- Related production code: `DepthStencilState` (`TwoSidedStencilMode`, `CounterClockwiseStencilFunction/Fail/
  DepthBufferFail/Pass`), `EasyGLGraphicsBackend::ApplyDepthStencilState` (lines 1924-1974, specifically the
  `twoSidedStencilMode` branch at 1946-1965)
- FNA reference: `Graphics/States/DepthStencilState.cs:44-90, 200-210`
- Build registration: `cmake/Tests/EasyGLTests.cmake:1411-1415` and `cmake/Tests/VulkanTests.cmake:192-198`

## Purpose

Verifies `DepthStencilState.TwoSidedStencilMode` actually routes a *separate* stencil function/op set
(`CounterClockwiseStencilFunction`/`Fail`/`DepthBufferFail`/`Pass`) to back-facing triangles instead of reusing the
front-facing settings, via a 2-column test using a genuinely back-facing (reverse-wound) triangle in both columns.

## Executive Verdict

**Healthy.** The test is built from the start as a single genuinely-differential pair (unlike the "critical lesson"
its own comment says Task 317 learned the hard way) — same back-facing geometry, same front/CCW property values in
both columns, only `TwoSidedStencilMode` toggled, with opposite expected final stencil values. Cross-checked against
`EasyGLGraphicsBackend::ApplyDepthStencilState`'s `twoSidedStencilMode` branch, which genuinely calls separate
front/back stencil func/op/mask setters — confirming the property under test is real, implemented behavior.

## Checklist Results

### Purpose
PASS — correctly scoped, single responsibility, Task 318.

### API / XNA / FNA parity
PASS — `setTwoSidedStencilModeProperty`, `setCounterClockwiseStencilFunctionProperty`,
`setCounterClockwiseStencilFailProperty`, `setCounterClockwiseStencilPassProperty`,
`setCounterClockwiseStencilDepthBufferFailProperty` map exactly to FNA's `TwoSidedStencilMode`,
`CounterClockwiseStencilFunction`, `CounterClockwiseStencilFail`, `CounterClockwiseStencilPass`,
`CounterClockwiseStencilDepthBufferFail` (`DepthStencilState.cs:44-90, 200-210`). Correctly reflects that XNA has a
single, shared `ReferenceStencil` (not a separate one per face) — the test's own comment (line 28) explicitly notes
this and both `MakeStampState`/`MakeOpState` use one shared `ReferenceStencil=0x05` for both faces, matching real
XNA semantics rather than inventing a per-face reference value that doesn't exist in the API.

### Behavioral correctness
PASS, independently traced:
- `DrawQuadFront`/`DrawQuadBack` (lines 84-109) use opposite vertex winding for the same on-screen quad — verified
  by comparing the two vertex lists: `DrawQuadFront`'s first triangle is
  `(x0,1)→(x0,-1)→(x1,-1)` while `DrawQuadBack`'s corresponding triangle is `(x1,-1)→(x0,-1)→(x0,1)`, the reverse
  order, i.e. genuinely flipped winding, not just a relabeling.
- `MakeOpState(twoSided)` (lines 124-144): front-face set to `Equal` (passes, `0x05==0x05`) → `Decrement`; CCW set to
  `NotEqual` (fails, since `0x05!=0x05` is false) → `Increment` **on fail**. So:
  - `twoSided=true`: the back-facing triangle uses the CCW settings → stencil test fails → `CounterClockwiseFail`
    (`Increment`) fires → buffer `0x05→0x06`.
  - `twoSided=false`: the back-facing triangle instead uses the front-face settings (since two-sided is off) →
    `Equal` passes → `StencilPass` (`Decrement`) fires → buffer `0x05→0x04`.
  Both outcomes were hand-verified against the `MakeOpState` field assignments; the read-back state (`0x06`, Equal)
  then correctly discriminates `0x06` (column 0, PASS) from `0x04` (column 1, FAIL) — a genuine, unambiguous
  differential pair, not two checks that happen to both expect the same outcome.
- `RasterizerState::CullMode::None` is set globally before drawing (line 185) so the back-facing triangle is
  actually rasterized rather than culled — correctly noted in the file's own header comment (lines 11-13) as
  independent from the stencil-op-selection question being tested.

### Logic
PASS — `MakeStampState()` (lines 111-122) explicitly sets *both* front and CCW stencil function/pass to
`Always`/`Replace` so the initial stamp is winding-independent — correctly avoids the stamp draw itself being
subject to the very front/back distinction under test.

### C++ correctness
PASS — no issues found; straightforward value-type state construction, no pointer/lifetime concerns.

### Architecture
PASS — `DepthBufferEnable=false` throughout (isolating stencil-only behavior, consistent with the sibling
Task-316/317 tests in this shard).

### Testing
This file is itself a test; internal validity was checked directly (see Behavioral correctness).

### Cross-file consistency
Consistent with the shard's established pattern (`MakeStampState`/`MakeOpState`/`MakeReadBackState` helper-function
naming and one-shared-`ReferenceStencil` design recur identically across Tasks 316/317/318). The Vulkan-specific
caveat in this file's header comment (lines 43-51: Vulkan discards two-sided stencil settings entirely and never
enables the stencil test, so "column A" passes "purely by coincidence" while "column B" — which requires a genuine
reject — fails) is internally consistent with this file's own 2-column pass/fail expectation table
(`expectGreen = {true, false}`, line 237) and does not exhibit the same cross-file prediction mismatch found in the
sibling stencil-ops test (see that file's F1) — checked `VulkanTests.cmake:192-198`'s comment ("expected to FAIL the
contrast check per Task 870") and confirmed it agrees with this file's own prediction (column B/the contrast check
fails).

## Detailed Findings

No HIGH, CRITICAL, or MEDIUM findings. The test is correctly designed, its arithmetic was independently verified by
hand, and its claims about the EasyGL backend's actual two-sided-stencil implementation were cross-checked against
`EasyGLGraphicsBackend::ApplyDepthStencilState`'s real `twoSidedStencilMode` branch.

## Missing or Weak Tests

Only one operation slot per face is exercised per column (`StencilPass`/`CounterClockwiseStencilFail`); the sibling
`StencilFail`/`StencilDepthBufferFail` and `CounterClockwiseStencilPass`/`CounterClockwiseStencilDepthBufferFail`
combinations under `TwoSidedStencilMode=true` are not covered by any test in this shard. This is a legitimate
(if low-priority) coverage gap — the current test proves two-sided routing exists for one operation pairing, not
that every combination is independently correct.

## Positive Findings

- Correctly reflects XNA's actual single-shared-`ReferenceStencil` model rather than inventing a per-face value.
- Winding reversal between `DrawQuadFront`/`DrawQuadBack` was verified vertex-by-vertex to be a genuine flip, not a
  cosmetic rename.
- Built from the start as one differential pair (per its own "critical lesson" comment), avoiding the
  same-outcome-only blind spot found and fixed in the sibling Task 317 test.

## Final Assessment

A correct, well-reasoned differential test for `TwoSidedStencilMode`, verified against both the FNA API reference
and the real EasyGL backend implementation. No defects found; only a modest, low-priority coverage gap around the
untested operation-slot combinations under two-sided mode.
