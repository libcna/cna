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

## Phase 9 (2026-07-04) execution results — honest status per case

`plan_devices_phase9.md` Task P9-5 actually attempted every hardware case its brief
listed, in this exact container, this session — not a re-statement of the assumption
above. Nothing below is marked verified unless it was physically run.

1. **Android phone accelerometer** (`IsSupported`/`Start`/`Stop`/`CurrentValueChanged`/
   axis direction/timestamp sanity): **NOT RUN.** No physical Android device attached to
   this container; the only Android emulator configured here (`Medium_Phone`, x86_64)
   fails to start at all — confirmed via a real launch attempt (Task P9-4): "x86_64
   emulation currently requires hardware acceleration!", `/dev/kvm` absent. No APK
   packaging exists to install onto a device even if one were attached (Task P9-4).
2. **Android phone gyroscope** (same sub-items): **NOT RUN.** Same blocker as case 1.
3. **Android phone vibration** (`VibrateController::Start(TimeSpan)`/`Stop`/duration
   cap/no crash if unsupported): **NOT RUN** for the "on a real phone" claim (same
   blocker as case 1) — but the **software-level guarantees are verified on this
   desktop**: `VibrateControllerTests.StartWithNegativeDurationThrows`/
   `StartWithOverlongDurationThrows` (the duration-cap boundary) and every
   `Start`/`Stop`/`StartLeftRight` test in the suite pass cleanly with no crash when no
   haptic hardware is present (confirmed live this session, see case 4) — this is the
   "no crash if unsupported" half of this item, not the "actually buzzes a real motor"
   half, which remains genuinely unverified.
4. **Desktop without sensors — silent/expected unsupported behavior**: **VERIFIED, live,
   this session, on this actual machine.** This container has no accelerometer,
   gyroscope, haptic device, or joystick/gamepad attached (confirmed via
   `/proc/bus/input/devices` showing only keyboard/power/lid/sleep/video input nodes,
   and no `/dev/input/js*`). Ran
   `AccelerometerTests.GetIsSupportedPropertyDoesNotCrash`/`StartOnUnsupportedPlatformThrows`/
   `GetCurrentValuePropertyThrowsWhenUnsupported`, the identical three for
   `GyroscopeTests`, and `VibrateControllerTests.GetIsSupportedPropertyDoesNotCrash`/
   `UnsupportedImpliesEmptyDeviceName` directly (not just as part of the full suite) —
   all 8 passed. Notably, `GetCurrentValuePropertyThrowsWhenUnsupported` contains its own
   `GTEST_SKIP()` guard that would skip itself if this machine *did* have real hardware —
   it did not skip, which is itself live confirmation this desktop has none. This is the
   one case in this list this container can actually verify, and it is genuinely
   verified, not inferred.
5. **Desktop with gamepad** (`VibrateController` must not steal gamepad rumble from
   `GamePad::SetVibration()`): **NOT RUN.** No gamepad or joystick device is connected to
   this container (confirmed via `/proc/bus/input/devices` and the absence of
   `/dev/input/js*` — same check as case 4). The exclusion logic itself
   (`IsConnectedGamepadHapticDevice()`, ID-correlation via
   `SDL_OpenHapticFromJoystick()`) is unit-tested and reasoned-about against SDL3's own
   documented API contract, but has never been exercised against a real, physically
   connected controller, in any session to date.
6. **iOS device/toolchain**: **NOT RUN — unavailable, confirmed explicitly.** No
   `xcodebuild`/`xcrun`/`osxcross`, nothing matching `*ios*toolchain*` anywhere on this
   filesystem (re-checked fresh this session, same result as every prior phase since
   `plan_devices_phase4.md` Task P4-12). iOS cross-compilation fundamentally requires
   macOS/Xcode — not fixable by installing a package in this Linux container.

**Net result: 1 of 6 cases verified (case 4, live, this session); 5 of 6 remain
genuinely unverified**, each for a concrete, confirmed reason (no device, no working
emulator, no APK packaging, no toolchain) — not vague unavailability. This checklist
stays the authoritative source for whoever eventually has the missing hardware/toolchain
available.

