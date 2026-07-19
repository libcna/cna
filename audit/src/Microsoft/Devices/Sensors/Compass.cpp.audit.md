# Audit: src/Microsoft/Devices/Sensors/Compass.cpp

## Metadata
- Source file: `src/Microsoft/Devices/Sensors/Compass.cpp` (487 lines)
- Audit status: AUDITED (full read)
- Subsystem: `microsoft-devices` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA has no reference material for this namespace (WP7-only API, never implemented in desktop FNA)
- Main related tests: not independently located in this pass

## Purpose
Implements `Compass`'s lifecycle: instance limiting, the `SensorOwnerControlBlock`-based
generation-counter scheme for safely disowning in-flight native-backend callbacks, and the
`Start()`/`Stop()` non-blocking-supersession design (`transitioning_`/`stopClaimed_`/
`backendCallsInFlight_`).

## Executive Verdict
Very carefully engineered, with an explicit, well-reasoned decision to make `Stop()` deliberately
non-blocking with respect to an in-flight `Start()` (documented and tested:
`ConcurrentStopDuringStartDoesNotDeadlock`), while `Start()` itself does wait for any earlier
orphaned cleanup to finish (Task TEST2-001, closing a real ThreadSanitizer-confirmed data
race/heap-corruption bug in a test fake). Shares the same MEDIUM `Dispose(bool)` finding as
`Accelerometer.cpp`.

## Checklist Results
- Constructor (lines 37-117): correctly wraps everything past the atomic `instanceCount_`
  increment (backend construction, `IsSupported()` probing, event-handler subscription) in a
  try/catch that rolls back the counter on any throw (Task LIFE-008) — mirrors
  `Accelerometer`'s own already-correct pattern.
- `Start()` (lines 127-329): the `orphanedStart` handling (lines 270-322) correctly tears down a
  `Start()` attempt that raced against a concurrent, superseding `Stop()` and "won" the backend
  call but lost the generation race — a real, subtle case the code handles by checking
  `control_->generation != myGeneration` after the backend call returns, not before.
- Backend callbacks (lines 209-251) correctly capture `control_` (a `shared_ptr` copy) and
  `myGeneration` by value, never `this` — and correctly never call user code
  (`CurrentValueChanged.Raise()`/`Calibrate.Raise()`) while holding `control->mutex`.
- `Dispose(bool disposing)` (lines 397-456): the `disposing == true` path correctly nulls
  `control_->owner` *before* calling `Stop()` (so any in-flight callback that hasn't yet passed its
  own generation/owner check safely no-ops) — correct ordering. **The `disposing == false`
  early-return path is the MEDIUM finding — see below.**

## Detailed Findings

### MEDIUM — `Dispose(bool disposing)`'s `!disposing` branch skips all real cleanup (identical to `Accelerometer.cpp`)
```cpp
void Compass::Dispose(bool disposing)
{
    if (!disposing)
    {
        SensorBase<CompassReading>::Dispose(disposing);
        return;
    }
    // ... full cleanup (nulling control_->owner, Stop(), decrementing instanceCount_) only
    // runs here, when disposing == true ...
}
```
(lines 397-403). Identical mechanism and identical real-world consequence to the finding already
documented in full in `src/Microsoft/Devices/Sensors/Accelerometer.cpp.audit.md` — since
`Dispose(bool)` is `public` (see `Compass.hpp.audit.md`), an external `compass.Dispose(false)` call
marks the object disposed without nulling `control_->owner`, without calling `Stop()` (so a real
Android backend keeps running), and without decrementing `instanceCount_`.

## Cross-File Observations
Identical finding already reported for `Accelerometer.cpp`. This is now confirmed present in both
of the two sensor classes audited in this pass — strongly suggesting `Gyroscope`/`Motion`
(audited by a sibling pass in this shard) share the identical structure, since all four classes are
explicitly designed in parallel throughout this subsystem's own doc comments.

## Missing or Weak Tests
Same gap as `Accelerometer.cpp`.

## Positive Findings
The non-blocking-`Stop()`-during-in-flight-`Start()` design, and its explicit, tested
non-deadlock guarantee, is a genuinely sophisticated piece of lock-free-adjacent concurrent
design reasoning.

## Final Assessment
One MEDIUM finding, identical in shape and mechanism to `Accelerometer.cpp`'s.
