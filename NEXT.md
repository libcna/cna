# NEXT.md — CNA Project Handoff

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model, built on
SDL3 and a pluggable graphics backend (`EASYGL` / `VULKAN` / `BGFX`). It
preserves XNA-style public APIs (`Microsoft::Xna::Framework`,
`Microsoft::Devices`) while using modern C++ internally. It targets desktop
Linux/Windows/macOS, Android, and iOS. Branch: `feature/devices`.

**`Microsoft::Devices` has now been through three hardening passes** —
`plan_devices_phase4.md` (2026-07-03/04), `plan_devices_phase5.md`
(2026-07-04), and `plan_devices_phase6.md` (2026-07-04). Each pass's own
explicit premise was to **not trust the previous pass's "complete"/"hardened"
claims** and re-audit from the actual code. Phase 5's audit found Phase 4 had,
as a side effect of fixing one real bug (Task P4-8), introduced a *different*
real bug in the same commit (Task P5-1), left at least one confirmed data race
unfixed in the shared `SensorBase<T>` base class (Task P5-2), and had skipped
an RAII cleanup based on an assumption that was never actually checked and
turned out to be false (Task P5-11). Phase 6's audit found Phase 5's own
"correctly thread-safe" claim about `SensorBase<T>` didn't fully hold either
(`disposed_`/`isSupported_` still inconsistently locked, Task P6-3), and —
most notably — **a single passing test run is not proof a concurrency fix is
correct**: Phase 6's own new instance-counting test (Task P6-1) only revealed
a real, reproducible heap-corruption bug (concurrent, unsynchronized calls
into SDL's sensor API, violating SDL3's own documented thread-safety contract)
after being run in a loop tens of times, not once. **Read this as the honest
status, not as another "now it's really done" claim** — see Section 2's
layered breakdown instead of a single verdict.

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
- `plan_devices_phase6.md` (10 tasks, re-audit + hardening) — all 10 tasks
  done except Task P6-10's own final reproducible-verification writeup
  (in progress as of this writing). Found and fixed: an unlocked
  `instanceCount_` race in all four sensor classes' constructors (Task
  P6-1) — whose own new concurrency test then surfaced a *second*, more
  serious bug only visible under repeated stress-testing, not a single run:
  `getIsSupportedProperty()` made real SDL calls with no synchronization,
  violating SDL3's own documented thread-safety contract and reproducibly
  corrupting the heap under concurrent construction; a subsystem-hold leak
  on failed `Start()` (Task P6-2); inconsistent locking of
  `started_`/`state_`/`subsystemHeld_`/`disposed_`/`isSupported_` plus a
  double-dispose race (Task P6-3); an exception-safety gap that could
  permanently corrupt dispatch-tracking state and deadlock a future
  `Dispose()` (Task P6-4); zero test coverage for
  `SensorBase<T>`'s `TimeBetweenUpdates` (Task P6-5); confirmed
  `VibrateController`'s SDL lifetime pattern matches `GraphicsDevice`'s own
  established convention (Task P6-6); added semantic Android axis tests
  (Task P6-7); reconfirmed `Compass`/`Motion` honesty and sketched future
  native backend interfaces (Task P6-8).

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
real, SDL3-backed, and — as of Phase 6 — the specific bugs actual
line-by-line re-audits and (crucially) repeated stress-testing found are now
fixed:
- Subsystem probe/instance-ownership init-quit balancing is correct for both
  `SDL_INIT_SENSOR` (Task P5-1, fixing a leak Task P4-8 itself introduced;
  Task P6-2 additionally fixed a subsystem-hold leak on a *failed* `Start()`)
  and `SDL_INIT_HAPTIC` (Task P5-11, with an RAII destructor now closing
  `VibrateController`'s haptic device — previously left open on a wrong
  assumption; Task P6-6 confirmed this pattern matches
  `Graphics::GraphicsDevice`'s own established convention).
- `SensorBase<T>`'s `currentValue_`/`isDataValid_` are mutex-protected
  (Task P5-2) — previously a real, unguarded data race between the SDL
  callback thread and the game thread, undetected through 4 prior plans.
  **Phase 5's own "now correctly thread-safe" claim did not fully hold**:
  Task P6-3 found `disposed_`/`isSupported_` were still written unlocked
  despite being read under lock/from another thread elsewhere, and added
  `SensorBase::ClaimDisposalOnce()` after finding two threads calling
  `Dispose()` on the same instance concurrently could both run derived
  cleanup logic once each (e.g. double-decrementing a shared instance
  counter).
- `Accelerometer`/`Gyroscope`'s callback quiescence tracking is a
  per-thread-id vector, not a bool or plain counter (Tasks P5-2/P5-3) — the
  single-bool version could under-count concurrent dispatches, and a
  handler disposing its own sender from inside its own callback used to
  deadlock (previously an accepted, documented limitation — now fixed, not
  just documented). **Task P6-4** closed a related exception-safety gap: a
  throwing `CurrentValueChanged`/`ReadingChanged` handler used to skip this
  cleanup entirely, permanently corrupting the tracking state and
  deadlocking any future `Dispose()` call — fixed with an RAII
  `Detail::ScopeExit` guard.
- Instance-count checking/incrementing is now properly locked in all four
  sensor classes' constructors (Task P6-1) — previously unlocked, racing
  against `Dispose()`'s locked decrement. **This fix's own new concurrency
  test then surfaced a second, more serious, previously-undiscovered bug**
  — but only after being run in a loop dozens of times, not on the first
  (or fifth, or twentieth) pass: `getIsSupportedProperty()` made real SDL
  calls (`SDL_InitSubSystem`/`SDL_GetSensors`/`SDL_OpenSensor`/
  `SDL_CloseSensor`/`SDL_QuitSubSystem`) with **no synchronization of its
  own**, directly violating SDL3's own documented contract
  (`SDL_InitSubSystem()`: *"should only be called on the main thread"*;
  `SDL_QuitSubSystem()`: *"is not thread safe"*) — concurrently constructing
  multiple `Accelerometer`/`Gyroscope` instances reproducibly corrupted the
  heap (glibc's `malloc(): unaligned tcache chunk detected` abort, roughly
  1 in 4 runs of the same test). Fixed by locking `subsystem.mutex_` for
  `getIsSupportedProperty()`'s entire call, serializing it against every
  other SDL sensor call this class makes. **Concrete lesson for future
  work in this namespace: re-run new concurrency tests in a loop (20-60+
  iterations) before trusting a single green `ctest` pass** — see
  `docs/devices-build.md`'s Section 2 for the exact loop command.
- The two classes' subsystem/event-watch machinery is a shared,
  de-duplicated internal template (`Detail::SdlSensorSubsystem<TSensor>`,
  Task P5-4) instead of two hand-maintained near-copies — verified
  byte-for-byte behavior-preserving against the full existing test suite.

Still **not independently verified against real hardware** — see the next
paragraph.

**Native mobile backend (Compass/Motion magnetometer, real device sensor
fusion):** **missing, by design, not a gap to silently close.** SDL3 exposes
no magnetometer/compass API on any platform. `Compass`/`Motion` are honest
`SensorState::NotSupported` stubs — confirmed still true and still tested
(Task P5-9, re-confirmed Task P6-8). `plan_devices_phase5.md`'s "Future
native backend plan" section sketches concrete Android (`SensorManager`/JNI,
`TYPE_MAGNETIC_FIELD`, `TYPE_ROTATION_VECTOR`) and iOS (`CLLocationManager`
heading APIs, `CMDeviceMotion`) paths — not implemented, planning only.
`plan_devices_phase6.md`'s Task P6-8 additionally sketched a concrete
`ICompassBackend`/`IMotionBackend` C++ interface shape (documentation only,
inside the plan file itself — deliberately not added as compiled `.hpp`/
`.cpp` files with zero callers) a future implementation task could work
against. GPS/location is explicitly **not** part of this — see
`docs/location-future-plan.md` (Task P5-10).

