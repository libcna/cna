# Microsoft::Devices — Manual Hardware Verification Checklist

## Purpose

Automated `Microsoft::Devices`/`Microsoft::Devices::Sensors` tests run on hosts without
accelerometer, gyroscope, magnetometer or haptic hardware. They either exercise the
no-hardware silent-no-op / `SensorState::NotSupported` path, or drive the real
`CurrentValueChanged`/`ReadingChanged` dispatch through the `CNAEXT` synthetic-injection hooks
(`InjectSyntheticSensorUpdate()`, `SetStartedForTesting()`; Task P4-2) and fake backends. That
proves the C++ plumbing, not that the *numbers* flowing through it are physically correct.

This checklist closes the gap between "compiles and dispatches correctly" and "is physically
correct" for whoever runs CNA on a physical Android device, an iOS device (once an iOS backend
exists), or a desktop with a real accelerometer/gyroscope/haptic controller. None of it can be
verified in a headless container.

**Recording results (Task `DEMO-002`):** this file is "what to check";
`docs/devices_sensor_hardware_qa_template.md` is the companion "how to record a run" template,
with one section per numbered section below plus device/OS/commit metadata, so results stay
comparable across testers, devices and sessions.

## Still open on real hardware

Each row below is implemented and host-tested, but its acceptance needs a physical device, and none
has been run on one. The SDL sensor and haptic code those rows inspected lives behind
`CNA::Platform::IPlatformSensors`/`IPlatformHaptics`
(`modules/platform/src/Sdl3/Sdl3DeviceServices.cpp`), so recheck an SDL finding there rather than
at the file the row names.

| Row | What a device has to show | Section |
|---|---|---|
| `ACCEL-004` | accelerometer axis signs in portrait and both landscapes | §1 |
| `GYRO-003` | gyroscope axes and the Android orientation remap | §2 |
| `SDLCORE-005` | a sensor removed, re-added or replaced mid-session is detected and reopened | §2a |
| `VIB2-005` | the phone vibrator against a direct backend | §3 |
| `VIB2-003` | a failing haptic effect is cleaned up and reported | §4a |
| `VIB2-004` | haptic disconnect/reconnect mid-session | §4b |
| `VIB2-006` | zero duration, repeated `Start`, intensity zero, mutual exclusion on a real backend | §5a |
| `SDLCORE-011` | closing a haptic device after SDL has shut its subsystem down; from SDL's source this is a heap use-after-free, never reproduced under ASan for want of an opened device | — |
| `ANDR2-002` | the Probe cache under TSan stress and a real sensor-service restart | §6a |
| `ANDR2-009` | Stop latency and coalescing with the bounded event drain | §6b |
| `ANDR2-010` | requested versus effective sample interval | §6c |
| `COMPASS-004`, `COMP2-003` | Android heading math and the derived Android-to-Windows-Phone basis | §7 |
| `COMP2-004` | a physical compass truth table (cardinal headings, tilts) | §7 |
| `COMP2-005` | `MagnetometerReading` units and axes | §7 |
| `COMP2-001` | compass source freshness/skew alignment end to end | §7a |
| `MOTION-002`, `MOT2-001` | quaternion/attitude mapping: cardinal yaw/pitch/roll poses match Windows Phone; the vector-remap contract is in §1 | §8 |
| `MOT2-010` | a physical Motion pose and dynamics matrix | §8 |
| `MOT2-005` | rotation-vector versus game-rotation-vector fallback, and drift | §8b |
| `TEST2-006`, `TEST2-007` | real Android evidence as a release gate; iOS and desktop matrices | — |
| `PERF2-004` | mobile power and thermal budgets | — |

---

## 1. Accelerometer axis sign/orientation

**Code under test:** the Android accelerometer and gyroscope adapters and Motion's vector
conversion share `Detail::ConvertAndroidPortraitToXnaLandscape()` in
`modules/devices/include/Microsoft/Devices/Sensors/Detail/AndroidSensorOrientation.hpp`.
The helper models a natural-portrait device in the two landscape orientations. Its output
axes are landscape right (+X), landscape top (+Y), and out of the screen (+Z).

