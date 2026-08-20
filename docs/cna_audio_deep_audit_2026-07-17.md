# CNA Audio Deep Audit

**Audit date:** 2026-07-17  
**Scope:** `Microsoft::Xna::Framework::Audio`, audio-related `Media`, CNA internal audio/XACT/XNB code, tests, examples, deployment paths, and SDL3/SDL3_mixer integration.  
**Independence rule:** the pre-existing `plans/plan_audio.md` was deliberately not read. This report and the replacement plan were derived from source code, tests, fixtures, and external reference implementations/documentation only.

## Executive conclusion

CNA audio is already a substantial implementation rather than a stub. The reviewed production surface contains roughly 13,945 lines across public Audio/Media and internal audio code, with about 15,993 lines of directly audio-related tests. It implements `SoundEffect`, `SoundEffectInstance`, `DynamicSoundEffectInstance`, listener/emitter 3D controls, XACT engine/category/cue/sound/wave-bank behavior, `MediaPlayer`, `Song`, and microphone support.

However, the current test strategy is much stronger at API state, parsing, and lifecycle checks than at proving what reaches the speakers. There is no end-to-end rendered-waveform conformance laboratory that can assert frequency, duration, channel mapping, loudness, loop boundaries, decoder output, or parity with XNA/FNA. That is the central reason a framework can have many tests and still play a real game at the wrong pitch or omit sounds.

The reported symptoms are credible and map to concrete implementation risks:

1. **High-pitched or sped-up effects:** the public XNA pitch conversion itself appears correct (`ratio = 2^pitch`), but CNA currently lacks sufficient observability at every sample-rate and format boundary. A 22,050 Hz buffer incorrectly declared as 44,100 Hz produces exactly double speed and one octave higher pitch. Non-neutral XACT pitch, RPC pitch, random pitch variation, or Doppler can also raise the final frequency ratio. Dynamic stream format negotiation and unchecked SDL failures are additional risks.
2. **Missing/not-loading effects:** CNA's XNB `SoundEffectReader` explicitly rejects every format except mono/stereo 16-bit PCM. The repository includes test fixtures for 8-bit PCM, 32-bit float, IMA ADPCM, and MS ADPCM, which therefore cannot load through this path. Wave banks also reject parsed XMA/WMA entries. Linux case-sensitive path resolution can expose asset-name mismatches hidden by Windows.
3. **Distorted gameplay audio:** raw constructors always interpret bytes as signed 16-bit little-endian PCM. Passing 8-bit, float, compressed, wrongly interleaved, misaligned, or incorrectly channel-counted data causes noise, clipping, speed errors, or channel corruption. Dynamic format switching has a concrete asymmetric state risk: switching to float is tracked, but a later integer submission does not switch the instance back to S16.

The exact root cause in the specific C#→C++ game port cannot be proven from the CNA archive alone because the game call sites, original XNA build, exact assets/content products, and matched recordings were not supplied. The plan therefore begins with a differential capture harness designed to identify the cause empirically instead of guessing.

## Audit limitations

- The archive contains empty SDL, SDL_image, SDL_mixer, and GoogleTest submodule directories and no Git metadata identifying their pinned commits. A full build and runtime playback test was therefore not possible in this environment.
- No original XNA C# game project, C++ port project, XACT project, complete XNB/XWB/XSB/XGS asset set, or paired recordings were included.
- No physical-device testing was possible for ALSA, PipeWire, PulseAudio, WASAPI, CoreAudio, Android, iOS, or browser output.
- Findings below are labelled **confirmed**, **strong risk**, or **suspected**. Suspected findings require a fixture or runtime trace before code changes.

## Architecture map

### Static sound path

`ContentManager / SoundEffect::FromStream / raw buffer constructor` → `MIX_Audio` → `MIX_Track` → composed gain/pan/frequency/filter callbacks → `MIX_Mixer` → SDL audio device.

### Dynamic sound path

`DynamicSoundEffectInstance::SubmitBuffer*` → queued byte chunks → `SDL_AudioStream` → `MIX_Track` → mixer/device.

### XACT path

