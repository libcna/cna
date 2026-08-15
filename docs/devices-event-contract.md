# Devices/Sensors event callback contract (Task DEVPERF-004)

This is the single, explicit statement of what CNA promises about the five
`System::EventHandler<T>` events declared across `Microsoft::Devices::Sensors`:
`SensorBase<T>::CurrentValueChanged` (all four sensor classes),
`SensorBase<T>::TimeBetweenUpdatesChanged` (`CNAEXT`, all four),
`Accelerometer::ReadingChanged` (`Accelerometer`-only, real XNA API),
`Compass::Calibrate`, and `Motion::Calibrate`. Each class's own header
cross-references this file instead of repeating the policy inline. See
`docs/devices-thread-safety.md` for the separate (but related) contract on
concurrent *method* calls across threads — this file is about event
*dispatch* semantics specifically: which thread raises an event, in what
order, whether handlers may safely mutate the handler list or trigger a new
dispatch from within themselves, what happens if a handler destroys the
sender, and what happens if a handler throws.

## What is WP7-documented vs. a CNA-only guarantee

The archived MSDN reference pages for these events do not document dispatch
thread identity, reentrancy, or exception behavior at all — WP7's own
`EventHandler<T>`/multicast-delegate semantics are inherited from the
.NET Framework's own well-known behavior (synchronous, in-subscription-order
invocation over a snapshot of subscribers taken at raise time; unhandled
exceptions propagate to the raiser), but no WP7-specific page for any of
these five events adds anything beyond that baseline. Everything in this
document is therefore either **matching that inherited .NET baseline exactly**
(marked "WP7 baseline" below) or a **CNA-specific decision** where CNA had to
choose a policy the real API leaves unspecified for a native, multi-backend
runtime with no direct WP7 equivalent (marked "CNA policy" below, `CNAEXT` in
spirit even where not literally tagged in code).

## 1. Dispatch thread identity — CNA policy, unspecified by WP7

WP7 sensors deliver events on whatever thread the underlying OS sensor
subsystem chooses to call back on (WP7 itself does not marshal sensor events
to the UI thread) — CNA's own per-backend behavior is analogous, not
identical, since the underlying OS mechanisms differ:

- **`Accelerometer`/`Gyroscope`** (`Detail::PlatformSensorSubsystem<TSensor>`):
  `CurrentValueChanged`/`ReadingChanged` are raised synchronously from the
  selected `IPlatformSensorSession` callback. That callback may run on any
  platform-owned thread; CNA does not marshal it to the thread that called
  `Start()`.
- **`Compass`/`Motion`** (`Detail::AndroidCompassBackend`/
  `Detail::AndroidMotionBackend`, backed by `Detail::AndroidSensorBridge`):
  `CurrentValueChanged`/`Calibrate` are raised synchronously from
  `AndroidSensorBridge::Run()`'s dedicated per-instance worker thread (an
  `ALooper`-based poll loop, started by `Start()`, joined by `Stop()`), never
  from the thread that called `Start()`/`Stop()` itself.
