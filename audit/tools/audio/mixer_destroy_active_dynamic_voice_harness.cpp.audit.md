# Audit: tools/audio/mixer_destroy_active_dynamic_voice_harness.cpp

## Metadata
- Source file: `tools/audio/mixer_destroy_active_dynamic_voice_harness.cpp` (87 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-audio` shard
- File type: C++ standalone tool (process-isolated UAF regression harness)
- XNA/FNA relevance: exercises `Microsoft::Xna::Framework::Audio::DynamicSoundEffectInstance`'s
  `SubmitBuffer`/`Play`/`getStateProperty`/`Pause`/`Stop`/`Dispose` against
  `CNA::Internal::Audio::DestroyMixer()`
- Main related tests: the orchestrating GTest that spawns this process (per its own comment,
  analogous to the static-voice harness's own precedent) — not located/verified in this pass

## Purpose
The `DynamicSoundEffectInstance` counterpart to `mixer_destroy_active_static_voice_harness.cpp`
(Task AUD-04-009, sharing the fix from AUD-04-008 via the inherited `track_` member per
`P13-DYNAMIC-001`): destroys the shared mixer out from under a still-playing dynamic instance, then
exercises every operation a real caller could still reach afterward, proving
`GetLiveTrackHandle()`'s generation check prevents a use-after-free through this class's own,
independent `track_` access sites.

## Executive Verdict
Correct, and precisely reasoned about *why* this needs its own harness rather than reusing the
static-voice one: the top comment explicitly notes `DynamicSoundEffectInstance`'s `getStateProperty()`,
`Play()`'s resume-from-Paused and new-track-creation paths, `Stop(bool)`'s early-return guard, and
`StopInternal()`'s own inline teardown "don't share code with the base class's
Play()/Stop()/Pause()/Dispose()" — so a regression in one class's generation-check usage would not
imply a regression in the other, making both harnesses independently necessary.

## Checklist Results
- The pre-fill of 10 buffers (lines 42-49) is explicitly sized and commented to ensure "the stream
  cannot run dry and self-stop during this harness" — a real, reasoned protection against a false
  negative (the dynamic stream halting on its own before `DestroyMixer()` runs, which would make the
  subsequent checks meaningless).
- The exit-code contract (0/1/2, lines 15-18) precisely distinguishes "everything worked as
  expected" from "an exception escaped (or playback never started, e.g. no audio device)" from "the
  generation check itself regressed (wrong state, even though nothing crashed)" — matching the
  static-voice harness's own precise contract.
- Every operation a real caller could still reach post-`DestroyMixer()` (`Pause()`, `Stop()`,
  `Dispose()`, then natural destructor via scope exit) is exercised in the order "a real caller
  would plausibly hit them," per the comment — not just a single canary call.

## Detailed Findings
None.

## Cross-File Observations
Directly paired with `mixer_destroy_active_static_voice_harness.cpp` (audited alongside this file)
— confirmed the two harnesses' exit-code contracts and overall structure are deliberately
consistent, differing only in which class and which specific internal code paths are exercised.

## Missing or Weak Tests
N/A in the isolated-harness sense — this file IS the regression test for this specific scenario; not
verified in this pass whether the orchestrating GTest (analogous to `AudioMixerTests.cpp` for the
static-voice case, per that harness's own comment) actually exists and spawns this process.

## Positive Findings
The explicit "why this class needs its own harness, not just the static one" reasoning is a strong
example of avoiding false confidence from a single passing test standing in for two genuinely
independent code paths.

## Final Assessment
No findings.