The [Android sensor coordinate contract](https://developer.android.com/develop/sensors-and-location/sensors/sensors_overview#sensors-coords)
defines a fixed natural-device frame; display rotation does not rotate sensor samples.
[Display.getRotation](https://developer.android.com/reference/android/view/Display#getRotation())
reports the graphics rotation, opposite to the physical device turn. Thus Rotation90 means
the portrait top points left, and Rotation270 means it points right. From those basis vectors:

| Landscape orientation | Portrait +X becomes | Portrait +Y becomes | Converted `(x, y, z)` |
|---|---|---|---|
| Rotation90 | +Y | -X | `(-y, x, z)` |
| Rotation270 | -Y | +X | `(y, -x, z)` |

Both are proper rotations: determinant +1, unchanged lengths and cross products, inverse
90/270 transforms, and unchanged Z (CNA-FIX-D22). There is no measured Windows Phone contract that
puts landscape right on output Y.
`AndroidSensorOrientationTests` verifies the basis and these independent geometric properties;
`AndroidMotionMathTests` also checks acceleration/gravity unit conversion and raw opt-out.
These are host mathematical tests, not a physical sensor qualification.

**Retained owner decision (`ACCEL-008`):** this is a CNA convenience, enabled by default.
`Detail::SetAndroidLandscapeRemapEnabled(false)` disables it for Accelerometer, Gyroscope
and Motion's three vector fields, exposing their device-fixed axes after unit conversion.
Windows Phone's fixed sensor frame does not require this convenience. Motion.Attitude stays
in the device/world frame and is not display-remapped by this switch.

**Hardware checks still required:**

1. Run on a natural-portrait Android device and record the raw vector, platform display rotation
   and converted output together. Check both physical landscape turns against the table.
2. With remapping enabled, verify screen-horizontal changes affect X and screen-vertical changes
   affect Y in both rotations. Confirm the screen-normal Z component agrees with the raw sample.
   Distinguish accelerometer/gravity force signs from the direction in which an edge was tilted.
3. Disable remapping and confirm the same samples retain their device-fixed components, allowing
   for accelerometer/gravity unit normalization. Re-enable it and repeat.
4. Confirm the application's actual orientation policy selects the expected two landscape states.
   The helper does not establish portrait support, natural-landscape tablet behavior, or the
   correctness of a particular platform's display-orientation query.

## 2. Gyroscope axis correctness

The same proper rotation applies to angular velocity in radians per second; it preserves
right-handed cross products as well as the acceleration vectors' lengths. The shared default
and opt-out are described in Section 1.

On hardware, rotate about each screen-local axis in each landscape orientation, record the raw
and remapped samples, and compare with the table. Confirm the two landscape poses give consistent
screen-local angular-rate directions. These checks must exercise the real adapter and event path;
passing pure math tests does not qualify sensor calibration, event delivery or hardware signs.

## 2a. Platform sensor disconnect/reconnect and default-device change

**Code under test:** `Detail::PlatformSensorSubsystem<TSensor>` and
`IPlatformSensors::OpenSensor()` (shared behavior for `Accelerometer` and `Gyroscope`). A fresh
platform session is opened for each stopped-to-started cycle, so a disconnected device cannot
leave a cached native handle behind and a replacement/default device can be selected normally.

**Why this needs real hardware:** SDL3 has no sensor-specific hotplug event (only
`SDL_EVENT_SENSOR_UPDATE`), so a disconnect is detected only by re-querying the device list when a
session is opened. A host without sensors only ever exercises the "no device" path; a genuine
remove/re-add/default-device change needs real hardware or a native fault-injection layer for the
sensor enumeration/open/close calls (`TEST2-005`). **Status: NOT RUN — hardware or TEST2-005
validation open.**

**Accepted gap:** a device that disappears while an instance is already started is not detected
mid-session. Only the next `Start()` (of any instance) opens a fresh session. Continuous
disconnect monitoring of a running instance would be new design work, not part of this contract.

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

**Code under test:** `VibrateController` forwards to `Detail::PlatformVibrateBackend`
(`modules/devices/src/Detail/PlatformVibrateBackend.cpp`), which holds the platform's
`PlatformSubsystem::Haptic` reference and calls `IPlatformHaptics`. On SDL3 that service is
`Sdl3Haptics` in `modules/platform/src/Sdl3/Sdl3DeviceServices.cpp`:
`GetDefaultVibrationDevice()` selects the first enumerated device that is not a gamepad's
haptic (the file-local `IsGamepadHapticDevice()` ID correlation through
`SDL_OpenHapticFromJoystick()`, Task P4-10's gamepad-exclusion fix), and
`PlayRumble()`/`StopAll()` carry the plain `Start(TimeSpan)`/`Start(TimeSpan, intensity)`/`Stop()`
rumble path. Opened `SDL_Haptic` handles are owned by `Sdl3Haptics` and closed by its
destructor or `Deactivate()`; the backend releases its `Haptic` subsystem reference in
`ReleaseService()` while the same platform is still installed. These are resource-lifetime
contracts, **not** a hardware verification; the status below is unchanged by them.

**Why this needs real hardware:** SDL3's Android haptic backend automatically registers the
phone's own vibration motor as a haptic device once the `Haptic` subsystem initializes, with no
custom JNI/Java bridge code in this project (see [devices-android.md](devices-android.md)) —
this has never been confirmed to actually reach `Vibrator.vibrate(milliseconds)` on a real
device.

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

## 4a. `StartLeftRight()` cleans up an effect whose `SDL_RunHapticEffect()` fails (Task VIB2-003)

**Code under test:** `Sdl3Haptics::PlayLeftRight()`'s handling of a
successfully-uploaded (`SDL_CreateHapticEffect()` succeeds) but then failing
`SDL_RunHapticEffect()` call — the fix destroys the just-uploaded effect
(`SDL_DestroyHapticEffect()` through `DestroyLeftRight()`) and erases the device's
`leftRightEffects_` entry immediately, rather than
leaving an uploaded-but-never-playing effect slot allocated until the next
`Start()`/`StartLeftRight()`/`Stop()`/destructor call happens to reclaim it.

**Why this needs real hardware:** with no haptic device opened (`GetDefaultVibrationDevice()` finds
none), `StartLeftRight()` returns before `PlayLeftRight()` is called, and there is no mockable SDL
haptic boundary: `VibrateControllerTests`' fake `Detail::IVibrateBackend` covers only
`VibrateController`'s forwarding/clamping, not the platform's native call sequence. The failure
path is reasoned from SDL3's documented `SDL_RunHapticEffect()` failure modes (e.g. device removed
between create and run). **Status: NOT RUN — hardware validation open.**

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
   resource usage) that each failed `Run()` is followed by a clean `leftRightEffects_`
   erase rather than an accumulating series of orphaned uploaded effects.

## 4b. Haptic device disconnect/reconnect mid-session (Task VIB2-004)

**Code under test:** `Sdl3Haptics::EnumerateIds()`/`RetireMissing()`, reached by every platform
haptic probe/play operation. A device unplugged mid-session is retired before the next operation;
a later call can select and open a replacement device without recreating `VibrateController`.

**Why this needs real hardware:** SDL3 has no haptic-specific hotplug event, so disconnect is
detected by re-querying `SDL_GetHaptics()` and comparing instance IDs before every operation. With
no haptic device present, `Sdl3Haptics::Acquire()` never adds an entry to `open_`, so the
retirement branch of `RetireMissing()` (an `open_` entry whose ID is no longer enumerated is closed
with `SDL_CloseHaptic()` and its `rumbleInitialized_`/`leftRightEffects_` state erased) is never
taken. **Status: NOT RUN — hardware validation open.**

**Steps:**
1. Connect a real haptic-capable device (phone motor via Android, USB force-feedback wheel, or
   any non-gamepad-excluded rumble device — see `IsGamepadHapticDevice()`). Confirm
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

**Code under test:** `Sdl3Haptics::GetDefaultVibrationDevice()` and its file-local
`IsGamepadHapticDevice()` in `modules/platform/src/Sdl3/Sdl3DeviceServices.cpp` (Task
P4-10's `SDL_OpenHapticFromJoystick()`-based ID correlation) — the whole point of this
filter is that `VibrateController` and
`Microsoft::Xna::Framework::Input::GamePad::SetVibration()` must never both try to drive
the *same* physical controller's rumble motor, since XNA's `VibrateController` targets
the phone's own vibration motor, not a connected gamepad's.

**Why this needs real hardware:** the exclusion — the entire reason for Task P4-10 — is unit-tested
and reasoned from SDL3's API documentation, but has never been exercised against a physically
connected controller.

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

## 5a. Vibration validation matrix (Task VIB-010)

Consolidates Sections 3-5 into one matrix, plus two cases neither section covers as its own row
(desktop with no haptics at all, and desktop with a non-gamepad haptic device). Record a run's
results in `docs/devices_sensor_hardware_qa_template.md`, not here.

| Device / OS | Backend | Action | Expected — strict XNA | Expected — `CNAEXT` extensions | Status |
|---|---|---|---|---|---|
| Android phone | `PlatformVibrateBackend` → `IPlatformHaptics` | `Start(TimeSpan)` | Phone's built-in motor buzzes for the given duration | `Start(TimeSpan, intensity)` scales buzz strength; `getIsSupportedProperty()` true; `getDeviceNameProperty()` non-empty | **NOT RUN** (Section 3) |
| Android phone | same | `StartLeftRight(large, small, duration)` | N/A (not real XNA API) | Blends to one intensity on the phone's single actuator (from SDL3's source, `VIB-003`) — do **not** expect two independently-felt motors | **NOT RUN**; the single-actuator blend is source-confirmed (`VIB-003`) |
| iOS phone | none (`VIB-004` design only) | `Start(TimeSpan)` | Deterministic silent no-op (no backend registered) until a `CHHapticEngine` backend is implemented | Same — `getIsSupportedProperty()` false | **DEFERRED** — no backend exists |
| Desktop, no haptic hardware present | `PlatformVibrateBackend`, no preferred device | `Start()`/`Stop()`/`StartLeftRight()`/`getIsSupportedProperty()`/`getDeviceNameProperty()` | Silent no-op for `Start`/`Stop`/`StartLeftRight`; `getIsSupportedProperty()` false; `getDeviceNameProperty()` empty | Same | **VERIFIED** — the platform migration tests exercise this case deterministically on any host without haptics |
| Desktop with a connected non-gamepad haptic device (e.g. a USB force-feedback wheel) | same, real device opened | `Start(TimeSpan)`/`Start(TimeSpan, intensity)` | Device actuates for the given duration; `getIsSupportedProperty()` true | Intensity scales strength; `StartLeftRight()` may achieve genuine independent magnitudes if the device/driver supports `SDL_HAPTIC_LEFTRIGHT` natively (unlike the Android single-actuator blend above) | **NOT RUN** |
| Desktop with a connected rumble-capable gamepad, no other haptic device | same | `getIsSupportedProperty()`, `Start()`, `GamePad::SetVibration()` | `VibrateController` excludes the gamepad's own haptic ID (`Sdl3Haptics`' `IsGamepadHapticDevice()`) — behaves as if unsupported; `GamePad::SetVibration()` drives the gamepad normally; neither API fights the other for the same motor | Same | **NOT RUN** (full steps: Section 5) |

