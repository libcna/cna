# Audit: include/Microsoft/Xna/Framework/Input/GamePadCapabilities.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Input/GamePadCapabilities.hpp`
- Audit status: AUDITED (full read, 243 lines)
- Subsystem: `xna-input` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Input/GamePadCapabilities.cs`
- Main related tests: not independently located in this pass

## Purpose
Describes a gamepad's capabilities: which buttons/axes/motors/features it has, plus 10 FNA-extension
properties (light bar, trigger-vibration motors, paddles, touchpad, gyro, accelerometer).

## Executive Verdict
Correct. The class doc comment explicitly discloses the visibility-mapping decision (FNA's
`{ get; internal set; }` properties become CNA `NOXNA`-tagged public setters, since only
`SdlInputBridge` populates a connected instance in practice) and correctly separates the 10 FNA
`EXT` properties (both getter and setter `NOXNA`) from the real XNA property set (getter public,
setter `NOXNA` only because C++ has no internal-to-assembly visibility).

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
Populated by `CNA::Internal::Input::SdlInputBridge::GetCapabilities()` via `GamePad::GetCapabilities()`
(audited separately).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Correctly disclosed visibility mapping and EXT/non-EXT property separation.

## Final Assessment
No findings.
