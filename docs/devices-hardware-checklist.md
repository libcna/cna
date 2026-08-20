# Microsoft::Devices — Manual Hardware Verification Checklist

## Purpose

Everything in `Microsoft::Devices`/`Microsoft::Devices::Sensors` has been developed and
tested in a headless Linux dev container with no real accelerometer, gyroscope,
magnetometer, or haptic/vibration hardware, and no Android/iOS device or emulator. Every
existing automated test either:

- exercises the no-hardware-found silent-no-op / `SensorState::NotSupported` path, or
- exercises the real `CurrentValueChanged`/`ReadingChanged` dispatch logic via the
  `CNAEXT` synthetic-injection test hooks (`InjectSyntheticSensorUpdate()`,
  `SetStartedForTesting()` — see `plans/plan_devices_phase4.md` Task P4-2), which prove the
  C++ dispatch plumbing works but say nothing about whether the *numbers* flowing
  through it are physically correct.

This checklist is for whoever eventually runs CNA on real hardware — a physical Android
device/emulator, an iOS device (once `plans/plan_devices_phase4.md` Task P4-12's toolchain
blocker is lifted), or a desktop machine with an actual accelerometer/gyroscope/haptic
controller attached — to close the gap between "compiles and dispatches correctly" and
"is physically correct."

None of the items below can be verified by an AI agent in a headless container. This is
a plain checklist, not a task with its own build/test cycle.

**Recording results (Task `DEMO-002`, 2026-07-06):** this file is "what to check" —
`docs/devices_sensor_hardware_qa_template.md` is the companion "how to record a
specific run's results" template, with one section per numbered section below, plus
device/OS/commit-hash metadata fields, so results stay comparable across different
testers, devices, and sessions instead of each tester inventing their own format.

---

## Phase 9 (2026-07-04) execution results — honest status per case

`plans/plan_devices_phase9.md` Task P9-5 actually attempted every hardware case its brief
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
   `plans/plan_devices_phase4.md` Task P4-12). iOS cross-compilation fundamentally requires
   macOS/Xcode — not fixable by installing a package in this Linux container.

**Net result: 1 of 6 cases verified (case 4, live, this session); 5 of 6 remain
genuinely unverified**, each for a concrete, confirmed reason (no device, no working
emulator, no APK packaging, no toolchain) — not vague unavailability. This checklist
stays the authoritative source for whoever eventually has the missing hardware/toolchain
available.

