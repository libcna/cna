# Phase 8 — Final hardening / lifetime audit

Scope: `Microsoft::Devices`/`Microsoft::Devices::Sensors` plus tests/docs only. Branch
`feature/devices`. Builds on `plan_devices_phase7.md` (process-wide SDL sensor mutex,
concurrent-Dispose fix, per-instance dispatch revalidation, locked test-only getters,
hardened `ScopeExit`) but — per this task's own framing — does not assume Phase 7 is
perfect. Independent re-audit, verified against the actual current source before any
edit.

## Audit findings

### P8-1. Self-destroy-during-own-callback — CONFIRMED, real gap, partially fixable

Re-read `Detail::SdlSensorSubsystem<TSensor>::DispatchToInstances()`,
`Accelerometer::InjectSyntheticSensorUpdate()`/`DispatchSensorReading()`, and
`Gyroscope`'s equivalents.

**`sensor.Dispose()` from inside its own `CurrentValueChanged`/`ReadingChanged` handler:
already supported**, confirmed by the existing
`DisposeFromWithinOwnCallbackDoesNotDeadlock` test (both classes). `Dispose()` does not
deallocate the object — it only flips `disposed_`/runs cleanup — so any code that keeps
running against `this` afterward (within the same dispatch) is still touching valid,
allocated memory. Not re-litigated this phase.

**Actually destroying the object (`delete`/`unique_ptr::reset()`) from inside its own
callback: confirmed a real, previously-unguarded use-after-free**, in two independent
places:

1. **The dispatch bookkeeping itself.** `DispatchToInstances()`'s `cleanupGuard` (a
   `ScopeExit`) captures `instance` (the raw `TSensor*`) and dereferences
   `instance->dispatchingThreadIds_` *after* `dispatchOne(instance)` returns.
   `InjectSyntheticSensorUpdate()`'s equivalent guard captures `this` the same way. If
   the just-invoked callback destroyed the object, both guards touch freed memory the
   moment they run.
2. **A second, independent, *class-design* issue specific to `Accelerometer`.**
   `Accelerometer::DispatchSensorReading()` raises `CurrentValueChanged` (via
   `setCurrentValueProperty()`), then unconditionally calls `getIsDataValidProperty()`
   again (`if (getIsDataValidProperty() && !ReadingChanged.Empty())`) to decide whether
   to also raise the legacy `ReadingChanged` event. That second call touches `this`
   *regardless of whether `ReadingChanged` has any subscribers* (short-circuit still
   evaluates the left operand). So for `Accelerometer` specifically, destroying the
   object from within a `CurrentValueChanged` handler is unsafe even with a dispatch-
   bookkeeping fix, because `DispatchSensorReading()`'s own body touches `this` again
   before `ReadingChanged` even gets a chance to fire. **`Gyroscope::DispatchSensorReading()`
   has no such second touch — it raises `CurrentValueChanged` as its last statement and
   returns immediately** — so this specific issue does not apply to `Gyroscope`.

**Decision:** implement the "dispatch cleanup never touches the `TSensor` object after
the callback returns" design from a stable, subsystem-independent location — via a
`std::shared_ptr<std::vector<std::thread::id>>` ("dispatch token") owned by each
instance and copied *by value* into the dispatch loop's cleanup lambda *before* invoking
the callback, replacing the current per-instance `std::vector<std::thread::id>` member.
This closes gap (1) completely (for both classes' outer dispatch bookkeeping, and both
the real SDL event-watch path and the `InjectSyntheticSensorUpdate()`/
`DispatchToInstancesForTesting()` test paths) without needing a full subsystem-owned
registry/map (which would add its own lifecycle-management risk — e.g. deciding when to
erase entries for instances that were started but never dispatched to — for no extra
benefit over a simple per-instance shared_ptr). Gap (2) is **not** fixable by this token
(it is not a dispatch-bookkeeping problem — `ReadingChanged` is itself a member of
`this`, so raising it after `CurrentValueChanged` inherently requires `this` to still
exist) without redesigning where the event objects themselves live relative to instance
identity, which is a materially larger, riskier change with its own semantic question
(should a destroyed object still fire a *second* event?) not clearly resolved by the
brief. **Documented as a permanent, explicit boundary instead of fixed** — see Task
P8-1's Resolution for the precise wording and where it lives.

