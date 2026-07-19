# Audit: src/Microsoft/Xna/Framework/Graphics/EffectTechniqueCollection.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/EffectTechniqueCollection.cpp`
- Audit status: AUDITED (full read, 33 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Effect/EffectTechniqueCollection.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements `Count`, both `operator[]` overloads, `Add`, and the iterator pair.

## Executive Verdict
Correct overall; shares this batch's one recurring MEDIUM exception-type finding.

## Checklist Results
Same shape and correctness as its sibling collection `.cpp` files in this batch.

## Detailed Findings

### MEDIUM — `operator[](int index)` throws raw `std::out_of_range` (`elements_.at(index)`, line 8)
instead of this project's own `System::ArgumentOutOfRangeException`. See
`EffectParameterCollection.cpp.audit.md`'s identical finding for the full writeup.

## Cross-File Observations
See the cross-cutting note in `EffectParameterCollection.cpp.audit.md`.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Bounds-checking present and correct; only the exception type deviates from convention.

## Final Assessment
One MEDIUM finding, shared with three sibling collection types in this batch.
