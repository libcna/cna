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
| Graphics backend (`CNA_GRAPHICS_RENDERER`: `SDL_RENDERER`/`OPENGLES3`/`VULKAN`/`BGFX`) | |
| Build type (Debug/Release) | |
| Test app used (`modules/devices/examples/demo_devices` or a specific game) | |

---

## 1. Accelerometer axis sign/orientation

See checklist Section 1 for the code under test and full step-by-step instructions.

| Step | Expected | Observed | Pass/Fail |
|---|---|---|---|
| 2. `ROTATION_90`, tilt physical right edge down | `Acceleration.Y > 0` | | |
| 3. `ROTATION_90`, tilt physical top edge (landscape) down/forward | consistent sign, note which | | |
| 4. `ROTATION_270`, tilt right (player perspective) | `Acceleration.Y > 0` | | |
| 5. Face-up vs. face-down | `Acceleration.Z` sign flips | | |

- Is the coordinate convention the same in portrait as landscape, or does it change
  with device orientation? (Directly answers `ACCEL-008`, `plans/plan_devices.md` — an
  archived MSDN Magazine article claims the real WP7 accelerometer's coordinate system
  "is the same whether... running in portrait or landscape mode," which would
  contradict this codebase's entire `ConvertAndroidAccelerometerToXnaLandscape()`
  premise if true. This is currently an open, unresolved question — a real answer here
  is directly actionable.)
- Notes / anomalies:

## 2. Gyroscope axis correctness

See checklist Section 2.

| Step | Expected | Observed | Pass/Fail |
|---|---|---|---|
| Rotation direction/sign per axis, both landscape rotations | matches accelerometer's remap convention | | |

- Notes / anomalies:

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

## 5. Gamepad-exclusion filter / 5a. Vibration validation matrix

See checklist Sections 5 and 5a for the full matrix this should mirror.

| Device/controller | `IsSupported` | `Start()` works | `StartLeftRight()` works | Notes |
|---|---|---|---|---|
| Phone's own vibrator | | | | |
| Connected gamepad (should be excluded) | | | | |

## 6. `Detail::AndroidSensorBridge` lifecycle safety

See checklist Section 6. This section is inherently about crash/race absence under
real concurrent use, not a numeric expected/observed value.

| Check | Pass/Fail | Notes |
|---|---|---|
| Rapid Start/Stop/Dispose cycling does not crash or hang | | |
| App backgrounding/foregrounding while sensors are running does not crash | | |
| Rotating the device while sensors are running does not crash | | |

## 7. `Compass` real Android backend

See checklist Section 7.

| Check | Expected | Observed | Pass/Fail |
|---|---|---|---|
| `MagneticHeading` while rotating the device in a full circle, flat | sweeps 0-360 plausibly | | |
| `HeadingAccuracy` / `Calibrate` event | fires when accuracy genuinely degrades (e.g. near a magnet) | | |
| `TrueHeading == MagneticHeading` always (no declination applied) | matches (documented limitation, not a bug) | | |

- **Tilt-dependent axis behavior** (directly answers `COMPASS-009`, `plans/plan_devices.md`):
  does the heading calculation visibly change/misbehave when the device goes from
  held-upright to lying-flat? The real WP7 API documents switching its heading-axis
  calculation based on physical device tilt; this codebase's Android compass math has
  no equivalent. Record what you actually observe in both orientations.
- Notes / anomalies:

## 8. `Motion` real Android backend

See checklist Section 8.

| Check | Expected | Observed | Pass/Fail |
|---|---|---|---|
| `Attitude.Pitch`/`Roll`/`Yaw` while rotating the device on each axis | changes plausibly, matches physical rotation | | |
| `Gravity` while lying flat / standing up / on each side | ~1g along the axis facing down, ~0 on the others | | |
| `DeviceAcceleration` while still | ~0 on all axes (gravity-filtered) | | |
| `DeviceRotationRate` while still vs. rotating | ~0 while still, nonzero and consistent-signed while rotating | | |
| Timestamps (`MotionReading.Timestamp` vs. `Attitude.Timestamp`) | always equal (Task `MOTION-006`) | | |
| No visible "stuck"/stale fused reading during fast motion | none observed (Task `MOTION-007`'s 500ms staleness guard) | | |

## 9. Emulator limitations encountered (if using an emulator, not a physical device)

See checklist Section 9 for the known list. Record anything encountered beyond what's
already documented there.

---

## Summary

- Overall pass/fail:
- Bugs filed (link issues/commits):
- Open questions this session could not resolve:
