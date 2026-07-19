# Audit: docs/demo-input-checklist.md

## Metadata
- Source file: `docs/demo-input-checklist.md` (106 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown manual-verification checklist
- XNA/FNA relevance: N/A directly — manual hardware-verification process document
- Main related tests: `cna_demo_input` (examples shard, not yet audited); references
  `GestureDetectorTests`/`SdlInputBridgeTouchGestureTests` (`tests-cna-internal`, already audited)

## Purpose
Manual, hardware-gated verification checklist for `demo_input`'s keyboard/text-input/IME/mouse/
touch/gamepad panels, cross-linked with the other 9 Input docs.

## Executive Verdict
Healthy, and unusually careful about distinguishing "the demo exercises this code path
programmatically" from "a human confirmed the physical result on real hardware" — the "Now
exercised by the demo" section explicitly states the demo builds/smoke-launches crash-free as of a
specific date, while "their visual/behavioral correctness on real hardware is still human/
hardware-gated," pointing to a separate results file rather than conflating the two.

## Checklist Results
- The final section (lines 97-106) explicitly discloses a genuine, dated gap (P7-039, found
  2026-07-17): `cna_demo_input` does not surface the standalone `CNA::Input::Joysticks`/`Sensors`/
  `Power` APIs at all, correctly distinguishing this from the already-covered gamepad-attached
  gyro/accel/rumble the demo does exercise — a precise, non-conflated scope boundary.
- That same section correctly notes these three APIs' current verification tier is real unit-test
  coverage against fake SDL backends (`SdlJoystickBackendTests.cpp`, `SensorsTests.cpp`,
  `PowerTests.cpp`) — cross-verified consistent with this session's own earlier reading of
  `tests/CNA/Input/SensorsTests.cpp`/`PowerTests.cpp` (`tests-cna-input` shard, already audited: both
  confirmed clean, dependency-injected fake-backend test files).
- Explicitly cites `CLAUDE.md`'s own "no new features beyond audit/repair/test/doc" scope rule as
  the reason for not extending the demo UI here — correctly deferring rather than scope-creeping.

## Detailed Findings
None.

## Cross-File Observations
The `SdlJoystickBackendTests.cpp`/`SensorsTests.cpp`/`PowerTests.cpp` citations are directly
cross-verifiable against this session's own `tests-cna-input` shard audit (`PowerTests.cpp.audit.md`/
`SensorsTests.cpp.audit.md`, both written earlier this session) — confirmed these files exist and
match the described fake-backend dependency-injection pattern.

## Missing or Weak Tests
Not applicable to this document itself; the P7-039 gap it discloses (no demo-level manual-
verification path for `Joysticks`/`Sensors`/`Power`) is honestly flagged as a real, currently-
unaddressed gap, not hidden.

## Positive Findings
Precise, non-conflated distinction between "demo exercises this code path" and "hardware-confirmed
correct," and honest disclosure of the P7-039 gap with correct scope deferral per `CLAUDE.md`.

## Final Assessment
No findings.
