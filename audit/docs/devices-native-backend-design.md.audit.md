# Audit: docs/devices-native-backend-design.md

## Metadata
- Source file: `docs/devices-native-backend-design.md` (360 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (architecture/design doc, updated-in-place convention)
- XNA/FNA relevance: describes `Microsoft::Devices::Sensors` (`Compass`/`Motion`) native-backend
  architecture — this session's `microsoft-devices` shard already fully audited the actual C++
  source this doc describes
- Related audit: `audit/include/Microsoft/Devices/Sensors/` (54-file `microsoft-devices` shard,
  fully audited this session, one MEDIUM finding: `Dispose(bool)` public not protected on all 4
  sensor classes)

## Purpose
Architectural reference for why `Compass`/`Motion` remain permanent `SensorState::NotSupported`
stubs on desktop (no SDL3 compass/fused-motion API), and the real, implemented Android NDK native
backend (`Detail::AndroidSensorBridge`, `AndroidCompassBackend`, `AndroidMotionBackend`) built for
them, plus the still-unimplemented iOS sketch.

## Executive Verdict
Accurate and current. This is one of the best-maintained documents in the shard: it explicitly
follows an "update in place, don't treat as frozen" convention and visibly practices it — the
"Purpose and status" section at the top is itself a running amendment log (2026-07-05 Android
landing, 2026-07-16 Task MOTION-011 interface-shape change), not a stale snapshot. Its description
of the Android backend matches this session's own `microsoft-devices` shard audit: real, compiling,
cross-compile-verified code wired via `#if defined(__ANDROID__)`, with `Compass`/`Motion` remaining
honest desktop stubs. No contradiction found with the shard's one confirmed finding (`Dispose(bool)`
access-specifier gap), which is an orthogonal C++ visibility detail this design doc does not claim
anything about either way.

## Checklist Results
- The claim "`Compass`/`Motion`... hardcodes `return false`... deliberate... re-confirmed unchanged
  across Phases 6, 7, 8, and 9's audits" matches the `microsoft-devices` shard's own direct
  confirmation this session that these two classes are still honest, permanent stubs on desktop.
- The Android Motion bug-fix note ("one real bug found and fixed while implementing": both
  `TYPE_GRAVITY`/`TYPE_LINEAR_ACCELERATION` report m/s² but `MotionReading.hpp` documents "in g",
  fixed via `StandardGravity` division) is a specific, plausible, well-reasoned unit-conversion fix
  consistent with this project's general engineering discipline observed elsewhere this session.
- The "Coordinate-system remap status" section's honest admission that "what remains unverified is
  the remap's sign/axis correctness on real hardware" (not whether a remap should exist) is
  consistent with `docs/devices-hardware-checklist.md`'s own still-open §1/§2/§8 hardware-gated
  items (not audited in this batch, but cross-referenced here and in
  `docs/devices_sensor_hardware_qa_template.md`, audited alongside this file).
- Permission/lifecycle notes (Android needs no runtime sensor permission; iOS Compass needs location
  permission for heading; `Dispose()`/destruction must stop listeners, reusing `SensorBase<T>`'s
  existing disposal machinery) are precise, sourced claims (cites specific iOS API quirks), not
  vague generalities.

## Detailed Findings
None. No stale claims found; the document's own update-in-place discipline appears to be genuinely
followed, not just declared.

## Cross-File Observations
Cross-references `../audit_devices.md` (an external audit, not this session's `audit/` tree) for two
specific findings (`DEV-AUD-002`, `DEV-AUD-003`) that this doc says were used to justify closing two
real gaps (`Motion::Calibrate`'s permanent no-op; the Android coordinate-remap question) — both
citations are specific and traceable, not vague "an audit found issues" hand-waving.

## Missing or Weak Tests
N/A — this is a design document, not source under test. Test counts for the features it describes
are cited (11 `AndroidCompassMathTests` + 6 fake-backend `CompassTests`, 9 `AndroidMotionMathTests`
+ 5 fake-backend `MotionTests`) but not independently re-verified in this pass (out of scope for a
docs audit; the `microsoft-devices` shard already covers the corresponding test files separately).

## Positive Findings
The "update in place, don't treat as frozen" convention, visibly followed across multiple dated
amendments in a single running document rather than superseded-and-abandoned copies, is a
genuinely good documentation practice — it keeps one authoritative file current instead of
accumulating stale duplicates (a problem this audit found repeatedly elsewhere in the `docs/`
shard, e.g. `docs/coverage.md` being superseded by `docs/graphics-backend-feature-matrix.md`, or
`docs/easygl_bugs.md`'s own self-flagged staleness).

## Final Assessment
No findings. Accurate, current, and a model example of the "update in place" documentation
discipline this project aspires to elsewhere.