`.xgs` + `.xsb` + `.xwb` parsers → `AudioEngine`, `SoundBank`, `WaveBank`, `Cue` → track selection/variation/RPC/category/3D → `SoundEffectInstance` or streaming source.

### Media path

`Song / MediaPlayer` → SDL3_mixer audio load/track → mixer/device, with a separate wall-clock position model.

## Ranked findings

### Critical / P0

#### A-01 — XNB SoundEffect format support is too narrow — **confirmed**

`src/CNA/Internal/Xnb/SoundEffectContentTypeReader.cpp:44-118` parses WAVEFORMATEX metadata but discards average bytes/sec and block alignment, skips extension data, and rejects everything except format tag 1 with 16 bits per sample. Mono and stereo are the only accepted channel counts.

Consequences:

- 8-bit PCM XNB sound effects fail.
- IEEE float XNB sound effects fail.
- MS ADPCM and IMA ADPCM XNB sound effects fail.
- XMA2 is parsed enough to skip metadata but then rejected.
- A failed content load can be perceived by a game as a missing effect, depending on its exception/logging behavior.

The included MonoGame-derived XNB fixture tree contains examples of several rejected formats, so this is not theoretical. FNA's internal constructor preserves the complete WAVEFORMATEX fields and extension data and has play-length handling for PCM, MS ADPCM, and XMA2, demonstrating a materially broader compatibility model.

#### A-02 — No end-to-end audible-output conformance tests — **confirmed**

The test suite is extensive, but tests predominantly validate state, exceptions, parser fields, and mocked/dummy-driver behavior. There is no standard harness that renders output and verifies:

- dominant frequency,
- speed/duration ratio,
- sample count,
- channel order and pan law,
- RMS/peak/loudness,
- loop boundaries,
- decoder parity,
- Doppler/pitch composition,
- XNA/FNA differential results.

This is the most important process gap behind the reported issue.

#### A-03 — Dynamic integer/float format switching is asymmetric — **confirmed code risk**

`DynamicSoundEffectInstance::SubmitFloatBufferEXT` sets `isFloat_ = true` and rebuilds the stream when stopped. `SubmitBuffer` does not reset `isFloat_` to false or prohibit integer submission after float mode. A stopped instance can therefore accept S16 bytes while the next stream remains F32, making the bytes sound like invalid floating-point samples. This needs a direct regression test and a parity decision against FNA extension semantics.

#### A-04 — Dynamic stream submission errors can become false-success state — **confirmed**

- `SDL_CreateAudioStream` is not checked for null.
- `SDL_PutAudioStreamData` return values are ignored, yet failed chunks are still added to `submittedChunkSizes_`.
- `MIX_PlayTrack` return values are ignored, after which the instance state is set to `Playing`.

This can produce silence, stale pending-buffer accounting, or a framework state that says “playing” while no audio is being consumed.

#### A-05 — Raw SoundEffect boundaries permit exact octave/speed mistakes — **confirmed design risk**

`SoundEffect(buffer, sampleRate, channels, ...)` always labels the supplied bytes as `SDL_AUDIO_S16LE` and trusts the caller-provided sample rate and channel enum. It does not currently validate sample-frame alignment or a supported sample-rate/channel domain before passing data to the renderer.

This is especially dangerous in a C#→C++ port because C++ byte vectors do not retain audio metadata. Common symptom signatures:

| Observed output | Likely metadata error |
|---|---|
| Exactly 2.000× speed, +12 semitones | 22,050 Hz data declared as 44,100 Hz, or final pitch ratio 2.0 |
| 1.088435× speed, about +1.47 semitones | 44,100 Hz data consumed as 48,000 Hz |
| 2.176871× speed, about +13.46 semitones | 22,050 Hz data consumed as 48,000 Hz |
| Harsh noise / near-full-scale distortion | wrong sample width, float-vs-S16, compressed bytes treated as PCM, endian mismatch |
| Half/double apparent duration with channel corruption | mono/stereo metadata mismatch or non-frame-aligned byte count |

### High / P1

#### A-06 — Hard-coded 44.1 kHz stereo S16 mixer without negotiated-spec diagnostics — **confirmed**

