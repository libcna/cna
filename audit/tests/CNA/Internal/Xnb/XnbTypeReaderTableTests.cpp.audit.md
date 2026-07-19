# Audit: tests/CNA/Internal/Xnb/XnbTypeReaderTableTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Xnb/XnbTypeReaderTableTests.cpp` (129 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests `CNA::Internal::Xnb::ParseXnbTypeReaderTable` (backs the type-reader
  table every `.xnb` file's header must parse before its body can be decoded), Task XNB-12
- Main related tests: builds on `XnbTypeNameTests.cpp`'s parser (read separately, same folder)

## Purpose
Tests the type-reader table parser: multi-entry parsing in order, the zero-entry edge case, a
resource-limit guard against an oversized declared count, a real externally-produced fixture's
exact byte layout, and malformed-generic-name error propagation.

## Executive Verdict
Excellent. `RealMonoGameFixturePreservesRawNameAlongsideNormalized` uses REAL bytes extracted
byte-for-byte from an actual MonoGame-produced `.xnb` fixture (`white-1.xnb`), not hand-invented
data — genuine ground truth for the exact on-the-wire encoding this parser must handle.

## Checklist Results
- `CountExceedingLimitThrowsContentLoadException` correctly tests a resource-exhaustion guard
  (a declared type-reader count exceeding a configurable limit) with the appropriate project
  exception type (`ContentLoadException`, not a raw `std::` exception) — proactive hardening
  against a crafted/corrupted `.xnb` declaring an implausibly large table before any real entries
  are even read.
- `RealMonoGameFixturePreservesRawNameAlongsideNormalized` verifies BOTH the raw
  (assembly-qualified) name AND the normalized name are preserved together in the same table entry
  — confirming the parser doesn't discard the original string when producing the normalized one,
  which downstream diagnostic/debugging code might reasonably want.
- `MalformedGenericTypeNameThrowsContentLoadExceptionNotInvalidArgument` (Task XNB-43) correctly
  tests a genuinely important exception-translation boundary: the lower-level
  `XnbTypeName`/`ParseXnbTypeName` parser (tested separately in `XnbTypeNameTests.cpp`) throws
  `std::invalid_argument` for a malformed generic name, but this test confirms that gets TRANSLATED
  to `ContentLoadException` at this layer — its own comment correctly explains why this matters: a
  caller catching `ContentLoadException` around `Load<T>()` (the documented, standard error-handling
  pattern) would otherwise miss a raw `std::invalid_argument` leaking through from a lower layer.
  This is a meaningful, specific boundary-translation test, not a redundant re-test of the lower
  parser's own behavior.
- `ParsesTwoEntriesInOrder` correctly uses `Write7BitEncodedInt` (matching the real XNB wire
  format's variable-length integer encoding for the table count) rather than a fixed-width
  integer, and includes one plain name and one generic name — reasonable variety without needing
  exhaustive coverage (already provided by `XnbTypeNameTests.cpp`).

## Detailed Findings
None.

## Cross-File Observations
This file correctly builds on and complements `XnbTypeNameTests.cpp`'s lower-level parser tests
without duplicating them — the malformed-generic-name test here specifically targets the
EXCEPTION-TRANSLATION boundary between the two layers, a distinct and valuable property neither
file's tests alone would cover.

## Missing or Weak Tests
None identified.

## Positive Findings
Using real, byte-for-byte-extracted bytes from an actual MonoGame-produced fixture for the raw/
normalized-name-preservation test is a strong ground-truth verification choice, consistent with
this shard's broader pattern of preferring real external fixtures over hand-invented data where
practical.

## Final Assessment
No findings.
