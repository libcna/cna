# Audit: include/Microsoft/Xna/Framework/Graphics/DepthFormat.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/DepthFormat.hpp`
- Audit status: AUDITED (full read, 19 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/DepthFormat.cs`
- Main related tests: not independently located in this pass

## Purpose
Enumerates supported depth-stencil buffer formats.

## Executive Verdict
Correct. Values (`None, Depth16, Depth24, Depth24Stencil8`) match FNA's `DepthFormat.cs` exactly.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
Consumed by `IRenderTarget::getDepthStencilFormatProperty()`, `RenderTarget2D`, `RenderTargetCube` (all audited in this same batch).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Exact match to FNA reference.

## Final Assessment
No findings.
