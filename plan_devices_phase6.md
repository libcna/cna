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
