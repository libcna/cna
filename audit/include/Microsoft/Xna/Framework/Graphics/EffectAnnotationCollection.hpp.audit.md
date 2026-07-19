# Audit: include/Microsoft/Xna/Framework/Graphics/EffectAnnotationCollection.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/EffectAnnotationCollection.hpp`
- Audit status: AUDITED (full read, 91 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Effect/EffectAnnotationCollection.cs`
- Main related tests: not independently located in this pass

## Purpose
An indexed, name-keyed collection of `EffectAnnotation` objects (attached to parameters, passes,
and techniques).

## Executive Verdict
Correct. Unlike its three siblings in this batch, this collection stores elements **by value**
(`std::vector<EffectAnnotation>`, not `std::vector<std::unique_ptr<EffectAnnotation>>`) — a
deliberate, reasonable difference: annotations are pure, immutable read-only metadata never handed
out as a long-lived cached reference the way an `EffectParameter&`/`EffectTechnique*` commonly is
(no real usage pattern caches an `EffectAnnotation&` across a later `Add()` the way a shader
parameter reference is cached across frames), so the reallocation-safety concern that motivated
`unique_ptr` storage elsewhere doesn't apply here with the same force.

## Checklist Results
No issues found beyond the shared exception-type note in the paired `.cpp` report.

## Detailed Findings
See the paired `.cpp` report for the shared MEDIUM finding.

## Cross-File Observations
FNA's real `EffectAnnotationCollection` has a static, shared `Empty` singleton instance
(`internal static readonly EffectAnnotationCollection Empty`) used whenever a parameter/pass/
technique has zero annotations, avoiding a per-instance empty-list allocation. This port has no
equivalent — every instance constructs its own empty `std::vector`. A minor, purely internal
allocation-count difference with no observable behavioral impact.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The by-value storage choice (vs. its siblings' `unique_ptr` storage) is a correctly-reasoned,
deliberate divergence based on this type's actual usage pattern, not an inconsistency.

## Final Assessment
No findings in this header beyond the paired `.cpp`'s exception-type note.
