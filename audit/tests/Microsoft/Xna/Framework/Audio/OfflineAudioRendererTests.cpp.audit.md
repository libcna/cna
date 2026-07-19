# Audit: tests/Microsoft/Xna/Framework/Audio/OfflineAudioRendererTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Audio/OfflineAudioRendererTests.cpp` (511 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-audio` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Harness self-tests for `OfflineAudioRenderer.hpp`, and golden-matrix
  regression tests for SDL3_mixer sample-rate/pitch/gain composition underlying the whole
  `Audio` subsystem
- Main related tests: N/A (this IS a test file)

## Purpose
Two-part file: (1) harness self-tests proving `OfflineAudioRenderer.hpp`'s measurement primitives
(Goertzel, frequency refinement, zero-crossing, RMS/peak, NaN/Inf detection) actually measure what
they claim before being trusted elsewhere; (2) golden-matrix regression tests for sample-rate
round-tripping (8000-96000 Hz x mono/stereo), pitch-ratio-to-frequency mapping (2^pitch, 9-point
matrix), and track-gain/mixer-gain composition (multiplicative, not additive/double-applied),
plus loop-region and extreme-gain edge cases.

## Executive Verdict
No findings. Every test in the golden matrices asserts an exact, independently-derived expected
value (e.g. `PitchCase{0.25f, 1.1892071}` — a hand-computed `2^0.25`, not derived from running the
implementation) — a textbook example of this project's "derive expected values from the real
spec, not from current output" standard. The file also contains a deliberate, well-documented
*negative-control* test (`MisdeclaredSourceRateReproducesExactlyDoubleFrequencySignature`) that
proves a well-known bug signature is a metadata problem, not a mixer defect — a valuable technique
for distinguishing bug classes.

## Checklist Results
- `Calibration440HzMonoAtNativeRateMeasuresCorrectFrequency` uses three independent measurement
  methods (Goertzel bin dominance, phase-refined frequency estimate, zero-crossing rate) with
  appropriately different tolerances for each method's own inherent precision — not just one
  brittle check.
- The `GoldenSampleRateTest`/`GoldenPitchRatioTest` parameterized suites (13 sample rates x 2
  channel counts; 9 pitch ratios from -1 to +1 octave) give this file genuinely broad, exact-value
  coverage of the shared SDL3_mixer resampling/pitch mechanism that every higher-level XACT
  pitch/Doppler/RPC contributor ultimately composes into.
- `TrackAndMixerGainComposeMultiplicativelyNotDoubleAppliedOrOmitted`'s comment explicitly states
  the three specific wrong-composition signatures a regression would produce (0.0625 for
  double-applied, 0.5 for one-factor-omitted) rather than just asserting the correct value alone —
  making a future regression's failure message self-diagnosing.
- `MixerGainChangeMidPlaybackAffectsActiveVoiceOnNextChunk`'s comment explicitly distinguishes and
  cross-references why this test (existing-active-voice) is not redundant with the composition
  tests above (which only ever create a fresh track already reflecting gain at creation).
- `LoopRegionFarBeyondDecodedLengthDegradesGracefullyNoCrashNoNaN` correctly documents and tests a
  deliberate "match FNA's own lack of validation" fidelity decision (P9-VALIDATION-002): CNA
  performs no loop-region bounds check, matching FNA, relying on the backend to behave safely —
  and this test empirically confirms SDL3_mixer's real behavior for a nonsensical loop region
  rather than merely asserting the intent in prose.

## Detailed Findings
None.

## Cross-File Observations
This file is the harness-validation and cross-cutting-mechanism counterpart to the per-class test
files (`SoundEffectInstanceTests.cpp`, `CueTests.cpp`, etc.) — those files test XNA-facing API
semantics using this file's own already-validated measurement primitives, and this file's pitch
matrix comment explicitly cross-references `SoundEffectInstance::INTERNAL_calculatePitchRatio` as
consuming the same `2^pitch` formula it golden-tests here.

## Missing or Weak Tests
None identified.

## Positive Findings
The three-tests-per-claim pattern seen throughout (baseline, isolated-factor, extreme-value) for
gain composition, and the deliberate negative-control test for the misdeclared-sample-rate bug
signature, reflect careful, hypothesis-driven test design rather than coverage-for-its-own-sake.

## Final Assessment
No findings.
