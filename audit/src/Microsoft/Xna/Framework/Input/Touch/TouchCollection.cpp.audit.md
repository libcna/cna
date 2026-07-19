# Audit: src/Microsoft/Xna/Framework/Input/Touch/TouchCollection.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Input/Touch/TouchCollection.cpp`
- Audit status: AUDITED (full read, 131 lines)
- Subsystem: `xna-input` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Input/Touch/TouchCollection.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements the mutation methods, `FindById()`'s unconditional out-param write, and `CopyTo()`.

## Executive Verdict
Correct, and `CopyTo()`'s deviation from .NET's `List<T>.CopyTo` (which overwrites pre-allocated
slots of a caller-sized fixed array) is honestly disclosed as a necessary consequence of C++'s
destination being a growable `std::vector` rather than a fixed-size array — the C++ version inserts
at `arrayIndex` instead of overwriting, with a clear comment explaining why an index-based
overwrite semantic doesn't map onto a growable container, plus a correctly-added bounds guard
(FNA's real `List<T>.CopyTo`'s implicit bounds check maps to `std::out_of_range` here, since an
invalid `array.begin() + arrayIndex` would otherwise be UB). `FindById()`'s unconditional out-param
write on both the found and not-found paths is confirmed to match FNA exactly.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
None beyond the paired `.hpp` report.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
`CopyTo()`'s deviation is exactly the right way to handle a case where a literal FNA/.NET semantic
doesn't safely map onto C++'s container model — disclosed, bounds-checked, and reasoned through
rather than either silently copied (UB risk) or silently reinterpreted (behavior drift).

## Final Assessment
No findings.
