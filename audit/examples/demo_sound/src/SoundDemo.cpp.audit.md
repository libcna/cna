# Audit: examples/demo_sound/src/SoundDemo.cpp

## Metadata
- Source file: `examples/demo_sound/src/SoundDemo.cpp` (436 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-demo_sound` shard
- File type: standalone `Game`-subclass demo implementation
- XNA/FNA relevance: exercises `SoundEffect::Play()`/`Play(volume,pitch,pan)`/`CreateInstance`,
  `SoundEffectInstance::Play`/`Pause`/`Resume`/`Stop`/`Apply3D`/volume-pitch-pan properties,
  `DynamicSoundEffectInstance::SubmitBuffer`/`Play`/`Stop`/`Update`/`BufferNeeded`
- Related production code: `SoundEffect.hpp`, `DynamicSoundEffectInstance.hpp` (read directly this
  pass for cross-reference; both part of the already-fully-audited `xna-audio` shard)

## Purpose
Exercises every major `SoundEffect`/`SoundEffectInstance`/`DynamicSoundEffectInstance` code path via
keyboard controls (fire-and-forget play, looping-instance play/pause/stop, volume/pitch/pan, 3D
positional audio, and a hand-generated dynamic sine-wave stream).

## Executive Verdict
Correct. Two points independently verified against the production headers rather than assumed:
1. **`SoundEffect(const std::string&)` (line 21 ctor list)** is confirmed `NOXNA`-tagged in
   `SoundEffect.hpp` (line 66), with the header's own comment confirming it is a placeholder-name
   constructor, not the real content-loading path — `LoadContent()` correctly uses
   `getContentProperty().Load<SoundEffect>(...)` (lines 47/52/57) for actual loading, matching real
   XNA's content-pipeline-based loading model.
2. **`DynamicSoundEffectInstance::Update()` (called at line 253, gated on `dynamicPlaying_`)** is
   confirmed `NOXNA`-tagged in `DynamicSoundEffectInstance.hpp` (line 166), whose own doc comment
   ("Pumps stream data and raises `BufferNeeded`...") confirms `BufferNeeded` fires synchronously
   from this call — i.e., from the game thread, during this demo's own `Update()` — not from a
   background audio-mixing thread. This resolves a plausible-looking concern (a `this`-capturing
   lambda subscribed to `BufferNeeded`, line 71-74, racing background-thread firing against demo
   teardown) as a non-issue: the callback can only fire synchronously from a call this demo already
   controls, never after `Update()` stops being called.

## Checklist Results
- `[5] Apply3D to beep instance` (lines 176-183) constructs a **local, non-owned**
  `SoundEffectInstance inst = beep_.CreateInstance();`, applies 3D positioning, sets volume, calls
  `Play()`, and lets `inst` go out of scope at the end of the `if` block — the same
  short-lived-temporary pattern real XNA's own `SoundEffect::Play(volume, pitch, pan)` fire-and-forget
  overload uses internally (playback is expected to continue independently of the C++/CLR wrapper
  object's lifetime). Consistent usage, not a leak or a premature-stop bug, assuming
  `SoundEffectInstance`'s playback backing survives its wrapper's destruction — the same assumption
  the `xna-audio` shard's own audit of `SoundEffectInstance`/`SoundEffect::Play()` already had to
  make and (per that shard being closed with no HIGH findings) evidently confirmed.
- `dynamicInstance_`'s `BufferNeeded` lambda closes over `this`; member destruction order
  (`dynamicInstance_` destructs before `~SoundDemo()`'s body already ran, per reverse
  declaration order) is safe because `Update()` (the only trigger for the callback) is never called
  during destruction.
- `GenerateSineBuffer` (lines 97-116) produces correct little-endian 16-bit PCM (`val & 0xFF` then
  `(val >> 8) & 0xFF`), matching the required on-disk/in-buffer byte order for
  `DynamicSoundEffectInstance` regardless of host endianness (x86_64 is LE, so this is also
  incidentally native-order, but the code doesn't rely on that — the byte-splitting is manual and
  correct either way).
- Fade envelope (`std::min(1.0f, t*10) * std::min(1.0f, (duration-t)*10)`) correctly produces a
  10%-of-duration linear fade-in and fade-out, avoiding a click at buffer boundaries — appropriate
  engineering for a synthetic tone generator.

## Detailed Findings
None.

## Cross-File Observations
This demo's `dynamicInstance_->Update()` call is the exercised, correct use of a documented NOXNA
extension (`DynamicSoundEffectInstance::Update()`) that real XNA does not require the application to
call — real XNA's dynamic buffering is automatic/internal. This is a known, already-disclosed
(NOXNA-tagged) architectural deviation in CNA's `DynamicSoundEffectInstance`, not something this
demo introduces; the demo simply participates correctly in the documented manual-pump contract.

## Missing or Weak Tests
Not applicable — manual/visual demo; the underlying `DynamicSoundEffectInstance`/`SoundEffectInstance`
classes are unit-tested elsewhere (`xna-audio`/`tests-xna-audio` shards).

## Positive Findings
Correct, idiomatic use of the fire-and-forget temporary-instance pattern for one-shot 3D audio, and
a correctly-reasoned (verified against the actual header, not assumed) callback-safety analysis for
the dynamic-buffer lambda.

## Final Assessment
No findings.
