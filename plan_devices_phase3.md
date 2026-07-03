# plan_devices_phase3.md — Devices Follow-up: Behavioral Correctness, Thread Safety, Test Depth

## Context

`plan_devices.md` (31 tasks) and `plan_devices_phase2.md` (17 tasks) are both complete —
`Microsoft::Devices::Sensors` + `VibrateController` match the documented WP7 API shape,
have full test suites, build cleanly under `EASYGL`/`VULKAN`/`BGFX`, and passed a
`CHECKLIST.md` compliance spot-check with no functional bugs found.

This plan comes from a **third, deeper pass** (2026-07-02) specifically hunting for
remaining correctness/behavior gaps, concurrency issues, and test-depth gaps that the
earlier, broader passes weren't specifically looking for. Three independent research
passes fed this plan:

1. A second API-completeness re-audit against archived Microsoft Learn "previous-versions"
   pages, going past what `plan_devices_phase2.md` Task P2-2 already checked (unverified
   items, exact property visibility/behavior, not just member presence/absence).
2. A line-by-line implementation review of every `.cpp` under `src/Microsoft/Devices/`,
   cross-checked against the vendored SDL3 source directly (`third_party/SDL/src/haptic/`)
   for behavior claims, looking for real defects (not style — `CHECKLIST.md` compliance
   already covers that, Task P2-5).
3. A test-coverage gap analysis against `CHECKLIST.md`'s test requirements, looking for
   edge cases missed on methods that already have *some* coverage, not just methods with
   zero coverage.

Two real bugs came out of this (Tasks P3-1, P3-3), one design gap with a real physical
consequence (Task P3-4), one judgment-call finding that needs a decision rather than a
forced fix (Task P3-2), and a cluster of test-coverage gaps (Tasks P3-5 through P3-10).

Work through phases in order, verifying build + tests after each task, same as
`plan_devices_phase2.md`. Every source-touching task needs: build `CNA` + `CnaTests`,
run the affected suite via `--gtest_filter`, then full `ctest --output-on-failure` to
confirm no regression beyond the pre-existing headless `EasyGL_*` baseline (64 failures
as of this writing — re-check the current count, since it may drift), and an update to
`NEXT.md` (status/recent-changes/known-bugs sections) after each task.

---

## Phase 1: Confirmed behavioral gaps vs. the real WP7 API

### Task P3-1 — `SensorBase<T>::CurrentValue` should throw when the sensor isn't supported — ✅ Done (2026-07-03)

**Gap (confirmed via archived MSDN `hh239261`, high confidence — direct primary
source):** the real `SensorBase<T>.CurrentValue` property doc states explicitly: *"If
the sensor is not present, a `System.InvalidOperationException` is thrown when you
access this property. Before accessing this property, use the `IsSupported` property...
to determine if the device is present."* CNA's
`SensorBase<T>::getCurrentValueProperty()` (`include/Microsoft/Devices/Sensors/SensorBase.hpp`)
unconditionally returns `currentValue_` — it never throws, regardless of whether the
underlying sensor is supported.

**Design note:** `SensorBase<T>` is a shared template base with no access to a derived
class's static `getIsSupportedProperty()` (each sensor class defines its own; there's no
virtual/polymorphic hook today). A reasonable approach: add a `protected` instance flag
(e.g. `bool isSupported_`) to `SensorBase<T>`, plus a `protected` setter the derived
class's constructor calls once, based on that class's own static
`getIsSupportedProperty()` result — mirrors the existing pattern where every derived
constructor already does `state_ = getIsSupportedProperty() ? SensorState::Initializing
: SensorState::NotSupported;`. `getCurrentValueProperty()` then throws
`System::InvalidOperationException` (already exists in `sharp-runtime`,
`System/InvalidOperationException.hpp`) if `!isSupported_`.

**Steps:**
1. Add the `isSupported_` flag (or equivalent) and its setter to `SensorBase<T>` in
   `include/Microsoft/Devices/Sensors/SensorBase.hpp`.
