# NEXT.md — CNA Project Handoff

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model, built on SDL3
with a pluggable graphics backend (`EASYGL` / `VULKAN` / `BGFX`). It preserves
XNA-style public APIs (`Microsoft::Xna::Framework`, `Microsoft::Devices`) while using
modern C++ internally. Targets desktop Linux/Windows/macOS and Android; iOS is planned
but has no toolchain in this environment. Branch: `feature/devices`.

Current development phase: `Microsoft::Devices`/`Microsoft::Devices::Sensors` has been
through ten hardening/implementation passes (`plan_devices.md` [original, 31 tasks] →
`plan_devices_phase2.md` through `plan_devices_phase9.md` → a second, larger
`plan_devices.md` [143 tasks, Phases 0-10], all closed). This second `plan_devices.md`
gave `Compass`/`Motion` real native Android backends (previously permanent stubs) and
produced this project's first working Android APK (installed, launched, and rendered on
an emulator). **There is no open plan file for this namespace right now.**

**Important architectural decisions:**
- Public API names/signatures match XNA 4.0 (or, for `Microsoft::Devices`, the
  archived WP7 SDK docs — FNA has no equivalent) exactly; C# properties become
  `getXProperty()`/`setXProperty()`.
- Non-XNA extensions are tagged `NOXNA` on the public declaration.
- `Microsoft::Devices::Sensors::SensorBase<T>` (header-only template) is the shared
  base for `Accelerometer`/`Compass`/`Gyroscope`/`Motion` — see Section 6.
- `VibrateController` is a singleton (`getDefaultProperty()`), lives directly in
  `Microsoft::Devices` (not `::Sensors`), does not derive `SensorBase<T>`/`IDisposable`.
- `Compass`/`Motion` each hold a `std::unique_ptr<Detail::ICompassBackend>`/
  `IMotionBackend`, constructed only inside `#if defined(__ANDROID__)`. Every other
  platform keeps the exact original stub behavior. See Section 6.
- Tests live under `tests/` mirroring the `include`/`src` namespace path 1:1, Google Test.

**Plan history (all closed, no open plan file):** the original `plan_devices.md`
(31 tasks) → `plan_devices_phase2.md` (17) → `plan_devices_phase3.md` (12) →
`plan_devices_phase4.md` (14) → `plan_devices_phase5.md` (14) →
`plan_devices_phase6.md` (10) → `plan_devices_phase7.md` (7) →
`plan_devices_phase8.md` (8) → `plan_devices_phase9.md` (9) → a second, larger
`plan_devices.md` (143 tasks, Phases 0-10, 2026-07-05). Each phase's premise was to
*not* trust the previous phase's "done" claims and re-audit from the actual code —
real concurrency/lifetime bugs were found and fixed this way across Phases 5-8; this
final plan found and fixed a genuine gravity/acceleration unit-conversion bug in the
new `Motion` Android backend the same way (see `AUDIT.md` and each plan file's own
findings for full detail; not repeated here to keep this file concise).

---

## 2. Current status

