# Audit: include/Microsoft/Xna/Framework/Graphics/VertexElement.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/VertexElement.hpp`
- Audit status: AUDITED (full read, 148 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Vertices/VertexElement.cs`
- Main related tests: not independently located in this pass

## Purpose
Describes a single attribute slot (offset, format, usage, usage index) inside a vertex stride.

## Executive Verdict
Correct. Every field, the constructor, `operator==`/`operator!=`/`Equals`, and `ToString()`'s
format match FNA's real `VertexElement` exactly. `GetHashCode()` returning a constant `0` is
correctly documented as consistent with FNA's own explicit `// TODO: Fix hashes` stub — a real,
honestly-preserved FNA quirk, not a CNA gap.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
The equality comparison order (`offset_, usageIndex_, vertexElementUsage_, vertexElementFormat_`)
matches FNA's `operator==` field-comparison order exactly (`Offset, UsageIndex,
VertexElementUsage, VertexElementFormat`).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
`GetHashCode()`'s doc comment correctly cites the exact reason for its constant value (FNA's own
unfixed TODO) rather than presenting it as an unexplained oddity.

## Final Assessment
No findings.
