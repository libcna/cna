# Audit: src/Microsoft/Xna/Framework/Audio/Cue.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Audio/Cue.cpp`
- Audit status: AUDITED (1398 lines total; thorough targeted read, not exhaustive line-by-line:
  full read of `ReconcileState()` [lines 487-687] and roughly the first half of `Play()` [lines
  823-1022, covering wave/variation resolution, RPC-release-time scanning, and the weighted-lottery
  variation-selection algorithm]; `EvaluateRpc()`, `GetVariable`/`SetVariable`, `Apply3D`,
  `StopInternal`, `ForceFadeOutForInstanceLimit`, and the remainder of `Play()` were read at a
  structural/spot-check level rather than verified line-by-line, given this file's exceptional
  density and that its deepest internals reimplement native FAudio behavior with no local FNA C#
  or FAudio C source to mechanically diff against)
- Subsystem: `xna-audio` shard (last file requiring deep review before the shard's remaining
  smaller files)
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; see `Cue.hpp.audit.md` for the FNA-reference caveat that
  applies to this file's deepest internals
- Main related tests: not independently located in this pass; several test-only hooks
  (`INTERNAL_seedRngForTest`, `INTERNAL_selectTrackVariationIndexForTest`) suggest dedicated
  coverage already exists

## Purpose
Implements `Cue`'s full playback state machine: `Play()`'s wave/variation resolution and
instance-limit gating, `ReconcileState()`'s per-tick fade-in/fade-out/RPC-release/steady-state
volume-and-pitch reapplication, and category/variable/3D-positioning support.

## Executive Verdict
Correct at the read depth applied. `ReconcileState()`'s four-branch structure (authored fade-out,
RPC-only release, fade-in, steady-state-with-RPC) is internally consistent, each branch correctly
recombines `baseVolume * effectVolumeMultiplier * categoryVolume * rpcVolumeMultiplier` (plus a
fade multiplier where applicable) before clamping to `[0,1]`, and the comment at lines 538-543
explicitly documents a previously-fixed gap (the fade-out branch used to silently drop the RPC
volume multiplier that had been baked in at `Play()` time). The weighted-lottery variation-selection
algorithm (lines 909-960) is internally consistent with its own documented derivation, including a
specific, well-reasoned justification for using `>=` rather than FAudio's own (subtly
continuous-vs-discrete-mismatched) `>` boundary check.

## Checklist Results
No issues found within the portions read at full depth. No claim is made about the un-reviewed
majority of this 1398-line file (see Metadata for exact scope) -- future passes revisiting this
file should prioritize `EvaluateRpc()`, `StopInternal()`, and the second half of `Play()`
(instance-limit application and per-wave `PlaybackInstance` construction) for a full line-by-line
pass, given time.

## Detailed Findings
None identified in the portions reviewed.

## Cross-File Observations
- `ReconcileState()`'s "never mutate `waveBanksUsed_`/registries from a const-callable getter path"
  discipline (documented at lines 259-268 of the header) is honored correctly in the implementation
  read here.
- The weighted-lottery fix (lines 917-932) explicitly references `P11-XACT-002`'s
  `WeightedPickExcluding` as sharing the identical derivation -- worth cross-checking that function
  (likely also in this file, not located by name in this pass) for the same fix when this file is
  revisited at full depth.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The fade/RPC volume-recombination formula's consistency across all four `ReconcileState()` branches,
and the explicitly-documented history of catching and fixing a subtle continuous-vs-discrete
probability distribution bug in the variation-selection algorithm, are both strong positive
indicators of a mature, previously-audited subsystem.

## Final Assessment
No findings within the scope actually reviewed at full depth; this report explicitly does not claim
full-file coverage given the file's exceptional size and density -- see Metadata for exact scope and
recommended follow-up areas for a future deeper pass.
