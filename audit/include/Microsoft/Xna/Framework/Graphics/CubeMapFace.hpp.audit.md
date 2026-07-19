# Audit: include/Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp`
- Audit status: AUDITED (full read, 23 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/CubeMapFace.cs`
- Main related tests: not independently located in this pass

## Purpose
Enumerates the six faces of a cube map, used by `TextureCube`/`RenderTargetCube`.

## Executive Verdict
Correct. Values and order (`PositiveX, NegativeX, PositiveY, NegativeY, PositiveZ, NegativeZ`) match FNA's `CubeMapFace.cs` exactly.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
Consumed correctly by `TextureCube`'s per-face `SetData`/`GetData` overloads (audited separately).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Exact match to FNA reference.

## Final Assessment
No findings.
