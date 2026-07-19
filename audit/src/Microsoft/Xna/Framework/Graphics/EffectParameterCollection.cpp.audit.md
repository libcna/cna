# Audit: src/Microsoft/Xna/Framework/Graphics/EffectParameterCollection.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/EffectParameterCollection.cpp`
- Audit status: AUDITED (full read, 46 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Effect/EffectParameterCollection.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements `Count`, both `operator[]` overloads (int and string), `Add` (NOXNA — FNA's collection
is populated entirely by its own internal constructor, not incrementally), `GetParameterBySemantic`,
and the mutable/const iterator pairs.

## Executive Verdict
Correct overall; one MEDIUM exception-type finding shared with this batch's other three collection
types.

## Checklist Results
- `operator[](const std::string&)` and `GetParameterBySemantic`: correct linear search, correct
  nullptr-on-miss, matching FNA.
- `Add`: correctly NOXNA-tagged (FNA's collection has no public/internal incremental `Add` — its
  constructor takes a pre-built `List<EffectParameter>` directly); this port's `Add` exists because
  its own construction pattern (an initially-empty collection filled by whatever parses effect
  metadata) differs.

## Detailed Findings

### MEDIUM — `operator[](int index)` throws raw `std::out_of_range` instead of this project's own
`System::ArgumentOutOfRangeException`
`return *elements_.at(index);` (line 8) — `std::vector::at()` throws `std::out_of_range` on an
out-of-range index. This project's established convention (confirmed via numerous other findings
across this audit, e.g. `PropertyDictionary` in the `xna-gamerservices` shard) is to use the
project's own `System::ArgumentOutOfRangeException`/equivalent sharp-runtime exception types instead
of raw `std::` exceptions, so callers catching `System::Exception`-derived types (the project's own
convention) don't miss this. Bounds-checking itself is present and correct — only the exception
type is non-conforming.

## Cross-File Observations
The identical pattern (`.at()`, raw `std::out_of_range`) appears in `EffectPassCollection::
operator[](int)`, `EffectTechniqueCollection::operator[](int)`, and `EffectAnnotationCollection::
operator[](int)` in this same batch — a consistent, repeated instance of this project's known
"wrong exception type" cross-cutting pattern, not isolated to this one file.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Bounds-checking itself is present and correct in every indexer; only the exception type deviates
from this project's convention.

## Final Assessment
One MEDIUM finding: wrong exception type on out-of-range `operator[](int)`, shared with three
sibling collection types in this batch.
