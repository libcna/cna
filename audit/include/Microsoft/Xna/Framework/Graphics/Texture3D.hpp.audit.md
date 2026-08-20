# Audit: include/Microsoft/Xna/Framework/Graphics/Texture3D.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/Texture3D.hpp`
- Audit status: AUDITED (full read, 177 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Texture3D.cs`
- Main related tests: not independently located in this pass

## Purpose
Represents a 3D (volume) texture: `SetData`/`GetData` over a sub-box (`left/top/right/bottom/
front/back`) at a given mip level, plus the entire-texture convenience overloads.

## Executive Verdict
Correct. `GetTypeName()` correctly returns the fully-qualified `.NET` name (confirmed in the paired
`.cpp`, unlike the sibling `Texture2D`'s isolated regression). Move-only semantics are explicitly and
correctly declared (copy deleted, move defaulted), with a well-documented rationale for why a move
path was newly added (`plans/plan_xnb.md` XNB-25's `Texture3DReader` needed to return this type by value).

## Checklist Results
- Doxygen coverage: complete.
- `NOXNA` tagging: correctly applied to the explicit copy/move declarations, `SetDataPointerEXT`,
  and `GetBackend`.
- Move-only correctness: a user-declared destructor suppresses the implicit move constructor in
  C++, and the pre-existing `std::unique_ptr` member independently blocks the implicit copy
  constructor — the header's own comment correctly explains both facts as the reason explicit
  declarations were required here.

## Detailed Findings
None.

## Cross-File Observations
Shares the `.cpp`-level recurring exception-type finding (raw `std::` instead of `System::`) — see
the paired `.cpp` report.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Correct fully-qualified `GetTypeName()`, correct move-only ownership semantics, well-documented
rationale for the move path's existence.

## Final Assessment
No findings in this header; see the paired `.cpp` report for the recurring exception-type note.
