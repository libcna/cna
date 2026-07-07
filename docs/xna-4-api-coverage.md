# XNA 4.0 API Coverage Audit

**Date:** 2026-06-26 (updated 2026-06-26 — Tasks 197–199; updated 2026-07-03 — Input/Touch
sections, `feature/input` Phases I1–I6; updated 2026-07-04 — final Input status, `feature/input`
Phase I9, `plan_input.md` tasks 700–840, coverage split by category)  
**Reference:** FNA source at `/rv/data/library/github.com/FNA-XNA/FNA/src`  
**CNA headers:** `include/Microsoft/Xna/Framework/`

---

## 1. Overview

This document compares the CNA public C++ API surface against:

1. The FNA C# source tree, which is the primary faithful reimplementation of XNA 4.0.
2. The XNA 4.0 SDK documentation for areas that FNA omits (GamerServices, Avatar, etc.).

### Status Definitions

| Status | Meaning |
|--------|---------|
| **Implemented** | Header + working `.cpp` implementation; passes tests or is exercised in demos. |
| **Partial** | Header exists; some public methods are missing, or behavior is incomplete. |
| **Stub** | Header + declaration exist; behavior throws `std::runtime_error` / returns a no-op default. Marked with `// CNA_STUB:` in source. |
| **Missing** | No header in CNA at all. |
| **Intentionally excluded** | Deliberately not ported; reason documented below. |

### What counts as "public XNA 4.0 API"

- `public` and `protected` members of non-internal classes/structs/enums in the `Microsoft.Xna.*` namespace.
- FNA-internal helper classes, platform backends, `internal` C# classes, and Xbox-Live–specific runtime classes that were never meaningful on PC are **excluded** from this audit.

---

## 2. Namespace Coverage

| Namespace | FNA has it | CNA has it | Status |
|-----------|-----------|------------|--------|
| `Microsoft::Xna::Framework` (root) | ✅ | ✅ | Implemented |
| `Microsoft::Xna::Framework::Audio` | ✅ | ✅ | Implemented (see §4 for the small remaining accepted-deviation list) |
| `Microsoft::Xna::Framework::Content` | ✅ | ✅ | Partial (see §3) |
| `Microsoft::Xna::Framework::Design` | ✅ | ❌ | Intentionally excluded (see §6) |
| `Microsoft::Xna::Framework::GamerServices` | ❌ (not in FNA) | ⚠️ | Stub – Guide only (see §5) |
| `Microsoft::Xna::Framework::Graphics` | ✅ | ✅ | Implemented / Stub |
| `Microsoft::Xna::Framework::Graphics::PackedVector` | ✅ | ✅ | Implemented |
| `Microsoft::Xna::Framework::Input` | ✅ | ✅ | Implemented |
| `Microsoft::Xna::Framework::Input::Touch` | ✅ | ✅ | Implemented (see §4) |
| `Microsoft::Xna::Framework::Media` | ✅ | ✅ | Implemented / Stub |
| `Microsoft::Xna::Framework::Media::Video` | ✅ | ✅ | Implemented |
| `Microsoft::Xna::Framework::Storage` | ✅ | ✅ | Implemented |
| `Microsoft::Devices::Sensors` | ❌ (not in FNA) | ✅ | Stub – Windows Phone extension |

---

## 3. FNA Public API Found but Missing in CNA

### `Microsoft::Xna::Framework::Content`

| Missing class | Notes |
|---------------|-------|
| `ResourceContentManager` | Loads content from .NET assembly resources. CNA equivalent would load from embedded binary. **Should be stubbed.** |
| `ContentReader` | XNB binary stream reader used by stock ContentTypeReaders. CNA uses a file-extension reader approach, not XNB. **Intentionally deferred** – see §6. |
| `ContentTypeReaderManager` | Internal registry used by ContentReader. Not needed without XNB support. **Intentionally deferred.** |
| `ContentExtensions` | Adds `GetLoadedAssets()` extension to ContentManager. FNA-internal convenience; not core XNA 4.0 public API. **Intentionally excluded.** |
| `ContentSerializerAttribute` and siblings | Design-time attributes for the Content Pipeline build tool. Not a runtime concern. **Intentionally excluded** – see §6. |
| `LzxDecoder` | XNB LZX decompressor. FNA internal. **Intentionally excluded.** |

### `Microsoft::Xna::Framework::GamerServices`

FNA itself does not implement GamerServices. CNA has only `Guide` (stub).  
The following XNA 4.0 classes are absent from both FNA and CNA; they should exist as stubs
so that XNA 4.0 game code that references them can at least compile:

| Missing class | Notes |
|---------------|-------|
| `GamerServicesComponent` | `GameComponent` subclass that must be added to `Game.Components` to enable gamer services. **Should be stubbed.** |
| `GamerServicesNotAvailableException` | Thrown when gamer services calls are made on a platform that does not support them. **Should be stubbed.** |
| `Gamer` | Abstract base for `SignedInGamer` and `NetworkGamer`. |
| `SignedInGamer` | Represents a locally signed-in player profile. |
| `GamerCollection<T>` | Generic collection of Gamer objects. |
| `SignedInGamerCollection` | Indexed collection of locally signed-in gamers. |
| `GamerPresence` | Current presence information (what the player is doing). |
| `GamerPresenceMode` | Enum of presence modes. |
| `GamerPrivilege` | Enum of privilege flags. |
| `GameDefaults` | Static helper for reading default player settings. |
| `FriendCollection` | Collection of friend gamers. |
| `FriendGamer` | Represents a friend in the gamer's friend list. |

---

## 4. CNA API Present but Incomplete

### `Microsoft::Xna::Framework::Content::ContentManager`

- **Missing:** `ServiceProvider` property (`IServiceProvider*`). XNA 4.0 ContentManager takes a service provider in its constructor and exposes it. CNA uses `setGraphicsDevice()` instead. Any XNA 4.0 game that passes a service provider to ContentManager will not compile without this.
- **Missing:** Protected `OpenStream(string)` virtual — needed if subclasses want to intercept asset loading.
- **Missing:** Protected `ReadAsset<T>` — needed if subclasses customise loading.
- **Status:** Partial

### `Microsoft::Xna::Framework::Graphics::GraphicsDevice`

- All public methods are declared. Some behaviors are backend-dependent and may be no-ops.
- **Status:** Partial (functionally working for 2D and 3D; some query methods return stubs)

### `Microsoft::Xna::Framework::Graphics` — Stock Effects

- `BasicEffect`, `AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect`: API surface complete; GPU shader logic fully implemented in EasyGL (pixel-tested, all property setters verified). Vulkan has working shaders for most effects but fewer pixel-test coverage points. Bgfx compiles effect objects but 3D rendering is blocked by unimplemented depth/blend state. See §7 for full backend parity table.
- **Status:** Implemented (EasyGL); Partial (Vulkan); Blocked (Bgfx)

### `Microsoft::Xna::Framework::Graphics::PackedVector::*`

All 17 XNA 4.0 PackedVector types are fully implemented:

