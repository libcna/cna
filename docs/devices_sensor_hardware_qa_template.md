# `Microsoft::Devices` Hardware QA Report Template

## How this relates to `docs/devices-hardware-checklist.md`

`docs/devices-hardware-checklist.md` is the checklist of **what** to verify on real
hardware — the numbered sections below (1-9) match its own section numbers exactly, so
a completed report can be read side-by-side with the checklist step it answers. This
file is **not** a duplicate of that checklist and does not repeat its rationale,
code-under-test details, or step-by-step instructions — read the checklist first, then
use this file to **record** what you actually observed running those steps on a
specific device, in a format that stays comparable across different testers, devices,
and sessions.

**Usage:** copy this template (do not edit the template itself) to a new file per test
session, e.g. `docs/hardware-qa-reports/2026-07-06-pixel8-android15.md`, fill in every
section that applies to the device under test, and leave sections that don't apply
(e.g. no compass on this particular device) explicitly marked `N/A — <why>` rather than
deleted, so a reader can tell "not tested" apart from "tested, not applicable."

If a section reveals an actual bug, follow `docs/devices-hardware-checklist.md`'s own
"Reporting results" section — root-cause it against real SDL3/Android/iOS behavior and
fix the specific CNA logic; never adjust downstream game code to compensate for a
coordinate-convention or motor-selection bug in this layer.

---

## Session metadata

| Field | Value |
|---|---|
| Date | |
| Tester | |
| Device model | |
| OS / API level (e.g. Android 14 / API 34, iOS 17.5) | |
| Physical or emulator/simulator (name the AVD/simulator if not physical) | |
| CNA commit hash (`git rev-parse HEAD`) | |
| CNA platform and graphics renderer (`CNA_PLATFORM`, `CNA_GRAPHICS_RENDERER`) | |
| Build type (Debug/Release) | |
| Build preset/options, sanitizer, CNA_DEVICES and keyboard-emulation settings | |
| Sensor landscape-remap policy / effective display orientation | |
| SDL/dependency revision and driver/provider names | |
| Test app used (`modules/devices/examples/demo_devices` or a specific game) | |
| Logs, captures and raw measurement artifacts (paths/links) | |

Use PASS / FAIL / NOT RUN / N/A with a reason. An emulator pass is emulator evidence;
it does not qualify physical axes, motor output or real device reconnection. Record requested
and observed values separately. Historical task IDs below route evidence; their plan is in Git
history, not an active file to update.

---

## 1. Accelerometer axis sign/orientation

See checklist Section 1 for the code under test and full step-by-step instructions.

| Step | Expected | Observed | Pass/Fail |
|---|---|---|---|
| 1. Record raw vector and platform rotation in both landscape poses | device-fixed source axes, identified orientation | | |
| 2. Enable remap in Rotation90 | output `(-rawY, rawX, rawZ)` after unit conversion | | |
| 2. Enable remap in Rotation270 | output `(rawY, -rawX, rawZ)` after unit conversion | | |
| 3. Disable remap | unit-converted raw components in both poses | | |
| Face-up vs. face-down | Z agrees with the raw sample in both poses | | |

- Record the device's natural orientation and actual app orientation policy. The helper models
  natural-portrait devices and two landscape poses; other configurations need separate evidence.
- The default-enabled remap is the retained CNA convenience (`ACCEL-008`); it is separate from
  Windows Phone's device-fixed convention. CNA-FIX-D22 corrects the former reflection to a rotation.
- Notes / anomalies:

## 2. Gyroscope axis correctness

See checklist Section 2.

| Step | Expected | Observed | Pass/Fail |
|---|---|---|---|
| Rotation direction/sign per axis, both landscape rotations | same table as Section 1, preserving radians per second and right-handed axes | | |

- Notes / anomalies:

## 2a. Platform sensor disconnect/reconnect and default-device change

| Check | Observed / evidence | Status |
|---|---|---|
| Initial sensor id/provider and normal delivery | | |
| Disconnect while running: delivery, IsDataValid and State | | |
| Stop/Start or second instance after removal: fresh selection/failure | | |
| Replacement/default-device selection after reconnect | | |

Separate recovery after a new Start from automatic mid-session detection; the latter is not
an implemented continuous-monitoring guarantee.

## 3. `VibrateController::Start()` actually vibrates the phone motor

See checklist Section 3.

| Check | Expected | Observed | Pass/Fail |
|---|---|---|---|
| `Start(TimeSpan)` | motor runs for the given duration | | |
| Duration accuracy (stopwatch or feel) | matches requested `TimeSpan` within a reasonable margin | | |
| `getIsSupportedProperty()` | `true` on a device with a vibrator | | |
| `getDeviceNameProperty()` | non-empty, plausible name | | |

## 4. `StartLeftRight()` drives two distinct motors