**Update (2026-07-05, `plan_devices.md` Phase 9, Tasks DEVICES-0122-0126): the "no working
emulator"/"no APK packaging" blockers above are resolved** — `/dev/kvm` now exists in this
container (absent every session above), and a real Gradle/CMake Android app integration
now exists (`examples/demo_devices/android/`, see `docs/devices-build.md` Section 4.1).
The `Medium_Phone` AVD boots, `cna_demo_devices` installs, launches, and renders its real
UI (screenshot-confirmed), and responds to synthetic sensor values injected via the
emulator console. **This closes the "the software pipeline works end-to-end" gap, not
the "physically verified" one** — every item below this line is about *real* hardware,
which an emulator's virtual sensors are explicitly not a substitute for (Section 8 below
has the emulator's own specific limitations). Case 6 (iOS) is unaffected — still blocked,
no Apple toolchain exists in this Linux container (re-confirmed this session).

---

## 1. Accelerometer axis sign/orientation

**Code under test:** `Accelerometer.cpp`'s `ConvertAndroidAccelerometerToXnaLandscape()`
(Android only — desktop/iOS have no equivalent remap and report raw SDL axes directly),
which since `plan_devices_phase5.md` Task P5-7 delegates its actual sign/axis math to
`Detail::ConvertAndroidPortraitToXnaLandscape()`
(`include/Microsoft/Devices/Sensors/Detail/AndroidSensorOrientation.hpp`) — a pure
function taking an explicit `AndroidSensorLandscapeOrientation` instead of querying SDL
directly, so it's unit-testable on any platform.
`tests/Microsoft/Devices/Sensors/AndroidSensorOrientationTests.cpp` now covers both
rotations for both sensor classes' representative magnitudes, plus semantic tilt-right/
tilt-left/face-up/face-down examples added in `plan_devices_phase6.md` Task P6-7 (9
tests total, all passing in this headless container).

**Why this still needs real hardware despite the new unit tests:** the unit tests only
prove the code *implements the documented sign convention correctly* — they were derived
from, and assert against, the same reasoning-about-SDL3/Android-documentation write-up
in the function's own doc comment, never observed against a real accelerometer. Task
P4-11 (2026-07-04) confirmed this code *compiles* cleanly under the Android NDK for the
first time in this project's history, and Task P5-7 (2026-07-04) confirmed the
documented convention is what the code actually does — but neither step can prove the
convention itself is physically correct. A wrong assumption baked into both the
implementation and its own tests would still pass every automated check here and only
show up as the game tilting the wrong direction on a real device. **Task P6-7
(2026-07-04) deliberately did not add a test asserting an absolute sign for the
forward/backward (X) axis** (step 3 below) after an attempt to independently re-derive
one from rotation geometry alone produced a contradiction with the already-trusted Y-axis
convention on the first pass — see that task's Resolution in `plan_devices_phase6.md` for
the full account. This is exactly the kind of mistake this checklist exists to catch
before it reaches a real device, not after.

