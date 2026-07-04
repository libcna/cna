# XNA 4.0 API Coverage Audit

**Date:** 2026-06-26 (updated 2026-06-26 — Tasks 197–199)  
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
| `Microsoft::Xna::Framework::Input::Touch` | ✅ | ✅ | Stub |
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
  real. Not FAudio/FACT — SDL3_mixer is the backend, so 3D HRTF/Doppler and streaming-wavebank
  offset/packetSize params are documented accepted deviations, not stubs (`CHECKLIST.md`).
- `Microphone`: **Implemented.** Real SDL3 capture device enumeration, `Start()`/`Stop()`,
  `GetData()`/`GetQueuedBytes()`, `BufferReady` event.
- `DynamicSoundEffectInstance`: **Implemented.** Real buffer queue via `SDL_AudioStream`,
  `BufferNeeded` event pumped by `FrameworkDispatcher::Update()`, `PendingBufferCount` tracks real
  stream consumption.
- `SoundEffect`, `SoundEffectInstance`: **Implemented** (SDL3_mixer backend). Move-only with
  instance-tracking `Dispose()` cascade; real low/high/band-pass filters; reverb and full 3D
  HRTF/Doppler remain documented no-ops (SDL3_mixer has no aux-send bus or 3D audio graph).
- **Status:** Implemented, with a small set of accepted deviations documented in `CHECKLIST.md`
  (stereo hard-pan instead of crossfeed, no reverb/Doppler/HRTF, `instanceLimit`/fade parsed but
  not enforced, `IsPlaying`/`IsPaused` mutually exclusive unlike real FACT) — none of these are
  "unimplemented," they are deliberate, reasoned SDL3_mixer-backend trade-offs.

#### Audio compatibility table (`P9-DOCS-005`)

A concise summary of every known-and-documented gap versus real XNA 4.0/FNA. Full rationale for
each row is in `CHECKLIST.md`'s "Known acceptable C++ deviations" table (search for `Audio:`);
this table exists to answer "is X implemented?" at a glance without reading every row there.

| Bucket | Behavior |
|---|---|
| **Implemented** (matches FNA/XNA 4.0) | `SoundEffect` construction (file/buffer/buffer+range+loop), `Play`/`CreateInstance`, move-only Dispose cascade to every live instance • `SoundEffectInstance` Play/Pause/Resume/Stop/Volume/Pan/Pitch/IsLooped, real low/high/band-pass filters, `Resume()`-plays-if-never-started quirk • `DynamicSoundEffectInstance` SubmitBuffer/SubmitFloatBufferEXT, `BufferNeeded`, real `PendingBufferCount` • `AudioEngine` `.xgs` parsing, categories, global variables, `Update()` lifecycle sweep, Dispose cascade • `SoundBank`/`WaveBank` `.xsb`/`.xwb` parsing (compact + non-compact + ADPCM), `PlayCue`, `GetCue`, real lazy streaming reads • `Cue` playback, natural-completion state reconciliation, weighted-lottery variation selection, category routing • `AudioCategory` Pause/Resume/Stop/SetVolume against real active cues over a mutation-safe snapshot • `Microphone` real SDL3 capture • `AudioListener`/`AudioEmitter`/`RendererDetail`/all enums |
| **Approximate** (real effect, not bit-exact vs FNA/XAudio2/FACT) | 3D audio is pan + distance-attenuation only, no elevation/HRTF • stereo hard-pan (`Pan`=±1) eliminates the opposite channel instead of crossfeed-blending it (mono is bit-exact) • a bounded loop region truncates the *entire* track at `loopStart+loopLength`, not just later iterations (`MIX_PROP_PLAY_MAX_FRAME_NUMBER` has no per-iteration distinction) • interactive-type (`type==3`) XACT variation tables use a uniform pick instead of a variable-driven one |
| **Intentionally unsupported** (documented, no plan to implement) | Doppler/velocity-based pitch shift (stored, never applied) • reverb (`INTERNAL_applyReverb` is a no-op — no aux-send/return bus in SDL3_mixer) • `NoAudioHardwareException` never thrown (CNA always reports exactly one renderer) • `AudioEngine`/`SoundBank`/`WaveBank` silently stub instead of throwing on a missing/corrupt file |
| **Not yet implemented / open decision** | XACT category `instanceLimit`/`fadeInMS`/`fadeOutMS` are parsed but never enforced • `Cue::IsPlaying`/`IsPaused` are mutually exclusive, unlike real FACT (found `P9-LIFECYCLE-013`, decision on whether to fix pending) |

#### SDL3_mixer vs FAudio/FACT backend limitations (`P9-DOCS-006`)

CNA's audio backend is **SDL3_mixer**, not FAudio/FACT — XACT content (`.xgs`/`.xsb`/`.xwb`) is
parsed by a hand-written `CNA::Internal::Audio::XactParser` and played through SDL3_mixer's own
mixing graph, which is structurally different from FAudio's XAudio2-derived voice graph. Concrete
consequences, all downstream of this one architectural choice:

- **No `F3DAudio` equivalent.** SDL3_mixer has no positional-audio DSP graph; 3D sound is
  approximated at the CNA layer (pan + linear distance attenuation), not computed by the backend.