**Hardware manually verified:** **none of it, on any physical device, in any
session to date.** Every claim above is verified by code reading, unit
tests, and cross-compilation — never by running on a real accelerometer,
gyroscope, or haptic motor. `docs/devices-hardware-checklist.md` (Task
P4-13, tightened in Task P5-7) exists specifically to close this gap
whenever real hardware becomes available; `examples/demo_devices/`
(`cna_demo_devices`, Task P4-14) is the tool to use when it does.

**Build:** `CNA` and `CnaTests` build cleanly with the `EASYGL` backend
(`cmake-build-debug`) as of `plan_devices_phase6.md`'s latest commit on
`feature/devices` (2026-07-04, not yet pushed).

**`CNA` also built clean for Android** during Phase 5 (arm64-v8a, NDK r30,
API 24, `cmake-build-android/`) — Phase 6 has not yet re-run this
cross-compile against its own changes (tracked as part of Task P6-10, not
yet complete as of this writing); Phase 6's changes are all thread-safety/
locking-discipline fixes to code that already compiled for Android in
Phase 5, so a regression is unlikely but not yet *confirmed* the way Phase
5's own changes were.

**iOS cross-compilation confirmed still blocked** — no toolchain of any kind
in this Linux container. Re-confirmed during Phase 5's own audit (not just
carried over from Phase 4's finding); not independently re-checked in
Phase 6 (no reason to expect it changed).

**`VULKAN`/`BGFX` re-verified clean against Phase 5's full changeset**
(Task P5-14, 2026-07-04) — both build `CNA`/`CnaTests` with zero errors;
Devices-only filter 187/187 passing on each, matching `EASYGL` exactly at
that point in time. **Not yet re-verified against Phase 6's changes** —
tracked as part of Task P6-10.

**Tests:** last full `ctest` run (`EASYGL`) as of `plan_devices_phase6.md`
Task P6-9 (2026-07-04, after Task P6-1's addendum fix): **2036 tests, 2
failures** (99.9% passing). The 2 failures are pre-existing, unrelated
`EasyGL`/`easy-gl` graphics-backend bugs (`EasyGL_MRT_TwoAttachments`,
`easy-gl-resource-smoke-tests`) — this environment unexpectedly gained a
real GPU/display mid-Phase-5 (Task P5-1's own discovery), which surfaced
these for the first time (previously silently `Not Run` headless, which is
why every prior session's baseline said "64 failures" — that was actually
"64 tests never run at all," not 64 failing tests). Devices-only filter:
**211 tests via `ctest -R`, 100% passing** (plus 2 tests that correctly
`GTEST_SKIP()` themselves on hardware-dependent paths this machine doesn't
have) — see `docs/devices-build.md` for the exact command, including a new
Section 2 note on why concurrency tests specifically must be re-run in a
loop, not trusted from a single pass (Task P6-1's own addendum found a real
bug this way).

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

**2026-07-04 — `plan_devices_phase6.md`, Tasks P6-1 through P6-9 done
(re-audit + hardening, not new features; Task P6-10 in progress):**
- **Audit (before any code change):** re-read every sensor/`VibrateController`
  file and test directly, on the same "don't trust the previous phase's
  claims" premise Phase 5 itself established. Found Phase 5's own
  "correctly thread-safe" claim about `SensorBase<T>` didn't fully hold
  (`disposed_`/`isSupported_` still inconsistently locked); confirmed the
  raw-pointer dispatch design was lifetime-safe but not exception-safe;
  confirmed `VibrateController`'s destructor pattern matches an established,
  project-wide convention (`GraphicsDevice` does the identical thing) rather
  than being a unique risk; found a claimed "duplicate comment line" that
  did not actually exist in the current code.
- **P6-1** — locked `instanceCount_`'s check+increment in all four sensor
  classes' constructors (previously unlocked, racing `Dispose()`'s locked
  decrement). Its own new concurrency test then surfaced a *second*, more
  serious bug, but only under repeated stress-testing (looping the test
  binary dozens of times), not a single run: `getIsSupportedProperty()`
  made real SDL calls with no synchronization, violating SDL3's own
  documented thread-safety contract and reproducibly corrupting the heap.
  Fixed by locking `subsystem.mutex_` for the whole call (addendum commit,
  found and fixed during Task P6-9's own verification pass — a concrete
  demonstration of why re-running concurrency tests matters).
- **P6-2** — fixed a subsystem-hold leak when `Start()` fails *after*
  acquiring the SDL sensor subsystem: the hold is now released immediately
  on failure instead of waiting for `Dispose()`.
- **P6-3** — made `started_`/`state_`/`subsystemHeld_`
  (`Accelerometer`/`Gyroscope`) and `disposed_`/`isSupported_`
  (`SensorBase<T>`) consistently locked everywhere they're read or written
  (previously a mix of locked and unlocked access to the same fields).
  Found and fixed an additional race while doing so: two threads calling
  `Dispose()` on the same instance concurrently could both run derived
  cleanup logic once each — added `SensorBase::ClaimDisposalOnce()` to
  prevent it.
- **P6-4** — added an RAII `Detail::ScopeExit` guard so a throwing
  `CurrentValueChanged`/`ReadingChanged` handler can no longer skip
  dispatch-tracking cleanup and permanently deadlock a future `Dispose()`
  call; `SensorEventWatch()`'s per-instance dispatch also now catches and
  deliberately swallows any exception, since it's an `SDL_EventFilter`
  callback and letting a C++ exception cross that C-API boundary is unsafe
  independent of the cleanup bug.
- **P6-5** — added `tests/Microsoft/Devices/Sensors/SensorBaseTests.cpp`,
  the first-ever direct tests of `SensorBase<T>`'s own logic (previously
  only exercised indirectly through a concrete sensor class): confirms
  `TimeBetweenUpdates`'s default value and change-notification behavior
  (both already correct, just untested).
