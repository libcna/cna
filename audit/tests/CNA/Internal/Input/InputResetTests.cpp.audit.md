# Audit: tests/CNA/Internal/Input/InputResetTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Input/InputResetTests.cpp` (191 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `CNA::Internal::Input::InputManager::ResetAllForTests()` (CNA-internal
  test-support fan-out reset, no direct FNA equivalent)
- Main related tests: N/A (this IS a test file)

## Purpose
Proves `InputManager::ResetAllForTests()`'s single-call fan-out actually clears every input
subsystem's process-wide static state (keyboard, touch/gesture queue, mouse buttons/position/wheel,
callbacks, the sequential touch-ID counter) and that repeated resets are idempotent.

## Executive Verdict
Correct and well-targeted test infrastructure verification. Each test isolates exactly one piece of
process-wide static state, dirties it, resets, and confirms the specific piece of state returned to
its documented zero/default value — a clean, systematic approach to verifying a fan-out reset
function's completeness.

## Checklist Results
- `ClearsPreviousTouchSlotContinuityOnReset` is a genuinely subtle test: it verifies not just that
  a touch is cleared, but that `previousTouches_`'s slot-continuity tracking is cleared too, by
  checking a same-slot/same-finger re-appearance reads as a fresh `Pressed` (not `Moved`) after
  reset — this correctly targets internal continuity state that a naive "is `GetState()` empty"
  check would miss.
- `ClearsAccumulatedMouseButtonsPositionAndWheel`'s own comment explicitly notes the scroll-wheel
  value is process-cumulative, so the test defensively resets first to avoid a prior test's leaked
  delta contaminating the "before" assertion under `--gtest_shuffle` — a real, specific awareness of
  cross-test-ordering risk, not an accidental omission.
- `ResetsSequentialTouchIdCounterViaBridge` correctly proves the reset restarts the compact
  finger-ID counter (not just clears the current touch list) by using a *different* raw SDL finger
  ID before and after reset and confirming the *compact* ID comes out the same — this specifically
  distinguishes "the counter restarted" from "the same raw ID happened to map to the same compact
  ID," a real, non-obvious distinction.
- `IsIdempotent` (Task 891) directly targets test-ordering safety under `--gtest_shuffle`, an
  explicitly-stated goal of this file.

## Detailed Findings
None.

## Cross-File Observations
Complements `GestureDetectorTests.cpp`'s own more granular `ResetForTestsClearsDetectorInternalState`
test — this file verifies the broader `InputManager`-level fan-out includes that gesture-detector
reset among everything else it clears.

## Missing or Weak Tests
None identified.

## Positive Findings
The explicit, stated awareness of `--gtest_shuffle`-driven test-ordering risk (both in this file's
own top-of-file comment citing Task 891 and in `ClearsAccumulatedMouseButtonsPositionAndWheel`'s
defensive pre-reset) is a mature testing practice not universally present even in otherwise
well-written test suites.

## Final Assessment
No findings.
