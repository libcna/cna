# Audit: examples/demo_xact/src/XactFileGen.hpp

## Metadata
- Source file: `examples/demo_xact/src/XactFileGen.hpp` (389 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-demo_xact` shard
- File type: header-only binary-format generator (`.xgs`/`.xwb`/`.xsb`)
- XNA/FNA relevance: hand-constructs the exact on-disk XACT binary layouts that CNA's own
  `XactParser.cpp` (part of the already-audited `cna-internal-core`/`tests-cna-internal` shards)
  parses
- Related production code: `XactParser.cpp` (not re-read in this pass; already audited elsewhere),
  `XactParserTests.cpp`/`XactParserFuzzTests.cpp` (already audited in `tests-cna-internal`)

## Purpose
Generates a minimal, self-contained XACT project (one `WaveBank` with 4 sine-tone waves, one
`SoundBank` with one simple cue per wave, one `AudioEngine` settings file with 2 categories and 1
global variable) entirely in-process, so `demo_xact` needs no shipped binary fixtures.

## Executive Verdict
Correct — the one non-trivial, independently-checkable technical claim in this file (the WaveBank
entry format's bit-packed `fmt` field, line 223 in the sibling `XactDemo.cpp`... actually defined
in-context here via `MakeXwb`) was verified against the real XACT `MiniWaveFormatEx` bit layout
rather than taken on faith.

## Checklist Results
- **`fmt = (sampleRate << 5) | (1u << 31)` (mono PCM 16-bit) is bit-accurate.** The real XACT
  `MiniWaveFormatEx` 32-bit layout is: bits 0-1 `wFormatTag` (0=PCM), bits 2-4 `nChannels-1` (3
  bits), bits 5-22 `nSamplesPerSec` (18 bits), bits 23-30 `wBlockAlign` (8 bits), bit 31
  `wBitsPerSample` (0=8-bit, 1=16-bit) — 2+3+18+8+1 = 32 bits total. With `wFormatTag=0` and
  `nChannels-1=0` (mono) both contributing zero at bits 0-4, the sample rate correctly begins at bit
  5, exactly matching this generator's `sampleRate << 5`; and `1u << 31` correctly sets the
  16-bit-sample flag. `wBlockAlign` is left at the implicit `0` from the base sample-rate value
  rather than being explicitly computed as `channels × bytesPerSample` — acceptable for a
  PCM-only generator, since PCM frame size is directly derivable from channel count and bit depth
  without needing `wBlockAlign` (unlike compressed formats such as ADPCM, where block alignment is
  load-bearing).
- `w16`/`w32`/`wf32`/`ws32` (lines 18-36) all correctly write little-endian, matching the documented
  on-disk XACT byte order; `wf32`'s `float`→`uint32_t` bit-reinterpretation via `std::memcpy` (not
  `reinterpret_cast`, avoiding strict-aliasing UB) is the correct, safe way to write a raw IEEE-754
  bit pattern.
- `MakeXgs()`'s own layout-comment header (lines 89-95) is cross-checked field-by-field against the
  actual `w32`/`w16`/`w8` call sequence immediately below it and matches exactly, including the
  `assert(out.size() == 136)` self-check (line 162) and the equivalent `assert(out.size() == HDR)`
  mid-point self-check in `MakeXsb()` (line 338) — both are real, load-bearing sanity checks on the
  hand-computed offset arithmetic, not decorative comments.
- `MakeSineWave`'s fade envelope (lines 69-73, a 20ms linear fade-in/out) avoids a click at wave
  boundaries, matching the same technique independently used in the sibling `SoundDemo.cpp`'s own
  `GenerateSineBuffer`.
- `SaveFile` (lines 381-388) throws `std::runtime_error` on a failed file open — a real, checked
  error path, not a silently-ignored write failure.

## Detailed Findings
None.

## Cross-File Observations
This generator's own header comment ("The formats produced match exactly what `XactParser.cpp`
expects") is a claim this audit could partially, independently corroborate: the one field checked in
depth (the WaveBank `MiniWaveFormatEx` bit-packing) matches the real, documented XACT on-disk format
exactly, which is a stronger property than merely "self-consistent with whatever CNA's own parser
happens to expect" — it suggests CNA's `XactParser.cpp` itself correctly implements the real format
(consistent with that file's own shard being audited with no HIGH findings), not merely a
CNA-private format that happens to round-trip through CNA's own tooling alone.

## Missing or Weak Tests
Not applicable — this is itself effectively test-fixture-generation code; its correctness is
exercised at runtime by every `demo_xact` invocation, and the underlying parser is separately
unit-tested (`XactParserTests.cpp`/`XactParserFuzzTests.cpp`).

## Positive Findings
Genuinely careful binary-format engineering for a demo-support file: hand-verified offset arithmetic
backed by real `assert()` self-checks, little-endian-correct helpers, strict-aliasing-safe float
bit-reinterpretation, and (per the independent verification above) a bit-accurate implementation of
the real XACT `MiniWaveFormatEx` layout rather than an invented approximation.

## Final Assessment
No findings.
