# Audit: include/Microsoft/Xna/Framework/Audio/SoundBank.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Audio/SoundBank.hpp`
- Audit status: AUDITED (full read, 170 lines)
- Subsystem: `xna-audio` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Audio/SoundBank.cs`
- Main related tests: not independently located in this pass

## Purpose
Manages named cues loaded from a `.XSB` file: `GetCue()` (caller-owned), `PlayCue()`
(fire-and-forget), and cascading disposal.

## Executive Verdict
Needs a minor API-consistency note. `GetCue()` returns a raw, owning `Cue*` (the doc comment states
"the caller is responsible for disposing the returned Cue") rather than a `std::unique_ptr<Cue>` --
functionally fine (the header is explicit about the ownership contract), but inconsistent with this
codebase's own established convention elsewhere for exactly this same "factory returns an
owning pointer" pattern (e.g. `StorageDevice::EndOpenContainer()`/`EndShowSelector()`, audited under
`xna-storage`, both return `std::unique_ptr<T>` for the identical ownership-transfer situation).
Everything else -- the `RegisterCue`/`UnregisterCue`/`activeCues_` lifetime-tracking design, and the
comment explicitly citing a real external-audit finding it fixes (`AUDIO-LIFECYCLE-001`: registration
now happens at construction, not just around play state, because the original design "missed a cue
that was constructed but never played at all") -- is correct and well-reasoned.

## Checklist Results

### LOW: `GetCue()` returns a raw owning pointer instead of `std::unique_ptr<Cue>`
Line 79: `[[nodiscard]] Cue* GetCue(const std::string& name);`, documented as caller-owned. This
codebase already has an established, safer convention for the identical ownership-transfer pattern
(`std::unique_ptr<T>` return type) used elsewhere (`StorageDevice::EndOpenContainer()`/
`EndShowSelector()`). Relying on a doc comment alone to communicate ownership, when a raw pointer
return could otherwise reasonably be assumed non-owning (as `AudioEngine::FindWaveBank()` and
similar accessor-style methods in this same shard correctly are), is more error-prone than
expressing the transfer in the type system.

## Detailed Findings
1. **[LOW] `GetCue()` returns a raw owning `Cue*` instead of `std::unique_ptr<Cue>`, inconsistent
   with this codebase's own established convention for the same ownership-transfer pattern** —
   line 79; cf. `StorageDevice::EndOpenContainer()`/`EndShowSelector()`.

## Cross-File Observations
`RegisterCue`/`UnregisterCue`/`activeCues_` (lines 137-152)'s "register once at construction, for
the cue's entire lifetime" design directly parallels `WaveBank`'s identical pattern (audited
separately) -- both explicitly cited as fixing `AUDIO-LIFECYCLE-001`.

## Missing or Weak Tests
Not independently located in this pass. A test verifying `GetCue()`'s returned pointer is safe to
`delete` (and that a caller who forgets to would leak the `Cue` object itself, though not its
underlying playback resources, which `SoundBank::Dispose()` force-releases regardless) would
document the current contract explicitly.

## Positive Findings
`AUDIO-LIFECYCLE-001`'s fix (registering at construction rather than around play state) is a
genuine, well-reasoned lifecycle correctness improvement, explicitly attributed to an external audit
finding.

## Final Assessment
One LOW finding: a raw-pointer ownership-transfer API inconsistent with this codebase's own
`std::unique_ptr`-based convention for the identical pattern elsewhere.
