# Audit: include/Microsoft/Devices/Sensors/Motion.hpp

## Metadata
- Source file: `include/Microsoft/Devices/Sensors/Motion.hpp` (233 lines)
- Audit status: AUDITED (full read)
- Subsystem: `microsoft-devices` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA has no reference material for this namespace (WP7-only API, never implemented in desktop FNA)
- Main related tests: not independently located in this pass

## Purpose
Declares the WP7 fused-motion sensor class (Attitude/Gravity/DeviceAcceleration/DeviceRotationRate), backed by a real Android native implementation (`Detail::AndroidMotionBackend`) and a permanent unsupported stub on every other platform (SDL3 has no fused-orientation API anywhere).

## Executive Verdict
Honestly scoped, with one real MEDIUM-severity defect shared with its sensor-class siblings: the
class's own doc comment states plainly that SDL3 exposes no fused-motion API on any platform, so
this class has no SDL-backed implementation at all — real functionality exists only via the
Android-specific backend. This is disclosed precisely, not left for a reader to discover by
testing on desktop and finding it silently unsupported. Separately, `Dispose(bool disposing)` is
re-declared `public` here, unlike the base class's correct `protected` declaration, letting
external code call `Dispose(false)` directly and silently skip all real cleanup.

## Checklist Results
- `control_` (a `shared_ptr<Detail::SensorOwnerControlBlock<Motion>>`) replaces a prior plain `mutex_` member specifically because `Compass` (a structurally identical sibling class) had a real ThreadSanitizer-confirmed data race (Task SENSORBASE-004) that this design closes — the doc comment cites the actual TSAN finding, not a hypothetical concern.
- The doc comment explicitly states `control_->mutex` is now never held across a call into `backend_` (Task LIFE-001), since `AndroidMotionBackend::Start()`/`Stop()` can block and may synchronously invoke a callback before returning — a real, correctly-identified deadlock/reentrancy hazard.
- `getIsAttitudeNorthReferencedProperty()`'s doc comment cites an external audit (`audit_devices_2026-07-17.md`, Task MOT2-005) as the origin of this diagnostic property, added specifically to expose a real fallback (magnetometer-fused vs. gyroscope/accelerometer-only rotation vector) that previously had no observable signal for callers.

## Detailed Findings

### MEDIUM — `Dispose(bool disposing)` is declared `public`, not `protected`, breaking the standard `IDisposable` `Dispose(bool)` idiom
```cpp
public:
    Motion();
    ~Motion() override;
    void Start() override;
    void Stop() override;
    void Dispose(bool disposing) override;
    using SensorBase<MotionReading>::Dispose;
    GetTypeNameHPP()
    ...
```
Confirmed directly (`SensorBase.hpp` line 106 `protected:` ... line 566
`virtual void Dispose(bool disposing)`, i.e. the base class correctly protects this overload)
against this header's own `Dispose(bool disposing) override;` declaration, which sits under a
`public:` section (the block opened at line 157) — the identical shape already confirmed as a real
MEDIUM finding in `Accelerometer.hpp`/`Compass.hpp` (see those reports for the full
failure-scenario writeup). This resolves that finding's own open question — **all four sensor
classes (`Accelerometer`/`Compass`/`Gyroscope`/`Motion`) share this exact override-visibility gap**,
not just the two originally checked.

**Suggested fix** (report-only; no source changes made per this audit's scope): move
`void Dispose(bool disposing) override;` back under a `protected:`/`private:` section, matching
the base class, in all four sensor classes.

## Cross-File Observations
This class's `SensorOwnerControlBlock`-based design (shared with `Compass`, not in this batch) is the more sophisticated sibling to `Accelerometer`/`Gyroscope`'s `dispatchToken_`-based design — the two coexist because they serve genuinely different backend shapes (SDL polling+event-watch dispatch vs. a native async backend with its own worker threads), not because one is a stale predecessor of the other. That structural parity with `Compass` extends to sharing its `Dispose(bool)` visibility defect too.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The platform-honesty in this class's own top-of-file comment (explicitly stating SDL3 has no fused-motion API at all, rather than implying broader platform support than actually exists) is a strong example of accurate scope disclosure.

## Final Assessment
One MEDIUM finding: `Dispose(bool disposing)` is public, not protected — the same defect already
confirmed in `Accelerometer.hpp`/`Compass.hpp`, now confirmed present in all four sensor classes.
