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
- **Capture** (`plans/plan_x11.md` X11-0162): `Microphone` records through ALSA — see below.

## Capture

`Microphone.All` lists ALSA's `default` PCM first — PipeWire's or PulseAudio's plugin on a
desktop, the card through `dsnoop` on a bare system — when the host's configuration lists it for
input, marked default and named by its description; then every sound card's capture devices, named
after the card and the device (`HD-Audio Generic, ALC257 Analog`) and opened through `plughw`, so
the 16-bit mono XNA asks for is converted from whatever the card records. Nothing is invented: a
host whose configuration lists no `default` for input gets no default entry, and a machine with no
card lists nothing. `CNA_AUDIO_RECORDING_DEVICE` names one ALSA PCM instead, which is then the whole
list — `null` records silence.

A session reads the device on a thread of its own, ten milliseconds at a time, into a queue the
game takes from when it polls — so nothing is lost to the device's half-second buffer running over
between two frames. The queue keeps at most eight seconds; a game that never reads gets the latest
audio, not an unbounded backlog. An overrun is recovered from and capture restarted; a device that
disappears is reported as lost once what it captured has been read. `null` and `file` have no clock,
and for those the thread keeps real time, as playback does.

**The test suites never record a room:** the test binaries set `CNA_AUDIO_RECORDING_DEVICE=null`
themselves, as they set the playback device. The capture tests use ALSA's `null` device and its
`file` device reading a known tone through a configuration of their own, and compare the bytes.

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
| MP3 (MPEG-1, 2 and 2.5 layer III — 8 to 48 kHz — CBR or VBR) | [dr_mp3](https://github.com/mackron/dr_libs) v0.7.3, vendored as `third_party/dr_libs/dr_mp3.h` (public domain / MIT No Attribution). Decoded when loaded for sound effects, as they play for Songs, like Vorbis (`plans/plan_x11.md` X11-0161) |
| FLAC, native or in Ogg | dr_flac v0.13.3, `third_party/dr_libs/dr_flac.h`, the same way |
| Ogg Opus, WMA/xWMA, XMA | **not decoded.** Loading one fails with a message naming what is played — `SoundEffect` throws `NotSupportedException`, `MediaPlayer.Play()` plays nothing |

A file is recognised by its content, not its name: an MP3 named `.wav` plays as the MP3 it is. An
MP3 with a LAME header has the encoder's delay and padding trimmed, so a 0.5 s tone lasts exactly
22 050 frames; one without is exactly its frames, since nothing says how much of the first and last
is padding — 24 192 for the same tone. Both are what ffmpeg decodes from the same files.

A streamed Vorbis track that loops back to a frame other than its first seeks on the audio thread,
which stb_vorbis does by bisecting the file's pages — cheap for a short file, a few milliseconds for
a long one; MP3 and FLAC seek the same way through their decoders. Songs loop from their start,
which is instant; a long streamed track with a loop point in its middle could underrun once per
loop.

## What has been validated, and what has not

On a Debian 13 laptop, with every test on ALSA's `null` device (the test binaries default to it
themselves, so no test can make a sound):

- the mixer against sample-exact expectations: gain order, mixing, interpolation, speed, loops, loop
  regions, pause, restart from a stopped callback, deferred destruction, streams, Vorbis, MP3 and
  FLAC streaming against the same file fully decoded, and each decoded format's tone and channel
  order measured (`CnaMixerTests.cpp`);
- the device against ALSA's real `null` and `file` devices: negotiation, the stop barrier, real-time
  pacing, and a recording compared with what the callback produced, byte for byte
  (`AlsaAudioDeviceTests.cpp`), plus the cross-device conformance suite;
- capture from ALSA's `null` device (real-time pacing, the contract's edges) and from its `file`
  device reading a tone, byte for byte (`AlsaAudioRecordingDeviceTests.cpp`), and XNA's
  `Microphone` over it;
- the XNA classes through the real facade (`CnaMixerXnaTests.cpp`), and the XACT category suite
  that previously ran only on SDL3_mixer;
- the media module's whole suite with the mixer in place, and an MP3 and a FLAC `Song` played by
  `MediaPlayer` to their own end on the real-time `null` device.

**Not validated:** playback through PipeWire, PulseAudio or a sound card. Nobody was at the machine,
and opening the real device even with silence can wake the codec with an audible pop, so it was
not opened. Nor capture from a real microphone: recording the room of someone who is not there is
not a test; the machine's devices were only enumerated. Latency and underruns under real load, and devices with other than two channels, are
likewise untested.
