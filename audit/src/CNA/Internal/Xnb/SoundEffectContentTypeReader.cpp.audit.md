# Audit: src/CNA/Internal/Xnb/SoundEffectContentTypeReader.cpp

## Metadata
- Source file: `src/CNA/Internal/Xnb/SoundEffectContentTypeReader.cpp`
- Audit status: AUDITED (full read, 323 lines)
- Subsystem: `cna-internal-core` shard (Xnb)
- File type: C++ implementation
- XNA/FNA relevance: matches FNA's `SoundEffectReader`, with several evidence-based deviations for formats
  FNA's own FAudio backend handles but CNA's SDL3_mixer-backed stack needs a different path for
- Main related tests: not independently located in this pass (referenced: a real externally-produced
  MonoGame MS-ADPCM fixture, and an "AUD-06-023" property-based fuzz finding)

## Purpose
Implements `SoundEffectReader::Read()`: parses a `WAVEFORMATEX`-shaped header (with an XMA2-specific
extension variant), then either constructs `SoundEffect` directly (16-bit PCM fast path) or wraps the raw
bytes in a synthetic in-memory WAV file decoded via SDL3's native decoder (8-bit PCM/float/MS-ADPCM/
IMA-ADPCM), validating the result against the file's own stored duration as a sanity oracle.

## Executive Verdict
Healthy -- the most rigorously evidence-based hardening in this entire Xnb shard: every non-obvious design
choice cites either a real, named, externally-produced test fixture (hex-dump-verified) or a specific
property-based fuzzing finding, rather than a general "just in case" justification. No new defect found in
this pass.

## Checklist Results

### `formatLength`-derived skip/extension-read bounds: correct
Both the XMA2-extension branch (`skip = formatLength - 18 - 34`) and the generic-extension branch
(`skip = formatLength - 18`) compute the remaining-bytes-to-skip/capture in `int64_t` and explicitly reject
a negative result (an adversarially-undersized `formatLength`) with a clear `ContentLoadException` before
calling `ReadBytesExactOrThrow()` -- correctly bounded, matching this subsystem's established "reject
before reading" posture.

### MS-ADPCM extension synthesis: genuinely evidence-based, not speculative
`ComputeMsAdpcmSamplesPerBlock()`'s formula and the `needsSynthesizedAdpcmExtension` decision are backed by
a cited, hex-dump-verified real MonoGame fixture confirming the content pipeline writes `cbSize=0` (no
coefficient table at all) for MS-ADPCM -- and correctly distinguishes this from IMA-ADPCM, where SDL3's own
decoder auto-derives the missing value instead of requiring an explicit table. This is a rare example in
this audit of a workaround whose necessity is empirically demonstrated in the comment itself, not asserted.

### `ValidateDecodedDurationAgainstStoredOracle()`: a genuinely clever defense-in-depth check
Uses the `.xnb`'s own stored duration field as an independent cross-check against the actually-decoded
`SoundEffect`'s duration, catching a whole class of structural misinterpretation bugs (wrong sample rate,
wrong channel count, a doubled/halved frame count from a format mixup) that would otherwise only manifest
as audibly-wrong playback, never a thrown exception. The 2x/0.5x threshold and the `storedDurationMs == 0`
skip-condition are both explicitly calibrated against real fixture behavior (documented in the comment,
not chosen arbitrarily), and correctly avoid flagging the common real-world case of an unset duration field
as an error.

### Channel-count restriction: correct
`nChannels != 1 && nChannels != 2` correctly rejects anything but mono/stereo, matching XNA's own
`SoundEffect` channel support.

### Error-context propagation: consistently good
Both `BuildDirectPcm16()` and `BuildViaWavWrapper()` catch the underlying construction/decode exception and
re-throw as `ContentLoadException` carrying the asset name and the relevant format fields (channels, sample
rate, bit depth, format tag) -- a caller debugging a real failed asset gets everything needed without
reproducing the failure with extra instrumentation.

## Detailed Findings
None found in this pass.

## Cross-File Observations
`input.ReadBytesExactOrThrow()` (used throughout this file, and identically in the Texture2D/3D/Cube
readers) is trusted to correctly bound its read against the actual remaining stream length -- a
cross-file dependency on `ContentReader`'s own implementation (Task #4), not independently re-verified
here, consistent with how this same dependency was flagged in the texture readers' own reports.

## Missing or Weak Tests
Not independently located in this pass; the file's own comments reference at least one real fixture-backed
test and one property-based fuzz finding, suggesting meaningful coverage already exists elsewhere for this
reader specifically.

## Positive Findings
The most rigorously evidence-based file in this entire Xnb shard -- every non-obvious behavior is backed by
a specific, named, verifiable source (a real fixture's hex dump, a property-based fuzzing result, or a
direct FNA source citation) rather than an unsubstantiated design rationale.

## Final Assessment
No issues found.
