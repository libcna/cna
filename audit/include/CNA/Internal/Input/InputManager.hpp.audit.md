# Audit: include/CNA/Internal/Input/InputManager.hpp

## Metadata

- Source file: `include/CNA/Internal/Input/InputManager.hpp`
- Audit status: AUDITED
- Subsystem: `cna-internal-core` shard (`CNA::Internal::Input`)
- File type: C++ header
- XNA/FNA relevance: internal seam/bridge feeding the real `Microsoft::Xna::Framework::Input`
  (Keyboard/Mouse/GamePad/Touch) and `CNA::Input` (Joysticks/Haptics/Sensors/Power/InputDevices) public
  APIs — several functions here are directly and explicitly cross-referenced against FNA's own
  `SDL3_FNAPlatform.cs` source line numbers
- Graphics backend relevance: none directly (input subsystem)
- Main related tests: see Missing or Weak Tests

## Purpose

Declares InputManager: the internal, event-driven (not poll-driven) accumulated input state for Mouse/Keyboard/GamePad/Touch, snapshotted by the public XNA-facing Get*State() calls.

## Executive Verdict

Healthy.

## Checklist Results

### Behavioral correctness / FNA parity / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
Explicitly and correctly documents its own architectural divergence from FNA (event-driven accumulation vs. FNA's poll-fresh-every-call model) and its single-threaded-only contract (writes from `SdlInputBridge::ProcessEvent` during `Game::PollEvents()`, reads from `Update()`/`Draw()`, all same thread — matching XNA/FNA's own game-loop model, not a defect).

### Testing
Covered indirectly via the public CNA::Input API's own test files, or via dedicated Internal test files.

## Detailed Findings

Explicitly and correctly documents its own architectural divergence from FNA (event-driven accumulation vs. FNA's poll-fresh-every-call model) and its single-threaded-only contract (writes from `SdlInputBridge::ProcessEvent` during `Game::PollEvents()`, reads from `Update()`/`Draw()`, all same thread — matching XNA/FNA's own game-loop model, not a defect).

## Cross-File Observations

None.

## Missing or Weak Tests

Covered indirectly via the public CNA::Input API's own test files, or via dedicated Internal test files.

## Positive Findings

Clean, correct, well-documented internal seam/implementation.

## Final Assessment

See findings above.
