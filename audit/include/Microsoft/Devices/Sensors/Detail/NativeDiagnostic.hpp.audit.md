# Audit: include/Microsoft/Devices/Sensors/Detail/NativeDiagnostic.hpp

## Metadata
- Source file: `include/Microsoft/Devices/Sensors/Detail/NativeDiagnostic.hpp` (94 lines)
- Audit status: AUDITED (full read)
- Subsystem: `microsoft-devices` shard
- File type: C++ header
- XNA/FNA relevance: NOXNA internal diagnostics facility, no FNA/WP7 equivalent
- Main related tests: not independently located in this pass

## Purpose
Process-wide sink recording native-call-failure/swallowed-exception events (backend, operation, native code/message, device id, timestamp, severity) for observability across every sensor/haptic backend.

## Executive Verdict
Correct, well-designed. `NativeDiagnosticSeverity`'s three-value scale (Info/Warning/Error) has precisely distinct, non-overlapping definitions — `Info` for "an equivalent, more specific error signal already exists elsewhere," `Warning` for "intentionally ignored, no corrective action exists," `Error` for "leaves this backend's own internal state degraded" — a genuinely useful triage signal, not three vaguely-similar levels.

## Checklist Results
- `Record()`'s doc comment explicitly requires it "never throws," correctly reflecting the real constraint that callers include C callback boundaries (SDL event filters, Android NDK sensor callbacks) where a C++ exception escaping would be undefined behavior at best.
- Every test-only hook (`GetRecordCountForTesting`/`GetLastRecordForTesting`/`SetCallbackForTesting`/`ResetForTesting`) is clearly documented as process-wide, not per-test state, with an explicit instruction that tests must call `ResetForTesting()` in `SetUp()`/`TearDown()` — a real, easy-to-miss test-isolation hazard called out proactively.

## Detailed Findings
None.

## Cross-File Observations
Consumed by `Detail::SdlSensorSubsystem<TSensor>` (audited separately) for exactly the DEVPERF-005-described purpose (stale-sensor-disconnect Info records, swallowed-dispatch-exception Warning records) — confirmed consistent field usage between the two files.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The `noexcept`-at-the-boundary design and the explicit three-tier severity taxonomy are both genuinely useful, well-thought-out additions for a cross-platform native-interop diagnostics facility.

## Final Assessment
No findings.