**Correction (2026-07-06, Task ACCEL-004):** this section previously said the two-
rotation restriction came from `AndroidManifest.xml`'s `android:screenOrientation=
"sensorLandscape"` — that attribute is not actually present in the demo's manifest
(confirmed by inspection). The real mechanism is SDL's own runtime orientation-lock
request (`SDLActivity.setOrientationBis()`, `SCREEN_ORIENTATION_SENSOR_LANDSCAPE` for a
non-resizable, wider-than-tall window with no `SDL_HINT_ORIENTATIONS` hint set) — see
`Detail::AndroidSensorLandscapeOrientation`'s own doc comment for the full citation.
Whatever the exact mechanism, the demo's window is expected to only ever reach the two
landscape rotations, never portrait — so this checklist (and
`AndroidSensorOrientationTests.cpp`) deliberately does not include separate
portrait-upright/portrait-upside-down steps; if a future session finds the app *can*
actually reach a portrait orientation on real hardware, that would itself be a new,
separate bug to investigate (a missing orientation lock), not a gap in this checklist.

**Steps:**
1. Run a game (or the Task P4-14 demo screen, once it exists) on a real Android device
   or emulator with a working virtual/physical accelerometer.
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
   `Detail::ConvertAndroidPortraitToXnaLandscape()` (shared by both
   `Accelerometer.cpp`/`Gyroscope.cpp` — see Section 2) — never in downstream game code,
   and remember to update/add a case in `AndroidSensorOrientationTests.cpp` for whatever
   convention turns out to be correct.

## 2. Gyroscope axis correctness

**Code under test:** `Gyroscope.cpp`'s `ConvertAndroidGyroscopeToXnaLandscape()` — same
caveat and same never-physically-verified status as the accelerometer remap above. As
of Task P5-7, it delegates to the exact same
`Detail::ConvertAndroidPortraitToXnaLandscape()` pure function `Accelerometer.cpp` uses
(the sign remap doesn't depend on which physical quantity — linear acceleration vs.
angular rate — the raw values represent), also covered by
`AndroidSensorOrientationTests.cpp`.

**Steps (Task GYRO-003, re-confirmed 2026-07-06 — same landscape-only scope as Section 1,
see `ACCEL-004`'s correction: the demo's window never reaches a portrait orientation, so
there are no separate portrait rotation cases to test here either):**
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
plain `Start(TimeSpan)`/`Start(TimeSpan, intensity)` rumble path. As of
`plan_devices_phase5.md` Task P5-11, `g_haptic` is now closed and `SDL_INIT_HAPTIC`
released via `~VibrateController()` at process exit (previously left open forever, on
an unverified assumption about `SDL_Quit()` ordering that turned out to be wrong — see
that task's Resolution) — this is a resource-lifetime fix, **not** a hardware
verification; the status below is unchanged by it.

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

## 5a. Vibration validation matrix (Task VIB-010, 2026-07-06)

Consolidates Sections 3-5 above into one at-a-glance matrix, plus the two cases neither
section calls out as its own row (desktop with no haptics at all, and desktop with a
non-gamepad haptic device connected). `Task DEMO-002` plans a separate
`docs/devices_sensor_hardware_qa_template.md` report template to record an actual run's
results against — this table is the checklist of *what* to check, not a form for
recording one specific session's results.

| Device / OS | Backend | Action | Expected — strict XNA | Expected — `NOXNA` extensions | Status |
|---|---|---|---|---|---|
| Android phone | `Detail::SdlHapticVibrateBackend` → `Context.VIBRATOR_SERVICE` | `Start(TimeSpan)` | Phone's built-in motor buzzes for the given duration | `Start(TimeSpan, intensity)` scales buzz strength; `getIsSupportedProperty()` true; `getDeviceNameProperty()` non-empty | **NOT RUN** — no Android device in this session (Section 3) |
| Android phone | same | `StartLeftRight(large, small, duration)` | N/A (not real XNA API) | Blends to one intensity on the phone's single actuator (confirmed via SDL3 source, `VIB-003`) — do **not** expect two independently-felt motors | **NOT RUN**, but the single-actuator-blend *code path* is source-confirmed, not just assumed (`VIB-003`) |
| iOS phone | none yet (`VIB-004`, plan only) | `Start(TimeSpan)` | Deterministic silent no-op (no backend registered) until a `CHHapticEngine` backend is implemented | Same — `getIsSupportedProperty()` false | **DEFERRED** — no backend exists; nothing to run yet |
| Desktop, no haptic hardware present | `Detail::SdlHapticVibrateBackend`, no device opens | `Start()`/`Stop()`/`StartLeftRight()`/`getIsSupportedProperty()`/`getDeviceNameProperty()` | Silent no-op for `Start`/`Stop`/`StartLeftRight`; `getIsSupportedProperty()` false; `getDeviceNameProperty()` empty | Same | **VERIFIED, this container** — every `VibrateControllerTests` case exercises exactly this environment live (313→ growing test count, all passing) |
| Desktop with a connected non-gamepad haptic device (e.g. a USB force-feedback wheel) | same, real device opened | `Start(TimeSpan)`/`Start(TimeSpan, intensity)` | Device actuates for the given duration; `getIsSupportedProperty()` true | Intensity scales strength; `StartLeftRight()` may achieve genuine independent magnitudes if the device/driver supports `SDL_HAPTIC_LEFTRIGHT` natively (unlike the Android single-actuator blend above) | **NOT RUN** — no such hardware available in this session |
| Desktop with a connected rumble-capable gamepad, no other haptic device | same | `getIsSupportedProperty()`, `Start()`, `GamePad::SetVibration()` | `VibrateController` excludes the gamepad's own haptic ID (`IsConnectedGamepadHapticDevice()`) — behaves as if unsupported; `GamePad::SetVibration()` drives the gamepad normally; neither API fights the other for the same motor | Same | **NOT RUN** — no gamepad connected in this session (full steps: Section 5 above) |

## 6. `Detail::AndroidSensorBridge` lifecycle safety (`plan_devices.md` Task DEVICES-0085)

**Code under test:** `AndroidSensorBridge::Stop()`'s self-join-detects-and-detaches
logic (`src/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.cpp`) — confirmed by
code review and a successful Android NDK cross-compile (`llvm-nm` symbol check) in this
session, but **never actually run**: this bridge's real (`#ifdef __ANDROID__`) code path
cannot execute in this headless container at all, only compile.