| Type | Packed width | Status |
|------|-------------|--------|
| `Alpha8` | 8-bit | ✅ Implemented |
| `Bgr565` | 16-bit | ✅ Implemented |
| `Bgra4444` | 16-bit | ✅ Implemented |
| `Bgra5551` | 16-bit | ✅ Implemented |
| `Byte4` | 32-bit | ✅ Implemented |
| `HalfSingle` | 16-bit half-float | ✅ Implemented |
| `HalfVector2` | 32-bit (2× half) | ✅ Implemented |
| `HalfVector4` | 64-bit (4× half) | ✅ Implemented |
| `NormalizedByte2` | 16-bit (2× snorm8) | ✅ Implemented |
| `NormalizedByte4` | 32-bit (4× snorm8) | ✅ Implemented |
| `NormalizedShort2` | 32-bit (2× snorm16) | ✅ Implemented |
| `NormalizedShort4` | 64-bit (4× snorm16) | ✅ Implemented |
| `Rg32` | 32-bit (2× unorm16) | ✅ Implemented |
| `Rgba1010102` | 32-bit (10-10-10-2) | ✅ Implemented |
| `Rgba64` | 64-bit (4× unorm16) | ✅ Implemented |
| `Short2` | 32-bit (2× int16) | ✅ Implemented |
| `Short4` | 64-bit (4× int16) | ✅ Implemented |

`HalfTypeHelper` implements the full FNA IEEE 754 half-float algorithm including subnormals, ±∞, ±0, and NaN (Tasks 197–199). Pack rounding uses `std::lroundf` matching FNA's `Math.Round` for signed-normalized types (Tasks 198). Golden-value tests and edge-case tests (clamping, specials) all pass.

- **Status:** Implemented

### `Microsoft::Xna::Framework::Audio`

> Updated 2026-07-04 (Fáze 9 `P9-DOCS-002`) — the entries below describing XACT/`Microphone`/
> `DynamicSoundEffectInstance` as unimplemented stubs were stale by a full branch's worth of work
> (`plan_audio.md` Fáze 0–9). See `AUDIT.md`'s Audio table for the per-class summary and
> `plan_audio.md` for the complete file-by-file history.

- `AudioEngine`, `SoundBank`, `WaveBank`, `Cue`: **Implemented.** Real hand-written `.xgs`/`.xsb`/
  `.xwb` (XACT) parser (`CNA::Internal::Audio::XactParser`), mixed through SDL3_mixer. Cue playback,
  variation selection (weighted lottery, matching FAudio), category volume/pause/resume/stop, 3D
  pan/attenuation, natural-completion state reconciliation, and fire-and-forget lifecycle are all
  real, including real Doppler pitch shift (`P9-3D-004/005`). Not FAudio/FACT — SDL3_mixer is the
  backend, so 3D HRTF/elevation and streaming-wavebank offset/packetSize params are documented
  accepted deviations, not stubs (`CHECKLIST.md`).
- `Microphone`: **Implemented.** Real SDL3 capture device enumeration, `Start()`/`Stop()`,
  `GetData()`/`GetQueuedBytes()`, `BufferReady` event.
- `DynamicSoundEffectInstance`: **Implemented.** Real buffer queue via `SDL_AudioStream`,
  `BufferNeeded` event pumped by `FrameworkDispatcher::Update()`, `PendingBufferCount` tracks real
  stream consumption.
- `SoundEffect`, `SoundEffectInstance`: **Implemented** (SDL3_mixer backend). Move-only with
  instance-tracking `Dispose()` cascade; real low/high/band-pass filters; real Doppler pitch
  shift via `Apply3D` (`P9-3D-004/005`); reverb and full 3D HRTF/elevation remain documented
  no-ops (SDL3_mixer has no aux-send bus or 3D audio graph).