- **P6-6** — investigated whether `VibrateController`'s destructor (Task
  P5-11) could unsafely run after a *host application* independently calls
  `SDL_Quit()` — confirmed this class's SDL lifecycle pattern is identical
  to `Graphics::GraphicsDevice`'s own established convention, not a unique
  risk; documented this directly rather than adding narrow, inconsistent
  mitigation.
- **P6-7** — added semantic Android axis-remap tests (tilt-left, face-up/
  face-down); deliberately did *not* add an absolute "top/bottom edge down"
  claim for the forward/backward axis after an attempt to independently
  re-derive it from rotation geometry alone produced a contradiction on the
  first pass — see that task's Resolution for why guessing here would be
  dishonest.
- **P6-8** — reconfirmed `Compass`/`Motion` remain honest stubs; sketched
  (documentation only) `ICompassBackend`/`IMotionBackend` interface shapes
  for a future native backend, without adding unused compiled code.
- **P6-9** (this update) — `NEXT.md`/`AUDIT.md`/`docs/devices-build.md`
  updated with current test counts and Phase 6's findings.

Every task above re-ran the Devices-relevant test filter (and, for the
concurrency-sensitive ones, looped it dozens of times — see P6-1's addendum
above for why a single pass isn't enough) plus the full `ctest` suite —
consistently the same 2 pre-existing, unrelated `EasyGL`/`easy-gl` failures
throughout, no regressions at any point once the P6-1 addendum fix landed.
See `plan_devices_phase6.md`'s per-task Resolution notes for full detail.

All work committed on `feature/devices`, not yet pushed.

---

## 4. Current blocker / main problem

**No blocker.** `plan_devices_phase5.md` is fully closed. `plan_devices_phase6.md`
has Tasks P6-1 through P6-9 done; Task P6-10 (final reproducible
verification across `VULKAN`/`BGFX`/Android, mirroring Phase 5's own P5-14)
is the one remaining item, in progress as of this writing — not a blocker,
just not yet finished. See Section 8 for what's next after that.

---

## 5. Known bugs and limitations

- **By design, not a bug:** `Compass`/`Motion` are permanent
  `SensorState::NotSupported` stubs — SDL3 has no magnetometer API on any
  platform. See `plan_devices_phase5.md`'s "Future native backend plan" (and
  `plan_devices_phase6.md` Task P6-8's interface sketch) for what a real
  implementation would need.
- **Deliberate, documented limitation (Task P6-3, 2026-07-04):** calling
  `Dispose()` on the *same* sensor instance concurrently from two different
  threads is not guaranteed to give the second caller a clean
  `ObjectDisposedException` — `SensorBase::ClaimDisposalOnce()` guarantees
  the *shared state* (e.g. instance counters) is never corrupted by this,
  but the two callers' individual outcomes aren't both deterministic. This
  matches the conventional .NET `IDisposable` contract (Dispose() is not
  generally required to be thread-safe against concurrent callers) and is a
  proportionate scope decision, not an oversight — see `SensorBase::Dispose()`'s
  own doc comment.
- **Needs verification, likely permanent:** iOS cross-compilation — no
  Apple toolchain possible in this Linux container.
- **Needs physical hardware verification (never done, any session):**
  Android's axis-remap math's actual tilt-direction correctness (Task P6-7
  added semantic tilt-left/face-up/face-down tests, but deliberately did
  *not* assert an absolute forward/backward sign — see that task's
  Resolution for why); `VibrateController::Start()`/`StartLeftRight()`
  actually actuating a real phone motor / two distinct motors; the
  gamepad-exclusion filter not competing with `GamePad::SetVibration()` on
  a real connected gamepad. Use `docs/devices-hardware-checklist.md` +
  `cna_demo_devices` when real hardware is available — nothing in this
  codebase can verify these itself.
- **Resolved as of Phase 5 (do not re-list as open):** the self-dispose
  deadlock (Task P5-3), the `SensorBase<T>` `currentValue_`/`isDataValid_`
  data race (Task P5-2), the subsystem probe leak (Task P5-1), and
  `VibrateController`'s unclosed haptic device (Task P5-11).
- **Resolved as of Phase 6 (do not re-list as open):** unlocked
  `instanceCount_` in all four sensor classes' constructors (Task P6-1); a
  real, reproducible heap-corruption bug from unsynchronized concurrent
  calls into SDL's sensor API via `getIsSupportedProperty()` (Task P6-1's
  own addendum — found only by looping a new concurrency test dozens of
  times, not a single run); a subsystem-hold leak on failed `Start()` (Task
  P6-2); inconsistent locking of `started_`/`state_`/`subsystemHeld_`/
  `disposed_`/`isSupported_` and a double-dispose corruption race (Task
  P6-3); an exception-safety gap in dispatch cleanup that could deadlock a
  future `Dispose()` (Task P6-4).
- **Not yet re-verified against Phase 6's changes (tracked as Task P6-10,
  in progress):** `VULKAN`/`BGFX` builds, Android cross-compile.
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
and a private `mutable std::mutex` guarding `currentValue_`/`isDataValid_`
(Task P5-2) **and, as of Task P6-3, `disposed_`/`isSupported_` too** —
never held across `CurrentValueChanged.Raise()`.
`getCurrentValueProperty()` returns by value (Task P5-2), not by reference.
Also has `ClaimDisposalOnce()` (Task P6-3, protected) — derived
`Dispose(bool)` overrides must call this (not just check
`getIsDisposedProperty()`) to decide whether to run their own cleanup, so
two threads calling `Dispose()` on the same instance concurrently can't
both run cleanup once each. Concrete sensors override `Start()`, `Stop()`,
`Dispose(bool)`, and must call `setIsSupportedProperty()` once from their
constructor. **Do not restructure this class further** — stable, used by
production code. **Do not remove `ClaimDisposalOnce()` or revert
`Dispose(bool)` overrides to a plain `if (!getIsDisposedProperty() && disposing)`
guard** — Task P6-3 found and closed a real double-cleanup race there.

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
subsystem's mutex wherever touched (Task P6-3 — `Start()` now holds this
lock for its *entire* body, not just part of it). `getIsSupportedProperty()`
also locks this same mutex for its whole call (Task P6-1's addendum) —
**this is not optional/removable**: SDL3's own `SDL_InitSubSystem()`/
`SDL_QuitSubSystem()` are documented "should only be called on the main
thread"/"not thread safe", and calling this method concurrently from
multiple threads without the lock reproducibly corrupts the heap (verified
by actually stress-testing it, not just reasoning about it — see
`plan_devices_phase6.md` Task P6-1's addendum). `ProcessSensorUpdateEvent()`
validates the event belongs to this instance's open device, then delegates
to `DispatchSensorReading()` — this split lets the `NOXNA` test-only
`InjectSyntheticSensorUpdate()`/`SetStartedForTesting()`/`SetSupportedForTesting()`
hooks exercise the real dispatch path without a real, opened SDL sensor.
Both `SensorEventWatch()` (the real SDL event-watch callback) and
`InjectSyntheticSensorUpdate()` wrap their dispatch call in a
`Detail::ScopeExit` RAII guard (Task P6-4) so dispatch-tracking cleanup
still runs if the dispatched call throws — `SensorEventWatch()`
additionally catches and swallows any such exception (it's an
`SDL_EventFilter` callback; letting a C++ exception cross that C-API
boundary is unsafe on its own, independent of the cleanup concern).
`Timestamp` on dispatched readings is always
`System::DateTimeOffset::getUtcNowProperty()` — real wall-clock time.
Android's axis-remap sign math is a pure function,
`Detail::ConvertAndroidPortraitToXnaLandscape()`
(`include/Microsoft/Devices/Sensors/Detail/AndroidSensorOrientation.hpp`,
Task P5-7), shared identically by both classes and unit-tested on any
platform, including semantic tilt-left/face-up/face-down cases (Task P6-7)
— **the forward/backward (X) axis deliberately has no asserted absolute
sign**, since neither the code's own doc comment nor
`docs/devices-hardware-checklist.md` establishes one; don't add one without
real hardware to check against (Task P6-7's Resolution explains why a
first attempt to derive one from rotation geometry alone was wrong). **Do
not** "fix" the subsystem pattern by building a separate hand-rolled
reference counter; SDL already provides one, and `SdlSensorSubsystem`'s
`ProbeGuard`/per-instance `subsystemHeld_` already use it correctly. **Any
new concurrency test added to this namespace must be re-run in a loop
(20-60+ iterations) before being trusted** — a single `ctest` pass missed
the Task P6-1 addendum's heap-corruption bug entirely.

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
anywhere. Task P6-6 additionally confirmed this per-instance
`SDL_InitSubSystem()`/`SDL_QuitSubSystem()` pairing (as opposed to the
umbrella `SDL_Init()`/`SDL_Quit()`) is an established, project-wide
convention — `Graphics::GraphicsDevice` does the identical thing for
`SDL_INIT_VIDEO` — so the residual risk of a *host application* calling
`SDL_Quit()` directly is shared identically by `GraphicsDevice`, not unique
to `VibrateController`. Excludes haptic devices that are also connected joysticks/gamepads
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

# Run only Devices/Sensors + VibrateController tests (211 tests as of Task P6-9;
# see docs/devices-build.md Section 2 for the loop command to use for any new
# concurrency test before trusting a single pass):
cd cmake-build-debug && ctest --output-on-failure -R "Accelerometer|SensorFailed|Compass|Gyroscope|Attitude|Motion|VibrateController|SensorSubsystemOwnership|AndroidSensorOrientation|SensorBase"

# Build the Devices demo screen:
cmake --build cmake-build-debug --target cna_demo_devices -j$(nproc)
./cmake-build-debug/cna_demo_devices   # needs a real display; fails headless (no GPU/display), same as cna_demo_input

# Android cross-compile check (NDK present in this container at ~/Android/Sdk/ndk/):
cmake -S . -B cmake-build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$HOME/Android/Sdk/ndk/30.0.14904198/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DCNA_BUILD_TESTS=OFF
cmake --build cmake-build-android --target CNA -j$(nproc)

# Cross-platform build verification (Vulkan/BGFX; last verified 2026-07-04
# against Phase 5's changes (Task P5-14) — NOT yet re-run against Phase 6's
# changes, tracked as Task P6-10, see Section 4/8):
cmake --build cmake-build-vulkan --target CNA --target CnaTests -j$(nproc)
cmake --build cmake-build-bgfx   --target CNA --target CnaTests -j$(nproc)
```

No dedicated lint/format tooling is configured for this project as of this
writing.

---

## 8. Next smallest tasks

With `plan_devices_phase4.md`/`plan_devices_phase5.md` fully closed and
`plan_devices_phase6.md` at Task P6-9 (Task P6-10, final cross-backend
re-verification, still open), the immediate next step is finishing Task
P6-10: re-verify `VULKAN`/`BGFX`/Android against Phase 6's complete
changeset, the same way Task P5-14 did for Phase 5. After that closes,
there is no standing plan file driving further `Microsoft::Devices` work.
Pick one of these, or ask the user what the next priority actually is — do
not invent new `Microsoft::Devices` scope without a plan or explicit
request.

1. **Finish Task P6-10** (if not already done by the time this is read):
   `VULKAN`/`BGFX` build + Devices-only test re-verification, Android
   cross-compile re-check, full `ctest` run, all against Phase 6's complete
   changeset. Append results to `plan_devices_phase6.md`.

2. **Physical hardware verification**, if real Android/iOS hardware or a
   rumble-capable gamepad ever becomes available in a session: work through
   `docs/devices-hardware-checklist.md` using `cna_demo_devices`. Not
   attemptable in this headless container — don't attempt it here, just
   note if the environment changes. This is the single biggest remaining
   gap — everything else in this namespace has been verified by code
   reading, unit tests, or cross-compilation, never by real hardware.

3. **Native Android/iOS backend for `Compass`/`Motion`**, if ever scoped as
   its own task — `plan_devices_phase5.md`'s "Future native backend plan"
   section plus `plan_devices_phase6.md` Task P6-8's `ICompassBackend`/
   `IMotionBackend` interface sketch have a starting point (Android
   `SensorManager`/JNI, iOS `CLLocationManager`/`CMDeviceMotion`), not
   verified against any real platform API. A real scoping/design pass
   would be needed first, not a direct implementation from that sketch
   alone.

4. **A fourth independent re-audit of `Microsoft::Devices`**, if a future
   session has reason to doubt this one — Phase 6's entire premise was that
   Phase 5's "correctly thread-safe" claims didn't fully survive re-reading
   the actual code (and, more importantly, didn't survive actually
   stress-testing new concurrency tests in a loop rather than trusting a
   single pass); the same discipline should apply to Phase 6's own claims
   too.

5. **Anything outside `Microsoft::Devices`.** Ask before assuming scope.

---

## 9. Do not do yet

- Do not claim `Microsoft::Devices` is "complete"/"hardened" as a flat
  statement — use Section 2's layered breakdown instead. Phase 5 exists
  specifically because Phase 4 made that mistake and it hid real bugs;
  Phase 6 exists because Phase 5's own "correctly thread-safe" claim about
  `SensorBase<T>` didn't fully hold either.
- Do not "fix" the SDL sensor subsystem ownership pattern by building a
  separate hand-rolled reference counter — SDL3 already provides one (see
  Section 6).
- Do not re-introduce a single bool/counter for callback quiescence
  tracking — the `std::vector<std::thread::id>` design (Tasks P5-2/P5-3)
  exists because simpler versions have concrete, traced failure modes.
- Do not re-add the self-dispose deadlock as an "accepted limitation" —
  it's fixed (Task P5-3), not merely documented.
- Do not remove the `subsystem.mutex_` lock from `Accelerometer`/
  `Gyroscope`'s `getIsSupportedProperty()` (Task P6-1's addendum) — this
  isn't a style preference, it prevents a real, reproducible heap
  corruption bug (concurrent SDL calls violating SDL3's own documented
  main-thread-only/not-thread-safe contract for `SDL_InitSubSystem()`/
  `SDL_QuitSubSystem()`). Verified by actually stress-testing it in a loop,
  not just by reading the code.
- Do not remove `SensorBase::ClaimDisposalOnce()` or revert a derived
  `Dispose(bool)` override to a plain `if (!getIsDisposedProperty() && disposing)`
  guard (Task P6-3) — closes a real double-cleanup race between two
  threads calling `Dispose()` on the same instance concurrently.
- Do not trust a single passing `ctest`/`--gtest_filter` run as proof a new
  concurrency test (or a fix to existing concurrent code) is correct — Task
  P6-1's own addendum found a real heap-corruption bug that only showed up
  after looping the same test binary invocation dozens of times. See
  `docs/devices-build.md` Section 2 for the loop command.
- Do not refactor or restructure `SensorBase<T>`, `Detail::SdlSensorSubsystem`,
  or `ISensorReading` further without a concrete need — stable, used by
  production code, and Tasks P5-4/P6-1/P6-3/P6-4 already did the hardening
  that was actually needed.
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
plan_devices_phase4.md and plan_devices_phase5.md are fully closed;
plan_devices_phase6.md has Tasks P6-1 through P6-9 done, Task P6-10 (final
cross-backend re-verification) still open — finish that first if picking
this back up, then there is no standing Microsoft::Devices plan left to
work through. Ask the user what to work on next, or pick one of Section 8's
items, before inventing new scope.
If given a new task, make one small, verified improvement at a time.
Run the relevant build/test command from Section 7 / docs/devices-build.md
after each change — and if the change touches concurrency, re-run the
relevant test in a loop (20-60+ iterations, see docs/devices-build.md
Section 2), not just once: Task P6-1's own addendum found a real
heap-corruption bug that a single passing run completely missed.
Update NEXT.md after finishing, and keep Section 2 honest rather than
declaring victory.
```
