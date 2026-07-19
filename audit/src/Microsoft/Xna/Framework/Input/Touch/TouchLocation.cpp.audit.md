# Audit: src/Microsoft/Xna/Framework/Input/Touch/TouchLocation.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Input/Touch/TouchLocation.cpp`
- Audit status: AUDITED (full read, 109 lines)
- Subsystem: `xna-input` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Input/Touch/TouchLocation.cs` (read in
  full) — `TryGetPreviousLocation`/`Equals`/`GetHashCode`/`ToString` all verified matching
- Main related tests: not independently located in this pass

## Purpose
Implements all 5 constructors, `TryGetPreviousLocation()`'s unconditional out-param write,
`Equals`/`GetHashCode`/`ToString`.

## Executive Verdict
Correct. `TryGetPreviousLocation()` correctly writes its out-param on every path (matching C#'s
mandatory-before-return `out` parameter semantics), citing the same `DEC-12` decision already
applied consistently to `TouchCollection::FindById()`. `GetHashCode()` correctly applies the
project's overflow-safe rewrite to FNA's genuine, portable `Id.GetHashCode() + Position.GetHashCode()`
formula. `ToString()` matches FNA's exact `"{Position:...}"` format.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
The `DEC-12` "out-param written unconditionally" pattern is consistently applied here and in
`TouchCollection::FindById()` (audited separately) — a real, cross-file design consistency worth
noting positively.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Exact, verified match to FNA across all constructors and override methods.

## Final Assessment
No findings.
