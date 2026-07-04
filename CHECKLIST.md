# CNA Port Checklist

Use this checklist for every `.cs` file ported from FNA to CNA.

---

## Per-file checklist

### Headers / boilerplate
- [ ] `// SPDX-License-Identifier: MS-PL` present in `.hpp`
- [ ] `// SPDX-License-Identifier: MS-PL` present in `.cpp`
- [ ] `#include "CNA/CNAHelper.hpp"` present in `.hpp` if `NOXNA` is used anywhere

### Doxygen documentation
- [ ] Every public method, constructor, operator, property getter/setter, and constant in the `.hpp` has a `/** @brief … */` Doxygen block comment
- [ ] Methods with parameters have `@param` for each parameter
- [ ] Non-void methods have `@return`
- [ ] No public member is left undocumented
- [ ] No bare `///` comments on public API declarations (only `/** */` blocks allowed)

### API surface (compare line-by-line with FNA source)
- [ ] All public fields / constants present
- [ ] All public properties mapped to `getXProperty()` / `setXProperty()`
- [ ] All public methods present with correct signatures
- [ ] All public static methods present
- [ ] All events present as `System::EventHandler<TArgs>` fields
- [ ] All `ref`/`out` overloads present as value-ref pairs
- [ ] `operator==` / `operator!=` present if FNA defines them

### Inheritance
- [ ] All interfaces from FNA implemented (e.g. `IEquatable<T>` → `System::IEquatable<T>`, `IComparable<T>` → `System::IComparable<T>`, `IDisposable` → `System::IDisposable`)
- [ ] Methods that implement interfaces have `override` keyword

### NOXNA markers
- [ ] Every method / field / type alias **not** in the XNA 4.0 API surface is marked `NOXNA`
- [ ] C++ iterator support (`begin`, `end`, `size_type`, …) marked `NOXNA`
- [ ] `GetTypeName()` marked `NOXNA` (applies to classes that inherit `System::Object`)

### GetTypeName()
- [ ] Concrete classes that inherit `System::Object` override `GetTypeName()` with `NOXNA`
- [ ] Return value is the fully-qualified .NET name, e.g. `"Microsoft.Xna.Framework.Foo"`

### Logic verification (method by method vs FNA)
- [ ] Each method body compared line-by-line with the FNA equivalent
- [ ] Every intentional deviation from FNA logic has a `//` comment explaining why
- [ ] Null/range guard differences between C# and C++ documented where relevant

### Tests
- [ ] Test file exists at `tests/Microsoft/Xna/Framework/<ClassName>Tests.cpp`
- [ ] Every public method has at least one test
- [ ] Edge cases covered: boundary values, empty collections, null/nullptr inputs where applicable
- [ ] `ref`/`out` overloads tested separately
- [ ] Event firing verified with a lambda subscriber
- [ ] `GetHashCode()`: equal objects → equal hash; different objects → (typically) different hash
- [ ] `ToString()`: format spot-checked against FNA output

### Classes that cannot be unit-tested
If the class depends on `Game` / SDL / graphics backend, document it and skip tests:
- [ ] Add comment in test suite directory or `// No tests: requires SDL/Game` at the top of a stub file

---

## Known acceptable C++ deviations from FNA/XNA

