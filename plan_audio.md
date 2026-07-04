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
* [ ] P9-LIFECYCLE-013 Audit `Cue::Pause()`, `Cue::Resume()`, and `Cue::Stop()` behavior when called in Stopped, Playing, Paused, and Disposed states.
  *Note:* Partially explored as a side effect of P9-LIFECYCLE-010: real FACT leaves `FACT_STATE_PLAYING` set even while `PAUSED` (`IsPlaying` and `IsPaused` can both be true at once in XNA/FNA!) — CNA has `State` as a mutually exclusive enum, so `IsPlaying`/`IsPaused` are always disjoint here. This is a documented deviation, but fixing it would be invasive (changes the semantics of `PauseCategoryInternal`/`ResumeCategoryInternal` and a number of tests) — deferred to a full audit of this item, not implemented in this pass.
* [ ] P9-LIFECYCLE-014 Align disposed-state behavior for `Cue` methods with XNA/FNA and add tests.
* [ ] P9-LIFECYCLE-015 Ensure `Cue::GetVariable()` and `Cue::SetVariable()` handle disposed cues consistently with XNA/FNA.

## P9-STOP — Stop semantics and authored stop behavior

* [ ] P9-STOP-001 Audit current `Cue::Stop(AudioStopOptions::Immediate)` behavior against XNA/FNA.
* [ ] P9-STOP-002 Audit current `Cue::Stop(AudioStopOptions::AsAuthored)` behavior against XNA/FNA.
* [ ] P9-STOP-003 Do not mark a cue as fully inactive while authored stop tails/fades are still active.
* [ ] P9-STOP-004 Introduce explicit internal tracking for stopping/tail state if needed.
* [ ] P9-STOP-005 Ensure wave bank and sound bank in-use tracking remains true while authored stop tails are still active.
* [ ] P9-STOP-006 Add tests for `Stop(Immediate)` state transitions.
* [ ] P9-STOP-007 Add tests for `Stop(AsAuthored)` state transitions.
* [ ] P9-STOP-008 Add tests for category stop with authored stop behavior.
* [ ] P9-STOP-009 Verify that stopping a cue unregisters from `AudioEngine` only when it is actually finished.
* [ ] P9-STOP-010 Document any remaining deviation from exact XACT authored stop behavior.

## P9-CATEGORY — AudioCategory correctness

* [ ] P9-CATEGORY-001 Fix category operations so they iterate over a snapshot of active cues instead of mutating `activeCues` during iteration.
* [ ] P9-CATEGORY-002 Add regression tests for stopping multiple active cues in the same category.
* [ ] P9-CATEGORY-003 Add regression tests for pausing and resuming multiple active cues in the same category.
* [ ] P9-CATEGORY-004 Add regression tests for changing category volume while cues are active.
* [ ] P9-CATEGORY-005 Implement XACT category `instanceLimit` if enough parsed data is already available.
* [ ] P9-CATEGORY-006 Add tests for category instance limits using synthetic/minimal XACT data or direct internal fixtures.
* [ ] P9-CATEGORY-007 Implement category fade-in behavior where feasible.
* [ ] P9-CATEGORY-008 Implement category fade-out behavior where feasible.
* [ ] P9-CATEGORY-009 Add tests for category fade-in/fade-out behavior.
* [ ] P9-CATEGORY-010 Clearly document any category behavior that remains approximate.

## P9-VALIDATION — Constructor and argument validation

* [ ] P9-VALIDATION-001 Audit all `SoundEffect` constructors against XNA/FNA argument validation.
* [ ] P9-VALIDATION-002 Fix `SoundEffect` buffer/range constructor validation for negative loop start and loop length.
* [ ] P9-VALIDATION-003 Fix `SoundEffect` buffer/range constructor validation for offset/count overflow.
* [ ] P9-VALIDATION-004 Fix `SoundEffect` buffer/range constructor validation for invalid channel count.
* [ ] P9-VALIDATION-005 Fix `SoundEffect` buffer/range constructor validation for invalid sample rate.
* [ ] P9-VALIDATION-006 Add tests for invalid `SoundEffect` buffer constructor arguments.
* [ ] P9-VALIDATION-007 Audit `DynamicSoundEffectInstance` constructor validation for sample rate and `AudioChannels`.
* [ ] P9-VALIDATION-008 Fix `DynamicSoundEffectInstance` constructor validation to match XNA/FNA.
* [ ] P9-VALIDATION-009 Add tests for invalid `DynamicSoundEffectInstance` constructor arguments.
* [ ] P9-VALIDATION-010 Audit `SubmitBuffer`, `SubmitFloatBufferEXT`, `Play`, `Pause`, `Resume`, and `Stop` after `Dispose`.
* [ ] P9-VALIDATION-011 Ensure `DynamicSoundEffectInstance::SubmitBuffer` cannot queue buffers after disposal.
* [ ] P9-VALIDATION-012 Ensure `SoundEffect::CreateInstance()` handles disposed `SoundEffect` consistently with XNA/FNA.
* [ ] P9-VALIDATION-013 Add tests for disposed `SoundEffect::CreateInstance()`.
* [ ] P9-VALIDATION-014 Audit `SoundEffect::FromStream()` ownership and exception behavior.
* [ ] P9-VALIDATION-015 Add tests for invalid and empty streams where feasible.

## P9-XACT — XACT cue behavior fidelity

