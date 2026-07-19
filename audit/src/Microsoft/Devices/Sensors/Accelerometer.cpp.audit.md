# Audit: src/Microsoft/Devices/Sensors/Accelerometer.cpp

## Metadata
- Source file: `src/Microsoft/Devices/Sensors/Accelerometer.cpp` (882 lines)
- Audit status: AUDITED (full read)
- Subsystem: `microsoft-devices` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA has no reference material for this namespace (WP7-only API, never implemented in desktop FNA)
- Main related tests: not independently located in this pass

## Purpose
Implements `Accelerometer`'s real SDL3-backed lifecycle: instance-count limiting, subsystem
init/teardown, event-watch registration, per-instance dispatch-token bookkeeping for safe
concurrent-callback disposal, and the Android landscape axis remap.

## Executive Verdict
Exceptionally well-engineered concurrent C++ — every non-trivial synchronization decision cites a
specific, named prior bug (many confirmed via real ThreadSanitizer/stress-test runs, e.g. the
constructor's `instanceCount_` race reproducing a genuine glibc heap-corruption abort "in roughly 1
in 4 runs"). One real MEDIUM finding, shared with `Compass.cpp`: the public `Dispose(bool disposing)`
override's `!disposing` branch skips all real cleanup.

## Checklist Results
- Constructor (lines 120-158): the `instanceCount_` check+increment is correctly atomic under
  `subsystem.mutex_`, released before the (slow, real-SDL-probing) `getIsSupportedProperty()` call —
  correctly avoids holding a lock across a genuinely slow operation while still keeping the counter
  race-free. Constructor exception safety (rollback of the just-incremented counter on any
  downstream throw) is correct.
- `Start()` (lines 168-283): registers the SDL event watch *before* committing `started_`/`state_`
  to `Ready` (a documented fix, Task SDLCORE-003, for a real "permanently Ready but the event watch
  never actually registered" bug) — confirmed correctly ordered. Subsystem-hold rollback on failure
  correctly releases only the hold *this* call itself acquired, not a pre-existing one from an
  earlier `Start()`/`Stop()` cycle.
- `Dispose(bool disposing)` (lines 303-441): the `disposing == true` path correctly implements the
  full `ClaimDisposalOnce()`/`DisposalTerminalStateGuard`/wait-for-in-flight-callbacks/
  decrement-instance-count/release-subsystem-hold sequence, with a defensive `assert()` (Task
  BASE2-007) guarding against `instanceCount_` underflow rather than silently clamping it (which
  would mask a real regression instead of surfacing it) — a good, deliberate choice. **The
  `disposing == false` early-return path is the MEDIUM finding — see below.**
- `DispatchSensorReading()` (lines 581-714): correctly takes a local snapshot of `ReadingChanged`
  (Task LIFE-004) before raising `CurrentValueChanged`, specifically to survive a
  `CurrentValueChanged` handler destroying `this` before `ReadingChanged` would otherwise need to be
  raised through a now-dangling `this->ReadingChanged` — a real, subtle use-after-free this fix
  correctly closes, with an honestly-disclosed remaining boundary (a handler destroying `this` from
  within its own `CurrentValueChanged`/`ReadingChanged` callback is still unsupported, since
  `DispatchSensorReading()` itself touches `this` again afterward — documented as a known,
  class-design-level limit, not silently unhandled).
- The `#ifdef __ANDROID__` axis-remap math (`ConvertAndroidAccelerometerToXnaLandscape`) is
  extensively justified against SDL's own documented `SDL_SENSOR_ACCEL` axis convention, with the
  actual sign-remap math factored into a separately-unit-testable pure function
  (`Detail::ConvertAndroidPortraitToXnaLandscape`, shared with `Gyroscope`) — a good design choice
  for testability of platform-specific math with no real Android device required.

## Detailed Findings

### MEDIUM — `Dispose(bool disposing)`'s `!disposing` branch skips all real cleanup, and this method is reachable directly (see `Accelerometer.hpp.audit.md` for the full analysis)
```cpp
void Accelerometer::Dispose(bool disposing)
{
    if (!disposing)
    {
        SensorBase<AccelerometerReading>::Dispose(disposing);
        return;
    }
    // ... full cleanup only runs here, when disposing == true ...
}
```
(lines 303-309). Since this override is declared `public` (see the header's own finding), any
caller can invoke `accel.Dispose(false)` directly and take this early-return branch, which marks
the object disposed without running `Stop()`, without decrementing `instanceCount_`, and without
releasing the SDL subsystem hold — see the header report for the full failure-scenario writeup.
This `.cpp` file confirms the exact mechanism.

## Cross-File Observations
Identical structure in `Compass.cpp`'s own `Dispose(bool)` — see that file's report. Both classes'
destructors (`~Accelerometer()`, `~Compass()`) always call `Dispose(true)`, never `Dispose(false)`,
so this early-return branch is dead code from every call site *within this codebase itself* — the
risk is entirely from external callers exercising the public API surface directly, which the
`protected` convention in `SensorBase.hpp` was specifically designed to prevent.

## Missing or Weak Tests
A test directly calling `accel.Dispose(false)` and asserting either a compile-time visibility error
(if fixed to `protected`) or full cleanup (if intentionally kept public) is missing.

## Positive Findings
The dispatch-token/`shared_ptr`-based use-after-free protection (Task P8-1) for a callback that
destroys its own sensor instance mid-dispatch, and the constructor's exception-safe instance-count
rollback, are both genuinely careful, well-reasoned pieces of defensive concurrent design.

## Final Assessment
One MEDIUM finding, shared with `Compass.cpp`: the public `Dispose(bool)` override's `!disposing`
branch provides a real, externally-reachable way to corrupt this object's resource lifecycle.
