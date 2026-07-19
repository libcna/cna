# Audit: docs/devices-hardware-checklist.md

## Metadata
- Source file: `docs/devices-hardware-checklist.md` (899 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown manual hardware-verification checklist
- XNA/FNA relevance: `Microsoft::Devices`/`Microsoft::Devices::Sensors` physical-hardware verification

## Purpose
The authoritative "what remains genuinely unverified without real hardware" checklist for
`Microsoft::Devices` — accelerometer/gyroscope axis correctness, vibration (phone motor, dual-motor,
gamepad-exclusion), Android sensor-bridge lifecycle/probe-cache/backpressure, Compass/Motion Android
backends, with an explicit "Phase 9 execution results" section scoring exactly which of 6 hardware
cases were actually attempted (1 of 6, honestly).

## Executive Verdict
The single most exhaustive and disciplined "here is exactly what we could and couldn't verify without
real hardware, and why" document in this entire corpus. Its "Net result: 1 of 6 cases verified... 5 of
6 remain genuinely unverified, each for a concrete, confirmed reason" framing, followed by per-item
sections that each independently restate "Status: NOT RUN — hardware validation open" rather than
letting a reader assume anything beyond what was actually checked, is exemplary.

## Checklist Results
- The `ACCEL-008` decision record (keep the Android landscape remap as a documented CNA-only
  deviation from real WP7 behavior, with an opt-out) is consistent, word-for-word in substance, with
  `docs/devices-android.md`'s own account of the same decision.
- Section 4a/4b's (`VIB2-003`/`VIB2-004`) "Why this needs real hardware" explanations are precise about
  exactly which code branch is unreachable in this dev container (`OpenFirstHapticDevice()` always
  returns `nullptr` here) rather than a vague "no hardware available" — a specific, falsifiable claim
  about test-environment limitations.
- Cross-checked the `Dispose(bool)` visibility topic indirectly: this document does not itself discuss
  `Dispose(bool)` visibility (out of its scope — a hardware-verification checklist, not an API-surface
  reference), so no contradiction with this session's own confirmed MEDIUM finding on that topic
  (correctly the concern of `docs/devices-api-coverage.md` instead, see that file's own audit report).
- The "Vibration validation matrix" (Section 5a) consolidating Sections 3-5 into one table is a good
  redundancy-avoiding organizational choice, consistent with the rest of the document's discipline.

## Detailed Findings
None — no internal inconsistency, and every cross-checkable claim (Android remap decisions, the
single-actuator blend formula, CI/emulator status) is consistent with the sibling `devices-*.md`
documents that also describe them.

## Cross-File Observations
- Directly consistent with `docs/devices-android.md`'s Section 8 emulator-limitations list (both
  independently but consistently describe: no real vibration motor on emulators, injected sensor
  values prove pipeline delivery not physical correctness, no virtual rotation-vector sensor found in
  this emulator's console command set).
- This document's Section 9 ("Emulator limitations for Devices testing") and `docs/devices-android.md`'s
  own "Emulator limitations specific to this namespace" section cover overlapping ground with no
  contradiction — a reasonable amount of intentional cross-referencing/restating rather than one
  silently diverging from the other over time.
- Consistent with `docs/devices-event-contract.md`'s own account of `Compass`/`Motion`'s Android-only
  exception-handling gap (this document's Section 6/7/8 items all correctly note the underlying
  Android-only code paths are "confirmed by code review... never actually run" — the same honest
  boundary `devices-event-contract.md` states for the identical code).

## Missing or Weak Tests
The document itself is the record of what automated tests *cannot* cover (physical hardware behavior);
it is precise about what *is* host-testable within each Android-only feature (e.g. Section 6c's rate-set
diagnostics: "both getters' plumbing and their sensible defaults... are host-tested (4 new tests)" vs.
what needs a real device (whether a real sensor driver actually rejects a rate) — a clear, itemized
test/non-test boundary throughout.

## Positive Findings
Section 8a's `MOT2-003` entry is a particularly strong example of disciplined scope control: it
explicitly declines to implement a larger, "required work"-requested redesign (bounded per-source
sample queues with measured-skew interpolation) because doing so responsibly would require empirical
jitter measurement on real hardware first — "guessing one would not actually satisfy the requirement's
own wording" — and instead ships only the safely-addable diagnostic counter, with a clear account of
why the larger piece was deferred rather than rushed.

## Final Assessment
No findings. The most exhaustive, most honestly-bounded hardware-verification checklist in this
entire `docs` corpus.
