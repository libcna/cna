# Phase 7 — Independent concurrency and backend-boundary audit

Scope: `Microsoft::Devices`/`Microsoft::Devices::Sensors` plus tests/docs only. Branch
`feature/devices`. Builds directly on `plan_devices_phase6.md` (Phase 6 added
`Detail::SdlSensorSubsystem<TSensor>`, `ScopeExit`, `ClaimDisposalOnce()`, and found a
real heap-corruption bug via stress-testing) but treats every Phase 6 claim as a
hypothesis to re-verify, not a given.

## Audit findings

Each point below was verified against the actual current source (not against what
earlier plan docs claimed) before any code was changed.

### A. Cross-class SDL sensor calls are not globally serialized — CONFIRMED, real bug

`Accelerometer::getIsSupportedProperty()` locks `Accelerometer::GetSubsystem().mutex_`;
`Gyroscope::getIsSupportedProperty()` locks `Gyroscope::GetSubsystem().mutex_`. These are
two distinct `Detail::SdlSensorSubsystem<T>` instances (one per template instantiation,
per `SdlSensorSubsystem.hpp`'s own doc comment), hence two distinct `std::mutex` objects.
Nothing prevents a thread constructing an `Accelerometer` (which calls
`ProbeIsSupported()` → real `SDL_InitSubSystem`/`SDL_GetSensors`/`SDL_OpenSensor`/
`SDL_GetSensorType`/`SDL_CloseSensor`/`SDL_QuitSubSystem` calls under
`Accelerometer`'s mutex only) from running fully concurrently with a thread constructing
a `Gyroscope` (the same real SDL calls under `Gyroscope`'s mutex only).

Checked the vendored SDL3 headers directly (`third_party/SDL/include/SDL3/SDL_init.h`):
`SDL_InitSubSystem()` — *"This function should only be called on the main thread."*;
`SDL_QuitSubSystem()` — *"This function is not thread safe."* `SDL_sensor.h`'s
`SDL_GetSensors()`/`SDL_OpenSensor()`/`SDL_GetSensorType()`/`SDL_CloseSensor()` carry **no**
`\threadsafety` annotation at all (unlike most other SDL3 functions, which do), meaning
SDL makes no safety claim for them either — the only reasonable inference, and the one
`plan_devices_phase6.md`'s P6-1 addendum already reached the hard way (a real, reproduced
heap corruption), is to treat the whole sensor-subsystem API surface as unsafe for
concurrent access unless proven otherwise. Two different classes each locking their own
mutex does not serialize any of this against each other. **Confirmed real — fixed in
P7-1.**

By contrast, `SDL_events.h` documents `SDL_AddEventWatch()`/`SDL_RemoveEventWatch()` as
*"safe to call from any thread"* — these do **not** need the new global mutex; they
already touch only per-class subsystem state (`eventWatchRegistered_`), already correctly
serialized by that class's own `subsystem.mutex_`.

### B. Concurrent `Dispose()` can flip `disposed_` before the winning cleanup finishes — CONFIRMED, real bug

Current pattern (`Accelerometer`/`Gyroscope`/`Compass`/`Motion`):
```cpp
if (!getIsDisposedProperty() && disposing && ClaimDisposalOnce())
{
    // derived cleanup, including Stop()
}
SensorBase<T>::Dispose(disposing);   // unconditional — sets disposed_ = true
```
If two threads call `Dispose()` on the same instance concurrently: the winner passes
`ClaimDisposalOnce()` and starts cleanup (which, for `Accelerometer`/`Gyroscope`, calls
public `Stop()` if the instance was started). The loser fails `ClaimDisposalOnce()`,
skips the cleanup block, but still falls through to the unconditional
`SensorBase<T>::Dispose(disposing)` call — **immediately** setting `disposed_ = true`,
while the winner may still be inside `Stop()`. `Stop()`'s own precondition
(`ObjectDisposedException::ThrowIf(getIsDisposedProperty(), ...)`) then throws inside the
*winner's* own call, escaping `Dispose(bool)` before `instanceCount_` is decremented, the
sensor is closed, or `subsystemHeld_` is released — a real resource/count leak plus an
exception escaping `Dispose()`.

For `Accelerometer`/`Gyroscope` this is **directly reachable**: `started_` can legitimately
be `true` at disposal time, so the winner's cleanup really does call `Stop()`.
For `Compass`/`Motion` this is currently **latent, not observable**: `Start()` always
throws immediately (`SensorFailedException`) and never sets `started_ = true`, so the
`if (started_) { Stop(); }` branch in their `Dispose(bool)` is dead code today — the race
exists in the code but nothing currently exercises the `Stop()` call it would affect. Both
existing test files' `ConcurrentDisposeFromMultipleThreadsNeverCorruptsInstanceCount`-style
tests use never-started instances, so they could not have caught this either way.
**Confirmed real for Accelerometer/Gyroscope, latent for Compass/Motion — fixed uniformly
in P7-2** (so the same bug can't resurface if Compass/Motion ever gain a real backend per
`plan_devices_phase5.md`'s native-backend sketch).

### C. Same-thread cross-instance dispose during dispatch can cause a use-after-free — CONFIRMED, real bug, most serious of this pass

`SdlSensorSubsystem<TSensor>::SensorEventWatch()` snapshots `startedInstances_` under
`mutex_`, pushing the *current* thread id into **every** snapshotted instance's
`dispatchingThreadIds_` **before dispatching any of them**, then dispatches each one (in
snapshot order) with the lock released. If instance A's callback (running inside A's own
dispatch, mid-loop) disposes a *different*, not-yet-reached instance B from the same
batch: B already has this thread's id in `dispatchingThreadIds_` (pre-marked, not because
B is actually executing) — so B's `Dispose(bool)` wait predicate
(`dispatchingThreadIds_.size() == selfCount`) is satisfied immediately (it looks
indistinguishable from a legitimate same-thread self-dispose), B is fully disposed (and,
if this was the last reference, destroyed) with no wait at all. The loop then continues to
its next snapshotted entry — which may be the now-dangling `B*` — and calls
`B->ProcessSensorUpdateEvent(...)` on freed memory.

Root cause: the design conflates "this instance was pre-marked pending for this thread"
with "this instance is *currently* executing its callback on this thread" — only the
latter should exempt a same-thread `Dispose()` call from waiting. There is also no
re-validation step before the dispatch loop touches its next snapshotted pointer, so even
independent of the self-dispose semantics, a stale-pointer read was always possible once
any instance in the batch could be disposed mid-loop.

**Confirmed real — fixed in P7-3** by (1) marking `dispatchingThreadIds_` per-instance
immediately before dispatching to that instance (not in bulk upfront), and (2)
re-validating each instance against the live `startedInstances_` list — by pointer value
only, never dereferencing — atomically with that same mark, inside the same lock
acquisition. If a prior instance's callback already disposed (and possibly freed) a later
instance in the batch, that instance will no longer be present in `startedInstances_`
(its own `Dispose(bool)`/`Stop()` removes it before the wait/cleanup that could free it),
so the loop detects this via pure pointer-value comparison and skips it — never
dereferencing the possibly-dangling pointer.

### D. Test-only hooks read/write unguarded state — CONFIRMED, one real gap

`GetSubsystemHeldForTesting()` (both `Accelerometer` and `Gyroscope`) returns
`subsystemHeld_` with **no** lock, while every write to `subsystemHeld_` happens under
`subsystem.mutex_`. Real, if narrow, data race. Everything else already locks correctly:
`SetStartedForTesting()` locks `subsystem.mutex_`; `SetSupportedForTesting()` forwards to
`SensorBase::setIsSupportedProperty()`, which locks `SensorBase`'s own `mutex_`;
`InjectSyntheticSensorUpdate()` locks `subsystem.mutex_` around its `started_` check and
`dispatchingThreadIds_` push. **Confirmed real for `GetSubsystemHeldForTesting()`
only — fixed in P7-4.** New P7-1/P7-3 test hooks are written already-locked from the
start, verified as part of those tasks.

### E. `ScopeExit` is not defensively self-contained — CONFIRMED

`SdlSensorSubsystem.hpp` uses `std::move` inside `ScopeExit`'s constructor and
`MakeScopeExit()` but does not `#include <utility>` directly (currently compiles only
because some other transitively-included header happens to pull it in — not guaranteed by
this header's own contract). `~ScopeExit()` calls `onExit_()` unconditionally with no
exception handling; if the cleanup callable ever threw, throwing out of a destructor
during stack unwinding calls `std::terminate()`. **Confirmed real — fixed in P7-5.**

### F. Documentation must not overstate verification (ZIP-export caveat) — CONFIRMED, addressed in P7-6

`docs/devices-build.md` and `NEXT.md` state build/test results as flatly verified facts.
Correct for *this* session (a real git checkout with submodules initialized), but a raw
ZIP export of this repository — without `git submodule update --init --recursive` having
been run — has empty `third_party/SDL` (and `SDL_image`/`SDL_mixer`, `vendor/googletest`)
directories and will not configure, let alone build. **P7-6 adds an explicit "this is not
a self-contained ZIP" caveat** distinguishing: code actually compiled locally in this
session; tests actually run locally in this session; Android cross-compile (library only,
no test binary, no APK, no device/emulator run); real Android/iOS hardware (never
verified, see `docs/devices-hardware-checklist.md`); and the ZIP-export caveat itself.

---

## Tasks

- P7-1 — Add a shared global SDL sensor API mutex
- P7-2 — Fix concurrent Dispose state ownership
- P7-3 — Fix same-thread cross-instance dispose during dispatch
- P7-4 — Lock test-only getters/setters consistently
- P7-5 — Harden `ScopeExit`
- P7-6 — Documentation accuracy pass
- P7-7 — Final verification

Each task below gets its own commit and its own `### Resolution` subsection, filled in
as that task is completed (not written in advance).

## P7-1: Add a shared global SDL sensor API mutex

### Resolution

**Files changed:**
- `include/Microsoft/Devices/Sensors/Detail/SdlSensorSubsystem.hpp` — added
  `Detail::GetGlobalSdlSensorMutex()`, a process-wide `std::mutex&` (function-local static
  inside an `inline` function — one instance for the whole process). Documented the exact
  lock-order rule: whenever a per-class `SdlSensorSubsystem<TSensor>::mutex_` is also held,
  it must be acquired *before* this mutex, never after.
- `src/Microsoft/Devices/Sensors/Accelerometer.cpp` / `Gyroscope.cpp` (identical changes
  in both):
  - `getIsSupportedProperty()` — dropped `subsystem.mutex_` entirely (it was only ever
    protecting against this class's *own* other SDL calls; `ProbeIsSupported()` touches no
    per-class subsystem state) and now locks only `GetGlobalSdlSensorMutex()`.
  - `Start()` — the real SDL-calling section (`EnsureSubsystemInitialized()` +
    `OpenDefaultSensorLocked()`, including the failure-path `SDL_QuitSubSystem()`) is now
    additionally wrapped in `GetGlobalSdlSensorMutex()`, nested inside the already-held
    `subsystem.mutex_`.
  - `Dispose(bool)` — the real SDL-calling section (`SDL_CloseSensor()` +
    `SDL_QuitSubSystem()`) is likewise additionally wrapped, nested inside
    `subsystem.mutex_`.
- `tests/Microsoft/Devices/Sensors/SensorSubsystemOwnershipTests.cpp` — added
  `ConcurrentCrossClassConstructDestroyProbeDoesNotCrash`: 32 threads (8 groups × 4:
  Accelerometer construct/destroy, Gyroscope construct/destroy, Accelerometer probe,
  Gyroscope probe), 50 iterations each, running fully concurrently — the scenario the
  P6-1-era per-class-only locking could never serialize. Followed by both classes'
  existing 10-instance-cap sanity check.

**Deadlock analysis:** `getIsSupportedProperty()` never holds a per-class `mutex_` while
holding the global mutex (it holds *only* the global mutex), so it can never participate
in a lock-order cycle. `Start()`/`Dispose(bool)` always acquire `subsystem.mutex_` first,
`GetGlobalSdlSensorMutex()` second, consistently in both classes and both methods — a
single fixed nesting order, so no cycle is possible.

**Tests added:** `ConcurrentCrossClassConstructDestroyProbeDoesNotCrash` (new).

**Commands run:**
```bash
cmake --build cmake-build-debug --target CNA -j"$(nproc)"        # clean
cmake --build cmake-build-debug --target CnaTests -j"$(nproc)"   # clean
./cmake-build-debug/CnaTests --gtest_filter="Accelerometer*:Gyroscope*:Compass*:Motion*:VibrateController*:SensorSubsystemOwnership*:AndroidSensorOrientation*:SensorBase*"
# 132 tests, 130 passed, 2 skipped (expected — no real accelerometer/gyroscope hardware)

cd cmake-build-debug
for i in $(seq 1 40); do
  ./CnaTests --gtest_filter="SensorSubsystemOwnershipTests.*:AccelerometerTests.*:GyroscopeTests.*" || echo "run $i FAILED"
done
# 40/40 clean

cd .. && ctest --output-on-failure
# 2035/2037 passed; same 2 pre-existing EasyGL failures as every prior Phase 5/6
# verification (EasyGL_MRT_TwoAttachments, easy-gl-resource-smoke-tests) — unrelated to
# Microsoft::Devices, not fixed here (out of scope).
```

**Remaining risk:** low. The fix is a straightforward, uniformly-applied serialization
with a proven-acyclic lock order, verified by the same stress-loop discipline the P6-1
addendum established (40 clean iterations of real concurrent cross-class contention,
versus the ~12–25% failure rate the P6-1-era per-class-only locking reproduced for the
single-class case). `SensorEventWatch()`'s own real-time SDL sensor event delivery
(`event->sensor.data[...]`) is not itself an SDL_INIT_SENSOR-management call (no
`SDL_InitSubSystem`/`SDL_GetSensors`/`SDL_OpenSensor`/`SDL_CloseSensor`/
`SDL_QuitSubSystem`), so it correctly does not need the global mutex — confirmed by
reading its body, not assumed.

## P7-2: Fix concurrent Dispose state ownership

### Resolution

**Files changed:**
- `include/Microsoft/Devices/Sensors/SensorBase.hpp` — added `#include <condition_variable>`,
  a `disposalFinishedCv_` member, a new protected `WaitForDisposalToComplete()` (blocks
  until `disposed_` becomes true), and updated the base `Dispose(bool)` to call
  `disposalFinishedCv_.notify_all()` after setting `disposed_ = true`. Updated
  `ClaimDisposalOnce()`'s, `Dispose()`'s (public), and `Dispose(bool)`'s (base) doc
  comments to describe the new contract: the winner runs cleanup then calls the base
  `Dispose(bool)`; the loser calls `WaitForDisposalToComplete()` and returns — it must
  never call the base `Dispose(bool)` itself.
- `src/Microsoft/Devices/Sensors/Accelerometer.cpp` / `Gyroscope.cpp` / `Compass.cpp` /
  `Motion.cpp` — restructured `Dispose(bool)` in all four: `disposing == false` now
  delegates straight to the base `Dispose(bool)` (unaffected — the concurrency race only
  applies to the `disposing == true`, explicit-`Dispose()` path); `disposing == true`
  now branches on `ClaimDisposalOnce()` explicitly — the loser calls
  `WaitForDisposalToComplete()` and returns without touching any shared state; the winner
  runs the exact same cleanup as before (unchanged), then calls the base `Dispose(bool)`
  after its own lock scopes have closed (kept the cleanup's `subsystem.mutex_`/global SDL
  mutex acquisitions in an explicit nested block so the final base-`Dispose()` call — and
  its `disposalFinishedCv_.notify_all()` — runs unlocked, matching the pre-P7-2
  lock-duration discipline).
- `include/Microsoft/Devices/Sensors/Accelerometer.hpp` / `Gyroscope.hpp` +
  matching `.cpp` — added a NOXNA test-only `disposalTestHook_` member (a
  `std::function<void()>`, documented as safe to read without a lock because it is only
  ever written in a test's single-threaded setup phase, before any thread that calls
  `Dispose()` is spawned) and `SetDisposalCleanupHookForTesting()`, invoked once by
  `Dispose(bool)` immediately after `ClaimDisposalOnce()` succeeds, before any cleanup
  runs — lets a test force a controlled pause at that exact point.
- `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp` / `GyroscopeTests.cpp` — added
  `ConcurrentDisposeLoserWaitsForWinnerCleanupToFinishBeforeStateAppearsDisposed`: starts
  an instance, uses the new test hook to pause the winner right after it claims disposal,
  confirms a concurrently-racing loser is genuinely still blocked (has not returned) while
  the winner is paused, then releases the winner and confirms both complete cleanly, over
  15 rounds, followed by the existing 10-instance-cap sanity check.

**Compass/Motion:** applied the identical `ClaimDisposalOnce()`/`WaitForDisposalToComplete()`
restructuring for consistency (per the audit's finding B), but did not add an equivalent
delayed-cleanup hook/test for these two — their `Start()` always throws before ever
setting `started_ = true`, so their `Dispose(bool)`'s `if (started_) { Stop(); }` branch
is dead code today; the race was latent, not observable, and their existing
`ConcurrentDisposeFromMultipleThreadsNeverCorruptsInstanceCount` tests (unaffected by
this change, re-verified passing) already cover the reachable "never-started concurrent
dispose" path.

**Regression-proof check (not part of the permanent test suite):** temporarily reverted
the loser branch back to the old pre-P7-2 behavior (falling through to call the base
`Dispose(bool)` immediately instead of waiting) and re-ran the new
`ConcurrentDisposeLoserWaitsForWinnerCleanupToFinishBeforeStateAppearsDisposed` test — it
failed exactly as predicted: the winner's own `Dispose()` call threw
`System::ObjectDisposedException` (from its own internal `Stop()` call observing
`disposed_ == true`), the loser returned immediately instead of waiting, and
`instanceCount_` leaked enough that the trailing 10-instance-cap check itself threw
`SensorFailedException` early. Restored the real fix immediately after confirming this;
all tests pass again with the fix in place.

**Tests added:**
`ConcurrentDisposeLoserWaitsForWinnerCleanupToFinishBeforeStateAppearsDisposed`
(Accelerometer, Gyroscope).

**Commands run:**
```bash
cmake --build cmake-build-debug --target CNA -j"$(nproc)"        # clean
cmake --build cmake-build-debug --target CnaTests -j"$(nproc)"   # clean
./cmake-build-debug/CnaTests --gtest_filter="Accelerometer*:Gyroscope*:Compass*:Motion*:VibrateController*:SensorSubsystemOwnership*:AndroidSensorOrientation*:SensorBase*"
# 134 tests, 132 passed, 2 skipped (expected)

cd cmake-build-debug
for i in $(seq 1 40); do
  ./CnaTests --gtest_filter="AccelerometerTests.*:GyroscopeTests.*:CompassTests.*:MotionTests.*" || echo "run $i FAILED"
done
# 40/40 clean

cd .. && ctest --output-on-failure
# 2037/2039 passed; same 2 pre-existing EasyGL failures as every prior verification.
```

**Remaining risk:** low for Accelerometer/Gyroscope (directly tested, including a
regression-proof failure/fix cycle). Lower confidence but acceptable for Compass/Motion,
since the fixed code path (`Stop()` inside cleanup) is currently unreachable there —
worth revisiting with a dedicated test if Compass/Motion ever gain a real `Start()`
implementation (see `plan_devices_phase5.md`'s native-backend plan). Assumes the winning
cleanup path never throws once `ClaimDisposalOnce()` succeeds (documented in
`WaitForDisposalToComplete()`'s doc comment) — matches this codebase's pre-existing
assumption elsewhere (e.g. `callbackFinished_`'s own no-timeout wait), not a new risk.

## P7-3: Fix same-thread cross-instance dispose during dispatch

### Resolution

**Files changed:**
- `include/Microsoft/Devices/Sensors/Detail/SdlSensorSubsystem.hpp` — extracted the
  dispatch loop's bookkeeping into a new public template method
  `DispatchToInstances(instancesSnapshot, dispatchOne)`, shared by both the real
  `SensorEventWatch()` path and the new NOXNA test hooks (so they can never diverge).
  Per-instance, it now re-validates the pointer against the *live* `startedInstances_`
  list (by pointer value only, never dereferencing until validated) and marks it as
  actively dispatching (`dispatchingThreadIds_`) atomically, immediately before
  dispatching to that specific instance — not in bulk, up front, for the whole snapshot,
  as the old code did. `SensorEventWatch()` itself now just takes a plain pointer-value
  snapshot under the lock and delegates to `DispatchToInstances()` with a lambda calling
  the real (hardware-gated) `ProcessSensorUpdateEvent()`.
- `include/Microsoft/Devices/Sensors/Accelerometer.hpp` / `Gyroscope.hpp` + matching
  `.cpp` — added three NOXNA test-only hooks: `RegisterStartedInstanceForTesting()` /
  `UnregisterStartedInstanceForTesting()` (register/unregister an instance into the
  shared subsystem's `startedInstances_` directly, without a real SDL sensor) and
  `DispatchToInstancesForTesting()` (forwards to the shared `DispatchToInstances()` with
  a lambda calling the private `DispatchSensorReading()` directly — bypassing
  `ProcessSensorUpdateEvent()`'s hardware-presence gate, the same way
  `InjectSyntheticSensorUpdate()` already does, since no real SDL sensor is ever open in
  this headless environment).
- `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp` / `GyroscopeTests.cpp` — added
  `DisposingDifferentInstanceDuringSameBatchDispatchDoesNotUseAfterFree`: registers two
  started instances A and B into a simulated dispatch batch; A's `CurrentValueChanged`
  handler disposes B by resetting B's owning `unique_ptr` (not merely calling `Dispose()`
  — this actually frees B's memory, not just logically disposes it, so a regression
  would touch genuinely freed memory, not just an already-disposed-but-still-allocated
  object); confirms A's handler ran, B's handler never ran, and the whole batch dispatch
  completes without crashing.

**Regression-proof check (not part of the permanent test suite):** temporarily reverted
`DispatchToInstances()` to the old pre-P7-3 "mark every snapshotted instance up front,
no re-validation" bookkeeping and re-ran the new UAF test 5 times — it **segfaulted
5/5 times** (real memory corruption, not just a failed assertion), confirming this is a
genuine, reliably reproducible use-after-free, not a theoretical concern. Restored the
real fix immediately after confirming this; all tests pass again with the fix in place.

**Tests added:** `DisposingDifferentInstanceDuringSameBatchDispatchDoesNotUseAfterFree`
(Accelerometer, Gyroscope).

**Commands run:**
```bash
cmake --build cmake-build-debug --target CNA -j"$(nproc)"        # clean
cmake --build cmake-build-debug --target CnaTests -j"$(nproc)"   # clean
./cmake-build-debug/CnaTests --gtest_filter="Accelerometer*:Gyroscope*:Compass*:Motion*:VibrateController*:SensorSubsystemOwnership*:AndroidSensorOrientation*:SensorBase*"
# 136 tests, 134 passed, 2 skipped (expected)

cd cmake-build-debug
for i in $(seq 1 40); do
  ./CnaTests --gtest_filter="AccelerometerTests.*:GyroscopeTests.*" || echo "run $i FAILED"
done
# 40/40 clean

cd .. && ctest --output-on-failure
# 2039/2041 passed; same 2 pre-existing EasyGL failures as every prior verification.
```

**Remaining risk:** low. This is the most serious bug found this phase (a genuinely
reproducible use-after-free, not just a data race or a resource leak), and it now has a
dedicated regression test verified to actually crash without the fix (5/5 segfaults) and
pass reliably with it (40 stress-loop iterations, full suite). One acknowledged
limitation, unchanged from before this task and out of scope for it: a handler that
disposes its *own* sender reentrantly (same instance, not a different one) still uses
the pre-existing Task P5-2/P5-3 self-dispose exemption — that mechanism is untouched by
this fix and still relies on the object remaining valid (not literally freed) through the
remainder of its own callback frame, a pre-existing, already-documented design tradeoff
from Phase 5, not something this task's scope covers.
