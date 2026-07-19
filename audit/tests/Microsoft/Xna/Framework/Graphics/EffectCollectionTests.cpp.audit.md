# Audit: tests/Microsoft/Xna/Framework/Graphics/EffectCollectionTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Graphics/EffectCollectionTests.cpp` (392 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-graphics` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `EffectTechniqueCollection.hpp`/`.cpp`,
  `EffectParameterCollection.hpp`/`.cpp`, `EffectPassCollection.hpp`/`.cpp`
- Main related tests: N/A (this IS a test file)

## Purpose
Exercises all three collection types' index/name lookup, iteration, and — notably — case
sensitivity, duplicate-name first-match-wins semantics, out-of-range-index exception type, and
pointer stability across reallocating `Add()` calls.

## Executive Verdict
An exceptionally thorough, well-cross-referenced test file. `PointerStableAcrossReallocatingAdd`
(present for both `EffectParameterCollection` and `EffectPassCollection`) directly documents and
locks in a real, previously-fixed dangling-pointer bug (Task 884: `std::vector<T>`-by-value storage
reallocating and invalidating previously-returned references, the identical class of bug Task 355
found for `EffectTechniqueCollection`) via `std::vector<std::unique_ptr<T>>` storage.

## Checklist Results
- `EffectParameterCollectionTest.IndexByIntNegativeThrowsOutOfRange`/
  `IndexByIntEqualToCountThrowsOutOfRange`/`ConstIndexByIntOutOfRangeThrowsOutOfRange`/
  `IndexByIntOnEmptyCollectionThrowsOutOfRange` correctly assert `std::out_of_range` — consistent
  with `.at()`-based bounds checking, matching the project's own established convention for this
  specific collection (confirmed correct, per the sibling `effects-infra` production fork's own
  MEDIUM finding scope — that finding is about the *exception type* used, not about whether bounds
  are checked at all; bounds-checking itself is present and correct here).
- `IndexByNameIsCaseSensitive`/`GetParameterBySemanticIsCaseSensitive` and their
  first-match-wins-on-duplicates counterparts are genuinely valuable, easy-to-miss edge cases
  explicitly tested and cross-referenced to specific FNA source behavior (ordinal, case-sensitive
  `name.Equals` scan, first match wins, no dedup/ambiguity check) via inline comments.
- `IterationOrderMatchesInsertionOrder` deliberately uses non-alphabetical names ("Zebra", "Apple",
  "Mango") specifically so an accidental sort would be caught rather than masked — a subtle,
  well-reasoned test design choice.

## Detailed Findings
None.

## Cross-File Observations
`PointerStableAcrossReallocatingAdd`'s Task 884 fix (and Task 355's earlier
`EffectTechniqueCollection` fix it references) demonstrates a real, recurring class of bug in this
codebase (by-value `std::vector<T>` storage invalidating previously-returned pointers/references on
reallocation) that was found and fixed at least twice — worth checking whether any other
`Collection`-suffixed type in this project shares the same by-value storage pattern and hasn't yet
been audited for this specific hazard.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
This file's case-sensitivity, duplicate-handling, and pointer-stability coverage goes well beyond
basic happy-path testing — a strong example of a test suite actively hunting for edge cases rather
than just confirming the obvious.

## Final Assessment
No findings.
