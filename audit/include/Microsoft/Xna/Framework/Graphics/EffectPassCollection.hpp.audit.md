# Audit: include/Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp`
- Audit status: AUDITED (full read, 152 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Effect/EffectPassCollection.cs`
- Main related tests: not independently located in this pass

## Purpose
An indexed, name-keyed collection of `EffectPass` objects (`EffectTechnique.Passes`).

## Executive Verdict
Correct. Same `std::unique_ptr`-backed-storage pointer-stability design as
`EffectParameterCollection`/`EffectTechniqueCollection`, correctly reasoned in the doc comment.

## Checklist Results
No issues found beyond the shared exception-type note in the paired `.cpp` report.

## Detailed Findings
See the paired `.cpp` report for the shared MEDIUM finding.

## Cross-File Observations
FNA's real `EffectPassCollection` has a "single item, no list allocated" fast-path optimization
(`elements == null` + a separate `singleItem` field) for the extremely common single-pass-technique
case, avoiding a `List<T>` allocation. This port always uses a `std::vector`, forgoing that
micro-optimization — a reasonable simplification, not a correctness issue (real XNA behavior is
fully preserved; only an internal allocation-count difference, invisible to any caller).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Clean, consistent design matching its sibling collection types in this batch.

## Final Assessment
No findings in this header beyond the paired `.cpp`'s exception-type note.
