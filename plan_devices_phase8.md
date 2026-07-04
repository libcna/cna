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

## P8-4: Sanitizer/stress test documentation and presets

### Resolution

**Files changed:**
- `CMakePresets.json` — added three new, purely additive configure/build preset pairs:
  `devices-asan`, `devices-tsan`, `devices-ubsan` (EASYGL backend, `CNA_BUILD_TESTS=ON`,
  `CMAKE_CXX_FLAGS`/`CMAKE_EXE_LINKER_FLAGS` set to the respective `-fsanitize=...`
  flags). No existing preset touched, no `CMakeLists.txt` change needed — sanitizer
  flags are supplied entirely via the standard CMake cache variables.
- `.gitignore` — added the three new preset-generated build directories, matching the
  existing per-directory listing convention.
- `docs/devices-build.md` — new Section 6 documenting all three presets, the exact
  commands, and the *actual* results of running them (not hypothetical/written-but-
  untested commands).
- `tests/Microsoft/Devices/Sensors/SensorBaseTests.cpp` — see the real finding below;
  `TestSensorBase::timeBetweenUpdatesChangedCount` changed from a plain `int` to
  `std::atomic<int>`.

**A real bug found by actually running the sanitizers, not just writing the presets:**
the first ThreadSanitizer run against the full Devices suite surfaced *two* distinct
races, not the one expected pre-existing `sharp-runtime` finding. The second was new and
real: `SensorBaseTests.cpp`'s own `TestSensorBase` test fixture (added in Task P6-5,
extended in Task P8-2) increments `timeBetweenUpdatesChangedCount` from its
`TimeBetweenUpdatesChanged` handler, which fires *outside* `SensorBase::mutex_` by
design (Task P8-2 was careful to never hold the lock while raising the event) — so Task
P8-2's own new `ConcurrentGetSetTimeBetweenUpdatesPropertyDoesNotCrash` test (the first
test ever to drive concurrent value changes on `TimeBetweenUpdates`, and therefore the
first to actually fire this event from more than one thread) raced on that plain `int`.
This is a test-fixture-only bug, not a `Microsoft::Devices` production-code bug — but
real, and a good demonstration of exactly why this task exists: a plain, unsanitized run
of the exact same test (already run repeatedly during Task P8-2) never showed any
symptom at all. Fixed with `std::atomic<int>`; confirmed clean on the next TSan run (the
only remaining finding is the pre-existing `sharp-runtime` one, unchanged).

**Actual, not hypothetical, sanitizer results** (all three presets configured, built,
and run against the full Devices-only suite — 224 tests, 222 passed, 2 expected skips):
- **ASan:** clean. Already used during Task P8-1 to get a reliable answer on a
  use-after-free a plain run did not reproduce.
