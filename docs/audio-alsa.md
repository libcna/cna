# ALSA audio and CNA's mixer

`CNA_AUDIO_PLATFORM=ALSA` selects CNA's Linux playback/capture implementation and its own mixer
behind the shared `MixerEngine.hpp` facade. It can be used with an SDL3 window or an SDL-free
HEADLESS/TERMINAL platform. Audio-platform selection is independent of `CNA_PLATFORM` and
`CNA_GRAPHICS_RENDERER`.

The implementation lives in `modules/audio/src/Platform/Alsa/` and
`modules/audio/src/Backend/CnaMixer/`. The completed implementation work is in Git history
(X11-0151 and the later capture/codec tasks); this page describes the current contract.

## Configuration and dependencies

```sh
# SDL-free configuration with ALSA sound.
cmake -S . -B build-alsa-headless -G Ninja \
  -DCNA_ENABLE_SDL=OFF \
  -DCNA_PLATFORM=HEADLESS \
  -DCNA_AUDIO_PLATFORM=ALSA \
  -DCNA_GRAPHICS_RENDERER=HEADLESS

# SDL3 window integration with ALSA sound.
cmake -S . -B build-alsa-window -G Ninja \
  -DCNA_ENABLE_SDL=ON \
  -DCNA_PLATFORM=SDL3 \
  -DCNA_AUDIO_PLATFORM=ALSA \
  -DCNA_GRAPHICS_RENDERER=OPENGL33

cmake --build build-alsa-headless --parallel 8
```

ALSA selection is Linux-only. Configuration requires ALSA development headers
(`libasound2-dev` on Debian/Ubuntu or `alsa-lib-devel` on Fedora); missing headers cause a named
configure error. CNA loads `libasound.so.2` through `dlopen` at runtime rather than directly
linking libasound. Missing library/device availability is therefore a runtime audio failure, not
proof that an otherwise loadable application cannot start.

The source selection excludes SDL3 audio and SDL3_mixer from the ALSA audio module. Other selected
platforms/renderers/features can still require SDL; selecting ALSA alone does not make the whole
application SDL-free. The selection rules are in
[AudioPlatformSelection.cmake](../cmake/AudioPlatformSelection.cmake) and
[modules/audio/CMakeLists.txt](../modules/audio/CMakeLists.txt).

## Playback device and host routing

Playback opens ALSA PCM `default`, or the name in `CNA_AUDIO_DEVICE`. On hosts configured with
ALSA plugins, that PCM can route to PipeWire or PulseAudio; on other hosts it can route to a
hardware/software PCM. That routing is host configuration, not a CNA guarantee. CNA has no separate
PipeWire/PulseAudio implementation in this selection.

The mixer requests Float32 stereo at 44.1 kHz. ALSA negotiates sample representation, rate and
channel count, trying float and signed-16 formats, and the mixer adapts to the negotiated format.
Mono output averages left/right; channels beyond stereo are silent.

The device targets roughly 10 ms periods with four periods buffered. One output thread writes each
period, handles partial writes and recovers underruns/suspends where ALSA permits. Its availability
wait is capped at 100 ms. This cap is a wait policy, not a measured end-to-end hardware latency or
hard real-time deadline.

A write error `snd_pcm_recover` cannot mend -- an unplugged card, a restarted sound server -- no
longer ends playback (CNA-FIX-L45). The thread reopens the same PCM name with the negotiated format,
every quarter second while the device is started, and keeps the dead handle until a new one
replaces it, so `IsOpen()` stays true; `IsRunning()` is false and the mixer callback is not pulled
until the device is back. A reopen that negotiates a different format is refused and retried.
`Stop()` ends the retries within one 50 ms slice. `AlsaAudioDevice.ALostDeviceIsReopenedAndPlaybackContinues`
and `...StopEndsTheReopenAttemptsOfALostDevice` inject the failure through a libasound table
whose writes return `ENODEV` and whose opens fail on demand; no physical unplug has been run.

ALSA `null` and `file` PCMs consume data without a device clock, so CNA paces them using steady
time. `CNA_AUDIO_DEVICE=null` is the silent test path; a configured
`file:FILE=out.raw,FORMAT=raw` PCM records output bytes for comparison.

## Capture

The recording provider lists an input-capable `default` hint when the host exposes one, followed
by enumerated card capture devices opened through `plughw`. It does not invent a default when
there is no matching hint. `CNA_AUDIO_RECORDING_DEVICE` overrides enumeration with one named PCM,
including `null` for silent capture.

