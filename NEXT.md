# NEXT.md — CNA Project Handoff

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model, built on
SDL3 and a pluggable graphics backend (`EASYGL` / `VULKAN` / `BGFX`). It
preserves XNA-style public APIs (`Microsoft::Xna::Framework`,
`Microsoft::Devices`) while using modern C++ internally. It targets desktop
Linux/Windows/macOS, Android, and iOS. Branch: `feature/devices`.

**`Microsoft::Devices` has now been through five hardening passes** —
`plan_devices_phase4.md` (2026-07-03/04), `plan_devices_phase5.md`
(2026-07-04), `plan_devices_phase6.md` (2026-07-04), `plan_devices_phase7.md`
(2026-07-04), and `plan_devices_phase8.md` (2026-07-04). Each pass's own
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
after being run in a loop tens of times, not once. Phase 7's audit found
Phase 6's own fixes still had three real gaps: the P6-1 addendum's
per-class mutex didn't serialize `Accelerometer`'s and `Gyroscope`'s real SDL
calls *against each other* (Task P7-1); a losing concurrent `Dispose()` call
could flip `disposed_` true while the winner's own cleanup was still relying
on it being false, causing the winner's own `Stop()` call to throw mid-cleanup
and leak resources (Task P7-2); and, most seriously, a callback disposing a
*different*, not-yet-dispatched instance in the same SDL event-watch batch
could leave the dispatch loop holding a genuinely dangling pointer — confirmed
as a real, reliably reproducible (5/5) segfault via a deliberate temporary
revert (Task P7-3). **Phase 8's audit found one more real use-after-free
Phase 7's own fix hadn't covered**: a callback destroying (not just
`Dispose()`-ing) *its own* sensor object mid-dispatch could still leave the
dispatch-cleanup guard touching freed memory — confirmed via a throwaway
ASan build that detected a definitive `heap-use-after-free` on the reverted
code and reported zero issues with the fix in place (Task P8-1). Phase 8 also
found and fixed a real (if test-fixture-only) data race using ThreadSanitizer
for the first time in this project's history (Task P8-4), closed the last
unguarded `SensorBase<T>` field (`TimeBetweenUpdates`, Task P8-2), and made
three previously comment-only SDL-locking preconditions compiler-enforced
(Task P8-3). **Read this as the honest status, not as another "now it's
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
- `plan_devices_phase6.md` (10 tasks, re-audit + hardening) — **closed**,
  all 10 tasks done, including Task P6-10's final re-verification against
  `EASYGL`/`VULKAN`/`BGFX`/Android. Found and fixed: an unlocked
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
- `plan_devices_phase7.md` (7 tasks, re-audit + hardening) — **closed**, all
  7 tasks done. Found and fixed: `Accelerometer`'s and `Gyroscope`'s real SDL
  sensor-subsystem calls were serialized only against *each class's own*
  other calls, not against each other, despite sharing SDL's one global
  `SDL_INIT_SENSOR` subsystem (Task P7-1, a new process-wide
  `Detail::GetGlobalSdlSensorMutex()`); a losing concurrent `Dispose()` call
  could flip `disposed_` true while the winner's own cleanup was still
  relying on it being false, making the winner's own internal `Stop()` call
  throw mid-cleanup and leak `instanceCount_`/the open sensor/the subsystem
  hold (Task P7-2, `SensorBase::WaitForDisposalToComplete()`); a real,
  reliably reproducible use-after-free when one instance's dispatch callback
  disposed a *different*, not-yet-dispatched instance from the same SDL
  event-watch batch (Task P7-3, confirmed via a deliberate temporary revert
  that segfaulted 5/5 times — the most serious bug found this phase); one
  remaining unguarded test-only getter (Task P7-4); `ScopeExit`'s missing
  `<utility>` include and non-`noexcept` destructor, confirmed via a
  temporary revert to actually call `std::terminate()` (Task P7-5).
- `plan_devices_phase8.md` (8 tasks, final hardening/lifetime audit) —
  **closed**, all 8 tasks done. Found and fixed: a callback destroying (not
  just `Dispose()`-ing) its own sensor object mid-dispatch could still leave
  the dispatch-cleanup guard touching freed memory — fixed with a
  `shared_ptr` dispatch token, confirmed via a throwaway ASan build (Task
  P8-1); documented (not fixed — a materially larger, separate class-design
  issue) that destroying `Accelerometer` specifically from within its own
  `CurrentValueChanged` handler remains unsupported, since
  `DispatchSensorReading()` unconditionally touches `this` again afterward to
  decide whether to also raise the legacy `ReadingChanged` event; the last
  unguarded `SensorBase<T>` field, `TimeBetweenUpdates`, now locked (Task
  P8-2); three SDL-calling helpers now require a compiler-enforced
  lock-proof parameter instead of relying on a doc comment alone (Task P8-3);
  added and verified working `CMakePresets.json` entries for ASan/TSan/UBSan
  builds against the Devices suite — the first TSan run found and fixed a
  real (if test-fixture-only) race in a test's own counter (Task P8-4);
  proved `DispatchToInstances()`'s batch-continues-after-one-throw claim
  directly for the first time (Task P8-5); one small defensive consistency
  fix in `VibrateController`'s destructor, no new gap found in the rest of
  the resource-ownership re-audit (Task P8-6).

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

**Quick-reference table (`plan_devices_phase9.md` Task P9-7, 2026-07-04)** — read the
prose below for full detail and reasoning; this is a fast-scan summary, not a
replacement for it. See `AUDIT.md`'s own copy of this table for the identical content
kept in sync.

| Component | API surface | SDL/native runtime | Sanitizers | Android compile | Physical hardware |
|---|---|---|---|---|---|
| `Accelerometer` | Complete | Real, SDL3-backed | Clean (ASan/TSan/UBSan) | Passes | **Not verified** |
| `Gyroscope` | Complete | Real, SDL3-backed | Clean | Passes | **Not verified** |
| `Compass` | Complete shell | Permanent `NotSupported` stub (no SDL magnetometer API) | Clean | Passes | N/A (stub) |
| `Motion` | Complete shell | Permanent `NotSupported` stub (depends on `Compass`) | Clean | Passes | N/A (stub) |
| `VibrateController` | Complete | Real, SDL3 haptic-backed | Clean | Passes | **Not verified** (motor/gamepad) |
| `SensorBase<T>` | Complete | N/A (base class) | Clean, every field locked | N/A | N/A |
| `System.Device.Location` | **Not implemented** | N/A | N/A | N/A | N/A — future plan only |