`AudioMixer.cpp:36-41` requests S16 stereo 44,100 Hz. SDL may convert to the physical device, but CNA does not record the actual device/mixer format, conversion path, resampler behavior, buffer size, or latency. This prevents diagnosis of 44.1↔48 kHz issues and makes renderer-version differences hard to detect.

Hard-coding 44.1 kHz is not by itself proof of a pitch bug; the defect is the absence of verified negotiation and test evidence.

#### A-07 — SDL_AudioStream output format is initially null — **strong risk requiring integration verification**

`DynamicSoundEffectInstance::EnsureStream` creates an audio stream with a valid source specification and null destination specification, then gives it to SDL3_mixer. SDL documentation says put/get operations require valid specifications at both ends; assigning the stream to the mixer may establish the output side, but this must be verified against the exact pinned SDL3_mixer version. CNA currently neither queries the resulting specs nor fails loudly if they remain incomplete.

#### A-08 — Final pitch ratio has several independent contributors and no trace — **confirmed observability gap**

CNA composes base pitch (`2^Pitch`) with Doppler and XACT-driven pitch values. A game-visible `Pitch == 0` therefore does not prove a final ratio of 1.0. There is no structured trace showing:

- source sample rate,
- base pitch,
- cue/track/random/RPC pitch in cents,
- Doppler factor,
- clamping,
- final `MIX_SetTrackFrequencyRatio` argument.

Without this trace, high pitch is difficult to attribute.

#### A-09 — Doppler can create a large pitch increase when ported velocity units are wrong — **strong risk**

The 3D code calculates radial velocities and clamps the Doppler factor up to 4×. If the C++ port derives velocity in “units per frame” while the original used “units per second,” uses milliseconds incorrectly, swaps listener/emitter velocity, or changes world scale, a moving sound can become substantially higher-pitched. A 4× factor is two octaves.

A fast diagnostic is to set both velocities to zero and `SoundEffect::DopplerScale = 0`. If the problem disappears only for 3D sounds, the issue is in 3D inputs/composition rather than decoding.

#### A-10 — XACT pitch/RPC/variation composition needs differential verification — **strong risk**

`Cue.cpp` contains substantial logic for random variations, RPC curves, track events, filters, loops, categories, and pitch. This breadth is positive, but it creates many opportunities to apply cents twice, use an incorrect unit, apply a category/track value at the wrong scope, or change random selection order. No XNA capture corpus proves the resulting voice parameters over time.

#### A-11 — XWB XMA/WMA entries are parsed but not playable — **confirmed**

`WaveBank.cpp:328-367` creates sound effects for PCM and ADPCM, but returns null for other formats. Therefore XMA/WMA entries can parse successfully yet be absent at playback time. Error handling currently logs to stderr and returns null, which may be missed in normal gameplay.

#### A-12 — Compact XWB last-entry length may ignore deviation — **suspected defect**

`XactParser.cpp:555-623` subtracts `deviations[i]` when calculating every compact entry except the final one. The comments state that the final entry should also use the end of the wave-data segment minus the deviation. The implementation instead uses the complete remaining segment. This may include padding/trailing bytes and cause clicks or decoder failure. It must be confirmed against FAudio/FACT behavior and a real fixture before patching.

#### A-13 — Content lookup can fail by case on Linux — **confirmed portability risk**

Content resolution uses `std::filesystem::exists` on literal paths and extension candidates. A port originally developed on case-insensitive Windows can refer to `Sounds/Explosion` while the deployed asset is `sounds/explosion.xnb`. Such content works under many Windows filesystems and fails on Linux. An asset manifest and case-consistency gate are needed.

#### A-14 — Important playback failures are silent or weakly surfaced — **confirmed**

Examples include `MediaPlayer` returning when loading/track creation/playback fails and XACT wave creation returning null after stderr logging. A perfect audio layer needs one structured error policy: content exceptions for invalid assets, device exceptions for unavailable hardware, result state for expected voice exhaustion, and diagnostics containing asset/cue/wave/format/renderer context.

### Medium / P2

#### A-15 — 3D output is an approximation, not XAudio2/F3DAudio parity — **confirmed**