2. Update `getCurrentValueProperty()` (both the `const&` getter — check if there's only
   one overload) to throw `System::InvalidOperationException` when unsupported, with a
   message pointing callers at `IsSupported`.
3. Update all 4 derived constructors (`Accelerometer`, `Compass`, `Gyroscope`, `Motion`)
   to set the new flag from their own `getIsSupportedProperty()` result.
4. Add `#include "System/InvalidOperationException.hpp"` to `SensorBase.hpp`.
5. Tests: for each of the 4 sensor classes, on an unsupported instance, assert
   `getCurrentValueProperty()` throws `System::InvalidOperationException`; on a
   supported instance (branch on live `getIsSupportedProperty()`, same pattern as every
   other hardware-dependent test in this namespace), assert it does NOT throw and
   returns the expected default-constructed reading before any data has arrived.
6. Double-check this doesn't break any existing test that currently calls
   `getCurrentValueProperty()` on an unsupported stub (`Compass`/`Motion` tests) expecting
   a benign default value — those tests will need updating to expect the throw instead.

**Resolution (2026-07-03):** Implemented exactly as designed above. Added
`bool isSupported_` (default `false`) + `protected void setIsSupportedProperty(bool)` to
`SensorBase<T>` (`include/Microsoft/Devices/Sensors/SensorBase.hpp`); added
`#include "System/InvalidOperationException.hpp"`; `getCurrentValueProperty()` now throws
`System::InvalidOperationException` when `!isSupported_`. All 4 derived constructors
(`Accelerometer`, `Compass`, `Gyroscope`, `Motion`) now capture their own
`getIsSupportedProperty()` result once and call `setIsSupportedProperty(supported)` right
after computing `state_`. Step 6 turned out to be moot: grepped the whole `tests/`
tree first — no existing test called `getCurrentValueProperty()` at all, so there was
zero test churn from the new throw. Added 6 new tests: `Accelerometer`/`Gyroscope` each
got a throws-when-unsupported + does-not-throw-when-supported pair (branching on the live
`getIsSupportedProperty()` result, same pattern as their other hardware-conditional
tests — the does-not-throw case is `GTEST_SKIP()`ped in this headless environment);
`Compass`/`Motion` (permanent `NotSupported` stubs) each got a single
unconditional-throw test. Verified: `CNA` + `CnaTests` build clean (no warnings after
void-casting the `[[nodiscard]]` return inside `EXPECT_THROW`/`EXPECT_NO_THROW`); full
`ctest` — 1970 tests total (up from 1964), 97% passing, same 64 pre-existing headless
`EasyGL_*` failures, zero regressions.

### Task P3-2 — Decide: should the five reading types' setters be `internal`-equivalent?

**Finding (confirmed via archived MSDN `ff239107` for `AccelerometerReading.Acceleration`
and `hh239090` for `GyroscopeReading.RotationRate`, high confidence — same pattern
independently confirmed on two different classes):** both are `{ get; internal set; }` in
the real API — settable only from within `Microsoft.Devices.Sensors.dll`, not by
application code. The same pattern almost certainly applies to `CompassReading`,
`AttitudeReading`, and `MotionReading`'s fields too (not individually re-checked, but the
pattern held twice independently). This project's own `CLAUDE.md` Visibility Mapping
table says `internal` → "`private`, `protected`, detail/internal namespace, or omit
entirely" — CNA's five reading types (`AccelerometerReading`, `CompassReading`,
`GyroscopeReading`, `AttitudeReading`, `MotionReading`) instead expose fully **public**
`setXProperty()` on every field.

**This is NOT a drop-in fix — read before doing anything:** `tests/Microsoft/Devices/Sensors/*ReadingTests.cpp`
(and `AccelerometerReadingEventArgsTests.cpp`) directly call `setXProperty()` on these
types as the `CHECKLIST.md`-mandated way to test getter/setter pairs
(e.g. `AccelerometerReadingTests.cpp:31: r.setAccelerationProperty(v);`). C++ has no
assembly-scoped `internal` — the closest mechanism is `friend`, which is *more*
restrictive than C#'s `internal` (friend grants access to specifically-named classes
only, not "anything in this library"). Making these setters `private` + `friend
Accelerometer`/etc. would break every existing property-pair test that constructs a
reading and mutates it directly — those tests would need rewriting to exercise mutation
only through the owning sensor class (e.g. drive a real/simulated sensor update and
assert on the resulting `CurrentValue`), which is a meaningfully different and more
awkward testing style.