A capture session negotiates its native format and reads on a dedicated thread in approximately
10 ms periods. Its queue is capped at eight seconds; overflow drops the oldest complete frames.
The game drains that queue when it polls. Recoverable overruns restart capture; device loss is
reported after already queued bytes have been drained. Unclocked null/file inputs are paced too.

Native `Microphone::Start` publishes `Started` only after opening a connected mono PCM16 session
with a valid, representable sample rate. Open failures propagate, rejected sessions are closed,
and a failed start leaves the microphone stopped so it can be retried. See the
[XNA failure-contract evidence](xna-fna-behavior-decisions.md#measured-xna-over-fna-decisions).
Successful provider enumeration alone does not prove that capture started; physical-device
qualification limits remain in [handoff.md §5](../handoff.md#5-known-bugs-and-limitations).

## Mixer behavior

The mixer implements the same internal facade consumed by XNA audio classes, with these current
rules:

- Track resampling uses linear interpolation at the frequency ratio, then track gain, then the
  track mix callback (filter/pan), then accumulation with master gain. The post-mix callback sees
  the completed mix.
- Mono sources feed both stereo channels. Loop counts mean extra passes; -1 repeats indefinitely.
  Each pass respects its end/max frame, then restarts at its loop-start frame.
- A non-halting stream stays playing silently when its submitted queue runs out and resumes as data
  arrives. A halting track stops and invokes its callback after its final mixed samples.
- Callback-triggered track destruction is deferred to a safe point. Every facade call and the
  device callback share a recursive mutex; callbacks run under it.
- The facade installs an exit handler when opening the device to stop callbacks before normal
  native static teardown.

These are implementation rules, not a claim of sample-for-sample identity with every SDL3_mixer,
FAudio or physical audio path.

## Codecs and streaming

| Input | ALSA/CNA mixer path |
|---|---|
| WAV PCM 8/16/24/32-bit, IEEE float, MS-ADPCM, IMA-ADPCM | CNA's WAV decoder; also used by XNB/wave-bank content. |
| Ogg Vorbis | Vendored `stb_vorbis` (currently v1.22). |
| MP3 | Vendored `dr_mp3` (currently v0.7.3). |
| Native/Ogg FLAC | Vendored `dr_flac` (currently v0.13.3). |
| WMA/xWMA in XACT WaveBanks | Optional shared FFmpeg entry adapter, before either mixer; WMAv2 stereo is qualified; WMA Pro is refused pending native duration/priming measurement. See [wavebank-codecs.md](wavebank-codecs.md). |
| Ogg Opus, ordinary WMA files, XMA/XMA2 | No new general file-loader path. XMA WaveBank entries remain explicitly refused pending lawful fixture qualification. |

Detection uses content signatures rather than the filename extension. Sound effects decode at load;
Songs retain encoded file bytes and decode incrementally during playback. This is encoded-memory
streaming, not on-demand disk streaming. Long tracks therefore still retain their encoded file.
MP3 delay/padding handling follows the pinned decoder and its metadata; fixture frame counts are
test evidence, not universal durations for every encoder.

Seeking compressed loop points occurs on the audio thread. Nonzero loop points can incur decoder
seek work; glitch-free hardware playback under load has not been established by silent PCM tests.

## Verification and qualification

An ALSA-enabled build registers:

```sh
tools/platform/run_gpu_tests_private.sh build-alsa-headless \
  --output-on-failure -R '^CnaAudioAlsa(Tests|RecordingTest)$'
```

`CnaAudioAlsaTests` selects mixer, XNA facade, playback-device, capture-device, category and
microphone tests with null playback/recording PCMs. `CnaAudioAlsaRecordingTest` uses a file PCM
to compare the complete output chain. The underlying source suites are `CnaMixerTests.cpp`,
`CnaMixerXnaTests.cpp`, `AlsaAudioDeviceTests.cpp` and
`AlsaAudioRecordingDeviceTests.cpp`. Some device tests skip when libasound is unavailable;
a skip is not a successful device measurement.

Recorded earlier Linux runs cover sample/codec expectations, null/file pacing, stop barriers,
capture bytes and media integration. This documentation audit rechecked source/registration rules
and the eight `CnaAudioPlatformSelection_*` tests; it did not rerun an ALSA-enabled runtime build.
PipeWire/PulseAudio routing, physical playback/capture, hardware latency, real-load underruns and
non-stereo physical devices remain separately unqualified.
