# Audit: tests/Microsoft/Xna/Framework/Audio/SoundBankTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Audio/SoundBankTests.cpp` (783 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-audio` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Microsoft::Xna::Framework::Audio::SoundBank`
- Main related tests: N/A (this IS a test file)

## Purpose
Exercises `SoundBank`'s constructor validation (null engine, empty filename, missing file, corrupt
file), `Dispose`, `GetCue`, `PlayCue` (2-arg and 3-arg 3D overloads), `IsInUse` (including
paused/GetCue-obtained/never-played/already-stopped edge cases), the fire-and-forget cue sweep's
age/still-playing/safety-net interaction, and `GetTypeName()`. Builds real, minimal, byte-accurate
`.xsb`/`.xwb`/`.xgs` fixtures by hand rather than depending on external asset files.

## Executive Verdict
No findings. This file is a strong example of testing lifecycle edge cases with real, targeted
fixtures rather than mocks: it constructs two distinct fixture families — a wavebank-less
"Explosion" cue (sufficient for lifecycle/sweep tests where no real audio instance is needed) and
a real WaveBank-backed "Apply3DCue" (needed specifically to exercise real `MIX_Track` gain
attenuation) — and clearly documents why each is used where it is.

## Checklist Results
- `PlayCueThreeArgAppliesRealAttenuationToActiveInstance` (T-4B) is a genuine, strong regression
  test: it plays the same cue from a near and a far emitter position and asserts the *real* SDL3
  mixer track gain (`MIX_GetTrackGain`) is lower for the far case — not just that `Apply3D` was
  called, but that its effect actually reached the underlying audio track.
- `DisposeForceStopsCueObtainedViaGetCue`/`DisposeForceStopsNeverPlayedCueObtainedViaGetCue`/
  `DisposeForceStopsAlreadyStoppedButUndisposedCueObtainedViaGetCue` (P12-BANK-001,
  AUDIO-LIFECYCLE-001 external audit) are three well-differentiated regression tests for a real,
  confirmed defect: a `GetCue()`-obtained cue was only registered for the bank's force-stop cascade
  while actually playing, leaving never-played or already-stopped-but-undisposed cues with a
  dangling `bank_` pointer after the bank itself was destroyed. Each test isolates a distinct
  lifecycle state (playing / never played / stopped-not-disposed) at the moment of `Dispose()`.
- `FireAndForgetCueSurvivesSweepPastOldFiveSecondThresholdWhileStillPlaying`/
  `FireAndForgetCueIsForceSweptPastSafetyNetEvenIfStillPlaying`/
  `PausedFireAndForgetCueSurvivesSweepAndCanStillBeResumed` (XA-2/XA-1/XA-7) together correctly
  distinguish three sweep behaviors that a single naive assertion could conflate: the old
  time-based bug, the still-needed safety net, and the pause-must-not-count-as-finished fix.
- `ConstructorWithExistingButCorruptFileStaysInStubState` (XA-13) is a genuine edge case not
  covered by the missing-file test above it: an existing-but-garbage file must produce the same
  "silent stub" behavior as a missing one, not a different unhandled exception.
- `ConstructorMissingFileThrowsFileNotFound`/`ConstructorNullEngineThrowsArgumentNull`/
  `ConstructorEmptyFilenameThrowsArgumentNull` correctly assert the specific exception types
  matching FNA's real `TitleContainer.ReadToPointer`/argument-validation chain (P9-HARDWARE-003).

## Detailed Findings
None.

## Cross-File Observations
`PlayCueThreeArgAppliesRealAttenuationToActiveInstance` and
`IsInUseFalseSoonAfterFireAndForgetCueNaturallyFinishes` both correctly wrap real-hardware-path
code in a `try`/`catch` + `GTEST_SKIP()` pattern for the dummy-driver-unavailable case, consistent
with this shard's established headless-CI-safety convention (`SoundEffectInstanceTests.cpp`,
`MicrophoneTests.cpp`).

## Missing or Weak Tests
None identified — every public method/overload/property has direct or edge-case coverage.

## Positive Findings
The deliberate two-fixture-family design (minimal wavebank-less vs. real WaveBank-backed) is a
clean way to keep the majority of lifecycle tests fast/simple while still having a real-audio path
available for the one test (T-4B) that genuinely needs to measure actual mixer state.

## Final Assessment
No findings.
