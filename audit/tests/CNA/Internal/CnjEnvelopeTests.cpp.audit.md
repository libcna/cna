# Audit: tests/CNA/Internal/CnjEnvelopeTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/CnjEnvelopeTests.cpp` (282 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `CNA::Internal::ParseCnjEnvelope`/`ValidateCnjEnvelope` (CNA-internal
  `.cnj` JSON-envelope parsing, no direct FNA equivalent)
- Main related tests: N/A (this IS a test file)

## Purpose
Tests JSON-envelope-level parsing/validation of `.cnj` content files (top-level `cnjVersion`/`type`/
`sourceFile` fields), independent of any specific content-type reader.

## Executive Verdict
A genuinely thorough, carefully-designed test suite that specifically targets real historical
parsing bugs (a naive first-occurrence-anywhere scanner mistaking a nested field for a top-level
one; truncation at an escaped quote; `std::stoi`-style silent partial-number acceptance) rather than
only testing the straightforward well-formed case.

## Checklist Results
- `NestedTypeFieldIsNotMistakenForTopLevelType`/`NestedCnjVersionFieldIsNotMistakenForTopLevelCnjVersion`/
  `RealTopLevelFieldFoundEvenWhenNestedDecoyPrecedesItTextually` are three distinct, well-reasoned
  adversarial cases proving the parser is a real JSON-structure-aware parser, not a naive substring/
  regex scan — each comment explains exactly what a naive scanner would get wrong.
- `EscapedQuoteInTypeIsDecodedNotTruncated` targets a real, specific historical bug (the old
  hand-rolled scanner searching for the next raw `"` byte) with a precise, well-chosen test string.
- `TrailingGarbageCnjVersionIsAParseError` correctly distinguishes "malformed JSON number token"
  from "valid but out-of-range number" — verifying the whole document fails to parse rather than
  silently truncating via `std::stoi`, a real and easy-to-miss distinction.
- `ValidateCnjEnvelopeTest`'s exception tests all correctly assert both the exception type
  (`ContentLoadException`) and, where meaningful, a substring of the message (naming the actual
  mismatched type/version) rather than just the exception type alone — this correctly matches
  `ContentLoadException`'s established role as this project's real content-loading exception type
  (not a raw `std::` exception, consistent with the project's XNA-facing exception-type convention).

## Detailed Findings
None.

## Cross-File Observations
None beyond what's already generally established: JSON parsing throughout this project appears to
consistently use a real JSON library rather than hand-rolled scanning, and this file's adversarial
tests function as a regression suite proving that migration was both completed and correct.

## Missing or Weak Tests
None identified.

## Positive Findings
This is one of the more thoughtfully adversarial test files in this shard — it doesn't just test
"parsing works," it specifically tests "parsing isn't fooled by structurally-similar-but-wrong
input," which is exactly the class of bug that matters for a hand-authored or semi-hand-authored
parser.

## Final Assessment
No findings.
