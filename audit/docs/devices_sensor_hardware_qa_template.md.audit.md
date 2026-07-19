# Audit: docs/devices_sensor_hardware_qa_template.md

## Metadata
- Source file: `docs/devices_sensor_hardware_qa_template.md` (158 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (blank report template)
- XNA/FNA relevance: companion template to `docs/devices-hardware-checklist.md` (not itself
  audited in this batch) for recording real-hardware sensor/vibration QA sessions
- Related audit: `microsoft-devices` shard (this session)

## Purpose
A copy-per-session template for recording hardware QA results (accelerometer/gyroscope axis
sign, vibration, Compass/Motion Android backend behavior) side-by-side with
`docs/devices-hardware-checklist.md`'s numbered sections.

## Executive Verdict
Sound as a template — it is explicitly not meant to contain results itself (a blank form), so
there is nothing to fact-check against current source the way a completed report would need. Its
structure (session metadata table, per-section expected/observed/pass-fail rows,
explicit "N/A — why" convention for inapplicable sections) is well-designed for its stated purpose:
staying comparable across testers/devices/sessions.

## Checklist Results
- Its own "How this relates to `docs/devices-hardware-checklist.md`" section correctly scopes
  itself as a recording format, not a duplicate of the checklist's rationale or instructions —
  avoids the drift risk of two documents describing the same test steps independently.
- Section numbers (1-9) are stated to match the checklist's own section numbers exactly — could not
  be independently verified without auditing `docs/devices-hardware-checklist.md` itself (out of
  this batch's scope), but the claim is specific and checkable by a future pass.
- Section 4's note ("known limitation, not a bug to re-discover" — a phone's single vibrator always
  blends `StartLeftRight`'s two values via SDL3's own blending) correctly cross-references
  `docs/devices-android.md`'s Vibration section rather than re-asserting the claim independently.
- Section 1's flagged open question (whether the real WP7 accelerometer coordinate convention is
  identical between portrait/landscape, contradicting this codebase's own remap premise if an
  archived MSDN Magazine claim is taken literally) is presented as a genuinely open, actionable
  question rather than resolved — consistent with `docs/devices-native-backend-design.md`'s own
  parallel acknowledgment (audited alongside this file) that hardware sign/axis correctness remains
  unverified.

## Detailed Findings
None — there is no stale factual claim to find in a blank template; its only "claims" are about its
own relationship to a sibling document and its own usage convention, both self-consistent.

## Cross-File Observations
Section 7's Compass checklist item and Section 8's Motion checklist item are consistent with
`docs/devices-native-backend-design.md`'s own description of the Android Compass/Motion backends
(audited alongside this file) — e.g. "TrueHeading == MagneticHeading always (no declination
applied)" matches that document's explicit statement that true heading is deliberately left equal
to magnetic heading pending a future `System.Device.Location` layer.

## Missing or Weak Tests
N/A — a manual-QA template has no automated test coverage to assess.

## Positive Findings
The explicit "N/A — why" convention (rather than silently deleting inapplicable sections) is a
good practice that keeps "not tested" distinguishable from "tested, not applicable" across
different testers and sessions — directly useful for an audit like this one, which repeatedly had
to distinguish "not yet verified" from "verified and passing" across many other documents.

## Final Assessment
No findings. A sound, well-scoped template.
