# Audit: src/Microsoft/Xna/Framework/Plane.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Plane.cpp`
- Audit status: AUDITED (full read, 200 lines)
- Subsystem: `xna-framework-core` shard
- File type: C++ implementation
- XNA/FNA relevance: matches real XNA `Plane`'s exact math, including the inverse-transpose
  plane-transform technique
- Main related tests: not independently located in this pass

## Purpose
Implements all `Plane` methods.

## Executive Verdict
Healthy.

## Checklist Results

### 3-point plane constructor: correct winding convention
`Plane(Vector3 a, Vector3 b, Vector3 c)` computes `Cross(b-a, c-a)`, normalizes it as `Normal`, and sets
`D = -Dot(Normal, a)` -- matches real XNA's own winding convention for this constructor exactly (same
operand order for the cross product).

### `Transform(Plane, Matrix)`: correct inverse-transpose technique
Uses `Invert(matrix)` then `Transpose(...)` then `Vector4::Transform` on the plane's `(Normal, D)` packed
as a `Vector4` -- the mathematically correct technique for transforming a plane by a matrix that may
include non-uniform scaling (a plane's normal does not transform the same way a position/direction does
under such a matrix), matching XNA's own real `Plane.Transform(Plane, Matrix)` implementation.

### `Transform(Plane, Quaternion)`: correct
A quaternion is always a pure rotation (no scaling), so this correctly just rotates `Normal` directly via
`Vector3::Transform` and leaves `D` unchanged (a rotation about the origin doesn't change a plane's distance
from the origin).

### `GetHashCode()`: safe at this level, transitively affected by `Vector3`'s already-flagged bug
`Normal.GetHashCode() ^ std::hash<float>{}(D)` XOR-combines safely at Plane's own level, but
`Normal.GetHashCode()` calls `Vector3::GetHashCode()`, which already has the confirmed signed-overflow-UB
gap (see `AUDIT_CROSS_CUTTING_FINDINGS.md`) -- not a new/distinct bug, just inherited.

## Detailed Findings
None new in this file (the `GetHashCode()` issue is `Vector3`'s, already tracked).

## Cross-File Observations
Confirms the cross-cutting note that `Plane::GetHashCode()` transitively inherits `Vector3`'s bug via
`Normal.GetHashCode()`.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Correct, non-trivial inverse-transpose plane-transform technique -- an easy detail to get wrong (naively
transforming the normal the same way a direction vector would be) that was implemented correctly here.

## Final Assessment
No new issues found in this file.
