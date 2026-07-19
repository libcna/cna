# Audit: include/Microsoft/Devices/Sensors/Accelerometer.hpp

## Metadata
- Source file: `include/Microsoft/Devices/Sensors/Accelerometer.hpp` (447 lines)
- Audit status: AUDITED (full read)
- Subsystem: `microsoft-devices` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA has no reference material for this namespace (WP7-only API, never implemented in desktop FNA)
- Main related tests: not independently located in this pass; extensive task-ID citations imply a substantial existing test suite (`ConcurrentDisposeFromMultipleThreadsNeverCorruptsInstanceCount`, `EleventhSimultaneousInstanceThrows`, `DisposingOneOfTenAllowsAnotherConstruction` are named directly in the `.cpp`)

## Purpose
Provides access to the device accelerometer sensor via a real SDL3-backed implementation
(`SDL_SENSOR_ACCEL`), supporting Android/iOS/Desktop wherever SDL detects real hardware.

## Executive Verdict
Extensively hardened, with one real MEDIUM-severity defect: `Dispose(bool disposing)` is
re-declared `public` here, unlike the base class's correct `protected` declaration, letting
external code call `Dispose(false)` directly and silently skip all real cleanup.

## Checklist Results
- `Accelerometer::ReadingChanged` (legacy WP7 7.0 event) is correctly retained alongside the
  WP7 7.1 `CurrentValueChanged` pattern, with its own doc comment citing the archived MSDN
  `[ObsoleteAttribute]` annotation confirming it's still real, present API on every WP7 version —
  not a fabricated addition.
- Extensive `NOXNA`-tagged test-only hooks (`InjectSyntheticSensorUpdate`,
  `SetStartedForTesting`, `SetSupportedForTesting`, `GetSubsystemHeldForTesting`,
  `SetDisposalCleanupHookForTesting`, `RegisterStartedInstanceForTesting`/
  `UnregisterStartedInstanceForTesting`, `DispatchToInstancesForTesting`,
  `SetEventWatchRegistrationFailureForTesting`, `GetDispatchExceptionCountForTesting`/
  `GetLastDispatchExceptionMessageForTesting`, `IsSensorConnectedForTesting`) are all correctly
  tagged and each has a substantive doc comment explaining exactly what real concurrency/lifecycle
  scenario it exists to make testable in a headless environment with no real hardware — an unusually
  thorough test-seam design.

## Detailed Findings

### MEDIUM — `Dispose(bool disposing)` is declared `public`, not `protected`, breaking the standard `IDisposable` `Dispose(bool)` idiom and letting external code silently skip real cleanup
```cpp
public:
    Accelerometer();
    ~Accelerometer() override;
    void Start() override;
    void Stop() override;
    void Dispose(bool disposing) override;
    using SensorBase<AccelerometerReading>::Dispose;
    GetTypeNameHPP()
    ...
```
The base class, `SensorBase<T>`, correctly declares `virtual void Dispose(bool disposing)` under a
`protected:` section (see `SensorBase.hpp.audit.md`) — matching the standard C++/`.NET` idiom where
this overload must never be callable directly by a consumer, only reachable via the public no-arg
`Dispose()` or the destructor path. Here, the override is re-declared under a `public:` access
specifier. C++ permits a derived class to widen an inherited virtual member's access on override
(unlike C#, which requires matching accessibility) — this compiles, but defeats the entire purpose
of the pattern.

**Failure scenario**: `Accelerometer accel; accel.Start(); accel.Dispose(false);` — a syntactically
legal, directly-callable public API call. Per `Accelerometer::Dispose(bool)`'s own body (`.cpp`,
audited separately): `if (!disposing) { SensorBase<AccelerometerReading>::Dispose(disposing); return; }`
— this branch marks `disposed_ = true` (via the base) and returns immediately, **without** calling
`Stop()`, without decrementing the shared `instanceCount_`, without releasing the SDL subsystem hold
(`subsystemHeld_`), and without unregistering the SDL event watch. The result: the sensor keeps
polling and delivering events indefinitely (its "started" bookkeeping is untouched), the shared
10-instance limit permanently loses one slot for the lifetime of the process, and the object itself
is now permanently unusable (every subsequent public method throws `ObjectDisposedException` via
`getIsDisposedProperty()`) — a real, externally-reachable resource leak combined with a
permanently-broken object, not a theoretical visibility nitpick.

**Suggested fix** (report-only; no source changes made per this audit's scope): move
`void Dispose(bool disposing) override;` back under a `protected:` (or `private:`) section in both
`Accelerometer` and `Compass`, matching the base class.

## Cross-File Observations
The identical pattern is present in `Compass.hpp` (see that file's own report). Not confirmed in
this pass whether `Gyroscope.hpp`/`Motion.hpp` (audited by a sibling pass in this same shard) share
it — worth checking, since all four sensor classes are structurally parallel and very likely
share this exact override shape.

## Missing or Weak Tests
A test asserting `Dispose(false)` either isn't public API or, if it is, still performs full cleanup
would have caught this; not located in this pass.

## Positive Findings
The `ReadingChanged`/`CurrentValueChanged` dual-event design, and the exhaustive suite of
test-only concurrency-scenario hooks, both reflect a genuinely thorough engineering effort.

## Final Assessment
One MEDIUM finding: `Dispose(bool disposing)` is public, not protected, letting external code
silently corrupt the object's lifecycle.
