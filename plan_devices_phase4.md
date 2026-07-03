# plan_devices_phase4.md — Devices Hardening: Callback Safety, Real Event-Path Testing, Cross-Platform Verification

## Context

`plan_devices.md` (31 tasks), `plan_devices_phase2.md` (17 tasks, 1 environment-blocked),
and `plan_devices_phase3.md` (12 tasks, all done — 3 confirmed real bugs fixed, 1 decision
task resolved, 6 test-coverage tasks filled, 1 documentation-only research task partially
resolved with no known bug) are complete. `Microsoft::Devices` — `Microsoft::Devices::Sensors`
(`Accelerometer`, `Compass`, `Gyroscope`, `Motion`, their reading/event-args/exception
types) plus `Microsoft::Devices::VibrateController` — matches the documented Windows
Phone 7 XNA API shape and has a full, green test suite (1985 tests as of `plan_devices_phase3.md`'s
close).

**This plan is user-authored** (priority list supplied directly, 2026-07-03), not the
output of an agent research pass like phase3 was. Before writing each task below, the
relevant claims were independently verified against this codebase and the vendored SDL3
source (not taken at face value) — see each task's "Verified" note. Two of the user's
original suggestions turned out to have a better concrete answer than what was proposed:

- **SDL3 already ref-counts subsystem init/quit** (`SDL_InitSubSystem`'s own doc:
  *"Subsystem initialization is ref-counted, you must call `SDL_QuitSubSystem()` for each
  `SDL_InitSubSystem()`..."*). The real bug (Task P4-8) is that CNA's
  `EnsureSensorSubsystemInitialized()` *bypasses* that ref-count by guarding the call with
  `SDL_WasInit()` first — the fix is to stop working around SDL's own mechanism, not to
  build a second one from scratch.
- **SDL3 has a direct haptic↔joystick correlation API**, `SDL_OpenHapticFromJoystick(SDL_Joystick*)`
  (confirmed present in `third_party/SDL/include/SDL3/SDL_haptic.h`), which makes Task
  P4-10 a real, concrete fix rather than open-ended research.

Work through phases in order. Every source-touching task needs: build `CNA` + `CnaTests`,
run the affected suite via `--gtest_filter`, then full `ctest --output-on-failure` to
confirm no regression beyond the pre-existing headless `EasyGL_*` baseline (64 failures as
of `plan_devices_phase3.md`'s close — re-check the current count, since it may drift), and
an update to `NEXT.md` (status/recent-changes/known-bugs sections) after each task, per
the "always commit after finishing a task" rule in `CLAUDE.md`.

---

## Phase 1: Documentation cleanup

### Task P4-1 — Reconcile plan/status docs, state the baseline clearly, separate genuinely open items — ✅ Done (2026-07-03)

**Finding (checked before writing this task, not assumed):** `plan_devices_phase2.md` and
`plan_devices_phase3.md` are **not actually stale** in the way "stale notes" usually
implies — every task's original (pre-resolution) description is intentionally left
in place, with a `**Resolution (date):**` paragraph appended after each one once done
(e.g. `plan_devices_phase2.md` Task P2-16's step 2 still says "unverified assumption" in
its *original* task text, but the Resolution paragraph immediately after confirms it).
This is deliberate, established formatting in this project — a task's original text is a
record of what was believed *at the time it was opened*, not a live status field. Reading
either plan file top-to-bottom (not just grepping for "unverified"/"TODO") shows accurate
current state throughout. `NEXT.md` and `AUDIT.md` were kept current after every task this
session (both were part of each task's own build+test+commit cycle, not a separate pass).

**What's actually worth doing here** (the real, narrower version of this task):
1. Add a one-line, unambiguous statement near the top of `NEXT.md` Section 1 (Project
   summary) — something like *"Baseline `Microsoft::Devices` API surface is complete and
   tested against the documented WP7 spec; remaining work is hardening, deeper testing,
   and cross-platform verification, not API completeness."* — so a reader skimming only
   the first paragraph gets the right takeaway without reading the whole changelog.
2. In `NEXT.md` Section 5 (Known bugs and limitations), explicitly group the genuinely
   open items into named categories so they don't read as an undifferentiated list:
   **Android/iOS cross-compilation** (blocked, Task P2-7 / this plan's Phase 7),
   **hardware-in-the-loop verification** (nothing in this codebase has ever run against
   real accelerometer/gyroscope/haptic hardware — Phase 7/8 of this plan),
   **timestamp correctness** (Phase 4 of this plan — see Task P4-7, a real, newly-found
   bug, not carried over from phase3), and **event-callback lifetime safety** (the
   documented residual gap from `plan_devices_phase3.md` Task P3-4 — Phase 2/3 of this
   plan close it).
3. Add a one-line pointer from `plan_devices_phase3.md`'s closing state to this file, so
   anyone reading phase3 top-to-bottom knows a phase4 exists.

**Steps:** edit `NEXT.md` Section 1 and Section 5 as above; add the one-line pointer to
`plan_devices_phase3.md`. No `plan_devices_phase2.md` change needed (checked, not stale).
No build/test needed — documentation only.

**Resolution (2026-07-03):** Implemented exactly as scoped. `NEXT.md` Section 1 now opens
with a one-line "baseline complete" statement before the existing detailed paragraph, and
names `plan_devices_phase4.md` explicitly. Section 5 was restructured with a new
"Genuinely open work, grouped" block at the top (the 4 named categories: event-callback
lifetime safety, real event-path test coverage, timestamp correctness, hardware-in-the-loop
verification, plus Android/iOS cross-compilation — 5 categories, not 4, once
Android/iOS was pulled out as its own bullet for parity with how it was already called
out by name elsewhere in the doc), each pointing at its `plan_devices_phase4.md` task
number; the pre-existing fixed/resolved bullets were kept below under a
"Resolved (historical record)" heading rather than deleted (this project's convention is
to preserve history, not erase it — same reasoning as why `plan_devices_phase2.md`/
`plan_devices_phase3.md` keep original task text after a Resolution is appended). Also
updated 2 bullets that had gone stale in the process: the `VibrateController`
gamepad-exclusion-filter bullet now points at Task P4-10 instead of calling it an
"accepted limitation" (it isn't anymore — a concrete fix exists), and a
now-fully-redundant duplicate Android/iOS bullet further down the section was removed.
`NEXT.md` Section 8 (Next smallest tasks) was also updated — it still described "decide
whether to open phase4" as a next step, which was moot the moment this plan file was
created; replaced with the actual phase4 task order. `plan_devices_phase3.md` got a
one-line closing pointer to this file. No `plan_devices_phase2.md` change made (confirmed
not stale, per the Finding above). No build/test run — documentation only, as scoped.

---

## Phase 2: Event callback lifetime safety

### Task P4-2 — Close the `Accelerometer`/`Gyroscope` callback lifetime gap left open by Task P3-4 — ✅ Done (2026-07-03)

**Verified gap (re-derived from current source, not assumed):**
`Accelerometer::SensorEventWatch()` (and `Gyroscope`'s identical twin) — the SDL
event-filter callback that may run off the main thread — takes `mutex_`, copies
`startedInstances_` into a local snapshot `std::vector<Accelerometer*>`, releases the
lock, then iterates the snapshot calling `accelerometer->ProcessSensorUpdateEvent(...)`
**unlocked**, on each raw pointer. This was a deliberate design choice in Task P3-4 (to
avoid holding a lock across a callout that might re-enter `Start()`/`Stop()` and
deadlock), and its residual risk was explicitly documented there: if the main thread's
`Dispose()` destroys an `Accelerometer` instance *after* the event-watch thread has
already copied that instance's pointer into its snapshot but *before* it finishes calling
`ProcessSensorUpdateEvent()` on it, the call runs on freed memory. Narrow window, no
observed crash (untestable headless — no real concurrent hardware events in this
environment), but a real use-after-free class of bug, not hypothetical.

**Design note — three concrete approaches, pick one during implementation:**

- **(A) Pending-event queue drained on the main/update thread.** `SensorEventWatch()`
  stops touching any `Accelerometer*` at all — it just pushes the raw payload
  (`sensorId`, `x`, `y`, `z`, `timestampNs`) into a thread-safe queue (a
  `mutex_`-guarded `std::deque`, reusing the existing mutex). Add a new `NOXNA` method
  (e.g. `static void Accelerometer::PumpEvents()`) that drains the queue and dispatches
  to `startedInstances_` — call sites: (1) games call it themselves once per frame (WP7
  precedent: XNA's own sensor APIs are asynchronous/event-driven from the app's
  perspective, so this would be a `NOXNA` CNA-specific requirement, not a real-API
  violation, similar to how `VibrateController`'s intensity/dual-motor extensions are
  tagged), or (2) wire it into `Game::Update()`'s internal tick if `Microsoft::Devices`
  is judged tightly-enough coupled to `Game` to justify that (currently it isn't —
  `Microsoft::Devices::Sensors` has zero dependency on `Microsoft::Xna::Framework::Game`
  today, and adding one is a bigger, separate architectural decision outside this task's
  scope). Recommend option (1) for this task: an explicit `NOXNA PumpEvents()` call,
  documented as a required per-frame call for any game using live sensor data. Since
  dispatch now only ever happens on whichever thread calls `PumpEvents()` (expected: main
  thread, matching every other CNA API's threading assumption), and `startedInstances_`
  is only ever mutated from that same thread, there is no cross-thread pointer risk at
  dispatch time at all — the strongest guarantee of the three options, at the cost of a
  new required call games must remember to make and up to one frame of added latency.
- **(B) Stable token/registry + `weak_ptr` liveness check.** Replace
  `startedInstances_`'s raw pointers with a `mutex_`-guarded registry of
  `std::weak_ptr<Accelerometer>`. `SensorEventWatch()` locks each `weak_ptr` to a
  `shared_ptr` before calling `ProcessSensorUpdateEvent()` (guaranteeing the object stays
  alive for the call, or safely skipping it if already destroyed). Correct and
  zero-added-latency, but requires `Accelerometer` instances to be reachable via
  `shared_ptr` for this to work, which is a real, invasive API/ownership change — today
  test code and (presumably) game code construct plain-value/stack instances
  (`Accelerometer a;`, seen throughout `AccelerometerTests.cpp`). Only choose this if the
  latency/required-pump-call cost of (A) is judged worse than an ownership-model
  migration — likely not worth it for this task's scope.
- **(C, smallest diff) Per-instance quiescence flag under the existing mutex.** Add a
  per-instance `bool inFlightCallback_` (or a small counter, if re-entrant dispatch is
  ever possible) guarded by the existing static `mutex_`. `SensorEventWatch()`, while
  still holding `mutex_` (right after finding a live instance in `startedInstances_`,
  before releasing the lock to call out), sets `inFlightCallback_ = true` for that
  instance; releases the lock; calls `ProcessSensorUpdateEvent()` unlocked (unchanged);
  re-acquires `mutex_` afterward to clear the flag. `Dispose(bool)` and the destructor,
  before actually freeing the instance, acquire `mutex_`, remove the instance from
  `startedInstances_` (as today — prevents *new* dispatches), and if
  `inFlightCallback_` is still `true`, wait on a `std::condition_variable` (associated
  with `mutex_`) until it clears, before proceeding to destroy the object. This is a
  quiescence/RCU-style pattern: small diff on top of Task P3-4's existing structure, no
  new required per-frame call, no ownership-model change, closes the exact UAF window
  described above. Main cost: `Dispose()` can now briefly block waiting for an in-flight
  callback to finish (bounded — a single `ProcessSensorUpdateEvent()` call, not
  unbounded), which is a new (small) behavior change worth documenting.

**Recommendation:** (C) — smallest, most targeted fix for the specific risk described,
consistent with this codebase's existing threading model and public API shape. Only
reach for (A) or (B) if (C) is found to be insufficient in practice (e.g. if
`ProcessSensorUpdateEvent()` itself is judged too slow/blocking to have `Dispose()` wait
on).

**Also required by this task (item 3 of the user's original priority list): a test-only
synthetic-event hook.** This is *not* a trivial addition — worth flagging precisely.
`ProcessSensorUpdateEvent()` early-returns unless `started_` is true, `g_sensor_ !=
nullptr`, and `sensorId == g_sensorId_` — all three require a genuinely-opened SDL sensor,
which never happens in this (or any known CI) headless environment (confirmed:
`Accelerometer::Start()` throws `AccelerometerFailedException` on this machine because
`OpenDefaultAccelerometer()` finds no sensors — `started_` never becomes `true`, so
`ProcessSensorUpdateEvent()` can never legitimately run today). Two ways to build the
hook, pick alongside whichever of (A)/(B)/(C) above is chosen:
- **(i) Split validation from conversion+dispatch.** Refactor
  `ProcessSensorUpdateEvent()` into a private guard (`started_`/`g_sensor_`/`sensorId`
  checks) that, once passed, calls a separate private method doing the actual
  raw-floats-to-`AccelerometerReading` conversion and `setCurrentValueProperty()`/
  `ReadingChanged.Raise()` dispatch. Add a `NOXNA` test-only method (guarded so it's only
  ever meant for test builds — a clearly-named public method is acceptable, matching
  this project's existing precedent of exposing narrowly-scoped `NOXNA` test/debug
  surface, e.g. `VibrateController::getDeviceNameProperty()`) that calls the
  conversion+dispatch method directly, skipping the hardware-presence guard entirely.
  Safer (can't corrupt shared `g_sensor_`/`g_sensorId_` state), but skips exercising the
  `sensorId` matching guard.
- **(ii) Fake an open device for the duration of a synthetic call.** A test-only method
  temporarily sets `g_sensor_`/`g_sensorId_`/`started_` to simulate a real device, routes
  through the unmodified `SensorEventWatch()`/`ProcessSensorUpdateEvent()` path, then
  restores prior state. Exercises more real code (including the id-match guard) but risks
  corrupting real shared state if a real sensor happens to be present when tests run —
  must save/restore carefully and only be used when this environment's actual sensor
  state doesn't conflict (check `getIsSupportedProperty()` first).

**Recommendation:** (i) — safer, and the id-matching guard it skips is simple enough
(single integer comparison) that a dedicated unit test of that guard alone (not full
event-path integration) is sufficient coverage for it, without needing the riskier (ii).

**Steps:**
1. Implement the chosen lifetime-safety fix (recommended: (C)) in both `Accelerometer`
   and `Gyroscope` (identical duplicated pattern in both — keep them in sync, same as
   Task P3-4).
2. Implement the chosen synthetic-event hook (recommended: (i)) in both classes.
3. No behavior change to any existing public API — verify via full `ctest`, same 1985
   tests, no regressions, before moving to Phase 3's tests which consume the new hook.

**Files:** `include/Microsoft/Devices/Sensors/Accelerometer.hpp`,
`src/Microsoft/Devices/Sensors/Accelerometer.cpp`,
`include/Microsoft/Devices/Sensors/Gyroscope.hpp`,
`src/Microsoft/Devices/Sensors/Gyroscope.cpp`.

**Resolution (2026-07-03):** Implemented option (C) (per-instance quiescence flag) and
option (i) (split validation from conversion+dispatch) exactly as recommended, in both
`Accelerometer` and `Gyroscope`. Added `bool inFlightCallback_` (per-instance, guarded by
the existing `mutex_`) and a shared `static std::condition_variable callbackFinished_`.
`SensorEventWatch()` now sets `inFlightCallback_ = true` for each instance while still
holding `mutex_` (right when building the dispatch snapshot), releases the lock, calls
`ProcessSensorUpdateEvent()` unlocked (unchanged from Task P3-4), then re-acquires
`mutex_` to clear the flag and notify. `Dispose(bool)` — after `Stop()` has already
removed the instance from `startedInstances_`, preventing any *new* dispatch — waits on
`callbackFinished_` for `inFlightCallback_` to clear before proceeding to decrement
`instanceCount_`/touch `g_sensor_`, closing the exact use-after-free window Task P3-4 left
open. `ProcessSensorUpdateEvent()` was split into itself (guards: `started_`,
`getIsDisposedProperty()`, `g_sensor_`/`sensorId` hardware match) plus a new private
`DispatchSensorReading(x, y, z, timestampNs)` doing the actual conversion+dispatch —
both classes now share this shape. Added the 2 planned `NOXNA` test-only hooks to both
classes: `InjectSyntheticSensorUpdate(x, y, z, timestampNs)` (calls
`DispatchSensorReading()` directly, skipping only the hardware-presence checks — still
respects `started_`/disposed state) and `SetStartedForTesting(bool)` (sets `started_`
directly, since the real `Start()` always throws in this headless environment and there
would otherwise be no way to test `InjectSyntheticSensorUpdate()`'s `started_` gating, or
confirm the real `Stop()` correctly disables it). The known accepted limitation (a handler
that reentrantly calls `Dispose()` on its own sender from within its own callback would
deadlock) is documented in both headers' `inFlightCallback_` doc comment, not solved,
matching the plan's own scoping. Verified: `CNA` + `CnaTests` build clean, no behavior
change to any existing public API. Full `ctest` — 1985 tests, same 64 pre-existing
headless failures, zero regressions (no new tests added by this task itself — Tasks P4-3
through P4-6, next, are what actually exercise the new hooks).

---

## Phase 3: Real event-path testing (headless, via the new synthetic hook)

All four tasks below depend on Task P4-2's synthetic-event hook existing first.

### Task P4-3 — `Accelerometer.CurrentValueChanged` receives the expected `AccelerometerReading`

**Steps:** using the synthetic hook, inject a known `(x, y, z, timestampNs)` triple;
subscribe to `CurrentValueChanged`; assert the raised `SensorReadingEventArgs<AccelerometerReading>`
carries a reading whose `Acceleration` matches the expected converted value (`x /
StandardGravity`, etc. — mirror `ProcessSensorUpdateEvent()`'s own conversion math) and
whose `Timestamp` is populated. Also assert `getCurrentValueProperty()` reflects the same
reading afterward.

**Files:** `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp`.

### Task P4-4 — Legacy `Accelerometer.ReadingChanged` receives the same X/Y/Z/Timestamp

**Steps:** same synthetic injection as P4-3; subscribe to `ReadingChanged`; assert the
raised `AccelerometerReadingEventArgs`'s `X`/`Y`/`Z`/`Timestamp` match the same expected
values used in P4-3 (both events are raised from the same call site in
`ProcessSensorUpdateEvent()`, per the existing `ReadingChangedSubscriptionDoesNotThrow`
test's own comment — this task finally verifies that claim with real data instead of only
verifying subscription doesn't crash).

**Files:** `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp`.

### Task P4-5 — `Gyroscope.CurrentValueChanged` receives the expected `GyroscopeReading`

**Steps:** mirror Task P4-3 for `Gyroscope` (no unit conversion — `GyroscopeReading`'s
`RotationRate` is raw radians/second, unlike `Accelerometer`'s g-normalization).

**Files:** `tests/Microsoft/Devices/Sensors/GyroscopeTests.cpp`.

### Task P4-6 — `Stop()` prevents a subsequent synthetic event from doing anything

**Steps:** start an instance (via whatever mechanism P4-2's hook allows, or the real
`Start()` if the platform happens to support it — branch on `getIsSupportedProperty()`
same as existing tests), inject a synthetic event, confirm `CurrentValueChanged` fires;
call `Stop()`; inject another synthetic event with different values; assert
`CurrentValueChanged` does **not** fire again and `getCurrentValueProperty()`'s value is
unchanged from before `Stop()`. Do for both `Accelerometer` and `Gyroscope`.

**Files:** `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp`,
`tests/Microsoft/Devices/Sensors/GyroscopeTests.cpp`.

---

## Phase 4: Timestamp audit

### Task P4-7 — Decide and fix: `Timestamp` should be wall-clock, not monotonic ticks

**Gap (verified, high confidence — a real bug, not a style question):**
`Accelerometer::ProcessSensorUpdateEvent()` (and `Gyroscope`'s twin) builds the reading's
`Timestamp` like this:
```cpp
const SharpRuntime::longcs ticks = static_cast<SharpRuntime::longcs>(timestampNs / 100);
System::DateTime dateTime(ticks);
System::DateTimeOffset timestamp(dateTime, System::TimeSpan::Zero);
```
where `timestampNs` comes from `SDL_GetTicksNS()`. Two independently-verified facts make
this wrong as a wall-clock timestamp:
1. `SDL_GetTicksNS()` returns nanoseconds since **SDL library initialization** — an
   arbitrary monotonic reference point near process start, *not* nanoseconds/ticks since
   any real calendar epoch.
2. `System::DateTime(longcs ticks)` (confirmed via `sharp-runtime`'s
   `include/System/DateTime.hpp` doc comment and `src/System/DateTime.cpp`'s
   constructor) interprets `ticks` as *"100-nanosecond ticks since the .NET epoch
   (0001-01-01 00:00:00)"*.

Feeding a small monotonic value (nanoseconds-since-SDL-init, divided by 100) into a
constructor that expects ticks-since-year-1 produces a `DateTime` within a fraction of a
second of `0001-01-01 00:00:00` — not the actual time the sensor reading occurred, and
not usable for any real calendar/elapsed-wall-time calculation a game might do with it.
This has been present since the original `plan_devices.md` implementation and was never
caught because no existing test asserts anything about `Timestamp`'s actual calendar
value (only that it round-trips through getters/setters/equality — see
`AccelerometerReadingTests.cpp`).

**Decision needed (matches `plan_devices_phase3.md` Task P3-2's "this is a decision, not
a mechanical fix" framing):**
- **(A) Recommended: make it real wall-clock time.** Replace the `SDL_GetTicksNS()`-based
  conversion with `System::DateTimeOffset::getUtcNowProperty()` (confirmed to already
  exist in `sharp-runtime`, `include/System/DateTimeOffset.hpp`) called at the point the
  event is processed. This matches the real WP7 API's documented behavior for
  `AccelerometerReading.Timestamp`/`GyroscopeReading.Timestamp` (wall-clock
  `DateTimeOffset`, per every class's MSDN property doc referenced throughout
  `plan_devices.md`/`plan_devices_phase2.md`'s research). Slightly less precise than the
  original per-sample SDL timestamp (system clock query overhead/resolution vs. a
  hardware timestamp), but correct in the dimension that matters (calendar time), and
  simple — no new type/state needed, just swap the conversion.
- **(B) Keep monotonic time, document it as an intentional deviation.** If a hardware
  timestamp's *precision* (not wall-clock accuracy) is judged more valuable for some use
  case, keep `SDL_GetTicksNS()` but stop constructing a bogus `DateTime` from it — e.g.
  store it as a `NOXNA` separate monotonic-nanoseconds field instead, and set the real
  `Timestamp` property via `DateTimeOffset::getUtcNowProperty()` regardless (both — real
  wall-clock in the real property, precise relative timing in a CNA extension field). Add
  a `CHECKLIST.md` deviations-table row either way documenting the choice.

**Steps (assuming option A, the recommendation):**
1. In `Accelerometer::ProcessSensorUpdateEvent()` and `Gyroscope::ProcessSensorUpdateEvent()`,
   replace the `timestampNs`-derived `DateTime`/`DateTimeOffset` construction with
   `System::DateTimeOffset::getUtcNowProperty()`.
2. `timestampNs`/`SDL_GetTicksNS()` may become entirely unused in the event path if not
   needed elsewhere — remove the now-dead conversion code and unused includes if so.
3. Tests: add a test asserting a freshly-produced reading's `Timestamp` is within a
   reasonable tolerance (e.g. a few seconds) of `DateTimeOffset::getUtcNowProperty()` at
   assertion time, rather than asserting an exact value (inherently non-deterministic,
   same pattern used elsewhere in this codebase for wall-clock-adjacent assertions, if
   any precedent exists — otherwise this is a new but standard testing pattern: assert
   "close to now", not "equals a fixed value").
4. Update `AUDIT.md`'s `AccelerometerReading`/`GyroscopeReading` rows to note the fix and
   its date.

**Files:** `src/Microsoft/Devices/Sensors/Accelerometer.cpp`,
`src/Microsoft/Devices/Sensors/Gyroscope.cpp`,
`tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp`,
`tests/Microsoft/Devices/Sensors/GyroscopeTests.cpp`, `AUDIT.md`.

---

## Phase 5: SDL sensor subsystem ownership between `Accelerometer` and `Gyroscope`

### Task P4-8 — Stop bypassing SDL's own subsystem ref-counting

**Gap (verified against `third_party/SDL/include/SDL3/SDL_init.h`'s own doc comment,
high confidence):** SDL3 states explicitly: *"Subsystem initialization is ref-counted,
you must call `SDL_QuitSubSystem()` for each `SDL_InitSubSystem()` to correctly shutdown
a subsystem manually."* — SDL already provides exactly the cross-class sharing mechanism
Priority 5 asked for. The bug is that CNA's own wrapper defeats it:
```cpp
bool Accelerometer::EnsureSensorSubsystemInitialized()
{
    if (SDL_WasInit(SDL_INIT_SENSOR)) { return true; }   // <-- skips the SDL_InitSubSystem() call entirely
    return SDL_InitSubSystem(SDL_INIT_SENSOR);
}
```
(identical in `Gyroscope`). If `Gyroscope` initializes `SDL_INIT_SENSOR` first,
`Accelerometer::EnsureSensorSubsystemInitialized()` sees `SDL_WasInit() == true` and never
calls `SDL_InitSubSystem()` itself — so SDL's internal ref-count only reflects
`Gyroscope`'s one call, not two. Later, when `Accelerometer`'s last instance disposes and
calls `SDL_QuitSubSystem(SDL_INIT_SENSOR)` (also currently guarded by `SDL_WasInit()`,
which is `true`), it decrements SDL's ref-count by one — down to zero, since only one
increment ever happened — and the subsystem is torn down for real, **while `Gyroscope`
instances are still active and expect it to be alive.** This is the exact conflict
Priority 5 described, confirmed as real and root-caused, not hypothetical.

**Recommended fix (simpler than a hand-rolled shared reference counter, since SDL already
provides one):**
1. Remove the `SDL_WasInit()` guard from `EnsureSensorSubsystemInitialized()` in both
   classes — always call `SDL_InitSubSystem(SDL_INIT_SENSOR)` so SDL's own ref-count is
   correctly incremented every time. (SDL's implementation only does the real
   first-time-init work once internally; every call is cheap and safe to repeat — this is
   the documented, intended usage.)
2. **Nuance:** `EnsureSensorSubsystemInitialized()` currently runs on every `Start()`
   call, not just an instance's first one — calling it unconditionally on every `Start()`
   without a matching `SDL_QuitSubSystem()` on every `Stop()` would over-increment SDL's
   ref-count relative to the decrements in `Dispose()`, permanently preventing the
   subsystem from reaching zero (harmless at process exit — `SDL_Quit()` tears down every
   subsystem regardless of ref-count per its own doc comment — but means
   `SDL_QuitSubSystem()` calls before then would never actually release resources). Add a
   per-instance `bool subsystemHeld_` flag: call `SDL_InitSubSystem()` at most once per
   instance (on that instance's first successful `Start()`), and pair it with exactly one
   `SDL_QuitSubSystem()` call for that instance (in `Stop()` or `Dispose()`, whichever
   this instance's own increment corresponds to), keeping each instance's own init/quit
   calls balanced 1:1 and letting SDL's subsystem-level ref-count correctly aggregate
   across instances *and* across `Accelerometer`/`Gyroscope`.
3. Remove the `SDL_WasInit()` guard around `SDL_QuitSubSystem()` in `Dispose(bool)` for
   the same reason — call it unconditionally when `subsystemHeld_` is true for the
   disposing instance, and let SDL's own ref-count decide whether that was the last
   holder.

**Steps:**
1. Add `bool subsystemHeld_` to both `Accelerometer` and `Gyroscope`.
2. Update `Start()`/`Stop()`/`Dispose(bool)` in both classes per the design above.
3. Tests: this is fundamentally a cross-class SDL-internal-state interaction — hard to
   assert on directly without real hardware (headless, no real sensors, `Start()` always
   throws in this environment before reaching the subsystem calls). Focus tests on
   confirming no regression in existing single-class behavior; if feasible, add a test
   that constructs both an `Accelerometer` and a `Gyroscope` instance, disposes one,
   and confirms the other's `getStateProperty()`/behavior is unaffected — this can
   at least verify the *code path* doesn't crash, even if it can't observe SDL's
   internal ref-count directly.

**Files:** `include/Microsoft/Devices/Sensors/Accelerometer.hpp`,
`src/Microsoft/Devices/Sensors/Accelerometer.cpp`,
`include/Microsoft/Devices/Sensors/Gyroscope.hpp`,
`src/Microsoft/Devices/Sensors/Gyroscope.cpp`.

---

## Phase 6: `VibrateController` hardening

### Task P4-9 — Thread-safety for `g_haptic`/`g_leftRightEffectId`; consider RAII cleanup

**Gap:** `VibrateController.cpp`'s file-static `g_haptic`/`g_leftRightEffectId` are read
and written from `Start()`/`Start(duration,intensity)`/`Stop()`/`StartLeftRight()`/
`getIsSupportedProperty()`/`getDeviceNameProperty()` with **zero synchronization** — same
class of gap Task P3-4 fixed for the sensor classes, not yet addressed here.
`VibrateController` doesn't have an SDL event-watch callback (unlike the sensor classes),
so the risk is narrower — same-class as any unsynchronized shared mutable state accessed
from multiple application threads (e.g. a game calling `Start()` from a gameplay thread
while another thread calls `Stop()` — plausible in a multi-threaded game architecture,
even though this project's own usage today is presumably single-threaded from `Game`).

**Steps:**
1. Add a `static std::mutex` (matching Task P3-4's naming/placement convention) guarding
   `g_haptic`/`g_leftRightEffectId`. Lock around every function in the anonymous namespace
   and every `VibrateController::` method that touches either variable.
2. **RAII cleanup consideration:** the existing code comment already documents the
   current design choice explicitly (*"Never explicitly closed; it is released by the
   OS/SDL_Quit at process exit, matching XNA's fire-and-forget static VibrateController
   design"*) — this is a deliberate choice, not an oversight. Re-evaluate whether it's
   still the right one now that `VibrateController` is a proper singleton
   (`getDefaultProperty()`, since `plan_devices_phase2.md` Task P2-14) rather than a bag
   of static functions: a `static` local in `getDefaultProperty()` already has
   process-lifetime, so an RAII wrapper around `g_haptic` (e.g. destroyed when the
   singleton itself is destroyed at process exit via a static local's destructor) would
   be a very small change with no API-visible effect, closing the "never closed" gap
   cheaply. Recommend doing this alongside the mutex work since it touches the same
   `g_haptic` variable, but only if it doesn't complicate the mutex design (e.g. avoid
   double-locking a mutex from a static destructor running after other statics may have
   already been destroyed — verify destruction order carefully, or skip this half if it
   introduces static-destruction-order risk not worth taking for a process-exit cleanup
   that already happens automatically via the OS).

**Files:** `src/Microsoft/Devices/VibrateController.cpp`.

### Task P4-10 — Replace name-matching haptic↔gamepad correlation with `SDL_OpenHapticFromJoystick()`

**Verified fix (not just research — a concrete API exists):**
`third_party/SDL/include/SDL3/SDL_haptic.h` declares
`SDL_Haptic* SDL_OpenHapticFromJoystick(SDL_Joystick* joystick)`, documented as SDL's own
intended way to correlate a haptic device with a specific already-open joystick (its
own usage example in the header opens a joystick, then calls this function directly to
get that joystick's haptic device, returning `NULL` if *"joystick isn't haptic"*). This
directly replaces `VibrateController.cpp`'s current `IsConnectedGamepadHapticDevice()`
heuristic, which compares `SDL_GetHapticNameForID()`/`SDL_GetJoystickNameForID()` string
equality — acknowledged in the existing code comment as fragile (*"two physically
distinct controllers reporting an identical product name would both be
excluded/included together"*).

**Steps:**
1. In `VibrateController.cpp`'s `OpenFirstHapticDevice()`/`IsConnectedGamepadHapticDevice()`,
   replace the name-comparison approach: enumerate connected joysticks (`SDL_GetJoysticks()`),
   open each (`SDL_OpenJoystick()`), call `SDL_OpenHapticFromJoystick()` on it — if it
   returns non-`NULL`, that haptic ID is definitively a gamepad's own haptic motor
   (matches by ID, not name) and should be excluded from `VibrateController`'s device
   selection, same as today's intent, just correctly implemented. Close the haptic handle
   returned by `SDL_OpenHapticFromJoystick()` immediately after checking (don't hold it
   open — this is a probe, matching the existing `AcquireHapticDeviceForProbe()`
   discipline of not holding devices open as a side effect of checking). Close the
   joystick handle too, unless something else in this codebase already needs it open
   (check `Microsoft::Xna::Framework::Input::GamePad`'s implementation for any shared
   joystick-handle-ownership assumptions before closing anything cavalierly).
2. Tests: this can't be asserted on directly headless (no real gamepad), same limitation
   as today's `IsConnectedGamepadHapticDevice()` — the existing
   `VibrateControllerTests.cpp` note about this already applies; no new test possible
   beyond confirming the changed code path still builds and existing no-throw tests still
   pass.

**Files:** `src/Microsoft/Devices/VibrateController.cpp`.

---

## Phase 7: Cross-platform build

### Task P4-11 — Compile the Android branch

`Accelerometer.cpp`/`Gyroscope.cpp`'s `#ifdef __ANDROID__` landscape-axis-remap code has
never been compiled by any compiler in this project's history (confirmed repeatedly
across `plan_devices.md`/`plan_devices_phase2.md`/`plan_devices_phase3.md`'s own status
notes) — same for anything `VibrateController.cpp` does differently on Android (currently
nothing `#ifdef`-gated there; its Android behavior comment notes SDL3's Android haptic
backend needs no CNA-side branching). **Blocked in this environment** — no Android NDK
available in this dev container, same blocker as `plan_devices_phase2.md` Task P2-7.
Re-check whether the environment has changed before assuming this is still blocked; if
still blocked, this needs a different environment or CI, not further attempts here.

### Task P4-12 — Compile the iOS branch, or explicitly mark it unverified

Same status as P4-11 for iOS — no toolchain available in this Linux dev container. If
genuinely unattemptable here, at minimum ensure `NEXT.md`/`AUDIT.md` say so explicitly
rather than silently omitting iOS from the conversation (Android is already called out
by name in existing docs; iOS should be too, symmetric treatment).

### Task P4-13 — Manual hardware verification checklist

Write a plain checklist (new file, e.g. `docs/devices-hardware-checklist.md`, or a
section in `NEXT.md` if a new file is judged unnecessary overhead) for whoever eventually
runs this on real hardware: accelerometer axis sign/orientation correctness in both
landscape rotations (the `ConvertAndroidAccelerometerToXnaLandscape()` math has never
been physically verified — only reasoned about from SDL/Android coordinate-system
documentation), gyroscope axis correctness (same caveat), `VibrateController::Start()`
actually vibrating the phone motor (not a connected gamepad), `StartLeftRight()`
independently driving two distinct motors if the test device has them, and the
gamepad-exclusion filter (Task P4-10) genuinely not competing with
`GamePad::SetVibration()` on the same physical controller. This is a plain checklist
document, not a task with a build/test cycle.

---

## Phase 8: Demo / manual verification

### Task P4-14 — `Microsoft::Devices` demo screen

**Precedent (confirmed in this codebase):** `examples/demo_input/src/InputDemo.hpp`/`.cpp`
is a `Microsoft::Xna::Framework::Game` subclass with `LoadContent()`/`Update()`/`Draw()`
overrides, demonstrating `GamePad`/`Keyboard`/`Mouse`/`TouchPanel` — the established
pattern for this kind of manual/visual verification tool in this project. Mirror it.

**Steps:**
1. New `examples/demo_devices/src/DevicesDemo.hpp`/`.cpp`, following `InputDemo`'s
   structure (check `examples/demo_input`'s `CMakeLists.txt`/parent build wiring for how
   demo targets get registered, and replicate for `demo_devices`).
2. Display, per sensor class (`Accelerometer`, `Compass`, `Gyroscope`, `Motion`):
   `getIsSupportedProperty()`, `getStateProperty()` (where applicable), the latest
   `CurrentValue` reading's key fields, and a running count of `CurrentValueChanged`
   events received (proves the real event path works end-to-end on whatever device the
   demo actually runs on — ties back into Phase 7's manual checklist).
3. Add input bindings (mirroring `InputDemo`'s existing keyboard-binding style) to call
   `VibrateController::getDefaultProperty()->Start(...)`/`Stop()`/the `NOXNA`
   intensity/`StartLeftRight()` overloads, so a human can trigger and feel vibration
   directly.

**Files:** new `examples/demo_devices/` directory; whatever root/`examples/CMakeLists.txt`
wiring registers new demo targets in this project.

---

## Verification checklist (apply to every source-touching task above)

- Build `cmake --build cmake-build-debug --target CNA` then `--target CnaTests`.
- Run the specific new/changed test suite via `--gtest_filter`.
- Run full `cd cmake-build-debug && ctest --output-on-failure` and confirm no new
  regressions beyond the existing headless `EasyGL_*` baseline (re-check the current
  count first — it was 64 as of `plan_devices_phase3.md`'s close, 2026-07-03).
- Update `NEXT.md` (status/recent-changes/known-bugs sections) after each task.
- Commit immediately after each finished task, per `CLAUDE.md`'s "Git Commits" section —
  one task, one commit, staged by explicit filename.
