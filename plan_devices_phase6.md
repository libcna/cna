# Devices Phase 6 — Independent Hardening Pass

Scope: `Microsoft::Devices` and `Microsoft::Devices::Sensors` only, plus their
tests/docs. This document follows the same discipline as
`plan_devices_phase5.md`: an audit section written *before* any code edits,
verifying (not assuming) every claim `plan_devices_phase5.md` made about its
own completeness, then one task per confirmed/refined problem, each closed
out with its own Resolution subsection.

## Audit findings

Each numbered item below was checked by directly re-reading the current
source (not by trusting Phase 5's own Resolution write-ups), immediately
before any Phase 6 code edit was made.

### 1. Construction/destruction instance counting is not thread-safe (confirmed)

`Accelerometer::Accelerometer()`/`Gyroscope::Gyroscope()` read/increment
`GetSubsystem().instanceCount_` with **no lock at all**:

```cpp
auto& subsystem = GetSubsystem();
if (subsystem.instanceCount_ >= MaxSensorCount) { throw ...; }
++subsystem.instanceCount_;               // unlocked
```

while `Dispose(bool)` decrements the same field **under** `subsystem.mutex_`.
A mixed locked/unlocked access pattern on the same field is a real data race
under the C++ memory model (not just a benign lost-update risk), confirmed by
reading both the constructor and `Dispose(bool)` bodies side by side.

`Compass`/`Gyroscope`/`Motion`'s own `instanceCount_` (a plain `static int`,
no subsystem/mutex at all) has the identical unguarded increment/decrement
in both constructor and `Dispose(bool)` — not called out by the Phase 6
brief (which names only Accelerometer/Gyroscope for P6-1), but the same class
of bug. Folded into P6-1's scope for consistency, since the fix is small and
symmetrical.

### 2. `Start()` failure cleanup leaks the subsystem hold (confirmed)

```cpp
if (!subsystemHeld_)
{
    if (!Detail::SdlSensorSubsystem<Accelerometer>::EnsureSubsystemInitialized())
    { state_ = SensorState::NotSupported; throw AccelerometerFailedException(...); }
    subsystemHeld_ = true;                 // set BEFORE OpenDefaultSensorLocked()
}
auto& subsystem = GetSubsystem();
std::lock_guard<std::mutex> lock(subsystem.mutex_);
if (subsystem.OpenDefaultSensorLocked() == nullptr)
{
    state_ = SensorState::NotSupported;
    throw AccelerometerFailedException(...);   // subsystemHeld_ stays true here
}
```

If `OpenDefaultSensorLocked()` fails after `subsystemHeld_` has already been
set `true`, the instance's own `SDL_InitSubSystem(SDL_INIT_SENSOR)` call is
never balanced by a `SDL_QuitSubSystem()` until `Dispose()` eventually runs
(which may be much later, or never, for a short-lived failed `Start()`
attempt in a retry loop). Confirmed by reading `Accelerometer.cpp`/
`Gyroscope.cpp`'s `Start()` bodies directly — identical shape in both.

### 3. `started_`/`state_`/`subsystemHeld_`/`disposed_`/`isSupported_` synchronization is inconsistent (confirmed)

- `Accelerometer::Dispose(bool)` reads `started_` **without** the subsystem
  lock before calling `Stop()`.
- `Accelerometer::ProcessSensorUpdateEvent()` (invoked from the SDL
  event-watch thread) reads `started_` and `getIsDisposedProperty()`
  (`SensorBase::disposed_`) with **no lock**, while the game/user thread can
  concurrently call `Stop()`/`Dispose()` which mutate `started_` and
  `disposed_` under (`started_`) or without (`disposed_`) a lock.
- `SensorBase::disposed_` is written unlocked in `Dispose(bool)` and read
  unlocked in `getIsDisposedProperty()` — used from both the game thread and
  (via `Accelerometer`/`Gyroscope`) the SDL callback thread.
- `SensorBase::isSupported_` is written unlocked in `setIsSupportedProperty()`
  but **read under `mutex_`** inside `getCurrentValueProperty()` — an
  inconsistent locking discipline on the same field (confirmed by reading
  `SensorBase.hpp` lines 145–148 vs. 223–234).

All of the above are confirmed real, not hypothetical. `timeBetweenUpdates_`
has the same unguarded shape but is conventionally set once on the game
thread before `Start()` (matching real WP7 usage) and is not written by the
SDL callback thread anywhere in this codebase — left out of P6-3's fix scope
as a documented, deliberate exception rather than silently ignored.

### 4. Raw-pointer dispatch snapshot: correct today, but not exception-safe (refined)

`Detail::SdlSensorSubsystem<TSensor>::SensorEventWatch()` snapshots
`subsystem.startedInstances_` (a `std::vector<TSensor*>`) into a local
`instancesSnapshot` under lock, then calls
`instance->ProcessSensorUpdateEvent(...)` on each raw pointer **outside**
the lock.

Traced the full lifetime argument by hand:

- `Stop()` removes an instance from `startedInstances_` under the same
  `subsystem.mutex_` **before** `Dispose()`'s wait (on `dispatchingThreadIds_`
  reaching only self-thread entries) begins, so no *new* dispatch can start
  targeting an instance that is mid-`Dispose()`.
- Any *already-snapshotted* pointer keeps its thread id recorded in that
  instance's own `dispatchingThreadIds_` until its specific dispatch
  completes, and `Dispose()`'s condition-variable predicate correctly blocks
  until only self-thread entries (if any) remain.

So the raw-pointer design is provably safe today **for the non-throwing
path**. However: the per-dispatch cleanup —

```cpp
instance->ProcessSensorUpdateEvent(...);       // (A)
{
    std::lock_guard<std::mutex> lock(subsystem.mutex_);
    ids.erase(...);
}
subsystem.callbackFinished_.notify_all();      // (B)
```