## 6. `Detail::AndroidSensorBridge` lifecycle safety (Task DEVICES-0085)

**Code under test:** `AndroidSensorBridge::Stop()`'s self-join detection
(`modules/devices/src/Sensors/Detail/AndroidSensorBridge.cpp`): stopping or disposing the owning
`Compass`/`Motion` from within its own `CurrentValueChanged` handler, which runs on the bridge's
worker thread, must not deadlock — `Stop()` detects `std::this_thread::get_id() ==
worker_.get_id()` and detaches instead of joining. The `#ifdef __ANDROID__` path is
cross-compiled and reviewed but has never run.

**Why this needs real hardware:** whether the detached thread really leaves `Run()`'s loop cleanly
afterward, and whether any Android sensor-subsystem resource leaks, can only be observed on a
device.

**Steps:**
1. On a real Android device, construct a `Compass`/`Motion` instance, `Start()` it, and
   subscribe a `CurrentValueChanged` handler that calls `Dispose()` on the same instance.
2. Confirm the process does not hang or deadlock when a sensor reading arrives.
3. Confirm no crash/use-after-free on subsequent readings (there should be none, since
   the instance is disposed) or on process exit.

## 6a. `AndroidSensorBridge` Probe cache locking and invalidation (Task ANDR2-002)

