# Audit: tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp` (586 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests `Microsoft::Xna::Framework::Input::Touch::TouchPanel` and
  `CNA::Internal::Input::InputManager`'s touch tracking directly (Tasks 825-828, 868-871, 892-894,
  896, INP-AUD-001/003)
- Main related tests: the granular direct-API counterpart to
  `SdlInputBridgeTouchGestureTests.cpp`'s end-to-end bridge tests (read separately, same batch)

## Purpose
Exercises touch-pipeline edge cases directly against `TouchPanel`/`InputManager`: the
`touches_`-vs-`InputManager`-fallback exclusivity rule, multi-touch edges (>MAX_TOUCHES capping,
unknown-finger release, repeated-down, ID reuse), `GetCapabilities()` behavior across every
observation path (sticky flag, live touch, SDL enumeration), and `INTERNAL_onTouchEvent`'s
coordinate-scaling arithmetic.

## Executive Verdict
Excellent — this file is a model example of regression-driven testing: numerous tests explicitly
cite a specific historical bug class by task ID (INP-AUD-001, 894/896, P5-007/008/009/011) and
describe exactly what would have gone wrong without the fix, rather than merely asserting current
behavior.

## Checklist Results
- `GetStatePrefersTouchesArrayAndDoesNotDoubleReport` correctly verifies a genuinely important
  invariant (exclusive-source, not union) — a "shadowed" `InputManager` touch alongside a
  `touches_`-array touch must not cause the API to report 2 touches when only 1 exists in the
  active source.
- `MoreThanMaxTouchesAreCappedAtMaxTouchesByTouchPanelGetState` (DEC-10, P5-011) doesn't just check
  the count is capped — it verifies the truncation is DETERMINISTIC (ascending-ID sort, lowest IDs
  survive) rather than arbitrary, which is the more valuable and harder-to-get-right claim.
- `SetFingerRejectsOutOfRangeSlotIndexWithoutOutOfBoundsWrite` correctly tests both invalid extremes
  (-1, MAX_TOUCHES, MAX_TOUCHES+5 all throw `std::out_of_range`) AND the valid boundary extremes (0,
  MAX_TOUCHES-1 do NOT throw) in the same test — a complete boundary-condition treatment, notably
  using the project's own `System`-style `std::out_of_range` rather than an unchecked write (a good
  counter-example to this project's own recurring "raw std:: exceptions" cross-cutting finding —
  here the exception TYPE choice itself is correct even if it's a raw `std::` type, since
  `std::out_of_range` is specifically the semantically-correct standard type for this case).
- `GetCapabilitiesHasNoSideEffectOnTouchState` / `EnumerationQueryDoesNotMutateTouchState` both
  explicitly document and test a real historical regression (Task 894/896): a naive
  `GetCapabilities()` fallback previously called the mutating `GetTouchState()`, silently corrupting
  the NEXT frame's `GetState()` by prematurely promoting Pressed→Moved. The fix (a non-mutating
  `HasAnyTouch()` peek) is verified by asserting the touch survives unchanged across repeated
  `GetCapabilities()` calls — this is a genuinely valuable "the getter must be free of side effects"
  test class that's easy to skip and easy to regress.
- The `FakeTouchDeviceBackend`/`TouchCapabilitiesEnumerationTest` fixture correctly makes SDL
  touch-device enumeration deterministic and CI-machine-independent (rather than depending on
  whether the CI runner happens to have a touch device attached) — a real, meaningful test-hygiene
  choice, and its 4 tests (enumerated-but-untouched, empty-and-untouched, sticky-flag-fallback for
  Windows' late-enumeration quirk, live-touch-fallback) each target a genuinely distinct
  observation path to "is a touch device present."
- `ScalingRoundsNonIntegerNormalizedCoordinates` independently derives its own expected rounded
  values (0.6667×1000→667, 0.3333×1000→333) rather than trusting a captured value — correct
  independent-derivation practice per this shard's established test-quality bar.
- `GetTouchStateIsPureAndRepeatedReadsWithinAFrameAreIdentical` / `AdvanceTouchFrameWorksEvenWithoutAnIntermediateRead`
  / `ReleasedTouchIsVisibleForExactlyOnePostAdvanceReadRegardlessOfPriorReads` (all INP-AUD-001
  regression guards) collectively give thorough confidence that the read/advance frame model is
  correctly decoupled from how many times (zero, one, or many) a frame is read — a subtle
  read-vs-mutate distinction that's easy to get wrong in an event-driven, frame-buffered API.

## Detailed Findings
None.

## Cross-File Observations
This file's coordinate-scaling tests (`ScalingUsesDisplaySizeForPixelPosition`/
`ScalingReflectsResizedDisplay`/`ScalingRoundsNonIntegerNormalizedCoordinates`) are the direct-API
unit-level complement to `SdlInputBridgeTouchGestureTests.cpp`'s bridge-level coordinate-basis
consistency test — together they give layered confidence (unit-level arithmetic correctness, then
end-to-end basis consistency) in the touch coordinate pipeline.

## Missing or Weak Tests
None identified — this is one of the most thorough, regression-history-aware test files in this
shard.

## Positive Findings
The GetCapabilities-non-mutation regression tests and the deterministic SDL-enumeration fake
backend are both excellent examples of testing an API's absence of side effects, not just its
return value — a class of test many suites omit entirely.

## Final Assessment
No findings.
