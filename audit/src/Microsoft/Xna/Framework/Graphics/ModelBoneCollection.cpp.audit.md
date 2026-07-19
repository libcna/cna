# Audit: src/Microsoft/Xna/Framework/Graphics/ModelBoneCollection.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/ModelBoneCollection.cpp`
- Audit status: AUDITED (full read, 54 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/ModelBoneCollection.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements both `operator[]` overloads, `getCountProperty`, `TryGetValue`, `Contains`, and
`begin`/`end`.

## Executive Verdict
Correct in its bounds-checking logic — `operator[](int)` uses `bones_.at(...)` (throws
`std::out_of_range` for an out-of-range index, matching real .NET `List<T>`/
`ReadOnlyCollection<T>` indexer semantics that back FNA's real `ModelBoneCollection`) — but the
by-name overload's exception TYPE diverges from FNA's documented contract.

## Checklist Results
- `operator[](int)` (line 8-11): bounds-checked via `.at()` — correct, positive counter-example to
  this audit's recurring unchecked-collection-indexer pattern (contrast with the sibling
  `ModelMeshPartCollection`/`ModelEffectCollection` in this same batch, both of which lack this
  check).
- `TryGetValue`/`Contains`: correct linear-search implementations, null-safe (`if (bone && ...)`).

## Detailed Findings

### LOW — `operator[](const std::string&)` throws raw `std::out_of_range` instead of FNA's
documented `System::Collections::Generic::KeyNotFoundException`
FNA's real `ModelBoneCollection.cs` (`this[string boneName]` indexer) explicitly throws
`KeyNotFoundException` when the name isn't found (confirmed by direct source read). This file
(line 18) throws `std::out_of_range` instead. Same recurring cross-cutting exception-type pattern
noted elsewhere in this audit (e.g. `PropertyDictionary` in the `xna-gamerservices` shard) — rated
LOW here rather than MEDIUM since a name-keyed lookup failure is a less commonly-caught-by-type
scenario than the numeric-index case, but still a real catch-compatibility gap for code written
against FNA's documented contract.

## Cross-File Observations
Contrast with `ModelMeshPartCollection`/`ModelEffectCollection` (audited in this same batch): both
of those types' `operator[](int)` use raw, unchecked `std::vector::operator[]` instead of `.at()`
like this file does — meaning `ModelBoneCollection`/`ModelMeshCollection` (see its own report) are
the well-behaved counter-examples in this batch, not the exception.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The numeric-index bounds-check is correct and present, unlike two sibling collection types in this
same batch.

## Final Assessment
One LOW finding: by-name lookup throws the wrong exception type relative to FNA's documented
contract.
