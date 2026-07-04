# Microsoft::Devices — Manual Hardware Verification Checklist

## Purpose

Everything in `Microsoft::Devices`/`Microsoft::Devices::Sensors` has been developed and
tested in a headless Linux dev container with no real accelerometer, gyroscope,
magnetometer, or haptic/vibration hardware, and no Android/iOS device or emulator. Every
existing automated test either:

- exercises the no-hardware-found silent-no-op / `SensorState::NotSupported` path, or
- exercises the real `CurrentValueChanged`/`ReadingChanged` dispatch logic via the
  `NOXNA` synthetic-injection test hooks (`InjectSyntheticSensorUpdate()`,
  `SetStartedForTesting()` — see `plan_devices_phase4.md` Task P4-2), which prove the
  C++ dispatch plumbing works but say nothing about whether the *numbers* flowing
  through it are physically correct.

This checklist is for whoever eventually runs CNA on real hardware — a physical Android
device/emulator, an iOS device (once `plan_devices_phase4.md` Task P4-12's toolchain
blocker is lifted), or a desktop machine with an actual accelerometer/gyroscope/haptic
controller attached — to close the gap between "compiles and dispatches correctly" and
"is physically correct."

None of the items below can be verified by an AI agent in a headless container. This is
a plain checklist, not a task with its own build/test cycle.

---

## 1. Accelerometer axis sign/orientation

**Code under test:** `Accelerometer.cpp`'s `ConvertAndroidAccelerometerToXnaLandscape()`
(Android only — desktop/iOS have no equivalent remap and report raw SDL axes directly).

**Why this needs real hardware:** the remap's sign choices were derived entirely by
reasoning about SDL3's and Android's documented coordinate systems (see the function's
own doc comment in `Accelerometer.cpp`), never observed against a real accelerometer.
Task P4-11 (2026-07-04) confirmed this code *compiles* cleanly under the Android NDK for
the first time in this project's history, but compiling is not the same as being
correct — a sign error would compile fine and only show up as the game tilting the
wrong direction.

**Steps:**
1. Run a game (or the Task P4-14 demo screen, once it exists) on a real Android device
   or emulator with a working virtual/physical accelerometer, `AndroidManifest.xml`
   `android:screenOrientation="sensorLandscape"`.
2. Rotate the device to `ROTATION_90` (`SDL_ORIENTATION_LANDSCAPE`). Tilt the device so
   the physical right edge goes down. Confirm `AccelerometerReading.Acceleration.Y > 0`
   (matching the documented WP7 convention: `Y > 0` means "tilt right").
3. Tilt the device so the physical top edge (in landscape) goes down/forward. Confirm
   `Acceleration.X`'s sign matches what feels like "forward" tilt, consistently between
   runs.
4. Repeat steps 2–3 with the device rotated to `ROTATION_270`
   (`SDL_ORIENTATION_LANDSCAPE_FLIPPED`) — the remap uses a different sign combination
   for this rotation (see `Accelerometer.cpp`); confirm `Y > 0` still means "tilt right"
   from the *player's* perspective, not the raw sensor's, in this rotation too.
5. Confirm `Acceleration.Z` behaves as "perpendicular to the screen" in both rotations
   (e.g. laying the device flat face-up vs. face-down should flip its sign).
6. If any sign is backwards, the fix is a sign change in
   `ConvertAndroidAccelerometerToXnaLandscape()` only — per its own doc comment, never in
   downstream game code.

## 2. Gyroscope axis correctness

**Code under test:** `Gyroscope.cpp`'s `ConvertAndroidGyroscopeToXnaLandscape()` — same
caveat and same never-physically-verified status as the accelerometer remap above (it
mirrors the same sign logic, per its own doc comment referring back to
`Accelerometer.cpp`).

**Steps:**
1. Same device/rotation setup as Section 1.
2. Rotate the physical device around each of its three axes in turn (yaw, pitch, roll)
   and confirm `GyroscopeReading.RotationRate`'s sign for each axis matches an intuitive
   "positive rotation direction" consistently across both landscape rotations — there is
   no single authoritative WP7 sign convention documented for gyroscope the way there is
   for accelerometer tilt, so use internal consistency (same physical rotation always
   produces the same sign, regardless of which rotation state the device is in) as the
   primary correctness bar.

## 3. `VibrateController::Start()` actually vibrates the phone motor