**Why this needs real hardware:** the claim under test is that stopping/disposing the
owning `Compass`/`Motion` instance from within its own `CurrentValueChanged` handler
(running on the bridge's dedicated worker thread) does not deadlock — `Stop()` detects
`std::this_thread::get_id() == worker_.get_id()` and detaches instead of joining. This
logic has been reasoned about and cross-compiled, but its actual runtime behavior (does
the detached thread really exit `Run()`'s loop cleanly afterward? does any Android
sensor-subsystem resource leak?) has never been observed on a real device.

**Steps (once Compass/Motion's Android backend, Phase 7/8, is wired to this bridge):**
1. On a real Android device, construct a `Compass`/`Motion` instance, `Start()` it, and
   subscribe a `CurrentValueChanged` handler that calls `Dispose()` on the same instance.
2. Confirm the process does not hang or deadlock when a sensor reading arrives.
3. Confirm no crash/use-after-free on subsequent readings (there should be none, since
   the instance is disposed) or on process exit.

## 7. `Compass` real Android backend (`plan_devices.md` Phase 7, Tasks DEVICES-0086-0100)

**Code under test:** `Detail::AndroidCompassBackend` (`src/Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.cpp`)
and `Detail::ConvertRotationVectorToMagneticHeadingDegrees()`
(`include/Microsoft/Devices/Sensors/Detail/AndroidCompassMath.hpp`) — confirmed by code
review, unit tests of the pure azimuth/accuracy math (self-consistency only), and a
successful Android NDK cross-compile (`llvm-nm` symbol check). **Never actually run**:
same reason as Section 6 — no Android device/emulator in this container.

**Why this needs real hardware:** the azimuth formula is derived from first-principles
quaternion algebra reproducing Android's own documented world-frame axis convention, but
this project's own history (`Detail::ConvertAndroidPortraitToXnaLandscape()`'s multi-phase
axis-sign saga for `Accelerometer`/`Gyroscope`) shows this exact kind of math is easy to
get subtly wrong in a way unit tests against self-derived expected values cannot catch —
only comparing against a real device's own compass app can.

**Note added 2026-07-06 (`COMPASS-004`/`COMPASS-009`):** the real WP7 `Compass` API
documents switching which axis it reads based on the phone's physical tilt (upright
vs. flat, MSDN `hh220912`/`hh202974`) — `Detail::AndroidCompassMath` currently
implements only one fixed axis extraction, with no tilt-mode switch at all
(`COMPASS-009`, new, not yet implemented). The steps below should therefore be treated
as testing whichever single mode the current implementation happens to produce, not
confirmed to be "flat mode" specifically — re-run once `COMPASS-009` is implemented,
covering both tilt modes explicitly.

