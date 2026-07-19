# Audit: include/Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp` (20 lines)
- Audit status: AUDITED (full read)
- Subsystem: `xna-graphics` shard
- File type: C++ header (enum only)
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/PrimitiveType.cs`
- Main related tests: not independently located in this pass

## Purpose
Enumerates how vertex data is assembled into geometric primitives for a draw call.

## Executive Verdict
Correct. `TriangleList=0`/`TriangleStrip=1`/`LineList=2`/`LineStrip=3` confirmed to match FNA's real
`PrimitiveType.cs` exactly (including doc-comment content for `TriangleList`/`TriangleStrip`,
directly diffed). `PointListEXT=4` is correctly `EXT`-suffixed and represents a real, disclosed
addition beyond real XNA 4.0's enum: XNA 4.0 removed `PointList`/`TriangleFan` (present in earlier
XNA versions) from this enum entirely — this port correctly does not silently reintroduce
`PointList` as if it were a real XNA 4.0 member, instead clearly marking it as a CNA extension via
the `EXT` suffix convention.

## Checklist Results
Every value has a Doxygen `/** @brief */` block, each accurately describing the primitive assembly
rule (including per-primitive vertex-count/winding-order semantics for `TriangleList`/
`TriangleStrip`, matching FNA's own wording closely).

## Detailed Findings
None.

## Cross-File Observations
This project's own persistent memory records a prior, real defect class in this exact area
(`feedback_drawprimitives_primitivecount_not_vertexcount` — test files passing a vertex count where
a primitive count was expected). This enum's own documentation is unambiguous about vertex-grouping
semantics per primitive type, which is a necessary (though not sufficient on its own) precondition
for callers to compute a correct primitive count — worth keeping in mind when `GraphicsDevice`'s own
`DrawPrimitives`/`DrawIndexedPrimitives` methods (which actually take a `primitiveCount` parameter)
are audited.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Correctly and clearly marks its one non-XNA-4.0 addition (`PointListEXT`) rather than silently
reintroducing a pre-4.0 XNA enum member as if it were standard.

## Final Assessment
No findings.