- **Status:** Implemented, with a small set of accepted deviations documented in `CHECKLIST.md`
  (stereo hard-pan instead of crossfeed, no reverb/HRTF/elevation, `QUEUE`/`REPLACE_OLDEST`/
  `REPLACE_QUIETEST` instance-limit behaviors all collapsing to "evict oldest," a cue-level
  instanceLimit eviction's victim search having no category/same-cue-definition filter at all) —
  none of these are "unimplemented," they are deliberate, reasoned SDL3_mixer-backend trade-offs
  (or, for the `maxInstanceBehavior` collapse and the unfiltered victim search, matching FAudio's
  own acknowledged/shipped behavior exactly, quirks included). `Cue::IsPlaying`/`IsPaused` mutual
  exclusivity was one such deviation but is now fixed (`P9-LIFECYCLE-013`, resolved) — pausing no
  longer clears `IsPlaying`, matching real FACT's independent bits. XACT category- **and**
  cue-level `instanceLimit`/`maxInstanceBehavior`/fade in/out are now both real and enforced
  (`P9-CATEGORY-005..011`, resolved).

#### Audio compatibility table (`P9-DOCS-005`)

A concise summary of every known-and-documented gap versus real XNA 4.0/FNA. Full rationale for
each row is in `CHECKLIST.md`'s "Known acceptable C++ deviations" table (search for `Audio:`);
this table exists to answer "is X implemented?" at a glance without reading every row there.

| Bucket | Behavior |
|---|---|
| **Implemented** (matches FNA/XNA 4.0) | `SoundEffect` construction (file/buffer/buffer+range+loop), `Play`/`CreateInstance`, move-only Dispose cascade to every live instance • `SoundEffectInstance` Play/Pause/Resume/Stop/Volume/Pan/Pitch/IsLooped, real low/high/band-pass filters, `Resume()`-plays-if-never-started quirk, real Doppler pitch shift via `Apply3D` (exact `F3DAudio.c` `CalculateDoppler` formula, `P9-3D-004/005`) • `DynamicSoundEffectInstance` SubmitBuffer/SubmitFloatBufferEXT, `BufferNeeded`, real `PendingBufferCount` • `AudioEngine` `.xgs` parsing, categories, global variables, `Update()` lifecycle sweep, Dispose cascade, real `System::IO::FileNotFoundException` on a missing settings file and `System::InvalidOperationException` on existing-but-corrupt settings (`P9-HARDWARE-003`, matching FNA's `TitleContainer.ReadToPointer`/checked `FACTAudioEngine_Initialize` exactly) • `SoundBank`/non-streaming `WaveBank` real `System::IO::FileNotFoundException` on a missing file (same fix) • `SoundBank`/`WaveBank` `.xsb`/`.xwb` parsing (compact + non-compact + ADPCM), `PlayCue`, `GetCue`, real lazy streaming reads • `Cue` playback, natural-completion state reconciliation, weighted-lottery variation selection for non-interactive tables AND variable-range-driven selection for interactive (`type==3`) tables (`P9-XACT-002/003/004`, not a uniform fallback), category routing, `IsPlaying`/`IsPaused` as independent (non-mutually-exclusive) flags matching real FACT (`P9-LIFECYCLE-013`, resolved), real authored `fadeOutMS` timing for `Stop(AsAuthored)` -- a real linear volume ramp over the exact authored duration, hard-stopping immediately when no fade is authored at all (every simple cue), matching FACT's own `FACTCue_Stop` condition exactly (`P9-STOP-010`, resolved) • real XACT category- and cue-level `instanceLimit`/`maxInstanceBehavior` enforcement (`FAIL` rejects, `REPLACE_LOWEST_PRIORITY` evicts by priority, `QUEUE`/`REPLACE_OLDEST`/`REPLACE_QUIETEST` evict oldest; cue-level checked before category-level, matching FACT's own `play_sound()` order) plus real fade-in/fade-out within that same instance-limit-replacement path, at both levels (`P9-CATEGORY-005..011`, resolved) • `AudioCategory` Pause/Resume/Stop/SetVolume against real active cues over a mutation-safe snapshot • real continuous XACT RPC volume/pitch re-evaluation every `AudioEngine::Update()` tick, not just once at `Cue::Play()` time (`P9-XACT-016`), extended to the built-in `"AttackTime"`/`"ReleaseTime"` envelope variables (real elapsed-play/release-time tracking, `P10-RPC-002/003/004`) and to RPC-driven live filter frequency/Q targeting (`P10-FILTER-002/003/004/006`) • a bounded loop region (`loopStart`/`loopLength`) plays its pre-loop intro exactly once, then repeats only `[loopStart, loopStart+loopLength)` — confirmed against real decoded audio, not just the SDL3_mixer property docs (`P10-LOOP-003/004`) • `Microphone` real SDL3 capture • `AudioListener`/`AudioEmitter`/`RendererDetail`/all enums |
| **Approximate** (real effect, not bit-exact vs FNA/XAudio2/FACT) | `Apply3D` pan now projects onto the listener's own right axis (`Forward`/`Up`-aware, `P9-3D-010`) instead of raw world-space X, but is still a linear approximation, not real X3DAudio's multi-speaker energy-diffusion azimuth pipeline (Doppler and distance attenuation are exact closed-form formulas, see Implemented — pan is the only remaining approximate positional effect) • stereo hard-pan (`Pan`=±1) eliminates the opposite channel entirely instead of crossfeed-blending it (mono is bit-exact) • category/cue `maxInstanceBehavior`'s `QUEUE`/`REPLACE_OLDEST`/`REPLACE_QUIETEST` values all collapse to "evict oldest active cue," matching FAudio's own acknowledged collapse of those three (`P9-CATEGORY-005/010/011`) • a cue-level instanceLimit eviction's victim search has no category or same-cue-definition filter at all, matching FAudio's own `handle_instance_limit(cue, NULL)` exactly — it can evict an unrelated cue instead of a sibling instance of the same named cue (`P9-CATEGORY-011`) |
| **Intentionally unsupported** (documented, no plan to implement) | reverb (`INTERNAL_applyReverb` is a no-op — no aux-send/return bus in SDL3_mixer) • true 3D elevation/HRTF (no native 3D audio graph) • `NoAudioHardwareException` never thrown from `AudioEngine`'s own constructor (CNA always reports exactly one renderer, so FNA's "zero renderers" check can never fail here; it *is* thrown from `SoundEffect`/`DynamicSoundEffectInstance` when the mixer device itself fails to open, `P9-HARDWARE-002`) • `SoundBank`/`WaveBank` silently stay in a stub/unprepared state on existing-but-corrupt content (confirmed matching FNA, `P9-HARDWARE-003`: FNA never checks its own native bank-creation calls' return codes either) |
| **Not yet implemented / open decision** | RPCs targeting a DSP preset (`parameter >= RPC_PARAMETER_COUNT`) remain unevaluated — no DSP preset system exists at all |

#### `Apply3D` / 3D audio fidelity — consolidated summary (`P9-3D-001..010`)

Fáze 9's `P9-3D` group (`plan_audio.md`) audited every piece of `Apply3D` against FNA/FAudio one
at a time; this is the consolidated result now that all nine original items plus one user-directed
follow-up (`P9-3D-010`) have landed. Per-property breakdown:

| Property | Fidelity | Notes |
|---|---|---|
| Distance attenuation | **Exact** | Matches FAudio's `F3DAudio.c` `ComputeDistanceAttenuation` no-custom-curve formula precisely: full volume within `SoundEffect.DistanceScale`, inverse-distance falloff (`gain = DistanceScale/distance`) only strictly beyond it (`P9-3D-003`). |
| Doppler pitch shift | **Exact** | Matches FAudio's `F3DAudio.c` `CalculateDoppler` precisely: relative-velocity projection onto the emitter-to-listener axis, `SpeedOfSound`/`DopplerScale` clamping, NaN guard, `[0.5, 4.0]` output clamp (`P9-3D-004/005`). Needed no native 3D audio API — pure math over `Position`/`Velocity`, applied via the same `MIX_SetTrackFrequencyRatio` the plain `Pitch` property already uses. |
| Pan | **Approximate** | Linear projection onto the listener's own right axis (`Cross(Forward, Up)`, normalized) over distance, clamped to `[-1,1]` — orientation-aware since `P9-3D-010` (previously raw world-space `(emitter.X - listener.X) / distance`, ignoring listener orientation entirely; found during the `P9-3D-009` audit and implemented as a user-directed follow-up). Two remaining known gaps vs. real X3DAudio: (1) still a single linear axis, not X3DAudio's full multi-speaker energy-diffusion azimuth pipeline (no equivalent in SDL3_mixer's single stereo-gain-pair model, `CP-19`) — elevation/vertical displacement plays no role, matching CNA's documented no-HRTF/elevation limitation. (2) Stereo sources hard-pan (eliminate the opposite channel) instead of crossfeed-blending, the same `SoundEffectInstance::Pan`-property limitation `CP-19`/`P9-3D-001` already found (mono sources are bit-exact either way). The **emitter's** own `Forward`/`Up` remain unread — matching real X3DAudio, where emitter orientation only affects multi-channel emitter configurations, not a mono source's pan. |
| Elevation / true HRTF | **Not supported** | SDL3_mixer has no positional-audio DSP graph or head-related transfer function; no plan to implement (would need a different backend entirely). |
| Reverb (aux-send routing) | **Not supported** | Confirmed by the `P9-XACT-010` audit to be FACT's sound-level `SOUND_FLAG_HAS_DSP` reverb-send-enable flag, not something `Apply3D` itself touches; SDL3_mixer has no aux-send/return bus for it to route to. |

**Bottom line:** of `Apply3D`'s three positional effects (pan, distance attenuation, Doppler),
two are bit-exact matches for real XNA/FNA's math, and the third (pan) is a deliberate,
narrowly-scoped linear approximation that's now orientation-aware (`P9-3D-010`) but still not a
full multi-speaker azimuth port, with one remaining named gap (stereo-hard-pan) rather than a vague
"not fully implemented." Elevation/HRTF and reverb aux-send remain out of scope, since both would
require a fundamentally different audio backend than SDL3_mixer.

#### SDL3_mixer vs FAudio/FACT backend limitations (`P9-DOCS-006`)

CNA's audio backend is **SDL3_mixer**, not FAudio/FACT — XACT content (`.xgs`/`.xsb`/`.xwb`) is
parsed by a hand-written `CNA::Internal::Audio::XactParser` and played through SDL3_mixer's own
mixing graph, which is structurally different from FAudio's XAudio2-derived voice graph. Concrete
consequences, all downstream of this one architectural choice:

- **No `F3DAudio` equivalent.** SDL3_mixer has no positional-audio DSP graph; pan and distance
  attenuation are approximated at the CNA layer, not computed by the backend. Doppler pitch shift
  is the exception: it's a closed-form formula over `Position`/`Velocity` needing no native 3D
  audio API at all, so CNA computes it exactly (matching FAudio's `F3DAudio.c`
  `CalculateDoppler`, `P9-3D-004/005`) and applies it via `MIX_SetTrackFrequencyRatio` (the same
  primitive already used for the plain `Pitch` property). Correspondingly XACT `FACT3DApply`-style
  dedicated 3D application from `Cue` also is not the same call path — CNA approximates it via
  `SoundEffectInstance::Apply3D` on every playing wave.
- **No aux-send/return bus.** FAudio (like XAudio2) supports a shared reverb submix voice;
  SDL3_mixer has no equivalent routing concept, so reverb has no backend primitive to attach to.
- **Only one "cooked" (post-mix, pre-output) callback slot per track.** This is why the DSP filter
  (`T-4C`) and a hypothetical stereo-crossfeed pan implementation (`CP-19`) can't coexist without a
  real redesign — SDL3_mixer's per-track callback isn't a chain, it's a single slot.
- **Stereo panning is a 2-value gain pair (`MIX_StereoGains`), not a 4-coefficient output matrix.**
  FAudio/XAudio2 can crossfeed (send part of the left channel to the right output and vice versa);
  SDL3_mixer's stereo pan can only scale each channel's own output, so a hard pan silences the
  opposite channel instead of blending it in.
- **Loop region uses a "stop at frame N" property (`MIX_PROP_PLAY_MAX_FRAME_NUMBER`) combined with
  a separate loop-back point (`MIX_PROP_PLAY_LOOP_START_FRAME_NUMBER`).** Despite having no
  dedicated `LoopBegin`/`LoopLength` pair, the combination matches FNA/XAudio2's semantics exactly:
  the intro plays once, then only the loop region repeats -- confirmed against real decoded audio
  via a raw SDL3_mixer callback (`P10-LOOP-003/004`), not just inferred from the property docs.
- **Global master gain is a real, live mixer-level primitive** (`MIX_SetMixerGain`/
  `MIX_GetMixerGain`) — unlike some of the above, this is a case where SDL3_mixer's model is
  *simpler and better suited* than re-deriving master volume per-track would have been (`CP-16`).

#### FNA-matching vs CNA-specific compromise (`P9-DOCS-007`)

Two different kinds of "doesn't match FNA line-for-line" show up in this module, and it matters
which one a given deviation is:

1. **Deliberate CNA-specific compromises**, forced by the SDL3_mixer backend choice — the entire
   "SDL3_mixer vs FAudio/FACT" list above. These are documented in `CHECKLIST.md` precisely because
   they are permanent, reasoned trade-offs, not bugs to eventually fix. (The interactive-variation
   selection used to be a uniform-pick fallback here, but that was a real gap, not a backend
   compromise — it's been variable-range-driven since `P9-XACT-002/003/004`, see Implemented above;
   this stale claim was corrected during the `P10-AUDIT` pass, `plan_audio.md`.)
2. **Bugs that were fixed to *actually* match FNA**, not compromises — e.g. every `P9-LIFECYCLE`/
   `P9-CATEGORY`/`P9-VALIDATION` fix in Fáze 9 (natural cue-completion reconciliation, the
   category mutate-during-iteration bug, the `offset+count` integer-overflow segfault, `Resume()`
   calling `Play()` when never-started) made CNA *more* faithful to FNA, they didn't introduce new
   compromises. Where CNA intentionally throws instead of replicating an FNA quirk (`Cue::
   GetVariable`/`SetVariable` after Dispose, `P9-LIFECYCLE-015`; `Resume()` after Dispose,
   `P9-VALIDATION-010`), it's because the FNA behavior being deviated from is itself an
   unintentional native-crash bug in FAudio, not a documented contract — see each fix's own
   comment in source for the specific FNA/FAudio line reference.

When in doubt about which category a future finding falls into: if fixing it would require
touching SDL3_mixer's actual capabilities, it's category 1 (document in `CHECKLIST.md`). If it's
purely a CNA-side logic bug independent of the backend, it's category 2 (just fix it).

#### Full per-member cross-reference (`P10-AUDIT-002/003`)

The compatibility table above answers "is X implemented?" at class granularity. This section goes
one level deeper: every public property/method/constructor/enum-value/exception across all 18
files in `include/Microsoft/Xna/Framework/Audio/`, bucketed as **Implemented+Tested**,
**Implemented, untested**, **Approximate**, or **Unsupported**, cross-referenced against the
authoritative FNA source (`/rv/data/library/github.com/FNA-XNA/FNA/src/Audio/*.cs`) and this
repo's test suite. Produced by five parallel audit passes (2026-07-07); every "new gap" they found
is either fixed in this same pass (see the list at the end) or explicitly deferred with a reason.

**`SoundEffect`** — every constructor overload (file / buffer / buffer+range+loop), copy-deleted +
move ctor/assign, dtor/`Dispose`, `IsDisposed`, `Name` get/set, static `MasterVolume`/
`DistanceScale`/`DopplerScale`/`SpeedOfSound` (incl. `ArgumentOutOfRangeException` guards),
`CreateInstance`, both `Play` overloads, static `GetSampleDuration`/`GetSampleSizeInBytes`,
`FromStream` (incl. 7 malformed-input paths) — all **Implemented+Tested**. `Duration` get is
**Implemented, untested in isolation** (only exercised transitively via `FromStream`'s duration
checks against a known buffer) — fixed this pass, see below.

**`SoundEffectInstance`** — dtor/move ctor/assign, `Play`/both `Stop` overloads/`Pause`/`Resume`,
both `Apply3D` overloads (incl. `NotSupportedException` for `listenerCount != 1`), `IsDisposed`,
`Volume`/`Pan`/`Pitch`/`IsLooped` get/set (incl. post-`Play` `IsLooped` `InvalidOperationException`),
`State`, and the private filter machinery (`INTERNAL_apply{Low,High,Band}PassFilter`,
`INTERNAL_applyXactTrackFilter`, `INTERNAL_applyRpcFilterOverride`,
`INTERNAL_calculateFilterCutoff`/`OneOverQ`, `INTERNAL_calculateListenerRight`/`calculatePan`) via
`SoundEffectInstanceTestAccess` — all **Implemented+Tested**. `INTERNAL_applyReverb` is the one
**Unsupported** member (documented no-op, tested for non-throw).

**`DynamicSoundEffectInstance`** — ctor (permissive, no validation, matching FNA exactly,
`P10-DYN-001/002/003`), `PendingBufferCount`, `IsLooped` (no-op override), `Dispose`,
`GetSampleDuration`/`GetSampleSizeInBytes`, `Play`/`Stop`(both)/`Pause`/`Resume`, both
`SubmitBuffer` overloads (incl. `int32` offset+count overflow regression), both
`SubmitFloatBufferEXT` overloads, `State`, `BufferNeeded` (incl. reentrancy) — all
**Implemented+Tested**.

**`AudioEngine`** — `ContentVersion`, both constructors, `IsDisposed`, `GetCategory` (all 3
exception paths), `GetGlobalVariable`/`SetGlobalVariable` (all exception paths), `Update`,
`Dispose`/`Disposing`, internal registries (wave bank/category/cue-instance-limit bookkeeping) —
all **Implemented+Tested**. `RendererDetails` is **Implemented, weakly tested** (the one test only
asserts non-emptiness, not the exact single `("SDL3_mixer", "SDL3_mixer")` entry) — strengthened
this pass, see below.

**`AudioCategory`** — `Name`, `Pause`/`Resume`/`SetVolume`/`Stop` (real routing verified against
active cues), `Equals`/`GetHashCode`/`operator==`/`operator!=` — all **Implemented+Tested**.
`GetHashCode` uses `std::hash<std::string>`, not .NET's algorithm — already a documented, generic
Audio-wide deviation (`CHECKLIST.md`), not new here.

**`SoundBank`** — ctor (incl. null-engine/empty-filename/missing-file/corrupt-file),
`IsDisposed`/`IsInUse` (by design only reflects fire-and-forget `PlayCue` cues, not
`GetCue()`-obtained ones, matching its own doc comment), `GetCue`, both `PlayCue` overloads,
`Dispose`/`Disposing` — all **Implemented+Tested**.

**`WaveBank`** — non-streaming ctor — **Implemented+Tested**. Streaming ctor's `offset`/
`packetSize` parameters are **Approximate**: parsed into the signature but never forwarded to the
real stream-open call, matching FNA's own `WaveBank.cs` exactly (already documented,
`CHECKLIST.md`). `IsDisposed`/`IsPrepared` (incl. corrupt-file)/`IsInUse` (incl. paused/dispose/
natural-completion), `Dispose`/`Disposing` — **Implemented+Tested**.

**`Cue`** — `Disposing`, `IsDisposed`, `IsPaused` (independent flag alongside `Playing`, matching
real FACT's bitmask, `P9-LIFECYCLE-013`), `IsPlaying` (incl. natural-completion reconciliation),
`IsPrepared`, `IsStopped`, `IsStopping` (real tail state — authored fade **and** RPC-only release,
`P10-RPC-004`), `Name`, `Apply3D`, `GetVariable`/`SetVariable` (incl. every built-in variable:
`Distance`/`DopplerPitchScalar`/`OrientationAngle` live-written by every `Apply3D` call,
`AttackTime`/`ReleaseTime` live only through RPC curve evaluation and never through plain
`GetVariable`, correctly matching real FACT's asymmetry), `Play`, `Pause`/`Resume` (no-op, not
throw, after `Dispose` — deliberate, safer-than-FNA divergence), `Stop` (both
`AudioStopOptions` values), `Dispose` — all **Implemented+Tested**. `IsCreated`/`IsPreparing` are
**Approximate**: always `false` and permanently unreachable, since CNA's synchronous `.xsb`
parsing skips FACT's `CREATED`→`PREPARING` phase entirely (`state_` starts at `Prepared` in the
ctor and never regresses) — tested and commented in `CueTests.cpp`, but not previously in
`CHECKLIST.md`/this doc — added this pass, see below.

**`AudioListener`/`AudioEmitter`** — ctor, `Forward`/`Position`/`Up`/`Velocity` get/set (round-trip
tested), `AudioEmitter::DopplerScale` (incl. negative → `ArgumentOutOfRangeException`) — all
**Implemented+Tested**. No coordinate-flip logic vs. FNA's internal `F3DAUDIO_LISTENER`
Z-negation, but that's an unobservable FNA-internal marshaling detail (`get(set(x)) == x` holds
identically on both sides), not a behavioral difference.

**`Microphone`** — `Name`, static `All`/`Default`, `BufferDuration` get/set (incl. both throw
paths), `IsHeadset` (hardcoded `false` — confirmed this **matches FNA exactly**: FNA's own getter
has a `// FIXME: I think this is just for Windows Phone?` comment and also unconditionally returns
`false`), `SampleRate` (hardcoded 44100, matches FNA's `SAMPLERATE` constant), `State`, both
`GetData` overloads (incl. negative offset/beyond-buffer/zero-or-negative count/integer-overflow
guards), `GetSampleDuration`/`GetSampleSizeInBytes` (delegate to `SoundEffect`), `Start`/`Stop` —
all **Implemented+Tested**. `BufferReady` and the internal `CheckAllBuffers` sweep are
**Implemented, untested for the real-hardware-capture-exceeds-threshold path** (only the
empty-subscriber-list/below-threshold cases are tested) — expected, already covered by `NEXT.md`'s
general "device-dependent tests only run against the dummy driver" note, not a new gap.

**Enums** (`AudioChannels`, `AudioStopOptions`, `MicrophoneState`, `SoundState`) and
**`RendererDetail`** — every value/member **Implemented+Tested**, exact match to FNA.

**Exceptions** — `NoAudioHardwareException`'s ctors/hierarchy are **Implemented+Tested**, and it
**is** thrown for real from `SoundEffect`/`DynamicSoundEffectInstance` when the mixer device fails
to open, with dedicated end-to-end coverage via the `cna_audio_no_hardware_harness` subprocess.
`NoMicrophoneConnectedException`/`InstancePlayLimitException`'s ctors/hierarchy are
**Implemented+Tested**, but neither is ever thrown anywhere in CNA production code — confirmed by
grepping FNA's own `Audio/*.cs`, this is exact dead-code parity with the reference (FNA declares
both but never throws either from its own Audio source either), not a gap.

**Gaps found by this audit and their disposition:**
- `Cue::IsCreated`/`IsPreparing` permanently unreachable — undocumented until this pass; added to
  `CHECKLIST.md` and the compatibility table above.
- `CueTests.cpp`'s `IsStoppingIsAlwaysFalse` test name/comment was stale (written before
  `P9-STOP-010`/`P10-RPC-004` added real `IsStopping` tail states) — renamed and recommented to
  describe the specific case it actually covers (immediate-stop-only, no authored tail), not a
  blanket claim the rest of this same file's own tests already contradict.
- `SoundEffect::Duration` had no dedicated direct test against a known buffer — added.
- `AudioEngine::RendererDetailsNonEmpty` only asserted non-emptiness — strengthened to assert the
  exact single SDL3_mixer entry.
- `Microphone.hpp`'s `setBufferDurationProperty` Doxygen comment stated the valid range as
  "[100, 999]"; the real (FNA-matching) condition is `< 100 || > 1000` — corrected the comment
  (behavior was already correct; this was a wording-only nit).
- Every other candidate gap the five audit passes surfaced was, on inspection, either already
  documented in `CHECKLIST.md` or a confirmed exact match to FNA's own behavior (including two
  cases of FNA's own dead code) — no new production behavior changes were needed.

### `Microsoft::Xna::Framework::Input::Touch`

- `TouchPanel`, `TouchPanelCapabilities`, `TouchCollection`, `TouchLocation`, `TouchLocationState`, `GestureSample`, `GestureType`: headers exist, full API surface.
- SDL3 touch backend is wired up (`feature/input` branch, `plan_input.md` Phase I2, INPUT-TOUCH-*/INPUT-GESTURE-* cluster): `SDL_EVENT_FINGER_*` feeds `TouchPanel::INTERNAL_onTouchEvent`, `TouchDeviceExists`, and `DisplayWidth`/`DisplayHeight` (from the real back-buffer size). Gestures (Tap, DoubleTap, Hold, Horizontal/Vertical/Free drag, Flick, Pinch, PinchComplete) are recognized end-to-end by `GestureDetector` and covered by a dedicated test suite.
- **Input member-level parity (2026-07-06):** the full public Input surface is now mechanically parity-checked against FNA — member/signature parity via the generated `docs/input-member-parity-matrix.md` (INPUT-API-027) + the compile-time signature freeze (INPUT-API-031), and enum values byte-pinned (INPUT-API-034). Keyboard keycode/scancode maps are byte-identical to FNA (INPUT-KBD-009/010). See `plan_input.md` for the per-type task status.
- Gesture recognition is a byte-faithful port of FNA's `GestureDetector.cs` (audited, task 829) with deterministic clock-injected tests (task 830); multi-touch edge cases + coordinate scaling covered (tasks 825–828).
- Known deviation: `TouchPanel::GetState()` falls back to an event-driven `InputManager` snapshot rather than FNA's per-frame poll population of `touches_` (documented in-source, task 714) — CNA's input bridge is event-driven, not poll-driven, throughout. The event-driven `InputManager` map is internally unbounded, but `TouchPanel::GetState()` caps the public snapshot at `MAX_TOUCHES` (8) to match FNA (DEC-10, 2026-07-05).
- `TouchPanel::GetCapabilities()` reports `MaximumTouchCount = 0` when disconnected and **4** when connected, matching FNA/XNA (DEC-09, 2026-07-05 — XNA always reports 4; a fixed XNA-compat value, not the `MAX_TOUCHES` tracking cap).
- **Status:** Implemented

### `Microsoft::Xna::Framework::Media`

- `Song`, `SongCollection`, `MediaPlayer`, `MediaQueue`: implemented with SDL_mixer.
- `Album`, `Artist`, `Genre`, `Picture`, `Playlist` and their collections: stub.
- `MediaLibrary`: stub.
- `Video`, `VideoPlayer`: implemented (FFmpeg backend).
- **Status:** Partial

### `Microsoft::Xna::Framework::Storage`

- `StorageDevice`, `StorageContainer`, `StorageDeviceNotConnectedException`: headers exist with full XNA API shape. Behavior is implemented via native file-system calls.
- **Status:** Implemented

### `Microsoft::Xna::Framework::GamerServices::Guide`

- Single method `Show()` and `IsTrialMode` property exist.
- No other gamer-services classes exist.
- **Status:** Stub (minimal)

---

## 5. XNA 4.0 API Not Present in FNA

These classes were part of XNA 4.0 but FNA does not implement them (Xbox Live / Xbox 360 exclusive).  
They are listed here for completeness; CNA should expose stub declarations so XNA game code compiles.

### `Microsoft::Xna::Framework::GamerServices` (Xbox Live)

All classes listed in §3 under GamerServices fall here. FNA has no implementation for any of them.

### Avatar API

Xbox 360 avatar rendering API; not part of PC XNA 4.0 and never implemented by FNA.

| Class | Notes |
|-------|-------|
| `AvatarAnimation` | Avatar skeletal animation clip. |
| `AvatarDescription` | Describes avatar body/clothing. |
| `AvatarExpression` | Enum of facial expressions. |
| `AvatarRenderer` | Renders an avatar with BasicEffect-compatible matrices. |
| `AvatarUpdateParameters` | Parameters for per-frame avatar update. |

**Decision:** These are Xbox 360 exclusive. They will remain `// CNA_STUB: Xbox 360 specific. Not applicable on PC.` if ever added. For now they are **not planned**.

### Net — Xbox Live Networking

`Microsoft.Xna.Framework.Net` (NetworkSession, PacketReader/Writer, NetworkGamer, etc.) is an Xbox Live multiplayer API. FNA does not implement it and it has no PC equivalent. **Intentionally excluded from CNA.**

---

## 6. Intentionally Excluded or Deferred API

### `Microsoft::Xna::Framework::Design` — TypeConverter classes

FNA provides 13 TypeConverter subclasses (e.g. `BoundingBoxConverter`, `ColorConverter`) that integrate XNA math types with `System.ComponentModel.TypeDescriptor` for use in .NET design-time editors (Visual Studio property grid).

**Why excluded from CNA:**
- `System.ComponentModel` is a .NET-only framework; no C++ equivalent exists.
- CNA has no design-time editors.
- These classes have zero runtime value for game code.
- Implementing a `TypeConverter` abstraction in sharp-runtime solely for this purpose would add significant complexity for no benefit.

**Decision:** Permanently excluded. If ever revisited, a minimal `ITypeConverter` interface could be added to sharp-runtime with no-op converters.

### `Microsoft::Xna::Framework::Content` — XNB pipeline classes

CNA's `ContentManager` is not XNB-based. It uses file-extension readers instead of the XNB binary format.

**Excluded because of this design decision:**
- `ContentReader` (reads from an XNB stream)
- `ContentTypeReaderManager` (manages XNB type reader registrations)
- `ContentSerializerAttribute` and siblings (content pipeline build-tool attributes)
- `LzxDecoder` (XNB LZX compression)

**Deferred:**
- `ResourceContentManager` — loads from C++ embedded resources; no XNB needed. Should be stubbed as a subclass of `ContentManager` with a `// CNA_STUB:` comment.

### FNA-Internal Classes (excluded as non-XNA-4.0 API)

| Class | Reason |
|-------|--------|
| `FNA3D` | FNA private GPU backend |
| `PipelineCache` | FNA internal shader cache |
| `ProfileCapabilities` | FNA internal feature detection |
| `VertexDeclarationCache` | FNA internal |
| `DxtUtil`, `X360TexUtil` | FNA internal texture utilities |
| `EffectHelpers`, `EffectMaterial`, `Resources` | FNA internal effect helpers |
| `GestureDetector` | FNA internal touch gesture FSM |
| `FNALoggerEXT` | FNA-specific logging extension |
| `BaseYUVPlayer`, `IVideoPlayerCodec`, `VideoPlayerAV1`, `VideoPlayerTheora` | FNA internal video codec backends |
| All `FNAPlatform/*`, `Utilities/*` | FNA platform glue |

---

## 7. Stock Effect Backend Parity (Task 196)

This section documents which rendering backends support each stock XNA 4.0 effect and
`ShaderEffect`, and at what level. Updated 2026-06-26.

### Status symbols

| Symbol | Meaning |
|--------|---------|
| ✅ | Pixel-tested — integration test draws geometry and reads back pixels; output verified. |
| ⚠️ | Compiles and links; effect object created and applied; **no pixel readback test**. |
| ❌ | Not implemented; calling the effect throws or is silently ignored. |
| N/A | Backend is 2D-only; 3D stock effects do not apply. |

### Backend overview

| Backend | CMake option | Notes |
|---------|-------------|-------|
| **EasyGL** | `EASYGL` | OpenGL ES 3.2 via SDL3 window; primary backend; all stock effects shader-implemented. |
| **Vulkan** | `VULKAN` | Custom SPIR-V pipelines; selected effects pixel-tested; others compile-only. |
| **Bgfx** | `BGFX` | Community fork of bgfx; `SetDepthTestEnabled`/`SetBlendEnabled` still throw — no 3D pixel tests possible yet. |
| **SDL\_Renderer** | `SDL_RENDERER` | 2D-only; all 3D calls throw `std::runtime_error`. Stock effects are N/A. |

### Per-effect status

| Effect | EasyGL | Vulkan | Bgfx | SDL\_Renderer | Notes |
|--------|--------|--------|------|---------------|-------|
| `BasicEffect` | ✅ | ⚠️ | ⚠️ | N/A | **EasyGL:** pixel-tested across all 4 shader variants (vertex-colour stride=16, texture stride=20, col+texture stride=24, lit+texture stride=32); fog, default lighting, and all property setters verified (Tasks 22, 189, 194, 195). **Vulkan:** used in `Vulkan_DrawInstanced_3Instances`, `Vulkan_RenderTarget2D_FullCycle`, `Vulkan_RenderTargetUsage` — compiles and applies correctly but no dedicated Basic-only pixel verification. **Bgfx:** shader compiled; no pixel test because `SetDepthTestEnabled` throws. |
| `AlphaTestEffect` | ✅ | ⚠️ | ⚠️ | N/A | **EasyGL:** all 8 `CompareFunction` values pixel-tested at reference=pixel=128 (Tasks 118, 190). **Vulkan/Bgfx:** shader compiled; no pixel tests. |
| `DualTextureEffect` | ✅ | ✅ | ⚠️ | N/A | **EasyGL:** 4 sub-tests incl. yellow×cyan→green (Tasks 136, 191). **Vulkan:** `Vulkan_DualTextureEffect_Blend` pixel-tests blend of two textures. **Bgfx:** dedicated shader compiled; no pixel test. |
| `EnvironmentMapEffect` | ✅ | ✅ | ❌ | N/A | **EasyGL:** 4 sub-tests verifying EmissiveColor, EnvMapAmount, EnvMapSpecular independently (Tasks 134, 192). **Vulkan:** `Vulkan_EnvironmentMapEffect_Readback` pixel-tested. **Bgfx:** `CreateEffectBackend` returns a no-op stub; no SPIR-V/bgfx shaders compiled for this effect. |
| `SkinnedEffect` | ✅ | ⚠️ | ⚠️ | N/A | **EasyGL:** stride-52 bone-weighted vertices tested with identity / translate / 2-bone-blend bones; pixel readback verifies vertex displacement (Tasks 134, 193). **Vulkan/Bgfx:** skinned shader compiled (`kSkinned3dShaders`); no pixel tests. |
| `ShaderEffect` (custom GLSL / SPIR-V) | ✅ | ✅ | ❌ | N/A | **EasyGL:** custom GLSL shader compiled and pixel-tested (`EasyGL_ShaderEffect_GLSL`, Task 140). **Vulkan:** custom SPIR-V shader compiled and pixel-tested (`Vulkan_ShaderEffect_SpirV`). **Bgfx:** `CreateEffectBackend` returns a no-op stub; vertex/fragment sources ignored. |

### Known gaps

| Gap | Priority | Notes |
|-----|----------|-------|
| Vulkan `BasicEffect` pixel test | Medium | No dedicated test; only incidentally exercised via instanced/RT tests. |
| Vulkan `AlphaTestEffect` pixel test | Medium | `CompareFunction` modes not pixel-verified on Vulkan. |
| Vulkan `SkinnedEffect` pixel test | Medium | Bone displacement not pixel-verified on Vulkan. |
| Bgfx 3D state (`SetDepthTestEnabled` / `SetBlendEnabled`) | High | Throws unconditionally; blocks all 3D pixel tests on Bgfx. |
| Bgfx `EnvironmentMapEffect` / `ShaderEffect` | Low | No shaders compiled; needs dedicated bgfx shader authoring. |
| EasyGL fog for `AlphaTestEffect` | Low | `AlphaTestEffect::FillGpuDrawParams` does not yet populate fog fields (fog only wired to `BasicEffect`). |

---

## 8. Overall Coverage Estimate

Coverage is estimated as the fraction of public XNA 4.0 API surface that is usable
(not merely declared) in a typical 2D or 3D game on the EasyGL backend.

| Subsystem | Estimated coverage | Notes |
|-----------|-------------------|-------|
| Math types (`Vector2/3/4`, `Matrix`, `Color`, `BoundingBox`, etc.) | ~98 % | All types implemented; minor numeric-precision edge cases only |
| `GraphicsDevice` (core device + state) | ~85 % | Missing: some query/counter methods; sRGB SurfaceFormats silently linear |
| `SpriteBatch` | ~95 % | All overloads; all `SpriteEffects`; `transformMatrix`; tested |
| `Texture2D / Texture3D / TextureCube` | ~90 % | All mip levels, partial rects, all 6 cube faces; sRGB formats not fully mapped |
| `RenderTarget2D / RenderTargetCube` | ~90 % | DiscardContents/PreserveContents; MRT; round-trip tested |
| `VertexBuffer / IndexBuffer` | ~90 % | All typed + raw paths; skinned stride 52; tested |
| `BasicEffect` | ~95 % | All 4 shader variants; lighting; fog; all property setters |
| `AlphaTestEffect` | ~90 % | All 8 `CompareFunction` modes pixel-tested; fog not wired |
| `DualTextureEffect` | ~90 % | Both texture slots + diffuse multiplier pixel-tested |
| `EnvironmentMapEffect` | ~90 % | EmissiveColor, EnvMapAmount, EnvMapSpecular pixel-tested |
| `SkinnedEffect` | ~85 % | Bone identity/translate/blend pixel-tested; full skinned pipeline |
| `ShaderEffect` (custom GLSL/SPIR-V) | ~80 % | EasyGL GLSL and Vulkan SPIR-V tested; no HLSL path |
| `PackedVector` (all 17 types) | ~100 % | Full Pack/Unpack with correct rounding; golden-value + edge-case tests |
| `SpriteFont` / `Model` | ~80 % | Functional for typical use; some edge-case APIs stubs |
| `SoundEffect / SoundEffectInstance` | ~95 % | SDL3_mixer backend; real filters, instance-tracking cascade; 3D is pan+attenuation+Doppler (no HRTF/elevation, documented) |
| `MediaPlayer / VideoPlayer` | ~85 % | FFmpeg video; SDL_mixer audio; Album/Artist/Genre stub |
| `ContentManager` | ~65 % | File-extension readers; no XNB; no ServiceProvider property |
| `StorageDevice / StorageContainer` | ~90 % | Native filesystem; full XNA API shape |
| `GamePad / Keyboard / Mouse` (XNA 4.0 core) | ~100 % behavior | SDL3 backend; FNA-faithful `GetHashCode`/`ToString`/ordering, keycode/scancode maps, dead-zone math, button/axis mapping — all wired and tested (`feature/input` Phases I3–I5, I9–I10). `Mouse::SetPosition` now converts logical→window for scaled/letterboxed windows (a-0001, task 846) — no remaining input-layer gap; residual items are platform/hardware-gated only. |
| `TextInputEXT` / `Mouse`+`GamePad` EXT / `Keyboard` scancode EXT | ~95 % behavior | FNA extensions, all implemented and FNA-faithful (`feature/input` Phases I1, I3–I5, I9): `TextInputEXT` is `char16_t`/UTF-16 with Unicode/IME tests; relative mouse, `ClickedEXT`, rumble, `GetGUIDEXT` (format fixed, task 816), gyro/accel. Untested slice is hardware/IME-gated. |
| `MouseCursor` (MonoGame-inspired, `NOXNA`) | ~100 % of exposed surface | 12 stock cursors, `FromTexture2D`, `IDisposable` singleton-safe dispose; no XNA/FNA equivalent. |
| `Input::Touch` | ~98 % behavior | Gesture pipeline (Tap…PinchComplete) byte-faithful FNA port, wired end-to-end and tested with a deterministic clock (`feature/input` Phase I2, I9). Documented deviations only: event-driven vs. poll-based `GetState()`. `MaximumTouchCount` reports 4 and `GetState()` caps at `MAX_TOUCHES` (8), both matching FNA (DEC-09/DEC-10). |
| `GamerServices` | ~5 % | `Guide` stub only |
| `Audio (XACT)` — AudioEngine/SoundBank/WaveBank/Cue | ~97 % | Real `.xgs`/`.xsb`/`.xwb` parser + SDL3_mixer playback; category/lifecycle/3D/instance-limit+fade (both category- and cue-level)/continuous RPC volume+pitch all real; gaps are documented accepted deviations (no HRTF/elevation, no AttackTime/ReleaseTime envelope tracking), not missing implementation |
| `Framework.Net` (NetworkSession, etc.) | 0 % | Xbox Live exclusive; intentionally excluded |
| **Overall (EasyGL backend, 2D+3D game)** | **~85 %** | Main gaps: GamerServices, XNB content pipeline. (Touch and XACT were main gaps as of this table's original estimate; Touch closed by `feature/input` Phase I2, XACT closed by `feature/audio` — see the `Input::Touch` and Audio rows above.) |

---

## 10. Recommended Implementation Order

1. **Math / common framework types** — ✅ Done: Color, Vector2/3/4, Matrix, Quaternion, BoundingBox/Sphere/Frustum, Ray, Plane, Curve, MathHelper, Point, Rectangle, GameTime.

2. **Input** — ✅ Done: GamePad, Keyboard, Mouse, Touch, TextInputEXT, MouseCursor. All have real,
   FNA-faithful runtime behavior wired to SDL3 (`feature/input` branch Phases I1–I9,
   `plan_input.md` tasks 700–840), not stubs. Coverage is assessed **by category, not blended**
   (per the input review): **XNA 4.0 core** ~99% behavior / ~99% tested (complete & faithful);
   **FNA `*EXT`** ~95% (all implemented; untested slice hardware/IME-gated); **MonoGame
   `MouseCursor`** (`NOXNA`) complete for the exposed surface; **platform-dependent** items
   (Wayland global-mouse, live sensors/rumble, gamepad hotplug slot-assignment, `SetPosition`
   letterbox) documented, not headless-verifiable. See `plan_input.md`'s "final split" table,
   `AUDIT.md`, `docs/platform-input-notes.md`, and `docs/demo-input-checklist.md` for detail.

3. **Audio** — ✅ Done: SoundEffect/Instance, and XACT (AudioEngine/SoundBank/WaveBank/Cue) via a
   real hand-written parser + SDL3_mixer. Remaining gaps are documented accepted deviations
   (`CHECKLIST.md`), not missing implementation.

4. **Graphics 2D** — ✅ Done: SpriteBatch, Texture2D, SpriteFont, BlendState, RasterizerState, SamplerState.

5. **Graphics 3D** — ✅ Done: VertexBuffer/IndexBuffer, Model, all 5 stock effects (EasyGL pixel-tested), all 17 PackedVector types fully implemented.

6. **Content / Media / Storage** — ✅ Partial: ContentManager (file-extension approach), MediaPlayer, VideoPlayer done. `ResourceContentManager` stub needed.

7. **GamerServices / Platform stubs** — ⚠️ Only `Guide` stub exists. `GamerServicesComponent` and `GamerServicesNotAvailableException` should be added for compile-compatibility.

---

## 11. Summary

### What was added by this audit and subsequent tasks

- `docs/xna-4-api-coverage.md` (this file; updated Tasks 196, 200)
- `include/Microsoft/Xna/Framework/Content/ResourceContentManager.hpp` — stub
- `src/Microsoft/Xna/Framework/Content/ResourceContentManager.cpp` — stub
- `include/Microsoft/Xna/Framework/GamerServices/GamerServicesComponent.hpp` — stub
- `src/Microsoft/Xna/Framework/GamerServices/GamerServicesComponent.cpp` — stub
- `include/Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp` — stub
- All 17 PackedVector types fully implemented with correct FNA Pack/Unpack math (Tasks 197–199)
- `tests/PackedVectorGolden.md` — FNA bit-packing reference table for all 17 types
- §7 Stock Effect Backend Parity table (Task 196)
- §8 Overall Coverage Estimate table (Task 200)

### What remains missing or incomplete

- `Design` namespace TypeConverter classes (intentionally excluded)
- `GamerServices` rich API (Gamer, SignedInGamer, etc.) — stubs can be added on demand
- `ContentReader` XNB-based class (deferred; CNA uses non-XNB approach)
- `ContentSerializerAttribute` family (intentionally excluded)
- Vulkan pixel tests for BasicEffect, AlphaTestEffect, SkinnedEffect
- Bgfx 3D state (depth test / blend enable) blocks all Bgfx 3D pixel tests

### What is intentionally excluded

- `Design` namespace — requires `System.ComponentModel` which has no C++ equivalent
- `ContentReader` and XNB pipeline — CNA uses file-extension approach, not XNB
- Avatar API — Xbox 360 exclusive
- Xbox Live Networking (`Microsoft.Xna.Framework.Net`) — Xbox Live exclusive
- All FNA-internal implementation classes

### Build status

See build run in task notes. Build must remain clean after each stub addition.

### Recommended next steps

1. Add Vulkan pixel tests for `BasicEffect`, `AlphaTestEffect`, `SkinnedEffect` (see §7 known gaps).
2. Unblock Bgfx 3D state (`SetDepthTestEnabled`, `SetBlendEnabled`) to enable 3D pixel tests on Bgfx.
3. Add compile-compatibility stubs for `Gamer` / `SignedInGamer` / `GamerCollection` if target games need them.
4. Audit `GraphicsDevice` public methods against FNA for any missing overloads or validation differences.
