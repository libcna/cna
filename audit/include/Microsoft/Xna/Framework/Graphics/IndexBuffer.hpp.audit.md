# Audit: include/Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp` (207 lines)
- Audit status: AUDITED (full read)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Vertices/IndexBuffer.cs`
- Main related tests: not independently located in this pass

## Purpose
GPU index buffer storing 16-bit or 32-bit indices for indexed draw calls, with typed `SetData`/
`GetData` overloads for both element sizes.

## Executive Verdict
Structurally correct and closely mirrors `VertexBuffer`'s own design (including its move-only
semantics and `NOXNA`-tagged internal accessors). Shares the same HIGH-severity finding as
`VertexBuffer`: no destination-offset concept anywhere in its `SetData`/`SetDataWithOptions`
surface, confirming the destination-offset gap is a shared, consistent design choice across both
buffer types rather than isolated to one.

## Checklist Results
- Doxygen coverage: complete.
- `NOXNA` correctly tags the internal accessors and the two constructors with no direct FNA
  equivalent.
- `GetTypeName()` declared `override` — confirmed implemented in the `.cpp`.

## Detailed Findings

### HIGH — No destination-offset concept in `SetData`/`SetDataWithOptions`, matching the identical
gap already found in `VertexBuffer.hpp`
`SetDataWithOptions(const std::uint16_t* data, int startIndex, int elementCount, SetDataOptions
options)` (protected, lines 163-164) and its 32-bit sibling have no destination-offset parameter —
only a source `startIndex` into the caller's array. Real FNA's `IndexBuffer.SetData<T>(int
offsetInBytes, T[] data, int startIndex, int elementCount)` (confirmed in `IndexBuffer.cs` lines
242-260) takes a genuine destination `offsetInBytes`; this port has dropped that overload from the
public surface entirely, for the same reasons and with the same practical consequence already
documented in `VertexBuffer.hpp.audit.md` (a real XNA dynamic-buffer streaming pattern becomes
unimplementable at the public API level, not merely suboptimal at the backend layer).

## Cross-File Observations
This is the second of two sibling buffer types (alongside `VertexBuffer`) sharing the identical
destination-offset omission — confirms this is a consistent, shard-wide design choice in this
port's buffer classes, not an isolated oversight in one type. See the shard-wide cross-cutting note
this pass will add for the combined significance.

## Missing or Weak Tests
Not independently located in this pass; same gap as `VertexBuffer` means no test could currently
exercise a non-zero destination offset for dynamic index-buffer streaming.

## Positive Findings
Clean, consistent design mirroring `VertexBuffer`'s own established pattern (`GetBackend()`/
`HasBackend()` NOXNA accessors, move-only semantics, protected dynamic-flag constructor).

## Final Assessment
One HIGH finding, identical in nature to `VertexBuffer.hpp`'s own: the shared destination-offset gap
across both concrete buffer types in this port.
