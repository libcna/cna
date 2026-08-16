# CNA C API Audio Resources and Control

## PCM sound ownership

`cna_sound_effect_create_pcm16` copies a nonempty sequence of complete interleaved signed 16-bit
little-endian channel frames into an owned native `SoundEffect`. The initial entry point accepts
mono or stereo data and a positive sample rate. It deliberately does not reinterpret WAV, Ogg,
MP3, XNB or another container as raw samples; container/content loading remains a later mapping.

The game argument supplies a creation-thread and lifetime parent even though audio does not borrow
a graphics device. A sound effect must be destroyed before that game. Every controllable instance
must in turn be destroyed before its parent effect; parent destruction is refused while a C
instance is live. All handles are generation-checked and become stale immediately after successful
destruction.

## Instance control

An instance supports play, pause, resume, immediate stop, release-tail stop, volume, pitch, pan and
full-effect looping. `CNA_SoundEffectInstanceInfo` snapshots the current state and properties:

- volume follows CNA/FNA pass-through behavior and is not clamped by the adapter;
- pitch follows CNA's clamp to `[-1, 1]`;
- pan is validated in `[-1, 1]`;
- looping may be changed only before playback has begun;
- `Play` while already playing is CNA's no-op, and `Resume` starts an unplayed instance.

The native backend can fail to allocate a playback voice without throwing; in that case the
control call succeeds and the following info snapshot reports `STOPPED`, matching the underlying
void `SoundEffectInstance::Play()` contract.

## Threads, hardware and shutdown

Creation, query, control and destruction calls all run on the game creation thread. A wrong-thread
call returns `CNA_RESULT_THREAD` before touching mixer state. SDL/CNA may mix and finish tracks on
an internal audio thread, but this initial API installs no C callback and retains no caller context.

`cna_audio_get_capabilities` is the stable preflight for playback availability. It validates an
active game and probes CNA's real process-wide native mixer rather than guessing from a renderer,
operating system or compile-time name. The first query may initialize that mixer. A missing device
is represented by `CNA_RESULT_SUCCESS` plus `is_playback_available == CNA_FALSE`; malformed ABI
structures, stale handles and wrong-thread calls remain errors. The query creates no owned C
resource, so a false result does not prevent clean game destruction.

Instance destruction calls native `Dispose`, which stops and detaches its track. CNA/SDL performs
any mixer-iteration-safe internal cleanup; when the C call returns, the C handle is invalid and no
later mixer activity can call into user C code. A non-immediate `Stop` merely exits a loop and lets
the current sound finish; destroying that instance still cuts off and releases it.

When no audio device can be opened, effect creation returns `CNA_RESULT_NOT_SUPPORTED` and leaves
the output handle invalid. The positive regression uses SDL's dummy audio driver so it exercises
real availability probing, mixer creation and track transitions without requiring speakers. A
separate regression process forces a nonexistent SDL audio driver and proves repeatable successful
unavailable snapshots followed by `NOT_SUPPORTED` creation, invalid output handles, structured
diagnostics and clean game shutdown after failed mixer initialization.

## Current boundary

The initial slice does not yet expose file/content sound loading, fire-and-forget overloads, master
or 3D properties, listeners/emitters, dynamic streaming, microphones, XACT engines/banks/cues, or
audio events. They remain planned for the complete public CNA inventory and are not implied by the
presence of this minimal PCM/instance route.

## The complete sound-effect surface

A sound effect can now be created four ways, and the difference is what the bytes are: raw PCM16
(whole buffer, or an explicit range with a loop region), an **encoded file already in memory**, or a
**path on disk**. The canonical stream factory takes a C++ stream and reads it to the end, so the C
route takes the bytes it would have read; what it accepts is whatever the audio backend can decode,
which is more than the raw PCM the other routes take.

Two canonical behaviors are reported rather than evened out:

- **Pan is range-checked and pitch is clamped.** `cna_sound_effect_play_with_settings` refuses a pan
  outside -1 to 1 and silently accepts any pitch, because that is exactly what the canonical route
  does.
- **An empty asset path is not an error.** The canonical constructor answers an effect with no audio
  rather than throwing, so `cna_sound_effect_create_from_asset_ext` reports success with a silent
  effect.

The C range route adds one check the canonical constructor does not have: a negative offset, an empty
count or a range that leaves the buffer is refused before the decoder ever sees a length nobody
validated. That is argument validation at the boundary, which is this ABI's job, not a behavioral
change.

### The four 3D-audio settings belong to the process