**Options — pick one and act on it, don't leave this open indefinitely:**
- **(A) Recommended: document as an accepted C++ deviation, no code change.**
  `CHECKLIST.md` already has a "Known acceptable C++ deviations from FNA/XNA" table with
  precedent for skipping strict fidelity where C++ has no analogous mechanism (e.g.
  `Equals(object obj)` override omitted — "No `object` base in C++ structs/value types").
  Add a row there (or a note in `AUDIT.md`'s `Microsoft::Devices::Sensors` section) for
  this specific case: public setters where real API has `internal set`, reasoning above.
  Zero test churn, zero behavior change, matches how this project has handled the same
  class of C++/C# visibility mismatch before.
- **(B) Make them `private` + `friend`.** Only pursue this if maintainability/API-surface
  correctness is judged more important than the test-rewrite cost. If chosen: for each of
  the 5 reading types, move `setXProperty()` under `private:`, add `friend class
  <OwningSensorClass>;` (`Accelerometer`, `Compass`, `Gyroscope`, `Motion` respectively),
  then rewrite every test that currently calls `setXProperty()` directly — likely via a
  test-only factory function in the owning sensor class, or by constructing the reading
  through the sensor's real update path where feasible.

### Task P3-3 — `AccelerometerFailedException : SensorFailedException` inheritance — now fully confirmed, no action needed

Already correctly implemented; this task just closes the loop on `plan_devices_phase2.md`
Task P2-16's "unverified inheritance assumption" note. Confirmed directly via
`SensorFailedException`'s own MSDN Inheritance Hierarchy listing (`hh239255`) this pass.
Update `AUDIT.md`'s `AccelerometerFailedException` row to drop the "unverified" caveat —
this is now a documentation-only touch-up, not a code change. (Numbered here so the
`AUDIT.md` update doesn't get lost, not because it needs its own build/test cycle.)

---

## Phase 2: Thread safety

### Task P3-4 — Guard `Accelerometer`/`Gyroscope`'s shared sensor state against the SDL event-watch callback running off-thread — ✅ Done (2026-07-03)

**Bug (confirmed, high severity):** `Accelerometer.cpp` and `Gyroscope.cpp` (identical
duplicated pattern in both) register a `SensorEventWatch` static callback via
`SDL_AddEventWatch()`. That callback iterates the class-static
`std::vector<Accelerometer*> startedInstances_` (or `Gyroscope*`) and calls
`ProcessSensorUpdateEvent()` on each entry. The same vector — plus `g_sensor_`,
`g_sensorId_`, `eventWatchRegistered_` — is read/written with **zero synchronization**
by `Start()` (push_back), `Stop()` (erase), and `Dispose(bool)` (clear), all called from
the application/main thread.

`third_party/SDL/include/SDL3/SDL_events.h`'s own doc comment for `SDL_AddEventWatch()`
states explicitly: *"Be very careful of what you do in the event filter function, as it
may run in a different thread!"* This is SDL's stated contract, not a hypothetical. If
any platform backend ever posts `SDL_EVENT_SENSOR_UPDATE` from a background/polling
thread (plausible on Android, where sensor callbacks commonly arrive off the caller's
thread — unverified here since Android hasn't been build-tested this whole project, no
NDK in this dev container), this is a genuine data race: `startedInstances_` mutated on
the main thread (`Stop()`'s `erase()`, `Dispose()`'s `clear()`, both of which can
reallocate/invalidate the vector) while the event-watch thread concurrently iterates the
same vector — iterator invalidation or a use-after-free if an instance is destructed
while the watch thread is mid-callback on a stale pointer. A secondary consequence:
`CurrentValueChanged`/`ReadingChanged` handlers could fire on a non-main thread without
warning, surprising game code that assumes XNA-style single-threaded event delivery.

**Steps:**
1. Add a `std::mutex` (file-static or a class-static member, matching the existing
   file-static state style) to both `Accelerometer.cpp` and `Gyroscope.cpp`, guarding
   `startedInstances_`, `g_sensor_`, `g_sensorId_`, `eventWatchRegistered_`.
2. Lock it around every read/write site: `Start()`, `Stop()`, `Dispose(bool)`, and
   `SensorEventWatch()`'s iteration (`ProcessSensorUpdateEvent()` itself doesn't need to
   be called under the lock — copy the instance list, or the specific instance pointer,
   out while holding the lock, then call out to `ProcessSensorUpdateEvent()` unlocked, to
   avoid holding a lock across an event-handler callout that might re-enter `Start()`/
   `Stop()`).
3. Alternative if a mutex is judged overkill for this codebase's actual threading model:
   explicitly document (in both `.cpp` files and `NEXT.md`) that this codebase currently
   assumes SDL never delivers `SDL_EVENT_SENSOR_UPDATE` off the calling thread on any
   currently-supported platform, and that this is unverified against the real
   Android/iOS backends. Pick the mutex fix unless there's a concrete reason not to pay
   the (tiny) lock cost — this is exactly the kind of latent bug that's cheap to fix now
   and expensive to debug later once it manifests as an intermittent Android crash.
4. No behavior change on any currently-tested (single-threaded, headless, no real sensor
   hardware) path — this fix should not require new tests beyond confirming the existing
   suites still pass, since correctness here can't be meaningfully unit-tested without
   real concurrent hardware events. Note that explicitly in the resolution instead of
   inventing a synthetic concurrency test that wouldn't actually exercise the real race.

**Resolution (2026-07-03):** Went with the mutex (step 2's fix), not the "document and
accept" alternative (step 3) — cheap to add, and this is exactly the kind of latent bug
that's expensive to debug later. Added a `static std::mutex mutex_` class-static member to
both `Accelerometer`/`Gyroscope` (declared in the `.hpp`, defined in the `.cpp`, matching
the existing static-member style rather than an anonymous-namespace file-static). Locked
around: `Start()`'s entire `g_sensor_`/`startedInstances_`/`RegisterEventWatchIfNeeded()`
block; `Stop()`'s entire `startedInstances_`/`UnregisterEventWatchIfNeeded()` block;
`Dispose(bool)`'s `instanceCount_`/`startedInstances_`/`g_sensor_`/`g_sensorId_` block
(acquired **after** calling `Stop()`, not around it — `Stop()` takes its own lock
internally, and `std::mutex` isn't recursive, so nesting would deadlock);
`SensorEventWatch()`'s iteration (copies `startedInstances_` into a local
`std::vector` snapshot under the lock, then iterates and calls
`ProcessSensorUpdateEvent()` on each **unlocked**, exactly as step 2 specifies, so a
`CurrentValueChanged`/`ReadingChanged` handler that re-enters `Start()`/`Stop()` can't
deadlock); and `ProcessSensorUpdateEvent()`'s reads of `g_sensor_`/`g_sensorId_` (snapshot
into locals under a short-lived lock, then use the locals — this wasn't explicitly named
in step 2's bullet list but is one of the two variables step 1 says the mutex must guard).

**Known residual gap, judged out of scope for this task:** the per-instance `started_`
member (not one of the four statics the task named) is still read by
`ProcessSensorUpdateEvent()` without synchronization, and in the (still narrow) window
where the event thread has already copied a pointer out of `startedInstances_` but the
object is concurrently destroyed by `Dispose()` on the app thread, that copied pointer can
dangle — the mutex closes the *iterator-invalidation*/*shared-static-corruption* class of
bug this task targeted, not a full ownership-safety guarantee (that would need
`shared_ptr`/`weak_ptr`-based lifetime tracking, a materially bigger change than this task
scoped). Not fixed here; flag if it ever becomes a priority.

Verified: `CNA` + `CnaTests` build clean, no new warnings. Full `ctest` — 1970 tests
(unchanged from Task P3-1, no new tests added per step 4's guidance), 97% passing, same 64
pre-existing headless `EasyGL_*` failures, zero regressions.

---

## Phase 3: `VibrateController` effect conflict

### Task P3-5 — Make `Start()`/`Start(duration,intensity)`/`StartLeftRight()` mutually exclusive

**Gap (confirmed via reading `third_party/SDL/src/haptic/SDL_haptic.c`):**
`SDL_InitHapticRumble()` (used by `Start()`/`Start(duration,intensity)`) allocates its
own dedicated `haptic->rumble_id` effect slot (`SDL_haptic.c` ~line 817-847), completely
independent from the `SDL_HapticEffectID` this codebase tracks in its own
`g_leftRightEffectId` for `StartLeftRight()`. `Stop()` correctly stops both. But neither
`Start()` variant stops `StartLeftRight()`'s effect before starting, and vice versa.
Concrete failure scenario: `Start(TimeSpan::FromSeconds(2))` then, mid-playback,
`StartLeftRight(1.0f, 1.0f, TimeSpan::FromSeconds(2))` — both effects run simultaneously
on the same physical motor(s), a state the real WP7 API (one `Start`/`Stop` pair, no
layering concept) never allows.

**Steps:**
1. Before starting the plain rumble path (`Start`/`Start` with intensity), if
   `g_leftRightEffectId >= 0`, stop+destroy it first (same cleanup already done at the
   top of `Stop()` and at the top of `StartLeftRight()`'s re-entry path — factor into a
   shared private helper if that avoids tripling the same 3 lines).
2. Before starting `StartLeftRight()`'s effect, if the plain-rumble path is currently
   active, stop it (`SDL_StopHapticRumble(g_haptic)` — confirm this exact function name
   exists in the vendored `SDL_haptic.h`; if not, `SDL_StopHapticEffect(g_haptic,
   haptic-internal-rumble-id)` isn't directly reachable since `rumble_id` is private to
   SDL's `SDL_Haptic` struct — `SDL_StopHapticEffects(g_haptic)` (plural, stops
   everything) may be the only available blanket tool; if so, use that, accepting it also
   stops nothing incorrectly since nothing else should be running at that point anyway
   given step 1's symmetric cleanup).
3. Tests: add a case exercising `Start()` then `StartLeftRight()` then confirm `Stop()`
   still cleans up without throwing (can't assert on actual simultaneous-motor-state
   headless, but can assert the call sequence itself is safe and no resource leaks/double
   frees occur — run under a leak-checking build if one is easily available, otherwise
   just confirm no crash/throw across the sequence).

---

## Phase 4: Test coverage — systemic gaps (highest bug-catching value)

### Task P3-6 — Add `CurrentValueChanged` subscription coverage to all 4 sensor classes

**Gap:** the primary, non-deprecated event in the entire namespace
(`SensorBase<T>::CurrentValueChanged`) has **zero** test coverage on `Accelerometer`,
`Compass`, `Gyroscope`, or `Motion` — not even a "subscribing doesn't crash" check
(`Accelerometer.ReadingChanged` got exactly this treatment in Task P2-15; the modern,
non-legacy event never did).

**Steps:** for each of the 4 sensor classes' test files, add a test subscribing a lambda
to `CurrentValueChanged` and asserting the subscription itself doesn't throw, plus (where
the class is genuinely hardware-supported, branch on live `getIsSupportedProperty()`)
`Start()`/`Stop()` still behave correctly with a subscriber attached — same
headless-safe pattern already used for `Accelerometer.ReadingChanged` and
`VibrateController`'s untestable-hardware-behaviors. Document, same as those precedents,
that actually observing the event *fire* needs real hardware this environment doesn't
have.

### Task P3-7 — Add `GetTypeName()` tests to `Compass`, `Gyroscope`, `Motion`

**Gap:** `Accelerometer` got a `GetTypeName()` test in Task P2-4 (specifically to catch
the `::`-vs-`.` convention bug found there). `Compass`, `Gyroscope`, and `Motion` all
correctly override `GetTypeName()` via `SensorBase<T>`/`GetTypeNameHPP()` — but **none of
their test files verify it**. This is exactly the kind of bug Task P2-4 fixed for
`Accelerometer` (the dot-vs-colon `GetTypeNameCPP` naming-convention mistake) that
nothing would catch here if it existed.

**Steps:** add one `GetTypeName` test per class (`CompassTests.cpp`, `GyroscopeTests.cpp`,
`MotionTests.cpp`), asserting the expected `"Microsoft.Devices.Sensors.<ClassName>"`
string, mirroring `AccelerometerTests.cpp::GetTypeName`.

### Task P3-8 — Add `Calibrate` event subscription coverage to `Compass` and `Motion`

**Gap:** `Compass.Calibrate`/`Motion.Calibrate` (shared `CalibrationEventArgs` type) are
never subscribed to in either class's test file, not even a no-crash check.

**Steps:** add a test per class subscribing a lambda to `Calibrate` and asserting no
throw, matching the pattern used for `ReadingChanged`/`CurrentValueChanged` (Task P3-6).
Since both `Compass` and `Motion` are permanent `NotSupported` stubs, `Calibrate` will
never actually fire in this environment regardless of hardware — document that plainly
rather than pretending otherwise.

### Task P3-9 — Verify the instance-count decrement, not just the instance-count cap, on all 4 sensor classes

**Gap:** every sensor class's `EleventhSimultaneousInstanceThrows` test proves the
10-instance cap triggers, but none of them prove `instanceCount_` actually decrements on
`Dispose()` — i.e. dispose one of the 10, then confirm an 11th construction now succeeds.
This is the one piece of the limit logic nothing currently verifies.

**Steps:** for each of the 4 sensor classes, add a test: construct 10 instances, dispose
one, construct one more (should succeed, not throw), matching the existing
`EleventhSimultaneousInstanceThrows`'s construction-loop style.

### Task P3-10 — Add "different objects → different hash" coverage for `GetHashCode()` across all reading/event-args types

**Gap:** `CHECKLIST.md` explicitly requires both "equal objects → equal hash" AND
"different objects → (typically) different hash." Every `GetHashCode()` test in this
namespace (`AccelerometerReadingTests`, `CompassReadingTests`, `GyroscopeReadingTests`,
`AttitudeReadingTests`, `MotionReadingTests`, `AccelerometerReadingEventArgsTests`,
`CalibrationEventArgsTests`) only tests the equal-hash case — the different-hash case is
missing everywhere, a systemic omission across all 6+ files, not an isolated gap.

**Steps:** for each of the 6 reading/event-args test files, add a
`GetHashCodeConsistency`-adjacent test asserting two *unequal* instances produce
different hashes (accept, per `CHECKLIST.md`'s own "(typically)" hedge, that a genuine
collision is possible in principle — if one is hit by chance during implementation, pick
different field values rather than treating it as a bug).

---

## Phase 5: Test coverage — smaller gaps (bundle into one pass per class)

### Task P3-11 — Remaining smaller coverage gaps

Bundle these into whichever class's test file they touch — no need for one task per gap:

- **`Stop()`-after-`Dispose()`** — untested on all 4 sensor classes (only `Start()`- and
  `Dispose()`-after-`Dispose()` are tested today). Each has the
  `ObjectDisposedException::ThrowIf` guard already in the implementation; add the test.
- **`Start()`-then-`Dispose()`** on `Accelerometer` (started-then-disposed cleanup path,
  as opposed to disposed-while-never-started) — add alongside the above.
- **Multi-field inequality coverage** — `AttitudeReading` (6 fields, only `Pitch` varied
  in the inequality test), `MotionReading` (5 fields, only `DeviceAcceleration` varied),
  `CompassReading` (5 fields, only `HeadingAccuracy` varied) each only vary one field for
  their inequality test. Add cases varying at least one more field independently per
  class (`AccelerometerReadingEventArgsTests`/`GyroscopeReadingTests` already do this
  correctly — use them as the template).
- **`ToString()` full-field spot-checks** — `AttitudeReadingTests` checks `Pitch`/`Roll`/
  `Yaw` but not `Quaternion`/`RotationMatrix`/`Timestamp`; `MotionReadingTests` checks
  `DeviceAcceleration`/`Gravity` but not `Attitude`/`DeviceRotationRate`/`Timestamp`.
  Extend both to cover every field in the format string.
- **`ErrorId` negative-value test** — `SensorFailedExceptionTests`/
  `AccelerometerFailedExceptionTests` only round-trip a positive `errorId` (42/7).
  Nothing in the implementation clamps/validates `errorId`, so a negative value is a real
  untested code path, not just a missing assertion. Add a negative-value round-trip case
  to both files.
- **`VibrateController::getDeviceNameProperty()`/`getIsSupportedProperty()` consistency**
  — each is tested only independently ("doesn't crash"). Add a test asserting the
  relationship holds (e.g. unsupported ⇒ empty name) rather than treating them as
  unrelated facts.
- **`VibrateController::StartLeftRight()` lower boundary + zero duration** — the upper
  magnitude boundary (`1.0f`/`1.0f`) is incidentally covered by
  `StartLeftRightDoesNotThrow`, but `0.0f` for either motor is never tested, and
  `TimeSpan::Zero` duration is never tested for `StartLeftRight` specifically (only
  ~50ms and the `5.001s` throw case). Add both.

---

## Phase 6: Follow-up research (low priority, no urgency)

### Task P3-12 — Re-attempt confirming `SensorFailedException`'s real constructor overloads and `CalibrationEventArgs`'s exact members

Both remain genuinely unverified after two research passes now:
- `SensorFailedException`'s class page found this pass (`hh239255`) is a different
  doc-family generation (`.NET Framework/vs.110`) than the WP8-app-docs family
  (`vs.105`) used successfully for `Compass`/`Gyroscope`/`Motion`/`Accelerometer`, and
  shows no Constructors section at all — ambiguous whether that means "no public
  constructor" or a scraping artifact of this specific doc-family variant. The
  `(message, errorId)` constructor Task P2-16 added remains an educated guess, not a
  confirmed match.
- `CalibrationEventArgs`'s exact member list has no direct class page found in either
  pass — only indirect confirmation via `Compass.Calibrate`/`Motion.Calibrate`'s own
  event docs (which incidentally confirmed real `Compass` fires `Calibrate` when
  `HeadingAccuracy` exceeds ±20°, moot for CNA since `Compass` is a permanent stub).
  Current empty-class implementation is unconfirmed either way — low priority given it
  matches every other event-args marker-class precedent already seen in this namespace.

Not urgent — no evidence of an actual bug from either gap, just incomplete verification.
If picked up: try the `.NET Framework` reference source (not just archived MSDN pages) for
`SensorFailedException`, since Windows Phone's `Microsoft.Devices.Sensors.dll` may share
lineage with a desktop `System.Device.*` equivalent; for `CalibrationEventArgs`, try
searching for its constructor signature specifically rather than its class member page
(constructor pages sometimes exist independently of a found class page, as happened for
`Accelerometer.ReadingChanged` in Task P2-15's research).

---

## Verification checklist (apply to every source-touching task above)

- Build `cmake --build cmake-build-debug --target CNA` then `--target CnaTests`.
- Run the specific new/changed test suite via `--gtest_filter`.
- Run full `cd cmake-build-debug && ctest --output-on-failure` and confirm no new
  regressions beyond the existing headless `EasyGL_*` baseline (re-check the current
  count first — it was 64 as of `plan_devices_phase2.md`'s completion, 2026-07-02).
- Update `NEXT.md` (status, recent changes, known bugs sections) after each task, same
  as throughout `plan_devices.md`/`plan_devices_phase2.md`.