**Code under test:** `VibrateController.cpp`'s `OpenFirstHapticDevice()`/
`IsConnectedGamepadHapticDevice()` (Task P4-10's ID-based gamepad-exclusion fix) and the
plain `Start(TimeSpan)`/`Start(TimeSpan, intensity)` rumble path.

**Why this needs real hardware:** per `VibrateController.cpp`'s own comment, SDL3's
Android haptic backend automatically registers the phone's own vibration motor as a
haptic device once `SDL_INIT_HAPTIC` initializes, with no custom JNI/Java bridge code in
this project — this has never been confirmed to actually reach
`Vibrator.vibrate(milliseconds)` on a real device.

**Steps:**
1. On a real Android phone (not an emulator, which typically has no vibration motor to
   feel/observe) with **no gamepad connected**, call
   `VibrateController::getDefaultProperty()->Start(TimeSpan::FromSeconds(1))`.
2. Confirm the phone's own vibration motor actually buzzes for ~1 second.
3. Call `Start(TimeSpan::FromMilliseconds(500), 0.25f)` and `Start(TimeSpan::FromMilliseconds(500), 1.0f)`
   in turn; confirm the buzz is noticeably weaker at `0.25f` than at `1.0f` (the
   `NOXNA` intensity extension actually changes physical rumble strength, not just a
   silently-clamped no-op).
4. Call `Stop()` mid-vibration; confirm it actually stops immediately rather than
   running to completion.

## 4. `StartLeftRight()` drives two distinct motors

**Code under test:** `VibrateController::StartLeftRight()`'s `SDL_HAPTIC_LEFTRIGHT`
effect upload.

**Steps:**
1. On a device/controller with two physically distinct rumble motors (typically a
   large low-frequency motor and a small high-frequency one — most game controllers
   have this; most phones do not, so this item may only be testable with a connected
   controller once Section 5 below establishes that path is even reachable) call
   `StartLeftRight(1.0f, 0.0f, TimeSpan::FromSeconds(1))`, then
   `StartLeftRight(0.0f, 1.0f, TimeSpan::FromSeconds(1))`.
2. Confirm each call noticeably actuates only its own motor (large-only, then
   small-only), not both simultaneously — i.e. `largeMotor`/`smallMotor` really map to
   independent physical actuators, not just independent intensity scalars on the same
   motor.

## 5. Gamepad-exclusion filter doesn't compete with `GamePad::SetVibration()`

**Code under test:** `VibrateController.cpp`'s `IsConnectedGamepadHapticDevice()` (Task
P4-10's `SDL_OpenHapticFromJoystick()`-based ID correlation) — the whole point of this
filter is that `VibrateController` and
`Microsoft::Xna::Framework::Input::GamePad::SetVibration()` must never both try to drive
the *same* physical controller's rumble motor, since XNA's `VibrateController` targets
the phone's own vibration motor, not a connected gamepad's.

**Why this needs real hardware:** no real gamepad is connected in this dev container, so
this exclusion path — the entire reason Task P4-10 exists — has never actually been
exercised, only reasoned about from SDL3's API documentation.

**Steps:**
1. Connect a real rumble-capable gamepad (tested with at least one of: Xbox, PS4/PS5,
   or a generic HID gamepad with rumble — SDL3's haptic backend enumerates these
   differently across platforms, so more than one model is worth trying if available).
2. Call `VibrateController::getDefaultProperty()->getIsSupportedProperty()`. If the only
   haptic-capable device present is the connected gamepad, this should return `false`
   (or `true` only if a *separate* phone/desktop haptic device also exists) — confirming
   the gamepad's own haptic ID was correctly excluded from `VibrateController`'s device
   selection.
3. Call `GamePad::SetVibration(PlayerIndex::One, 1.0f, 1.0f)` and confirm the gamepad
   rumbles as expected via the normal `GamePad` path.
4. Call `VibrateController::getDefaultProperty()->Start(TimeSpan::FromSeconds(1))` while
   the gamepad is still connected; confirm it does **not** also rumble the gamepad (it
   should be a no-op if no other haptic device exists, or actuate a distinct device if
   one does) — the two APIs must never fight over the same physical motor.
5. Disconnect the gamepad, reconnect it, and repeat step 2 — confirm the exclusion is
   re-evaluated per-call (not cached from the first probe), since gamepads can connect
   and disconnect at any time during a running game.

---

## Reporting results

If any item above reveals an actual bug (wrong sign, no vibration, gamepad conflict),
file it the same way as any other confirmed bug in this project: root-cause it against
the actual SDL3/Android behavior observed, then fix the specific `Accelerometer.cpp`/
`Gyroscope.cpp`/`VibrateController.cpp` logic — never adjust downstream game code to
compensate for a coordinate-convention or motor-selection bug in this layer.