The current implementation uses simplified pan, attenuation, Doppler, and custom filtering on a stereo mixer. It does not reproduce the complete speaker matrix, channel mask behavior, cones, curves, LFE handling, HRTF, and reverb sends of an XNA/XAudio2/FAudio path. Even after fixing speed, a 3D game can sound spatially different from XNA.

#### A-16 — Reverb/DSP support is incomplete — **confirmed**

SDL3_mixer lacks a direct equivalent of XACT's complete DSP/reverb graph in the current implementation, and some cue events are documented as unsupported. This affects timbre and ambience rather than the basic pitch defect, but matters for “perfect” parity.

#### A-17 — MediaPlayer position is wall-clock based — **confirmed design limitation**

A wall-clock timer can diverge from actually rendered media during device stalls, resampling, seeking, pause races, decoder startup, or playback failure. Position/end-of-song behavior should be driven by decoder/track state where possible and tested against XNA semantics.

#### A-18 — Microphone state can claim success after renderer failure — **confirmed**

The microphone path can set state to started even if opening the capture stream fails, conflates no-data and renderer error, and caches device enumeration without a clear hotplug refresh strategy. This is not the reported playback defect but is part of audio completeness.

#### A-19 — Audio dependencies are not auditable from this archive — **confirmed release-process gap**

`.gitmodules` names moving upstream repositories, while the archive has no Git links/commits and empty submodule folders. Reproducible audio behavior requires exact SDL/SDL_mixer revisions, build options, decoder libraries, licenses, and a compatibility matrix.

#### A-20 — Numerous Media library classes remain unimplemented — **confirmed**

Album/artist/genre/playlist/picture and collection operations contain explicit `not implemented` exceptions. These are lower priority than gameplay sound correctness, but CNA cannot claim complete XNA Media API parity until they are implemented or explicitly scoped out with compatible platform behavior.

## Why the C++ CNA port can sound different from the original XNA C# game

### 1. Content-pipeline metadata may have been lost

The XNA content pipeline emits format, sample rate, channels, block alignment, compression metadata, loop points, and duration. A manual C++ port often copies only bytes and calls a raw constructor. If the port guessed `44100` or `Stereo`, the renderer will faithfully play the wrong interpretation.

### 2. The C++ type system no longer protects array/asset assumptions

C# streams and content readers preserve richer object-level context, and managed array bounds prevent certain memory mistakes. A `std::vector<uint8_t>` can contain PCM16, PCM8, float, ADPCM, or an entire WAV file; CNA's raw constructor cannot tell which one the caller intended.

### 3. XNA/FAudio and SDL3_mixer are not identical DSP engines

Even with correct input, resampling quality, interpolation, panning law, clipping, channel mapping, voice limits, filter coefficients, and 3D calculations can differ. Pitch in SDL3_mixer changes consumption rate and pitch together; CNA's `2^Pitch` mapping is conceptually correct, but the final result must include every additional ratio exactly once.

### 4. XACT compatibility is incomplete

The original game may use XACT cues whose playback depends on wave-bank compression, RPC curves, category settings, random variation, event timing, loops, or DSP. CNA implements much of this, but unsupported bank codecs and unverified parameter composition can make sounds disappear or differ.

### 5. Windows asset naming and deployment assumptions can fail on Linux

Case mismatches, omitted extensions, copied files, content root differences, and build-install rules can all produce “missing” sounds in the C++ port without any decoder defect.

### 6. Velocities and time units may have changed during the port

Doppler uses velocity, distance scale, and speed of sound. A frame-rate-dependent C++ velocity calculation can shift pitch even when the original C# code looked equivalent.

## High-pitch investigation decision tree

