# Audit: tests/CNA/Internal/JsonTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/JsonTests.cpp` (137 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `CNA::Internal::ParseJson`/`JsonValue`/`JsonParseException`
  (CNA-internal minimal JSON parser backing `.cnj` envelope parsing, no direct FNA equivalent)
- Main related tests: N/A (this IS a test file); complements `CnjEnvelopeTests.cpp` (already audited
  this session), which tests the `.cnj`-specific envelope layer built on top of this parser

## Purpose
Tests the underlying general-purpose JSON parser: object/array/string/number/boolean/null parsing,
string-escape decoding (including Unicode `\uXXXX` and UTF-16 surrogate pairs), and malformed-input
rejection.

## Executive Verdict
Correct, with genuinely careful string-encoding coverage. `DecodesUnicodeEscape`/
`DecodesSurrogatePairEscape` both verify the *exact byte count and specific leading bytes* of the
resulting UTF-8 encoding (not just "did it not throw" or "is the string non-empty") — correctly
distinguishing a 2-byte UTF-8 sequence (U+00E9) from a 4-byte one (U+1F600, requiring surrogate-pair
combination first) with concrete, verifiable byte-level assertions.

## Checklist Results
- `DecodesBasicStringEscapes` covers all seven standard JSON escape sequences (`\"`, `\\`, `\/`,
  `\b`, `\f`, `\n`, `\r`, `\t`) in a single compact test — thorough without being repetitive.
- `ParsesNumberVariants` covers integer, negative, decimal, positive exponent, and negative exponent
  forms — a reasonable spread of JSON's number grammar.
- The five `Rejects*` tests (trailing garbage after a number, unterminated string, invalid escape,
  trailing content after the root value, empty document, missing comma) each target one specific,
  distinct malformed-input class rather than one generic "garbage in, exception out" test — this
  gives real confidence the parser's error paths are individually exercised, not just that the
  happy path avoids them.
- `RejectsTrailingGarbageAfterNumber`'s fixture (`{"cnjVersion": 1abc}`) is the exact adversarial
  case `CnjEnvelopeTests.cpp`'s own `TrailingGarbageCnjVersionIsAParseError` test built its
  higher-level assertion on top of — confirms the two files' test coverage is properly layered
  (this file proves the parser itself rejects it; the sibling file proves the envelope layer
  correctly surfaces that as a `ContentLoadException`).
- `ParsesNonObjectRoots` correctly confirms the parser accepts any valid JSON value as the document
  root (array/string/number/boolean/null), not just objects — matching real JSON's grammar (RFC
  8259 permits any value as the root), not an unnecessarily restrictive implementation.

## Detailed Findings
None.

## Cross-File Observations
Directly corroborates and layers correctly with `CnjEnvelopeTests.cpp` (already audited this
session) — this file owns "does the JSON parser itself behave correctly," while that file owns
"does the `.cnj`-specific envelope layer correctly consume and validate this parser's output," with
no overlap or gap between the two responsibilities.

## Missing or Weak Tests
None identified.

## Positive Findings
The byte-level UTF-8 encoding verification for Unicode/surrogate-pair escapes is a genuinely careful
piece of test design — many JSON parser test suites would stop at "doesn't throw" for these cases.

## Final Assessment
No findings.
