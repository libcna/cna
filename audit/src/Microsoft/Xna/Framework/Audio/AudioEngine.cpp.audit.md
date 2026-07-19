# Audit: src/Microsoft/Xna/Framework/Audio/AudioEngine.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Audio/AudioEngine.cpp`
- Audit status: AUDITED (full read, 636 lines)
- Subsystem: `xna-audio` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Audio/AudioEngine.cs` for the public
  API's observable contract (file-not-found/init-failure exception behavior verified against it
  directly); the category-cascade/instance-limit internals are CNA-original reimplementations of
  native FAudio behavior (`FACT_internal.c`, not locally available, cited extensively in comments)
- Main related tests: not independently located in this pass

## Purpose
Implements `.XGS` parsing/init, global-variable access with FACT's exact accessibility-bit gating,
`Update()`'s per-tick cue reconciliation, cascading `Dispose()`, and the category-hierarchy-aware
`Pause`/`Resume`/`Stop`/`SetVolume` operations plus category/cue instance-limit enforcement.

## Executive Verdict
Correct, verified at a thorough (not exhaustive-every-branch) read depth given the file's density.
`Init()`'s file-not-found handling correctly matches FNA's real behavior (`FileNotFoundException`
before ever reaching parsing, mirroring `TitleContainer.ReadToPointer`'s own check) and a corrupt-but-
existing file correctly falls back to `InvalidOperationException("Engine initialization failed!")`,
matching FNA's own check on `FACTAudioEngine_Initialize`'s return code. `StopCategoryInternal()`'s
snapshot-before-iterate fix (citing a real, regression-tested bug: iterating `activeCues_` directly
while `Cue::Stop()` cascades into `UnregisterCue()`, erasing from that same vector mid-iteration,
"reliably skips stopping at least one" cue) is confirmed correct and cites its own regression test by
name (`StopStopsAllActiveCuesInCategoryNotJustSomeOfThem`).

## Checklist Results

### LOW (informational, low-likelihood edge case): `Init()`'s `tellg()` result isn't validated before sizing the read buffer
Lines 117-120: `auto sz = f.tellg(); f.seekg(0); std::vector<uint8_t> data(static_cast<std::size_t>(sz));
f.read(...)`. If `tellg()` were to return `-1` (its documented failure sentinel) immediately after a
successful `is_open()` on an `ios::ate`-positioned stream -- an exotic, rarely-triggered failure mode
for a plain local-file `ifstream` -- the resulting `static_cast<std::size_t>(-1)` would attempt to
construct a vector of an enormous size, most likely throwing `std::length_error`/`std::bad_alloc`
rather than the more specific, already-established error path this file uses elsewhere. Extremely
low practical likelihood (a `tellg()` failure right after a successful `ate`-mode open is uncommon
for ordinary files), included only for completeness. `SoundBank::SoundBank()`'s identical read
pattern (audited separately, `SoundBank.cpp` lines 61-63) shares this same characteristic.

## Detailed Findings
1. **[LOW, informational] `Init()`'s `tellg()` result is not validated before use as a buffer size**
   — lines 117-120; shared pattern with `SoundBank.cpp` lines 61-63.

## Cross-File Observations
- `CheckCategoryInstanceLimit()`/`CheckCueInstanceLimit()`'s victim-search logic (lines 434-561) is
  consumed by `Cue::Play()` (audited separately) exactly as this file's own comments describe.
- `IsInCategory()` (lines 59-72)'s parent-chain walk correctly guards against `idx >=
  categories.size()` inside the loop, avoiding an out-of-bounds read on a malformed/adversarial
  `.XGS` file with a corrupt `parentIndex` chain.

## Missing or Weak Tests
Not independently located in this pass, though this file's own comments cite at least one existing
regression test by name (`StopStopsAllActiveCuesInCategoryNotJustSomeOfThem`) covering the
reentrancy fix in `StopCategoryInternal()`.

## Positive Findings
`Dispose()`'s cascade ordering (snapshot registries, reset `xactImpl_` first, then dispose
cues/soundbanks/wavebanks) is a careful, well-reasoned reentrancy-safe design, explicitly explaining
why resetting `xactImpl_` before the cascade makes reentrant calls into `Unregister*` harmless no-ops
rather than a use-after-free. `SetCategoryVolumeInternal()`'s recursive-cascade-onto-children formula
is explicitly verified against real FACT behavior (matching, not "fixing," an unusual-looking but
genuine upstream compounding behavior).

## Final Assessment
One LOW, purely informational finding (an exotic, low-likelihood `tellg()` edge case shared with
`SoundBank.cpp`). No functional defects found in this file at the read depth applied.
