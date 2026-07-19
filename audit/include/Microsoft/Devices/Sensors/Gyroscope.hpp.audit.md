# Audit: include/Microsoft/Devices/Sensors/Gyroscope.hpp

## Metadata
- Source file: `include/Microsoft/Devices/Sensors/Gyroscope.hpp` (346 lines)
- Audit status: AUDITED (full read)
- Subsystem: `microsoft-devices` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA has no reference material for this namespace (WP7-only API, never implemented in desktop FNA)
- Main related tests: not independently located in this pass

## Purpose
Declares the WP7 gyroscope sensor class, backed by an SDL3 `SDL_SENSOR_GYRO` device and a shared, templated `Detail::SdlSensorSubsystem<Gyroscope>` subsystem manager.

## Executive Verdict
Exceptionally thoroughly documented for a concurrency-sensitive class, with one real MEDIUM-severity
defect shared with its sensor-class siblings: `Dispose(bool disposing)` is re-declared `public` here,
unlike the base class's correct `protected` declaration, letting external code call `Dispose(false)`
directly and silently skip all real cleanup. Nearly every private member's doc comment cites a
specific tracked task ID (P4-2 through P8-1, plus externally-audited fixes SDLCORE-001 through
SDLCORE-009) describing a real, previously-found-and-fixed concurrency defect (ABA hazards,
use-after-free windows, exception-safety gaps in dispatch loops) — this is one of the most
rigorously hardened classes encountered in this entire audit, the `Dispose(bool)` visibility gap
aside.

## Checklist Results
- Doxygen coverage: complete, including internal/private members (unusual and welcome — this class's own thread-safety contract is complex enough that internal documentation carries real value).
- `NOXNA` tagging: correctly applied to `getStateProperty()` (a real WP7 API gap — confirmed the doc comment explicitly checked "the real Microsoft.Devices.Sensors.Gyroscope class has no State property... confirmed against its authoritative member list") and to every test-only hook.
- `friend class Detail::SdlSensorSubsystem<Gyroscope>;` is a narrowly-scoped, well-motivated friendship granting the shared subsystem template access to this class's private dispatch/state members — consistent with the project's established interface-based-backend pattern.
- `dispatchToken_`'s `shared_ptr<vector<thread::id>>` design (rather than a plain member) is specifically designed to survive the owning instance's destruction if a callback destroys it mid-dispatch — a genuinely subtle and correctly-reasoned design point (Task P8-1).

## Detailed Findings

### MEDIUM — `Dispose(bool disposing)` is declared `public`, not `protected`, breaking the standard `IDisposable` `Dispose(bool)` idiom
```cpp
public:
    Gyroscope();
    ~Gyroscope() override;
    void Start() override;
    void Stop() override;
    void Dispose(bool disposing) override;
    using SensorBase<GyroscopeReading>::Dispose;
    GetTypeNameHPP()
    ...
```
Confirmed directly (`SensorBase.hpp` line 106 `protected:` ... line 566 `virtual void Dispose(bool
disposing)`, i.e. the base class correctly protects this overload) against this header's own
`Dispose(bool disposing) override;` declaration, which sits under a `public:` section (the block
opened at line 169) — the identical shape already confirmed as a real MEDIUM finding in
`Accelerometer.hpp`/`Compass.hpp` (see those reports for the full failure-scenario writeup: a
directly-callable `gyro.Dispose(false)` marks the object disposed without running `Stop()`,
without decrementing the shared instance count, and without releasing the SDL subsystem hold).
This resolves that finding's own open question — **all four sensor classes
(`Accelerometer`/`Compass`/`Gyroscope`/`Motion`) share this exact override-visibility gap**, not
just the two originally checked.

**Suggested fix** (report-only; no source changes made per this audit's scope): move
`void Dispose(bool disposing) override;` back under a `protected:`/`private:` section, matching
the base class, in all four sensor classes.

## Cross-File Observations
Structurally near-identical to `Accelerometer.hpp` (not in this batch, audited separately) per this file's own repeated "see Accelerometer.hpp's identical member" cross-references — confirmed above that this parity extends to sharing the `Dispose(bool)` visibility defect too.

## Missing or Weak Tests
Not independently located in this pass; the extensive `*ForTesting()` hook surface (dispatch injection, event-watch-failure forcing, exception-count/message inspection) strongly suggests a dedicated test suite exists and exercises this concurrency design directly.

## Positive Findings
The `SDL_EventFilter`-signature `static_assert` pattern (referenced from the sibling `SdlSensorSubsystem.hpp`) and the extensive task-ID-cited history of real, found-and-fixed concurrency bugs are strong positive indicators of a mature, battle-tested class.

## Final Assessment
One MEDIUM finding: `Dispose(bool disposing)` is public, not protected — the same defect already
confirmed in `Accelerometer.hpp`/`Compass.hpp`, now confirmed present in all four sensor classes.