**Native backend for `Compass`/`Motion`:** missing (design sketched only, see
`docs/devices-native-backend-design.md`, Task P9-8) — not a temporary gap, a permanent
one until a native (non-SDL) backend is separately scoped and implemented.

**API surface (matches documented WP7 shape):** implemented and stable.
`Accelerometer`, `Compass`, `Gyroscope`, `Motion`, `VibrateController`, and
every reading/event-args/exception type match the archived WP7 SDK docs
(`plan_devices_phase2.md` Task P2-2's independent re-verification). This
layer has not changed in Phase 5 and is the most trustworthy claim in this
document.

**SDL runtime implementation (`Accelerometer`/`Gyroscope`/`VibrateController`):**
real, SDL3-backed, and — as of Phase 7 — the specific bugs actual
line-by-line re-audits and (crucially) repeated stress-testing found are now
fixed:
- **Phase 7's three fixes, each found by *not* trusting Phase 6's own "fixed"
  claims:** (1) `getIsSupportedProperty()`'s per-class `subsystem.mutex_`
  lock (Task P6-1's addendum) only serialized each class's own SDL calls
  against *itself* — `Accelerometer` and `Gyroscope` lock two different
  mutexes, so their real SDL sensor-subsystem calls could still run fully
  concurrently with each other, against SDL's one shared `SDL_INIT_SENSOR`
  subsystem. Fixed with a new process-wide `Detail::GetGlobalSdlSensorMutex()`
  (Task P7-1), nested inside each class's own `subsystem.mutex_` in a fixed,
  proven-acyclic order, verified with a new cross-class concurrent
  construct/destroy/probe stress test (40/40 clean loop iterations).
  (2) A losing concurrent `Dispose()` call (one that failed
  `ClaimDisposalOnce()`) previously fell through immediately to the base
  `SensorBase<T>::Dispose(bool)`, flipping `disposed_` true while the
  winner's own cleanup — which calls the public `Stop()`, guarded by that
  same disposed-state precondition — could still be running, making the
  winner's own `Stop()` call throw `ObjectDisposedException` mid-cleanup and
  leak `instanceCount_`/the open sensor/the subsystem hold. Fixed with
  `SensorBase::WaitForDisposalToComplete()` (Task P7-2): the loser now waits
  for the winner's cleanup to actually finish instead of racing ahead,
  verified with a regression test that reliably reproduced the exact failure
  when temporarily reverted. (3) **The most serious bug found this phase:**
  `SensorEventWatch()` used to mark *every* snapshotted instance's
  `dispatchingThreadIds_` up front, before dispatching to any of them — if
  one instance's callback disposed a *different*, not-yet-reached instance
  from the same batch, that instance's pre-marked entry made its concurrent
  `Dispose()` call look like a same-thread self-dispose (exempt from
  waiting), so it was fully disposed (and possibly freed) with no wait, and
  the dispatch loop would then call into the now-dangling pointer on its next
  iteration. Fixed (Task P7-3) by re-validating each pointer against the
  *live* `startedInstances_` list — by pointer value only, never
  dereferencing until validated — atomically with marking it, immediately
  before dispatching to that specific instance. Confirmed as a genuine bug,
  not a theoretical one: temporarily reverting to the old bulk-marking
  bookkeeping made the new regression test **segfault 5 times out of 5**.
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
- **Phase 8's own audit found Phase 7's dispatch-loop fix (Task P7-3) had
  not covered every use-after-free path.** Task P7-3 fixed a callback
  disposing a *different* instance mid-batch; **Task P8-1** found and fixed
  the harder case — a callback destroying (not just `Dispose()`-ing) *its
  own* sensor object while still inside its own dispatch. The
  `dispatchingThreadIds_` member (a plain per-instance
  `std::vector<std::thread::id>`) is now `dispatchToken_`, a
  `std::shared_ptr<std::vector<std::thread::id>>` created once in each
  constructor: `DispatchToInstances()`'s and `InjectSyntheticSensorUpdate()`'s
  cleanup guards now copy this shared_ptr *before* invoking the user
  callback and operate on that copy afterward, never on the (possibly
  freed) instance itself. Confirmed via a **throwaway ASan build** (not a
  plain unsanitized run, which did not reproduce the bug at all — freed
  small heap allocations often aren't immediately overwritten): a
  definitive, exact-line `heap-use-after-free` with the fix temporarily
  reverted, zero issues with it restored. **One boundary remains
  deliberately unfixed and explicitly documented, not silently left open:**
  destroying `Accelerometer` specifically from within its own
  `CurrentValueChanged` handler is still unsafe, because
  `DispatchSensorReading()` unconditionally calls `getIsDataValidProperty()`
  again afterward (to decide whether to also raise the legacy
  `ReadingChanged` event) — this is a class-design property (`ReadingChanged`
  is itself a member of `this`), not a dispatch-bookkeeping gap the token
  can close, and fixing it would require redesigning where the event
  objects live relative to instance identity. `Gyroscope` has no such
  second event and is fully safe with the token fix; so is `Accelerometer`
  when the destroy happens from its own *last*-fired event (`ReadingChanged`).
- **Task P8-2** locked the one remaining unguarded `SensorBase<T>` field,
  `TimeBetweenUpdates` (`currentValue_`/`isDataValid_`/`isSupported_`/
  `disposed_` were all already fixed across Phases 5-7) — verified with a
  new concurrency test under a **ThreadSanitizer** build (the first time
  this project has used TSan), which itself surfaced and led to fixing a
  real, if test-fixture-only, race in the test's own counter (Task P8-4).
- **Task P8-3** made three SDL-calling helpers
  (`EnsureSubsystemInitialized()`/`OpenDefaultSensorLocked()`/
  `ProbeIsSupported()`) require a compile-time lock-proof parameter instead
  of relying on a doc comment alone to remember the global SDL sensor mutex
  — verified the guard actually rejects a lock-free call (compile error) via
  a throwaway scratch file.

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
(`cmake-build-debug`) as of `plan_devices_phase8.md`'s latest commit on
`feature/devices` (2026-07-04). All Phase 8 commits are local to this
session's git checkout (a real clone with submodules initialized) — see the
ZIP-export caveat below and `docs/devices-build.md` for what "builds
cleanly" does and does not mean for a bare source export. As of Phase 8,
also verified clean under **AddressSanitizer, ThreadSanitizer, and
UndefinedBehaviorSanitizer** (`CMakePresets.json`'s `devices-asan`/
`devices-tsan`/`devices-ubsan` presets, Task P8-4) — actually configured,
built, and run, not just written. TSan's only remaining finding after Task
P8-4's own fix is one pre-existing, out-of-scope `sharp-runtime` race (see
Task P8-2's Resolution).

**`CNA` also builds clean for Android** (arm64-v8a, NDK r30, API 24,
`cmake-build-android/`) — re-verified against Phase 6's complete changeset
(Task P6-10, 2026-07-04) using the NDK's own `llvm-nm` to confirm Phase
6's actual new symbols (`SensorBase::ClaimDisposalOnce()`,
`Detail::ScopeExit<...>`, `GetSubsystemHeldForTesting()`) compiled in, not
just that *something* compiled. Still **compile-only** — no APK
packaging, no emulator/device run, `CnaTests` itself not cross-compiled
(`googletest` not configured for the NDK toolchain in this session, same
as every prior phase). Phase 8's Android re-verification is
`plan_devices_phase8.md` Task P8-8 — see that task's Resolution for the
exact re-run against Phase 8's actual new symbols (`dispatchToken_`, the
lock-proof-parameter overloads of `EnsureSubsystemInitialized()`/
`OpenDefaultSensorLocked()`/`ProbeIsSupported()`).

**iOS cross-compilation confirmed still blocked** — no toolchain of any kind
in this Linux container. Re-confirmed during Phase 5's own audit, Phase 6's
Task P6-10, Phase 7's Task P7-7, and again during Phase 8's Task P8-8
(2026-07-04) — checked fresh each time, not assumed carried over.

**`VULKAN`/`BGFX` re-verified clean against Phase 8's complete changeset**
(Task P8-8, 2026-07-04) — see that task's Resolution for exact commands and
counts, including re-running the Task P8-1 self-destroy regression tests on
both backends, not just assumed fixed everywhere because they were fixed on
`EASYGL`.

**Tests:** last full `ctest` run (`EASYGL`) as of `plan_devices_phase8.md`
Task P8-8 (2026-07-04, final) — see that task's Resolution for the exact
count; unrelated `EasyGL`/`easy-gl` graphics-backend failures
(`EasyGL_MRT_TwoAttachments`, `easy-gl-resource-smoke-tests`) remain the
only pre-existing failures, same 2 tests every phase since Phase 5.
Devices-only filter: **226 tests via `--gtest_filter`, 224 passing** (plus 2
tests that correctly `GTEST_SKIP()` themselves on hardware-dependent paths
this machine doesn't have) — see `docs/devices-build.md` for the exact
command, including a Section 2 note on why concurrency tests specifically
must be re-run in a loop (and, as of Phase 8, under a sanitizer — Section 6)
rather than trusted from a single pass; Task P6-1's addendum, Phase 7's
Tasks P7-1/P7-3, and Phase 8's Tasks P8-1/P8-4 have each found a real bug
this way.

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

**2026-07-04 — `plan_devices_phase6.md`, all 10 tasks done (re-audit +
hardening, not new features):**
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
- **P6-9** — `NEXT.md`/`AUDIT.md`/`docs/devices-build.md`/
  `docs/devices-hardware-checklist.md` updated with current test counts and
  Phase 6's findings.
- **P6-10** — final re-verification against the complete changeset:
  `EASYGL`/`VULKAN`/`BGFX` all built and tested clean (Devices-only filter
  211/211 on each); the concurrency stress loop that found Task P6-1's
  heap-corruption bug was specifically re-run on `VULKAN`/`BGFX` too
  (30/30 clean on each), not just assumed fixed everywhere because it was
  fixed on `EASYGL`; Android cross-compile re-confirmed with the NDK's own
  `llvm-nm` showing Phase 6's actual new symbols present; iOS re-confirmed
  still blocked.

Every task above re-ran the Devices-relevant test filter (and, for the
concurrency-sensitive ones, looped it dozens of times — see P6-1's addendum
above for why a single pass isn't enough) plus the full `ctest` suite —
consistently the same 2 pre-existing, unrelated `EasyGL`/`easy-gl` failures
throughout, no regressions at any point once the P6-1 addendum fix landed.
See `plan_devices_phase6.md`'s per-task Resolution notes for full detail.

**2026-07-04 — `plan_devices_phase7.md`, all 7 tasks done (re-audit +
hardening, not new features):**
- **Audit (before any code change):** re-read `SdlSensorSubsystem.hpp`,
  `SensorBase.hpp`, and all four sensor classes' `Dispose(bool)` directly
  against the vendored SDL3 headers' own `\threadsafety` doc comments,
  on the same "don't trust the previous phase's claims" premise Phase 6
  itself established. Confirmed all six audit points the brief raised were
  real (see `plan_devices_phase7.md`'s "Audit findings" section for the
  full per-point detail), one of them (the cross-instance dispose-during-
  dispatch bug) more serious than a data race — a genuine use-after-free.
- **P7-1** — added `Detail::GetGlobalSdlSensorMutex()`, a process-wide mutex
  nested inside each class's own `subsystem.mutex_` (fixed, proven-acyclic
  lock order), serializing every real SDL sensor-subsystem call across
  *both* `Accelerometer` and `Gyroscope`, not just within one class. The
  P6-1-era per-class-only locking left this open: `getIsSupportedProperty()`
  locked a different mutex per class, so the two classes' real SDL calls
  could still run fully concurrently with each other. New cross-class
  concurrent construct/destroy/probe stress test, 40/40 clean.
- **P7-2** — added `SensorBase::WaitForDisposalToComplete()`: a losing
  concurrent `Dispose()` call now waits for the winner's cleanup to actually
  finish (checking `disposed_` via a condition variable) instead of falling
  through immediately to the base `Dispose(bool)`, which previously flipped
  `disposed_` true while the winner's own `Stop()` call — guarded by that
  same precondition — could still be running, causing it to throw
  mid-cleanup and leak `instanceCount_`/the open sensor/the subsystem hold.
  Verified with a regression test using a new NOXNA delayed-cleanup test
  hook; confirmed via a temporary revert that the old behavior really did
  throw `ObjectDisposedException` from the *winner's* own `Stop()` call and
  leak state exactly as the audit predicted.
- **P7-3** — **the most serious bug found this phase.** Refactored
  `SensorEventWatch()`'s dispatch bookkeeping into a shared
  `DispatchToInstances()` method that re-validates each snapshotted pointer
  against the *live* `startedInstances_` list (by pointer value only, never
  dereferencing until validated), marking it as actively dispatching
  atomically with that check, immediately before dispatching to that
  specific instance — not in bulk, up front, for the whole batch, as the old
  code did. The old bookkeeping let one instance's callback dispose a
  *different*, not-yet-reached instance from the same batch without the
  dispatch loop ever noticing, leaving it holding a dangling pointer.
  Confirmed as a real, not theoretical, bug: a new regression test
  (disposing — by `unique_ptr::reset()`, not just `Dispose()`, so the memory
  is genuinely freed — a different in-batch instance from within another's
  callback) segfaulted **5 times out of 5** when the fix was temporarily
  reverted, and passes cleanly (including a 40-iteration stress loop) with
  the fix in place.
- **P7-4** — found and fixed the one remaining unguarded NOXNA test-only
  hook: `GetSubsystemHeldForTesting()` read `subsystemHeld_` with no lock,
  while every write to it happens under `subsystem.mutex_`. Audited every
  other test-only hook (including the ones added earlier in this same
  phase) and found the rest already correctly guarded.
- **P7-5** — `ScopeExit` now includes `<utility>` directly (previously relied
  on a transitive include) and its destructor is `noexcept`, swallowing any
  exception the cleanup callable throws — confirmed via a temporary revert
  that the old behavior really did call `std::terminate()` and abort the
  process when a cleanup callable threw during unwinding.
- **P7-6** — this documentation pass: `NEXT.md`/`AUDIT.md`/
  `docs/devices-build.md` updated with Phase 7's findings and current test
  counts, plus an explicit caveat distinguishing what was actually compiled/
  tested locally in this session from Android-cross-compile-only,
  never-verified-on-real-hardware, and a bare ZIP export's lack of
  self-containment without initialized git submodules.
- **P7-7** — final re-verification across `EASYGL`/`VULKAN`/`BGFX`/Android;
  see Section 2 for the resulting counts and `plan_devices_phase7.md`'s own
  Resolution for exact commands.

Every task above re-ran the Devices-relevant test filter (and, for the
concurrency-sensitive ones — P7-1 and P7-3 especially — looped it dozens of
times, each confirmed via a deliberate temporary revert to actually fail
without the fix, not just assumed correct from a single green run) plus the
full `ctest` suite — consistently the same 2 pre-existing, unrelated
`EasyGL`/`easy-gl` failures throughout, no regressions at any point. See
`plan_devices_phase7.md`'s per-task Resolution notes for full detail, exact
commands run, and the reasoning behind every non-obvious choice.

**2026-07-04 — `plan_devices_phase8.md`, all 8 tasks done (final hardening/
lifetime audit, not new features):**
- **Audit (before any code change):** re-read `SdlSensorSubsystem.hpp`,
  `SensorBase.hpp`, and both real sensor classes' dispatch/`Dispose()` paths
  directly against the actual current code, on the same "don't trust the
  previous phase's claims" premise every prior phase established. Confirmed
  all six audit points the brief raised were real — see
  `plan_devices_phase8.md`'s "Audit findings" section for the full detail —
  including a use-after-free Task P7-3's own fix hadn't covered.
- **P8-1** — **the most significant task this phase.** A callback destroying
  (not just `Dispose()`-ing) its own sensor object mid-dispatch could still
  leave `DispatchToInstances()`'s/`InjectSyntheticSensorUpdate()`'s cleanup
  guards touching freed memory. Replaced the plain per-instance
  `dispatchingThreadIds_` vector with `dispatchToken_`, a
  `std::shared_ptr<std::vector<std::thread::id>>` copied into the cleanup
  guard *before* invoking the callback. Confirmed via a throwaway ASan build
  (a plain run did not reproduce the bug at all): a definitive
  `heap-use-after-free` with the fix reverted, zero issues restored.
  Explicitly documented (not fixed) one remaining boundary: destroying
  `Accelerometer` from within its own `CurrentValueChanged` handler stays
  unsafe, since `DispatchSensorReading()` unconditionally touches `this`
  again afterward for the legacy `ReadingChanged` event — a class-design
  property, not a dispatch-bookkeeping gap.
- **P8-2** — locked `SensorBase<T>`'s last unguarded field,
  `TimeBetweenUpdates` (the getter now returns by value, matching the same
  precedent Task P5-2 set for `CurrentValue`). Verified under a throwaway
  ThreadSanitizer build — the first time this project has used TSan — whose
  only finding was a single pre-existing, out-of-scope `sharp-runtime` race
  in `TimeSpan`'s copy constructor, not this project's own code.
- **P8-3** — `EnsureSubsystemInitialized()`/`OpenDefaultSensorLocked()`/
  `ProbeIsSupported()` now require a `const std::lock_guard<std::mutex>&`
  parameter referencing the global SDL sensor mutex as a compile-time
  "you must already hold this lock" proof, instead of relying on a doc
  comment alone — every existing call site already had the lock in scope
  (Task P7-1), so this is a pure signature hardening with zero behavior
  change. Verified the guard actually rejects a lock-free call via a
  throwaway scratch compile.
- **P8-4** — added and actually verified (not just written)
  `CMakePresets.json` entries for ASan/TSan/UBSan builds against the
  Devices-only suite. The first TSan run surfaced a *second*, real race
  beyond the expected `sharp-runtime` one: `SensorBaseTests.cpp`'s own test
  fixture incremented a plain `int` counter from an event handler that fires
  outside `SensorBase::mutex_` by design — Task P8-2's own new concurrency
  test was the first to actually fire that event from multiple threads.
  Fixed with `std::atomic<int>`.
- **P8-5** — proved `DispatchToInstances()`'s own doc-comment claim (a
  throwing handler doesn't prevent the next instance in the same batch from
  being dispatched to) directly for the first time, for both sensor classes.
  Confirmed via a temporary revert (removing the try/catch entirely) that
  the new tests fail exactly as predicted without the swallow-and-continue
  behavior.
- **P8-6** — final resource-ownership re-audit; re-confirmed every item
  already correct from Phases 5-7, with one small defensive consistency fix
  (`VibrateController`'s destructor now also resets `g_leftRightEffectId`,
  not a fix for a reachable bug).
- **P8-7** — this documentation pass: `NEXT.md`/`AUDIT.md`/
  `docs/devices-build.md`/`docs/devices-hardware-checklist.md` updated with
  Phase 8's findings and current test counts.
- **P8-8** — final re-verification across `EASYGL`/`VULKAN`/`BGFX`/Android;
  see Section 2 for the resulting counts and `plan_devices_phase8.md`'s own
  Resolution for exact commands.

Every task above re-ran the Devices-relevant test filter plus the full
`ctest` suite — consistently the same 2 pre-existing, unrelated
`EasyGL`/`easy-gl` failures throughout, no regressions at any point — and,
for Tasks P8-1/P8-2/P8-3/P8-5, backed a claim with a deliberate temporary
revert (confirming the regression test/compile-guard actually fails/rejects
without the fix) rather than trusting a single green run. See
`plan_devices_phase8.md`'s per-task Resolution notes for full detail, exact
commands run, and the reasoning behind every non-obvious choice — including
why the preferred full lifetime-token refactor was scoped down for
`Accelerometer`'s `CurrentValueChanged` case specifically, and why no death
test was added for that documented boundary (this project has no existing
death-test convention).

All work committed on `feature/devices`, not yet pushed.

---

## 4. Current blocker / main problem

**No blocker.** `plan_devices_phase5.md` through `plan_devices_phase8.md` are
all fully closed — Task P8-8, its final task, re-verified `EASYGL`/`VULKAN`/
`BGFX`/Android all clean against the complete changeset (no regressions
found anywhere), including specifically re-running the Task P8-1 self-destroy
regression tests on `VULKAN`/`BGFX` too, not just the backend they were
originally found on. See Section 8 for what's next; none of it is a
blocker, just unstarted or unverifiable in this environment (physical
hardware, iOS toolchain).

---

## 5. Known bugs and limitations

- **By design, not a bug:** `Compass`/`Motion` are permanent
  `SensorState::NotSupported` stubs — SDL3 has no magnetometer API on any
  platform. See `plan_devices_phase5.md`'s "Future native backend plan" (and
  `plan_devices_phase6.md` Task P6-8's interface sketch) for what a real
  implementation would need.
- **Deliberate, documented limitation (Task P6-3, refined Task P7-2,
  2026-07-04):** calling `Dispose()` on the *same* sensor instance
  concurrently from two different threads is not guaranteed to give the
  second caller a clean `ObjectDisposedException` — the second (losing)
  caller instead blocks in `SensorBase::WaitForDisposalToComplete()` until
  the first (winning) caller's cleanup genuinely finishes, then returns as a
  silent no-op. `ClaimDisposalOnce()`/`WaitForDisposalToComplete()` together
  guarantee the *shared state* (e.g. instance counters, the winner's own
  `Stop()` call) is never corrupted by this, but the two callers' individual
  outcomes still aren't both deterministic (neither is guaranteed a clean
  exception). This matches the conventional .NET `IDisposable` contract
  (`Dispose()` is not generally required to be thread-safe against
  concurrent callers) and is a proportionate scope decision, not an
  oversight — see `SensorBase::Dispose()`'s own doc comment.
