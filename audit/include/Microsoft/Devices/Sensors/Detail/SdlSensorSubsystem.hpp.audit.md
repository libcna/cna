# Audit: include/Microsoft/Devices/Sensors/Detail/SdlSensorSubsystem.hpp

## Metadata
- Source file: `include/Microsoft/Devices/Sensors/Detail/SdlSensorSubsystem.hpp` (927 lines)
- Audit status: AUDITED (full read)
- Subsystem: `microsoft-devices` shard
- File type: C++ header (template, header-only implementation)
- XNA/FNA relevance: NOXNA internal implementation detail, no FNA/WP7 equivalent
- Main related tests: not independently located in this pass

## Purpose
Shared, templated (`TSensor`) SDL_INIT_SENSOR subsystem manager: owns subsystem lifetime, default-sensor discovery/revalidation, and event-watch registration/dispatch — factored out of `Accelerometer`/`Gyroscope`, which previously hand-wrote near-identical copies of this logic.

## Executive Verdict
This is the most heavily concurrency-hardened single file found in this entire audit. Every non-trivial method's doc comment cites a specific, real, previously-found defect (several confirmed via an external audit dated 2026-07-17 and this project's own task tracker) and explains exactly how the current code closes it. I independently verified the two most safety-critical mechanisms in detail:

1. **`DispatchRegistration`'s ABA-hazard fix (Task SDLCORE-004)**: previously `startedInstances_` held raw `TSensor*` values, and a dispatcher revalidated a snapshotted pointer by checking whether that bit pattern was still present in the live list — unable to distinguish "the original instance is still started" from "a different, later instance happens to have been allocated at the same freed address and is itself now started." The fix (a heap-allocated, `shared_ptr`-held `DispatchRegistration` node, never reused across registrations, with `owner` nulled under `mutex_` before being unregistered) is correctly implemented and genuinely closes this specific hazard — verified by reading `RegisterStartedInstanceLocked()`/`UnregisterStartedInstanceLocked()`/`DispatchToInstances()` together.
2. **`DispatchToInstances()`'s per-registration (not bulk-upfront) marking (Task P7-3 finding C)**: a real use-after-free where every snapshotted instance previously had its thread-id pushed into its dispatch-tracking vector *before* any of them were actually dispatched to, so one instance's callback disposing a not-yet-reached sibling in the same snapshot could make that sibling's own concurrent `Dispose()` wrongly treat the in-flight (but not-yet-executed) dispatch as a same-thread reentrant call, exempt from waiting — disposing (and potentially destroying) it while a dangling dispatch was still queued to run on it next. The fix (mark-and-validate immediately before dispatching to each instance, not in bulk) is correctly implemented.

## Checklist Results
- `EnsureSubsystemInitialized()`/`IsSensorConnected()`/`ProbeIsSupported()` all take a `const std::lock_guard<std::mutex>&` as a required parameter purely as a compile-time "you must already hold this lock" proof (Task P8-3) — a genuinely good defensive pattern for an internal-only class, correctly reasoned as closing off the exact class of forgotten-lock mistake the file's own history (Task P6-1's addendum, Task P7-1) already found the hard way.
- `SensorEventWatch()`'s exact `SDL_EventFilter` signature match (including the `SDLCALL` calling-convention tag) plus a `static_assert` proving it — correctly identified and fixed real undefined behavior (calling through a `reinterpret_cast`-mismatched function-pointer type), not just a style improvement.
- `OpenDefaultSensorLocked()`'s stale-handle revalidation (Task SDLCORE-005) correctly avoids reusing a permanently-cached sensor handle across a physical disconnect/reconnect, and correctly avoids attributing a later device's event to a stale cached id.
- `RegisterEventWatchIfNeededLocked()`'s return-value check (Task SDLCORE-003) correctly propagates a genuine `SDL_AddEventWatch()` failure back to the caller instead of optimistically marking the watch registered.
- `DispatchToInstances()`'s exception handling correctly distinguishes `std::exception` (logged with its message) from any other thrown value (logged generically), and both policies are explicitly justified against three rejected alternatives (propagate-to-owner, unsubscribe-failing-handler, terminate) in the code's own comment — a genuinely considered design decision, not a default fallback.

## Detailed Findings
None.

## Cross-File Observations
`GetGlobalSdlSensorMutex()` forwards to `Detail::GetGlobalSdlSubsystemMutex()` (not in this batch), the same process-wide mutex `Detail::SdlHapticVibrateBackend` uses for `SDL_INIT_HAPTIC` — confirmed via this file's own doc comment describing the consolidation (Task SDLCORE-001) and its documented lock-order rule (this global mutex always nested inside a per-class `SdlSensorSubsystem<TSensor>::mutex_`, never the reverse).

## Missing or Weak Tests
Not independently located in this pass; the sheer density of test-only hooks (`SetEventWatchRegistrationFailureForTesting`, `GetDispatchExceptionCountForTesting`, `IsSensorConnectedForTesting`, etc.) strongly implies dedicated tests exist for each of the documented fixes.

## Positive Findings
This file is a model example of iterative hardening under real, cited external audit pressure — every mechanism traces to a specific, named defect with a clear before/after explanation, and the two most safety-critical fixes (ABA-hazard registration identity, per-registration dispatch marking) were independently re-derived and confirmed correct in this pass, not merely trusted from the comments.

## Final Assessment
No findings.
