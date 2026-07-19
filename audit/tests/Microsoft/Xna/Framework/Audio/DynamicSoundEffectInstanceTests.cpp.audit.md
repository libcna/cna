# Audit: tests/Microsoft/Xna/Framework/Audio/DynamicSoundEffectInstanceTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Audio/DynamicSoundEffectInstanceTests.cpp` (1140 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-audio` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Microsoft::Xna::Framework::Audio::DynamicSoundEffectInstance`
- Main related tests: N/A (this IS a test file)

## Purpose
Exercises `DynamicSoundEffectInstance`'s construction (including deliberately un-validated
sample-rate values, matching real FNA), `SubmitBuffer`/`SubmitFloatBufferEXT` (including int/float
mode-switching and integer-overflow rejection), `BufferNeeded` starvation semantics,
`Pause`/`Resume`/`Stop`/`Dispose` buffer-clearing rules, live-track `Volume`/`Pitch`/`Apply3D`
control, mixer-destruction orphaning, and two substantial multi-threaded stress tests.

## Executive Verdict
Another exceptionally rigorous file, consistent with the rest of this shard. Two genuine
multi-threaded stress tests (`StressProducerConsumerWithRandomPauseStopDispose`,
`StressSubmitFloatBufferEXTAgainstRepeatedPlayCyclesNeverCorruptsLiveStream`) use real
`std::thread`s with deterministic LCG-based randomization (not `<random>`, for exact
reproducibility) to race a "producer" submission thread against a "game" thread driving
`Play`/`Pause`/`Resume`/`Stop`/`Update`, explicitly designed to catch real concurrency defects
this project's own history confirms were previously found via ASan/TSAN in this exact subsystem
(AUD-15-006).

## Checklist Results
- `ConstructorAcceptsSampleRateBelowXnaDocumentedMinimum`/`AboveXnaDocumentedMaximum`/`ZeroSampleRate`/
  `NegativeSampleRate` (P10-DYN-001/002/003) correctly lock down a deliberate, disclosed
  XNA-docs-vs-real-FNA-behavior divergence: MSDN documents an 8,000-48,000 Hz range, but FNA's
  real constructor has zero validation — this port matches FNA, not the documentation, per this
  project's established practical-compatibility policy, and these tests pin that decision down as
  intentional rather than an untested accidental gap.
- `PendingBufferCountOnlyDropsOnceAWholeChunkIsActuallyConsumed` (AUDIO-BUFFER-001) is a genuine,
  well-targeted regression test for a real, confirmed prior bug: the byte-accounting loop used to
  drop an entire submitted chunk from tracking on ANY partial consumption, not once that chunk's
  own full byte count had actually played — the test proves two one-second chunks correctly report
  2 pending after only 50ms, then 1 after ~1.2s, not the old buggy behavior.
- `MixerDestructionOrphansTrackWithoutUseAfterFree` (AUD-04-009) directly and deterministically
  (no subprocess/crash-detection needed) proves the generation-check mechanism correctly detects a
  destroyed mixer and nulls the dangling `track_` pointer before any real accessor touches it —
  reading the raw pointer via a test-only accessor first to prove it's still dangling, then
  showing the first real accessor call clears it.
- The two stress tests both explicitly document *why* their specific design (alternating
  int/float submission, not float-only; a specific iteration count) is necessary to actually
  exercise the race being tested, citing empirical confirmation that a weaker design (e.g.
  float-only submission) failed to catch a deliberately-reintroduced regression under repeated
  ASan runs — a genuinely rare and valuable level of rigor for a concurrency test.
- `BufferNeededSubscriberCanRemoveItselfDuringCallbackWithoutCrashing` correctly exercises a
  real, common event-handler pattern (self-unsubscribe during callback) against
  `System::EventHandler<T>`'s snapshot-before-iterating semantics.

## Detailed Findings
None.

## Cross-File Observations
`MixerDestructionOrphansTrackWithoutUseAfterFree`'s own comment explicitly cross-references its
sibling in `SoundEffectInstanceTests.cpp` (`MixerDestructionOrphansTrackWithoutUseAfterFree`,
AUD-04-008) as testing the identical mechanism through a structurally independent code path (this
class's own `getStateProperty()`/`track_` access, which shares no code with the base class's) —
correctly treated as a distinct, necessary test rather than a redundant duplicate.

## Missing or Weak Tests
None identified.

## Positive Findings
The two stress tests' explicit empirical justification for their specific design choices (why
alternating formats, why this iteration count, citing what a weaker design failed to catch under
ASan) is an exemplary level of rigor for testing genuine concurrency bugs.

## Final Assessment
No findings.
