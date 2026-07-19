# Audit: include/Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp` (225 lines)
- Audit status: AUDITED (full read)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `Graphics/States/DepthStencilState.cs` (fully diffed line-for-line)
- Main related tests: not independently located in this pass

## Purpose
Represents depth-test/depth-write/stencil-test/two-sided-stencil state for the graphics pipeline, matching real XNA's `DepthStencilState`.

## Executive Verdict
Fully correct and complete. Every one of the 16 properties (depth enable/write/function, stencil enable/function/masks/reference, stencil pass/fail/depth-fail operations for both clockwise and counter-clockwise faces, two-sided mode) and all three presets (`Default`/`DepthRead`/`None`) match FNA's `DepthStencilState.cs` exactly.

## Checklist Results
- Doxygen coverage: complete.
- `GetTypeName()` correctly declared and implemented (via `GetTypeNameCPP` in the `.cpp`).
- No exception-throwing methods — convention check not applicable.

## Detailed Findings
None.

## Cross-File Observations
This project's own `audit/AUDIT_CROSS_CUTTING_FINDINGS.md` records a HIGH finding that D3D12's backend never forwards *any* of `DepthStencilState`'s stencil fields, or `RasterizerState.ScissorTestEnable`, into a real PSO. This audit confirms that finding is entirely backend-side: this XNA-facing `DepthStencilState` class itself correctly stores and exposes every field FNA's real class does, with no omission. No fix is needed or possible in this file for that cross-cutting finding.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
A byte-for-byte-faithful, complete port with no omissions — notably including `stencilMask_`/`stencilWriteMask_` defaulting to `0x7FFFFFFF`, confirmed exactly equal to FNA's `Int32.MaxValue`.

## Final Assessment
No findings.
