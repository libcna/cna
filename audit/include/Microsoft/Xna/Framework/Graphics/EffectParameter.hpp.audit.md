# Audit: include/Microsoft/Xna/Framework/Graphics/EffectParameter.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/EffectParameter.hpp`
- Audit status: AUDITED (full read, 464 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Effect/EffectParameter.cs`
- Main related tests: not independently located in this pass

## Purpose
Represents a parameter (uniform variable) declared in a custom effect shader: name, semantic,
type/class metadata, `Elements`/`StructureMembers` sub-collections, and the full `GetValueXxx`/
`SetValue` overload set for every XNA data type (bool, int, float, string, Vector2/3/4, Quaternion,
Matrix, Texture2D/3D/Cube), each with a scalar and array variant.

## Executive Verdict
The public API surface itself is complete and well-documented (every real XNA `EffectParameter`
member is present with matching signatures, adapted correctly for C++ — raw pointers for textures,
`std::vector` for arrays, `std::string` for string/semantic). However, two of this header's own
doc comments describe behavior the paired `.cpp` implementation does not actually provide — see the
paired `.cpp` audit report for the full analysis (an `EffectParameter::Elements`/`StructureMembers`
population gap, and a Matrix Get/Set/Transpose semantic inversion relative to FNA). This header
itself is internally consistent and well-formed; the defects are implementation-only.

## Checklist Results
- Doxygen coverage: complete, every public member documented.
- `SetValue(const Matrix& value)`'s doc comment claims "(column-major packing)" and
  `GetValueMatrix()`'s claims "(column-major unpacking)" — see the `.cpp` report: the actual
  implementation does neither; it performs a straight, untransposed row-major copy. The doc
  comment describes the intended/FNA-correct behavior, not the implemented one.
- Storage design: `elements_`/`members_` are `std::unique_ptr<EffectParameterCollection>` (not
  by-value), a sound choice avoiding self-referential-collection copy issues — but see the `.cpp`
  report for why these are never actually populated.

## Detailed Findings
See `src/Microsoft/Xna/Framework/Graphics/EffectParameter.cpp.audit.md` for the two confirmed HIGH
findings (Matrix semantics inverted vs. FNA; `Elements`/`StructureMembers` permanently empty) —
both are implementation-only, not visible from this header's declarations alone, though this
header's own doc comments for the Matrix methods are shown to be aspirational/inaccurate as a
direct consequence.

## Cross-File Observations
`EffectAnnotation.hpp`'s parallel `GetValueMatrix()` doc comment makes no "column-major" claim at
all (just "Gets the value of this annotation as a Matrix") — and its implementation is the one that
actually matches FNA's real formula. Worth noting: this header's more specific, "column-major"-
naming doc comments for `EffectParameter`'s Matrix methods are the ones that turn out to be wrong
about the shipped implementation.

## Missing or Weak Tests
Not independently located in this pass; a test comparing `SetValue(Matrix)`+`GetValueMatrix()`
round-trip behavior against FNA's real byte-level convention (not just internal CNA round-tripping)
would have caught the Matrix-semantics finding, and a test setting an array- or struct-typed
parameter's `Elements`/`StructureMembers` would have caught the empty-collection finding.

## Positive Findings
Complete overload surface, correct C++ adaptation of C#'s texture-type-specific getters (separate
`Texture2D*`/`Texture3D*`/`TextureCube*` getters/setters, matching FNA's own separate accessors
rather than collapsing to a single base `Texture*` — though see the `.cpp` report for a related
architectural note on this).

## Final Assessment
Two HIGH findings, both implementation-side — see the paired `.cpp` report for the full analysis.
