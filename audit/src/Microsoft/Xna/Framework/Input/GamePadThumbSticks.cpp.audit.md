# Audit: src/Microsoft/Xna/Framework/Input/GamePadThumbSticks.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Input/GamePadThumbSticks.cpp`
- Audit status: AUDITED (full read, 101 lines)
- Subsystem: `xna-input` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Input/GamePadThumbSticks.cs` (read in
  full) — `ApplyDeadZone`/`ApplySquareClamp`/`ApplyCircularClamp`/`ExcludeCircularDeadZone` all
  verified line-for-line identical
- Main related tests: not independently located in this pass

## Purpose
Implements dead-zone application (`None`/`IndependentAxes`/`Circular`), square/circular clamping,
and `Equals`/`GetHashCode`.

## Executive Verdict
Correct. Every dead-zone/clamp formula matches FNA exactly. `GetHashCode()` correctly applies the
project's `INPUT-BUILD-006` unsigned-wraparound-safe pattern to FNA's real formula
(`Left.GetHashCode() + 37 * Right.GetHashCode()`) — this is a case where FNA genuinely has a
portable, real formula to preserve (unlike `GamePadState`/`MouseState`, noted separately), and the
overflow-safe rewrite is verified to produce the identical numeric result via wraparound.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
None beyond the paired `.hpp` report.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Exact, verified match to FNA's dead-zone math, correctly overflow-hardened.

## Final Assessment
No findings.
