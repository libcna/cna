# Audit: src/Microsoft/Xna/Framework/Graphics/VertexElement.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/VertexElement.cpp`
- Audit status: AUDITED (full read, 51 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Vertices/VertexElement.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements `ToString()`'s format-name/usage-name lookup and final string assembly.

## Executive Verdict
Correct. `fmtName`/`usageName` cover all 12/13 real enum values respectively, and the final
string layout (`"{{Offset:N Format:F Usage:U UsageIndex: I}}"`) matches FNA's own `ToString()`
format exactly, including the single space after `UsageIndex:`.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
None beyond the paired `.hpp` report.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Exhaustive `switch` coverage for both enums, with a sensible numeric fallback for any future
value.

## Final Assessment
No findings.
