# Audit: src/CNA/Internal/Media/AudioTagParser.cpp

## Metadata
- Source file: `src/CNA/Internal/Media/AudioTagParser.cpp`
- Audit status: AUDITED (full read, 714 lines)
- Subsystem: `cna-internal-core` shard
- File type: C++ implementation
- XNA/FNA relevance: N/A -- NOXNA, entirely CNA-original (see header report)
- Main related tests: not independently located in this pass

## Purpose
Implements the from-scratch Ogg/Opus/FLAC/ID3v2 tag and embedded-art parsers declared in
`AudioTagParser.hpp`, reading raw bytes from audio files placed in the user's Music library.

## Executive Verdict
Needs attention -- one real (if narrowly-reachable) memory-safety finding: an unsigned-integer-overflow
bounds-check bypass in the ID3v2.3 frame-size and FLAC picture-block-length validation, exploitable only on
builds where `size_t` is 32 bits (e.g. 32-bit Android ABIs, Emscripten/wasm32) with a maliciously crafted
audio/image tag. On 64-bit desktop builds the same code is safe, since the arithmetic cannot wrap. Everything
else in this file -- the Ogg page framing, the shared Vorbis-comment-list walker, the UTF-16/Latin-1 decoders,
and the FLAC-picture path's local field-walking -- is correctly bounds-checked.

## Checklist Results

### Memory/resource safety -- confirmed 64-bit-safe pattern used almost everywhere
`ReadOggPages()`, `TryReadVorbisComments()`, `ParseVorbisCommentList()` (the function shared by all three
Vorbis-comment-list-based readers), and `TryReadFlacComments()`'s block-header walk all follow the same
correct idiom: read a length field, then reject with `pos + len > buffer.size()` before advancing `pos` by
`len`. On any platform where `size_t` is 64-bit (all of this project's primary desktop targets), this is
airtight: `len`'s maximum value (a `uint32_t`, ~4.29e9) can never make `pos + len` wrap a 64-bit `size_t`
for any realistic file size, so an oversized length is always correctly rejected before any read occurs.

### HIGH-severity finding, 32-bit-only: unsigned-overflow bounds-check bypass in ID3v2.3 frame parsing
`TryReadId3v2()` (lines 423-514) and the ID3v2 branch of `ExtractEmbeddedArt()` (lines 613-658) compute a
frame's `frameSize` for ID3v2.3-and-earlier tags as a **plain 32-bit big-endian** value (lines 461-464,
638-641) -- unlike ID3v2.4's synchsafe (28-bit-max) encoding, this can be any value up to `0xFFFFFFFF`. The
guard `if (frameSize == 0 || pos + frameSize > tagEnd) break;` (line 468; equivalently at line 644) is only
safe if `pos + frameSize` cannot wrap. On a 64-bit `size_t` build it cannot (see above). **On a build where
`size_t` is 32 bits**, `pos + frameSize` computed entirely in 32-bit unsigned arithmetic CAN wrap to a small
value that passes the `> tagEnd` check even though `frameSize` itself is enormous (e.g. `pos=10,
frameSize=0xFFFFFFF8` wraps to `2`). Because the check passes, the code proceeds to use the original
(un-wrapped, enormous) `frameSize` as a loop bound with no further bounds check against the actual buffer:
the POPM handler's `while (q < frameSize && fileBytes[pos + q] != 0) ++q;` (line 479) and
`DecodeId3TextFrame()`'s callees (`Utf16ToUtf8`/`Latin1ToUtf8`, which trust the `size`/`textLen` they're
given) would then walk `fileBytes[pos + q]` for `q` far beyond the vector's actual allocation -- a genuine
out-of-bounds heap read, not merely a logic error, continuing until it happens to hit a zero byte or the
process reads into an unmapped page and crashes. The identical wraparound shape exists in
`ExtractEmbeddedArt()`'s FLAC METADATA_BLOCK_PICTURE path (`p + mimeLen + 4 > bytes.size()`,
`p + descLen + 20 > bytes.size()`, `p + dataLen > bytes.size()`, lines 686/689/694) for the same 32-bit-only
reason.

