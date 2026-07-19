# Audit: src/Microsoft/Xna/Framework/Audio/SoundEffectInstance.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Audio/SoundEffectInstance.cpp`
- Audit status: AUDITED (1233 lines total; full read of `Apply3D()` [lines 1003-1100] and the
  `Volume`/`Pan` property accessors [lines 1102-1161]; the DSP-filter callback machinery, pitch/pan
  crossfeed pure-math helpers, and the constructor/destructor/`Play`/`Stop`/`Pause`/`Resume` bodies
  were read at a structural/spot-check level given the file's size and the absence of a local
  FAudio C reference for its deepest internals)
- Subsystem: `xna-audio` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Audio/SoundEffectInstance.cs` for the
  public contract; DSP internals reimplement native FAudio behavior (see Metadata caveat)
- Main related tests: not independently located in this pass

## Purpose
Implements `Apply3D()`'s distance-attenuation/pan/Doppler approximation and the
`Volume`/`Pan`/`Pitch` property setters' recomposition into the underlying SDL3_mixer track.

## Executive Verdict
Correct, and confirms a genuinely significant, previously-fixed mathematical bug. `Apply3D()`'s
distance-attenuation formula (`P9-3D-003`) explicitly documents replacing an incorrect `1/(1+x)`
falloff (which "attenuated far too aggressively close to the listener... already at half volume
exactly at distance == DistanceScale, where real XNA/FNA is still at full volume") with the correct
FAudio-matching formula: full volume within `DistanceScale`, inverse-distance falloff only beyond
it. This is exactly the kind of "check the formula, don't just tune the parameter" fix this
project's own history shows it values (a documented `AUDIO-001` fix elsewhere in this same file
generalizes the pan-axis projection to account for listener orientation, verified to reduce to the
prior world-X-only approximation exactly for the default/common orientation, so the fix is a strict
generalization with no regression for the common case). `setVolumeProperty()`'s composition-through-
`INTERNAL_applyComposedTrackProperties()` (rather than a direct, attenuation-erasing
`MIX_SetTrackGain()` write) correctly preserves `Apply3D()`'s spatial attenuation across a later
`Volume` write, also cited as an `AUDIO-001` fix.

## Checklist Results
No issues found within the portions read at full depth.

## Detailed Findings
None identified in the portions reviewed.

## Cross-File Observations
`Apply3D(listeners, listenerCount, emitter)`'s single-listener-only guard (`NotSupportedException`
for `listenerCount != 1`) matches FNA's real behavior (XNA's multi-listener overload was never
meaningfully supported on any FNA platform either).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The distance-attenuation formula fix (`P9-3D-003`) is a genuine, well-verified mathematical
correctness improvement with a clear before/after numerical example demonstrating the bug. The
`AUDIO-001` volume/attenuation-composition fix prevents a real, observable regression (a later
`Volume` write silently erasing 3D positioning).

## Final Assessment
No findings within the scope reviewed at full depth; this report does not claim full coverage of
the DSP-filter callback machinery or `Play`/`Stop`/`Pause`/`Resume` bodies (see Metadata).