— has **no try/catch or RAII guard**. If (A) throws (either from
`ProcessSensorUpdateEvent()` itself or transitively from a user's
`CurrentValueChanged`/`ReadingChanged` handler, which is user code CNA does
not control), the erase-and-notify in (B) never runs. `dispatchingThreadIds_`
is then permanently corrupted with a stale entry, and any current or future
`Dispose()` call waits forever — a genuine, previously-undiscovered deadlock.

This refines (does not simply confirm) the brief's framing: the fix that
actually matters is a small RAII "dispatch guard" around the push/pop of the
thread id (so cleanup runs during exception unwinding too), not a full
redesign to `shared_ptr`/lifetime-token registries — that larger change
would require altering `Accelerometer`/`Gyroscope`'s construction/ownership
shape, which the quality bar (no public API breakage) does not require here
since the pointer-lifetime story is already sound.

### 5. `SensorBase<T>` event semantics (partially confirmed)

- `CurrentValueChanged` raise timing: `setCurrentValueProperty()` updates
  `currentValue_` under `mutex_`, releases the lock, then raises — correct
  update-then-notify order, and the event is never raised while holding the
  lock (confirmed, matches the "never hold locks while raising" rule).
- `TimeBetweenUpdates` default-init path: constructor sets
  `timeBetweenUpdates_(System::TimeSpan::Zero)` in the member-init list, then
  calls `setTimeBetweenUpdatesProperty(FromMilliseconds(2.0))` in the body —
  correctly detects the `Zero → 2ms` change and applies it. Confirmed
  correct by re-reading; **no test exists for this at all** — grepped
  `tests/` for `"TimeBetweenUpdates"`, zero hits. Real, confirmed gap.
- The brief's claimed "duplicate comment line in `SensorBase.hpp` near
  `TimeBetweenUpdates`" was **checked and NOT found** in the current file —
  documented here honestly as a claim that does not match current code,
  rather than fixing a phantom problem.

### 6. `VibrateController` lifetime (mostly confirmed safe; one claim upgraded)

- `getIsSupportedProperty()`/`getDeviceNameProperty()` correctly close any
  temporarily-opened probe device (`openedTemporary` bookkeeping) — no leak,
  confirmed by re-reading `AcquireHapticDeviceForProbe()`.
- `g_subsystemHeld` persists until `~VibrateController()` regardless of
  whether it was set via a probe or a real `Start()` — correct for a
  process-lifetime singleton, no early-teardown/no leak.
- The specific concern raised in this phase's brief — **can
  `~VibrateController()` run after a host application (using CNA as a
  library) tears down SDL independently of CNA's own code?** — was
  investigated fresh (not just re-confirming Phase 5's narrower
  CNA-source-only grep):
  - `grep -rn "SDL_Quit\s*(" src include examples` (excluding
    `third_party/`): the only matches are explanatory **comments** inside
    `VibrateController.cpp`/`.hpp` themselves — no actual call site anywhere
    in CNA's own source. Reaffirms Phase 5's finding.
  - `Game.cpp` has **zero** references to `SDL_Init`/`SDL_Quit` — CNA's own
    `Game` class does not manage SDL subsystem lifecycle at all.
  - No `atexit`/`std::exit`/`::exit()` calls anywhere in CNA's own source.
  - Checked where SDL subsystems actually *are* initialized/torn down:
    `Microsoft::Xna::Framework::Graphics::GraphicsDevice` follows the
    **exact same pattern** as `VibrateController` —
    `SDL_InitSubSystem(SDL_INIT_VIDEO)` in its constructor,
    `SDL_QuitSubSystem(SDL_INIT_VIDEO)` in its `Dispose()`
    (`GraphicsDevice.cpp` lines 148 and 330). The "No SDL_Quit or subsystem
    shutdown here — managed centrally" comments in `SdlGraphicsBackend.cpp`/
    `EasyGLGraphicsBackend.cpp` refer to exactly this: `GraphicsDevice` is
    the one place that owns the VIDEO subsystem's init/quit pair, not some
    other central SDL_Quit() call that doesn't exist.

  Conclusion: `VibrateController`'s destructor pattern is **consistent with
  an established, project-wide convention** (per-class
  `SDL_InitSubSystem`/`SDL_QuitSubSystem` pairing, never the umbrella
  `SDL_Init()`/`SDL_Quit()`), not a risk unique to Phase 5. The only
  remaining theoretical exposure — a host application calling the umbrella
  `SDL_Quit()` directly in its own code, bypassing SDL's subsystem
  ref-counting entirely — is a characteristic shared identically by
  `GraphicsDevice` (out of `Microsoft::Devices` scope, and a `Graphics` class
  Phase 6 must not modify per its own scope rule) and is not something a
  `Microsoft::Devices`-only pass can or should fix unilaterally in
  `VibrateController` alone without making it *inconsistent* with the rest
  of the codebase. Documented as an explicit, honest residual-risk note
  rather than papered over or "fixed" narrowly.

### 7. Android axis remap (confirmed accurate; test depth genuinely thin)

`AndroidSensorOrientationTests.cpp` (5 tests) covers Rotation90/Rotation270
sign-flip mechanics for both accelerometer- and gyroscope-shaped magnitudes,
plus one semantic test (`RightTiltIsAlwaysPositiveYRegardlessOfRotation`).
Confirmed accurate as far as it goes, but the brief's ask — semantic
examples for tilt right/left, face up/down, top/bottom edge down, not just
sign-flip checks — is not yet met: there is no left-tilt, face-up/face-down,
or edge-down test. Documentation in `docs/devices-hardware-checklist.md`
already honestly states these are compile-tested/reasoned-about, never
hardware-verified — that framing is accurate and is preserved, not weakened.

