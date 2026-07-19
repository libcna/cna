# Audit: tests/Microsoft/Xna/Framework/CurveKeyCollectionTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/CurveKeyCollectionTests.cpp` (196 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-framework-core` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Microsoft::Xna::Framework::CurveKeyCollection`
- Main related tests: N/A (this IS a test file)

## Purpose
Tests `CurveKeyCollection`'s position-sorted `Add`, `Contains`/`Remove`/`RemoveAt`/`Clear`/`Clone`/
`IndexOf`, the position-aware `setItemProperty` (in-place replace vs. reinsert-on-position-change),
`CopyTo`, out-of-range/negative-index throws, and range-for iteration.

## Executive Verdict
Thorough, with correct, non-obvious behavioral coverage of the position-sorted insert/replace
semantics (`AddSortsAscendingByPosition`, `SetItemPropertyDifferentPositionReinserts`). All
throw-testing assertions use raw `std::out_of_range` rather than this project's own
`System::ArgumentOutOfRangeException`.

## Checklist Results
- `RemoveAtOutOfRangeThrows`, `SetItemPropertyOutOfRangeThrows`, `CopyToNegativeIndexThrows`,
  `CopyToArrayTooSmallThrows`, `GetItemPropertyOutOfRangeThrows` all assert `std::out_of_range` —
  consistent with this session's already-established cross-cutting pattern (raw `std::` exceptions
  used instead of this project's own `System::` exception types), very likely because
  `CurveKeyCollection`'s own implementation genuinely throws these raw types (a production-level
  observation, not a test-authoring defect — the tests correctly reflect actual behavior).
- `CloneIsIndependent` correctly verifies a deep-enough copy (clearing the clone doesn't affect the
  original) rather than just checking initial value equality.

## Detailed Findings
None new in this test file itself (see Cross-File Observations for the broader pattern this file
contributes another data point to).

## Cross-File Observations
Adds further instances to this session's already-tracked cross-cutting exception-type pattern
(recurring `std::` exception usage instead of `System::` exception types) — worth including
`CurveKeyCollection`'s throwing methods in whatever follow-up sweep addresses that pattern
project-wide.

## Missing or Weak Tests
Not otherwise identified — coverage is comprehensive.

## Positive Findings
The position-sorted insert/replace semantics are meaningfully and correctly tested, not just
assumed.

## Final Assessment
No new findings; contributes further confirmed instances to the already-tracked project-wide
exception-type cross-cutting pattern.
