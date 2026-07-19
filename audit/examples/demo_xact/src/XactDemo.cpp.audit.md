# Audit: examples/demo_xact/src/XactDemo.cpp

## Metadata
- Source file: `examples/demo_xact/src/XactDemo.cpp` (322 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-demo_xact` shard
- File type: standalone `Game`-subclass demo implementation
- XNA/FNA relevance: exercises `AudioEngine::GetCategory`/`GetGlobalVariable`/`SetGlobalVariable`/
  `Update`, `WaveBank` construction, `SoundBank::PlayCue`, `AudioCategory::Pause`/`Resume`/
  `SetVolume`/`Stop(AudioStopOptions)`
- Related production code: `xna-audio` shard (already fully audited)

## Purpose
Generates a real, minimal XACT project (`Demo.xgs`/`Waves.xwb`/`Sounds.xsb`, via the sibling
`XactFileGen.hpp`) into `Content/Audio/`, loads it through the real `AudioEngine`/`WaveBank`/
`SoundBank` production classes, and exercises cue playback, category pause/resume/volume/stop, and a
global variable (`SpeedOfSound`) round-trip.

## Executive Verdict
Correct, and a genuinely valuable integration test: because this demo constructs its own binary
XACT files (rather than shipping pre-authored fixtures) and immediately loads them through the same
`AudioEngine`/`WaveBank`/`SoundBank`/`XactParser` production code path other demos and the test suite
use, every successful run of this demo is a live, end-to-end proof that CNA's own XACT binary format
(as produced by `XactFileGen.hpp`) round-trips correctly through its own parser.

## Checklist Results
- `LoadContent()`'s try/catch (lines 81-97) correctly leaves `audioEngine_`/`waveBank_`/`soundBank_`/
  `musicCategory_`/`sfxCategory_` in whatever partial state a mid-construction exception leaves them
  (some non-null, some still null/unset) — every later use site in `Update()` is individually
  null/optional-guarded (`&& soundBank_`, `&& musicCategory_`, `&& sfxCategory_`, `&& audioEngine_`),
  so a partial load failure degrades gracefully into per-feature no-ops rather than crashing or
  operating on an unconstructed object.
- `~XactDemo()` (lines 32-38) explicitly tears down `soundBank_` → `waveBank_` → `audioEngine_` →
  `spriteBatch_`, in dependency order (SoundBank depends on WaveBank+AudioEngine; WaveBank depends on
  AudioEngine) — correct, and consistent with what automatic reverse-declaration-order `unique_ptr`
  destruction would already do; the explicit `.reset()` calls make the intended ordering
  self-documenting rather than relying on declaration-order to be right.
- `musicCategory_->Stop(AudioStopOptions::Immediate)` (line 204, the `Q` key) correctly passes the
  enum rather than a bool/default, exercising the real XNA `AudioStopOptions` parameter rather than
  a simplified always-immediate stop.
- Cue-flash bookkeeping (`cuePlayed_[i]`/`cuePlayFrames_[i]`, lines 138-145) correctly counts down
  and clears the flag exactly at 0, not leaving a permanently-lit indicator after the flash window.

## Detailed Findings
None.

## Cross-File Observations
This file's `GenerateXactFiles()` is the demo-side half of a real integration test whose other half
lives in the sibling `XactFileGen.hpp` (audited alongside this file) — see that report for the one
substantive technical finding in this shard (an independently-verified `MiniWaveFormatEx` bit-layout
check).

## Missing or Weak Tests
Not applicable — manual/visual demo; production XACT parsing is unit-tested in
`tests/CNA/Internal/Xnb/`-adjacent files already covered by the `tests-cna-internal` shard.

## Positive Findings
Graceful, individually-guarded degradation on partial XACT-load failure is good defensive design for
a demo whose entire content pipeline is self-generated at runtime rather than shipped as static
fixtures (i.e., more can plausibly go wrong here than in a demo loading pre-authored assets).

## Final Assessment
No findings.
