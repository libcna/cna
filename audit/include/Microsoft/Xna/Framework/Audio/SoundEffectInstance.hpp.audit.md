# Audit: include/Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp`
- Audit status: AUDITED (full read, 453 lines)
- Subsystem: `xna-audio` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Audio/SoundEffectInstance.cs`; the
  DSP-filter/pan-crossfeed internals reimplement native FAudio (`FAudio_internal.c`) behavior with
  no local FAudio C source to diff directly against
- Main related tests: not independently located in this pass

## Purpose
Controls playback of a bound `SoundEffect`: volume/pan/pitch/looping, `Apply3D()`'s distance/pan
approximation, and (protected/private) real state-variable DSP filtering and stereo crossfeed
matching FAudio's exact algorithms.

## Executive Verdict
Correct, and exceptionally well-cross-referenced against a long history of specifically-cited,
previously-fixed defects (`CP-7`, `CP-17`, `CP-20`, `AUDIO-001`, `AUD-04-008/009`, `P9-XACT-011`,
`P10-FILTER-002/003/004`, `P11-PAN-001`, `P11-XACT-003`, `P12-PITCH-001`, `P9-3D-007/010`,
`P14-ORDER-002`). Two design points stand out as particularly careful: `GetLiveTrackHandle()`'s
generation-counter check (`AUD-04-008/009`) detects a `track_` orphaned by a destroyed mixer instead
of dereferencing a dangling handle; and `soundEffectKeepAlive_`'s type-erased keep-alive (`CP-7`)
ensures an instance's underlying audio resource outlives the originating `SoundEffect` object even
for the common `SoundEffect(path).CreateInstance()` temporary-object pattern.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
`INTERNAL_applyXactTrackFilter`/`INTERNAL_applyRpcFilterOverride`/`INTERNAL_calculatePitchRatio`/
`INTERNAL_calculatePanCrossfeedMatrix` (all `NOXNA`, friended to `Cue`) are confirmed consumed
correctly by `Cue.cpp`'s `ReconcileState()`/`Play()` (audited separately) for the continuous
per-tick RPC-driven filter/volume/pitch reapplication.

## Missing or Weak Tests
Not independently located in this pass. The extensive `NOXNA` test-only hooks
(`ProcessFilterSamplesForTest`, `INTERNAL_getFilterStateForTest`, `INTERNAL_setPanStateForTest`/
`INTERNAL_getPanStateForTest`) indicate dedicated deterministic DSP-math tests already exist,
sidestepping the need to test against a real, asynchronous SDL3_mixer callback.

## Positive Findings
The generation-counter-based dangling-handle detection (`GetLiveTrackHandle()`) and the type-erased
keep-alive (`soundEffectKeepAlive_`) are both genuinely careful solutions to real C++ lifetime
hazards that a literal port of FNA's GC-backed reference semantics would not automatically avoid.

## Final Assessment
No findings.
