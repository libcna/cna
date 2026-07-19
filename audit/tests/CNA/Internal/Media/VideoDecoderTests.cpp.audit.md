# Audit: tests/CNA/Internal/Media/VideoDecoderTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Media/VideoDecoderTests.cpp` (497 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests `CNA::Internal::Media::VideoDecoder` (backs
  `Microsoft::Xna::Framework::Media::VideoPlayer`; CNA-internal FFmpeg-based implementation — FNA's
  own video playback is platform-specific/Theora-based, so CNA's chroma-subsampling/bit-depth/AV1
  coverage here is a documented, deliberate superset)
- Main related tests: none in this shard

## Purpose
Tests the FFmpeg-backed video/audio decoder across chroma subsampling formats (4:2:0/4:2:2/4:4:4),
high bit depths (10/12-bit), AV1-with-audio (an intentional improvement beyond FNA's video-only AV1
handling), multi-track audio switching, corrupted/truncated-file hardening, seeking, and several
`Close()`/`SeekToStart()` state-leakage regressions.

## Executive Verdict
One of the most rigorous test files in this entire audit. Five separate tests are explicitly
labeled "found by external code review" (MEDIA-40, 146, 155, 159, 171) with detailed provenance
comments explaining exactly what the prior, insufficient test looked like and why it failed to
catch the real defect — this is genuinely exceptional traceability for a test suite, turning each
test into a documented incident report rather than an opaque assertion.

## Checklist Results
- `TruncatedFileEndsAtCleanEOFRatherThanThrowing`'s own comment is a remarkable piece of honest
  test-quality self-correction: it explicitly describes how the PREVIOUS version of this exact test
  accepted "either a thrown exception OR a clean EOF," making it a tautology that could never fail
  and provided zero real coverage, then documents the actual verified FFmpeg/Matroska behavior
  (empirically confirmed: trailing truncation → `AVERROR_EOF`, not a distinct error) and asserts
  that specific, real behavior instead. This is a genuinely valuable meta-finding pattern: a test
  that can never fail is worse than no test (it manufactures false confidence), and this file
  documents having caught and fixed exactly that failure mode in itself.
- `CorruptedMidStreamDataThrowsRatherThanSilentlyEndingCleanly` (MEDIA-40) is a carefully-engineered
  adversarial test: its own comment documents that the project's own ffv1/Matroska fixtures were
  empirically found (via "direct ffmpeg CLI experimentation across a dozen+ corruption strategies")
  to be UNABLE to exercise a genuine mid-stream decode error — Matroska's demuxer is
  deliberately EOF-tolerant and this build's ffv1 decoder never hard-fails on CRC mismatches — so a
  dedicated H264 fixture was added specifically because H264's macroblock decoding is much less
  tolerant of corruption. This is real, hands-on empirical test-fixture engineering, not a guess
  at what "should" trigger an error.
- `DecodesTheFullFileWithoutSilentlyDroppingAnyFrame` (MEDIA-146) documents a genuinely subtle
  concurrency/state-lifetime bug: an EAGAIN packet-retention flag was a FUNCTION-LOCAL variable, so
  a retained packet was silently lost across the call-return boundary whenever the very next
  receive-frame call (the overwhelmingly common case after EAGAIN) returned a buffered frame first —
  and the test's regression guard (exact frame count of 50, independently cross-checked against
  `ffprobe`'s own `nb_read_frames=50`, plus strictly-increasing PTS) is a real, deterministic,
  ground-truth-verified proof no frame silently vanishes — far stronger than "decoding didn't throw."
- `CloseClearsAnyUndrainedPendingAudioFromThePreviousFile` (MEDIA-155) and
  `SeekToStartClearsAnyUndrainedPendingAudioFromBeforeTheSeek` (MEDIA-159) both document a real
  state-leakage class (undrained audio samples from a previous file/seek splicing onto the next
  file/post-seek decode) and both explicitly note WHY the bug was previously invisible in practice
  (the one real caller, `VideoPlayer`, happens to always drain or allocate fresh instances before
  hitting the leaking path) — correctly framing these as "the class's own contract shouldn't depend
  on caller behavior" tests, not merely "the one known caller works."
- `SeekToStartRebuildsAWorkingResamplerSoAudioStillDecodesAfterTheSeek` (MEDIA-171) documents an
  even sharper meta-finding: an EARLIER fix (MEDIA-167) claimed regression coverage from two
  EXISTING tests, but this test's comment proves neither actually covered the resampler-rebuild
  path (one fixture has no audio at all; the other only checked the pending buffer was empty, which
  would pass even MORE easily if the resampler were left broken/null). The new test explicitly
  proves the resampler is not just present but FUNCTIONALLY WORKING by decoding real audio both
  before and after the seek — a genuinely rigorous "prove a false claim of coverage wrong, then fix
  it for real" case study.
- `SetAudioStreamSwitchesToTheRequestedTrack` correctly uses a fixture with two audio tracks at
  DIFFERENT sample rates (48000 vs 44100) specifically so a successful switch is provable by an
  externally observable value change, not merely "the call didn't crash."
- `SetVideoStreamReselectingTheSameStreamPreservesDimensions`'s own comment HONESTLY discloses a
  real coverage gap: the available fixture has only one video stream, so this test can only prove
  re-selecting the SAME stream is a safe no-op, not that an actual cross-stream switch works
  correctly — flagged transparently rather than silently passed off as complete coverage.
- `ExpectBarsMatch`'s SMPTE-bar reference-color sampling with a documented generous tolerance (20)
  for chroma-subsampling/bit-depth-downshift rounding is a sound, reusable ground-truth check shared
  across all six format-decode tests (420/422/444/10-bit/12-bit + the seek test), giving real,
  visually-meaningful color-correctness proof rather than just "decoding produced non-empty bytes."

## Detailed Findings
None — every test in this file that could have been a weak tautology instead documents having been
caught and hardened, which this audit treats as strong positive evidence rather than a concern.

## Cross-File Observations
This file's "found by external code review" provenance-labeling convention (also seen in
`SavedPictureStoreTests.cpp`'s path-traversal test earlier in this shard) is a valuable, consistent
project practice — it lets a future auditor distinguish "defensive test written on spec" from "test
written because this exact thing broke in production/review," which materially affects how much
confidence to place in the corresponding code path.

## Missing or Weak Tests
The `SetVideoStreamReselectingTheSameStreamPreservesDimensions` gap (no fixture with 2 video
streams, so a genuine cross-stream video switch is unverified) is already honestly disclosed by the
test's own comment; noting it here for completeness rather than as a new finding, since it is not a
silent gap.

## Positive Findings
This file's repeated pattern of documenting and fixing its OWN prior test-quality failures
(tautological truncation test, false regression-coverage claims) is one of the most valuable
test-quality artifacts found anywhere in this audit — it demonstrates genuine adversarial
self-review of the test suite itself, not just of the production code.

## Final Assessment
No findings. This file is exemplary and should be treated as a reference example of rigorous,
self-correcting regression-test authorship for any future test-writing guidance in this project.
