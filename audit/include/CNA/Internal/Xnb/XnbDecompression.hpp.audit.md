# Audit: include/CNA/Internal/Xnb/XnbDecompression.hpp

## Metadata
- Source file: `include/CNA/Internal/Xnb/XnbDecompression.hpp`
- Audit status: AUDITED (full read, 42 lines)
- Subsystem: `cna-internal-core` shard (Xnb)
- File type: C++ header
- XNA/FNA relevance: matches FNA's block-framing loop in `ContentManager.GetContentReaderFromXnb`
- Main related tests: not independently located in this pass

## Purpose
Declares the LZX block-framing decompression entry point consuming `XnbReadLimits` bounds.

## Executive Verdict
Healthy -- see the paired `.cpp` for the full block-framing loop and its interaction with `LzxDecoder`'s own
bounds checks.

## Checklist Results
Clearly documents the block-framing format (32KB default frames, `0xFF`-prefixed explicit-size frames) and
correctly notes the decoder's state (sliding window, repeated-offset queue) persists across blocks within
one file, matching LZX's inherently sequential, non-seekable decompression model.

## Detailed Findings
None in this header -- see the paired `.cpp`.

## Cross-File Observations
Depends on `XnbReadLimits` (audited, correct) for the two size bounds it enforces before ever touching
`LzxDecoder`.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
N/A (see .cpp).

## Final Assessment
No issues found.
