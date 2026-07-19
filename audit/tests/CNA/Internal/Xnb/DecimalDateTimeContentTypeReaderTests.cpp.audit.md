# Audit: tests/CNA/Internal/Xnb/DecimalDateTimeContentTypeReaderTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Xnb/DecimalDateTimeContentTypeReaderTests.cpp` (105 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests `CNA::Internal::Xnb::DecimalDateTimeContentTypeReaders` (backs `.xnb`
  readers for `System::Decimal`/`System::TimeSpan`/`System::DateTime`), Tasks XNB-18B/XNB-18C
- Main related tests: none in this shard

## Purpose
Tests registration and binary round-tripping for all three readers: `Decimal`'s 4-int32 layout
(lo/mid/hi/flags with scale extraction), `TimeSpan`'s raw tick count, and `DateTime`'s packed
64-bit value with `DateTimeKind` bits masked out of the tick count.

## Executive Verdict
Correct, with one unexplained platform-conditional gap: both the `DecimalReader` registration
check and its round-trip test are wrapped in `#if !defined(_MSC_VER)` with no comment explaining
why — every other conditional-compilation guard encountered elsewhere in this audit (Emscripten
SOCKFS limitations, ASYNCIFY requirements, etc.) has been accompanied by a specific, documented
reason. This one is not.

## Checklist Results
- `DateTimeReaderExtractsTicksMaskingOutKindBits` correctly constructs a packed 64-bit value with
  BOTH a realistic tick count AND non-zero kind bits (`DateTimeKind.Utc` packed into the top 2
  bits) in the same test value, verifying the reader correctly masks out the kind bits rather than
  merely testing an all-zero-kind case that a masking bug could accidentally pass.
- `DecimalReaderRoundTrips` correctly verifies both the `lo` component AND the extracted scale
  (bits 16-23 of the flags field) — a meaningful two-part check of the decimal's compound bit
  layout, not just a single field.
- The `MakeReader`/`ReadViaRegisteredReader` helpers correctly manage stream lifetime (storing
  each `MemoryStream` in a member vector so the `ContentReader` returned to the caller remains
  valid) — a real, easy-to-get-wrong lifetime detail for a helper returning a reader over a stream
  built inside the helper itself.

## Detailed Findings
- **LOW**: The `#if !defined(_MSC_VER)` guard around BOTH the `DecimalReader` registration check
  (`AllThreeReadersAreRegistered`, which is consequently misleadingly named — it only actually
  checks two readers on MSVC) and the entire `DecimalReaderRoundTrips` test has no explanatory
  comment anywhere in the file. Every other platform-conditional test file encountered in this
  audit documents its specific reason (an Emscripten SOCKFS limitation, an ASYNCIFY requirement,
  etc.); this one gives no indication whether `DecimalReader` is genuinely unregistered on MSVC
  (a real, permanent platform difference worth documenting), a known-broken/TODO gap, or simply an
  environment-specific test-authoring convenience. This doesn't block auditing the test's logic,
  but the missing rationale is itself a small process gap worth flagging for whoever maintains this
  file next — without cross-referencing the actual `DecimalDateTimeContentTypeReaders.cpp`/`.hpp`
  source (out of scope for this test-file audit pass), it cannot be determined here whether this
  reflects a real, intentional platform limitation or an oversight.

## Cross-File Observations
None.

## Missing or Weak Tests
The `AllThreeReadersAreRegistered` test's name promises coverage the MSVC build path does not
actually get (see the LOW finding above) — a minor naming/documentation mismatch rather than a
missing test per se.

## Positive Findings
The DateTime kind-bit-masking test's deliberate inclusion of non-zero kind bits alongside a
realistic tick count is a good, non-trivial bit-layout verification choice.

## Final Assessment
One LOW-severity documentation gap: the MSVC-only `DecimalReader` test/registration exclusion has
no stated rationale anywhere in the file.