- **No per-source velocity/Doppler.** `DopplerScale`/`SpeedOfSound`/emitter velocity are stored
  (API-complete) but have nothing to feed into — no backend hook applies them to pitch.
  Correspondingly XACT `FACT3DApply`-style dedicated 3D application from `Cue` also is not the
  same call path — CNA approximates it via `SoundEffectInstance::Apply3D` on every playing wave.
- **No aux-send/return bus.** FAudio (like XAudio2) supports a shared reverb submix voice;
  SDL3_mixer has no equivalent routing concept, so reverb has no backend primitive to attach to.
- **Only one "cooked" (post-mix, pre-output) callback slot per track.** This is why the DSP filter
  (`T-4C`) and a hypothetical stereo-crossfeed pan implementation (`CP-19`) can't coexist without a
  real redesign — SDL3_mixer's per-track callback isn't a chain, it's a single slot.
- **Stereo panning is a 2-value gain pair (`MIX_StereoGains`), not a 4-coefficient output matrix.**
  FAudio/XAudio2 can crossfeed (send part of the left channel to the right output and vice versa);
  SDL3_mixer's stereo pan can only scale each channel's own output, so a hard pan silences the
  opposite channel instead of blending it in.
- **Loop region is a single "stop at frame N" property (`MIX_PROP_PLAY_MAX_FRAME_NUMBER`), not a
  distinct per-iteration `LoopBegin`/`LoopLength` pair.** FNA/XAudio2 only start truncating at the
  loop boundary on the *second and later* pass through the audio; SDL3_mixer's single max-frame
  property truncates unconditionally, including the first playthrough.
- **Global master gain is a real, live mixer-level primitive** (`MIX_SetMixerGain`/
  `MIX_GetMixerGain`) — unlike some of the above, this is a case where SDL3_mixer's model is
  *simpler and better suited* than re-deriving master volume per-track would have been (`CP-16`).

#### FNA-matching vs CNA-specific compromise (`P9-DOCS-007`)

Two different kinds of "doesn't match FNA line-for-line" show up in this module, and it matters
which one a given deviation is:

1. **Deliberate CNA-specific compromises**, forced by the SDL3_mixer backend choice — the entire
   "SDL3_mixer vs FAudio/FACT" list above, plus the interactive-variation uniform-pick fallback
   (parser limitation, not backend). These are documented in `CHECKLIST.md` precisely because they
   are permanent, reasoned trade-offs, not bugs to eventually fix.
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

### `Microsoft::Xna::Framework::Input::Touch`

- `TouchPanel`, `TouchPanelCapabilities`, `TouchCollection`, `TouchLocation`, `TouchLocationState`, `GestureSample`, `GestureType`: headers exist.
- No SDL3 touch backend is wired up yet.
- **Status:** Stub

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
| `SoundEffect / SoundEffectInstance` | ~95 % | SDL3_mixer backend; real filters, instance-tracking cascade; 3D is pan+attenuation only (no HRTF/Doppler, documented) |
| `MediaPlayer / VideoPlayer` | ~85 % | FFmpeg video; SDL_mixer audio; Album/Artist/Genre stub |
| `ContentManager` | ~65 % | File-extension readers; no XNB; no ServiceProvider property |
| `StorageDevice / StorageContainer` | ~90 % | Native filesystem; full XNA API shape |
| `GamePad / Keyboard / Mouse` | ~90 % | SDL3 backend; tested |
| `Input::Touch` | ~10 % | API declared; no SDL3 touch backend wired |
| `GamerServices` | ~5 % | `Guide` stub only |
| `Audio (XACT)` — AudioEngine/SoundBank/WaveBank/Cue | ~90 % | Real `.xgs`/`.xsb`/`.xwb` parser + SDL3_mixer playback; category/lifecycle/3D all real; gaps are documented accepted deviations (no HRTF/Doppler, `instanceLimit`/fade parsed not enforced), not missing implementation |
| `Framework.Net` (NetworkSession, etc.) | 0 % | Xbox Live exclusive; intentionally excluded |
| **Overall (EasyGL backend, 2D+3D game)** | **~83 %** | Main gaps: Touch, GamerServices, XNB content pipeline (XACT audio moved from "not implemented" to "implemented with documented deviations" — see Audio row above) |

---

## 10. Recommended Implementation Order

1. **Math / common framework types** — ✅ Done: Color, Vector2/3/4, Matrix, Quaternion, BoundingBox/Sphere/Frustum, Ray, Plane, Curve, MathHelper, Point, Rectangle, GameTime.

2. **Input** — ✅ Done: GamePad, Keyboard, Mouse, Touch (stubs).

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
- SDL3 touch backend for `TouchPanel`
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

1. Wire up SDL3 touch backend for `TouchPanel` (currently 0 % functional).
2. Add Vulkan pixel tests for `BasicEffect`, `AlphaTestEffect`, `SkinnedEffect` (see §7 known gaps).
3. Unblock Bgfx 3D state (`SetDepthTestEnabled`, `SetBlendEnabled`) to enable 3D pixel tests on Bgfx.
4. Add compile-compatibility stubs for `Gamer` / `SignedInGamer` / `GamerCollection` if target games need them.
6. Audit `GraphicsDevice` public methods against FNA for any missing overloads or validation differences.
