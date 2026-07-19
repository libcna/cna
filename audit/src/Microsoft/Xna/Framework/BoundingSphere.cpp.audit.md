# Audit: src/Microsoft/Xna/Framework/BoundingSphere.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/BoundingSphere.cpp`
- Audit status: AUDITED (full read, 391 lines; `Contains(BoundingFrustum)` additionally cross-checked
  directly against `/rv/data/library/github.com/FNA-XNA/FNA/src/BoundingSphere.cs`, the authoritative
  reference per this project's own conventions)
- Subsystem: `xna-framework-core` shard
- File type: C++ implementation
- XNA/FNA relevance: matches real XNA `BoundingSphere`'s algorithms, including one confirmed faithful
  reproduction of an FNA-upstream incomplete implementation
- Main related tests: not independently located in this pass

## Purpose
Implements every `BoundingSphere` Transform/Contains/Intersects overload and the 4 factory methods
(`CreateFromBoundingBox`/`CreateFromFrustum`/`CreateFromPoints`/`CreateMerged`).

## Executive Verdict
Needs attention -- one confirmed finding (verified directly against the FNA reference source):
`Contains(BoundingFrustum)` can never return `ContainmentType::Disjoint`, because it faithfully reproduces
a real, incomplete, `// TODO`-marked implementation in FNA's own `BoundingSphere.cs`. This is correct,
policy-compliant FNA-fidelity behavior (not a CNA-introduced regression), but the port is missing the
explanatory comment FNA's own source carries at this exact spot, unlike this project's own established
practice of documenting known-incomplete-but-faithfully-preserved FNA behavior elsewhere (e.g.
`LzxDecoder.cpp`'s Intel E8 comment, `VideoDecoder.cpp`'s various similar notes). Every other algorithm in
this file (sphere transform's scale-factor derivation, `CreateFromPoints`' Ritter's-algorithm-style
bounding-sphere construction, `CreateMerged`'s sphere-merge math, `Contains(BoundingBox)`'s distance-to-
nearest-point technique) is correct.

## Checklist Results

### CONFIRMED (via direct FNA source read): `Contains(BoundingFrustum)` can never return `Disjoint`
```cpp
ContainmentType BoundingSphere::Contains(const BoundingFrustum& frustum) const
{
    bool inside = true;
    for (const Vector3& corner : frustum.GetCorners())
    {
        if (Contains(corner) == ContainmentType::Disjoint) { inside = false; break; }
    }
    if (inside) { return ContainmentType::Contains; }

    double dmin = 0.0;
    if (dmin <= Radius * Radius)
    {
        return ContainmentType::Intersects;
    }
    return ContainmentType::Disjoint;
}
```
`dmin` is initialized to `0.0` and never accumulated from anything before the `dmin <= Radius * Radius`
check -- since `Radius * Radius >= 0` always holds for a valid (non-negative) radius, this condition is
*always* true, meaning this method returns either `Contains` (all frustum corners inside) or `Intersects`
(any other case) -- **it can never actually detect that the sphere and frustum are fully disjoint**, which
is precisely the case this branch exists to distinguish.

Directly verified this is not a CNA-introduced defect: `/rv/data/library/github.com/FNA-XNA/FNA/src/
BoundingSphere.cs` (lines 218-248) contains the **identical** dead-code shape, including FNA's own comment
marking it explicitly incomplete:
```csharp
// Check if the distance from sphere center to frustrum face is less than radius.
double dmin = 0;
// TODO : calcul dmin

if (dmin <= Radius * Radius)
{
    return ContainmentType.Intersects;
}

// Else disjoint
return ContainmentType.Disjoint;
```
This confirms `Contains(BoundingFrustum)` is a **known, real, upstream-incomplete XNA/FNA method** (the
`// TODO : calcul dmin` comment is FNA's own admission that the per-axis distance accumulation the
`BoundingBox`/`BoundingSphere`-vs-`BoundingBox` overloads correctly implement was never written for this
one overload). Per this project's own explicit FNA-fidelity mandate ("match XNA/FNA behavior over personal
C++ preference... do not redesign behavior for cleaner C++"), faithfully preserving this incompleteness
rather than independently completing the `dmin` calculation is the policy-correct choice here -- this is
*not* scored as a defect introduced by this port.

**However**: the CNA port's own code has **no comment at all** explaining this -- FNA's own source carries
an explicit `// TODO : calcul dmin` at exactly this spot, but the ported `dmin` line here has none. This
is a documentation-quality gap relative to this project's own established practice elsewhere (multiple
other files in this audit -- `LzxDecoder.cpp`'s Intel E8 handling, `VideoDecoder.cpp`'s several similar
notes -- explicitly flag "this looks incomplete but faithfully matches FNA's own unfinished implementation"
so a future maintainer doesn't mistake it for an accidental omission and "fix" it in a way that would
silently diverge from FNA, or waste time re-discovering what this audit pass just confirmed).

