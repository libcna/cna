# Audit: src/Microsoft/Xna/Framework/Audio/WaveBank.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Audio/WaveBank.cpp`
- Audit status: AUDITED (417 lines total; full read of constructors, the `XactWaveBankImpl`
  cache-locking design, the WAV-builder helpers, and `Dispose()`; `Init()`/`InitStreaming()`'s
  `.XWB` parsing bodies and the bulk of `GetSoundEffect()`'s per-entry decode dispatch were read at
  a structural/spot-check level given the file's size)
- Subsystem: `xna-audio` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Audio/WaveBank.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements `.XWB` parsing (streaming and non-streaming), lazy per-entry `SoundEffect` decoding with
a mutex-guarded cache, PCM/MS-ADPCM WAV-wrapper construction, and cascading disposal.

## Executive Verdict
Correct, with a genuinely important historical fix confirmed present. `BuildAdpcmWav()`'s comment
(lines 44-53, "AUDIO-ADPCM-001, 2026-07-17 deep audit follow-up") documents a real, previously-shipped
defect: the old WAV-wrapper wrote an incomplete `fmt ` chunk missing the MS-ADPCM coefficient table
SDL3's own decoder requires, meaning "every MS-ADPCM-compressed XACT WaveBank entry silently failed
to load" (caught and swallowed as a `nullptr` return from `GetSoundEffect()`, not a crash, but a
silent total functional failure for that codec). The current code shares the same corrected,
tested WAV-assembly logic as the `.xnb` `SoundEffectReader`. `xactImplMutex_`'s locking discipline
(confirmed: both `GetSoundEffect()` and `Dispose()` take it; `Dispose()`'s lock scope is correctly
narrowed to just `xactImpl_.reset()`, not held across the cue-disposal cascade) matches the header's
documented contract exactly.

## Checklist Results
No issues found within the portions read at full depth.

## Detailed Findings
None identified in the portions reviewed.

## Cross-File Observations
`RegisterCue`/`UnregisterCue` mirrors `SoundBank`'s identical `AUDIO-LIFECYCLE-001` fix pattern.
`Dispose()`'s snapshot-before-iterate cue cascade matches the same reentrancy-safety discipline
already confirmed in `AudioEngine.cpp`/`SoundBank.cpp`.

## Missing or Weak Tests
Not independently located in this pass. `WaveBankTestAccess`'s streaming-vs-resident-bytes
introspection suggests dedicated coverage for the streaming/non-streaming distinction already
exists; the MS-ADPCM fix (AUDIO-ADPCM-001) would benefit from an explicit regression test loading a
real MS-ADPCM-compressed `.xwb` entry and asserting successful (not silently-`nullptr`) decode, if
one doesn't already exist.

## Positive Findings
The MS-ADPCM fix is a genuine, well-documented, previously-shipped-defect correction with a clear
root-cause explanation (SDL3's stricter coefficient-table validation vs. the old wrapper's
incomplete header) -- exactly the kind of finding a deep audit pass is meant to catch, already
caught and fixed here.

## Final Assessment
No findings within the scope reviewed at full depth; this report does not claim full-file coverage
of `Init()`/`InitStreaming()`'s parsing bodies (see Metadata).
