# Audit: include/CNA/Internal/Xnb/XnbHeader.hpp

## Metadata
- Source file: `include/CNA/Internal/Xnb/XnbHeader.hpp`
- Audit status: AUDITED (full read, 159 lines, header-only)
- Subsystem: `cna-internal-core` shard (Xnb)
- File type: C++ header (header-only implementation)
- XNA/FNA relevance: matches FNA's `ContentManager.GetContentReaderFromXnb` header-parsing logic
  (`src/Content/ContentManager.cs`)
- Main related tests: not independently located in this pass

## Purpose
Parses and validates the 10-byte `.xnb` container header (magic bytes, platform identifier, version,
compression flags, total length).

## Executive Verdict
Healthy -- correct, careful FNA-parity port with a well-reasoned, harmless intentional deviation.

## Checklist Results

### FNA parity: correct, deviation documented and justified
`XnbAcceptedPlatforms()` matches FNA's own `ContentManager.targetPlatformIdentifiers` verbatim, including
deprecated identifiers FNA itself still accepts. `XnbCompression` is deliberately a real enum (not a bool),
matching the fact that MonoGame's content pipeline can emit an LZ4-compressed payload FNA/this project
doesn't yet decode -- a `Lz4`/`Unknown` distinction lets callers give an accurate error rather than silently
misinterpreting the payload as LZX.

The one documented intentional deviation from FNA (reading each header byte through the `BinaryReader`
individually rather than one raw multi-byte `Stream.Read`, so a truncated file throws
`EndOfStreamException` instead of comparing against garbage/leftover bytes) is correctly reasoned as
strictly a *clearer* failure mode for an input case a well-formed file could never trigger anyway -- not an
actual behavioral divergence for any real `.xnb` file.

### Bounds safety
Version/platform/compression-flag validation all reject before any size-driven allocation happens later in
the pipeline (that's `XnbReadLimits`' job, audited separately in this shard) -- this file's own job (header
field validation) is complete and correct.

## Detailed Findings
None.

## Cross-File Observations
Feeds directly into `XnbReadLimits`/`XnbDecompression` (both audited in this shard) for the actual
size-bounded decompression that follows header parsing.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Careful, well-justified single intentional deviation from FNA's own byte-reading style; correct inclusion
of FNA's own deprecated-platform-identifier backward-compatibility list.

## Final Assessment
No issues found.
