# Audit: tests/Microsoft/Xna/Framework/Audio/AudioEngineTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Audio/AudioEngineTests.cpp` (1159 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-audio` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Microsoft::Xna::Framework::Audio::AudioEngine`
- Main related tests: N/A (this IS a test file)

## Purpose
Exercises `AudioEngine`'s constructors, `Dispose`/cascading-disposal semantics,
`GetGlobalVariable`/`SetGlobalVariable` (including the PUBLIC/READONLY/CUE accessibility bits and
clamping), `Update()`'s disposal/fade-progression/fire-and-forget-sweep behavior, and
`GetTypeName()`.

## Executive Verdict
Exceptionally thorough — on par with `AudioCategoryTests.cpp`. Notably includes a 200-iteration
Fisher-Yates-shuffled stress test (`StressDisposalOrderPermutationsAcrossEngineWaveBankSoundBoundAndCue`)
specifically designed to catch use-after-free/double-unregister bugs across every possible
disposal ordering of `AudioEngine`/`WaveBank`/`SoundBank`/a caller-held `Cue` — a real, deliberate
stress-test methodology, not just a handful of hardcoded happy-path orderings.

## Checklist Results
- `DisposeCascadesToWaveBankSoundBankAndCue` (XA-8) is a genuine regression test for a real,
  previously-fixed defect: `AudioEngine::Dispose()` used to only reset its own `xactImpl_`,
  leaving every dependent `WaveBank`/`SoundBank`/`Cue` permanently `IsDisposed==false` — this test
  would fail against that old behavior.
- `StressDisposalOrderPermutationsAcrossEngineWaveBankSoundBankAndCue` (AUD-15-007): the stress
  test's own comment precisely explains why freeing the held cue *mid*-permutation (not always
  last) is deliberate — it's exactly the ordering that would turn a hypothetical
  unregister-on-Dispose gap into a real, crashing UAF rather than a silently-harmless stale
  pointer. This is a well-reasoned, adversarial test design.
- `GetGlobalVariableRejectsCueScopedVariable`/`RejectsNonPublicVariable` and the parallel
  `Cue::GetVariable`/`SetVariable` accessibility tests correctly exercise all four combinations of
  the real FACT `PUBLIC`/`READONLY`/`CUE` accessibility bits via a dedicated 4-variable fixture —
  matches this file's own comment citing `FACT_internal.h`'s real bit values directly.
- `SetGlobalVariableOnReadOnlyVariableIsSilentNoOp` correctly matches a real, subtle FNA fidelity
  point: FNA's C# wrapper never checks `FACTAudioEngine_SetGlobalVariable`'s native return code, so
  a READONLY variable's setter is a silent no-op in real XNA, not an exception — this test locks
  down that exact (non-obvious, easy-to "improve" incorrectly) behavior.
- `UpdateAfterDisposeDoesNotThrow` documents and locks down a deliberate asymmetry: unlike
  `GetCategory`/`GetGlobalVariable`/`SetGlobalVariable`, `Update()` does NOT check `isDisposed_`
  — matching neither a naive "always guard" pattern nor FNA's own real undefined-behavior-if-
  disposed `Update()`, but a considered middle ground (early-return via null `xactImpl_` after
  `Dispose()` resets it) that avoids penalizing the common "call `Update()` every frame regardless
  of teardown order" pattern.
- `RepeatedCategoryOperationsDoNotDuplicateActiveCueRegistryEntries` (P9-LIFECYCLE-012) verifies a
  real invariant (repeated category operations never duplicate registry entries) with an exact
  count assertion, not just "doesn't crash."
- `StopAsAuthoredDoesNotUnregisterFromAudioEngineWhileTailStillPlaying` (P9-STOP-005/009) is a
  genuine regression test: the old code unregistered a cue from `activeCues` on ANY `Stop()` call
  regardless of a still-playing release tail, breaking category-wide operations issued during that
  tail.
- `UpdateProgressesInProgressAuthoredFadeWithoutAnyOtherCueQuery` (P9-STOP-010) specifically uses
  a raw-field test accessor (`CueTestAccess::ActiveInstance`, not a getter) as the *only* other
  operation between `Stop(AsAuthored)` and the volume readback — a careful design choice
  explicitly reasoned to prove `Update()` itself progresses the fade, not merely that querying a
  getter happens to reconcile it.

## Detailed Findings
None. This file substantially exceeds this project's own test-coverage bar.

## Cross-File Observations
`GetGlobalVariableValidReturnsInitialValue`'s fixture-authoring comment explicitly documents a
real, previously-existing test bug: the "Volume" variable's accessibility byte was `0x03`
(PUBLIC|READONLY) rather than the intended `0x01` (PUBLIC only), silently making
`SetGlobalVariableValidUpdatesValue` a no-op-writable-variable test by accident before this
project started enforcing accessibility semantics — a good example of the test suite catching and
fixing its own latent fixture bug, not just production code bugs.

## Missing or Weak Tests
None identified.

## Positive Findings
The 200-iteration disposal-order stress test and the deliberate mid-permutation free-the-held-cue
design are genuinely sophisticated test engineering, well beyond typical unit-test coverage
expectations.

## Final Assessment
No findings.
