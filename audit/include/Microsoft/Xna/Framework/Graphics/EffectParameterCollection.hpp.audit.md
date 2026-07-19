# Audit: include/Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp`
- Audit status: AUDITED (full read, 149 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Effect/EffectParameterCollection.cs`
- Main related tests: not independently located in this pass

## Purpose
An indexed, name-keyed collection of `EffectParameter` objects (`Effect.Parameters`,
`EffectParameter.Elements`/`.StructureMembers`).

## Executive Verdict
Correct design: elements are stored behind `std::unique_ptr<EffectParameter>` specifically so a
previously-obtained `EffectParameter&`/`*` (a very common real usage pattern — caching a parameter
reference once, calling `SetValue` on it every frame) stays valid across a later `Add()` even if the
backing vector reallocates. The doc comment explicitly and correctly identifies the alternative
(`std::vector<EffectParameter>` by value) as unsafe for this exact reason.

## Checklist Results
- `GetParameterBySemantic` correctly returns `nullptr` on no match, matching FNA's real behavior
  (including FNA's own acknowledged "FIXME: ArrayIndexOutOfBounds?" wart on the name-indexer, which
  this port reasonably keeps as nullptr-return rather than "fixing" into a throw FNA itself doesn't
  do).
- `operator[](int)` — see the paired `.cpp` report for its exception-type finding.

## Detailed Findings
See the paired `.cpp` report for one MEDIUM finding (raw `std::out_of_range` instead of this
project's own `System::ArgumentOutOfRangeException`).

## Cross-File Observations
Shares the identical `std::unique_ptr`-backed-storage design and the identical name-indexer
nullptr-on-miss behavior with `EffectPassCollection`/`EffectTechniqueCollection` in this same batch
— a consistent, deliberate pattern across all three collection types, not an isolated choice.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The `std::unique_ptr`-per-element storage design is a genuine, well-motivated fix for a real C++
pointer-stability hazard that C#'s reference-type `List<T>` doesn't have to worry about at all —
correctly identified and solved.

## Final Assessment
No findings in this header beyond the paired `.cpp`'s exception-type note.
