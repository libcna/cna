# Audit: src/CNA/Internal/Input/InputManager.cpp

## Metadata

- Source file: `src/CNA/Internal/Input/InputManager.cpp`
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

Implements InputManager: per-frame mouse/keyboard/gamepad/touch state accumulation and snapshotting, including the touch-frame-advance (Pressed->Moved promotion, Released removal) state machine.

## Executive Verdict

Healthy — careful, correct state machine, verified in detail.

## Checklist Results

### Behavioral correctness / FNA parity / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
**`AdvanceTouchFrame()` verified correct**: records "previous" location BEFORE the Pressed->Moved promotion (matching its own doc comment's stated ordering rationale), and correctly two-phases removal (mark-then-erase in a second loop) to avoid iterator invalidation while range-for iterating the touch map. **`GetMouseState()`'s relative-mode drain-on-read verified correct** (accumulated delta reported once, then reset to 0). **`PacketNumber` increment logic verified correct**: only bumped when a `Set*` call's new value actually differs from the stored one (matching FNA's real "increments whenever a poll differs from the previous poll" semantics, adapted correctly for this class's event-driven model).

### Testing
Covered indirectly via the public CNA::Input API's own test files, or via dedicated Internal test files.

## Detailed Findings

**`AdvanceTouchFrame()` verified correct**: records "previous" location BEFORE the Pressed->Moved promotion (matching its own doc comment's stated ordering rationale), and correctly two-phases removal (mark-then-erase in a second loop) to avoid iterator invalidation while range-for iterating the touch map. **`GetMouseState()`'s relative-mode drain-on-read verified correct** (accumulated delta reported once, then reset to 0). **`PacketNumber` increment logic verified correct**: only bumped when a `Set*` call's new value actually differs from the stored one (matching FNA's real "increments whenever a poll differs from the previous poll" semantics, adapted correctly for this class's event-driven model).

## Cross-File Observations

None.

## Missing or Weak Tests

Covered indirectly via the public CNA::Input API's own test files, or via dedicated Internal test files.

## Positive Findings

Careful, correct adaptation of FNA's poll-based PacketNumber semantics to this class's event-driven architecture; correct touch-frame previous/current tracking ordering.

## Final Assessment

See findings above.
