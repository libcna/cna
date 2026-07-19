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
`CnaInputSensorInfoEXTTest.EqualityComparesIdNameAndType` (lines 83-89) only exercises an
all-fields-equal case and a differing-`type` case — unlike the sibling `InputDeviceInfoEXTTest` in
`InputDevicesTests.cpp`, which separately isolates differing-`id`-only and differing-`name`-only
cases. This test cannot distinguish "compares all three fields correctly" from "compares only
`type`, or only `type` plus one other field."

## Detailed Findings
- **LOW** — `CnaInputSensorInfoEXTTest.EqualityComparesIdNameAndType` (lines 83-89) never isolates
  a differing-`id`-only or differing-`name`-only case for `SensorInfoEXT`, only an all-equal case
  and a differing-`type` case. If `SensorInfoEXT::operator==` had a latent bug comparing only
  `type` (ignoring `id`/`name`), this test suite would not catch it.

## Cross-File Observations
Same fake-backend dependency-injection pattern as `InputDevicesTests.cpp`/`PowerTests.cpp` in this
shard. Note this `CNA::Input::Sensors` NOXNA class is architecturally distinct from
`Microsoft::Devices::Sensors::{Accelerometer,Compass,Gyroscope,Motion}` (the XNA-facing classes
audited in the `microsoft-devices` shard, which have the confirmed `Dispose(bool)` public-visibility
MEDIUM finding) — this test file's subject is unrelated to that finding.

Separately, `include/CNA/Input/Sensors.hpp.audit.md` (the production header, audited earlier)
flagged that `SensorTypeEXT` has no entry for `SDL_SENSOR_INVALID` (-1) and that the real
`SDL_SensorType`-to-`SensorTypeEXT` mapping lives entirely in `SystemSensorBackend`, not yet
audited at that time. This test file's `FakeSystemSensorBackend` bypasses that mapping layer
entirely by constructing `SensorTypeEXT` values directly in C++, so the invalid-sensor-type
question remains unverified by any test in this shard — worth revisiting once
`SystemSensorBackend`'s own implementation/tests are audited.

## Missing or Weak Tests
Same gap as noted in Detailed Findings (equality-operator field isolation), plus the already-flagged
absence of any test exercising the real `SDL_SensorType`-to-`SensorTypeEXT` conversion path.

## Positive Findings
Explicitly asserting the out-param is left untouched on a failed read is a valuable, easy-to-omit
contract check.

## Final Assessment
1 LOW finding: `SensorInfoEXT` equality test doesn't isolate `id`-only/`name`-only mismatches, only
an all-equal and a `type`-mismatch case.
