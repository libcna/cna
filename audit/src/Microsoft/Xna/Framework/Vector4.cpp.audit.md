# Audit: src/Microsoft/Xna/Framework/Vector4.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Vector4.cpp`
- Audit status: AUDITED (full read, 517 lines)
- Subsystem: `xna-framework-core` shard
- File type: C++ implementation
- XNA/FNA relevance: matches real XNA `Vector4`'s exact math formulas
- Main related tests: not independently located in this pass

## Purpose
Implements every `Vector4` method, including the 3-overload `Transform(Vector2/Vector3/Vector4, Matrix)`
family.

## Executive Verdict
Needs attention -- confirms the cross-cutting `GetHashCode()` finding already flagged for this file
(`AUDIT_CROSS_CUTTING_FINDINGS.md`, discovered via grep during `Vector3.cpp`'s audit, now confirmed via
full read of this file). Every other formula (Barycentric/CatmullRom/Hermite/SmoothStep scalar helpers,
identical to `Vector2`/`Vector3`'s own already-verified versions; the 3-overload `Transform(_, Matrix)`
family) is correct.

## Checklist Results

### MEDIUM (already tracked cross-cuttingly): `GetHashCode()` signed-overflow UB
```cpp
int Vector4::GetHashCode() const { return FloatHash(W) + FloatHash(X) + FloatHash(Y) + FloatHash(Z); }
```
Same unfixed pattern as `Vector3::GetHashCode()` (plain signed `int` addition of 4 arbitrary-range
`FloatHash()` values, vs. `Vector2::GetHashCode()`'s explicit unsigned-wraparound fix for exactly this UB
class, cited INPUT-BUILD-006). See `AUDIT_CROSS_CUTTING_FINDINGS.md` for the full cross-file writeup;
recorded here as this file's own confirmed instance.

### `Transform(Vector2/Vector3/Vector4, Matrix)`: independently verified correct
Each overload's result computation correctly reflects its input's implicit higher-dimensional components
(`Vector2` input: Z implicitly 0 -- no `M31`/`M32`/`M33`/`M34` terms -- and W implicitly 1 -- unconditional
`+matrix.M41/M42/M43/M44` terms; `Vector3` input: W implicitly 1, all rotation-block terms present; `Vector4`
input: full 4x4 multiply) -- all three correctly match row-vector*matrix convention.

### Scalar interpolation helpers: identical to (already-verified) `Vector2`/`Vector3` versions
`BarycentricScalar`/`CatmullRomScalar`/`HermiteScalar`/`SmoothStepScalar`/`ClampScalar`/`LerpScalar` are
byte-for-byte identical to the same helpers in `Vector2.cpp`/`Vector3.cpp` (already independently verified
against standard XNA formulas in those files' own audits) -- correct by the same verification.

## Detailed Findings

1. **[MEDIUM] `GetHashCode()`'s signed-integer-overflow UB** (already tracked in
   `AUDIT_CROSS_CUTTING_FINDINGS.md`). Line 120.

## Cross-File Observations
Fourth confirmed instance (after `Vector3`) of the same unpropagated `Vector2::GetHashCode()` fix; see the
cross-cutting entry for the full list (`Vector3`/`Vector4`/`Quaternion`/`Matrix` all affected;
`Point`/`Rectangle` unaffected via XOR-combining; `Plane` transitively inherits `Vector3`'s bug).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
`Transform`'s 3-overload family correctly handles each input dimensionality's implicit components; scalar
interpolation helpers correct (shared verification with `Vector2`/`Vector3`).

## Final Assessment
One MEDIUM-severity finding (already tracked cross-cuttingly): `GetHashCode()`'s signed-overflow UB.
