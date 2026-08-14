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