**Update (2026-07-05, `plans/plan_devices.md` Phase 9, Tasks DEVICES-0122-0126): the "no working
emulator"/"no APK packaging" blockers above are resolved** — `/dev/kvm` now exists in this
container (absent every session above), and a real Gradle/CMake Android app integration
now exists (`modules/devices/examples/demo_devices/android/`, see `docs/devices-build.md` Section 4.1).
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
which since `plans/plan_devices_phase5.md` Task P5-7 delegates its actual sign/axis math to
`Detail::ConvertAndroidPortraitToXnaLandscape()`
(`include/Microsoft/Devices/Sensors/Detail/AndroidSensorOrientation.hpp`) — a pure
function taking an explicit `AndroidSensorLandscapeOrientation` instead of querying SDL
directly, so it's unit-testable on any platform.
`tests/Microsoft/Devices/Sensors/AndroidSensorOrientationTests.cpp` now covers both
rotations for both sensor classes' representative magnitudes, plus semantic tilt-right/
tilt-left/face-up/face-down examples added in `plans/plan_devices_phase6.md` Task P6-7 (9
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
convention on the first pass — see that task's Resolution in `plans/plan_devices_phase6.md` for
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

**Decision recorded (2026-07-07, Task `ACCEL-008`):** an archived MSDN Magazine article
plus SDL3's own documentation both state the real WP7 `Accelerometer` never performs this
remap at all — it always reports the same fixed device-relative frame regardless of
display orientation. The project maintainer decided to **keep** the existing remap
(existing CNA games/demos may already depend on it) but mark it explicitly as a
**deliberate CNA convenience deviation**, not required XNA/WP7 behavior, and add an
opt-out: `Detail::SetAndroidLandscapeRemapEnabled(false)` makes `Accelerometer`/
`Gyroscope` report SDL's raw, unremapped, device-fixed axes instead (defaults to `true`).
If step 2 above is ever re-verified on real hardware and found to disagree with the
documented convention, that is now a bug in the *opt-in* remap specifically, not evidence
that the remap should be removed outright — removal was considered and explicitly
declined. **Not yet applied to `Motion`'s Gravity/DeviceAcceleration/RotationRate**
(see Section 8, `plans/plan_devices.md` Task `MOTION-012`) — tracked separately since it needs
its own careful math derivation, not a rushed addition alongside this decision.

## 2. Gyroscope axis correctness

**Decision recorded (2026-07-07, Task `ACCEL-008`):** same decision as Section 1 above —
kept enabled by default, now documented as a CNA-only deviation from real WP7 behavior,
with the same `Detail::SetAndroidLandscapeRemapEnabled(false)` opt-out (shared with
`Accelerometer`, since both remap through the same `Detail::` function and flag).

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

## 2a. Platform sensor disconnect/reconnect and default-device change

**Code under test:** `Detail::PlatformSensorSubsystem<TSensor>` and
`IPlatformSensors::OpenSensor()` (shared behavior for `Accelerometer` and `Gyroscope`). A fresh
platform session is opened for each stopped-to-started cycle, so a disconnected device cannot
leave a cached native handle behind and a replacement/default device can be selected normally.

**Why this needs real hardware:** SDL3 has no sensor-specific hotplug event (confirmed
by reading `third_party/SDL/include/SDL3/SDL_events.h` — only `SDL_EVENT_SENSOR_UPDATE`
exists), so `IsSensorConnected()`'s only way to detect a disconnect is re-querying the
live device list. This container never has a real SDL sensor open at all
(`SDL_GetSensors()` always returns an empty list here), so
`OpenDefaultSensorLocked()`'s actual staleness branch
(`sensor_ != nullptr && !IsSensorConnected(...)`) is never taken by any test — every run
here only exercises the "nothing cached yet" path.
`IsSensorConnectedForTesting()` (`Accelerometer`/`Gyroscope`'s own test-only wrappers)
proves the underlying `SDL_GetSensors()` query itself works and correctly reports "not
found," but a genuine remove/re-add/default-device-change scenario — this task's own
acceptance criteria's literal ask — would need either real hardware or a native
fault-injection layer capable of safely mocking `SDL_GetSensors()`/`SDL_OpenSensor()`/
`SDL_CloseSensor()` (`plans/plan_devices.md` Task `TEST2-005`'s own separate scope; building
one ad hoc here to fake-inject a full device lifecycle was judged out of scope for this
task, the same call made for `ANDR2-002`'s identical gap). **Status: NOT RUN — hardware
or TEST2-005 validation open.**

**Separately not addressed by this fix, and flagged rather than silently dropped:** this
task's required work also asks that a device disappearing **while an instance is
already started** stop delivery, invalidate `CurrentValue`/`IsDataValid`, transition
`SensorState` appropriately, and attempt policy-driven reacquisition. The SDL sensor path
is entirely event-driven (`SDL_EventFilter`, no polling loop the way
`AndroidSensorBridge::Run()` has) — there is no natural trigger point today to detect a
disconnect *mid-session* for an instance that is not itself calling `Start()` again. This
fix only closes the "cached indefinitely, reused forever" gap for the *next* `Start()`
call (any instance, new or restarting) — it does not add live, continuous
disconnect-monitoring for an already-running instance. That would need a genuinely new
architectural piece (e.g. checking every already-started instance's `sensorId_` liveness
opportunistically whenever any `SDL_EVENT_SENSOR_UPDATE` of the same `TSensor` type
fires) and is significant enough design work that it was not attempted as part of this
task — a candidate for its own future task if picked up, not a silently-abandoned piece
of this one.

**Steps:**
1. On a real device/emulator with an accelerometer or gyroscope, `Start()` an instance
   and confirm readings arrive normally.
2. If the device can be simulated as disconnected (e.g. an emulator's sensor panel, or a
   USB sensor peripheral that can be unplugged), disconnect it, then construct and
   `Start()` a **second**, independent instance of the same sensor class — confirm the
   second `Start()` correctly detects the first's cached handle is stale, discards it,
   and either fails cleanly (`SensorState::NotSupported`, if nothing else is available)
   or picks up a different available device, rather than reusing the dead cached handle.
3. Reconnect the original device (or connect a different one of the same type) and
   repeat step 2 — confirm the same instance/class can recover without any process
   restart.
4. Separately confirm (or, more likely, document as an accepted gap per the note above)
   whether an **already-started, still-running** instance ever notices a mid-session
   disconnect on its own, without a fresh `Start()` call from elsewhere prompting the
   re-validation.

## 3. `VibrateController::Start()` actually vibrates the phone motor

**Code under test:** `VibrateController.cpp`'s `OpenFirstHapticDevice()`/
`IsConnectedGamepadHapticDevice()` (Task P4-10's ID-based gamepad-exclusion fix) and the
plain `Start(TimeSpan)`/`Start(TimeSpan, intensity)` rumble path. As of
`plans/plan_devices_phase5.md` Task P5-11, `g_haptic` is now closed and `SDL_INIT_HAPTIC`
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
   `CNAEXT` intensity extension actually changes physical rumble strength, not just a
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

## 4a. `StartLeftRight()` cleans up an effect whose `SDL_RunHapticEffect()` fails (Task VIB2-003, 2026-07-17)

**Code under test:** `Sdl3Haptics::PlayLeftRight()`'s handling of a
successfully-uploaded (`SDL_CreateHapticEffect()` succeeds) but then failing
`SDL_RunHapticEffect()` call — the fix destroys the just-uploaded effect
(`SDL_DestroyHapticEffect()`) and resets `leftRightEffectId_` immediately, rather than
leaving an uploaded-but-never-playing effect slot allocated until the next
`Start()`/`StartLeftRight()`/`Stop()`/destructor call happens to reclaim it.

**Why this needs real hardware:** in this dev container no haptic device is ever opened
(`OpenFirstHapticDevice()` returns `nullptr`), so `StartLeftRight()` always returns at the
earlier "no haptic device found" guard — `SDL_CreateHapticEffect()`/`SDL_RunHapticEffect()`
are never reached at all, let alone the specific case of the former succeeding while the
latter fails. There is no mockable SDL boundary in this codebase for haptics (unlike
`Detail::IVibrateBackend`'s own fake used by `VibrateControllerTests`' fake-backend suite,
which only exercises `VibrateController`'s forwarding/clamping logic, not the platform's
internal native call sequence), so this specific failure path has
only been reasoned about from SDL3's own `SDL_RunHapticEffect()` documented failure modes
(e.g. device removed between create and run), not exercised. **Status: NOT RUN — hardware
validation open.**

**Steps:**
1. On a real haptic device, call `StartLeftRight(1.0f, 1.0f, TimeSpan::FromSeconds(2))` in
   a loop while physically disconnecting/reconnecting the device (or otherwise forcing
   `SDL_RunHapticEffect()` to fail after a successful `SDL_CreateHapticEffect()` — e.g. via
   a debugger breakpoint between the two calls that unplugs the device) to try to trigger
   the failing-`Run()` path.
2. Confirm with a debugger/platform diagnostic that the native run failure is actually observed.
3. Confirm no effect id is leaked: repeat step 1 many times and check (via SDL's own
   haptic effect count, if exposed by the platform, or simply that subsequent
   `StartLeftRight()`/`Start()`/`Stop()` calls keep behaving normally with no growing
   resource usage) that each failed `Run()` is followed by a clean `leftRightEffectId_`
   reset rather than an accumulating series of orphaned uploaded effects.

## 4b. Haptic device disconnect/reconnect mid-session (Task VIB2-004, 2026-07-17)

**Code under test:** `Sdl3Haptics::EnumerateIds()`/`RetireMissing()`, reached by every platform
haptic probe/play operation. A device unplugged mid-session is retired before the next operation;
a later call can select and open a replacement device without recreating `VibrateController`.

**Why this needs real hardware:** SDL3 has no haptic-specific hotplug event, so this fix detects
disconnect by re-querying `SDL_GetHaptics()` and comparing instance IDs before every operation —
this container never has a haptic device open in the first place (`OpenFirstHapticDevice()`
always returns `nullptr`), so `ReleaseHapticDeviceIfStale()`'s actual staleness branch
(`haptic_ != nullptr && !IsHapticDeviceStillConnected(haptic_)`) is never taken; every test run
here only exercises the no-op path where `haptic_` stays `nullptr` throughout. **Status: NOT
RUN — hardware validation open.**

**Steps:**
1. Connect a real haptic-capable device (phone motor via Android, USB force-feedback wheel, or
   any non-gamepad-excluded rumble device — see `IsConnectedGamepadHapticDevice()`). Confirm
   `VibrateController::getDefaultProperty()->getIsSupportedProperty()` returns `true` and
   `Start(TimeSpan::FromSeconds(1))` actually vibrates it.
2. Physically disconnect the device. Immediately call `Start()`, `Stop()`, and
   `StartLeftRight()` again — confirm none crash (VIB2-003 already makes the underlying SDL
   calls fail gracefully; this task additionally expects the stale handle to be closed and
   discarded on the very first post-disconnect call).
3. Call `getIsSupportedProperty()` after the disconnect — confirm it now returns `false` (not a
   stale cached `true`), and `getDeviceNameProperty()` returns an empty string, matching the
   "no device" contract `UnsupportedEnvironmentFullContract` already asserts for the
   never-had-a-device case.
4. Reconnect the same device (or connect a different rumble-capable one). Call
   `getIsSupportedProperty()` again — confirm it returns `true` again, and `Start()` actually
   vibrates the (re)connected device — all without recreating `VibrateController`'s singleton or
   restarting the process.
5. Repeat steps 2-4 a few times in a row to confirm the release-and-retry cycle is stable, not
   just a one-shot recovery.

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

| Device / OS | Backend | Action | Expected — strict XNA | Expected — `CNAEXT` extensions | Status |
|---|---|---|---|---|---|
| Android phone | `PlatformVibrateBackend` → `IPlatformHaptics` | `Start(TimeSpan)` | Phone's built-in motor buzzes for the given duration | `Start(TimeSpan, intensity)` scales buzz strength; `getIsSupportedProperty()` true; `getDeviceNameProperty()` non-empty | **NOT RUN** — no Android device in this session (Section 3) |
| Android phone | same | `StartLeftRight(large, small, duration)` | N/A (not real XNA API) | Blends to one intensity on the phone's single actuator (confirmed via SDL3 source, `VIB-003`) — do **not** expect two independently-felt motors | **NOT RUN**, but the single-actuator-blend *code path* is source-confirmed, not just assumed (`VIB-003`) |
| iOS phone | none yet (`VIB-004`, plan only) | `Start(TimeSpan)` | Deterministic silent no-op (no backend registered) until a `CHHapticEngine` backend is implemented | Same — `getIsSupportedProperty()` false | **DEFERRED** — no backend exists; nothing to run yet |
| Desktop, no haptic hardware present | `PlatformVibrateBackend`, no preferred device | `Start()`/`Stop()`/`StartLeftRight()`/`getIsSupportedProperty()`/`getDeviceNameProperty()` | Silent no-op for `Start`/`Stop`/`StartLeftRight`; `getIsSupportedProperty()` false; `getDeviceNameProperty()` empty | Same | **VERIFIED, this container** — platform migration tests exercise this environment deterministically |
| Desktop with a connected non-gamepad haptic device (e.g. a USB force-feedback wheel) | same, real device opened | `Start(TimeSpan)`/`Start(TimeSpan, intensity)` | Device actuates for the given duration; `getIsSupportedProperty()` true | Intensity scales strength; `StartLeftRight()` may achieve genuine independent magnitudes if the device/driver supports `SDL_HAPTIC_LEFTRIGHT` natively (unlike the Android single-actuator blend above) | **NOT RUN** — no such hardware available in this session |
| Desktop with a connected rumble-capable gamepad, no other haptic device | same | `getIsSupportedProperty()`, `Start()`, `GamePad::SetVibration()` | `VibrateController` excludes the gamepad's own haptic ID (`IsConnectedGamepadHapticDevice()`) — behaves as if unsupported; `GamePad::SetVibration()` drives the gamepad normally; neither API fights the other for the same motor | Same | **NOT RUN** — no gamepad connected in this session (full steps: Section 5 above) |

## 6. `Detail::AndroidSensorBridge` lifecycle safety (`plans/plan_devices.md` Task DEVICES-0085)

**Code under test:** `AndroidSensorBridge::Stop()`'s self-join-detects-and-detaches
logic (`src/Devices/Microsoft/Sensors/Detail/AndroidSensorBridge.cpp`) — confirmed by
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

## 6a. `AndroidSensorBridge` Probe cache locking and invalidation (Task ANDR2-002, 2026-07-17)

**Code under test:** `AndroidSensorBridge::Impl::Probe()`/`InvalidateProbeCache()` and
`IsAvailable()`'s now-locked call to `Probe()`. Before this fix, `IsAvailable()` called
`Probe()` (which reads/writes `manager_`/`sensor_`, plain pointers with no atomics) with no
lock at all, while `Start()` already called the same `Probe()` under `stateMutex_` — a
genuine, TSan-detectable data race between a concurrent `IsAvailable()` and `Start()` call
(or two concurrent `IsAvailable()` calls, before the fields are first populated). The fix
also adds `InvalidateProbeCache()`, called from three points in `Run()` (a failed
`ASensorManager_createEventQueue()`, a failed `ASensorEventQueue_enableSensor()`, and a run
of `MaxConsecutiveGetEventsFailures` consecutive `ASensorEventQueue_getEvents()` errors) —
each is this bridge's strongest available signal that the cached `manager_`/`sensor_`,
despite `Probe()` reporting them usable, may no longer be tied to a live sensor service.

**Why this needs real hardware:** confirmed the lock-discipline fix is correct by direct
code reasoning (the same mutex now serializes every access to `manager_`/`sensor_`,
matching `Start()`'s pre-existing discipline) and verified via a successful Android NDK
cross-compile of this exact translation unit. **Neither acceptance criterion could actually
be run in this environment:**
- "Concurrent IsAvailable/Start/Stop stress is TSan-clean" — this bridge's `#ifdef
  __ANDROID__` code (including `manager_`/`sensor_`/`Probe()` themselves) does not exist at
  all in the non-Android stub `Impl`, so it cannot even compile into a desktop TSan build,
  let alone run under one. Running an actual TSan-instrumented stress test requires either
  real Android hardware/an emulator with TSan support, or a future native fault-injection
  seam (`plans/plan_devices.md` Task `TEST2-005`) that can host this logic off-device.
- "A fake service restart re-probes successfully" — there is no seam in this codebase today
  to make `ASensorManager_createEventQueue()`/`ASensorEventQueue_enableSensor()`/
  `ASensorEventQueue_getEvents()` fail on demand to simulate a service restart; that also
  depends on `TEST2-005`'s planned fault-injection layer, or a real device/emulator run
  where the sensor service can genuinely be killed and restarted (e.g. `adb shell` service
  manipulation, where available) mid-session. **Status: NOT RUN — hardware/TEST2-005
  validation open.**

**Steps:**
1. On a real Android device/emulator, call `IsAvailable()`/`getIsSupportedProperty()` and
   `Start()`/`Stop()` concurrently from several threads in a tight loop (e.g. 30+ seconds)
   under a TSan-instrumented build, if one can be produced for this target — confirm no
   race is reported on `manager_`/`sensor_`.
2. If the device/emulator allows forcibly restarting the sensor service (e.g. via `adb
   shell`), do so while this bridge is started, and confirm `Run()` observes one of the
   three failure signals above, `InvalidateProbeCache()` fires, and a subsequent `Start()`
   call successfully re-probes and resumes delivering samples rather than silently reusing
   a now-dead cached `manager_`/`sensor_` forever.

## 6b. `AndroidSensorBridge` bounded event drain and backpressure counter (Task ANDR2-009, 2026-07-17)

**Code under test:** `AndroidSensorBridge::Impl::Run()`'s inner drain loop now caps how
many events it processes per outer-loop pass (`MaxEventsPerDrainBatch = 64`) before
yielding back to the outer loop, which re-polls the looper and re-checks
`rateChangeRequested_` immediately — previously the inner loop could, in principle, drain
a continuous high-rate event flood indefinitely without ever returning to the outer loop,
delaying a pending `SetSampleInterval()` request far longer than reasonable.
`GetDrainBatchLimitHitCountForTesting()` counts how often this cap actually fires.

**Important nuance found while investigating (do not re-litigate without re-checking the
code):** the required work's "starve Stop" framing is only partially accurate —
`stopRequested_` was **already** re-checked at the top of every single inner-loop
iteration (confirmed by reading `Run()` before this task), so `Stop()` itself was never
literally starved by this loop. What actually was unbounded was `rateChangeRequested_`.

**What is already verified, without hardware:** `GetDrainBatchLimitHitCountForTesting()`
itself (the getter, and its `0` default on a never-started/non-Android bridge) is
host-tested — 2 new tests in `AndroidSensorBridgeTests.cpp`. This does **not** exercise
the actual cap-hitting logic, which only fires from inside `Run()`'s real drain loop
(Android-only, and only reachable under a genuine high-rate event flood).

**Why this needs real hardware:** confirming the cap actually triggers under sustained
high-rate delivery, and that a pending rate-change request is applied measurably sooner
than it would have been without this cap, requires either a real high-rate Android sensor
or a native fault-injection layer that can simulate one (`TEST2-005`'s own scope) — this
container has neither. **Status: NOT RUN — hardware/TEST2-005 validation open.**

**Steps:**
1. On a real Android device, start a high-rate sensor stream (e.g. a game/demo requesting
   the fastest supported `TimeBetweenUpdates`) and, while it is under sustained load, call
   `SetSampleInterval()` with a different value — measure the delay before the new rate
   actually takes effect (observable via a change in delivered sample cadence).
2. If feasible, temporarily lower `MaxEventsPerDrainBatch` (or add ad hoc logging) to
   confirm `GetDrainBatchLimitHitCountForTesting()` actually increments under this load,
   and that the measured rate-change delay from step 1 improves relative to an unbounded
   drain loop (e.g. by temporarily reverting this fix for an A/B comparison).

## 6c. `AndroidSensorBridge` rate-set result and min-delay diagnostics (Task ANDR2-010, 2026-07-17)

**Code under test:** `Run()`'s two `ASensorEventQueue_setEventRate()` call sites (the
initial `Start()`-time rate and every subsequent live `SetSampleInterval()` update) now
record whether the platform accepted or rejected the requested rate
(`lastSetEventRateSucceededForTesting_`, reset to `true` by every `Start()` call so a
stale result never leaks across runs), exposed via
`AndroidSensorBridge::GetLastSetEventRateSucceededForTesting()`. Separately,
`GetMinDelayMicrosecondsForTesting()` exposes `ASensor_getMinDelay()` — the sensor's own
hardware/driver-documented minimum delay between events, independent of what was actually
requested.

**What is already verified, without hardware:** both getters' plumbing and their
sensible defaults (`true`/`0`) when no Android worker has ever run are host-tested (4 new
tests, `AndroidSensorBridgeTests.cpp`).

**Why this needs real hardware:** confirming a real device's sensor driver actually
*rejects* a requested rate under some realistic condition (so
`GetLastSetEventRateSucceededForTesting()` can be observed returning `false`, not just its
default `true`), and confirming `GetMinDelayMicrosecondsForTesting()` reports a sane,
sensor-type-appropriate value (e.g. matching a known device's published sensor
specifications) both require a real Android device — this container has neither a real
sensor driver to reject a rate nor real hardware specs to cross-check a min-delay value
against. "Continue delivery on nonfatal rejection" (required work) needed no code change
— confirmed already correct by reading `Run()`'s existing comments/logic, not assumed.
"Keep software throttling if required" (required work) was **not** attempted — whether
native rate-limiting is unreliable enough in practice to need a software backstop is a
question only answerable with real hardware measurements, not guessed at. **Status: NOT
RUN — hardware validation open.**

**Steps:**
1. On a real Android device, request a rate faster than a given sensor's own documented
   minimum (e.g. via a very small `TimeBetweenUpdates`) and confirm
   `GetLastSetEventRateSucceededForTesting()` correctly reports `false` if/when the
   platform rejects it, while delivery still continues at whatever rate was already in
   effect (not stopped).
2. Confirm `GetMinDelayMicrosecondsForTesting()`'s reported value for a known sensor type
   on a known device roughly matches that device's published sensor specifications (or,
   at minimum, is a plausible microsecond value, not obviously wrong).
3. If step 1 reveals the platform rejects requested rates often enough in practice to be
   a real concern (not just a theoretical possibility), that is the evidence needed to
   decide whether "keep software throttling if required" should actually be implemented —
   record the finding either way.

## 7. `Compass` real Android backend (`plans/plan_devices.md` Phase 7, Tasks DEVICES-0086-0100)

**Code under test:** `Detail::AndroidCompassBackend` (`src/Devices/Microsoft/Sensors/Detail/AndroidCompassBackend.cpp`)
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

## 7a. Compass fusion freshness/skew handling (Task COMP2-001, 2026-07-17)

**Code under test:** `AndroidCompassBackend::PublishReading()`'s new freshness check —
before fusing the rotation-vector stream's heading with the magnetic-field stream's
magnetometer/accuracy, both streams' most recent sample must be no older than
`ComputeCompassMaxSampleSkew(timeBetweenUpdates)` relative to "now" (`IsCompassSampleFresh()`,
`AndroidCompassMath.hpp`) — otherwise the publish is skipped entirely, rather than fusing a
fresh sample from one stream with an arbitrarily-stale one from the other.

**What is already verified, without hardware:** `ComputeCompassMaxSampleSkew()`/
`IsCompassSampleFresh()` are pure functions with no Android/sensor dependency, unlike the
rest of `AndroidCompassBackend`/`AndroidCompassMath.hpp` (entirely `#ifdef __ANDROID__`- or
hardware-behavior-dependent) — 9 new host-run unit tests
(`AndroidCompassMathTests.cpp`) directly prove the skew-threshold derivation (floored at
500ms, scales to 5x a larger requested interval) and the freshness boundary itself
(exactly-at-threshold is fresh, one unit beyond is stale, a future-dated sample is treated
as fresh, a multi-minute-old sample is correctly rejected). This is a **stronger**
evidentiary position than most other Section 16 fixes this pass (e.g. `VIB2-004`,
`SDLCORE-005`) — the underlying decision logic is directly tested, not merely reasoned
about.

**What is NOT verified without hardware:** the actual wiring inside
`AndroidCompassBackend::PublishReading()` — confirmed correct by code review and a
successful Android NDK cross-compile of this exact translation unit, but never run. This
container has no way to start a real `AndroidCompassBackend` (no Android device/emulator),
so the specific runtime scenario the required work and acceptance criteria describe — one
underlying `AndroidSensorBridge` (rotation-vector or magnetic-field) silently stopping mid-session
while the other keeps delivering, and confirming `Compass.CurrentValue` genuinely stops
advancing rather than continuing to report a fused-but-half-stale reading — has not been
observed. **Status: NOT RUN — hardware validation open** (matching this pass's
`VIB2-003`/`004`/`ANDR2-002`/`SDLCORE-005` precedent).

**Steps (once real hardware is available):**
1. Start `Compass` on a real device and confirm normal fused readings arrive.
2. Force one of the two underlying Android sensors (rotation vector or magnetic field) to
   stop delivering mid-session without the other stopping — e.g. via whatever
   emulator/ADB-level sensor control is available, or physically shielding a magnetometer
   from all magnetic fields for an extended period to trigger persistent read errors
   (`ANDR2-006`'s `MaxConsecutiveGetEventsFailures` path in the underlying bridge).
3. Confirm `Compass.CurrentValueChanged` stops firing (or `Compass.CurrentValue` stops
   changing) once the stalled stream's last sample exceeds `ComputeCompassMaxSampleSkew()`'s
   threshold — rather than continuing to publish readings that combine the still-live
   stream's fresh values with the stalled stream's frozen ones.
4. Let the stalled sensor resume (if possible) and confirm fused readings resume
   automatically, with no special recovery action needed (per `PublishReading()`'s own
   "self-heals once the stalled stream delivers again" design).

## 8. `Motion` real Android backend (`plans/plan_devices.md` Phase 8, Tasks DEVICES-0101-0119)

**Code under test:** `Detail::AndroidMotionBackend` (`src/Devices/Microsoft/Sensors/Detail/AndroidMotionBackend.cpp`)
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

**Cross-reference (2026-07-07, Task `ACCEL-008` → `MOTION-012`; resolved 2026-07-16):**
`ACCEL-008` decided to keep (and document + make opt-out-able) the landscape remap for
`Accelerometer`/`Gyroscope`. `MOTION-012` (`../audit_devices.md` `DEV-AUD-003`) has now
confirmed, via Android's own public developer documentation (`developer.android.com/guide/
topics/sensors/sensors_motion`: gravity/linear-acceleration/gyroscope sensors each
"use... the same [coordinate system as]... the acceleration sensor"; `sensors_overview`:
that coordinate system "never changes as the device moves") that `TYPE_GRAVITY`/
`TYPE_LINEAR_ACCELERATION`/`TYPE_GYROSCOPE` report in the exact same raw, device-fixed,
natural-orientation frame as `TYPE_ACCELEROMETER` — not already partially
orientation-corrected by Android's own sensor fusion, so there is no double-correction
risk. `AndroidMotionBackend.cpp` now applies the same
`Detail::ConvertAndroidPortraitToXnaLandscape()` remap (respecting the shared
`Detail::IsAndroidLandscapeRemapEnabled()` opt-out) to `Gravity`/`DeviceAcceleration`/
`DeviceRotationRate`, mirroring `Accelerometer.cpp`/`Gyroscope.cpp`'s own call sites.
**What remains unverified is the same thing Sections 1/2 already flag for
`Accelerometer`/`Gyroscope` themselves: the remap's *sign/axis* correctness on real
hardware**, not whether a remap should exist at all (that question is now closed).
`Motion.Attitude` (the quaternion) remains explicitly out of scope for this remap — a
quaternion isn't a plain vector, so the same sign-flip logic does not apply to it; any
fix there needs a real change-of-basis derivation, still `MOTION-002`'s own open
question.

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
7. **(Task `MOTION-012`, 2026-07-16)** Confirm steps 3-5 above still hold with the device
   physically rotated between the two supported landscape orientations (matching Section
   1's `Accelerometer` steps) — `Gravity`/`DeviceAcceleration`/`DeviceRotationRate` are now
   remapped the same way `Acceleration`/`RotationRate` already are. Also confirm
   `Detail::SetAndroidLandscapeRemapEnabled(false)` makes all three report Android's raw,
   unremapped portrait-frame axes instead, matching Section 1's equivalent opt-out check.
8. **(Task `MOTION-011`, 2026-07-16)** Cover the magnetometer with a hand or move to a
   location with magnetic interference; confirm `Motion.Calibrate` fires, matching
   `Compass.Calibrate`'s own already-verified-on-emulator-only behavior (Section 7).

If any step reveals a wrong sign/axis, the fix belongs in `ConvertRotationVectorToXnaQuaternion()`
(or `AndroidMotionBackend`'s vector handling for `Gravity`/`DeviceAcceleration`/
`DeviceRotationRate`) — never in downstream game code — and a new round-trip/self-consistency
test case should be added for whatever convention turns out correct.

## 8a. Motion fusion drop-frame counter and its deliberately-deferred redesign (Task MOT2-003, 2026-07-17)

**Code under test:** `AndroidMotionBackend::PublishReading()`'s existing `MOTION-007`
freshness check (fixed 500ms `MaxFusionAgeWindow` across all four fused sources'
timestamps) now also increments a new `droppedFusionFrameCountForTesting_` counter,
exposed via `GetDroppedFusionFrameCountForTesting()`, every time it fires.

**What this task does NOT implement, and why:** the required work's larger ask — bounded
per-source sample queues keyed by native timestamp, choosing the attitude sample as an
anchor and selecting/interpolating the nearest gravity/linear-acceleration/gyroscope
samples within a *tight, measured* skew (replacing the current fixed 500ms
latest-value-across-all-four bound), and proving lower fusion error than the current
logic against synthetic fast-motion fixtures — was investigated and deliberately
**deferred as its own, larger design task**, not rushed as part of this pass. Reasons:
1. "A tight, *measured* skew" literally requires empirical jitter measurement between
   Android's four independently-rated sensor streams on real hardware — there is no way
   to responsibly choose a specific tighter number without that measurement; guessing one
   would not actually satisfy the requirement's own wording.
2. The interpolation/nearest-sample-selection machinery itself is a genuine architecture
   change (bounded queues per source, a real interpolation algorithm for
   gravity/acceleration/gyro vectors bracketing the attitude sample's timestamp) — this is
   comparable in scope to `LIFE-007`/`010`/`011` (this backlog's other explicitly-deferred,
   large design tasks), not an isolated bug fix.
3. Only the counter (the required work's third, clearly-scoped bullet) could be added
   safely and immediately, without pretending to have solved the harder two.

**Why the counter needs real hardware to be meaningful:** this container never runs a
real `AndroidMotionBackend` (no Android device/emulator), so
`droppedFusionFrameCountForTesting_` has never actually incremented outside of code
reading — confirmed correct by inspection and a successful Android NDK cross-compile of
this exact translation unit, never observed incrementing at runtime.

**Steps (for whoever picks up the full redesign, not just the counter):**
1. On a real Android device, run `Motion` under normal use and log
   `GetDroppedFusionFrameCountForTesting()` periodically — confirm it stays at (or very
   near) zero under normal, gentle device handling, establishing a baseline.
2. Perform deliberately fast, jerky motion (a hard shake, a fast flick-rotation) and watch
   whether the counter increments — if MOTION-007's existing 500ms bound never trips even
   under fast motion, that is itself useful evidence about how urgent the full
   queue/interpolation redesign actually is in practice (as opposed to only in theory).
3. If picking up the full redesign: measure the *actual* inter-sample timestamp skew
   between the four streams during steps 1-2 above (via ad hoc logging) to derive a real,
   evidence-based "tight" threshold, rather than picking one arbitrarily — the required
   work's own "measured" wording is a literal instruction, not a suggestion.

## 8b. Motion attitude-source fallback diagnostic (Task MOT2-005, 2026-07-17)

**Code under test:** `Motion::getIsAttitudeNorthReferencedProperty()` (new `CNAEXT`
property) / `Detail::AndroidMotionBackend::IsUsingNorthReferencedAttitudeSource()` (new
`IMotionBackend` method) — reports whether the real Android backend's currently-active
attitude source is the magnetometer-fused, north-referenced `TYPE_ROTATION_VECTOR`, or
the drift-prone `TYPE_GAME_ROTATION_VECTOR` fallback (`usingGameRotationVector_`, set once
by `Start()`, per Task DEVICES-0104's original fallback logic). Before this task, nothing
exposed which of the two was actually in effect at all.

**What is already verified, without hardware:** the delegation plumbing itself
(`Motion` → `IMotionBackend` → `AndroidMotionBackend`) is fully host-testable via
`MotionTests.cpp`'s `FakeMotionBackend` (unlike most other Android-only fixes this pass)
— 4 new tests confirm the property returns `true` with no backend at all, correctly
mirrors a fake backend reporting either `true` or `false`, and throws
`ObjectDisposedException` after disposal, matching this class's other properties'
convention.

**Why this needs real hardware:** whether `AndroidMotionBackend::Start()` actually picks
`TYPE_GAME_ROTATION_VECTOR` in the expected circumstance (the device genuinely lacking a
usable `TYPE_ROTATION_VECTOR` — usually meaning no magnetometer, or one currently
uncalibrated/unavailable) — and whether the property then correctly reports `false` end
to end — has never been observed on a real device/emulator. **Status: NOT RUN — hardware
validation open**, matching this pass's established precedent for Android-only runtime
behavior.

**Steps:**
1. On a real Android device with a working magnetometer, start `Motion` and confirm
   `getIsAttitudeNorthReferencedProperty()` reports `true`.
2. If possible, disable/cover the magnetometer (or use a device/emulator profile known to
   lack `TYPE_ROTATION_VECTOR`) and confirm `AndroidMotionBackend` falls back to
   `TYPE_GAME_ROTATION_VECTOR` and the property correctly reports `false`.
3. With the fallback active, let the device sit still for an extended period (several
   minutes) and confirm `Motion.CurrentValue.Attitude`'s yaw visibly drifts over time
   (the expected, documented behavior of the game rotation vector with no absolute
   reference) — this is the concrete symptom the property exists to let an application
   detect and react to.

---

## 9. Emulator limitations for Devices testing (`plans/plan_devices.md` Task DEVICES-0129)

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

Use `docs/devices_sensor_hardware_qa_template.md` to record what you actually observed
for each section above, in a reusable, comparable format — copy it to a new file per
test session rather than editing the template in place.

If any item above reveals an actual bug (wrong sign, no vibration, gamepad conflict),
file it the same way as any other confirmed bug in this project: root-cause it against
the actual SDL3/Android behavior observed, then fix the specific `Accelerometer.cpp`/
`Gyroscope.cpp`/`VibrateController.cpp` logic — never adjust downstream game code to
compensate for a coordinate-convention or motor-selection bug in this layer.
