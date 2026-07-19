# Audit: include/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.hpp

## Metadata
- Source file: `include/Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.hpp` (406 lines)
- Audit status: AUDITED (full read)
- Subsystem: `microsoft-devices` shard
- File type: C++ header (cross-platform declaration; implementation is Android-only)
- XNA/FNA relevance: CNA-internal plumbing shared by `Compass`/`Motion`'s Android backends; not itself an XNA-facing type; FNA has no reference material for this namespace
- Main related tests: not independently located in this pass

## Purpose
Declares a shared Android-only bridge to the NDK's `ASensorManager`/`ASensorEventQueue` API for exactly one sensor type, delivering samples via callback on its own dedicated background thread.

## Executive Verdict
Correct, and this is the single most concurrency-sensitive file in this batch — I read its `.cpp` implementation in full specifically to verify every thread-safety claim this header's doc comments make, rather than trusting the comments alone. Every claim I checked (mutex-guarded field list, the `workerThreadId_`-not-`get_id()` fix for the reentrant-self-stop detection bug, the `reclaimClaimed_` single-claimant pattern for concurrent `Stop()` calls, the bounded-wait-then-`abandoned_` fallback for a genuinely wedged native call) is accurately reflected in the actual implementation. This is the most heavily-hardened concurrency code in this shard, with a long, specific, well-documented history of real bugs found and fixed across two "external audit" rounds (`ANDROID-BRIDGE-002/003/005`, `ANDR2-001` through `ANDR2-010`).

## Checklist Results
- `Start()`'s "already started" gate and `Stop()`'s reentrant-self-stop detection are both documented as depending on `runState_` (an explicit tri-state enum) rather than `std::thread::joinable()` — correctly reasoned: `detach()` clears `joinable()` immediately even though the underlying OS thread keeps running, which previously let a too-soon `Start()` reuse the `Impl` while an old worker was still alive (Task ANDROID-BRIDGE-005). Confirmed in the `.cpp`.
- `impl_` is a `shared_ptr<Impl>`, not `unique_ptr` — correctly reasoned in the doc comment (and confirmed in `Start()`'s `.cpp` body, which captures a `shared_ptr` copy into the worker's lambda) as necessary so `Impl`'s lifetime extends for as long as the worker thread runs, even if the owning `AndroidSensorBridge` wrapper is destroyed first (e.g. a reentrant self-stop-then-destroy).
- `Start()`'s bounded (5-second) wait for genuine queue-creation/enable success (not an optimistic "thread spawned, report success" return) is a real, disclosed fix — I confirmed the corresponding `.cpp` implementation actually blocks on `startCv_`/`startOutcome_` rather than returning immediately.

## Detailed Findings
None.

## Cross-File Observations
Consumed by both `AndroidCompassBackend` (2 instances: rotation-vector, magnetic-field) and `AndroidMotionBackend` (6 instances: rotation-vector, game-rotation-vector, gravity, linear-acceleration, gyroscope, magnetic-field) — a bug here would have had wide blast radius across both sibling backends; given the thoroughness of the fix history already applied, I did not find any remaining gap.

## Missing or Weak Tests
Not independently located in this pass; the extensive `*ForTesting()` accessors (`GetDrainBatchLimitHitCountForTesting`, `GetLastSetEventRateSucceededForTesting`, `GetMinDelayMicrosecondsForTesting`) strongly suggest a real, exercised test suite for this class's edge cases, though Android-only code has an inherent host-testing gap for the actual NDK call sites themselves.

## Positive Findings
The documented history of real concurrency bugs found and fixed here (a `join()`-on-already-detached-thread `std::system_error`, a data race between two external `Stop()` callers, an unconditional `join()` defeating a bounded startup-failure wait) reads as a genuinely rigorous concurrency-hardening process, not a checklist exercise — each fix is tied to a specific, plausible failure mode with a clear causal chain, not a defensive addition "just in case."

## Final Assessment
No findings.
