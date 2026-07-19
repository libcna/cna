# Audit: src/Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTextureSkinned.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTextureSkinned.cpp`
- Audit status: AUDITED (full read, 33 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: NOXNA extension, no FNA equivalent
- Main related tests: not independently located in this pass

## Purpose
Implements the static `VertexDeclaration` and `ToString()`.

## Executive Verdict
Correct. Element offsets (0, 12, 24, 40, 48, 64) match the struct's own cumulative field sizes
exactly, ending in a 68-byte stride (12+12+16+8+16+4).

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
None beyond the paired `.hpp` report.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Correct, self-consistent offset arithmetic; `ToString()` correctly formats the fixed-size
`BlendIndices` array element-by-element.

## Final Assessment
No findings.
