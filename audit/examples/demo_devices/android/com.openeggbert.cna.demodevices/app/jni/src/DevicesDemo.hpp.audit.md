# Audit: examples/demo_devices/android/.../app/jni/src/DevicesDemo.hpp

## Metadata
- Source file: `examples/demo_devices/android/com.openeggbert.cna.demodevices/app/jni/src/DevicesDemo.hpp` (~118 lines)
- Audit status: AUDITED (full read, direct `diff` against `examples/demo_devices/src/DevicesDemo.hpp`)
- Subsystem: `examples-demo_devices` shard
- File type: Android jni mirror copy of the desktop demo header
- XNA/FNA relevance: same as the desktop copy (`Microsoft::Devices::Sensors`/`VibrateController`)
- Main related tests: none

## Purpose
Intended to be a mirror copy of `examples/demo_devices/src/DevicesDemo.hpp`, adapted for the Android
jni build target (compiled by the sibling `jni/src/CMakeLists.txt`, gated behind the project's
`-PBUILD_WITH_CMAKE` gradle flag — see `CMakeLists.txt.audit.md`).

## Executive Verdict
**Confirmed stale relative to the desktop copy** (already flagged in
`examples/demo_devices/src/DevicesDemo.cpp.audit.md`, MEDIUM): missing the entire Task DEMO-001
feature — no `HandleTimeBetweenUpdatesInput` declaration, no `timeBetweenUpdates_` member, no
`System::TimeSpan` include. This is otherwise a correct, working header (the demo builds and runs
without these members; they are additive functionality, not a correctness dependency), just an
out-of-date copy of a file this project clearly intends to keep mirrored.

## Checklist Results
- Every member and method this copy DOES have, matches the desktop copy's pre-Task-DEMO-001
  state exactly, character for character (confirmed via `diff` — the only differences are the
  DEMO-001 additions, not an independent divergence).

## Detailed Findings
See `examples/demo_devices/src/DevicesDemo.cpp.audit.md` for the full write-up of this MEDIUM
finding (spans both `.hpp` and `.cpp` on both sides of the desktop/Android pair).

## Cross-File Observations
Confirms the finding is genuinely a stale-copy issue, not an intentional Android-specific
simplification: every member present here is a strict subset of the desktop copy, in the same
order, with identical naming and comments — consistent with "this copy was made once and never
updated since," not "these two were deliberately written to differ."

## Missing or Weak Tests
N/A — manual-verification demo header.

## Positive Findings
N/A (see desktop copy's report for genuine positive findings that also apply to this subset).

## Final Assessment
Confirmed stale relative to `examples/demo_devices/src/DevicesDemo.hpp` (missing Task DEMO-001).
Not independently broken — this is the disclosed drift, not a new defect.