Master volume, distance scale, Doppler scale and speed of sound are canonical **statics**. Their
routes take a game handle for thread affinity only — setting one changes every sound effect in the
process, including ones created later. The two sample computations (`..._get_sample_duration_ticks`
and `..._get_sample_size_in_bytes`) are static too, and take no handle at all.

### Both audio exceptions now convert at the boundary

`NoAudioHardwareException` becomes `CNA_RESULT_NOT_SUPPORTED` and `InstancePlayLimitException`
becomes `CNA_RESULT_INVALID_STATE`, in the exception firewall rather than in one creation route that
happened to catch the first one locally. Every audio route gets the same conversion.

## Streaming: one handle kind, two shapes

A streaming instance is a **sound-effect instance**. It lives under the same handle kind, so every
`cna_sound_effect_instance_*` route accepts it — transport, mixing setters, state, destroy — and the
canonical overrides dispatch virtually behind them. What the kind adds is the buffer queue, and what
it lacks is a parent: `cna_dynamic_sound_effect_instance_create` takes a sample rate and a channel
count, because the caller is the source of every sample. A route that needs streaming refuses an
ordinary instance with `CNA_RESULT_INVALID_STATE`, which is how a caller tells the two apart.

**Submitted buffers are copied.** The caller's array may be reused or freed the moment the call
returns, which is what makes submission safe from a producer thread while playback runs — the
canonical usage. Both the byte and float routes take an explicit range, covering both canonical
overloads with one route each.

The two sample computations here are **instance methods**, unlike the sound-effect ones: they use the
rate and channel count the instance was created with rather than taking them as arguments.

The pending buffer count only shrinks once a buffer has actually been **consumed by playback**, not
when it was handed to the mixer. That is the canonical contract and the reason the count is worth
polling at all.

## Capture is enumerated, and a short read is not a failure

Microphones are addressed by **index**, like every other enumerated device in this ABI, because the
canonical list hands out pointers the runtime owns and never transfers. The default microphone
follows the availability-separate-from-the-answer rule: no default is an ordinary success with the
flag clear and the index left exactly as the caller set it.

`cna_microphone_get_data_at` is the one count/copy route in this ABI where **a short read is not a
failure**. Every text route refuses a buffer it cannot fill; capture is a stream, so this one fills
what it can and reports how much arrived. Zero bytes means nothing has been captured yet, not an
error.

**No verification tree has a capture device**, so the count is zero and every index route refuses.
That is the microphone's real availability rather than a gap in the binding — the same honesty the
compass and the camera already record. The type-name routes still answer, because a type name
belongs to the type rather than to a device.

`NoMicrophoneConnectedException` joins the other two audio exceptions in the firewall: no microphone
connected is `CNA_RESULT_NOT_SUPPORTED`.

## Positioning: two values and one supported listener

An emitter and a listener are **fixed values, not handles**. The canonical types carry settings and
no behavior — no identity, no lifetime, nothing to observe between calls — so `CNA_AudioEmitter` and
`CNA_AudioListener` are structures the caller fills in, and `cna_audio_emitter_init` /
`cna_audio_listener_init` write the canonical defaults: at the origin, facing -Z with +Y up, at rest,
and (for the emitter alone) a Doppler scale of 1. The listener has no Doppler scale of its own, which
is why the two are separate structures rather than one shared one.

`cna_sound_effect_instance_apply_3d` positions a playing instance against one listener. The
attenuation it applies is **full volume within the process-wide distance scale and a computed inverse
law beyond it**, not a falloff that starts at zero distance, and pan is the emitter's offset projected
onto the listener's own right axis — so which way the listener faces changes the result. The four
process-wide settings are what this route reads, which is what makes them worth setting.

**Positioning latches.** Once an instance has been positioned, the spatial gain, pan and pitch it
computed are combined with the instance's own settings on every later call, and `..._set_pan` stops
reaching the output. The instance's properties keep reading back whatever the caller last set them
to; the output is driven by the position from then on. That is the canonical behavior, reported here
rather than hidden.

`cna_sound_effect_instance_apply_3d_multi_ext` covers the array overload, and it is `_ext` because it
reports a limit a C caller could not otherwise see: **this runtime supports exactly one listener**.
The canonical overload accepts the array XNA's split-screen API needs and then refuses every count
but one, so a count of zero or two is `CNA_RESULT_NOT_SUPPORTED` rather than a silent fallback to the
first listener. A null array is an argument failure, which is a different answer from an empty one.

`RendererDetail` is not mapped here. Its constructor is private and its only source is
`AudioEngine::getRendererDetailsProperty`, so it cannot be reached without an engine — it belongs
with the XACT surface, and is recorded there.
