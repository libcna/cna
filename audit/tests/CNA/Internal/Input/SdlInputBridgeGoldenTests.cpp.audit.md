# Audit: tests/CNA/Internal/Input/SdlInputBridgeGoldenTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Input/SdlInputBridgeGoldenTests.cpp` (286 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests `CNA::Internal::Input::SdlInputBridge::ProcessEvent` end-to-end against
  `Microsoft::Xna::Framework::Input::{Keyboard,Mouse}` and `Touch::TouchPanel` (INPUT-TEST-008)
- Main related tests: the value-correctness counterpart to `SdlInputBridgeFuzzTests.cpp`
  (documented cross-reference in both files)

## Purpose
Drives fixed, hand-recorded SDL event scripts through the real `ProcessEvent` entry point and
asserts the COMPLETE resulting state snapshot (keyboard pressed-set, mouse position/buttons/wheel,
multi-finger touch collection, and an interleaved cross-subsystem script) against explicitly
pre-computed expected values.

## Executive Verdict
Correct and unusually well-documented — the two-finger touch test in particular includes a careful,
explicit explanation of `TouchPanel::GetState()`'s frame-boundary state-promotion semantics
(Pressed→Moved, Released→retired) at each checkpoint, which is exactly the kind of subtle stateful
behavior a golden test is suited to pin precisely.

## Checklist Results
- `KeyboardScriptResolvesToExactPressedSet` verifies both the exact pressed-key *set* and the
  individual `IsKeyDown`/`IsKeyUp` results for the released keys — checking the accumulation and
  release semantics from two independent angles in one test.
- `MouseScriptResolvesToExactState` correctly captures the scroll wheel value *before* the script
  runs and asserts the *delta*, avoiding false failures from any cross-test wheel-value leakage
  (the file is process-lifetime cumulative by XNA design, as established in
  `SdlInputBridgeMouseTests.cpp`) — the same defensive-baseline pattern already praised in
  `InputResetTests.cpp` earlier in this shard.
- `TwoFingerScriptResolvesToExactTouchSnapshots` is the standout test: it walks through 4 explicit
  checkpoints of a two-finger down/move/lift sequence, correctly modeling that `GetState()` itself
  mutates tracking state (a call is not idempotent), and gets every expected `TouchLocationState`
  transition (Pressed→Moved→Released→retired) right at each step — this is real, careful reasoning
  about a genuinely stateful API, not a naive "call twice, expect the same thing."
- `InterleavedSessionResolvesEachSubsystemIndependently` is a meaningful cross-subsystem isolation
  test: it interleaves keyboard/mouse/touch events in a single script and confirms none of the three
  subsystems' resulting state depends on the interleaving with the others.
- All events use `windowID = 0`, deliberately chosen (per the file's own header comment) so
  coordinate-transform code passes values straight through, keeping the golden values
  window/renderer-independent and headless-safe.

## Detailed Findings
None.

## Cross-File Observations
The fuzz/golden split with `SdlInputBridgeFuzzTests.cpp` is a clean, deliberate separation already
noted in that file's own audit; this file's touch-state-machine test is a good complement to
`GestureDetectorTests.cpp`'s and `InputResetTests.cpp`'s touch-tracking coverage elsewhere in this
shard.

## Missing or Weak Tests
None identified.

## Positive Findings
The two-finger touch test's careful, explicit reasoning about `GetState()`'s own state-mutating
side effects at each checkpoint is one of the more rigorous stateful-API golden tests in this audit.

## Final Assessment
No findings.
