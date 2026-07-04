# plan_devices_phase5.md — Devices Runtime Robustness: Ownership, Concurrency, Native-Backend Roadmap

## Context

`plan_devices.md`/`plan_devices_phase2.md`/`plan_devices_phase3.md`/`plan_devices_phase4.md`
all claim `Microsoft::Devices`/`Microsoft::Devices::Sensors` is "complete"/"hardened"/
"thread-safe"/"verified". **This plan treats those claims as a hypothesis, not a fact,
and re-audits the actual code before making any change.** The audit below found that
several of those claims do not survive direct code re-reading — most notably, Task
P4-8 (2026-07-04, earlier this same session) fixed one subsystem-refcount bug but, by
removing a guard, made a *different*, pre-existing subsystem-refcount bug in the same
file strictly worse. This plan exists to find and fix issues like that, not to add new
API surface.

**Scope discipline:** this plan only touches `Microsoft::Devices`/
`Microsoft::Devices::Sensors` source, tests, and docs. No public XNA/WP7 API signature
changes. Any new public member is `NOXNA` and documented as a CNA extension. GPS/
location is explicitly out of scope for `Microsoft::Devices::Sensors` (see Task P5-10).

---

## Audit findings (written before any code change in this plan)

Re-read `Accelerometer.{hpp,cpp}`, `Gyroscope.{hpp,cpp}`, `Compass.{hpp,cpp}`,
`Motion.{hpp,cpp}`, `SensorBase.hpp`, `VibrateController.{hpp,cpp}`, and every test file
under `tests/Microsoft/Devices/` directly, plus grepped the whole tree for `SDL_Quit(`
and read SDL3's own `SDL_AddEventWatch()`/`SDL_PushEvent()` doc comments in
`third_party/SDL/include/SDL3/SDL_events.h`. Findings, numbered to match the 7 items in
this plan's originating task brief:

### 1. `EnsureSensorSubsystemInitialized()` probe imbalance — CONFIRMED REAL, and worse than before

`Accelerometer::getIsSupportedProperty()` (`Accelerometer.cpp`) and
`Gyroscope::getIsSupportedProperty()` (`Gyroscope.cpp`) both call
`EnsureSensorSubsystemInitialized()` unconditionally on **every call** — including every
time, since `Compass`/`Motion` aside, every `Accelerometer`/`Gyroscope` constructor
unconditionally calls this static method to compute `isSupported_`/`state_`. Since Task
P4-8 (this same session, earlier), `EnsureSensorSubsystemInitialized()` **always** calls
`SDL_InitSubSystem(SDL_INIT_SENSOR)` — the `SDL_WasInit()` guard that used to make
repeat calls a no-op was deliberately removed, specifically so `Start()`'s own
`subsystemHeld_`-gated call would correctly increment SDL's real ref-count. But
`getIsSupportedProperty()`'s call is **not** gated by any per-instance or per-call
flag, and nothing ever calls a matching `SDL_QuitSubSystem(SDL_INIT_SENSOR)` for it. Net
effect: constructing an `Accelerometer`/`Gyroscope` that is *never* `Start()`'d — or
calling the static `getIsSupportedProperty()` directly, which existing tests already do
repeatedly (`GetIsSupportedPropertyDoesNotCrash`) — leaves the SDL sensor subsystem's
internal ref-count permanently incremented with **zero** matching decrement. Before Task
P4-8, the `SDL_WasInit()` guard accidentally masked this (only the very first-ever call
did a real init); after P4-8 removed that guard, every one of these calls is a real,
unmatched `SDL_InitSubSystem()` call. **This is a real regression Task P4-8 introduced
as a side effect of fixing a different bug, undetected until this audit.** Fixed in
Task P5-1.

### 2. `inFlightCallback_` single bool — CONFIRMED PLAUSIBLE, not ruled out by SDL's own contract

`third_party/SDL/include/SDL3/SDL_events.h`'s own doc comment for `SDL_AddEventWatch()`
states, verbatim: *"**WARNING**: Be very careful of what you do in the event filter
function, as it may run in a different thread!"* — and `SDL_PushEvent()` (the function
that synchronously invokes every registered watch as part of adding an event) is
documented `\threadsafety It is safe to call this function from any thread.` Neither
guarantees a single call to `Accelerometer::SensorEventWatch()`/
`Gyroscope::SensorEventWatch()` is ever *not* re-entered concurrently from a second
thread while the first invocation is still mid-dispatch (e.g. two sensor events
delivered in quick succession from whatever platform-specific backend thread produces
them). If that happens for the *same* instance: thread A finishes its dispatch, sets
`inFlightCallback_ = false`, and notifies — while thread B is still inside
`ProcessSensorUpdateEvent()`/`DispatchSensorReading()` for that same instance. A
concurrently-running `Dispose()` waiting on `!inFlightCallback_` would then wake up and
proceed to free the object while thread B is still using it: a genuine use-after-free
window, just one this headless, single-threaded-callback test environment cannot
reproduce today. Cannot be proven to occur in current desktop testing, but is not ruled
out by SDL's documented contract, and the fix (an integer count instead of a bool) is
cheap and strictly more correct regardless. Fixed in Task P5-2.

### 3. Data races — CONFIRMED, several found by direct reading

- `started_` is read with **no lock** in both `Accelerometer::ProcessSensorUpdateEvent()`
  and `Accelerometer::InjectSyntheticSensorUpdate()` (`if (!started_) return;`), while
  `Start()`/`Stop()` write it under `mutex_`. Same in `Gyroscope`. Unsynchronized
  concurrent read/write across threads — undefined behavior by the C++ memory model,
  independent of whether it "usually works" on a given platform/compiler.
- `Accelerometer::getStateProperty()`/`Gyroscope::getStateProperty()` read `state_` with
  **no lock at all**, while `Start()` writes `state_` partly **outside** its own
  `mutex_` scope (the early-throw paths, e.g. `state_ = SensorState::NotSupported;`
  before subsystem init succeeds, execute before the `{ std::lock_guard ... }` block
  begins) and `Stop()` writes it under the lock. Inconsistent locking discipline around
  the same field.
