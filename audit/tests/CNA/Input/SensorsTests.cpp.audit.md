# Audit: tests/CNA/Input/SensorsTests.cpp

## Metadata
- Source file: `tests/CNA/Input/SensorsTests.cpp` (90 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-input` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `CNA::Input::Sensors` (NOXNA extension; distinct from the XNA-facing
  `Microsoft::Devices::Sensors::*` classes audited in the `microsoft-devices` shard)
- Main related tests: N/A (this IS a test file)

## Purpose
Verifies `Sensors::GetSensorsEXT` forwards enumeration, `GetAccelerometerEXT`/`GetGyroscopeEXT`
correctly read a present sample (returning `true` and filling the `Vector3` out-param) or report
absence (`false`, out-param left untouched), plus `SensorInfoEXT`'s equality operator.

## Executive Verdict
Good, deterministic coverage via a dependency-injected fake backend keyed by `SensorTypeEXT`.
`ReadsReturnFalseWhenSensorAbsent` correctly asserts the out-param is left untouched on failure — a
detail that's easy to get wrong (writing a zeroed/garbage Vector3 instead of leaving caller data
alone) and valuable to pin down explicitly.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
Same fake-backend dependency-injection pattern as `InputDevicesTests.cpp`/`PowerTests.cpp` in this
shard. Note this `CNA::Input::Sensors` NOXNA class is architecturally distinct from
`Microsoft::Devices::Sensors::{Accelerometer,Compass,Gyroscope,Motion}` (the XNA-facing classes
audited in the `microsoft-devices` shard, which have the confirmed `Dispose(bool)` public-visibility
MEDIUM finding) — this test file's subject is unrelated to that finding.

## Missing or Weak Tests
Not identified for this wrapper's scope.

## Positive Findings
Explicitly asserting the out-param is left untouched on a failed read is a valuable, easy-to-omit
contract check.

## Final Assessment
No findings.