**Net supported/unsupported boundary after this task:**
- `Dispose()` from within own callback: supported (unchanged, already tested).
- Destroying the object from within `Gyroscope`'s own `CurrentValueChanged` handler:
  now supported (fixed by the token).
- Destroying the object from within `Accelerometer`'s own `ReadingChanged` handler (the
  *last*-fired event in that class's dispatch): now supported (fixed by the token, same
  reasoning as `Gyroscope` — nothing touches `this` afterward).
- Destroying the object from within `Accelerometer`'s own `CurrentValueChanged` handler,
  when the dispatch would otherwise also need to raise `ReadingChanged`, or even when it
  wouldn't (the second-touch call happens unconditionally): **not supported, documented
  as unsupported C++ lifetime misuse, not fixed.**

### P8-2. `SensorBase::timeBetweenUpdates_` unguarded — CONFIRMED, real gap

Read the current `include/Microsoft/Devices/Sensors/SensorBase.hpp`:
`getTimeBetweenUpdatesProperty()` (returns `const System::TimeSpan&`, no lock) and
`setTimeBetweenUpdatesProperty()` (compares and writes `timeBetweenUpdates_` with no
lock) are the *only* two `SensorBase<T>` members that don't go through `mutex_`, unlike
`currentValue_`/`isDataValid_`/`isSupported_`/`disposed_`, all fixed across Phases 5/6/7.
Confirmed real, if narrower in practical reach than the dispatch-thread races those
fixes addressed — `TimeBetweenUpdates` is not touched by
`ProcessSensorUpdateEvent()`/`DispatchSensorReading()` at all (it's a pure game-facing
configuration knob), so the only way to race on it is two *application* threads calling
the getter/setter concurrently — plausible in a multi-threaded game engine, and
inconsistent with the rest of the class's now-uniform locking discipline either way.
**Fixed in Task P8-2** by locking around both, returning the getter by value (matching
the exact precedent `getCurrentValueProperty()` already set in Task P5-2 for the same
reason — the real WP7 `TimeSpan` property is a value type in C#, so this is a more
faithful match, not a breaking API change), and never holding the lock across
`TimeBetweenUpdatesChanged.Raise()`.

### P8-3. SDL-calling helpers can be called without the required lock — CONFIRMED, real gap