**Code under test:** `AndroidSensorBridge::Impl::Probe()`/`InvalidateProbeCache()`.
`IsAvailable()` calls `Probe()` under `stateMutex_`, as `Start()` does, so a concurrent
`IsAvailable()`/`Start()` (or two first `IsAvailable()` calls) cannot race on the plain
`manager_`/`sensor_` pointers. `Run()` calls `InvalidateProbeCache()` on its three strongest signs
that the cached `manager_`/`sensor_` may no longer be tied to a live sensor service: a failed
`ASensorManager_createEventQueue()`, a failed `ASensorEventQueue_enableSensor()`, and
`MaxConsecutiveGetEventsFailures` consecutive `ASensorEventQueue_getEvents()` errors.

**Why this needs real hardware:** the Android code (including `manager_`/`sensor_`/`Probe()`) does
not exist in the non-Android stub `Impl`, so it cannot run in a desktop TSan build, and there is no
seam to make the NDK queue calls fail on demand to simulate a service restart. Both need a real
device/emulator (e.g. restarting the sensor service through `adb shell`) or the planned `TEST2-005`
fault-injection layer. **Status: NOT RUN — hardware/TEST2-005 validation open.**

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

## 6b. `AndroidSensorBridge` bounded event drain and backpressure counter (Task ANDR2-009)

