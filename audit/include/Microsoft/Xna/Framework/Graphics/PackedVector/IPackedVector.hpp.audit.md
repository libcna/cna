# Audit: include/Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp`
- Audit status: AUDITED (full read, 65 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/PackedVector/IPackedVector.cs`
- Main related tests: not independently located in this pass

## Purpose
Non-generic and generic packed-vector interfaces underlying every concrete `PackedVector` type in
this directory.

## Executive Verdict
Correct, and a well-explained C++ structural adaptation. C# has both a non-generic
`IPackedVector` (with `PackFromVector4`/`ToVector4`) and a generic `IPackedVector<TPacked>`
(adding a `PackedValue` property) — the file's own top-of-file comment explains that since C++ has
no generics, this is split into `IPackedVector` (abstract base, `PackFromVector4` only) and a
template `IPackedVectorT<T>` (adding typed `getPackedValueProperty()`/`setPackedValueProperty()`),
with `IPackedVectorT<T>` inheriting `IPackedVector` so a concrete type is still polymorphically an
`IPackedVector*`. This preserves the real structural relationship (every `IPackedVector<T>` is
also an `IPackedVector`) despite the language's lack of generics.

## Checklist Results
- `NOXNA` correctly applied to the virtual destructor (no C# equivalent finalizer concept applies
  here the same way).
- Every concrete `PackedVector` type in this shard correctly derives from `IPackedVectorT<T>` for
  its specific packed-storage type.

## Detailed Findings
None. Note: `ToVector4()` is NOT declared here as a pure virtual (unlike FNA's real `IPackedVector.ToVector4()`)
— each concrete type instead provides its own non-virtual `ToVector4()` with a different return
mechanism (by value, not through a shared virtual dispatch path). This is a reasonable design
choice given every consumer in this codebase's audited call sites uses concrete types directly
rather than polymorphically through `IPackedVector*` for reading — but it does mean
`ToVector4()` is not available through a base `IPackedVector*`/`IPackedVectorT<T>*` pointer,
unlike `PackFromVector4()`. Not flagged as a defect (no broken caller found), but worth noting as
an asymmetry versus the fully-virtual FNA interface.

## Cross-File Observations
Every one of the 19 concrete `PackedVector` types audited in this pass correctly implements this
interface pair.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The split-interface design is clearly explained in the file's own comment, converting what could
be a confusing C#-to-C++ structural gap into a documented, deliberate adaptation.

## Final Assessment
No findings.