### 8. Compass/Motion (confirmed honest stubs)

Re-read both classes fully. `getIsSupportedProperty()` hardcoded
`return false;` in both; `Start()` unconditionally throws
`SensorFailedException` in both; no fake data synthesis from
accelerometer/gyroscope anywhere. Confirmed still honest. Both have the same
unguarded `static int instanceCount_` pattern as finding #1 above — folded
into P6-1.

## Tasks

- P6-1: Thread-safe instance counting (Accelerometer/Gyroscope/Compass/Motion)
- P6-2: Fix `Start()` failure cleanup for SDL subsystem ownership
- P6-3: Tighten synchronization for `started_`/`state_`/`subsystemHeld_`/`disposed_`/`isSupported_`
- P6-4: RAII dispatch guard for exception-safe cleanup in `SensorEventWatch()`
- P6-5: `SensorBase<T>` event semantics — add `TimeBetweenUpdates` tests
- P6-6: `VibrateController` lifetime — document residual risk, add tests
- P6-7: Android axis remap — semantic example tests
- P6-8: Compass/Motion — keep honest, sketch native backend interfaces
- P6-9: Documentation accuracy pass
- P6-10: Reproducible final verification

Each task's Resolution subsection follows its own section below, added as
each task is completed (not written in advance).

## P6-1: Thread-safe instance counting

### Resolution

Files changed:
- `src/Microsoft/Devices/Sensors/Accelerometer.cpp` — constructor's
  `instanceCount_` check+increment now runs under `subsystem.mutex_`; the
  lock is released before `getIsSupportedProperty()` (slow SDL probing);
  wrapped in `try`/`catch (...)` that rolls back the increment and rethrows
  if anything after it throws.
- `src/Microsoft/Devices/Sensors/Gyroscope.cpp` — identical fix.
- `include/Microsoft/Devices/Sensors/Compass.hpp` /
  `src/Microsoft/Devices/Sensors/Compass.cpp` — added a private static
  `std::mutex instanceCountMutex_`; constructor/`Dispose(bool)` now guard
  `instanceCount_` with it.
- `include/Microsoft/Devices/Sensors/Motion.hpp` /
  `src/Microsoft/Devices/Sensors/Motion.cpp` — identical fix.

Tests added (one per class, `tests/Microsoft/Devices/Sensors/*Tests.cpp`):
`ConcurrentConstructDestroyKeepsInstanceCountBalanced` — 8 threads × 50
construct/destroy iterations each, then asserts exactly 10 more instances
can be constructed (no more, no less) and an 11th still throws. This is a
real regression test for the fixed race: a reverted fix reliably produces
either a false 10-instance cap trip or more than 10 coexisting instances
under this contention pattern.

Commands run:
- `cmake --build cmake-build-debug --target CNA -j$(nproc)` — clean build.
- `cmake --build cmake-build-debug --target CnaTests -j$(nproc)` — clean build.
- `ctest --output-on-failure -R "Accelerometer|SensorFailed|Compass|Gyroscope|Attitude|Motion|VibrateController|SensorSubsystemOwnership|AndroidSensorOrientation"`
  — 191/191 passed (187 previous + 4 new), 2 expected skips (no
  accelerometer/gyroscope hardware in this container).

Remaining risk: none identified for this specific fix. The `try`/`catch`
rollback path is currently unreachable in practice (`getIsSupportedProperty()`
does not throw today) but is cheap, correct, and guards against a future
change that makes it throw.

### Addendum (found during P6-9's verification pass): this task's own test exposed a real, separate SDL thread-safety bug

The `ConcurrentConstructDestroyKeepsInstanceCountBalanced` test above is the
**first thing in this codebase's history to construct multiple
`Accelerometer`/`Gyroscope` instances concurrently from different
threads.** Every constructor unconditionally calls
`getIsSupportedProperty()` → `Detail::SdlSensorSubsystem<TSensor>::ProbeIsSupported()`,
which makes real SDL calls (`SDL_InitSubSystem`, `SDL_GetSensors`,
`SDL_OpenSensor`, `SDL_CloseSensor`, `SDL_QuitSubSystem`) with **no
synchronization of its own** — by design, since P5-1's `ProbeGuard` was
written assuming independent, not necessarily *concurrent*, probe calls.