**Code under test:** `AndroidSensorBridge::Impl::Run()`'s inner drain loop processes at most
`MaxEventsPerDrainBatch = 64` events per outer-loop pass before returning to the outer loop, which
re-polls the looper and re-checks `rateChangeRequested_`, so a continuous high-rate event flood
cannot delay a pending `SetSampleInterval()` indefinitely. `stopRequested_` is re-checked on every
inner iteration, so `Stop()` itself was never starved; the bound exists for the rate change.
`GetDrainBatchLimitHitCountForTesting()` counts how often the cap fires.

**Verified without hardware:** the getter and its `0` default on a never-started/non-Android
bridge (`AndroidSensorBridgeTests.cpp`). The cap itself fires only inside `Run()`'s real drain loop
under a genuine high-rate flood, which needs a real high-rate sensor or `TEST2-005`'s
fault-injection layer. **Status: NOT RUN — hardware/TEST2-005 validation open.**

**Steps:**
1. On a real Android device, start a high-rate sensor stream (e.g. a game/demo requesting
   the fastest supported `TimeBetweenUpdates`) and, while it is under sustained load, call
   `SetSampleInterval()` with a different value — measure the delay before the new rate
   actually takes effect (observable via a change in delivered sample cadence).
2. If feasible, temporarily lower `MaxEventsPerDrainBatch` (or add ad hoc logging) to
   confirm `GetDrainBatchLimitHitCountForTesting()` actually increments under this load,
   and that the measured rate-change delay from step 1 improves relative to an unbounded
   drain loop (e.g. by temporarily reverting this fix for an A/B comparison).

## 6c. `AndroidSensorBridge` rate-set result and min-delay diagnostics (Task ANDR2-010)

**Code under test:** both `ASensorEventQueue_setEventRate()` call sites in `Run()` (the initial
`Start()` rate and every live `SetSampleInterval()` update) record whether the platform accepted
the requested rate (`GetLastSetEventRateSucceededForTesting()`, reset to `true` by every `Start()`
so a stale result never leaks across runs). `GetMinDelayMicrosecondsForTesting()` exposes
`ASensor_getMinDelay()`, the driver-documented minimum delay between events. Delivery continues
on a nonfatal rejection.

**Verified without hardware:** both getters' plumbing and their `true`/`0` defaults when no Android
worker has run (`AndroidSensorBridgeTests.cpp`). Observing a real rejection, and a sane
sensor-specific minimum delay, needs a real device and its published sensor specifications.
Software throttling is not implemented: whether native rate limiting is unreliable enough to need a
software backstop can only be answered by hardware measurements. **Status: NOT RUN — hardware
validation open.**

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

## 7. `Compass` real Android backend (Tasks DEVICES-0086-0100)

**Code under test:** `Detail::AndroidCompassBackend`
(`modules/devices/src/Sensors/Detail/AndroidCompassBackend.cpp`) and the heading functions in
`modules/devices/include/Microsoft/Devices/Sensors/Detail/AndroidCompassMath.hpp`
(`ConvertRotationVectorToMagneticHeadingDegreesWithTiltMode()` and the flat/upright conventions it
selects between). Reviewed, cross-compiled, and unit-tested for self-consistency of the pure
azimuth/accuracy math; never run on a device.

