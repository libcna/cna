# Audit: include/Microsoft/Xna/Framework/Graphics/CompareFunction.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/CompareFunction.hpp` (26 lines)
- Audit status: AUDITED (full read)
- Subsystem: `xna-graphics` shard
- File type: C++ header (enum)
- XNA/FNA relevance: Direct XNA type; FNA reference: `Graphics/States/CompareFunction.cs` (fully diffed)
- Main related tests: not independently located in this pass

## Purpose
Defines the 8 depth/stencil/alpha comparison functions (`Always`, `Never`, `Less`, `LessEqual`, `Equal`, `GreaterEqual`, `Greater`, `NotEqual`).

## Executive Verdict
Correct. All 8 values, same order/ordinal, accurate descriptions matching FNA exactly.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
None.

## Missing or Weak Tests
Not applicable (plain enum).

## Positive Findings
Complete, faithful port.

## Final Assessment
No findings.