**Steps:**
1. On a real Android device with a magnetometer, run a game/demo using `Compass`, holding
   the device flat and facing a known direction (compare against the device's own,
   already-calibrated compass app).
2. Confirm `Compass.CurrentValue.MagneticHeading` roughly matches the reference compass
   app's reading (within a reasonable tolerance — exact hardware calibration will differ).
3. Rotate the device slowly through a full 360°; confirm `MagneticHeading` increases (or
   decreases — confirm which direction this implementation actually produces, and note it
   here) monotonically and wraps correctly at the 0°/360° boundary, matching Section 1's
   "internal consistency" bar for the Android accelerometer/gyroscope remap.
4. Perform the classic figure-8 calibration gesture with the device in a low-magnetic-accuracy
   state (e.g., near a magnet or metal object); confirm `Compass.Calibrate` fires.
5. Confirm `Compass.CurrentValue.MagnetometerReading` reports plausible raw µT values (Earth's
   field is roughly 25-65 µT per `ASENSOR_MAGNETIC_FIELD_EARTH_MIN`/`_MAX`) — a reading wildly
   outside this range without a nearby magnet suggests the raw-vector wiring itself is wrong,
   not just the heading math.
6. Confirm `Compass.CurrentValue.TrueHeading` currently equals `MagneticHeading` exactly (the
   documented, honest limitation — not yet computing real declination) — this is expected,
   not a bug, until `System.Device.Location` exists.

If step 2 or 3 reveals a wrong sign/zero-point, the fix belongs in
`ConvertRotationVectorToMagneticHeadingDegrees()` — never in downstream game code — and a
new self-consistency test case should be added for whatever convention turns out correct,
matching Section 1's own reporting convention.

## 8. `Motion` real Android backend (`plan_devices.md` Phase 8, Tasks DEVICES-0101-0119)

**Code under test:** `Detail::AndroidMotionBackend` (`src/Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.cpp`)
and `Detail::ConvertRotationVectorToXnaQuaternion()`/`ExtractYawPitchRollFromQuaternion()`
(`include/Microsoft/Devices/Sensors/Detail/AndroidMotionMath.hpp`) — confirmed by code
review, round-trip unit tests of the yaw/pitch/roll extraction (against CNA's own already-
tested `Quaternion::CreateFromYawPitchRoll()`), and a successful Android NDK cross-compile
(`llvm-nm` symbol check). **Never actually run** — same reason as Sections 6/7.

