# Audit: include/Microsoft/Devices/Sensors/AccelerometerFailedException.hpp

## Metadata
- Source file: `include/Microsoft/Devices/Sensors/AccelerometerFailedException.hpp` (33 lines)
- Audit status: AUDITED (full read)
- Subsystem: `microsoft-devices` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA has no reference material for this namespace (WP7-only API, never implemented in desktop FNA)
- Main related tests: not independently located in this pass

## Purpose
Exception thrown when an accelerometer-specific operation fails; derives from
`SensorFailedException`.

## Executive Verdict
Correct, matches the real WP7 exception hierarchy (`AccelerometerFailedException : SensorFailedException`).

## Checklist Results
All three constructor overloads correctly mirror the base class's own set.

## Detailed Findings
None.

## Cross-File Observations
Thrown by `Accelerometer::Start()` (audited separately) for every real failure path (max-instance
limit exceeded, already-started, subsystem init failure, no default sensor found, event-watch
registration failure) — consistently used, not just declared.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Minimal, correct.

## Final Assessment
No findings.
