# Audit: examples/demo_devices/android/.../app/jni/src/DevicesDemo.cpp

## Metadata
- Source file: `examples/demo_devices/android/com.openeggbert.cna.demodevices/app/jni/src/DevicesDemo.cpp` (~234 lines)
- Audit status: AUDITED (full read, direct `diff` against `examples/demo_devices/src/DevicesDemo.cpp`)
- Subsystem: `examples-demo_devices` shard
- File type: Android jni mirror copy of the desktop demo implementation
- XNA/FNA relevance: same as the desktop copy
- Main related tests: none

## Purpose
Intended mirror copy of `examples/demo_devices/src/DevicesDemo.cpp` for the Android jni CMake build
target.

## Executive Verdict
Confirmed stale (MEDIUM, fully written up in `examples/demo_devices/src/DevicesDemo.cpp.audit.md`):
missing `HandleTimeBetweenUpdatesInput` entirely (not declared, not called from `Update()`), and
`UpdateWindowTitle()` prints a strictly smaller field set at `precision(2)` instead of the desktop
copy's `precision(3)`. Everything this file DOES contain is otherwise correct and functionally
equivalent to the desktop copy's pre-Task-DEMO-001 state.

## Checklist Results
- Constructor sensor-start try/catch pattern, `HandleVibrationInput`, `HandleSensorToggleInput`,
  `Draw()`/drawing-primitive functions are all identical to the desktop copy (confirmed via `diff` —
  the only deltas are the Task DEMO-001 additions).
- No `Dispose(bool)` call here either — same conclusion as the desktop copy: this demo never
  exploits the `microsoft-devices` shard's confirmed `Dispose(bool)` public-visibility finding.

## Detailed Findings
See `examples/demo_devices/src/DevicesDemo.cpp.audit.md` for the full MEDIUM write-up.

## Cross-File Observations
Same conclusion as the sibling `.hpp` report: this is a stale-copy drift, not an intentional
Android-specific behavior difference — Android has no title bar, so the missing window-title fields
have zero practical effect there, but the missing Numpad +/- rate-adjustment feature is a real,
if minor, functional gap on this platform specifically (assuming the Android build maps a physical
+/- input to those keys at all, which was not verified in this pass).

## Missing or Weak Tests
N/A — manual-verification demo.

## Positive Findings
N/A (see desktop copy's report).

## Final Assessment
Confirmed stale relative to `examples/demo_devices/src/DevicesDemo.cpp` (missing Task DEMO-001);
not independently broken.
