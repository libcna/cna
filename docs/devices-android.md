# `Microsoft::Devices` on Android — Consolidated Reference

Every Android-specific decision made across `plan_devices.md`'s Phases 2, 3, 6, 7, 8, 9,
in one place. This doc links to, rather than duplicates, the deeper detail already in
`docs/devices-native-backend-design.md` (architecture/field mappings),
`docs/devices-build.md` (build commands actually run), and
`docs/devices-hardware-checklist.md` (manual verification steps).

## Vibration: no native bridge exists, and none is needed

`VibrateController` uses **only** SDL3's own haptic backend on Android — no JNI, no
custom bridge. Confirmed by reading SDL3's actual Android haptic source
(`third_party/SDL/src/haptic/android/SDL_syshaptic.c` +
`.../android-project/.../SDLControllerManager.java`): it already queries
`Context.VIBRATOR_SERVICE` directly (separate from any connected-gamepad vibrator) and
implements amplitude control end to end (`VibrationEffect.createOneShot()` on API 26+,
`VibratorManager` on API 31+), including the exact `intensity==0→stop()`/`intensity*255`
clamped-`[1,255]` mapping a custom bridge would have had to reinvent. Building one
anyway was explicitly decided against (`plan_devices.md` Task DEVICES-0031) — see
`docs/devices-build.md` Section 7 for the full evidence trail before reconsidering this.

**Re-verified 2026-07-06 (`plan_devices.md` Task VIB-003)**, re-reading the same source
files with fresh eyes rather than trusting the prior pass's conclusion unchecked: the
conclusion above still holds — `Android_JNI_HapticRun`/`Android_JNI_HapticStop`
(`SDL_syshaptic.c`'s `SDL_SYS_HapticRunEffect`/`SDL_SYS_HapticStopEffect`) reach
`Context.VIBRATOR_SERVICE` via `SDLHapticHandler`/`SDLHapticHandler_API26`/
`SDLHapticHandler_API31`'s `run()`/`stop()` methods, exactly as before. **One new,
previously-undocumented finding surfaced by this re-read, relevant to `StartLeftRight()`
specifically (`plan_devices.md` Task VIB-008):** on Android, `StartLeftRight(largeMotor,
smallMotor, duration)` does **not** produce genuine independent dual-motor vibration on
the phone's own vibrator. The call path is `SDL_HAPTIC_LEFTRIGHT` effect →
`SDL_SYS_HapticRunEffect()` (`SDL_syshaptic.c`), which **blends both magnitudes into a
single intensity** —
`total = (large_magnitude/32767 * 0.6f) + (small_magnitude/32767 * 0.4f)` — before
calling `Android_JNI_HapticRun()` (the single-intensity path). The *true* independent
dual-motor path in SDL3's Android backend, `Android_JNI_HapticRumble()` →
`SDLHapticHandler_API31.rumble()` (which calls `InputDevice.getVibratorManager()` and
drives up to two vibrators independently), is wired up **only** from
`SDL_sysjoystick.c`'s controller-rumble path (`SDL_RumbleJoystick`, i.e.
`Microsoft::Xna::Framework::Input::GamePad::SetVibration()`) — never from the generic
`SDL_Haptic` effect path `VibrateController` uses. This is not a CNA bug: CNA calls the
standard, documented SDL3 `SDL_Haptic` API correctly; the blending happens entirely
inside SDL3's own Android backend, matching the identical `0.6f`/`0.4f` weighting
`SDLHapticHandler_API31.rumble()`'s own single-vibrator fallback branch already uses for
non-phone haptic devices with only one vibrator — i.e. SDL3 treats "one vibrator, two
requested intensities" consistently the same way in both the Java and native layers.
Practically: a real phone's single built-in vibrator motor, via this codebase's
`StartLeftRight()`, always receives one blended intensity, never two independent
values — worth knowing before writing gameplay code that assumes true per-motor
independence will be felt on a phone. Desktop dual-actuator force-feedback hardware
(non-gamepad, so not excluded by `IsConnectedGamepadHapticDevice()`) is unaffected by
this Android-specific blending and may achieve genuine independent magnitudes if the
underlying platform driver supports `SDL_HAPTIC_LEFTRIGHT` natively — not verified
against real desktop dual-actuator hardware in this session (no such hardware
available), but architecturally unrelated to the Android limitation above.

## Sensors: pure NDK native, no JNI, no Java bridge

