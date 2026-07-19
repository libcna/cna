# Audit: include/Microsoft/Xna/Framework/Graphics/BufferUsage.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/BufferUsage.hpp` (14 lines)
- Audit status: AUDITED (full read)
- Subsystem: `xna-graphics` shard
- File type: C++ header (enum only)
- XNA/FNA relevance: Direct XNA type; real XNA 4.0 `BufferUsage` enum (values not independently
  re-derived from a standalone FNA `.cs` file — no dedicated `BufferUsage.cs` file exists in the
  local FNA reference tree, this enum lives inline in FNA's `GraphicsResource.cs`/other files there)
- Main related tests: not independently located in this pass

## Purpose
Usage hint for optimizing memory placement of vertex/index buffers: `None`, `WriteOnly`.

## Executive Verdict
Correct — matches the well-known, documented real XNA 4.0 `BufferUsage` enum shape (`None=0`,
`WriteOnly=1`; XNA 4.0 notably removed the earlier `Points`/`InstanceData`-adjacent values some
older XNA versions had, and this port correctly does not carry those forward).

## Checklist Results
Both enum values have Doxygen `/** @brief */` blocks.

## Detailed Findings
None.

## Cross-File Observations
Consumed correctly and consistently by `VertexBuffer`/`IndexBuffer`'s `WriteOnly`-gated
`GetData()` rejection (`System::NotSupportedException`), audited separately in this batch.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Minimal, correct.

## Final Assessment
No findings.
