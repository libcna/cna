# NEXT.md — CNA Project Handoff

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model, built on
SDL3 and a pluggable graphics backend (`EASYGL` / `VULKAN` / `BGFX`). It
preserves XNA-style public APIs (`Microsoft::Xna::Framework`,
`Microsoft::Devices`) while using modern C++ internally. It targets desktop
Linux/Windows/macOS, Android, and iOS. Branch: `feature/devices`.

**`Microsoft::Devices` has now been through two hardening passes** —
`plan_devices_phase4.md` (2026-07-03/04) and `plan_devices_phase5.md`
(2026-07-04). The second pass's own explicit premise was to **not trust the
first pass's "complete"/"hardened" claims** and re-audit from the actual code.
That audit found the first pass had, as a side effect of fixing one real bug
(Task P4-8), introduced a *different* real bug in the same commit (Task
P5-1), left at least one confirmed data race unfixed in the shared
`SensorBase<T>` base class (Task P5-2), and had skipped an RAII cleanup based
on an assumption that was never actually checked and turned out to be false
(Task P5-11). **Read this as the honest status, not as another "now it's
really done" claim** — see Section 2's layered breakdown instead of a single
verdict.

**Plan history:**
- `plan_devices.md` (31 tasks) — closed.
- `plan_devices_phase2.md` (17 tasks) — closed. Its one open item (Task P2-7,
  Android/iOS build verification) is superseded by `plan_devices_phase4.md`
  Tasks P4-11 (Android, done) / P4-12 (iOS, confirmed still blocked).
- `plan_devices_phase3.md` (12 tasks) — closed.
- `plan_devices_phase4.md` (14 tasks) — closed 2026-07-04, all 14 tasks done.
  Read `plan_devices_phase5.md`'s "Audit findings" section before trusting
  any specific claim from this plan's own Resolution notes without
  re-checking — that audit is what found the two issues above.
- `plan_devices_phase5.md` (14 tasks, re-audit + hardening) — **closed**, all
  14 tasks done, including Task P5-14's final re-verification against all 3
  graphics backends (`EASYGL`/`VULKAN`/`BGFX`) plus the Android
  cross-compile. Full task-by-task detail, and the audit findings that
  motivated each fix, live in that file.

**Important architectural decisions:**
- Public API names/signatures must match XNA 4.0 (or, for `Microsoft::Devices`,
  the documented WP7 SDK) exactly; C# properties become `getXProperty()` /
  `setXProperty()`.