**Recommended fix shape**: add a comment at the `double dmin = 0.0;` line explaining that this faithfully
reproduces FNA's own incomplete `BoundingSphere.Contains(BoundingFrustum)` (citing the FNA source location),
so the method's real-world behavior (never returns `Disjoint`) is documented rather than silently
surprising, without actually changing the behavior itself (which would be an unreviewed FNA-parity
deviation, out of scope for a pure documentation fix).

### Everything else: independently verified correct

- **`Transform(Matrix)`**: the radius scale-factor (`sqrt(max of the 3 rotation-block rows' squared
  magnitudes))`) correctly handles non-uniform scale by taking the *largest* axis scale (conservative,
  matching real XNA's own approach -- the resulting sphere may over-approximate under non-uniform scale,
  which is the documented, accepted XNA behavior, not a bug).
- **`CreateFromPoints`**: implements the standard "find extreme points along each axis, pick the pair with
  the largest separation as the initial diameter, then iteratively grow the sphere to include any
  point still outside it" (Ritter's bounding-sphere approximation) algorithm -- independently traced through
  the iterative-growth loop (`g = center - radius*direction; center = (g+pt)/2; radius = Distance(pt,
  center)`) and confirmed it correctly re-centers/re-grows the sphere to include each outlying point.
- **`CreateMerged`**: the two early-return cases (one sphere already contains the other) and the general
  merge-along-the-center-line formula both correctly handle the sphere-merging geometry.
- **`Contains(BoundingBox)`**: same "accumulate squared distance to nearest point per axis" (`dmin`)
  technique as `BoundingBox`'s own audited version, but here genuinely computed (each axis actually
  accumulates into `dmin`), correctly contrasting with the broken `Contains(BoundingFrustum)` overload
  right next to it.
- **`GetHashCode()`**: `Center.GetHashCode() + std::hash<float>{}(Radius)` -- the addition promotes to
  unsigned `std::size_t` arithmetic (the function's own return type), so this is safe regardless of what
  `Vector3::GetHashCode()`'s own signed-`int` value is; unsigned overflow is well-defined wraparound in
  C++, not UB.

## Detailed Findings

1. **[Confirmed, FNA-faithful -- documentation gap, not a behavior defect]** `Contains(BoundingFrustum)`
   can never return `Disjoint`, exactly reproducing FNA's own `// TODO`-marked incomplete implementation.
   The port is missing the explanatory comment FNA's own source has at this spot. Lines 112-136.

## Cross-File Observations
This is a genuine example of the audit's FNA cross-referencing methodology catching a real, user-visible
behavioral quirk that turns out to be *correct per this project's own policy* rather than a port defect --
worth surfacing prominently (e.g. in `AUDIT_CROSS_CUTTING_FINDINGS.md`) precisely because a future
maintainer encountering this method's surprising behavior needs to know it's intentional-by-policy, not
independently investigate and potentially "fix" it in a way that would violate FNA parity.

## Missing or Weak Tests
Not independently located in this pass; if a test asserts `Contains(BoundingFrustum)` correctly returns
`Disjoint` for a genuinely non-overlapping sphere/frustum pair, it would currently fail -- worth confirming
no such test exists (which would itself indicate the FNA-parity bug was already known and accepted) when
the `tests-*` shard for this area is audited.

## Positive Findings
Every other algorithm in this file -- including the non-trivial `CreateFromPoints` bounding-sphere
approximation and `CreateMerged`'s sphere-merge geometry -- independently verified correct.

## Final Assessment
One confirmed finding: `Contains(BoundingFrustum)` faithfully reproduces a real, upstream-incomplete FNA
implementation (verified directly against the FNA reference source) -- correct per this project's
FNA-fidelity policy, but missing the explanatory comment FNA's own source has, which is worth adding as a
documentation-only fix.
