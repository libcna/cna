# Audit: tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp` (383 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests the SDL→XNA mouse conversion inside `SdlInputBridge::ProcessEvent`
  (backs `Microsoft::Xna::Framework::Input::Mouse`), Task 804 + several P3/P8/N-005 items
- Main related tests: cross-references `MouseInputTests.cpp` (not in this shard; covers the
  `InputManager`-level API directly rather than through the SDL bridge)

## Purpose
Tests the SDL mouse-button-index mapping (1-based SDL → 0-based XNA `ClickedEXT`), full five-button
Pressed/Released state transitions through the real bridge, absolute/relative motion including a
DPI/letterboxed-renderer coordinate-space conversion, and vertical + horizontal (NOXNA/EXT) wheel
accumulation.

## Executive Verdict
Correct and thorough, with one standout test: `FractionalSubNotchIsTruncatedBeforeScaling`
explicitly documents and verifies the cast-before-multiply ordering
(`(int)evt.wheel.y * 120`, not `(int)(evt.wheel.y * 120)`) is the one that matches real FNA/XNA
"whole notches only" semantics — its own comment correctly predicts what a naive
multiply-then-cast implementation would wrongly produce (60/108/-60 instead of 0/0/120), which is
exactly the kind of precise, falsifiable prediction that catches a subtle off-by-scaling-order bug.

## Checklist Results
- `ButtonDownFiresClickedEXTWithZeroBasedIndex` correctly covers all five SDL buttons
  (Left/Middle/Right/X1/X2) through the real 1-based→0-based conversion, not just Left.
- `AllFiveButtonsTransitionThroughBridge` uses a member-function-pointer table to iterate all five
  button getters and, for each button pressed, explicitly asserts every OTHER button stays
  Released — real cross-button independence verification, not just "this one button works."
- `UnknownSdlButtonIsIgnoredSafely` (P3-004) tests SDL button values SDL itself never emits (0, 6,
  99, 255) and confirms none of the five XNA buttons are spuriously affected while position IS still
  applied — correctly distinguishing "button index parsing" from "position parsing," two genuinely
  separate fields on the same event.
- `MotionEventConvertsWindowCoordinatesToLogicalForLetterboxedRenderer` (P3-039) is a real,
  non-trivial integration test: it creates an actual hidden SDL window+renderer, sets a 2x logical
  presentation scale, and confirms a window-space motion event correctly reads back in logical
  space — explicitly cross-referenced as the read-side counterpart to an existing write-side test
  in `MouseInputTests.cpp` (`SetPositionConvertsLogicalToWindowForLetterboxedRenderer`), closing a
  read/write symmetry gap. It also gracefully `GTEST_SKIP()`s if SDL video/window/renderer creation
  fails rather than falsely failing in an environment without a display — correct headless-CI
  hygiene.
- `MotionEventRelativeDeltaReachesInputManagerThroughBridge` (P3-013/P3-025) correctly distinguishes
  the *drain* semantics (a second read with no new motion returns 0,0) as a real, distinct behavior
  from mere accumulation, and correctly notes this specifically tests the SDL-event-to-
  `InputManager` wiring, not `InputManager`'s internal accumulation logic (already covered
  elsewhere).
- `HorizontalWheelDoesNotAffectVerticalScrollWheel` / `VerticalWheelDoesNotAffectHorizontalEXTValue`
  (N-005) are a correctly paired bidirectional-independence test for the two wheel axes — testing
  only one direction would leave the other direction's independence unverified.
- All wheel-delta helper functions correctly capture a "before" baseline and assert only the delta,
  avoiding cross-test cumulative-value contamination — consistent with the same defensive pattern
  already praised in `InputResetTests.cpp` and `SdlInputBridgeGoldenTests.cpp` elsewhere in this
  shard.

## Detailed Findings
None.

## Cross-File Observations
`MotionEventConvertsWindowCoordinatesToLogicalForLetterboxedRenderer`'s explicit read/write
symmetry cross-reference to `MouseInputTests.cpp` (outside this shard) is a good example of
deliberate coverage-gap closing across files rather than duplicated or siloed testing.

## Missing or Weak Tests
None identified.

## Positive Findings
The cast-before-multiply wheel-notch truncation test and the real window/renderer DPI-conversion
integration test are both genuinely rigorous, well above a superficial pass/fail check.

## Final Assessment
No findings.
