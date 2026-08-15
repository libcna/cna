# Devices/Sensors thread-safety contract (Task SENSORBASE-004)

This is the single, explicit statement of what CNA promises about calling
`Microsoft::Devices::Sensors` (`Accelerometer`, `Gyroscope`, `Compass`,
`Motion`) and `Microsoft::Devices::VibrateController` instances from more than
one thread. Each class's own header cross-references this file instead of
repeating the policy inline.

## The real WP7 API's own baseline

The archived MSDN reference page for `Microsoft.Devices.Sensors.Compass`
(`https://learn.microsoft.com/en-us/previous-versions/windows/apps/hh220912(v=vs.105)`,
redirected from `msdn.microsoft.com/.../microsoft.devices.sensors.compass(v=vs.105)`)
carries the standard .NET Framework reference boilerplate "Thread Safety"
section, verbatim:

> Any public static (Shared in Visual Basic) members of this type are thread
> safe. Any instance members are not guaranteed to be thread safe.

`Accelerometer`, `Gyroscope`, and `Motion`'s equivalent archived pages carry
the same boilerplate. This is the floor CNA must not regress below: the real
API never promised instance-member thread safety at all. Everything below is
either matching that floor exactly, or a **CNA-specific stronger guarantee**
that goes beyond it (marked `CNAEXT` in spirit, even where not literally tagged
in code, since games written against the real API cannot rely on it).

## What CNA additionally guarantees, per class

These are guarantees, not accidents — each is backed by a specific mutex and
verified empirically under `devices-tsan`, not just reasoned about statically.

- **`SensorBase<T>` property getters/setters** (`getCurrentValueProperty()`,
  `getIsDataValidProperty()`, `getIsSupportedProperty()`,
  `getTimeBetweenUpdatesProperty()`/`setTimeBetweenUpdatesProperty()`,
  `getIsDisposedProperty()`) are safe to call concurrently with each other and
  with `Start()`/`Stop()`/`Dispose()` from any thread, on all four sensor
  classes. Guarded by `SensorBase<T>::mutex_` (Task P6-3/P8-2).
- **`Accelerometer`/`Gyroscope` `Start()`/`Stop()`/`Dispose()`** are safe to
  call concurrently with each other, on the same or different instances.
  Guarded by the shared `Detail::PlatformSensorSubsystem<TSensor>::mutex_` (Task
  P5-4) — one mutex per sensor *type*, not per instance. Native process-wide
  subsystem serialization is independently owned by `IPlatform`.
- **`Compass`/`Motion` `Start()`/`Stop()`/`getStateProperty()`/
  `SetBackendForTesting()`** are safe to call concurrently with each other, on
  the same instance. Guarded by each instance's own `mutex_` (Task
  SENSORBASE-004, this task). Before this task, these methods and
  `getStateProperty()` read/wrote `state_`/`started_` completely unguarded —
  confirmed as a real, reproducible data race (not a theoretical one) by a
  dedicated concurrency test
  (`CompassTests.ConcurrentStartStopFromMultipleThreadsDoesNotCrash`,
  `MotionTests.ConcurrentStartStopFromMultipleThreadsDoesNotCrash`) run under
  `devices-tsan`, which reported the race at `Compass.cpp`'s `Start()`/
  `Stop()` writes racing `getStateProperty()`'s read before the fix, and
  reported nothing at those locations afterward.
- **`Compass`/`Motion`'s internal `TimeBetweenUpdatesChanged` handler** (forwards a
  live interval change to `backend_`, Task ANDROID-BRIDGE-002) is also safe to run
  concurrently with `SetBackendForTesting()` on the same instance (Task
  SENSORBASE-009, 2026-07-16, external audit `audit_devices.md` finding
  `DEV-AUD-006`) — the handler previously read `backend_` without holding the same
  `mutex_` `SetBackendForTesting()` replaces it under; now guarded identically,
  confirmed by
  `CompassTests`/`MotionTests.ConcurrentSetTimeBetweenUpdatesAndSetBackendForTestingDoesNotCrash`
  under `devices-tsan`.
- **Concurrent `Dispose()` calls on the same instance**, across all four
  sensor classes and `VibrateController`, are safe: exactly one caller runs
  cleanup, the rest block in `WaitForDisposalToComplete()` until it finishes
  (Task P6-3/P7-2 — see `SensorBase::ClaimDisposalOnce()`'s doc comment for
  the full race this closes).
- **Instance construction/destruction across different instances** never
  corrupts the shared per-class instance counter (`instanceCount_`), guarded
  by a `static instanceCountMutex_` (Task P6-1).

## Known gap (accepted, not fixed by this task)

A `Dispose()` call and a concurrent `Start()` call on the *same* instance can
race in one specific narrow window: `Dispose(bool)` reads `started_` under its
own lock, releases the lock, and only then calls `Stop()` (unavoidable, since
`Stop()` re-acquires the same non-recursive mutex). If another thread's
`Start()` runs in that gap, the instance can end up disposed while its
backend is still logically started. This is pre-existing behavior shared
identically by `Accelerometer`/`Gyroscope`'s own `Dispose(bool)` (same
read-then-unlock-then-`Stop()` shape against `PlatformSensorSubsystem::mutex_`),
not something introduced or worsened by this task. Calling `Dispose()`
concurrently with `Start()` on the same instance is not a supported usage
pattern for any of the four sensor classes; `Dispose()` racing `Stop()` or
another `Dispose()` is fully safe (see above).

## What is still just the WP7 floor, not strengthened

- Subscribing/unsubscribing `CurrentValueChanged`/`Calibrate` **from a
  different thread** while a raise is in flight (as opposed to from within a
  handler of that same raise, which now has a documented, tested contract —
  see below) is not specifically guaranteed beyond "does not crash".

## Event dispatch semantics — see `docs/devices-event-contract.md`

Task `DEVPERF-004` (2026-07-18) formalized what was previously only "does not
crash": dispatch thread identity, ordering (`CurrentValueChanged` before
`ReadingChanged`, `Calibrate` independent of both), handler-list mutation
during dispatch, reentrancy (a handler triggering a new dispatch from within
itself), destruction-during-dispatch, and exception-swallowing policy for all
five events (`CurrentValueChanged`, `TimeBetweenUpdatesChanged`,
`ReadingChanged`, and both `Calibrate` events) are now each explicitly
documented and backed by a passing test, in `docs/devices-event-contract.md`
— read that file rather than this one for anything dispatch-related; this
file remains the source of truth for cross-thread *method*-call safety only.
- `VibrateController`'s own instance methods follow the same static-vs-instance
  split as the sensors; see `VibrateControllerTests.
  ConcurrentCallsFromMultipleThreadsDoNotCrashOrDeadlock` for its equivalent
  empirical check.

## Verification

`devices-tsan` (ThreadSanitizer) is the empirical tool used to confirm both
gaps and fixes in this area — a clean static read of the code is not treated
as sufficient by itself once a concurrency test is easy to write. As of this
task, running the full Devices `--gtest_filter` list (see `docs/devices-build.md`)
under `devices-tsan` reports no races inside `Microsoft::Devices` or
`Microsoft::Devices::Sensors` code. The only remaining reports are a single,
pre-existing, unrelated race in the sibling `sharp-runtime` repository
(`System::TimeSpan::copy_count`, a debug-only copy-constructor counter,
tracked separately — not part of this codebase and out of scope for this task).
