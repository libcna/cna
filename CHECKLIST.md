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
| Audio: 3D positional audio is pan + distance-attenuation + Doppler pitch shift only, no elevation (`Mix_SetPosition`) or true HRTF | SDL_mixer has no FAudio F3DAudio equivalent for elevation/HRTF; distance attenuation and Doppler are both real, exact closed-form computations (`plan_audio.md` P9-3D-003/004/005) needing no native 3D audio API, unlike elevation/HRTF |
| Audio: `Apply3D`'s pan approximation now projects the emitter's relative position onto the listener's own right axis (`Cross(Forward, Up)`, normalized, via `SoundEffectInstance::INTERNAL_calculateListenerRight()`) instead of raw world-space X displacement, so turning the listener changes which side an emitter pans to, matching real X3DAudio's own `listenerBasis.right` projection (`F3DAudio.c`'s `ComputeEmitterChannelCoefficients`) | For the default orientation (`Forward=(0,0,-1)`, `Up=(0,1,0)`), `Cross(Forward, Up)` reduces to exactly `Vector3.Right`, so this is a strict generalization of the old world-X-only approximation, not a behavior change for an unrotated listener (the common case). Still an approximation, not a port of X3DAudio's full multi-speaker energy-diffusion/azimuth pipeline (no equivalent in SDL3_mixer's single stereo-gain-pair model, `CP-19`) -- only the **listener's** orientation is used; the **emitter's** own `Forward`/`Up` remain unread, matching real X3DAudio too (emitter orientation only affects multi-channel emitter configurations there, not the pan of CNA's mono point-source approximation) (`plan_audio.md` P9-3D-010) |
| Audio: `GetHashCode()` uses `std::hash` on the category/cue name, doesn't match C# `String.GetHashCode()` | Platform C++ hash instead of .NET's algorithm; internal consistency preserved |
| Audio: streaming `WaveBank` ctor's `offset`/`packetSize` parameters are unused | Matches FNA's own `WaveBank.cs`, which never forwards them to `FACTStreamingParameters` either (only `.file` is set); real per-entry lazy disk reads are implemented (`plan_audio.md` T-3F) |
| Audio: `SoundEffect` is move-only (copy ctor/assignment deleted) | Required so its instance-tracking + Dispose-cascade (matching FNA's `SoundEffect.Instances`) has a single, unambiguous owner per resource (`plan_audio.md` T-3G) |
| Audio: `ContentManager::Load<Audio::SoundEffect>()` never caches instances (always a fresh load) | `SoundEffect`'s move-only, per-owner Dispose-cascade semantics make cross-caller sharing actively wrong, not just impossible (`plan_audio.md` T-3G) |
| Audio: `SoundEffectInstance::INTERNAL_applyReverb` is a documented no-op | SDL3_mixer has no aux-send/return bus (no equivalent to FAudio's shared `ReverbVoice`); low/high/band-pass filters are implemented for real via a state-variable filter run in an SDL3_mixer per-track callback (`plan_audio.md` T-4C) |
| Audio: hard-panning a **stereo** source (`Pan` = ±1, or `Apply3D` with the emitter hard left/right) eliminates the opposite channel entirely instead of crossfeed-blending it (mono sources are bit-exact vs FNA either way) | `MIX_SetTrackStereo` only takes a per-channel gain pair, no 4-coefficient crossfeed matrix; a real fix would need to share SDL3_mixer's single per-track "cooked callback" slot with the already-shipped T-4C DSP filter, a real regression risk to already-tested filter code for a cosmetic panning difference. `Apply3D` reaches this exact same formula (`ApplyTrackProperties`) as the `Pan` property setter -- confirmed by the `P9-3D-001` audit -- unlike FNA, where `Apply3D` uses a completely different, channel-count-aware native X3DAudio computation instead of the simplified `SetPanMatrixCoefficients` the `Pan` setter uses (`plan_audio.md` CP-19, P9-3D-001) |
| Audio: XACT category `instanceLimit`/`maxInstanceBehavior`/`fadeInMS`/`fadeOutMS` are now enforced for real (`AudioEngine::CheckCategoryInstanceLimit()`, called from `Cue::Play()`) -- `FAIL` rejects the new cue outright; `REPLACE_LOWEST_PRIORITY` evicts the active same-category cue with the lowest `XsbSound::priority`; `QUEUE`/`REPLACE_OLDEST`/`REPLACE_QUIETEST` are all treated as "evict the oldest active cue in the category" | Real FAudio's own `handle_instance_limit()` (`FACT_internal.c`) carries a `FIXME: How does QUEUE differ from REPLACE_OLDEST?` comment and treats both identically, and its `REPLACE_QUIETEST` branch is an unfinished stub that (despite the name) just keeps overwriting the victim with whatever cue it last saw -- i.e. it also behaves like `REPLACE_OLDEST` in practice. CNA matches FAudio's real shipped behavior rather than implementing a "more correct" quietest-by-volume search FAudio itself never does. Category fade in/out is applied exactly where real FACT applies it -- only as part of this instance-limit replacement (a fading victim via `Cue::ForceFadeOutForInstanceLimit(category.fadeOutMS)`, the new cue fading in via `category.fadeInMS`, both reusing the `Cue::ReconcileState()` wall-clock ramp `P9-STOP-010` already added) -- `AudioCategory::Pause/Resume/SetVolume/Stop` remain instantaneous with no fade, matching real FACT exactly (category `fadeInMS`/`fadeOutMS` are never referenced anywhere else in `FACT_internal.c`/`FACT.c`). Cue-level `instanceLimit`/`fadeInMS`/`fadeOutMS`/`maxInstanceBehavior` (from a *complex* `.xsb` cue's own fields) are now also enforced for real, via `AudioEngine::CheckCueInstanceLimit()`, called from `Cue::Play()` *before* the category-level check above, matching `FACT_internal.c`'s `play_sound()` order exactly (`plan_audio.md` P9-CATEGORY-011) |
| Audio: a cue-level `instanceLimit` eviction (`AudioEngine::CheckCueInstanceLimit()`) picks its victim from *every* live cue in the whole `SoundBank`, with no filter by category or by the triggering cue's own definition -- so it can evict a completely unrelated cue instead of another instance of the same named cue | Matches `FACT_internal.c`'s `handle_instance_limit(cue, NULL)` exactly: its victim-search loop only ever filters by category when a non-NULL category is passed in, which never happens for a cue-level check -- this looks like a genuine oversight in FAudio itself (a cue-level instanceLimit conceptually ought to only compete against other instances of the same named cue), but CNA replicates it exactly rather than "fixing" upstream FAudio's own shipped behavior (`plan_audio.md` P9-CATEGORY-011) |
| Audio: `AudioEngine` never throws `NoAudioHardwareException` from its own constructor (it always reports exactly one renderer — SDL3_mixer is compiled in — so FNA's "zero renderers" check can never fail here); `NoAudioHardwareException` is still thrown at the actual point of failure, when the SDL3_mixer device itself won't open (`SoundEffect`/`DynamicSoundEffectInstance`'s `GetMixerOrThrowXna()`, `plan_audio.md` P9-HARDWARE-002) | CNA has exactly one audio backend (SDL3_mixer) with no renderer-enumeration API to ever report zero of; matching FNA's dead code path exactly would require fabricating a "no renderers" condition that cannot occur in this environment (`plan_audio.md` XA-9, P9-HARDWARE-003) |
| Audio: `SoundBank`/`WaveBank` (non-streaming) constructors silently stay in a "stub"/unprepared state on an existing-but-corrupt `.xsb`/`.xwb` file instead of throwing | Matches FNA exactly: `SoundBank.cs`/`WaveBank.cs` never check `FACTAudioEngine_CreateSoundBank`/`CreateInMemoryWaveBank`'s return code either, so corrupt-but-present bank content never throws a catchable C# exception in FNA (`plan_audio.md` P9-HARDWARE-003). A *missing* file, by contrast, now throws `System::IO::FileNotFoundException` from all three ctors (`AudioEngine`, `SoundBank`, non-streaming `WaveBank`), and an existing-but-corrupt `AudioEngine` settings file now throws `System::InvalidOperationException("Engine initialization failed!")` — both match FNA's `TitleContainer.ReadToPointer`/checked `FACTAudioEngine_Initialize` return code precisely (`plan_audio.md` P9-HARDWARE-003) |
| Audio: `Cue::Stop(AsAuthored)`'s release is driven by a real, retained, authored `fadeOutMS` (`XactParser` now retains it into `XsbCue`, `P9-STOP-010`) -- a real linear volume ramp over that exact duration, then a hard stop, matching FACT's `SOUND_STATE_FADE_OUT` handling (`FACT_INTERNAL_UpdateSound`, `FACT_internal.c`) closed-form and exactly, ticked both lazily (every state getter) and per-frame (`AudioEngine::Update()`). A cue with no authored `fadeOutMS` at all is now an *immediate* stop, matching FACT's own `FACTCue_Stop` condition (`fadeOutMS == 0 && maxRpcReleaseTime == 0` forces the immediate path) -- a "simple" cue's format has no `fadeOutMS` field at all, so this is every simple cue, always. RPC-driven release timing (`maxRpcReleaseTime`, when `fadeOutMS == 0` but a release-bound RPC curve exists) remains unimplemented: it depends specifically on the built-in `"ReleaseTime"` envelope variable (elapsed-release-time tracking), which is still unsupported even though RPC volume/pitch are now otherwise continuously re-evaluated (`P9-XACT-016`) — so a cue relying on RPC-only release timing (no authored `fadeOutMS`) still hard-stops immediately instead of waiting out its RPC release curve — a narrower remaining gap than before this fix, not a new one |
| Audio: XACT RPC (Runtime Parameter Control) volume/pitch curves are now re-evaluated continuously, every `AudioEngine::Update()` tick (via `Cue::ReconcileState()`), instead of only once at `Cue::Play()` time | Matches real FACT (`FACT_INTERNAL_UpdateRPCs`/`FACT_INTERNAL_UpdateEngine`, `FACT_internal.c`) recomputing every active track's bound RPC curves on every `FACTAudioEngine_DoWork` tick, so a variable that changes mid-playback now continuously updates volume/pitch, matching real XNA/FNA. Two narrower gaps remain: (1) the built-in `"AttackTime"`/`"ReleaseTime"` envelope variables (which track elapsed play/release time rather than a stored value) are still unsupported — CNA has no elapsed-playback-time tracking; (2) RPCs targeting filter frequency/Q (`parameter` 3/4) or a DSP preset (`parameter >= RPC_PARAMETER_COUNT`) still aren't evaluated, same unsupported status as the filter row below. As a side effect of the same fix, the fade-out/fade-in wall-clock ramps (`P9-STOP-010`/`P9-CATEGORY-007`) now also fold in the freshly-evaluated RPC volume multiplier — previously they recombined only `baseVolume*categoryVolume*fadeMultiplier`, silently dropping whatever RPC multiplier had been baked in at `Play()` time the moment a fade began, a real (if narrow) gap independent of one-shot-vs-continuous RPC (`plan_audio.md` P9-XACT-016) |
| Audio: the built-in `"Distance"`/`"OrientationAngle"`/`"DopplerPitchScalar"` RPC/cue variables are recognized as valid (`Cue::GetVariable`/`SetVariable` don't throw for them) but never automatically kept in sync with `Apply3D`'s real, live-computed distance/angle/Doppler values — an RPC curve bound to one only ever sees whatever value was last set via `SetVariable`, or `0.0f` if never set | Real FACT recomputes these from the live 3D graph every tick; CNA's `Cue::Apply3D()` computes real distance/attenuation/Doppler internally for its own pan/pitch math but never writes the results back into `variables_` for RPC curves to read. Found and precisely documented during the `plan_audio.md` Phase 10 audit (`P10-RPC-002`) — not previously called out this specifically (the pre-existing `AttackTime`/`ReleaseTime` row covered the *other* two built-ins, not these three) |
| Audio: `DynamicSoundEffectInstance`'s constructor performs no validation of `sampleRate`/`channels` (accepts values below 8000 Hz, above 48000 Hz, zero, or negative without throwing) | MSDN documents an 8,000-48,000 Hz range with `ArgumentOutOfRangeException` otherwise, but real FNA's own constructor (`DynamicSoundEffectInstance.cs`) performs zero validation either — a straight field-assignment into a `FAudioWaveFormatEx`. CNA matches FNA's actual (undocumented, permissive) behavior rather than retrofitting MSDN's stricter documented contract, consistent with the identical precedent already established for `SoundEffect`'s own constructors (`P9-VALIDATION-001`). Decision made and locked down by tests during the `plan_audio.md` Phase 10 audit (`P10-DYN-001/002/003`) |
| Audio: a complex sound's per-track low/high/band-pass filter (`filterData`/`frequency` in the `.xsb`) is now wired for real into `SoundEffectInstance::INTERNAL_apply{Low,High,Band}PassFilter` at `Cue::Play()` time, evaluated once (unlike RPC volume/pitch, which is now continuously re-evaluated, `P9-XACT-016`), not continuously re-evaluated, and **not** overridable by a live filter-frequency/filter-Q RPC (RPC `parameter` 3/4) the way real FACT does | `INTERNAL_apply{Low,High,Band}PassFilter` gained a NOXNA-only `oneOverQ` parameter (default `1.0f`, so FNA's own hardcoded-`1.0f` public behavior is unchanged for every other caller) so the real parsed XACT Q-factor byte has a place to go; RPC `parameter` 3 (filter frequency)/4 (filter Q) are parsed as curve targets but never evaluated against a track's filter, same "unsupported" status as `parameter >= 5` DSP-preset RPCs above (`plan_audio.md` P9-XACT-006's note). Sound-level `SOUND_FLAG_HAS_DSP` remains a no-op — confirmed by audit (`plan_audio.md` P9-XACT-010) to be FACT's reverb-send-enable flag, not a filter selector, so this is unrelated to `INTERNAL_applyReverb`'s no-op status above (`plan_audio.md` P9-XACT-011/012/013) |
| Audio: a parsed per-track filter's type can only ever come out as low-pass or high-pass, never band-pass, even though the format has a band-pass value | Replicates FAudio's own bit-decode of the `filterData` field exactly (`(filterData>>1)&0x02` structurally can only yield 0 or 2); this looks like a genuine upstream FAudio quirk, not a CNA bug — deliberately not "corrected" so CNA stays byte-for-byte behaviorally identical to real FACT content (`plan_audio.md` P9-XACT-010/011) |
