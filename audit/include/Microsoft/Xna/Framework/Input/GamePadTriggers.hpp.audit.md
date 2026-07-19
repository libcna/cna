# Audit: include/Microsoft/Xna/Framework/Input/GamePadTriggers.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Input/GamePadTriggers.hpp`
- Audit status: AUDITED (full read, 76 lines)
- Subsystem: `xna-input` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Input/GamePadTriggers.cs` (read in full)
- Main related tests: not independently located in this pass

## Purpose
Represents left/right analog trigger positions with clamping and dead-zone processing.

## Executive Verdict
Correct. Both constructors, `Equals`/`GetHashCode`/operators verified byte-for-byte identical to
FNA in the paired `.cpp`, including the exact comment ("XNA applies dead zones before
rounding/clamping values... The public constructor does not allow this because the dead zone must
be known first") preserved as the design rationale.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
Consumed correctly by `GamePad::GetState()` and `GamePadState`'s constructor (both audited
separately) with `GamePad::TriggerThreshold` (confirmed matching FNA's `30/255` constant).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Exact, verified match to FNA.

## Final Assessment
No findings.
