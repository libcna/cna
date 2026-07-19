# Audit: include/CNA/Internal/Xnb/XnbTypeReaderTable.hpp

## Metadata
- Source file: `include/CNA/Internal/Xnb/XnbTypeReaderTable.hpp`
- Audit status: AUDITED (full read, 85 lines, header-only)
- Subsystem: `cna-internal-core` shard (Xnb)
- File type: C++ header (header-only implementation)
- XNA/FNA relevance: matches FNA's `ContentTypeReaderManager.LoadAssetReaders`
- Main related tests: not independently located in this pass

## Purpose
Parses a `.xnb` file's type-reader table (a 7-bit-encoded count, then per-entry a length-prefixed name and
a 4-byte version), normalizing each entry's raw type name via `XnbTypeName`.

## Executive Verdict
Needs attention -- correct exception-translation design (wraps `XnbTypeName`'s lower-level
`std::invalid_argument` into the pipeline's own `ContentLoadException`, so callers only ever need to catch
one exception type), but its `reader.ReadString()` call for each entry's raw name does not pass
`XnbReadLimits::maxStringBytes` at all -- the concrete call site backing that limit's "declared but never
enforced" finding (see `XnbReadLimits.hpp`'s own report).

## Checklist Results

### Count validation: correct
`count < 0 || count > limits.maxTypeReaderCount` is checked before `table.reserve(count)` -- correctly
fails fast rather than attempting a corrupted-count-driven reservation.

### Exception translation: correct, and a good design choice
`XnbTypeName`'s `std::invalid_argument` (a lower-level, parser-internal exception type) is caught and
re-thrown as `ContentLoadException` with the offending name and file path -- exactly the uniform
"one exception type for malformed `.xnb` content" contract this pipeline aims for elsewhere (`XnbHeader.hpp`,
`XnbDecompression.cpp`).

### MEDIUM: `entry.rawName = reader.ReadString();` does not enforce `limits.maxStringBytes`
See `XnbReadLimits.hpp`'s own report for the full analysis -- this is the concrete, clearest call site that
should have consulted `limits.maxStringBytes` but doesn't. Some protection still exists via
`BinaryReader::ReadString()`'s own seekable-stream remaining-length clamp (bounding the allocation to what
the whole file could physically contain, i.e. up to `maxFileSize`), but that is materially coarser than the
1MB `maxStringBytes` this file's own `limits` parameter is supposed to enforce.

## Detailed Findings

1. **[MEDIUM] `maxStringBytes` not enforced on the type-reader-name read** -- see above and
   `XnbReadLimits.hpp`'s report. Line 67.

## Cross-File Observations
This is one of two concrete unenforced-limit sites found in this shard (`maxStringBytes` here,
`maxObjectNestingDepth` in `XnbTypeName.hpp`'s recursive parser) -- together they undermine two of the
seven bounds `XnbReadLimits` declares.

## Missing or Weak Tests
Not independently located in this pass; a test with an oversized (but still within-file-size-bound) claimed
string length for a type-reader name would confirm the current (coarser-than-intended) behavior.

## Positive Findings
Correct count validation and correct, uniform exception translation.

## Final Assessment
One MEDIUM-severity finding: `maxStringBytes` is not enforced on this file's own type-reader-name read.
