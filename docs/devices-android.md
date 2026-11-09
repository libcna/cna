# `Microsoft::Devices` on Android — Consolidated Reference

Every Android-specific `Microsoft::Devices` decision, in one place. Deeper detail lives in
`docs/devices-native-backend-design.md` (architecture and field mappings), `docs/devices-build.md`
(build, APK and emulator commands) and `docs/devices-hardware-checklist.md` (manual verification).

## Vibration: no native bridge exists, and none is needed

`VibrateController` uses **only** SDL3's own haptic backend on Android — no JNI, no custom bridge,
and no `IDeviceVibrationBackend` seam to select one (Task DEVICES-0031). SDL3's Android haptic code
(`third_party/SDL/src/haptic/android/SDL_syshaptic.c` and `SDLControllerManager.java`'s
`SDLHapticHandler`/`SDLHapticHandler_API26`/`SDLHapticHandler_API31`) already queries
`Context.VIBRATOR_SERVICE` directly (the phone's own vibrator, separate from any connected
gamepad's) and implements amplitude control end to end (`VibrationEffect.createOneShot()` on
API 26+, `VibratorManager` on API 31+), including the `intensity==0 → stop()` and
`intensity*255` clamped to `[1,255]` mapping a custom bridge would have to reinvent. A second
native backend behind a seam with one real implementation would be speculative abstraction.

**`StartLeftRight()` on a phone blends both motors into one intensity (VIB-003/VIB-008).** The
call path is an `SDL_HAPTIC_LEFTRIGHT` effect → `SDL_SYS_HapticRunEffect()` (`SDL_syshaptic.c`),
which computes
`total = (large_magnitude/32767 * 0.6f) + (small_magnitude/32767 * 0.4f)` before calling the
single-intensity `Android_JNI_HapticRun()`. SDL3's genuinely independent dual-motor path
(`Android_JNI_HapticRumble()` → `SDLHapticHandler_API31.rumble()`, which drives up to two vibrators
through `InputDevice.getVibratorManager()`) is wired only from the controller-rumble path
(`SDL_RumbleJoystick`, i.e. `GamePad::SetVibration()`), never from the generic `SDL_Haptic` effect
path `VibrateController` uses. This is not a CNA bug: CNA calls the documented SDL3 API, and SDL
applies the same `0.6`/`0.4` weighting in its Java single-vibrator fallback. Gameplay code must not
assume per-motor independence on a phone. Desktop dual-actuator force-feedback hardware (non-gamepad,
so not excluded by `Sdl3Haptics`' `IsGamepadHapticDevice()`) is unaffected by this blending and may
drive independent magnitudes where its driver supports `SDL_HAPTIC_LEFTRIGHT` natively; that is not
verified on real hardware.

## Sensors: pure NDK native, no JNI, no Java bridge

Compass/Motion's Android backends use `<android/sensor.h>`/`<android/looper.h>`
(`ASensorManager`/`ASensorEventQueue`/`ALooper`) directly from C++ (Task DEVICES-0073). There is no
Java/Kotlin code and no JNI call in `Detail::AndroidSensorBridge`/`AndroidCompassBackend`/
`AndroidMotionBackend`. See `docs/devices-native-backend-design.md` for the per-sensor-type mapping
and rationale.

**API level note:** `ASensorManager_getInstance()` (deprecated since API 26, replaced by
`ASensorManager_getInstanceForPackage()`) is used deliberately — the replacement requires
API 26+, above this project's minimum (API 24). Using the deprecated, package-agnostic form is the
correct choice for that minimum; the deprecation warning is locally suppressed in
`AndroidSensorBridge.cpp`.

## Permissions and manifest features

- **Minimum API level: 24** (the package-agnostic `ASensorManager_getInstance()` above is the
  binding constraint).
- `android.permission.VIBRATE` — already present, uncommented, in SDL's own vendored
  Android manifest template. No CNA-side action needed.
- `android.hardware.sensor.{accelerometer,gyroscope,compass}` — declared
  `android:required="false"` `uses-feature` entries in `cna_demo_devices`'s manifest (Task
  DEVICES-0123), so the demo still installs on devices missing any one sensor. No runtime
  permission prompt is needed for `SensorManager`/NDK sensor registration on current Android
  (re-verify at the exact target API level before relying on this).
- `android.permission.HIGH_SAMPLING_RATE_SENSORS` — declared in `cna_demo_devices`'s manifest
  (Task ANDROID-BRIDGE-004). Android 12+ (API 31+) caps sensor sampling at about 200 Hz for apps
  without it, while `SensorBase<T>`'s default `TimeBetweenUpdates` (2 ms, about 500 Hz) exceeds that
  cap; without the permission a device would silently deliver samples slower than requested. It is
  a normal-protection-level permission (manifest only, never prompted), so it is declared
  unconditionally. `AndroidSensorBridge.hpp`'s `Start()` comment documents the same cap; the bridge
  does not detect or compensate for the OS slowing samples down if the permission is removed.
- **Devices actually tested:** only the `Medium_Phone` emulator image. **No physical Android
  device has been used** to verify any sensor, vibration or permission behavior in this namespace;
  every API-level/permission claim above comes from NDK headers and Android platform
  documentation.

## Build integration and emulator

The APK project is `modules/devices/examples/demo_devices/android/`, generated from SDL's
`create-android-project.py --variant copy` template and rewired to reuse CNA's own root CMake
project and its cross-compiled SDL install. Its commands and rules (the `PUBLIC android` link, the
`SDL_main.h` include, the copied-sources caveat) are in `docs/devices-build.md` §4.1; emulator
limits for this namespace are in `docs/devices-hardware-checklist.md` §9.

## What is still not implemented on Android

- Real declination-based `Compass.TrueHeading` (needs `System.Device.Location`, a
  separate, unstarted plan — see `docs/location-future-plan.md`).
- Physical qualification of the optional landscape conversion: Accelerometer, Gyroscope and
  Motion's vector fields share the proper rotation fixed by CNA-FIX-D22, with the existing raw
  opt-out. Host math regressions establish the basis and handedness; they do not qualify a phone's
  orientation query, sensor calibration or event path. See
  [the hardware checklist](devices-hardware-checklist.md#1-accelerometer-axis-signorientation).
  Motion.Attitude intentionally remains device/world-relative and is independent of the vector
  switch; no new display-relative quaternion API is introduced.
- Android CI: `.github/workflows/devices-tests.yml` runs the Devices tests and the strict XNA API
  surface check on a desktop Linux runner (`docs/devices-build.md` §7); nothing builds or runs the
  Android APK in CI.
