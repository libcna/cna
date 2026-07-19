# Audit: include/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.hpp

## Metadata
- Source file: `include/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.hpp` (176 lines)
- Audit status: AUDITED (full read)
- Subsystem: `microsoft-devices` shard
- File type: C++ header (Android-only, `#ifdef __ANDROID__`-gated)
- XNA/FNA relevance: Direct XNA type (`Motion`); FNA has no reference material for this namespace
- Main related tests: not independently located in this pass

## Purpose
Declares the Android native `Motion` backend: fuses a rotation-vector (or game-rotation-vector fallback) attitude source with `TYPE_GRAVITY`/`TYPE_LINEAR_ACCELERATION`/`TYPE_GYROSCOPE`, plus a best-effort `TYPE_MAGNETIC_FIELD` calibration-only stream.

## Executive Verdict
Correct. `usingGameRotationVector_`'s doc comment correctly documents a real fix (Task MOT2-005): the field went from "written once at `Start()`, never read, therefore harmless unsynchronized" to "now read by `IsUsingNorthReferencedAttitudeSource()` from any thread," and is now correctly guarded by `stateMutex_` — I confirmed in the `.cpp` that both the write (in `Start()`) and the read (in `IsUsingNorthReferencedAttitudeSource()`) are actually under the lock, not just documented as such.

## Checklist Results
- Five `AndroidSensorBridge` members plus a sixth calibration-only one — each independently owns its own worker thread/lifecycle per `AndroidSensorBridge`'s own design; correctly noted in the doc comment that a separate `TYPE_GYROSCOPE` registration here doesn't conflict with any live `Gyroscope` C++ instance since Android allows multiple listeners per physical sensor.
- `GetDroppedFusionFrameCountForTesting()`/`droppedFusionFrameCountForTesting_` correctly documented as guarded by `stateMutex_`, consistent with every other field a background callback can write.
- Non-copyable, matching `AndroidCompassBackend`'s identical discipline.

## Detailed Findings
None.

## Cross-File Observations
`MaxFusionAgeWindow`'s doc comment explains the same "catch a source that stopped delivering entirely, not enforce tight synchronization" rationale as `AndroidCompassMath.hpp`'s `ComputeCompassMaxSampleSkew()` — consistent design language for the same underlying staleness-detection problem, applied independently to two different fusion pipelines (2-stream compass, 4-stream motion).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
`MOTION-011`'s magnetic-field-for-calibration-only design is cleanly scoped: explicitly documented as best-effort/optional, with `Start()`'s overall success never depending on it — confirmed correctly implemented in the `.cpp` (`(void)magneticFieldBridge_.Start(...)`, return value deliberately ignored).

## Final Assessment
No findings.
