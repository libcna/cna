# Audit: src/Microsoft/Xna/Framework/Graphics/EffectParameter.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/EffectParameter.cpp`
- Audit status: AUDITED (full read, 219 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Effect/EffectParameter.cs`
  (diffed byte-for-byte against every `GetValue*`/`SetValue*` overload)
- Main related tests: not independently located in this pass

## Purpose
Implements every `EffectParameter` getter/setter, backed by `std::vector<float>`/`std::vector<int>`/
`std::string`/raw texture pointers rather than FNA's raw `IntPtr` into a MojoShader-parsed effect
blob.

## Executive Verdict
Two confirmed HIGH-severity findings. All non-Matrix, non-array-structural behavior is correct and
faithfully adapted. The Matrix finding is unusually well-evidenced: this exact codebase's own
sibling type, `EffectAnnotation::GetValueMatrix()` (audited in the same pass), implements the
*identical* FNA formula correctly, proving the correct convention was known and applied elsewhere —
making this file's inversion look like a straightforward transcription slip rather than a
deliberate design choice.

## Checklist Results
- `GetValueBoolean`/`GetValueInt32`/`GetValueSingle`/`GetValueString` and their non-Matrix,
  non-Vector array counterparts: correct, faithfully mirror FNA's semantics (first-element read for
  scalars, direct positional array copy).
- `GetValueVector2/3/4`/`SetValue(Vector2/3/4)` (scalar and array): correct, straight
  component-order copy, matching FNA exactly (Vector types have no row/column transpose ambiguity).
- `GetValueQuaternion`/`SetValue(Quaternion)`: correct, matches FNA's X,Y,Z,W straight copy.
- `GetValueTexture2D/3D/Cube`/`SetValue(Texture*/Texture2D*/Texture3D*/TextureCube*)`: correct,
  reasonable adaptation of FNA's single `Texture texture` field into four separately-typed slots
  (documented in the header as an intentional, scoped-out unification, `plans/plan_graphics.md` Task 863).

## Detailed Findings

### HIGH — Every Matrix-related Get/Set method has its "plain" and "Transpose" semantics inverted
relative to FNA's real, documented behavior
FNA's real convention (verified directly from `EffectParameter.cs`):
- `GetValueMatrix()` reads the raw float buffer with an implicit transpose (`M11=resPtr[0],
  M12=resPtr[4], M13=resPtr[8], M14=resPtr[12], M21=resPtr[1], ...`) — i.e. it assumes the
  underlying bytes are stored **column-major** (HLSL's default constant-buffer layout for a
  `float4x4` declared without the `row_major` pragma) and converts them into XNA's row-major
  `Matrix` struct.
- `GetValueMatrixTranspose()` reads the buffer straight, in declaration order (`M11=resPtr[0],
  M12=resPtr[1], M13=resPtr[2], ...`) — the direct, untransposed read, intended for effects whose
  `.fx` source explicitly declares `row_major float4x4`.
- `SetValue(Matrix value)` writes the mirror-image transpose (column 1 first: `dstPtr[0]=M11,
  dstPtr[1]=M21, dstPtr[2]=M31, dstPtr[3]=M41, dstPtr[4]=M12, ...`), and `SetValueTranspose(Matrix
  value)` writes the straight, declaration-order layout.

This convention is *the* single most well-known XNA `EffectParameter` gotcha (real-world custom-
effect authors must choose `SetValue` vs. `SetValueTranspose` based on whether their `.fx` file's
matrix is declared `row_major`), so getting the two swapped is a real, high-impact behavioral
divergence for anyone porting existing XNA game code that calls one or the other deliberately.

This file's actual implementation:
```cpp
Matrix EffectParameter::GetValueMatrix() const
{
    ...
    return Matrix(floatData_[0], floatData_[1], floatData_[2], floatData_[3],
                  floatData_[4], floatData_[5], floatData_[6], floatData_[7],
                  floatData_[8], floatData_[9], floatData_[10], floatData_[11],
                  floatData_[12], floatData_[13], floatData_[14], floatData_[15]);
}
Matrix EffectParameter::GetValueMatrixTranspose() const { return Matrix::Transpose(GetValueMatrix()); }
```
`GetValueMatrix()` here does the **straight, declaration-order read** — exactly what FNA's real
`GetValueMatrixTranspose()` does, not what FNA's real `GetValueMatrix()` does. Consequently
`GetValueMatrixTranspose()` here (`Transpose` of the straight read) computes what FNA's real
`GetValueMatrix()` computes. The two methods' *names* are correct relative to each other structurally
(one is genuinely the transpose of the other), but the mapping of *which name does which
FNA-documented thing* is swapped. The identical inversion applies to `GetValueMatrixArray`/
`GetValueMatrixTransposeArray` and to `SetValue(Matrix)`/`SetValue(vector<Matrix>)` vs.
`SetValueTranspose(Matrix)`/`SetValueTranspose(vector<Matrix>)` (8 methods total affected).

**Cross-validation, not just derivation**: `EffectAnnotation::GetValueMatrix()` (same shard, audited
in this same pass, `EffectAnnotation.cpp` lines 65-74) implements the byte-for-byte identical FNA
formula (`data_[0], data_[4], data_[8], data_[12], data_[1], data_[5], ...`) **correctly** — proving
this codebase's author applied the right convention for that sibling type. `EffectAnnotation` has no
`SetValue`/`Transpose` variant to compare (annotations are read-only, matching FNA), but the one
directly-comparable method it does have is right where `EffectParameter`'s is wrong.

**Caveat on downstream impact**: whether this produces visibly wrong rendering depends on how
`floatData_` is actually consumed once handed to a real compiled shader (in `Effect.cpp`/
`ShaderEffect.cpp`, both out of this batch's scope — flagged for cross-check when those files are
audited). If CNA's own shader-parameter-upload path applies a compensating transpose consistently,
the net visible effect could be masked for parameters this codebase's own shader system populates
end-to-end. It cannot be masked, however, for the documented, real-world use case this API exists
for: a user porting existing XNA game code that calls `effect.Parameters["Foo"].SetValueTranspose(m)`
(or `SetValue(m)`) expecting FNA/XNA's real, documented semantics for that specific method name.

**Failure scenario**: a ported XNA game with a custom `.fx`-equivalent shader whose matrix constant
is declared in the "default" (non-`row_major`) convention calls `SetValue(worldViewProjection)` — in
real XNA/FNA this correctly transposes into column-major GPU storage; in CNA today it writes the
matrix straight/untransposed, silently transformed geometry incorrectly (assuming the underlying
custom shader compilation does not separately compensate).

### HIGH — `Elements`/`StructureMembers` are permanently empty collections; array- and struct-typed
effect parameters silently report zero sub-elements
`elements_`/`members_` are constructed as empty `EffectParameterCollection` instances in the
constructor (lines 18-19) and **never populated anywhere** — confirmed via a repo-wide grep for
any call site adding to them (`elements_->Add`/`members_->Add`, `BuildElementList`/
`BuildMemberList`, or any external construction path): zero matches outside their own declaration
and the trivial `getElementsProperty()`/`getStructureMembersProperty()` accessors that just return
the always-empty collections. FNA's real `EffectParameter.Elements`/`.StructureMembers` are lazily
built (`BuildElementList`/`BuildMemberList`) from the parsed effect's actual array-element-count/
struct-member metadata the first time either property is read — a real, commonly-used feature for
array-typed effect parameters (e.g. `float3 LightPositions[4];`) and struct-typed ones.

**Failure scenario**: any custom effect with an array- or struct-typed parameter, ported from real
XNA and relying on `effect.Parameters["LightPositions"].Elements[i].SetValue(...)` (the standard
XNA idiom for setting individual array elements when a single flat `SetValue(vector<T>)` isn't
convenient, or when the array elements are themselves structs) silently gets an empty collection —
`Elements.Count == 0` — regardless of the shader's actual declared array size. Since `rowCount`/
`columnCount`/`paramClass`/`paramType` metadata *is* stored and exposed correctly, a caller can
detect "this is an array-class parameter" but has no way to reach its individual elements through
the documented API.

**Suggested fix** (report-only; no source changes made per this audit's scope): populate
`elements_`/`members_` at construction time (or lazily, mirroring FNA's `BuildElementList`/
`BuildMemberList` pattern) once whatever code parses a custom effect's parameter metadata (in
`Effect.cpp`, out of this batch) has the real element-count/struct-layout information available.

## Cross-File Observations
See both findings' own "Cross-File Observations"/"Cross-validation" notes above. Recommend this
finding be re-checked once `Effect.cpp`/`ShaderEffect.cpp` (queued for a different fork in this same
shard) are audited, specifically for: (a) whether the shader-parameter-fill path applies its own
compensating transpose that would mask the Matrix-semantics inversion for CNA-native (not ported)
effects, and (b) whether any effect-parsing code was intended to call into `BuildElementList`/
`BuildMemberList`-equivalent logic that never got wired up.

## Missing or Weak Tests
Not independently located in this pass. A `SetValueTranspose(Matrix)` + manual byte-level buffer
inspection test (or a cross-check against a real FNA-computed expected value) would have caught the
Matrix-semantics finding; a test setting an array-typed parameter's `Elements[i]` would have caught
the empty-collection finding.

## Positive Findings
Every non-Matrix, non-structural method (bool/int/float/string/Vector2/3/4/Quaternion/Texture,
scalar and array) is correct and faithfully adapted from FNA. The texture-type-specific accessor
split is a reasonable, disclosed architectural choice.

## Final Assessment
Two HIGH findings: (1) Matrix Get/Set/Transpose semantics inverted relative to FNA, cross-validated
against a correctly-implemented sibling method in `EffectAnnotation` in the same shard; (2)
`Elements`/`StructureMembers` are permanently empty, confirmed via repo-wide grep — array- and
struct-typed effect parameters silently lose access to their sub-elements.
