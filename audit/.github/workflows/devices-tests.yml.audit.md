# Audit: .github/workflows/devices-tests.yml

## Metadata
- Source file: `.github/workflows/devices-tests.yml` (129 lines)
- Audit status: AUDITED (full read)
- Subsystem: `build-ci` shard
- File type: GitHub Actions workflow (YAML)
- XNA/FNA relevance: N/A — CI infrastructure; the test suite it runs targets
  `Microsoft::Devices`/`Microsoft::Devices::Sensors`, already deep-audited in the
  `microsoft-devices`/`tests-microsoft-devices` shards
- Main related tests: runs the exact-name-filtered `Microsoft::Devices`/`.Sensors` gtest suite (see
  `DEVICES_GTEST_FILTER` below) plus a separate `cna_strict_xna_api_check` target

## Purpose
Builds `CnaTests` under the `devices-ubsan` CMake preset and runs the full Devices/Sensors gtest
suite (via an explicit `--gtest_filter` allow-list) on a clean-room `ubuntu-latest` runner, plus a
separate strict-XNA-API-surface check target, triggered on push/PR to relevant paths or manually.

## Executive Verdict
Correct and carefully cross-checked against its own test suite's actual contents. The
`DEVICES_GTEST_FILTER` allow-list (line 40) was directly cross-referenced during this audit against
every `TEST`/`TEST_F` suite name discovered while auditing the `tests-microsoft-devices` shard
(25 files, read in full): every suite name in the filter —
`AccelerometerFailedExceptionTests`, `AccelerometerReadingEventArgsTests`, `AccelerometerReadingTests`,
`AccelerometerTests`, `AndroidSensorOrientationTests`, `AttitudeReadingTests`,
`CalibrationEventArgsTests`, `CompassReadingTests`, `CompassTests`, `AndroidCompassMathTests`,
`AndroidMotionMathTests`, `AndroidSensorBridgeTests`, `GyroscopeReadingTests`, `GyroscopeTests`,
`MotionReadingTests`, `MotionTests`, `ScopeExitTests`, `SensorBaseTests`, `SensorFailedExceptionTests`,
`SensorSubsystemOwnershipTests`, `VibrateControllerTests` — matches a real suite name found in that
shard. Two suite names from that shard are notably ABSENT from this filter:
`NativeDiagnosticTests` (in `tests/Microsoft/Devices/Sensors/Detail/NativeDiagnosticTests.cpp`) and
`DevicesShutdownCoordinatorTests`/`DevicesShutdownOrderingTests`/`ProcSelfResourceCounters` (under
`tests/Microsoft/Devices/Detail/`) — see Detailed Findings.

## Checklist Results
- The `on.push`/`on.pull_request` path filters correctly include `CMakeLists.txt`, `CMakePresets.json`,
  `cmake/**`, and the workflow file itself alongside the actual `Microsoft/Devices` source/test
  trees — a build-system change that could silently break the devices build (e.g. a `devices-ubsan`
  preset edit) correctly re-triggers this job even without touching `Microsoft/Devices` source.
- The in-file comment for the two hardware-dependent tests
  (`AccelerometerTests.GetCurrentValuePropertyDoesNotThrowWhenSupported`,
  `GyroscopeTests.GetCurrentValuePropertyDoesNotThrowWhenSupported`) correctly explains they are
  NOT excluded from the filter but instead self-skip via `GTEST_SKIP()` — an accurate description
  cross-checked against this audit's own read of `AccelerometerTests.cpp`/`GyroscopeTests.cpp`.
- Comment (line 37) explicitly instructs keeping `docs/devices-build.md` Section 2/6 in sync with
  this exact filter string — a real, disclosed dual-maintenance point rather than a silent one.

## Detailed Findings
- **LOW** — `DEVICES_GTEST_FILTER` (line 40) omits `NativeDiagnosticTests`,
  `DevicesShutdownCoordinatorTests`, `DevicesShutdownOrderingTests`, and any suite in
  `ProcSelfResourceCounters.hpp` (a header-only support file, not itself a suite, so its correctness
  is only indirectly exercised — this is expected and not itself a gap). `NativeDiagnosticTests` and
  `DevicesShutdownCoordinatorTests`/`DevicesShutdownOrderingTests` ARE real, independent `TEST_F`
  suites (confirmed during the `tests-microsoft-devices` shard audit) that live under
  `tests/Microsoft/Devices/` yet are not included in this exact-name allow-list — meaning this CI
  job's own explicit filter does not run them, even though the path-trigger filter (`paths:
  'tests/Microsoft/Devices/**'`) would still re-trigger the job on a change to those files. If this
  filter is genuinely meant to be "every suite under `tests/Microsoft/Devices/`" (per the comment's
  own stated intent, "no more and no fewer"), these three suites are a real, if minor, omission.

## Cross-File Observations
This is a rare and valuable case where a CI workflow's own test-selection string could be directly
verified against this audit's independent, full read of the actual test source files it targets
(`tests-microsoft-devices` shard) — most CI-workflow audits in this project cannot be cross-checked
this concretely.

## Missing or Weak Tests
Not applicable to a CI workflow file — the gap identified above is in the filter's own completeness
relative to its stated intent, not a test-coverage gap in the underlying suites themselves.

## Positive Findings
Directly and successfully cross-verifiable against real test source; the hardware-dependent-test
self-skip design is correctly and accurately described.

## Final Assessment
1 LOW finding: `DEVICES_GTEST_FILTER` omits `NativeDiagnosticTests`,
`DevicesShutdownCoordinatorTests`, and `DevicesShutdownOrderingTests` — three real suites under
`tests/Microsoft/Devices/` not covered by this job's exact-name filter, despite the comment's stated
intent to cover "every `TEST(...)` under `tests/Microsoft/Devices/`, no more and no fewer."
