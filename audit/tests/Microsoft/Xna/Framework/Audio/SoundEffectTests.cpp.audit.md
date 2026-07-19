# Audit: tests/Microsoft/Xna/Framework/Audio/SoundEffectTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Audio/SoundEffectTests.cpp` (1298 lines)
- Audit status: AUDITED (full read, 2 sequential reads)
- Subsystem: `tests-xna-audio` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Microsoft::Xna::Framework::Audio::SoundEffect`
- Main related tests: N/A (this IS a test file)

## Purpose
Covers static sample-duration/size math, `MasterVolume`/`DistanceScale`/`DopplerScale`/
`SpeedOfSound` static properties, the raw-buffer-range constructor's validation (bad range,
integer overflow, zero/negative sample rate, zero channels, misaligned byte count, loop-region
propagation including out-of-range regions), `FromStream` (WAV decode, `smpl` chunk loop-point
parsing, malformed/truncated chunk rejection), the path constructor, move-only semantics, `Play()`
overloads, the instance-tracking `Dispose()` cascade (including move-tracking edge cases), and
diagnostic-only (non-throwing) heuristics for misused raw-buffer input (container-signature
detection, byte-entropy detection).

## Executive Verdict
No findings. Continues this shard's very high bar: `static_assert`s lock down move-only semantics
at compile time (T-3G), and multiple tests are explicit regression guards for real, named,
previously-fixed defects, each documented with a clear description of the original failure mode.

## Checklist Results
- `MasterVolumeAffectsAlreadyPlayingInstanceViaMixerGainNotTrackGain` (CP-16) is a strong,
  discriminating regression test: it explicitly checks the *mixer-level* gain (`MIX_GetMixerGain`)
  changed while the *track-level* gain (`MIX_GetTrackGain`) stayed fixed at `Volume_`'s value —
  correctly distinguishing "the property round-trips" from "the property reaches the right stage
  of the real signal chain," which a naive getter-only test would not catch.
- `BufferRangeConstructorRejectsOffsetCountIntegerOverflow`/`HugeCountAgainstSmallBufferThrowsBeforeReachingBackend`/
  `HugeOffsetNearIntMaxThrowsBeforeReachingBackend`/`OffsetPlusCountThatWouldOverflowInt32ThrowsCleanly`
  (P9-VALIDATION-003) thoroughly exercise the exact integer-overflow class of bug this project
  audits for elsewhere in the codebase — offset+count validated via unsigned arithmetic rather than
  a naively-overflowable signed sum.
- `PlaySucceedsAfterOriginatingSoundEffectTemporaryIsDestroyed` (CP-7) is a genuine, well-targeted
  UAF regression test: the exact "chained temporary" pattern (`SoundEffect(...).CreateInstance()`)
  that previously caused `Play()` to dereference an already-destroyed `SoundEffect*`.
- `DisposeAfterInstanceMovedOutOfScopeDisposesTheMovedToInstance`/
  `DisposeAfterInstanceMoveConstructedOutOfScopeDisposesTheMovedToInstance` correctly test that the
  cascade-tracking mechanism follows a move (updating its tracked pointer to the moved-to object),
  not just that moves work in isolation — a subtle and easy-to-miss dangling-pointer class of bug.
- `RawBufferStartingWithRiffSignatureEmitsDiagnosticWithoutThrowing`/`...XnbSignature...`/
  `RawBufferWithHighEntropyRandomDataEmitsDiagnosticWithoutThrowing`/`RawBufferWithRealSineWaveEmitsNoEntropyDiagnostic`
  (AUD-05-006/007) correctly test a diagnostic-only (non-throwing) heuristic on both its true-positive
  and true-negative sides — verifying a real sine wave does NOT false-positive on the entropy
  heuristic is just as important as verifying random/compressed-looking data does trigger it.
- `OutOfRangeChannelsEnumValueEitherConstructsOrThrowsCleanlyNeverUB` correctly documents and
  accepts either of two valid outcomes (constructs safely, or throws `NotSupportedException`) for
  a value that's out of the `AudioChannels` enum's documented range but not rejected by the
  language itself — appropriately flexible given the test explicitly ties its expected branch to
  the current SDL3_mixer version's actual (empirically confirmed) behavior, not an assumption.
- The FromStream malformed-chunk tests (`FromStreamTruncatedFmtChunkThrowsNotSupported`,
  `FromStreamTruncatedDataChunkThrowsNotSupported`, `FromStreamWithTruncatedSmplChunkDoesNotCrash`)
  are well-targeted P10-SE-002 fixtures for parser-robustness edge cases, each isolating a single
  malformed-chunk scenario.

## Detailed Findings
None.

## Cross-File Observations
`PlayWithHardPanDoesNotCrash`'s comment candidly explains its own limitation — it is a smoke/
non-crash test only, since the fire-and-forget `Play()` path exposes no way to reach the internally
created-and-destroyed `MIX_Track`, and explicitly defers to `SoundEffectInstanceFilterMathTest`'s
pure-math pan-crossfeed-matrix tests (in `SoundEffectInstanceTests.cpp`) as the place the
underlying math is actually verified — a good example of this shard's practice of documenting
*where* a claim is actually verified when it can't be verified locally.

## Missing or Weak Tests
None identified.

## Positive Findings
The diagnostic-heuristic tests' true-positive/true-negative pairing (container signature and
entropy detection) is a disciplined pattern that avoids a common test-suite gap (only ever testing
the "should trigger" side of a heuristic).

## Final Assessment
No findings.
