# NEXT.md — CNA Project Handoff

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model, built on
SDL3 and a pluggable graphics backend (`EASYGL` / `VULKAN` / `BGFX`). It
preserves XNA-style public APIs (`Microsoft::Xna::Framework`,
`Microsoft::Devices`) while using modern C++ internally. It targets desktop
Linux/Windows/macOS, Android, and iOS.

**Main goal (current phase):** implement the full `Microsoft::Devices`
namespace — `Microsoft::Devices::Sensors` (Accelerometer, Compass, Gyroscope,
Motion, and their reading/event-args/exception types) plus
`Microsoft::Devices::VibrateController` — matching the Windows Phone 7 XNA
API spec. Branch: `feature/devices`.

**Important architectural decisions:**
- Public API names/signatures must match XNA 4.0 (or, for `Microsoft::Devices`,
  the documented WP7 SDK) exactly; C# properties become `getXProperty()` /
  `setXProperty()`.
- Non-XNA extensions are tagged `NOXNA` on the public declaration.
- `Microsoft::Devices::Sensors::SensorBase<T>` (header-only template) is the
  shared base for all sensor classes (`CurrentValue`, `IsDataValid`,
  `TimeBetweenUpdates`, `CurrentValueChanged`, `Dispose()`).
- `VibrateController` is a static-only utility class (no instances, no
  `SensorBase<T>`/`IDisposable`) — it does not follow the sensor pattern.