- `subsystemHeld_`'s check-and-set in `Start()` (`if (!subsystemHeld_) { ...;
  subsystemHeld_ = true; }`) happens **outside** `mutex_`'s scope, while `Dispose()`
  reads/writes the same field **inside** the lock. Same inconsistency as `state_`.
- **Most significant:** `SensorBase<T>` (`SensorBase.hpp`, the template base shared by
  all 4 sensor classes) has **zero synchronization** around `currentValue_`,
  `isDataValid_`, or `eventArgs_`. `setCurrentValueProperty()`/`setIsDataValidProperty()`
  (called from `DispatchSensorReading()`, on whatever thread the SDL event-watch
  callback runs on for `Accelerometer`/`Gyroscope`) write these with no lock, while
  `getCurrentValueProperty()`/`getIsDataValidProperty()` (callable from the game/user
  thread at any time) read them with no lock. This is in the shared base, not specific
  to one sensor class — the single biggest data-race finding in this audit.

Fix the clear ones (P5-2 extends to cover `SensorBase<T>`'s race, since it's directly
tied to the same callback-thread-vs-user-thread theme already in scope there; P5-4's
migration to a shared subsystem manager resolves `started_`/`state_`/`subsystemHeld_`'s
inconsistent locking as part of centralizing that state under one lock). None of these
fixes hold a lock while calling `CurrentValueChanged.Raise()`/`ReadingChanged.Raise()` —
a subscriber's handler can legitimately call back into the sensor (e.g. `Dispose()`,
`getStateProperty()`), and raising under a lock risks deadlock.

### 4. `SetStartedForTesting(bool)` + unsupported hardware — CONFIRMED correct, was undocumented/untested

`SetStartedForTesting(bool)` (`Accelerometer.cpp`/`Gyroscope.cpp`) only ever writes the
private `started_` field. It never touches `isSupported_`. `SensorBase<T>::
getCurrentValueProperty()`'s `InvalidOperationException` gate checks `isSupported_`,
which is set exactly once, in each sensor's constructor, from the real hardware probe
(`setIsSupportedProperty(supported)`). So on hardware where the real sensor is
unsupported, `getCurrentValueProperty()` **still throws** even after
`SetStartedForTesting(true)` + `InjectSyntheticSensorUpdate(...)` — this is intentional
and already the *de facto* contract (an existing `AccelerometerTests.cpp` comment on
`CurrentValueChangedReceivesExpectedReading` already relies on this: *"getCurrentValueProperty()
is deliberately not asserted here since it independently throws when the platform is
genuinely unsupported"*), but no test directly proves this combination, and the header
comments don't state it as a deliberate contract. Documented explicitly and tested in
Task P5-6.

### 5. `Compass`/`Motion` stub honesty — CONFIRMED, matches intended scope

`Compass::getIsSupportedProperty()`/`Motion::getIsSupportedProperty()` are hardcoded
`return false;` (`Compass.cpp`/`Motion.cpp`) with a comment citing "SDL3 exposes no
magnetometer/compass API on any supported platform." `Start()` on both always throws
`SensorFailedException`. `Calibrate` (`System::EventHandler<CalibrationEventArgs>`)
exists on both for WP7 API-completeness but no code path ever raises it (there is no
compass/motion data source to trigger a calibration event from). This matches the
intended, honestly-incomplete design — not a bug. What's missing is a written plan for
what a real backend would look like; see Tasks P5-8/P5-9 and the "Future native backend
plan" section below.

### 6. `VibrateController`'s `g_haptic` lifetime — RE-EXAMINED, prior reasoning was wrong

Task P4-9's Resolution note (this same session) explicitly skipped adding RAII cleanup
for `g_haptic`, reasoning: *"`SDL_Quit()` from `Game`'s own shutdown path very plausibly
runs before C++ static destructors at true program exit."* That reasoning was never
actually verified — re-checked now: `grep -rn "SDL_Quit(" src include` (excluding
`third_party/`) returns **zero results** anywhere in this codebase. `SDL_Quit()` is
never called by CNA at all. That means every SDL subsystem stays fully valid for the
entire process lifetime until the OS reclaims the process — which happens *after* C++
static destructors already ran as part of normal program termination. There is no
race between "SDL already torn down" and a static destructor closing `g_haptic`,
because nothing tears SDL down first. **The prior session's stated reason to skip RAII
cleanup was an unverified assumption that turns out to be false.** RAII cleanup is
safe and will be added (Task P5-11). Separately confirmed: `EnsureHapticSubsystemInitialized()`
(`VibrateController.cpp`) still uses the same `SDL_WasInit()`-guard pattern Task P4-8
removed from the sensor classes — never updated in P4-9/P4-10. Lower severity here
(`VibrateController` is a true process-wide singleton with exactly one call path in
this codebase touching `SDL_INIT_HAPTIC`, unlike the two-classes-sharing-one-subsystem
case P4-8 fixed), but the same "own bookkeeping instead of trusting `SDL_WasInit()`"
principle applies and is fixed alongside the RAII change in Task P5-11.

### 7. Android/iOS claims — CONFIRMED accurate for what was actually run this session

Tasks P4-11 (Android) and P4-12 (iOS) were verified by actually running the respective
commands earlier in this same continuous session: `cmake-build-android` was configured
against the real NDK at `~/Android/Sdk/ndk/30.0.14904198` and `CNA` was built, then
`nm` confirmed `ConvertAndroidAccelerometerToXnaLandscape()`/
`ConvertAndroidGyroscopeToXnaLandscape()` are present in the compiled objects; iOS was
confirmed blocked by actually checking for `xcodebuild`/`xcrun`/`osxcross` on this
filesystem (none found). These are not stale claims copied from a previous run — they
were run in-session. However, this plan makes substantial changes to
`Accelerometer.cpp`/`Gyroscope.cpp`/`VibrateController.cpp`, so the Android build must
be **re-run** at the end of this plan (Task P5-14) — an untested claim from before this
plan's edits would not cover code that didn't exist yet when P4-11 ran.

---

## Task P5-1 — Fix SDL sensor subsystem probe balancing

**Problem:** see Audit finding 1. `getIsSupportedProperty()`'s probe-only subsystem
touch is never balanced by a matching quit.

**Design:** distinguish two lifetimes touching `SDL_INIT_SENSOR`:
- **Probe-only** (`getIsSupportedProperty()`): must leave the subsystem exactly as it
  found it — init only if not already held by a live instance, quit again immediately
  if this probe was the one that had to init it.
- **Instance ownership** (`Start()`/`Dispose()`): already correctly balanced 1:1 per
  instance via `subsystemHeld_` since Task P4-8 — unchanged by this task.

**Steps:**
1. Add a small RAII helper (`SensorSubsystemProbeGuard`, per the originating brief's
   suggested name) used only by `getIsSupportedProperty()`: on construction, if the
   subsystem is not already held by any live instance (tracked via a new class-static
   `int liveInstanceSubsystemHolders_` counter, incremented/decremented by
   `subsystemHeld_` transitions in `Start()`/`Dispose()`), call
   `SDL_InitSubSystem(SDL_INIT_SENSOR)` and remember to quit it on destruction; if the
   subsystem is already held by a live instance, do nothing on construction or
   destruction (piggy-back on the existing holder, matching SDL's own ref-counting so
   the probe doesn't prematurely tear down a subsystem a live instance still needs).
2. Apply to both `Accelerometer` and `Gyroscope` identically.
3. Tests: exact SDL internal ref-count isn't portably assertable headless (no public
   SDL API returns it). Add a behavioral regression test instead: construct and
   immediately discard many `Accelerometer`/`Gyroscope` instances and call
   `getIsSupportedProperty()` many times in a loop, then construct a *new* instance and
   `Start()` it (or confirm it throws identically to a fresh-process baseline) —
   proving repeated probing doesn't leave the subsystem in some corrupted or
   permanently-mis-refcounted state that changes later behavior. Document in the test
   comment why exact ref-count assertion isn't possible here.

**Files:** `include/Microsoft/Devices/Sensors/Accelerometer.hpp`,
`src/Microsoft/Devices/Sensors/Accelerometer.cpp`,
`include/Microsoft/Devices/Sensors/Gyroscope.hpp`,
`src/Microsoft/Devices/Sensors/Gyroscope.cpp`,
`tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp`,
`tests/Microsoft/Devices/Sensors/GyroscopeTests.cpp`.

**Resolution (2026-07-04):** Implemented a simpler design than originally sketched
above — no new `liveInstanceSubsystemHolders_` counter needed. Added a small
`SensorSubsystemProbeGuard` RAII class (file-local, anonymous namespace) to both
`Accelerometer.cpp`/`Gyroscope.cpp`: its constructor unconditionally calls
`SDL_InitSubSystem(SDL_INIT_SENSOR)`, its destructor calls `SDL_QuitSubSystem()` if
that succeeded. `getIsSupportedProperty()` now holds one on the stack instead of
calling `EnsureSensorSubsystemInitialized()` directly. This is simpler than tracking a
separate "does a live instance already hold it" counter because SDL's own internal
ref-counting (the same mechanism Task P4-8 already established as the source of
truth) already correctly aggregates the probe's own init/quit pair with whatever any
live `Start()`'d instance separately holds — no new shared state needed, and no new
locking concerns. `include/*.hpp` files were **not** touched — the guard is entirely
internal to each `.cpp` file, needs no public or even class-private declaration.
Added `RepeatedSupportProbingDoesNotChangeSubsequentBehavior` to both
`AccelerometerTests.cpp`/`GyroscopeTests.cpp` (50 discarded probe constructions +
direct `getIsSupportedProperty()` calls, then confirms a fresh instance's
support/state is unchanged) — exact SDL ref-count isn't assertable via any public API,
so this proves the observable behavioral consequence instead. Verified: `CNA`/
`CnaTests` build clean; Devices-only `ctest` filter 172/172 passing (2 expected
skips); full `ctest` 1997 tests, 2 failures — both pre-existing, unrelated `EasyGL`/
`easy-gl` graphics-backend bugs (`EasyGL_MRT_TwoAttachments`,
`easy-gl-resource-smoke-tests`), newly *surfaced* (not caused) by this environment
unexpectedly now having a real GPU/display (`DISPLAY=:0`, `/dev/dri` present,
confirmed via `glxinfo`) for the first time in this project's history — every prior
session's "64 failures" baseline was actually "64 tests silently skipped as Not Run,
no GPU to run them against." Out of scope for this Devices-focused plan per
`CLAUDE.md`'s scope discipline; not touched.

---

## Task P5-2 — Replace `inFlightCallback_` bool with a callback counter, fix `SensorBase<T>`'s data race

**Problem:** see Audit findings 2 and 3 (the `SensorBase<T>` part).

**Steps:**
1. Replace `bool inFlightCallback_` with `int inFlightCallbackCount_` in both
   `Accelerometer` and `Gyroscope`. `SensorEventWatch()` increments it (under
   `mutex_`) for every instance in its dispatch snapshot *before* releasing the lock to
   call `ProcessSensorUpdateEvent()`, and decrements it (under `mutex_`) immediately
   after each call returns, notifying `callbackFinished_` whenever an instance's count
   reaches 0.
2. `Dispose(bool)` waits on `callbackFinished_.wait(lock, [this] { return
   inFlightCallbackCount_ == 0; })` instead of `!inFlightCallback_`.
3. Add `SensorBase<T>::mutex_` (a private `mutable std::mutex`, since
   `getCurrentValueProperty()`/`getIsDataValidProperty()` are `const`) guarding
   `currentValue_`/`isDataValid_`/`eventArgs_`. `setCurrentValueProperty()` locks,
   updates `currentValue_`/`eventArgs_`, copies what's needed for the event, unlocks,
   *then* raises `CurrentValueChanged` — never holding the lock across `Raise()`.
   `getCurrentValueProperty()`/`getIsDataValidProperty()` lock only long enough to
   read+copy the value. This is a private, internal-only change — no public API/ABI
   contract change beyond correctness.
4. Tests: a test-only hook that simulates two overlapping "in-flight" dispatches for
   the same instance (e.g. a `NOXNA` test seam that lets a test manually increment/
   decrement the counter alongside real `InjectSyntheticSensorUpdate()` calls, or a
   dedicated unit test against the counter's increment/decrement/notify behavior in
   isolation) without requiring actual concurrent SDL hardware events.

**Files:** `include/Microsoft/Devices/Sensors/SensorBase.hpp`,
`include/Microsoft/Devices/Sensors/Accelerometer.hpp`,
`src/Microsoft/Devices/Sensors/Accelerometer.cpp`,
`include/Microsoft/Devices/Sensors/Gyroscope.hpp`,
`src/Microsoft/Devices/Sensors/Gyroscope.cpp`, relevant test files.

**Resolution (2026-07-04):** Implemented as scoped, with two simplifications found
during implementation:
- `SensorBase<T>::eventArgs_` (the shared, reused `SensorReadingEventArgs<T>` member)
  was **removed entirely** rather than kept and locked — it was only ever touched from
  inside `setCurrentValueProperty()`, so replacing it with a local, per-call
  `SensorReadingEventArgs<T>` temporary removes the race on it completely (a concurrent
  dispatch on another thread can never mutate event args this call is still raising
  with, since each call now owns its own) without needing any additional locking
  around it.
- `getCurrentValueProperty()`'s return type changed from `const TSensorReading&` to
  `TSensorReading` (by value) — returning a reference into a mutex-protected member
  would let a caller hold a reference whose backing storage could be overwritten the
  instant the lock releases (a torn/inconsistent read, not memory-unsafe but still
  wrong). Confirmed zero existing call sites bind to a reference (`grep` across
  `tests/`/`src/`/`examples/` — every call site is `(void)x.getCurrentValueProperty()`
  or passes the temporary straight into a comparison), so this doesn't break anything,
  and it's arguably more faithful to the real WP7 API — `AccelerometerReading`/etc. are
  C# structs (value types), so the real `CurrentValue` getter already returns a copy,
  not a reference, at the C# language level.
- `inFlightCallback_` → `inFlightCallbackCount_`: implemented exactly as scoped in both
  `Accelerometer`/`Gyroscope`. `InjectSyntheticSensorUpdate()` (the `NOXNA` test hook)
  now also increments/decrements the same counter around its dispatch, so a handler
  that calls `Dispose()` on the same instance from within a synthetic-update-triggered
  callback is recognized identically to the real SDL event-watch path (needed for Task
  P5-3, next).
- Tests: rather than a synthetic test-only hook for "simulating" overlap, added
  `ConcurrentSyntheticUpdatesDoNotCrashAndDrainBeforeDispose` to both
  `AccelerometerTests.cpp`/`GyroscopeTests.cpp` — 8 real `std::thread`s each calling
  `InjectSyntheticSensorUpdate()` 10 times concurrently on the same instance (each
  handler invocation sleeps 1ms to widen the overlap window), confirming no crash, all
  80 events received, and a subsequent `Dispose()` completes (doesn't hang) once every
  thread finishes — exercising the real counter/lock/condition-variable machinery under
  genuine concurrent contention rather than a synthetic simulation.

Verified: `CNA`/`CnaTests` build clean; Devices-only filter 39/39 passing (2 expected
skips); full `ctest` 1999 tests, same 2 pre-existing unrelated `EasyGL`/`easy-gl`
failures as Task P5-1's new baseline — no regressions. The self-dispose deadlock
(Audit finding accepted-limitation) is **not yet fixed** — confirmed still present at
this commit (both header comments explicitly say so); Task P5-3 fixes it next.

---

## Task P5-3 — Remove the self-dispose deadlock accepted-limitation

**Problem:** `Accelerometer.hpp`/`Gyroscope.hpp`'s own doc comments on
`inFlightCallback_` document that a handler calling `Dispose()` on its own sender from
inside its own `CurrentValueChanged`/`ReadingChanged` callback deadlocks, and Task P4-2
explicitly accepted this rather than fixing it. The brief for this plan says not to
leave this as permanent.

**Design:** track which thread (if any) is currently dispatching a callback *for a
given instance*. `Dispose(bool)` only waits on the quiescence condition if the
disposing thread is *not* the thread currently dispatching this instance's callback;
if it *is* the same thread (the reentrant self-dispose case), skip the wait — the
call is already inside the one and only in-flight dispatch for this instance on this
thread, so there is nothing else to wait for, and the callback's own stack frame will
finish unwinding (decrementing the count) after `Dispose()` returns control back up to
it. Must not reintroduce the Task P3-4/P4-2 use-after-free: the instance's memory must
still not be freed while any *other* thread's dispatch for it is in flight.

**Steps:**
1. Add a per-instance `std::atomic<std::thread::id> dispatchingThreadId_` (or store it
   under `mutex_` alongside the count), set to the calling thread's id when
   `SensorEventWatch()`/`InjectSyntheticSensorUpdate()` begins dispatching to this
   instance, cleared (back to a sentinel "no thread") when that dispatch finishes.
2. `Dispose(bool)` compares `std::this_thread::get_id()` against the stored id under
   the lock: if equal, proceed without waiting (this thread's own in-flight count for
   itself doesn't block it); if different (or no thread dispatching), wait for
   `inFlightCallbackCount_ == 0` as before.
3. Apply to both `Accelerometer` and `Gyroscope`.
4. Tests: `Accelerometer`/`Gyroscope` — subscribe a `CurrentValueChanged` handler that
   calls `Dispose()` on the same instance from inside itself, then call
   `InjectSyntheticSensorUpdate()`; assert no deadlock (test itself would hang/timeout
   if it regressed) and no crash, and that a second `Dispose()` call afterward throws
   `ObjectDisposedException` as normal.

**Files:** `include/Microsoft/Devices/Sensors/Accelerometer.hpp`,
`src/Microsoft/Devices/Sensors/Accelerometer.cpp`,
`include/Microsoft/Devices/Sensors/Gyroscope.hpp`,
`src/Microsoft/Devices/Sensors/Gyroscope.cpp`, relevant test files.

**Resolution (2026-07-04):** Implemented a more general design than the single
`dispatchingThreadId_` sketched above, after tracing a correctness gap in that simpler
version: with only *one* stored thread id, a genuine cross-thread concurrent dispatch
(the scenario Task P5-2 already established as plausible) happening at the same moment
as a reentrant self-dispose could have the *other* thread's id clobber the
self-dispatching thread's own id in that single slot — so when the self-disposing
thread checked "is the stored id mine?", it could find someone else's id instead and
incorrectly decide it must wait, recreating the exact deadlock this task exists to fix
(traced by hand: thread A dispatching + reentrantly disposing, thread B also
dispatching concurrently to the same instance; if B's write to the single slot lands
after A's, A's own self-check sees B's id, not its own, and waits forever on a count
that only A's own return-from-Dispose() could ever complete draining).

Replaced `inFlightCallbackCount_` (the plain `int` from Task P5-2) with
`std::vector<std::thread::id> dispatchingThreadIds_` — one entry pushed per in-flight
dispatch (its `.size()` is the count, same as before), each entry independently
identifying which thread it belongs to. `Dispose(bool)`'s wait predicate now checks
`dispatchingThreadIds_.size() == (number of entries matching this thread)` — i.e. wait
only if some *other* thread's entry remains; proceed as soon as every remaining entry
(if any) belongs to the calling thread itself. Re-traced the same A/B scenario against
this design: B finishing and erasing its own entry leaves only A's — at that point A's
self-check (`size() == selfCount`) is satisfied and it proceeds correctly, regardless
of which thread's push happened to land in whatever slot. Applied identically to both
`Accelerometer`/`Gyroscope`, including their `InjectSyntheticSensorUpdate()` test hooks
(so a reentrant self-dispose from a synthetic-update-triggered handler is recognized
the same way a real SDL-event-triggered one would be).

Tests: added `DisposeFromWithinOwnCallbackDoesNotDeadlock` to both
`AccelerometerTests.cpp`/`GyroscopeTests.cpp` — a `CurrentValueChanged` handler that
calls `Dispose()` on its own sender, triggered via `InjectSyntheticSensorUpdate()`;
asserts no deadlock/crash (a regression here shows as a test-runner timeout, not a
clean assertion failure) and that dispatch-then-dispose leaves the instance correctly
disposed (a *second*, external `Dispose()` call afterward still throws
`ObjectDisposedException` exactly as for any other already-disposed instance).

Verified: `CNA`/`CnaTests` build clean; both new tests pass in well under a second
(no hang); Devices-only filter 41/41 passing (2 expected skips); full `ctest` 2001
tests, same 2 pre-existing unrelated `EasyGL`/`easy-gl` failures as the Task
P5-1/P5-2 baseline — no regressions. The header doc comments that used to describe
this as a permanently-accepted limitation (`Accelerometer.hpp`/`Gyroscope.hpp`) were
already rewritten in place during Tasks P5-2/P5-3's own edits; `NEXT.md`'s older
"accepted limitation" language is updated in Task P5-13 (this task only touches
`Microsoft::Devices` source/tests/this plan file, per scope discipline).

---

## Task P5-4 — Shared internal SDL sensor subsystem manager

**Problem:** `Accelerometer.cpp`/`Gyroscope.cpp` are near-duplicate implementations of
the same subsystem-lifetime/event-watch/quiescence machinery, differing only in
`SDL_SensorType`, the reading-conversion math, and which exception type to throw. This
duplication is also where Audit finding 3's `started_`/`state_`/`subsystemHeld_`
inconsistent-locking issues live — fixing it once in a shared class is more reliable
than fixing it twice.

**Design:** `Microsoft::Devices::Sensors::Detail::SdlSensorSubsystem` — one instance per
sensor *type* (an `Accelerometer`-flavored instance and a `Gyroscope`-flavored
instance, each a private static member of its respective class, not a single global
shared between both types). It owns: the balanced probe/instance init+quit from P5-1,
default-sensor discovery by `SDL_SensorType`, event-watch registration/removal, SDL
sensor-id bookkeeping, the started-instances list, and the counter+quiescence logic
from P5-2/P5-3. `Accelerometer`/`Gyroscope` each keep their own `state_`/`started_`
members (per-instance, class-specific) but access them only while holding the shared
manager's mutex (exposed via a small lock-guard-returning accessor), resolving the
inconsistent locking found in the audit.

**Steps:**
1. New `include/Microsoft/Devices/Sensors/Detail/SdlSensorSubsystem.hpp` +
   `src/Microsoft/Devices/Sensors/SdlSensorSubsystem.cpp`. `Detail` is an internal
   namespace — not part of the public API, not XNA-relevant, so no `NOXNA` tagging
   needed (it's never visible to a game including only the public sensor headers).
   Keeps SDL types (`SDL_Sensor*`, `SDL_SensorType`) out of `Accelerometer.hpp`/
   `Gyroscope.hpp` themselves (already true today via `void*`-typed statics; the new
   header continues that discipline for its own public-facing surface where
   practical, though as an internal-only header it may use SDL types directly since it
   is never included by a public CNA header).
2. Dispatch routing: each `Accelerometer`/`Gyroscope` instance registers itself with
   its class's shared manager via `this` + a static trampoline function matching a
   `void(*)(void* instance, std::int64_t sensorId, float, float, float)` signature
   (e.g. `&Accelerometer::ProcessSensorUpdateEventTrampoline`), avoiding the need for
   the manager itself to know about `Accelerometer`/`Gyroscope` types.
3. Migrate `Accelerometer`/`Gyroscope` to delegate to their manager instance for
   everything the manager now owns; keep the public API and all externally-observable
   behavior byte-for-byte identical.
4. Re-run every existing `AccelerometerTests`/`GyroscopeTests`/
   `SensorSubsystemOwnershipTests` suite after migration — this task must not change
   observable behavior, only internal structure.

**Files:** new `include/Microsoft/Devices/Sensors/Detail/SdlSensorSubsystem.hpp`, new
`src/Microsoft/Devices/Sensors/SdlSensorSubsystem.cpp`,
`include/Microsoft/Devices/Sensors/Accelerometer.hpp`,
`src/Microsoft/Devices/Sensors/Accelerometer.cpp`,
`include/Microsoft/Devices/Sensors/Gyroscope.hpp`,
`src/Microsoft/Devices/Sensors/Gyroscope.cpp`.

**Resolution (2026-07-04):** Implemented as scoped, with two deliberate deviations:

1. **No separate `.cpp` file.** `SdlSensorSubsystem<TSensor>` is a class template; every
   member function is defined inline in the header (standard practice for templates —
   the alternative, explicit instantiation in a `.cpp` for exactly the two known
   `TSensor`s, adds a circular-feeling dependency for no real benefit here, since this
   header is never included by any public CNA header anyway). No
   `src/Microsoft/Devices/Sensors/SdlSensorSubsystem.cpp` was created.
2. **Dispatch routing uses the template parameter directly, not a void*/function-pointer
   trampoline.** The brief suggested "each instance registers itself with the manager
   via `this` + a static trampoline function pointer matching a
   `void(*)(void*, ...)` signature." Since `SdlSensorSubsystem<Accelerometer>` and
   `SdlSensorSubsystem<Gyroscope>` are already distinct *types* (one template
   instantiation per concrete sensor class — never a single instance shared between
   the two, exactly as scoped), the event-watch trampoline can call
   `TSensor::ProcessSensorUpdateEvent()` directly by name, resolved at compile time —
   no `void*`-erasure or function-pointer indirection needed. Simpler and equally
   correct.

