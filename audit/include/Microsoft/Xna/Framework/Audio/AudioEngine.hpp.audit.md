# Audit: include/Microsoft/Xna/Framework/Audio/AudioEngine.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Audio/AudioEngine.hpp`
- Audit status: AUDITED (full read, 251 lines)
- Subsystem: `xna-audio` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Audio/AudioEngine.cs` (public API
  surface, which wraps native FAudio); the deep instance-limit/category-hierarchy semantics
  documented here are verified against FAudio's native `FACT_internal.c` behavior (cited
  extensively in comments) rather than FNA's own C# source, since FNA itself just P/Invokes into
  FAudio for this logic
- Main related tests: not independently located in this pass

## Purpose
The XACT audio engine: parses `.XGS` global settings, coordinates `WaveBank`/`SoundBank`/`Cue`
lifetimes, and enforces category/cue-level instance limits.

## Executive Verdict
Correct, and one of the most thoroughly documented files audited this session. Every deviation from
a naive implementation is justified with a specific citation -- either to FNA's own C# source or, for
the deeper FACT/FAudio-native semantics this class reimplements from scratch (no FNA C# equivalent
exists to diff against, since real FNA just calls into native FAudio), to `FACT_internal.c`'s exact
behavior, including several places where a real, known FAudio *quirk* (e.g. `QUEUE`/`REPLACE_OLDEST`/
`REPLACE_QUIETEST` collapsing to identical behavior, matching FAudio's own unfinished stub and
"FIXME" comment) is deliberately replicated rather than "fixed," per this project's behavior-fidelity
mandate.

## Checklist Results
No issues found. The `(lookAheadTime, rendererId)` constructor's "currently unused, accepted for API
compatibility" disclosure (lines 47-58) is exactly the right way to document a parameter XNA defines
but CNA's single-backend architecture has no use for.

## Detailed Findings
None.

## Cross-File Observations
`InstanceLimitDecision` (lines 123-128) and its two consumers (`CheckCategoryInstanceLimit`/
`CheckCueInstanceLimit`, implemented in the paired `.cpp`) are consumed by `Cue::Play()` (audited
separately) -- confirmed there to correctly gate new-cue admission and trigger
`ForceFadeOutForInstanceLimit()` on a chosen victim.

## Missing or Weak Tests
Not independently located in this pass. This file's own comments reference several existing
regression tests by name (`ActiveCueCountForTest`, `AudioEngineTestAccess`) that appear to already
cover the lifecycle/category-registry surface.

## Positive Findings
Exceptional documentation discipline: every FACT/FAudio-quirk-replication decision is justified with
a specific native-source citation, and several comments explicitly reference prior audit/task IDs
that already found and fixed real bugs in this exact area (e.g. the category-volume-cascade formula,
described as "the bug this task fixes: the old code discarded the authored base entirely").

## Final Assessment
No findings.
