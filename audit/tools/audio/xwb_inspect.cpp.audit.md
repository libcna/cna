# Audit: tools/audio/xwb_inspect.cpp

## Metadata
- Source file: `tools/audio/xwb_inspect.cpp` (152 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-audio` shard
- File type: C++ CLI tool (inspection/extraction utility)
- XNA/FNA relevance: exercises `CNA::Internal::Audio::ParseXwb`/`WavWrapper`'s `.xwb`-parsing and
  PCM/MS-ADPCM decode path, reusing the same technique `WaveBank.cpp` itself uses
- Main related tests: N/A (standalone inspection tool; never plays anything)

## Purpose
Lists a `.xwb` wave bank's parsed entry metadata as stable JSON and, optionally, exports every
entry's raw payload as a real, playable `.wav` file for offline diagnosis, without spinning up a
full `AudioEngine`/`WaveBank`/`Cue` pipeline.

## Executive Verdict
Correct, with careful bounds- and div-by-zero-checking on untrusted binary input.

## Checklist Results
- The non-streaming export path's bounds check (line 109,
  `static_cast<uint64_t>(e.dataOffset) + e.dataLength <= xwb.fileData.size()`) correctly guards
  against an out-of-bounds slice for a malformed/corrupt `.xwb` file before dereferencing
  `xwb.fileData.data() + e.dataOffset` — a real, necessary defensive check given this tool parses
  externally-supplied binary content with no other validation layer in front of it. The `uint64_t`
  cast on `dataOffset` correctly avoids a 32-bit-arithmetic overflow in the addition for a
  pathological large offset+length pair.
- MS-ADPCM's `avgBytesPerSec`/`totalSamples` calculations (lines 124-127) are both correctly
  guarded against division by zero (`samplesPerBlock > 0`, `blockAlign > 0`) with a sane fallback
  for the guarded-against case, rather than an unguarded divide that could crash on a
  corrupt/degenerate entry.
- XMA/WMA entries are correctly and explicitly left unexported (lines 134-135, "no decode path
  anywhere in this stack (CHECKLIST.md accepted deviation)") rather than silently producing garbage
  output or crashing on an unsupported format.
- JSON output is built incrementally with per-line printf calls rather than buffering the whole
  string — reasonable for a large wave bank with many entries, avoiding an unbounded string-
  concatenation cost, though this means a mid-stream write failure would leave partially-emitted
  JSON on stdout (not flagged as a defect — CLI inspection tools of this kind commonly accept this
  tradeoff).

## Detailed Findings
None.

## Cross-File Observations
Reuses the same `CNA::Internal::Audio::WavWrapper`/`BuildWavFromWaveFormatEx` technique
`WaveBank.cpp` itself uses (per this file's own top comment) — a shared, already-tested decode path
rather than a parallel reimplementation.

## Missing or Weak Tests
No test was located for this tool itself; reasonable given its role as a manual/offline inspection
utility, though a test exercising the bounds-check/div-by-zero-guard paths specifically (a crafted
malformed `.xwb` with an out-of-range `dataOffset`/zero `samplesPerBlock`) would give direct
regression coverage for exactly the defensive code this report calls out as its main positive
finding.

## Positive Findings
The bounds-check and div-by-zero guards on untrusted binary input are genuinely careful, defensive
coding — not something every quick inspection tool bothers to get right.

## Final Assessment
No findings.
