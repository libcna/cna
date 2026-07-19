# Audit: tests/Microsoft/Xna/Framework/Input/GamePadTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Input/GamePadTests.cpp` (270 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-input` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Microsoft::Xna::Framework::Input::GamePad`/`GamePadCapabilities`
  and `CNA::Internal::Input::SdlInputBridge::FormatGamePadGUIDEXT`
- Main related tests: N/A (this IS a test file)

## Purpose
Covers `GamePad::ExcludeAxisDeadZone`'s boundary/rescale/max-magnitude behavior, `GamePad`'s
no-hardware fallback paths (`GetCapabilities`, `SetVibration`, `SetTriggerVibrationEXT`,
`SetLightBarEXT`, `GetGyroEXT`, `GetAccelerometerEXT`, `GetGUIDEXT`), `FormatGamePadGUIDEXT`'s
XInput/real-device GUID formatting (matching FNA's `GetGamePadGUID`), and `GamePadCapabilities`'s
35 boolean capability flags plus its `GamePadType` field.

## Executive Verdict
No findings. `EachBoolCapabilitySetterAffectsOnlyItsOwnGetter` is an exemplary isolation test for a
class with 35 same-typed boolean properties: a pointer-to-member table iterates every
(setter, getter) pair, sets exactly one flag per outer iteration, and asserts every *other* getter
stays false — directly catching a getter/field mis-wiring that a merely-cumulative
round-trip test (`EveryGetterAndSetterRoundTrips`, also present) could miss, since a getter wired
to an already-set neighboring field would still read `true` there.

## Checklist Results
- `FormatGUIDEmitsVendorThenProductLittleEndianHex` pins down real, specific vendor/product ID
  pairs (DualShock 4, DualSense, Xbox 360 wired) against their exact expected hex strings, matching
  FNA's hardcoded `GetGamePadGUID` byte order — a strong, concrete regression test rather than a
  synthetic example only.
- `FormatGUIDReturnsXinputWhenVendorAndProductAreZero` correctly tests the special-cased "xinput"
  literal FNA returns for XInput controllers (which report no USB vendor/product), distinct from
  the general hex-formatting path.
- `FormatGUIDIsAlwaysEightHexCharsForNonZeroIds` correctly guards against truncated (non-zero-padded)
  output for low IDs.
- The `static_assert(n == 35, ...)` in `EachBoolCapabilitySetterAffectsOnlyItsOwnGetter` is a good
  safety net ensuring the isolation table itself stays in sync if a new capability flag is added.
- The no-hardware-fallback tests correctly cover every `GamePad` API that has a defined
  disconnected/no-op behavior, including output-parameter zeroing (`GetGyroEXT`/`GetAccelerometerEXT`
  zeroing their `Vector3&` out-parameter on failure).

## Detailed Findings
None.

## Cross-File Observations
The `EachBoolCapabilitySetterAffectsOnlyItsOwnGetter` isolation-table pattern (pointer-to-member
table, single-flag-set-per-iteration, assert all others false) is a strong technique that could be
reused for any other class with many same-typed boolean properties in this codebase.

## Missing or Weak Tests
None identified for the covered public surface.

## Positive Findings
The GUID-formatting tests' use of real, named, recognizable device vendor/product IDs (not
arbitrary placeholder values) makes the test both a correctness check and useful documentation of
what devices this formatting was verified against.

## Final Assessment
No findings.
