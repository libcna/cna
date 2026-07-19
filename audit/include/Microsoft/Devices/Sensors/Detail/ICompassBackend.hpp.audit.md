# Audit: include/Microsoft/Devices/Sensors/Detail/ICompassBackend.hpp

## Metadata
- Source file: `include/Microsoft/Devices/Sensors/Detail/ICompassBackend.hpp` (75 lines)
- Audit status: AUDITED (full read)
- Subsystem: `microsoft-devices` shard
- File type: C++ header
- XNA/FNA relevance: NOXNA internal backend interface, no FNA/WP7 equivalent (a CNA-internal abstraction)
- Main related tests: not independently located in this pass

## Purpose
Platform-native compass backend interface; `Compass` (not in this batch) selects a concrete implementation (e.g. `AndroidCompassBackend`) at construction time, matching this project's established `IGraphicsBackend`-style pluggable-backend convention.

## Executive Verdict
Correct, clean, well-scoped interface. Cleanly separates the XNA-facing `Compass` class from any platform-specific implementation — this header itself has zero platform-specific `#include`s, confirmed by direct inspection, matching its own doc comment's claim that it "compiles and is mockable on every platform."

## Checklist Results
- `Start()`'s doc comment correctly specifies the callback-thread-safety contract ("implementations may call this from a background thread — callers must treat it as running on an unknown thread") consistent with `Accelerometer`/`Gyroscope`'s already-audited `CurrentValueChanged` contract elsewhere in this shard.
- `Start()`'s return-value contract explicitly forbids "reporting success optimistically before delivery has genuinely begun" — a precise, testable contract rather than a vague "returns whether it started."
- `SetSampleInterval()`'s "safe no-op if not currently started" contract is a sensible, low-surprise default for a method whose real-world call pattern (changing sample rate live) is inherently racy against a concurrent `Stop()`.

## Detailed Findings
None.

## Cross-File Observations
Structurally identical in shape to `IMotionBackend` (audited separately), confirmed via direct comparison — both mirror each other's `Start`/`Stop`/`SetSampleInterval`/calibration-callback shape, consistent with `IMotionBackend`'s own doc comment stating it "mirrors `ICompassBackend`'s shape."

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
A clean, minimal, platform-agnostic interface with precisely specified contracts for the two hardest parts to get right (background-thread callback delivery, honest start/fail signaling).

## Final Assessment
No findings.