- **Deliberate, unfixed by design (confirmed, not newly introduced, Task
  P7-3, 2026-07-04):** a handler that disposes its *own* sender reentrantly
  (same instance, from within its own dispatch callback) still relies on
  the pre-existing Task P5-2/P5-3 self-dispose exemption, which lets
  `Dispose()` proceed without waiting even though the object's memory may be
  freed before its own callback frame finishes unwinding — a pre-existing,
  already-documented Phase 5 design tradeoff, unaffected by Phase 7's fix
  for the *different*-instance case (Task P7-3's actual bug).
- **Deliberate, unfixed by design (Task P8-1, 2026-07-04):** destroying (not
  just `Dispose()`-ing) `Accelerometer` specifically from within its own
  `CurrentValueChanged` handler is unsafe — `DispatchSensorReading()`
  unconditionally calls `getIsDataValidProperty()` again afterward (to
  decide whether to also raise the legacy `ReadingChanged` event), touching
  `this` regardless of whether `ReadingChanged` even has a subscriber. Not
  fixable by Task P8-1's `dispatchToken_` fix (that closes the *outer*
  dispatch-loop bookkeeping gap, not this *inner*, class-design one —
  `ReadingChanged` is itself a member of `this`, so raising it after
  `CurrentValueChanged` inherently requires `this` to still exist). Fixing
  this would mean redesigning where the event objects live relative to
  instance identity — a materially larger change, out of scope here.
  `Gyroscope` has no second event and is fully safe with the token fix;
  `Accelerometer` is also safe when the destroy happens from its own
  *last*-fired event (`ReadingChanged`) instead.
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
- **Resolved (Task P6-10, 2026-07-04):** `VULKAN`/`BGFX` builds and Android
  cross-compile re-verified clean against Phase 6's complete changeset —
  see Section 2.