**Why this needs real hardware:** the azimuth formula reproduces Android's documented world-frame
axis convention from first-principles quaternion algebra. Axis-sign math of this kind is easy to
get subtly wrong in a way that tests against self-derived expected values cannot catch (as the
accelerometer/gyroscope landscape remap showed); only comparison with a real device's own compass
app can.

**Tilt modes (`COMPASS-004`/`COMPASS-009`):** like the WP7 `Compass` (MSDN `hh220912`/`hh202974`),
the backend switches between flat-mode and upright-mode axis conventions by the device's tilt
(`IsDeviceInUprightCompassMode()`). Run the steps below in both poses.

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

## 7a. Compass fusion freshness/skew handling (Task COMP2-001)

**Code under test:** before `AndroidCompassBackend::PublishReading()` fuses the rotation-vector
heading with the magnetic-field stream's magnetometer/accuracy, both streams' latest samples must be
no older than `ComputeCompassMaxSampleSkew(timeBetweenUpdates)` (`IsCompassSampleFresh()`,
`AndroidCompassMath.hpp`); otherwise the publish is skipped, rather than fusing a fresh sample from
one stream with an arbitrarily stale one from the other. It self-heals once the stalled stream
delivers again.

**Verified without hardware:** `ComputeCompassMaxSampleSkew()`/`IsCompassSampleFresh()` are pure
functions, and `AndroidCompassMathTests.cpp` tests the threshold derivation (floored at 500 ms,
scaling to 5x a larger requested interval) and the freshness boundary (exactly at the threshold is
fresh, one unit beyond is stale, a future-dated sample is fresh, a multi-minute-old sample is
rejected). The wiring inside `PublishReading()` is reviewed and cross-compiled but has not run: one
underlying `AndroidSensorBridge` stopping mid-session while the other keeps delivering has not
been observed. **Status: NOT RUN — hardware validation open.**

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

## 8. `Motion` real Android backend (Tasks DEVICES-0101-0119)

**Code under test:** `Detail::AndroidMotionBackend`
(`modules/devices/src/Sensors/Detail/AndroidMotionBackend.cpp`) and
`Detail::ConvertRotationVectorToXnaQuaternion()`/`ExtractYawPitchRollFromQuaternion()`
(`modules/devices/include/Microsoft/Devices/Sensors/Detail/AndroidMotionMath.hpp`). Reviewed,
cross-compiled, and covered by round-trip tests of the yaw/pitch/roll extraction against
`Quaternion::CreateFromYawPitchRoll()`; never run on a device.

**Why this needs real hardware:** quaternion normalization and Euler round-trips have host
regressions, and the optional vector remap is a proper rotation (Section 1, CNA-FIX-D22).
The real rotation-vector source, physical poses, calibration and platform orientation still need
measurement. `ConvertRotationVectorToXnaQuaternion()` preserves the device/world frame; it is not
changed by the vector-remap switch. With remapping enabled, the vector fields are deliberately
screen-relative while Attitude remains device-relative.

