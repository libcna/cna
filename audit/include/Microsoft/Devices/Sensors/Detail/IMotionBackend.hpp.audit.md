# Audit: include/Microsoft/Devices/Sensors/Detail/IMotionBackend.hpp

## Metadata
- Source file: `include/Microsoft/Devices/Sensors/Detail/IMotionBackend.hpp` (103 lines)
- Audit status: AUDITED (full read)
- Subsystem: `microsoft-devices` shard
- File type: C++ header
- XNA/FNA relevance: NOXNA internal backend interface, no FNA/WP7 equivalent (a CNA-internal abstraction)
- Main related tests: not independently located in this pass

## Purpose
Platform-native fused-motion backend interface; `Motion` (audited separately) selects a concrete implementation (e.g. `AndroidMotionBackend`) at construction time.

## Executive Verdict
Correct, clean, well-scoped interface, structurally mirroring the sibling `ICompassBackend` by design (explicitly stated in this file's own doc comment, since `Motion`'s fused attitude depends on the same magnetometer data `Compass`'s `Calibrate` logic reacts to).

## Checklist Results
- `IsUsingNorthReferencedAttitudeSource()`'s doc comment precisely specifies its "meaningless before first successful Start(), return true as the nothing-to-warn-about default" contract — confirmed consistent with `Motion::getIsAttitudeNorthReferencedProperty()`'s own identical vacuous-true-when-not-yet-started documentation (audited separately).
- `onCalibrationNeeded`'s contract correctly documents that "an implementation that cannot detect this condition at all... simply never invokes it — this is not an error" — an honest, precise statement of an optional-signal contract rather than implying every backend must support calibration detection.

## Detailed Findings
None.

## Cross-File Observations
Confirmed structurally identical in shape to `ICompassBackend`, plus one additional method (`IsUsingNorthReferencedAttitudeSource()`) specific to `Motion`'s fallback-diagnostics need — consistent with both files' own cross-referencing doc comments.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The explicit "meaningless before first Start(), return true" default contract for a diagnostic property is a good example of a well-specified interface avoiding an ambiguous default.

## Final Assessment
No findings.
