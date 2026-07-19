# Audit: include/CNA/Internal/Input/SdlInputBridge.hpp

## Metadata

- Source file: `include/CNA/Internal/Input/SdlInputBridge.hpp`
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

Declares SdlInputBridge: the central SDL-event-to-CNA-input-state bridge, plus the full set of gamepad/joystick/keyboard NOXNA/EXT query surface.

## Executive Verdict

Needs attention — 1 minor documentation-placement defect found.

## Checklist Results

### Behavioral correctness / FNA parity / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
**Confirmed: `GetKeyFromScancode()` (declared at line 244) has no Doxygen comment of its own — its intended comment ("Translates a US-layout Keys value to the Keys value the current keyboard layout produces...", lines 204-208) is orphaned 40 lines earlier, immediately preceding `SetScancodeModeForTests()`'s own (correctly separate) doc comment instead.** This looks like a comment that was not moved along with its declaration during a reorder/edit — a minor, cosmetic documentation defect (not a behavioral bug), but a real violation of this project's own stated rule that every public method in a `.hpp` file must have a Doxygen comment (`CLAUDE.md`), since `GetKeyFromScancode()` itself is currently effectively undocumented at its own declaration site.

### Testing
Covered indirectly via the public CNA::Input API's own test files, or via dedicated Internal test files.

## Detailed Findings

**Confirmed: `GetKeyFromScancode()` (declared at line 244) has no Doxygen comment of its own — its intended comment ("Translates a US-layout Keys value to the Keys value the current keyboard layout produces...", lines 204-208) is orphaned 40 lines earlier, immediately preceding `SetScancodeModeForTests()`'s own (correctly separate) doc comment instead.** This looks like a comment that was not moved along with its declaration during a reorder/edit — a minor, cosmetic documentation defect (not a behavioral bug), but a real violation of this project's own stated rule that every public method in a `.hpp` file must have a Doxygen comment (`CLAUDE.md`), since `GetKeyFromScancode()` itself is currently effectively undocumented at its own declaration site.

## Cross-File Observations

None.

## Missing or Weak Tests

Covered indirectly via the public CNA::Input API's own test files, or via dedicated Internal test files.

## Positive Findings

Clean, correct, well-documented internal seam/implementation.

## Final Assessment

See findings above.