`AndroidMotionBackend.cpp` applies the same shared conversion to Gravity, DeviceAcceleration
and DeviceRotationRate. Android defines those source sensors in the same natural-device frame
as Accelerometer; it has not already applied CNA's display remap. Disabling the switch retains
that frame for all three vectors after the existing unit conversion.

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
7. **(Task `MOTION-012`)** Confirm steps 3-5 above still hold with the device
   physically rotated between the two supported landscape orientations (matching Section
   1's `Accelerometer` steps) — `Gravity`/`DeviceAcceleration`/`DeviceRotationRate` are now
   remapped the same way `Acceleration`/`RotationRate` already are. Also confirm
   `Detail::SetAndroidLandscapeRemapEnabled(false)` makes all three report Android's raw,
   unremapped portrait-frame axes instead, matching Section 1's equivalent opt-out check.
8. **(Task `MOTION-011`)** Cover the magnetometer with a hand or move to a
   location with magnetic interference; confirm `Motion.Calibrate` fires, matching
   `Compass.Calibrate`'s behavior (Section 7).

If any step reveals a wrong sign/axis, the fix belongs in `ConvertRotationVectorToXnaQuaternion()`
(or `AndroidMotionBackend`'s vector handling for `Gravity`/`DeviceAcceleration`/
`DeviceRotationRate`) — never in downstream game code — and a new round-trip/self-consistency
test case should be added for whatever convention turns out correct.

## 8a. Motion fusion drop-frame counter and its deliberately-deferred redesign (Task MOT2-003)

**Code under test:** `AndroidMotionBackend::PublishReading()`'s `MOTION-007` freshness check (a
fixed 500 ms `MaxFusionAgeWindow` across all four fused sources' timestamps) increments
`droppedFusionFrameCountForTesting_`, exposed through `GetDroppedFusionFrameCountForTesting()`,
every time it fires.

**Deferred redesign, and why:** bounded per-source sample queues keyed by native timestamp, with
the attitude sample as anchor and gravity/linear-acceleration/gyroscope samples selected or
interpolated within a *tight, measured* skew, would replace the fixed latest-value bound. It is a
separate architecture change (comparable to the deferred `LIFE-007`/`010`/`011` designs), and a
"measured" skew requires empirical jitter measurement between Android's four independently rated
streams on real hardware; choosing a number without that measurement would not meet the
requirement. Only the counter is implemented.

**Why the counter needs real hardware to be meaningful:** it increments only inside a real
`AndroidMotionBackend`; it is reviewed and cross-compiled but has never been observed incrementing.

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

## 8b. Motion attitude-source fallback diagnostic (Task MOT2-005)

**Code under test:** `Motion::getIsAttitudeNorthReferencedProperty()` (`CNAEXT`) /
`Detail::AndroidMotionBackend::IsUsingNorthReferencedAttitudeSource()` (an `IMotionBackend`
method) report whether the active attitude source is the magnetometer-fused, north-referenced
`TYPE_ROTATION_VECTOR` or the drift-prone `TYPE_GAME_ROTATION_VECTOR` fallback
(`usingGameRotationVector_`, set once by `Start()`; Task DEVICES-0104).

**Verified without hardware:** the `Motion` → `IMotionBackend` → backend delegation, through
`MotionTests.cpp`'s `FakeMotionBackend`: the property is `true` with no backend, mirrors a fake
backend reporting `true` or `false`, and throws `ObjectDisposedException` after disposal. Whether
`Start()` actually picks the game rotation vector when a device lacks a usable rotation vector, and
the property then reports `false` end to end, has not been observed. **Status: NOT RUN — hardware
validation open.**

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

## 9. Emulator limitations for Devices testing (Task DEVICES-0129)

An Android emulator (`docs/devices-build.md` Section 4.1) answers "does the software pipeline work
at all", but is **not a substitute** for any item above that asks "does this read or feel correct
on a real device":

- **No real vibration motor.** Android emulators have no physical haptic actuator —
  `VibrateController::Start()` can be confirmed to run without crashing, but Section 3/4's "does it
  actually buzz" steps need a real device.
- **Virtual, not physical, sensor motion.** The emulator console (`sensor set acceleration
  <x>:<y>:<z>`, `sensor set magnetic-field <x>:<y>:<z>`, etc.) injects arbitrary values instantly,
  which confirms the C++ dispatch pipeline delivers *whatever value is injected* end to end (the
  demo's `DrawEventFlash()` indicator responds), but proves nothing about whether a real tilt or
  rotation produces the *correct* value — Sections 1/2/7/8's axis-sign questions still need a real
  device.
- **No virtual rotation-vector/game-rotation-vector sensor in the `Medium_Phone` console command
  set,** so `Detail::AndroidSensorBridge`'s `Compass`/`Motion` path (`ASENSOR_TYPE_ROTATION_VECTOR`
  etc.) has only been built and launched, not fed injected values. Whether the system image's
  virtual sensor HAL exposes this sensor type at all is unconfirmed; check before assuming an
  emulator can close any part of Sections 7/8.
- **The emulator's own system apps can become unresponsive under load** ("Pixel Launcher isn't
  responding", "System UI isn't responding", likely from `-gpu swiftshader_indirect` software
  rendering). That is an emulator/environment issue, not a `cna_demo_devices` bug: check the demo's
  own process with `adb shell pidof`.

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