- **Resolved as of Phase 7 (do not re-list as open):** `Accelerometer`'s and
  `Gyroscope`'s real SDL sensor-subsystem calls were serialized only against
  each class's own other calls, not against each other, despite sharing
  SDL's one global subsystem (Task P7-1); a losing concurrent `Dispose()`
  call could flip `disposed_` true while the winner's own cleanup was still
  relying on it being false, causing the winner's own `Stop()` call to throw
  mid-cleanup and leak state (Task P7-2); a real, reliably reproducible
  (5/5 segfault) use-after-free when one instance's dispatch callback
  disposed a different, not-yet-dispatched instance from the same SDL
  event-watch batch (Task P7-3 — the most serious bug found this phase); one
  remaining unguarded test-only getter (Task P7-4); `ScopeExit`'s missing
  `<utility>` include and non-`noexcept` destructor, confirmed to actually
  call `std::terminate()` when reverted (Task P7-5).
- **Resolved (Task P7-7, 2026-07-04):** `VULKAN`/`BGFX` builds and Android
  cross-compile re-verified clean against Phase 7's complete changeset —
  see Section 2.
- **Resolved as of Phase 8 (do not re-list as open):** a callback destroying
  (not just `Dispose()`-ing) its own sensor object mid-dispatch could leave
  the dispatch-cleanup guard touching freed memory — confirmed via a
  throwaway ASan build and fixed with a `shared_ptr` dispatch token (Task
  P8-1); the last unguarded `SensorBase<T>` field, `TimeBetweenUpdates`, now
  locked (Task P8-2); three SDL-calling helpers now require a
  compiler-enforced lock-proof parameter instead of a doc comment alone
  (Task P8-3); a real (test-fixture-only) data race caught by this
  project's first-ever ThreadSanitizer run (Task P8-4); `VibrateController`
  destructor's incomplete state reset (Task P8-6, a defensive consistency
  fix, not a reachable bug).
