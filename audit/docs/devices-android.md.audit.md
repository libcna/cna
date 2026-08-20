# Audit: docs/devices-android.md

## Metadata
- Source file: `docs/devices-android.md` (165 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown consolidated reference
- XNA/FNA relevance: `Microsoft::Devices`/`Microsoft::Devices::Sensors` on Android specifically

## Purpose
Consolidates every Android-specific decision across `plans/plan_devices.md`'s Phases 2/3/6/7/8/9 in one
place: vibration (no native bridge needed), sensors (pure NDK native), permissions/manifest,
build integration, emulator limitations, and what's still unimplemented.

## Executive Verdict
A model example of "correct a prior stale claim explicitly rather than silently editing it away": the
"What is still not implemented on Android" section contains two dated corrections (2026-07-18) that
each explicitly quote what the line "previously claimed," state that it "was stale/false," and give
the corrected current status with a citation (`Task MOTION-012`, `.github/workflows/devices-tests.yml`).
This is a substantially more transparent self-correction style than most sibling documents' silent
status-banner updates.

## Checklist Results
- The `StartLeftRight()` Android single-actuator-blend finding (`total = large*0.6 + small*0.4`) is
  cross-referenced consistently in `docs/devices-hardware-checklist.md` (Section 4a) and
  `docs/devices-build.md` (Section 5.1's iOS planning note, which explicitly reuses "the identical
  blend weighting" for consistency) — a genuinely coherent piece of cross-document engineering
  knowledge, not restated inconsistently in different places.
- The corrected `Motion` remap claim ("Task MOTION-012... applies the same landscape remap... to
  Gravity/DeviceAcceleration/DeviceRotationRate... only unverified on real hardware") is consistent
  with `docs/devices-hardware-checklist.md` Section 8's own account of the identical
  `MOTION-012`/`ACCEL-008` decision chain — no drift between the two documents' description of the
  same fix.
- The corrected CI claim ("a GitHub Actions workflow... now exists... What remains genuinely
  unconfirmed is whether that workflow has actually run green on a real GitHub-hosted runner") is
  consistent with `docs/devices-build.md`'s own Section 8 ("This CI job has not yet actually executed
  on GitHub Actions as of this writing") — same honest boundary stated in both places.

## Detailed Findings
None — both self-corrections are internally consistent with the sibling documents they reference, and
no new discrepancy was found.

## Cross-File Observations
This document's re-verification methodology note for `VIB-003` ("re-reading the same source files
with fresh eyes rather than trusting the prior pass's conclusion unchecked") is exactly the discipline
this audit session itself has had to apply repeatedly (re-verifying claims against current source
rather than trusting prior summaries) — a good example of the target codebase's own documentation
culture matching this audit's own methodology.

## Missing or Weak Tests
N/A — a consolidated reference document; test coverage claims within it (e.g. `AndroidSensorOrientationTests.cpp`)
are cross-verified via the sibling `devices-hardware-checklist.md`/`devices-api-coverage.md` documents
and this session's own `tests-microsoft-devices` shard audit, with no contradiction found.

## Positive Findings
The explicit "Devices actually tested: the `Medium_Phone` Android emulator image only... no physical
Android device has ever been used" disclosure, repeated consistently across this document and its
siblings, is exactly the honest verification-boundary discipline this entire `Microsoft::Devices`
documentation cluster maintains well.

## Final Assessment
No findings. A well-corrected, cross-consistent consolidated reference document.
