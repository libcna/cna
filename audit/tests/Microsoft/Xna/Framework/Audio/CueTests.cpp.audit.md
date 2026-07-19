# Audit: tests/Microsoft/Xna/Framework/Audio/CueTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Audio/CueTests.cpp` (4619 lines, 92 `TEST` cases)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-audio` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Microsoft::Xna::Framework::Audio::Cue`
- Main related tests: N/A (this IS a test file)

## Purpose
The largest and most comprehensive test file audited in this entire session: exercises `Cue`'s
full state machine (9 state properties), `Apply3D`, `Stop(AsAuthored)`/`Stop(Immediate)` tail
semantics, `GetVariable`/`SetVariable`, non-interactive weighted and interactive variable-driven
sound variation selection, `PlayWaveTrackVariation`/`PlayWaveEffectVariation` per-track
randomization, RPC-driven volume/pitch/filter-frequency curves (including built-in
`AttackTime`/`ReleaseTime`/3D variables), XACT track filters, and cue-level/category-level
instance-limit eviction policies (FAIL/REPLACE_OLDEST/REPLACE_LOWEST_PRIORITY).

## Executive Verdict
This is, by a clear margin, the single most rigorous test file encountered across this entire
audit. Nearly every test derives its expected value from an independently-worked-out formula
(log-centibel volume conversion, cents-to-pitch-ratio conversion, Doppler factor, 3-4-5-triangle
distance geometry) rather than a loose "greater than" check, and the file's own comments document
a real history of finding and fixing bugs in **the test suite itself**, not just production code
(e.g. `PlayWeightedVariationFavorsHigherWeightEntryStatistically`'s comment candidly explains a
previous test bug where reusing one `Cue` object across 200 "iterations" collapsed to a single
real trial, discovered by direct instrumentation).

## Checklist Results
- **Real, confirmed selection-algorithm bug found and fixed via this test suite itself**
  (P11-XACT-004, documented in the comment above `PlayWeightedVariationWithFourEntriesMatchesIndependentReplicaForSeededRng`):
  a prior audit pass's own conclusion ("same strict `>` boundary comparison, no fix needed") was
  itself wrong — it verified the comparison matched FAudio's C source character-for-character but
  missed that FAudio's draw is a *continuous* float where `>` is correct, while this port's draw is
  *discrete* integer, where `>` silently steals probability mass, becoming a *total* bias (not just
  statistical skew) for small/equal weights. `PlayWeightedVariationWithTwoEqualWeightEntriesSelectsBoth`
  is the resulting regression test, explicitly noting the pre-fix code could **never** select the
  second of two equal-weight entries.
- Every RPC/variable-driven test (`PlayScalesVolumeByRpcCurveEvaluatedAtCurrentVariableValue`,
  `PlayShiftsPitchByRpcCurveEvaluatedAtCurrentVariableValue`, the `AttackTime`/`ReleaseTime` tests)
  asserts an exact, independently-derived numeric value (e.g. a precise 10x amplitude ratio for a
  20dB/2000-centibel curve span), not just directional change.
- `RepeatedReconcileStateTicksWithConstantVariableDoNotDriftPitch` (AUD-10-013) is a genuine,
  well-targeted regression test for a real class of bug (incremental/compounding floating-point
  drift across repeated per-frame ticks) — 50 ticks with a held-constant input must produce
  bit-for-bit identical output, not just "close enough."
- `AuthoredFadeOutTakesPrecedenceOverRpcOnlyReleaseWhenBothAreAuthored` correctly locks down a
  specific FAudio precedence rule (authored fade always wins over RPC-only release when both are
  present) rather than assuming either "longer wins" or "they combine."
- `CueInstanceLimitReplaceOldestEvictsOldestBankWideCueNotSameDefinitionSibling`'s own comment
  explicitly documents and preserves — rather than "fixing" — a real, confirmed FAudio quirk: a
  cue-level `instanceLimit` check has no same-definition filter at all, so it competes against
  *every* live cue in the bank, not just siblings of the same definition. This is exactly the kind
  of "looks like a bug, is actually faithful behavior" case this project's broader audit
  methodology repeatedly emphasizes checking for.
- `PlayWithUnresolvableSoundCodeSpawnsNoInstance`/`PlayWithUnregisteredWaveBankSpawnsNoInstance`/
  `PlayWithOutOfRangeWaveIndexSpawnsNoInstance` (P9-XACT-014/015) are genuine regression tests for
  a real, confirmed prior defect: an unresolvable sound code used to silently alias onto sound
  index 0 rather than resolving to "no sound found."
- Every hardware-dependent test correctly `GTEST_SKIP()`s with an explanatory message.

## Detailed Findings
None. This file substantially and consistently exceeds this project's own test-coverage bar
across its entire, very large surface.

## Cross-File Observations
`PlayWiresRealXactTrackFilterIntoSpawnedInstance`/`PlayWiresEffectVariationFilterFrequencyAndQIntoSpawnedInstance`
both explicitly note they are regression guards for a specific ordering fix (P14-ORDER-002: filter
application must happen *before* `inst->Play()`, not after, since the old code's `if (!track_)
return;` guard would silently no-op if reverted) — a good example of a test whose value is tied to
a specific code-ordering invariant, not just the final numeric outcome.

## Missing or Weak Tests
None identified — an exceptionally thorough file.

## Positive Findings
This file's own comments repeatedly and transparently document real defects found in **the test
suite's own methodology** (not just production code) across multiple audit rounds — a level of
self-scrutiny and intellectual honesty rarely seen even in well-tested codebases. The exact-value,
independently-derived-formula approach used throughout (rather than "increased"/"decreased"
directional checks) sets a very high bar for XACT-semantics fidelity verification.

## Final Assessment
No findings.
