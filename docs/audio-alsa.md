# ALSA audio and CNA's own mixer (`CNA_AUDIO_PLATFORM=ALSA`)

`CNA_AUDIO_PLATFORM=ALSA` is sound for a CNA build that has no SDL in it. Before it, an SDL-free
build (`CNA_ENABLE_SDL=OFF`, `CNA_PLATFORM=X11`) could only select `NULL` audio: `SoundEffect.Play()`
returned false, every instance was Stopped the moment it started, and a
`DynamicSoundEffectInstance` stopped asking for buffers after its third. Now it plays.

It is two pieces, both CNA's own:

- **an ALSA playback device** (`modules/audio/src/Platform/Alsa/`), behind the same
  `IAudioDevice` contract the SDL3, SDL2 and NULL devices implement;
- **a mixer** (`modules/audio/src/Backend/CnaMixer/`) behind `MixerEngine.hpp`, the facade the
  XNA audio classes were already written against — the one SDL3_mixer implements for SDL3 builds.

The plan and the evidence are `plans/plan_x11.md` X11-0151.

---

## Selecting it

```sh
cmake -S . -B build -G Ninja \
      -DCNA_ENABLE_SDL=OFF \
      -DCNA_PLATFORM=X11 \
      -DCNA_AUDIO_PLATFORM=ALSA \
      -DCNA_GRAPHICS_RENDERER=OPENGL33
```

Linux only, and it needs the ALSA **headers** at build time (Debian/Ubuntu `libasound2-dev`,
Fedora `alsa-lib-devel`); asking for it without them is a configure error naming the package.
`libasound.so.2` itself is **loaded at run time**, not linked — the way the X11 platform loads GLX
— so a machine without it still starts the program; opening the device then fails with the reason,
which the XNA layer reports as `NoAudioHardwareException`, exactly as it does on SDL3.

`CNA_AUDIO_PLATFORM` stays independent of `CNA_PLATFORM`: ALSA audio works with any platform
backend on Linux, not only X11.

## Why ALSA and not PipeWire or PulseAudio

Because one ALSA backend reaches all three. On a PipeWire desktop ALSA's `default` device *is*
PipeWire (`pipewire-alsa`); on a PulseAudio desktop it is PulseAudio (`alsa-plugins`); on a bare
system it is the card itself through `dmix`. A PipeWire or PulseAudio backend would each add a
second path for what this one already reaches, and would be worth it only for what they add beyond
playback — per-application device choice, device hot-plug — none of which the audio contract
exposes today.

## The device

- **Which one:** `default`, or the ALSA PCM named by `CNA_AUDIO_DEVICE`. `null` is ALSA's own
  silent device; `file:FILE=out.raw,FORMAT=raw` records exactly what would have been played.
- **Format:** float stereo at 44.1 kHz is requested; the device answers with what it has (16-bit
  or float, its own rate and channel count) and the mixer produces that. A mono device gets the
  average of left and right; channels past the second of a surround device stay silent.
- **Latency:** periods of about 10 ms, four of them buffered.
- **Threading:** one thread per device, writing a period whenever ALSA has room and never waiting
  more than 100 ms at a time, so `Stop()` returns promptly even if the device stalls. An underrun
  or a suspend is recovered from; a device that is gone stops the thread.
- **Devices with no clock:** ALSA's `null` and `file` devices take any amount at once. For those
  the thread keeps real time itself, so a 100 ms sound lasts 100 ms there as well.
- **Capture:** not implemented. `Microphone.All` is empty, as under SDL2 and NULL.

## The mixer

Its behaviour is SDL3_mixer's, because that is what the XNA classes above it were written
against. It was taken from SDL3_mixer's source, not assumed:

- Each track's audio is converted to the mixer's rate at the track's frequency ratio (pitch),
  **then** scaled by its gain, **then** handed to the track's mix callback — where
  SoundEffectInstance runs its filter and pan — **then** added to the mix at the master gain. The
  post-mix callback (MediaPlayer's visualisation) sees the finished mix.
- Mono is doubled into both channels; stereo is kept as it is.
- A play's loop count is the number of *extra* passes, `-1` for ever. Each pass ends at the play's
  max frame if it has one, and each loop restarts at its loop-start frame — so a SoundEffect's loop
  region plays its intro once and then repeats only the region.
- A stream (DynamicSoundEffectInstance, a video's soundtrack) plays what is queued and, told not to
  halt, waits silently for more rather than stopping.
- A track that runs out stops and fires its stopped callback — after its last audio has been mixed,
  where SDL3_mixer can fire it partway through the final buffer. A track destroyed from its own
  stopped callback (every fire-and-forget `SoundEffect.Play()`) leaves the mix at once and is freed
  at the next safe point, as in SDL3_mixer's facade.

**Resampling is linear interpolation** — what FAudio, and so FNA, does. It is not SDL3's
windowed-sinc resampler; a pitched sound can sound slightly different from the SDL3 build, and
the same as it does under FNA.

Every facade call and the device's callback share one recursive lock; the mixer's callbacks run
under it. The device is closed from an `atexit` handler registered when it first opens, so no
callback reaches into XNA objects while the process's statics are destroyed.

## Formats

| Format | How |
|---|---|
| WAV — PCM 8/16/24/32-bit, IEEE float, MS-ADPCM, IMA-ADPCM | CNA's own decoder (`WavDecoder.cpp`), the one XNB content already uses |
| XNB sound effects, WaveBank PCM/ADPCM entries | the same |
| Ogg Vorbis | [stb_vorbis](https://github.com/nothings/stb) v1.22, vendored as `third_party/stb/stb_vorbis.c` (public domain / MIT). Sound effects are decoded when loaded; **Songs are decoded as they play**, so a long track costs its file size in memory, not its decoded size |
| MP3, FLAC, Ogg Opus, WMA/xWMA, XMA | **not decoded.** Loading one fails with a message naming the format — `SoundEffect` throws `NotSupportedException`, `MediaPlayer.Play()` plays nothing |

A streamed Vorbis track that loops back to a frame other than its first seeks on the audio thread,
which stb_vorbis does by bisecting the file's pages — cheap for a short file, a few milliseconds for
a long one. Songs loop from their start, which is instant; a long streamed track with a loop point
in its middle could underrun once per loop.

## What has been validated, and what has not

On a Debian 13 laptop, with every test on ALSA's `null` device (the test binaries default to it
themselves, so no test can make a sound):

- the mixer against sample-exact expectations: gain order, mixing, interpolation, speed, loops, loop
  regions, pause, restart from a stopped callback, deferred destruction, streams, Vorbis streaming
  against the same file fully decoded (`CnaMixerTests.cpp`);
- the device against ALSA's real `null` and `file` devices: negotiation, the stop barrier, real-time
  pacing, and a recording compared with what the callback produced, byte for byte
  (`AlsaAudioDeviceTests.cpp`), plus the cross-device conformance suite;
- the XNA classes through the real facade (`CnaMixerXnaTests.cpp`), and the XACT category suite
  that previously ran only on SDL3_mixer;
- the media module's whole suite with the mixer in place.

**Not validated:** playback through PipeWire, PulseAudio or a sound card. Nobody was at the machine,
and opening the real device even with silence can wake the codec with an audible pop, so it was
not opened. Latency and underruns under real load, and devices with other than two channels, are
likewise untested.