**Build:** `CNA` and `CnaTests` build cleanly under `EASYGL` (`cmake-build-debug`),
`VULKAN`, and `BGFX`. Android cross-compiles cleanly (`arm64-v8a`, NDK r30, API 24) —
the `CNA` library target, re-verified as recently as the 2026-07-05 micro-cleanup pass
below. **`cna_demo_devices`'s Android cross-compile is currently blocked** in this
container: `examples/demo_devices/src/Main.cpp` fails with `'SDL3/SDL_main.h' file not
found` (an environment/vendoring gap in this container's Android sysroot, unrelated to
any `Microsoft::Devices` code — `Main.cpp` doesn't touch Sensors at all). It *did*
successfully build, install, and launch as a real APK once, in the original Phase 9
session (see Section 3) — that result is historical, not current-session-reproducible
here; re-verify before relying on it again. iOS: no toolchain in this Linux container,
confirmed blocked every phase to date, including this one.

**Tests:** Devices-only filter is 273 tests (via `ctest -R`, see Section 7's updated
filter) — 269 passing, 2 expected `GTEST_SKIP()`s on hardware-dependent paths (this
container has no accelerometer/gyroscope hardware). Full `ctest` suite has 36
pre-existing, unrelated `EasyGL`/`easy-gl` graphics-backend failures in this session's
headless-display container (not caused by, or fixed by, any Devices work — count varies
by environment's GPU/display availability, not a regression). See Section 7 for commands.

**Sanitizers:** `devices-asan`/`devices-ubsan` presets clean. `devices-tsan` reports 40
warnings, all individually confirmed (via each report's own `Location is global ...`
line) to be the same one pre-existing, out-of-scope race in `sharp-runtime`'s
`System::TimeSpan::TimeSpan(const TimeSpan&)` copy constructor — reached via more call
paths than before, still the same single unsynchronized debug counter, still not a
`Microsoft::Devices` bug.

**Working:** `Accelerometer`/`Gyroscope` — real, SDL3-backed, thread-safe, lifetime-safe
(unchanged this pass). `VibrateController` — real, SDL3 haptic-backed (a native Android
JNI bridge was considered and explicitly rejected — SDL3's own Android haptic backend
already reaches `Context.VIBRATOR_SERVICE` with full amplitude control, confirmed by
reading its source). **`Compass`/`Motion` — real on Android**:
`Detail::AndroidCompassBackend`/`AndroidMotionBackend`, pure NDK (`<android/sensor.h>`/
`<android/looper.h>`), no JNI — the `CNA` library itself (including this code) still
cross-compiles cleanly for Android as of the 2026-07-05 micro-cleanup pass. In the
original Phase 9 session, `examples/demo_devices` (`cna_demo_devices`) was successfully
packaged into a real APK, installed, and launched on the `Medium_Phone` emulator — with
the demo's real UI rendering confirmed via screenshot and live sensor-event delivery
confirmed via emulator-injected synthetic values. **That emulator run verified the
software pipeline works end-to-end; it did not verify physical sensor correctness, and
no physical Android device has ever been used in any session.** As of the two
subsequent stabilization/cleanup passes, `cna_demo_devices` itself no longer
cross-compiles for Android in this container (see "Build" above) — re-verify before
attempting another APK build or emulator run.

**Not working / not implemented:** `Compass.TrueHeading` permanently equals
`MagneticHeading` — real declination needs `System.Device.Location`, still not
implemented, still explicitly out of scope for `Microsoft::Devices::Sensors` (see
`docs/location-future-plan.md`). `Motion`'s coordinate-system remap (whether
`Gravity`/`DeviceAcceleration`/`DeviceRotationRate`/`Attitude` need the same kind of
landscape remap `Accelerometer`/`Gyroscope` use) is an open, unresolved question, not
assumed either way. **No physical hardware verification has ever been done, in any
session** — the emulator run closes the "does the software pipeline work end-to-end"
question, explicitly not the "is it physically correct" one (see
`docs/devices-hardware-checklist.md` §9 for exactly what an emulator can't substitute
for).

---

## 3. Recent changes

**Second `plan_devices.md` (2026-07-05) — 143 tasks, Phases 0-10, all closed:**
- **Phase 0-5:** fresh, from-scratch re-audit found no regressions in Phases 2-9's
  work. Found SDL3's Android haptic backend already fully implements phone-vibrator
  amplitude control (Task DEVICES-0014/0031) — decided against building a redundant
  native Android vibration bridge. Confirmed `/dev/kvm` now exists in this container
  (Task DEVICES-0012) — a major, exploited-later finding. Confirmed both
  Accelerometer/Gyroscope unit conversions were already correct (Tasks DEVICES-0063/64).
- **Phase 6:** built `Detail::AndroidSensorBridge` — a shared, pure-NDK (no JNI)
  wrapper around `ASensorManager`/`ASensorEventQueue`/`ALooper`, verified via a real
  Android cross-compile and `llvm-nm` symbol check.
- **Phase 7:** `Compass` gained `Detail::AndroidCompassBackend` — real on Android
  (`TYPE_ROTATION_VECTOR` for heading, `TYPE_MAGNETIC_FIELD` for the raw reading/
  accuracy/`Calibrate`), honest stub everywhere else. 17 new tests.
- **Phase 8:** `Motion` gained `Detail::AndroidMotionBackend` — real on Android
  (rotation-vector/game-rotation-vector fallback for `Attitude`, gravity/linear-accel/
  gyroscope sensors for the rest), honest stub everywhere else. Found and fixed a real
  gravity/acceleration unit-conversion bug (m/s² reported, "in g" documented) while
  implementing. Corrected a misleading doc comment claiming `Motion` requires live
  `Accelerometer`/`Compass`/`Gyroscope` instances (it doesn't — Android's OS fuses
  internally). 14 new tests.
- **Phase 9:** built and ran this project's **first-ever Android APK**
  (`examples/demo_devices/android/`) — found and fixed 5 real bugs along the way (a
  stale SDL prebuilt-cache path, a missing `libandroid.so` link, a cross-directory
  CMake target-visibility gap, a missing `SDL_main` export, an invalid XML comment).
  Installed and launched on the `Medium_Phone` emulator (now bootable — `/dev/kvm`
  exists), confirmed real UI rendering and live sensor-event delivery via screenshots.
- **Phase 10:** rewrote `docs/devices-native-backend-design.md` from sketch to as-built;
  added `docs/devices-api-coverage.md`/`docs/devices-android.md`; fixed a real staleness
  in `DevicesDemo.cpp`'s window title (hardcoded "Compass/Motion not supported," no
  longer true on Android) and a real documentation gap in `docs/devices-build.md` (the
  documented test filter silently didn't match 3 new test suites); re-ran the full
  regression suite (looped 40/40, all 3 sanitizers, full `ctest`) fresh.

Full task-by-task detail (all 143 tasks, exact commands, every finding) is in the
second `plan_devices.md`; Phases through P9's detail is in the original
`plan_devices.md`/`plan_devices_phase2-9.md` files. This section intentionally does not
restate that history.

**Stabilization pass (2026-07-05, no plan file — a targeted correctness review, not a
new phase):** a focused review flagged 7 concrete concerns in the just-landed Android
`Compass`/`Motion` code; all 7 were confirmed real and fixed. Scope was deliberately
narrow — no new features, no new plan file.

Files changed (2 commits):
- `include/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.hpp`,
  `src/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.cpp`,
  `tests/Microsoft/Devices/Sensors/Detail/AndroidSensorBridgeTests.cpp` — thread safety,
  async-start reporting, use-after-free hardening, explicit callback exception policy.
- `include/Microsoft/Devices/Sensors/Compass.hpp`,
  `include/Microsoft/Devices/Sensors/Detail/ICompassBackend.hpp`,
  `src/Microsoft/Devices/Sensors/Compass.cpp`,
  `include/Microsoft/Devices/Sensors/Motion.hpp`,
  `include/Microsoft/Devices/Sensors/Detail/IMotionBackend.hpp`,
  `src/Microsoft/Devices/Sensors/Motion.cpp`,
  `tests/Microsoft/Devices/Sensors/CompassTests.cpp`,
  `tests/Microsoft/Devices/Sensors/MotionTests.cpp` — repeated-`Start()` guard,
  `SetBackendForTesting()` enforcement, doc-drift fixes.

What changed, by original review goal:
1. **Repeated Start/Stop safety** — `AndroidSensorBridge::Start()` previously reassigned
   an already-joinable `std::thread` on a second call (`std::terminate()`). Now returns
   `false` immediately (no-op) if already running. `Compass::Start()`/`Motion::Start()`
   now throw `SensorFailedException` immediately if already started, before touching
   `backend_` — matching `Accelerometer::Start()`'s existing convention.
2. **Async startup reporting** — `Start()` previously returned `true` right after
   spawning the worker thread, without confirming `ASensorEventQueue` creation or
   `ASensorEventQueue_enableSensor()` (return value previously discarded) actually
   succeeded. Now a condition-variable handshake (bounded to 5s) blocks `Start()` until
   the worker signals real `Success`/`Failure`; `Stop()` is called to clean up on
   failure/timeout.
3. **Reentrant Stop/dispose** — `impl_` changed `unique_ptr` → `shared_ptr`, with the
   worker thread capturing its own copy, so `Impl`'s lifetime now survives the owning
   `AndroidSensorBridge`'s destruction for as long as `Run()` is still executing; the
   inner event loop rechecks `stopRequested_` before every callback. **Explicitly still
   unsupported, by design:** destroying (not just `Stop()`-ing) the *outer owning*
   object (e.g. `Compass` itself) from inside its own callback — matches the existing,
   accepted `Accelerometer` boundary, not newly introduced here.
4. **Callback exception policy, made explicit** — `callback_()` is now wrapped in
   `try { } catch (...) { }`, matching `SdlSensorSubsystem::DispatchToInstances()`'s
   existing Task P8-5 policy, so a throwing `CurrentValueChanged`/`Calibrate` handler
   cannot `std::terminate()` the process from the worker thread.
5. **Doc drift fixed** — `Compass.hpp`/`Motion.hpp` no longer claim `Start()` "Always"
   throws or that no platform supports these sensors; both now state Android is real,
   every other platform still throws, matching Section 2 above.
6. **`SetBackendForTesting()` contract enforced**, not just documented — both now throw
   `SensorFailedException` if called while `started_`, instead of silently swapping a
   running backend's state out from under an active session.
7. **Tests re-run:** `CompassTests`/`MotionTests`/`AndroidSensorBridgeTests` plus
   `Accelerometer`/`Gyroscope`/`VibrateController` (73 tests) — all pass; the
   concurrency-relevant subset looped 40/40 clean; full Devices-only filter 280/280 (2
   expected hardware skips); full project `ctest` 3266 tests, 3264 passed, 2 expected
   skips, 0 regressions. `devices-asan`: 0 issues. `devices-tsan`: 25 warnings, every one
   individually confirmed (via its own `Location is global ...` line) to be the same
   pre-existing `sharp-runtime` `TimeSpan::copy_count` race, none new. `devices-ubsan`: 3
   pre-existing findings, all in `Vector3::GetHashCode()`/`Matrix::GetHashCode()` (signed
   integer overflow), unrelated to any file touched this pass — 0 findings in the
   reviewed files. Android cross-compile of the `CNA` library target re-verified clean;
   `cna_demo_devices` itself currently fails to cross-compile for Android in this
   container on an unrelated, pre-existing gap (`SDL3/SDL_main.h` not found for the
   Android sysroot — an environment/vendoring issue in `Main.cpp`, not `Microsoft::Devices`
   code, and not introduced by this pass). **No physical Android device was used for any
   part of this verification.**

**Micro-cleanup pass (2026-07-05, no plan file — a follow-up to the stabilization pass
above, not a new phase):** a second, narrower review of the same Android code found 4
more concrete, fixable issues plus a doc-drift item. Only `AndroidSensorBridge.hpp/.cpp`
and its tests were touched; `AndroidCompassBackend`/`AndroidMotionBackend`/`Compass`/
`Motion` were reviewed but needed no further changes (their existing Start()-failure
propagation already benefits from the bridge fixes transparently).

Files changed:
- `include/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.hpp`,
  `src/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.cpp`,
  `tests/Microsoft/Devices/Sensors/Detail/AndroidSensorBridgeTests.cpp`.

What changed:
1. **`Start()`/`Stop()` thread safety** — added `Impl::stateMutex_`, guarding the
   "already started"/"is a worker running" checks and every field `Start()` writes
   (`worker_`, `callback_`, `timeBetweenUpdates_`, `stopRequested_`/`startOutcome_`
   reset) against a second concurrent `Start()` or a concurrent `Stop()` reading
   `worker_` mid-write. Deliberately never held across `Start()`'s bounded
   condition-variable wait or `Stop()`'s blocking `join()`/non-blocking `detach()` —
   holding it across either would risk a real deadlock against the already-documented
   reentrant self-stop case (a `Stop()` call from the worker's own callback thread would
   block trying to acquire a mutex held by an external `Stop()` sitting inside `join()`,
   which itself is waiting for that same worker thread to finish). **Still explicitly
   unsupported, not fixed by this mutex:** two or more distinct *external* (non-worker)
   threads calling `Stop()` on the same bridge concurrently — this can still race on
   `join()` itself (documented in both methods' Doxygen comments as the caller's
   responsibility to serialize, matching `Compass`/`Motion`'s existing
   single-owner-thread usage).
2. **Stale looper state reset** — `Impl::Run()` now resets `looper_` to `nullptr` right
   before returning (after the event queue is disabled/destroyed), since an
   `ALooper_prepare()`-obtained looper is thread-local and torn down once its owning
   (worker) thread exits. Keeps the invariant "non-null `looper_` implies a live,
   running worker" exactly true.
3. **`ASensorEventQueue_setEventRate()` handled explicitly** — its previously-discarded
   return value is now checked; a negative result is treated as non-fatal and
   documented (the sensor is already enabled and will keep delivering at whatever rate
   the platform was already using), rather than silently ignored.
4. **`ConvertTimeBetweenUpdatesToSensorEventRateMicroseconds()` clamped at both ends** —
   already floored at 1 microsecond; now also ceils at `INT32_MAX` microseconds. A huge
   requested interval (e.g. `TimeSpan::MaxValue`) converted to microseconds as a
   `double` can exceed what `std::int32_t` can represent, and `static_cast`-ing an
   out-of-range `double` is undefined behavior, not a saturating truncation — this was a
   real, if obscure, reachable UB path. Two new tests
   (`MaxValueTimeSpanClampsToInt32Max`, `HugeButNotMaxTimeSpanClampsToInt32Max`) cover it
   on the host, since it's the pure-function half of `Start()` that doesn't need a real
   Android sensor queue.
5. **`NEXT.md` doc drift fixed** — Section 2's "Build"/"Working" text previously implied
   `cna_demo_devices` still packages into a working APK; it now says plainly that the
   `CNA` library cross-compiles cleanly but `cna_demo_devices` itself is currently
   blocked by the `SDL3/SDL_main.h` gap, and that the one successful APK build/emulator
   run was a historical Phase 9 result, not reproduced in either of the two follow-up
   passes. No claim of physical Android hardware verification was added or implied —
   the emulator-vs-hardware distinction from Section 2 is unchanged.
6. **Tests re-run:** targeted suite (`AndroidSensorBridgeTests`/`CompassTests`/
   `MotionTests`/`AndroidCompassMathTests`/`AndroidMotionMathTests`, 75 tests including
   the 2 new clamp tests) — all pass. Full Devices-only filter: 280/280 (2 expected
   hardware skips). Concurrency-relevant subset looped 40/40 clean. Full project
   `ctest`: 3268 tests, 3265 passed, 2 expected skips, 1 failure
   (`CueTest.PlayWeightedVariationFavorsHigherWeightEntryStatistically`, a pre-existing
   statistically-flaky Audio test outside `Microsoft::Devices`, confirmed to pass
   reliably in isolation — not a regression from this pass). `devices-asan`: 0 issues.
   `devices-tsan`: 24 warnings, all the same pre-existing `TimeSpan::copy_count` race,
   none new. `devices-ubsan`: same 3 pre-existing, out-of-scope `Vector3`/`Matrix`
   findings, 0 in any reviewed file. Android cross-compile of the `CNA` library
   re-verified clean, including a `llvm-nm` symbol check confirming the new
   `std::lock_guard<std::mutex>` code is actually present in the compiled Android
   object. `cna_demo_devices`'s Android cross-compile remains blocked by the
   pre-existing `SDL3/SDL_main.h` gap (unchanged by this pass). **No physical Android
   device was used for any part of this verification.**

---

## 4. Current blocker / main problem

**No code-level blocker.** Both `plan_devices.md` generations (143 + 31 + intermediate
phase tasks) are closed; the latest pass's own fresh audits found no regressions. What
remains is exclusively environment/scope-limited:
- No physical Android/iOS device or rumble-capable gamepad in this session (the
  emulator now works, but is explicitly not a substitute — see Section 2).
- No Apple toolchain in this Linux container (iOS cross-compilation impossible here),
  re-confirmed this session.
- `Compass.TrueHeading`/`Motion`'s coordinate remap — open questions requiring real
  hardware to resolve, not code-level gaps.

None of these are bugs to "fix" in this repo; they're gaps to close only when the
environment or scope changes (see Section 8).

---

## 5. Known bugs and limitations

- **By design, not a bug:** `Compass.TrueHeading` always equals `MagneticHeading` — real
  declination needs `System.Device.Location`, out of scope. `Motion.Calibrate` is never
  raised by any backend (`IMotionBackend` has no calibration callback at all).
- **Deliberate, documented limitation:** concurrent `Dispose()` calls on the *same*
  sensor instance from two threads is not guaranteed to give the losing caller a clean
  exception — it blocks until the winner's cleanup finishes, then returns as a silent
  no-op. Shared state is never corrupted; this matches the conventional .NET
  `IDisposable` contract.
- **Deliberate, unfixed by design:** destroying (not just `Dispose()`-ing)
  `Accelerometer` specifically from within its own `CurrentValueChanged` handler is
  unsafe (the legacy `ReadingChanged` event check touches `this` again afterward).
  `Gyroscope` has no second event and is fully safe. `Detail::AndroidSensorBridge`'s own
  analogous boundary — a callback reentrantly calling `Stop()`/triggering destruction of
  the *outer owning* object (`Compass`/`Motion`) from its own worker thread — was
  hardened in the 2026-07-05 stabilization pass: `Impl` is now `shared_ptr`-owned with
  the worker thread holding its own copy, so `Impl` itself can no longer be
  use-after-freed by a reentrant `Stop()`/destruction of the `AndroidSensorBridge`
  wrapper. Destroying the *outer* object (`Compass`/`Motion`) from inside its own
  callback remains explicitly unsupported, matching `Accelerometer`'s existing boundary
  — code-reviewed but never runtime-exercised (its real code path can't execute in this
  container, no Android device available).
- **Deliberate, documented limitation (micro-cleanup pass, 2026-07-05):**
  `Detail::AndroidSensorBridge::Start()`/`Stop()` are now guarded by a mutex against a
  second concurrent `Start()` and against `Start()`/`Stop()` racing on the same
  `std::thread` handle, but two or more distinct *external* (non-worker) threads calling
  `Stop()` on the same bridge at the same time is still unsupported — both could pass
  the "is a worker running" check and attempt to `join()` the same thread concurrently.
  Deliberately not fully serialized: doing so by holding the mutex across the blocking
  `join()` call would risk a real deadlock against the already-accepted reentrant
  self-stop case. Callers needing genuinely concurrent multi-thread `Stop()` must
  serialize it themselves; `Compass`/`Motion` (and this bridge) are designed for
  single-owner-thread `Start()`/`Stop()` usage, same as their own `Dispose()` guard above
  assumes.
- **New, out-of-scope finding this pass:** `sharp-runtime`'s
  `System::EventHandler<T>::Raise()` iterates its live handler list directly, not a
  snapshot — `Add()`/`Remove()` called reentrantly from within a handler mutate that
  same list mid-iteration. Confirmed the one plausible pattern (a handler removing a
  not-yet-invoked one) doesn't currently crash, but this is observed tolerance of
  `libstdc++`'s current `std::vector::erase()` behavior, not a guaranteed contract. Not
  currently reachable by any production code path in this namespace; not fixed here (a
  `sharp-runtime` concern, separate repo).
- **Open, unresolved question (not assumed either way):** whether `Motion`'s `Gravity`/
  `DeviceAcceleration`/`DeviceRotationRate`/`Attitude` need the same Android-landscape
  axis remap `Accelerometer`/`Gyroscope` use — left as raw sensor-frame axes,
  deliberately, pending real-hardware testing.
- **Needs verification, likely permanent:** iOS cross-compilation — no Apple toolchain
  possible in this Linux container.
- **Needs physical hardware verification (never done, for anything, ever):** every
  axis-sign question above, `VibrateController::Start()`/`StartLeftRight()` actually
  actuating a real motor, gamepad-exclusion on a real controller, `Compass`'s heading
  accuracy/calibration behavior, `Motion`'s attitude tracking. The emulator run this
  session closes the "software pipeline works" question only — see
  `docs/devices-hardware-checklist.md` §9 for the specific things an emulator cannot
  substitute for (no real vibration motor, no confirmed virtual rotation-vector sensor
  to inject through, etc.).
- **Unverified, low priority, no evidence of an actual bug:** `SensorFailedException`'s
  exact constructor overload signature is an educated guess.
- **Out of scope, not this repo's bug:** `sharp-runtime`'s `TimeSpan` copy-constructor
  TSan race — a sibling repo issue, do not fix it from here.

---

## 6. Architecture notes

```
include/Microsoft/Devices/Sensors/          ← XNA WP7 sensor API headers
include/Microsoft/Devices/Sensors/Detail/   ← internal-only, never in public headers
src/Microsoft/Devices/Sensors/              ← sensor implementations (SDL3-backed + Android-native)
tests/Microsoft/Devices/Sensors/            ← Google Test suites per class
include/Microsoft/Devices/                  ← VibrateController.hpp
src/Microsoft/Devices/                      ← VibrateController.cpp
examples/demo_devices/                      ← DevicesDemo (cna_demo_devices target)
examples/demo_devices/android/              ← Android Gradle/CMake app project (new)
docs/devices-hardware-checklist.md          ← manual real-hardware verification steps
docs/devices-build.md                       ← reproducible build/test commands (incl. Android APK)
docs/devices-native-backend-design.md       ← Compass/Motion native backend design — Android IMPLEMENTED, iOS still sketch
docs/devices-api-coverage.md                ← per-member API coverage table (new)
docs/devices-android.md                     ← consolidated Android-specific reference (new)
docs/location-future-plan.md                ← why GPS/location isn't here
```

- **`SensorBase<T>`** owns `CurrentValue`, `IsDataValid`, `TimeBetweenUpdates`,
  `CurrentValueChanged`, `Dispose()`. Every field is mutex-guarded; getters return by
  value. Has `ClaimDisposalOnce()`/`WaitForDisposalToComplete()` (protected) — derived
  `Dispose(bool)` overrides must use these. **Do not restructure further** — stable,
  hardened across many phases, and now genuinely exercised on Android (`Compass`/
  `Motion`'s `Start()` can actually succeed there, unlike before).
- **Invariant:** any class overriding `Dispose(bool)` must add
  `using SensorBase<T>::Dispose;`, or C++ name-hiding breaks the inherited public
  no-arg `Dispose()`.
- **`Accelerometer`/`Gyroscope`** share `Detail::SdlSensorSubsystem<TSensor>`, unchanged
  by this pass — see "SDL backend scope, unchanged" in
  `docs/devices-native-backend-design.md`.
- **`Compass`/`Motion`** each hold a `std::unique_ptr<Detail::ICompassBackend>`/
  `IMotionBackend`, constructed only inside `#if defined(__ANDROID__)`. Both interfaces
  compile and are mockable on every platform (no platform-specific `#include`); the
  concrete `AndroidCompassBackend`/`AndroidMotionBackend` implementations are entirely
  `#ifdef __ANDROID__`-gated. Both build on the shared `Detail::AndroidSensorBridge`
  (one instance per Android sensor type, owns its own worker thread + `ALooper` —
  thread-affine, can't be pumped externally). `SetBackendForTesting()` (`NOXNA`, both
  classes) lets tests inject a fake backend without needing Android. See
  `docs/devices-native-backend-design.md` for the full field-mapping rationale and
  `docs/devices-api-coverage.md` for the per-member table.
- **`VibrateController`:** unchanged this pass — SDL3 haptic-backed, no native Android
  bridge (deliberately rejected, see Section 3).
- **`GetTypeName()` invariant:** returns `.`-separated fully-qualified .NET names,
  tagged `NOXNA`.
- **Boundaries — do not cross:**
  - `third_party/SDL` is vendored with its own `CLAUDE.md` forbidding AI-authored
    contributions — read-only for research.
  - `sharp-runtime` is a sibling repo under separate development — if a build breaks in
    a file Devices work didn't touch, check there first. Do not fix `sharp-runtime` bugs
    (the `TimeSpan` race, the `EventHandler<T>` reentrancy fragility) by editing files
    there directly.
  - Do not expand `Microsoft::Devices` scope to camera, radio, or
    phone-call/photo-picker APIs.
  - Do not fake `Compass`/`Motion` data from other sensors, and do not add GPS/location
    to `Microsoft::Devices::Sensors` under any circumstances (including as `NOXNA`) —
    see `docs/location-future-plan.md`.

---

## 7. Useful commands

**ZIP-export caveat:** every command below assumes a real `git clone` with submodules
initialized (`git submodule update --init --recursive`) — a bare source export has
empty `third_party/SDL`/`SDL_image`/`SDL_mixer`/`vendor/googletest` and will not
configure. See `docs/devices-build.md` Section 0.

```bash
# Configure (only if CMakeCache.txt is stale/missing):
cmake -S . -B cmake-build-debug \
      -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug

# Build:
cmake --build cmake-build-debug --target CNA -j$(nproc)
cmake --build cmake-build-debug --target CnaTests -j$(nproc)

# Run all tests:
cd cmake-build-debug && ctest --output-on-failure

# Devices-only filter (273 tests; includes Phase 6-8's new AndroidSensorBridge/
# AndroidCompassMath/AndroidMotionMath suites — see docs/devices-build.md Section 2 for
# the loop-it-20-60x convention before trusting a single pass on new concurrency tests):
cd cmake-build-debug && ctest --output-on-failure -R "Accelerometer|SensorFailed|Compass|Gyroscope|Attitude|Motion|VibrateController|SensorSubsystemOwnership|AndroidSensorOrientation|SensorBase|ScopeExit|AndroidSensorBridge|AndroidCompassMath|AndroidMotionMath"

# Build and run the Devices demo (needs a real display):
cmake --build cmake-build-debug --target cna_demo_devices -j$(nproc)
./cmake-build-debug/cna_demo_devices

# Android cross-compile check (NDK at ~/Android/Sdk/ndk/):
cmake -S . -B cmake-build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$HOME/Android/Sdk/ndk/30.0.14904198/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DCNA_BUILD_TESTS=OFF
cmake --build cmake-build-android --target CNA -j$(nproc)

# Build + install + run the Android APK (see docs/devices-build.md Section 4.1 for the
# full command sequence and every bug hit/fixed getting here):
cd examples/demo_devices/android/com.openeggbert.cna.demodevices
echo "sdk.dir=$HOME/Android/Sdk" > local.properties
export ANDROID_HOME="$HOME/Android/Sdk"
./gradlew -PBUILD_WITH_CMAKE assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.openeggbert.cna.demodevices/com.openeggbert.cna.demodevices.DemodevicesActivity

# Vulkan/BGFX:
cmake --build cmake-build-vulkan --target CNA --target CnaTests -j$(nproc)
cmake --build cmake-build-bgfx   --target CNA --target CnaTests -j$(nproc)

# Sanitizer builds (see docs/devices-build.md Section 6 for findings history):
cmake --preset devices-asan && cmake --build --preset devices-asan
cmake --preset devices-tsan && cmake --build --preset devices-tsan
cmake --preset devices-ubsan && cmake --build --preset devices-ubsan
```

No dedicated lint/format tooling is configured for this project. No CI infrastructure
exists in this repo (confirmed, Task DEVICES-0127) — the commands above are the current
gate.

---

## 8. Next smallest tasks

No open plan file drives further `Microsoft::Devices` work. Pick one, or ask the user
first — do not invent new scope, and do not open a new plan file solely to keep
re-auditing code that many consecutive audits already found nothing new in.

1. **Physical hardware verification**, if real Android/iOS hardware or a
   rumble-capable gamepad becomes available. Goal: work through
   `docs/devices-hardware-checklist.md` (now 9 sections) using `cna_demo_devices`
   (desktop or the new Android APK), mark each item verified/failed with evidence.
   Files: none changed unless a real bug is found. Verification: the checklist itself,
   updated with results.
2. **Resolve `Motion`'s coordinate-remap open question** (Task DEVICES-0111) — requires
   real Android hardware to determine whether `Gravity`/`DeviceAcceleration`/
   `DeviceRotationRate`/`Attitude` need the same landscape remap `Accelerometer`/
   `Gyroscope` use. Files: `AndroidMotionBackend.cpp`, `AndroidMotionMathTests.cpp`.
   Verification: a new test case for whatever convention turns out correct.
3. **iOS Compass/Motion backend**, only if an Apple toolchain ever becomes available in
   this environment, and only once explicitly scoped as its own task. Goal: implement
   `ICompassBackend`/`IMotionBackend` for iOS per `docs/devices-native-backend-design.md`'s
   already-confirmed-compatible interfaces. Files: new `.mm`/Swift under
   `src/Microsoft/Devices/Sensors/Detail/`. Verification: new unit tests plus the
   existing Devices-only `ctest` filter still green.
4. **Anything outside `Microsoft::Devices`.** Ask before assuming scope.

---

## 9. Do not do yet

- Do not claim `Microsoft::Devices` is "complete" as a flat statement — use Section 2's
  layered breakdown (API vs. SDL/native runtime vs. hardware-verified), now with
  `Compass`/`Motion` real on Android but still stub elsewhere.
- Do not restructure `SensorBase<T>`, `Detail::SdlSensorSubsystem<TSensor>`, or the
  `dispatchToken_`/global-mutex locking scheme without a concrete, newly-found bug.
- Do not fake `Compass`/`Motion` data from other sensors (accelerometer-only heading,
  manual gyroscope integration instead of consuming Android's own OS-fused rotation
  vector), and do not add GPS/location to `Microsoft::Devices::Sensors` under any
  circumstances (including as `NOXNA`) — see `docs/location-future-plan.md`.
- Do not build a native Android vibration (JNI) backend — explicitly decided against,
  SDL3's own Android haptic backend already covers it (Task DEVICES-0031).
- Do not claim Android/iOS physical-hardware support, or a real-device run, unless it
  was actually done in the current session — the emulator is not a substitute (Section 2).
- Do not edit anything under `third_party/SDL` — vendored, has its own `CLAUDE.md`
  forbidding AI-authored contributions.
- Do not fix bugs found in `sharp-runtime` by editing files there directly — it's a
  separate repo with its own build/test/commit process. This now includes the
  `EventHandler<T>` reentrancy fragility found this pass, not just the `TimeSpan` race.
- Do not trust a single passing `ctest` run as proof a new concurrency/lifetime change
  is correct — loop it (20-60+ iterations) and/or run it under a sanitizer preset first.
- Do not open a new Devices plan/phase file just to keep iterating — only for a
  concrete, newly-found code-level issue.
- Do not assume `examples/demo_devices/src/*` edits automatically apply to the Android
  build — `--variant copy` duplicated the sources into
  `examples/demo_devices/android/.../app/jni/src/`; re-copy (or regenerate) after any
  change to the originals.

---

## 10. Resume prompt

```
Read NEXT.md first. Microsoft::Devices/Sensors has Compass/Motion now real on Android
(Detail::AndroidCompassBackend/AndroidMotionBackend, pure NDK, no JNI) plus a working
Android APK for cna_demo_devices — both closed out by the second plan_devices.md
(143 tasks, Phases 0-10). There is no open plan file for this namespace right now. Do
not summarize it as flatly "complete"; use Section 2's layered status — real on Android,
honest stub everywhere else, never physically hardware-verified anywhere.

Inspect only the files needed for the first task you pick from Section 8 (or ask the
user what to prioritize). Do not refactor unrelated code. Make one small, verified
improvement at a time.

Run the relevant build/test command from Section 7 after each change — and if the
change touches concurrency or object lifetime, also loop the test (20-60+ iterations,
docs/devices-build.md Section 2) and/or run it under a sanitizer preset
(devices-asan/devices-tsan/devices-ubsan, Section 6) before trusting it, per Section 9.

Update NEXT.md after finishing, keeping it concise — this file should stay a short,
current-state handoff, not a full phase-by-phase history (that detail belongs in the
relevant plan_devices*.md file).
```
