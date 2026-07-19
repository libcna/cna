# Audit: src/CNA/Internal/Xnb/XnbDecompression.cpp

## Metadata
- Source file: `src/CNA/Internal/Xnb/XnbDecompression.cpp`
- Audit status: AUDITED (full read, 89 lines)
- Subsystem: `cna-internal-core` shard (Xnb)
- File type: C++ implementation
- XNA/FNA relevance: matches FNA's `ContentManager.GetContentReaderFromXnb`'s LZX block-framing loop
  byte-for-byte
- Main related tests: not independently located in this pass

## Purpose
Implements the LZX block-framing loop: reads each block's (optionally-explicit) frame/block size, feeds it
to a single `LzxDecoder` instance, re-syncs the input stream position after each block, and verifies the
final decompressed length matches the header's claim.

## Executive Verdict
Healthy -- independently traced the block-size/stream-length interaction that could in principle allow a
corrupted `block_size` field to request more bytes than remain, and confirmed it cannot cause an
out-of-bounds read: the loop's own bound (`compressedSize`) is set to exactly the real backing
`MemoryStream`'s length, so `pos < compressedSize` and "more real bytes exist" are the same condition by
construction -- the only residual risk is what `LzxDecoder::Decompress()` itself does when asked to consume
more bytes than are actually available within a single block, which is correctly rejected inside
`LzxDecoder.cpp` (see that file's own report).

## Checklist Results

### Size validation before any allocation: correct
Both `compressedSize`/`decompressedSize` are validated against `limits.maxFileSize`/`maxDecompressedSize`
(and against negative values) before `MemoryStream`/`LzxDecoder` construction -- correctly fails fast rather
than attempting a bounds-exceeding allocation.

### Block-framing loop: correctly bounded, verified by hand-tracing the `pos`/`compressedSize` relationship
`compressedStream` is constructed directly over `(compressedData, compressedSize)`, meaning its real
`Length` always exactly equals the loop's own `compressedSize` bound. This means `while (pos < compressedSize)`
and "there is still at least one more real byte in the stream" are the *same* condition -- there is no
window where the loop's manual `pos` bookkeeping could exceed the stream's actual backing length while the
loop condition still permits another iteration. The residual question (an in-block `block_size` claiming
more bytes than remain within the overall `compressedSize` bound) is correctly handled by `LzxDecoder`'s own
internal bounds checks on `inLen` (see that file's report) rather than by this loop.

### Post-decompression length check: correct
`decompressedStream.getPositionProperty() != decompressedSize` after the loop correctly rejects any
mismatch between the claimed and actually-produced decompressed length -- catches both an
under-decompression and any (already-otherwise-impossible) over-run.

## Detailed Findings
None.

## Cross-File Observations
The "reset input position after each block... in case the bit buffer read in some unused bytes" comment
(lines 77-78) is corroborated directly by `LzxDecoder::BitBuffer`'s own 16-bit-at-a-time read-ahead
behavior (audited in `LzxDecoder.cpp`) -- a genuine, correctly-handled interaction between the two files,
not just an assumption.

## Missing or Weak Tests
Not independently located in this pass; a test feeding a `block_size` field that claims more bytes than
remain in a truncated/corrupt compressed payload would directly exercise the boundary case reasoned through
above.

## Positive Findings
Faithful, byte-for-byte port of FNA's own block-framing loop, with the two size checks
(`XnbReadLimits`-driven) that FNA's own C# implementation doesn't need but a C++ port genuinely does.

## Final Assessment
No issues found.
