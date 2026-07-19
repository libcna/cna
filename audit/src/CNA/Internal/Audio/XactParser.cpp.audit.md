# Audit: src/CNA/Internal/Audio/XactParser.cpp

## Metadata

- Source file: `src/CNA/Internal/Audio/XactParser.cpp`
- Audit status: AUDITED (scoped-depth review — 1233 lines, the `Ctx` binary-reading helper section (lines
  1-110) and the `ParseXwb` function's opening ~120 lines read in full; the remaining `ParseFirstPlayWave`/
  `ParseXgs`/`ParseXwbStreamingHeader`/`ParseXsb` functions inventoried by signature but not read line-by-line,
  consistent with this audit's scoped-depth standard for files of this size)
- Subsystem: `cna-internal-core` shard
- File type: C++ implementation
- XNA/FNA relevance: parses the 4 XACT binary container formats (XGS/XWB/XSB) backing
  `Microsoft::Xna::Framework::Audio::AudioEngine`/`WaveBank`/`SoundBank`/`Cue`
- Graphics backend relevance: none
- Main related tests: `XactParserTests.cpp`, `XnbContainerFuzzTests.cpp` (both referenced directly by this
  file's own comments)

## Purpose

Implements XGS/XWB/XSB binary parsing: the shared bounds-checked `Ctx` byte-cursor helper, volume-byte-to-
amplitude conversion (FAudio's own centibel formula), and the 4 top-level `Parse*` entry points.

## Executive Verdict

**Healthy — exceptionally security-conscious, already externally audited and fuzz-hardened in the areas
read.**

## Checklist Results

### Confirmed, genuinely hardened: pointer-arithmetic-UB class of bugs
`Ctx::seek()`/`Ctx::skip()` both explicitly validate an offset/length against the buffer's remaining size as a
**plain integer comparison** before ever forming an out-of-bounds pointer (`start + absOffset` or `cur + n`) —
both carry an explicit comment citing a real external audit (`AUDIO-PARSER-001`, dated 2026-07-16) that found
and fixed the previous, UB-per-standard version (`if (start + absOffset > end)`, which forms the
out-of-bounds pointer before checking it — undefined behavior even though it doesn't reliably trip ASan/UBSan
on typical 64-bit targets, per the comment's own honest caveat). `Ctx::cstr()` similarly fixed a real,
previously-shipped one-byte-overrun bug (an unterminated string's `strnlen() == maxlen` case wasn't detected,
letting `cur` advance one byte past `end`, cascading into a genuine out-of-bounds read on the *next* call).

### Confirmed, genuinely hardened: allocation-bomb / DoS class of bugs
`ParseXwb()`'s `entryCount` (a full, unvalidated `uint32_t` read directly from the file) is explicitly
bounds-checked against the file's own remaining byte count before being passed to `result.entries.resize()`
— with a comment explaining precisely why this matters (a virtual-memory-overcommit-enabled huge resize can
hang or trigger the OOM killer rather than cleanly throwing `std::bad_alloc`, a real DoS class, not just a
theoretical one) and citing the project's own established fuzzing-derived policy (`XnbContainerFuzzTests.cpp`:
"an uncaught std::bad_alloc is an allocation-bomb guard gap, not an acceptable outcome").

### Confirmed, genuinely hardened: another real, empirically-reproduced ASan finding
The 64-byte bank-name string read in `ParseXwb()` explicitly caps its `strnlen()` scan to the real remaining
buffer size (not a hardcoded 64) — the comment cites a real, standalone ASan heap-buffer-overflow repro against
a deliberately truncated fixture, not a theoretical concern.

### A genuine, deliberate non-replication of an upstream reference bug
The compact-XWB-entry length calculation's own comment explicitly documents that it does NOT replicate a real,
long-standing bug in the actual FAudio reference source (`FACT_internal.c`'s non-last-entry length computation
always yields zero due to subtracting an offset from itself) — CNA computes a real length from the gap to the
next entry's offset instead, with 2 named regression tests (`CompactWaveBankComputesLengthsFromConsecutiveOffsets`/
`CompactWaveBankLastEntryLengthIgnoresItsOwnDeviation`) backing the deliberate divergence.

### C++ correctness / Memory/resource lifetime / Performance / Portability / Maintainability / Robustness
No issues found in the areas read — every finding above is itself evidence of a mature, already-hardened
codepath, not a newly-discovered defect.

## Detailed Findings

None — this pass corroborates an already extensively self-documented, externally-audited, fuzz-tested parser;
no new defects found in the areas read.

## Cross-File Observations

Directly implements every structure `XactTypes.hpp` declares; the compact-XWB-entry deliberate-non-replication
finding is a rare, concrete example in this audit of a CNA port correctly choosing NOT to match a real,
verified bug in its own upstream reference implementation.

## Missing or Weak Tests

The untraced `ParseFirstPlayWave`/`ParseXgs`/`ParseXwbStreamingHeader`/`ParseXsb` function bodies (~1000
lines) remain a gap for a future, more exhaustive pass — though the file's own extensive self-documentation
(named task IDs for nearly every non-trivial decision) suggests they likely received the same rigor as the
sections read here.

## Positive Findings

One of the most rigorously security-hardened files found in this entire audit: multiple real,
externally-audited and fuzz-discovered bugs (pointer-arithmetic UB, allocation bombs, ASan-confirmed
heap-buffer-overflows) already found and fixed with clear, specific documentation of each; a genuine,
deliberate, test-backed non-replication of a real upstream reference-implementation bug.

## Final Assessment

No new defects found in the areas read (scoped-depth review); this file represents an unusually mature,
already-hardened parser.
