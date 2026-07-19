# Audit: tests/CNA/Internal/Input/GestureDetectorTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Input/GestureDetectorTests.cpp` (724 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `CNA::Internal::Input::GestureDetector` (backs
  `Microsoft::Xna::Framework::Input::Touch::TouchPanel`'s gesture recognition — this session's
  already-completed `xna-input` shard audit found `TouchLocation` correct with no findings; this
  detector is the underlying CNA-internal state machine, no direct FNA equivalent structurally)
- Main related tests: N/A (this IS a test file)

## Purpose
Exhaustive coverage of every gesture type (Tap, DoubleTap, Hold, HorizontalDrag, VerticalDrag,
FreeDrag, DragComplete, Flick, Pinch, PinchComplete) including boundary conditions (threshold
distances, timing windows) and state-machine transitions (drag interrupted by a second finger
becoming a pinch; reset clearing stale in-progress state).

## Executive Verdict
An exceptionally thorough state-machine test suite. Every test's comment cites the specific
threshold/gate being exercised (e.g. `MOVE_THRESHOLD = 35.0f`, `MIN_FLICK_VELOCITY = 100.0f`,
DoubleTap's 300ms window, Hold's 1s minimum) and many explicitly test the *boundary* (at/just-above/
just-below a threshold) rather than only an unambiguous interior case. The deterministic test-clock
injection (`EnableTestClock`/`AdvanceTestClockMilliseconds`) correctly replaces real
`sleep_for`-based timing, making every timing-dependent test both fast and reproducible.

## Checklist Results
- Every gesture's "fires when enabled" case is paired with a "does NOT fire when that gesture type
  is disabled" case (e.g. `TapDoesNotFireWhenTapGestureIsDisabled`, `HoldDoesNotFireWhenHoldGestureIsDisabled`,
  `FlickDoesNotFireWhenFlickGestureIsDisabled`) — proving the enabled-gesture filter itself works,
  not just that the underlying detection logic works.
- `SecondFingerDuringADragInterruptsItAndBecomesAPinch`/`DragInterruptedByASecondFingerReportsPinchCompleteNotDragComplete`
  correctly test a genuinely subtle state-machine transition (an in-progress drag re-classified as a
  pinch when a second finger appears), including that the *terminal* event on release is
  `PinchComplete`, not the now-stale `DragComplete` — a real, easy-to-get-wrong edge case.
- `GestureStateRecoversAfterADragEndsSoAFreshTapStillFires`/`ResetForTestsClearsDetectorInternalState`
  both correctly verify state cleanup by proving a *subsequent, independent* gesture behaves
  correctly afterward (a fresh finger taps cleanly) rather than only checking an internal flag
  directly — a more robust verification style since it exercises the real observable consequence of
  leftover state, not just an internal implementation detail.
- `PinchAndPinchCompleteFireForTwoFingerGesture` correctly asserts `PinchComplete` carries all-zero
  position/delta fields (a terminal marker, matching `DragComplete`'s documented XNA/FNA shape) —
  not merely that the event type is correct.
- `GestureTimestampIsNonNegativeAndAdvancesWithTheClock`'s own comment explains a real, deliberate
  divergence from FNA: CNA intentionally does not replicate FNA's literal tick/millisecond
  unit-mismatch formula, citing `docs/input-fna-fidelity.md`'s own documented rationale — an
  honestly-disclosed intentional divergence, not an unexplained deviation.

## Detailed Findings
None.

## Cross-File Observations
None beyond general consistency with this project's established test-fixture conventions
(`ResetForTests`/`EnableTestClock` setup/teardown pattern) seen elsewhere in this shard.

## Missing or Weak Tests
None identified — this file's coverage of gesture-type combinations and state transitions is
unusually thorough.

## Positive Findings
The systematic "fires when enabled / does not fire when disabled" pairing across every gesture
type, combined with genuine boundary-condition testing (not just interior cases), represents some
of the most disciplined state-machine test coverage found in this audit.

## Final Assessment
No findings.