* [ ] P9-XACT-001 Audit XSB cue variation parsing against FNA.
* [ ] P9-XACT-002 Preserve parsed interactive variation variable ranges instead of falling back to uniform random selection.
* [ ] P9-XACT-003 Implement interactive variation selection based on cue variables where feasible.
* [ ] P9-XACT-004 Add tests for variable-driven interactive variation selection.
* [ ] P9-XACT-005 Audit XACT RPC parsing and currently unused runtime data.
* [ ] P9-XACT-006 Wire simple RPC volume changes into cue playback where feasible.
* [ ] P9-XACT-007 Wire simple RPC pitch changes into cue playback where feasible.
* [ ] P9-XACT-008 Add tests for XACT variable-to-volume RPC behavior.
* [ ] P9-XACT-009 Add tests for XACT variable-to-pitch RPC behavior.
* [ ] P9-XACT-010 Audit DSP/filter parsing and runtime application.
* [ ] P9-XACT-011 Wire parsed low-pass/high-pass/band-pass filters to `SoundEffectInstance` where feasible.
* [ ] P9-XACT-012 Keep reverb as documented no-op only if faithful implementation is not feasible with current backend.
* [ ] P9-XACT-013 Document exact XACT DSP features supported and unsupported.
* [ ] P9-XACT-014 Ensure missing wave, missing sound, and invalid cue index behavior matches XNA/FNA as closely as possible.
* [ ] P9-XACT-015 Add tests for missing wave/cue behavior.

## P9-3D — 3D audio fidelity

* [ ] P9-3D-001 Audit `Apply3D` behavior against FNA for mono and stereo sources.
* [ ] P9-3D-002 Fix or document stereo panning behavior. Avoid hard stereo pan if FNA uses crossfeed or another model.
* [ ] P9-3D-003 Audit distance attenuation behavior against `SoundEffect.DistanceScale`.
* [ ] P9-3D-004 Audit doppler behavior against `SoundEffect.DopplerScale` and `SoundEffect.SpeedOfSound`.
* [ ] P9-3D-005 Implement doppler pitch adjustment if feasible.
* [ ] P9-3D-006 Add tests for distance attenuation.
* [ ] P9-3D-007 Add tests for panning left/right based on listener/emitter orientation.
* [ ] P9-3D-008 Add tests for doppler behavior if implemented.
* [ ] P9-3D-009 Document remaining limitations of CNA 3D audio compared to XNA/FNA.

## P9-HARDWARE — Audio hardware and exception behavior

* [ ] P9-HARDWARE-001 Audit `NoAudioHardwareException` usage across the audio backend.
* [ ] P9-HARDWARE-002 Replace raw `std::runtime_error` backend initialization failures with XNA-compatible exception behavior where appropriate.
* [ ] P9-HARDWARE-003 Decide whether missing/corrupt XGS/XSB/XWB constructors should remain soft stubs or throw XNA/FNA-compatible exceptions.
* [ ] P9-HARDWARE-004 If constructor behavior changes, update tests that currently lock in silent stub behavior.
* [ ] P9-HARDWARE-005 Add tests for no-audio-device behavior using SDL dummy/no-device configuration where feasible.
* [ ] P9-HARDWARE-006 Document backend behavior when audio hardware is unavailable.

## P9-DYNAMIC — DynamicSoundEffectInstance correctness

* [ ] P9-DYNAMIC-001 Audit `PendingBufferCount` transitions across Play, Pause, Resume, Stop, and Dispose.
* [ ] P9-DYNAMIC-002 Add tests for buffer completion while playing.
* [ ] P9-DYNAMIC-003 Add tests for buffer completion while paused.
* [ ] P9-DYNAMIC-004 Add tests for Stop clearing or preserving buffers according to XNA/FNA behavior.
* [ ] P9-DYNAMIC-005 Add tests for `BufferNeeded` event ordering.
* [ ] P9-DYNAMIC-006 Add tests for multiple subscribers to `BufferNeeded`.
* [ ] P9-DYNAMIC-007 Add tests for subscriber removal during callback.
* [ ] P9-DYNAMIC-008 Audit dynamic stream format conversion for mono/stereo and byte/float paths.
* [ ] P9-DYNAMIC-009 Add tests for invalid buffer sizes and alignment.

## P9-DOCS — Documentation synchronization

* [ ] P9-DOCS-001 Update `AUDIT.md` audio rows so they no longer describe implemented features as stubs.
* [ ] P9-DOCS-002 Update `docs/xna-4-api-coverage.md` for current Audio coverage.
* [ ] P9-DOCS-003 Update `docs/coverage.md` for current Audio/XACT/Microphone status.
* [ ] P9-DOCS-004 Update `NEXT.md` so it does not claim both "all audio tasks complete" and stale pending/uncommitted Phase 8 status.
* [ ] P9-DOCS-005 Add a concise Audio compatibility table: implemented, approximate, intentionally unsupported, not yet implemented.
* [ ] P9-DOCS-006 Document SDL3/SDL_mixer backend limitations versus FAudio/FACT.
* [ ] P9-DOCS-007 Document which behavior is intended to match FNA and which behavior is a CNA-specific compatibility compromise.

## P9-BUILD — Reproducible build and test workflow

* [ ] P9-BUILD-001 Ensure a clean checkout can configure audio tests with documented dependencies.
* [ ] P9-BUILD-002 Add or document a native desktop CMake preset for running tests outside the Emscripten/web preset.
* [ ] P9-BUILD-003 Ensure missing vendored dependencies produce a clear error message.
* [ ] P9-BUILD-004 Document whether SDL/SDL_mixer/googletest are expected as submodules, vendored source, or system packages.
* [ ] P9-BUILD-005 Add a minimal command sequence to build and run audio tests locally.
* [ ] P9-BUILD-006 Ensure CI or local test docs mention the SDL dummy audio driver setup.
* [ ] P9-BUILD-007 Verify that all audio tests pass from a clean checkout.
