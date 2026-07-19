# Audit: src/Microsoft/Xna/Framework/Vector3.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Vector3.cpp`
- Audit status: AUDITED (full read, 502 lines)
- Subsystem: `xna-framework-core` shard
- File type: C++ implementation
- XNA/FNA relevance: matches real XNA `Vector3`'s exact math formulas
- Main related tests: not independently located in this pass

## Purpose
Implements every `Vector3` method, including `Cross`, and the 3D forms of `Transform`(Matrix/Quaternion)/
`TransformNormal`.

## Executive Verdict
Needs attention -- one confirmed MEDIUM-severity finding: `GetHashCode()` lacks the signed-overflow-UB fix
its sibling `Vector2::GetHashCode()` explicitly received. Every other formula (Cross product, matrix/
quaternion transform, `TransformNormal`) independently verified correct against known XNA formulas.

## Checklist Results

### MEDIUM: `GetHashCode()` missing the signed-overflow-UB fix `Vector2`'s own version already has
```cpp
int Vector3::GetHashCode() const { return FloatHash(X) + FloatHash(Y) + FloatHash(Z); }
```
Compare against `Vector2::GetHashCode()` (same shard, audited immediately before this file):
```cpp
int Vector2::GetHashCode() const
{
    // Unsigned wraparound avoids signed-overflow UB (UBSan, INPUT-BUILD-006); result unchanged.
    return static_cast<int>(static_cast<unsigned>(FloatHash(X)) + static_cast<unsigned>(FloatHash(Y)));
}
```
`FloatHash()` returns the raw bit pattern of a `float` reinterpreted as a signed `int` -- an essentially
arbitrary value across the full `int` range depending on the float's bits (including very large positive or
very negative values for typical game-world coordinates). Summing 3 such arbitrary `int` values with plain
signed `+` can genuinely overflow `int`'s range, which is undefined behavior in C++ (not merely
"wraps oddly" the way `Vector2`'s own fix explicitly guards against). This is a clear case of a documented,
specific fix (cited by task ID in `Vector2.cpp`) not being propagated to the structurally-identical sibling
file in the same header/cpp pattern -- `Vector3.cpp` was very likely written by copying `Vector2.cpp`'s
scaffolding (the file-local `FloatHash`/`ClampScalar`/`LerpScalar`/etc. helpers are verbatim-identical
between the two files) either before the fix landed, or without carrying it forward.

**Fix shape**: apply the identical unsigned-wraparound pattern:
```cpp
return static_cast<int>(static_cast<unsigned>(FloatHash(X)) + static_cast<unsigned>(FloatHash(Y)) +
                         static_cast<unsigned>(FloatHash(Z)));
```

### Cross product: independently verified correct
`Cross()`'s three component formulas (`x = v1.Y*v2.Z - v2.Y*v1.Z`, `y = -(v1.X*v2.Z - v2.X*v1.Z)`,
`z = v1.X*v2.Y - v2.X*v1.Y`) were independently expanded and confirmed algebraically identical to the
standard cross-product formula (`(a×b).x = a.y*b.z - a.z*b.y`, etc.) -- matches XNA's own literal source
expression shape (the explicit negation in the `y` term, rather than reordering the subtraction) as well.

### Matrix/quaternion transform and `TransformNormal`: independently verified correct
`Transform(Vector3, Matrix)` correctly applies the full 3x3 rotation block plus the M41-M43 translation row;
`TransformNormal` correctly omits the translation row (direction vectors are not translated) -- both
consistent with row-vector*matrix convention. `Transform(Vector3, Quaternion)`'s optimized rotation formula
(`x/y/z = 2*cross(q.xyz, v)`, then `result = v + w*(x,y,z) + cross(q.xyz, (x,y,z))`, expanded component-wise)
matches the standard optimized quaternion-vector-rotation shortcut used throughout XNA/FNA.

## Detailed Findings

1. **[MEDIUM] `GetHashCode()` uses plain signed-`int` addition, missing the signed-overflow-UB fix its
   sibling `Vector2::GetHashCode()` explicitly received (INPUT-BUILD-006)**. Line 117.

## Cross-File Observations
This finding directly motivates checking `Vector4::GetHashCode()` (same shard, not yet audited at time of
writing) for the identical gap, given the same evident copy-derived file structure.

## Missing or Weak Tests
Not independently located in this pass; a UBSan-instrumented test computing `GetHashCode()` for a `Vector3`
whose components' bit patterns are engineered to overflow on summation would directly demonstrate this.

## Positive Findings
Cross product and both Transform families independently verified correct against standard/XNA-reference
formulas.

## Final Assessment
One MEDIUM-severity finding: `GetHashCode()`'s signed-integer-overflow UB, already fixed in the sibling
`Vector2.cpp` but not propagated here.
