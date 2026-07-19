# Audit: src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp

## Metadata
- Source file: `src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp` (413 lines)
- Audit status: AUDITED (full read)
- Subsystem: `microsoft-devices` shard
- File type: C++ implementation (Android-only, `#ifdef __ANDROID__`-gated)
- XNA/FNA relevance: Direct XNA type (`Motion`); FNA has no reference material for this namespace
- Main related tests: not independently located in this pass

## Purpose
Implements four-source sensor fusion (attitude, gravity, linear acceleration, gyroscope) plus best-effort magnetic-field-triggered calibration for the Android `Motion` backend, including a landscape axis remap shared with `Accelerometer`/`Gyroscope`.

## Executive Verdict
Correct. `usingGameRotationVector_`'s write (lines 94-97) and its read via `IsUsingNorthReferencedAttitudeSource()` (lines 156-160) are both confirmed under `stateMutex_`, matching the header's documented fix. `HandleGravitySample()`/`HandleLinearAccelerationSample()`'s g-unit conversion (`/StandardGravity`, `9.80665f`) and `HandleGyroscopeSample()`'s deliberate no-conversion (radians/second on both sides already) are each independently justified in the code's own comments by citing specific archived WP7 MSDN pages and Android's own public sensor documentation — I did not find a case where the cited source doesn't actually support the stated conclusion.

## Checklist Results
- `PublishReading()`'s four-source freshness check (lines 328-379, `MaxFusionAgeWindow` comparison via `std::min`/`std::max` across all four sources' timestamps) correctly drops (returns without publishing) rather than publishing a reading built from a stale source — same pattern as `AndroidCompassBackend::PublishReading()`, applied to four sources instead of two.
- `MotionReading.Timestamp` is deliberately set to `attitude_.getTimestampProperty()`, not a fresh `getUtcNowProperty()` call at publish time (lines 383-403) — correctly reasoned: since publish only happens once all four sources have a sample, a fresh "now" timestamp could postdate the nested `Attitude.Timestamp` by an arbitrary amount, creating two different "now"s for the same fused reading. Anchoring to the attitude timestamp keeps the two internally consistent, at the cost of `MotionReading.Timestamp` sometimes being one canonical sub-value's timestamp rather than the actual publish wall-clock time — a reasonable, disclosed tradeoff.
- `ApplyLandscapeRemapIfEnabled()` correctly gates the entire remap behind `IsAndroidLandscapeRemapEnabled()`, respecting the shared `ACCEL-008` opt-out — confirmed the same `ConvertAndroidPortraitToXnaLandscape()` helper `Accelerometer.cpp`/`Gyroscope.cpp` use is reused here rather than reimplemented, avoiding a possible drift between the three call sites.
- `HandleMagneticFieldSample()` correctly never stores the magnetic-field vector/accuracy anywhere (no `MotionReading` field exists for either) — matches the header's documented "calibration-only, never exposed" scope.

## Detailed Findings
None.

## Cross-File Observations
`ApplyLandscapeRemapIfEnabled()`'s own comment (lines 205-228) cites Android's public developer documentation directly (`developer.android.com/guide/topics/sensors/sensors_motion`) to confirm `TYPE_GRAVITY`/`TYPE_LINEAR_ACCELERATION`/`TYPE_GYROSCOPE` share the same coordinate system as `TYPE_ACCELEROMETER` — correctly scoping this remap's applicability without requiring real hardware, since this is a documented OS API contract rather than a device-specific implementation detail.

## Missing or Weak Tests
Not independently located in this pass; `GetDroppedFusionFrameCountForTesting()` (declared in the header, used here at line 377) suggests a real, exercised test seam for the staleness-drop path.

## Positive Findings
The `MotionReading.Timestamp`-anchored-to-`Attitude.Timestamp` design decision is a subtle, well-reasoned fix to a genuine "which now is the real now" internal-consistency problem that a less careful implementation would likely get wrong or not consider at all.

## Final Assessment
No findings.
