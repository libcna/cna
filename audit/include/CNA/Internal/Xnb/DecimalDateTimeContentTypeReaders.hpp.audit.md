# Audit: include/CNA/Internal/Xnb/DecimalDateTimeContentTypeReaders.hpp

## Metadata
- Source file: `include/CNA/Internal/Xnb/DecimalDateTimeContentTypeReaders.hpp`
- Audit status: AUDITED (full read, 75 lines)
- Subsystem: `cna-internal-core` shard (Xnb)
- File type: C++ header
- XNA/FNA relevance: matches FNA's `DecimalReader`/`TimeSpanReader`/`DateTimeReader`
- Main related tests: not independently located in this pass

## Purpose
Declares `.xnb` readers for `System.Decimal`/`System.TimeSpan`/`System.DateTime`, with `DecimalReader`
conditionally compiled out on MSVC (matching `System::Decimal`'s own `unsigned __int128`
GCC/Clang-only requirement).

## Executive Verdict
Healthy -- one intentional, clearly-documented FNA deviation (DateTimeKind discarded), correctly reasoned
as a pre-existing sharp-runtime limitation rather than a `.xnb`-specific gap.

## Checklist Results

### FNA parity: bit-masking verified correct
`DateTimeReader::Read()`'s `uint64_t` decoding (top 2 bits = `DateTimeKind`, bottom 62 bits = ticks) is
independently verified against .NET's real internal `DateTime` binary representation
(`DateTime.ToBinary()`'s documented layout) -- `kindMask = 3 << 62` correctly isolates the top 2 bits, and
`~kindMask` correctly isolates the remaining 62 bits for the tick count.

### Intentional deviation: documented per project convention
`DateTimeKind` is parsed (consuming the correct number of bits) but discarded, since `System::DateTime` is
documented `Status: Partial` and has no field to store it -- correctly framed as a pre-existing
sharp-runtime limitation the reader itself can't fix in scope, not silently swallowed.

### Platform-conditional compilation: correctly scoped
`#if !defined(_MSC_VER)` correctly gates only `DecimalReader` (and its registration in the paired `.cpp`)
rather than the whole file, matching `System::Decimal`'s own stated MSVC-unsupported requirement.

## Detailed Findings
None.

## Cross-File Observations
N/A.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Clear, honest documentation of the DateTimeKind-discarding deviation with its actual root cause identified
(sharp-runtime, not this reader).

## Final Assessment
No issues found.