- **Resolved (Task P8-8, 2026-07-04):** `VULKAN`/`BGFX` builds and Android
  cross-compile re-verified clean against Phase 8's complete changeset —
  see Section 2.
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
(Task P5-2), `disposed_`/`isSupported_` (Task P6-3), **and, as of Task P8-2,
`timeBetweenUpdates_` too — every field on this class is now consistently
locked.** `getCurrentValueProperty()`/`getTimeBetweenUpdatesProperty()` both
return by value (Tasks P5-2/P8-2), not by reference — matches the real
WP7 API's value-type semantics for both properties, not a divergence.
`setTimeBetweenUpdatesProperty()` locks around the compare-and-write only,
never while raising `TimeBetweenUpdatesChanged`, same discipline every
other event-raising setter on this class already follows.
Also has `ClaimDisposalOnce()` (Task P6-3, protected) — derived
`Dispose(bool)` overrides must call this (not just check
`getIsDisposedProperty()`) to decide whether to run their own cleanup, so
two threads calling `Dispose()` on the same instance concurrently can't
both run cleanup once each. **As of Task P7-2**, also has
`WaitForDisposalToComplete()` (protected): the caller that loses
`ClaimDisposalOnce()` must call this and then simply return — it must
**never** call the base `Dispose(bool)` itself, since doing so (the old
pre-P7-2 behavior) flips `disposed_` true while the winner's own cleanup may
still be relying on it being false (e.g. the winner's own internal `Stop()`
call, guarded by that same precondition). Only the winner calls the base
`Dispose(bool)`, and only after its cleanup has fully finished; the base
implementation now also `notify_all()`s a condition variable so the loser's
wait wakes up. Concrete sensors override `Start()`, `Stop()`, `Dispose(bool)`,
and must call `setIsSupportedProperty()` once from their constructor. **Do
not restructure this class further** — stable, used by production code. **Do
not remove `ClaimDisposalOnce()`/`WaitForDisposalToComplete()` or let a
losing `Dispose(bool)` call the base `Dispose(bool)` directly** — Tasks
P6-3/P7-2 found and closed two related double-cleanup/premature-disposed-
state races there.

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
event-watch registration, and the started-instances/dispatch-tracking
bookkeeping (Tasks P5-2/P5-3, refactored in Task P8-1 — see below).
`state_`/`started_`/`subsystemHeld_`/`dispatchToken_` remain genuine
per-instance members on `Accelerometer`/`Gyroscope` themselves, consistently
guarded by the shared subsystem's mutex wherever touched (Task P6-3 —
`Start()` now holds this lock for its *entire* body, not just part of it).
**As of Task P7-1**, every real SDL sensor-subsystem call
(`SDL_InitSubSystem`/`SDL_QuitSubSystem`/`SDL_GetSensors`/`SDL_OpenSensor`/
`SDL_CloseSensor`/`SDL_GetSensorType`) is additionally serialized against a
process-wide `Detail::GetGlobalSdlSensorMutex()` — nested *inside* the
per-class `subsystem.mutex_` in `Start()`/`Dispose(bool)` (always acquire
the per-class mutex first, the global mutex second, never the reverse), or
acquired alone (no per-class mutex at all) in `getIsSupportedProperty()`,
which touches no per-class subsystem state. **As of Task P8-3**, the three
methods that actually make these calls
(`EnsureSubsystemInitialized()`/`OpenDefaultSensorLocked()`/`ProbeIsSupported()`)
require a `const std::lock_guard<std::mutex>&` parameter referencing this
global mutex as a *compiler-enforced* proof it's held — not just a doc
comment — closing off the exact class of "forgot to lock it" mistake that
caused the Task P6-1 addendum's and Task P7-1's bugs in the first place.
**None of this is optional/removable**: SDL3's own `SDL_InitSubSystem()`/
`SDL_QuitSubSystem()` are documented "should only be called on the main
thread"/"not thread safe", `SDL_GetSensors()`/`SDL_OpenSensor()`/
`SDL_GetSensorType()`/`SDL_CloseSensor()` carry no `\threadsafety` annotation
at all, and calling any of them concurrently — including *across*
`Accelerometer` and `Gyroscope`, not just within one class — reproducibly
corrupts the heap (verified by actually stress-testing it, not just
reasoning about it — see `plan_devices_phase6.md` Task P6-1's addendum and
`plan_devices_phase7.md` Task P7-1). `ProcessSensorUpdateEvent()` validates
the event belongs to this instance's open device, then delegates to
`DispatchSensorReading()` — this split lets the `NOXNA` test-only
`InjectSyntheticSensorUpdate()`/`SetStartedForTesting()`/`SetSupportedForTesting()`
hooks exercise the real dispatch path without a real, opened SDL sensor.
Both the real SDL event-watch path and `InjectSyntheticSensorUpdate()` wrap
their dispatch call in a `Detail::ScopeExit` RAII guard (Task P6-4, hardened
`noexcept` in Task P7-5) so dispatch-tracking cleanup still runs if the
dispatched call throws — the real path additionally catches and swallows
any such exception (it's an `SDL_EventFilter` callback; letting a C++
exception cross that C-API boundary is unsafe on its own, independent of the
cleanup concern; Task P8-5 directly proved this doesn't prevent a *different*
instance later in the same batch from still receiving its own event). **As
of Task P7-3**, the real event-watch path's per-instance dispatch bookkeeping
(`Detail::SdlSensorSubsystem<TSensor>::DispatchToInstances()`) re-validates
each snapshotted instance pointer against the *live* `startedInstances_` list
— by pointer value only, never dereferencing until validated — atomically
with marking it as actively dispatching, immediately before dispatching to
*that* specific instance, not in bulk up front for the whole batch as before.
**This re-validation is not optional/removable**: without it, a callback
disposing a *different*, not-yet-dispatched instance from the same batch
left the dispatch loop holding a dangling pointer — confirmed as a real,
reliably reproducible (5/5) use-after-free/segfault via a deliberate
temporary revert, not a theoretical concern (`plan_devices_phase7.md` Task
P7-3). **As of Task P8-1**, the dispatch-tracking state itself
(`dispatchingThreadIds_`, renamed `dispatchToken_`) is a
`std::shared_ptr<std::vector<std::thread::id>>`, not a plain member vector —
`DispatchToInstances()`'s and `InjectSyntheticSensorUpdate()`'s cleanup
guards copy this shared_ptr *before* invoking the user callback and operate
on that copy afterward, never on the instance itself. **This is also not
optional/removable**: without it, a callback *destroying* (not just
disposing) its own instance mid-dispatch left the cleanup guard touching
freed memory — confirmed via a throwaway ASan build (a plain run did not
reproduce this bug at all) that a fix-reverted build reliably reports a
`heap-use-after-free`, and a fixed build reports none (`plan_devices_phase8.md`
Task P8-1). **One boundary remains deliberately unfixed, not a silent gap**:
destroying `Accelerometer` specifically from within its own
`CurrentValueChanged` handler is still unsafe, because
`DispatchSensorReading()` unconditionally touches `this` again afterward
(to decide whether to also raise the legacy `ReadingChanged` event) — this
is a class-design property (`ReadingChanged` is itself a member of `this`),
not something the dispatch token can fix; see Section 5. `Timestamp` on
dispatched readings is always `System::DateTimeOffset::getUtcNowProperty()`
— real wall-clock time.
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
new concurrency/lifetime test added to this namespace must be re-run in a
loop (20-60+ iterations) and/or under a sanitizer (`CMakePresets.json`'s
`devices-asan`/`devices-tsan`/`devices-ubsan`, Task P8-4) before being
trusted** — a single plain `ctest` pass missed the Task P6-1 addendum's
heap-corruption bug, Task P7-1's cross-class race, Task P7-3's use-after-free,
and Task P8-1's self-destroy use-after-free (a plain run of that last one
didn't even reproduce under 5 manual attempts — only a throwaway ASan build
gave a reliable answer either way).

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

