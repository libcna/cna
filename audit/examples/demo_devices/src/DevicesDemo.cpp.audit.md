# Audit: examples/demo_devices/src/DevicesDemo.cpp

## Metadata
- Source file: `examples/demo_devices/src/DevicesDemo.cpp` (524 lines)
- Audit status: AUDITED (full read, plus a direct `diff` against the Android jni mirror copy)
- Subsystem: `examples-demo_devices` shard
- File type: example demo implementation
- XNA/FNA relevance: exercises `Microsoft::Devices::Sensors::{Accelerometer,Compass,Gyroscope,Motion}`
  and `Microsoft::Devices::VibrateController`
- Main related tests: none (manual/hardware-verification demo)

## Purpose
Implements the Devices demo: constructs all four sensors with `CurrentValueChanged` lambdas,
starts Accelerometer/Gyroscope unconditionally (Compass/Motion wrapped identically even though
they're documented as always throwing off-Android), handles vibration/sensor-toggle/rate-adjustment
keyboard input, and draws per-sensor indicator/bar sections plus a very information-dense window
title (this demo's only text-output channel, by design — no SpriteFont/Content dependency).

## Executive Verdict
The desktop source itself is correct and carefully reasoned throughout. **One real, confirmed
finding: the Android jni mirror copy of this file
(`android/com.openeggbert.cna.demodevices/app/jni/src/DevicesDemo.cpp`) has silently drifted out of
sync with this one** — it is missing the entire Task DEMO-001 feature added here on 2026-07-06
(the `HandleTimeBetweenUpdatesInput` Numpad +/- rate control, the `timeBetweenUpdates_` member, and
the corresponding window-title fields: TBU value, Compass/Motion `IsDataValid`, raw
`MagnetometerReading`/`HeadingAccuracy`, and Motion's `DeviceRotationRate`/`Attitude`). Confirmed via
direct `diff`: the jni copy is missing `HandleTimeBetweenUpdatesInput` entirely, never calls it from
`Update()`, and its `UpdateWindowTitle()` prints a strictly smaller subset of fields with
`precision(2)` instead of `precision(3)`.

## Checklist Results
- Constructor's `try { X.Start(); } catch (const System::Exception&) {}` pattern (lines 92-95) is
  applied uniformly to all four sensors and is the correct way to treat Compass/Motion's documented
  "always throws off never-magnetometer-having-SDL3-platforms" behavior as expected, not exceptional.
- `HandleSensorToggleInput`'s `accelStarted_`/`gyroStarted_` bookkeeping (lines 204-230) is correctly
  independent of `getStateProperty()`, as its own comment states — a deliberate, sound design choice
  to track "what did the demo itself last request" separately from the sensor's actual reported
  state (which can independently reflect NoPermissions/NotSupported regardless of the demo's last
  request).
- `HandleTimeBetweenUpdatesInput`'s clamp range `[1ms, 1000ms]` (lines 246-247) and doubling/halving
  step are reasonable for interactive manual testing, and the comment correctly notes
  `setTimeBetweenUpdatesProperty()` is documented-safe to call whether or not each sensor is
  currently started.
- No `Dispose(bool)` call of any kind appears anywhere in this file — the confirmed
  `microsoft-devices` shard finding (public instead of protected `Dispose(bool disposing)` in all 4
  sensor classes) is not surfaced or exploited here; this demo only ever calls the public
  `Start()`/`Stop()` API, never `Dispose`/`Dispose(bool)` directly.
- `DrawSignedBar`/`DrawUnsignedBar` (lines 407-437) correctly clamp before computing a fill
  fraction, avoiding an out-of-rectangle draw for a reading exceeding `maxAbs`/`maxValue`.

## Detailed Findings

### MEDIUM — Android jni mirror copy of `DevicesDemo.cpp`/`.hpp` has drifted out of sync, silently missing an entire feature (Task DEMO-001)
Confirmed via direct `diff` against `android/com.openeggbert.cna.demodevices/app/jni/src/DevicesDemo.cpp`
and its `.hpp`: the Android build of this demo lacks the TimeBetweenUpdates Numpad +/- control and
several window-title fields (Compass/Motion `IsDataValid`, raw magnetometer vector,
`HeadingAccuracy`, Motion's `DeviceRotationRate`/`Attitude`, the current TBU value) that the desktop
copy has had since 2026-07-06. Since Android has no title bar to display any of this anyway (this
demo's stated "one text channel" rationale is desktop-specific), the practical user-facing impact is
limited to the missing interactive rate control — but the two files are clearly meant to be kept as
mirrored copies (identical apart from platform entry-point wiring), and this is a real, confirmed
content divergence between them, not a deliberate platform-specific difference.

## Cross-File Observations
- `Main.cpp` (both desktop and jni copies) are byte-for-byte identical (confirmed via `diff`) —
  only `DevicesDemo.cpp`/`.hpp` have drifted.
- Confirms, via absence, that this demo does not exercise or depend on the `microsoft-devices`
  shard's confirmed `Dispose(bool)` visibility finding at all.

## Missing or Weak Tests
This is a manual/hardware-verification demo by explicit design (see its own header comment); no
automated test coverage is expected or present.

## Positive Findings
The very information-dense window title, and its own multi-paragraph comment explaining exactly
why it exists (Task DEMO-001's "enough data to write a useful bug report from its output alone"
requirement), is a genuinely thoughtful design choice for a demo whose entire purpose is manual
hardware verification without a full UI toolkit.

## Final Assessment
One MEDIUM finding: the Android jni mirror copy of this file has silently drifted, missing an
entire feature (Task DEMO-001) added to the desktop copy on 2026-07-06.
