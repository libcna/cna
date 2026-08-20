# Audit: src/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.cpp

## Metadata
- Source file: `src/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.cpp` (228 lines)
- Audit status: AUDITED (full read)
- Subsystem: `microsoft-devices` shard
- File type: C++ implementation (Android-only, `#ifdef __ANDROID__`-gated)
- XNA/FNA relevance: Direct XNA type (`Compass`); FNA has no reference material for this namespace
- Main related tests: not independently located in this pass (Android-only, no host test seam per this file's own comment)

## Purpose
Implements the two-stream (rotation-vector + magnetic-field) fusion, freshness-gating, and calibration-callback dispatch for the Android `Compass` backend.

## Executive Verdict
Correct, and demonstrates careful, specific reasoning about reentrancy that I verified independently. `HandleMagneticFieldSample()`'s comment (lines 136-153) explicitly documents that `PublishReading()` must run *before* `calibrationCallback()`, not after, because the calibration callback invokes user code that may destroy the owning `Compass`/backend reentrantly — I confirmed the actual code (lines 154-159) matches this ordering exactly (`PublishReading();` then `if (calibrationCallback) calibrationCallback();`), and that `PublishReading()` itself never touches `this` after its own callback invocation (its last statement is `if (callback) { callback(reading); }`, with no further access to member state afterward) — so this is genuinely safe, not merely commented as if it were.

## Checklist Results
- `PublishReading()` (lines 162-225) correctly requires both `hasRotationVectorSample_`/`hasMagneticFieldSample_` AND both streams' freshness (`IsCompassSampleFresh`) before publishing — confirmed this closes the "one stream silently stalls, the other keeps refreshing `CompassReading.Timestamp` forever" staleness bug the header's doc comment describes; the check correctly `return`s (skips publish) rather than publishing partial/stale data.
- `TrueHeading` is deliberately set equal to `MagneticHeading` (line 218, `magneticHeadingDegrees_` passed twice) rather than fabricated from an assumed declination — correctly and honestly disclosed as a real limitation (no location/declination source exists in this codebase), not silently wrong.
- `Start()` resets `hasRotationVectorSample_`/`hasMagneticFieldSample_`/both timestamps at the top of a fresh `Start()` call (lines 41-53) — correctly prevents a stale timestamp from a *previous* run making the first new sample of *this* run look artificially stale relative to it.
- Lock scoping in every `Handle*Sample()` method: `stateMutex_` is acquired only for the state-mutation block, then released before `PublishReading()`/callback invocation — consistent, correct pattern avoiding holding the lock across a callback that could re-enter this same object.

## Detailed Findings
None.

## Cross-File Observations
Directly consumes `ConvertRotationVectorToMagneticHeadingDegreesWithTiltMode()` and `ShouldRaiseCalibrateForAccuracyStatus()`/`ConvertMagneticFieldAccuracyStatusToHeadingAccuracyDegrees()` from `AndroidCompassMath.hpp` (audited separately, no findings) — confirmed this file passes the raw `sample.Values[0..3]`/`sample.Status` straight through with no intermediate transformation of its own that could introduce a mismatch with that header's own documented parameter conventions.

## Missing or Weak Tests
Android-only, no host-side test seam reaches this exact multi-callback reentrancy chain, per this file's own comment (citing `plans/plan_devices.md`'s `COMPASS-008` closing note) — an honestly disclosed gap, not a silent one.

## Positive Findings
The `PublishReading()`-before-`calibrationCallback()` ordering fix (Task COMPASS-008) is a genuine, subtle reentrancy bug correctly identified and fixed, with the fix's correctness independently confirmed in this pass by tracing the actual call sequence.

## Final Assessment
No findings.