- **Synthetic test paths** (`InjectSyntheticSensorUpdate()`,
  `FakeCompassBackend`/`FakeMotionBackend`'s captured callbacks): raise
  synchronously on the calling (test) thread — this is a deliberate
  simplification for determinism, not a claim that it matches either real
  backend's thread identity.

**CNA guarantee:** a caller must not assume any event fires on the thread
that called `Start()`, and must not assume it fires on the same thread across
different sensor classes or backends. This is intentionally left
unconstrained (matching the real API's own silence) rather than papered over
with a fabricated "always fires on thread X" claim.

## 2. Ordering — mixed WP7 baseline / CNA policy

- **`CurrentValueChanged` before `ReadingChanged`** (`Accelerometer` only,
  the one class with both): CNA policy, decided and documented at the
  `SetCurrentValueAndMarkDataValid()` call site in `Accelerometer.cpp` (Task
  `ACCEL-002`, reaffirmed unchanged by Task `LIFE-004`). WP7's own archived
  pages document both events existing on `Accelerometer` but not their
  relative order; CNA fixes this order and tests it
  (`AccelerometerTests.CurrentValueChangedFiresBeforeReadingChanged`).
- **`CurrentValueChanged` vs. `Calibrate`** (`Compass`/`Motion`): no ordering
  relationship exists or is claimed — these are two structurally independent
  callbacks handed to the backend (`ICompassBackend::Start()`/
  `IMotionBackend::Start()` each take a separate `ReadingCallback` and
  `CalibrationCallback`), invoked whenever the backend itself decides either
  condition has occurred. Neither WP7-documented nor a CNA guarantee in
  either direction; do not write code that assumes one always precedes the
  other.
- **Handler invocation order within a single event's dispatch**: WP7
  baseline — subscribers are invoked in subscription order, over a snapshot
  taken at `Raise()` time (`System::EventHandler<T>::Raise()`, sharp-runtime:
  `auto snapshot = handlers_; for (auto& entry : snapshot) { entry.second(sender, e); }`).
  Matches standard .NET multicast-delegate semantics exactly.

## 3. Handler-list mutation during dispatch — WP7 baseline, now proven by test

`Add()`/`Remove()` called from inside a handler only affects the *next*
`Raise()` call, never the one currently in progress — because `Raise()`
iterates a local copy (`snapshot`), not the live `handlers_` vector, this is
true by construction for every `System::EventHandler<T>` instance, not
something each event has to separately implement. This matches standard .NET
multicast-delegate semantics (WP7 baseline), and is now proven, not just
reasoned about, for both events that can meaningfully exercise it:

- `CurrentValueChanged`: `AccelerometerTests`/`GyroscopeTests`/`CompassTests`/
  `MotionTests.RemovingAnotherNotYetInvokedHandlerDuringDispatchStillInvokesIt`
  (Task `DEVPERF-004`, one test per sensor class — previously only
  `Accelerometer` had this test, and even that one's own assertion was
  weakened to a non-asserting `(void)` cast before this task; see this file's
  own history for the "stale test" finding).
- `Calibrate`: `CompassTests`/`MotionTests.
  RemovingAnotherNotYetInvokedCalibrateHandlerDuringDispatchStillInvokesIt`
  (Task `DEVPERF-004` — no prior test exercised this for `Calibrate`
  specifically; the guarantee is generic to `EventHandler<T>`, but each
  distinct `Raise()` call site is proven independently rather than assumed
  correct by analogy alone).
- `ReadingChanged`/`TimeBetweenUpdatesChanged`: not independently tested —
  both go through the identical, already-proven `EventHandler<T>::Raise()`
  mechanism with no event-specific dispatch logic of their own; adding
  near-duplicate tests here was judged low-value repetition, not a gap.

## 4. Reentrancy (a handler triggering a new dispatch from within itself) — WP7 baseline, now proven by test

A handler that calls back into the sensor's own public API and triggers a
*new* `Raise()` of the same event (e.g. injecting another synthetic reading,
or a real backend delivering a second sample synchronously) does not deadlock
or corrupt state: each `Raise()` call's `snapshot` is a local stack variable,
not shared/member state, so nested calls each get their own independent
iteration with nothing to corrupt. This is a natural consequence of
`EventHandler<T>`'s design (WP7 baseline, standard .NET behavior — a
handler-triggered reentrant raise is not a violation of anything), previously
untested anywhere in this codebase for `CurrentValueChanged` before this
task:

- `AccelerometerTests`/`GyroscopeTests`/`CompassTests`/`MotionTests.
  HandlerTriggeringAReentrantUpdateDoesNotDeadlockOrCorruptState` (Task
  `DEVPERF-004`, one test per sensor class): a one-shot-guarded handler
  triggers a second, inner update from within the outer dispatch; all four
  confirm the outer and inner dispatch are both observed, in the correct
  order (inner completes fully before the outer handler returns, since the
  reentrant call is synchronous), and the final `CurrentValue` reflects the
  inner (most recent) update.

## 5. Destruction during dispatch — CNA policy, no WP7 equivalent

WP7's reference-counted, garbage-collected runtime has no equivalent failure
mode to a handler `delete`-ing or `Dispose()`-ing its own sender mid-dispatch
in unmanaged C++ — this is entirely a CNA-specific concern with its own
CNA-specific guarantee:

- **`Accelerometer`/`Gyroscope`**: a handler may safely destroy or `Dispose()`
  the sending instance from within its own `CurrentValueChanged`/
  `ReadingChanged` callback. `Detail::PlatformSensorSubsystem<TSensor>::
  DispatchToInstances()` snapshots/revalidates registrations before each
  call, closing the ABA/use-after-free window (Task `P8-1`–`P8-5` family).
  Proven by `DestroyingOwnerFromCurrentValueChangedStillFiresReadingChangedSafely`,
  `SelfDestroyingFromOwnReadingChangedCallbackDuringInjectSyntheticSensorUpdateDoesNotUseAfterFree`,
  `DisposeFromWithinOwnCallbackDoesNotDeadlock`,
  `DisposingDifferentInstanceDuringSameBatchDispatchDoesNotUseAfterFree`.
- **`Compass`/`Motion`**: a handler may safely `Dispose()` the sending
  instance from within its own callback — proven by each class's own
  `DisposeFromWithinOwnCallbackDoesNotDeadlock` via the fake-backend seam.
  This relies on `Detail::SensorOwnerControlBlock`'s generation-guard pattern
  (Task `LIFE-001`/`LIFE-002`/`LIFE-003`): the backend's captured callback
  lambdas hold a `shared_ptr` to a small control block, never `this`
  directly, and re-validate `(generation, owner)` under the control block's
  own mutex — released again *before* calling into user code — so a stale
  callback from an already-disposed or already-restarted instance safely
  no-ops instead of touching a dangling `Compass*`/`Motion*`. **Left
  explicitly unverified beyond the fake-backend seam**: whether the real
  `Detail::AndroidCompassBackend`/`AndroidMotionBackend`/`AndroidSensorBridge`
  chain behaves identically if torn down while still executing further up
  its own call stack is Android-only and requires real hardware or an
  Android ASan build this container cannot run (already flagged this way at
  `SENSORBASE-003`'s own closing note; not re-claimed as resolved here).
- **`VibrateController`**: has no `CurrentValueChanged`-style event and is
  out of scope for this document.

## 6. Exception semantics — CNA policy, decided but not yet uniformly implemented

An unhandled exception escaping a user-supplied handler must never propagate
across a C callback boundary or out of a `std::thread` entry point — doing so
is at best implementation-defined and at worst calls `std::terminate()`,
crashing the whole process, which is strictly worse than any alternative CNA
could choose (WP7's own runtime, by contrast, safely propagates such
exceptions up a fully managed call stack — there is no equivalent hazard on
that platform, so this is a **CNA-only policy decision**, not a WP7-mismatch
CNA is choosing to tolerate).

**Decided normative policy, to apply uniformly across every backend:**
log-and-continue — catch the exception at the C-callback/thread-entry
boundary, do not let it propagate further, and make the fact that it happened
observable (a debug log line plus a test-visible counter/last-message hook),
rather than silently discarding it with a bare `catch (...) {}`.

**Current implementation state relative to that policy — a real,
concrete gap, found while writing this document, not assumed:**

- **`Accelerometer`/`Gyroscope`** (`Detail::PlatformSensorSubsystem<TSensor>::
  DispatchToInstances()`): **fully matches the decided policy.** Task
  the prior callback-hardening task split the swallow into a typed
  `std::exception&` clause (extracts `.what()`) plus a fallback, both routed
  through `LogAndRecordDispatchException()` — debug-only `SDL_Log()`
  diagnostics plus `dispatchExceptionCountForTesting_`/
  `lastDispatchExceptionMessageForTesting_` test hooks. PLAT-108 moved the catch and diagnostic
  policy unchanged to the platform-neutral manager. Proven by
  `ThrowingCallbackDuringSyntheticUpdateStillCleansUpAndDoesNotHangDispose`,
  `ThrowingNonStdExceptionDuringDispatchToInstancesForTestingIsObservable`,
  `ThrowingHandlerInBatchDispatchDoesNotPreventNextInstanceFromReceivingItsEvent`
  (all four sensor-independent tests re-verified still passing, `Accelerometer`/
  `Gyroscope` both covered).
- **`Compass`/`Motion`** (`Detail::AndroidSensorBridge::Run()`, the shared
  worker-thread loop both backends sit on top of): **does not yet match the
  decided policy.** The `callback_(sample)` call site (around
  `AndroidSensorBridge.cpp`'s `Run()` — this is the actual point where a
  throwing `CurrentValueChanged`/`Calibrate` handler's exception would
  otherwise reach a `std::thread` entry point, confirmed by tracing the full
  call chain: `AndroidCompassBackend::PublishReading()`/
  `AndroidMotionBackend`'s equivalent call `owner->SetCurrentValueAndMarkDataValid()`/
  `owner->Calibrate.Raise()` directly, with no intervening `try`/`catch` of
  their own) is wrapped in a bare `catch (...) { }` with **no logging and no
  test-visible counter at all** — the exception is caught (so no crash — the
  `std::terminate()` hazard this policy exists to prevent is already
  avoided), but silently, unlike the platform sensor path. The existing comment at that
  call site ("Mirrors `Detail::PlatformSensorSubsystem<TSensor>::
  DispatchToInstances()`'s identical policy") was accurate when written but
  is now **stale**: it no longer is identical, since the callback-hardening task upgraded
  the platform sensor side afterward. **Deliberately not fixed by this task**: adding the
  matching `__android_log_print()`-based logging plus a testable counter to
  `AndroidSensorBridge.cpp` is exactly `DEVPERF-005`'s scope ("structured
  native error/diagnostic channel... cover SDL and Android failure paths"),
  not a documentation task — recorded here as a concrete, named, verified gap
  for that task to close, not silently left as a stale/misleading comment.
- **`ReadingChanged`/`TimeBetweenUpdatesChanged`**: `ReadingChanged` is
  raised from the same `DispatchToInstances()` path as `CurrentValueChanged`
  (`Accelerometer`-only) — already covered by the SDL-path tests above.
  `TimeBetweenUpdatesChanged`'s own internal handler (forwards to `backend_`,
  Task `ANDROID-BRIDGE-002`) is CNA-internal wiring, not a user-subscribable
  dispatch path with the same untrusted-handler-exception concern — out of
  scope for this exception policy.

## Verification

`devices-ubsan`: clean on every test added/changed by this task (`0`
failures across the precise Devices/Sensors filter, see `docs/devices-build.md`
for the exact filter string). `devices-tsan`: re-run 3-4 consecutive times
after this task's new/changed tests (all touch dispatch/reentrancy on a real
event-raise path) — `0` `WARNING: ThreadSanitizer` occurrences.