`Detail::SdlSensorSubsystem<TSensor>::EnsureSubsystemInitialized()`,
`OpenDefaultSensorLocked()`, and `ProbeIsSupported()` all make real SDL sensor-subsystem
calls and currently rely entirely on a documentation comment ("Caller must already hold
mutex_", or nothing at all for the global SDL mutex) — nothing in the type system stops
a future call site (in `Accelerometer.cpp`/`Gyroscope.cpp`, or a future third sensor
class) from calling any of them without holding `Detail::GetGlobalSdlSensorMutex()`,
silently reintroducing the exact class of bug Tasks P6-1's addendum and P7-1 already
found and fixed. **Fixed in Task P8-3** with a "lock-proof parameter" pattern: each of
these three methods now takes a `const std::lock_guard<std::mutex>&` referencing the
global SDL sensor mutex as a required parameter — every existing call site already has
such a lock in scope (from Task P7-1), so this requires zero new lock acquisitions and
zero risk of a new deadlock; it only makes it structurally awkward (not merely
"undocumented") to call these methods without a lock already visibly in hand at the call
site. This does not guarantee the passed lock actually references
`GetGlobalSdlSensorMutex()` specifically (C++ has no "prove this specific mutex" type
without a bespoke tag type), but it makes an accidental omission far more visible than a
comment alone, and is a low-risk, purely-additive signature change confined to
`Detail::` internals never exposed outside this translation unit's own `.cpp` files.

### P8-4. No sanitizer/stress documentation, no CMake presets for it — CONFIRMED

Grepped `CMakeLists.txt` for any existing `-fsanitize`/sanitizer cache variable: none.
`CMakePresets.json` exists (one `web`/Emscripten preset) but has nothing for
ASan/UBSan/TSan. Both `g++` 14.2.0 and `clang++` 19.1.7 are available in this container,
both support all three sanitizers. **Addressed in Task P8-4** by actually running a
representative sanitizer build (not just writing hypothetical commands) and documenting
the real result, plus adding small, additive `CMakePresets.json` entries (new presets
only — no existing preset touched, no `CMakeLists.txt` change needed since sanitizer
flags can be supplied via the standard `CMAKE_CXX_FLAGS`/`CMAKE_EXE_LINKER_FLAGS` cache
variables with zero project-file changes).

### P8-5. Batch-continues-after-one-handler-throws is not directly tested — CONFIRMED

`ThrowingCallbackDuringSyntheticUpdateStillCleansUpAndDoesNotHangDispose` (Task P6-4,
both classes) proves a single instance's own dispatch survives its own handler
throwing and that `Dispose()` doesn't subsequently hang. It does **not** prove the
specific claim `DispatchToInstances()`'s own doc comment makes: that when instance A's
handler throws, instance B (later in the same batch) still gets dispatched to. No
existing test exercises this multi-instance batch continuation path — confirmed via
grep, no test does this today. **Fixed in Task P8-5** using the same
`DispatchToInstancesForTesting()` hook Task P7-3 added, for both classes.

### P8-6. Resource ownership re-audit — mostly re-confirms Phases 5/6/7, one small consistency fix

Re-read `Accelerometer.cpp`/`Gyroscope.cpp`'s `SDL_INIT_SENSOR` hold
(`subsystemHeld_`)/`ProbeGuard`/cached-sensor-handle/event-watch paths: all consistent
with Phase 6/7's fixes, no new gap found (already covered by
`ConcurrentConstructDestroyKeepsInstanceCountBalanced`,
`FailedStartReleasesSubsystemHoldItAcquired`,
`RepeatedSupportProbingDoesNotChangeSubsequentBehavior`, and the various Start/Stop/
Dispose concurrency tests). Re-read `VibrateController.cpp` in full: `g_haptic`/
`g_subsystemHeld` lifecycle (Task P5-11/P6-6) still correct; `g_leftRightEffectId`'s
lifecycle is correct in every *live* code path (`Stop()`/`Start()`/`StartLeftRight()`'s
own re-entry all call `DestroyLeftRightEffectIfAny()`), and `SDL_CloseHaptic()` in
`~VibrateController()` implicitly invalidates any still-uploaded effect on that device
(SDL3 does not require a separate `SDL_DestroyHapticEffect()` call before closing the
owning device) — **but `~VibrateController()` does not reset `g_leftRightEffectId` to
`-1` after closing the device**, unlike `g_haptic`/`g_subsystemHeld`, which both are.
Not a reachable bug in practice (the singleton's destructor runs once, at static
destruction, and no legitimate code path calls into `VibrateController` afterward), but
inconsistent with the rest of the destructor's own "reset every piece of state, not just
some of it" pattern. **Fixed in Task P8-6** as a one-line defensive consistency addition,
not because a real bug was found.

---

## Tasks

- P8-1 — Clarify and harden callback lifetime semantics
- P8-2 — Lock `SensorBase::TimeBetweenUpdates`
- P8-3 — Make SDL sensor helper locking impossible to misuse
- P8-4 — Sanitizer/stress test documentation and presets
- P8-5 — Test exception behavior policy (batch continuation)
- P8-6 — Final resource ownership audit
- P8-7 — Documentation accuracy pass
- P8-8 — Final verification report

