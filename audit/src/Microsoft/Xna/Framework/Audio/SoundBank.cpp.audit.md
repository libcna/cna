# Audit: src/Microsoft/Xna/Framework/Audio/SoundBank.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Audio/SoundBank.cpp`
- Audit status: AUDITED (full read, 231 lines)
- Subsystem: `xna-audio` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Audio/SoundBank.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements `.XSB` parsing/loading, `GetCue()`/`PlayCue()`, fire-and-forget cue lifetime management
with a safety-net timeout sweep, and cascading `Dispose()`.

## Executive Verdict
Correct. `PlayCueInternal()`'s `Apply3D()`-before-`Play()` ordering explicitly cites and fixes a
real external-audit finding (`AUDIO-ORDER-001`, 2026-07-16): calling `Apply3D()` first (rather than
after `Play()`, which only matched FNA "in spirit" rather than literally) ensures every wave instance
a `Play()` call creates starts already positioned, matching FNA's exact ordering. `SweepFireAndForget()`'s
paused-cue-stays-alive logic (`XA-7`) and `Dispose()`'s cascading force-stop of every associated cue
(`P12-BANK-001`, matching `FACTSoundBank_Destroy`) are both correct and consistent with the header's
documented contract.

## Checklist Results

### LOW (shared pattern, see AudioEngine.cpp): constructor's `tellg()` result isn't validated
Lines 61-63: identical pattern to `AudioEngine::Init()` (audited separately) -- `tellg()`'s result
is cast to `std::size_t` and used to size a read buffer with no failure check. Same low-likelihood,
informational-only note applies.

## Detailed Findings
1. **[LOW, informational, shared pattern] Constructor's `tellg()` result not validated before
   sizing the read buffer** — lines 61-63; cf. `AudioEngine.cpp`'s identical pattern.

## Cross-File Observations
`Dispose()`'s snapshot-before-iterate pattern (lines 220-223) matches the same reentrancy-safety
discipline already confirmed in `AudioEngine::StopCategoryInternal()`/`WaveBank::Dispose()`.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
`AUDIO-ORDER-001`'s fix (`Apply3D()` before `Play()`) is a genuine, well-reasoned XNA-fidelity
correction, explicitly attributed to an external audit finding with a clear before/after rationale.

## Final Assessment
One LOW, purely informational finding (shared `tellg()` pattern with `AudioEngine.cpp`). No
functional defects found.
