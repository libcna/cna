# Audit: examples/easygl_depthstencilstate_write_enable_test.cpp

## Metadata

- Source file: `examples/easygl_depthstencilstate_write_enable_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard), Task 313
- File type: standalone `Game` subclass / pixel-readback integration test
- Related production code: `DepthStencilState::DepthBufferWriteEnable`, `EasyGLGraphicsBackend::
  ApplyDepthStencilState` (`device.set_depth_mask(depthWriteEnable)`, `EasyGLGraphicsBackend.cpp:1936`)
- FNA reference: `Graphics/States/DepthStencilState.cs:32-42` (`DepthBufferWriteEnable`)
- Build registration: `cmake/Tests/EasyGLTests.cmake:1380-1384` and `cmake/Tests/VulkanTests.cmake:153-158`
- Consumed by: `examples/easygl_depthstencilstate_write_enable_golden_test.cpp` (Task 468), which reuses this file's
  "Check A" scene and its documented rationale rather than duplicating it — see that file's own audit report.

## Purpose

Verifies `DepthStencilState.DepthBufferWriteEnable` actually gates whether a draw's depth values are recorded into
the depth buffer, via a three-quad differential test: a far quad A (depth 0.8, writes), a near quad B (depth 0.2,
write-enable is the variable under test), then a third quad C at an *in-between* depth (0.5) with normal
depth-test-and-write settings — C's final visibility reveals what the buffer actually holds after B.

## Executive Verdict

**Healthy.** The choice of an in-between depth for quad C (rather than reusing A's or B's exact depth) is a
deliberate, well-reasoned design decision documented in the file's own header (lines 14-19) to route around a
separate, already-tracked Vulkan-backend defect (hardcoded per-pipeline depth-compare op) that would otherwise make
the test backend-ambiguous. Verified `EasyGLGraphicsBackend::ApplyDepthStencilState` genuinely forwards
`depthWriteEnable` to a real `set_depth_mask` call (line 1936), so the property under test is real, implemented
behavior on EasyGL.

## Checklist Results

### Purpose
PASS — correctly scoped single-file test for Task 313.

### API / XNA / FNA parity
PASS — `setDepthBufferWriteEnableProperty`/`setDepthBufferEnableProperty`/`setDepthBufferFunctionProperty` map to
FNA's `DepthBufferWriteEnable`/`DepthBufferEnable`/`DepthBufferFunction` exactly (`DepthStencilState.cs:20-42, 92-102`).
`CompareFunction::LessEqual` (line 103) matches FNA's own default (`DepthStencilState.cs:256`,
`DepthBufferFunction = CompareFunction.LessEqual;`), so the test's explicit `writeDisabled` state is a faithful
"same as Default except write-disabled" variant rather than an arbitrary compare function that could introduce a
confound.

### Behavioral correctness
PASS, independently traced:
- Check A (left half, lines 109-115): A(red,0.8,writes)→B(green,0.2,`writeDisabled`)→C(blue,0.5,`Default`). If B's
  write is correctly skipped, the depth buffer still holds A's `0.8`; C's compare `0.5 < 0.8` (via `LessEqual`)
  passes → C visible → centre BLUE. If B's write happens anyway (the bug this test exists to catch), the buffer
  holds `0.2`; C's compare `0.5 < 0.2` is false → C rejected → centre stays GREEN (B's color). The test's own
  in-code comment (lines 138-142) states this explicitly: "A mismatch here means DepthBufferWriteEnable=false did
  not actually skip recording the depth value" — accurately describing what a FAIL would mean.
- Check B (right half, lines 117-121): identical geometry with B's writes left ON (`DepthStencilState::Default`
  reused for all three quads) — this is the **sanity check** proving the depth *compare* itself works and that
  Check A's BLUE outcome is genuinely about the write flag, not a broken/always-passing depth test. If the depth
  test were broken (e.g. always-pass), Check B would show BLUE (C wrongly visible) instead of the expected GREEN,
  correctly failing and signaling that Check A's result cannot be trusted. This differential pairing is correctly
  reasoned and present.

### Logic
PASS — `RasterizerState::CullNone` applied before each quad (line 69, matching a documented Task 896 finding that
this quad's default winding is back-facing under CNA's real default rasterizer state) — correctly prevents an
unrelated culling bug from silently making both checks fail regardless of the depth-write logic.

### C++ correctness
PASS — no lifetime/UB issues; straightforward stack-local color/rectangle values, `GetBackBufferData` calls use
correctly-sized 1x1 regions.

### Robustness
PASS for a test file — the Z-range note (lines 27-33) is a real, substantive piece of institutional knowledge:
XNA/DirectX/this-project's-Vulkan-backend clip-space Z is `[0,+w]`, not OpenGL's native `[-1,+1]`; a negative Z would
be silently clipped on Vulkan but not on EasyGL/OpenGL, which would make the test backend-inconsistent. All Z values
here are kept in `[0,1]`, correctly avoiding that trap.

### Testing
This file is itself a test — see Behavioral correctness. It is also the single source of truth later reused (not
duplicated) by the golden-image variant (Task 468); see that file's own report.

## Detailed Findings

No HIGH, CRITICAL, or MEDIUM findings. The test's differential design (Check A + Check B sanity pairing) correctly
distinguishes "write-disable works" from "the depth test is broken/bypassed," and its claims were verified against
the real `EasyGLGraphicsBackend::ApplyDepthStencilState` implementation.

## Missing or Weak Tests

Does not test `DepthBufferWriteEnable=false` combined with a failing depth *compare* (i.e., confirming the buffer is
untouched even when the draw would have failed the depth test anyway) — a low-priority gap since `DepthBufferWriteEnable`
in real hardware/GL only ever gates *successful* fragments' writes, so this combination is not actually
distinguishable in practice and is a reasonable scope boundary, not an oversight.

## Positive Findings

- The Z-range note is a genuinely valuable, non-obvious piece of cross-backend knowledge captured directly in the
  test file where a future maintainer needs it, rather than left implicit.
- Choosing an in-between depth for quad C (rather than an exact match to A or B) is a subtle, well-justified design
  choice that specifically defeats a known confound from a separate backend defect (Vulkan's hardcoded compare op).

## Final Assessment

A correctly-designed, well-documented differential test with a genuine sanity-check pairing, verified against both
the FNA reference semantics and the real EasyGL implementation. No defects found.