See checklist Section 4. **Known limitation, not a bug to re-discover:**
`docs/devices-android.md`'s Vibration section already documents that a real phone's
single built-in vibrator always receives one blended intensity via this call, never two
independent values (SDL3's own `SDL_HAPTIC_LEFTRIGHT` blending). Use this section to
confirm that documented blending, not to look for true independent dual-motor output on
a phone.

| Check | Expected | Observed | Pass/Fail |
|---|---|---|---|
| `StartLeftRight(1.0, 0.0, ...)` vs `(0.0, 1.0, ...)` vs `(1.0, 1.0, ...)` | felt intensity differs plausibly per the documented `0.6`/`0.4` blend weighting | | |

## 4a. Haptic effect run failure and cleanup

| Check | Observed / evidence | Status |
|---|---|---|
| Failure mechanism: effect creation succeeds, native run fails | | |
| Diagnostic/native error and failed effect retirement | | |
| Repeated failure/retry: resource growth and later playback | | |

An unplug that fails before effect creation does not exercise this case.

## 4b. Haptic disconnect/reconnect

| Check | Observed / evidence | Status |
|---|---|---|
| Start/Stop/StartLeftRight after physical removal | | |
| Support/name after removal | | |
| Reconnect/replacement: support and actual motor output | | |
| Repeated cycles without process restart | | |

## 5. Gamepad-exclusion filter

See checklist Sections 5 and 5a for the full matrix this should mirror.

| Device/controller | `IsSupported` | `Start()` works | `StartLeftRight()` works | Notes |
|---|---|---|---|---|
| Phone's own vibrator | | | | |
| Connected gamepad (should be excluded) | | | | |

## 5a. Vibration validation matrix

| Case | Validation result / actual motor behavior | Status |
|---|---|---|
| Zero / negative / overlong duration | | |
| Repeated Start | | |
| Zero intensity | | |
| Ordinary / left-right mutual exclusion | | |
| Stop | | |

Distinguish argument-validation results from observed native motor behavior.

## 6. `Detail::AndroidSensorBridge` lifecycle safety

See checklist Section 6. This section is inherently about crash/race absence under
real concurrent use, not a numeric expected/observed value.

| Check | Pass/Fail | Notes |
|---|---|---|
| Rapid Start/Stop/Dispose cycling does not crash or hang | | |
| App backgrounding/foregrounding while sensors are running does not crash | | |
| Rotating the device while sensors are running does not crash | | |

## 6a. Android probe locking and service restart

| Check | Observed / evidence | Status |
|---|---|---|
| Concurrent probe/Start/Stop duration and sanitizer result | | |
| Actual sensor-service failure/restart mechanism | | |
| Cache invalidation and successful later re-probe | | |

## 6b. Bounded drain and rate-change latency

| Requested rate / load | Drain-limit counter before/after | Rate-change latency / Stop latency | Status |
|---|---|---|---|
| | | | |

Record any instrumentation/build changes. A default zero counter without a running Android
worker does not exercise the drain cap.

## 6c. Native rate acceptance and minimum delay

| Sensor/provider | Requested interval | Min delay / native result | Measured cadence | Status |
|---|---|---|---|---|
| | | | | |

Include delivery after a rejected rate change, if a real rejection was observed.

## 7. `Compass` real Android backend

See checklist Section 7.

| Check | Expected | Observed | Pass/Fail |
|---|---|---|---|
| `MagneticHeading` while rotating the device in a full circle, flat | sweeps 0-360 plausibly | | |
| `HeadingAccuracy` / `Calibrate` event | fires when accuracy genuinely degrades (e.g. near a magnet) | | |
| `TrueHeading == MagneticHeading` always (no declination applied) | matches (documented limitation, not a bug) | | |

- **Tilt-dependent axis behavior** (directly answers `COMPASS-009`, the devices plan):
  does the heading calculation visibly change/misbehave when the device goes from
  held-upright to lying-flat? The real WP7 API documents switching its heading-axis
  calculation based on physical device tilt; CNA now implements flat/upright mode selection
  in ConvertRotationVectorToMagneticHeadingDegreesWithTiltMode. Record cardinal headings,
  the transition around upright/flat poses and what the physical device actually reports.
- Notes / anomalies:

## 7a. Compass fusion freshness

| Check | Source timestamps / event evidence | Status |
|---|---|---|
| Both source streams deliver normally | | |
| One source actually stalls; publication stops after freshness threshold | | |
| Source resumes; publication resumes | | |

Record the actual stall mechanism. Disturbing a magnetic field alone does not prove that
the sensor stopped delivering samples.

## 8. `Motion` real Android backend

See checklist Section 8.

| Check | Expected | Observed | Pass/Fail |
|---|---|---|---|
| `Attitude.Pitch`/`Roll`/`Yaw` while rotating the device on each axis | changes plausibly, matches physical rotation | | |
| `Gravity` while lying flat / standing up / on each side | ~1g along the axis facing down, ~0 on the others | | |
| `DeviceAcceleration` while still | ~0 on all axes (gravity-filtered) | | |
| `DeviceRotationRate` while still vs. rotating | ~0 while still, nonzero and consistent-signed while rotating | | |
| Timestamps (`MotionReading.Timestamp` vs. `Attitude.Timestamp`) | always equal (Task `MOTION-006`) | | |
| No visible "stuck"/stale fused reading during fast motion | record source timestamp spread; current rule rejects spread over 500ms | | |

## 8a. Motion fusion drops and source timing

| Motion/load | Drop counter before/after | Per-source timestamp spread / visible result | Status |
|---|---|---|---|
| Gentle / still baseline | | | |
| Fast rotation / shake | | | |

The current implementation uses latest values, not interpolated per-source queues. Counter
evidence does not establish that the larger fusion redesign has been implemented.

## 8b. Motion attitude source and drift

| Source/device capability | IsAttitudeNorthReferenced | Time stationary / measured yaw drift | Status |
|---|---|---|---|
| Rotation-vector source | | | |
| Verified game-rotation-vector fallback | | | |

Record how the actual source choice was established. A noisy magnetometer or a false property
from a fake backend does not itself qualify native fallback selection.

## 9. Emulator limitations encountered (if using an emulator, not a physical device)

See checklist Section 9 for the known list. Record anything encountered beyond what's
already documented there.

---

## Summary

- Overall pass/fail:
- Bugs filed (link issues/commits):
- Open questions this session could not resolve:
- Qualification scope (physical device, emulator, host-only or source inspection):
- Follow-up owner / evidence location:
