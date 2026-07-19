# Audit: src/Microsoft/Xna/Framework/Graphics/EffectAnnotationCollection.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/EffectAnnotationCollection.cpp`
- Audit status: AUDITED (full read, 35 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Effect/EffectAnnotationCollection.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements `Count`, both `operator[]` overloads, `Add`, and the iterator pair over a by-value
`std::vector<EffectAnnotation>`.

## Executive Verdict
Correct overall; shares this batch's one recurring MEDIUM exception-type finding.

## Checklist Results
Same correctness shape as its sibling collection `.cpp` files in this batch, adjusted for by-value
(not `unique_ptr`) element storage.

## Detailed Findings

### MEDIUM — `operator[](int index)` throws raw `std::out_of_range` (`elements_.at(index)`, line 9)
instead of this project's own `System::ArgumentOutOfRangeException`. See
`EffectParameterCollection.cpp.audit.md`'s identical finding for the full writeup.

## Cross-File Observations
See the cross-cutting note in `EffectParameterCollection.cpp.audit.md` — this makes four confirmed
instances of the identical pattern across all four Effect-reflection collection types in this
batch.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Bounds-checking present and correct; only the exception type deviates from convention.

## Final Assessment
One MEDIUM finding, the fourth and final instance of this batch's recurring exception-type pattern.
