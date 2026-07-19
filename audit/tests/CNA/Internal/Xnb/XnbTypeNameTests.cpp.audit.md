# Audit: tests/CNA/Internal/Xnb/XnbTypeNameTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Xnb/XnbTypeNameTests.cpp` (97 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests `CNA::Internal::Xnb::NormalizeXnbTypeReaderName`/`ParseXnbTypeName`
  (backs `.xnb` type-reader name resolution for every content type), Tasks XNB-13/XNB-13A
- Main related tests: foundational for `XnbBuiltInReaderRegistrationTests.cpp`/
  `XnbTypeReaderTableTests.cpp` (read separately, same folder)

## Purpose
Tests the .NET assembly-qualified type name parser/normalizer: stripping assembly-qualification
info, handling already-bare names, and correctly parsing nested generic type arguments (one level
and two levels deep, e.g. `Dictionary<string, List<int>>`), plus malformed-bracket error handling.

## Executive Verdict
Correct and thorough for a genuinely fiddly string-parsing problem — .NET's assembly-qualified
generic type name grammar has real nesting/bracket-matching complexity, and this file tests it at
increasing levels of real depth (plain name → one-level generic → doubly-nested generic) rather
than stopping at the simplest case.

## Checklist Results
- `DoublyNestedGenericDictionaryOfStringToListOfInt` and its companion
  `DoublyNestedGenericParsedFieldsAreCorrect` correctly test a genuinely representative real-world
  case (a `Dictionary<string, List<int>>`-shaped XNB type reader name) with full assembly
  qualification noise on every level — this is exactly the kind of nested-generic case that a naive
  single-level bracket parser would mishandle.
- `OneLevelGenericParsedFieldsAreCorrect`/`DoublyNestedGenericParsedFieldsAreCorrect` verify the
  STRUCTURED parse result (`baseName`, `genericArguments` recursively) in addition to the
  string-normalization result tested by the sibling `Normalize*` tests — testing both the
  string-level and structured-data-level outputs of the same underlying parse gives more complete
  confidence than testing only one representation.
- `UnbalancedBracketsThrowsInvalidArgument`/`MissingOpenBracketForArgumentThrowsInvalidArgument`
  correctly test two DIFFERENT malformed-bracket failure modes (an unclosed bracket; a missing open
  bracket for a generic argument) with the appropriate exception type
  (`std::invalid_argument` — a reasonable, standard choice for malformed caller-supplied string
  input) rather than a single generic "throws on garbage" test.
- `AlreadyBareNamePassesThroughUnchanged` correctly tests the identity/no-op case separately from
  the stripping case, confirming the normalizer doesn't accidentally mangle already-clean input.

## Detailed Findings
None.

## Cross-File Observations
This file's parser is foundational to `XnbBuiltInReaderRegistrationTests.cpp`'s and
`XnbTypeReaderTableTests.cpp`'s correct type-reader lookup — a real, load-bearing utility correctly
tested in isolation before being relied upon by higher-level registration tests elsewhere in this
folder.

## Missing or Weak Tests
None identified — the nested-generic and malformed-input coverage is appropriately thorough for a
non-trivial string grammar.

## Positive Findings
The progressive depth (plain → one-level generic → doubly-nested generic) and the structured-result
verification alongside the string-normalization verification are both good, complete test-design
choices for a recursive parsing problem.

## Final Assessment
No findings.
