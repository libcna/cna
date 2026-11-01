# CNA C API Audio

The C audio surface is split between [`audio.h`](../../modules/c-api/include/CNA/C/audio.h) for
sound effects, streaming, capture and 3D positioning, and
[`xact.h`](../../modules/c-api/include/CNA/C/xact.h) for authored XACT engines, banks, categories
and cues. This page records the shared ownership and behavioural contract; the declarations remain
the source of truth for individual arguments and result codes.

## Playback availability and ownership

`cna_audio_get_capabilities` is the playback preflight. It validates an active game and probes the
real process-wide mixer; it does not infer support from the operating system, renderer or build
name. No playback device is an ordinary successful snapshot with
`is_playback_available == CNA_FALSE`. The probe creates no owned C resource, although its first
call may initialize the native mixer.

Every sound effect, controllable instance, streaming instance and XACT engine is an owned game
child. A normal instance is also a child of its sound effect; a dynamic instance has no parent
effect because the caller supplies its samples. XACT banks and category handles are children of
their engine, and an owned cue is a child of its sound bank. Parents refuse destruction while a
tracked C child remains live. Successful destruction invalidates the generation-checked handle
immediately, and a second destroy returns `CNA_RESULT_INVALID_HANDLE`.

The direct sound-effect routes cover four native constructions:

- `cna_sound_effect_create_pcm16` copies a nonempty complete sequence of interleaved signed PCM16LE
  mono or stereo frames;
- `cna_sound_effect_create_pcm16_range_ext` copies the caller's bytes and constructs from a
  validated range with an explicit loop region;
- `cna_sound_effect_create_from_encoded_ext` copies encoded file bytes and asks the configured
  audio backend to decode them;
- `cna_sound_effect_create_from_asset_ext` decodes a UTF-8 path from disk. An empty path
  deliberately creates the canonical silent effect rather than failing.

The range route rejects a negative offset, empty count, negative loop values or a range outside the
supplied buffer before native decoding. Encoded-format support is build-dependent; an unsupported
codec or unavailable playback device returns `CNA_RESULT_NOT_SUPPORTED` and leaves the output
handle invalid.

`cna_content_manager_load_sound_effect` is a fifth acquisition path outside `audio.h`. It maps the
canonical content load, does not cache the result, and returns a new independently owned game child
on every success. All acquisition paths produce the same sound-effect handle kind and use the same
query, playback and destruction routes.

The two `cna_sound_effect_get_sample_*` functions are pure static computations and need no handle.
Their dynamic-instance counterparts are instance methods that use the streaming instance's own
sample rate and channel count.

## Sound effects and controllable instances

A sound-effect handle exposes duration, name, disposal state, type identity, controllable-instance
creation and fire-and-forget playback. The two fire-and-forget routes preserve the canonical
asymmetry: pan is range-checked in `[-1, 1]`, while pitch is clamped. They return success plus
`out_played == CNA_FALSE` when the disposed/native state declines playback; an instance-limit
failure maps to `CNA_RESULT_INVALID_STATE`.

`CNA_SoundEffectInstanceInfo` snapshots transport and caller-visible properties together. Instance
behaviour is:

- finite volume is passed through without clamping;
- pitch and pan are restricted to `[-1, 1]`;
- looping may be changed only before playback begins;
- `Play` on an already playing instance is a no-op;
- `Resume` resumes a paused instance and starts an unplayed one;
- immediate stop cuts playback off, while non-immediate stop exits a loop and allows the current
  sound to finish.

The native backend may fail to allocate a playback voice without throwing. The control call then
succeeds and the next info snapshot reports `CNA_SOUND_STATE_STOPPED`, matching the native void
`Play` contract. Destroying an instance stops and detaches its track before invalidating the C
handle; the parent effect cannot be destroyed until every C-created instance has gone.

## Threads, callbacks and errors

Audio handles use the game creation thread by default. The explicit exceptions are:

- `cna_sound_effect_play` and `cna_sound_effect_play_with_settings` accept a live effect handle from
  any thread and execute directly rather than entering the game-thread queue;