**ZIP-export caveat (Task P7-6):** every command and count below was run
against a real `git clone` of this repository with submodules initialized
(`git submodule update --init --recursive`) — see `docs/devices-build.md`
Section 0. A bare ZIP/tarball export of this source tree, without that step,
has empty `third_party/SDL` (and `SDL_image`/`SDL_mixer`, `vendor/googletest`)
directories and will not configure, let alone build. Nothing in this
document implies a raw source snapshot is self-contained.

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

# Run only Devices/Sensors + VibrateController tests (226 tests as of Task P8-8;
# see docs/devices-build.md Section 2 for the loop command to use for any new
# concurrency test before trusting a single pass):
cd cmake-build-debug && ctest --output-on-failure -R "Accelerometer|SensorFailed|Compass|Gyroscope|Attitude|Motion|VibrateController|SensorSubsystemOwnership|AndroidSensorOrientation|SensorBase|ScopeExit"

# Build the Devices demo screen:
cmake --build cmake-build-debug --target cna_demo_devices -j$(nproc)
./cmake-build-debug/cna_demo_devices   # needs a real display; fails headless (no GPU/display), same as cna_demo_input

# Android cross-compile check (NDK present in this container at ~/Android/Sdk/ndk/):
cmake -S . -B cmake-build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$HOME/Android/Sdk/ndk/30.0.14904198/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DCNA_BUILD_TESTS=OFF
cmake --build cmake-build-android --target CNA -j$(nproc)

