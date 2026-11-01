# Devices/Sensors event callback contract

This page describes current CNA dispatch semantics for `Microsoft::Devices::Sensors`.
The public events are `SensorBase<T>::CurrentValueChanged`,
`Accelerometer::ReadingChanged`, `Compass::Calibrate` and `Motion::Calibrate`.
`SensorBase<T>::TimeBetweenUpdatesChanged` is a protected `CNAEXT` hook for derived
implementations, not a game-subscribable event.

These are implementation contracts, not measured Windows Phone dispatch/lifetime parity.
The original DEVPERF-004 investigation and subsequent DEVPERF-005 diagnostics fix are in Git
history. For concurrent method calls and subscription synchronization, see
[devices-thread-safety.md](devices-thread-safety.md).

## Dispatch thread

| Path | Where handlers run |
|---|---|
| Accelerometer/Gyroscope platform sessions | Synchronously inside the selected `IPlatformSensorSession` callback; CNA does not marshal to the Start caller. |
| Compass/Motion Android implementations | Synchronously on the `AndroidSensorBridge` worker delivering the relevant source sample. Different source streams need not share one thread. |
| Synthetic injection or fake backend | Synchronously on the caller's thread. |
| TimeBetweenUpdatesChanged | Synchronously on the thread changing the interval, after releasing the base value mutex. |

Do not assume a UI/game thread or one common thread for different sensors. Marshal work to an
application-owned queue when it must run on a particular thread. A synthetic test's thread
identity does not qualify a native provider.

## Ordering, mutation and reentrancy

Sharp Runtime's `System::EventHandler<T>::Raise` copies the subscriber list at the start of each
raise, then calls it in subscription order. Add/Remove/Clear from a handler affect a later raise,
including a nested raise begun after that change; they do not alter the outer raise's snapshot.
This supports same-thread mutation, not unsynchronized concurrent subscription.

Each nested raise has its own snapshot and completes before its caller resumes. Sensor value
publication releases its value mutex before invoking handlers, allowing a handler to read the
value or reenter supported sensor methods. A reentrant newer reading remains CurrentValue after
the outer dispatch returns.

For Accelerometer, CurrentValueChanged precedes the separately snapshotted legacy ReadingChanged
event in an accepted dispatch. This is CNA's fixed order. A throwing CurrentValueChanged handler
interrupts that dispatch before ReadingChanged; exception containment does not resume it.

Compass/Motion reading and calibration callbacks have no general cross-stream ordering guarantee.
Compass's magnetic-sample handler currently tries to publish a reading before invoking calibration,
but freshness checks may suppress that reading and other source threads can interleave.

## Disposal and destruction inside handlers

Accelerometer/Gyroscope registrations, dispatch tokens and scope-exit cleanup support disposal
or destruction from their own callbacks and disposal of another registered instance during a batch.
Accelerometer copies its ReadingChanged collection and arguments before raising CurrentValueChanged,
so legacy delivery can finish after a handler destroys the owner.

An event snapshot keeps callbacks/arguments alive, not the sender object. After destruction,
remaining handlers must treat sender as an opaque value and must not dereference it.

Compass/Motion callbacks use a shared owner/generation control block to reject stale callbacks
after Stop, restart or disposal. Dispose from a callback is covered through fake backends.
That evidence is not a blanket guarantee for deleting an owner while its real Android backend
is still executing, or for arbitrary concurrent destruction; native teardown qualification remains
separate. Respect [devices-thread-safety.md](devices-thread-safety.md)'s lifetime requirements.

## Exceptions and diagnostics

`EventHandler<T>::Raise` itself does not catch handler exceptions: the first exception interrupts
the remaining handlers in that snapshot. Containment belongs to the calling boundary:

| Boundary | Current behavior |
|---|---|
| Accelerometer/Gyroscope `PlatformSensorSubsystem::DispatchToInstances` | Catches standard and other exceptions per instance, records diagnostics, cleans up the dispatch token and proceeds to the next registered instance. |
| Android `AndroidSensorBridge::Run` sample callback | Catches standard and other exceptions and records diagnostics; the worker can continue processing later samples. |
| Direct `InjectSyntheticSensorUpdate` | Cleans up its token, then lets the exception propagate to the ordinary C++ caller. |
| Direct fake Compass/Motion callback | No worker boundary surrounds it; a handler exception can propagate to the test caller. |
| Protected TimeBetweenUpdatesChanged hook | Ordinary synchronous C++ raise; an exception can leave the interval already changed. |

Both native dispatch paths now use `Detail::NativeDiagnosticSink`. DEVPERF-005 closed the old
Android silent-catch gap. Records identify the operation, native message/code when applicable,
device, wall-clock timestamp and severity. The sink offers debug logging and a process-wide
count/last-record/observer seam for tests. Its Record method is noexcept and suppresses its own
logging/observer failures; diagnostics are best effort under allocation/logging failure.

The platform manager also retains its per-class exception count/last-message test accessors.
This is not a promise that every handler continues after one throws, or that ordinary public
method exceptions are swallowed.

## Verification

Relevant tests live in `modules/devices/tests/Microsoft/Devices/Sensors/`:

- Accelerometer/Gyroscope/Compass/Motion removal-during-dispatch and guarded reentrant updates.
- Accelerometer legacy ordering, self-destruction, direct-injection exception cleanup and
  manager batch continuation/diagnostics.
- Compass/Motion fake-backend disposal and stale-generation rejection.
- NativeDiagnostic sink recording and observer-failure containment.

Host tests establish the C++/fake-provider contracts. A Linux UBSan or TSan pass cannot execute
the Android-only sensor-service/worker code or qualify physical timing and axes.
Use [devices-build.md](devices-build.md) for focused builds and
[devices-hardware-checklist.md](devices-hardware-checklist.md) for native qualification.

Audit verification (2026-11-01): the focused CnaDevicesTests UBSan build passes 484 tests, with
four supported-hardware skips, through the private display runner. No new TSan or Android
hardware run is claimed by this audit.
