# Audit: docs/devices-event-contract.md

## Metadata
- Source file: `docs/devices-event-contract.md` (241 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown formal contract document
- XNA/FNA relevance: `Microsoft::Devices::Sensors`'s 5 `EventHandler<T>` events' dispatch semantics

## Purpose
The single explicit statement of what CNA promises about event dispatch (thread identity, ordering,
handler-list mutation during dispatch, reentrancy, destruction during dispatch, exception semantics)
across all 5 sensor-related events, distinguishing WP7-inherited baseline behavior from CNA-only
policy decisions where the real API is silent.

## Executive Verdict
An exceptionally rigorous concurrency-contract document — genuinely rare in that it doesn't just
assert guarantees, it names the specific test proving each one, and explicitly flags a real,
currently-unresolved gap in its own team's stated policy: `Compass`/`Motion`'s exception-swallowing at
the `AndroidSensorBridge::Run()` boundary does **not yet match** the decided project-wide log-and-continue
policy (unlike the `Accelerometer`/`Gyroscope` SDL path, upgraded by `SDLCORE-009`), and further notes
the existing in-code comment claiming parity with the SDL path is now **stale** ("accurate when
written but is now stale... no longer is identical").

## Checklist Results
- Cross-checked the "destruction during dispatch" section's claims about
  `Detail::SdlSensorSubsystem<TSensor>::DispatchToInstances()` snapshotting/revalidating registrations
  against this session's own `microsoft-devices`/`cna-devices` shard audits, which independently
  characterized this subsystem's polling-thread/event-callback design as "exceptionally mature" — no
  contradiction, consistent characterization from two independent angles.
- The `Compass`/`Motion` generation-guard pattern (`Detail::SensorOwnerControlBlock`, a `shared_ptr`
  to a control block rather than a raw `this` capture) is a sound, standard technique for this exact
  class of dangling-callback problem; consistent with this session's own review of the sensor
  lifecycle design.
- The explicitly-disclosed gap (Android exception path not yet upgraded to structured
  logging/counting) is precise and actionable — it names the exact file (`AndroidSensorBridge.cpp`),
  the exact call site, and the exact follow-up task (`DEVPERF-005`) that will close it, rather than a
  vague "needs work" note.

## Detailed Findings
None against this document's own claims — its self-disclosed gap is exactly the kind of honest,
specific "not yet done" note this audit values, not a defect in the document itself.

## Cross-File Observations
This document's exception-policy gap (`Compass`/`Motion`'s bare `catch (...) {}` with no logging) is a
genuine, currently-open production-code finding this document itself surfaces — worth cross-referencing
in `AUDIT_CROSS_CUTTING_FINDINGS.md` as an already-self-disclosed gap (not something this audit
"discovered," but worth citing since it corroborates this project's own honest self-tracking) if a
future pass audits `AndroidSensorBridge.cpp` directly.

## Missing or Weak Tests
The document itself names precisely which guarantees are tested (one test per sensor class for
reentrancy/handler-mutation-during-dispatch/self-destruction) vs. which are "not independently
tested... judged low-value repetition, not a gap" (`ReadingChanged`/`TimeBetweenUpdatesChanged`,
since they share the identical, already-proven dispatch mechanism) — a reasoned, not merely absent,
test-scope boundary.

## Positive Findings
The distinction drawn throughout between "WP7 baseline" (inherited .NET multicast-delegate semantics)
and "CNA policy" (a decision CNA had to make because the real API is silent, e.g. destruction-during-
dispatch safety, which has no managed-runtime equivalent) is a clear, well-organized way to keep a
reader from confusing "this matches real XNA" with "this is a reasonable CNA-only design choice" —
exactly the distinction this entire audit has been trying to preserve when writing up NOXNA findings.

## Final Assessment
No findings against the document's own accuracy. It transparently discloses one real, still-open
production-code gap (Android exception-logging parity, tracked as `DEVPERF-005`) — a positive
disclosure, not a documentation defect.
