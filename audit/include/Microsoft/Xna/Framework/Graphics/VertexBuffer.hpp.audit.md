# Audit: include/Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp` (333 lines)
- Audit status: AUDITED (full read)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Vertices/VertexBuffer.cs`
- Main related tests: not independently located in this pass

## Purpose
GPU vertex buffer for storing vertex data, with typed `SetData`/`GetData` overloads for every
concrete CNA vertex-struct type plus a raw byte-stride overload (`SetDataRaw`, NOXNA).

## Executive Verdict
Mostly correct, well-documented C#-to-C++ structural adaptation (typed overloads standing in for
FNA's generic `T : struct` constraint, which C++ has no direct equivalent for). One HIGH finding:
this class's entire public `SetData`/`GetData`/`SetDataWithOptions` surface has **no concept of a
destination byte offset into the GPU buffer at all** — every overload only has a *source*
`startIndex` into the caller's `data` array. Real FNA's equivalent class exposes a real
`offsetInBytes` **destination** parameter as its most general overload
(`SetData<T>(int offsetInBytes, T[] data, int startIndex, int elementCount, int vertexStride)`,
confirmed directly in `VertexBuffer.cs` lines 245-267) — an actual, documented public XNA method
this port is missing entirely, not just an internal implementation gap.

## Checklist Results
- Doxygen coverage: complete on every public member.
- `NOXNA` correctly tags `SetDataRaw`, `GetBackend()`, `HasBackend()`, and the two constructors/
  destructor whose signatures/existence have no direct FNA counterpart.
- `GetTypeName()` is declared `override` — confirmed implemented in the `.cpp` via `GetTypeNameCPP`.
- Move-only semantics (`= delete` copy, defaulted move) are a reasonable, disclosed C++ ownership
  choice for a GPU-handle-holding resource type; FNA's C# class has no equivalent concept (GC-managed
  reference type) so this isn't a "missing" feature, just a necessary C++ substitution.

## Detailed Findings

### HIGH — No destination-offset concept anywhere in this class's public API; this is the actual
root cause of the already-documented backend-level `SetDataOptions::NoOverwrite` gap, not merely a
downstream symptom of it
This project's own `audit/AUDIT_CROSS_CUTTING_FINDINGS.md` already documents (under "Architecture")
that `IVertexBufferBackend::SetDataWithOptions()` has no destination-offset parameter, so every
backend's `NoOverwrite` path always overwrites byte range `[0, byteCount)`. Reading this class
directly confirms the gap doesn't originate at the backend-interface layer at all — it originates
here, at the very top of the stack: `VertexBuffer::SetDataWithOptions(const T*, int startIndex, int
elementCount, SetDataOptions options)` (protected, lines 288-298) has no `offsetInBytes`-style
parameter for where in the *destination* GPU buffer to write; `startIndex` addresses only the
*source* CPU array. The `.cpp` confirms this is passed straight through:
`backend_->SetDataWithOptions(packed.data(), elementCount, sizeof(GpuVertex), options)` — no
destination offset argument exists to forward even if the backend interface grew one tomorrow.

Real FNA's own `VertexBuffer.SetData<T>(int offsetInBytes, T[] data, int startIndex, int
elementCount, int vertexStride)` (the most general overload every other overload funnels through)
takes a genuine destination `offsetInBytes`, forwarded straight to `FNA3D_SetVertexBufferData`.
This port has dropped that parameter — and, by extension, that overload — from the public surface
entirely.

**Practical consequence**: a real, common XNA streaming-buffer technique — a growing ring buffer of
per-frame dynamic vertex data, using `SetDataOptions::NoOverwrite` across multiple `Draw` calls in
one frame while incrementing a destination offset each time to avoid overwriting data a pending
draw still reads — is not just suboptimal on this port (as the backend-level finding already
noted), it is **architecturally impossible to express at the public API level at all**. There is no
call a game could make to say "write these vertices starting at byte 4096 of this buffer."

`DynamicVertexBuffer`'s own doc comments (audited separately) honestly disclose the consequence
("all writes go to the buffer beginning") — this is a known, self-disclosed limitation, not a
silently-hidden one, which somewhat mitigates its severity as a *documentation* concern, but the
functional gap itself (a real, XNA-documented public method entirely absent, and the streaming
pattern it exists for being unimplementable) is real and worth escalating this project's existing
cross-cutting note from "backend interface limitation" to "top-to-bottom architectural gap
originating in the public XNA-facing class design."

## Cross-File Observations
See `src/Microsoft/Xna/Framework/Graphics/VertexBuffer.cpp.audit.md` for confirmation of how
`SetData`/`SetDataWithOptions` are actually implemented, and for a related, lower-severity
bounds-checking observation. See `DynamicVertexBuffer.hpp.audit.md` for the concrete public-facing
symptom of this same gap.

## Missing or Weak Tests
Not independently located in this pass; given the finding above, no test could currently exercise a
non-zero destination offset for dynamic vertex-buffer streaming, since the API to request one does
not exist.

## Positive Findings
The typed-overload strategy (one `SetData`/`GetData` pair per concrete vertex-struct type, plus a
raw byte-stride escape hatch) is a reasonable, well-documented substitute for C#'s generic
`T : struct` constraint, which has no direct C++ equivalent without templates that would leak GPU
packing details into every call site.

## Final Assessment
One HIGH finding: the class's public API has no destination-offset concept, which is the confirmed
root cause of an already-documented cross-cutting backend gap and makes a real XNA streaming
pattern architecturally impossible, not merely under-optimized.
