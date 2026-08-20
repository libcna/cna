# Audit: docs/location-future-plans/plan.md

## Metadata
- Source file: `docs/location-future-plans/plan.md` (118 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (a deliberate not-implemented placeholder/guard-rail doc)
- Cross-references: `microsoft-devices` shard audit (confirmed `Microsoft::Devices::Sensors`
  correctly has no location member)

## Purpose
Explicitly documents that GPS/location support is NOT implemented and does not belong in
`Microsoft::Devices::Sensors`, sketches where it would live if ever built, and updates
`Compass::TrueHeading`'s reasoning for not consuming a declination source today.

## Executive Verdict
A correct, unusually purposeful "anti-pattern guard rail" document — written specifically to
prevent a plausible-looking future mistake (bolting location onto `Sensors`) rather than to describe
existing functionality. Consistent with this audit's own confirmation (in the `microsoft-devices`
shard) that `Accelerometer`/`Compass`/`Gyroscope`/`Motion` correctly have no location member.

## Checklist Results
- The core claim (location is a separate real WP7 assembly/namespace, `System.Device.Location` vs.
  `Microsoft.Devices.dll`) is stated as a definite architectural fact and used to justify why no
  code exists — consistent with this audit's own `microsoft-devices` shard review, which found no
  location-related member anywhere in the 4 sensor classes.
- The 2026-07-06 "Compass::TrueHeading" re-confirmation section is dated and explicitly framed as
  "re-checked rather than silently re-asserted" — a good practice this audit has seen elsewhere
  (e.g. `docs/model-content-pipeline-support.md`'s dated update note) for keeping a living decision
  current without needing to rewrite the whole document each time.
- Explicitly labels its own API sketch as unverified ("not independently re-verified against an
  archived MSDN page... treat these as a starting sketch, re-verify before actually implementing") —
  appropriately hedged, not presented as authoritative.

## Detailed Findings
None. Nothing in this document is currently implemented, so there is no code to be inconsistent
with; the document's own internal claims are consistent and appropriately hedged.

## Cross-File Observations
Directly consistent with the `microsoft-devices` shard audit (completed earlier in this session),
which confirmed all 4 sensor classes have no location-related public API surface — this document's
central architectural claim (location is out of scope for `Sensors`) is empirically true of the
current codebase.

## Missing or Weak Tests
N/A — no code exists for this feature; not applicable.

## Positive Findings
This is a genuinely valuable kind of documentation this audit doesn't see often: a deliberate
"do not implement this the easy-but-wrong way" guard rail, written proactively rather than after a
mistake was made. The dependency-injection sketch for `Compass`'s hypothetical future consumption
of a declination source (`Detail::IDeclinationSource`, mirroring the existing
`SetBackendForTesting()` seam-injection pattern) is a well-reasoned architectural note that would
save real design time if this feature is ever scoped.

## Final Assessment
No findings. A correct, well-reasoned, and unusually purposeful placeholder document.