- pure value helpers such as the static sample calculations and emitter/listener initialization may
  run on any thread;
- when a game enables foreign-thread service, other qualifying handle calls may block and execute
  on the game thread.

Copying input bytes does not itself change thread affinity. In particular, a producer thread may
reuse a submitted streaming buffer after the route returns, but calling that route from the producer
requires the opt-in game-thread service queue. Its pumping and shutdown rules are in
[`CALLBACKS_AND_THREADING.md`](CALLBACKS_AND_THREADING.md).

The native mixer may work on an internal audio thread, but it never invokes these C event handlers
from that real-time mixer callback. Buffer-needed events are raised by whichever owner thread
advances the dynamic queue; microphone events are raised when capture buffers are checked; XACT
disposal events run synchronously during disposal. Every audio subscription returns an owned
registration released by `cna_audio_unsubscribe_ext`. The registration retains its source storage
where needed, so it may be released safely after observing source disposal; its caller-owned
context must remain valid until release or the documented source lifetime ends.

The exception firewall maps `NoAudioHardwareException` and `NoMicrophoneConnectedException` to
`CNA_RESULT_NOT_SUPPORTED`, and `InstancePlayLimitException` to
`CNA_RESULT_INVALID_STATE`. A missing playback device therefore has the same result semantics
regardless of which creation or playback route encounters it.

## Dynamic streaming

A `DynamicSoundEffectInstance` uses the ordinary sound-effect-instance handle kind. All
`cna_sound_effect_instance_*` transport, property, type and destroy routes accept it; streaming-only
routes reject an ordinary instance with `CNA_RESULT_INVALID_STATE`.

PCM16 byte submissions and float submissions copy the selected range before returning. One
instance uses the sample representation established by its first submission; callers must not mix
byte and float feeds. The pending-buffer count decreases only after playback consumes a buffer, not
when the buffer is merely handed to the mixer.

`cna_framework_dispatcher_update` advances every live dynamic instance during the normal game
loop. The explicit queue-initialization, clear and update routes exist because the canonical
operations are public and support a host that drives them directly. A buffer-needed handler runs
on the thread that performs that update and can submit more copied data before returning.

## Microphone capture

Microphones are runtime-owned and addressed by zero-based index. A machine with no capture device
successfully reports a count of zero. The default query separates availability from identity: when
there is no default it clears `out_available` and leaves `out_index` unchanged. Index routes reject
an index at or beyond the reported count.

`cna_microphone_get_data_at` is a stream read rather than an all-or-nothing copy. It writes up to
the supplied capacity and reports the actual byte count; a short or zero-byte read is success, not
`CNA_RESULT_BUFFER_TOO_SMALL`. The type-name routes remain available even when the device count is
zero.

Buffer-ready registrations use the shared audio-registration handle. The normal framework
dispatcher checks all microphones; `cna_microphone_check_all_buffers_ext` exposes the same public
canonical operation to manually driven hosts.

## 3D positioning

`CNA_AudioEmitter` and `CNA_AudioListener` are versioned values rather than handles. Their init
routes produce the canonical origin, forward `-Z`, up `+Y` and zero velocity; only the emitter has
a per-emitter Doppler scale, defaulting to 1. Master volume, distance scale, Doppler scale and speed
of sound are process-wide statics. Their C routes take a game only for owner-thread validation.

An instance must choose 3D positioning before its first `Play`. An already playing instance that
was never positioned returns `CNA_RESULT_INVALID_STATE`; stop it, position it and play again. An
instance positioned before playback may be repositioned while it plays. Its spatial attenuation,
pan and Doppler values remain latched and are recombined with later volume and pitch changes.
Once a submitted voice is using 3D, `cna_sound_effect_instance_set_pan` is refused and leaves the
stored pan unchanged until `Stop` permits the playback mode to change.

Distance attenuation stays at full volume within the process-wide distance scale and follows a
computed inverse law beyond it. Pan projects the emitter offset onto the listener's own right axis,
so listener orientation matters. The multi-listener route accepts any positive count. CNA evaluates
every listener and uses the nearest/loudest one because its stereo mixer has no XACT-equivalent
per-listener output matrix; zero listeners are explicitly refused because XNA's native outcome for
that case is not established.

