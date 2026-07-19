# Audit: include/Microsoft/Xna/Framework/Audio/SoundEffect.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Audio/SoundEffect.hpp`
- Audit status: AUDITED (full read, 303 lines)
- Subsystem: `xna-audio` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Audio/SoundEffect.cs`
- Main related tests: not independently located in this pass

## Purpose
Represents a loaded sound effect asset: file/buffer-based construction, static
volume/distance/Doppler/speed-of-sound properties, `CreateInstance()`/`Play()`, and
`FromStream()`.

## Executive Verdict
Correct. Both raw-PCM-buffer constructors carry an explicit, detailed warning (citing `AUD-05-005`)
about the single most common real-world misuse -- passing a whole WAV file's bytes (container
header included) instead of headerless PCM samples -- with a clear pointer to the correct
alternative (the file-path constructor). The move-only design (`T-3G`) is justified precisely:
single-owner instance tracking for `SoundEffect::Dispose()`'s cascade to every live
`SoundEffectInstance`. `setMasterVolumeProperty()`'s "unclamped, matching FNA" doc comment (line
176) is a good example of explicitly disclosing a real, verified FNA behavior that might otherwise
look like a missing validation.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
`RegisterInstance`/`UnregisterInstance`'s keep-alive-pointer-keyed design (lines 46-52) is confirmed
consumed correctly by `SoundEffectInstance` (audited separately) via `soundEffectKeepAlive_`.

## Missing or Weak Tests
Not independently located in this pass. `GetLiveInstanceCountInternal()`'s doc comment (lines 54-58)
references a specific stress test that "turned out not to reliably catch a deliberately-broken
`UnregisterInstance()` at a few thousand entries" via wall-clock timing alone -- suggesting a
direct-introspection test already exists as the fix for that gap.

## Positive Findings
The raw-PCM-buffer misuse warning is a genuinely helpful, well-targeted piece of defensive
documentation addressing a real, common mistake.

## Final Assessment
No findings.