**Keeping SDL types out of public headers** (the brief's other stated goal) — already
true before this refactor (`void* g_sensor_`) — is preserved via a small twist:
`Accelerometer.hpp`/`Gyroscope.hpp` only *forward-declare* `Detail::SdlSensorSubsystem`
(a one-line `template <typename TSensor> class SdlSensorSubsystem;` inside a nested
`namespace Detail` block) and declare `static Detail::SdlSensorSubsystem<Accelerometer>&
GetSubsystem();` — a function *declaration* referencing an incomplete type doesn't need
the type's definition, so this compiles without `Accelerometer.hpp` ever including
`SdlSensorSubsystem.hpp` or any SDL header. The actual storage lives in
`Accelerometer.cpp`/`Gyroscope.cpp` as a function-local static inside `GetSubsystem()`'s
own definition (which *does* include the full `Detail/SdlSensorSubsystem.hpp`) — the
same "hide the real type behind an opaque handle" idiom this class already used for
`void* g_sensor_`, just applied to a whole subsystem object instead of one pointer.
Similarly, `TSensor::GetSdlSensorType()` returns a plain `int` (not `SDL_SensorType`),
so the enum type itself never needs to appear in either public header either.

Migrated `instanceCount_`, `sensor_`/`sensorId_`, `eventWatchRegistered_`,
`startedInstances_`, `mutex_`, and `callbackFinished_` into the shared
`SdlSensorSubsystem<TSensor>` instance (one per class, reached via each class's own
`GetSubsystem()`); `state_`/`started_`/`subsystemHeld_`/`dispatchingThreadIds_` remain
genuine per-instance members on `Accelerometer`/`Gyroscope` themselves (correctly so —
they're per-object data, not shared/class-level state), now consistently
lock-protected by the shared subsystem's `mutex_` everywhere they're touched from a
context that previously might have raced (Audit finding 3's `state_`/`subsystemHeld_`
inconsistent-locking observations from before this task — `Start()`'s subsystem-init
check-and-set is still deliberately outside the lock, matching the pre-refactor
design and Task P4-8's own instance-ownership pairing, which never needed the shared
lock in the first place since `subsystemHeld_` is per-instance, single-thread-written
data during normal, non-reentrant `Start()`/`Dispose()` usage).

Verified: `CNA` builds clean for `Accelerometer.cpp` alone first (before touching
`Gyroscope.cpp`, to isolate any template-instantiation errors to one file at a time),
then both together; a full top-level build (`cmake --build .`, every target including
`cna_demo_devices`) also builds clean — confirms nothing outside
`Microsoft::Devices::Sensors` referenced any of the now-removed private static methods
(`EnsureSensorSubsystemInitialized()`, `OpenDefaultAccelerometer()`/
`OpenDefaultGyroscope()`, `RegisterEventWatchIfNeeded()`/`UnregisterEventWatchIfNeeded()`,
`SensorEventWatch()`), which is expected since they were always private, but worth
confirming given how much surface area this refactor touched. Full `ctest`: 2001
tests (unchanged), same 2 pre-existing unrelated `EasyGL`/`easy-gl` failures as the
Task P5-1/P5-2/P5-3 baseline — every existing `AccelerometerTests`/`GyroscopeTests`/
`SensorSubsystemOwnershipTests` test (41 total, 2 expected skips) passes unchanged,
confirming this task's own requirement that behavior stay byte-for-byte identical.

---

## Task P5-5 — Document (and, if low-risk, improve) the event-thread model

**Steps:**
1. Document plainly (in `AUDIT.md` and this file) that `CurrentValueChanged`/
   `ReadingChanged` are raised synchronously from whatever thread calls
   `DispatchSensorReading()` — which, for the real SDL event path, is whatever thread
   SDL itself invokes the registered event watch on (platform/backend-dependent, see
   Audit finding 2), not guaranteed to be the game's main/Update thread.
2. Evaluate a `NOXNA` opt-in main-thread queue (e.g. `SetMainThreadDispatchEnabled(bool)`
   plus a `PumpMainThreadEvents()` call games would invoke from `Update()`) that
   buffers dispatched readings and replays their events on whatever thread calls the
   pump function, *without* changing the default (current) behavior — opt-in only, so
   no existing game's behavior changes unless it explicitly asks for this.
3. If implemented, update `NOXNA.md` with the new members and `AUDIT.md` with the
   thread-model clarification either way (implemented or not).

**Files:** `AUDIT.md`, `NOXNA.md`, and (only if the opt-in pump is implemented)
`Accelerometer.hpp/.cpp`, `Gyroscope.hpp/.cpp`, plus new tests.

---

## Task P5-6 — Test current-value semantics with synthetic updates

**Steps:**
1. Add a test proving `InjectSyntheticSensorUpdate()` actually updates
   `getCurrentValueProperty()`/`getIsDataValidProperty()` — this requires a way to make
   `isSupported_` true in a test without real hardware, since `getCurrentValueProperty()`
   throws otherwise (Audit finding 4). Add a `NOXNA` test-only
   `SetSupportedForTesting(bool)` hook (distinct from `SetStartedForTesting()`) that
   sets `isSupported_` directly, for both `Accelerometer` and `Gyroscope`.
2. Add a test proving that *without* calling the new `SetSupportedForTesting(true)`
   hook, `getCurrentValueProperty()` still throws `InvalidOperationException` on
   unsupported hardware even after `SetStartedForTesting(true)` +
   `InjectSyntheticSensorUpdate(...)` — closing Audit finding 4's documentation/test gap
   explicitly, on both classes.
3. Document the full contract in the header comments for `SetStartedForTesting()`
   (already present) and the new `SetSupportedForTesting()` (new) — cross-reference
   each other so the split is unambiguous to a future reader.

**Files:** `include/Microsoft/Devices/Sensors/Accelerometer.hpp`,
`src/Microsoft/Devices/Sensors/Accelerometer.cpp`,
`include/Microsoft/Devices/Sensors/Gyroscope.hpp`,
`src/Microsoft/Devices/Sensors/Gyroscope.cpp`,
`tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp`,
`tests/Microsoft/Devices/Sensors/GyroscopeTests.cpp`.

---

## Task P5-7 — Android axis conversion unit tests

**Steps:**
1. Refactor `ConvertAndroidAccelerometerToXnaLandscape()`/
   `ConvertAndroidGyroscopeToXnaLandscape()` (currently `static`, file-local,
   `#ifdef __ANDROID__`-only functions in `Accelerometer.cpp`/`Gyroscope.cpp`) into a
   pure, platform-independent function taking an explicit orientation enum (e.g. a new
   `NOXNA` `enum class AndroidSensorLandscapeOrientation { Rotation90, Rotation270 };`
   in a small internal header) instead of calling
   `SDL_GetCurrentDisplayOrientation()`/`SDL_GetPrimaryDisplay()` itself. The
   `#ifdef __ANDROID__` call site still queries SDL for the live orientation and maps
   it to the enum before calling the now-testable pure function.
2. Add unit tests (buildable and runnable on any platform, not `#ifdef __ANDROID__`-gated)
   covering all 4 combinations: accelerometer × `Rotation90`, accelerometer ×
   `Rotation270`, gyroscope × `Rotation90`, gyroscope × `Rotation270` — asserting the
   exact sign/axis mapping documented in each function's own doc comment.
3. Document the coordinate convention in `docs/devices-hardware-checklist.md` (already
   has a section on this; tighten it to reference the now-testable pure function and
   note the tests exist, distinct from physical hardware verification, which the
   checklist still separately requires).

**Files:** `src/Microsoft/Devices/Sensors/Accelerometer.cpp`,
`src/Microsoft/Devices/Sensors/Gyroscope.cpp`, new internal header for the shared
orientation enum/pure function if factored out to avoid duplication, new test file(s),
`docs/devices-hardware-checklist.md`.

---

## Task P5-8 — Native Android/iOS compass backend plan (documentation only)

No implementation — SDL3 has no compass/magnetometer API, and faking one from
gyroscope data would be dishonest (a gyroscope measures rotation *rate*, not absolute
heading; integrating it drifts unboundedly with no magnetometer to correct against).
See "Future native backend plan" below for the concrete Android/iOS design. Current SDL
backend (`Compass::getIsSupportedProperty()` returning `false`) is unchanged.

---

## Task P5-9 — Native Motion backend plan (documentation only)

Same reasoning as P5-8: `Motion` fuses accelerometer + compass + gyroscope (and
ideally a hardware rotation-vector sensor); it cannot be honestly implemented from
SDL3 alone since the compass component doesn't exist. See "Future native backend plan"
below. Add a test explicitly asserting `Motion::getIsSupportedProperty()` stays `false`
and `Start()` keeps throwing, so a future accidental partial implementation doesn't
silently start reporting "supported" without the plan's Android/iOS backend actually
landing.

---

## Task P5-10 — GPS/location decision document (documentation only)

`Microsoft.Devices.Sensors`'s real WP7 API surface has no GPS/location member —
`System.Device.Location` (`GeoCoordinateWatcher`, `GeoCoordinate`,
`GeoPositionChangedEventArgs<T>`) is a *separate* WP7 namespace/assembly. New
`docs/location-future-plan.md` documents that a future compatibility layer for this
belongs under a `System::Device::Location`-shaped namespace, never under
`Microsoft::Devices::Sensors`, with a sketch of the Android (`LocationManager` for
AOSP/no-GMS, optional Google Fused Location Provider as a separate opt-in backend) and
iOS (`CoreLocation`) native paths. No implementation in this task.

---

## Task P5-11 — `VibrateController` cleanup and Android vibration honesty pass

**Steps:**
1. Add RAII cleanup for `g_haptic` — see Audit finding 6 for why this is now confirmed
   safe (no `SDL_Quit()` anywhere in this codebase to race against). Add
   `~VibrateController()` (declared in the header, defined in the `.cpp`) that locks
   `g_mutex`, closes `g_haptic` if non-null, and releases the `SDL_INIT_HAPTIC`
   subsystem hold if this process ever acquired one.
2. Replace `EnsureHapticSubsystemInitialized()`'s `SDL_WasInit()` guard with an
   explicit `g_subsystemHeld` file-static bool (mirroring the sensor classes'
   `subsystemHeld_` principle, simplified since `VibrateController` is a true
   process-wide singleton with one call path) — init at most once, tracked explicitly,
   released exactly once in the new destructor.
3. Confirm (already true, re-verified in the audit) that `getIsSupportedProperty()`/
   `getDeviceNameProperty()`'s temporarily-opened probe *device* handles are still
   properly closed (`SDL_CloseHaptic()` when `openedTemporary`) — unaffected by this
   task, no leak there.
4. Add tests: repeated `getIsSupportedProperty()`/`getDeviceNameProperty()` probe calls
   in a loop, and repeated `Start()`/`Stop()`/`StartLeftRight()` sequences, confirming
   no behavior change/crash — mirroring Task P4-9's existing
   `ConcurrentCallsFromMultipleThreadsDoNotCrashOrDeadlock` in spirit but for repeated
   sequential calls specifically probing subsystem-lifetime robustness.
5. Document Android vibration status honestly in `docs/devices-hardware-checklist.md`
   (already has a section on this from Task P4-13) — explicitly mark it **unverified**
   (no real Android hardware run in any session to date), not done, and state plainly
   that SDL3's own Android haptic backend is what's relied upon (confirmed by reading
   `VibrateController.cpp`'s own code comment, not independently verified against a
   real device).

**Files:** `include/Microsoft/Devices/VibrateController.hpp`,
`src/Microsoft/Devices/VibrateController.cpp`,
`tests/Microsoft/Devices/VibrateControllerTests.cpp`,
`docs/devices-hardware-checklist.md`.

---

## Task P5-12 — Build reproducibility cleanup (`docs/devices-build.md`)

**Steps:**
1. New `docs/devices-build.md`: desktop debug build commands (configure, build `CNA`,
   build `CnaTests`), the Devices-only `ctest`/`--gtest_filter` invocation, the exact
   Android cross-compile command (NDK toolchain file path, ABI, platform level — as
   actually run in Task P4-11/this plan's P5-14), and an explicit iOS-still-blocked
   note (no toolchain in this Linux container).
2. Only state a command "works" if it was actually run in a session (this one or an
   earlier one whose result was independently re-verified) — no copy-pasted
   aspirational commands presented as tested.
3. Note the `third_party/SDL` submodule/vendoring setup this repo actually uses (check
   `cmake/ThirdPartySDL.cmake`/`.gitmodules` for the real mechanism before writing this
   down — don't guess).

**Files:** new `docs/devices-build.md`.

---

## Task P5-13 — Update status docs accurately

**Steps:**
1. `NEXT.md`: remove/qualify "complete"/"hardened"/"real thread-safety"/"verified"
   language that this plan's audit found to be inaccurate at the time it was written
   (Task P4-8's own regression is the clearest example — it *was* real thread-safety
   work, but it also introduced a new leak in the same commit, undetected until now).
   Replace with the accurate, layered breakdown: API surface implemented vs. SDL
   runtime implemented vs. native mobile backend missing (Compass/Motion) vs. hardware
   manually verified vs. not verified.
2. `AUDIT.md`/`NOXNA.md`: same accuracy pass for anything Devices-related.
3. `docs/devices-hardware-checklist.md`: keep, tighten per Tasks P5-7/P5-11's updates.

**Files:** `NEXT.md`, `AUDIT.md`, `NOXNA.md`, `docs/devices-hardware-checklist.md`.

---

## Task P5-14 — Final test run and report

**Steps:**
1. Configure, build `CNA`, build `CnaTests`, run the Devices test filter, run full
   `ctest`, in this session, on current `HEAD`.
2. Re-run the Android cross-compile (`cmake-build-android`, per Task P4-11/
   `docs/devices-build.md`'s documented command) against this plan's changes
   specifically — Audit finding 7 requires this, since P4-11's own verification
   predates this plan's edits.
3. Append a `Resolution` section to the bottom of this file: files changed, tests
   added, exact commands run, any failures/skips (with reasons), and remaining risks
   (anything this plan could not verify in this environment).

---

## Future native backend plan

*(Written as part of Tasks P5-8/P5-9 — no implementation, planning only.)*

### Compass — Android

- JNI/Kotlin bridge from CNA's native Android glue code (wherever the project's
  existing Android SDL activity/glue lives) into `android.hardware.SensorManager`.
- Register listeners for `TYPE_MAGNETIC_FIELD` (magnetometer) and `TYPE_ACCELEROMETER`
  (already available via SDL directly, but Android's own tilt-compensation math wants
  both together) — optionally `TYPE_ROTATION_VECTOR` if available, which already
  fuses magnetometer+accelerometer(+gyroscope) into an orientation quaternion in one
  sensor, reducing the amount of fusion math CNA would need to write itself.
- Map Android's `SensorManager.getOrientation()`/rotation-vector output to
  `CompassReading.MagneticHeading`/`TrueHeading` (true heading needs the device's
  geomagnetic declination for the current location — Android's `GeomagneticField`
  class provides this, but needs a location fix, tying this loosely to Task P5-10's
  location layer for full accuracy; without a location fix, `TrueHeading` could
  fall back to `MagneticHeading` with a documented caveat).
- `SensorManager`'s `SENSOR_STATUS_*` accuracy constants map to
  `CompassReading.HeadingAccuracy` (degrees) and to firing `Compass::Calibrate`
  (`CalibrationEventArgs` carries no data itself, matching the real WP7 API — the
  *reading*'s own `HeadingAccuracy` field is where accuracy actually lives, not the
  event).

### Compass — iOS

- `CLLocationManager`'s heading APIs (`startUpdatingHeading`,
  `CLLocationManagerDelegate.locationManager(_:didUpdateHeading:)`) deliver
  `CLHeading` (`magneticHeading`, `trueHeading`, `headingAccuracy`,
  `x`/`y`/`z` raw magnetometer field) — a near 1:1 mapping to `CompassReading`'s own
  fields.
- Requires location permission authorization (`CLLocationManager.requestWhenInUseAuthorization()`
  or similar) even though only heading is used, not GPS position — an iOS-specific
  quirk worth documenting clearly so it isn't mistaken for a P5-10 location-layer
  dependency.
- `CLHeading`'s own accuracy/calibration display (`CLLocationManager`'s
  `shouldDisplayHeadingCalibration` delegate callback) maps naturally to
  `Compass::Calibrate`.

### Motion — Android

- `TYPE_ROTATION_VECTOR` or `TYPE_GAME_ROTATION_VECTOR` (the latter doesn't use the
  magnetometer, avoiding compass-availability coupling for the orientation component,
  at the cost of no true-north reference) for `MotionReading.Attitude`.
- `TYPE_GRAVITY` for `MotionReading.Gravity`, `TYPE_LINEAR_ACCELERATION` for
  `MotionReading.DeviceAcceleration` (both are Android "virtual" sensors already
  separating gravity from linear acceleration, matching XNA's own split of these two
  fields — no manual high-pass/low-pass filtering needed on CNA's side).
- `TYPE_GYROSCOPE` (already available via CNA's own `Gyroscope` class) for
  `MotionReading.DeviceRotationRate`.

### Motion — iOS

- `CMMotionManager.deviceMotion` (`CMDeviceMotion`) is a near-complete match: `.attitude`
  (`CMAttitude`, quaternion/rotation-matrix/Euler available) →
  `MotionReading.Attitude`; `.gravity` → `MotionReading.Gravity`;
  `.userAcceleration` → `MotionReading.DeviceAcceleration`; `.rotationRate` →
  `MotionReading.DeviceRotationRate`. Essentially a direct field-for-field mapping,
  the cleanest of the four native-backend sketches in this plan.

Both `Compass`/`Motion` SDL backends remain `NotSupported` stubs until a native backend
from this section is actually scoped and implemented as its own future plan — not part
of this plan's changes.

---

## Verification checklist (apply to every source-touching task above)

- Build `cmake --build cmake-build-debug --target CNA` then `--target CnaTests`.
- Run the specific new/changed test suite via `--gtest_filter`.
- Run full `cd cmake-build-debug && ctest --output-on-failure` and confirm no new
  regressions beyond the current headless `EasyGL_*`/`Vulkan_*`/`Bgfx_*` baselines
  (re-check current counts first, since prior sessions found them shifting slightly as
  new test files are added).
- Update `NEXT.md` (status/recent-changes/known-bugs sections) after each task, per
  `CLAUDE.md`'s "always commit after finishing a task" rule.
- Commit each task separately, staged by explicit filename, referencing the task ID.