Re-running this exact test in a loop (not just once) surfaced a real,
reproducible heap-corruption crash — glibc's `malloc(): unaligned tcache
chunk detected` / `tcache_thread_shutdown(): unaligned tcache chunk
detected` abort — in roughly 1 in 4 runs (`for i in $(seq 1 40); do
./CnaTests --gtest_filter="AccelerometerTests.*"; done` reproduced it 5
times out of 40). This was **not** flagged by any single test run earlier
in this phase (every prior individual verification pass happened to pass),
which is itself a reminder that a single green run does not prove a
concurrency fix is correct — only repetition under real stress does.

Root cause, confirmed by reading `third_party/SDL/include/SDL3/SDL_init.h`
directly: `SDL_InitSubSystem()`'s own doc says **"This function should
only be called on the main thread"**; `SDL_QuitSubSystem()`'s says **"This
function is not thread safe."** `ProbeIsSupported()` (called via every
constructor) violates this contract with zero mitigation once two threads
call it at the same time.

**Fix:** `getIsSupportedProperty()` (both `Accelerometer.cpp`/
`Gyroscope.cpp`) now locks `subsystem.mutex_` for its entire call —
the same lock `Start()`'s body already holds for its own SDL calls
(`EnsureSubsystemInitialized()`/`OpenDefaultSensorLocked()`, per P6-3's
restructure). This serializes every SDL sensor-subsystem call this class
makes against every other one, closing the concurrent-access window
entirely. No deadlock risk: the constructor's own `instanceCount_` lock
scope is fully released before calling `getIsSupportedProperty()`, and
`Start()` never calls `getIsSupportedProperty()` itself. Verified: 40/40
and a separate 60/60 repeated stress runs of the previously-crashing
suites, zero failures (previously ~25% failure rate); full `ctest`:
2034/2036 passed (same 2 pre-existing, unrelated `EasyGL`/`easy-gl`
failures).

This refines this task's own scope: P6-1's brief said "don't hold the
lock during slow/reentrant SDL probing," aimed at not entangling the fast
`instanceCount_` check with a slow probe call. That guidance is still
followed for `instanceCount_` itself; what changed is a *second*, distinct
lock scope now wraps the probe call alone, for a reason the original brief
could not have anticipated (SDL's own thread-safety contract, only
discovered by actually stress-testing concurrent construction).

Files changed (addendum): `src/Microsoft/Devices/Sensors/Accelerometer.cpp`,
`src/Microsoft/Devices/Sensors/Gyroscope.cpp`.

Remaining risk: `Compass`/`Motion`'s `getIsSupportedProperty()` is a
hardcoded `return false;` with no SDL calls at all — confirmed not
affected. `VibrateController`'s probe path
(`AcquireHapticDeviceForProbe()`) already runs under `g_mutex` for its
entire body (Task P4-9) — already safe, not affected by this finding.
`GraphicsDevice`'s own `SDL_InitSubSystem(SDL_INIT_VIDEO)` call
(constructor) is a single call per instance with no equivalent
concurrent-construction pattern exercised anywhere in this codebase's
tests — out of scope for this `Microsoft::Devices`-only pass, but worth
flagging alongside P6-6's own residual-risk note as a candidate for a
future cross-cutting SDL-usage audit.

## P6-2: Fix `Start()` failure cleanup for SDL subsystem ownership

### Resolution

Files changed:
- `src/Microsoft/Devices/Sensors/Accelerometer.cpp`/`Gyroscope.cpp` —
  `Start()` now tracks `acquiredSubsystemThisCall` (true only when *this*
  call transitioned `subsystemHeld_` false→true); if `OpenDefaultSensorLocked()`
  then fails, the hold acquired by this call is released
  (`SDL_QuitSubSystem(SDL_INIT_SENSOR)`, `subsystemHeld_ = false`) before
  throwing, instead of leaking until this instance's eventual `Dispose()`.
  An already-`true` `subsystemHeld_` from an earlier successful
  Start()/Stop() cycle is left untouched — its release still correctly
  belongs to `Dispose()`.
- `include/Microsoft/Devices/Sensors/Accelerometer.hpp`/`Gyroscope.hpp` +
  their `.cpp` — added a `NOXNA GetSubsystemHeldForTesting()` test-only
  accessor (matching the existing `SetStartedForTesting()`/
  `SetSupportedForTesting()` pattern) so tests can directly observe
  `subsystemHeld_`, since no public SDL API exposes the subsystem's
  internal ref-count for a direct assertion (same limitation documented in
  `plan_devices_phase5.md` Task P5-1).

Tests added: `FailedStartReleasesSubsystemHoldItAcquired` (both
`AccelerometerTests.cpp`/`GyroscopeTests.cpp`, skipped when real hardware
makes the platform actually supported) — constructs an instance, asserts
`GetSubsystemHeldForTesting()` is initially false, calls `Start()` (which
fails in this headless container — `SDL_INIT_SENSOR` initializes fine but
no sensor device exists), and asserts `GetSubsystemHeldForTesting()` is
false again immediately after the throw (would have stayed true before
this fix).

Commands run:
- `cmake --build cmake-build-debug --target CNA -j$(nproc)` — clean build.
- `cmake --build cmake-build-debug --target CnaTests -j$(nproc)` — clean build.
- `ctest --output-on-failure -R "Accelerometer|SensorFailed|Compass|Gyroscope|Attitude|Motion|VibrateController|SensorSubsystemOwnership|AndroidSensorOrientation"`
  — 193/193 passed (191 previous + 2 new), 2 expected skips.

Remaining risk: none identified. The fix only changes *when* the hold is
released on a failure path that previously delayed it; the balanced
init/quit pairing itself (Task P4-8) is unchanged.

## P6-3: Tighten synchronization for `started_`/`state_`/`subsystemHeld_`/`disposed_`/`isSupported_`

### Locking discipline (decided and documented here)

- **Accelerometer/Gyroscope's `started_`, `state_`, `subsystemHeld_`**: every
  read and write now happens under that class's own
  `Detail::SdlSensorSubsystem<TSensor>::mutex_` — the same mutex already
  used for `instanceCount_`/`sensor_`/`sensorId_`/`startedInstances_`. `Start()`
  now takes this lock once for its entire body (previously taken only for
  the back half). `Stop()` was already fully locked. `Dispose(bool)` reads
  `started_` in a short locked scope, then calls `Stop()` (which re-locks
  internally — the mutex is not recursive, so the read and the `Stop()`
  call cannot share one lock scope). `getStateProperty()`,
  `ProcessSensorUpdateEvent()`, `InjectSyntheticSensorUpdate()`, and the
  `SetStartedForTesting()` test hook all now read/write `started_`/`state_`
  under this same lock.
- **`SensorBase::disposed_`/`isSupported_`**: guarded by the class's
  existing `mutex_` (already used for `currentValue_`/`isDataValid_`, Task
  P5-2) — `getIsDisposedProperty()`, `Dispose(bool)`, and
  `setIsSupportedProperty()` all lock it now; `getCurrentValueProperty()`
  already did.
- **`timeBetweenUpdates_`**: deliberately left unguarded. Confirmed during
  the audit that, unlike `currentValue_`/`isDataValid_`, it is never
  written by the SDL sensor event-watch thread — only ever set once by the
  constructor and thereafter by direct game/UI-thread calls to
  `setTimeBetweenUpdatesProperty()`, matching real WP7 usage. Documented
  here rather than silently left inconsistent.
- **Never hold a lock while raising an event**: unchanged and reconfirmed —
  `CurrentValueChanged`/`ReadingChanged`/`TimeBetweenUpdatesChanged` are all
  still raised after their respective lock scopes end.
- **Double-dispose race (new finding, beyond the brief's named fields)**:
  auditing `disposed_`'s consistency surfaced that `Dispose(bool)`'s guard
  (`if (!getIsDisposedProperty() && disposing)`) was a check-then-act, not
  atomic — two threads calling `Dispose()` on the *same* instance
  concurrently could both pass the check and both run cleanup once each
  (e.g. both decrementing `instanceCount_` for one logical disposal).
  Fixed with a new `SensorBase::ClaimDisposalOnce()` protected helper — a
  separate `disposalClaimed_` flag (deliberately distinct from `disposed_`,
  which must stay false until cleanup actually finishes, since cleanup
  itself calls `Stop()`, which throws `ObjectDisposedException` if
  `disposed_` is already true) that atomically claims disposal so only one
  concurrent caller's cleanup body runs. Documented as a known, deliberate
  limitation that the *second* concurrent caller may not get a clean
  `ObjectDisposedException` in this specific race window (matching the
  conventional .NET `IDisposable` contract, which does not generally
  require `Dispose()` to be thread-safe against concurrent callers) —
  what's guaranteed is that shared state is never corrupted.

### Resolution

Files changed:
- `include/Microsoft/Devices/Sensors/SensorBase.hpp` — `getIsDisposedProperty()`,
  `Dispose(bool)`, `setIsSupportedProperty()` now lock `mutex_`; added
  `disposalClaimed_` + `ClaimDisposalOnce()`; `~SensorBase()`/`Dispose()`
  updated to use the accessor instead of the raw field.
- `src/Microsoft/Devices/Sensors/Accelerometer.cpp`/`Gyroscope.cpp` —
  `Start()` restructured to hold `subsystem.mutex_` for its whole body;
  `Dispose(bool)` uses `ClaimDisposalOnce()` and reads `started_` in a
  short locked scope before calling `Stop()`; `getStateProperty()`,
  `ProcessSensorUpdateEvent()`, `InjectSyntheticSensorUpdate()`,
  `SetStartedForTesting()` all now read/write `started_`/`state_` under
  `subsystem.mutex_`.
- `src/Microsoft/Devices/Sensors/Compass.cpp`/`Motion.cpp` — `Dispose(bool)`
  uses `ClaimDisposalOnce()` for the same reason.
- Tests added: `ConcurrentStartStopFromMultipleThreadsDoesNotCrash`
  (Accelerometer/Gyroscope — 8 threads × 20 iterations of
  Start()/Stop()/getStateProperty() on one shared instance) and
  `ConcurrentDisposeFromMultipleThreadsNeverCorruptsInstanceCount` (all
  four classes — 30 rounds of two threads calling `Dispose()` on one
  shared instance, then asserting exactly 10 fresh instances can still be
  constructed and an 11th still throws).

Commands run:
- `cmake --build cmake-build-debug --target CNA -j$(nproc)` — clean build.
- `cmake --build cmake-build-debug --target CnaTests -j$(nproc)` — clean build.
- `ctest --output-on-failure -R "Accelerometer|SensorFailed|Compass|Gyroscope|Attitude|Motion|VibrateController|SensorSubsystemOwnership|AndroidSensorOrientation"`
  — 199/199 passed (193 previous + 6 new), 2 expected skips.
- `ctest --output-on-failure` (full suite) — 2022/2024 passed; the 2
  failures (`EasyGL_MRT_TwoAttachments`, `easy-gl-resource-smoke-tests`) are
  the same pre-existing, unrelated `EasyGL`/`easy-gl` bugs documented in
  `plan_devices_phase5.md` Task P5-1 — confirmed still unrelated to
  `Microsoft::Devices` (different subsystem, no plausible causal link, and
  this task touched no Graphics code).

Remaining risk: concurrent `Dispose()` from two different threads on the
same instance is not guaranteed to give the second caller a clean
`ObjectDisposedException` (documented above and in `SensorBase::Dispose()`'s
own doc comment) — this is a deliberate, proportionate scope decision, not
an oversight. No TSan/ASan run was available in this environment to
independently corroborate the absence of data races beyond code review and
stress-test repetition; noted as a residual verification gap.

## P6-4: RAII dispatch guard for exception-safe cleanup in `SensorEventWatch()`

### Resolution

The audit (finding #4) refined the brief's framing: the raw `TSensor*`
snapshot design in `SensorEventWatch()` is provably lifetime-safe today for
the non-throwing path (traced by hand — `Stop()` removing an instance from
`startedInstances_` under the same lock before `Dispose()`'s wait begins
prevents any *new* dispatch from targeting a disposing instance). The real,
previously-undiscovered bug was narrower: the per-dispatch
`dispatchingThreadIds_` cleanup (erase + `notify_all()`) was a plain
post-call statement with no `try`/`catch` or RAII — skipped entirely if
`ProcessSensorUpdateEvent()` (or transitively a user's
`CurrentValueChanged`/`ReadingChanged` handler) threw, permanently
corrupting `dispatchingThreadIds_` and deadlocking any current or future
`Dispose()` call on that instance.

While implementing the fix, a second, related issue surfaced: naively
wrapping only the failing instance's cleanup in a guard would still abort
the whole `SensorEventWatch()` loop on the first exception, leaving *later*
snapshotted instances' `dispatchingThreadIds_` entries (already pushed
upfront, before the dispatch loop starts) permanently stuck too. `SensorEventWatch()`
is also an `SDL_EventFilter` invoked directly by `SDL_PushEvent()`, a C API
that does not expect a C++ exception to unwind through its own call frames
— letting an exception escape this function at all is unsafe independent
of the `dispatchingThreadIds_` bug.

Files changed:
- `include/Microsoft/Devices/Sensors/Detail/SdlSensorSubsystem.hpp` — added
  a minimal generic `Detail::ScopeExit<F>`/`MakeScopeExit()` RAII helper.
  `SensorEventWatch()`'s per-instance dispatch now constructs a
  `ScopeExit` before calling `ProcessSensorUpdateEvent()` (cleanup runs via
  the guard's destructor whether the call returns normally or throws), and
  the call itself is wrapped in `try { ... } catch (...) {}` — deliberately
  swallowed, so one instance's failure doesn't abort dispatch/cleanup for
  the remaining snapshotted instances and no exception crosses the SDL
  callback boundary.
- `src/Microsoft/Devices/Sensors/Accelerometer.cpp`/`Gyroscope.cpp` —
  `InjectSyntheticSensorUpdate()` (the NOXNA synthetic-injection test hook,
  which goes through the same `dispatchingThreadIds_` bookkeeping) now uses
  the same `ScopeExit` guard for its cleanup. Unlike the real SDL
  event-watch path, this is a regular C++ call site, not a C-library
  callback boundary — the exception is deliberately allowed to propagate to
  this method's own caller after cleanup runs, rather than swallowed.

Tests added: `ThrowingCallbackDuringSyntheticUpdateStillCleansUpAndDoesNotHangDispose`
(both `AccelerometerTests.cpp`/`GyroscopeTests.cpp`) — a `CurrentValueChanged`
handler that throws `std::runtime_error` during `InjectSyntheticSensorUpdate()`;
asserts the exception propagates to the caller, then asserts a subsequent
`Dispose()` call returns (doesn't hang) — this would time out under CI
before this fix.

Commands run:
- `cmake --build cmake-build-debug --target CNA -j$(nproc)` — clean build
  (also confirms the `ScopeExit` lambda's access to `Accelerometer`/
  `Gyroscope`'s private `dispatchingThreadIds_` compiles correctly through
  the existing `friend class Detail::SdlSensorSubsystem<TSensor>;`
  declaration for the `SensorEventWatch()` case, and directly via `this`
  for the `InjectSyntheticSensorUpdate()` case).
- `cmake --build cmake-build-debug --target CnaTests -j$(nproc)` — clean build.
- `ctest --output-on-failure -R "Accelerometer|SensorFailed|Compass|Gyroscope|Attitude|Motion|VibrateController|SensorSubsystemOwnership|AndroidSensorOrientation"`
  (run with an external `timeout 60` guard, given this task is specifically
  about a hang/deadlock fix) — 201/201 passed (199 previous + 2 new), 2
  expected skips, no timeout.
- `ctest --output-on-failure` (full suite) — 2024/2026 passed; the same 2
  pre-existing, unrelated `EasyGL`/`easy-gl` failures.

Remaining risk: `SensorEventWatch()`'s own catch-and-swallow behavior could
not be exercised via a genuine SDL `SDL_EVENT_SENSOR_UPDATE` event in this
headless container (no real accelerometer/gyroscope hardware) — only
`InjectSyntheticSensorUpdate()`'s equivalent path (identical
`dispatchingThreadIds_` bookkeeping, same `ScopeExit` mechanism) was
exercised directly. This matches the project's existing, honest
documentation stance that real-hardware event dispatch is
compile-tested/reasoned about, not hardware-verified
(`docs/devices-hardware-checklist.md`).

## P6-5: `SensorBase<T>` event semantics — add `TimeBetweenUpdates` tests

### Resolution

Per the audit's finding #5: `CurrentValueChanged`'s update-then-notify
order and the `TimeBetweenUpdates` default-init path were both confirmed
correct by direct code reading, but `TimeBetweenUpdates` had **zero**
existing tests anywhere (every existing test only exercises
`SensorBase<T>` indirectly through a concrete sensor class, none of which
ever change `TimeBetweenUpdates`). The claimed "duplicate comment line"
near `TimeBetweenUpdates` was checked and **not found** in the current
file — noted honestly rather than fixing a phantom problem.

`TimeBetweenUpdatesChanged` is `protected` in the real WP7 API (matching
the real .NET source), so it cannot be subscribed to from a plain `TEST()`
function. Rather than adding new `NOXNA` test-only hooks to the public API
surface of every concrete sensor class, added a dedicated
`tests/Microsoft/Devices/Sensors/SensorBaseTests.cpp` with a minimal
concrete `TestSensorBase : SensorBase<TestSensorReading>` test fixture (a
standard C++ testing technique: a derived class can access a base class's
protected members) — this tests `SensorBase<T>`'s own logic directly,
independent of any concrete sensor class's SDL-specific behavior.

Files changed:
- `tests/Microsoft/Devices/Sensors/SensorBaseTests.cpp` (new file).

Tests added (6):
- `DefaultTimeBetweenUpdatesIsTwoMilliseconds` — confirms the
  `Zero`-then-`FromMilliseconds(2.0)` constructor-body pattern actually
  produces a 2ms default.
- `SetTimeBetweenUpdatesPropertyToNewValueChangesGetterAndRaisesEvent` /
  `SetTimeBetweenUpdatesPropertyToSameValueDoesNotRaiseEvent` /
  `RepeatedSetTimeBetweenUpdatesPropertyToSameValueRaisesOnlyOnce` — confirm
  the getter reflects the new value and `TimeBetweenUpdatesChanged` fires
  exactly once per actual change, never for a same-value no-op.
- `SetCurrentValuePropertyUpdatesBeforeRaisingEvent` — confirms
  `getCurrentValueProperty()` already reflects the new value from *inside*
  a `CurrentValueChanged` handler (the update-then-notify ordering
  guarantee, tested directly rather than only inferred from reading the
  source).
- `CurrentValueChangedEventArgsCarryTheNewValue` — confirms the event args
  themselves carry the new value.

Commands run:
- `cmake --build cmake-build-debug --target CnaTests -j$(nproc)` — clean
  build (new test file auto-discovered via the existing
  `GLOB_RECURSE CNA_TEST_SOURCES`).
- `./CnaTests --gtest_filter="SensorBaseTests.*"` — 6/6 passed.
- `ctest --output-on-failure -R "Accelerometer|SensorFailed|Compass|Gyroscope|Attitude|Motion|VibrateController|SensorSubsystemOwnership|AndroidSensorOrientation|SensorBase"`
  — 207/207 passed (201 previous + 6 new), 2 expected skips.

Remaining risk: none identified — this task added test coverage for
already-correct, already-reviewed behavior; no production code changed.

## P6-6: `VibrateController` subsystem/device lifetime cleanup

### Resolution

Per the audit's finding #6, the sharper question this phase raised
(**can a host application using CNA as a library call the umbrella
`SDL_Quit()` independently of CNA's own code, racing `~VibrateController()`
against an already-torn-down SDL?**) was investigated and answered: this
per-class `SDL_InitSubSystem()`/`SDL_QuitSubSystem()` pairing (never the
umbrella `SDL_Init()`/`SDL_Quit()`) is an established, project-wide
convention — `Microsoft::Xna::Framework::Graphics::GraphicsDevice` does the
*identical* thing for `SDL_INIT_VIDEO` (`GraphicsDevice.cpp` lines 148 and
330: `SDL_InitSubSystem`/`SDL_QuitSubSystem` in its own constructor/
`Dispose()`). The residual risk of a host app calling `SDL_Quit()` directly
is therefore shared identically by `GraphicsDevice` — a `Graphics` class
this phase's scope rule forbids modifying — and is not a
`VibrateController`-specific gap to fix unilaterally; doing so would make
`VibrateController` *inconsistent* with the rest of the codebase for no
proportionate benefit.

Also re-confirmed (no code change needed, already correct from prior
phases):
- `getIsSupportedProperty()`/`getDeviceNameProperty()` correctly close any
  temporarily-opened probe device — no leak.
- `g_subsystemHeld` persists correctly regardless of whether it was set via
  a probe or a real `Start()` — no leak, no incorrect early teardown.
- Repeated-probe, repeated-Start/Stop, and `StartLeftRight()` test coverage
  already exists and is thorough (`RepeatedProbeCallsStayConsistent`,
  `RepeatedStartStopSequencesDoNotDegrade`,
  `ConcurrentCallsFromMultipleThreadsDoNotCrashOrDeadlock`, plus the
  `StartLeftRight`/`Start` interleaving tests) — added in Phases 4/5, no
  gap found requiring new tests here.
- `docs/devices-hardware-checklist.md` Section 3 already documents Android
  vibration as unverified pending real hardware — re-read and confirmed
  still accurate; no change needed.

Files changed:
- `include/Microsoft/Devices/VibrateController.hpp` — `~VibrateController()`'s
  doc comment extended with the `GraphicsDevice` precedent and the explicit
  residual-risk framing above.
- `src/Microsoft/Devices/VibrateController.cpp` — `g_haptic`'s file-local
  comment extended identically.

Commands run:
- `cmake --build cmake-build-debug --target CNA -j$(nproc)` — clean build.
- `cmake --build cmake-build-debug --target CnaTests -j$(nproc)` — clean build.
- `ctest --output-on-failure -R "VibrateController"` — 29/29 passed
  (unchanged from before — this task was documentation-only, no behavior
  change).

Remaining risk: a host application calling the umbrella `SDL_Quit()`
directly remains a theoretical risk shared by `VibrateController` and
`GraphicsDevice` alike — explicitly out of scope for a
`Microsoft::Devices`-only pass per this phase's own scope rule. Worth
flagging as a candidate for a future cross-cutting "SDL lifecycle
ownership" pass spanning `Graphics` too, not a `Microsoft::Devices` task.

## P6-7: Android axis remap — semantic example tests

### Resolution

Confirmed the audit's finding #7: the existing 5 tests covered sign-flip
mechanics accurately but not the semantic examples the brief asked for
(tilt right/left, face up/down, top/bottom edge down). Added 4 new tests
to `tests/Microsoft/Devices/Sensors/AndroidSensorOrientationTests.cpp`:

- `LeftTiltIsAlwaysNegativeYRegardlessOfRotation` — mirrors the existing,
  already-validated `RightTiltIsAlwaysPositiveYRegardlessOfRotation` with
  negated raw signs (safe: doesn't require independently re-deriving
  anything, just the sign-inverse of an already-correct test).
- `FaceUpProducesPositiveZRegardlessOfRotation` /
  `FaceDownProducesNegativeZRegardlessOfRotation` — Z passes through both
  rotation formulas completely unchanged, so "face up"/"face down"
  (perpendicular-to-screen) is the one semantic that does *not* depend on
  which landscape rotation is active — safe to assert directly.
- `ForwardBackwardSignConventionIntentionallyFlipsBetweenRotations` — for
  the forward/backward (X) axis, deliberately does **not** assert an
  absolute "this sign means top-edge-down" claim.

**Why the X-axis test stops short of a "top/bottom edge down" label:**
while implementing this task, an attempt to *independently re-derive*
which raw X sign corresponds to "top edge down" from the rotation geometry
alone (tracing the 90°/270° CCW rotation cycle by hand) produced a
contradiction with the already-implemented and already-trusted Y-axis
convention on the first pass, and was only reconciled after very carefully
re-doing the rotation-cycle direction. This is precisely the kind of error
docs/devices-hardware-checklist.md already warns about for the gyroscope
axis ("there is no single authoritative WP7 sign convention documented...
so use internal consistency... as the primary correctness bar") and
implicitly for the accelerometer's X axis (the checklist's own Section 1
step 3 only asks to confirm the sign "matches what feels like 'forward'
tilt, **consistently between runs**" — it does not assert a required
absolute sign either). Given real, first-hand difficulty getting this
right by reasoning alone, asserting an unverified absolute physical claim
in a test would be dishonest — it would look authoritative while
potentially being backwards. The test instead confirms only what the code
*itself* already claims (the formula intentionally flips X's sign between
rotations), leaving the absolute direction to real-hardware verification,
matching this phase's explicit quality bar ("do not fake hardware
verification").

Files changed:
- `tests/Microsoft/Devices/Sensors/AndroidSensorOrientationTests.cpp`.

No changes needed to `docs/devices-hardware-checklist.md` — re-read
Section 1/2 and confirmed they already use this same honest,
internal-consistency framing for exactly this reason.

Commands run:
- `cmake --build cmake-build-debug --target CnaTests -j$(nproc)` — clean build.
- `./CnaTests --gtest_filter="AndroidSensorOrientationTests.*"` — 9/9
  passed (5 previous + 4 new).
- `ctest --output-on-failure -R "Accelerometer|SensorFailed|Compass|Gyroscope|Attitude|Motion|VibrateController|SensorSubsystemOwnership|AndroidSensorOrientation|SensorBase"`
  — 211/211 passed (207 previous + 4 new), 2 expected skips.

Remaining risk: none of these tests (old or new) prove physical hardware
correctness — only that the pure function implements its own documented,
internally-consistent convention. Real-device verification remains
required and is tracked in `docs/devices-hardware-checklist.md`.

## P6-8: Compass/Motion honest stubs + backend interface sketch

### Resolution

Re-read `Compass.hpp`/`.cpp` and `Motion.hpp`/`.cpp` fully (audit finding
#8): both remain honest, permanent `SensorState::NotSupported` stubs —
`getIsSupportedProperty()` hardcoded `return false;` in both, `Start()`
unconditionally throws `SensorFailedException` in both, no fake data
synthesis from `Accelerometer`/`Gyroscope` anywhere. Confirmed still true;
no code change made or needed here.

Existing test coverage already satisfies "confirm tests that they still
throw/not-supported": `CompassTests.GetIsSupportedPropertyDoesNotCrash`
(`EXPECT_FALSE(...)`), `CompassTests.StartThrowsSensorFailedException`,
`MotionTests.GetIsSupportedPropertyIsFalse`,
`MotionTests.StartThrowsSensorFailedException` — all re-run and confirmed
still passing (see P6-1's/P6-3's Resolutions above, which also exercised
Compass/Motion's construct/destroy paths heavily under concurrency without
ever observing a change in this behavior).

**Interface sketch** (documentation only, per the brief's own "optionally
sketch... only if they don't destabilize current code" — no `.hpp`/`.cpp`
files added to the build; adding an unused compiled interface with zero
callers would be exactly the kind of premature/half-finished abstraction
this project's own conventions warn against). This translates
`plan_devices_phase5.md`'s existing prose-level Android/iOS field mappings
(Tasks P5-8/P5-9) into a concrete C++ shape a future native-backend task
could implement against, without committing to it now:

```cpp
// Sketch only -- not compiled, not wired into Compass/Motion today.
namespace Microsoft::Devices::Sensors::Detail
{
    // One instance per platform (Android JNI bridge / iOS CLLocationManager
    // heading wrapper), constructed only once a real native backend is
    // scoped and implemented. Until then, Compass has no backend_ member
    // at all and getIsSupportedProperty() stays a hardcoded `return false;`.
    class ICompassBackend
    {
    public:
        virtual ~ICompassBackend() = default;
        [[nodiscard]] virtual bool IsSupported() = 0;
        virtual void Start() = 0;
        virtual void Stop() = 0;
        // Pull-model to match this project's existing SensorEventWatch()
        // dispatch shape (Detail::SdlSensorSubsystem<TSensor>) rather than
        // inventing a second, backend-specific push callback mechanism.
        [[nodiscard]] virtual CompassReading GetLatestReading() = 0;
    };