1. **Capture the exact affected asset and call path.** Determine whether it is loose WAV, XNB SoundEffect, XACT cue/wave bank, dynamic audio, or MediaPlayer.
2. **Disable all pitch contributors.** Set instance pitch to zero, XACT pitch/RPC/random pitch to neutral, listener/emitter velocity to zero, and DopplerScale to zero.
3. **Measure duration ratio.** Compare original asset duration, original XNA recording, and CNA rendered duration.
4. **Log metadata at all boundaries.** File metadata → parsed format → `SoundEffect` metadata → track ratio → mixer spec → device spec.
5. **Render a 440 Hz calibration tone.** Verify dominant output frequency and frame count at 22.05, 44.1, and 48 kHz.
6. **Swap only the loader.** Feed decoded reference PCM into CNA. If it becomes correct, the defect is decoding/metadata; if not, it is playback/mixing/pitch.
7. **Swap only the playback engine.** Feed CNA-decoded PCM to a reference WAV writer/player. If the file is correct but live output is wrong, inspect stream/device conversion.
8. **Test static vs dynamic vs XACT.** This isolates the defective path.
9. **Test stationary vs moving 3D.** This isolates Doppler.
10. **Run on a 44.1 kHz and 48 kHz physical device.** A device-dependent ratio implicates format negotiation/resampling.

## Required acceptance metrics

The exact thresholds should be finalized after collecting XNA reference captures, but the following are suitable initial gates:

- Neutral pitch: final frequency ratio exactly 1.0 within floating-point tolerance.
- Calibration tone frequency: within 0.1% of expected after steady-state analysis.
- PCM duration: within one source frame in offline decode; within 2 ms end-to-end after device resampling.
- Loop boundary: no missing/repeated frame beyond documented renderer latency; click energy below an agreed threshold.
- Channel identity: no channel swap; mono duplication/pan matrix documented and golden-tested.
- Static PCM parity: sample-identical after normalizing container metadata when the same decoder path is used.
- Compressed parity: decoded frame count exact; waveform similarity and spectral error within codec-appropriate tolerance.
- No silent renderer failures: every failed SDL/MIX operation produces a structured diagnostic and leaves public state truthful.
- No unbounded queue/memory growth under underrun, pause/resume, device loss, or failed submission.
- Cross-platform smoke: Linux, Windows, macOS, Android, and Web targets execute the applicable calibration subset.

## Recommended implementation order

1. Add the offline render/capture and metadata trace before changing playback code.
2. Reproduce the reported game sound with a minimal asset and record XNA/CNA pairs.
3. Fix dynamic stream error handling and integer/float mode invariants.
4. Add raw-buffer validation and a debug metadata assertion layer.
5. Expand XNB SoundEffect loading based on a documented format matrix.
6. Validate the SDL stream's destination spec after attaching it to a track.
7. Trace and differential-test final pitch/Doppler/XACT composition.
8. Fix XWB format and compact-entry issues with authoritative fixtures.
9. Replace silent failures with structured diagnostics.
10. Only then tune resampling, panning, filters, 3D, and performance.

## Code areas reviewed

- `src/CNA/Internal/Audio/AudioMixer.cpp`
- `src/CNA/Internal/Audio/XactParser.cpp`
- `src/CNA/Internal/Xnb/SoundEffectContentTypeReader.cpp`
- `src/Microsoft/Xna/Framework/Audio/AudioEngine.cpp`
- `src/Microsoft/Xna/Framework/Audio/AudioCategory.cpp`
- `src/Microsoft/Xna/Framework/Audio/Cue.cpp`
- `src/Microsoft/Xna/Framework/Audio/DynamicSoundEffectInstance.cpp`
- `src/Microsoft/Xna/Framework/Audio/Microphone.cpp`
- `src/Microsoft/Xna/Framework/Audio/SoundBank.cpp`
- `src/Microsoft/Xna/Framework/Audio/SoundEffect.cpp`
- `src/Microsoft/Xna/Framework/Audio/SoundEffectInstance.cpp`
- `src/Microsoft/Xna/Framework/Audio/WaveBank.cpp`
- `src/Microsoft/Xna/Framework/Media/MediaPlayer.cpp`
- `src/Microsoft/Xna/Framework/Media/Song.cpp`
- content resolution in `ContentManager`
- audio/XACT/XNB tests and included fixtures
- build/submodule declarations and examples

## Reference baseline used for the audit

- Microsoft XNA documentation for pitch semantics and public API behavior.
- FNA's `SoundEffect` implementation and FAudio-oriented format handling.
- SDL3 documentation for `SDL_AudioStream` creation and data submission.
- SDL3_mixer documentation for track audio streams and frequency ratios.

The replacement `plans/plan_audio.md` contains the actionable work breakdown and completion gates derived from this report.
