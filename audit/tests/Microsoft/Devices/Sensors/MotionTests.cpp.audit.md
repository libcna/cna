# Audit: tests/Microsoft/Devices/Sensors/MotionTests.cpp

## Metadata
- Source file: `tests/Microsoft/Devices/Sensors/MotionTests.cpp` (1040 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-microsoft-devices` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Microsoft::Devices::Sensors::Motion` (WP7-only API, no FNA reference)
- Main related tests: N/A (this IS a test file)

## Purpose
Structurally near-identical to `CompassTests.cpp` (both are permanently `NotSupported`-stub-by-
default on non-Android platforms, both use a fake `I*Backend` to exercise real delegation), covering
`Motion`'s `CurrentValueChanged`/`Calibrate`/`GetIsAttitudeNorthReferencedProperty` surface.

## Executive Verdict
Equally thorough. **Confirms the same `Dispose(bool)` coverage gap**: no test in this file calls
`Dispose(false)` directly.

## Checklist Results
- `GetIsAttitudeNorthReferencedPropertyIsTrueWithNoBackend`'s own comment (lines 569-572) correctly
  frames the "true with no backend" default as "a vacuous 'nothing to warn about' default, not an
  affirmative claim" — a careful, precise framing of what a default value actually means, avoiding
  a misleading "definitely north-referenced" implication.
- `GetIsAttitudeNorthReferencedPropertyThrowsAfterDispose` — confirms this specific getter DOES
  check disposed state (contrast with `SensorBaseTests.cpp`'s own explicit finding that
  `getCurrentValueProperty()`/`getIsDataValidProperty()` on the shared base do NOT check disposed
  state) — a real, deliberate, already-disclosed asymmetry within this class's own property set,
  not a new inconsistency this audit is flagging.
- `SynchronousReadingCallbackDuringStartIsHandledSafely`/`ConcurrentStopDuringStartDoesNotDeadlock`/
  `DestroyingOwnerFromCurrentValueChangedThenFiringCalibrateDoesNotCrash` mirror `CompassTests.cpp`'s
  identical LIFE-001/LIFE-002/LIFE-005 regression tests.

## Detailed Findings
None new beyond the already-documented cross-cutting `Dispose(bool)` finding.

## Cross-File Observations
Same `Dispose(bool)` coverage gap as `AccelerometerTests.cpp`/`CompassTests.cpp`/`GyroscopeTests.cpp`.

## Missing or Weak Tests
No test calls `Dispose(false)` directly — the same gap as every other sensor test file.

## Positive Findings
The careful "vacuous default, not an affirmative claim" framing for
`GetIsAttitudeNorthReferencedPropertyIsTrueWithNoBackend` is a good example of precise test-comment
writing that avoids overclaiming what a default value actually proves.

## Final Assessment
No new findings beyond the already-documented `Dispose(bool)` visibility gap's coverage
consequence.