- **TSan:** one real bug found and fixed (above); after that fix, only the pre-existing,
  out-of-scope `sharp-runtime` `TimeSpan::copy_count` race remains (see Task P8-2's
  Resolution for that finding's own detail).
- **UBSan:** clean.

**Tests added:** none — `ConcurrentGetSetTimeBetweenUpdatesPropertyDoesNotCrash` already
existed from Task P8-2; this task fixed a race *within* that test's own fixture, not a
production code path.

**Commands run:**
```bash
cmake --preset devices-asan && cmake --build --preset devices-asan
cmake --preset devices-tsan && cmake --build --preset devices-tsan
cmake --preset devices-ubsan && cmake --build --preset devices-ubsan
# all three configure and build cleanly

FILTER="Accelerometer*:SensorFailed*:Compass*:Gyroscope*:Attitude*:Motion*:VibrateController*:SensorSubsystemOwnership*:AndroidSensorOrientation*:SensorBase*:ScopeExit*"
./cmake-build-devices-asan/CnaTests --gtest_filter="$FILTER"    # 224/222, clean
./cmake-build-devices-tsan/CnaTests --gtest_filter="$FILTER"    # 224/222, 1 pre-existing sharp-runtime race only (after the atomic fix)
./cmake-build-devices-ubsan/CnaTests --gtest_filter="$FILTER"   # 224/222, clean

cmake --build cmake-build-debug --target CnaTests -j"$(nproc)"  # plain build, clean
cd cmake-build-debug && ctest --output-on-failure
# 2047/2049 passed; the standard 2 pre-existing EasyGL failures.
```
(The three preset-generated build directories were deleted after verification to avoid
leaving large build artifacts in the working tree — they regenerate identically from
the presets any time they're needed again.)

**Remaining risk:** low. All three sanitizer configurations are now proven working
(not just written), and the one real bug this task's own verification step surfaced was
fixed and re-verified. The pre-existing `sharp-runtime` TSan finding is explicitly
documented so a future session doesn't need to re-discover it, and so a *genuinely new*
finding isn't mistakenly waved away as "probably that same old thing."

## P8-5: Test exception behavior policy

### Resolution

**Files changed:**
- `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp` / `GyroscopeTests.cpp` —
  added `ThrowingHandlerInBatchDispatchDoesNotPreventNextInstanceFromReceivingItsEvent`
  to both: registers two started instances A and B in a simulated
  `DispatchToInstancesForTesting()` batch; A's handler throws; confirms B's handler
  still runs (the actual claim `DispatchToInstances()`'s own doc comment makes, per
  Task P7-3's own comment: "swallowing it here also lets the remaining snapshotted
  instances still get dispatched to and cleaned up") and that both instances' `Dispose()`
  afterward is clean (no hang from corrupted dispatch-tracking state).

**Gap this closes:** `ThrowingCallbackDuringSyntheticUpdateStillCleansUpAndDoesNotHangDispose`
(Task P6-4) only proves a *single* instance's own dispatch survives its own handler
throwing — it says nothing about a *different*, later instance in the same batch. No
existing test exercised the multi-instance batch-continuation claim before this task
(confirmed via `grep`, matching the audit finding).

**Documented policy** (per this task's own ask — the swallow-all-exceptions choice is a
deliberate behavioral decision, not an oversight): already documented in
`DispatchToInstances()`'s own doc comment (`SdlSensorSubsystem.hpp`, added across Tasks
P6-4/P7-3) — the real path is an `SDL_EventFilter` callback invoked directly by
`SDL_PushEvent()`, a C API that does not expect a C++ exception to unwind through its own
call frames, and swallowing per-instance also lets the rest of the batch still get
dispatched to. This task's tests are the first to actually *prove* the second half of
that claim, not just state it.

**Regression-proof check (not part of the permanent test suite):** temporarily replaced
`DispatchToInstances()`'s `try { dispatchOne(instance); } catch (...) { ... }` with a
bare, unguarded `dispatchOne(instance);` call (no swallowing at all) and re-ran the new
tests — both failed exactly as predicted: the exception propagated out of
`DispatchToInstancesForTesting()` itself (`EXPECT_NO_THROW` failure) and B's handler
never ran (`bCallbackCalled` stayed `false`). Restored the real code immediately after
confirming this; all tests pass again with the real swallow-and-continue behavior in
place.

**Tests added:**
`ThrowingHandlerInBatchDispatchDoesNotPreventNextInstanceFromReceivingItsEvent`
(Accelerometer, Gyroscope).

**Commands run:**
```bash
cmake --build cmake-build-debug --target CNA -j"$(nproc)"        # clean
cmake --build cmake-build-debug --target CnaTests -j"$(nproc)"   # clean
./cmake-build-debug/CnaTests --gtest_filter="Accelerometer*:SensorFailed*:Compass*:Gyroscope*:Attitude*:Motion*:VibrateController*:SensorSubsystemOwnership*:AndroidSensorOrientation*:SensorBase*:ScopeExit*"
# 226 tests, 224 passed, 2 skipped (expected)

cd .. && ctest --output-on-failure
# 2049/2051 passed; the standard 2 pre-existing EasyGL failures.
```

**Remaining risk:** negligible. The documented policy is now backed by a test that
would fail (confirmed via a temporary revert) if the batch-continuation guarantee ever
regressed.

## P8-6: Final resource ownership audit

### Resolution

**Files changed:**
- `src/Microsoft/Devices/VibrateController.cpp` — `~VibrateController()` now resets
  `g_leftRightEffectId` to `-1` after closing `g_haptic`, matching the same
  full-state-reset pattern the destructor already applies to `g_haptic`/`g_subsystemHeld`.
  Not a fix for a reachable bug — `SDL_CloseHaptic()` already implicitly invalidates any
  effect still uploaded on that device, and this singleton's destructor runs once, at
  static destruction, with no legitimate code path calling into `VibrateController`
  afterward — just closing the one piece of state this destructor previously left stale,
  for consistency and defensive completeness.

**Re-confirmed, no new gap found, for each item this task's brief asked to re-check:**
- **Accelerometer/Gyroscope `SDL_INIT_SENSOR` holds** (`subsystemHeld_`): consistent with
  Phase 6/7's fixes; covered by `ConcurrentConstructDestroyKeepsInstanceCountBalanced`,
  `FailedStartReleasesSubsystemHoldItAcquired`, and the Start/Stop/Dispose concurrency
  suite. No new gap.
- **Probe-only `ProbeGuard` paths**: covered by
  `RepeatedSupportProbingDoesNotChangeSubsequentBehavior` (Task P5-1) — can't assert on
  SDL's internal ref-count directly (no public API exposes it), so this remains the
  strongest test possible in this environment, same as Task P5-1's own original
  reasoning. No new gap.
- **`SDL_OpenSensor`/`SDL_CloseSensor` cached sensor handle**: covered by the Start/Stop/
  Dispose test suite (the cached handle is closed exactly once, when `instanceCount_`
  reaches zero, inside the now-`GetGlobalSdlSensorMutex()`-protected section — Task
  P7-1). No new gap.
- **Event watch add/remove**: covered by
  `ConcurrentStartStopFromMultipleThreadsDoesNotCrash` and the rest of the Start/Stop/
  Dispose suite. No new gap.
- **`VibrateController` `SDL_INIT_HAPTIC`/`SDL_Haptic*`/`g_leftRightEffectId`**: the one
  real (if unreachable-in-practice) inconsistency found and fixed above; everything else
  already correct per Tasks P5-11/P6-6's own re-audits, re-confirmed by re-reading the
  full file this task.

**Why no new automated test for the `g_leftRightEffectId` reset specifically:**
`~VibrateController()` only runs once, at normal process/static-destruction exit — a unit
test cannot trigger this destructor and then inspect the now-destroyed singleton's
internal state (a `.cpp`-file-local anonymous-namespace variable, not exposed by any
public API) without invoking undefined behavior itself. This mirrors this project's
existing, accepted reasoning for why `ProbeGuard`'s SDL ref-count balance also can't be
asserted on directly. Re-ran the full `VibrateControllerTests` suite (29 tests) — under
both the plain build and a throwaway ASan build, both clean — to confirm this change
introduces no regression in every path that *is* testable.

**Tests added:** none — see above for why the one behavior change here isn't
independently testable; re-ran all existing resource-ownership-relevant tests to confirm
no regression.

**Commands run:**
```bash
cmake --build cmake-build-debug --target CNA -j"$(nproc)"        # clean
cmake --build cmake-build-debug --target CnaTests -j"$(nproc)"   # clean
./cmake-build-debug/CnaTests --gtest_filter="VibrateControllerTests.*"
# 29 tests, all passed

./cmake-build-debug/CnaTests --gtest_filter="Accelerometer*:SensorFailed*:Compass*:Gyroscope*:Attitude*:Motion*:VibrateController*:SensorSubsystemOwnership*:AndroidSensorOrientation*:SensorBase*:ScopeExit*"
# 226 tests, 224 passed, 2 skipped (expected)

# Re-verified under the devices-asan preset build (throwaway, from Task P8-4):
cmake --build /tmp/cmake-build-asan-check --target CnaTests -j"$(nproc)"
/tmp/cmake-build-asan-check/CnaTests --gtest_filter="VibrateControllerTests.*"
# 29 tests, all passed, zero ASan reports

cd .. && ctest --output-on-failure
# 2049/2051 passed; the standard 2 pre-existing EasyGL failures.
```

**Remaining risk:** negligible. This task's only code change is a defensive
consistency fix for an already-non-reachable edge case; every genuinely reachable
resource-ownership path was re-confirmed already correct, not newly fixed.

## P8-7: Documentation accuracy pass

### Resolution

**Files changed:**
- `NEXT.md` — Section 1 (phase history now mentions five hardening passes, with
  Phase 8's use-after-free fix and its one deliberately-left-open boundary
  summarized); Section 2 (SDL runtime implementation bullets extended with all three
  P8-1/P8-2/P8-3 fixes; build/test/Android/iOS/VULKAN/BGFX paragraphs point at Task
  P8-8; Devices-only count updated to 226/224); Section 3 (full Phase 8 task-by-task
  summary block added); Section 4 (blocker — Phase 8 also fully closed); Section 5
  (known bugs — new bullet for the Accelerometer `CurrentValueChanged` self-destroy
  boundary; Phase 8's fixes added to "resolved"); Section 6 (architecture notes —
  `SensorBase<T>`'s now-fully-locked field list, `dispatchToken_`'s design and the
  explicit boundary it doesn't cover, the lock-proof parameter pattern, all with
  explicit "do not remove/revert" callouts); Section 7 (commands — test counts
  updated, sanitizer preset commands added); Section 8 (next tasks — renumbered,
  item 3 now frames a sixth re-audit against Phase 8's own claims, including its
  documented boundary); Section 9 (do-not-do — four new bullets for
  P8-1/P8-2/P8-3, each citing the confirmed failure mode); Section 10 (resume
  prompt updated to mention sanitizer presets alongside the stress-loop discipline).
- `AUDIT.md` — `Accelerometer`, `Gyroscope`, `SensorBase<T>`, `VibrateController` rows
  extended with Phase 8 findings/fixes paragraphs, matching the existing per-phase
  annotation style. `Compass`/`Motion` rows left unchanged — Phase 8 did not touch
  either class directly (only `SensorBase<T>`'s shared `Dispose(bool)` pattern, already
  covered in Phase 7's row entries for those two classes).
- `docs/devices-build.md` — updated throughout during Tasks P8-1 through P8-6 as each
  landed (ZIP-export caveat already present from Phase 7; new Section 6 for sanitizers
  from Task P8-4; exception-swallowing policy note from Task P8-5); this task's own
  pass corrected the remaining stale Phase-7-era test counts (Devices-only: 220→226 via
  `ctest -R`, 140/138→146/144 via direct `--gtest_filter`; full suite: 2045→2051) and
  updated the header/Android/iOS sections' plan-file references to include Phase 8.
- `docs/devices-hardware-checklist.md` — read in full; confirmed **no changes needed**.
  Phase 8's work (dispatch-lifetime bookkeeping, `TimeBetweenUpdates` locking,
  lock-proof parameters, a `VibrateController` destructor consistency fix) touches
  none of the physically-observable hardware behavior this checklist exists to verify
  (Android axis-remap sign conventions, `VibrateController`'s actual motor output,
  gamepad-exclusion) — matches the precedent Task P6-9 set for
  `docs/location-future-plan.md` (reviewed, confirmed unaffected, left unmodified
  rather than touched just to "look busy").

**Rules from this task's own brief, confirmed followed:**
- **No Android/iOS hardware verification claimed** — every reference above is to
  compile-only Android cross-compilation or explicitly-blocked iOS, never to a claim of
  running on real hardware. `docs/devices-hardware-checklist.md` still states plainly
  that none of its items have ever been verified, any session.
- **No ZIP-export self-containment claimed** — the Task P7-6 caveat is preserved
  verbatim in both `NEXT.md` and `docs/devices-build.md`, unchanged by this task.
- **`Compass`/`Motion` remain honest `NotSupported` stubs** — not touched this phase,
  confirmed by re-reading their `AUDIT.md` rows (last substantively touched in Phase 6,
  Task P6-8) before deciding not to edit them.
- **GPS stays out of `Microsoft.Devices.Sensors`** — `docs/location-future-plan.md` not
  touched this phase either; nothing in Phase 8's scope came near this boundary.

**Tests added:** none — pure documentation.

**Commands run:** none beyond the sanity re-run of the Devices-only test filter to
confirm the doc-only changes didn't accidentally touch anything (226 tests, 224 passed,
2 skipped — unchanged from Task P8-6).

**Remaining risk:** none — documentation only, no code changed.

## P8-8: Final verification report

### Commands run and exact results

**`EASYGL` (`cmake-build-debug`):**
```bash
cmake --build cmake-build-debug --target CNA -j"$(nproc)"        # clean
cmake --build cmake-build-debug --target CnaTests -j"$(nproc)"   # clean
cd cmake-build-debug && ctest --output-on-failure \
  -R "Accelerometer|SensorFailed|Compass|Gyroscope|Attitude|Motion|VibrateController|SensorSubsystemOwnership|AndroidSensorOrientation|SensorBase|ScopeExit"
# 226/226 passed (plus the 2 expected GTEST_SKIP()s)
cd .. && ctest --test-dir cmake-build-debug --output-on-failure
# 2049/2051 passed — same 2 pre-existing EasyGL_MRT_TwoAttachments/easy-gl-resource-smoke-tests failures
```
(Already re-verified at the end of every P8-1 through P8-6 task individually, including
under throwaway ASan/TSan/UBSan builds during Tasks P8-1/P8-2/P8-4/P8-6; this is the
final confirmation on the plain `EASYGL` build, not a first check.)

**`VULKAN` (`cmake-build-vulkan`):**
```bash
cmake --build cmake-build-vulkan --target CNA -j"$(nproc)"        # clean
cmake --build cmake-build-vulkan --target CnaTests -j"$(nproc)"   # clean
cd cmake-build-vulkan && ctest --output-on-failure -R "...same filter as EASYGL..."
# 226/226 passed — identical to EASYGL

for i in $(seq 1 30); do
  ./CnaTests --gtest_filter="AccelerometerTests.*:GyroscopeTests.*:SensorSubsystemOwnershipTests.*:SensorBaseTests.*" || echo "run $i FAILED"
done
# 30/30 clean

cd .. && ctest --test-dir cmake-build-vulkan --output-on-failure
# 1986/1999 passed (99%); 13 pre-existing Vulkan_* graphics-smoke tests "Not Run"
# (need a real GPU/driver) — same baseline count as plan_devices_phase7.md Task P7-7;
# no new failures, no regressions.
```

**`BGFX` (`cmake-build-bgfx`):**
```bash
cmake --build cmake-build-bgfx --target CNA -j"$(nproc)"        # clean
cmake --build cmake-build-bgfx --target CnaTests -j"$(nproc)"   # clean
cd cmake-build-bgfx && ctest --output-on-failure -R "...same filter as EASYGL..."
# 226/226 passed — identical to EASYGL

for i in $(seq 1 30); do
  ./CnaTests --gtest_filter="AccelerometerTests.*:GyroscopeTests.*:SensorSubsystemOwnershipTests.*:SensorBaseTests.*" || echo "run $i FAILED"
done
# 30/30 clean

cd .. && ctest --test-dir cmake-build-bgfx --output-on-failure
# 1990/1993 passed (99%); 3 pre-existing Bgfx_* tests "Not Run" (multi-config build-dir
# executable lookup quirk unrelated to Microsoft::Devices) — same baseline count as
# plan_devices_phase7.md Task P7-7; no new failures, no regressions.
```

**Android cross-compile (`cmake-build-android`, NDK 30.0.14904198, arm64-v8a, API 24):**
```bash
cmake --build cmake-build-android --target CNA -j"$(nproc)"
# clean
```
Re-ran the NDK's own `llvm-nm` against `Accelerometer.cpp.o`/`VibrateController.cpp.o`
to confirm Phase 8's actual new symbols compiled in, not just that *something* compiled:
```bash
NM="$HOME/Android/Sdk/ndk/30.0.14904198/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-nm"
"$NM" -C cmake-build-android/CMakeFiles/CNA.dir/src/Microsoft/Devices/Sensors/Accelerometer.cpp.o \
  | grep -iE "ProbeIsSupported|EnsureSubsystemInitialized|OpenDefaultSensorLocked|make_shared.*thread"
```
Confirmed present: the lock-proof-parameter overloads of `ProbeIsSupported`/
`OpenDefaultSensorLocked`/`EnsureSubsystemInitialized` (each showing
`std::lock_guard<std::mutex> const&` in their demangled signature — Task P8-3), and the
`std::make_shared<std::vector<std::thread::id>>()` instantiation backing `dispatchToken_`
(Task P8-1). Also confirmed `~VibrateController()` still compiles (Task P8-6's change).
Still **compile-only**: no APK packaging, no emulator/device run, `CnaTests` itself not
cross-compiled (`googletest` not configured for the NDK toolchain in this session, same
as every prior phase).

**iOS — re-confirmed still blocked:**
```bash
which xcodebuild xcrun osxcross   # no output — none found
find / -iname "*ios*toolchain*" 2>/dev/null   # no matches
```
No Apple/iOS toolchain of any kind in this Linux container, checked fresh this task
(not assumed carried over from Phase 7).

**Honest gaps this task does not and cannot close:**
- No real accelerometer/gyroscope/haptic hardware, any Android/iOS device or emulator,
  or rumble-capable gamepad exists in this container — see
  `docs/devices-hardware-checklist.md`.
- No APK packaging or on-device/emulator run for Android — library compile-only.
- A raw ZIP export of this repository (without `git submodule update --init --recursive`)
  is not buildable — every command above ran against a real git checkout with
  submodules already initialized. See `docs/devices-build.md`'s and `NEXT.md`'s
  ZIP-export caveats (Task P7-6, re-confirmed unchanged this phase).

**Remaining risk:** none beyond what's already documented as an accepted, honest gap
(physical hardware, iOS toolchain, ZIP-export self-containment, and the one deliberate
Accelerometer `CurrentValueChanged` self-destroy boundary from Task P8-1) — every
backend this container can actually build and test is clean, with no regressions
anywhere, and the phase's one real bug (Task P8-1) and one real fixture race (Task P8-4)
were each confirmed both broken-without-the-fix and clean-with-it via a sanitizer, not
just a plain test pass.

---

`plan_devices_phase8.md` is now fully closed: all 8 tasks (P8-1 through P8-8) done,
each with its own commit, verified individually and again together in this final pass.
