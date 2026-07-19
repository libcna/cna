# Audit: include/Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp`
- Audit status: AUDITED (full read, 36 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Vertices/VertexElementUsage.cs`
- Main related tests: not independently located in this pass

## Purpose
Enum defining the semantic usage (Position, Color, Normal, etc.) of a vertex element.

## Executive Verdict
Correct. All 13 real FNA/XNA enum values present with identical implicit ordinals (`Position=0`
through `TessellateFactor=12`).

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
Every concrete `VertexPosition*` type audited in this pass uses these usage values consistently
with their documented semantic meaning (`Position`, `Color`, `Normal`, `Tangent`,
`TextureCoordinate`, `BlendWeight`, `BlendIndices`).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Exact enum-value parity with FNA.

## Final Assessment
No findings.
