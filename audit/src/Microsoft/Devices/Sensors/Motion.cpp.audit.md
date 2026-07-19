# Audit: src/Microsoft/Devices/Sensors/Motion.cpp

## Metadata
- Source file: `src/Microsoft/Devices/Sensors/Motion.cpp` (384 lines)
- Audit status: AUDITED (full read)
- Subsystem: `microsoft-devices` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA has no reference material for this namespace (WP7-only API, never implemented in desktop FNA)
- Main related tests: not independently located in this pass

## Purpose
Implements `Motion`'s full lifecycle and the generation-gated, `SensorOwnerControlBlock`-mediated backend callback wiring (reading + calibration).

## Executive Verdict
Correct, and a careful, well-reasoned implementation of a genuinely hard concurrency problem: safely handing a `Motion*` to native-backend-owned worker threads without risking a callback dereferencing a destroyed object. Verified the full `Start()`/`Stop()`/`Dispose(bool)` sequencing:

- `Start()` reserves state (`transitioning_=true`, bumps `control_->generation`, captures a raw `backendPtr`) under lock, then calls into `backendPtr->Start(...)` **without** holding the lock — correctly avoiding a deadlock/reentrancy hazard if the backend synchronously invokes a callback.
- The reading/calibration lambdas passed to the backend capture `control_` (a `shared_ptr`, keeping the control block alive even if `Motion` itself is destroyed) and the generation snapshot at registration time, re-validating both `generation` match and `owner != nullptr` under `control_->mutex` before ever touching `owner`.
- `Dispose(bool)` nulls `control_->owner` under lock **before** calling `Stop()`, so any callback invoked synchronously by `Stop()`'s own `backendPtr->Stop()` call sees `owner == nullptr` and safely no-ops rather than reading a half-torn-down `Motion`.
- The "orphaned start" path (`Start()` completing successfully after a concurrent `Stop()`/newer `Start()` already bumped the generation) correctly calls `backendPtr->Stop()` to undo the now-unwanted started backend, rather than leaving it silently running with nothing tracking it.

## Checklist Results
- `instanceCountMutex_` correctly guards the check+increment/decrement of the shared `instanceCount_`, closing the same class of race the header documents as previously real for `Compass` (Task P6-1).
- The constructor's `catch (...)` block correctly decrements `instanceCount_` if backend construction/probing throws, preventing the max-instance counter from being permanently inflated by a failed construction.
- `backendCallsInFlight_`/`backendQuiescent_` correctly bracket every call into `backendPtr` (`Start()`, the orphaned-start `Stop()`, and `Stop()` itself), letting `SetBackendForTesting()` safely wait for any in-flight backend call to finish before swapping the backend out from under it.

## Detailed Findings
None. One documented, deliberately-accepted boundary (not a defect) is worth noting explicitly: `SensorOwnerControlBlock`'s own doc comment (audited separately) states that a callback which has *already* passed its generation/owner check and is *currently* calling into `owner` on another thread, at the exact instant a *different* thread completes that owner's destruction, remains unsupported/undefined — this class's `Dispose(bool)` does not wait for `backendCallsInFlight_` to drain to zero before allowing the object's lifetime to end (unlike `Accelerometer`/`Gyroscope`'s `dispatchToken_`-based wait in their own `Dispose(bool)`). This is explicitly disclosed as an accepted, narrower boundary than full concurrent-destruction safety, consistent with this project's established practice of documenting known limits rather than hiding them — not re-raised as a new finding here.

## Cross-File Observations
See `Detail::SensorOwnerControlBlock.hpp.audit.md` for the full analysis of the shared control-block design and its documented remaining boundary.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The generation-based callback invalidation and the deliberate "never hold `control_->mutex` across a call into `backend_`" discipline are both genuinely sound solutions to a hard problem, each with a specific, cited real-world motivation (a TSAN-confirmed race in the sibling `Compass` class).

## Final Assessment
No findings.