Reachable via a `.mp3` or `.flac` file placed anywhere under the user's scanned Music library (an ID3v2.3
tag with a maliciously large, non-synchsafe frame-size field, or a FLAC picture block with an oversized
big-endian length field) -- entirely attacker-controlled input if such a file is downloaded/shared and later
indexed by `MediaLibraryIndex`. Severity is scored HIGH for the affected configuration (genuine OOB heap
read/crash) but the practical blast radius is narrower than a typical HIGH finding because it requires a
32-bit `size_t` target; on this project's primary desktop (Linux/Windows/macOS, all 64-bit) builds, the
identical code is safe. CNA does target Android, which historically includes 32-bit ABIs (armeabi-v7a) --
worth confirming against the project's actual minimum supported Android ABI before triage.

**Fix shape**: validate `frameSize`/`blockLen`/`mimeLen`/`descLen`/`dataLen` against
`tagEnd - pos`/`bytes.size() - p` (a subtraction bounded to be non-negative first, or an explicit
`size_t`-widening compare against a 64-bit intermediate) rather than `pos + len > bound`, matching the
integer-domain-first validation style this project's own `XactParser.cpp` already uses elsewhere in this
same shard (AUDIO-PARSER-001) -- that fix was specifically for the identical class of pointer/length-
arithmetic bug.

### Everything else: correctly bounds-checked
`ReadOggPages()`'s segment-table/payload-size accumulation (max 255*255, no overflow risk regardless of
`size_t` width), `Utf16ToUtf8()`/`Latin1ToUtf8()`'s NUL-terminator-seeking loops (bounded by `byteLen`/`len`
directly, no attacker-controlled length field involved), and `ParseApicBody()`'s own internal MIME/
description-terminator walk (also self-bounded by `size`, independently re-verified correct by hand-tracing
the `p + 2 >= size` guard before the pictureType-byte read) are all safe regardless of `size_t` width.

### FNA/XNA parity
N/A -- entirely NOXNA, CNA-original functionality; there is no XNA/FNA reference behavior to diverge from.

## Detailed Findings

1. **[HIGH, 32-bit-only] Unsigned-overflow bounds-check bypass in ID3v2.3/FLAC-picture length validation**
   (see above). Files: `TryReadId3v2()` lines 423-514, `ExtractEmbeddedArt()` lines 604-713.

## Cross-File Observations
Contrast with `XactParser.cpp` (already audited this shard): that file was explicitly hardened against this
exact bug class per a cited external audit (AUDIO-PARSER-001). `AudioTagParser.cpp` was written using the
more common (but width-dependent) `pos + len > bound` idiom instead -- the two files in the same shard show
different levels of hardening maturity against the identical vulnerability class.

## Missing or Weak Tests
No malformed/adversarial-input test coverage (oversized ID3v2.3 frame-size fields, oversized FLAC picture-
block length fields) located in this pass -- exactly the kind of input that would need a 32-bit build (or a
sanitizer that models the intended target's `size_t` width) to actually demonstrate the finding above.

## Positive Findings
The synchsafe-vs-plain frame-size distinction (ID3v2.4 vs 2.3) is correctly implemented; the shared
`ParseVorbisCommentList()` helper is correctly reused (and correctly bounds-checked) by all three
Vorbis-comment-list-based readers (Ogg-Vorbis, Ogg-Opus, native FLAC); the UTF-16 surrogate-pair-to-UTF-8
encoder in `Utf16ToUtf8()` is independently correct.

## Final Assessment
One HIGH-severity (32-bit-target-only) finding: unsigned-integer-overflow bounds-check bypass in ID3v2.3 and
FLAC-picture length validation, in `TryReadId3v2()` and `ExtractEmbeddedArt()`. Safe on all 64-bit desktop
targets.