- Non-XNA extensions are tagged `NOXNA` on the public declaration.
- `Microsoft::Devices::Sensors::SensorBase<T>` (header-only template) is the
  shared base for all sensor classes (`CurrentValue`, `IsDataValid`,
  `TimeBetweenUpdates`, `CurrentValueChanged`, `Dispose()`, and an
  `isSupported_` flag gating `CurrentValue`'s `InvalidOperationException`).
  Now has its own private mutex (Task P5-2) — see Section 6.
- `VibrateController` is a singleton reached via `getDefaultProperty()`. It
  does not derive `SensorBase<T>`/`IDisposable` — it does not follow the
  sensor pattern. Now has a real destructor (Task P5-11) — see Section 6.
- FNA (the usual local reference tree for XNA behavior) implements **no**
  equivalent of `Microsoft::Devices` (it's WP7-only) — API completeness was
  judged from archived Microsoft Learn "previous-versions" WP7 SDK docs.
- Tests live under `tests/` mirroring the `include`/`src` namespace path
  1:1, using Google Test, one file per class.

---

## 2. Current status — layered, not a single verdict

**API surface (matches documented WP7 shape):** implemented and stable.
`Accelerometer`, `Compass`, `Gyroscope`, `Motion`, `VibrateController`, and
every reading/event-args/exception type match the archived WP7 SDK docs
(`plan_devices_phase2.md` Task P2-2's independent re-verification). This
layer has not changed in Phase 5 and is the most trustworthy claim in this
document.

**SDL runtime implementation (`Accelerometer`/`Gyroscope`/`VibrateController`):**
real, SDL3-backed, and — as of Phase 5 — the specific bugs an actual
line-by-line re-audit found are now fixed:
- Subsystem probe/instance-ownership init-quit balancing is correct for both
  `SDL_INIT_SENSOR` (Task P5-1, fixing a leak Task P4-8 itself introduced)
  and `SDL_INIT_HAPTIC` (Task P5-11, with an RAII destructor now closing
  `VibrateController`'s haptic device — previously left open on a wrong
  assumption).
- `SensorBase<T>`'s `currentValue_`/`isDataValid_` are now mutex-protected
  (Task P5-2) — previously a real, unguarded data race between the SDL
  callback thread and the game thread, undetected through 4 prior plans.
- `Accelerometer`/`Gyroscope`'s callback quiescence tracking is a
  per-thread-id vector, not a bool or plain counter (Tasks P5-2/P5-3) — the
  single-bool version could under-count concurrent dispatches, and a
  handler disposing its own sender from inside its own callback used to
  deadlock (previously an accepted, documented limitation — now fixed, not
  just documented).
- The two classes' subsystem/event-watch machinery is now a shared,
  de-duplicated internal template (`Detail::SdlSensorSubsystem<TSensor>`,
  Task P5-4) instead of two hand-maintained near-copies — verified
  byte-for-byte behavior-preserving against the full existing test suite.

Still **not independently verified against real hardware** — see the next
paragraph.

**Native mobile backend (Compass/Motion magnetometer, real device sensor
fusion):** **missing, by design, not a gap to silently close.** SDL3 exposes
no magnetometer/compass API on any platform. `Compass`/`Motion` are honest
`SensorState::NotSupported` stubs — confirmed still true and still tested
(Task P5-9). `plan_devices_phase5.md`'s "Future native backend plan" section
sketches concrete Android (`SensorManager`/JNI, `TYPE_MAGNETIC_FIELD`,
`TYPE_ROTATION_VECTOR`) and iOS (`CLLocationManager` heading APIs,
`CMDeviceMotion`) paths — not implemented, planning only. GPS/location is
explicitly **not** part of this — see `docs/location-future-plan.md`
(Task P5-10).

**Hardware manually verified:** **none of it, on any physical device, in any
session to date.** Every claim above is verified by code reading, unit
tests, and cross-compilation — never by running on a real accelerometer,
gyroscope, or haptic motor. `docs/devices-hardware-checklist.md` (Task
P4-13, tightened in Task P5-7) exists specifically to close this gap
whenever real hardware becomes available; `examples/demo_devices/`
(`cna_demo_devices`, Task P4-14) is the tool to use when it does.

**Build:** `CNA` and `CnaTests` build cleanly with the `EASYGL` backend
(`cmake-build-debug`) as of Task P5-12's commit on `feature/devices`
(2026-07-04, not yet pushed). A full top-level build (`cmake --build .`,
every target including `cna_demo_devices`) also builds clean — re-verified
after Task P5-4's large refactor specifically, since it touched the most
surface area of any single task in either phase.

**`CNA` (static lib only) also builds clean for Android** (arm64-v8a, NDK
r30, API 24, `cmake-build-android/`) — re-verified after Task P5-7's changes
to the Android-specific axis-remap code, using the NDK's own `llvm-nm` to
confirm the relevant functions are actually compiled in (the host's plain
`nm` gives empty/wrong output against the cross-compiled object files).

**iOS cross-compilation confirmed still blocked** — no toolchain of any kind
in this Linux container. Re-confirmed during Phase 5's own audit (not just
carried over from Phase 4's finding).

**`VULKAN`/`BGFX` re-verified clean against Phase 5's full changeset**
(Task P5-14, 2026-07-04) — both build `CNA`/`CnaTests` with zero errors;
Devices-only filter 187/187 passing on each, matching `EASYGL` exactly.
Full suite: `VULKAN` 1960 tests/99% passing (13 pre-existing `Vulkan_*`
graphics-smoke failures needing a real GPU/driver, same baseline as
before Phase 5); `BGFX` 1954 tests/99% passing (3 pre-existing `Bgfx_*`
failures, same reason). This closes the one concrete gap this document
itself flagged after Task P5-4's large `Accelerometer`/`Gyroscope`
refactor — no regressions found on either backend.

**Tests:** last full `ctest` run (`EASYGL`) as of Task P5-14: **2012 tests,
99% passing.** The 2 failures are pre-existing, unrelated `EasyGL`/
`easy-gl` graphics-backend bugs (`EasyGL_MRT_TwoAttachments`,
`easy-gl-resource-smoke-tests`) — this environment unexpectedly gained a
real GPU/display mid-session (Task P5-1's own discovery), which surfaced
these for the first time (previously silently `Not Run` headless, which is
why every prior session's baseline said "64 failures" — that was actually
"64 tests never run at all," not 64 failing tests). Devices-only filter:
187 tests via `ctest -R`, 100% passing (plus 2 tests that correctly
`GTEST_SKIP()` themselves on hardware-dependent paths this machine doesn't
have).

---

## 3. Recent changes

**2026-07-03/04 — `plan_devices_phase4.md`, all 14 tasks.** Summarized in
that file; the short version, now qualified by what Phase 5 found: fixed 3
confirmed real bugs, closed a callback-lifetime use-after-free window,
added the first real event-path tests via `NOXNA` synthetic-injection
hooks, fixed a `Timestamp` bug, fixed a *different* SDL subsystem-ownership
bug (Task P4-8) while — unnoticed until Phase 5 — introducing a new one in
the same commit, added `VibrateController` thread-safety (implemented
correctly, but skipped an RAII cleanup based on an unverified assumption
that Phase 5 found was wrong), replaced a fragile gamepad-exclusion
heuristic with an ID-based one, verified Android/iOS build status, wrote
a hardware checklist, and added a demo screen.

**2026-07-04 — `plan_devices_phase5.md`, Tasks P5-1 through P5-12 done
(re-audit + hardening, not new features):**
- **Audit (before any code change):** re-read every sensor/`VibrateController`
  file and test directly rather than trusting Phase 4's own claims. Found:
  a real subsystem-refcount leak Task P4-8 introduced as a side effect of
  fixing a different one; a plausible (SDL-documented, not reproduced
  headless) use-after-free window from a single-bool callback-quiescence
  flag; confirmed data races in `SensorBase<T>` and in
  `started_`/`state_`/`subsystemHeld_`'s inconsistent locking; and that
  Task P4-9's stated reason for skipping `VibrateController` RAII cleanup
  (an assumed `SDL_Quit()` ordering) was never actually checked and is
  false — this codebase never calls `SDL_Quit()` anywhere.
- **P5-1** — fixed the leak: `getIsSupportedProperty()`'s probe now uses a
  balanced `SensorSubsystemProbeGuard` RAII pair instead of an unbalanced
  `EnsureSensorSubsystemInitialized()` call.
- **P5-2** — replaced the single-bool callback-quiescence flag with a
  `std::vector<std::thread::id>` count; fixed `SensorBase<T>`'s
  `currentValue_`/`isDataValid_` data race with a new private mutex, never
  held across `CurrentValueChanged.Raise()`. `getCurrentValueProperty()`
  now returns by value, not by reference (matches the real WP7 API's
  value-type semantics more closely too).
- **P5-3** — removed the self-dispose deadlock Task P4-2 had explicitly
  accepted as permanent: `Dispose()` now recognizes when every remaining
  in-flight dispatch belongs to its own calling thread and doesn't wait on
  itself, while still correctly waiting for genuinely other threads.
- **P5-4** — extracted `Accelerometer`/`Gyroscope`'s near-duplicate
  subsystem/event-watch machinery into a shared internal
  `Detail::SdlSensorSubsystem<TSensor>` template, keeping SDL types out of
  both public headers (forward-declared, function-local-static storage,
  same idiom as the old `void* g_sensor_`). Verified byte-for-byte
  behavior-preserving against all 41 pre-existing tests, plus a full
  top-level rebuild.
- **P5-5** — documented that `CurrentValueChanged`/`ReadingChanged` fire
  synchronously on whatever thread SDL invokes the event watch on, not
  necessarily the game thread; evaluated and explicitly declined to add a
  speculative main-thread dispatch queue with no concrete need for it yet.
- **P5-6** — added `SetSupportedForTesting()` (separate from
  `SetStartedForTesting()`) and tests proving `getCurrentValueProperty()`
  both reflects synthetic updates when marked supported *and* still throws
  on unsupported hardware when not — closing a documentation/test gap
  around behavior that was already correct.
- **P5-7** — extracted the Android axis-remap sign math into a pure,
  platform-independent function (`Detail::ConvertAndroidPortraitToXnaLandscape()`),
  testable on any platform; added 5 unit tests; re-verified the Android
  cross-compile afterward.
- **P5-8/P5-9/P5-10** — wrote (documentation only, no code) a native
  Android/iOS backend plan sketch for `Compass`/`Motion`, and
  `docs/location-future-plan.md` stating GPS/location does not belong in
  `Microsoft::Devices::Sensors`.
- **P5-11** — added `~VibrateController()` RAII cleanup for `g_haptic`
  (confirmed safe per the audit above), replacing `EnsureHapticSubsystemInitialized()`'s
  `SDL_WasInit()` guard with explicit own-state tracking.
- **P5-12** — wrote `docs/devices-build.md`; every command in it was
  actually re-run this session, which caught and corrected one inaccuracy
  in the first draft (two test-invocation forms don't cover the same tests,
  contrary to the draft's initial assumption).

Every task above re-ran the Devices-relevant test filter and the full
`ctest` suite after its change — consistently the same 2 pre-existing,
unrelated `EasyGL`/`easy-gl` failures throughout, no regressions at any
point. See `plan_devices_phase5.md`'s per-task Resolution notes for full
detail, exact commands run, and the reasoning behind every non-obvious
choice — this document only summarizes.

All work committed on `feature/devices`, not yet pushed.

---

## 4. Current blocker / main problem

**No blocker.** `plan_devices_phase5.md` is fully closed (Task P5-14, its
final task, re-verified `EASYGL`/`VULKAN`/`BGFX`/Android all clean against
the complete changeset — no regressions found anywhere). See Section 8 for
what's next; none of it is a blocker, just unstarted or unverifiable in
this environment (physical hardware, iOS toolchain).

---

## 5. Known bugs and limitations

- **By design, not a bug:** `Compass`/`Motion` are permanent
  `SensorState::NotSupported` stubs — SDL3 has no magnetometer API on any
  platform. See `plan_devices_phase5.md`'s "Future native backend plan" for
  what a real implementation would need.
- **Resolved (Task P5-14, 2026-07-04):** `VULKAN`/`BGFX` builds re-verified
  clean against Phase 5's complete changeset — see Section 2.
- **Needs verification, likely permanent:** iOS cross-compilation — no
  Apple toolchain possible in this Linux container.
- **Needs physical hardware verification (never done, any session):**
  Android's axis-remap math's actual tilt-direction correctness;
  `VibrateController::Start()`/`StartLeftRight()` actually actuating a real
  phone motor / two distinct motors; the gamepad-exclusion filter not
  competing with `GamePad::SetVibration()` on a real connected gamepad. Use
  `docs/devices-hardware-checklist.md` + `cna_demo_devices` when real
  hardware is available — nothing in this codebase can verify these itself.
- **Resolved as of Phase 5 (previously listed here as accepted
  limitations — do not re-list these as open):** the self-dispose deadlock
  (Task P5-3), the `SensorBase<T>` data race (Task P5-2), the subsystem
  probe leak (Task P5-1), and `VibrateController`'s unclosed haptic device
  (Task P5-11).
- **Unverified, low priority, no evidence of an actual bug:**
  `SensorFailedException`'s exact constructor overload signature remains an
  educated guess — its MSDN doc page consistently lacks a Constructors
  table, more consistent with an archival gap than proof it doesn't exist.
  See `plan_devices_phase3.md` Task P3-12.

---

## 6. Architecture notes

```
include/Microsoft/Devices/Sensors/          ← XNA WP7 sensor API headers
include/Microsoft/Devices/Sensors/Detail/   ← internal-only (Task P5-4/P5-7), never in public headers
src/Microsoft/Devices/Sensors/              ← sensor implementations (SDL3-backed)
tests/Microsoft/Devices/Sensors/            ← Google Test suites per class
include/Microsoft/Devices/                  ← VibrateController.hpp
src/Microsoft/Devices/                      ← VibrateController.cpp
tests/Microsoft/Devices/                    ← VibrateControllerTests.cpp
examples/demo_devices/                      ← DevicesDemo (cna_demo_devices target)
docs/devices-hardware-checklist.md          ← manual real-hardware verification steps
docs/devices-build.md                       ← reproducible build/test commands (Task P5-12)
docs/location-future-plan.md                ← why GPS/location isn't here (Task P5-10)
```

**`SensorBase<T>`** (header-only template) owns `CurrentValue`,
`IsDataValid`, `TimeBetweenUpdates`, `CurrentValueChanged`, `Dispose()`, an
`isSupported_` flag gating `CurrentValue`'s `InvalidOperationException`,
and (Task P5-2) a private `mutable std::mutex` guarding
`currentValue_`/`isDataValid_` — never held across `CurrentValueChanged.Raise()`.
`getCurrentValueProperty()` returns by value (Task P5-2), not by reference.
Concrete sensors override `Start()`, `Stop()`, `Dispose(bool)`, and must
call `setIsSupportedProperty()` once from their constructor. **Do not
restructure this class further** — stable, used by production code, and
now correctly thread-safe.

**Invariant:** any class overriding `Dispose(bool)` **must** add `using
SensorBase<T>::Dispose;`, or C++ name-hiding silently breaks the inherited
public no-arg `Dispose()`. This bug has already been found and fixed 4
times across the project's history — don't reintroduce it in any new
sensor class.

**Sensor pattern (real, SDL3-backed — `Accelerometer`/`Gyroscope`):** as of
Task P5-4, the shared subsystem/event-watch machinery lives in
`Detail::SdlSensorSubsystem<TSensor>` (`include/Microsoft/Devices/Sensors/Detail/SdlSensorSubsystem.hpp`)
— one instantiation per class, reached via each class's own private
`static GetSubsystem()` (defined in that class's own `.cpp` as a
function-local static, keeping SDL types out of the public header, same
"opaque handle" idiom the old `void* g_sensor_` used). It owns: balanced
subsystem probe/instance init-quit (Task P5-1), default-sensor discovery,
event-watch registration, and the started-instances/dispatching-thread-id
bookkeeping (Tasks P5-2/P5-3). `state_`/`started_`/`subsystemHeld_`/
`dispatchingThreadIds_` remain genuine per-instance members on
`Accelerometer`/`Gyroscope` themselves, consistently guarded by the shared
subsystem's mutex wherever touched. `ProcessSensorUpdateEvent()` validates
the event belongs to this instance's open device, then delegates to
`DispatchSensorReading()` — this split lets the `NOXNA` test-only
`InjectSyntheticSensorUpdate()`/`SetStartedForTesting()`/`SetSupportedForTesting()`
hooks exercise the real dispatch path without a real, opened SDL sensor.
`Timestamp` on dispatched readings is always
`System::DateTimeOffset::getUtcNowProperty()` — real wall-clock time.
Android's axis-remap sign math is a pure function,
`Detail::ConvertAndroidPortraitToXnaLandscape()`
(`include/Microsoft/Devices/Sensors/Detail/AndroidSensorOrientation.hpp`,
Task P5-7), shared identically by both classes and unit-tested on any
platform. **Do not** "fix" the subsystem pattern by building a separate
hand-rolled reference counter; SDL already provides one, and
`SdlSensorSubsystem`'s `ProbeGuard`/per-instance `subsystemHeld_` already
use it correctly.

**Stub pattern (`Compass`/`Motion`):** always `SensorState::NotSupported`;
`Start()` always throws `SensorFailedException`; still expose the
`Calibrate` event for API completeness even though it's never raised.

**`VibrateController`:** singleton (private default constructor, reached
via `getDefaultProperty()`), no `SensorBase<T>`, no `IDisposable`, lives
directly in `Microsoft::Devices` (not `::Sensors`). Drives SDL3's haptic API
directly; file-static `g_haptic`/`g_leftRightEffectId` guarded by a
`std::mutex` locked for the entire body of every public method. As of Task
P5-11, `~VibrateController()` closes `g_haptic` and releases
`SDL_INIT_HAPTIC` (tracked via a file-static `g_subsystemHeld` bool, not
`SDL_WasInit()`) when the singleton is destroyed at normal process
termination — confirmed safe since this codebase never calls `SDL_Quit()`
anywhere. Excludes haptic devices that are also connected joysticks/gamepads
from device selection via ID correlation (`SDL_OpenHapticFromJoystick()`),
not name matching. `Start()`/`StartLeftRight()` correctly stop each other's
SDL effect before starting.

**`GetTypeName()` invariant:** must return `.`-separated fully-qualified
.NET names (e.g. `"Microsoft.Devices.Sensors.Compass"`), tagged `NOXNA`.
Classes deriving `System::Object` (via `SensorBase<T>`) use the
`GetTypeNameHPP()`/`GetTypeNameCPP(Class, "Name")` macro pair; classes that
don't (e.g. `AccelerometerReading`-style value types) declare a plain
`NOXNA std::string GetTypeName() const;` method instead.

**Boundaries — do not cross:**
- `third_party/SDL` is vendored and has its **own `CLAUDE.md` forbidding
  AI-authored code contributions**. Safe to *read* for research, never edit.
- `sharp-runtime` is a sibling repo under separate, concurrent development —
  its public API can change without notice mid-session (has happened
  before). If a build breaks in a file `Microsoft::Devices` work didn't
  touch, check there first before assuming you broke it. It has its own
  `CLAUDE.md`/`NEXT.md` and its own git history — commits there are
  separate from `cna_devices`'s.
- Do not expand `Microsoft::Devices` scope to camera, radio, or
  phone-call/photo-picker APIs — explicitly out of scope.
- Do not implement sensor fusion in `Motion`, and do not add any GPS/
  location member to `Microsoft::Devices::Sensors` under any circumstances
  (including as `NOXNA`) — see `docs/location-future-plan.md`.

---

## 7. Useful commands

See `docs/devices-build.md` (Task P5-12) for the full, individually
re-verified set with exact test counts. Quick reference:

```bash
# Configure (only needed once, or if CMakeCache.txt is stale/points elsewhere):
cmake -S /rv/data/development/github.com/openeggbert/cna_devices \
      -B /rv/data/development/github.com/openeggbert/cna_devices/cmake-build-debug \
      -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug

# Build library / tests:
cmake --build cmake-build-debug --target CNA -j$(nproc)
cmake --build cmake-build-debug --target CnaTests -j$(nproc)

# Run all tests:
cd cmake-build-debug && ctest --output-on-failure

# Run only Devices/Sensors + VibrateController tests (187 tests as of Task P5-12):
cd cmake-build-debug && ctest --output-on-failure -R "Accelerometer|SensorFailed|Compass|Gyroscope|Attitude|Motion|VibrateController|SensorSubsystemOwnership|AndroidSensorOrientation"

# Build the Devices demo screen:
cmake --build cmake-build-debug --target cna_demo_devices -j$(nproc)
./cmake-build-debug/cna_demo_devices   # needs a real display; fails headless (no GPU/display), same as cna_demo_input

# Android cross-compile check (NDK present in this container at ~/Android/Sdk/ndk/):
cmake -S . -B cmake-build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$HOME/Android/Sdk/ndk/30.0.14904198/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DCNA_BUILD_TESTS=OFF
cmake --build cmake-build-android --target CNA -j$(nproc)

# Cross-platform build verification (Vulkan/BGFX; last verified 2026-07-04
# BEFORE Phase 5's changes — needs re-running, see Section 4/8):
cmake --build cmake-build-vulkan --target CNA --target CnaTests -j$(nproc)
cmake --build cmake-build-bgfx   --target CNA --target CnaTests -j$(nproc)
```

No dedicated lint/format tooling is configured for this project as of this
writing.

---

## 8. Next smallest tasks

With `plan_devices_phase4.md` and `plan_devices_phase5.md` both fully closed and
all 3 graphics backends + Android re-verified clean against the complete
changeset (Task P5-14), there is no standing plan file driving further
`Microsoft::Devices` work and no known outstanding code issue. Pick one of
these, or ask the user what the next priority actually is — do not invent
new `Microsoft::Devices` scope without a plan or explicit request.

1. **Physical hardware verification**, if real Android/iOS hardware or a
   rumble-capable gamepad ever becomes available in a session: work through
   `docs/devices-hardware-checklist.md` using `cna_demo_devices`. Not
   attemptable in this headless container — don't attempt it here, just
   note if the environment changes. This is the single biggest remaining
   gap — everything else in this namespace has been verified by code
   reading, unit tests, or cross-compilation, never by real hardware.

2. **Native Android/iOS backend for `Compass`/`Motion`**, if ever scoped as
   its own task — `plan_devices_phase5.md`'s "Future native backend plan"
   section has a starting sketch (Android `SensorManager`/JNI, iOS
   `CLLocationManager`/`CMDeviceMotion`), not verified against any real
   platform API. A real scoping/design pass would be needed first, not a
   direct implementation from that sketch alone.

3. **A third independent re-audit of `Microsoft::Devices`**, if a future
   session has reason to doubt this one — Phase 5's entire premise was that
   Phase 4's "complete" claims didn't survive re-reading the actual code;
   the same discipline should apply to Phase 5's own claims, not just
   Phase 4's.

4. **Anything outside `Microsoft::Devices`.** Ask before assuming scope.

---

## 9. Do not do yet

- Do not claim `Microsoft::Devices` is "complete"/"hardened" as a flat
  statement — use Section 2's layered breakdown instead. Phase 5 exists
  specifically because Phase 4 made that mistake and it hid real bugs.
- Do not "fix" the SDL sensor subsystem ownership pattern by building a
  separate hand-rolled reference counter — SDL3 already provides one (see
  Section 6).
- Do not re-introduce a single bool/counter for callback quiescence
  tracking — the `std::vector<std::thread::id>` design (Tasks P5-2/P5-3)
  exists because simpler versions have concrete, traced failure modes.
- Do not re-add the self-dispose deadlock as an "accepted limitation" —
  it's fixed (Task P5-3), not merely documented.
- Do not refactor or restructure `SensorBase<T>`, `Detail::SdlSensorSubsystem`,
  or `ISensorReading` further without a concrete need — stable, used by
  production code, and Task P5-4 already did the one refactor that was
  actually asked for.
- Do not expand `Microsoft::Devices` to camera, radio, phone-hardware APIs,
  or GPS/location (including as `NOXNA`) — see Section 6 and
  `docs/location-future-plan.md`.
- Do not implement real sensor fusion in `Motion`, or a real magnetometer
  in `Compass` — keep both `NotSupported` stubs until SDL3 itself gains
  magnetometer access, or until a native (non-SDL) backend is separately
  scoped per `plan_devices_phase5.md`'s "Future native backend plan".
- Do not edit anything under `third_party/SDL` — vendored, has its own
  `CLAUDE.md` forbidding AI-authored contributions; read-only for research.
- Do not assume iOS cross-compilation is still blocked without checking
  first each time — but Android's NDK situation (present as of Task P4-11,
  after being absent repeatedly across this project's history) is a poor
  prior for iOS: Apple's toolchain fundamentally needs macOS/Xcode.
- Do not re-attempt to configure `cmake-build-android/` from scratch to
  re-verify past tasks unless something in `Microsoft::Devices` actually
  changed Android-relevant code since the last check.
- Do not run `cmake --build` without first checking `CMakeCache.txt` points
  at the correct source directory (this repo has hit stale-cache issues
  before).
- Do not fix bugs discovered in `sharp-runtime` by editing files there
  without also verifying `sharp-runtime`'s own build/tests independently —
  it's a separate repo with its own `CLAUDE.md` requiring zero warnings and
  all tests passing before any commit there, and its own git history.

---

## 10. Resume prompt

```
Read NEXT.md first, especially Section 2's layered status (API vs. SDL
runtime vs. native backend vs. hardware-verified) — do not summarize this
project's Devices work as simply "complete."
plan_devices_phase4.md and plan_devices_phase5.md are both fully closed —
there is no standing Microsoft::Devices plan left to work through. Ask the
user what to work on next, or pick one of Section 8's items, before
inventing new scope.
If given a new task, make one small, verified improvement at a time.
Run the relevant build/test command from Section 7 / docs/devices-build.md
after each change.
Update NEXT.md after finishing, and keep Section 2 honest rather than
declaring victory.
```
