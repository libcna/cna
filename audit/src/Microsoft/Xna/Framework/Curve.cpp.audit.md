# Audit: src/Microsoft/Xna/Framework/Curve.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Curve.cpp`
- Audit status: AUDITED (full read, 312 lines; `ComputeTangent()`'s epsilon thresholds cross-checked
  directly against `/rv/data/library/github.com/FNA-XNA/FNA/src/Curve.cs`)
- Subsystem: `xna-framework-core` shard
- File type: C++ implementation
- XNA/FNA relevance: matches real XNA `Curve`'s exact Hermite-spline evaluation and loop-type handling,
  including a confirmed faithful reproduction of a well-known FNA/.NET quirk
- Main related tests: not independently located in this pass

## Purpose
Implements `Evaluate()` (all 5 `CurveLoopType` pre/post-loop behaviors), `ComputeTangent()`
(Flat/Linear/Smooth tangent types), and the `GetNumberOfCycle`/`GetCurvePosition` helpers.

## Executive Verdict
Healthy -- the full `Evaluate()` loop-type dispatch (`Constant`/`Linear`/`Cycle`/`CycleOffset`/
`Oscillate`, for both pre- and post-loop) and the Hermite-basis-function curve-segment evaluation in
`GetCurvePosition()` both independently verified correct against the known XNA/FNA algorithm shape. One
confirmed-FNA-faithful (not a port defect) finding: `ComputeTangent()`'s `Smooth` branches use two
*different* near-zero epsilon thresholds for `TangentIn` vs. `TangentOut`, and this exact inconsistency is
present in FNA's own real source too.

## Checklist Results

### `Evaluate()`: correct, full 5-loop-type dispatch matches FNA exactly
`Constant`/`Linear`/`Cycle`/`CycleOffset`/`Oscillate` are all correctly implemented for both the
before-first-key (`preLoop`) and after-last-key (`postLoop`) cases, including the `Oscillate` case's
even/odd-cycle mirroring (reflecting the virtual position around the curve's span on odd cycles) and
`CycleOffset`'s value-shift-per-cycle accumulation (`cycle * (last.Value - first.Value)`) -- all matching
the known XNA/FNA algorithm shape exactly.

### `GetCurvePosition()`: correct cubic Hermite basis functions
The `(2t³-3t²+1)`/`(t³-2t²+t)`/`(3t²-2t³)`/`(t³-t²)` basis-function coefficients applied to
`prev.Value`/`prev.TangentOut`/`next.Value`/`next.TangentIn` respectively are the standard Hermite spline
basis functions, correctly matching XNA/FNA's own per-segment curve evaluation.

### CONFIRMED FNA-faithful, not a port defect: `ComputeTangent()`'s asymmetric epsilon thresholds
`TangentIn`'s `Smooth` branch uses `MathHelper::WithinEpsilon(pn, 0.0f)` (the ~1.19e-7 machine-epsilon-based
comparison), while `TangentOut`'s `Smooth` branch uses `std::fabs(pn) < std::numeric_limits<float>::
denorm_min()` -- an almost-never-true threshold (`denorm_min()` is the smallest positive representable
`float`, not a reasonable "close to zero" tolerance). Directly verified against FNA's real
`Curve.ComputeTangent()` (`/rv/data/library/github.com/FNA-XNA/FNA/src/Curve.cs`, lines 289-320): FNA's own
`TangentIn` branch uses `MathHelper.WithinEpsilon(pn, 0.0f)` and its `TangentOut` branch uses
`Math.Abs(pn) < float.Epsilon` -- and C#'s `float.Epsilon` is the well-known "smallest positive
representable `float`" constant (a famously easy-to-misuse .NET API, often mistaken for "a small tolerance
value" when writing exactly this kind of near-zero check). `std::numeric_limits<float>::denorm_min()` is
the precise C++ equivalent of C#'s `float.Epsilon`, so this port has faithfully, byte-for-byte reproduced
FNA's own real (and reasonably well-known in the XNA/MonoGame community) inconsistency between its two
`Smooth`-tangent branches. Per this project's FNA-fidelity policy, this is correct porting behavior -- but,
consistent with the pattern already seen at `BoundingSphere::Contains(BoundingFrustum)` and
`BoundingFrustum::Intersects(Ray)` (same overall shard), the CNA port carries no comment here explaining
that the asymmetry is an intentional, faithful reproduction rather than an accidental transcription slip.

### `ComputeTangent()`'s explicit bounds check: correctly documented intentional addition
The comment "FNA has no bounds check here; C++ adds one to avoid undefined behaviour" correctly explains why
this port validates `keyIndex` explicitly where FNA's own C# implementation relies on `List<T>`'s own
implicit range-checking (which C++'s raw indexing does not provide) -- exactly the right kind of disclosed,
minimal, safety-motivated addition.

## Detailed Findings
None newly actionable (the `ComputeTangent()` epsilon asymmetry is confirmed FNA-faithful); worth adding a
short comment noting the asymmetry is intentional, matching this shard's now-recurring pattern of
faithfully-preserved-but-undocumented FNA quirks.

## Cross-File Observations
This is the third instance in this shard of a "looks like an inconsistency/bug" spot that direct FNA
source comparison confirmed is a faithful, real upstream quirk (after `BoundingSphere::Contains
(BoundingFrustum)` and `BoundingFrustum::Intersects(Ray)`) -- worth a consolidated note in
`AUDIT_CROSS_CUTTING_FINDINGS.md` observing this as a recurring pattern across `xna-framework-core`: several
spots that would look like defects in isolation are confirmed-faithful FNA reproductions once checked
against the reference source, and in each case the port is missing the kind of explanatory comment this
project's own conventions otherwise use well.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Every non-trivial curve-evaluation algorithm (all 5 loop types, Hermite basis functions) independently
verified correct; the `ComputeTangent()` bounds-check addition is a good example of a disclosed, minimal,
safety-motivated intentional deviation.

## Final Assessment
No new actionable defects; confirms (via direct FNA source comparison) that an apparent epsilon-threshold
inconsistency in `ComputeTangent()` is a faithful reproduction of a real FNA/.NET quirk, not a port bug.
