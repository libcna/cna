# `Microsoft::Devices` on Android — Consolidated Reference

Every Android-specific decision made across `plans/plan_devices.md`'s Phases 2, 3, 6, 7, 8, 9,
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
anyway was explicitly decided against (`plans/plan_devices.md` Task DEVICES-0031) — see
`docs/devices-build.md` Section 7 for the full evidence trail before reconsidering this.

**Re-verified 2026-07-06 (`plans/plan_devices.md` Task VIB-003)**, re-reading the same source
files with fresh eyes rather than trusting the prior pass's conclusion unchecked: the
conclusion above still holds — `Android_JNI_HapticRun`/`Android_JNI_HapticStop`
(`SDL_syshaptic.c`'s `SDL_SYS_HapticRunEffect`/`SDL_SYS_HapticStopEffect`) reach
`Context.VIBRATOR_SERVICE` via `SDLHapticHandler`/`SDLHapticHandler_API26`/
`SDLHapticHandler_API31`'s `run()`/`stop()` methods, exactly as before. **One new,
previously-undocumented finding surfaced by this re-read, relevant to `StartLeftRight()`
specifically (`plans/plan_devices.md` Task VIB-008):** on Android, `StartLeftRight(largeMotor,
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

- **Minimum API level: 24** (`ASensorManager_getInstance()`'s deprecated-but-required
  package-agnostic form, see the API level note above, is the binding constraint).
- `android.permission.VIBRATE` — already present, uncommented, in SDL's own vendored
  Android manifest template. No CNA-side action needed.
- `android.hardware.sensor.{accelerometer,gyroscope,compass}` — added as
  `android:required="false"` `uses-feature` declarations to `cna_demo_devices`'s manifest
  (Task DEVICES-0123), so the demo still installs on devices missing any one sensor. No
  runtime permission prompt is needed for `SensorManager`/NDK sensor registration on
  current Android (re-verify at the exact target API level before relying on this).
- `android.permission.HIGH_SAMPLING_RATE_SENSORS` — added to `cna_demo_devices`'s
  manifest (`plans/plan_devices.md` Task ANDROID-BRIDGE-004, 2026-07-06). Android 12+ (API 31+)
  caps sensor sampling at ~200Hz for apps that don't declare this permission; this
  project's own default `TimeBetweenUpdates` (2ms, ~500Hz — see `SensorBase<T>`'s default)
  exceeds that cap, so without the permission a device on API 31+ would silently deliver
  samples slower than requested, with no error or signal that the cap was hit. It is a
  normal-protection-level permission (declared in the manifest only, never prompted to the
  user at runtime), so it is safe to declare unconditionally rather than gate behind a
  build flavor. `include/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.hpp`'s
  `Start()` Doxygen comment documents the same cap from the code side; this bridge does
  not detect or compensate for the OS silently slowing samples down if the permission is
  ever removed.
- **Devices actually tested:** the `Medium_Phone` Android emulator image only (see
  "Build integration" below) — **no physical Android device has ever been used** to
  verify any sensor, vibration, or permission behavior in this namespace. Every
  API-level/permission claim above is sourced from NDK headers and Android platform
  documentation, not from on-device confirmation.

## Build integration (`modules/devices/examples/demo_devices/android/`)

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
  `modules/devices/examples/demo_devices/src/*` do not auto-propagate.

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
  separate, unstarted plan — see `docs/location-future-plans/plan.md`).
- **Corrected 2026-07-18 (independent re-verification of `audit_devices.md` finding
  `DEV-AUD-003`) — this line previously claimed no remap existed at all for any of
  `Motion`'s vector fields, which was stale/false:** `Task MOTION-012` (2026-07-16)
  applies the same landscape remap `Accelerometer`/`Gyroscope` already use to
  `Gravity`/`DeviceAcceleration`/`DeviceRotationRate` (`AndroidMotionBackend.cpp`'s
  `ApplyLandscapeRemapIfEnabled()`, respecting the shared
  `Detail::IsAndroidLandscapeRemapEnabled()` opt-out) — that part **is** implemented,
  only unverified on real hardware (see `docs/devices-hardware-checklist.md` Section 8,
  step 7). What remains genuinely unimplemented is `Motion.Attitude` (the quaternion)
  specifically — a quaternion isn't a plain vector, so the same remap logic doesn't
  apply, and any correction there needs its own change-of-basis derivation (`MOTION-002`
  — the stale `DEVICES-0111` task ID this line previously cited no longer exists in
  `plans/plan_devices.md`; `MOTION-002` is the current, correct reference).
- CI for any of this — **corrected 2026-07-18 (same re-verification, same finding):**
  a GitHub Actions workflow (`.github/workflows/devices-tests.yml`, `Task DEVPERF-001`,
  2026-07-17) now exists and runs the Devices/Sensors filtered suite plus the strict-XNA
  API surface check — this line previously claimed "no CI infrastructure exists
  anywhere in this repo" at all, which is now false. What remains genuinely unconfirmed
  is whether that workflow has actually run **green on a real GitHub-hosted runner** —
  no push to a remote branch has triggered it yet (see `docs/devices-build.md`'s own
  "This CI job has not yet actually executed on GitHub Actions" note) — `Task
  DEVICES-0127`'s original "no CI at all" framing is stale; the current gap is narrower
  ("exists but unconfirmed"), not "does not exist".
