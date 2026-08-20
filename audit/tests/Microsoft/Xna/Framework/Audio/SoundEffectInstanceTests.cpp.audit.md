# Audit: tests/Microsoft/Xna/Framework/Audio/SoundEffectInstanceTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Audio/SoundEffectInstanceTests.cpp` (2122 lines)
- Audit status: AUDITED (full read, 3 sequential reads)
- Subsystem: `tests-xna-audio` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Microsoft::Xna::Framework::Audio::SoundEffectInstance`
- Main related tests: N/A (this IS a test file)

## Purpose
The largest single-class test file in this shard: covers `Volume`/`Pan`/`Pitch`/`IsLooped`
property semantics across every lifecycle stage (before/during play, paused, stopped, disposed),
`Play`/`Pause`/`Resume`/`Stop`(immediate and non-immediate)/`Dispose`, move construction/assignment,
`Apply3D` (distance attenuation, Doppler, pan projection, persistence across replay/property
changes), DSP filters (low/high/band-pass state-variable filter math, XACT track filter dispatch,
RPC filter override), stereo pan crossfeed matrix math, bounded loop-region real playback behavior,
and a stress test for live-instance registry bookkeeping.

## Executive Verdict
No findings. This file directly and thoroughly answers the directive's audio-fix regression-test
check for **P9-3D-003** (distance-attenuation formula): three dedicated, exact-value tests —
`Apply3DAppliesFullVolumeWithinDistanceScale`, `Apply3DAppliesFullVolumeExactlyAtDistanceScaleBoundary`,
and `Apply3DAppliesInverseDistanceLawBeyondDistanceScale` — pin down the corrected formula
(full volume for distance <= DistanceScale, inverse-law falloff beyond it) and the boundary test's
own comment explicitly states the fixed formula's boundary value (1.0) diverges from the old,
wrong `1/(1+x)` formula's boundary value (0.5) at exactly this point — the precise regression
signature a reverted fix would reproduce.

## Checklist Results
- **P9-3D-003 regression coverage confirmed** (see Executive Verdict) — plus
  `Apply3DWithRotatedListenerAppliesSameDistanceAttenuation` (P9-3D-010) additionally proves
  distance attenuation is invariant to listener orientation, a property the three core tests alone
  don't establish.
- Doppler tests (`Apply3DAppliesDopplerPitchDownWhenEmitterRecedes`/`...PitchUpWhenEmitterApproaches`/
  `...WhenListenerApproaches`, AUD-09 series) each derive their expected ratio from an independently
  worked-out dot-product/speed-of-sound calculation shown in the comment, not from running the
  implementation — genuinely verifying the formula, not just its self-consistency. Edge cases
  (zero velocity, equal velocity, tangential motion, coincident positions, extreme velocity clamp
  to FAudio's documented [0.5, 4.0] range) are all separately covered.
- **AUDIO-001 persistent-spatial-state series** (`Apply3DBeforePlayPersistsSpatialStateOntoLiveTrack`,
  `SetVolumeAfterApply3DPreservesDistanceAttenuation`, `SetPitchAfterApply3DPreservesDopplerFactor`,
  `StopThenReplayReappliesLastApply3DState`) are strong regression tests for a real, confirmed
  defect class: `Apply3D`'s effect used to be a one-shot, easily clobbered by a subsequent
  `Volume`/`Pitch` set or lost entirely if called before the first `Play()`.
- The three `*FilterFirstSampleMatchesStateVariableFilterMath` tests derive their expected
  first-sample outputs from FAudio's actual state-variable filter recursion algebra (shown
  worked-out in the comment), not from "the filter changes the signal somehow."
- `ConcurrentFilterUpdatesDoNotRaceWithRealMixingThread`'s comment is exceptionally candid about
  its own limitation: under a normal (non-TSan) build it only catches crashes/hangs, not data
  races, and explicitly cross-references where the real TSan verification result is recorded
  (plans/plan_audio.md).
- `BoundedLoopRegionPlaysIntroOnceThenRepeatsOnlyTheLoopRegion`'s comment documents a genuine
  **prior audit claim later found to be under-verified**: CHECKLIST.md's CP-17 asserted a behavior
  ("max-frame truncates the pre-loop playthrough too") that had never actually been checked against
  real decoded PCM, only inferred from reading SDL3_mixer's property semantics — this test uses a
  raw-callback hook to observe actual played-back samples and confirms the behavior directly,
  correcting a documentation gap rather than a code bug.
- `StressCreatePlayDisposeThousandsOfShortLivedInstancesFromSharedEffect`'s comment documents that
  this exact test was previously **falsely passing** against a deliberately-broken
  `UnregisterInstance()` (a no-op) because 5000 stale pointers don't reliably crash or get caught
  by ASan at this scale — it was rewritten to directly assert the registry size stays bounded,
  and the comment states it was re-verified to actually fail against the same broken probe before
  being accepted. This is a valuable, self-documented example of "indirect symptom" tests
  (timing, "does one more still work") being insufficient and replaced with direct introspection.

## Detailed Findings
None.

## Cross-File Observations
`Apply3DWritesComputedPanIntoDspState`'s expected value (1.0f for a purely-+X emitter) is
explicitly cross-checked against the pure-math `CalculatePanIsFullyRightWhenEmitterDirectlyToTheRight`
test later in the same file, and the `ComposedPan*` test family (P10-3D-003) explicitly re-derives
the full listener-right-projection + pan-formula pipeline for all six canonical emitter directions
(right/left/ahead/behind/above/below) rather than trusting the isolated-formula tests alone to
generalize — closing a gap the file's own comment says was previously untested (vertical emitter
placement).

## Missing or Weak Tests
None identified — coverage of this class's large public surface (properties, lifecycle transitions,
3D audio, filters, pan) is exhaustive and consistently uses exact, independently-derived values.

## Positive Findings
The two self-documented "this test used to pass when it shouldn't have" corrections
(`BoundedLoopRegionPlaysIntroOnceThenRepeatsOnlyTheLoopRegion`'s stale-claim correction and
`StressCreatePlayDisposeThousandsOfShortLivedInstancesFromSharedEffect`'s broken-probe history) are
a strong, transparent signal of real test-quality rigor rather than coverage theater.

## Final Assessment
No findings. This file's P9-3D-003 tests give the strongest, most exact-value confirmation in the
shard for the specific distance-attenuation-formula regression check called out in this audit's
directive.