| Deviation | Reason |
|---|---|
| `GetHashCode()` returns `std::size_t` instead of `int` | C++ hash size is platform-native |
| `ref`/`out` params become value-reference pairs | No C# ref/out in C++ |
| `IEnumerable<T>` replaced by `begin()`/`end()` (NOXNA) | C++ iterator idiom |
| `Type`-based service lookup uses `typeid` / templates | No C# reflection |
| Type-assignability check in `AddService` omitted | No runtime reflection in C++ |
| `Equals(object obj)` override omitted | No `object` base in C++ structs/value types |
| `DeviceCreated`/`DeviceDisposing` event hookup simplified | Service always available in CNA |
| `IsAssignableFrom` check in `GameServiceContainer` omitted | No runtime reflection |
| C# `internal set` mapped to `private` + `friend class <OneSpecificClass>` (e.g. `Microsoft::Devices::Sensors::AccelerometerReading`'s setters, friended to `Accelerometer` only) | C++ `friend` is per-named-class, not assembly-scoped like C#'s `internal` — narrower than the real API but the closest available mechanism; acceptable since each reading type has exactly one producing sensor class |
| Audio: 3D positional audio is pan + distance-attenuation only, no elevation (`Mix_SetPosition`) | SDL_mixer has no FAudio F3DAudio equivalent |
| Audio: `DopplerScale`/`Velocity` are stored but never applied to pitch | SDL_mixer has no per-source velocity-based pitch shift |
| Audio: `GetHashCode()` uses `std::hash` on the category/cue name, doesn't match C# `String.GetHashCode()` | Platform C++ hash instead of .NET's algorithm; internal consistency preserved |
| Audio: streaming `WaveBank` ctor's `offset`/`packetSize` parameters are unused | Matches FNA's own `WaveBank.cs`, which never forwards them to `FACTStreamingParameters` either (only `.file` is set); real per-entry lazy disk reads are implemented (`plan_audio.md` T-3F) |
| Audio: `SoundEffect` is move-only (copy ctor/assignment deleted) | Required so its instance-tracking + Dispose-cascade (matching FNA's `SoundEffect.Instances`) has a single, unambiguous owner per resource (`plan_audio.md` T-3G) |
| Audio: `ContentManager::Load<Audio::SoundEffect>()` never caches instances (always a fresh load) | `SoundEffect`'s move-only, per-owner Dispose-cascade semantics make cross-caller sharing actively wrong, not just impossible (`plan_audio.md` T-3G) |
| Audio: `SoundEffectInstance::INTERNAL_applyReverb` is a documented no-op | SDL3_mixer has no aux-send/return bus (no equivalent to FAudio's shared `ReverbVoice`); low/high/band-pass filters are implemented for real via a state-variable filter run in an SDL3_mixer per-track callback (`plan_audio.md` T-4C) |
| Audio: a bounded loop region (`loopStart`/`loopLength`) truncates the *entire* track (including the first, pre-loop playthrough) at `loopStart+loopLength`, not just subsequent loop iterations | SDL3_mixer's `MIX_PROP_PLAY_MAX_FRAME_NUMBER` has no per-iteration distinction, unlike FNA/XAudio2's `LoopBegin`/`LoopLength`; the loop-start point itself (`MIX_PROP_PLAY_LOOP_START_FRAME_NUMBER`) is applied correctly (`plan_audio.md` CP-17) |
| Audio: hard-panning a **stereo** source (`Pan` = ±1) eliminates the opposite channel entirely instead of crossfeed-blending it (mono sources are bit-exact vs FNA) | `MIX_SetTrackStereo` only takes a per-channel gain pair, no 4-coefficient crossfeed matrix; a real fix would need to share SDL3_mixer's single per-track "cooked callback" slot with the already-shipped T-4C DSP filter, a real regression risk to already-tested filter code for a cosmetic panning difference (`plan_audio.md` CP-19) |
| Audio: XACT category `instanceLimit`/`fadeInMS`/`fadeOutMS` are parsed but never enforced/applied | No consumer implemented for category instance-limiting or category-level fade in/out; deliberately deferred at `T-4D` but never previously recorded here (`plan_audio.md` XA-11) |
| Audio: `AudioEngine`/`SoundBank`/`WaveBank` constructors silently swallow a missing file or a parse error (`cerr` only) and leave the object in a "stub" state, instead of throwing a `System::` exception; `AudioEngine` never throws `NoAudioHardwareException` (it always reports exactly one renderer, so the check can never fail) | Throwing instead would require rewriting the `SharedEngine()` helper (independently defined in `CueTests.cpp`/`WaveBankTests.cpp`/`SoundBankTests.cpp`/`AudioCategoryTests.cpp`), which deliberately points at a nonexistent `.xgs` path today and would need a real fixture instead — a wide, cross-cutting change to the shared foundation ~80+ existing tests build on, for a corner case (missing/corrupt content files, absent audio hardware) rather than a user-visible playback bug (`plan_audio.md` CP-18, XA-9) |
| Audio: `Cue::IsPlaying` and `Cue::IsPaused` are mutually exclusive (a cue is never both at once) | Real FACT keeps the `PLAYING` bit set while `PAUSED` (pausing never clears it), so both can be `true` simultaneously in real XNA/FNA; CNA models `Cue::State` as a single mutually-exclusive enum. Found during the `plan_audio.md` `P9-LIFECYCLE-013` audit; fixing it would ripple into `AudioEngine::PauseCategoryInternal`/`ResumeCategoryInternal` and a number of already-passing tests that assume disjoint `IsPlaying`/`IsPaused` — flagged as a decision pending user input, not yet fixed or formally accepted (`plan_audio.md` P9-LIFECYCLE-013) |
| Audio: `Cue::Stop(AsAuthored)`'s release-tail duration is however long the underlying wave naturally takes to finish (or until `Stop(Immediate)`/`Dispose()`), not the authored `fadeOutMS`/RPC-release timing from the `.xsb` | `XactParser` doesn't retain per-cue `fadeOutMS`/`instanceLimit`/`maxInstanceBehavior` into `XsbCue` at all (they're skipped over as dead bytes while parsing a COMPLEX cue's header, only needed to locate later fields); real FACT (`FACTCue_Stop`, `FACT.c`) begins an actual timed fade-out or RPC-driven release and only reaches `FACT_STATE_STOPPED` once that completes. CNA's `State::Stopping` (`P9-STOP-003/004`) correctly leaves the cue non-stopped while its wave(s) are still audible, which is the right *shape* of behavior, but the *duration* comes from the wave's own remaining length, not an authored fade curve — a cue whose only audio has already finished before `Stop(AsAuthored)` is called reconciles to `Stopped` immediately, with no fade at all |
| Audio: XACT RPC (Runtime Parameter Control) volume/pitch curves are evaluated once, at `Cue::Play()` time, against the bound variable's value at that instant, instead of being continuously re-evaluated every engine tick while the cue plays | Real FACT (`FACT_INTERNAL_UpdateRPCs`/`FACT_INTERNAL_UpdateEngine`, `FACT_internal.c`) re-evaluates every active track's bound RPC curves on every `FACTAudioEngine_DoWork` tick, so a variable that changes mid-playback (or the built-in "AttackTime"/"ReleaseTime" envelope variables, which track elapsed play/release time rather than a stored value) continuously updates volume/pitch; CNA has no per-frame `Cue` update tick at all (`AudioEngine::Update()` only sweeps fire-and-forget cues, `P9-LIFECYCLE-008/009`), so a live-updating implementation is out of scope. `AttackTime`/`ReleaseTime` and RPCs targeting a DSP preset parameter (`parameter >= RPC_PARAMETER_COUNT`) are unsupported outright — CNA has no elapsed-playback-time tracking and no DSP preset system (`plan_audio.md` P9-XACT-005/006/007) |
