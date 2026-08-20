# Audit: include/Microsoft/Devices/Sensors/CompassReading.hpp

## Metadata
- Source file: `include/Microsoft/Devices/Sensors/CompassReading.hpp` (197 lines)
- Audit status: AUDITED (full read)
- Subsystem: `microsoft-devices` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA has no reference material for this namespace (WP7-only API, never implemented in desktop FNA)
- Main related tests: not independently located in this pass

## Purpose
Represents one compass reading: heading accuracy, magnetic/true heading, raw magnetometer vector,
timestamp.

## Executive Verdict
Correct, and honestly discloses a real, unavoidable functional limitation:
`getTrueHeadingProperty()`'s doc comment (Task COMP2-008) states every backend in this codebase
currently reports the same value as `MagneticHeadingProperty`, since a genuine true-north
correction requires magnetic declination (which needs geographic location data this codebase's
`Microsoft::Devices::Sensors` does not implement) — explicitly framed as "never fabricates an
assumed declination... the most accurate honest answer available," not silently presented as a
real distinct value.

## Checklist Results
- Setters are correctly `private` with `friend class Compass;`, matching the real WP7 `internal
  set` visibility as closely as C++ allows, consistent with `AccelerometerReading`'s identical
  pattern.
- `operator==`/`ToString()`/`GetHashCode()` are all correctly `NOXNA`-tagged with the same
  MSDN-page-citation discipline as `AccelerometerReading`'s equivalents.

## Detailed Findings
None.

## Cross-File Observations
`getTrueHeadingProperty()`'s "identical to MagneticHeading, pending real declination data"
limitation is consistent with `Compass::Start()`'s own backend-callback code (audited separately),
which indeed only ever computes/publishes a single heading value from the native backend.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The `TrueHeading`-equals-`MagneticHeading` limitation is one of the most honestly and precisely
disclosed "known, real functional gap" write-ups encountered in this entire audit — explains
exactly why (missing location API), what it would take to fix, and where that's documented
(`docs/location-future-plans/plan.md`), rather than a vague "not yet implemented" note.

## Final Assessment
No findings.
