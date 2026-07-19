# Audit: tests/CNA/Internal/Input/SdlInputBridgeTouchGestureTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Input/SdlInputBridgeTouchGestureTests.cpp` (290 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests `SdlInputBridge::ProcessEvent` → `TouchPanel::INTERNAL_onTouchEvent` →
  `GestureDetector` → `TouchPanel::ReadGesture` end-to-end (backs
  `Microsoft::Xna::Framework::Input::Touch::{TouchPanel,GestureSample}`), Task 782
- Main related tests: explicitly scoped as the end-to-end integration counterpart to
  `GestureDetectorTests.cpp`'s direct-API unit tests (both in this shard)

## Purpose
Proves the full touch/gesture pipeline works end-to-end from the real SDL event entry point: Tap/
Flick gesture production, coordinate-basis consistency between gesture and touch-state positions,
pre-display-size startup behavior, finger-cancel recovery, previous-location exposure, and pressure
forwarding.

## Executive Verdict
Correct and well-reasoned, with one standout test: `GestureAndTouchStateShareTheLogicalCoordinateBasis`
(INPUT-TOUCH-024) precisely verifies a coordinate-basis invariant — that a gesture's pixel position
divided by the display metric equals the same finger's normalized touch-state position — which is
exactly the kind of algebraic invariant test that catches a units-mismatch bug (a genuinely common
bug class per this project's own cross-cutting findings elsewhere: `VertexBufferBinding.VertexOffset`
unit-semantics, `DrawPrimitives` count-vs-vertex confusion) before it manifests as visually-wrong
touch targeting.

## Checklist Results
- `TouchBeforeDisplaySizeIsKnownTracksTouchButSuppressesGestures` (P5-014) is a careful test of a
  genuinely subtle startup-ordering issue: before `GraphicsDevice` publishes a display size, touch
  PRESENCE is still tracked but GESTURES are correctly suppressed (to avoid scaling into a bogus
  (0,0)-corner gesture) — and the test also verifies RECOVERY once the display size is later
  published, for a fresh finger. Testing both the suppression and the recovery in one test is a
  complete treatment of this edge case.
- `FingerCanceledReleasesTouchLikeFingerUp` / `FingerIdReusableAfterCancel` /
  `FingerCanceledMidDragRecoversAndAllowsAFreshTap` (Tasks 892-893, INPUT-GESTURE-012) form a
  thorough cancel-handling test set: a cancel must behave like a lift (not get stuck Pressed), the
  underlying finger-ID slot must be freed for reuse, and — most rigorously — a cancel occurring
  MID-DRAG must not wedge the gesture state machine, verified by draining the interim gesture then
  proving a completely independent subsequent finger still produces a clean Tap. This progression
  (simple cancel → ID reuse → cancel during an active gesture) is a genuinely escalating,
  well-designed edge-case series.
- `FingerEventsExposePreviousLocationThroughTouchPanelGetState` correctly walks Pressed→Moved→
  Released transitions checking `TryGetPreviousLocation` at each step through the real bridge path
  — the end-to-end counterpart to `TouchEdgeCaseTests.cpp`'s more granular direct-API previous-
  location tests (read separately in this same batch).
- `FingerPressureIsSurfacedThroughGetStateGetPressureEXT` (N-006) correctly verifies pressure flows
  end-to-end from the SDL event field through to the public `getPressureEXT()` getter, with a
  second motion event to confirm the value updates (not just captured once at press time).

## Detailed Findings
None.

## Cross-File Observations
The coordinate-basis-equality test here and `TouchEdgeCaseTests.cpp`'s coordinate-scaling tests
(read separately, same batch) together give strong, complementary confidence in the touch
coordinate pipeline: this file proves gesture-vs-state consistency at the bridge level;
`TouchEdgeCaseTests.cpp` proves the underlying `INTERNAL_onTouchEvent` scaling arithmetic itself
(zero/normal/resized/non-integer display sizes) is correct in isolation.

## Missing or Weak Tests
None identified.

## Positive Findings
The coordinate-basis-equality test and the cancel-during-active-gesture recovery test are both
genuinely rigorous integration tests that go well beyond a superficial "does a Tap fire" check.

## Final Assessment
No findings.