Each task gets its own commit and its own `### Resolution` subsection, filled in as
that task is completed.

## P8-1: Clarify and harden callback lifetime semantics

### Resolution

**Files changed:**
- `include/Microsoft/Devices/Sensors/Detail/SdlSensorSubsystem.hpp` — added
  `#include <memory>`; `DispatchToInstances()` now copies `instance->dispatchToken_`
  (a `shared_ptr<vector<thread::id>>`) into a local `token` while `instance` is
  confirmed alive+started under the lock, and the dispatch-cleanup `ScopeExit` guard
  now captures `token` (not `instance`) — so the cleanup step, which runs *after*
  `dispatchOne(instance)` returns, never touches `instance` again. Updated the class's
  TSensor-requirements doc comment to describe `dispatchToken_` instead of the old
  plain-vector `dispatchingThreadIds_`.
- `include/Microsoft/Devices/Sensors/Accelerometer.hpp` / `Gyroscope.hpp` — renamed
  `dispatchingThreadIds_` (a `std::vector<std::thread::id>`) to `dispatchToken_` (a
  `std::shared_ptr<std::vector<std::thread::id>>`), created once in the constructor
  and never replaced. Added `#include <memory>`.
- `src/Microsoft/Devices/Sensors/Accelerometer.cpp` / `Gyroscope.cpp` — constructor
  now initializes `dispatchToken_` via `std::make_shared<...>()`; `Dispose(bool)`'s
  wait predicate dereferences `*dispatchToken_` via `this` (always valid for that
  method's entire execution); `InjectSyntheticSensorUpdate()` now copies
  `dispatchToken_` into a local `token` *before* calling `DispatchSensorReading()`, and
  its `ScopeExit` cleanup guard captures `token` (and `&subsystem`, never `this`).

**What this fixes:** if a `CurrentValueChanged`/`ReadingChanged` handler destroys (not
just `Dispose()`s) the exact instance it was invoked for, the dispatch-cleanup guard
— which previously dereferenced `instance->dispatchingThreadIds_`/`this->dispatchingThreadIds_`
after the callback returned — now operates on the token, kept alive by its own
`shared_ptr` copy, regardless of whether the instance itself still exists. Covers both
dispatch entry points (the real SDL event-watch path and
`InjectSyntheticSensorUpdate()`) for both classes.

**What this does NOT fix, and why (documented, not a gap):**
`Accelerometer::DispatchSensorReading()` raises `CurrentValueChanged`, then
unconditionally calls `getIsDataValidProperty()` again (to decide whether to also
raise the legacy `ReadingChanged` event) — this second call touches `this` regardless
of whether `ReadingChanged` has any subscribers. So destroying the instance from
within its own `CurrentValueChanged` handler remains unsafe for `Accelerometer`
specifically, independent of this fix — `ReadingChanged` is itself a member of `this`,
so raising it after `CurrentValueChanged` inherently requires `this` to still exist.
Fixing this would mean redesigning where the event objects live relative to instance
identity (a materially larger change, with its own unresolved semantic question:
should a destroyed object still fire a second event?) — out of scope for this token
fix, documented instead in `dispatchToken_`'s own doc comment on both classes and in
the audit finding above. This is the one supported/unsupported boundary this task
leaves in place, by design.

**Regression-proof check (not part of the permanent test suite):** temporarily
reverted `DispatchToInstances()`'s cleanup guard and `Gyroscope::InjectSyntheticSensorUpdate()`'s
cleanup guard back to capturing `instance`/`this` directly (dereferencing
`dispatchToken_` through the stale pointer instead of a captured token copy). A plain
(non-sanitized) run of the new self-destroy tests passed anyway — heap-use-after-free
bugs do not reliably crash without instrumentation, since freed small allocations
often aren't immediately overwritten. Built a **separate, throwaway ASan-instrumented
build** (`-fsanitize=address`, EASYGL backend, `/tmp/cmake-build-asan-check`, not part
of the repo) specifically to get a reliable answer: ASan reported a definitive
`heap-use-after-free` on the reverted code, with a full stack trace pinpointing the
exact reverted line (`Gyroscope.cpp:427`, inside the cleanup guard, reading through
`instance->dispatchToken_` after `~Gyroscope()` had already run via
`unique_ptr::reset()`). Restored the real fix immediately after confirming this,
rebuilt both the normal and ASan binaries, and confirmed all tests — including the new
self-destroy tests — pass cleanly with **zero ASan reports** anywhere in the
Accelerometer/Gyroscope/SensorSubsystemOwnership suites (65 tests).

**Tests added:**
- `GyroscopeTests.SelfDestroyingFromOwnCallbackDuringInjectSyntheticSensorUpdateDoesNotUseAfterFree`
- `GyroscopeTests.SelfDestroyingFromOwnCallbackDuringBatchDispatchDoesNotUseAfterFree`
- `AccelerometerTests.SelfDestroyingFromOwnReadingChangedCallbackDuringInjectSyntheticSensorUpdateDoesNotUseAfterFree`
  (proves the *safe* boundary for Accelerometer — destroying during the *last*-fired
  event is fine, same reasoning as Gyroscope's single-event case)

**Deliberately not added:** a test exercising Accelerometer's *unsafe*
destroy-during-`CurrentValueChanged` boundary. The project has no existing
death-test convention (confirmed via grep — zero `EXPECT_DEATH`/`ASSERT_DEATH` usages
anywhere in this codebase), and deliberately exercising known-UB in a permanent test
would risk unpredictable CI behavior depending on allocator/platform, for a boundary
that's already clearly documented in code and in this plan. Matches this task's own
guidance: add a death test only if the project already uses the convention; otherwise
document why not.

**Commands run:**
```bash
cmake --build cmake-build-debug --target CNA -j"$(nproc)"        # clean
cmake --build cmake-build-debug --target CnaTests -j"$(nproc)"   # clean
./cmake-build-debug/CnaTests --gtest_filter="AccelerometerTests.*:GyroscopeTests.*:SensorSubsystemOwnershipTests.*"
# 65 tests, 63 passed, 2 skipped (expected)

# Throwaway ASan verification build (not committed, not part of the repo):
cmake -S . -B /tmp/cmake-build-asan-check -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g -O0" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
cmake --build /tmp/cmake-build-asan-check --target CnaTests -j"$(nproc)"
/tmp/cmake-build-asan-check/CnaTests --gtest_filter="AccelerometerTests.*:GyroscopeTests.*:SensorSubsystemOwnershipTests.*"
# 65 tests, 63 passed, 2 skipped, ZERO ASan reports (confirmed clean with the real fix;
# confirmed a definitive heap-use-after-free with the fix temporarily reverted).
```

**Remaining risk:** low for the fixed case (verified two ways: a real, deterministic
ASan detection of the reverted bug, and a clean ASan run of the fix). The documented,
unfixed Accelerometer boundary (destroy-during-`CurrentValueChanged`) is a known,
explicit limitation, not a silent gap — matches this task's own framing ("if a full
lifetime-token refactor is too large, document the boundary").

## P8-2: Lock `SensorBase::TimeBetweenUpdates`

### Resolution

**Files changed:**
- `include/Microsoft/Devices/Sensors/SensorBase.hpp` — `getTimeBetweenUpdatesProperty()`
  now returns `System::TimeSpan` by value (was `const System::TimeSpan&`) and locks
  `mutex_` around the read. `setTimeBetweenUpdatesProperty()` locks `mutex_` around the
  compare-and-write, releasing it before `TimeBetweenUpdatesChanged.Raise()` — matching
  every other event-raising setter on this class, none of which hold the lock while
  raising.
- `tests/Microsoft/Devices/Sensors/SensorBaseTests.cpp` — added
  `ConcurrentGetSetTimeBetweenUpdatesPropertyDoesNotCrash`: 8 threads × 200 iterations
  each calling both the getter and setter concurrently on one shared instance.

**API compatibility:** confirmed via `grep` that every existing call site (all in
`SensorBaseTests.cpp`) either compares the return value or copies it to a `const
TimeSpan` local — both work identically with a value or reference return, so this is
not a breaking change. Matches the exact precedent Task P5-2 already set for
`getCurrentValueProperty()` for the same reason (the real WP7 `TimeSpan` property is a
C# value type, so returning by value is the more faithful match, not a divergence).

**Verification beyond a plain pass:** built a **second throwaway sanitizer
configuration** — ThreadSanitizer (`-fsanitize=thread`, EASYGL backend,
`/tmp/cmake-build-tsan-check`, not part of the repo) — specifically because TSan, not
ASan, is the tool that actually detects data races (ASan only catches memory-safety
bugs like the P8-1 use-after-free). Ran the *entire* Devices-only test suite
(`SensorBase`/`Accelerometer`/`Gyroscope`/`Compass`/`Motion`/`SensorSubsystemOwnership`)
under TSan: 28 warnings reported, but every single one is the *identical* pre-existing
race — `System::TimeSpan::TimeSpan(const TimeSpan&)` incrementing an unsynchronized
global `copy_count` debug/instrumentation counter at `sharp-runtime/src/System/TimeSpan.cpp:55`
— confirmed by reading that file directly. This is a `sharp-runtime` bug (a separate
repo with its own `CLAUDE.md`/git history, per this project's established
boundary — see `NEXT.md`'s "do not fix bugs discovered in sharp-runtime..." rule), not
a `Microsoft::Devices` bug, and not something this task's new locking introduced —
`ConcurrentGetSetTimeBetweenUpdatesPropertyDoesNotCrash` itself shows **zero** TSan
reports anywhere near it. Not fixed here; documented as an out-of-scope, pre-existing
finding (see Task P8-4/P8-7 for where this is written up for future sessions).

**Tests added:** `SensorBaseTests.ConcurrentGetSetTimeBetweenUpdatesPropertyDoesNotCrash`.

**Commands run:**
```bash
cmake --build cmake-build-debug --target CNA -j"$(nproc)"        # clean
cmake --build cmake-build-debug --target CnaTests -j"$(nproc)"   # clean
./cmake-build-debug/CnaTests --gtest_filter="SensorBaseTests.*"
# 7 tests, all passed

cd .. && ctest --output-on-failure
# 2046/2049 passed; the same 2 pre-existing EasyGL failures, plus one additional
# transient failure (EasyGL_SkinnedBones) confirmed via immediate re-run in isolation
# to pass cleanly (100%) — an environmental flake under this session's concurrent
# sanitizer-build load, not a regression from this task's changes.

# Throwaway TSan verification build (not committed, not part of the repo):
cmake -S . -B /tmp/cmake-build-tsan-check -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer -g -O1" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
cmake --build /tmp/cmake-build-tsan-check --target CnaTests -j"$(nproc)"
/tmp/cmake-build-tsan-check/CnaTests --gtest_filter="SensorBaseTests.*:AccelerometerTests.*:GyroscopeTests.*:CompassTests.*:MotionTests.*:SensorSubsystemOwnershipTests.*"
# 28 TSan warnings, ALL the identical pre-existing sharp-runtime TimeSpan::copy_count
# race — zero warnings involving Microsoft::Devices's own locking.
```

**Remaining risk:** low. The fix itself is verified clean under TSan; the only
sanitizer findings in this run point to a pre-existing, out-of-scope `sharp-runtime`
issue, not this task's own change.

## P8-3: Make SDL sensor helper locking impossible to misuse

### Resolution

**Files changed:**
- `include/Microsoft/Devices/Sensors/Detail/SdlSensorSubsystem.hpp` —
  `EnsureSubsystemInitialized()`, `OpenDefaultSensorLocked()`, and `ProbeIsSupported()`
  now each take a required `const std::lock_guard<std::mutex>& /*globalSdlSensorMutexHeld*/`
  parameter — not used for anything except as a compile-time "you must already hold
  this lock" proof, chosen over renaming (the brief's other suggested option) because
  it makes the precondition structurally enforced rather than only nominally clearer.
- `src/Microsoft/Devices/Sensors/Accelerometer.cpp` / `Gyroscope.cpp` — every one of the
  three call sites (`getIsSupportedProperty()`, and `Start()`'s two calls) already had
  the correct `std::lock_guard<std::mutex>` on `Detail::GetGlobalSdlSensorMutex()` in
  scope from Task P7-1 — this task only threads that existing local variable into the
  call, adding zero new lock acquisitions and zero deadlock risk.

**Why a lock-proof parameter over the brief's other two options:** a rename alone
(`ProbeIsSupportedGlobalSdlLockHeld`) is still just a stronger-worded comment — nothing
stops a future call site from calling it without the lock, it just makes the mistake
slightly more embarrassing to make. Making the methods outright `private` doesn't fit
either: they're already `Detail::`-internal, never exposed outside
`Accelerometer.cpp`/`Gyroscope.cpp`'s own translation units, so the actual risk isn't
external misuse, it's a *future edit within these same two files* forgetting the lock —
exactly the mistake Task P6-1's addendum and Task P7-1 both found and fixed after the
fact. A required lock-proof parameter is the only one of the three options that the
*compiler* enforces, not just a human reviewer.

**What this does not guarantee:** the parameter's type (`const std::lock_guard<std::mutex>&`)
doesn't prove the passed lock references `GetGlobalSdlSensorMutex()` *specifically* — a
caller could construct a decoy lock over some unrelated mutex and still compile. C++ has
no built-in mechanism to prove "this lock guards this exact mutex" without a bespoke tag
type wrapping the mutex, which would be more machinery than this internal-only class
warrants for a mistake that would require a much more deliberate, visible action (writing
a whole separate, pointless `std::lock_guard` construction) than the original one-line
omission this fixes.

**Verification that misuse now fails to compile (not just a documentation claim):**
wrote a throwaway scratch file calling `SdlSensorSubsystem<Accelerometer>::ProbeIsSupported()`
with no argument and compiled it directly against this header with the project's real
include paths — confirmed a compile error (`no matching function for call to
'ProbeIsSupported()' ... candidate expects 1 argument, 0 provided`). Deleted the scratch
file immediately after confirming; not part of the permanent test suite (a
compile-failure check isn't a runnable `gtest`, and this project has no separate
compile-fail-test harness).

**Tests added:** none — this is a pure signature-hardening change with no behavior
change; every existing call site already held the required lock, so no test's
observable behavior changes. Re-ran the existing Accelerometer/Gyroscope/
SensorSubsystemOwnership suite to confirm.

**Commands run:**
```bash
cmake --build cmake-build-debug --target CNA -j"$(nproc)"        # clean
cmake --build cmake-build-debug --target CnaTests -j"$(nproc)"   # clean
./cmake-build-debug/CnaTests --gtest_filter="AccelerometerTests.*:GyroscopeTests.*:SensorSubsystemOwnershipTests.*"
# 65 tests, 63 passed, 2 skipped (expected) — identical to before this task, as expected
# for a pure signature change.

cd .. && ctest --output-on-failure
# 2047/2049 passed; back to the standard 2 pre-existing EasyGL failures (confirming
# the 3rd failure noted in Task P8-2 was indeed transient, not a regression).
```

**Remaining risk:** negligible. No behavior change; the added parameter is a
compile-time-only safety net verified to actually reject the exact misuse it targets.
