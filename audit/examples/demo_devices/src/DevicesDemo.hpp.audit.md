# Audit: examples/demo_devices/src/DevicesDemo.hpp

## Metadata
- Source file: `examples/demo_devices/src/DevicesDemo.hpp` (132 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-demo_devices` shard
- File type: example demo header (`Game` subclass)
- XNA/FNA relevance: exercises `Microsoft::Devices::Sensors::{Accelerometer,Compass,Gyroscope,Motion}`
  and `Microsoft::Devices::VibrateController`, all already fully audited in the `microsoft-devices` shard
- Main related tests: none (manual/hardware-verification demo, per `docs/devices-hardware-checklist.md`)

## Purpose
Declares a rectangle-only (no SpriteFont/Content dependency), keyboard-driven manual-verification
screen for all four `Microsoft::Devices::Sensors` classes plus `VibrateController`, drawing
supported/state/event-flash indicators and signed/unsigned bars per sensor.

## Executive Verdict
Correct and clearly documented. See `DevicesDemo.cpp.audit.md` for the confirmed Android-jni-copy
staleness finding this header is one half of (the Android copy is missing this header's
`HandleTimeBetweenUpdatesInput`/`timeBetweenUpdates_` member entirely).

## Checklist Results
- `GetTypeNameHPP()` macro used correctly (per this project's `System::Object::GetTypeName()`
  override convention).
- Member order matches declaration order used consistently by drawing/update code in the `.cpp`.
- `timeBetweenUpdates_`'s doc comment (lines 122-131) correctly cross-references the specific
  confirmed defect IDs it manually exercises (SENSORBASE-001/ACCEL-005/GYRO-004/MOTION-008/
  SENSORBASE-002) — precise, falsifiable claims, not vague.

## Detailed Findings
None specific to this header (see sibling `.cpp` report for the cross-file Android-copy staleness
finding, which spans both `.hpp` and `.cpp`).

## Cross-File Observations
See `DevicesDemo.cpp.audit.md` — this header's `HandleTimeBetweenUpdatesInput` declaration and
`timeBetweenUpdates_`/`System::TimeSpan` include are both entirely absent from the Android jni
mirror copy at `android/.../app/jni/src/DevicesDemo.hpp`.

## Missing or Weak Tests
This is a manual-verification demo by design (no SpriteFont/Content dependency, per its own header
comment) — not unit-testable in the normal sense; N/A.

## Positive Findings
The `timeBetweenUpdates_` comment precisely documents which confirmed defects a human tester is
meant to be able to manually re-verify using the Numpad +/- controls — a genuinely useful bridge
between an interactive demo and a specific set of tracked bug IDs.

## Final Assessment
No findings intrinsic to this file. See `DevicesDemo.cpp.audit.md` for the one real, confirmed
finding spanning this header and its Android jni counterpart.
