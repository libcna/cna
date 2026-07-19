# Audit: include/Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp`
- Audit status: AUDITED (full read, 151 lines)
- Subsystem: `xna-input` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Input/Touch/TouchLocation.cs` (read in
  full) — `Equals`/`GetHashCode`/`ToString` verified matching (including a genuine, portable FNA
  formula for `GetHashCode`, unlike `GamePadState`/`MouseState` noted elsewhere in this shard)
- Main related tests: not independently located in this pass

## Purpose
One touch-point snapshot with optional previous-location data; adds a NOXNA pressure extension.

## Executive Verdict
Correct. `TryGetPreviousLocation()`'s contract and the pressure field's correct exclusion from
`Equals`/`GetHashCode`/`ToString` (to stay FNA-frozen) are both consistent with the same pattern
already seen in `MouseState`'s horizontal-scroll-wheel extension.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
`GetHashCode()` verified in the paired `.cpp` to match FNA's real, portable formula
(`Id.GetHashCode() + Position.GetHashCode()`) exactly, via the same `INPUT-BUILD-006`
overflow-safe rewrite pattern used correctly elsewhere in this shard.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Correct, verified match to FNA including a genuine portable hash formula.

## Final Assessment
No findings.
