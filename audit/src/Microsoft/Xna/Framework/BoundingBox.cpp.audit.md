# Audit: src/Microsoft/Xna/Framework/BoundingBox.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/BoundingBox.cpp`
- Audit status: AUDITED (full read, 556 lines)
- Subsystem: `xna-framework-core` shard
- File type: C++ implementation
- XNA/FNA relevance: matches real XNA `BoundingBox`'s exact containment/intersection algorithms
- Main related tests: not independently located in this pass

## Purpose
Implements every `BoundingBox` Contains/Intersects overload, `GetCorners`, and the 3 factory methods.

## Executive Verdict
Healthy -- every algorithm independently verified correct against known XNA reference behavior. Notable
positive finding: `GetHashCode()` explicitly avoids the signed-overflow-UB pattern found in 4 sibling files
in this same shard, by deliberately choosing a different (documented) combining scheme rather than a
literal line-by-line port of FNA's `int`-addition approach.

## Checklist Results

### `Contains(BoundingSphere)`/`Intersects(BoundingSphere)`: correct "distance to nearest point" technique
Both use the same "accumulate squared distance from the sphere center to the nearest point on the box's
surface, per axis" (`dmin`) technique real XNA/FNA uses -- independently re-derived and confirmed
algebraically correct for both the full-containment fast-path check and the general per-axis distance
accumulation.

### `GetCorners()`: correct order
Both the value-returning and buffer-writing overloads produce corners in the exact same order (verified
identical between the two implementations, and matching real XNA's own documented corner ordering:
front-face Y-max-to-Y-min going Min.X→Max.X→Max.X→Min.X at Max.Z, then repeated at Min.Z).

### `Intersects(Plane)`: correct positive/negative-vertex optimized technique
Selects the "positive vertex" (furthest along the plane normal) and "negative vertex" (furthest against
it) per-axis based on the normal's sign, then classifies using just those two vertices instead of testing
all 8 corners -- the standard optimized AABB-vs-plane classification technique, matching XNA's own
approach exactly (test negative vertex for `Front`, positive vertex for `Back`, else `Intersecting`).

### `GetHashCode()`: a positive contrast to the `Vector3`/`Vector4`/`Quaternion`/`Matrix` UB pattern
```cpp
// FNA: Min.GetHashCode() + Max.GetHashCode().
// C++ uses a platform-native size_t with boost-style combining instead of int addition.
```
This file's author explicitly recognized that porting FNA's `int`-addition `GetHashCode()` verbatim would
carry over a C++-specific UB risk, and *deliberately* substituted a different, safe combining scheme (a
`std::hash<float>`-based boost-style `hash_combine`) instead of a line-by-line port -- the comment shows
this was a conscious choice, not an accident. This is a useful positive counter-example demonstrating that
the sibling `Vector3`/`Vector4`/`Quaternion`/`Matrix` bug (flagged in `AUDIT_CROSS_CUTTING_FINDINGS.md`) was
an avoidable oversight in those specific files, not an unavoidable consequence of porting FNA's `int`-sum
`GetHashCode()` pattern to C++.

## Detailed Findings
None.

## Cross-File Observations
Strengthens the cross-cutting `GetHashCode()` finding: this file proves the fix was known and applicable
(a different developer, or the same developer at a different time, clearly understood the UB risk and
addressed it here) -- reinforcing that `Vector3`/`Vector4`/`Quaternion`/`Matrix` not receiving the same
treatment is a real, fixable gap rather than an accepted tradeoff.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Every algorithm correctly matches real XNA reference behavior; `GetHashCode()` shows deliberate, correct
avoidance of a UB pattern present in 4 sibling files.

## Final Assessment
No issues found.
