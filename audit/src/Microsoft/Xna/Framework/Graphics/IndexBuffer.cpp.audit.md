# Audit: src/Microsoft/Xna/Framework/Graphics/IndexBuffer.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/IndexBuffer.cpp` (141 lines)
- Audit status: AUDITED (full read)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Vertices/IndexBuffer.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements `IndexBuffer`'s constructors, `SetData`/`GetData` for both 16-bit and 32-bit indices,
and the protected `SetDataWithOptions` pair.

## Executive Verdict
Correct. `GetData()` bounds-checks against `cpuShadow_.size()` via
`System::ArgumentOutOfRangeException` (unconditional, real validation); `BufferUsage::WriteOnly`
correctly rejected via `System::NotSupportedException` with FNA-matching message text. Shares the
same LOW-severity `SetData()`-has-zero-validation observation already documented for
`VertexBuffer.cpp` — same root cause (FNA's own equivalent `ErrorCheck` is `[Conditional("DEBUG")]`,
so CNA's total absence of checks matches FNA's dominant Release-mode behavior rather than
regressing from it), so not repeated in full detail here.

## Checklist Results
- `SetData(data, count)`: no validation, matching `VertexBuffer::SetData`'s identical gap and the
  same FNA-Release-parity rationale.
- `GetData(data, startIndex, elementCount)`: correctly validates `byteOffset + byteCount >
  cpuShadow_.size()` before the `memcpy`, throwing `ArgumentOutOfRangeException` — real, always-on
  protection.
- `SetDataWithOptions` (both 16-bit and 32-bit): passes `data + startIndex` and `elementCount`
  straight to `backend_->SetData16WithOptions`/`SetData32WithOptions` — no destination offset
  argument exists to forward, confirming the header's HIGH finding in the implementation.

## Detailed Findings
See `include/.../IndexBuffer.hpp.audit.md` for the HIGH finding (no destination-offset concept). A
LOW finding (zero unconditional validation in `SetData`, matching FNA's Release-mode behavior) is
recorded in full against `VertexBuffer.cpp.audit.md`'s equivalent entry and applies identically
here.

## Cross-File Observations
None beyond what's already recorded against the paired header.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
`GetData`'s exception messages and validation logic are a faithful, correctly-implemented match for
FNA's real `IndexBuffer.GetData<T>`.

## Final Assessment
No new findings beyond those already recorded against the paired header.
