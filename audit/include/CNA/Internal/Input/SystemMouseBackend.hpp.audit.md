# Audit: include/CNA/Internal/Input/SystemMouseBackend.hpp

## Metadata

- Source file: `include/CNA/Internal/Input/SystemMouseBackend.hpp`
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

Declares ISystemMouseBackend: an injectable seam over SDL3's global-mouse API (capture/position/warp).

## Executive Verdict

Healthy.

## Checklist Results

### Behavioral correctness / FNA parity / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
Clean, minimal interface.

### Testing
Covered indirectly via the public CNA::Input API's own test files, or via dedicated Internal test files.

## Detailed Findings

Clean, minimal interface.

## Cross-File Observations

None.

## Missing or Weak Tests

Covered indirectly via the public CNA::Input API's own test files, or via dedicated Internal test files.

## Positive Findings

Clean, correct, well-documented internal seam/implementation.

## Final Assessment

See findings above.
