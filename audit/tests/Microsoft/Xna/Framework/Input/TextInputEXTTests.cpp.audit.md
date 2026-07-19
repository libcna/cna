# Audit: tests/Microsoft/Xna/Framework/Input/TextInputEXTTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Input/TextInputEXTTests.cpp` (355 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-input` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Microsoft::Xna::Framework::Input::TextInputEXT` (FNA EXT +
  NOXNA extension)
- Main related tests: N/A (this IS a test file)

## Purpose
Covers `TextInputEXT`'s `TextInput`/`TextEditing`/`TextEditingCandidatesEXT` multicast events
(dispatch correctness, multicast delivery, no-subscriber safety), `ResetForTests`' full-state-reset
contract (subscriber lists AND window handle), the null-window-guard no-op behavior for every
window-dependent call (`StartTextInput`/`StopTextInput`/`SetInputRectangle`/`IsTextInputActive`/
`IsScreenKeyboardShown`/`StartTextInputWithTypeEXT` for all 9 `TextInputTypeEXT` hint values), and
real-SDL-window round-trip verification for the same window-dependent calls.

## Executive Verdict
No findings. `ResetForTestsClearsAllSubscriberLists` correctly distinguishes itself from the
sibling `ResetForTestsClearsWindowHandleSoLaterCallsAreNullGuarded` test via an explicit comment —
`ResetForTests`'s own documentation claims it resets "callbacks, window handle" and this file
verifies both halves of that claim separately, rather than assuming one verifies the other.

## Checklist Results
- `TextEditingEmptyCompositionFiresWithEmptyString`'s comment correctly documents and tests a
  specific FNA-vs-CNA mapping decision: FNA passes null for an empty composition, CNA maps that to
  an empty `std::string` — a deliberate, disclosed deviation rather than an accidental behavior
  difference.
- The real-SDL-window round-trip tests (`StartStopAndIsActiveRoundTripThroughRealWindow`,
  `StartTextInputWithTypeRoundTripsThroughRealWindowForEveryType`) correctly `GTEST_SKIP()` (not
  fail) when a specific platform/IME setup doesn't toggle `SDL_TextInputActive` on a hidden window,
  explicitly reasoning that this keeps the test "a genuine real-path check without becoming
  environment-flaky" rather than either silently passing or spuriously failing on such platforms.
- `StartTextInputWithTypeWithoutWindowIsSafeNoOpForEveryType`/
  `StartTextInputWithTypeRoundTripsThroughRealWindowForEveryType` both iterate all 9
  `TextInputTypeEXT` hint values, giving genuinely exhaustive coverage for this NOXNA/EXT enum
  rather than a sample.
- `SetInputRectangleWithZeroOrNegativeValuesIsSafe` correctly covers three distinct degenerate
  cases (all-zero, negative position with zero size, all-negative) for a geometry input that must
  tolerate degenerate values without crashing.
- `TextInputDispatchesEachCodeUnitToSubscriber`/`TextEditingDispatchesTextStartAndLength`/
  `TextEditingCandidatesDispatchesListSelectedAndHorizontal` each correctly verify the exact
  dispatched values, not just that a callback fired.

## Detailed Findings
None.

## Cross-File Observations
The real-SDL-window round-trip pattern (SDL_INIT_VIDEO + hidden window + GTEST_SKIP on
environment failure) directly mirrors the equivalent pattern in `MouseInputTests.cpp`, a
consistent, reusable idiom for this shard's hardware-adjacent extension APIs.

## Missing or Weak Tests
None identified for this class's public surface.

## Positive Findings
The explicit "keeps this a genuine real-path check without becoming environment-flaky" reasoning
in the real-window tests is a good example of deliberate, disclosed test-environment-tolerance
design rather than silent flakiness risk.

## Final Assessment
No findings.
