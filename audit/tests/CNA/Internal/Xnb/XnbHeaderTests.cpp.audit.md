# Audit: tests/CNA/Internal/Xnb/XnbHeaderTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Xnb/XnbHeaderTests.cpp` (155 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests `CNA::Internal::Xnb::ParseXnbHeader` (backs every `.xnb` file's
  10-byte container header — the first thing parsed for any content load), Tasks XNB-11/27/44
- Main related tests: foundational for every other `Xnb/` reader test in this folder

## Purpose
Tests the `.xnb` container header parser: a real MonoGame fixture's exact header bytes, LZX/LZ4
compression-flag-bit detection (including the "both bits set" unknown case), version acceptance,
every accepted platform identifier, and several malformed/rejected-input cases (bad magic,
unrecognized platform, invalid version, truncated header).

## Executive Verdict
Excellent, with a particularly interesting piece of precise FNA-vs-MonoGame compatibility
reasoning: `MonoGameWebAssemblyPlatformIsNotAcceptedMatchingFnaExactly` (Task XNB-44) documents and
tests a DELIBERATE, intentional divergence from MonoGame's own expanded platform-identifier list —
CNA's `XnbAcceptedPlatforms()` matches FNA's original (pre-fork) list, so a real
MonoGame-for-WebAssembly-produced `.xnb` (using platform byte `'b'`) is correctly REJECTED rather
than silently mishandled, an explicit, considered compatibility-scope decision rather than an
accidental gap.

## Checklist Results
- `RealMonoGameFixtureHeaderParsesCorrectly` uses the real first 10 bytes of an actual
  MonoGame-produced fixture (`white-1.xnb`) — genuine ground truth for the exact byte layout, not
  hand-invented values.
- `LzxCompressedFlagBitIsDetected`/`Lz4CompressedFlagBitIsDetected`/`BothCompressionBitsSetIsUnknown`
  give complete coverage of the compression-flag-bit space: LZX alone, LZ4 alone, and the
  ambiguous/invalid both-bits-set case — a naive implementation testing only the two "normal" cases
  could leave the invalid combination's behavior (silently picking one, or crashing) completely
  unverified.
- `Lz4CompressedFlagBitIsDetected`'s comment correctly cites the exact flag bit value (`0x40`)
  cross-referenced against MonoGame's own `ContentManager.cs` source (`ContentCompressedLz4`), and
  correctly notes FNA itself never produces this format but MonoGame's pipeline can — precise
  provenance for a format CNA supports beyond FNA's own native capability.
- `EveryAcceptedPlatformParses` correctly iterates the COMPLETE `XnbAcceptedPlatforms()` list
  rather than spot-checking one or two platform characters, giving exhaustive coverage of the
  accepted set.
- `MonoGameWebAssemblyPlatformIsNotAcceptedMatchingFnaExactly`'s comment thoughtfully explains why
  a real 'b'-platform fixture is NOT vendored in the repo despite having been "confirmed empirically
  against a real, independently-produced fixture during this session's compatibility sweep" — since
  no reader-logic path is actually exercised by a rejected platform byte, a hand-crafted header is
  equally conclusive and avoids an unnecessary large binary in the repository. This is a reasonable,
  explicitly justified tradeoff between fixture-fidelity and repository bloat.
- `TruncatedHeaderThrowsEndOfStreamException` correctly distinguishes this specific failure mode
  (a header too short to even contain all its fields) with the appropriate, distinct exception type
  from the various `ContentLoadException` cases (deliberately-malformed-but-complete headers) —
  precise about which layer's guard is actually being exercised (stream-underflow vs.
  semantic-validation).
- `BadMagicBytesThrowsContentLoadException`/`UnrecognizedPlatformThrowsContentLoadException`/
  `InvalidVersionThrowsContentLoadException` each target a genuinely distinct header-field
  validation independently, rather than one combined "garbage header" test.

## Detailed Findings
None.

## Cross-File Observations
This file's header-parsing correctness is foundational to every other `Xnb/` reader test in this
folder, all of which depend on a correctly-parsed header before their own body-parsing logic can be
meaningfully exercised.

## Missing or Weak Tests
None identified — the compression-bit, platform, version, and truncation coverage is complete.

## Positive Findings
The FNA-vs-MonoGame platform-compatibility-scope test, with its explicit, considered justification
for not vendoring an unnecessary large binary fixture, is a good example of precise, deliberate
compatibility-boundary documentation rather than either blind MonoGame-superset support or a silent
FNA-only limitation.

## Final Assessment
No findings.
