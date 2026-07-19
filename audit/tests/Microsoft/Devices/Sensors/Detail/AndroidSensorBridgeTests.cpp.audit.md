# Audit: tests/Microsoft/Devices/Sensors/Detail/AndroidSensorBridgeTests.cpp

## Metadata
- Source file: `tests/Microsoft/Devices/Sensors/Detail/AndroidSensorBridgeTests.cpp` (227 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-microsoft-devices` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Detail::AndroidSensorBridge`/`ConvertTimeBetweenUpdatesToSensorEventRateMicroseconds()`/
  `GetValueCountForAndroidSensorType()` (WP7-only API, no FNA reference; cross-checked against the
  vendored Android NDK's `android/sensor.h`)
- Main related tests: N/A (this IS a test file)

## Purpose
Tests the pure microsecond-conversion helper (including zero/negative/overflow clamping to
`int32_t`), the NDK sensor-type-to-value-count mapping, and the bridge's own inert no-op behavior on
this non-Android host.

## Executive Verdict
Correct and appropriately honest about its own coverage boundary: every test explicitly
acknowledges (in-comment) that the real Android worker-thread/JNI code path is `#ifdef
__ANDROID__`-gated and cannot run on this host — verified separately via cross-compilation, not
runtime-exercised here. What IS host-testable (the pure conversion/mapping functions, and the
on-this-platform-always-false/no-op contract of every public method) is fully covered.

## Checklist Results
- `MaxValueTimeSpanClampsToInt32Max`/`HugeButNotMaxTimeSpanClampsToInt32Max` correctly test the
  explicit clamp exists and triggers well before `TimeSpan`'s own representable limit, not only at
  the extreme end — a genuine UB-prevention test (`static_cast`-ing an out-of-range double to
  `int32_t` is undefined behavior, not saturating truncation).
- `GetValueCountForAndroidSensorType`'s test values are cross-checked against the vendored NDK's
  `android/sensor.h` directly per the file's own comment (Task ANDROID-BRIDGE-001), not guessed.
- Every "on non-Android platform" test (`IsAvailableIsFalseOnNonAndroidPlatform`,
  `StartReturnsFalseOnNonAndroidPlatform`, etc.) correctly confirms the inert-no-op contract this
  desktop build provides, distinctly from (and not claiming to prove) the real Android behavior.

## Detailed Findings
None.

## Cross-File Observations
None.

## Missing or Weak Tests
The real Android worker-thread/drain-cap/event-rate logic is verified only via a separate
cross-compile + `llvm-nm` symbol check (per the file's own comments), never runtime-exercised in
this pass or (as far as this audit can confirm) anywhere in CI — an honestly-disclosed, real
coverage gap for the actual hardware path, consistent with this whole shard's documented
Android-hardware-verification limitations.

## Positive Findings
Precise, well-reasoned UB-prevention tests for the `int32_t` clamp, and honest, consistent
disclosure of what remains unverified on this platform.

## Final Assessment
No findings.
