# Audit: include/Microsoft/Xna/Framework/Graphics/StencilOperation.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/StencilOperation.hpp` (27 lines)
- Audit status: AUDITED (full read)
- Subsystem: `xna-graphics` shard
- File type: C++ header (enum)
- XNA/FNA relevance: Direct XNA type; FNA reference: `Graphics/States/StencilOperation.cs` (fully diffed)
- Main related tests: not independently located in this pass

## Purpose
Defines the 8 stencil-buffer-update operations (`Keep`, `Zero`, `Replace`, `Increment`, `Decrement`, `IncrementSaturation`, `DecrementSaturation`, `Invert`).

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