# Cross-platform build verification (Vulkan/BGFX; last verified 2026-07-04
# against Phase 8's complete changeset, Task P8-8):
cmake --build cmake-build-vulkan --target CNA --target CnaTests -j$(nproc)
cmake --build cmake-build-bgfx   --target CNA --target CnaTests -j$(nproc)

# Sanitizer builds (ASan/TSan/UBSan; Task P8-4 — see docs/devices-build.md
# Section 6 for full detail, including the actual findings each one produced):
cmake --preset devices-asan && cmake --build --preset devices-asan
cmake --preset devices-tsan && cmake --build --preset devices-tsan
cmake --preset devices-ubsan && cmake --build --preset devices-ubsan
```

No dedicated lint/format tooling is configured for this project as of this
writing.

---

## 8. Next smallest tasks

With `plan_devices_phase4.md` through `plan_devices_phase8.md` **all fully
closed** (Task P8-8, its final task, re-verified `EASYGL`/`VULKAN`/`BGFX`/
Android all clean against the complete changeset — no regressions found
anywhere), there is no standing plan file driving further
`Microsoft::Devices` work and no known outstanding code issue. Pick one of
these, or ask the user what the next priority actually is — do not invent
new `Microsoft::Devices` scope without a plan or explicit request.

1. **Physical hardware verification**, if real Android/iOS hardware or a
   rumble-capable gamepad ever becomes available in a session: work through
   `docs/devices-hardware-checklist.md` using `cna_demo_devices`. Not
   attemptable in this headless container — don't attempt it here, just
   note if the environment changes. This is the single biggest remaining
   gap — everything else in this namespace has been verified by code
   reading, unit tests, cross-compilation, and (as of Phase 8) sanitizers,
   never by real hardware.

2. **Native Android/iOS backend for `Compass`/`Motion`**, if ever scoped as
   its own task — `plan_devices_phase5.md`'s "Future native backend plan"
   section plus `plan_devices_phase6.md` Task P6-8's `ICompassBackend`/
   `IMotionBackend` interface sketch have a starting point (Android
   `SensorManager`/JNI, iOS `CLLocationManager`/`CMDeviceMotion`), not
   verified against any real platform API. A real scoping/design pass
   would be needed first, not a direct implementation from that sketch
   alone.

3. **A sixth independent re-audit of `Microsoft::Devices`**, if a future
   session has reason to doubt this one — Phase 8's entire premise was that
   Phase 7's own fixes still had a real gap (a callback destroying its own
   sensor object mid-dispatch) that didn't survive re-reading the actual
   dispatch-cleanup code, and that a plain, unsanitized test run couldn't
   even reliably reproduce (only a throwaway ASan build gave a trustworthy
   answer); the same discipline should apply to Phase 8's own claims too —
   including its one deliberately-left-open boundary (Accelerometer's
   `CurrentValueChanged` self-destroy case), in case a future session finds
   a way to close it that this one didn't consider.

4. **Anything outside `Microsoft::Devices`.** Ask before assuming scope.

---

## 9. Do not do yet

- Do not claim `Microsoft::Devices` is "complete"/"hardened" as a flat
  statement — use Section 2's layered breakdown instead. Phase 5 exists
  specifically because Phase 4 made that mistake and it hid real bugs;
  Phase 6 exists because Phase 5's own "correctly thread-safe" claim about
  `SensorBase<T>` didn't fully hold either; Phase 7 exists because Phase 6's
  own fixes still had three real gaps (Tasks P7-1/P7-2/P7-3); Phase 8 exists
  because Phase 7's own dispatch-loop fix still had one more real
  use-after-free path (Task P8-1).
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
- Do not remove `SensorBase::WaitForDisposalToComplete()` or let a losing
  `Dispose(bool)` call (one that failed `ClaimDisposalOnce()`) call the base
  `Dispose(bool)` directly instead of waiting (Task P7-2) — confirmed via a
  temporary revert that this makes the *winner's* own `Stop()` call throw
  `ObjectDisposedException` mid-cleanup and leak `instanceCount_`/the open
  sensor/the subsystem hold.
- Do not remove `Detail::GetGlobalSdlSensorMutex()` or its acquisition in
  `Accelerometer`/`Gyroscope`'s `getIsSupportedProperty()`/`Start()`/
  `Dispose(bool)` (Task P7-1) — the per-class `subsystem.mutex_` alone does
  not serialize `Accelerometer`'s and `Gyroscope`'s real SDL sensor-subsystem
  calls against *each other*, only against each class's own other calls.
- Do not revert `Detail::SdlSensorSubsystem<TSensor>::DispatchToInstances()`
  back to marking every snapshotted instance's `dispatchingThreadIds_` up
  front, before dispatching to any of them (Task P7-3) — confirmed via a
  temporary revert that this causes a real, reliably reproducible (5/5)
  segfault when one instance's callback disposes a different, not-yet-
  dispatched instance from the same batch. Each instance must be
  re-validated against the live `startedInstances_` list (by pointer value
  only) and marked atomically with that check, immediately before
  dispatching to it.
- Do not remove `dispatchToken_` (the `shared_ptr<vector<thread::id>>` on
  `Accelerometer`/`Gyroscope`, Task P8-1) or make `DispatchToInstances()`'s/
  `InjectSyntheticSensorUpdate()`'s cleanup guards capture the raw instance/
  `this` again instead of a copy of this token — confirmed via a throwaway
  ASan build that this causes a real, deterministic `heap-use-after-free`
  when a callback destroys (not just disposes) its own instance mid-dispatch.
- Do not assume destroying `Accelerometer` from within its own
  `CurrentValueChanged` handler is safe just because the Task P8-1 token fix
  landed — it explicitly isn't (see Section 5) — and do not attempt to "fix"
  this by moving `ReadingChanged` off the instance or similar without
  recognizing that's a materially larger redesign than this phase scoped.
- Do not remove the lock-proof `const std::lock_guard<std::mutex>&`
  parameter from `EnsureSubsystemInitialized()`/`OpenDefaultSensorLocked()`/
  `ProbeIsSupported()` (Task P8-3) in favor of a doc comment alone — verified
  to actually reject a lock-free call at compile time; a comment doesn't.
- Do not let `SensorBase::setTimeBetweenUpdatesProperty()`/
  `getTimeBetweenUpdatesProperty()` go back to being unguarded, or let the
  getter go back to returning `const TimeSpan&` (Task P8-2) — every field on
  `SensorBase<T>` is now consistently locked; this was the last one that
  wasn't.
- Do not trust a single passing `ctest`/`--gtest_filter` run as proof a new
  concurrency/lifetime test (or a fix to existing concurrent code) is
  correct — Task P6-1's own addendum, Phase 7's own Tasks P7-1/P7-3, and
  Phase 8's own Task P8-1 each found a real bug (heap corruption, a
  cross-class race, a same-instance use-after-free, and a different-instance
  use-after-free respectively) that only showed up after looping the same
  test binary invocation dozens of times, after a deliberate temporary
  revert, or — for Task P8-1 specifically — only under an actual sanitizer
  (a plain unsanitized run did not reproduce that bug at all). See
  `docs/devices-build.md` Section 2 for the loop command and Section 6 for
  the sanitizer presets.
- Do not refactor or restructure `SensorBase<T>`, `Detail::SdlSensorSubsystem`,
  or `ISensorReading` further without a concrete need — stable, used by
  production code, and Tasks P5-4/P6-1/P6-3/P6-4/P7-1/P7-2/P7-3/P8-1/P8-2/P8-3
  already did the hardening that was actually needed.
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
plan_devices_phase4.md through plan_devices_phase8.md are all fully closed —
there is no standing Microsoft::Devices plan left to work through. Ask the
user what to work on next, or pick one of Section 8's items, before
inventing new scope.
If given a new task, make one small, verified improvement at a time.
Run the relevant build/test command from Section 7 / docs/devices-build.md
after each change — and if the change touches concurrency or object
lifetime, re-run the relevant test in a loop (20-60+ iterations, see
docs/devices-build.md Section 2) AND under a sanitizer (devices-asan/
devices-tsan/devices-ubsan presets, Section 6), not just once, not just
plain: Task P6-1's own addendum, Phase 7's own Tasks P7-1/P7-3, and Phase
8's own Task P8-1 each found a real bug that a single passing plain run
completely missed — Task P8-1's specifically didn't reproduce at all
without a sanitizer, even across repeated manual attempts.
Update NEXT.md after finishing, and keep Section 2 honest rather than
declaring victory.
```
