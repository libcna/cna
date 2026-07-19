# Audit: include/Microsoft/Devices/Sensors/Compass.hpp

## Metadata
- Source file: `include/Microsoft/Devices/Sensors/Compass.hpp` (235 lines)
- Audit status: AUDITED (full read)
- Subsystem: `microsoft-devices` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA has no reference material for this namespace (WP7-only API, never implemented in desktop FNA)
- Main related tests: not independently located in this pass

## Purpose
Provides access to the device compass sensor. Real on Android via `Detail::AndroidCompassBackend`
(NDK rotation-vector + magnetic-field sensors); `getIsSupportedProperty()` always returns `false` on
every other platform, since SDL3 exposes no magnetometer API anywhere.

## Executive Verdict
Shares the same MEDIUM finding as `Accelerometer.hpp`: `Dispose(bool disposing)` is re-declared
`public` here too. The `SensorOwnerControlBlock`-based generation-counter design for safely
disowning in-flight native-backend callbacks during teardown is a genuinely sophisticated and
correct piece of engineering.

## Checklist Results
- `getStateProperty()` is correctly `NOXNA`-tagged and documented as a CNA extension beyond the real
  WP7 `Compass` API (which has no `State` property at all, confirmed against its "authoritative
  member list") — added only for symmetry with `Accelerometer`, the one class that does have a real
  `State` property. Honestly disclosed rather than silently presented as real API.
- `Calibrate` event (real WP7 `Compass.Calibrate`) is present and correctly typed
  (`System::EventHandler<CalibrationEventArgs>`).
- `SetBackendForTesting()` is correctly `NOXNA`-tagged and its doc comment explicitly states it
  enforces (not just documents) that swapping the backend while started/transitioning throws,
  preventing an orphaned running backend.

## Detailed Findings

### MEDIUM — `Dispose(bool disposing)` is declared `public`, not `protected` (same defect as `Accelerometer.hpp`)
```cpp
public:
    Compass();
    ~Compass() override;
    void Start() override;
    void Stop() override;
    void Dispose(bool disposing) override;
    using SensorBase<CompassReading>::Dispose;
    GetTypeNameHPP()
    ...
```
Identical shape and identical real-world consequence to the finding already documented in full in
`include/Microsoft/Devices/Sensors/Accelerometer.hpp.audit.md` — see that report for the complete
failure-scenario analysis (an external `compass.Dispose(false)` call marks the object disposed
without running `Stop()`, without nulling `control_->owner`, and without decrementing
`instanceCount_`).

## Cross-File Observations
Identical finding already reported for `Accelerometer.hpp`/`Accelerometer.cpp`, and now confirmed
present in `Gyroscope.hpp`/`Motion.hpp` too (re-checked directly) — all four sensor classes share
this exact override-visibility gap.

## Missing or Weak Tests
Same gap as `Accelerometer.hpp` — a test directly exercising `Dispose(false)`'s public reachability
is missing.

## Positive Findings
`Detail::SensorOwnerControlBlock<Compass>`'s generation-counter design (each `Start()` call captures
a `myGeneration` value; a native-backend callback checks `control->generation == myGeneration` and
`control->owner != nullptr` before touching the sensor) is a clean, correct solution to safely
disowning in-flight callbacks from a backend that can invoke them on an arbitrary thread, without
ever needing to hold a lock across the actual callback invocation.

## Final Assessment
One MEDIUM finding, identical in shape to `Accelerometer.hpp`'s: public `Dispose(bool)`.
