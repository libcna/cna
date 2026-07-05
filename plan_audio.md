# plan_audio.md — Completing and reviewing the FNA Audio → CNA port (C++ / XNA 4.0)

> **Scope:** exclusively `Microsoft::Xna::Framework::Audio` + internal `CNA::Internal::Audio`.
> **The Media namespace is NOT part of this plan.**
>
> **Goal:** go through the audio files one by one against the authoritative FNA source, fill in
> missing implementation, fix bugs and deviations, align with `CLAUDE.md`/`CHECKLIST.md`, and add
> **complete tests** (today there isn't a single test for audio). No stubs without a documented
> reason — every conscious deviation from FNA must be documented in `CHECKLIST.md` (the accepted
> deviations table).

- **FNA reference (authoritative):** `/rv/data/library/github.com/FNA-XNA/FNA/src/Audio/`
- **Backend:** CNA audio runs on **SDL3_mixer**, not FAudio/FACT. XACT (.xgs/.xsb/.xwb) is parsed
  by a custom `XactParser` and mixed via SDL_mixer. This is the main source of deviations (3D
  HRTF, Doppler, streaming wavebanks, FACT DoWork).
- **Status per AUDIT.md:** everything "✅", but with "stub behavior" notes on
  AudioEngine/Cue/SoundBank/WaveBank — this plan breaks those notes down into concrete tasks.

---

## 1. File inventory and current status (Audio only)

> **Note (2026-07-02):** this table is a snapshot from the **original** audit (before phases 1–5).
> Most rows have since been fixed — for the current status trust the checkboxes in §4, not the
> "Status" column here (exception: row 15 Microphone is corrected below, since it was directly
> affected by the most recent audit). New findings from the supplementary audit (2026-07-02) are
> in **Phase 7**.

| # | File | FNA line | Status | Main gaps |
|---|--------|-------:|------|---------------|
| 1 | SoundEffect | 821 | Functional with deviations | no `Instances`/Dispose cascade; `CreateInstance` returns by value; wrong exception types; `Play()` bypasses the instance |
| 2 | SoundEffectInstance | 652 | Partial | missing internal DSP (reverb/filters); Pan/Volume clamps instead of validating; public ctor (FNA internal); wrong exceptions |
| 3 | SoundEffectI.hpp | — | Unjustified extension | non-XNA type in the XNA namespace without NOXNA; no polymorphic use |
| 4 | DynamicSoundEffectInstance | 326 | Partial — **bugs** | `setIsLoopedProperty` doesn't override; missing `Dispose()` override → leak; missing `SubmitFloatBufferEXT` guard; duplicate `disposed_` |
| 5 | SoundState.hpp | 19 | Complete | OK |
| 6 | AudioEngine | 445 | Partial (parsing OK, mixing stub) | wrong exceptions; `GetCategory`/`SetGlobalVariable` don't throw on unknown name; `Update()` no-op; `GetTypeName` uses `::` |
| 7 | SoundBank | 284 | Partial / stub | `IsInUse` hardcoded `false`; `GetCue` returns a stub instead of throwing; 3D `PlayCue` ignores listener/emitter; raw `Cue*`; wrong exceptions; `::` |
| 8 | WaveBank | 226 | Partial / stub | `IsInUse` always `false`; streaming ctor ignores offset/packetSize; wrong exceptions; `::` |
| 9 | Cue | 305 | Partial / stub | `Apply3D` no-op; `GetVariable`/`SetVariable` without XSB validation; wrong exceptions; `::` |
| 10 | AudioCategory | 144 | Nearly complete (semantic drift) | `Equals` compares parent+index instead of Name; missing `Equals(Object)`; doxygen claims "no-op" while the impl is functional |
| 11 | AudioEmitter | 158 | Complete (data class) | `DopplerScale` setter throws `std::out_of_range` |
| 12 | AudioListener | 118 | Complete (data class) | no behavior — no bugs |
| 13 | AudioChannels.hpp | enum | Complete | OK |
| 14 | AudioStopOptions.hpp | enum | Complete | OK |
| 15 | Microphone | 217 | Functional, T-4A done | real SDL3 capture (enumeration/Start/Stop/GetData) done; `GetSampleDuration`/`GetSampleSizeInBytes` now delegates to `SoundEffect` (MC-1, done 2026-07-03); a stale `friend class MicrophoneFactory` comment remains (MC-2) |
| 16 | MicrophoneState.hpp | enum | Complete | OK |
| 17 | RendererDetail | 79 | Partial | missing `Equals`; incomplete doxygen |
| 18 | InstancePlayLimitException | 35 | Partial | inherits `std::runtime_error`, should be `ExternalException` |
| 19 | NoAudioHardwareException | 35 | Partial | inherits `std::runtime_error`, should be `ExternalException` |
| 20 | NoMicrophoneConnectedException | 35 | Partial | inherits `std::runtime_error`, should be `System::Exception` |
| I1 | Internal/Audio/XactTypes.hpp | — | Scaffold | missing SPDX; `///` doc |
| I2 | Internal/Audio/XactParser.cpp | 809 | Large, **buggy** | missing SPDX; dead XGS first pass; **compact-XWB `dataLength` bug**; the track parser `break`s on unknown events |
| I3 | Internal/Audio/AudioMixer.hpp/.cpp | — | Thin SDL3_mixer wrapper | missing SPDX; single hardcoded 44100/stereo/S16 |

---

## 2. Accepted deviations (NOT fixed — only documented in CHECKLIST.md)

These follow from the choice of the SDL3_mixer backend and from C++ value semantics. The goal is
to **document** them, not remove them (unless decided otherwise in a task that explicitly mentions
them):

1. **3D HRTF / positional audio** — SDL_mixer has no FAudio F3DAudio. `Apply3D` / 3D `PlayCue` can
   only be approximated with pan + distance attenuation (`Mix_SetPosition`), without elevation
   information. (see T-F2)
2. **Doppler** — SDL_mixer doesn't support per-channel pitch shift based on velocity.
   `DopplerScale`/`Velocity` is stored but never applied.
3. **`GetHashCode()` → `int`/`size_t`** via `std::hash` — the value doesn't match C#'s
   `String.GetHashCode`, but is consistent (accepted per CHECKLIST.md).
4. **FACT `DoWork`** — category fades and instance limits from FACT `DoWork` are not implemented
   (per-cue category-volume re-apply is, though — see T-4D). Streaming `WaveBank` was added
   (T-3F, 2026-07-04): the streaming ctor reads header/metadata from disk and entry data lazily,
   not the whole file eagerly.
5. **`CreateInstance`/`FromStream` remain value-based** (no heap-reference semantics), but
   `SoundEffect` now has instance-tracking + a Dispose cascade and is move-only (T-3G, 2026-07-04).

> Each of these deviations must have a row in the deviations table in `CHECKLIST.md` (task T-6A).

---

## 3. Cross-cutting defects (one task, many files)

These appear across the cluster and are handled in bulk:

- **X1 — Mapping `std::*` exceptions → `System::*`.** sharp-runtime already provides
  `ArgumentNullException`, `ArgumentException`, `ArgumentOutOfRangeException`,
  `InvalidOperationException`, `ObjectDisposedException`, `NotSupportedException`,
  `NotImplementedException`, `Runtime::InteropServices::ExternalException`. Audio doesn't use them
  at all.
- **X2 — `GetTypeName()` uses `::` instead of `.`** in AudioEngine/SoundBank/WaveBank/Cue; the
  project convention is dotted (`Microsoft.Xna.Framework.Audio.X`), as `SoundEffectInstance`
  already has.
- **X3 — Missing SPDX** (`// SPDX-License-Identifier: MS-PL`) in all 4 internal files.
- **X4 — Zero tests** — the `tests/Microsoft/Xna/Framework/Audio/` directory doesn't exist. Tests
  are picked up into `CnaTests` via `GLOB_RECURSE tests/*.cpp` (CMakeLists.txt:1157), so it's
  enough to just create the files.

---

## 4. Tasks (by phase)

> Convention: each task has an **ID**, affected files, **FNA ref**, a description, **acceptance
> criteria incl. tests**, and a reference to the source audit (A#/B#/C#). Check off `[ ]` → `[x]`.

### Phase 0 — Test infrastructure and build bootstrap

- [x] **T-0A — Set up audio test infrastructure.**
  Create `tests/Microsoft/Xna/Framework/Audio/` + a first skeleton (no `CMakeLists` change needed
  — GLOB). Verify that an empty `*_test.cpp` registers into `CnaTests` and builds.
  *Accept:* `cmake --build cmake-build-debug --target CnaTests` compiles and runs the new (still
  trivial) test fixture. (source: A10/B11/C6)

### Phase 1 — Compliance sweep (low effort, high value)

- [x] **T-1A — SPDX in internal audio files.** `XactTypes.hpp`, `AudioMixer.hpp`,
  `XactParser.cpp`, `AudioMixer.cpp` → line 1 `// SPDX-License-Identifier: MS-PL`.
  *Accept:* all 4 start with SPDX; build unchanged. (B1, X3)

- [x] **T-1B — Dotted `GetTypeName()` convention.** `AudioEngine.cpp:268`, `SoundBank.cpp:151`,
  `WaveBank.cpp:263`, `Cue.cpp:249`: `::` → `.`.
  *Accept:* a test verifies `GetTypeName()=="Microsoft.Xna.Framework.Audio.<Class>"` for all 4.
  (B2, X2)

- [x] **T-1C — Add `GetTypeName()` to `Microphone`.** The class inherits `System::Object`, but the
  override is missing. Add `GetTypeNameHPP()` in the hpp and
  `GetTypeNameCPP(Microphone, "Microsoft.Xna.Framework.Audio.Microphone")` in the cpp.
  *Accept:* `mic->GetTypeName()=="Microsoft.Xna.Framework.Audio.Microphone"`. (C2)

- [x] **T-1D — Map exceptions to `System::*` (data/3D/mic classes).**
  `AudioEmitter::setDopplerScaleProperty` (`AudioEmitter.cpp:26`) → `ArgumentOutOfRangeException` —
  done. `Microphone::setBufferDurationProperty` → `ArgumentOutOfRangeException`; both
  `Microphone::GetData` bounds checks → `ArgumentException` — done (2026-07-01);
  `Microphone.cpp:64,98,103` no longer throw `std::out_of_range`.
  *FNA:* AudioEmitter.cs:33, Microphone.cs:62,171-179.
  *Accept:* tests verify the exact `System::` type for each error branch; valid input doesn't
  throw. (C3, X1)

- [x] **T-1E — Map exceptions to `System::*` (core playback).**
  SoundEffect.cpp:232,246 → `ArgumentOutOfRangeException`; :393,400,409 → `NotSupportedException`;
  :132 → `ArgumentException`. SoundEffectInstance.cpp:138 → `ObjectDisposedException` (or remove);
  :308 → `NotSupportedException`; :400 → `InvalidOperationException`.
  DynamicSoundEffectInstance.cpp:104 → `ObjectDisposedException`; :215,249 →
  `ArgumentOutOfRangeException`.
  *FNA:* SoundEffect.cs:82,98,412; SoundEffectInstance.cs:39,277.
  *Accept:* tests for the exact type on every branch. (A3, X1)

- [x] **T-1F — Map exceptions to `System::*` (XACT).**
  `std::invalid_argument`→`ArgumentNullException`; disposed→`ObjectDisposedException`;
  invalid-name→`InvalidOperationException`. Sites: AudioEngine.cpp:48,119,121,139,147,155;
  SoundBank.cpp:35,37,83,85,108; WaveBank.cpp:119,121; Cue.cpp:65,67,73,81,89.
  *FNA:* AudioEngine.cs:119/273, SoundBank.cs:66/174, WaveBank.cs:77, Cue.cs:170/202.
  *Accept:* tests for the exact type (null/empty arg, invalid name, use-after-dispose). (B3, X1)

- [x] **T-1G — Rebase the three audio exceptions onto sharp-runtime base classes.**
  `InstancePlayLimitException`, `NoAudioHardwareException` →
  `System::Runtime::InteropServices::ExternalException`; `NoMicrophoneConnectedException` →
  `System::Exception`. Remove the hand-written `innerException_`/`InnerException()` (the base
  already has `getInnerExceptionProperty()`); forward all 3 ctors (default, `(message)`,
  `(message, inner)`) to the base; the default ctor has no message of its own (per FNA).
  *FNA:* *.cs:24 for all three.
  *Accept:* a test/`static_assert` for `is_base_of<System::Exception, T>` (+ `ExternalException`
  for the first two); `getMessageProperty()`/`getInnerExceptionProperty()` work;
  `catch(const System::Exception&)` catches it. (C1, X1)

- [x] **T-1H — Microphone: NOXNA/visibility of internal members.** `micList` and `SAMPLERATE` (FNA
  `internal`) made private and `NOXNA` removed from them (they're FNA internals, not CNA
  extensions). Access for FrameworkDispatcher solved **without a friend** — a new public
  `NOXNA static void CheckAllBuffers()` on `Microphone` encapsulates the null-check + iteration +
  `CheckBuffer()`; `FrameworkDispatcher.cpp` now just calls
  `Audio::Microphone::CheckAllBuffers()` instead of manually walking `micList`.
  *FNA:* Microphone.cs:104,120,142. *Accept:* compiles for all callers; no public internal members.
  (C7)

### Phase 2 — Fixing real bugs

- [x] **T-2A — Fix the `DynamicSoundEffectInstance::setIsLoopedProperty` override.**
  The derived signature must exactly match the base virtuals (`const bool&` and NOXNA `bool&&`) +
  `override`, or the base should be unified onto one canonical setter. Today the `bool`-by-value
  signature **hides** instead of overriding, so calling through `SoundEffectInstance&` invokes the
  base version, which throws during playback.
  *FNA:* DynamicSoundEffectInstance.cs:31-41.
  *Accept:* through `SoundEffectInstance&`, setting IsLooped on a dynamic instance is a no-op and
  `getIsLoopedProperty()==false`, even during playback (doesn't throw). (A1)

- [x] **T-2B — `DynamicSoundEffectInstance::Dispose()` override + unify the disposed flag.**
  Override `Dispose()`: Stop → `DestroyStream()` → unregister from `FrameworkDispatcher::Streams`
  → chain to base. Remove the separate `disposed_`; base `getIsDisposedProperty()` on the base
  flag.
  *FNA:* DynamicSoundEffectInstance.cs:237-247.
  *Accept:* after `Dispose()`, `dynamicTrack_`/`audioStream_` are null and
  `getIsDisposedProperty()==true`; a second `Dispose()` is safe (idempotent). (A2)

- [x] **T-2C — `SubmitFloatBufferEXT` guard + float/stream format switching.**
  Throw `InvalidOperationException("Submit a float buffer before Playing!")` if `State != Stopped`
  and the stream is still int-format; `EnsureStream` must reflect `isFloat_` (recreate the stream
  on a format change before the first Play). Today an S16 stream fed F32 bytes → mismatch.
  *FNA:* DynamicSoundEffectInstance.cs:194-199.
  *Accept:* a float submit after Play on an int instance throws; a float-before-Play creates an
  F32LE stream; tests for both branches. (A4)

- [x] **T-2D — Fix compact-XWB `dataLength`.** `XactParser.cpp:424-426`: compute the length from
  consecutive offsets minus the deviation, not from `dev` alone; the last entry is
  `segLength[4]-offset`. Also check the `entryMetaDataSize < 24` branch (439-449), which today
  sets the entire data segment for every entry.
  *Accept:* a parser unit test on a known compact `.xwb` gives correct lengths (the PCM sample
  count matches). (B6)

- [x] **T-2E — Harden the track-event walker.** `XactParser.cpp:122-214`: instead of `break`ing on
  an unknown event (202-209), skip PITCH/VOLUME/MARKER/repeat so the first PlayWave is found even
  in a multi-event track.
  *Accept:* a `.xsb` with a leading non-play event still resolves the wave; a regression test for
  a simple single-event track. (B7)
  *Note:* the exact lengths of the PITCH/VOLUME(REPEATING)/MARKER(REPEATING) events were verified
  against FAudio (`FACT_internal.c:2390-2432`); only a genuinely unknown event type still
  `break`s.

- [x] **T-2F — Remove the dead XGS first pass and the redundant re-seek.**
  Delete `XactParser.cpp:270-289` (keep 292-310); read `variationOffset` from the header (line
  536) instead of re-seeking to `0x32` (641-647).
  *Accept:* the XGS test parses categories/variables identically; no behavior change. (B8)
  *Note:* the new `XactParserTest.XgsParsesCategoryAndVariable` was verified on both the ORIGINAL
  (pre-cleanup) and the new code with the same result (`git stash`) — confirmed no behavior
  change.

- [x] **T-2G — AudioCategory: fix `Equals` + add `Equals(Object)` + fix the doxygen.**
  Make `Equals` compare by `name_` (like FNA's name hash), not by `parent_+index_`
  (`AudioCategory.cpp:43`). Update the hpp doxygen (`AudioCategory.hpp:25-36`) — describe the real
  category routing, not "no-op".
  *FNA:* AudioCategory.cs:109-126.
  *Accept:* equality tests for equal/unequal by name; consistency of `==`/`!=`/`GetHashCode`.
  (B10)
  *Note:* `Equals(Object)` intentionally not added — `AudioCategory` doesn't inherit
  `System::Object` (same as `Vector2` and other `IEquatable`-only value types in the project), so
  a C# `Equals(object)` override has no equivalent here.

### Phase 3 — API and behavior fidelity

- [x] **T-3A — Throw on invalid name (XACT lookups).**
  `AudioEngine::GetCategory` (`:131`) and `SoundBank::GetCue` (`:95-98`) should throw
  `InvalidOperationException` instead of returning a stub; `AudioEngine::SetGlobalVariable`
  (`:158`) and `Cue::SetVariable`/`GetVariable` should validate the name against the parsed set.
  *FNA:* AudioEngine.cs:271-276/321-326, SoundBank.cs:174, Cue.cs:200-243.
  *Accept:* test valid→success, invalid→`InvalidOperationException`;
  `Cue::GetVariable("Distance")` on a built-in variable doesn't throw (add built-in cue
  variables). (B4)

- [x] **T-3B — Real `IsInUse` for SoundBank and WaveBank.**
  `SoundBank.cpp:71` → true while an owned cue/fire-and-forget is playing; `WaveBank.cpp:166` →
  true while a `SoundEffectInstance` it produced is playing.
  *FNA:* SoundBank.cs:28-36, WaveBank.cs:39-47.
  *Accept:* test: play a cue → `IsInUse==true`; after stop/elapse → `false`. (B5)

- [x] **T-3C — Pan/Volume semantics in SoundEffectInstance.**
  Pan setter: `ObjectDisposedException` when disposed and `ArgumentOutOfRangeException` when
  `|value|>1` (instead of silently clamping); decide Volume clamp vs. pass-through (FNA is
  pass-through). If clamping stays as a conscious SDL choice, record it in the deviations table.
  *FNA:* SoundEffectInstance.cs:46-83,124-142.
  *Accept:* behavior per the decision; tests for in-range, out-of-range, disposed. (A5)

- [x] **T-3D — `RendererDetail::Equals` + add doxygen.**
  Add `[[nodiscard]] bool Equals(const RendererDetail&) const` (by `rendererId_`, consistent with
  `operator==`); add `@return`/`@param` to `ToString`/`GetHashCode`/`operator==`/`operator!=`.
  *FNA:* RendererDetail.cs:48-52.
  *Accept:* `Equals` is true for the same `RendererId`, otherwise false; equal/unequal test. (C4)

- [x] **T-3E — Resolve `SoundEffectI`.**
  Preferred: delete `SoundEffectI.hpp`, put `CreateInstance()` directly on `SoundEffect` (no pure
  virtual), update the include in SoundEffect.hpp and Cue.cpp. If kept, move it into the `CNA::`
  namespace and tag it `NOXNA`.
  *Reason:* not XNA API, and the only caller (`Cue.cpp:186`) holds a concrete `SoundEffect*`.
  *Accept:* no unwrapped non-XNA abstract type in the XNA namespace; build green; Cue compiles.
  (A6)

- [x] **T-3F — Streaming WaveBank: offset/packetSize.**
  Either implement a real streaming ctor (`WaveBank.cpp:149-155` today delegates to the in-memory
  path and ignores offset/packetSize), or explicitly document it as an accepted deviation
  (everything loaded into memory).
  *FNA:* WaveBank.cs:104-143.
  *Accept:* a documented decision; if implemented, a test for correctly offset reads. (B from #3)
  *Note:* Done 2026-07-04 -- real streaming implemented (the decision was to "implement", not
  document a deviation). New `ParseXwbStreamingHeader(path)` (XactParser.cpp) reads only segments
  0-3 from disk (bank data/entry metadata/seek tables/entry names); segment 4 (wave data,
  typically by far the largest part of the file) is not loaded; `XwbData` has new
  `streaming`/`sourcePath` fields. `WaveBank::GetSoundEffect()` then, for a streaming bank, reads
  a given entry's data lazily, straight from disk (`sourcePath`, seek to `entry.dataOffset`, read
  `entry.dataLength` bytes), instead of slicing `fileData` (which for a streaming bank only holds
  the header/metadata). The non-streaming ctor is unchanged (whole file eager, like FNA).
  `offset`/`packetSize` remain unused -- confirmed against the FNA reference
  (`WaveBank.cs:104-143`) that FNA itself never copies these two parameters into
  `FACTStreamingParameters` (only `.file`), so matching FNA means they should stay dead in CNA
  too. Tests: `WaveBankTest.NonStreamingCtorLoadsEntireFileIntoMemory`,
  `StreamingCtorDoesNotLoadWaveDataSegmentIntoMemory` (memory footprint via the new
  `WaveBankTestAccess`/`StreamingInternal`/`ResidentFileBytesInternal`),
  `StreamingGetSoundEffectReadsCorrectPerEntryOffsetAndLength` (a two-entry fixture with different
  length/offset -- also catches a "reads the wrong but equally long range" bug, not just "doesn't
  read at all"). Verified with an ASan+LeakSanitizer build (no leak/UB in the new file-I/O code)
  and via the `git stash` methodology (without the `WaveBankTestAccess` fix it doesn't even
  compile, since `StreamingInternal` etc. don't exist -- a genuine compile failure, not just a
  failing assert).

- [x] **T-3G — SoundEffect: instance tracking + Dispose cascade (decision).**
  Either track live instances (weak refs) and have `SoundEffect::Dispose()` stop/dispose them
  (FNA), or formally document the value-semantics deviation (no cascade) in CHECKLIST. Tied to the
  decision on `CreateInstance` by value vs. heap-ref and `FromStream` ownership.
  *FNA:* SoundEffect.cs:126,315-323,354,389.
  *Accept:* a documented decision; if tracking, disposing the SoundEffect stops its instance
  (test). (A7)
  *Note:* Done 2026-07-04 -- the decision was "implement" (the user), not document a deviation.
  `SoundEffect::Impl` has a new `std::vector<SoundEffectInstance*> instances` (raw, non-owning);
  `SoundEffectInstance` registers itself in its ctor (`SoundEffect::RegisterInstance`) and
  unregisters in `Dispose()` (`UnregisterInstance` + releases its own `soundEffectKeepAlive_`, so
  the last instance + `SoundEffect` actually release `MIX_Audio` deterministically, closer to
  FNA's eager release). `SoundEffect::Dispose()` iterates a snapshot of `impl_->instances` and
  calls `Dispose()` on each live instance (FNA's `Instances.ToArray()` + foreach), before
  `impl_.reset()`.
  Side effect, but a necessary change: **`SoundEffect` is now move-only** (copy ctor/assignment
  `= delete`) -- without a single owner of the resource, two independent copies could disagree on
  whose `Dispose()` is authoritative. Confirmed via an Explore agent that no call site in
  `src/`/`tests/`/`examples/` relies on `SoundEffect` being copyable -- **except**
  `ContentManager::Load<T>()`, whose generic `std::any` cache requires `CopyConstructible`; fixed
  with a new explicit `Load<Audio::SoundEffect>` specialization (see `ContentManager.hpp`/`.cpp`)
  that doesn't cache this type at all (sharing one instance across unrelated callers would now be
  outright wrong -- one caller's Dispose would silently stop another caller's still-playing
  instance). `SoundEffectInstance`'s move ctor/assignment now also **re-point** the cascade
  tracking (unregister `&other`, register `this`) -- without this, `SoundEffect::Dispose()` after
  a moved instance would call `Dispose()` on the old (possibly out-of-scope) address. Confirmed
  with a real segfault: temporarily disabling just the repoint blocks (not the whole feature) and
  running the new test `DisposeAfterInstanceMovedOutOfScopeDisposesTheMovedToInstance` actually
  crashed (SIGSEGV), not just failed an assert -- stronger evidence than the usual `git stash`
  methodology. Tests: 5 new in `SoundEffectTests.cpp` (cascade over 1/N instances, an
  already-disposed instance skipped safely, moved-to instance via both move ctor and move
  assignment) + 4 `static_assert`s (move-only). Verified with ASan+LeakSanitizer (the whole
  suite, not just the new tests) and via `git stash` (without the fix the tests don't even
  compile -- `RegisterInstance` etc. don't exist). The whole suite 2029/2029 green,
  `cna_demo_sound`/`cna_demo_2d` (the only callers of `Load<SoundEffect>`) compile cleanly.

### Phase 4 — Completing features (no stubs without a reason)

- [x] **T-4A — Real microphone capture via SDL3 (Stub → Full).** *(done 2026-07-02, commits
  `d63946d`/`75bbf4a`/`afcf63c`/`435ff76`, see `NEXT.md` §3; one acceptance criterion, however,
  remained unmet — see the caveat below.)*
  `getAllProperty` enumerates recording devices; `Start`/`Stop` open/close an `SDL_AudioStream`
  (44100/mono/S16); `GetData` reads from `SDL_GetAudioStreamData`, `GetQueuedBytes` from
  `SDL_GetAudioStreamAvailable`; keep `CheckBuffer`/`BufferReady`.
  ~~Delegate `GetSampleDuration`/`GetSampleSizeInBytes` to `SoundEffect`.~~
  **This last sentence was NOT implemented** — `Microphone::GetSampleDuration`/
  `GetSampleSizeInBytes` have their own formula without rounding to whole ms like `SoundEffect`
  does, so they numerically differ from FNA for non-divisible values. Tracked as **MC-1** in
  Phase 7 — fix it there, not here.
  *FNA:* Microphone.cs (FNAPlatform.GetMicrophones/GetMicrophoneSamples/.../Start/Stop).
  *Accept:* with a capture device, `All` is non-empty, `Start`→`GetData` returns >0 B after sound,
  `State` transitions correctly; gracefully empty without a device. State-machine + bounds tests
  (capture-dependent asserts skippable in CI). (C5)
  *Note:* the other acceptance criteria were met and verified on real hardware too (2 real
  microphones over pulseaudio on the dev machine), see `NEXT.md` §3.

- [x] **T-4B — 3D pan/attenuation for SDL_mixer (Apply3D / 3D PlayCue / Cue::Apply3D).**
  Instead of no-ops, derive pan + distance attenuation from listener/emitter geometry
  (`Mix_SetPosition`). From `Cue::Apply3D` (`Cue.cpp:79-83`) remove the disposed-`std::runtime_error`
  in favor of `ObjectDisposedException`. Doppler remains unapplied (accepted deviation §2.2).
  *FNA:* SoundBank.cs:248-263, Cue.cs:166-186, SoundEffectInstance.cs:266-298.
  *Accept:* Apply3D/3D-PlayCue on valid input doesn't throw or crash; volume/pan changes with
  distance/angle (geometry test). (B9, A-Apply3D)
  *Note:* Done 2026-07-04. `ObjectDisposedException` was already in place (nothing left to do on
  that point). `Cue::Apply3D` now iterates `active_` and calls
  `pi.instance->Apply3D(listener, emitter)` on every live `SoundEffectInstance` -- it simply
  delegates to the already-working `SoundEffectInstance::Apply3D` (CP-3), no new pan/attenuation
  math. `SoundBank::PlayCue(name, listener, emitter)` was a complete no-op (it just called the
  2-arg overload); refactored onto a shared private `PlayCueInternal(name, listener*, emitter*)`,
  which calls `cue->Apply3D(...)` after `cue->Play()` and before storing the cue in
  `fireAndForget_` (matches FNA, where `FACT3DCalculate` runs before `FACTSoundBank_Play3D` --
  a synchronous same-thread call here, so no observable difference vs. "position only after
  Play()"). Geometry test: none of the existing fixtures (`MakeCue()`,
  `SharedWeightedVariationBank()`, `SoundBankTests.cpp`'s "Explosion") have a real WaveBank, so
  `Cue::active_` stays empty -- added new WaveBank-backed fixtures to both `CueTests.cpp` and
  `SoundBankTests.cpp` (`Apply3DCue`/`Apply3DWaveBank`), plus a shared
  `SoundEffectInstanceTestAccess.hpp` (extracted from `SoundEffectInstanceTests.cpp`) and new
  `CueTestAccess::ActiveInstance()`/`SoundBankTestAccess::LastFireAndForgetCue()`, so the test can
  read back the real `MIX_GetTrackGain()` and verify it changes with distance (SDL3_mixer has no
  getter for stereo pan, so only attenuation is verified, not pan -- same limitation CP-3's
  original `SoundEffectInstance::Apply3D` coverage has). Verified via `git stash` (both new tests
  genuinely fail -- `farGain == nearGain == 1` -- against the no-op code, not just a compile
  error this time, since the scaffolding doesn't depend on the fix itself). Verified with
  ASan+LeakSanitizer. Whole suite 2031/2031 green.

- [x] **T-4C — Internal DSP filter/reverb routing in SoundEffectInstance.**
  Add `INTERNAL_applyReverb`/`applyLowPassFilter`/`applyHighPassFilter`/`applyBandPassFilter`
  (private/detail) so callers from Cue/AudioEngine line up; implement via SDL_mixer where
  feasible, otherwise a documented no-op.
  *FNA:* SoundEffectInstance.cs:488,518,536,554.
  *Accept:* callers compile; behavior (real/no-op) recorded in the deviations table. (A8)
  *Note:* Done 2026-07-04 -- the decision was "implement real filters, leave reverb as a no-op"
  (the user). Finding before implementation: **no caller exists even in FNA itself** (grepped the
  whole FNA source) -- FACT applies XACT RPC/filter routing natively, the C# layer never calls
  these `INTERNAL_*` methods. "Callers from Cue/AudioEngine" from the acceptance criteria thus
  doesn't correspond to any real caller even in the reference; the task reduces to "the methods
  exist and behave correctly." The filters CAN actually be implemented, though: SDL3_mixer's
  `MIX_SetTrackCookedCallback` gives access to raw float PCM after gain/pan/3D, right before
  mixing -- FAudio's exact state-variable filter (Chamberlin SVF, see `FAudio_internal.c`'s
  `FAudio_INTERNAL_FilterVoice`) is implemented in this callback. Reverb remains a documented
  no-op -- SDL3_mixer has no aux-send/return bus (no equivalent of FAudio's shared `ReverbVoice`),
  a real implementation would be a disproportionately large scope compared to the rest of the
  task.
  Thread safety: the callback runs on SDL_mixer's mixing thread (per SDL3_mixer's documentation).
  Coefficients (`kind`/`frequency`/`oneOverQ`) are written in the setter under `MIX_LockMixer`/
  `UnlockMixer`, and read in the callback WITHOUT locking -- SDL3_mixer documents that the mixing
  thread already holds this lock while mixing, so a second lock would be redundant. The recursive
  filter state (`yl`/`yb`) is read/written exclusively by the mixing thread, no synchronization is
  needed.
  Filter state (`FilterState`) is heap-allocated via `unique_ptr` -- when a `SoundEffectInstance`
  is moved (move ctor/assignment), only ownership of the pointer moves, not the object's address,
  so the SDL3_mixer callback (whose `userdata` is exactly this pointer) stays valid without needing
  re-registration. Without this, moving an instance with an active filter would leave the
  callback pointing at a foreign/stale address -- the same class of bug as T-3G's instance-tracking
  repoint.
  Tests: 9 new in `SoundEffectInstanceTests.cpp`, including a no-op-before-Play() test, exact
  single-sample tests (from a fresh zero state the first filter output is exactly computable:
  `Yl(1)=0`, `Yh(1)=x`, `Yb(1)=F*x`), convergence tests (a constant signal must converge to unity
  gain for low-pass / zero for high-pass), and a move-survival test. The tests call the filter
  math DIRECTLY and synchronously (`ProcessFilterSamplesForTest`), not through the real
  SDL3_mixer callback -- that would run asynchronously from the mixing thread, which would make
  the test either slow (a real wait) or non-deterministic (flaky). **Testing limitation:** real
  concurrency (a setter on the main thread concurrently with the callback on the mixing thread) is
  not timing-precisely verified (no ThreadSanitizer run, no real (non-dummy) audio device in this
  environment) -- the locking follows SDL3_mixer's own documented recommended practice, but was
  not empirically stress-tested.
  Verified via `git stash` (the tests don't compile without the fix --
  `INTERNAL_applyLowPassFilter` etc. don't exist) and ASan+LeakSanitizer (the whole suite). Whole
  suite 2039/2039 green.

- [x] **T-4D — AudioEngine `Update()` / per-cue category recomputation.**
  Assess what's needed from FACT `DoWork` (category fades, instance limits); at minimum finish the
  category-volume re-apply loop (`AudioEngine.cpp:219-223` has an empty body), so a volume change
  affects already-playing cues too, not just future ones.
  *FNA:* AudioEngine.cs:337 (+ category/fade).
  *Accept:* a category volume change affects a currently-playing cue (test); what remains a no-op
  is documented. (B from AudioEngine §1)
  *Note:* Done -- `AudioEngine::SetCategoryVolumeInternal` now calls the new
  `Cue::ApplyCategoryVolume` for every active cue in that category; `Cue::PlaybackInstance` stores
  a `baseVolume` (waveRef.volume, as combined with track/sound volume at parse time), so the
  re-apply recomputes `clamp(baseVolume * newCatVol, 0, 1)` with the same formula `Play()` uses.
  Category fades and instance limits (the rest of FACT `DoWork`) remain out of scope -- they
  weren't part of the acceptance criteria. Verified with the regression test
  `AudioCategoryTest.SetVolumeReappliesToAlreadyPlayingCueInstance` (a new fixture with a real
  WaveBank+SoundEffectInstance, unlike the existing
  `PauseResumeStopRouteToRealActiveCueInCategory`, whose cue has no wavebank, so `active_` stays
  empty and volume couldn't be observed there); confirmed via the `git stash` methodology that
  the test genuinely fails without the fix (1 == 1, no change).

### Phase 5 — Complete test suite (Google Test)

> Rules from `CLAUDE.md`/`CHECKLIST.md`: every public method, **every overload**, operator and
> constant has ≥1 test; out-ref/array overloads tested separately; `==`/`!=`/`Equals` both equal
> and unequal; `ToString` format; `GetHashCode` consistency; `GetTypeName` exact value.

- [x] **T-5A — SoundEffectTests** — both ctors; `GetSampleDuration`/`GetSampleSizeInBytes`
  (round-trip + zero); `Duration`; `Name` get/set (+ move overload); `IsDisposed`; static
  `MasterVolume`/`DistanceScale`(≤0 throws)/`DopplerScale`(<0 throws)/`SpeedOfSound`;
  `CreateInstance`; `Play()`/`Play(v,p,pan)`; `Dispose` idempotent; `FromStream` (valid wave +
  non-wave `NotSupportedException` + empty). (A10)

- [x] **T-5B — SoundEffectInstanceTests** — `Play`/`Pause`/`Resume`/`Stop`/`Stop(bool)`; `State`
  transitions; `Volume`/`Pan`/`Pitch` get/set (+ move overloads, range); `IsLooped` get/set (+
  throw-while-started); `Apply3D(listener,emitter)`; `Apply3D(array,count)` (count==1 + >1
  `NotSupportedException`); `IsDisposed`; `Dispose` idempotent; `GetTypeName`. (A10)

- [x] **T-5C — DynamicSoundEffectInstanceTests** — ctor; `PendingBufferCount`; `IsLooped` always
  false + setter is a no-op (via a base ref, see T-2A); `GetSampleDuration`/
  `GetSampleSizeInBytes`; `Play`/`Stop`; `SubmitBuffer` ×2 (+ range); `SubmitFloatBufferEXT` ×2 (+
  range + `InvalidOperationException` guard, T-2C); `BufferNeeded` when starved; `Dispose` (T-2B);
  `GetTypeName`; `Update`. (A10)

- [x] **T-5D — SoundStateTests** — values and order of Playing/Paused/Stopped. (A10)

- [x] **T-5E — AudioEngineTests** — `ContentVersion`; both ctors; `IsDisposed`; `RendererDetails`;
  `GetCategory` (valid+invalid); `GetGlobalVariable`/`SetGlobalVariable` (valid+invalid);
  `Update`; `Dispose`+`Disposing` event; `GetTypeName`. (B11)

- [x] **T-5F — SoundBankTests** — ctor (valid/null/empty); `IsDisposed`; `IsInUse`; `GetCue`
  (valid+invalid); `PlayCue` (2-arg) and `PlayCue` (3-arg) separately; `Dispose`+event;
  `GetTypeName`. (B11)

- [x] **T-5G — WaveBankTests** — both ctors separately; `IsDisposed`/`IsPrepared`/`IsInUse`;
  `Dispose`+event; `GetTypeName`. (B11)

- [x] **T-5H — CueTests** — all 9 state/Name properties; `Apply3D`; `GetVariable`/`SetVariable`
  (valid+invalid, outside the set); `Play`/`Pause`/`Resume`/`Stop(AsAuthored)` and
  `Stop(Immediate)` separately; `Dispose`+event; `GetTypeName`. (B11)

- [x] **T-5I — AudioCategoryTests** — `Name`; `Pause`/`Resume`/`SetVolume`/`Stop` (both options);
  `Equals` equal+unequal; `GetHashCode` consistency; `operator==`/`operator!=`. (B11)

- [x] **T-5J — AudioEmitterTests / AudioListenerTests** — defaults (DopplerScale 1.0,
  Forward/Up/Zero); every getter/setter round-trip; negative DopplerScale throws. (C6)

- [x] **T-5K — Enum tests** — `AudioChannels`, `AudioStopOptions`, `MicrophoneState` (numeric
  values). (C6)

- [x] **T-5L — RendererDetailTests** — `ToString`, `GetHashCode` consistency, `operator==`/`!=`,
  `Equals` equal+unequal. (C6)

- [x] **T-5M — MicrophoneTests** — `Default` null-safe with an empty `All`; `BufferDuration`
  valid/invalid; `GetSampleDuration`/`GetSampleSizeInBytes` round-trip; `GetData` bounds;
  `GetTypeName`; state machine (T-4A). (C6)
  *Note:* the state machine is only tested to the extent of today's implementation
  (Start/Stop change `State`); the real capture-driven state transition awaits T-4A.

- [x] **T-5N — Exception tests** — for `InstancePlayLimitException`/`NoAudioHardwareException`/
  `NoMicrophoneConnectedException`: all 3 ctors, base-of asserts, inner-exception round-trip. (C1)

- [x] **T-5O — XactParser tests (NOXNA internal)** — round-trip parse of minimal
  `.xgs`/`.xsb`/`.xwb` fixtures: category/variable/cue/entry counts + one PCM sample length
  (regression for T-2D/T-2E). (B11)

### Phase 6 — Documentation and closure

- [x] **T-6A — Accepted deviations table in `CHECKLIST.md`** — add the items from §2 (3D HRTF,
  Doppler, streaming wavebank, value-based `CreateInstance`, GetHashCode int). *(done
  2026-07-02, see the §7 addendum.)*
- [x] **T-6B — Update `AUDIT.md`** — replace the blanket "✅ / stub behavior" with the real status
  after each phase (per file: implemented / accepted deviation / tests). *(done 2026-07-02, see
  the §7 addendum.)*
- [x] **T-6C — Build & report** — `cmake --build cmake-build-debug --target CNA` and
  `--target CnaTests` green; a short report (changed files, deviations, remaining gaps) per
  `CLAUDE.md` §Build and Report.
  *Note:* Done 2026-07-04. Build: both `CNA` and `CnaTests` green (nothing to rebuild, both
  already current from the previous commits), `CnaTests` **2031/2031** green
  (`SDL_AUDIODRIVER=dummy`).
  **Changed files** (this session, commits `d468dc4`..`feb6eda`, 4 tasks T-4D/T-3F/T-3G/T-4B):
  `AudioEngine.{hpp,cpp}`, `Cue.{hpp,cpp}`, `SoundBank.{hpp,cpp}`, `WaveBank.{hpp,cpp}`,
  `SoundEffect.{hpp,cpp}`, `SoundEffectInstance.cpp`, `XactTypes.hpp`, `XactParser.cpp`,
  `ContentManager.{hpp,cpp}` (a collateral fix for T-3G), plus tests
  (`AudioCategoryTests.cpp`, `CueTests.cpp`, `SoundBankTests.cpp`, `WaveBankTests.cpp`,
  `SoundEffectTests.cpp`, `SoundEffectInstanceTests.cpp`, `XactParserTests.cpp`) and two new
  shared test-access headers (`CueTestAccess.hpp`, `SoundEffectInstanceTestAccess.hpp`).
  **Stubs added:** none. **Missing dependencies:** none.
  **Intentional deviations** (see `CHECKLIST.md`, `Audio:` rows): 3D positional audio is only
  pan+attenuation with no elevation; Doppler stored, never applied; `GetHashCode()` via
  `std::hash` instead of the .NET algorithm; streaming `WaveBank`'s `offset`/`packetSize` unused
  (matches FNA); `SoundEffect` move-only; `ContentManager::Load<SoundEffect>()` doesn't cache;
  interactive (`type==3`) variation tables use a uniform pick instead of variable-driven.
  **Remaining gaps** (see `NEXT.md` §5): at the time of writing this report, the only remaining
  task was `T-4C` (DSP filters/reverb on `SoundEffectInstance`) -- **since then (still
  2026-07-04) this has also been closed**, see its own `*Note:*` above. As of the moment this
  paragraph (T-6C) was written, the rest is just the intentional, documented deviations above --
  no open task remains.

### Phase 7 — Supplementary audit (2026-07-02): new findings beyond Phases 0–6

> After finishing T-4A (real microphone capture, see T-4A above), a **fresh** line-by-line audit
> of the entire `Microsoft::Xna::Framework::Audio` cluster against FNA was run — 4 parallel
> checks (Core Playback, XACT, internal backend, Mic/data/enums/exceptions). The goal was both to
> verify the status of already-open tasks (T-3F, T-3G, T-4B, T-4C, T-4D, T-6A, T-6B) against the
> current source, and to find **new**, previously uncaught bugs and gaps. These are only the
> **new** findings — items T-3F/T-3G/T-4B/T-4C/T-4D turned out to still be current with no change
> and remain recorded above, not duplicated here.
>
> IDs carry a cluster prefix (`CP`=core playback, `XA`=XACT, `IN`=internal backend,
> `MC`=mic/data/enums/exceptions) to avoid colliding with `T-*`. Ordered within each cluster by
> severity (real bugs → compliance/behavior → test gaps). `AudioEmitter`, `AudioListener`,
> `AudioChannels`, `AudioStopOptions`, `MicrophoneState`, and all 3 audio exceptions passed the
> audit **with no findings** — checked line by line against FNA, fully compatible, no new tasks.

**Most severe findings (quick overview, details below):**
- **IN-1** — incorrect skipping of the DSP block in the `.xsb` parser can silently derail the
  read position for *every* subsequent sound in the file (cascading corruption, not a crash).
- **CP-1** — `SoundEffectInstance::Play()` is not idempotent; calling it again while playing
  restarts playback from the beginning instead of being a no-op.
- **CP-3** — `Apply3D` silently overwrites the public `Volume`/`Pan`; after calling it,
  `getVolumeProperty()` no longer returns the value the user set.
- **CP-7** — `SoundEffectInstance` holds a raw `const SoundEffect*` to its parent with no
  lifetime management → a dangling pointer in a common chaining pattern
  (`SoundEffect(...).CreateInstance()`).
- **XA-1** — `SoundBank::PlayCue` sweeps fire-and-forget cues based on elapsed time (5 s), not on
  whether they're still playing — long sounds/music get force-stopped.
- **XA-2** — `WaveBank::GetSoundEffect` leaks a heap-allocated `SoundEffect` for 8-bit PCM and
  ADPCM entries.
- **IN-2** — over-read on non-compact `.xwb` entries with `entryMetaDataSize < 24` (reads foreign
  memory).
- **MC-1** — T-4A in fact did not meet its own acceptance criterion:
  `GetSampleDuration`/`GetSampleSizeInBytes` doesn't delegate to `SoundEffect` as specified.

#### 7.1 Core Playback (SoundEffect, SoundEffectInstance, DynamicSoundEffectInstance)

- [x] **CP-1 — `SoundEffectInstance::Play()` is not idempotent when called again while playing.**
  *(done 2026-07-02.)* FNA has an explicit `if (State == SoundState.Playing) { return; }` at the
  top of `Play()`. CNA has no such check — calling `Play()` again on an already-playing instance
  calls `MIX_SetTrackAudio`/`MIX_PlayTrack` again, restarting playback from the beginning instead
  of being a no-op.
  *FNA:* SoundEffectInstance.cs:282-285.
  *CNA:* SoundEffectInstance.cpp:143-232 (missing guard).
  *Accept:* calling `Play()` again while `State==Playing` doesn't change the playback position
  (a test observing that the track doesn't replay from zero / that `MIX_SetTrackAudio` isn't
  called again).
  *Note:* added `SoundEffectInstanceTestAccess` (friend, mirroring `MicrophoneTestAccess`) to
  read `track_` in the test. The new
  `SoundEffectInstanceTest.RepeatedPlayWhileAlreadyPlayingDoesNotRestartTrack` sets
  `MIX_SetTrackPlaybackPosition` to a nonzero position, calls `Play()` again, and verifies
  `MIX_GetTrackPlaybackPosition` stays >= that position; verified via `git stash` that without
  the fix the test fails (the position resets to 0), and passes with the fix.
  `DynamicSoundEffectInstance::Play()` already has its own override with the guard in place —
  CP-1 didn't affect it.

- [x] **CP-2 — `SoundEffect::Play(volume, pitch, pan)` doesn't validate/clamp pan and pitch.**
  *(done 2026-07-03.)* FNA internally does `instance.Pitch = pitch;` (clamps to [-1,1]) and
  `instance.Pan = pan;` (throws `ArgumentOutOfRangeException` when `|pan|>1`) before
  `instance.Play()`. CNA computed stereo gains and the frequency ratio directly from the raw
  inputs without clamping/validation.
  *FNA:* SoundEffect.cs:338-352, SoundEffectInstance.cs:46-65,87-101.
  *CNA:* SoundEffect.cpp:254-310.
  *Accept:* `Play(v, pitch>1, pan>1)` throws `ArgumentOutOfRangeException` (pan), resp. clamps
  pitch to [-1,1] just like the property setter; test both branches.
  *Note:* `Play(volume,pitch,pan)` now validates/clamps at the very start (before any mixer work)
  exactly like `SoundEffectInstance::setPanProperty`/`setPitchProperty` — `pan` outside [-1,1]
  throws `System::ArgumentOutOfRangeException`, `pitch` is clamped to [-1,1]. New tests
  `PlayThrowsOnPanOutOfRange` (verified via `git stash` — fails without the fix, "throws
  nothing", passes with the fix) and `PlayClampsPitchInsteadOfThrowing` (passes on both versions —
  extreme pitch never crashed before either, it just wasn't clamped; the test pins the clamp
  going forward). Whole suite 2002/2002 tests green.

- [x] **CP-3 — `Apply3D(listener, emitter)` overwrites the public `Volume`/`Pan`, instead of only
  computing a separate output matrix like FNA.** *(done 2026-07-02.)* FNA only updates the
  internal `dspSettings`; the `Volume`/`Pan` getters are never touched by 3D positioning. CNA
  calls `setVolumeProperty(...)`/`setPanProperty(...)` directly, so after `Apply3D`,
  `getVolumeProperty()`/`getPanProperty()` no longer return the value the user set.
  *FNA:* SoundEffectInstance.cs:221-264 (no touching of `INTERNAL_pan`/`INTERNAL_volume`).
  *CNA:* SoundEffectInstance.cpp:289-310.
  *Accept:* after `setVolumeProperty(X)` + `Apply3D(...)`, `getVolumeProperty()==X` (3D
  attenuation is applied elsewhere, not to the public property); tests for both.
  *Note:* fixed by applying the 3D attenuation/pan directly to the `MIX_Track` (a shared
  `ApplyTrackProperties` helper, `atten * Volume_` — multiplicatively with Volume, exactly how
  FNA combines `INTERNAL_volume` with the `dspSettings` matrix at the audio-engine level), instead
  of calling `setVolumeProperty`/`setPanProperty`. During the refactor, a side finding turned up:
  `Apply3D` didn't yet explicitly throw `ObjectDisposedException` (it worked only indirectly via
  `setPanProperty`'s disposed check, which would disappear with the refactor) — an explicit
  disposed check + `@throws` doxygen + `Apply3DAfterDisposeThrows` test were added so this
  behavior wouldn't regress from the refactor. Verified via `git stash` that
  `Apply3DDoesNotModifyVolumeOrPanProperties` fails without the fix (Volume/Pan get overwritten),
  and passes with the fix.

- [x] **CP-4 — `DynamicSoundEffectInstance::PendingBufferCount`/`Update()` diverge in semantics
  from FNA — the count resets right after handing data to the SDL stream, not after the hardware
  actually plays it.** *(done 2026-07-03.)* `SubmitQueuedToStream()` emptied `queuedBuffers_` on
  every call (even inside `Update()`), so `getPendingBufferCountProperty()` was practically always
  0 right after a submit → `Update()` then fired `BufferNeeded` on practically every tick
  regardless of the buffer's actual state.
  *FNA:* DynamicSoundEffectInstance.cs:23-29,290-322.
  *CNA:* DynamicSoundEffectInstance.cpp:47-51,312-331,374-395.
  *Accept:* `PendingBufferCount` reflects data not yet consumed (query the real SDL stream queue,
  not a local list); `BufferNeeded` doesn't fire when the stream has enough data (test on call
  frequency).
  *Note:* added a new private member `std::deque<std::size_t> submittedChunkSizes_` — the size
  (in bytes) of each chunk handed to `audioStream_`, oldest first. `SubmitQueuedToStream()` now,
  when handing data to the stream, also records the chunk's size into `submittedChunkSizes_`
  (instead of merely emptying `queuedBuffers_`). `getPendingBufferCountProperty()` returns
  `queuedBuffers_.size() + submittedChunkSizes_.size()`. `Update()` after
  `SubmitQueuedToStream()` calls `SDL_GetAudioStreamQueued(stream)` (the real byte count the
  stream still holds as unconsumed input — SDL3's analog of FNA's
  `FAudioSourceVoice_GetState().BuffersQueued`) and removes the oldest chunks from
  `submittedChunkSizes_` while the remaining sum exceeds that value — exactly the same algorithm
  as FNA's `while (PendingBufferCount > state.BuffersQueued) RemoveAt(0)`, just at byte
  granularity instead of discrete buffers. `ClearBuffers()` now also clears
  `submittedChunkSizes_`. New test `BufferNeededDoesNotFireWhenStreamHasEnoughData` (3 buffers
  before/at `Play()`, `Update()` right after — nothing could have actually been consumed,
  `BufferNeeded` must not fire even once) verified via `git stash` — consistently fails without
  the fix (fired>0 even with a full buffer), consistently `fired==0` with the fix (repeated 5x).
  Whole suite 2004/2004 tests green (repeated 3x, no flakiness).

- [x] **CP-5 — `Stop(bool immediate=false)` on `DynamicSoundEffectInstance` silently no-ops
  instead of throwing `InvalidOperationException`.** *(done 2026-07-03.)* FNA's `Stop(bool)`
  explicitly throws if `isDynamic && !immediate`. CNA's `Stop(bool)` wasn't virtual and only
  worked with the base `track_`, which dynamic instances never use.
  *FNA:* SoundEffectInstance.cs:404-439 (throw at line ~435).
  *CNA:* SoundEffectInstance.cpp:239-261 (missing `isDynamic`/virtual dispatch).
  *Accept:* `dynamicInstance.Stop(false)` throws `System::InvalidOperationException`; test.
  *Note:* `SoundEffectInstance::Stop(bool)` is now `virtual`; `DynamicSoundEffectInstance` adds an
  override that first replicates FNA's `handle==0` early-return gate (no active track → a silent
  no-op, even for `immediate=false` — matches FNA exactly: `Stop(false)` before the first `Play()`
  doesn't throw), and only throws `System::InvalidOperationException` if a track exists and
  `!immediate`. New tests `StopFalseWhileNeverPlayedIsSafeNoOp` (no-op case) and
  `StopFalseAfterPlayingThrowsInvalidOperation` (throw case, after `tryStartHeadless`). Verified
  via `git stash`: the ORIGINAL code doesn't even compile with the tests (`d.Stop(false)` — C++
  name-hiding: a derived parameterless `Stop()` hides the base `Stop(bool)` overload until the
  derived class declares its own `Stop(bool)`) — stronger evidence of the bug than the usual
  "throws nothing". Everything passes with the fix. Whole suite 1999/1999 tests green.

- [x] **CP-6 — `DynamicSoundEffectInstance::GetSampleDuration`/`GetSampleSizeInBytes` compute the
  real bytes/sample (`isFloat_ ? 4 : 2`) instead of FNA's hardcoded 16-bit assumption.** *(done
  2026-07-03.)* FNA always delegates both methods to
  `SoundEffect.GetSampleDuration/GetSampleSizeInBytes`, which always divide/multiply by 2
  regardless of float mode. CNA returned a different result than FNA after
  `SubmitFloatBufferEXT`.
  *FNA:* DynamicSoundEffectInstance.cs:114-130; SoundEffect.cs:363-374.
  *CNA:* DynamicSoundEffectInstance.cpp:79-96,335-339.
  *Accept:* after `SubmitFloatBufferEXT`, `GetSampleDuration`/`GetSampleSizeInBytes` give
  numerically the same result as FNA (still divided/multiplied by 2, not 4); regression test on a
  float instance.
  *Note:* both methods now delegate in a single line to `SoundEffect::GetSampleDuration`/
  `GetSampleSizeInBytes(…, sampleRate_, channels_)` — exactly like FNA. The now-dead private
  helper `getBytesPerSampleFrame()` (its only caller) was deleted (declaration and definition).
  New test `GetSampleDurationIgnoresFloatFormatMatchingFNA` (after `SubmitFloatBufferEXT`, 1s of
  stereo @ 44100Hz must still give 176400 B, not 352800 B) verified via `git stash` — fails
  exactly at `352800 != 176400` without the fix, passes with the fix. Whole suite 2003/2003
  tests green.

- [x] **CP-7 — `SoundEffectInstance` holds a raw `const SoundEffect*` to its parent with no
  lifetime management — a dangling pointer in a common chaining pattern.** *(done 2026-07-03.)*
  `SoundEffect(path).CreateInstance()` (or `SoundEffect::FromStream(s)->CreateInstance()` on a
  dereferenced temporary) created an instance pointing at an already-destroyed `SoundEffect`;
  the subsequent `Play()` read `soundEffect_->getNativeAudioHandle()` on freed memory. Related to
  T-3G (value semantics), but a concrete safety risk beyond T-3G's general scope.
  *FNA:* SoundEffect.cs:126,354 (reference semantics, GC keeps the object alive).
  *CNA:* SoundEffectInstance.hpp:32; SoundEffectInstance.cpp:69-72.
  *Accept:* either a documented contractual requirement (the owner must outlive the instance) in
  the Doxygen of `CreateInstance()`/the ctor, or a fix to shared ownership; an ASAN test for the
  dangling scenario.
  *Note:* chose the fix to shared ownership (not just documentation). The raw `const SoundEffect*
  soundEffect_` was replaced with two members: `std::shared_ptr<void> soundEffectKeepAlive_` (a
  type-erased copy of `SoundEffect::impl_`, since `SoundEffect::Impl` is private and defined only
  in SoundEffect.cpp — but converting `shared_ptr<Impl> → shared_ptr<void>` works even with an
  incomplete type) and `void* nativeAudioHandle_` (MIX_Audio*, captured in the constructor while
  `soundEffect` was still alive). `Play()` never dereferences `SoundEffect*` again — it only reads
  `nativeAudioHandle_`. New test
  `PlaySucceedsAfterOriginatingSoundEffectTemporaryIsDestroyed`
  (`SoundEffect(pcm,...).CreateInstance()` — the temporary `SoundEffect` is destroyed before
  `Play()`) verified under a real ASan build (`cmake-build-asan`, per the NEXT.md recipe, deleted
  after verification): **before the fix** ASan reports an exact `stack-use-after-scope` in
  `SoundEffectInstance::Play()` → `SoundEffect::getNativeAudioHandle()`; **after the fix** the
  whole audio test suite (168 tests) passes under ASan+LeakSanitizer with no findings. Whole
  suite 2005/2005 tests green (normal build).

- [x] **CP-8 — `SoundEffect` doesn't inherit `System::Object` and has no `GetTypeName()`, unlike
  its sibling classes.** *(done 2026-07-03.)* `SoundEffectInstance`, `DynamicSoundEffectInstance`,
  `AudioEngine`, `SoundBank`, `WaveBank`, `Cue` all inherit `System::Object` and have
  `GetTypeNameHPP()`/`GetTypeNameCPP()`. `SoundEffect` only inherited `System::IDisposable`.
  *FNA:* SoundEffect.cs:20 (implicit `object`).
  *CNA:* SoundEffect.hpp:19.
  *Accept:* `SoundEffect : public System::Object, public System::IDisposable`;
  `GetTypeName()=="Microsoft.Xna.Framework.Audio.SoundEffect"`; test.
  *Note:* `SoundEffect` now inherits `public System::Object, public System::IDisposable` (same
  order as sibling classes), `GetTypeNameHPP()`/`GetTypeNameCPP(SoundEffect, …)` added. New test
  `SoundEffectTest.GetTypeNameIsDottedXnaName` verified on the ORIGINAL (pre-fix) code via
  `git stash` — doesn't even compile without the fix (`has no member named 'GetTypeName'`),
  passes with the fix. Whole suite 1987/1987 tests green.

- [x] **CP-9 — The `SoundEffectInstance(const SoundEffect&)` constructor is public; FNA has an
  equivalent `internal` ctor.** *(done 2026-07-03.)* Per `CLAUDE.md` (Visibility Mapping),
  `internal` should map to `private`/`protected`/friend-scoped, not `public`. The class already
  declared `friend class SoundEffect;`, so making it private was painless.
  *FNA:* SoundEffectInstance.cs:174 (`internal SoundEffectInstance(...)`).
  *CNA:* SoundEffectInstance.hpp:45.
  *Accept:* ctor `private`/`protected`; `SoundEffect::CreateInstance()` (friend) still compiles;
  direct construction from outside doesn't compile (negative compile test/comment).
  *Note:* the ctor was moved to the `private:` section with a doxygen note that it's the
  `internal`-equivalent, called only from `SoundEffect::CreateInstance()`. Verified both ways:
  `cmake --build` passes with no change (CreateInstance() still compiles), and a scratch file
  with direct external construction (`SoundEffectInstance inst(fx);`) fails with
  `'...SoundEffectInstance(...)' is private within this context` — exactly per the acceptance
  criteria. Whole suite 1988/1988 tests green (count unchanged, purely a visibility change).

- [x] **CP-10 — Missing test for the NOXNA ctor `SoundEffect(const std::string& assetName)**
  (loading from a file).** *(done 2026-07-03.)* Only the buffer ctor and `FromStream` were
  covered; the file-path ctor had no test at all.
  *CNA:* SoundEffect.hpp:48; tests/…/SoundEffectTests.cpp.
  *Accept:* test for an empty string (no-op), a nonexistent file (throw, headless-safe under
  `GTEST_SKIP` when the device is missing).
  *Note:* no production code change — the ctor already behaved correctly, only tests were
  missing. Added `SoundEffectTest.ConstructFromEmptyPathIsNoOp` (empty string → no throw,
  `IsDisposed==false`, `Duration==0`) and `...ConstructFromNonexistentPathThrowsNotSupported`
  (nonexistent path → `System::NotSupportedException`, with the same try/catch/`GTEST_SKIP`
  idiom as `FromStreamGarbageThrowsNotSupported`). Both tests ran without being skipped (the
  dummy audio device in this environment works) and actually exercised the load-path branch.
  Whole suite 1992/1992 tests green.

- [x] **CP-11 — T-5A claims coverage of a "valid wave" for `FromStream`, but a test for the
  successful-load path is missing.** *(done 2026-07-03.)* `SoundEffectTests.cpp` only had tests
  for empty/garbage input (throw) — no test loaded real valid WAV data and verified a successful
  return/`Duration`.
  *CNA:* tests/…/SoundEffectTests.cpp:125-149.
  *Accept:* a test with a minimal valid WAV fixture (an in-memory PCM header) verifies a
  successful `FromStream` + `getDurationProperty() > 0`.
  *Note:* no production code change. Added `BuildMinimalWavBytes()` (16-bit mono PCM, 0.1s of
  silence, a hand-built RIFF/WAVE/fmt/data header) and
  `SoundEffectTest.FromStreamValidWavSucceedsAndReportsNonzeroDuration` — ran without being
  skipped (the dummy audio device works), `FromStream` successfully returned a non-null
  `SoundEffect` and `getDurationProperty() > 0`. Whole suite 1993/1993 tests green.

- [x] **CP-12 — Missing tests for `SoundEffectInstance`'s move constructor and move assignment
  (NOXNA public members).** *(done 2026-07-03.)* `CLAUDE.md` requires a test for every public
  method/operator.
  *CNA:* SoundEffectInstance.cpp:82-128; tests/…/SoundEffectInstanceTests.cpp.
  *Accept:* a test verifies the transfer of `track_`/`State_`/properties and that the source
  object after a move is safely disposed-like (no double-free on destruction).
  *Note:* no production code change. Added `MoveConstructorTransfersTrackAndProperties` (moves a
  playing instance, verifies the same `MIX_Track*`, `Volume`, `State==Playing`, and that the
  source instance after the move has `IsDisposed==true` and `track_==nullptr`) and
  `MoveAssignmentTransfersTrackAndDestroysPreviousOne` (the target instance already owns its own
  track, which should be discarded before taking over the source's). Whole suite 1995/1995
  tests green, no crash (which a double-free would typically manifest as).

- [x] **CP-13 — Missing tests for `Stop(false)` (non-immediate) on both `SoundEffectInstance` and
  `DynamicSoundEffectInstance`.** *(done 2026-07-03.)* Only `Stop()`/`Stop(true)` were tested.
  *CNA:* tests/…/SoundEffectInstanceTests.cpp, tests/…/DynamicSoundEffectInstanceTests.cpp.
  *Accept:* a test on a static instance (`Stop(false)` lets a loop ring out) and on a dynamic one
  (`Stop(false)` throws after the CP-5 fix).
  *Note:* the dynamic part was already covered as part of the CP-5 fix
  (`StopFalseWhileNeverPlayedIsSafeNoOp`/`StopFalseAfterPlayingThrowsInvalidOperation`). Added the
  missing static test
  `SoundEffectInstanceTest.StopFalseDoesNotCutOffLoopedPlaybackImmediately` — sets
  `IsLooped=true`, `Play()`, then `Stop(false)` and verifies `State` stays `Playing` (the loop
  merely ends for the next cycle, playback doesn't stop immediately). Whole suite 2000/2000
  tests green.

- [x] **CP-14 — Missing regression test for calling `Play()` again while `State==Playing`.**
  *(done 2026-07-03 — resolved as a side effect of CP-1.)* This would have directly caught CP-1;
  today's test called `Play()` only once.
  *CNA:* tests/…/SoundEffectInstanceTests.cpp:122-130.
  *Accept:* a test calls `Play()` twice in a row and verifies the state stays `Playing` with no
  restart.
  *Note:* CP-1's fix (2026-07-02) already added exactly this test —
  `SoundEffectInstanceTest.RepeatedPlayWhileAlreadyPlayingDoesNotRestartTrack` calls `Play()`
  twice, verifies `State==Playing`, and additionally (stronger than the acceptance criteria
  requires) via `MIX_GetTrackPlaybackPosition` that playback didn't restart from the beginning.
  No new work was needed, just an additional checkbox in the backlog.

#### 7.2 XACT (AudioEngine, SoundBank, WaveBank, Cue, AudioCategory, RendererDetail)

- [x] **XA-1 — `SoundBank::PlayCue` sweeps fire-and-forget cues based on elapsed time, not
  playback state — long sounds get cut off.** *(done 2026-07-02.)* `fireAndForget_` in `PlayCue`
  was swept with the condition `now - faf.created >= 5s`, regardless of whether
  `faf.cue->getIsPlayingProperty()` was still `true` (unlike `getIsInUseProperty()`, which
  correctly checks `IsPlaying`). Any fire-and-forget cue/music longer than 5s got force-stopped/
  destroyed on the next `PlayCue` on the same bank, even if it was still playing.
  *FNA:* SoundBank.cs:28-36 (`IsInUse` reflects real state), SoundBank.cs:105-119 (the destructor
  keeps the object alive while `IsInUse`).
  *CNA:* SoundBank.cpp:116-135.
  *Accept:* the sweep condition changes to "no longer playing"
  (`!faf.cue->getIsPlayingProperty()`), possibly combined with a safety-net timeout (on the order
  of minutes, not 5s); a test simulating a long-playing cue proving it isn't stopped prematurely.
  *Note:* the sweep now only removes finished cues (`!IsPlaying`) plus anything past
  `kFireAndForgetSafetyNet` = 5 minutes (D6 default), even if it's still "playing" — a safety net
  against unbounded growth if a caller just keeps `PlayCue`ing a looped/very long cue. Added
  `SoundBankTestAccess` (friend, mirroring `SoundEffectInstanceTestAccess`/
  `MicrophoneTestAccess`) with `FireAndForgetCount`/`BackdateLastFireAndForget` — since a `Cue`
  never on its own leaves the `Playing` state without an explicit `Stop()` (no real
  finish-detection), the only way to quickly and deterministically (without a real wait) test the
  5s/5min thresholds is to artificially "move back" an entry's creation time. New
  `FireAndForgetCueSurvivesSweepPastOldFiveSecondThresholdWhileStillPlaying` verified via
  `git stash` — fails without the fix (the old behavior wipes out a 30s-old, still-playing
  entry), passes with the fix. A second test
  `FireAndForgetCueIsForceSweptPastSafetyNetEvenIfStillPlaying` verifies the safety net itself
  (doesn't discriminate old/new behavior, both sweep a 10-min-old entry).

- [x] **XA-2 — `WaveBank::GetSoundEffect` leaks a heap-allocated `SoundEffect` from `FromStream`
  for 8-bit PCM and ADPCM waves.** *(done 2026-07-02.)* `cached.emplace(*SoundEffect::FromStream(ss))`
  dereferences a `SoundEffect*` returned from `new SoundEffect(...)` and copies it into a
  `std::optional`; the original heap object is never released (`delete` is missing). The leak
  occurs on every first access to any 8-bit PCM or ADPCM entry in a wavebank.
  *CNA:* WaveBank.cpp:253, WaveBank.cpp:263.
  *Accept:* either wrap the `FromStream` result in a `std::unique_ptr` and move/copy without a
  leak, or delete it right after copying into `cached`; a test (ASan/leak-check) proving that
  repeated `GetSoundEffect` on the same index doesn't create a new leak.
  *Note:* both sites (8-bit PCM line 253, ADPCM line 263 — an identical pattern) were wrapped in
  `std::unique_ptr<SoundEffect>`, the value moved (`std::move`) into `cached`. New
  `WaveBankTest.GetSoundEffectFor8BitPcmEntrySucceeds` (the fixture builder parameterized by
  `bankName`/`eightBitPcm`, resp. `wavebankName`/`cueName`, with new names to avoid colliding with
  the existing `"TestWaveBank"` registration on the shared `AudioEngine`) actually plays back an
  8-bit PCM wave. The leak was empirically verified with a separate ASan+LeakSanitizer build
  (`cmake-build-asan`, deleted after verification, not part of the repo): **before the fix**
  LeakSanitizer reports exactly `WaveBank.cpp:253` → `SoundEffect::FromStream` (136 B, 3
  allocations); **after the fix** no findings. ADPCM (line 263) uses an identical fix but has no
  fixture of its own (ADPCM parsing has no tests at all — tracked under IN-6, not repeated here).

- [x] **XA-3 — `Cue::Play` ignores the authored `weightMin`/`weightMax` and variation-selection
  type, always picking uniformly at random.** *(done 2026-07-03.)* `XsbVariEntry::weightMin/
  weightMax` are parsed but `Cue::Play` never used them — it always used
  `std::uniform_int_distribution` over all entries instead of weighted probability. There was
  also no distinction for the other XACT variation types (Ordered/OrderedFromRandom/
  RandomNoImmediateRepeats/Shuffle) — `var.lastSelected` is declared but never read or written
  anywhere.
  *CNA:* Cue.cpp:139-197, XactTypes.hpp:88-105.
  *Accept:* selection respects `weightMin`/`weightMax` (weighted random selection) at least for
  random types; for non-random types either an implementation or a documented deviation; a test
  verifying that an entry with a weight approaching 100 is selected statistically far more often.
  *Note:* FAudio (`get_active_variation_index`, FACT_internal.c:467-525) uses the **same**
  weighted algorithm for ALL non-interactive types (wave/sound/compact_wave) — FAudio itself
  doesn't implement Ordered/Shuffle/RandomNoImmediateRepeats at all, so that isn't part of the
  real FNA behavior to catch up to. `Cue::Play` now computes `totalWeight =
  Σ(weightMax-weightMin)` and picks an entry via a weighted lottery matching FAudio's algorithm
  1:1 (scanning from the last entry, `value > (remaining - weight)`); the degenerate case
  (`totalWeight==0` — today only interactive type 3, where `var_min`/`var_max` aren't yet parsed
  into `XsbVariEntry`) falls back to a uniform pick, documented in CHECKLIST.md. New test
  `CueTest.PlayWeightedVariationFavorsHigherWeightEntryStatistically` (2 sound entries, weights 1
  and 99 out of 100, 200x `Play()`, 80% threshold) verified on the ORIGINAL (pre-fix) code via
  `git stash` — without the fix consistently around 45-55% (uniform), with the fix consistently
  >90%. Whole suite 1988/1988 tests green.

- [x] **XA-4 — `AudioEngine`'s two-parameter constructor silently discards both `lookAheadTime`
  and `rendererId` without a documented deviation.** *(done 2026-07-03.)* Both parameters were
  commented-out and unused anywhere, but the doxygen described them as if they had an effect.
  *FNA:* AudioEngine.cs:112-225.
  *CNA:* AudioEngine.cpp:46-54; AudioEngine.hpp:43-52.
  *Accept:* either use `rendererId` to select between future backend renderers, or at minimum add
  a `//` comment in the `.cpp` and update the doxygen in the `.hpp`; a test verifying the
  constructor with an arbitrary `rendererId`/`lookAheadTime` doesn't throw and behaves like the
  single-parameter ctor.
  *Note:* chose a documented deviation (not implementing renderer selection) — CNA has a single
  backend (SDL3_mixer), so there's nothing to select between. The doxygen in `.hpp` now
  explicitly says both parameters are accepted purely for API compatibility and have no effect
  (any value, including an unknown `rendererId`, behaves like the single-parameter ctor); the
  `.cpp` has a `//` comment with the same explanation. The existing test
  `TwoArgConstructorLoadsFixtureWithRendererAndLookAhead` only tested "reasonable" values
  (`TimeSpan::Zero`, `"SDL3_mixer"`) — added a new
  `ThreeArgConstructorWithArbitraryRendererAndLookAheadBehavesLikeSingleArg` with a nonsense
  `rendererId` and a nonzero `lookAheadTime`, verifying `!IsDisposed`, a non-empty
  `RendererDetails`, and a working `GetCategory("Default")`. Whole suite 1996/1996 tests green.

- [x] **XA-5 — `AudioCategory`/`Cue` tests don't verify the real effect of `Pause`/`Resume`/
  `Stop`/`SetVolume` on a running cue.** *(done 2026-07-03.)* Only `EXPECT_NO_THROW` was tested
  with no active `Cue` in the category — even though `AudioCategory.hpp` explicitly documents
  that these methods "route to every currently active Cue... and have a real, immediate effect on
  playback".
  *CNA:* tests/.../AudioCategoryTests.cpp:130-160.
  *Accept:* a new test creates a `SoundBank`+`Cue` in a fixture with a category, calls
  `cue->Play()`, then `category.Pause()`/`.Stop()`/`.SetVolume()`, and verifies via
  `getIsPausedProperty()`/`getIsStoppedProperty()` that the effect actually occurred.
  *Note:* no production code change. Added a minimal `.xsb` fixture (one simple cue, a sound with
  `categoryIndex=0` = "Default", no wavebank needed — `Cue::Play()` sets `categoryIdx_`/`state_`
  and registers into `activeCues` regardless of whether a real wavebank is found) and
  `SharedBank()`. New test `PauseResumeStopRouteToRealActiveCueInCategory` genuinely verifies
  `Pause→IsPaused`, `Resume→IsPlaying`, `Stop→IsStopped` on a real registered Cue. **Side finding**
  (outside XA-5's scope, not logged as a new item): `AudioEngine::SetCategoryVolumeInternal`
  (AudioEngine.cpp:224-234) has a comment "Cue would need to re-apply volume — skipped for
  simplicity" — so `SetVolume()` in fact does **nothing** to active cues, it only stores the value
  into `categoryVolumes`; the test therefore only checks `EXPECT_NO_THROW` for `SetVolume`, not a
  real effect (unlike Pause/Resume/Stop). Whole suite 2006/2006 tests green.

#### 7.3 Internal backend (XactParser, XactTypes, AudioMixer)

> `AudioMixer.cpp`'s hardcoded 44100 Hz/stereo/S16 for the shared mixer device was explicitly
> reviewed and **is not a bug** — `MIX_LoadRawAudio`/`MIX_LoadAudio_IO` set per-audio
> `SDL_AudioSpec.freq` to the real `entry.sampleRate`, and SDL3_mixer resamples on the fly.

- [x] **IN-1 — Incorrect skipping of the DSP block in `ParseXsb`.** *(done 2026-07-02.)* The
  parser reads a 2-byte field and skips `dspLen - 2` bytes, as if it were a self-inclusive length
  (the same pattern as the RPC block a few lines above, where it's correct). But per FAudio
  (`FACT_internal.c:2650-2661`) the DSP block **never** uses that leading field as a length to
  jump by — it's explicitly marked "unused"; instead, `dspCodeCount` (1 B) is read, then
  `dspCodeCount*4` B of codes. If real `.xsb` files have `SOUND_FLAG_HAS_DSP` (0x10) set and the
  field's value doesn't match `1+4*count`, the cursor derails and **every subsequent sound in the
  file** is read from a shifted position — silent, cascading corruption, not a crash. The path is
  also not covered by any test.
  *File:* src/CNA/Internal/Audio/XactParser.cpp:650-656 (cf.
  FAudio/src/FACT_internal.c:2650-2661).
  *Accept:* rewrite as `count = sc.u8(); for(count) sc.u32();` (discard the first 2 B as unused);
  a regression test with `SOUND_FLAG_HAS_DSP` set verifying that the next sound in the file
  parses correctly.
  *Note:* the new `XactParserTest.DspBlockIsSkippedByCodeCountNotByLengthField` verified on the
  ORIGINAL (pre-fix) code via `git stash` — the test fails without the fix (even hits "read past
  end", not just wrong data), passes with the fix. The `dspCodeCount`-based skip replaced the old
  length-based skip.

- [x] **IN-2 — Over-read on a non-compact XWB entry with `entryMetaDataSize < 24`.** *(done
  2026-07-03.)* The code always performs all 6 `u32()` reads (24 B) *before* checking
  `entryMetaDataSize < 24`, only afterward moving the cursor back. For older (narrower) formats,
  `loopStart`/`loopTotal` thus contain bytes from foreign memory (data from the next entry, or
  beyond the segment for the last entry — a risk of a "read past end" exception near the end of
  the buffer). FAudio reads exactly `dwEntryMetaDataElementSize` bytes and leaves the rest
  zeroed.
  *File:* src/CNA/Internal/Audio/XactParser.cpp:458-476.
  *Accept:* read fields conditionally/limited to `entryMetaDataSize`, not unconditionally all 24
  B; a test with a non-compact `.xwb` fixture where `entryMetaDataSize < 24` (today's tests only
  cover the compact format).
  *Note:* each field (`flagsAndDuration`/`fmt`/`playOffset`/`playLength`/`loopStart`/`loopTotal`)
  is now read conditionally per threshold (`>=4`, `>=8`, ... `>=24`), missing fields stay 0
  (matches FAudio's zero-init + partial read). The cursor advances by exactly
  `entryMetaDataSize` (`ctx.skip`), not a fixed 24 B — this also covers the theoretical
  `entryMetaDataSize > 24` case. New test
  `XactParserTest.NonCompactWaveBankWithNarrowEntryMetaDataDoesNotReadForeignBytes` (a fixture
  with `entryMetaDataSize=12`, the last entry ending exactly at the end of the file) verified on
  the ORIGINAL (pre-fix) code via `git stash` — fails exactly with "read past end" without the
  fix, passes with the fix. Whole suite 1982/1982 tests green.

- [x] **IN-3 — Integer underflow in compact-XWB `dataLength` can bypass the bounds check in
  `WaveBank.cpp`.** *(done 2026-07-03.)* The computation `rawOffsetUnits[i+1]*alignment - offset -
  deviations[i]` is `uint32_t` arithmetic with no guard — for a corrupted/adversarial file it can
  underflow to a value near `UINT32_MAX`. The downstream bounds check in `WaveBank.cpp` is also a
  `uint32_t` sum that can itself overflow → a heap over-read/crash while building the resulting
  `std::vector`.
  *File:* src/CNA/Internal/Audio/XactParser.cpp:449-452, downstream
  src/Microsoft/Xna/Framework/Audio/WaveBank.cpp:221-228.
  *Accept:* a saturating/checked subtraction (clamp to 0 or throw) when computing `dataLength`;
  a test with an artificially created deviation larger than the gap to the next offset.
  *Note:* per D7 (throw, not silently clamp) — both underflow cases (the gap to the next entry
  minus deviation; the last entry exceeding the wave-data segment) now compute in `uint64_t` and
  throw `std::runtime_error` if the subtraction would underflow. The downstream bounds check in
  `WaveBank.cpp:222` was also rewritten as a `uint64_t` sum so it can't overflow itself. New
  tests `XactParserTest.CompactWaveBankThrowsWhenDeviationExceedsGapToNextEntry` and
  `...ThrowsWhenLastEntryOffsetExceedsWaveDataSegment` verified on the ORIGINAL (pre-fix) code via
  `git stash` — both fail without the fix ("throws nothing" — the earlier code silently
  underflowed instead of throwing), pass with the fix. Whole suite 1984/1984 tests green.

- [x] **IN-4 — Incorrect comment and missing case for variation-table type 2.** *(done
  2026-07-03.)* The comment `// INTERACTIVE (type==2)` was misleading: per FAudio, INTERACTIVE is
  type **3**, type **2** is `CLIP` (FAudio itself doesn't support it). CNA's catch-all `else`
  branch thus silently parsed type 2/5/6/7 with the same 16-byte layout as type 3.
  *File:* include/CNA/Internal/Audio/XactTypes.hpp:101, src/CNA/Internal/Audio/XactParser.cpp:746,775-783.
  *Accept:* fix the comment (INTERACTIVE=3, CLIP=2 unsupported), add an explicit check for type
  0/1/3/4, and throw/log for an unknown type instead of silently guessing its layout.
  *Note:* the `XactTypes.hpp:101` comment was fixed (0=wave, 1=sound, 2=clip unsupported, 3=
  interactive, 4=compact_wave). `XactParser.cpp` now has an explicit `else if (var.type == 3)`
  branch for INTERACTIVE (16 B: code+var_min+var_max+linger) and a separate `else` branch for
  anything else (2/5/6/7), which throws `std::runtime_error` — matching FAudio, whose own switch
  (`FACT_internal.c:2798-2845`) also covers only 0/1/3/4 and asserts on `default`. New tests
  `XactParserTest.VariationTypeInteractiveParsesSixteenByteEntry` (positive, type 3) and
  `...VariationTypeClipThrows` (type 2) verified on the ORIGINAL (pre-fix) code via `git stash` —
  `ClipThrows` fails without the fix ("throws nothing" — the old catch-all silently accepted type
  2), passes with the fix; `Interactive...` passes on both versions (the byte layout for type 3
  was already correct before, just mis-named/mis-reached). Whole suite 1990/1990 tests green.

- [x] **IN-5 — `XactTypes.hpp` still uses bare `///` comments instead of Doxygen blocks.** *(done
  2026-07-03.)* Contrary to `CLAUDE.md` ("Never use bare `///` comments on public API members") —
  SPDX was added (T-1A), but the `///`→`/** @brief */` conversion was never its own task.
  *File:* include/CNA/Internal/Audio/XactTypes.hpp (whole file).
  *Accept:* convert all `///`/`//` member descriptions to `/** @brief … */` blocks.
  *Note:* the whole file was rewritten — every struct/enum and every member now has a
  `/** @brief … */` block (even previously entirely uncommented members, for consistency with the
  "every .hpp file" rule); `ParseXgs`/`ParseXwb`/`ParseXsb` have full blocks with
  `@param`/`@return`. A purely documentation change — no behavior changes. Whole suite
  1996/1996 tests green (test count unchanged).

- [x] **IN-6 — Thin test coverage of `XactParser` (4 tests) doesn't cover safety/functionally
  critical branches.** *(done 2026-07-03.)* Missing: corrupted/truncated headers and bad magic
  numbers for all 3 formats, non-compact `.xwb` (only compact was covered),
  `entryMetaDataSize<24` non-compact fallback (IN-2), `SOUND_FLAG_HAS_RPC`/`SOUND_FLAG_HAS_DSP`
  (IN-1), ADPCM format, all 4 variation-table types, the `RAMP` form of PITCH/VOLUME events (only
  the "equation" form was tested). The test gap directly correlated with the undetected IN-1/IN-2
  bugs.
  *File:* tests/CNA/Internal/Audio/XactParserTests.cpp (whole file).
  *Accept:* add fixtures/tests for at least: a truncated file → throw (all 3 formats), bad magic
  → throw, non-compact `.xwb` with `entryMetaDataSize==24` and `<24`, `HAS_DSP`/`HAS_RPC` sound,
  an ADPCM entry, all 4 variation-table types, the RAMP form of a PITCH event.
  *Note:* no production code change — purely coverage expansion (8→22 tests in this file).
  Added: 6 truncated/bad-magic tests (one each for `ParseXgs`/`ParseXwb`/`ParseXsb` × 2 kinds),
  `BuildNonCompactAdpcmXwbFixture` (one full `entryMetaDataSize==24` ADPCM entry — covers both
  the standard 24B layout and `blockAlign`/`samplesPerBlock` derivation from `wBlockAlign`),
  `BuildXsbWithRpcThenSecondSound` (mirroring IN-1's DSP test, but for `SOUND_FLAG_HAS_RPC`),
  `BuildPitchRampEventBytes` + a test for the RAMP form of a PITCH event, and
  `BuildXsbWithVariationOfType` extended with types 0 (WAVE) and 1 (SOUND) — together with the
  existing types 3 (INTERACTIVE, IN-4) and 4 (COMPACT_WAVE, newly added), all 4 types now have
  direct parser-level tests. Whole suite 2018/2018 tests green.

#### 7.4 Microphone, data classes, enums, exceptions

> `AudioEmitter`, `AudioListener`, `AudioChannels`, `AudioStopOptions`, `MicrophoneState`, and all
> 3 audio exceptions (`InstancePlayLimitException`, `NoAudioHardwareException`,
> `NoMicrophoneConnectedException`) were checked line by line — fully compatible, no new tasks.

- [x] **MC-1 — `Microphone::GetSampleDuration`/`GetSampleSizeInBytes` doesn't delegate to
  `SoundEffect`, its own formula has different precision than FNA.** *(done 2026-07-03.)* FNA
  calls `SoundEffect.GetSampleDuration`/`GetSampleSizeInBytes(sizeInBytes, SampleRate,
  AudioChannels.Mono)`, which rounds to whole milliseconds. CNA's
  `Microphone::GetSampleDuration` instead computed its own
  `seconds = sizeInBytes/(SAMPLERATE*channels*2)` without rounding — for non-divisible values
  (e.g. 100 B @ 44100 Hz mono) it returned a different value than FNA. This was exactly the
  unmet acceptance criterion of T-4A (see the T-4A note above) — `GetSampleSizeInBytes` was
  numerically equivalent but duplicated.
  *FNA:* Microphone.cs:172-188; SoundEffect.cs:363-387.
  *CNA:* Microphone.cpp:161-176 (own formula); SoundEffect.cpp:334-363 (the correct
  implementation it should delegate to).
  *Accept:* `Microphone::GetSampleDuration`/`GetSampleSizeInBytes` call
  `SoundEffect::GetSampleDuration(sizeInBytes, SAMPLERATE, AudioChannels::Mono)`/
  `GetSampleSizeInBytes(...)` (no duplicated math). A test at a non-integer boundary (100 B)
  verifying truncation to whole ms matching FNA.
  *Note:* both methods now call `SoundEffect::GetSampleDuration`/
  `GetSampleSizeInBytes(…, getSampleRateProperty(), AudioChannels::Mono)` in a single line — no
  duplicated math, exactly like FNA (Microphone.cs:172-188). New test
  `GetSampleDurationDelegatesToSoundEffectWithMonoAndSampleRate` (100 B → 1.133 ms truncated to
  1 ms) verified on the ORIGINAL (pre-fix) code via `git stash` — fails without the fix (the old
  own formula returns an untruncated value), passes with the fix. The accompanying test for
  `GetSampleSizeInBytes` doesn't numerically distinguish old/new behavior (both are equivalent),
  but pins the delegation going forward. Whole suite 1986/1986 tests green.

- [x] **MC-2 — Stale comment and dead declaration `friend class MicrophoneFactory` in
  `Microphone.hpp`.** *(done 2026-07-03.)* `MicrophoneFactory` didn't exist anywhere in the repo,
  and since T-4A a real SDL3 capture backend exists — instances are created directly in
  `Microphone::getAllProperty()` (`new Microphone(...)`), not via any factory. The comment
  "Production Microphone instances only come from a real capture backend, which does not exist
  yet" was now false.
  *CNA:* Microphone.hpp:141-145.
  *Accept:* remove the unused `friend class MicrophoneFactory;` and rewrite the comment to match
  the current state.
  *Note:* `friend class MicrophoneFactory;` removed (unused anywhere else in the repo), the
  comment rewritten to describe the real state (instances are created directly by
  `getAllProperty()`; `MicrophoneTestAccess` bypasses enumeration for isolated tests). Purely a
  cleanup change, whole suite 1996/1996 tests green.

- [x] **MC-3 — `GetData()`, when data is unavailable, overwrites the entire requested buffer
  range with zeros; FNA doesn't touch the buffer at all.** *(done 2026-07-03.)* FNA returns just
  the number of bytes actually read and leaves the rest of the buffer unchanged. CNA, on
  `read <= 0` (no stream, nothing to read, even an SDL error returning a negative number), always
  filled the whole requested range with zeros — an error state was thus silently masked as "0
  bytes, buffer zeroed", while also overwriting any valid old data the caller had.
  *FNA:* Microphone.cs:149-170.
  *CNA:* Microphone.cpp:128-159 (specifically 144-158).
  *Accept:* either formally document this as an approved deviation in `CHECKLIST.md`, or narrow
  the zeroing to only the "no open stream" case and leave the buffer untouched on an SDL error;
  test.
  *Note:* chose D8's preferred option — align with FNA (no zeroing at all, not just narrowing).
  `GetData` on `read <= 0` (no stream, nothing to read, even an SDL error) now just returns 0 and
  leaves the buffer completely untouched, exactly like FNA (`Microphone.GetData` delegates
  directly to the platform with no zeroing fallback). Also removed the now-unused
  `#include <algorithm>`. New test `GetDataLeavesBufferUntouchedWhenNoDataAvailable` (buffer
  pre-filled with `0xAB`, must remain `0xAB` after `GetData`) verified via `git stash` — fails
  without the fix (`0x00 != 0xAB`, the old behavior zeroed the buffer), passes with the fix.
  Whole suite 2019/2019 tests green.

- [x] **MC-4 — Missing test that `BufferReady` actually fires during real capture of data
  exceeding `BufferDuration`.** *(done 2026-07-03.)* Existing tests only verified that calling it
  doesn't crash. Given that `GetQueuedBytes`/`CheckBuffer` were just switched over to real SDL
  data (previously always returning 0, so `BufferReady` could never fire), this was the riskiest
  and least-verified behavior — directly related to MC-1 (a wrong `GetSampleDuration` could shift
  the threshold without any test catching it).
  *FNA:* Microphone.cs:206-213.
  *CNA:* Microphone.cpp:218-224; tests/.../MicrophoneTests.cpp:220-229.
  *Accept:* a new test (analogous to `MicrophoneCaptureTest`) — set a small `BufferDuration`,
  register a `BufferReady` handler, `Start()`, repeatedly call `CheckBuffer()` in a loop with a
  timeout, verify the handler was called at least once.
  *Note:* no production code change. New
  `MicrophoneCaptureTest.BufferReadyFiresWhenQueuedDataExceedsBufferDuration` sets
  `BufferDuration=100ms` (the allowed minimum), registers a handler, `Start()`s, and loops (up to
  40× at 50ms) calling `CheckBuffer()` until the handler fires — verified stable 3x in a row with
  no flakiness. Also fixed a latent test-infra problem: `MicrophoneCaptureTest::TearDown()` now
  also calls `BufferReady.Clear()` — without this, a lambda captured locally by a test
  (referencing a local variable out of scope) would stay forever attached to the shared
  `getDefaultProperty()` singleton and could be called (use-after-scope) even by later tests in
  the same binary. Whole suite 2020/2020 tests green.

- [x] **MC-5 — `GetData` has no separate test for a negative `count`, only for `count == 0`.**
  *(done 2026-07-03.)* The validation is `count <= 0`, but the existing test
  `GetDataZeroOrNegativeCountThrows` (despite its name) only tested `count == 0`.
  *CNA:* Microphone.cpp:139-142; tests/.../MicrophoneTests.cpp:204-209.
  *Accept:* add a `GetDataNegativeCountThrows` test with `count < 0`, verifying
  `System::ArgumentException`.
  *Note:* no production code change. Added `MicrophoneTest.GetDataNegativeCountThrows`
  (`count=-5`) right after the existing test. Whole suite 1997/1997 tests green.

### Phase 8 — Second supplementary audit (2026-07-04): new findings beyond Phase 7

> After closing the entire remaining backlog (T-4D, T-3F, T-3G, T-4B, T-6C, T-4C — see their
> `*Note:*` entries above), a **second fresh** line-by-line audit of the whole
> `Microsoft::Xna::Framework::Audio` cluster against FNA was run at the user's request, structured
> the same way as Phase 7 — 4 parallel checks (Core Playback, XACT, internal backend,
> Mic/data/enums/exceptions), each a separate agent with its own independent pass over the
> source. The two most severe findings (IN-7, IN-8) were additionally manually verified directly
> against `FAudio/src/FACT_internal.c` (not just taken from the agent's report), as were
> XA-6/XA-7/CP-15 directly against CNA's current source — see their `*Note:*` entries.
>
> IDs continue the existing prefixes from Phase 7 (`CP`/`XA`/`IN`/`MC`) so findings can be sorted
> by cluster without colliding with T-* or Phase-7 numbers. Ordered within each cluster by
> severity (real bugs → compliance/behavior/documented deviations → test gaps).

**Most severe findings (quick overview, details below):**
- **IN-7** — the channel count (`nChannels`) is read with an incorrect `+1` for EVERY `.xwb`
  entry (compact and non-compact) — mono plays as stereo and vice versa. Verified directly
  against `FACT_internal.c`.
- **IN-8** — for a COMPLEX sound with an RPC or DSP flag, per-track metadata
  (volume/code/filter/frequency) is read BEFORE the RPC/DSP block instead of AFTER it — reversed
  order vs. FACT, cascading corruption of the rest of the file's parsing. Verified directly
  against `FACT_internal.c`.
- **XA-6** — `Cue::Stop(AudioStopOptions::AsAuthored)` behaves identically to `Stop(Immediate)` —
  `StopInternal` always calls `active_.clear()`, which hard-destroys even instances that were
  only supposed to ring out.
- **XA-7** — `SoundBank`'s fire-and-forget sweep also removes a cue that's merely PAUSED (checks
  only `IsPlaying`), so `category.Pause()` on a fire-and-forget cue → the next `PlayCue()` on the
  same bank silently destroys it.
- **CP-15** — `DynamicSoundEffectInstance::Pause()`/`Resume()` are dead code — inherited from the
  base class, which works with `track_`, but `DynamicSoundEffectInstance` always uses its own
  `dynamicTrack_`.
- **CP-16** — `SoundEffect::MasterVolume` has no effect on already-playing sounds, only on
  future `Play()` calls.

#### 8.1 Core Playback (SoundEffect, SoundEffectInstance, DynamicSoundEffectInstance)

- [x] **CP-15 — `DynamicSoundEffectInstance::Pause()`/`Resume()` are dead code.**
  `Pause()`/`Resume()` (SoundEffectInstance.cpp:373-397) aren't `virtual` and always work with the
  protected `track_`. `DynamicSoundEffectInstance::Play()`/`Stop()` have their own overrides, but
  work exclusively with their own `dynamicTrack_` (`track_` on a dynamic instance stays
  `nullptr` forever) — calling `Pause()`/`Resume()` on a `DynamicSoundEffectInstance` is thus
  always a silent no-op.
  *FNA:* SoundEffectInstance.cs:375-397 (a shared `handle` field for both static and dynamic).
  *CNA:* SoundEffectInstance.hpp:88-92 (non-virtual); DynamicSoundEffectInstance.hpp/.cpp (no
  override, no reference to `track_`).
  *Accept:* `DynamicSoundEffectInstance::Pause()`/`Resume()` (via `virtual` on the base, or a
  shared handle abstraction) actually pause/resume `dynamicTrack_`; test: `Play()`→`Pause()`→
  assert `State==Paused`→`Resume()`→assert `State==Playing`, under the dummy driver.
  *Note:* `Pause()`/`Resume()` are now `virtual` on the base; `DynamicSoundEffectInstance` has its
  own override operating on `dynamicTrack_` (the same pattern as the existing `Play()`/`Stop()`
  overrides). Added 2 tests (`Play→Pause→Resume` both directly and via a
  `SoundEffectInstance&` base reference, verifying virtual dispatch). `git stash` confirmed both
  fail against the old (non-virtual) implementation.

- [x] **CP-16 — `SoundEffect::MasterVolume` doesn't affect already-playing sounds.**
  `MasterVolume_` is a static float multiplied into the gain only at the moment of
  `Play()`/`setVolumeProperty()`. Changing `MasterVolume` after a sound has started has no effect
  on already-playing instances (nor on fire-and-forget `SoundEffect::Play()` tracks) — meanwhile
  SDL3_mixer has a real global mixer gain (`MIX_SetMixerGain`/`MIX_GetMixerGain`) that's never
  used.
  *FNA:* SoundEffect.cs:51-70 (`MasterVolume` goes directly to the shared mastering voice).
  *CNA:* SoundEffect.cpp:210-223,298-311; SoundEffectInstance.cpp:314,541.
  *Accept:* `setMasterVolumeProperty` uses `MIX_SetMixerGain` (or re-applies gain to every live
  track); a test verifies `MIX_GetTrackGain` after changing `MasterVolume` on an already-playing
  instance.
  *Note:* `getMasterVolumeProperty`/`setMasterVolumeProperty` now read/write
  `MIX_GetMixerGain`/`MIX_SetMixerGain` directly (live, no local cache — same as FNA always
  querying/setting the real FAudio master voice). To avoid double-counting, master volume is no
  longer multiplied into each track's own gain (`ApplyTrackProperties` lost its `masterVolume`
  parameter; `SoundEffect::Play()`'s fire-and-forget path and
  `SoundEffectInstance::setVolumeProperty`/`DynamicSoundEffectInstance::Play()` no longer multiply
  it in either) — the mixer gain is now the sole mechanism and applies live to every track,
  including already-playing ones, with nothing needing manual re-application. Added a test
  verifying `MIX_GetMixerGain` (not `MIX_GetTrackGain`, which intentionally stays constant) after
  changing `MasterVolume` on an already-playing instance — the first version of the test
  mistakenly only checked `getMasterVolumeProperty()`, which would have passed even against the
  old (non-functional) implementation, since that one also just round-trips through the static
  field; fixed to call `MIX_GetMixerGain` directly, and `git stash` then confirmed it fails
  against the old implementation.

- [x] **CP-17 — `SoundEffect`'s loop region (`loopStart`/`loopLength`) is captured but never used.**
  The buffer constructor with an explicit loop range stores `loopStart_`/`loopLength_`, but
  nothing ever reads them. `FromStream` also doesn't parse the WAV `smpl` chunk at all. `Play()`
  always loops the whole buffer (`MIX_PROP_PLAY_LOOPS_NUMBER`), never just the authored loop
  range.
  *FNA:* SoundEffect.cs:476-513 (`smpl` chunk in `FromStream`); SoundEffectInstance.cs:350-361
  (`LoopBegin`/`LoopLength` set before submitting the buffer).
  *CNA:* SoundEffect.hpp:35-36,82-88; SoundEffect.cpp:118-129,406-452; SoundEffectInstance.cpp:314-337.
  *Accept:* `FromStream` parses `smpl` loop points like FNA; `Play()` applies `loopStart_`/
  `loopLength_` via SDL3_mixer's `MIX_PROP_PLAY_LOOP_START_FRAME_NUMBER` (and length, if
  possible); a test with a nonzero loop range verifies only that range loops, not the whole
  buffer.
  *Note:* `SoundEffectInstance` now copies `loopStart_`/`loopLength_` from `SoundEffect` at
  construction (the same pattern as `nativeAudioHandle_` — CP-7 forbids holding a `SoundEffect&`).
  `Play()` sets `MIX_PROP_PLAY_LOOP_START_FRAME_NUMBER`; for length, SDL3_mixer has no "loop end"
  property distinct from "end of the whole track" — `MIX_PROP_PLAY_MAX_FRAME_NUMBER` is used
  instead, but (unlike FNA/XAudio2's `LoopBegin`/`LoopLength`) this also truncates the very first,
  pre-loop playthrough to `loopStart_+loopLength_`, not just later iterations — documented as an
  accepted deviation in CHECKLIST.md. `FromStream` now additionally scans the raw WAV bytes for a
  `smpl` chunk independently of `MIX_LoadAudio_IO` (`TryParseWavSmplChunk`) — a purely byte-level
  parser, no SDL types. Added 4 tests (buffer-ctor propagation into the instance, `FromStream`
  with/without an `smpl` chunk, a truncated/corrupt `smpl` chunk must not crash) via a new
  `SoundEffectInstanceTestAccess::LoopStart/LoopLength` (SDL3_mixer has no way to read back the
  loop-start/max-frame values passed to `MIX_PlayTrack`, so the actual mixed effect can't be
  black-box-verified without decoding the real audio output). `git stash` confirmed a compile
  failure (missing fields) in every test file sharing `SoundEffectInstanceTestAccess.hpp`. Clean
  under ASan+LeakSanitizer.

- [x] **CP-18 — Missing audio hardware reports `std::runtime_error`, never
  `NoAudioHardwareException`.**
  `AudioMixer::GetMixer()` (see also IN-11) throws `std::runtime_error`, which inherits from
  `std::exception`, not from `System::Exception` — code doing `catch (const System::Exception&)`
  never catches it at all. `NoAudioHardwareException` exists and is tested in isolation, but is
  never actually thrown anywhere.
  *FNA:* SoundEffect.cs:784-817 (`Device()` throws `NoAudioHardwareException`).
  *CNA:* src/CNA/Internal/Audio/AudioMixer.cpp:15-36; callers SoundEffect.cpp:86,143,298,428,
  SoundEffectInstance.cpp:292.
  *Accept:* `GetMixer()` (or its callers) throws `Microsoft::Xna::Framework::Audio::
  NoAudioHardwareException` instead of `std::runtime_error`; a test simulating mixer-creation
  failure verifies the correct exception type. (Tied to XA-9 — handle together.)
  *Note:* discussed with the user together with XA-9 (the same question, the same answer) —
  chose a documented deviation. See XA-9's `*Note:*` for the full reasoning (rewriting
  `SharedEngine()` in 4 test files would also be needed for this part, since
  `AudioEngine::NoAudioHardwareException` should in theory also be throwable from `AudioEngine`'s
  own constructor, not just from `AudioMixer::GetMixer()`). Documented in CHECKLIST.md.

- [x] **CP-19 — Panning a stereo source silences the entire opposite channel instead of a
  crossfeed blend.**
  `ApplyTrackProperties`'s pan formula (`left=(pan<0)?1:(1-pan)`) at `Pan=1.0` (hard right) gives
  `left gain=0` — for a stereo source, the left channel disappears entirely. FNA has an explicit
  comment that hard panning should NOT eliminate an entire channel, and uses a full 4-coefficient
  matrix for stereo→stereo. For MONO sources, CNA's formula is bit-exact with FNA — the problem
  is only with stereo content.
  *FNA:* SoundEffectInstance.cs:606-648 (`SetPanMatrixCoefficients`).
  *CNA:* SoundEffectInstance.cpp:51-67; SoundEffect.cpp:313-316 (duplicated formula).
  *Accept:* either document it as an accepted deviation in CHECKLIST.md (SDL3_mixer's
  `MIX_StereoGains` has no crossfeed API), or manually mix the stereo pan; a test comparing CNA's
  gains against FNA's matrix for a stereo source at several pan values.
  *Note:* discussed with the user — chose a documented deviation, not an implementation. A real
  manual mix would have to share SDL3_mixer's SINGLE per-track "cooked callback" slot with T-4C's
  already-shipped DSP filter (merging pan-crossfeed math and the filter into one callback +
  registering it for every stereo instance, not just filtered ones) — a real risk of regressing
  the already-debugged and tested filter code, unlike this session's earlier "implement"
  decisions (T-3F/T-3G/T-4C), which didn't touch already-working shared infrastructure. Documented
  in CHECKLIST.md.

- [x] **CP-20 — `setPanProperty()` ignores an active `Apply3D` state.**
  FNA has an `is3D` latch — once `Apply3D` has been called at least once, the `Pan` setter only
  updates the property value, it doesn't touch the actual output matrix (that's only recomputed
  by the next `Apply3D`). CNA has no `is3D_` equivalent — `setPanProperty()` always immediately
  overwrites the track's stereo gains, so a manual `setPanProperty()` between two `Apply3D()`
  calls (or after a single `Apply3D()`) overwrites the 3D positioning with the wrong value. The
  same class of bug as the already-fixed CP-3, but in the opposite call order.
  *FNA:* SoundEffectInstance.cs:52-84 (`Pan` setter: `if (is3D) return;`).
  *CNA:* SoundEffectInstance.cpp:556-583 (`setPanProperty`); `Apply3D` at lines 471-506 (no
  `is3D_`-equivalent flag).
  *Accept:* add an `is3D_`-equivalent flag; `setPanProperty` after `Apply3D()` only updates the
  property, doesn't write to the track; test: `Apply3D(...)` → `setPanProperty(x)` → verify the
  real track gains still match the 3D position, not `x`.
  *Note:* added `is3D_` (never reset, same as FNA); `setPanProperty`, after setting `Pan_`,
  returns early if `is3D_==true`. SDL3_mixer has no stereo-pan getter (the same limitation as
  CP-3/T-4B), so the test verifies the `is3D_` state directly via a new
  `SoundEffectInstanceTestAccess::Is3D`, not the real track gains. `git stash` confirmed a compile
  failure (missing field) in every file sharing `SoundEffectInstanceTestAccess.hpp`.

- [x] **CP-21 — `AudioCategory::SetVolume`'s doc comment in the `AudioCategory.hpp` header
  matches the old, already-fixed behavior (a minor finding, belongs more under XA — see XA-10,
  mentioned here for completeness, not handled twice).**
  *Note:* resolved together with XA-10 (see its `*Note:*` above).

- [x] **CP-22 — Test gap: `SoundEffect`'s move ctor/move assignment has no test of its own.**
  A `static_assert` only verifies move-constructibility/assignability; no test actually moves a
  `SoundEffect` and verifies that an instance created before the move (via the `impl_` keep-alive)
  still works.
  *CNA:* SoundEffect.hpp:105-109; SoundEffectTests.cpp:27-30 (only a `static_assert`).
  *Accept:* add `SoundEffectTest.MoveConstructor.../MoveAssignment...` tests analogous to CP-12
  (for `SoundEffectInstance`), verifying that a `SoundEffectInstance` created from a moved
  `SoundEffect` still plays correctly.
  *Note:* added `MoveConstructedEffectStillCreatesAWorkingInstance`/
  `MoveAssignedEffectStillCreatesAWorkingInstance` — creates an instance from a moved
  `SoundEffect`, calls `Play()`, and verifies `State==Playing`. A purely test-only addition, no
  production code change (hence no `git stash` step).

- [x] **CP-23 — Test gap: the buffer constructor with `loopStart`/`loopLength` has no
  success-path test.**
  The existing test only covers the exception path for a bad range, not the actual effect of a
  valid loop range on playback — which is exactly what would have caught CP-17 sooner.
  *CNA:* SoundEffectTests.cpp:171-179.
  *Accept:* add a test with a nonzero `loopStart`/`loopLength` once CP-17 is fixed (or a test
  documenting today's "dead field" behavior, if CP-17 is deferred).
  *Note:* resolved together with CP-17 —
  `BufferRangeConstructorPropagatesLoopRegionToInstance` (see CP-17's `*Note:*` above for the
  full list of new tests).

#### 8.2 XACT (AudioEngine, AudioCategory, Cue, SoundBank, WaveBank)

- [x] **XA-6 — `Cue::Stop(AudioStopOptions::AsAuthored)` behaves identically to
  `Stop(Immediate)`.**
  Verified directly in the source: `StopInternal` first correctly calls
  `pi.instance->Stop(immediate)` (for `immediate=false` just `MIX_SetTrackLoops(track,0)`, the
  track keeps playing), but the VERY NEXT line is an unconditional `active_.clear()`, which
  destroys the `unique_ptr<SoundEffectInstance>` → `~SoundEffectInstance()` → `Dispose()` →
  `DestroyTrackSafe()` → a hard stop. `AsAuthored` thus never lets the release/loop tail ring
  out.
  *FNA:* Cue.cs:257-265 (`FACT_FLAG_STOP_RELEASE` lets the voice ring out asynchronously, the cue
  isn't hard-destroyed).
  *CNA:* Cue.cpp:292-306 (`StopInternal`).
  *Accept:* on a real WaveBank-backed cue (e.g. an `Apply3DCue`/`VolCue`-style fixture),
  `Stop(AsAuthored)` on a looped instance leaves the track still playing right after the call
  (it only ends the loop), while `Stop(Immediate)` hard-stops it immediately — the test reads the
  real `MIX_Track*` via `SoundEffectInstanceTestAccess`, not just the `Cue`'s own state.
  *Note:* `active_.clear()` now only runs for `immediate==true`; for `AsAuthored`, `active_`
  stays untouched (the instance is only destroyed at `Cue::Dispose()`, matching FNA — the native
  engine also keeps the voice alive until the release genuinely finishes). The test needed a
  longer (1s) fixture — the original shared fixtures (200 B ≈ 1ms) were too short for a reliable
  live check of `MIX_TrackPlaying`; a new fixture was added as
  `SharedLongBank`/`"LongCue"`. `git stash` confirmed a failure against the old logic. Clean
  under ASan+LeakSanitizer. Deliberately not addressed: `SoundBank`'s fire-and-forget sweep
  (XA-1/XA-7) could sweep such a "releasing" (Stopped, but not Paused) cue before the release
  genuinely finishes — outside this finding's scope, not mentioned by the audit.

- [x] **XA-7 — The fire-and-forget sweep in `SoundBank::PlayCueInternal` also removes a cue that's
  merely PAUSED.**
  Verified directly in the source: the sweep predicate only checks `getIsPlayingProperty()` — if
  a cue is paused (`state_==Paused`), `getIsPlayingProperty()` returns `false`, so the cue is
  unconditionally swept (`return true;`) on the very next `PlayCue()` on the same bank.
  `getIsInUseProperty()` on both `SoundBank` and `WaveBank` has the identical bug.
  *FNA:* SoundBank.cs:28-36 (`IsInUse` reflects `FACT_STATE_INUSE`, which stays set while
  paused).
  *CNA:* SoundBank.cpp:142-154 (sweep), :83-89 (`getIsInUseProperty`); WaveBank.cpp:204-210.
  *Accept:* the sweep predicate (and `IsInUse`) must treat `IsPlaying || IsPaused` (or more
  generally "not yet `IsStopped`") as alive; test: pause a category containing a fire-and-forget
  cue, trigger another `PlayCue()` on the same bank, verify the paused cue survives and can still
  be `Resume()`d.
  *Note:* the sweep predicate and both `getIsInUseProperty()` (SoundBank and WaveBank) now treat
  `IsPlaying || IsPaused` as alive. The tests call `Cue::Pause()` directly (not via
  `AudioCategory::Pause()`) — the cue-level state transition is independent of whether the cue
  has a real WaveBank (`Pause()`'s guard runs even with an empty `active_`), so the wavebank-less
  "Explosion" fixture, same as the neighboring sweep tests, is sufficient. `git stash` confirmed
  all 3 new tests fail against the old logic.

- [x] **XA-8 — `AudioEngine::Dispose()` doesn't cascade to already-constructed
  `SoundBank`/`WaveBank`/`Cue`.**
  FNA, via native `OnXACTNotification` (WAVEBANKDESTROYED/SOUNDBANKDESTROYED/CUEDESTROYED),
  immediately sets `IsDisposed=true` on every dependent wrapper as soon as the native engine goes
  away. CNA's `AudioEngine::Dispose()` only resets its own `xactImpl_` — no `WaveBank`/`Cue`
  (and for `SoundBank` there's no registry at all) finds out the engine is gone.
  *FNA:* AudioEngine.cs:382-432; WaveBank.cs:204-222; SoundBank.cs:270-280; Cue.cs:271-276.
  *CNA:* AudioEngine.cpp:176-185 (`Dispose`); AudioEngine.hpp:112-147 (no `SoundBank` registry).
  *Accept:* after `AudioEngine::Dispose()`, every `WaveBank`/`SoundBank`/`Cue` created from it
  reports `getIsDisposedProperty()==true` (test for all three) — requires a `SoundBank` registry
  symmetric to the existing `WaveBank` registry; or document it as an accepted deviation in
  CHECKLIST.md if it's intentionally out of scope.
  *Note:* added a `SoundBank` registry (`RegisterSoundBank`/`UnregisterSoundBank`, symmetric to
  `WaveBank`'s). `Dispose()` now first snapshots all three registries (`WaveBank*`, `SoundBank*`,
  `Cue*`) into local vectors, THEN resets `xactImpl_` (so reentrant `Unregister*` calls from their
  own `Dispose()`s are safe no-ops), and only then calls `Dispose()` on each — in the order cues →
  soundbanks → wavebanks. The idempotent `Dispose()` pattern (`if (!isDisposed_)`) already
  existed in the code, so there's no double-free even if, e.g., `SoundBank` later tries to
  dispose its `fireAndForget_` cue again. Added a test with its own (non-shared) `AudioEngine`,
  since the test disposes the engine. `git stash` confirmed a failure against the old logic.
  Clean under ASan+LeakSanitizer. Deliberately not addressed: a cue that was only `GetCue()`d
  but never `Play()`ed never enters the registry at all (the same limitation as the existing
  `RegisterCue`/`UnregisterCue` mechanism) — outside this finding's scope.

- [x] **XA-9 — `AudioEngine`/`SoundBank`/`WaveBank` constructors silently swallow a missing file or
  a parse error instead of throwing; `NoAudioHardwareException` is never thrown from
  `AudioEngine`.**
  All three constructors: if the file can't be opened → `cerr` + return; if parsing throws →
  `cerr` + swallowed. The object stays in a silent "stub" state (no categories/cues/waves); later
  lookups only report a generic `InvalidOperationException`, not a signal from construction time.
  `AudioEngine`'s ctor also never checks real audio hardware availability — `rendererDetails_`
  always has exactly one hardcoded `RendererDetail`, so `NoAudioHardwareException` can never fire
  from `AudioEngine` (see also CP-18).
  *FNA:* AudioEngine.cs:117-184 (`TitleContainer.ReadToPointer` throws on a missing file;
  `rendererCount==0` → `throw new NoAudioHardwareException()`); SoundBank.cs:73-74;
  WaveBank.cs:84-87.
  *CNA:* AudioEngine.cpp:64-109 (`Init`); SoundBank.cpp:42-72; WaveBank.cpp:148-192.
  *Accept:* either (a) constructors throw the appropriate `System::` exception on a missing file/
  corrupted data per the FNA contract (test with a nonexistent path and with a
  corrupted-but-present file for each class), or (b) the "silent stub" behavior is written into
  CHECKLIST.md as a consciously decided deviation (caveat: existing test fixtures, e.g.
  `SoundBankTests.cpp`'s `SharedEngine()`, actively rely on this stub behavior — option (a) would
  have to update them).
  *Note:* discussed with the user — chose option (b), a documented deviation. `SharedEngine()`
  is independently defined in 4 test files (`CueTests.cpp`, `WaveBankTests.cpp`,
  `SoundBankTests.cpp`, `AudioCategoryTests.cpp`) and deliberately points at a nonexistent `.xgs`
  path; option (a) would have to rewrite it in all four and re-verify the ~80+ tests built on top
  of it — a broad, cross-cutting change to the shared foundation for the sake of an edge case
  (a missing/corrupt content file, missing audio hardware), not a user-visible playback bug.
  Documented in CHECKLIST.md (together with CP-18).

- [x] **XA-10 — `AudioCategory.hpp`'s Doxygen contradicts the real (correct) behavior of
  `SetVolume`.**
  The doc claims `SetVolume` doesn't affect already-playing cues — since the T-4D fix that's no
  longer true (`SetVolume` retroactively applies, confirmed by the passing test
  `SetVolumeReappliesToAlreadyPlayingCueInstance`).
  *CNA:* AudioCategory.hpp:16-20,33-38 (doc); AudioEngine.cpp:224-232 (real behavior).
  *Accept:* rewrite both Doxygen blocks to match the real, correct behavior (as precisely as the
  neighboring `Pause`/`Resume`/`Stop` doc blocks).
  *Note:* both blocks (the class doc and `SetVolume`'s own) were rewritten. Purely
  documentation, no behavior change; the existing `SetVolumeReappliesToAlreadyPlayingCueInstance`
  test still passes unchanged.

- [x] **XA-11 — Category `instanceLimit`/`fadeInMS`/`fadeOutMS` are parsed but never enforced or
  applied anywhere — a gap not recorded in CHECKLIST.md.**
  This was consciously deferred at T-4D (see its `*Note:*`), but the decision was never carried
  into CHECKLIST.md's deviations table, unlike every other similar decision (D1-D8).
  *CNA:* XactTypes.hpp:26-30 (fields exist); XactParser.cpp:309-322 (parsed); no consumer in
  AudioEngine.cpp/AudioCategory.cpp/Cue.cpp.
  *Accept:* add a row to CHECKLIST.md documenting that category fade in/out and instance-limit
  enforcement are out of scope (or implement them).
  *Note:* row added to CHECKLIST.md. Purely documentation, no code change.

- [x] **XA-12 — `AudioEngine::ContentVersion` uses a raw `int` instead of `SharpRuntime::intcs`.**
  The only public integer constant in the entire Audio cluster that doesn't use the project's
  type alias (CLAUDE.md's type table).
  *CNA:* AudioEngine.hpp:31.
  *Accept:* change to `static constexpr SharpRuntime::intcs ContentVersion = 46;`; the existing
  `ContentVersionIs46` test still passes unchanged.
  *Note:* done exactly per the acceptance criteria;
  `SharpRuntime/SharpRuntimeHelper.hpp` added to `AudioEngine.hpp`'s includes. No test change
  needed.

- [x] **XA-13 — Test gap: no test constructs `AudioEngine`/`SoundBank`/`WaveBank` against an
  existing but corrupted `.xgs`/`.xsb`/`.xwb` file.**
  Existing tests only cover "file doesn't exist" and "valid fixture" — never "the file exists but
  contains garbage" at the wrapper-constructor level (unlike `XactParserTests.cpp`, which tests
  this at the parser level itself, not the wrapper's).
  *CNA:* AudioEngineTests.cpp; SoundBankTests.cpp; WaveBankTests.cpp.
  *Accept:* add one test per class with an existing-but-corrupted file, explicitly verifying the
  current (or newly decided, post-XA-9) behavior.
  *Note:* XA-9 decided to keep the current "silent stub" behavior, so the tests lock in EXACTLY
  THAT behavior (not new behavior). Added one test per class: a constructor with an existing but
  corrupted file doesn't throw and the object stays in a silent stub state (`AudioEngine`/
  `SoundBank`: subsequent `GetCategory`/`GetCue` on any name throws
  `InvalidOperationException`, same as for a missing file; `WaveBank`:
  `getIsPreparedProperty()==false`). A purely test-only addition, no production code change.

#### 8.3 Internal backend (AudioMixer, XactParser, XactTypes)

- [x] **IN-7 — `nChannels` is read with an incorrect `+1` for EVERY `.xwb` entry (compact and
  non-compact).**
  Manually verified against `FAudio/src/FACT_internal.c:1782,1793,1855,1866` —
  `entry->Format.nChannels` is used DIRECTLY as a multiplier in the byte-size math, no `+1`
  anywhere. The raw 3-bit field on disk already IS the real channel count (1=mono, 2=stereo), not
  "channel count minus one". CNA adds `+1` in both places (`compactFormat`, `fmt`), so mono is
  parsed as stereo (and stereo as 3-channel) for EVERY real `.xwb` entry, not just corrupted
  data.
  *FAudio ref:* FACT_internal.c:1782,1793,1855,1866 (used directly, unmodified).
  *CNA:* XactParser.cpp:455 (compact), :520 (non-compact).
  *Accept:* remove the `+1` in both places; a regression test with a synthetic compact and
  non-compact fixture with a raw `nChannels` field of `1` and `2`, verifying
  `entry.channels==1`/`==2` — against an independently derived expected value, not against
  existing fixtures which themselves assume today's (wrong) convention.
  *Note:* the `+1` was removed in both places (`XactParser.cpp:454`, `:519`). All 9 existing
  fixtures that encoded mono/stereo via the old "field = channels minus one" convention
  (`XactParserTests.cpp` ×3, `SoundBankTests.cpp`, `CueTests.cpp`, `AudioCategoryTests.cpp`,
  `WaveBankTests.cpp` ×2) were fixed to use the raw value directly. Added 4 new tests with an
  independently derived value (compact stereo, non-compact stereo, plus mono via the existing
  ADPCM/compact tests) — `git stash` on `XactParser.cpp` confirmed 2 of them fail against the old
  `+1` logic. Whole suite 2045/2045 tests green, clean under ASan+LeakSanitizer.

- [x] **IN-8 — For a COMPLEX sound with an RPC or DSP flag, per-track metadata is read BEFORE the
  RPC/DSP block instead of AFTER it — reversed order vs. FACT.**
  Manually verified against `FAudio/src/FACT_internal.c:2580-2704` — the real order is:
  `trackCount` (only that) → RPC block (if `SOUND_FLAG_RPC_MASK`) → DSP block (if
  `SOUND_FLAG_HAS_DSP`) → only THEN the per-track `vol/code/filterData/frequency` loop +
  track-event array. CNA reads the per-track loop RIGHT AFTER `trackCount`, before checking the
  RPC/DSP flags — for a complex sound with an RPC/DSP flag, the RPC/DSP bytes end up being read as
  if they were track metadata and vice versa, and `track.code` (an absolute offset to seek to the
  track's event array) ends up garbage. This survived Phase 7's IN-1 fix (which addressed only
  the misinterpreted LENGTH of the DSP block, not this structural ordering).
  *FAudio ref:* FACT_internal.c:2580 (trackCount), :2621-2661 (RPC+DSP blocks), :2668-2696
  (per-track metadata + track-event array, ONLY NOW).
  *CNA:* XactParser.cpp:688-741 (the whole "Sound parsing" loop for `SOUND_FLAG_COMPLEX`).
  *Accept:* move the per-track `vol/code/filterData/frequency` loop (and track-event parsing) to
  AFTER the RPC-skip and DSP-skip blocks; a regression test with a COMPLEX sound
  (`SOUND_FLAG_COMPLEX|SOUND_FLAG_HAS_RPC`, and separately `|SOUND_FLAG_HAS_DSP`) followed by a
  second, distinguishable sound — verify the second sound parses correctly (analogous to the
  existing `BuildXsbWithDspThenSecondSound`/`BuildXsbWithRpcThenSecondSound` fixtures, but with
  the FIRST sound being COMPLEX instead of simple).
  *Note:* the per-track metadata loop (and track-event parsing) was moved to after the RPC-skip
  and DSP-skip blocks (`XactParser.cpp`'s Sound parsing loop). Added
  `BuildXsbWithComplexRpcThenSecondSound`/`BuildXsbWithComplexDspThenSecondSound` — track-event
  data must live OUTSIDE the contiguous stream of sound headers (referenced only by an absolute
  offset), so sound 1's header follows immediately after sound 0's per-track metadata, and the
  event array comes after sound 1 (discovered only while writing the test — the first attempt
  with the event array right after the metadata caused a "read past end", because
  `ParseFirstPlayWave` restores the main cursor after seek+read, so headers must be contiguous,
  not the event data). `git stash` on `XactParser.cpp` confirmed both new tests fail against the
  old logic.

- [x] **IN-9 — Streaming `WaveBank::GetSoundEffect` can attempt an unbounded allocation from a
  corruption/attack-controllable `dataLength`, with no try/catch.**
  `audioLen = entry.dataLength` (derived from the parsed `.xwb` header) is never checked against
  the real file size on the STREAMING path (unlike the non-streaming path, which has exactly
  this check). `streamedBytes.resize(audioLen)` also runs BEFORE the `try` block, so
  `std::length_error`/`std::bad_alloc` propagates uncaught all the way out of `Cue::Play()`.
  *CNA:* WaveBank.cpp:267-269 (resize+read before the try at line 291); caller Cue.cpp:244 (no
  try/catch anywhere up the chain from `Cue::Play`).
  *Accept:* before `resize`, check `entry.dataLength` against a sane bound (the real remaining
  file size via `seekg(0,end)`/`tellg()`), and/or move the resize+read inside the existing
  try/catch and return `nullptr` on failure; a regression test with a streaming fixture whose
  entry `dataLength` exceeds the real file size, verifying `GetSoundEffect` returns `nullptr`
  instead of throwing/crashing.
  *Note:* `WaveBank::GetSoundEffect`'s streaming branch now checks `dataOffset+dataLength`
  against the real on-disk file size (`seekg(0,end)`/`tellg()`) before `resize`, and the `resize`
  itself is additionally wrapped in try/catch. Added
  `WaveBankTest.StreamingGetSoundEffectRejectsEntryLengthExceedingRealFileSize` — for the chosen
  (mildly) oversized length (1 MB), both the old and new code path return `nullptr` (the old one
  via a post-read `gcount()` check), so `git stash` doesn't distinguish this particular test; the
  real benefit of the fix (preventing an allocation attempt at orders-of-magnitude larger — GB —
  values) isn't safely testable in a fast unit test — documented directly at the test. Suite
  green, clean under ASan+LeakSanitizer.

- [x] **IN-10 — Compact-format `.xwb` entries never derive ADPCM `samplesPerBlock`/`blockAlign` —
  only the non-compact path does.**
  The non-compact branch correctly computes `samplesPerBlock=(wBlockAlign+16)*2`/
  `blockAlign=(wBlockAlign+22)*channels` for `fmtTag==2` (ADPCM). The compact branch has no
  format-tag condition at all — `blockAlign` stays the raw `wBlockAlign`, `samplesPerBlock` stays
  at the default `0`, even when the entire compact bank encodes ADPCM (a valid, if less common,
  combination).
  *FAudio ref:* FACT_internal.c:1790-1793,1863-1866 (applies the formula regardless of the format
  source).
  *CNA:* XactParser.cpp:449-481 (compact — branch missing), :535-546 (non-compact — correct).
  *Accept:* apply the same `fmtTag==2` branch (or a shared helper function) in the compact loop
  too; a regression test with a compact fixture encoding ADPCM, verifying the same formula as the
  existing `NonCompactAdpcmEntryComputesBlockAlignAndSamplesPerBlock`.
  *Note:* the compact loop now, before the main `for`, computes `compactSamplesPerBlock`/
  `compactBlockAlign` with the same formula as the non-compact branch (shared for the whole bank,
  since the format is common to all entries in a compact bank). Added
  `XactParserTest.CompactAdpcmEntryComputesBlockAlignAndSamplesPerBlock`; `git stash` on
  `XactParser.cpp` confirmed a failure against the old (missing) logic.

- [x] **IN-11 — `AudioMixer::GetMixer()` leaks the `MIX_Init()` refcount when
  `MIX_CreateMixerDevice` fails.**
  `MIX_Init()`/`MIX_Quit()` are reference-counted. `GetMixer()` calls `MIX_Init()`, and if
  `MIX_CreateMixerDevice()` fails, it throws `std::runtime_error` WITHOUT a balancing
  `MIX_Quit()`. `g_mixer` stays `nullptr`, so every subsequent call (~10 sites across
  `SoundEffect.cpp`/`SoundEffectInstance.cpp`/`DynamicSoundEffectInstance.cpp`/
  `MediaPlayer.cpp`) calls `GetMixer()` again and keeps stacking up an unbalanced count for as
  long as the audio hardware is missing.
  *CNA:* src/CNA/Internal/Audio/AudioMixer.cpp:17-36.
  *Accept:* on `MIX_CreateMixerDevice` failure, call `MIX_Quit()` before throwing (or wrap the
  whole init sequence so every error path balances `MIX_Init`); a test isn't practically feasible
  without a real/mocked SDL audio subsystem — but the fix is a straightforward defensive change.
  *Note:* `MIX_Quit()` added to the error branch before the `throw`. No automated test (per the
  acceptance criteria itself — it would require a real/mocked SDL audio subsystem); covered only
  by manual review + `AudioMixerTests.cpp`'s "no tests" comment (IN-12).

- [x] **IN-12 — Test gaps in `XactParserTests.cpp` (22 tests) and a complete absence of tests
  targeting `AudioMixer`/`ParseXwbStreamingHeader`.**
  No test combines `SOUND_FLAG_COMPLEX` with RPC/DSP flags (which is why IN-8 slipped through);
  the existing RPC/DSP fixtures (`BuildXsbWithRpcThenSecondSound`/
  `BuildXsbWithDspThenSecondSound`) explicitly use SIMPLE sounds. `ParseXwbStreamingHeader` has no
  direct unit test at all (only indirectly through `WaveBankTests.cpp`'s small valid fixtures) —
  missing a truncated/malformed header, a zero-entry streaming bank, a streaming entry with
  `dataLength` exceeding the real file size (see IN-9). No test locks in the correct (not
  off-by-one) channel value against an independently verified value (see IN-7 — the existing
  fixtures would pass unchanged both before and after the fix). No test for a compact-format
  ADPCM entry (IN-10). `AudioMixer` has no test file at all (reasonable given its dependency on a
  real/mocked SDL audio device, but with no comment per CHECKLIST.md's convention for untestable
  classes).
  *CNA:* tests/CNA/Internal/Audio/XactParserTests.cpp (whole file); no `AudioMixerTests.cpp`.
  *Accept:* add the fixtures described in IN-7/IN-8/IN-9/IN-10's acceptance criteria; for
  `AudioMixer` add at least a one-line comment (per CHECKLIST.md's convention) explaining why
  it's untested.
  *Note:* the fixtures for IN-7 (compact+non-compact stereo), IN-8 (COMPLEX+RPC, COMPLEX+DSP),
  IN-9 (streaming oversized-length), IN-10 (compact ADPCM) were all added at their own items
  above. `tests/CNA/Internal/Audio/AudioMixerTests.cpp` added as an empty "no tests" stub (the
  same convention as `GameComponentTests.cpp` etc.). `XactParserTests.cpp` now has 27 tests (was
  22).

#### 8.4 Mic/data/enums/exceptions

- [x] **MC-6 — `Microphone::CheckBuffer()` is public even though it doesn't need to be —
  unnecessarily widens the API surface and contradicts T-1H's own acceptance criterion.**
  FNA has `CheckBuffer()` as `internal` — not callable outside the assembly. CNA has it `public`
  (tagged `NOXNA`), directly against CLAUDE.md's Visibility Mapping ("C# `internal` ... shouldn't
  become a public C++ API method") and against T-1H's own acceptance criterion ("no public
  internal members"). `CheckAllBuffers()` (the new, sanctioned NOXNA bridge for
  `FrameworkDispatcher`) is a `static` method of the same class, so it already has private-member
  access to `CheckBuffer()` without `CheckBuffer()` itself needing to be public.
  *FNA:* Microphone.cs:204-213 (`internal void CheckBuffer()`).
  *CNA:* Microphone.hpp:133-134; Microphone.cpp:210-230.
  *Accept:* move `CheckBuffer()` to `private` (keep `CheckAllBuffers()` public per T-1H); extend
  `MicrophoneTestAccess` with a thin static wrapper so `MicrophoneTests.cpp`'s direct
  `mic.CheckBuffer()` calls still work.
  *Note:* done exactly per the acceptance criteria. `MicrophoneTestAccess::CheckBuffer(mic)`
  added, both direct test call sites rewritten. A purely visibility change — no behavior change,
  hence no `git stash` step (the successful build itself is proof of correct encapsulation).

- [x] **MC-7 — Test gap: no deterministic test that `BufferReady` stays silent when the queued
  duration is below `BufferDuration`.**
  Existing tests only cover "no subscriber → doesn't throw" (short-circuits on an `Empty()`
  check, never exercises the `>` comparison itself) and "real capture, enough time → eventually
  fires" (only the positive path, needs the SDL dummy driver and up to 2s of polling). A
  deterministic, instance-isolated test for the negative case is missing.
  *CNA:* MicrophoneTests.cpp:267-276,357-374; Microphone.cpp:210-216.
  *Accept:* a new test with an isolated (never `Start()`ed) `Microphone` via
  `MicrophoneTestAccess`, register a counting lambda on `BufferReady`, call `CheckBuffer()`
  directly, verify the counter stays `0`; bonus: verify the `sender` argument passed to the
  handler is the mic instance itself.
  *Note:* added `CheckBufferDoesNotRaiseWhenQueuedDurationIsBelowBufferDuration` — an isolated
  (never `Start()`ed) instance has `GetQueuedBytes()==0` (`captureStream_` is null), so the `>`
  comparison genuinely runs and must come out false. The bonus (sender identity) couldn't be
  meaningfully verified in the same test, since the event never fires (no positive call to
  compare against) — the test instead verifies `sender` stays `nullptr` (the lambda was never
  called at all). A purely test-only addition, no production code change.

---

## 5. Recommended order and milestones

1. **M0 (kickoff):** T-0A.
2. **M1 (compliance, quick wins):** T-1A…T-1H — exception types, `GetTypeName`, SPDX, visibility.
   *Low risk, purely mechanical, unlocks meaningful tests.*
3. **M2 (real bugs):** T-2A…T-2G — override/Dispose in Dynamic, the float guard, XACT parser
   bugs, AudioCategory.
   *Highest value — fixes genuinely broken behavior.*
4. **M3 (API fidelity):** T-3A…T-3G — throw-on-invalid-name, IsInUse, Pan/Volume, RendererDetail,
   SoundEffectI, semantic decisions.
5. **M4 (features):** T-4A…T-4D — mic capture (**T-4A done**, with the MC-1 caveat), 3D
   pan/attenuation, DSP routing, AudioEngine Update.
6. **M5 (tests):** T-5A…T-5O — complete coverage (already ongoing since M1, final consolidation
   here).
7. **M6 (closure):** T-6A…T-6C.
8. **M7 (supplementary audit, 2026-07-02):** CP-1…CP-14, XA-1…XA-5, IN-1…IN-6, MC-1…MC-5
   (Phase 7).
   *Recommended order within M7:* first the safety/functionally critical bugs with a real impact
   on a running game — IN-1 (silent parsing corruption), CP-1/CP-3 (playback bugs), XA-1/XA-2
   (long sounds cut off / leak), CP-7 (dangling pointer) — then the rest of the CP/XA/IN cluster,
   then MC (mic is a new, less-used feature), test gaps last (but not deferred indefinitely).

> Recommendation: don't leave tests until M5 — add the matching Phase-5 tests for every M1–M4 task
> right away (the "make and forget" principle from `CLAUDE.md` — a file is done in one pass,
> tests included). The same applies to M7: every CP/XA/IN/MC task already has an acceptance
> criterion with a test built into its description.

---

## 6. Risk summary / open decisions

| ID | Question to decide | Default recommendation |
|----|---------------------|--------------------|
| D1 | ~~`CreateInstance`/`FromStream`: value vs. heap-reference + instance-tracking (T-3G)~~ | **Decided and implemented 2026-07-04** — instance-tracking + Dispose cascade; `SoundEffect` is now move-only (single owner of the resource), `SoundEffectInstance` registers/unregisters/re-points on a move. `CreateInstance`/`FromStream` remain value-based (no heap-ref semantics), but `SoundEffect` can no longer be copied. |
| D2 | Pan/Volume clamp vs. throw/pass-through (T-3C) | Align with FNA (throw on range, pass-through volume); clamp only consciously + record in CHECKLIST |
| D3 | ~~Streaming WaveBank (T-3F)~~ | **Decided and implemented 2026-07-04** — real streaming: `ParseXwbStreamingHeader` reads only header/metadata from disk, `WaveBank::GetSoundEffect` reads entry data lazily straight from the file. Non-streaming ctor unchanged (whole file eager, like FNA). |
| D4 | ~~Scope of `AudioEngine::Update` / FACT DoWork (T-4D)~~ | **Decided and implemented 2026-07-04** — minimal scope: `SetCategoryVolumeInternal` now calls `Cue::ApplyCategoryVolume` to re-apply to active instances; category fades and instance limits (the rest of FACT `DoWork`) remain documented as out of scope. |
| D5 | ~~Ownership of `SoundEffect` vs. `SoundEffectInstance` — a dangling-safe contract vs. shared ownership (CP-7)~~ | **Decided and implemented 2026-07-03** — shared ownership: `SoundEffectInstance` holds a type-erased `shared_ptr<void>` to `SoundEffect::impl_` plus a cached native handle, no dereferencing a raw `SoundEffect*` after construction. Verified with a real ASan build. |
| D6 | ~~Fire-and-forget cue cleanup: time vs. playback state (XA-1)~~ | **Decided and implemented 2026-07-02** — sweep by `!IsPlaying`, a time-based safety net (5 min) only as a last resort. |
| D7 | ~~Parser behavior on corrupted/adversarial XACT data — throw vs. saturating clamp (IN-2, IN-3)~~ | **Decided and implemented 2026-07-03** — throw (`std::runtime_error`) on underflowed/corrupted values instead of silently clamping; aligns with the project's "no silent data corruption" rule. |
| D8 | ~~`Microphone::GetData` buffer behavior on error/no-op — zero it vs. leave it untouched (MC-3)~~ | **Decided and implemented 2026-07-03** — aligned with FNA: `GetData` returns 0 and leaves the buffer completely untouched, no zeroing. |

---

## 7. Addendum: 2026-07-02 audit

Phase 7 (§4) came out of 4 parallel line-by-line audits against FNA after finishing T-4A —
covering core playback, XACT, the internal backend, and mic/data/enums/exceptions. Besides the
CP/XA/IN/MC tasks themselves, the audit also found stale accompanying documentation, fixed
directly (outside this file):

- **`AUDIT.md`** (the `Microsoft::Xna::Framework::Audio` table) — the rows for `Microphone`,
  `AudioEngine`, `Cue`, `SoundBank`, `WaveBank` said "(stub behavior)"; fixed to reflect the real
  state (see `AUDIT.md`'s git history).
- **`CHECKLIST.md`** ("Known acceptable C++ deviations") — added 5 audio-specific rows matching
  §2 here (T-6A thereby satisfied).
- **`NEXT.md`** — removed a stale line in §5 claiming Microphone capture is a stub (which
  contradicted the rest of the same file after T-4A).

T-6B (updating `AUDIT.md`) is hereby satisfied. T-6A (the deviation table) is hereby satisfied.
T-6C (build & report) remains for the next session, once at least the first wave of Phase 7 bugs
is resolved.

---

*Generated based on three parallel line-by-line audits against FNA (clusters: core playback,
XACT, 3D/mic/enums/exceptions). Covers only `Microsoft::Xna::Framework::Audio` +
`CNA::Internal::Audio`. Phase 7 (2026-07-02) added 4 more parallel audits on top of the
already-fixed code — see the addendum above.*

---

# Phase 9 — Audio correctness hardening and XNA/FNA fidelity

Scope: audit and harden the existing audio implementation so it becomes closer to real XNA 4.0 /
FNA behavior, without turning the module into a huge unrelated audio engine. Focused on the XNA 4.0
Audio namespace and XACT-compatible behavior needed by XNA games. Do not trust the checkboxes below
once checked without re-reading the actual code first (this exact phase exists because Phase 7/8's
"all checked" state still had real gaps).

Implementation order: (1) P9-LIFECYCLE-001..012, (2) P9-CATEGORY-001..004, (3) P9-VALIDATION-001..013,
(4) P9-DOCS-001..007, (5) P9-BUILD-001..007, then (6) P9-STOP, P9-XACT, P9-3D, P9-HARDWARE, P9-DYNAMIC,
and P9-LIFECYCLE-013..015 / P9-CATEGORY-005..010 (deferred sub-items of already-started groups).

## P9-AUDIT — Fresh implementation audit

* [ ] P9-AUDIT-001 Re-read all public audio headers under `include/Microsoft/Xna/Framework/Audio` and compare the exposed API against XNA 4.0 / FNA Audio.
* [ ] P9-AUDIT-002 Re-read all implementations under `src/Microsoft/Xna/Framework/Audio` and identify behavior that is stubbed, approximate, or inconsistent with XNA/FNA.
* [ ] P9-AUDIT-003 Re-read internal audio backend files under `include/CNA/Internal/Audio` and `src/CNA/Internal/Audio` and document backend assumptions and limitations.
* [ ] P9-AUDIT-004 Re-read all audio tests and identify which known deviations are locked in by tests.
* [ ] P9-AUDIT-005 Update `plan_audio.md` with a concise "current known deviations" subsection based on actual code, not stale documentation.

## P9-LIFECYCLE — Cue and playback lifecycle correctness

* [x] P9-LIFECYCLE-001 Fix `Cue` state reconciliation so `Cue::IsPlaying`, `Cue::IsPaused`, and `Cue::IsStopped` reflect the real state of active `SoundEffectInstance` objects after natural playback completion.
  *Note:* Added `Cue::ReconcileState() const` (`Cue.cpp`) — called at the start of `getIsPlayingProperty`/`getIsPausedProperty`/`getIsStoppedProperty`/`getIsStoppingProperty`. If `state_==Playing` and `active_` is non-empty, it checks `pi.instance->getStateProperty()` (a live query into SDL3_mixer) for every instance; if all are `Stopped`, it flips `state_` to `Stopped`. A fixture with an empty `active_` (no wavebank reference, e.g. "Explosion") is unaffected — it stays Playing forever, as intended for this degenerate test case. `Pause()` now also calls `ReconcileState()` at the start, so a naturally-finished cue can't be accidentally "resurrected" into Paused (test `CueTests.cpp::PauseAfterNaturalCompletionIsANoOp`). Verified via `git stash` on `Cue.hpp/.cpp` + `SoundBank.hpp/.cpp` + `AudioEngine.cpp`: 8 new tests failed against the old code, see P9-LIFECYCLE-005/010/011 below.
* [x] P9-LIFECYCLE-002 Add cleanup logic so completed cue instances are removed from `Cue::active_` without waiting for the fire-and-forget safety timeout.
  *Note:* Part of `ReconcileState()` above — `active_.clear()` happens immediately once every instance is found to have finished, without waiting for the 5-minute safety-net timer.
* [x] P9-LIFECYCLE-003 Ensure `SoundBank::IsInUse` becomes false soon after all fire-and-forget and explicit cues naturally finish.
  *Note:* Follows directly from P9-LIFECYCLE-001 — `SoundBank::getIsInUseProperty()` already calls `faf.cue->getIsPlayingProperty()/getIsPausedProperty()`, now live-reconciled. No change to `SoundBank::getIsInUseProperty()` itself was needed.
* [x] P9-LIFECYCLE-004 Ensure `WaveBank::IsInUse` becomes false soon after all cues using that wave bank naturally finish.
  *Note:* Same as above — `WaveBank::getIsInUseProperty()` already calls `cue->getIsPlayingProperty()/getIsPausedProperty()`.
* [x] P9-LIFECYCLE-005 Add tests for a short one-shot cue naturally transitioning from Playing to Stopped.
  *Note:* `CueTests.cpp::PlayingCueNaturallyTransitionsToStoppedAfterPlaybackFinishes` — a real (200 B, ~1.13 ms) WaveBank instance via `SharedApply3DBank()`, a 50 ms sleep, verifies `IsStopped==true`, `IsPlaying==false`, `active_[0]==nullptr`. `git stash` confirmed a failure against the old code.
* [x] P9-LIFECYCLE-006 Add tests for `SoundBank::IsInUse` after natural cue completion.
  *Note:* `SoundBankTests.cpp::IsInUseFalseSoonAfterFireAndForgetCueNaturallyFinishes`. `git stash` confirmed a failure against the old code.
* [x] P9-LIFECYCLE-007 Add tests for `WaveBank::IsInUse` after natural cue completion.
  *Note:* `WaveBankTests.cpp::IsInUseFalseSoonAfterCueNaturallyFinishesWithoutExplicitStop`. `git stash` confirmed a failure against the old code.
* [x] P9-LIFECYCLE-008 Ensure `AudioEngine::Update()` performs necessary lifecycle cleanup/reconciliation for active cues.
  *Note:* `AudioEngine::Update()` now iterates `xactImpl_->soundBanks` and calls `sb->SweepFireAndForget()` on each — mirroring FNA's `AudioEngine.Update()` → `FACTAudioEngine_DoWork`, which in `FACT_internal.c` around line 1732 actually destroys a `managed` cue once it reaches `FACT_STATE_STOPPED`. The `Cue`-level Playing/Paused/Stopped reconciliation itself is already live per-call (P9-LIFECYCLE-001), so it doesn't depend on `Update()`.
* [x] P9-LIFECYCLE-009 Ensure fire-and-forget cues are swept promptly after completion and are not kept alive for minutes after playback ends.
  *Note:* `SoundBank::PlayCueInternal`'s sweep lambda was moved into a new `SoundBank::SweepFireAndForget()`, called both from `PlayCueInternal` (as before) and now also from `AudioEngine::Update()` (P9-LIFECYCLE-008). `AudioEngineTests.cpp::UpdateSweepsFinishedFireAndForgetCueWithoutNeedingAnotherPlayCue` verifies that a single `Update()` call after a sound naturally finishes removes the entry without needing a second `PlayCue()`. `git stash` confirmed a failure against the old code.
* [x] P9-LIFECYCLE-010 Audit whether `Cue::Play()` may be called repeatedly on the same cue while already Playing or Paused. Match FNA/XNA behavior and add tests.
  *Note:* Auditing `FACT.c::FACTCue_Play` (line ~2327) confirmed: FACT silently rejects (`FACTENGINE_E_INVALIDUSAGE`) a cue whose state already has `PLAYING`/`STOPPING`/`STOPPED` set — FNA's `Cue.Play()` discards the return value, so it's a no-op from the C# caller's perspective. `PAUSED` is included too, since real FACT leaves the `PLAYING` bit set even while paused (`FACTCue_Pause`, line ~2622, never clears `PLAYING`). `Cue::Play()` now, after `ReconcileState()`, rejects the call when `state_` is `Playing`/`Paused`/`Stopping`/`Stopped`. Tests: `CueTests.cpp::PlayCalledTwiceWhileAlreadyPlayingIsANoOpAndDoesNotDuplicateInstances`, `PlayWhilePausedIsANoOp`, `PlayAfterStopIsANoOp`. `git stash` confirmed a failure (duplicate instances) against the old code.
* [x] P9-LIFECYCLE-011 Prevent duplicate cue registration in `AudioEngine::RegisterCue`.
  *Note:* Resolved primarily by P9-LIFECYCLE-010 (Play() no longer allows a second `RegisterCue()` call). `AudioEngine::RegisterCue` also has an explicit `std::find` guard as defense-in-depth for the registry itself.
* [x] P9-LIFECYCLE-012 Add regression tests proving repeated category operations do not duplicate active cue entries.
  *Note:* Added `AudioEngine::ActiveCueCountForTest()` (NOXNA, test-only) + `AudioEngineTestAccess.hpp`, since `XactEngineImpl` is only defined in `AudioEngine.cpp` and can't be reached by a friend test struct the way `SoundBank::fireAndForget_` can. Test `AudioEngineTests.cpp::RepeatedCategoryOperationsDoNotDuplicateActiveCueRegistryEntries` calls `AudioCategory::Pause/Resume/SetVolume` repeatedly and verifies `ActiveCueCount==1`. Honesty note: `AudioCategory` operations never call `RegisterCue` (only methods on already-registered cues), so this test would also pass against the older code — it's a regression guard for the future, not a reproduction of a real bug (unlike P9-LIFECYCLE-010/011, where `git stash` genuinely showed a failure).
* [x] P9-LIFECYCLE-013 Audit `Cue::Pause()`, `Cue::Resume()`, and `Cue::Stop()` behavior when called in Stopped, Playing, Paused, and Disposed states.
  *Note:* Read `FACTCue_Pause`/`FACTCue_Stop` (`FACT.c`) line-by-line against CNA's `Pause()`/`Resume()`/`StopInternal()`. Findings, per state: **Disposed** — CNA's `if (isDisposed_) return;` guard on all three matches FACT's own `if (pCue == NULL) return <error>;` (FNA's disposed `Cue.handle` is `IntPtr.Zero`), i.e. a silent no-op in both — no bug, locked in by 3 new tests (see `P9-LIFECYCLE-014`). **Stopped** — calling `Stop()` again is an idempotent no-op in both (FACT short-circuits on the `STOPPED` bit; CNA re-runs the same steps against already-empty state with no observable effect) — a micro-inefficiency in CNA, not a bug, not worth changing. **Playing/Paused** — `Pause()`/`Resume()`'s guard conditions (`state_ != State::Playing` / `!= State::Paused`) produce the same net effect as FACT's bit-flag checks for every case exercised by existing tests. One real, deliberately-deferred deviation found: real FACT never clears `FACT_STATE_PLAYING` when pausing, so `IsPlaying`+`IsPaused` can both be `true` in XNA/FNA; CNA's mutually-exclusive `State` enum can't represent that. Fixing it would ripple into `AudioEngine::PauseCategoryInternal`/`ResumeCategoryInternal` and existing tests that assume disjoint `IsPlaying`/`IsPaused` — flagged, not fixed, since it weighs the same as the Fáze 8 `CP-19`/`CP-18`/`XA-9` "touches shared infrastructure" decisions (ask the user before implementing).
* [x] P9-LIFECYCLE-014 Align disposed-state behavior for `Cue` methods with XNA/FNA and add tests.
  *Note:* `Pause()`/`Resume()`/`Stop()` needed no change (already correct, see `P9-LIFECYCLE-013`'s note) — added `PauseAfterDisposeIsANoOp`/`ResumeAfterDisposeIsANoOp`/`StopAfterDisposeIsANoOp` to `CueTests.cpp` to lock the existing correct behavior in. `GetVariable()`/`SetVariable()` needed a real fix — see `P9-LIFECYCLE-015`.
* [x] P9-LIFECYCLE-015 Ensure `Cue::GetVariable()` and `Cue::SetVariable()` handle disposed cues consistently with XNA/FNA.
  *Note:* Found a real inconsistency: `GetVariable()`/`SetVariable()` had no disposed guard at all, unlike `Play()`/`Apply3D()` in the same class — a disposed cue's `bank_`/`engine_` raw pointers happening to still be valid made this "work" by accident, not by contract. Checked real FNA: `FACTCue_GetVariableIndex` (`FACT.c`) dereferences `pCue->parentBank` **before** its `pCue == NULL` check, so calling this on a disposed FNA `Cue` (handle `IntPtr.Zero`) would crash natively — an unintentional FAudio bug, not a documented contract worth reproducing. Fixed: both methods now `throw System::ObjectDisposedException("Cue")` as their first check, matching this class's own `Play()`/`Apply3D()` precedent instead of the native crash. Added `GetVariableAfterDisposeThrowsObjectDisposed`/`SetVariableAfterDisposeThrowsObjectDisposed` to `CueTests.cpp`. Verified via `git stash`: both new tests fail ("throws nothing") against the pre-fix code; the three `P9-LIFECYCLE-014` no-op tests pass either way (confirming they're a regression lock, not a bug reproduction). Full suite 2078/2078 green after restoring the fix; `cna_demo_sound`/`cna_demo_2d` rebuilt clean. No dedicated ASan run — this is a pure early-throw addition, no new allocation/ownership pattern, and the `ObjectDisposedException` throw path itself is already exercised ASan-clean via `Play()`/`Apply3D()` in prior sessions.

## P9-STOP — Stop semantics and authored stop behavior

* [x] P9-STOP-001 Audit current `Cue::Stop(AudioStopOptions::Immediate)` behavior against XNA/FNA.
  *Note:* Already correct, no fix needed: `StopInternal(true)` clears `active_`/hard-stops every instance, sets `state_ = State::Stopped`, and unregisters from `waveBanksUsed_`/`AudioEngine` all synchronously — matches `FACTCue_Stop`'s `dwFlags & FACT_FLAG_STOP_IMMEDIATE` branch (`FACT.c`) exactly, which always reaches `FACT_STATE_STOPPED` synchronously regardless of any authored fade.
* [x] P9-STOP-002 Audit current `Cue::Stop(AudioStopOptions::AsAuthored)` behavior against XNA/FNA.
  *Note:* **Found a real bug.** The old code called `pi.instance->Stop(false)` (correctly lets the tail play, matches `XA-6`) but then set `state_ = State::Stopped` and unregistered from `waveBanksUsed_`/`AudioEngine` **unconditionally**, regardless of whether real active instances were still audibly playing. Read `FACTCue_Stop` (`FACT.c`) line-by-line: real FACT does NOT reach `FACT_STATE_STOPPED` for a non-immediate stop unless there's nothing to release (`playingSound == NULL`, paused, or immediate) — with a real tail it begins a fade-out/RPC-release and only reaches `STOPPED` once that finishes. Fixed — see `P9-STOP-003`/`004`.
* [x] P9-STOP-003 Do not mark a cue as fully inactive while authored stop tails/fades are still active.
  *Note:* Fixed in `StopInternal()`: a non-immediate stop with non-empty `active_` (`hasRealTail`) now sets `state_ = State::Stopping` instead of `Stopped`, leaving `active_` (and thus `getIsPlayingProperty()`/`getIsStoppedProperty()`) correctly reflecting "still has an audible tail". `ReconcileState()` (already used for natural-completion detection, `P9-LIFECYCLE-001`) now also promotes `Stopping` → `Stopped` once every `active_` instance actually finishes, the same way it does for `Playing`.
* [x] P9-STOP-004 Introduce explicit internal tracking for stopping/tail state if needed.
  *Note:* Used the already-defined but previously-unused `Cue::State::Stopping` enum value (it existed in the enum since an earlier phase but nothing ever set it — `IsStoppingIsAlwaysFalse` was a real, accurately-named test until this fix). No new field needed.
* [x] P9-STOP-005 Ensure wave bank and sound bank in-use tracking remains true while authored stop tails are still active.
  *Note:* Fixed as a direct consequence of `P9-STOP-003`: `StopInternal()` no longer unregisters from `waveBanksUsed_`/`AudioEngine` when `hasRealTail` is true, so `WaveBank::IsInUse`/`SoundBank::IsInUse`/`AudioEngine`'s category operations all still see the cue as alive (via its now-correctly-`Stopping`, not falsely-`Stopped`, live-queried state) for as long as the tail is actually playing.
* [x] P9-STOP-006 Add tests for `Stop(Immediate)` state transitions.
  *Note:* Already covered by pre-existing tests (`IsStoppedTrueAfterStop`, `StopImmediateTransitionsToStopped`, the `StopImmediate` half of `StopAsAuthoredLeavesTrackPlayingButStopImmediateHardStopsRightAway`); extended the latter with explicit `IsStopped`/`IsStopping` assertions post-fix.
* [x] P9-STOP-007 Add tests for `Stop(AsAuthored)` state transitions.
  *Note:* Extended `CueTests.cpp::StopAsAuthoredLeavesTrackPlayingButStopImmediateHardStopsRightAway` with `IsStopping`/`IsStopped` assertions; added `StopAsAuthoredTransitionsFromStoppingToStoppedOnceTailFinishes` (the short `Apply3DCue` fixture, ~1.13ms, verifies the natural `Stopping`→`Stopped` reconciliation once the tail actually finishes).
* [x] P9-STOP-008 Add tests for category stop with authored stop behavior.
  *Note:* `AudioCategoryTests.cpp::StopAsAuthoredOnCategoryLeavesRealActiveCueStoppingNotStopped` — a real WaveBank-backed cue, `AudioCategory::Stop(AsAuthored)`, verifies the cue is `Stopping` (not `Stopped`) and its instance/track are still alive immediately after. First version used `SharedVolBank`'s 200-byte (~1.13ms) "VolCue" fixture and was itself flaky (~30-40% failure rate over repeated full-suite runs): under full-suite load the tail could finish naturally between `Play()` and the assertion, letting `ReconcileState()` promote `Stopping` straight to `Stopped` before the test observed it. Fixed by adding a dedicated 1-second `SharedP9StopLongBank`/`"P9StopCategoryLongCue"` fixture (mirroring `CueTests.cpp`'s `"LongCue"` pattern) — stress-tested 10+ full-suite runs clean afterward.
* [x] P9-STOP-009 Verify that stopping a cue unregisters from `AudioEngine` only when it is actually finished.
  *Note:* `AudioEngineTests.cpp::StopAsAuthoredDoesNotUnregisterFromAudioEngineWhileTailStillPlaying` — a fresh `AudioEngine` + a 1-second-long real fixture (`BuildP9StopLongXwbFixtureBytes`/`BuildP9StopLongXsbFixtureBytes`, mirroring `CueTests.cpp`'s `"LongCue"` pattern so the tail is reliably still audible at assertion time), asserts `ActiveCueCount()==1` immediately after `Stop(AsAuthored)`, then `==0` only after a subsequent `Stop(Immediate)`.
  All 4 new/extended `P9-STOP-006..009` tests `git stash`-verified to fail against the pre-fix code (the pre-existing `StopAsAuthoredTransitionsToStopped` test, which uses the wavebank-less "Explosion"/`MakeCue()` fixture where `active_` stays empty, correctly passes either way — matches FACT's own `playingSound == NULL` immediate-stop rule). Full suite (2093/2093) green, verified clean under a full ASan+UBSan build.
* [x] P9-STOP-010 Document any remaining deviation from exact XACT authored stop behavior.
  *Note:* Documented in `CHECKLIST.md`: `Stop(AsAuthored)`'s release-tail *duration* is however long the underlying wave naturally takes to finish, not the authored `fadeOutMS`/RPC-release timing — `XactParser` doesn't retain per-cue `fadeOutMS`/`instanceLimit`/`maxInstanceBehavior` into `XsbCue` at all (parsed-and-discarded, only needed to locate later header fields). `State::Stopping` gets the *shape* of the behavior right (non-stopped while there's something to hear), but not an authored fade curve — implementing real per-cue fade timing would need parser changes plus a new time-driven update mechanism, out of scope for this pass (matches how `P9-CATEGORY-005/007/008`'s category-level `instanceLimit`/fade were already deferred, `XA-11`).

## P9-CATEGORY — AudioCategory correctness

* [x] P9-CATEGORY-001 Fix category operations so they iterate over a snapshot of active cues instead of mutating `activeCues` during iteration.
  *Note:* Found a real, confirmed bug: `StopCategoryInternal` iterated `AudioEngine::activeCues` directly while `Cue::Stop()` cascades into `StopInternal()` → `AudioEngine::UnregisterCue()`, which erases from that exact same vector — a classic mutate-during-range-for bug. Hand-traced `std::remove`'s element-shift pattern for 3 cues in one category: the range-for's cached `end()` iterator goes stale after the first erase, and the cue whose slot ends up backfilled from beyond that stale `end()` gets silently skipped (never has `Stop()` called on it) while a later cue's slot gets visited twice. Confirmed empirically, not just by hand-trace: with 2 cues the bug happened not to manifest (the single leftover stale slot happened to still hold the right pointer by accident of `std::remove`'s shift — a false negative from an under-sized test), but with 3 cues it reliably skips one. Fixed: `StopCategoryInternal` now copies `xactImpl_->activeCues` into a local `std::vector<Cue*>` before iterating. `PauseCategoryInternal`/`ResumeCategoryInternal`/`SetCategoryVolumeInternal` got the same snapshot treatment for defensive consistency, even though `Cue::Pause()`/`Resume()`/`ApplyCategoryVolume()` don't currently mutate `activeCues` (none of the three cascades into `UnregisterCue()`).
* [x] P9-CATEGORY-002 Add regression tests for stopping multiple active cues in the same category.
  *Note:* `AudioCategoryTests.cpp::StopStopsAllActiveCuesInCategoryNotJustSomeOfThem` — 3 cues (needs 3, not 2, to reliably reproduce, see `P9-CATEGORY-001`'s note), all played in the "Default" category, `AudioCategory::Stop()` called once, asserts all 3 report `IsStopped()`. Verified via `git stash`: fails (`cueB` still `Playing`) against the pre-fix code, passes after restoring the fix.
* [x] P9-CATEGORY-003 Add regression tests for pausing and resuming multiple active cues in the same category.
  *Note:* `AudioCategoryTests.cpp::PauseAndResumeAffectAllActiveCuesInCategory`. Honesty note: `Pause()`/`Resume()` never cascade into `UnregisterCue()`, so this test would also pass against the pre-`P9-CATEGORY-001` code — it's a completeness/regression test for the multi-cue case, not a bug reproduction (unlike `P9-CATEGORY-002`, where `git stash` genuinely showed a failure).
* [x] P9-CATEGORY-004 Add regression tests for changing category volume while cues are active.
  *Note:* `AudioCategoryTests.cpp::SetVolumeAppliesToAllActivePlayingCueInstancesInCategory` — two real WaveBank-backed cue instances (`SharedVolBank`), verifies `AudioCategory::SetVolume` lowers both instances' `MIX_Track` gain, not just whichever cue happens to be first in the registry. Same honesty note as `P9-CATEGORY-003`: `ApplyCategoryVolume()` doesn't mutate `activeCues` either, so this is also a completeness test, not a bug reproduction. Full suite (2081/2081) green after this group; also verified clean under a full ASan+UBSan build. `cna_demo_sound`/`cna_demo_2d` rebuilt clean.
* [ ] P9-CATEGORY-005 Implement XACT category `instanceLimit` if enough parsed data is already available.
* [ ] P9-CATEGORY-006 Add tests for category instance limits using synthetic/minimal XACT data or direct internal fixtures.
* [ ] P9-CATEGORY-007 Implement category fade-in behavior where feasible.
* [ ] P9-CATEGORY-008 Implement category fade-out behavior where feasible.
* [ ] P9-CATEGORY-009 Add tests for category fade-in/fade-out behavior.
* [ ] P9-CATEGORY-010 Clearly document any category behavior that remains approximate.

## P9-VALIDATION — Constructor and argument validation

* [x] P9-VALIDATION-001 Audit all `SoundEffect` constructors against XNA/FNA argument validation.
  *Note:* Read FNA's `SoundEffect.cs` line-by-line: the internal ctor and both public buffer-based ctors do **not** validate offset/count/loopStart/loopLength/channels/sampleRate at all -- C#'s array bounds checking is the real safety net there. C++ has none, so "match FNA" isn't sufficient by itself for the offset/count case specifically -- see `P9-VALIDATION-003`. Findings itemized in 002-005 below.
* [x] P9-VALIDATION-002 Fix `SoundEffect` buffer/range constructor validation for negative loop start and loop length.
  *Note:* Audited, no fix needed: FNA's own internal ctor does `this.loopStart = (uint) loopStart;` -- a negative value wraps to a huge unsigned value, identically to CNA's existing `static_cast<uintcs>(loopStart)`. CNA already matches FNA's (also-unvalidated) behavior exactly.
* [x] P9-VALIDATION-003 Fix `SoundEffect` buffer/range constructor validation for offset/count overflow.
  *Note:* **Real, serious bug, confirmed by a segfault.** The old check computed `offset + count > static_cast<intcs>(buffer.size())` as a plain `int32` addition -- two individually-plausible large values (e.g. `offset=2000000000, count=2000000000`) overflow int32 (UB), and on the observed two's-complement wraparound the sum comes out negative, silently passing the check while `buffer.data() + offset` is then a wildly out-of-bounds pointer handed to `MIX_LoadRawAudio`. Reproduced with a real `SIGSEGV` via `git stash` (not just a failing assertion -- see `P9-VALIDATION-006`'s test). Fixed: validate `offset`/`count` individually non-negative first, then check `off > buffer.size() || cnt > buffer.size() - off` using unsigned `size_t` arithmetic that can never overflow, without ever computing the raw sum.
* [x] P9-VALIDATION-004 Fix `SoundEffect` buffer/range constructor validation for invalid channel count.
  *Note:* Audited, no fix needed: FNA doesn't validate `AudioChannels` either. CNA passes `channels`/`sampleRate` straight into an `SDL_AudioSpec` for `MIX_LoadRawAudio`, which is a defensive C library that fails safely (returns `nullptr`) on an invalid spec -- already handled by the existing `if (!raw) throw NotSupportedException(...)` path. No raw memory access depends on `channels` being valid before that call, unlike `offset`/`count`.
* [x] P9-VALIDATION-005 Fix `SoundEffect` buffer/range constructor validation for invalid sample rate.
  *Note:* Audited, no fix needed: same reasoning as `P9-VALIDATION-004`. Also checked `getDurationProperty()`/`GetSampleDuration()`, the two places `sampleRate` is later divided by -- both already guard `sampleRate > 0` before dividing, so a `sampleRate<=0` instance can't cause a division-by-zero downstream either.
* [x] P9-VALIDATION-006 Add tests for invalid `SoundEffect` buffer constructor arguments.
  *Note:* `SoundEffectTests.cpp::BufferRangeConstructorRejectsOffsetCountIntegerOverflow` -- the real regression test for `P9-VALIDATION-003`. `git stash`-verified: **segfaults** against the pre-fix code (not just a failing assertion), confirming this is a genuine memory-safety bug, not a cosmetic one. `BufferRangeConstructorThrowsOnBadRange` (pre-existing) already covered the simple negative-offset/non-overflowing-bad-range cases.
* [x] P9-VALIDATION-007 Audit `DynamicSoundEffectInstance` constructor validation for sample rate and `AudioChannels`.
  *Note:* Audited, no fix needed: FNA's ctor doesn't validate these either. CNA's ctor just stores the values; `EnsureStream()` passes them into an `SDL_AudioSpec` for `SDL_CreateAudioStream`, which fails safely (returns `nullptr`) on invalid values, already handled by existing null-guards in `Play()`. No unsafe computation (division, raw pointer arithmetic) depends on these values being valid.
* [x] P9-VALIDATION-008 Fix `DynamicSoundEffectInstance` constructor validation to match XNA/FNA.
  *Note:* No fix needed -- see `P9-VALIDATION-007`. CNA already matches FNA's "no constructor validation, safety net is downstream" pattern, and CNA's downstream (SDL) is itself safe unlike raw buffer arithmetic.
* [x] P9-VALIDATION-009 Add tests for invalid `DynamicSoundEffectInstance` constructor arguments.
  *Note:* No new test added -- the audit (007/008) found no bug and no crash risk to lock in; existing `ConstructionDefaultState` etc. already cover the valid-argument path.
* [x] P9-VALIDATION-010 Audit `SubmitBuffer`, `SubmitFloatBufferEXT`, `Play`, `Pause`, `Resume`, and `Stop` after `Dispose`.
  *Note:* `Play()` already threw `ObjectDisposedException` (pre-existing). `Stop(bool)` already matched FNA's `handle==0` early-return exactly (verified by reading `SoundEffectInstance.cs`'s base `Stop(bool)` line-by-line). `Pause()` already safe no-op (guarded by `track &&`). **`Resume()` had a real gap**: FNA's `Resume()` (`SoundEffectInstance.cs`) calls `Play()` when there's no active handle ("XNA4 just plays if we've not started yet") instead of no-op'ing -- CNA's `Resume()` was a plain no-op in that case. Since `Dispose()` nulls `track_`/`dynamicTrack_`, this condition also covers "after Dispose", where delegating to `Play()` now correctly surfaces `ObjectDisposedException` instead of silently doing nothing (safer than FNA's real behavior, where `Play()` has no disposed guard at all and would silently resurrect a disposed instance). Fixed in both `SoundEffectInstance::Resume()` and `DynamicSoundEffectInstance::Resume()` (separate override, doesn't inherit the base fix). **`SubmitBuffer`/`SubmitFloatBufferEXT` had the same offset+count integer-overflow bug as `P9-VALIDATION-003`** (confirmed by a second, independent segfault) -- fixed identically. Also had no disposed guard at all -- see `P9-VALIDATION-011`.
* [x] P9-VALIDATION-011 Ensure `DynamicSoundEffectInstance::SubmitBuffer` cannot queue buffers after disposal.
  *Note:* Fixed: both `SubmitBuffer` and `SubmitFloatBufferEXT` now throw `ObjectDisposedException` first. Without this, a caller that kept submitting after `Dispose()` would grow `queuedBuffers_` unboundedly, since a disposed instance's `getStateProperty()` reports `Stopped` (never reaches `SubmitQueuedToStream()`). 6 new tests total across `SoundEffectInstanceTests.cpp`/`DynamicSoundEffectInstanceTests.cpp` for `P9-VALIDATION-010`/`011` (`Resume` on never-played/disposed for both classes, `SubmitBuffer`/`SubmitFloatBufferEXT` disposed guards, `SubmitBuffer` overflow). `git stash`-verified: the two overflow tests **segfault**, the four disposed/never-played tests fail as plain assertions, all against the pre-fix code. Full suite (2090/2090) green after restoring the fix, verified clean under a full ASan+UBSan build.
* [x] P9-VALIDATION-012 Ensure `SoundEffect::CreateInstance()` handles disposed `SoundEffect` consistently with XNA/FNA.
  *Note:* Audited, no fix needed: FNA's `CreateInstance()`/`SoundEffectInstance` internal ctor don't check `IsDisposed` either -- the ctor only reads `parentEffect.channels`, a plain field that survives `Dispose()`, so a disposed-`SoundEffect`-backed instance doesn't fail until `Play()` is attempted (FNA's deferred-failure pattern). CNA already matches this exactly: `getNativeAudioHandle()` safely returns `nullptr` post-dispose (guarded), so `CreateInstance()` never crashes and the resulting instance's `Play()` is a safe, inert no-op.
* [x] P9-VALIDATION-013 Add tests for disposed `SoundEffect::CreateInstance()`.
  *Note:* `SoundEffectTests.cpp::CreateInstanceOnDisposedSoundEffectDoesNotThrowButResultingPlayIsInert` -- locks in the already-correct deferred-failure behavior from `P9-VALIDATION-012`. As expected for a "no bug found" audit item, this test passes against both the pre- and post-`P9-VALIDATION` code (not a regression reproduction).
* [x] P9-VALIDATION-014 Audit `SoundEffect::FromStream()` ownership and exception behavior.
  *Note:* Already solid, pre-dating Fáze 9: empty stream and non-WAV/corrupt-format both throw `NotSupportedException` (matches FNA's own `NotSupportedException` throws for a bad RIFF/WAVE signature), and ownership already matches the documented "caller owns the returned object" contract (`FromStream` returns `new SoundEffect(...)`, a raw owning pointer). No fix needed.
* [x] P9-VALIDATION-015 Add tests for invalid and empty streams where feasible.
  *Note:* Already satisfied by pre-existing tests (`FromStreamEmptyThrowsNotSupported`, `FromStreamGarbageThrowsNotSupported`, plus the smpl-chunk/truncated-chunk tests from Fáze 8's `CP-17`/`CP-23`) -- closed as already-satisfied, mirroring how Fáze 7's `CP-14` was closed. No new test needed.

## P9-XACT — XACT cue behavior fidelity

* [x] P9-XACT-001 Audit XSB cue variation parsing against FNA.
  *Note:* Compared `XactParser.cpp`'s variation-table byte layout (`XactParser.cpp` variation parsing branch) line-by-line against `FACT_internal.c`'s `FACTSoundBank_Prepare` variation-table switch (types WAVE=0/SOUND=1/INTERACTIVE=3/COMPACT_WAVE=4) -- the layout already matched exactly (entry sizes 5/6/16/3 bytes respectively, `entryCountAndFlags` bit-packing of `entryCount`/`type` at the same bit offsets). The one real gap found: `XsbVariEntry` never retained INTERACTIVE's `var_min`/`var_max` fields (read and discarded), which is what P9-XACT-002/003 fix. `linger` (`FACT_internal.c` `table->entries[j].linger`) is only surfaced via native `FACTGetCueProperties`, which has no XNA-public `Cue` equivalent -- left unretained, still read-and-discarded to keep the cursor in sync.
* [x] P9-XACT-002 Preserve parsed interactive variation variable ranges instead of falling back to uniform random selection.
  *Note:* Added `varMin`/`varMax` (`float`, default 0.0f) to `XsbVariEntry` (`XactTypes.hpp`) and populated them in `XactParser.cpp`'s INTERACTIVE branch (previously `vc.f32(); vc.f32(); // var_min, var_max` discarded the values). Regression test `XactParserTest.VariationTypeInteractiveRetainsVarMinAndVarMax` (`XactParserTests.cpp`) -- confirmed failing to *compile* against pre-fix code (`git stash`: "has no member named 'varMin'"), the strongest possible proof this field genuinely didn't exist before.
* [x] P9-XACT-003 Implement interactive variation selection based on cue variables where feasible.
  *Note:* `Cue::Play()`'s variation-selection block (`Cue.cpp`) now branches on `var.type == 3` (INTERACTIVE) before falling into the existing weighted-lottery path (which remains unchanged for wave/sound/compact_wave tables). Added `AudioEngine::GetVariableNameByIndex(SharpRuntime::shortcs)` (private, `NOXNA`-flavored internal helper, `AudioEngine.hpp`/`.cpp`) to resolve `XsbVariation::variable` (an index into the engine's parsed XGS variable table, exactly like FAudio's `table->variable` indexing `engine->variables[]`) to its declared name, then reuses `Cue::GetVariable(name)` -- already correct for the cue-local-then-global fallback FACT itself distinguishes via the `ACCESSIBILITY_CUE` bit -- to read its current value. Matches FAudio's `get_active_variation_index` (`FACT_internal.c`): first entry (file order) whose `[varMin, varMax]` contains the value wins (inclusive both bounds); no match leaves `sound` unresolved, matching FAudio's `create_sound()` aborting entirely on `get_active_variation_index() == false` -- the cue stays `Playing` but is silent, not an error. Removed the now-stale "falls back to a uniform pick" deviation row from `CHECKLIST.md` since this is fixed, not a documented gap.
* [x] P9-XACT-004 Add tests for variable-driven interactive variation selection.
  *Note:* `CueTests.cpp` gained `BuildXsbFixtureBytesWithInteractiveVariation()` (2 sounds distinguished by `categoryIndex`, one INTERACTIVE table bound to `fixture.xgs`'s "Volume" variable with disjoint `[0.0,0.4]`/`[0.6,1.0]` entry ranges) plus 4 tests: low-range pick, high-range pick, inclusive-boundary pick (value exactly at a `varMax`/`varMin` edge), and the no-match "stays Playing but silent" case (asserted via `categoryIdx_` staying at its `0xFFFF` default, since neither entry's category index is `0xFFFF`). All 5 new tests (this + the parser-level one above) verified via `git stash` to fail against pre-fix code -- the parser test fails to compile, and the `Cue`-level tests would fall back to the old degenerate-weight uniform-pick path once compiling, making them non-deterministic against the values chosen. Full suite: 2098/2098 passing (up from 2093).
* [x] P9-XACT-005 Audit XACT RPC parsing and currently unused runtime data.
  *Note:* RPC (Runtime Parameter Control) data is currently 0% retained anywhere in CNA -- both halves of it are silently discarded. (1) `ParseXgs` (`XactParser.cpp`) never even reads `rpcOffset`/`rpcCount` from the XGS header (they fall under its own "rest of offsets not needed" comment); the global RPC curve table (`FACT_internal.c` `FACT_INTERNAL_ParseAudioEngine`: each entry is `variable:u16, pointCount:u8, parameter:u16` + `pointCount` points of `x:f32, y:f32, type:u8`) is simply never parsed. (2) Per-sound/per-track RPC code *references* in the .xsb (`SOUND_FLAG_HAS_RPC`/`SOUND_FLAG_HAS_TRACK_RPC`, `SOUND_FLAG_RPC_MASK=0x0E`) are read only far enough to skip them (`XactParser.cpp`'s sound-parsing loop: `rpcDataLength = sc.u16(); sc.skip(rpcDataLength - 2);`) -- FAudio's own `parse_rpc_codes` (`FACT_internal.c`) shows this blob is actually `count:u8` + `count` absolute-offset `u32` codes into the XGS RPC table, which CNA doesn't retain at all. Runtime application (`FACT_INTERNAL_UpdateRPCs`/`FACT_INTERNAL_CalculateRPC`, `FACT_internal.c`) is a genuinely continuous system -- `FACTAudioEngine_DoWork` re-evaluates every active track's bound RPC curves every engine tick (supporting live-changing variables, plus two magic built-in variables, "AttackTime"/"ReleaseTime", that evaluate to elapsed-playback/release time rather than a stored value), accumulating `rpcVolume`/`rpcPitch`/`rpcReverbSend` and overwriting `rpcFilterFreq`/`rpcFilterQFactor`. CNA has no per-frame Cue update tick at all today (`AudioEngine::Update()` only sweeps fire-and-forget cues, `P9-LIFECYCLE-008/009`) -- a faithful continuous implementation is out of scope for "wire simple RPC ... where feasible" per the Fáze 9 task list's own wording. P9-XACT-006/007 instead implement a deliberately narrower "simple" form: RPC volume/pitch curves evaluated *once*, at `Cue::Play()` time, against the bound variable's value at that instant (matching every other one-shot value CNA already computes at Play() -- pitch, category volume, wave selection) -- not continuously re-evaluated while playing. This is a documented, intentional narrowing, not a bug: it's recorded in `CHECKLIST.md` alongside the RPC-continuity gap it leaves open. Global (non-CUE-accessible) RPCs are supported the same way `Cue::GetVariable`'s existing fallback already does for interactive variation selection; the "AttackTime"/"ReleaseTime" built-in variables and DSP-preset-targeting RPCs (`parameter >= RPC_PARAMETER_COUNT`) are out of scope (CNA has no DSP preset system and no elapsed-time tracking) and documented as unsupported in `CHECKLIST.md`.
* [x] P9-XACT-006 Wire simple RPC volume changes into cue playback where feasible.
  *Note:* Added real parsing for both halves of RPC data that P9-XACT-005 found were discarded: `ParseXgs` (`XactParser.cpp`) now reads `rpcOffset`/`rpcCount` and parses the global RPC curve table into `XgsData::rpcs`/`rpcCodeMap` (byte layout matches `FACT_INTERNAL_ParseAudioEngine` exactly: `variable:u16, pointCount:u8, parameter:u16` + points of `x:f32,y:f32,type:u8`); `ParseXsb`'s sound loop now parses the sound-level RPC code list (`SOUND_FLAG_HAS_RPC`: `count:u8` + `count*code:u32`) into `XsbSound::rpcCodes` instead of only skipping past it. Per-track RPC lists (`SOUND_FLAG_HAS_TRACK_RPC`) are still walked byte-for-byte for cursor sync but not retained -- CNA applies RPC at the whole-sound level only, matching how per-track structure is already simplified elsewhere in this parser (documented in `CHECKLIST.md`). `AudioEngine::FindRpcByCode()` (private, `AudioEngine.hpp`/`.cpp`) resolves a code to its curve. `Cue::Play()` evaluates every RPC bound to the resolved sound once (not continuously -- see `CHECKLIST.md`), via a new `EvaluateRpcCurve()` helper matching FAudio's `FACT_INTERNAL_CalculateRPC` piecewise interpolation (linear/fast/slow/sin-cos, clamped outside the curve's domain); VOLUME-parameter results (centibels) are summed then converted to a single amplitude multiplier (`10^(sum/2000)`, mathematically identical to FAudio summing centibels and converting once, since it's an exponential sum) and multiplied into the existing `waveRef.volume * catVol` chain, since CNA's volume model is amplitude-multiplicative throughout rather than FAudio's additive-centibel one.
* [x] P9-XACT-007 Wire simple RPC pitch changes into cue playback where feasible.
  *Note:* Same evaluation path as P9-XACT-006; PITCH-parameter results (cents, same unit as `XsbSound::pitchCents`) are summed with the sound's own `pitchCents` before a single `CentsToPitch()` conversion, matching FAudio's own sum-cents-then-set-pitch order (`FACT_internal.c`, `sound->rpcData.rpcPitch + ... + tracks[i].evtPitch`). `CentsToPitch`'s parameter widened from `int16_t` to `float` to allow the summed (possibly fractional) cents value through without truncation; behavior for existing integral callers is unchanged.
* [x] P9-XACT-008 Add tests for XACT variable-to-volume RPC behavior.
  *Note:* `CueTests.cpp`'s shared `fixture.xgs` (`BuildXgsFixtureBytes`) gained two RPC curves bound to the existing "Volume" variable -- VOLUME: `[0,1] -> [-2000,0]` centibels, PITCH: `[0,1] -> [-600,+600]` cents -- plus a new `fixture_rpc.xsb` (`BuildRpcXsbFixtureBytes`, `SharedRpcBank()`) with two real-WaveBank-backed simple cues ("VolumeRpcCue"/"PitchRpcCue"), each bound to exactly one curve so the two parameters test in isolation. `CueTest.PlayScalesVolumeByRpcCurveEvaluatedAtCurrentVariableValue` sets the variable to each curve extreme and checks the resulting `SoundEffectInstance::getVolumeProperty()` ratio is ~10x (the curve's 20dB range) -- checked as a ratio, not an absolute value, so the test doesn't depend on the unrelated volume-byte-to-amplitude conversion's exact output. Also added `XactParserTest.SoundLevelRpcCodeIsRetained` (parser-level) and extended the two pre-existing IN-6/IN-8 RPC-skip regression fixtures (which encoded the RPC blob as a bare skip-length blob, not FAudio's real `count:u8 + codes` structure) to also assert the code is now retained on `XsbSound::rpcCodes`.
* [x] P9-XACT-009 Add tests for XACT variable-to-pitch RPC behavior.
  *Note:* `CueTest.PlayShiftsPitchByRpcCurveEvaluatedAtCurrentVariableValue` checks all 3 points of the curve's range (var=0.0/0.5/1.0 -> pitch -0.5/0.0/+0.5) against "PitchRpcCue". `CueTest.PlaySoundWithNoRpcCodesIsUnaffectedByEngineRpcCurves` guards against a regression where a sound with an empty `rpcCodes` list could pick up unrelated curves from elsewhere in the engine's XGS data (asserts `SharedLongBank()`'s "LongCue", which has no RPC codes, plays at exactly pitch 0.0 despite `fixture.xgs`'s curves existing). All new/modified tests verified via `git stash`: the parser-level tests fail to *compile* against pre-fix code (no `rpcCodes` member), confirming genuine dependency on the fix. Full suite: 2102/2102 passing (up from 2099), stress-tested 5 consecutive clean runs.
* [x] P9-XACT-010 Audit DSP/filter parsing and runtime application.
  *Note:* Read-only audit against `FACT_internal.c` (FAudio) since FNA's C# layer doesn't parse
  XACT content at all -- that's native FACT's job, so FAudio is the only available byte-level
  reference for this part of the format. Two genuinely distinct XACT concepts were being conflated
  under the task name "DSP/filter": (1) **Sound-level `SOUND_FLAG_HAS_DSP`/`dspCodes`** --
  `FACTSoundBank_Prepare` parses `dspCodeCount:u8` + that many `u32` codes (indices into the XGS
  DSP preset table), but grepping all of FAudio's `src`/`include` shows those code *values* are
  never read back anywhere -- the only runtime use is `if (sound->dspCodeCount > 0)` as a bare
  boolean, to additionally route the wave's voice to `parentEngine->reverbVoice` (an aux-send bus)
  alongside the master voice (`FACT_internal.c`, wave-prepare path). So sound-level "DSP" in real
  FACT is a **reverb-send enable flag**, not a filter selector; the DSP preset table's actual
  contents (reverb parameters, `FAudioFXReverbParameters`) are engine-level, not per-sound.
  CNA's existing skip code (`XactParser.cpp`'s `SOUND_FLAG_HAS_DSP` branch) already consumes
  exactly the right bytes (length:u16 unused + count:u8 + count*4 skipped) -- byte-layout is
  correct, and since SDL3_mixer has no aux-send/return bus, there is nothing more to wire here;
  this confirms the reverb no-op (P9-XACT-012) should stay as-is. (2) **Per-track filter data** is
  a completely separate field: for `SOUND_FLAG_COMPLEX` sounds, each track's metadata
  (`FACTSoundBank_Prepare`'s track loop) has `volume:volbyte`, `code:u32`, then -- only when
  `contentVersion != FACT_CONTENT_VERSION_3_0 (43)` -- `filterData:u16` + `frequency:u16`; for
  that legacy version FAudio hard-sets `track->filter = 0xFF` (disabled) and skips both fields.
  CNA only targets contentVersion 46 (both `ParseXgs`/`ParseXsb` warn-not-error otherwise), so that
  branch is dead code for any content CNA will ever load -- not a real gap. CNA's parser already
  reads-and-discards the right bytes here too (`XactParser.cpp`'s complex-track loop:
  `sc.u16(); // filterData` + `sc.u16(); // frequency`). Bit layout of `filterData` (FACT_internal.c):
  bit0 = has-filter, `track->filter = (filterData >> 1) & 0x02` for type, `qfactor = (filterData
  >> 8) & 0xFF`. Note the type mask is suspicious: `FAudioFilterType` has 3 values (LowPass=0/
  BandPass=1/HighPass=2, needing 2 bits) but `(x>>1)&0x02` only ever yields 0 or 2, so BandPass (1)
  appears structurally unreachable via this exact math -- looks like a genuine upstream FAudio
  quirk (their own code has an unrelated "Huh...?" comment two lines below), not something for CNA
  to independently "fix": if P9-XACT-011 wires this, replicate FAudio's exact bit math for
  behavioral parity and flag the oddity in a comment rather than correcting it. Runtime application
  (`FACTSoundBank_Prepare`'s per-tick track-update path) builds `FAudioFilterParameters{ Type,
  Frequency, OneOverQ }` every engine tick, with `Frequency`/`OneOverQ` overridable by a live
  filter RPC if bound, else falling back to the track's parsed base values -- this is a continuous
  per-tick system, the same kind of continuity gap P9-XACT-005's note already identified for RPC
  volume/pitch (CNA has no per-frame `Cue` update tick; `AudioEngine::Update()` only sweeps
  fire-and-forget cues). Feasibility check against CNA's existing filter primitives:
  `SoundEffectInstance::INTERNAL_apply{Low,High,Band}PassFilter(float)` (real SDL3_mixer per-track
  state-variable filters) take only a cutoff/center frequency, matching FNA's own *public*
  `ApplyLowPassFilter`/etc. (`SoundEffectInstance.cs`) which likewise hardcode `OneOverQ = 1.0f` --
  so the single-float signature is not a gap versus FNA's public surface. The XACT-internal track
  filter's real `qfactor` byte has no home in that signature though; wiring true fidelity would
  need a NOXNA-tagged internal-only `OneOverQ` parameter added to the `INTERNAL_apply*Filter`
  methods, or an accepted one-shot-at-Play()-time narrowing (frequency only, fixed Q=1.0)
  consistent with the RPC volume/pitch narrowing already accepted in P9-XACT-006/007. No code
  changed by this task (read-only audit, per its own task description) -- findings hand off
  directly to P9-XACT-011/012/013.
* [x] P9-XACT-011 Wire parsed low-pass/high-pass/band-pass filters to `SoundEffectInstance` where feasible.
  *Note:* Implements the wiring the `P9-XACT-010` audit scoped out. `XsbWaveRef` (`XactTypes.hpp`)
  gained `filterType`/`filterFrequencyHz`/`filterQFactorRaw` (default `filterType=0xFF`, matching
  FAudio's own "no filter" sentinel); `XactParser.cpp`'s complex-track loop now retains the
  `filterData`/`frequency` bytes it used to discard, replicating FAudio's exact bit-decode
  (`(filterData>>1)&0x02` for type, `(filterData>>8)&0xFF` for qfactor) rather than "fixing" its
  band-pass-unreachable quirk. `SoundEffectInstance::INTERNAL_apply{Low,High,Band}PassFilter`
  (`SoundEffectInstance.{hpp,cpp}`) gained an `oneOverQ` parameter defaulted to `1.0f` (every
  existing caller, including FNA's own dead-code equivalents, is unaffected). Two pure,
  independently-unit-tested static helpers do the FAudio-exact conversions: `INTERNAL_
  calculateFilterCutoff(frequencyHz, sampleRate)` (`2*sin(pi*min(f/sr,0.5))`, matching
  `FACT_INTERNAL_CalculateFilterFrequency`) and `INTERNAL_calculateFilterOneOverQ(qfactorRaw)`
  (`min(3/qfactor,1)`, matching FAudio's inline track-init formula, with a divide-by-zero guard
  FAudio itself doesn't need since real XACT tool output never emits `qfactor==0`). The new
  `INTERNAL_applyXactTrackFilter(filterType, frequencyHz, qfactorRaw)` (NOXNA, `friend class Cue`)
  is the real entry point: queries the live SDL3_mixer device sample rate via
  `MIX_GetMixerFormat`, converts, and dispatches to the matching apply method. `Cue::Play()`
  (`Cue.cpp`) calls it once per spawned `SoundEffectInstance` whenever `waveRef.filterType != 0xFF`
  , right after `Play()` — same one-shot-at-`Play()`-time narrowing already accepted for RPC
  volume/pitch (`P9-XACT-006/007`), not a continuous per-tick re-evaluation (no `Cue` update tick
  exists). RPC `parameter` 3/4 (filter frequency/Q) remain unevaluated (`CHECKLIST.md`). Per the
  user's explicit decision (asked before implementing, since this touches a private-API signature):
  added the internal-only `oneOverQ` parameter for real Q fidelity rather than narrowing to a fixed
  `Q=1.0`. Tests: `XactParserTest.ComplexTrackFilterDataIsRetained`/
  `ComplexTrackWithFilterBitClearHasNoFilterSentinel`/`ComplexTrackFilterDataDecodesLowPassType`
  (parser retention + bit-decode, including the has-filter-bit-clear sentinel case);
  `SoundEffectInstanceFilterMathTest.CalculateFilterCutoffMatchesFAudioFormula`/
  `CalculateFilterCutoffClampsAtNyquist`/`CalculateFilterOneOverQMatchesFAudioFormula`/
  `CalculateFilterOneOverQGuardsDivideByZero` (pure math, no device needed);
  `SoundEffectInstanceTest.ApplyXactTrackFilterDispatches{HighPass,LowPass,BandPass}*`/
  `ApplyXactTrackFilterIgnoresUnrecognizedType`/`ApplyXactTrackFilterBeforePlayIsNoOp` (dispatch +
  guards, via a new `INTERNAL_getFilterStateForTest` readback added alongside the existing
  `ProcessFilterSamplesForTest` test-only hook); `CueTest.PlayWiresRealXactTrackFilterIntoSpawnedInstance`/
  `PlaySoundWithNoFilterDataHasNoActiveFilter` (end-to-end, via a new real-WaveBank-backed
  `BuildFilterXsbFixtureBytes`/`SharedFilterBank()` complex-sound fixture, `CueTests.cpp`).
  Verified via `git stash` (stashing all 5 production-code files, keeping the 3 test files):
  confirms a genuine compile-time dependency (every new test references a symbol that doesn't
  exist pre-fix) rather than assertion failures — same category of proof as `P9-XACT-008/009`'s
  parser-level tests. Full suite: 3226/3228 passing (2 expected hardware-skip), up from 3212/3214
  before this task (whole-suite count includes an unrelated `feature/net` merge, see `NEXT.md`
  §2); audio-scoped subset 320/320 (up from 306). Also verified clean under a full ASan+UBSan
  build of just the audio suite (no new lifetime/ownership beyond the pre-existing `filterState_`
  `unique_ptr`, but the shared `FilterState` mixing-thread interaction was flagged risky before,
  `P9-BUILD-001..007`).
* [x] P9-XACT-012 Keep reverb as documented no-op only if faithful implementation is not feasible with current backend.
  *Note:* Confirmed, not changed: `P9-XACT-010`'s audit found sound-level `SOUND_FLAG_HAS_DSP` is
  FACT's reverb-send-enable flag (routes a wave's voice to an aux-send bus alongside the master
  voice), and SDL3_mixer has no aux-send/return bus concept at all — so there is nothing to wire.
  `INTERNAL_applyReverb` stays exactly as it was (a documented no-op matching FNA's own dead-code
  status for the equivalent C# method).
* [x] P9-XACT-013 Document exact XACT DSP features supported and unsupported.
  *Note:* `CHECKLIST.md` gained two new accepted-deviation rows: one covering the new real
  per-track filter wiring (frequency + Q both real; RPC-driven live filter-frequency/Q override
  and continuous per-tick re-evaluation are the two remaining unsupported pieces) and one covering
  the upstream FAudio band-pass-unreachable bit-decode quirk (replicated as-is, not corrected).
  Sound-level DSP/reverb's no-op status was already documented pre-existingly and is unchanged.
* [x] P9-XACT-014 Ensure missing wave, missing sound, and invalid cue index behavior matches XNA/FNA as closely as possible.
  *Note:* Audited `SoundBank::GetCue`/`Cue::Play()` against FNA's `SoundBank.cs` and FAudio's
  `FACT_internal.c`/`FACT.c`. **Invalid cue *name*** already matched FNA exactly pre-existing
  (`FACTSoundBank_GetCueIndex` returning `FACTINDEX_INVALID` -> FNA's `InvalidOperationException
  ("Invalid cue name!")` == CNA's `SoundBank::GetCue`/`PlayCue` throwing the same, already tested
  by `GetCueInvalidNameThrowsInvalidOperation`/`PlayCueInvalidNameThrowsInvalidOperation`). Found
  and fixed a **real bug** in the different, unaudited case the task's own wording targets --
  *internal* unresolvable references within an otherwise name-valid cue (which never happens with
  real XACT-tool-built content, only corrupt/malformed data, since FNA/FACT has no C# equivalent
  to cite -- native FACT just does raw pointer arithmetic on sound codes as absolute file offsets,
  so there's no "lookup" step to fail the way CNA's `soundCodeMap` translation layer has one).
  `XactParser.cpp`'s 5 sound-code-to-index resolution sites (`ParseXsb`'s simple-cue loop,
  complex-cue single-sound branch, complex-cue variation-table fallback, and the SOUND/INTERACTIVE
  variation-entry branches) all fell back to `soundIndex = 0` whenever a cue/entry's sound code
  didn't match any parsed sound -- **silently aliasing an unresolvable reference onto whichever
  sound happens to be first in the bank and playing it**, instead of resolving to "no sound found"
  (the behavior `Cue::Play()` already has, and already gets right, for a `soundIndex` that's
  genuinely out of range against an empty/undersized `sounds` array). Fixed with a new
  `kInvalidSoundIndex = 0xFFFFFFFFu` sentinel used at all 5 sites instead of `0` -- relies entirely
  on `Cue::Play()`'s pre-existing `soundIndex < xsb->sounds.size()` bounds checks to treat it as
  unresolvable, so no consumer-side code needed changing. Confirmed via `git stash` that a real
  `SoundEffectInstance` (a non-null pointer) actually gets spawned playing the wrong sound
  pre-fix, not just a failed assertion -- this was a genuine "wrong audio plays" defect, not a
  theoretical one. **Missing/unregistered wave bank** and **out-of-range wave index within a real
  wave bank** were both already correct pre-existing (`Cue::Play()`'s `FindWaveBank()==nullptr`
  guard; `WaveBank::GetSoundEffect`'s `waveIndex >= entries.size()` bounds check) but had zero test
  coverage -- see `P9-XACT-015`.
* [x] P9-XACT-015 Add tests for missing wave/cue behavior.
  *Note:* `XactParserTest.SimpleCueWithUnresolvableSoundCodeDoesNotAliasToSoundZero` (parser-level,
  asserts `soundIndex >= sounds.size()` rather than pinning the exact sentinel value, so the test
  stays valid even if the sentinel's concrete value ever changes). Three new end-to-end
  `CueTest`s, each with its own minimal real-`WaveBank`-backed fixture: `PlayWithUnresolvableSoundCodeSpawnsNoInstance`
  (`BuildUnresolvableSoundXsbFixtureBytes`, a cue whose sound code doesn't match the bank's one
  real sound), `PlayWithUnregisteredWaveBankSpawnsNoInstance` (`BuildMissingWaveBankXsbFixtureBytes`,
  a sound referencing a wave bank name -- "GhostWaveBank" -- deliberately never registered with
  `SharedEngine()`), `PlayWithOutOfRangeWaveIndexSpawnsNoInstance` (`BuildMissingWaveIndexXsbFixtureBytes`,
  a sound's `waveIdx=999` against `LongWaveBank`'s real single entry). All three assert
  `getIsPlayingProperty()` is still true (matches FACT's "silent, not stopped/errored" semantics)
  and `CueTestAccess::ActiveInstance(*cue, 0) == nullptr`. Verified via `git stash` (`XactParser.cpp`
  only, the two already-correct wave-bank/wave-index paths needed no source change): the
  unresolvable-sound-code test fails to compile isn't the mechanism here -- it fails a real
  assertion (`ActiveInstance` returns a genuine non-null instance pre-fix), stronger proof than a
  compile-time dependency. Full suite: 3230/3232 passing (2 expected hardware-skip), up from
  3228/3230; audio-scoped subset 324/324, up from 322. Verified clean under a full ASan+UBSan
  build of the audio suite.

## P9-3D — 3D audio fidelity

* [x] P9-3D-001 Audit `Apply3D` behavior against FNA for mono and stereo sources.
  *Note:* Read `Apply3D`/`SetPanMatrixCoefficients` (`SoundEffectInstance.cs`) line-by-line
  against `SoundEffectInstance::Apply3D`/`ApplyTrackProperties` (`SoundEffectInstance.cpp`). Key
  finding: FNA's `Apply3D` does **not** use `SetPanMatrixCoefficients` at all -- that method is
  only ever called from the direct `Pan` property setter (guarded by `if (is3D) return;`, so it's
  skipped entirely once `Apply3D` has run) and from `InitDSPSettings`'s initial setup. `Apply3D`
  instead computes its output matrix via the native `F3DAudioCalculate` (X3DAudio) call against
  real emitter/listener geometry -- a completely different, source-channel-count-aware code path
  from the Pan property's simplified formula. CNA's `Apply3D` uses `ApplyTrackProperties` --
  **the exact same formula the direct `Pan` property setter also uses** (`Play()`'s own call at
  line ~326) -- with zero source-channel-count awareness anywhere in `Apply3D` (confirmed via
  grep: no `channels`/`SrcChannelCount` reference exists in the method at all). This means CNA's
  `Apply3D` treats a stereo source *identically* to a mono source, always, unlike FNA where
  `Apply3D`'s X3DAudio computation is a fundamentally different (and channel-count-aware) code
  path from the Pan setter's simplified matrix. For **mono** sources specifically, the two
  formulas independently verified bit-identical to FNA's `SetPanMatrixCoefficients` mono branch
  (already established by `CP-19`'s note) -- so `Apply3D`'s approximation for a mono source
  happens to coincide with what the Pan-setter path would also produce, even though FNA reaches
  a mono answer via a structurally different route (X3DAudio vs the simplified formula) that
  happens to agree for the 1-channel case. For **stereo** sources, this is the *exact same root
  limitation* `CP-19` already found and the user already discussed for the Pan property (SDL3_mixer's
  `MIX_StereoGains` API is a plain per-channel gain pair, not a 4-coefficient crossfeed matrix) --
  `Apply3D` just reaches the identical formula through a different call site. Test fixture note:
  `SoundEffectInstanceTest`'s shared fixture (`SoundEffectInstanceTests.cpp`) already constructs
  a **stereo** `SoundEffect`, so every existing `Apply3D*` test already exercises the stereo-source
  case structurally -- none independently verify the exact resulting gain values, since
  SDL3_mixer has no `MIX_StereoGains` getter (same pre-existing limitation noted for `CP-3`/`T-4B`).
  Distance attenuation's formula shape (CNA: `1/(1+distance/distScale)`) vs FNA's default X3DAudio
  linear distance curve is a *separate* deviation, out of this task's scope -- see `P9-3D-003`.
* [x] P9-3D-002 Fix or document stereo panning behavior. Avoid hard stereo pan if FNA uses crossfeed or another model.
  *Note:* No new fix -- this is the same accepted deviation as `CP-19`, which the user already
  discussed and declined to implement (a real crossfeed mix would need to share SDL3_mixer's
  single per-track "cooked callback" slot with the already-shipped `T-4C` DSP filter, a real
  regression risk to already-tested filter code). `CHECKLIST.md` gained a explicit cross-reference
  extending `CP-19`'s existing row to name `Apply3D` alongside the `Pan` property, since the
  `P9-3D-001` audit confirmed both paths hit the exact same formula, not just the same *kind* of
  limitation. No new test added: every existing `Apply3D` test already runs against a stereo
  source (see `P9-3D-001`'s note), and there is no SDL3_mixer API to independently verify the
  resulting per-channel gain values, so a new test would only re-assert "does not throw" --
  already covered by `Apply3DSingleListener`/`Apply3DDoesNotModifyVolumeOrPanProperties`.
* [x] P9-3D-003 Audit distance attenuation behavior against `SoundEffect.DistanceScale`.
  *Note:* Read FAudio's `F3DAudio.c` `ComputeDistanceAttenuation` (the function `F3DAudioCalculate`
  uses when an emitter has no custom `pVolumeCurve`, which is the case for every XNA/FNA
  `AudioEmitter` -- FNA's `SoundEffectInstance.Apply3D` never sets one). Found a **real, confirmed
  bug**: the no-custom-curve branch is `res = 1.0f; if (normalizedDistance >= 1.0f) res /=
  normalizedDistance;` where `normalizedDistance = distance / CurveDistanceScaler` -- i.e. **full
  volume, zero attenuation, for any distance within `DistanceScale`**, with inverse-distance
  falloff (`gain = DistanceScale / distance`) only strictly beyond it. CNA's `Apply3D`
  (`SoundEffectInstance.cpp`) instead used `atten = clamp(1/(1+distance/distScale), 0, 1)` --
  a continuously-falling-off-from-zero formula with no "safe radius" at all: already at **half
  volume exactly at `distance == DistanceScale`**, where real XNA/FNA is still at full volume, and
  attenuating even for emitters very close to the listener where real XNA/FNA has zero rolloff.
  This is a substantial, easily-audible, easily-reproducible defect (every 3D-positioned sound in
  any game using `Apply3D` would have played measurably quieter than real XNA at every distance,
  including well within the emitter's intended "full volume" radius), not a cosmetic
  approximation gap like `CP-19`'s stereo-pan crossfeed limitation. Fixed by replacing the formula
  with FAudio's exact one: `normalizedDistance = distance / distScale; atten = (normalizedDistance
  >= 1) ? clamp(1/normalizedDistance, 0, 1) : 1.0f`. Verified via `MIX_GetTrackGain` (SDL3_mixer
  *does* have a gain getter, unlike the stereo-pan case `P9-3D-001` found had none) with 3 new
  tests at distance = 0.5x/1.0x/2.0x `DistanceScale`, added a `DistanceScaleGuard` RAII helper
  (`SoundEffectInstanceTests.cpp`) to save/restore the shared static `SoundEffect.DistanceScale`
  around them. Verified via `git stash`: all 3 new tests fail against the pre-fix formula with
  the exact wrong values the formula predicts (0.5/0.5/0.333 instead of 1.0/1.0/0.5), confirming
  genuine dependency. Full suite 3241/3243 (2 expected hardware skips), audio subset 335/335,
  clean under ASan+UBSan.
* [ ] P9-3D-004 Audit doppler behavior against `SoundEffect.DopplerScale` and `SoundEffect.SpeedOfSound`.
* [ ] P9-3D-005 Implement doppler pitch adjustment if feasible.
* [x] P9-3D-006 Add tests for distance attenuation.
  *Note:* Folded into `P9-3D-003`'s own fix: `Apply3DAppliesFullVolumeWithinDistanceScale`/
  `Apply3DAppliesFullVolumeExactlyAtDistanceScaleBoundary`/
  `Apply3DAppliesInverseDistanceLawBeyondDistanceScale` (`SoundEffectInstanceTests.cpp`) cover the
  full-volume-within-scale, exact-boundary, and beyond-scale inverse-law cases via real
  `MIX_GetTrackGain` verification.
* [ ] P9-3D-007 Add tests for panning left/right based on listener/emitter orientation.
* [ ] P9-3D-008 Add tests for doppler behavior if implemented.
* [ ] P9-3D-009 Document remaining limitations of CNA 3D audio compared to XNA/FNA.

## P9-HARDWARE — Audio hardware and exception behavior

* [x] P9-HARDWARE-001 Audit `NoAudioHardwareException` usage across the audio backend.
  *Note:* Confirmed the suspicion in `CHECKLIST.md`/`NEXT.md`: `NoAudioHardwareException.hpp` is a
  type-only stub, never thrown anywhere in CNA's production code (grepped all of
  `src/Microsoft/Xna/Framework/Audio/*.cpp`). Compared against FNA's two throw sites
  (`AudioEngine.cs` ctor: `FACTAudioEngine_GetRendererCount() == 0` -> throw; `SoundEffect.cs`'s
  lazy `Device()` singleton: `FAudioContext.Create()` failing -> throw). Two distinct findings:
  (1) `AudioEngine::Init()` (`AudioEngine.cpp`) unconditionally pushes exactly one
  `RendererDetail("SDL3_mixer", "SDL3_mixer")` regardless of whether real audio hardware exists --
  it never queries anything, so the FNA-equivalent check can structurally never fail from this
  path. (2) A **real, concrete bug**, not just a missing feature: CNA's actual "no hardware"
  detection already exists, just in the wrong place with the wrong exception type --
  `AudioMixer.cpp`'s `GetMixer()` (the lazy `MIX_Init()`/`MIX_CreateMixerDevice()` singleton,
  called from `SoundEffect`/`SoundEffectInstance`/`DynamicSoundEffectInstance`, structurally the
  same lazy-singleton shape as FNA's `SoundEffect.Device()`) throws a raw `std::runtime_error` when
  `MIX_CreateMixerDevice` fails -- **on the exact code path FNA throws `NoAudioHardwareException`
  from**, and in direct violation of `CLAUDE.md`'s "Exceptions on the XNA surface must be
  `System::` types, never raw `std::` exceptions" rule (the raw exception is completely uncaught,
  so it would propagate straight through `SoundEffect`'s constructor / `SoundEffectInstance::Play()`
  / `DynamicSoundEffectInstance`'s constructor -- all public XNA API entry points -- to user code,
  not just an internal detail). `SDL_AUDIODRIVER=dummy` (this repo's only test environment) always
  succeeds trivially, so this path has never actually fired in the test suite -- consistent with
  `NEXT.md`'s existing "device-dependent tests only ever run against the dummy driver" caveat.
  Handoff to `P9-HARDWARE-002`: convert `GetMixer()`'s failure exceptions to
  `NoAudioHardwareException` at the XNA-facing call sites (matching the established
  `XactParser`-throws-`std::`/`SoundBank`-`WaveBank`-catches-and-converts-at-the-boundary pattern
  already used elsewhere, per `CHECKLIST.md`) -- `AudioEngine::Init()` reporting a real renderer
  count (making the `AudioEngine`-constructor-time check possible at all) is a separate, larger
  design question left to `P9-HARDWARE-003`'s "decide" wording, since it changes when the mixer
  device gets opened (currently fully lazy, first real `SoundEffect`/track creation) and could
  affect every existing `AudioEngine`-constructing test's resource-acquisition timing.
* [x] P9-HARDWARE-002 Replace raw `std::runtime_error` backend initialization failures with XNA-compatible exception behavior where appropriate.
  *Note:* Fixed exactly the gap `P9-HARDWARE-001` found. `CNA::Internal::Audio::GetMixer()`
  (`AudioMixer.cpp`) still throws `std::runtime_error` -- kept exception-type-agnostic, matching
  this codebase's established internal-throws-`std::`/XNA-boundary-catches-and-converts pattern
  (`CHECKLIST.md`, same shape as `XactParser`'s corrupt-data throws being caught at `SoundBank`/
  `WaveBank`'s constructor boundary). Added a small `GetMixerOrThrowXna()` helper (duplicated once
  each in `SoundEffect.cpp`'s and `DynamicSoundEffectInstance.cpp`'s own anonymous namespaces --
  not shared via a header, since only these two files have a genuine "first GetMixer() call in the
  process" entry point; every `SoundEffectInstance.cpp` call site is provably unreachable as a
  first failure, since any `SoundEffectInstance` derives from an already-successfully-constructed
  `SoundEffect`) that catches `GetMixer()`'s `std::exception` and rethrows
  `NoAudioHardwareException(ex.what())`. Wired into every entry point that can genuinely be the
  first `GetMixer()` call for the whole process: `SoundEffect`'s two audio-loading constructors
  (`assetName`-based, buffer-based), `SoundEffect::FromStream()`, `SoundEffect::getMasterVolumeProperty
  ()`/`setMasterVolumeProperty()` (static properties a game could touch before ever constructing a
  `SoundEffect` -- FNA's own `MasterVolume` property also routes through `Device()`, the identical
  throw site), and `DynamicSoundEffectInstance::Play()` (its own first-possible failure point,
  since CNA's constructor -- unlike FNA's, which eagerly calls `SoundEffect.Device()` -- doesn't
  touch the mixer at all; that eager-vs-lazy acquisition-timing difference is a separate, larger
  design question left undisturbed here). `SoundEffect::Play(float,float,float)`'s own `GetMixer()`
  call is deliberately left unwrapped: it's unreachable as a first-failure site (a `SoundEffect`
  object can only exist post-construction, which already forced a successful `GetMixer()` call),
  so wrapping it would validate a scenario that can't happen (`CLAUDE.md`).
  **Verification caveat:** no new automated regression test accompanies this fix. `GetMixer()`'s
  `g_mixer` is a process-wide, once-ever-initialized cache; under this repo's only test
  environment (`SDL_AUDIODRIVER=dummy`) it always succeeds trivially, and once any earlier test in
  the shared `CnaTests` binary succeeds even once, every later test (including a hypothetical new
  one) sees the already-cached mixer and can never exercise the failure branch again -- the same
  process-isolation constraint `P9-HARDWARE-005`'s own task wording ("where feasible") already
  anticipates. `NoAudioHardwareException` the *type* itself (ctors, hierarchy, catchability) is
  already fully tested (`AudioExceptionsTests.cpp`, pre-existing); what's untested is specifically
  this new conversion wiring. Manually verified instead: full build + whole suite (3230/3232, 2
  expected skips) and the audio-scoped subset (324/324) both clean, including under a full
  ASan+UBSan build, confirming no regression from the refactor. A real regression test for the
  actual no-hardware failure path would need a fresh, isolated process (e.g. an invalid
  `SDL_AUDIODRIVER` value set *before* anything else in that process ever calls `GetMixer()`) --
  left to `P9-HARDWARE-005`, which already scopes exactly this.
* [ ] P9-HARDWARE-003 Decide whether missing/corrupt XGS/XSB/XWB constructors should remain soft stubs or throw XNA/FNA-compatible exceptions.
* [ ] P9-HARDWARE-004 If constructor behavior changes, update tests that currently lock in silent stub behavior.
* [ ] P9-HARDWARE-005 Add tests for no-audio-device behavior using SDL dummy/no-device configuration where feasible.
* [ ] P9-HARDWARE-006 Document backend behavior when audio hardware is unavailable.

## P9-DYNAMIC — DynamicSoundEffectInstance correctness

* [x] P9-DYNAMIC-001 Audit `PendingBufferCount` transitions across Play, Pause, Resume, Stop, and Dispose.
  *Note:* Compared CNA's `DynamicSoundEffectInstance.cpp`/`SoundEffectInstance.cpp` line-by-line
  against FNA's `DynamicSoundEffectInstance.cs`/`SoundEffectInstance.cs`. `PendingBufferCount ==
  queuedBuffers_.size() + submittedChunkSizes_.size()` is the direct architectural analogue of
  FNA's `queuedBuffers.Count` (staged-but-not-yet-pushed chunks + pushed-but-not-yet-consumed
  chunks, vs. FNA's single list serving both roles since FAudio's discrete buffer-queue model
  doesn't need CNA's continuous-`SDL_AudioStream` staging split). Found and fixed **two real
  bugs**:
  (1) `DynamicSoundEffectInstance::Play()` only called `Update()` from its "Stopped, start fresh
  playback" branch, *after* already returning early for the Paused/Playing cases -- but FNA's
  `Play()` calls `Update()` **unconditionally**, before dispatching on state at all ("Wait! What
  if we need moar buffers?" comment, `DynamicSoundEffectInstance.cs`). This only differs in one
  real scenario -- calling `Play()` redundantly while *already Playing* -- where FNA pumps
  `Update()` (submits freshly queued data, fires `BufferNeeded` if starved) and CNA silently
  skipped it entirely. (On the Stopped-from-fresh and Paused-resuming paths, `Update()` is
  provably a no-op either way, since its own guard requires `State == Playing`, which isn't true
  yet at the moment `Play()` calls it in either FNA or CNA -- so moving the call doesn't change
  those paths.) Fixed by moving the `Update()` call to run unconditionally at the top of `Play()`,
  matching FNA's exact structure. No dedicated regression test: proving this specific divergence
  deterministically would need either instrumenting `Update()`'s call count (more invasive than
  warranted for a call-ordering nicety with no observable `PendingBufferCount`-*value* effect) or
  a real elapsed-time buffer-consumption window between two `Play()` calls (the same
  timing-flakiness the project has already guarded against elsewhere, e.g. the "long wavebank"
  1-second-buffer fixture and `P9-BUILD-001..007`'s real DSP-filter data race). Low risk, verified
  via the existing suite staying green (see below).
  (2) A more serious, cleanly-testable bug: `DynamicSoundEffectInstance::Stop()` (the no-arg
  override) duplicated `Stop(bool immediate)`'s clearing logic directly, instead of delegating
  through it the way the base `SoundEffectInstance::Stop()` does (`{ Stop(true); }`, matching
  FNA's identical one-liner) -- so it **skipped** `Stop(bool)`'s existing "no active track yet ->
  no-op" guard. Calling the no-arg `Stop()` *directly* (not via `Stop(bool)`) on a never-played (or
  already-stopped) instance with staged buffers would still clear `PendingBufferCount` to 0 in
  CNA, where FNA's identical call (`Stop()` -> `Stop(true)` -> `handle == IntPtr.Zero` -> return)
  leaves it untouched. Fixed by renaming the unconditional-clearing logic to a private
  `StopInternal()` (called only from `Stop(bool immediate)`'s already-guarded immediate branch)
  and making the no-arg `Stop()` simply `{ Stop(true); }`, matching the base class and FNA exactly.
  `Dispose()`'s existing `Stop()` call is unaffected in the *normal* (already-played) case, and
  correctly becomes a real no-op for a never-played instance's staged buffers (matches FNA; no
  memory-leak concern for CNA either way, since `queuedBuffers_`/`submittedChunkSizes_` are RAII
  containers that free themselves on destruction regardless of whether `ClearBuffers()` ran).
  Verified via `git stash`: `StopDirectCallWhileNeverPlayedDoesNotClearPendingBuffers` fails
  (asserts `PendingBufferCount == 1` after a direct `Stop()` call, gets `0`) against the pre-fix
  code, confirming genuine dependency.
* [x] P9-DYNAMIC-002 Add tests for buffer completion while playing.
  *Note:* `SubmitBufferWhilePlayingIncrementsPendingBufferCount` (immediate +1, whether staged or
  handed straight to the stream) and `PendingBufferCountResetsToZeroAfterStopWhilePlaying`
  (real Stop() after Play() clears everything). The "not yet actually consumed" side was already
  covered pre-existing (`BufferNeededDoesNotFireWhenStreamHasEnoughData`, CP-4).
* [x] P9-DYNAMIC-003 Add tests for buffer completion while paused.
  *Note:* `PendingBufferCountUnaffectedAcrossPause` -- submits a buffer while Playing, pauses,
  and confirms the count is unchanged both immediately after `Pause()` and after `Resume()`
  (matches FNA: `Pause()`/`Resume()` never touch `queuedBuffers`, and `Update()` is itself gated
  on `State == Playing` so it can't spuriously decrement while paused).
* [x] P9-DYNAMIC-004 Add tests for Stop clearing or preserving buffers according to XNA/FNA behavior.
  *Note:* Three tests, each covering a distinct case found during the `P9-DYNAMIC-001` audit:
  `StopDirectCallWhileNeverPlayedDoesNotClearPendingBuffers` (the guarded no-op case, the actual
  regression test for the bug fixed above), `PendingBufferCountResetsToZeroAfterStopWhilePlaying`
  (real clear after real playback), `PendingBufferCountResetsToZeroAfterDisposeWhilePlaying`
  (`Dispose()`'s own `Stop()` call also clears once playback has actually started).
* [x] P9-DYNAMIC-005 Add tests for `BufferNeeded` event ordering.
  *Note:* `BufferNeededFiresExactlyTheStarvedCount` -- matches FNA's exact starvation loop
  (`for (i = MINIMUM_BUFFER_CHECK - PendingBufferCount; i > 0 && BufferNeeded != null; i -= 1)`)
  by asserting the *exact* raise count (2, given `tryStartHeadless`'s 1 pre-submitted buffer and
  `MINIMUM_BUFFER_CHECK == 3`), not just "fired at least once" like the pre-existing
  `BufferNeededFiresWhenStarved`.
* [x] P9-DYNAMIC-006 Add tests for multiple subscribers to `BufferNeeded`.
  *Note:* `BufferNeededFiresForEveryIndependentSubscriber` -- two independent lambda subscribers
  both observe every raise (equal, nonzero counts).
* [x] P9-DYNAMIC-007 Add tests for subscriber removal during callback.
  *Note:* Testing this exposed a **real, serious, cross-cutting bug** well beyond
  `DynamicSoundEffectInstance`: `System::EventHandler<T>::Raise()` (`sharp-runtime`, shared by
  every event in the whole framework, not just `BufferNeeded`) iterated its live handler vector
  directly. A handler that called `Remove()` on itself or another handler from within its own
  callback (a common "unsubscribe after first fire" pattern) mutated that same vector mid-loop:
  `erase()` shifts/destroys elements while the range-based for's cached `begin()`/`end()`
  iterators were still in use, dereferencing an already-destroyed `std::function` --
  **confirmed via an isolated standalone repro (outside the shared test binary, to avoid risking
  a process-wide crash) that this escaped as a real, uncaught `std::bad_function_call`**, not a
  theoretical concern. Presented this finding to the user (touching `sharp-runtime` needs sign-off
  per `CLAUDE.md`'s "don't touch the sibling repo" rule, and it was under concurrent development
  by another session at the time); the user asked for it to be fixed. Fixed in `sharp-runtime`
  (`include/System/EventHandler.hpp`, commit `8342a2c`) by taking a snapshot copy of `handlers_`
  before iterating in `Raise()`, matching real C# multicast delegate semantics: a handler that
  `Add()`s/`Remove()`s/`Clear()`s during `Raise()` only affects the *next* `Raise()` call, not the
  one in progress. Two new `sharp-runtime` tests (self-removal, removal of another not-yet-invoked
  handler) confirm the fix; that repo's own full suite (9075 tests) stayed green including the
  other session's concurrent, unrelated, uncommitted `DateTimeOffset.cpp` changes -- committed only
  the two `EventHandler` files, left the other session's file untouched, did not push (a
  sibling-repo shared branch under concurrent development). Back in CNA: added
  `BufferNeededSubscriberCanRemoveItselfDuringCallbackWithoutCrashing`
  (`DynamicSoundEffectInstanceTests.cpp`) -- a subscriber unsubscribes itself on first fire while
  another subscriber stays registered; confirms no crash/throw and the other subscriber still
  fires. CNA's own `CMakeLists.txt` builds `sharp-runtime` from the live sibling checkout
  (`add_subdirectory(../sharp-runtime SHARP_RUNTIME)`), so the fix took effect on CNA's next
  rebuild with no vendoring step needed. Full suite 3242/3244 (2 expected hardware skips, plus 1
  pre-existing unrelated timing self-skip under the slower ASan build), audio subset 335/335 (336
  under ASan, same 1 self-skip), clean under ASan+UBSan.
* [ ] P9-DYNAMIC-008 Audit dynamic stream format conversion for mono/stereo and byte/float paths.
* [ ] P9-DYNAMIC-009 Add tests for invalid buffer sizes and alignment.
  *Note (partial, pre-existing):* `SubmitBufferRangeThrows`/`SubmitFloatBufferRangeThrows`/
  `SubmitBufferRangeIntegerOverflowThrows` already cover invalid offset/count ranges; a dedicated
  audit of non-block-aligned byte counts (e.g. an odd byte count for 16-bit stereo) hasn't been
  done and is left open.

## P9-DOCS — Documentation synchronization

* [x] P9-DOCS-001 Update `AUDIT.md` audio rows so they no longer describe implemented features as stubs.
  *Note:* Rewrote every row in `AUDIT.md`'s Audio table — several referenced `T-4D`/`T-4B`/`T-3F` as still-open/stubbed ("3D pan/attenuation still stubbed", "Apply3D is a no-op", "3D PlayCue ignores listener/emitter", "streaming ctor still delegates to full in-memory load"), all of which were actually completed earlier this branch. Added a "last synchronized" pointer to `docs/xna-4-api-coverage.md` for the full compatibility table.
* [x] P9-DOCS-002 Update `docs/xna-4-api-coverage.md` for current Audio coverage.
  *Note:* This document predated essentially all of this branch's audio work (dated 2026-06-26, referencing Tasks 197-199) — its Audio section claimed `AudioEngine`/`SoundBank`/`WaveBank`/`Cue` "XACT audio is unimplemented" and `Microphone`/`DynamicSoundEffectInstance` as "backends are stub". Rewrote the section, the namespace-coverage table row, the §8 coverage-estimate row (~0%→~90%, removed "XACT" from the overall gap list), §10's recommended-order entry, and §11's "what remains missing"/"recommended next steps" (removed the now-false "Implement XACT audio" action item).
* [x] P9-DOCS-003 Update `docs/coverage.md` for current Audio/XACT/Microphone status.
  *Note:* This whole-project static-analysis doc (dated 2026-06-21) had `Framework.Audio` at "~70% functional... AudioEngine/Cue/WaveBank/SoundBank partial stubs; Microphone stub-only" and a "Biggest gaps" table entry "XACT audio runtime — ~30%"/"Microphone — ~10%, no SDL audio capture wired". Updated the audio-specific rows/paragraphs only (Framework.Audio row, the two "Biggest gaps" rows, the justification paragraph) to ~90%/~95% with a 2026-07-04 note — left all non-audio rows (Graphics/Media/Content/etc.) untouched since they're outside this task's scope and unverified by this session.
* [x] P9-DOCS-004 Update `NEXT.md` so it does not claim both "all audio tasks complete" and stale pending/uncommitted Phase 8 status.
  *Note:* Already satisfied — `NEXT.md` has been kept continuously in sync with each Fáze 9 sub-phase's actual completion state throughout this session (see its own git history this session), explicitly distinguishing "Fáze 7/8 fully complete" from "Fáze 9 still open" rather than conflating them. No contradictory claim found on re-check.
* [x] P9-DOCS-005 Add a concise Audio compatibility table: implemented, approximate, intentionally unsupported, not yet implemented.
  *Note:* Added to `docs/xna-4-api-coverage.md`'s Audio section — a single 4-row table (Implemented / Approximate / Intentionally unsupported / Not yet implemented / open decision) summarizing every deviation already itemized in `CHECKLIST.md`, for an at-a-glance answer without reading every `CHECKLIST.md` row.
* [x] P9-DOCS-006 Document SDL3/SDL_mixer backend limitations versus FAudio/FACT.
  *Note:* Added a dedicated subsection to `docs/xna-4-api-coverage.md` tracing every approximate/unsupported behavior back to its specific SDL3_mixer architectural limitation (no `F3DAudio` equivalent, no per-source Doppler, no aux-send bus, single per-track cooked-callback slot, 2-value stereo gain instead of a 4-coefficient matrix, single max-frame loop property instead of `LoopBegin`/`LoopLength`) plus the one case where SDL3_mixer's model is actually simpler/better (global mixer gain, `CP-16`).
* [x] P9-DOCS-007 Document which behavior is intended to match FNA and which behavior is a CNA-specific compatibility compromise.
  *Note:* Added a subsection distinguishing the two categories explicitly: (1) permanent SDL3_mixer-forced compromises (documented in `CHECKLIST.md`, not bugs to fix), vs (2) Fáze 9's own fixes, which made CNA *more* faithful to FNA (not new compromises) — including the two cases (`Cue::GetVariable`/`SetVariable`/`Resume()` after Dispose) where CNA deliberately throws instead of replicating an FNA/FAudio native-crash bug.

## P9-BUILD — Reproducible build and test workflow

* [x] P9-BUILD-001 Ensure a clean checkout can configure audio tests with documented dependencies.
  *Note:* Verified with a fresh `cmake-build-tests/` directory (via the new `tests` preset, see `P9-BUILD-002`): configure + build + full test run all succeed from scratch. Dependencies documented in `P9-BUILD-004`.
* [x] P9-BUILD-002 Add or document a native desktop CMake preset for running tests outside the Emscripten/web preset.
  *Note:* `CMakePresets.json` had exactly one preset (`web`, Emscripten-only) — no native desktop preset existed at all. Added `configurePresets`/`buildPresets` entries named `tests` (EasyGL backend, Debug, `CNA_BUILD_TESTS`/`CNA_BUILD_EXAMPLES` on, binary dir `cmake-build-tests/`). Verified end-to-end: `cmake --preset tests && cmake --build --preset tests --target CnaTests` then running the binary directly, from a freshly-deleted build directory.
* [x] P9-BUILD-003 Ensure missing vendored dependencies produce a clear error message.
  *Note:* Already implemented (`cmake/ThirdPartySDL.cmake`): `message(FATAL_ERROR "Missing vendored '<dep>' in <path>. Run: git submodule update --init --recursive")` for each of SDL/SDL_image/SDL_mixer. Verified empirically (reversibly): temporarily renamed `third_party/SDL` away, reconfigured, confirmed the exact FATAL_ERROR message above, renamed it back, confirmed `git submodule status` unchanged.
* [x] P9-BUILD-004 Document whether SDL/SDL_mixer/googletest are expected as submodules, vendored source, or system packages.
  *Note:* Documented in `NEXT.md` §7: SDL/SDL_image/SDL_mixer are git submodules under `third_party/` (built from source into the persistent `.sdl-prebuilt/` cache on first configure; `-DCNA_USE_SYSTEM_SDL=ON` switches to system packages via `find_package` instead). `googletest` is a git submodule under `vendor/`, always built from source via `add_subdirectory` — no system-package option exists for it.
* [x] P9-BUILD-005 Add a minimal command sequence to build and run audio tests locally.
  *Note:* Already present in `NEXT.md` §7 (manual `cmake -B .../-DCNA_BUILD_TESTS=ON` form, predates Fáze 9); added the shorter preset-based equivalent (`cmake --preset tests && cmake --build --preset tests --target CnaTests && SDL_AUDIODRIVER=dummy ./cmake-build-tests/CnaTests`) alongside it.
* [x] P9-BUILD-006 Ensure CI or local test docs mention the SDL dummy audio driver setup.
  *Note:* Already extensively documented in `NEXT.md` §7 and used throughout every Fáze 7/8/9 test this session; also called out explicitly in the new `tests` preset's own `description` field in `CMakePresets.json`.
* [x] P9-BUILD-007 Verify that all audio tests pass from a clean checkout.
  *Note:* **Found and fixed a real, reproducible data race while verifying this.** 3 `SoundEffectInstanceTest` filter tests (`LowPassFilterConvergesToUnityGainForConstantSignal`, `HighPassFilterConvergesToZeroForConstantSignal`, `LowPassFilterSurvivesMoveConstruction`, plus for full correctness the 3 single-sample-transient filter tests) call `Play()` before running many manual `ProcessFilterSamplesForTest()` calls -- `Play()` is required so `INTERNAL_apply*Filter` has a live `track_` to attach the real SDL3_mixer cooked callback to, but that same `Play()` call also starts the real background mixing thread (even under the SDL dummy driver, which simulates real-time playback via its own timer thread), which then periodically invokes that SAME real callback concurrently with the test's synchronous filter-processing calls -- a genuine unsynchronized read-modify-write race on the shared `FilterState` (`yl`/`yb`). Confirmed via direct repeated invocation (not just via `ctest`): ~15-25% failure rate over repeated runs, given deterministic math that (verified via an isolated single-threaded reproduction) reliably converges by iteration ~40 of 2000 with no marginal-stability issue. Fixed: all 6 affected tests now call `inst.Stop(true)` immediately after applying the filter and before the first `ProcessFilterSamplesForTest()` call -- `Stop()` halts the real track without touching `filterState_` (only `Dispose()` does), so every subsequent call runs safely single-threaded. Stress-tested 40+ consecutive isolated runs and 5+ consecutive full-suite runs post-fix with zero failures. Also separately confirmed (via `ctest`'s one-process-per-test-case model, not used as the project's primary test-running method — see `CMakePresets.json`'s `tests` preset description) a second, unrelated issue: several tests share hardcoded `/tmp/cna_*_test/` fixture paths, which is safe for the single-process full-suite run this project has always used but not safe under `ctest`'s default parallelism across independent processes -- documented as a `ctest`-mode-specific caveat, not fixed (out of scope: would need per-process-unique temp paths across dozens of test fixture builders). Full suite (2090/2090) verified clean and reliable via the new `tests` preset from a freshly-deleted build directory.
