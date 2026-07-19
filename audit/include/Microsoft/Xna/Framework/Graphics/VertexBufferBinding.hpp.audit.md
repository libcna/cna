# Audit: include/Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp` (36 lines)
- Audit status: AUDITED (full read)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Vertices/VertexBufferBinding.cs`
- Main related tests: not independently located in this pass

## Purpose
Binds a vertex buffer with a vertex offset and instance frequency for `GraphicsDevice::SetVertexBuffers`-
style rendering.

## Executive Verdict
A genuine, disclosed unit-semantics divergence from FNA worth flagging precisely: FNA's real
`VertexOffset` is documented as "the offset **in bytes** from the beginning of the vertex buffer"
(confirmed in `VertexBufferBinding.cs` line 39's XML doc comment), while this port's
`vertexOffset`/`getVertexOffsetProperty()` is documented as "Offset in **vertices** from the start
of the buffer" (this header's own line 17/26). This is a real semantic change, not merely a naming
difference — a caller porting FNA code that computes a byte offset (e.g. `someVertexCount *
vertexStride`) and passes it here would get a value `vertexStride`-times too large.

## Checklist Results
- Constructor overload set (buffer-only, buffer+offset, buffer+offset+frequency) matches FNA's
  three public constructors structurally.
- No implicit `VertexBuffer* -> VertexBufferBinding` conversion is present (FNA's real
  `VertexBufferBinding` has an `implicit operator VertexBufferBinding(VertexBuffer)`, confirmed in
  `VertexBufferBinding.cs` lines 108-114) — a real, missing piece of API-surface convenience,
  though a caller can always construct explicitly instead. Not independently confirmed whether this
  gap is load-bearing anywhere in this codebase's own call sites (out of scope for this file's own
  audit).

## Detailed Findings

### MEDIUM — `VertexOffset` is documented and presumably consumed as a vertex-count offset, not
FNA's real byte offset
This is a genuine unit-semantics divergence, not just documentation wording: FNA's real
`VertexOffset` is in bytes (independent of any particular vertex struct's stride), matching how
`FNA3D`'s underlying `SetVertexData` offset parameters work. This port's own doc comment explicitly
states vertices, not bytes. Whether this is an intentional, tracked adaptation (perhaps because this
port's backend `SetVertexBuffers`-equivalent call consumes a vertex-count offset internally rather
than a byte offset) could not be confirmed without reading `GraphicsDevice::SetVertexBuffers`'s own
implementation (out of this file's scope) — flagged here as the specific place the divergence is
declared, for cross-checking against the actual backend consumer when `GraphicsDevice.cpp` is
audited.

## Cross-File Observations
This is a second, independent example (alongside `VertexBuffer`/`IndexBuffer`'s missing
destination-byte-offset overloads) of an offset-related unit/concept simplification in this shard's
buffer-related API surface — worth checking as a possible shared theme when `GraphicsDevice.cpp`
(the actual consumer of `VertexBufferBinding`) is audited.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The instance-frequency semantics (0 = no instancing) and the three-constructor overload shape both
correctly match FNA.

## Final Assessment
One MEDIUM finding: `VertexOffset`'s unit (vertices vs. FNA's real bytes) is a genuine, undisclosed-
as-such semantic divergence, worth cross-checking against `GraphicsDevice`'s actual consumption of
this field.