## XACT authored audio

The XACT surface uses content-version-46 `.xgs` settings alongside `.xwb` wave banks and `.xsb`
sound banks. Missing files map to `CNA_RESULT_IO`; malformed headers are rejected before a handle
is published. The C surface covers the engine, renderer-detail views, global variables, categories,
resident and streaming wave banks, sound banks, cues, 3D cue playback and disposal events.

Renderer details remain owned by their engine and are addressed by index. Friendly name, identifier,
text, hash and equality use query/copy/value routes rather than exposing the native type. This
runtime has one audio backend; the explicit constructor's look-ahead and renderer-id arguments are
accepted for canonical signature compatibility but do not select or configure another backend.

Engine-global and cue-scoped variables are separate domains. Both clamp writes to their authored
ranges. Writing a read-only global variable succeeds without changing it because the canonical API
does not report the underlying refusal. Category handles are owned C copies of canonical values;
releasing one affects neither playback nor the engine, and categories compare by name.

`cna_sound_bank_get_cue` returns a new independently controllable prepared cue on every call.
`cna_sound_bank_play_cue` and its 3D sibling create no C handle; the sound bank retains those cues,
and `cna_audio_engine_update` retires them after playback. `CNA_CueInfo` snapshots all eight state
predicates together. A prepared cue starts with `is_prepared` rather than `is_created`, and a paused
cue remains playing while `is_paused` is also true.

`AudioEngine::InstanceLimitDecision` has no C representation because every producer and consumer
of that public native value is internal to cue playback. A caller observes only the resulting cue
play decision.

## Shutdown order

For deterministic shutdown:

1. stop producer/capture work and release audio event registrations that no longer need to observe
   disposal;
2. destroy owned cues before sound banks, then wave/sound banks and category handles before their
   XACT engine;
3. destroy ordinary instances before their parent effects, and destroy parentless dynamic
   instances;
4. destroy every remaining sound effect and XACT engine before `cna_game_destroy`;
5. release any disposal registration deliberately retained across its source's destroy.

The game refuses destruction with `CNA_RESULT_INVALID_STATE` while any tracked owned audio resource
remains. A refused destroy changes neither the game nor its children.

## Verification and current limitations

The focused strict-C regressions are:

- `CApi_AudioSmoke`: availability, full-buffer PCM ownership, instance state, parent ordering and
  owner-thread refusal;
- `CApi_AudioSoundEffectSmoke`: ranged PCM, encoded and path acquisition, static settings, sample
  math, metadata and any-thread fire-and-forget playback;
- `CApi_ContentSmoke`: content-manager sound acquisition and interoperability with the ordinary
  sound-effect handle routes;
- `CApi_AudioStreamingSmoke`: dynamic instances, copied byte/float submissions, event registration
  and the enumerated microphone boundary;
- `CApi_Audio3DSmoke`: value defaults, validation, positioning order, latching and multi-listener
  acceptance;
- `CApi_XactSmoke`: authored resident XACT fixtures, renderer views, variable domains, categories,
  cues, ownership and disposal handlers;
- `CApi_AudioUnavailableSmoke`: repeatable no-device capability snapshots and atomic
  `CNA_RESULT_NOT_SUPPORTED` creation failure.

The remaining qualification and implementation limits are:

- microphone tests exercise whatever the host reports, but the committed deterministic path has no
  positive capture-device fixture;
- `Microphone::Start` currently sets `Started` even when native capture opening fails, rather than
  reporting the measured XNA failure;
- CNA has no playback-device-change or device-loss recovery of its own; SDL default-device handling
  and ALSA device disappearance remain backend behaviour;
- the XACT regression positively validates a resident wave bank. Its streaming-wave-bank route has
  atomic missing/malformed-file coverage, but the authored resident fixture is allowed to be
  non-streamable, so it is not a positive streaming-XWB qualification.

The microphone-start defect and playback-device-loss limitation are also recorded in
[`handoff.md`](../../handoff.md) section 5 until resolved and regression-tested.