Compass/Motion's Android backends use `<android/sensor.h>`/`<android/looper.h>`
(`ASensorManager`/`ASensorEventQueue`/`ALooper`) directly from C++ — confirmed viable by
reading the NDK sysroot headers before writing any code (Task DEVICES-0073). No Java/Kotlin
code, no JNI calls, anywhere in `Detail::AndroidSensorBridge`/`AndroidCompassBackend`/
`AndroidMotionBackend`. See `docs/devices-native-backend-design.md` for the full
per-sensor-type mapping and rationale.

**API level note:** `ASensorManager_getInstance()` (deprecated since API 26, replaced by
`ASensorManager_getInstanceForPackage()`) is used deliberately — the replacement requires
API 26+, above this project's actual minimum (API 24). Using the deprecated,
package-agnostic form is the correct choice for that minimum, not an oversight; the
deprecation warning is locally suppressed in `AndroidSensorBridge.cpp`.

## Permissions and manifest features

- `android.permission.VIBRATE` — already present, uncommented, in SDL's own vendored
  Android manifest template. No CNA-side action needed.
- `android.hardware.sensor.{accelerometer,gyroscope,compass}` — added as
  `android:required="false"` `uses-feature` declarations to `cna_demo_devices`'s manifest
  (Task DEVICES-0123), so the demo still installs on devices missing any one sensor. No
  runtime permission prompt is needed for `SensorManager`/NDK sensor registration on
  current Android (re-verify at the exact target API level before relying on this).

## Build integration (`examples/demo_devices/android/`)

The first — and, as of this writing, only — Android app packaging in this project's
history. Generated from SDL's own `create-android-project.py --variant copy` template,
then rewired to reuse CNA's own root CMake project and its already-cross-compiled
`.sdl-prebuilt-Android-aarch64` SDL install, instead of building a second, separate SDL
from the template's own vendored copy (deleted, ~34MB). See `docs/devices-build.md`
Section 4.1 for the exact commands and every bug hit/fixed getting there:

- A stale `CNA_SDL_PREBUILT_ROOT` CMake cache entry (not an architectural
  executable-vs-shared-library problem, as originally suspected).
- `CMakeLists.txt`'s root `CNA` target needed a `PUBLIC android` link — `libandroid.so`
  provides `ASensorManager_*`/`ALooper_*`, needed by any *consumer* of `CNA`, not by the
  static library itself.
- Cross-subdirectory `SDL3::SDL3` CMake target visibility (a `find_package()` re-run,
  cache-hit, cheap).
- `Main.cpp` needed `#include <SDL3/SDL_main.h>` for the `#define main SDL_main`
  redirection `SDLActivity.java`'s `dlsym()` lookup requires — a no-op on desktop.
- `--variant copy` duplicates source files into `app/jni/src/`; edits to
  `examples/demo_devices/src/*` do not auto-propagate.

**Result:** `./gradlew -PBUILD_WITH_CMAKE assembleDebug` produces a real, installable
`app-debug.apk`. Installed and launched on the `Medium_Phone` emulator (`/dev/kvm` now
available in this container — absent every prior session); the demo's real UI rendered
correctly and responded to synthetic sensor values injected via the emulator console.
**No physical Android device has ever been used** — see
`docs/devices-hardware-checklist.md` for what remains genuinely unverified.

## Emulator limitations specific to this namespace

See `docs/devices-hardware-checklist.md` Section 9 for the full list: no real vibration
motor, injected sensor values prove pipeline delivery but not physical-tilt correctness,
no confirmed virtual rotation-vector sensor in this emulator's console command set (so
`Compass`/`Motion`'s Android path was never exercised via emulator-injected values, only
confirmed to launch/compile/link), and observed (unrelated) system-app ANRs under this
container's resource constraints.

## What is still not implemented on Android

- Real declination-based `Compass.TrueHeading` (needs `System.Device.Location`, a
  separate, unstarted plan — see `docs/location-future-plan.md`).
- Any coordinate-system remap for `Motion`'s `Gravity`/`DeviceAcceleration`/
  `DeviceRotationRate`/`Attitude` beyond a direct, unremapped passthrough — an explicit,
  documented open question (Task DEVICES-0111), not an oversight.
- CI for any of this — no CI infrastructure exists anywhere in this repo (Task
  DEVICES-0127); `docs/devices-build.md`'s reproducible commands are the current gate.
