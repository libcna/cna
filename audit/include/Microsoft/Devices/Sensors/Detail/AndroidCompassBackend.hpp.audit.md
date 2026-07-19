# Audit: include/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.hpp

## Metadata
- Source file: `include/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.hpp` (105 lines)
- Audit status: AUDITED (full read)
- Subsystem: `microsoft-devices` shard
- File type: C++ header (Android-only, `#ifdef __ANDROID__`-gated)
- XNA/FNA relevance: Direct XNA type (`Compass`); FNA has no reference material for this namespace
- Main related tests: not independently located in this pass (Android-only code; no host test seam reaches it directly per the `.cpp`'s own comment)

## Purpose
Declares the Android native `Compass` backend: fuses `ASENSOR_TYPE_ROTATION_VECTOR` (heading) with `ASENSOR_TYPE_MAGNETIC_FIELD` (raw vector + accuracy) via two independent `AndroidSensorBridge` instances.

## Executive Verdict
Correct. Entirely and consistently `#ifdef __ANDROID__`-gated (both header and `.cpp`), matching `AndroidMotionBackend`'s identical discipline. The freshness-tracking fields (`rotationVectorTimestamp_`/`magneticFieldTimestamp_`/`maxSampleSkew_`) correctly guard against one of two fused streams silently going stale while the other keeps delivering — see the `.cpp` report for confirmation this is actually enforced in `PublishReading()`.

## Checklist Results
- `stateMutex_` guards every field a background-thread callback writes and `PublishReading()`/public methods read — consistent with this shard's established per-class mutex discipline (`AndroidMotionBackend`, `AndroidSensorBridge::Impl`).
- Correctly non-copyable (`= delete` on copy ctor/assignment) — appropriate given it owns two `AndroidSensorBridge` instances with their own non-trivial lifetimes.

## Detailed Findings
None.

## Cross-File Observations
See `src/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.cpp.audit.md` for confirmation that `PublishReading()` correctly implements the freshness check this header's doc comments describe, and that a genuine reentrancy hazard (a `Calibrate` callback destroying `this` before a subsequent use) was already found and fixed in a prior pass (Task COMPASS-008), verified still correctly ordered here (`PublishReading()` before `calibrationCallback()`, never the reverse).

## Missing or Weak Tests
Android-only code with no host-side test seam reaching this exact class, per its own `.cpp`'s comment — an accepted, disclosed testing gap for this platform-specific layer, not a silent one.

## Positive Findings
Clean separation between the OS-fused rotation-vector-derived heading (used for `MagneticHeading`) and the raw magnetic-field vector/accuracy (used for `MagnetometerReading`/`HeadingAccuracy`), matching this class's own doc comment's explicit statement that it never fakes a heading from the magnetometer alone.

## Final Assessment
No findings.
