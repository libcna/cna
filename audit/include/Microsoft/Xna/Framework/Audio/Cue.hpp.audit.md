# Audit: include/Microsoft/Xna/Framework/Audio/Cue.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Audio/Cue.hpp`
- Audit status: AUDITED (full read, 347 lines)
- Subsystem: `xna-audio` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Audio/Cue.cs` (305 lines, read in full
  for public-API-surface comparison); the private state-machine/RPC/instance-limit/variation-selection
  internals are CNA-original reimplementations of native FAudio behavior with no FNA C# equivalent
  to diff against (FNA itself P/Invokes into FAudio for all of this)
- Main related tests: not independently located in this pass

## Purpose
Represents a named sound cue: play/pause/resume/stop lifecycle, per-cue XACT variables, 3D
positioning, and (privately) the full FACT-style playback state machine -- fades, RPC curves,
instance-limit eviction, and wave/track variation selection.

## Executive Verdict
Correct. The public API surface (`IsCreated`/`IsDisposed`/`IsPaused`/`IsPlaying`/`IsPrepared`/
`IsPreparing`/`IsStopped`/`IsStopping`/`Name`/`Disposing`/`Apply3D`/`GetVariable`/`SetVariable`/
`Play`/`Pause`/`Resume`/`Stop`/`Dispose`) was verified property-for-property, method-for-method
against FNA's real `Cue.cs` and matches exactly. The private state (`paused_` modeled as an
independent flag layered on `State::Playing` rather than a separate enum value, matching FACT's own
bitmask semantics where a cue can be simultaneously `IsPlaying` and `IsPaused`) and the extensive
fade/RPC/instance-limit machinery are each justified with specific `FACT_internal.c` behavior
citations, including multiple places where a prior pass's own conclusion was later found incomplete
and corrected (e.g. the weighted-lottery boundary-check fix, `P11-XACT-004`, explicitly explaining
why an earlier `P10-VAR-002/005` "verified as a correct byte-for-byte port" conclusion missed a
continuous-vs-discrete-distribution distinction).

## Checklist Results
No issues found at the read depth applied (see Metadata).

## Detailed Findings
None.

## Cross-File Observations
- `ReconcileState()`'s "never touches `waveBanksUsed_`/`AudioEngine`'s registries" invariant (only
  `StopInternal()`/explicit `Dispose()`/`SoundBank`'s fire-and-forget sweep do that) is the same
  reentrancy-safety discipline already confirmed correctly applied in `AudioEngine::StopCategoryInternal()`
  and `SoundBank::Dispose()`/`WaveBank::Dispose()` (all audited separately) -- a consistent project-wide
  pattern for avoiding mutate-during-iteration hazards.
- `has3D_`/`pending3DListener_`/`pending3DEmitter_` (lines 239-241)'s "retained so a wave reference
  that starts playing after the last `Apply3D()` call still gets it" design is cited as fixing a real
  finding (`AUDIO-001 finding 4`) from an external audit -- confirms this subsystem has already been
  through independent review.

## Missing or Weak Tests
Not independently located in this pass. Several `NOXNA` test-only static hooks
(`INTERNAL_seedRngForTest`, `INTERNAL_selectTrackVariationIndexForTest`,
`INTERNAL_applyEffectVariationForTest`) indicate dedicated deterministic tests already exist for the
RNG-driven variation-selection algorithms.

## Positive Findings
Exceptional self-correction discipline: this file's comments document not just the current
believed-correct behavior but the history of a prior verification pass reaching an incomplete
conclusion and a later pass catching the gap -- a strong signal of genuine adversarial internal
review, not just initial-implementation confidence.

## Final Assessment
No findings, at a thorough (not exhaustive-every-line) read depth given the file's exceptional
density and the absence of a local FAudio C reference to diff the deepest internals against directly.
