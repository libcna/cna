# Audit: include/Microsoft/Devices/Sensors/SensorFailedException.hpp

## Metadata
- Source file: `include/Microsoft/Devices/Sensors/SensorFailedException.hpp` (45 lines)
- Audit status: AUDITED (full read)
- Subsystem: `microsoft-devices` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA has no reference material for this namespace (WP7-only API, never implemented in desktop FNA)
- Main related tests: not independently located in this pass

## Purpose
Base exception type for sensor operation failures; carries an optional platform-specific error
code (`ErrorId`).

## Executive Verdict
Correct. Derives from `System::Exception` (not a raw `std::` exception) — consistent with this
project's established exception-type convention, in contrast to the raw-`std::`-exception pattern
flagged repeatedly as a cross-cutting issue in other shards this session.

## Checklist Results
- All three constructor overloads (default, message-only, message+errorId) are present and
  correctly delegate to `System::Exception`'s own constructors.
- `getErrorIdProperty()` correctly marked `noexcept`.

## Detailed Findings
None.

## Cross-File Observations
`AccelerometerFailedException` (audited separately) correctly derives from this type, forming a
sensible two-level exception hierarchy (`SensorFailedException` → `AccelerometerFailedException`).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Correctly uses `System::Exception` rather than a raw `std::` exception — a positive contrast to
the recurring exception-type pattern flagged elsewhere in this audit.

## Final Assessment
No findings.