**Why this needs real hardware:** `ExtractYawPitchRollFromQuaternion()` itself is verified
self-consistent (it round-trips through CNA's own tested math), but
`ConvertRotationVectorToXnaQuaternion()` — the raw Android quaternion → XNA `Quaternion`
mapping — is currently a direct, unremapped passthrough, not a rigorously-derived change of
basis between Android's world frame and XNA's. Whether this needs the same kind of axis
remap `Detail::ConvertAndroidPortraitToXnaLandscape()` applies to
`Accelerometer`/`Gyroscope` is genuinely unknown until tested on a real device.

**Steps:**
1. On a real Android device, run a game/demo using `Motion`, holding the device flat and
   level. Confirm `Motion.CurrentValue.Attitude.Pitch`/`Roll`/`Yaw` all read approximately
   zero (or whatever the documented "flat" reference should be — confirm and note here).
2. Tilt/rotate the device through known pitch, roll, and yaw rotations in turn; confirm each
   of `Attitude.Pitch`/`Roll`/`Yaw` responds to the correct physical rotation, not a
   different or swapped axis, and that `Attitude.Quaternion`/`RotationMatrix` visually
   (e.g. via a rendered 3D model in the demo) track the same physical rotation consistently.
3. Confirm `Motion.CurrentValue.Gravity` reads approximately `(0, ±1, 0)`-ish (magnitude ~1g)
   at rest, matching whichever axis this implementation's `Gravity` convention assigns to
   "down," and `DeviceAcceleration` reads approximately zero at rest.
4. Shake or throw the device; confirm `DeviceAcceleration` spikes while `Gravity` stays
   roughly constant — confirms the gravity/linear-acceleration split is wired to the correct
   sensors, not swapped.
5. Confirm `DeviceRotationRate` responds to physical rotation consistently with the
   already-verified `Gyroscope` class's own sign convention (Section 2) — `Motion` registers
   its own, independent `TYPE_GYROSCOPE` listener, so this is worth checking is consistent,
   not assumed.
6. If the device's magnetometer is unavailable or disabled, confirm `Motion` falls back to
   `TYPE_GAME_ROTATION_VECTOR` (yaw will drift slowly over time with no true-north
   correction — expected, not a bug, per this backend's own documented drift-difference
   note) rather than failing entirely.

If any step reveals a wrong sign/axis, the fix belongs in `ConvertRotationVectorToXnaQuaternion()`
(or `AndroidMotionBackend`'s vector handling for `Gravity`/`DeviceAcceleration`/
`DeviceRotationRate`) — never in downstream game code — and a new round-trip/self-consistency
test case should be added for whatever convention turns out correct.

---

## 9. Emulator limitations for Devices testing (`plan_devices.md` Task DEVICES-0129)

An Android emulator (confirmed working this session, `docs/devices-build.md` Section 4.1)
closes the "does the software pipeline work at all" question, but is **not a substitute**
for any item above that asks "does this feel/read correct on a real device":

- **No real vibration motor.** `Medium_Phone` (and Android emulators generally) have no
  physical haptic actuator — `VibrateController::Start()` can be confirmed to run without
  crashing on an emulator, but Section 3/4's "does it actually buzz" steps are
  meaningless there and must use a real device.
- **Virtual, not physical, sensor motion.** The emulator's console (`sensor set
  acceleration <x>:<y>:<z>`, `sensor set magnetic-field <x>:<y>:<z>`, etc. — confirmed
  working this session) lets a script inject arbitrary values instantly, which is useful
  for confirming the C++ dispatch pipeline delivers *whatever value is injected*
  end-to-end (confirmed working this session via `DevicesDemo`'s `DrawEventFlash()`
  indicator responding to injected values), but proves nothing about whether a real
  physical tilt/rotation produces the *correct* value — Sections 1/2/7/8's axis-sign
  questions still require a real device.
- **No virtual rotation-vector/game-rotation-vector sensor found in this session's
  emulator console command set** — `Detail::AndroidSensorBridge`'s `Compass`/`Motion`
  path (`ASENSOR_TYPE_ROTATION_VECTOR` etc.) was not exercised via emulator-injected
  values this session, only confirmed to launch/compile/link correctly. Whether the
  `Medium_Phone` system image's virtual sensor HAL exposes this sensor type at all is
  unconfirmed — investigate further before assuming an emulator can close Section 7/8's
  gaps even partially.
- **The emulator's own system apps can become unresponsive under this container's
  resource constraints** (observed this session: "Pixel Launcher isn't responding",
  then "System UI isn't responding" ANR dialogs, likely from `-gpu swiftshader_indirect`
  software rendering under load) — confirmed to be an emulator/environment issue, not a
  `cna_demo_devices` bug: the demo's own process stayed alive and kept rendering
  correctly throughout (`adb shell pidof`), unaffected by the system-level ANRs.

---

## Reporting results

If any item above reveals an actual bug (wrong sign, no vibration, gamepad conflict),
file it the same way as any other confirmed bug in this project: root-cause it against
the actual SDL3/Android behavior observed, then fix the specific `Accelerometer.cpp`/
`Gyroscope.cpp`/`VibrateController.cpp` logic — never adjust downstream game code to
compensate for a coordinate-convention or motor-selection bug in this layer.