    // Same shape, mirroring MotionReading's Attitude/Gravity/
    // DeviceAcceleration/DeviceRotationRate fields (see
    // plan_devices_phase5.md's Motion-Android/Motion-iOS sections for the
    // field-for-field native source mapping).
    class IMotionBackend
    {
    public:
        virtual ~IMotionBackend() = default;
        [[nodiscard]] virtual bool IsSupported() = 0;
        virtual void Start() = 0;
        virtual void Stop() = 0;
        [[nodiscard]] virtual MotionReading GetLatestReading() = 0;
    };
}
```

Why this shape and not more: mirrors the existing
`Detail::SdlSensorSubsystem<TSensor>` pull-model (a reading is fetched, not
pushed via a second callback mechanism) so a future implementation could
plausibly reuse `SensorBase<T>`'s existing `setCurrentValueProperty()`
dispatch path from inside `Start()`'s own polling/callback glue, rather
than inventing a parallel event system. Deliberately not sketching
constructor/DI wiring, error handling, or thread-safety details — those
depend on decisions (polling vs. push, which thread delivers updates) that
belong to the actual implementation task, not this sketch.

Files changed: none (documentation only, added to this plan file).

Commands run: none (no code changed) — relied on P6-1's/P6-3's already-run
Compass/Motion test results in this same session.

Remaining risk: none — no behavior changed. The sketch above is
intentionally non-binding; an actual native-backend implementation task
may reasonably diverge from it once real platform constraints are known.
