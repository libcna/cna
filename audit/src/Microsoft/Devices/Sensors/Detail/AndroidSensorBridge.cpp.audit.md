# Audit: src/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.cpp

## Metadata
- Source file: `src/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.cpp` (1180 lines)
- Audit status: AUDITED (full read)
- Subsystem: `microsoft-devices` shard
- File type: C++ implementation (Android-only real logic behind `#ifdef __ANDROID__`; a trivial inert stub otherwise)
- XNA/FNA relevance: CNA-internal plumbing; FNA has no reference material for this namespace
- Main related tests: not independently located in this pass

## Purpose
Implements `AndroidSensorBridge::Impl`'s background-thread NDK poll loop (`Run()`), and `Start()`/`Stop()`/`SetSampleInterval()`'s thread-safe orchestration of it, including the reentrant-self-stop-vs-concurrent-external-stop race resolution.

## Executive Verdict
Correct — this is the deepest, most carefully-reasoned concurrency code in this entire shard, and I traced every synchronization claim by hand rather than trusting the comments. Specifically verified: (1) `stateMutex_` is genuinely held for every mutation of `runState_`/`reclaimClaimed_`/`workerThreadId_`/`callback_`/`timeBetweenUpdates_`, and genuinely released before both `Start()`'s bounded condition-variable wait and `Stop()`'s blocking `join()`/non-blocking `detach()`, so neither can deadlock against the other; (2) `workerThreadId_` is captured once, under the lock, immediately after spawning (`impl_->worker_.get_id()` at line 894) — not re-derived later from `worker_.get_id()` itself, which would collapse to the default "no thread" id the instant anyone calls `join()`/`detach()` on it, exactly the bug class the doc comments describe fixing; (3) `reclaimClaimed_`'s single-claimant pattern (lines 984-988, `if (!impl_->reclaimClaimed_) { impl_->reclaimClaimed_ = true; shouldReclaim = true; }`) correctly ensures exactly one caller — across any combination of concurrent self/external `Stop()` calls — ever touches the `std::thread` object; (4) the reentrant-self-stop path correctly `detach()`es (never `join()`s its own thread, which would throw `resource_deadlock_would_occur`) while the external-caller path correctly bounds its `join()` wait via `runExitedCv_`/`kNativeCallTimeout` and falls back to `detach()` + permanent `abandoned_` marking if a native call is genuinely wedged.

## Checklist Results
- `Run()`'s `RunExitGuard` (lines 51-89) is explicitly `noexcept` with an internal `try/catch(...)` around the exit callback — correctly reasoned: a destructor with a user-provided body is implicitly `noexcept(true)` unless stated otherwise, so an unswallowed exception from the exit callback would call `std::terminate()` immediately (Task ANDR2-004, a real, confirmed-by-reasoning fix, not merely defensive).
- `Run()`'s inner event-drain loop caps itself at `MaxEventsPerDrainBatch = 64` per outer-loop iteration (lines 543-569) specifically so a pending `SetSampleInterval()` request (checked once per *outer* iteration) can't be starved indefinitely by a continuous high-rate event flood — correctly reasoned as a batching-delay fix, not a dropped-sample fix (every event is still delivered to `callback_` exactly once; only the *draining* is bounded). `GetDrainBatchLimitHitCountForTesting()` correctly exposes how often this actually triggers.
- `ASensorEventQueue_getEvents()`'s negative return (genuine read error) is correctly distinguished from `0` (no events available) via `consecutiveGetEventsFailures`/`MaxConsecutiveGetEventsFailures = 5` (lines 573-602) — a transient one-off failure does not tear delivery down, but a run of 5 consecutive failures signals `stopRequested_` through the same clean shutdown path, correctly avoiding an infinite silent-retry loop on a persistently failing sensor.
- Every native NDK call whose failure matters (`ALooper_prepare`, `ASensorManager_createEventQueue`, `ASensorEventQueue_enableSensor`, `ASensorEventQueue_setEventRate`, `ASensorEventQueue_disableSensor`, `ASensorManager_destroyEventQueue`) is checked, not blindly called — each with a specific, correct fatal/non-fatal classification (queue creation/enable failure is fatal to `Start()`; a rejected event-rate is non-fatal, since the sensor keeps delivering at whatever rate was already in effect).
- The event-loop callback (`callback_(sample)`) is wrapped in `try { } catch (const std::exception&) { } catch (...) { }` (lines 667-693) — correctly prevents an exception escaping a `std::thread`'s entry point from calling `std::terminate()`, with the swallowed exception now routed through `NativeDiagnosticSink` (Task DEVPERF-005) rather than silently disappearing as it did before.
- `stopRequested_.load()` is re-checked before every single callback invocation inside the inner drain loop (line 560), not just once per outer iteration — correctly reasoned: a callback can reentrantly call `Stop()` (e.g. an owning `Compass`/`Motion` disposing itself from inside its own `CurrentValueChanged` handler), after which a second already-queued event must not trigger another `callback_()` call against a possibly-now-invalid captured pointer.

## Detailed Findings
None.

## Cross-File Observations
`Impl::Probe()` (lines 311-326) is documented and confirmed to require the caller already hold `stateMutex_` — I verified both call sites (`IsAvailable()` at line 778, `Start()`'s locked section at line 832) actually hold it, closing a previously-real data race (Task ANDR2-002) where `IsAvailable()` used to call `Probe()` completely unguarded while `Start()` called it under the lock.

## Missing or Weak Tests
Not independently located in this pass; the extensive test-only accessor surface (`GetDrainBatchLimitHitCountForTesting`, `GetLastSetEventRateSucceededForTesting`, `GetMinDelayMicrosecondsForTesting`) strongly suggests these specific edge cases are covered by a real test suite, though the Android-only NDK call sites themselves have an inherent host-testing gap (no Android device/emulator in this environment, per this shard's own repeated disclosure).

## Positive Findings
This file is the single best-hardened piece of low-level concurrent systems code encountered in this entire audit session. The `workerThreadId_`-vs-`get_id()` distinction, the `reclaimClaimed_` single-claimant pattern, and the bounded-wait-then-`abandoned_` fallback for a wedged native call are all genuinely subtle, correctly-reasoned solutions to real concurrency hazards — each with a specific causal chain for the bug it fixes, not a generic "add more locking" response.

## Final Assessment
No findings. This file received the most thorough hand-verification in this batch given its concurrency complexity, and no gap was found.
