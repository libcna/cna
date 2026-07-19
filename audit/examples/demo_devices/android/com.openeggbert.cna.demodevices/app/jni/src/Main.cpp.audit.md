# Audit: examples/demo_devices/android/.../app/jni/src/Main.cpp

## Metadata
- Source file: `examples/demo_devices/android/com.openeggbert.cna.demodevices/app/jni/src/Main.cpp` (18 lines)
- Audit status: AUDITED (full read; confirmed byte-for-byte identical to the desktop copy via `diff`)
- Subsystem: `examples-demo_devices` shard
- File type: Android jni mirror copy of the demo entry point
- XNA/FNA relevance: none
- Main related tests: none

## Purpose
Android jni build's copy of the demo entry point.

## Executive Verdict
Correct — byte-for-byte identical to `examples/demo_devices/src/Main.cpp` (confirmed via `diff`).
The one file in this demo's desktop/Android pair that has NOT drifted.

## Checklist Results
See `examples/demo_devices/src/Main.cpp.audit.md` — identical content, identical conclusion.

## Detailed Findings
None.

## Cross-File Observations
Contrast with the sibling `DevicesDemo.cpp`/`.hpp` jni copies in this same directory, both confirmed
stale relative to their desktop counterparts.

## Missing or Weak Tests
N/A.

## Positive Findings
Kept correctly in sync, unlike its neighbors.

## Final Assessment
No findings.
