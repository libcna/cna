# Audit: tests/CNA/Internal/Xnb/CurveContentTypeReaderTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Xnb/CurveContentTypeReaderTests.cpp` (78 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests `CNA::Internal::Xnb::CurveContentTypeReader` (backs `.xnb`-based loading
  of `Microsoft::Xna::Framework::Curve`), Task XNB-20
- Main related tests: none in this shard

## Purpose
Tests `CurveReader`'s canonical-name registration and a hand-constructed binary payload decoding to
the correct `Curve` object (loop types, key count, and each key's position/value/tangent/continuity
fields).

## Executive Verdict
Correct and appropriately thorough for a single-reader unit test.

## Checklist Results
- `ReadsLoopTypesAndTwoKeysInOrder` uses two keys with DISTINCT, non-overlapping continuity values
  (`Smooth` for key 0, `Step` for key 1) and distinct position/value/tangent values — good practice
  for catching an index-transposition or field-mapping bug that identical/uniform test data would
  mask.
- The test correctly verifies both `CurveKey` scalar fields (position, value) and the tangent
  fields specifically for the second key (which has non-zero tangents), giving coverage of the
  tangent-in/tangent-out fields that the first key's all-zero values would not meaningfully exercise.
- The hand-constructed binary payload directly mirrors the real XNB wire format (loop types as
  int32, key count, then per-key position/value/tangentIn/tangentOut/continuity) — a faithful,
  binary-level test rather than testing the reader through some higher-level abstraction.

## Detailed Findings
None.

## Cross-File Observations
None.

## Missing or Weak Tests
None identified for this file's narrow, well-covered scope.

## Positive Findings
Good choice of non-uniform test values across the two keys to catch field-mapping/ordering bugs.

## Final Assessment
No findings.
