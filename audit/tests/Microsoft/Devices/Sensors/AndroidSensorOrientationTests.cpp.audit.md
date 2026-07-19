# Audit: tests/Microsoft/Devices/Sensors/AndroidSensorOrientationTests.cpp

## Metadata
- Source file: `tests/Microsoft/Devices/Sensors/AndroidSensorOrientationTests.cpp` (250 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-microsoft-devices` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Detail::ConvertAndroidPortraitToXnaLandscape()` (WP7-only API, no
  FNA reference)
- Main related tests: N/A (this IS a test file)

## Purpose
Tests the pure Android-portrait-to-XNA-landscape coordinate-remap function across both supported
rotations, for accelerometer- and gyroscope-shaped inputs, plus the `IsAndroidLandscapeRemapEnabled`/
`SetAndroidLandscapeRemapEnabled` feature-flag pair.

## Executive Verdict
Correct, and unusually honest about the limits of what a pure-function unit test can prove.
`ForwardBackwardSignConventionIntentionallyFlipsBetweenRotations`'s own comment (lines 197-213)
explicitly discloses that, unlike the Y axis (which has a WP7-documented absolute sign convention,
independently verified elsewhere), the X (forward/backward) axis has no such documented absolute
convention — this test only locks in the code's *own* internally-consistent behavior, explicitly
declining to assert which sign corresponds to which physical tilt direction, since that requires
real-hardware verification the comment says was found, during investigation, to be "easy to get
backwards without a real device to check against."

## Checklist Results
- `ScopedAndroidLandscapeRemapSetting` is a well-designed RAII helper restoring the process-wide
  default (`true`) after each test, explicitly modeled on this codebase's established
  `ScopedFake*Backend` convention.
- `RightTiltIsAlwaysPositiveYRegardlessOfRotation`/`LeftTiltIsAlwaysNegativeYRegardlessOfRotation`
  correctly verify the *documented, absolute* Y-axis convention holds across both rotations, in
  both directions — a genuinely useful test given the two rotations report opposite raw signs for
  the same physical tilt.
- Z-axis tests (`FaceUpProducesPositiveZ...`/`FaceDownProducesNegativeZ...`) correctly confirm Z is
  untouched by either rotation's formula.

## Detailed Findings
None.

## Cross-File Observations
None.

## Missing or Weak Tests
The file's own comment acknowledges `docs/devices-hardware-checklist.md` still requires physically
verifying these signs against a real device tilt — this file only proves the documented convention
matches what the code implements, not that the convention itself is correct on real hardware. This
is an honestly-disclosed limitation, not a hidden gap.

## Positive Findings
The explicit, reasoned refusal to assert an unverified absolute sign convention for the X axis —
rather than guessing and risking a confidently-wrong regression test — is an example of good test
design discipline under genuine uncertainty.

## Final Assessment
No findings.