- FNA (the usual local reference tree for XNA behavior) implements **no**
  equivalent of `Microsoft::Devices` at all (it's WP7-only) — this namespace
  has no local reference tree to diff against; API completeness is judged
  from documented WP7 SDK knowledge instead.
- Tests live under `tests/` mirroring the `include`/`src` namespace path
  1:1, using Google Test, one file per class.

---

## 2. Current status

**Build:** `CNA` and `CnaTests` build cleanly with the `EASYGL` backend
(`cmake-build-debug`) as of the last verified build (during Task P2-8 work).

**Tests:** last full `ctest` run: **1935 tests total, 97% passing.** The only
failures are a fixed set of **64 pre-existing `EasyGL_*` graphics tests**
that cannot run headless (no display/GPU in this dev environment) — present
before this phase began and unrelated to `Microsoft::Devices` work. No
regressions have been introduced across the whole phase.

**Working:**
- Full `Microsoft::Devices::Sensors` namespace: `Accelerometer` (real,
  SDL3-backed — `SDL_SENSOR_ACCEL`, Android landscape axis remap),
  `AccelerometerReading`, `AccelerometerReadingEventArgs` (WP7 7.0 legacy),
  `AccelerometerFailedException`, `SensorFailedException`, `SensorBase<T>`,
  `SensorReadingEventArgs<T>`, `ISensorReading`, `SensorState`,
  `CalibrationEventArgs`, `CompassReading`/`Compass` (stub, see below),
  `GyroscopeReading`/`Gyroscope` (real, SDL3-backed — `SDL_SENSOR_GYRO`),
  `AttitudeReading`, `MotionReading`, `Motion` (stub, see below). All have
  passing test suites.
- `Microsoft::Devices::VibrateController` — static-only, SDL3 haptic-backed
  (`SDL_GetHaptics`/`SDL_OpenHaptic`/`SDL_InitHapticRumble`/
  `SDL_PlayHapticRumble`/`SDL_StopHapticEffects`). Now filters out haptic
  devices that are also connected gamepads, so it can't compete with
  `GamePad::SetVibration` (different SDL3 API path). Full tests.

**Does not work / not done yet:**
- `Accelerometer` has **no test file** (`AccelerometerTests.cpp` doesn't
  exist) — the only sensor class in this phase without one.
- `Accelerometer.hpp` has an unfixed `Dispose()` C++ name-hiding bug (see
  Section 4).
- `Compass` and `Motion` are permanent stubs — SDL3 exposes no magnetometer
  API on any platform, so both are always `SensorState::NotSupported` and
  `Start()` always throws. This is by design, not a gap, until SDL3 gains
  magnetometer support.
- Cross-platform builds (Vulkan/BGFX desktop backends, Android, iOS) have
  **not** been verified this phase — only the Linux desktop `EASYGL` build
  has been built and tested. No `cmake-build-vulkan`/`cmake-build-bgfx`
  directory currently exists in this checkout, and no Android NDK / iOS
  toolchain is available in this dev container.

---

## 3. Recent changes

- `plan_devices.md` (31 tasks: full `Microsoft::Devices::Sensors` +
  `VibrateController`) — **all 31 tasks complete**; a Status column was
  added to its Task Summary table.
- `AUDIT.md` — added a `Microsoft::Devices::Sensors` / `Microsoft::Devices`
  section (previously entirely missing, since FNA has no equivalent to diff
  against for this namespace).
- `plan_devices_phase2.md` (new) — follow-up plan: API-completeness audit,
  known-bug fixes, `CHECKLIST.md` compliance spot-check, cross-platform
  build verification, and a `VibrateController` review + proposed `NOXNA`
  vibration-API extensions.
- **Task P2-8 (done):** fixed a confirmed real bug —
  `VibrateController::Start()` could open and buzz a connected haptic-capable
  gamepad on desktop instead of safely no-opping, because `SDL_GetHaptics()`
  enumerates such controllers independently of the
  `GamePad::SetVibration`/`SDL_RumbleGamepad` path (confirmed by reading the
  vendored SDL3 Linux haptic backend,
  `third_party/SDL/src/haptic/linux/SDL_syshaptic.c`). Fix: added
  `IsConnectedGamepadHapticDevice()` in
  `src/Microsoft/Devices/VibrateController.cpp`, which skips haptic devices
  whose name matches a connected joystick before opening one. Files changed:
  `include/Microsoft/Devices/VibrateController.hpp` (doc comment),
  `src/Microsoft/Devices/VibrateController.cpp`,
  `tests/Microsoft/Devices/VibrateControllerTests.cpp` (explanatory note —
  no new test possible without real gamepad hardware).
- Bug found (not yet fixed): `Compass`, `Gyroscope`, and `Motion` all needed
  a `using SensorBase<T>::Dispose;` declaration added, because declaring
  `Dispose(bool) override` hides the inherited public no-arg `Dispose()`
  (C++ name-hiding). The identical bug exists in `Accelerometer.hpp` and is
  still unfixed (see Section 4/5).

Full per-class implementation history (constructors, tests added, etc.) for
all 31 `plan_devices.md` tasks is in `git log` and the plan files themselves
— not repeated here to keep this document short.

---

## 4. Current blocker / main problem

No blocker prevents work from continuing. The most important known problem:

**Symptom:** `include/Microsoft/Devices/Sensors/Accelerometer.hpp` declares
`void Dispose(bool disposing) override;` without a `using
SensorBase<AccelerometerReading>::Dispose;` declaration. This C++ name-hiding
means the inherited public no-arg `Dispose()` (the actual
`System::IDisposable` contract method, defined in `SensorBase<T>`) is hidden
for any `Accelerometer` instance — calling `accel.Dispose()` fails to
compile for any external caller.

**Failing scenario (not yet reduced to a committed failing test):** any code
that does `Accelerometer a; a.Dispose();` fails to compile with an
"ambiguous"/"no matching function" style error, because only the
`Dispose(bool)` overload is visible through the derived class.

**Affected files:** `include/Microsoft/Devices/Sensors/Accelerometer.hpp`.

**Suspected cause:** this bug predates the current phase and was simply
never triggered, because `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp`
does not exist — no code anywhere calls `Accelerometer::Dispose()` the
no-arg way. The same bug was independently found (and fixed) in `Compass`,
`Gyroscope`, and `Motion` while writing their test files, which is what
surfaced it for `Accelerometer` too.

**What has been tried:** nothing yet for `Accelerometer` specifically — the
fix is well-understood and already applied three times elsewhere in this
codebase (see `include/Microsoft/Devices/Sensors/Compass.hpp` for the exact
one-line pattern to copy). This is Task P2-3 in `plan_devices_phase2.md`.

---

## 5. Known bugs and limitations

- **Confirmed bug, not yet fixed:** `Accelerometer.hpp` `Dispose()`
  name-hiding (see Section 4). Fix: `plan_devices_phase2.md` Task P2-3.
- **Incomplete:** `AccelerometerTests.cpp` does not exist — `Accelerometer`
  is the only sensor class with zero test coverage. Same task (P2-3).
- **Confirmed inconsistency, not fixed:** `GetTypeNameCPP(...)` NAME-string
  convention is inconsistent across the codebase — some files use
  dot-separated .NET-style names (the documented invariant, see Section 6),
  others use `::` (`Accelerometer.cpp`, `Cue.cpp`, `AudioEngine.cpp`,
  `SoundBank.cpp`, `WaveBank.cpp`, `DateTime.cpp`, `DateTimeOffset.cpp` —
  grep `GetTypeNameCPP` to find all). All classes added during this phase
  use the dot convention correctly. Fixing `Accelerometer.cpp` specifically
  is Task P2-4; the rest is a separate, larger, cross-cutting cleanup
  outside this phase's scope.
- **By design, not a bug:** `Compass` and `Motion` are permanent
  `SensorState::NotSupported` stubs — SDL3 has no magnetometer API on any
  platform.
- **By design, not a bug:** `VibrateController::Start()` always rumbles at
  full strength (`1.0f`) — matches the real WP7 API, which has no intensity
  concept (WP7-era vibration motors were single-intensity on/off). A `NOXNA`
  variable-intensity overload is proposed but not implemented
  (`plan_devices_phase2.md` Task P2-10).
- **Accepted limitation:** `VibrateController`'s gamepad-exclusion filter
  (Task P2-8, Section 3) matches by device name; two physically distinct
  controllers reporting an identical product name would both be
  excluded/included together. Judged too rare to justify a more invasive
  fix (would require opening every connected joystick just to probe it).
- **Needs verification:** cross-platform builds — Vulkan/BGFX desktop
  backends, Android, iOS — have not been exercised this phase (only Linux
  `EASYGL` desktop). Android/iOS specifically are blocked in this dev
  container (no NDK/toolchain present). See `plan_devices_phase2.md` Tasks
  P2-6/P2-7.
- **Needs verification:** `cmake-build-vulkan`/`cmake-build-bgfx` build
  directories may have the same stale-`CMakeCache.txt` issue that
  `cmake-build-debug` once had (fixed by deleting the cache and
  reconfiguring) — not yet re-checked since neither directory currently
  exists in this checkout.
- **Unknown:** whether `Microsoft::Devices::Sensors`'s public API surface,
  as implemented, is 100% complete against the real WP7 Mango SDK — it was
  filled in from general reference knowledge, not diffed against a local
  source tree (none exists). Independent verification is proposed as
  `plan_devices_phase2.md` Task P2-2.

---

## 6. Architecture notes

```
include/Microsoft/Devices/Sensors/   ← XNA WP7 sensor API headers
src/Microsoft/Devices/Sensors/       ← sensor implementations (SDL3-backed)
tests/Microsoft/Devices/Sensors/     ← Google Test suites per class
include/Microsoft/Devices/           ← VibrateController.hpp
src/Microsoft/Devices/               ← VibrateController.cpp
tests/Microsoft/Devices/             ← VibrateControllerTests.cpp
```

**`SensorBase<T>`** (header-only template) owns `CurrentValue`,
`IsDataValid`, `TimeBetweenUpdates`, `CurrentValueChanged`, and `Dispose()`.
Concrete sensors override `Start()`, `Stop()`, and `Dispose(bool)`.

**Invariant — must not be forgotten again:** any class overriding
`Dispose(bool)` **must** add `using SensorBase<T>::Dispose;`, or C++
name-hiding silently breaks the inherited public no-arg `Dispose()`. This
exact bug has been found (and fixed) three times already (`Compass`,
`Gyroscope`, `Motion`) and is still present, unfixed, in `Accelerometer.hpp`.

**Sensor pattern (real, SDL3-backed — `Accelerometer`/`Gyroscope`):** static
`g_sensor_`/`g_sensorId_` hold the single open SDL sensor handle; static
`instanceCount_` enforces a ≤10 simultaneous-instance limit; static
`eventWatchRegistered_` guards the SDL event filter lifecycle. `Start()`
opens the sensor and registers the SDL event watch; `Stop()` unregisters;
`Dispose(bool)` stops, decrements the counter, and closes the sensor handle
when the last instance is disposed. `ProcessSensorUpdateEvent()` runs from
the SDL event filter on every `SDL_EVENT_SENSOR_UPDATE`, with an
Android-specific landscape axis remap (duplicated per-class, not shared —
see each `.cpp`'s `ConvertAndroid*ToXnaLandscape()`).

**Stub pattern (`Compass`/`Motion`):** always `SensorState::NotSupported`;
`Start()` always throws `SensorFailedException`; still expose the
`Calibrate` event for API completeness even though it's never raised.

**`VibrateController`:** static-only (`= delete`d default constructor, no
`SensorBase<T>`, no `IDisposable`), lives directly in `Microsoft::Devices`
(not `::Sensors`). Drives SDL3's haptic API directly rather than the sensor
pattern. As of Task P2-8, deliberately excludes haptic devices that are also
connected joysticks/gamepads from device selection, to avoid competing with
`GamePad::SetVibration` (a separate SDL3 subsystem — `SDL_RumbleGamepad` on
an `SDL_Gamepad*`, unrelated to the generic `SDL_Haptic*` API).

**`GetTypeName()` invariant:** must return `.`-separated fully-qualified
.NET names (e.g. `"Microsoft.Devices.Sensors.Compass"`), tagged `NOXNA`.
Classes deriving `System::Object` (via `SensorBase<T>`) use the
`GetTypeNameHPP()`/`GetTypeNameCPP(Class, "Name")` macro pair; classes that
don't (e.g. `AccelerometerReading`-style value types) declare a plain
`NOXNA std::string GetTypeName() const;` method instead. `GetHashCode()`
returns `std::size_t` for these value types (not the `int` used by
`System::Object::GetHashCode()`).

**Boundaries — do not cross:**
- `third_party/SDL` is vendored and has its **own `CLAUDE.md` forbidding
  AI-authored code contributions** to that project. It is safe (and useful)
  to *read* for research (this is how the P2-8 fix was verified), but never
  edit.
- Do not restructure `SensorBase<T>` or `ISensorReading` — stable, used by
  production code.
- Do not expand `Microsoft::Devices` scope to camera, radio, or
  phone-call/photo-picker APIs — explicitly out of scope (not sensor or
  vibration functionality).
- Do not implement sensor fusion in `Motion` — it stays a `NotSupported`
  stub until SDL3 itself gains magnetometer access.

---

## 7. Useful commands

```bash
# Configure (only needed once, or if CMakeCache.txt is stale/points elsewhere):
cmake -S /rv/data/development/github.com/openeggbert/cna_devices \
      -B /rv/data/development/github.com/openeggbert/cna_devices/cmake-build-debug \
      -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug

# Build library:
cmake --build cmake-build-debug --target CNA -j$(nproc)

# Build tests:
cmake --build cmake-build-debug --target CnaTests -j$(nproc)

# Run all tests:
cd cmake-build-debug && ctest --output-on-failure

# Run only Devices/Sensors + VibrateController tests:
cd cmake-build-debug && ctest --output-on-failure -R "Accelerometer|SensorFailed|Compass|Gyroscope|Attitude|Motion|VibrateController"

# Run one suite directly:
./cmake-build-debug/CnaTests --gtest_filter="GyroscopeTests*"

# Reproduce the Section 4 blocker (expected to fail to compile):
# Add `Accelerometer a; a.Dispose();` to any .cpp under tests/ and build —
# it will not compile until the `using SensorBase<AccelerometerReading>::Dispose;`
# fix (Task P2-3) is applied.
```

---

## 8. Next smallest tasks

Numbered per `plan_devices_phase2.md`.

1. **Task P2-3 — Fix `Accelerometer.hpp` Dispose() name-hiding + write `AccelerometerTests.cpp`**
   - Goal: add `using SensorBase<AccelerometerReading>::Dispose;` (copy the
     exact pattern from `include/Microsoft/Devices/Sensors/Compass.hpp`);
     write a new test file modeled on
     `tests/Microsoft/Devices/Sensors/GyroscopeTests.cpp` (branches on the
     live `getIsSupportedProperty()` result so it passes both headless and
     on real hardware).
   - Files: `include/Microsoft/Devices/Sensors/Accelerometer.hpp` (edit),
     `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp` (new).
   - Verify: `./cmake-build-debug/CnaTests --gtest_filter="AccelerometerTests*"`,
     then full `ctest --output-on-failure`.

2. **Task P2-4 — Fix `Accelerometer.cpp`'s `GetTypeNameCPP` to the
   dot-separated convention**
   - Goal: change `GetTypeNameCPP(Accelerometer, "Microsoft::Devices::Sensors::Accelerometer")`
     to use `.` separators, matching every other class from this phase.
     Small; do right after P2-3 since both touch the same file.
   - Files: `src/Microsoft/Devices/Sensors/Accelerometer.cpp`.
   - Verify: build `CNA` + `CnaTests`, run `AccelerometerTests*` (add a
     `GetTypeName()` assertion if the test doesn't already have one).

3. **Task P2-10 — NOXNA: `VibrateController::Start(duration, intensity)` overload**
   - Goal: expose SDL3's already-available rumble-strength parameter (see
     Section 5's "by design" note) as a `NOXNA` overload, clamped to
     `[0.0f, 1.0f]`; the existing XNA-compliant `Start(TimeSpan)` should
     delegate to it with `intensity = 1.0f`.
   - Files: `include/Microsoft/Devices/VibrateController.hpp`,
     `src/Microsoft/Devices/VibrateController.cpp`.
   - Verify: `./cmake-build-debug/CnaTests --gtest_filter="VibrateControllerTests*"`.

4. **Task P2-11 — NOXNA: `VibrateController::getIsSupportedProperty()`**
   - Goal: let calling code check haptic availability ahead of time (every
     `Sensors` class already has this; `VibrateController` doesn't).
   - Files: same as above.
   - Verify: same as above.

Full remaining list (P2-2, P2-5 through P2-9 [P2-8 done], P2-12/13,
Phase 5 cross-platform verification) is in `plan_devices_phase2.md`.

---

## 9. Do not do yet

- Do not refactor or restructure `SensorBase<T>` or `ISensorReading` —
  stable, used by production code.
- Do not perform the cross-cutting `GetTypeNameCPP` dot/colon cleanup beyond
  `Accelerometer.cpp` (Task P2-4) — the rest touches unrelated files
  (`Cue.cpp`, `AudioEngine.cpp`, etc.) and needs its own scoped plan.
- Do not expand `Microsoft::Devices` to camera, radio, or phone-hardware
  APIs (`PhotoCamera`, `CameraButtons`, `PhotoChooserTask`, etc.) — not
  sensor/vibration functionality, explicitly out of scope.
- Do not implement real sensor fusion in `Motion` — keep it a
  `NotSupported` stub until SDL3 gains magnetometer access.
- Do not edit anything under `third_party/SDL` — vendored, has its own
  `CLAUDE.md` forbidding AI-authored contributions; read-only for research.
- Do not attempt Android/iOS cross-compilation in this environment — no
  NDK/toolchain is available; that verification needs a different
  environment or CI.
- Do not run `cmake --build` without first checking `CMakeCache.txt` points
  at the correct source directory (this repo has hit stale-cache issues
  before).

---

## 10. Resume prompt

```
Read NEXT.md first.
Then inspect only the files listed for the first task in section 8.
Do not refactor unrelated code.
Make one small, verified improvement (implement or test one class at a time).
Run the relevant build/test command from section 7 after each change.
Update NEXT.md after finishing.
```
