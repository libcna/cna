# Audit: src/Microsoft/Xna/Framework/Input/GamePadTriggers.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Input/GamePadTriggers.cpp`
- Audit status: AUDITED (full read, 66 lines)
- Subsystem: `xna-input` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Input/GamePadTriggers.cs` (read in full)
  — verified line-for-line identical
- Main related tests: not independently located in this pass

## Purpose
Implements both constructors (public clamp-only, private dead-zone-aware) and
`Equals`/`GetHashCode`.

## Executive Verdict
Correct. `GetHashCode()` correctly applies the `INPUT-BUILD-006` overflow-safe pattern to FNA's real
formula (`Left.GetHashCode() + Right.GetHashCode()`), verified to produce the identical result via
unsigned wraparound.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
None beyond the paired `.hpp` report.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Exact, verified match to FNA.

## Final Assessment
No findings.
