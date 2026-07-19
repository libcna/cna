# Audit: src/Microsoft/Xna/Framework/BoundingFrustum.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/BoundingFrustum.cpp`
- Audit status: AUDITED (full read, 384 lines; `Intersects(Ray)`'s `NotImplementedException` path
  additionally cross-checked against `/rv/data/library/github.com/FNA-XNA/FNA/src/BoundingFrustum.cs`)
- Subsystem: `xna-framework-core` shard
- File type: C++ implementation
- XNA/FNA relevance: matches real XNA `BoundingFrustum`'s plane-extraction and corner-computation
  algorithms exactly, including one confirmed-faithful, loudly-failing incomplete method
- Main related tests: not independently located in this pass

## Purpose
Implements plane extraction from the view-projection matrix (`CreatePlanes`), corner computation via
3-plane intersection (`CreateCorners`/`IntersectionPoint`), and the full Contains/Intersects family.

## Executive Verdict
Healthy -- every algorithm independently verified correct, including the standard Gribb/Hartmann-style
frustum-plane-extraction formulas and the Cramer's-rule-style 3-plane intersection point formula.
`Intersects(Ray)`'s general-case `NotImplementedException` was cross-checked directly against the FNA
reference source and confirmed to be a faithful (and, unlike `BoundingSphere::Contains(BoundingFrustum)`,
*loudly failing rather than silently wrong*) reproduction of a genuine FNA limitation.

## Checklist Results

### Plane extraction (`CreatePlanes`): correct, matches known frustum-plane-extraction coefficients
Each of the 6 planes (Near/Far/Left/Right/Top/Bottom) is derived from specific row combinations of the
view-projection matrix (e.g. `Near = (-M13,-M23,-M33,-M43)`, `Far = (M13-M14, M23-M24, M33-M34, M43-M44)`)
-- these match the standard, well-known frustum-plane-extraction coefficients (the same technique
XNA/FNA's own `BoundingFrustum` uses), and each plane is correctly normalized afterward.

### Corner computation (`IntersectionPoint`): correct 3-plane intersection formula
The Cramer's-rule-style formula (`f = -dot(a.Normal, cross(b.Normal, c.Normal))`,
`v1/v2/v3` each a cross-product scaled by the third plane's `D`, `result = (v1+v2+v3)/f`) is the standard
3-plane intersection point formula, independently verified algebraically consistent, and the 8 corner
calls (`IntersectionPoint(planes[0/1], planes[2/3], planes[4/5], ...)`) correctly enumerate all 8
near/far x left/right x top/bottom combinations.

### CONFIRMED, FNA-faithful (loudly-failing, not silently wrong): `Intersects(Ray)`'s general case
```cpp
if (ctype != ContainmentType::Intersects) { throw std::out_of_range("ctype"); }
throw System::NotImplementedException("BoundingFrustum::Intersects(Ray) is not implemented");
```
Handles the "ray origin outside the frustum" (`Disjoint`) and "ray origin inside the frustum" (`Contains`,
distance `0`) cases correctly, but throws for the general `Intersects` case (ray origin on/near a frustum
boundary plane) rather than computing a real intersection distance. Directly verified this exactly matches
FNA's own `BoundingFrustum.Intersects(ref Ray, out float?)` (`/rv/data/library/github.com/FNA-XNA/FNA/src/
BoundingFrustum.cs`, lines 453-474), which has the identical `throw new NotImplementedException();` for
this exact case -- a real, intentional (if incomplete) FNA limitation, not a CNA port gap. Unlike
`BoundingSphere::Contains(BoundingFrustum)`'s version of this same "FNA never finished this" pattern (same
shard, already flagged), this one **fails loudly** with a clear, descriptive exception message (better than
FNA's own bare `NotImplementedException`) rather than silently returning a plausible-looking wrong answer
-- a caller hitting this case gets an unambiguous signal rather than corrupted-but-unnoticed data.

### Everything else: correct
`Contains`/`Intersects` against `BoundingBox`/`BoundingSphere`/`Vector3`/`Plane`/other `BoundingFrustum`
all correctly classify against all 6 planes, correctly distinguish `Disjoint` (any plane classifies
`Front`) from `Intersects` (any plane classifies `Intersecting`) from `Contains` (all planes classify
`Back`) -- the standard frustum-culling classification algorithm, correctly implemented. `GetHashCode()`
correctly delegates to `matrix.GetHashCode()` alone (a single value, not a sum, so it doesn't independently
introduce the sibling signed-overflow-UB pattern, though it does transitively return whatever
`Matrix::GetHashCode()`'s own already-flagged value is). `operator==`'s comment correctly explains the
omitted C#-reference-type null-guard as an intentional, documented C++ deviation.

## Detailed Findings
None new (the `Intersects(Ray)` limitation is confirmed FNA-faithful and already fails safely/loudly).

## Cross-File Observations
Provides a useful positive contrast to `BoundingSphere::Contains(BoundingFrustum)`'s silently-wrong version
of the same "FNA never finished this" pattern -- worth noting in `AUDIT_CROSS_CUTTING_FINDINGS.md`'s
existing entry for that file as an example of the *better* way to leave an FNA limitation faithfully
unimplemented (throw loudly, don't silently return plausible-but-wrong data).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Correct, non-trivial plane-extraction and 3-plane-intersection formulas; a properly-handled (loudly
failing) faithful reproduction of a real FNA limitation, with a better exception message than FNA's own.

## Final Assessment
No issues found; confirms and usefully contrasts with the `BoundingSphere::Contains(BoundingFrustum)`
finding already recorded elsewhere in this shard.
