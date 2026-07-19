# Audit: src/CNA/Internal/Input/GestureDetector.cpp

## Metadata

- Source file: `src/CNA/Internal/Input/GestureDetector.cpp`
- Audit status: AUDITED
- Subsystem: `cna-internal-core` shard (`CNA::Internal::Input`)
- File type: C++ implementation
- XNA/FNA relevance: internal seam/bridge feeding the real `Microsoft::Xna::Framework::Input`
  (Keyboard/Mouse/GamePad/Touch) and `CNA::Input` (Joysticks/Haptics/Sensors/Power/InputDevices) public
  APIs — several functions here are directly and explicitly cross-referenced against FNA's own
  `SDL3_FNAPlatform.cs` source line numbers
- Graphics backend relevance: none directly (input subsystem)
- Main related tests: see Missing or Weak Tests

## Purpose

Implements the gesture-recognition state machine: OnPressed/OnMoved/OnReleased/OnUpdate drive an 8-state machine (NONE/HOLDING/HELD/JUST_TAPPED/DRAGGING_H/V/FREE/PINCHING) producing GestureSample events.

## Executive Verdict

Healthy — a careful, internally self-consistent state machine; full line-by-line FNA reference-value cross-check not performed given this pass's scope.

## Checklist Results

### Behavioral correctness / FNA parity / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
Logic traced through the full pressed/moved/released/update cycle for single-touch (tap/double-tap/hold/drag/flick) and 2-finger (pinch) paths — internally consistent: `fingerIds` correctly tracks all down fingers so multi-touch-while-single-touch-active is handled (extra fingers ignored unless pinch is enabled); pinch-to-single-finger release correctly promotes the remaining finger or falls back to a 3rd tracked finger; velocity is computed via an exponential smoothing filter (0.45 blend) reset on release; `ResetForTests()` correctly restores the real (non-test) clock so a forgotten `EnableTestClock()` doesn't leak into later tests, with an explicit comment explaining why. Thresholds (`MOVE_THRESHOLD=35`, hold=1s, double-tap window=300ms, `MIN_FLICK_VELOCITY=100.0f`) were not independently verified against FNA's own exact reference constants in this pass — flagged as an honest scope limitation, not a claim of full FNA-parity verification.

### Testing
Test coverage not independently located/verified in this pass; the class's own `ResetForTests()`/test-clock mechanism strongly implies dedicated tests exist somewhere in the `tests/` tree (not confirmed by filename in this pass).

## Detailed Findings

Logic traced through the full pressed/moved/released/update cycle for single-touch (tap/double-tap/hold/drag/flick) and 2-finger (pinch) paths — internally consistent: `fingerIds` correctly tracks all down fingers so multi-touch-while-single-touch-active is handled (extra fingers ignored unless pinch is enabled); pinch-to-single-finger release correctly promotes the remaining finger or falls back to a 3rd tracked finger; velocity is computed via an exponential smoothing filter (0.45 blend) reset on release; `ResetForTests()` correctly restores the real (non-test) clock so a forgotten `EnableTestClock()` doesn't leak into later tests, with an explicit comment explaining why. Thresholds (`MOVE_THRESHOLD=35`, hold=1s, double-tap window=300ms, `MIN_FLICK_VELOCITY=100.0f`) were not independently verified against FNA's own exact reference constants in this pass — flagged as an honest scope limitation, not a claim of full FNA-parity verification.

## Cross-File Observations

None.

## Missing or Weak Tests

Test coverage not independently located/verified in this pass; the class's own `ResetForTests()`/test-clock mechanism strongly implies dedicated tests exist somewhere in the `tests/` tree (not confirmed by filename in this pass).

## Positive Findings

Clean, correct, well-documented internal seam/implementation.

## Final Assessment

See findings above.
