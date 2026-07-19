# Audit: tests/Microsoft/Xna/Framework/Audio/WaveBankTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Audio/WaveBankTests.cpp` (1591 lines)
- Audit status: AUDITED (full read, 3 sequential reads)
- Subsystem: `tests-xna-audio` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Microsoft::Xna::Framework::Audio::WaveBank`
- Main related tests: N/A (this IS a test file)

## Purpose
Exercises `WaveBank`'s streaming vs. non-streaming constructors (including the streaming path's
lazy per-entry disk reads and memory-footprint reduction), `Dispose`/`IsInUse`/lifecycle edge
cases, `GetSoundEffect()` for PCM8/PCM16/MS-ADPCM/XMA entry formats (exact decoded frame counts,
not just "does it play"), authored loop-region propagation, malformed/oversized/zero-length/
padded entry robustness, per-entry decode-cache thread-safety, and concurrent
`GetSoundEffect()`-vs-`Dispose()` safety. Builds real, byte-accurate `.xwb`/`.xsb`/`.xgs` fixtures
by hand for every scenario, including deliberately corrupt/adversarial ones.

## Executive Verdict
No findings. This file shows the same fixture-engineering discipline as `SoundBankTests.cpp`, with
an even wider variety of hand-built binary fixtures (compact/non-compact `.xwb`, PCM8/PCM16/ADPCM/
XMA formats, padded/oversized/zero-length entries) each purpose-built to isolate a single, specific
correctness dimension — most notably `BuildPaddedXwbFixtureBytes`'s deliberate use of
*different-length* entries separated by a gap, explicitly chosen so an offset bug could not
coincidentally produce the right answer for the wrong reason.

## Checklist Results
- `StreamingGetSoundEffectReadsCorrectPerEntryOffsetAndLength`'s comment explicitly explains why a
  single-entry fixture is insufficient to catch an offset bug (entry 0 always starts at offset 0
  either way) and why the two-entry, different-length `MultiEntryXwbFixtureBytes` fixture was
  built specifically to close that gap.
- `GetSoundEffectEntriesSeparatedByPaddingHaveExactLengthsNotLeakingTheGap` (AUD-11-020) is an
  excellent, well-reasoned edge case: entries with a gap between them and deliberately different
  lengths, so a gap-leak or wrong-offset bug produces an arithmetically detectable wrong duration,
  not just "still plays."
- `GetSoundEffectFromManyThreadsSimultaneouslyDecodesOnceNotPerThread` (AUD-11-023/024) and
  `GetSoundEffectConcurrentWithDisposeNeverCrashes` (AUD-11-025) are genuine, real concurrency
  regression tests, each with a clearly stated real defect history (git-stash-confirmed race on
  first-decode; a real Dispose()-vs-GetSoundEffect() UAF risk) and a design that actually exercises
  the race (many real `std::thread`s hammering the same entry / concurrent Dispose) rather than a
  single-threaded proxy.
- `GetSoundEffectForXmaEntryReturnsNullCleanly` (AUD-11-010/011) and
  `GetSoundEffectForAdpcmEntrySucceeds`/`...DecodesExactFrameCountFromSamplesPerBlockFormula`
  (AUDIO-ADPCM-001, AUD-11-013) both correctly distinguish "format CNA cannot decode at all" (must
  cleanly return nullptr) from "format CNA must decode correctly" (must produce the exact expected
  frame count from an independently-worked-out formula), rather than treating all unusual formats
  the same way.
- `StreamingGetSoundEffectRejectsEntryLengthExceedingRealFileSize`/
  `NonStreamingGetSoundEffectRejectsEntryLengthExceedingRealFileSize` (IN-9, AUD-11-004) correctly
  test the streaming and non-streaming bounds-check code paths *separately*, since they are
  distinct implementations (re-opened-file size vs. resident-buffer size) even though they enforce
  the same policy — the file's own comment explains this is a separate, previously-untested code
  path, not a duplicate of the streaming test.
- `GetSoundEffectForZeroLengthEntryDoesNotCrash`/`GetSoundEffectForZeroLengthEntryDoesNotCrash`
  correctly documents that either of two outcomes (zero-duration effect, or nullptr) is acceptable
  for a genuinely ambiguous edge case, rather than over-specifying one arbitrary choice.

## Detailed Findings
None.

## Cross-File Observations
`GetSoundEffectForPcm8EntryHasExactFrameCount`'s comment explicitly cross-references
`SoundEffectTests.cpp`'s `Pcm8BitLoadsSuccessfully`-style tests as already covering the underlying
8-bit-to-16-bit PCM conversion correctness, scoping this file's own test to just the wave-bank
entry extraction (offset/length/format-field decoding) instead of re-verifying shared logic.

## Missing or Weak Tests
None identified.

## Positive Findings
The concurrency tests here (`GetSoundEffectFromManyThreadsSimultaneouslyDecodesOnceNotPerThread`,
`GetSoundEffectConcurrentWithDisposeNeverCrashes`) are a strong, project-consistent example of
testing races with real multi-threading and an explicit real-defect history, rather than settling
for "looks thread-safe by inspection."

## Final Assessment
No findings. This completes the audio-fix regression-test check for the `tests-xna-audio` shard —
every specifically-named defect in this audit's directive (AUD-15-006, the `isFloat_` TSAN race,
P9-3D-003, P11-XACT-002/004) has confirmed, dedicated regression coverage somewhere in this shard.
