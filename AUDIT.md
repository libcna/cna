# CNA XNA 4.0 API Audit

Systematic per-class, per-method comparison: FNA (reference) vs CNA (implementation).

**Legend:**
- ✅ Done — class fully audited, all missing methods added as stubs or implemented
- 🔄 In progress
- ⬜ Not yet audited

For intentionally excluded items see `docs/xna-4-api-coverage.md`.

> **C ABI tracking note (2026-08-15):** `modules/c-api/` remains an experimental C17 ABI and is
> complete through `CBIND-035` (all slices closed): the graphics-resource, texture, vertex/index buffer, effect and
> model/animation families are mapped, and the graphics-device family now has its Viewport values,
> clear/status/2D-policy identities, complete device state, events and exception conversion, and
> stage-addressed texture collections, frame control, buffer binding, complete draw submission,
> SpriteBatch text, occlusion queries and the complete extended graphics layer. `CBIND-036A` adds
> the complete `storage` module: owned storage devices, containers and file streams, count/copy
> directory and file listings, and synchronous equivalents of the canonical fake-async selector and
> container pairs that still invoke the completion callback. `CBIND-036B` then completes the whole
> `content` module: asset-path/cache-key queries, loader registration, graphics-device get/set,
> manifest and `.xnb` reader-usage snapshots, typed Texture2D, TextureCube and SoundEffect load
> routes, and the compiled-asset reader pipeline with its owned readers, type readers and
> process-wide registry. `CBIND-036C` adds the network identity enumerations, the
> quality-of-service value, owned session-property lists with enumerators, owned packet read and
> write buffers, and the join-failure conversion with its per-thread join-error record.
> `CBIND-036D` adds owned network gamers and machines and the seven event descriptions, and
> `CBIND-036E1` adds discovered sessions and their collection, and `CBIND-036E2` adds the session
> object itself with its rosters, state and gamer management plus the minimum signed-in-gamer
> surface a session needs to exist, `CBIND-036E3` adds the ten session event registrations and
> `CBIND-036E4` adds discovery, join and the collapsed fake-async pairs, and `CBIND-036E5` adds the
> local gamers that close parent `CBIND-036`: every receive and send overload, the borrowed sender
> view and the two CNAEXT queue routes, leaving the whole `net` module mapped. `CBIND-037A` then
> opens `CBIND-037` by closing CNA's own `core` module: the logger, the process-wide minimum level,
> the compile-time platform, desktop operating system, renderer identity and renderer name, and
> both backend classifications for any of the 46 public renderer identities, and `CBIND-037B1`
> adds the gamepad controller type and the complete gamepad-capabilities value, and `CBIND-037B2`
> adds the five gamepad value types — button set, directional pad, thumbsticks, triggers and the
> snapshot itself — with every constructor, comparison, hash and string operation, and
> `CBIND-037B3` adds the `GamePad` statics: vibration, light bar, sensors, player index, power
> info, button labels, the four identity strings, connection state and the touchpad queries. The
> `CBIND-037B4a` adds the keyboard: the whole `KeyboardState` value surface, the key-state
> identity and every `Keyboard` static including both key-name families. The
> `CBIND-037B4b` adds the mouse, including the static clicked event as an owned registration,
> `CBIND-037B4c` adds the mouse cursor with its stock singletons as borrowed views, and
> `CBIND-037B4d` closes the input module's text surface: all three `TextInputEXT` events as owned
> registrations, the activation and screen-keyboard queries, the typed start and the input
> rectangle. `CBIND-037B5` then adds touch and gestures: the gesture type and sample, the touch
> location and collection value surfaces over the existing fixed snapshot, and every `TouchPanel`
> static. The
> generated inventory currently records 4,284 implemented, 30 partial, 1,975 planned and 126
> not-applicable public/protected declarations. This
> is not complete CNA C API coverage; status
> and the required full-surface mapping remain tracked in `plan_binding.md` and
> `docs/c-api/COVERAGE.md`.

---

## `Microsoft::Xna::Framework` (root)

Audited in batch. Agent comparison found the following gaps; all were confirmed
and corrected or confirmed as expected C++ adaptations.

| Class / Enum | Status | Notes |
|---|---|---|
| BoundingBox | ✅ | API complete |
| BoundingFrustum | ✅ | API complete |
| BoundingSphere | ✅ | API complete |
| Color | ✅ | All 141 named constants + methods present |
| ContainmentType (enum) | ✅ | Complete |
| Curve | ✅ | API complete |
| CurveContinuity (enum) | ✅ | Complete |
| CurveKey | ✅ | API complete |
| CurveKeyCollection | ✅ | API complete |
| CurveLoopType (enum) | ✅ | Complete |
| CurveTangent (enum) | ✅ | Complete |
| DisplayOrientation (enum) | ✅ | Complete |
| DrawableGameComponent | ✅ | API complete |
| ExitingEventArgs | ✅ | CNA-specific addition matching XNA pattern |
| FrameworkDispatcher | ✅ | CNA-specific addition |
| Game | ✅ | API complete |
| GameComponent | ✅ | API complete |
| GameComponentCollection | ✅ | API complete |
| GameComponentCollectionEventArgs | ✅ | API complete |
| GameServiceContainer | ✅ | API complete |
| GameTime | ✅ | API complete |
| GameWindow | ✅ | API complete (concrete vs abstract is expected) |
| GraphicsDeviceInformation | ✅ | API complete |
| GraphicsDeviceManager | ✅ | API complete |
| IDrawable | ✅ | Complete |
| IGameComponent | ✅ | Complete |
| IGraphicsDeviceManager | ✅ | Complete |
| IUpdateable | ✅ | Complete |
| LaunchParameters | ✅ | API complete |
| MathHelper | ✅ | API complete |
| Matrix | ✅ | API complete |
| Plane | ✅ | API complete |
| PlaneIntersectionType (enum) | ✅ | Complete |
| PlayerIndex (enum) | ✅ | Complete |
| Point | ✅ | API complete |
| PreparingDeviceSettingsEventArgs | ✅ | API complete |
| Quaternion | ✅ | API complete |
| Ray | ✅ | API complete |
| Rectangle | ✅ | API complete |
| TitleContainer | ✅ | API complete |
| TitleLocation | ✅ | API complete |
| Vector2 | ✅ | API complete |
| Vector3 | ✅ | API complete |
| Vector4 | ✅ | API complete |

---

## `Microsoft::Xna::Framework::Audio`

> **Last synchronized against real code: 2026-07-16 (Phase 13 audit, `plan_audio.md`).** For the
> full, up-to-date compatibility table (implemented / approximate / intentionally unsupported /
> not yet implemented) and the SDL3_mixer-vs-FAudio backend limitations behind these notes, see
> `docs/xna-4-api-coverage.md`'s Audio section. This table is a per-class summary only.

| Class / Enum | Status | Notes |
|---|---|---|
| AudioCategory | ✅ | API complete; `Pause`/`Resume`/`Stop`/`SetVolume` route to every real active `Cue` in the category over a mutation-safe snapshot (`P9-CATEGORY-001`), not a live-iterated list; `SetVolume` retroactively re-applies to already-playing instances (`T-4D`); real category-level `instanceLimit`/`maxInstanceBehavior` enforcement and fade-in/fade-out within instance-limit replacement (`P9-CATEGORY-005..010`) |
| AudioChannels (enum) | ✅ | Complete |
| AudioEmitter | ✅ | API complete |
| AudioEngine | ✅ | API complete; real `System::` exceptions; validated `GetCategory`/`SetGlobalVariable`; category-volume live re-apply to already-playing cues (`T-4D`); `Update()` sweeps every registered `SoundBank`'s finished fire-and-forget cues (`P9-LIFECYCLE-008`); `Dispose()` cascades to every `WaveBank`/`SoundBank`/`Cue` it created (`XA-8`). 3D pan/attenuation is real (see `Cue`/`SoundEffectInstance` below), not stubbed |
| AudioListener | ✅ | API complete |
| AudioStopOptions (enum) | ✅ | Complete |
| Cue | ✅ | API complete; real state machine that naturally reconciles `IsPlaying`/`IsPaused`/`IsStopped` once playback actually finishes (`P9-LIFECYCLE-001`, was previously stuck `Playing` forever); `Play()` rejects being called again on an already Playing/Paused/Stopped cue (`P9-LIFECYCLE-010`); `GetVariable`/`SetVariable` throw `ObjectDisposedException` (`P9-LIFECYCLE-015`); `Apply3D` forwards to `SoundEffectInstance::Apply3D` (`T-4B`, now listener-orientation-aware pan, `P9-3D-010`) — a real effect, not a no-op. `IsPlaying`/`IsPaused` can now coexist (`P9-LIFECYCLE-013`, **fixed**, not the mutually-exclusive behavior this row previously described — matches real FACT's independent bitmask semantics). Real cue-level `instanceLimit`/`maxInstanceBehavior` enforcement, checked before category-level (`P9-CATEGORY-011`). RPC volume/pitch continuously re-evaluated every tick, not just once at `Play()` (`P9-XACT-016`) |
| DynamicSoundEffectInstance | ✅ | API complete; `Pause`/`Resume` operate on the real `dynamicTrack_` (`CP-15`); `Resume()` starts playback when never-played, matching FNA (`P9-VALIDATION-010`); `SubmitBuffer`/`SubmitFloatBufferEXT` reject disposal (`P9-VALIDATION-011`) and validate `offset`/`count` overflow-safely (`P9-VALIDATION-003`/`010`, fixes a real out-of-bounds read confirmed by a segfault); constructor intentionally performs no sample-rate/channel validation, matching FNA's own permissive (not MSDN-documented) behavior (`P10-DYN-001/002`) |
| InstancePlayLimitException | ✅ | Complete |
| Microphone | ✅ | API complete — real SDL3 capture (enumeration, Start/Stop, GetData/GetQueuedBytes); GetSampleDuration/GetSampleSizeInBytes delegates to SoundEffect (plan_audio.md MC-1, done); `CheckBuffer()` is private, matching FNA's `internal` (`MC-6`) |
| MicrophoneState (enum) | ✅ | Complete |
| NoAudioHardwareException | ✅ | Type complete; thrown at the actual point of failure when the SDL3_mixer device won't open (`SoundEffect`/`DynamicSoundEffectInstance`'s `GetMixerOrThrowXna()`, `plan_audio.md` P9-HARDWARE-002); `AudioEngine`'s own constructor never throws it, since CNA always reports exactly one renderer (accepted deviation, `CHECKLIST.md`, `plan_audio.md` XA-9) |
| NoMicrophoneConnectedException | ✅ | Complete |
| RendererDetail | ✅ | API complete |
| SoundBank | ✅ | API complete; real `IsInUse` (treats `IsPlaying \|\| IsPaused` as alive, `XA-7`) and `GetCue` (throws on invalid name); 3D `PlayCue` forwards to `Cue::Apply3D` (`T-4B`) — uses the real listener/emitter, doesn't ignore them; registers with `AudioEngine` for the `Dispose()` cascade (`XA-8`) |
| SoundEffect | ✅ | Implemented (SDL3_mixer); move-only with real instance-tracking + `Dispose()` cascade to every live `SoundEffectInstance` (`T-3G`); `MasterVolume` reads/writes the real live SDL3_mixer master gain (`CP-16`); loop region (`loopStart`/`loopLength`) actually applied at `Play()`, `FromStream` parses the WAV `smpl` chunk (`CP-17`/`CP-23`); buffer/range constructor validates `offset`/`count` overflow-safely (`P9-VALIDATION-003`, fixes a real out-of-bounds read confirmed by a segfault) |
| SoundEffectInstance | ✅ | Implemented (SDL3_mixer); real low/high/band-pass filters via a per-track callback, reverb stays a documented no-op (`T-4C`, ThreadSanitizer-verified race-free under real concurrent mixing-thread load); `Apply3D` is a real, listener-orientation-aware pan (`P9-3D-010`) + distance-attenuation approximation (`CP-3`); `Resume()` starts playback when never-played or after `Dispose()` (`P9-VALIDATION-010`, matches FNA's own quirk) |
| SoundState (enum) | ✅ | Complete |
| WaveBank | ✅ | API complete; real `IsInUse`; streaming ctor does real lazy per-entry disk reads (`T-3F`) — only the non-streaming ctor loads the whole file eagerly |

---

## `Microsoft::Xna::Framework::Content`

| Class | Status | Notes |
|---|---|---|
| ContentLoadException | ✅ | Complete |
| ContentManager | ✅ | API complete (non-XNB approach) |
| ContentTypeReader\<T\> | ✅ | Complete |
| ResourceContentManager | ✅ | Stub added (throws until implemented) |

---

## `Microsoft::Xna::Framework::Graphics`

Partial audit via agent. Key gaps identified and fixed: SpriteBatch Draw overloads added as stubs.

| Class / Enum | Status | Notes |
|---|---|---|
| AlphaTestEffect | ✅ | API surface present (stub behavior) |
| BasicEffect | ✅ | API complete |
| Blend (enum) | ✅ | Complete |
| BlendFunction (enum) | ✅ | Complete |
| BlendState | ✅ | API complete. Vulkan's implementation used to be almost entirely fake (blend equations/factors hardcoded per-pipeline, largely disconnected from what `BlendState` actually requests, confirmed repeatedly via pixel tests — 5 known-failing Vulkan regression tests every run, e.g. `Vulkan_BlendState_AlphaBlend`/`Additive`/`SeparateFunctions`/`SeparateFactors`/`BlendFactor`) — **fixed, Task 868** (commit `459a0e37`, 2026-07-09): `ToVkBlendFactor()` now maps all 13 XNA `Blend` values across all 9 3D pipeline-creation sites. EasyGL/Bgfx are also correct and pixel-verified (see e.g. Task 467's `BlendState::Additive` golden-image case, EasyGL-only). |
| BufferUsage (enum) | ✅ | Complete |
| ClearOptions (enum) | ✅ | Complete |
| ColorWriteChannels (enum) | ✅ | Complete |
| CompareFunction (enum) | ✅ | Complete |
| CubeMapFace (enum) | ✅ | Complete |
| CullMode (enum) | ✅ | Complete |
| DepthFormat (enum) | ✅ | Complete |
| DepthStencilState | ✅ | API complete. Vulkan's implementation used to be almost entirely fake (hardcoded `depthCompareOp`, stencil parameters dropped entirely) — **fixed, Task 870** (2026-07-09 re-audit confirms this is resolved, not still-open): real per-pipeline depth-compare op, full front/back `VkStencilOpState`, stencil reference/masks as true dynamic state. `GraphicsDevice.ReferenceStencil`'s independent-override behavior is now connected on Vulkan (`vkCmdSetStencilReference`, an undocumented side effect of Task 870) but still has zero backend connection on EasyGL/Bgfx — Task 872, still open there. `ClearOptions::Stencil` is also still ignored by `GraphicsDevice::Clear` on all backends — Task 871, still open. |
| DeviceLostException | ✅ | Complete |
| DeviceNotResetException | ✅ | Complete |
| DirectionalLight | ✅ | API complete |
| DisplayMode | ✅ | API complete |
| DisplayModeCollection | ✅ | API complete |
| DualTextureEffect | ✅ | API surface present (stub behavior) |
| DynamicIndexBuffer | ✅ | API complete |
| DynamicVertexBuffer | ✅ | API complete |
| Effect | ✅ | API complete |
| EffectAnnotation | ✅ | API complete |
| EffectAnnotationCollection | ✅ | API complete |
| EffectMaterial | ✅ | API complete — trivial class in both FNA (single `EffectMaterial(Effect cloneSource) : base(cloneSource)` constructor, no other members) and CNA; previously missing from this table entirely, added 2026-07-09 re-audit. |
| EffectParameter | ✅ | API complete |
| EffectParameterClass (enum) | ✅ | Complete |
| EffectParameterCollection | ✅ | API complete |
| EffectParameterType (enum) | ✅ | Complete |
| EffectPass | ✅ | API complete |
| EffectPassCollection | ✅ | API complete |
| EffectTechnique | ✅ | API complete |
| EffectTechniqueCollection | ✅ | API complete |
| EnvironmentMapEffect | ✅ | API surface present (stub behavior) |
| FillMode (enum) | ✅ | Complete |
| GraphicsAdapter | ✅ | API complete |
| GraphicsDevice | ✅ | Implemented; some overloads are stubs |
| GraphicsDeviceStatus (enum) | ✅ | Complete |
| GraphicsProfile (enum) | ✅ | Complete |
| GraphicsResource | ✅ | API complete |
| IEffectFog | ✅ | Complete |
| IEffectLights | ✅ | Complete |
| IEffectMatrices | ✅ | Complete |
| IGraphicsDeviceService | ✅ | Complete |
| IndexBuffer | ✅ | API complete |
| IndexElementSize (enum) | ✅ | Member names match FNA, and — **fixed, Task 921** (2026-07-09) — the underlying numeric values now do too: `SixteenBits = 0`, `ThirtyTwoBits = 1`, matching FNA's implicit sequential values exactly (previously `16`/`32`, apparently assuming the enum encoded a literal bit-width; found from Task 479's real FNA-vs-CNA JSON comparison, confirmed via both reading FNA's `IndexElementSize.cs` directly and the real running `FNA.dll`'s own reflection dump). |
| IRenderTarget | ✅ | Complete |
| IVertexType | ✅ | Complete |
| Model | ✅ | API complete. The non-default constructor used to auto-default `Root` to `bones[0]` with no way to specify a different root bone index (FNA's real `Model` constructor never sets `Root` at all — `ModelReader` assigns it externally from an explicit `rootBoneIndex`) — **fixed, Task 916** (2026-07-09): an additive optional `rootBoneIndex` parameter (default `0`, matching prior behavior) was added, so a hand-built CNA model whose true root isn't the first bone in `bones` can now be represented correctly. |
| ModelBone | ✅ | API complete |
| ModelBoneCollection | ✅ | API complete |
| ModelEffectCollection | ✅ | API complete |
| ModelMesh | ✅ | API complete |
| ModelMeshCollection | ✅ | API complete |
| ModelMeshPart | ✅ | API complete |
| ModelMeshPartCollection | ✅ | API complete |
| NoSuitableGraphicsDeviceException | ✅ | Complete |
| OcclusionQuery | ✅ | API complete; full 4-backend correctness audit done (Tasks 441-450, `docs/occlusionquery-support.md`). EasyGL: fully correct, pixel-verified both directions. **Vulkan: fixed, Task 447/854** (2026-07-10) — a real `VulkanOcclusionQueryBackend` now correlates each query's Begin/End span with its draw calls via `Pending3DDraw::occlusionQuery` tagging plus `vkCmdBeginQuery`/`vkCmdEndQuery` recording, verified both visible/occluded directions plus a multi-draw-span case. Bgfx: real fix shipped (Task 448) matching bgfx's own documented API, but this sandbox's software GL driver couldn't discriminate whether it changes observable behavior at all; a further gap (query attached to the same view as other geometry rather than a dedicated view, unlike bgfx's own reference example) is tracked as Task 917. SDL_Renderer correctly throws (2D-only, no occlusion queries in FNA's own 2D path either). |
| PresentationParameters | ✅ | API complete |
| PresentInterval (enum) | ✅ | Complete |
| PrimitiveType (enum) | ✅ | Complete |
| RasterizerState | ✅ | API complete (2026-07-09 re-audit: `CullMode` cross-backend correctness confirmed via a new golden-image case, Task 468). `FillMode::WireFrame` genuinely supported on Vulkan, correctly gated behind a real `vkGetPhysicalDeviceFeatures.fillModeNonSolid` query (Task 454). One known, unresolved `DepthBias` sub-case failure on Vulkan (`DepthBias=-1e6`; other sub-cases pass) — a narrow, already-isolated pre-existing failure, not investigated further this session. |
| RenderTarget2D | ✅ | API complete |
| RenderTargetBinding | ✅ | API complete |
| RenderTargetCube | ✅ | API complete |
| RenderTargetUsage (enum) | ✅ | Complete |
| ResourceCreatedEventArgs | ✅ | API complete |
| ResourceDestroyedEventArgs | ✅ | API complete |
| SamplerState | ✅ | API complete. `MaxAnisotropy`/`TextureFilter::Anisotropic` now has genuine support on all 3 3D backends: Vulkan via `samplerAnisotropy`-device-feature-gated support (Task 454); Bgfx via real `BGFX_SAMPLER_ANISOTROPIC` flags; **EasyGL — fixed, Task 918** (2026-07-09), previously silently falling back to plain trilinear filtering, now issues a real `GL_EXT_texture_filter_anisotropic` call, clamped to the live driver cap. SDL_Renderer has no anisotropic filtering at all (2D-only, by design). |
| SamplerStateCollection | ✅ | API complete |
| SetDataOptions (enum) | ✅ | Complete |
| ShaderEffect | ✅ | NOXNA — not part of the XNA 4.0 API. GLSL-source-based custom effect, previously missing from this table entirely, added 2026-07-09 re-audit. Cross-backend support is narrower than the class's own doc comment ("GLSL-source-based … loaded from vertex and fragment shader strings") implies: `ShaderEffect`'s constructor forwards its `vertSrc`/`fragSrc` strings unmodified to whichever backend is active, with no GLSL→SPIR-V conversion in `ShaderEffect.cpp` itself. **EasyGL**: genuine live GLSL compilation via the real GL driver. **Vulkan**: `VulkanEffectBackend::CompileProgram`'s own parameters are named `vertSpv`/`fragSpv` and it validates `size() % 4 == 0` before calling `vkCreateShaderModule` — it expects pre-compiled raw SPIR-V bytecode, not GLSL text, despite sharing the same constructor signature; passing real GLSL source through on a Vulkan-backed device would fail immediately. **Bgfx**: already documented (Task 455) as requiring pre-compiled binary shaders too (`CompileProgram` always returns `false`); this is a non-silent, queryable-status design (`IsValid()`/`GetCompileError()`/`IsEffectValid()`), not a bug. **SDL_Renderer**: no `IEffectBackend` implementation at all (2D-only, no programmable shader stage). Net effect: only EasyGL currently supports the class's own documented "load from GLSL source" contract as written. |
| SkinnedEffect | ✅ | API surface present (stub behavior) |
| SpriteBatch | ✅ | Missing Draw overloads added as stubs. `SamplerState`/`TextureAddressMode` real cross-backend status (2026-07-09 re-audit, corrects a stale claim below): **EasyGL** — fully applied (Task 269). **Vulkan** — `Filter`+`AddressU`/`AddressV` now genuinely applied (Task 665, fixed a real bug where `Begin()`'s SamplerState had no effect at all); see "NPOT textures and SpriteBatch edge sampling" below for the original EasyGL finding. **Bgfx** — also genuinely applies both (`BgfxSpriteBatchBackend::SetSamplerFilter`/`SetSamplerAddressMode`, verified wired through to `ApplySamplerState` before each flush) — not a no-op. **SDL_Renderer** — `Filter` (Point/Linear) is honored, but `TextureAddressMode::Wrap`/`Mirror` are not (`SDL_RenderTexture`'s fixed edge behavior; a real fix needs an `SDL_RenderGeometry`-based rewrite) — **BLOCKED, Tasks 686/687**, awaiting a project-owner decision on scope (3 options, `docs/sdl-renderer-2d-completeness.md` §11). |
| SpriteEffect | ✅ | API surface present |
| SpriteEffects (enum) | ✅ | Complete |
| SpriteFont | ✅ | API complete |
| SpriteSortMode (enum) | ✅ | Complete |
| StencilOperation (enum) | ✅ | Complete |
| SurfaceFormat (enum) | ✅ | Complete |
| Texture | ✅ | API complete |
| Texture2D | 🔄 | Detailed re-audit (Task 261, Phase 32); 2 memory-safety bugs fixed (Task 266); missing `FromStream(w,h,zoom)` overload added + format support verified (Task 262); `SaveAsPng`/`SaveAsJpeg` round-trip verified + JPEG quality fixed (Tasks 263–264); missing `NOXNA` tags fixed. Still open: missing `SetDataPointerEXT`/`GetDataPointerEXT`/`TextureDataFromStreamEXT`/`DDSFromStreamEXT`, and Color-only format support — see below |
| Texture3D | ✅ | Detailed audit (Task 271, Phase 33): fixed `LevelCount` hardcoded to 1 (ignored `mipMap`), fixed missing null/count/startIndex/box-bounds guards on `SetData`/`GetData` (crash + OOB read/write risks), fixed missing `Dispose(bool)` override (GPU resource leak on explicit Dispose). See below. **SDL_Renderer note (2026-07-09 re-audit)**: construction currently succeeds silently with a permanently-null backend on this one backend (`SdlGraphicsBackend` never overrides `CreateTexture3D`), and `SetData`/`GetData` silently no-op rather than throw — **BLOCKED, Task 725**, awaiting a project-owner decision (94 existing shared cross-backend tests construct this type directly with no backend-specific guard today, giving any fix here a real, non-trivial blast radius; 3 options considered, none guessed at, see `docs/sdl-renderer-2d-completeness.md` §11). EasyGL/Vulkan/Bgfx are unaffected. |
| TextureAddressMode (enum) | ✅ | Complete |
| TextureCollection | ✅ | API complete |
| TextureCube | ✅ | Detailed audit (Task 272, Phase 33): fixed the same 3 bug classes as `Texture3D` (hardcoded `LevelCount`, missing `SetData`/`GetData` guards, missing `Dispose(bool)`), plus 2 `TextureCube`-specific findings: a missing `SetData`/`GetData(face,data,startIndex,elementCount)` overload (added), and a `rect==nullptr`-at-`level>0` bug that ignored mip-level dimensions entirely (fixed). `DDSFromStreamEXT` is now a real implementation (Task 663) — DDS header parsing (mirrors FNA's `Texture.ParseDDS`), per-face/per-level DXT1/3/5 decode via `DxtUtil`, uploaded as `SurfaceFormat::Color` (CNA doesn't implement compressed GPU formats end-to-end on any backend, matching `Texture2D::FromStream`'s own established precedent). See below. **SDL_Renderer note (2026-07-09 re-audit)**: same silent-null-backend construction gap as `Texture3D` above, same **BLOCKED, Task 725** decision (94 existing tests span both types; a single decision covers both). **Bgfx note**: `GetData`'s real GPU readback path (Task 914) can silently fail with zero diagnostic on hardware lacking `BGFX_CAPS_TEXTURE_BLIT`/`READ_BACK` — now logs clearly instead (Task 455). |
| TextureFilter (enum) | ✅ | Complete |
| VertexBuffer | ✅ | API complete |
| VertexBufferBinding | ✅ | API complete |
| VertexDeclaration | ✅ | API complete |
| VertexElement | ✅ | API complete |
| VertexElementFormat (enum) | ✅ | Complete |
| VertexElementUsage (enum) | ✅ | Complete |
| VertexPositionColor | ✅ | API complete |
| VertexPositionColorTexture | ✅ | API complete |
| VertexPositionNormalTexture | ✅ | API complete |
| VertexPositionNormalTextureSkinned | ✅ | NOXNA — not part of the XNA 4.0 API. GPU-skinned vertex (position/normal/texcoord/4 blend weights/4 blend indices, 52-byte logical layout) added for the Avatar real-rendering extension (see `docs/avatar-real-rendering-ext.md`); matching `VertexBuffer::SetData` overloads added |
| VertexPositionTexture | ✅ | API complete |
| Viewport | ✅ | API complete |
| SkinnedModelEXT | ✅ | NOXNA — not part of the XNA 4.0 API. Real, GPU-skinnable mesh + skeleton + animation-clip container for the Avatar real-rendering extension. Deliberately not built on `Model`/`ModelBone`/`ModelMesh` (those encode rigid multi-part model animation, the wrong shape for per-vertex GPU skinning). Its bone hierarchy is entirely independent of the real Xbox Avatar 71-bone arrays. Loaded via a new `SkinnedModelTypeReader` (`.skinnedmodel.json`/`.skeleton.bin`/`.clip.bin`) registered in `ContentManager` |

---

### Texture2D detailed audit (Task 261, Phase 32)

Line-by-line comparison of `include/.../Texture2D.hpp` + `src/.../Texture2D.cpp` against
`FNA/src/Graphics/Texture2D.cs` (635 lines) and the shared helpers in `FNA/src/Graphics/Texture.cs`.
No code was changed for this task — audit only. Findings below feed Phase 32 tasks 262–270.

#### Confirmed bugs — FIXED in Task 266

1. **~~Heap buffer overflow — OOB write~~ FIXED.**
   `Texture2D::SetData(int level, const Rectangle* rect, const Color* data, int startIndex, int elementCount)`
   (`Texture2D.cpp:197-252`) validated `elementCount < w*h` but never validated that the
   `rect` (`x, y, w, h`) actually fit inside the mip level's dimensions (`levelW`, `levelH`). The write
   loop computes `dst = ((y+row)*levelW + (x+col)) * 4` and writes directly into `buf` (sized
   `levelW*levelH*4`) with no clamping — a caller-supplied `Rectangle` with `x+w > levelW` or
   `y+h > levelH` (or negative `x`/`y`) would write past the end of the CPU-side mip buffer.
   The sibling method `GetData(int level, const Rectangle* rect, ...)` already had this exact check
   (`Texture2D.cpp:317`), so the omission in `SetData` was an asymmetry, not an intentional design
   choice. **Fix (Task 266):** added the identical bounds check to `SetData`, throwing
   `std::out_of_range("Texture2D::SetData: rectangle out of texture bounds")` before any write.
   Regression tests: `SetDataLevelRectXOutOfBoundsThrowsOutOfRange`,
   `SetDataLevelRectYOutOfBoundsThrowsOutOfRange`, `SetDataLevelRectNegativeXThrowsOutOfRange`,
   `SetDataLevelRectNegativeYThrowsOutOfRange`, `SetDataLevelRectWithinBoundsDoesNotThrow`.

2. **~~Heap buffer overflow — OOB read~~ FIXED.**
   `Texture2D::SetData(const Color* data, int elementCount)` (`Texture2D.cpp:177-195`, the simple
   2-arg overload) built an `ImageData` with `img.width = width; img.height = height;` (the texture's
   full dimensions) but sized `img.pixels` to only `elementCount * 4` bytes. If a caller passed
   `elementCount < width*height`, the resulting `ImageData` claimed full-size dimensions over an
   undersized buffer. The EasyGL backend's `EasyGLTextureBackend` constructor
   (`EasyGLGraphicsBackend.cpp:342`) calls `texture.set_image_2d(..., width, height, data.pixels.data())`,
   which reads `width*height*4` bytes from `data.pixels.data()` regardless of the vector's actual
   size — an out-of-bounds read. FNA's equivalent (`SetData<T>(T[] data)`, delegating to the 5-arg
   overload) explicitly validates `requiredBytes > availableBytes` and throws
   `ArgumentOutOfRangeException` before touching the texture. **Fix (Task 266):** added a
   `elementCount < width*height` check that throws `std::out_of_range`, and the pixel buffer /
   loop bound are now always sized to exactly `width*height` (matching `img.width`/`img.height`),
   eliminating the size mismatch. Regression tests (`SetDataSimpleGuardTest` fixture, requires a
   real `GraphicsDevice`): `InsufficientElementCountThrowsOutOfRange`, `ExactElementCountDoesNotThrow`.

#### Missing overloads / methods (present in FNA, absent in CNA)

3. **~~`static Texture2D FromStream(GraphicsDevice&, Stream&, int width, int height, bool zoom)`~~
   ADDED (Task 262).** Implements FNA3D's resize/crop-while-decoding semantics via
   `SDL_CreateSurfaceFrom` + `SDL_BlitSurfaceScaled`. Also verified (Task 262) that
   `FromStream` correctly decodes PNG, JPEG, and BMP via the linked SDL3_image build, in
   addition to the pre-existing DDS/DXT1/3/5 support — see `docs/texture-stream-formats.md`.
4. `SetDataPointerEXT(int level, Rectangle? rect, IntPtr data, int dataLength)` — no equivalent.
   The closest CNA method, `SetDataRGBA(const uint8_t*, int pixelCount)` (NOXNA), has a different
   signature (no `level`, no `rect`, always targets the full level-0 image) and does not validate
   that `pixelCount` matches `width*height` before calling `backend_->UpdatePixels` (same class of
   bug as finding #2, lower severity since it's a NOXNA extension, not core XNA surface).
5. `GetDataPointerEXT(int level, Rectangle? rect, IntPtr data, int dataLengthBytes)` — no equivalent
   at all, not even a NOXNA one.
6. `static void TextureDataFromStreamEXT(Stream, out width, out height, out byte[] pixels, ...)` —
   missing entirely.
7. `static Texture2D DDSFromStreamEXT(GraphicsDevice&, Stream&)` — missing as a *named* method.
   CNA does decode DDS/DXT1/3/5, but the logic is folded silently into `FromStream()`
   (`TryDecodeDds` helper, `Texture2D.cpp:341-376`) rather than exposed as its own public static
   method matching FNA's API surface. Functionally similar, but the public shape differs and
   `FromStream` in FNA does **not** auto-detect DDS (it always goes through the image decoder,
   throwing on unsupported formats) — this is a behavioral divergence worth documenting even
   though it's arguably a usability improvement.

#### Missing `NOXNA` tags (CLAUDE.md compliance) — FIXED

8. **~~`Texture2D(const std::string& assetName)` and
   `Texture2D(const std::string& assetName, GraphicsDevice& graphicsDevice)` missing `NOXNA`~~
   FIXED.** These constructors (`Texture2D.hpp:40,46`) are **not part of the FNA/XNA 4.0
   `Texture2D` API** — real XNA loads textures via `Texture2D.FromStream` or the content
   pipeline, never a direct filename constructor. They are CNA-only conveniences and per
   CLAUDE.md must be wrapped in `NOXNA`, exactly like the project's own established precedent:
   `SoundEffect(const std::string& assetName)` (`Audio/SoundEffect.hpp:49`) is correctly marked
   `NOXNA explicit`. Both `Texture2D` constructors are now tagged `NOXNA` the same way. Purely a
   marker-macro addition — no behavior change; 1808/1808 unit tests still pass.

#### Format support gap (affects the whole SetData/GetData story)

9. `Texture::ValidateFormat()` throws for anything other than `SurfaceFormat::Color`, so the
   `Texture2D(GraphicsDevice&, int, int, bool, SurfaceFormat)` constructor can only actually produce
   Color-format textures today, even though FNA supports DXT1/3/5, Bgra4444/5551, Bgr565,
   Rgba1010102, Rg32, Rgba64, Single/Vector2/Vector4, Half* formats, etc. This also explains why
   CNA's `SetData`/`GetData` are hardcoded to `Color*` (4 bytes/pixel) instead of FNA's generic
   `SetData<T>`/`GetData<T>` pattern (validated against the format's actual byte size via
   `Texture.GetFormatSizeEXT`/`ValidateGetDataFormat`, which have no CNA equivalent at all). This is
   the root scope item behind Phase 32 tasks 265/266/268/269.

#### Behavioral deviations (lower priority)

10. `SetData(const Color*, int elementCount)` silently returns (no exception) when `data == nullptr`
    or `elementCount <= 0`, while `SetData(int level, ...)` throws `std::invalid_argument` for the
    same conditions. FNA's real `SetData<T>(T[] data)` always throws (it delegates to the validated
    5-arg overload). This inconsistency is already encoded as expected behavior in
    `Texture2DTests.cpp` (`SetDataSimpleWithNullDataDoesNotThrow`,
    `SetDataSimpleWithZeroCountDoesNotThrow`), so any fix must update those tests too.
11. **~~`SaveAsJpeg` hardcodes JPEG quality to 100~~ FIXED (Task 264).** Added a
    `GetJpegSaveQuality()` helper (`Texture2D.cpp`) that reads `FNA_GRAPHICS_JPEG_SAVE_QUALITY`,
    falling back to 100 if unset or unparseable — matches FNA's `SaveAsJpeg` exactly. Both
    `SaveAsJpeg` overloads (stream and filename) now call it instead of hardcoding `100`.
    Verified via `SaveAsJpegTest.QualityEnvVarIsHonoredWithoutThrowing`.

#### Confirmed correct / faithful to FNA

- Public constructors `(GraphicsDevice&, int, int)` and `(GraphicsDevice&, int, int, bool, SurfaceFormat)`
  structurally match FNA's two public constructors (the FNA `ArgumentNullException` for a null
  `graphicsDevice` doesn't apply — CNA takes a reference, matching the project's established
  "null guards omitted for C++ references" convention in `CHECKLIST.md`).
- `CalculateMipLevels` (private free function, `Texture2D.cpp:118`) is mathematically equivalent to
  FNA's `Texture.CalculateMipLevels` (both count halvings of `max(width,height)` until reaching 1).
- `GetData` overloads (3-arg, 2-arg, and the level+rect 5-arg form) correctly mirror FNA's three
  `GetData<T>` overloads, including the rect-bounds check missing from `SetData` (finding #1).
- `Width`/`Height` read-only property convention (`getWidthProperty()`/`getHeightProperty()`,
  no public setters) matches FNA's `{ get; private set; }`.

#### Texture2D CPU shadow storage (Task 270, Phase 32)

Audit of `cpuPixels_` (level-0 shadow) and `extraMipLevels_` (mip level >0 shadow) retention,
prompted by the NOXNA `GraphicsDevice::SetContextRecoveryEnabled(false)` optimization
(`MaybeFreeCpuPixels()`, `Texture2D.cpp:34-38`).

**What the CPU shadow is for.** Unlike FNA, which restores GL textures after context loss (Android
`onPause`/`onResume`, etc.) by keeping no persistent CPU copy — it just re-runs whatever code created
the texture — CNA instead keeps a `shared_ptr<vector<uint8_t>>` per texture level, shared with the
backend (`ITextureBackend::ShareCpuPixels`), and re-uploads it via `EasyGLTextureBackend::recreate_gl_resource()`
after a context loss. `SetContextRecoveryEnabled(false)` disables this (safe on desktop, where GL
contexts don't get torn down), freeing the level-0 shadow after every full upload to save roughly
1x the texture's RAM (`MaybeFreeCpuPixels`, called from every constructor and from
`SetData(Color*, int)`).

**Confirmed: `GetData` does *not* work correctly once the shadow is freed.** CNA's `ITextureBackend`
has no pixel-readback method (no `glGetTexImage` equivalent) — every `GetData` overload reads
exclusively from `cpuPixels_`/`extraMipLevels_` and throws `std::runtime_error` if the relevant
buffer is null or empty. So with context recovery disabled, `GetData(level 0)` throws on every call
after the first full upload, permanently, for the lifetime of the texture. FNA's real `GetData<T>`
has no such failure mode — it always does a genuine GPU readback. This is an intentional but
previously undocumented trade-off: `SetContextRecoveryEnabled(false)` is not just a memory
optimization, it makes `GetData()` (level 0) unusable. Pinned by
`ContextRecoveryTest.GetDataThrowsAfterFullUploadWithRecoveryDisabled` /
`GetDataWorksAfterFullUploadWithRecoveryEnabledByDefault` (`Texture2DTests.cpp`).

**Confirmed and fixed bug: partial `SetData(level, rect, ...)` could silently corrupt the GPU
texture when the shadow was freed.** `getMipBuffer(0)` lazily *resurrects* `cpuPixels_` as a
fresh zero-filled buffer whenever it is null (`Texture2D.cpp:48-56`), regardless of why it became
null. The level-0 branch of `SetData(level, rect, ...)` then re-uploads that *entire* resurrected
buffer via `backend_->UpdatePixels()`. If a caller had previously done a full upload (real GPU
content, e.g. all pixels `(5,5,5,5)`), then a later partial `SetData` covering only part of the
level would overwrite the *rest* of the GPU texture with black, because the resurrected shadow only
had the newly-written region filled in — the other pixels were zero, not the actual `(5,5,5,5)`
GPU content, and got re-uploaded as such. This could only happen with context recovery disabled
(the only way `cpuPixels_` becomes null after having real content). **Fix:** `SetData(level, rect, ...)`
now throws `std::runtime_error` when `level == 0`, a backend already exists, the shadow is currently
null, and the requested rectangle does not cover the entire level — refusing to guess instead of
silently corrupting already-uploaded pixels. A rect that does cover the full level is still allowed
(every pixel gets overwritten, so no stale zero-fill can leak through). The level-0 branch now also
calls `MaybeFreeCpuPixels()` after uploading, so the shadow doesn't outlive its usefulness just
because a partial update touched it (previously, any partial `SetData` call — even with context
recovery enabled — would leave the shadow retained forever, since only the full-array
`SetData(Color*, int)` path called `MaybeFreeCpuPixels()`). Regression tests:
`ContextRecoveryTest.PartialUpdateAfterShadowFreedThrowsInsteadOfCorruptingTexture`,
`PartialUpdateCoveringFullLevelDoesNotThrowEvenWithRecoveryDisabled`,
`PartialUpdateNeverThrowsWithRecoveryEnabledByDefault`.

**Documented gap, not fixed: `extraMipLevels_` (mip levels >0) is never freed.**
`MaybeFreeCpuPixels()` only resets `cpuPixels_` (level 0); the mip-level shadow buffers
(`extraMipLevels_`, `Texture2D.cpp:58-68`) are retained for the lifetime of the texture regardless
of `contextRecoveryEnabled_`. For a full mipmap chain this is a relatively small fraction of total
texture RAM (the geometric mip series past level 0 sums to ~1/3 of the base level), so the
memory-savings claim in `SetContextRecoveryEnabled`'s doc comment ("saves approximately one copy of
texture RAM per loaded texture") is accurate for level 0 but does not extend to mip levels >0. Left
undone here — deliberately narrow fix per Phase 32 scope (see `NEXT.md` §9, "no broad
`GetData`/`SetData` rewrite"); freeing `extraMipLevels_` would need the same silent-corruption
analysis as above, applied per mip level, which is a task of its own if it's ever needed.

---

### NPOT textures and SpriteBatch edge sampling (Tasks 268–269, Phase 32)

#### Task 268 — non-power-of-two (NPOT) textures

Audited every texture-creation path for POT-only special-casing: `Texture2D`'s constructors/
`SetData` (`Texture2D.cpp`), `EasyGLTextureBackend`/`Texture::set_image_2d` (`easy-gl/src/Texture.cpp`),
`VulkanGraphicsBackend::CreateTexture`, and `BgfxGraphicsBackend::CreateTexture`. **No POT/NPOT
branching exists anywhere** — every path unconditionally uploads `width × height` pixels via
`glTexImage2D`/Vulkan image creation/`bgfx::createTexture2D`, all of which support NPOT natively on
the API levels CNA targets (OpenGL ES 3.2 core, Vulkan, bgfx). `easy-gl`'s texture upload also
already sets `GL_UNPACK_ALIGNMENT=1` unconditionally, sidestepping the classic NPOT row-padding
footgun that only matters for non-1-byte-aligned unpack settings.

This was previously unverified by any real draw + GPU-readback test — `Texture2DTests.cpp`'s
`LevelCountTest.MipMapTrueNonPowerOfTwo` (Task 267) only checks the CPU-side mip-count formula for
3×5/7×11, never uploads or samples an NPOT texture on the GPU. Added
`examples/easygl_npot_texture_test.cpp` (`EasyGL_NpotTexture` ctest): uploads a 3×5 texture (5
solid-colour rows) via `Texture2D::CreateFromPixels`, draws it full-screen via `SpriteBatch`, and
reads back one pixel per row — all 5/5 pass, confirming NPOT upload and GPU sampling work correctly
end-to-end on EasyGL. Vulkan/Bgfx were verified by code inspection only (no pixel-readback
infrastructure exists for them at the texture level — same limitation noted throughout Phase 31/32).

#### Task 269 — texture sampling at edges for clamp/wrap modes

Two confirmed, FIXED bugs — both specific to `SpriteBatch` (the 3D `DrawUserPrimitives`/
`DrawIndexedPrimitives` path via `GraphicsDevice::SamplerStates[i]` → `ApplySamplerState` was already
correctly implemented in all three real backends and needed no fix):

1. **`SpriteBatch::Begin()`'s `SamplerState` had zero effect on EasyGL, Vulkan, or Bgfx.**
   `SpriteBatch::Begin(..., SamplerState* samplerState, ...)` (`SpriteBatch.cpp`, pre-fix) only
   forwarded `Filter` via `backend_->SetSamplerFilter(int)`, and only when `samplerState` was
   non-null — `AddressU`/`AddressV` were never read at all. Worse: `ISpriteBatchBackend::
   SetSamplerFilter` (`IGraphicsBackend.hpp`) is a no-op by default and is **only overridden by
   `SdlSpriteBatchBackend`** (SDL_Renderer backend) — `EasyGLSpriteBatchBackend`,
   `VulkanSpriteBatchBackend`, and the Bgfx equivalent never overrode it. So on the primary EasyGL
   backend, a `SamplerState` passed to `SpriteBatch::Begin()` was **entirely ignored** — every
   sprite always sampled with whatever GL texture-object defaults were baked in at texture creation
   (`Linear` filter, `ClampToEdge` wrap, from `EasyGLTextureBackend`'s `set_image_2d` calls),
   regardless of `Point`/`Wrap`/`Mirror` being requested. This also meant FNA's real behaviour — a
   `null` `samplerState` resolving to `SamplerState.LinearClamp` (`SpriteBatch.cs:296`, `this.
   samplerState = samplerState ?? SamplerState.LinearClamp;`) — was only accidentally matched by
   coincidence (the hardcoded GL defaults happened to equal Linear+Clamp), not by design.
   **Fix (EasyGL only):** added `ISpriteBatchBackend::SetSamplerAddressMode(int, int)`
   (`IGraphicsBackend.hpp`); `SpriteBatch::Begin()` now always resolves to
   `samplerState ? *samplerState : SamplerState::LinearClamp` and unconditionally calls both
   `SetSamplerFilter`/`SetSamplerAddressMode` (matching FNA's unconditional default-resolution,
   instead of the previous `if (samplerState)` guard that skipped the call entirely for the common
   no-arg `Begin()`). `EasyGLSpriteBatchBackend` now overrides both methods, storing the raw values
   and applying them via the *existing, already-correct* `EasyGLGraphicsBackend::ApplySamplerState`
   (the same GL-sampler-object mechanism the 3D path already used) at the start of `FlushBatch()`,
   bound to texture unit 0 (the unit `EasyGLSpriteBatchBackend`'s texture bind already implicitly
   uses — confirmed via `easygl::Texture::bind()`, which binds to whatever unit is currently active,
   and this codebase's established "always leave unit 0 active" convention, e.g.
   `EasyGLGraphicsBackend.cpp` around the `DualTextureEffect`/`EnvironmentMapEffect` unit-1 binds).
   Default pending values (`Linear`/`Clamp`) exactly match the pre-fix hardcoded GL defaults, so a
   `SpriteBatch` that never receives a `SetSamplerFilter`/`SetSamplerAddressMode` call (impossible
   now, since `Begin()` always calls both, but kept as a safety net) behaves identically to before.
   **Vulkan and Bgfx were not fixed** — their `ISpriteBatchBackend` implementations still don't
   override either method, so `SamplerState` passed to `SpriteBatch::Begin()` remains silently
   ignored on those two backends. Left as a documented, narrow, backend-scoped gap (EasyGL is the
   primary/most-tested backend per `NEXT.md`), not fixed here.

2. **`EasyGLSpriteBatchBackend::Draw()` hard-clamped UVs to `[0,1]`, making `Wrap`/`Mirror`
   unreachable even after fix #1.** `Draw(texture, destRect, sourceRectangle, ...)`
   (`EasyGLGraphicsBackend.cpp`) computed `u1/v1/u2/v2` from `sourceRectangle` divided by texture
   width/height, then called `std::clamp(..., 0.0f, 1.0f)` on all four — meaning a `sourceRectangle`
   extending past the texture's bounds (the standard XNA technique for tiling/scrolling backgrounds
   via `SpriteBatch` + a `Wrap` `SamplerState`) could never actually produce a UV outside `[0,1]`,
   so the GPU sampler's address mode was structurally unreachable regardless of what `SamplerState`
   was bound. FNA's real `SpriteBatch.cs` (all `Draw` overloads with a `sourceRectangle`, e.g. lines
   372–375) does a plain division with **no clamp at all** — this was a CNA-only divergence, not an
   intentional design choice (no comment explained it). **Fix:** removed the clamp entirely, matching
   FNA. Safe for all well-formed (in-bounds) `sourceRectangle` usage — the clamp was a no-op for any
   `u`/`v` already within `[0,1]`, so this only changes behaviour for previously-broken out-of-bounds
   `sourceRectangle` calls. Verified no regression via the existing sprite pixel-readback tests
   (`EasyGL_TexturedQuad`, `EasyGL_SpriteEffects_Flip`, `EasyGL_TransformMatrix_Translation` — all
   still pass) plus the new tests below.

   Added `examples/easygl_texture_address_mode_test.cpp` (`EasyGL_TextureAddressMode` ctest):
   draws a 2×1 (Red|Blue) texture via `SpriteBatch` with a `sourceRectangle` twice the texture width
   (`Rectangle(0,0,4,1)` on a 2×1 texture, so sampled U spans `[0,2]`), reads back the pixel at
   `U≈1.25` under both `SamplerState::PointWrap` and `SamplerState::PointClamp`. Wrap correctly
   tiles (`fract(1.25)=0.25` → left texel → Red); Clamp correctly reads the last texel repeatedly
   (Blue) — both assertions pass, proving `TextureAddressMode` now genuinely affects `SpriteBatch`
   rendering end-to-end on EasyGL.

#### Unrelated build-blocking fix (not part of Task 268/269 scope)

While rebuilding to verify the above, `src/Microsoft/Xna/Framework/Storage/StorageDevice.cpp` failed
to compile: the sibling `sharp-runtime` repo added two new pure-virtual members to
`System::IAsyncResult` (`getAsyncStateProperty()` returning `const std::any&`,
`getAsyncWaitHandleProperty()` returning `System::Threading::WaitHandle&`) in a concurrent commit
outside this session. CNA's two local `IAsyncResult` implementers (`SelectorResult`,
`ContainerResult`, both internal to `StorageDevice.cpp`) didn't implement them. Fixed by mirroring
the pattern from `sharp-runtime`'s own `InterfaceTests.cpp` (`SyncResult`): replaced the unused
`void* asyncState` field with `std::any asyncState` (the existing `void* state` parameter still
assigns into it directly — `std::any` accepts any copyable type) and added a `mutable
System::Threading::EventWaitHandle waitHandle{true, EventResetMode::ManualReset}` member, since
both result types represent already-completed synchronous operations.

#### Newly discovered, pre-existing failures (not caused by this session, not fixed)

Running the full EasyGL ctest suite for the first time this session surfaced two failures unrelated
to any file touched above:
- **`EasyGL_MRT_TwoAttachments`** (Task 145, `examples/easygl_mrt_test.cpp`): deterministically fails
  4/4 runs — `left=(0,255,0)` (correct, green) but `right=(0,0,0)` (wrong, expected blue). This
  exercises `SetRenderTargets` with two attachments, a code path this session never touched
  (confirmed via `git diff --stat` and by re-running the test against a `git stash`-reverted tree,
  which still failed to build due to the unrelated `sharp-runtime` break above — reproduction was
  instead confirmed by inspection: no file in this session's diff touches FBO/render-target-
  attachment code). Needs its own dedicated investigation.
- **`easy-gl-resource-smoke-tests`** (sibling `easy-gl` repo, `tests/smoke/SmokeResourceTests.cpp:336`,
  `test_texture_upload_sets_unpack_alignment_wrap_and_unit0_binding`): deterministically aborts on an
  `assert(g_state.last_active_texture == 0x84C0)` failure, 3/3 runs. `easy-gl`'s own git history shows
  no changes to `src/Texture.cpp` since 2026-06-27 and no uncommitted changes — unrelated to this
  session (nothing here touches the `easy-gl` repo at all). Likely a pre-existing issue in that mock-
  GL-state smoke test suite; out of scope to investigate further here.

Both are new entries in `NEXT.md` §5 ("Known bugs and limitations"), not addressed by Tasks 268/269.

### Vulkan `TransitionImageLayout` missing a re-upload transition (found rebuilding/verifying Vulkan)

While rebuilding and verifying `cmake-build-vulkan` after the Task 268/269 work (per `NEXT.md` §8),
the new Task 270 `ContextRecoveryTest.PartialUpdateCoveringFullLevelDoesNotThrowEvenWithRecoveryDisabled`
and `PartialUpdateNeverThrowsWithRecoveryEnabledByDefault` tests — the first tests in the suite to
call `Texture2D::SetData` **more than once** on a texture backed by a real Vulkan `GraphicsDevice`
— failed with `"Vulkan: unsupported image layout transition"`.

**Root cause (pre-existing, unrelated to Tasks 268/269/270's own logic):**
`VulkanTextureBackend::UpdatePixels` (`VulkanGraphicsBackend.cpp:181`) transitions the image
`SHADER_READ_ONLY_OPTIMAL → TRANSFER_DST_OPTIMAL` before re-uploading (line 208-209) — necessary
because texture creation already transitions it the other way, `TRANSFER_DST_OPTIMAL →
SHADER_READ_ONLY_OPTIMAL`, once the initial upload finishes. But `VulkanGraphicsBackend::
TransitionImageLayout` (`VulkanGraphicsBackend.cpp:3843`) only implemented 4 specific
`(from, to)` barrier combinations, and `SHADER_READ_ONLY_OPTIMAL → TRANSFER_DST_OPTIMAL` was not
one of them — so any `SetData` call after a texture's first upload threw, unconditionally, on
Vulkan. No test before this session ever called `SetData` twice on a texture with a live Vulkan
backend, so this had never been exercised. **Fixed:** added the missing barrier case, symmetric
with the existing `TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL` case (reversed access masks
and pipeline stages: `srcAccessMask = SHADER_READ`, `dstAccessMask = TRANSFER_WRITE`,
`FRAGMENT_SHADER` → `TRANSFER` stage). Verified: `CnaTests` on `cmake-build-vulkan` now
1840/1840 (was 1838/1840 before the fix); full Vulkan ctest suite 1852/1853 (only
`Vulkan_DepthBias`'s `DepthBias=-1e6` sub-case still fails — a pre-existing, unrelated
depth-bias-precision issue, not investigated further here — up from the previously-documented
"11/13 historically pass" baseline for the 13 Vulkan-specific integration tests).

### `GetData` missing `startIndex < 0` guard (Task 265, Phase 32)

Goal: match FNA's exact `GetData<T>` exception behaviour for the rectangle-based overload. Reading
FNA's real `Texture2D.GetData<T>(int level, Rectangle? rect, T[] data, int startIndex, int
elementCount)` (`FNA/src/Graphics/Texture2D.cs:284-314`) shows it does **not** bounds-check `rect`
against the texture at the managed (C#) level at all — it validates only `data == null`,
`data.Length == 0`, and `data.Length < startIndex + elementCount`, then hands `rect` straight to
`GetDataPointerEXT` → the native `FNA3D_GetTextureData2D`, which does its own (unaudited, out of
scope) validation. So CNA's existing rect-bounds check in `GetData(level, rect, ...)`
(`Texture2D.cpp`, present since before this session) is stricter than FNA's managed layer — a
deliberate memory-safety improvement, not a literal FNA-parity requirement, consistent with the
project's existing "prefer safety over literal parity for internal bounds checks" precedent (Task
261/266's `SetData` OOB fixes).

What FNA's C# `data.Length < startIndex + elementCount` check *does* imply, though, is that
`startIndex` must never let the computed destination index fall outside `data`. Comparing CNA's two
`GetData` overloads against the equivalent `SetData` overloads (which already validate this)
surfaced a real, symmetric gap: **neither `GetData(Color*, int startIndex, int elementCount)` nor
`GetData(int level, const Rectangle*, Color*, int startIndex, int elementCount)` validated
`startIndex < 0`**, while both `SetData` counterparts already do.

- `GetData(Color*, int, int)`: `src = (startIndex + i) * 4` indexes into the internal `cpuPixels_`
  buffer. A negative `startIndex` makes `src` negative — an out-of-bounds **read** before the start
  of `cpuPixels_`'s heap allocation (undefined behaviour, potential crash/info leak).
- `GetData(int level, const Rectangle*, Color*, int, int)`: `dst = startIndex + row*w + col` indexes
  into the **caller-supplied** `data` array. A negative `startIndex` makes `dst` negative — an
  out-of-bounds **write** before the start of the caller's array (undefined behaviour, potential
  memory corruption) — the same class of bug as the two OOB bugs Task 261's audit found and Task 266
  fixed in `SetData`, just on the read/output side instead of the write/input side.

**Fix:** added `if (startIndex < 0) throw std::out_of_range("Texture2D::GetData: startIndex must be
>= 0");` to both overloads, placed immediately after the existing null/`elementCount<=0` check and
before any buffer access — mirrors `SetData`'s existing `startIndex < 0` guard exactly (same
exception type and message pattern). 2 new regression tests (`GetDataNegativeStartIndexThrowsOutOfRange`,
`GetDataLevelNegativeStartIndexThrowsOutOfRange`); 1842/1842 unit tests pass on all three backends
(EasyGL/Vulkan/Bgfx); 1911/1913 EasyGL ctest (same 2 pre-existing, unrelated failures as before).

No other discrepancy was found comparing `GetData`'s guards against `SetData`'s: both validate
`!data`/`elementCount<=0`, `level<0`, rect-bounds, and `elementCount < w*h` identically. The
remaining difference (`SetData`'s `data.Length < startIndex+elementCount` upper-bound check from
FNA) cannot be replicated in either direction — CNA's `Color*` pointer-based API has no way to know
the caller's actual buffer length, unlike C#'s length-carrying arrays; this is an already-accepted,
structural deviation (see `CHECKLIST.md`), not something Task 265 can or should attempt to close.

---

### Texture3D detailed audit (Task 271, Phase 33)

Line-by-line comparison of `include/.../Texture3D.hpp` + `src/.../Texture3D.cpp` against
`FNA/src/Graphics/Texture3D.cs` (282 lines). Unlike the Task 261 `Texture2D` audit, real bugs were
found and fixed in the same pass (small, well-scoped, guard-adding fixes — consistent with this
project's established pattern of fixing this exact class of finding immediately rather than
deferring to a follow-up task; see Tasks 261→266, 265, and the Vulkan `TransitionImageLayout` fix).

**API surface** — structurally complete and correct: `Texture3D` has exactly one public constructor
(`GraphicsDevice&, width, height, depth, mipMap, format`), matching FNA's single constructor
exactly (unlike `Texture2D`, which has extra NOXNA convenience constructors). `SetData`/`GetData`
each have the same 3 overload arities as FNA (2-arg whole-texture, 3-arg with `startIndex`, 10-arg
with an explicit `level`+box). `SetDataPointerEXT` exists and matches FNA's EXT method; correctly,
CNA has **no** `GetDataPointerEXT` for `Texture3D` — FNA's own `Texture3D.cs` doesn't have one
either (unlike `Texture2D`, which has both `SetDataPointerEXT` and `GetDataPointerEXT` in FNA — the
missing `GetDataPointerEXT` documented in the Task 261 `Texture2D` audit does **not** apply here).

#### Confirmed bugs — FIXED

1. **`LevelCount` hardcoded to 1, ignoring `mipMap` — FIXED.** The constructor
   (`Texture3D.cpp`, pre-fix) initialized `levelCount_(1)` unconditionally, never computing mip
   levels even when `mipMap=true` was passed. FNA: `LevelCount = mipMap ? CalculateMipLevels(width,
   height) : 1;` (`Texture3D.cs:60`, depth does not participate in the mip-level count — the same
   formula `Texture2D`/`Texture.CalculateMipLevels` already uses). **Fix:** added the same
   `CalculateMipLevels(w, h)` static helper CNA's `Texture2D.cpp` already has (small, intentional
   duplication — matches this codebase's existing per-file-static convention rather than
   introducing a new shared utility for a 5-line function) and wired it into the initializer list.
   6 new regression tests (`MipMapFalseIsAlwaysOne`, `MipMapTrueSquarePowerOfTwo`,
   `MipMapTrueNonPowerOfTwo`) mirroring `Texture2D`'s `LevelCountTest` coverage.
   Note: the EasyGL backend (`EasyGLTexture3DBackend`) still creates only a single-level GL 3D
   texture regardless of `mipMap` (its constructor's `mipMap` parameter is literally unused —
   `bool /*mipMap*/`) — so `getLevelCountProperty()` is now correct at the C++/XNA API layer, but
   the GPU texture itself still has no real mip chain. Left as a documented backend-completeness
   gap (see below), not fixed here — the API-layer bug (wrong property value) and the
   backend-rendering gap (no actual mip generation) are separable, and only the former is a Task
   271-scope "the class lies about its own state" bug.

2. **`SetData`/`GetData` had essentially no input validation at all — FIXED.** Every overload
   (2-arg, 3-arg, and the 10-arg level+box form, for both `SetData` and `GetData`) had zero guards
   beyond `SetDataPointerEXT`'s `if (backend_)`. Comparing against FNA and against `Texture2D`'s
   already-hardened equivalents surfaced multiple real, exploitable bugs:
   - **Null-pointer dereference crash:** `SetData(nullptr, n)`/`GetData(nullptr, n)` (any arity)
     unconditionally dereferenced `data` inside `colorsToRgba`/`rgbaToColors` before any check ran
     — an immediate segfault. FNA's real `SetData<T>`/`GetData<T>` both throw
     `ArgumentNullException` for `data == null` (`Texture3D.cs:117-120`, `:241-244`).
   - **Integer-cast heap-allocation hazard:** no `elementCount <= 0` check anywhere. A negative
     `elementCount` cast to `std::size_t` (`static_cast<std::size_t>(count) * 4` in
     `colorsToRgba`/the `GetData` RGBA buffer) becomes an enormous unsigned value, attempting a
     huge allocation — `std::bad_alloc`/crash, not a controlled error.
   - **Out-of-bounds read/write via negative `startIndex`:** the exact same bug class just fixed
     for `Texture2D::GetData` in Task 265 — `colorsToRgba(data, startIndex, elementCount)` reads
     `data[startIndex + i]` (OOB read, `SetData`) and `rgbaToColors(...)` writes
     `data[startIndex + i]` (OOB write, `GetData`) — a negative `startIndex` makes either
     computation land before the start of the caller's array.
   - **No box-bounds validation** on the 10-arg overloads at all — `left`/`top`/`front` could be
     negative, or `right`/`bottom`/`back` could be `<=` their counterpart, producing a negative
     `width`/`height`/`depth` (`right-left` etc.) passed straight to the backend's GL/Vulkan/Bgfx
     texture-upload call — undefined behaviour at the driver level. FNA's `GetData<T>` (10-arg)
     *does* validate this: `if ((left<0||left>=right) || (top<0||top>=bottom) ||
     (front<0||front>=back)) throw new ArgumentException(...)` (`Texture3D.cs:252-257`) — but
     FNA's `SetData<T>` (10-arg) notably does **not** have this check (only a null check,
     `Texture3D.cs:117-120`), an asymmetry in FNA itself. Matching CNA's own established
     precedent of extending C++ memory-safety beyond literal FNA parity when a raw-pointer API
     makes it cheap and unambiguous (`Texture2D`'s `SetData`/`GetData` rect-bounds checks, Tasks
     261/265/266), the same box-bounds check was added to **both** `Texture3D::SetData` and
     `Texture3D::GetData`'s 10-arg overloads, not just `GetData`.
   **Fix:** added `!data` (→ `std::invalid_argument`), `elementCount<=0` / `startIndex<0` /
   `level<0` / box-bounds (→ `std::out_of_range`) guards to both 10-arg overloads, matching the
   exact exception-type convention `Texture2D` already established. The 2-arg and 3-arg overloads
   were refactored to delegate to the 10-arg overload (`SetData(0,0,0,width_,height_,0,depth_,
   data,startIndex,elementCount)`) instead of duplicating the upload logic — this exactly mirrors
   how FNA's own `Texture3D.SetData<T>`/`GetData<T>` 1-arg and 3-arg overloads delegate to the
   10-arg form (`Texture3D.cs:79-103`, `:182-213`), so it's a correctness fix and a simplification
   in one, not a design choice made from scratch. `SetDataPointerEXT` also gained the null-data
   check FNA's own EXT method has (`Texture3D.cs:151-154`). 25 new regression tests across both
   methods' guard paths (see `Texture3DTests.cpp`).

3. **`Dispose(bool)` was never overridden — FIXED.** `Texture3D` had no `Dispose(bool disposing)`
   override at all (unlike `Texture2D`, which resets `backend_` in its override). Calling
   `texture3d.Dispose()` set `isDisposed_ = true` (via the `GraphicsResource` base) but never
   released `backend_` (the `unique_ptr<ITexture3DBackend>` holding the GPU texture handle) — the
   GPU resource stayed alive, un-freed, until the C++ `Texture3D` object's own destructor ran,
   defeating the entire purpose of an explicit, deterministic `Dispose()` call. **Fix:** added
   `void Texture3D::Dispose(bool disposing) { backend_.reset(); GraphicsResource::Dispose(disposing); }`
   plus the required `using GraphicsResource::Dispose;` in the header (needed because declaring the
   `Dispose(bool)` override otherwise hides the inherited public 0-arg `Dispose()` from name
   lookup) — both mirror `Texture2D`'s existing pattern exactly. 2 new regression tests
   (`DisposeMarksResourceDisposed`, `DoubleDisposeDoesNotThrow`).

#### Confirmed limitations (backend-level, documented, not fixed — out of Task 271's narrow scope)

- **EasyGL's `mipMap` and `SurfaceFormat` constructor parameters are silently ignored.**
  `EasyGLTexture3DBackend`'s constructor signature is
  `(int w, int h, int depth, bool /*mipMap*/, int /*surfaceFormat*/)` — both parameter names are
  commented out, i.e. explicitly unused. The GPU texture is always created as a single-level
  `Rgba8` 3D texture regardless of what the caller requested. The `SurfaceFormat`-ignoring half is
  consistent with the already-documented, project-wide Color-only limitation (`Texture::
  ValidateFormat` — same shared helper `Texture2D` uses, throws for any non-`Color` format before
  the backend is even reached, so this mostly matters if `ValidateFormat` is ever relaxed). The
  `mipMap`-ignoring half is new to this audit: even a `Texture3D` correctly reporting
  `LevelCount > 1` (per fix #1) has no actual mip chain on the GPU on EasyGL. Fixing real mip-chain
  generation for 3D textures is a backend-rendering task of its own (likely feeding a later Phase
  33 task, e.g. verifying `Texture3D` sampling — Task 277), not something to bundle into an
  audit-plus-guards pass.
- **No pixel-readback verification exists for `Texture3D` beyond the happy-path slice round-trip**
  (`examples/easygl_texture3d_slices_test.cpp`, `mipMap=false` only) — mip-level `SetData`/`GetData`
  round trips, and any real GPU sampling of a `Texture3D` (e.g. via a custom effect), are unverified.
  Feeds Phase 33 Tasks 273–274 (partial box upload/readback tests) and 277 (sampling verification).

#### Confirmed correct / faithful to FNA

- Constructor signature and property set (`Width`/`Height`/`Depth`/`Format`/`LevelCount`, all
  read-only) match FNA exactly.
- `SetData`/`GetData` overload arities and parameter order (`level, left, top, right, bottom,
  front, back, data, startIndex, elementCount`) match FNA's 10-arg overloads exactly, including the
  exclusive-upper-bound convention for `right`/`bottom`/`back`.
- No `GetDataPointerEXT` — correctly matches FNA, which doesn't have one for `Texture3D` either
  (confirmed by reading `Texture3D.cs` in full; only `Texture2D` has both directions of the EXT
  pointer API).

---

### TextureCube detailed audit (Task 272, Phase 33)

Line-by-line comparison of `include/.../TextureCube.hpp` + `src/.../TextureCube.cpp` against
`FNA/src/Graphics/TextureCube.cs` (410 lines). As predicted going into this task (see the Task 271
resume-prompt note), `TextureCube` had the *exact same 3 bug classes* Task 271 found in `Texture3D`
— confirming they're a systemic pattern from a shared implementation lineage, not one-off mistakes
— **plus 2 additional findings unique to `TextureCube`**, one of them severe.

#### Confirmed bugs — FIXED

1. **`LevelCount` hardcoded to 1, ignoring `mipMap` — FIXED.** Identical bug to Texture3D's Task 271
   finding #1. FNA: `LevelCount = mipMap ? CalculateMipLevels(Size) : 1;` (`TextureCube.cs:49`) —
   `Texture.CalculateMipLevels(int width, int height=0, int depth=0)` takes the max of whichever
   dimensions are passed; passing only `size` is equivalent to `CalculateMipLevels(size, size)`
   since a cube face is always square. **Fix:** reused the same `CalculateMipLevels(w, h)` helper
   pattern from `Texture2D.cpp`/`Texture3D.cpp` (again a small, intentional per-file duplication),
   called as `CalculateMipLevels(size, size)`. 3 new regression tests. The protected constructor
   used internally by `RenderTargetCube` (which doesn't take a `mipMap` parameter at all) still
   hardcodes `levelCount_(1)` — left unchanged; that constructor is `RenderTargetCube`'s concern,
   out of `TextureCube`'s own public-API scope for this task.

2. **`SetData`/`GetData` had almost no input validation — FIXED.** Same bug class as Texture3D
   finding #2, actually *worse* in the pre-fix state: not even a null check existed anywhere (the
   2-arg `SetData` immediately dereferenced `data` in a loop with zero guards at all). Fixed with
   the same guard set as `Texture3D`: `!data` → `std::invalid_argument`; `elementCount<=0` /
   `startIndex<0` / `level<0` → `std::out_of_range`. Also added a rect-bounds check (`x<0||y<0||
   x+w>levelSize||y+h>levelSize`) to **both** `SetData` and `GetData`'s 6-arg overloads — FNA's own
   `TextureCube.SetData`/`GetData` have *no* rect-bounds validation at all (not even the asymmetric
   GetData-only check `Texture3D.cs` has), so this is a pure C++-safety extension beyond FNA parity,
   consistent with the established project precedent (Texture2D Tasks 261/265/266, Texture3D Task
   271). 24 new regression tests across both methods' guard paths.

3. **`Dispose(bool)` was never overridden — FIXED.** Identical bug and fix to Texture3D's Task 271
   finding #3: `backend_` was never released on explicit `Dispose()`. Fixed the same way
   (`backend_.reset(); GraphicsResource::Dispose(disposing);` + `using GraphicsResource::Dispose;`
   in the header). 2 new regression tests.

4. **Missing `SetData`/`GetData(face, data, startIndex, elementCount)` overload — FIXED, TextureCube-
   specific.** FNA's `TextureCube` has 3 `SetData<T>`/`GetData<T>` overload arities: 2-arg (face,
   data), 4-arg (face, data, startIndex, elementCount), and 6-arg (face, level, rect, data,
   startIndex, elementCount) — `TextureCube.cs:104-178`, `:225-308`. CNA had only the 2-arg-shaped
   (`face, data, elementCount` — the C++ equivalent of FNA's 2-arg form, since a raw pointer needs
   an explicit count) and 6-arg forms; **the middle overload was missing from the API surface
   entirely**, unlike `Texture2D` and `Texture3D`, which both have their full set of `startIndex`-
   taking overloads. **Fix:** added `SetData(CubeMapFace, const Color*, int startIndex, int
   elementCount)` and the `GetData` equivalent, delegating to the 6-arg overload
   (`SetData(face, 0, nullptr, data, startIndex, elementCount)`), matching how FNA's own 4-arg
   overloads delegate to its 6-arg form.

5. **`rect == nullptr` at `level > 0` ignored `level` entirely, using the full face `Size` instead
   of `Size >> level` — FIXED, TextureCube-specific, the most severe finding in this audit.**
   Pre-fix, both `SetData` and `GetData`'s 6-arg overloads computed the default region as
   `w = size_, h = size_` whenever `rect` was null, with **no dependency on `level` at all** — unlike
   `Texture2D`/`Texture3D`, which both use a `mipDim(base, level) = max(1, base >> level)` helper
   for exactly this case. A caller writing/reading mip level 1 of a mipmapped cube (rect omitted, as
   FNA's own `DDSFromStreamEXT` pattern does — see finding below) would have the *full* face
   dimensions passed to the backend instead of the level's actual (smaller) dimensions — silently
   wrong `w`/`h` reaching `ITextureCubeBackend::SetData`/`GetData`, with no bounds check to catch it
   pre-fix either. **Fix:** added the same `mipDim()` helper `Texture2D.cpp` already has (per-file
   static, matching convention) and used `mipDim(size_, level)` as the default `w`/`h` when `rect`
   is null, before applying the new rect-bounds check from finding #2. Regression tests
   (`SetDataNullRectAtMipLevelUsesReducedSize`, `SetDataNullRectAtMipLevelRejectsFullFaceSizedElementCount`)
   construct a `mipMap=true` cube and prove both that a level-1-correctly-sized call succeeds and
   that a level-0-sized (`elementCount` too large for the actual level) call is now correctly
   rejected — pinning the fix, since prior to it *both* of these would have behaved identically
   (wrongly) since `level` was never consulted.

#### Confirmed severe bug — NOT fixed (out of Task 272's guard-fixing scope)

**RESOLVED (Task 663):** `DDSFromStreamEXT` is now a real implementation — see `plan_graphics.md`'s
Task 663 entry and `NEXT.md` §3 for the full writeup. Historical finding preserved below.

6. **`DDSFromStreamEXT` is a non-functional stub.** `TextureCube::DDSFromStreamEXT(GraphicsDevice&,
   Stream&)` (`TextureCube.cpp`, pre- and post- this task) is:
   ```cpp
   TextureCube TextureCube::DDSFromStreamEXT(GraphicsDevice& device, System::IO::Stream& stream)
   {
       return TextureCube(device, 1, false, SurfaceFormat::Color);
   }
   ```
   It **completely ignores the `stream` parameter** — no DDS header is read, no `isCube` flag is
   checked, no face or mip-level data is decoded or uploaded. It always returns a blank 1×1
   `Color` cube map, silently, regardless of what (if anything) valid DDS cube data was passed in.
   Unlike the Task 261 `Texture2D` audit's missing-EXT-method findings (those methods don't exist
   at all, so calling them is a compile error — an honest, loud failure), this one is far more
   dangerous: the method *exists*, *compiles*, *runs without throwing*, and *returns a
   plausible-looking `TextureCube`* that is silently wrong. A real implementation needs (per
   `TextureCube.cs:314-405`): DDS header parsing via a `ParseDDS`-equivalent (`Texture.ParseDDS` in
   FNA; CNA's closest existing building block is `Texture2D.cpp`'s private `TryDecodeDds` /
   `DxtUtil::DecompressDxt1/3/5`, currently only wired up for 2D), an `isCube` flag check (throwing
   if the DDS isn't actually a cube map, matching FNA's `FormatException`), and 6 faces ×
   `levelCount` `SetData` calls reading sequential per-face-per-level blocks from the stream. This
   is a substantial feature implementation, not a guard fix — deliberately left undone here,
   matching this session's established scope discipline (Task 271 left the EasyGL mip-chain gap
   similarly undone). **Strongly recommended as a dedicated, immediate follow-up task** (more urgent
   than the `Texture2D` missing-EXT-method findings, precisely because this one fails silently
   rather than loudly) — not yet given its own `plan_graphics.md` number; add one before Phase 33
   is considered complete.

#### Confirmed limitations (documented, not fixed, matching established precedent)

- **`CubeMapFace` values are never validated.** `static_cast<int>(face)` is passed straight to the
  backend with no range check, for both the pre-existing and newly-added overloads. This is already
  explicitly tracked as its own task: `plan_graphics.md` Task 279, "Add validation for invalid
  `CubeMapFace` values" — deliberately not pulled forward into this audit.
- **EasyGL's `mipMap`/`SurfaceFormat` handling for `TextureCube`** was not separately re-verified in
  this pass — Task 271 already documented the identical limitation for `Texture3D`
  (`EasyGLTexture3DBackend`'s constructor ignores both parameters); given the shared implementation
  pattern this almost certainly also applies to `EasyGLTextureCubeBackend`, but confirming that is
  Phase-33-sampling-verification territory (Task 278, "Verify TextureCube sampling in EasyGL/Vulkan/
  Bgfx EnvironmentMapEffect"), not this audit task.

#### Confirmed correct / faithful to FNA

- Constructor signature and the `Size`/`Format`/`LevelCount` property set match FNA (`Size` is the
  cube's one dimension parameter, correctly — cube faces are always square, unlike `Texture3D`'s
  independent width/height/depth).
- `SetData`/`GetData` overload arities now match FNA's 3-overload set exactly (post-fix #4), with
  parameter order (`face, level, rect, data, startIndex, elementCount`) matching FNA's 6-arg form.
- No `GetDataPointerEXT` for `TextureCube` in either FNA or CNA — consistent (same as `Texture3D`;
  only `Texture2D` has the full EXT pointer pair in FNA).
- `DDSFromStreamEXT` exists as a named static method (unlike `Texture2D`, where the equivalent DDS
  logic is folded silently into `FromStream` rather than exposed under its own name per the Task 261
  audit) — the *shape* is right, matching FNA's API surface; only the *implementation* is a stub
  (finding #6).

### Texture3D partial box upload verification (Task 273, Phase 33)

Task 173 (an earlier session) added `examples/easygl_texture3d_slices_test.cpp`, but every box it
uses spans the full width/height and only varies `front`/`back` — an x- or y-axis bug in either
`Texture3D::SetData`'s box math or `EasyGLTexture3DBackend::SetData`/`GetData` would not have been
caught.

Added `examples/easygl_texture3d_partial_box_test.cpp` (`EasyGL_Texture3D_PartialBox_RoundTrip`
ctest) with three sub-tests using boxes with distinct width/height/depth, offset on every axis:

1. **Asymmetric off-origin box** — a 4×5×3 volume (deliberately distinct dimensions per axis) filled
   Red, with a 2×3×2 Blue box written at `left=1,top=2,front=1`. Full-volume read-back checks all 60
   voxels individually.
2. **Single-voxel box** — a 3×3×3 volume, single voxel written at `(2,1,0)`; verifies exactly one of
   27 voxels changed.
3. **Far-corner box** — a 4×4×4 volume, box from `(2,2,2)` to `(4,4,4)` (i.e. `right==width`,
   `bottom==height`, `back==depth`), verifying the exclusive upper-bound semantics hold at the far
   edge.

All three sub-tests pass against the existing implementation — no bug found. This confirms
`EasyGLTexture3DBackend`'s use of `glTexSubImage3D` (upload) and a per-slice `glReadPixels` via a
temporary FBO (readback) already handle arbitrary x/y/z sub-regions correctly; the earlier
z-slice-only test just hadn't exercised that path. 1972/1972 EasyGL ctest pass (the 2 pre-existing,
unrelated failures — `EasyGL_MRT_TwoAttachments`, `easy-gl-resource-smoke-tests` — are unchanged).

### Texture3D partial box readback verification (Task 274, Phase 33)

Task 273's box-placement test uses a binary Red/Blue split, which would not catch a `GetData`-side
bug that reads the right box shape from the wrong (x,y,z) offset if the mistake happened to
preserve a symmetric colour boundary. This task instead fills a 4×3×5 volume with a colour unique
to every voxel's coordinate (`R=20+x*40, G=20+y*60, B=20+z*40`, all three axes using different
multipliers so a coordinate swap is also detectable), then reads sub-boxes back and compares every
element against the exact source formula.

Added `examples/easygl_texture3d_partial_box_readback_test.cpp`
(`EasyGL_Texture3D_PartialBox_Readback` ctest), three sub-tests:

1. **Asymmetric off-origin box read** — 2×2×3 box at `left=1,top=1,front=1`, `elementCount` exactly
   matching the box volume.
2. **`GetData` with non-zero `startIndex`** — a 2×1×2 box read into the middle of a
   sentinel-padded 6-element output array (mirrors Task 170B's `Texture2D` `startIndex` pattern);
   verifies the padding elements are left untouched.
3. **Far-corner box read** — box from `(2,1,3)` to `(4,3,5)` (`right==width`, `bottom==height`,
   `back==depth`), verifying the read path's exclusive upper bounds match the write path's (Task
   273C).

All three sub-tests pass — no bug found. Confirms `EasyGLTexture3DBackend::GetData`'s per-slice
`glReadPixels` correctly honours arbitrary x/y/z box offsets on read, not just on write.

While designing this test, cross-checked FNA's `Texture3D.cs`/`Texture2D.cs` `GetData<T>` against
CNA's `Texture3D::GetData`: **FNA itself never validates that `elementCount` matches the box's pixel
count** (`(right-left)*(bottom-top)*(back-front)`) — the only check is
`data.Length >= startIndex + elementCount`, then `elementCount * elementSizeInBytes` is passed
straight through to the native backend, which writes based on the box dimensions regardless of what
`elementCount` claims. CNA's `Texture3D::GetData` has the identical characteristic (no box-volume-
vs-`elementCount` cross-check). This is faithful-to-FNA behavior — matching an existing implicit
contract, not a gap to close — so no fix was made; every test in this task supplies a correctly-
sized `elementCount` for its box, as real XNA/FNA usage must. 1971/1973 EasyGL ctest pass (2
pre-existing, unrelated failures unchanged).

### TextureCube partial rect and startIndex verification, all six faces (Task 275, Phase 33)

Task 172 (an earlier session) already gives pixel-exact whole-face round-trip coverage for all six
faces, but only through the simple 2-arg `SetData`/`GetData(face,data,elementCount)` overload.
`TextureCubeTests.cpp`'s coverage of the rect-based 6-arg overload (added/fixed by Task 272) and the
startIndex 4-arg overload is argument-guards only — e.g. `SetDataRectWithinBoundsDoesNotThrow` checks
the call doesn't throw, not that it wrote the right pixels.

Added `examples/easygl_texturecube_partial_rect_test.cpp`
(`EasyGL_TextureCube_PartialRect_RoundTrip` ctest), three sub-tests:

1. **Partial rect, all six faces** — a 4×4 `TextureCube`; every face gets its own background colour
   (same 6-colour palette as Task 172), then an off-centre, asymmetric 2×2 White rect
   (`Rectangle(1,0,2,2)`) is written into every face via the 6-arg rect overload. Verified only
   *after* all six faces are written, so a cross-face bleed (writing face A's rect into face B) would
   be caught, not just a same-face placement bug.
2. **`SetData` with non-zero `startIndex`** (rect-based, `PositiveX`) — mirrors Task 170A's
   `Texture2D` pattern: a 6-element source array with Green padding around a 2-element Blue payload,
   verifying only the requested 2 elements are consumed.
3. **`GetData` with non-zero `startIndex`** (rect-based, `PositiveX`) — mirrors Task 170B's pattern:
   reads into the middle of a sentinel-padded array, verifying the padding stays untouched.

All three sub-tests pass — no bug found. Confirms `EasyGLTextureCubeBackend::SetData`/`GetData`
(`set_sub_image_2d` for upload; a per-face FBO + `glReadPixels` for readback) correctly honour
arbitrary x/y sub-rects independently per face. 1972/1974 EasyGL ctest pass (2 pre-existing,
unrelated failures unchanged).

### TextureCube mip-level allocation bug, all six faces (Task 276, Phase 33)

Added `examples/easygl_texturecube_mip_test.cpp` (`EasyGL_TextureCube_Mip_RoundTrip` ctest),
mirroring Task 171's `Texture2D` mip round-trip test but across all six faces: a 4×4
`mipMap=true` cube (levels 4×4, 2×2, 1×1) gets a distinct colour written to every level of every
face, then every level of every face is read back and verified.

**This test initially failed.** Mip levels 1 and 2 always read back `(0,0,0)` regardless of what
was written, on every face — level 0 was the only level that worked. Root cause:
`EasyGLTextureCubeBackend`'s constructor only ever allocated GPU storage for level 0 (one
`set_image_2d` call per face, no loop over levels), while `SetData`'s box writes go through
`set_sub_image_2d` (`glTexSubImage2D`). `glTexSubImage2D` requires the target level to already have
a defined image (from a prior `glTexImage2D`/`set_image_2d` call) — level 0 had one, levels 1+ never
did, so those writes silently went nowhere (no GL error surfaced through the wrapper).

Fixed in `EasyGLGraphicsBackend.cpp`: the constructor now computes the mip level count
(`CalculateCubeMipLevels`, mirroring `TextureCube.cpp`'s own `CalculateMipLevels`/`mipDim` logic,
duplicated locally since the backend doesn't share that translation unit) and pre-allocates every
level of every face via `set_image_2d(level, ..., nullptr)` before returning. Subsequent
`set_sub_image_2d` writes at any level now succeed because the level's storage already exists. All
126 checks (6 faces × 21 pixels across 3 levels) now pass. 1973/1975 EasyGL ctest pass (2
pre-existing, unrelated failures unchanged).

**Not fixed in this task, flagged as a follow-up (`plan_graphics.md` Task 862):**
`EasyGLTexture3DBackend`'s constructor has the identical single-level-only pattern (only level 0
allocated via `set_image_3d`, `SetData` writes via `set_sub_image_3d`), so `Texture3D::SetData` at
`level>0` on a mipmapped volume almost certainly has the same silent-failure bug. Task 271's audit
already documented that EasyGL ignores `Texture3D`'s `mipMap` parameter in general, but did not
specifically reproduce a level>0 `SetData` failure with a test — this session's finding gives that
documented limitation a concrete, fixable root cause and a matching fix shape (mirror this task's
constructor change), left for a follow-up task since it's outside Task 276's `TextureCube` scope.

### Texture3D sampling in shaders is not implemented (Task 277, Phase 33)

Audit-only finding — no code change. The task asks whether `Texture3D` sampling is exposed to any
effect (stock or custom); the answer is no, for structural reasons that go deeper than a single
missing wire-up.

**No stock XNA effect ever samples a `Texture3D`.** None of FNA's `BasicEffect`, `AlphaTestEffect`,
`DualTextureEffect`, `EnvironmentMapEffect`, or `SkinnedEffect` declare a `Texture3D` parameter — this
is true in real XNA/FNA too, not a CNA gap. So the only realistic path for `Texture3D` sampling is a
custom effect.

**Custom `ShaderEffect` has no texture-binding API at all.** Read `ShaderEffect.hpp` and
`IEffectBackend` (`IGraphicsBackend.hpp`) in full: both expose only scalar/vector/matrix uniform
setters (`SetUniformFloat/Int/Vec2/Vec3/Vec4/Mat4`). There is no `SetTexture`/`SetTexture2D`/
`SetTexture3D`/`BindSampler` method anywhere in either type, for *any* texture type — not just
`Texture3D`. A custom shader's `sampler2D`/`sampler3D` uniforms can only ever be fed by whatever the
backend implicitly binds from `GraphicsDevice.Textures[slot]` during the draw call.

**`Texture3D` cannot be placed into `GraphicsDevice.Textures[slot]` at all — a class-hierarchy
mismatch from FNA.** Confirmed in FNA source: `Texture3D : Texture` and `TextureCube : Texture`
(`Texture3D.cs`/`TextureCube.cs`), so in real XNA/FNA, `GraphicsDevice.Textures[0] = my3DTexture;`
compiles and works, because `TextureCollection` holds `Texture` references and any texture subtype
fits. In CNA, `Texture2D : public Texture` matches FNA, but **`Texture3D : public GraphicsResource`
and `TextureCube : public GraphicsResource`** (`Texture3D.hpp`/`TextureCube.hpp`) — neither inherits
`Texture`, so neither can be assigned into `TextureCollection` (`operator()(int, Texture*)`) at all.
This is why `EffectParameter` needed dedicated `texture3DData_`/`textureCubeData_` storage slots
instead of reusing the generic `textureData_` slot (see the comment in `EffectParameter.hpp`) — the
type system itself blocks the FNA-equivalent unification.

**`EffectParameter::SetValue(Texture3D*)`/`GetValueTexture3D()` are a write-only dead end.** Grepped
every backend (`EasyGL`, `Vulkan`, `Bgfx`) for `texture3DData_`/`GetValueTexture3D`/`sampler3D` —
zero matches outside `EffectParameter.hpp`/`.cpp` themselves. The API lets a game call
`effect->Parameters["MyVolume"]->SetValue(myTexture3D)`, and the pointer is stored and can be read
back, but nothing anywhere ever picks it up to bind the texture to the GPU or a shader uniform.
`EffectParameterTests.cpp` already fully covers this one working piece (pointer round-trip storage,
null and non-null); no further test was added here since there is no positive GPU-sampling behavior
to lock in — a pixel-readback test would only prove the negative already established by code
inspection.

**Not fixed here — tracked as new Task 863.** Closing this gap for real means either (a) making
`Texture3D`/`TextureCube` inherit `Texture` to match FNA and unify `TextureCollection` handling — a
non-trivial refactor touching `EffectParameter`, `TextureCollection`, and every backend's
texture-bind code — or (b) adding an entirely separate `Texture3D`-specific GPU-binding path outside
`TextureCollection`. Both are well outside a verify-only audit's scope.

### TextureCube sampling in EnvironmentMapEffect, cross-backend (Task 278, Phase 33)

Unlike Task 277's custom-effect finding, stock effects don't go through `EffectParameter`/
`TextureCollection` for their textures at all — `EnvironmentMapEffect` stores `TextureCube*`
directly and forwards it via `FillGpuDrawParams` into `GpuDrawParams::envMap` (a raw
`const ITextureCubeBackend*`), which every backend's draw dispatch consumes directly. This
completely bypasses the class-hierarchy problem from Task 277. So this had to be checked per
backend, not assumed either way.

- **EasyGL** — already fully wired. `EasyGLGraphicsBackend::SelectProgram` dispatches to
  `EnsureEnvMapped3DProgram()` when `params.envMapping`, which compiles a real reflection shader
  (`samplerCube uEnvMap`, `envColor = texture(uEnvMap, reflect(-E,N))`). `BindDrawParams` binds
  `params.envMap->BindGL()` to texture unit 1. The existing `EasyGL_EnvironmentMapEffect_Readback`
  pixel-readback test (`examples/easygl_env_map_test.cpp`, 4 sub-tests) still passes.
- **Vulkan** — already fully wired. A dedicated descriptor set layout (binding 0 `sampler2D`,
  binding 1 `samplerCube`, binding 2 UBO), pipeline, and push-constant path exist specifically for
  `params.envMapping` (`EnsureEnvMapResources`, `GetOrCreatePipelineEnvMap3D`,
  `env_map3d.frag.glsl`'s `samplerCube uEnvMap` + `reflect(-E,N)`, matching EasyGL's formula). The
  existing `Vulkan_EnvironmentMapEffect_Readback` pixel-readback test still passes.
- **Bgfx — found and fixed a real gap.** `BgfxGraphicsBackend::DrawPrimitivesEx` checked
  `params.dualTexture` and `params.skinned` but had **no branch at all** for `params.envMapping`.
  Since `EnvironmentMapEffect::FillGpuDrawParams` also sets `lightingEnabled=true` and
  `textureEnabled=true`, an `EnvironmentMapEffect` draw would silently fall into the
  `params.lightingEnabled` branch (`litTextured3DProgram_`) — rendering as plain lit-textured
  geometry with **no reflection, no cube-map sampling, no specular tint, no error of any kind**.
  This is a silent behavioral gap, not a crash — confirmed by reading the full `if`/`else if` chain
  in `DrawPrimitivesEx` before writing any code.

  Fixed by adding full Bgfx support, mirroring the EasyGL/Vulkan reflection formula (no alpha-test
  branch, matching Vulkan's simpler shader rather than EasyGL's — the two references had already
  diverged on that point before this task; picking one is a reasonable, documented scope choice):
  - `vs_env_map3d.sc`/`fs_env_map3d.sc` — new shader pair; `fs_env_map3d.sc` samples
    `SAMPLERCUBE(s_envMap, 1)` via `reflect(-E,N)`.
  - `varying.def.sc` — added `v_eyeDir : TEXCOORD1`, needed by the new vertex shader (world-space
    eye direction) and not covered by any existing varying.
  - `envMap3DProgram_` + 6 new uniforms (`u_world`, `u_eyePos`, `u_emissiveColor`,
    `u_envMapAmount`, `u_envMapSpecular`, `s_envMap`) added to `BgfxGraphicsBackend`, created in the
    constructor and destroyed in the destructor, mirroring the existing `dualTexture3DProgram_`
    pattern exactly.
  - A new `params.envMapping` branch in `DrawPrimitivesEx`, inserted between the existing
    `skinned` and `alphaTestActive` branches, binding `params.texture0` to slot 0 and
    `params.envMap` (cast to `BgfxTextureCubeBackend`, using its public `handle` member) to slot 1.

  **Regenerating `bgfx_shaders.hpp` required building bgfx's `shaderc` tool from source** — not
  built by default in this project's `bgfx.cmake` integration (`BGFX_BUILD_TOOLS` is forced `OFF`
  unless the `CNA_BGFX_BUILD_SHADERC` option is set, and setting only that option didn't
  self-propagate into forcing `BGFX_BUILD_TOOLS` on a reconfigure — had to pass
  `-DBGFX_BUILD_TOOLS=ON` directly). Built `shaderc` (~2 minutes, links against the already-built
  `tint`/`bx`/`bimg` libraries in the existing `cmake-build-bgfx` tree), then ran
  `compile_shaders.py` against it — all 8 target variants (GLSL/ESSL/SPIR-V/WGSL × vertex/fragment)
  compiled cleanly for the new shader pair, alongside the 7 pre-existing pairs. Diffed the
  regenerated `bgfx_shaders.hpp` against the pre-existing one: every GLSL/ESSL/SPIR-V variant and
  every fragment shader is byte-for-byte identical; only the 7 pre-existing vertex shaders' `_wgsl`
  (WebGPU) byte arrays changed, apparently from non-deterministic internal symbol numbering in
  bgfx's `tint`-based WGSL backend across separate invocations. WebGPU isn't wired into any runtime
  path this project actually exercises today (Linux/OpenGL here; WebGPU is Phase 56–69, parked), so
  this is inert — flagged here only for diff-review transparency, not a functional concern.

  Bgfx has no GPU readback API in this project (documented pre-existing limitation — see
  `bgfx_render_target_usage_test.cpp`), so a pixel-verified test analogous to EasyGL/Vulkan's isn't
  possible. Added `Bgfx_EnvironmentMapEffect_Smoke` (new `examples/bgfx_env_map_test.cpp`) instead:
  exercises all 4 of the EasyGL/Vulkan test's configurations (varying `EmissiveColor`,
  `EnvironmentMapAmount`, `EnvironmentMapSpecular`, and the cube map itself) across 3 frames,
  verifying no crash/exception — confirmed the new program compiles, links, and draws without
  error on the `OpenGL` bgfx renderer. 1908/1908 Bgfx ctest pass (100%, no regressions from either
  the shader regeneration or the new draw-dispatch branch).

### CubeMapFace validation (Task 279, Phase 33)

Task 272's audit already flagged this as a known gap: `static_cast<int>(face)` was passed straight
to the backend with no range check, for every `TextureCube::SetData`/`GetData` overload.

Checked FNA's `TextureCube.cs` first: FNA itself **never validates `cubeMapFace`** — it goes
straight through to `FNA3D_SetTextureDataCube`/`FNA3D_GetTextureDataCube` with no range check
anywhere in the managed layer. So this is not a literal-parity gap; it's a deliberate CNA safety
extra, matching the established pattern from Tasks 265/271/272.

Checked all 3 backends first, to see whether an invalid face was actually unsafe: `EasyGLTextureCubeBackend`,
`VulkanTextureCubeBackend`, and `BgfxTextureCubeBackend`'s `SetData`/`GetData` all already have
`if (face < 0 || face >= 6) return;` at the top — an out-of-range face was already memory-safe on
every backend, just a silent no-op instead of a clear, catchable error.

Added `IsValidCubeMapFace()` (a local static helper) to `TextureCube.cpp`, called at the top of the
6-arg `SetData`/`GetData` overload (the 2-arg/3-arg overloads both delegate to it, so the guard
covers every arity). Throws `std::out_of_range`, matching the exception type already used for this
function's other range checks (`level < 0`, rect bounds). 7 new unit tests in `TextureCubeTests.cpp`:
below-range and above-range for both `SetData`/`GetData`, covering both the simple and rect-based
overloads, plus a regression test confirming all 6 valid `CubeMapFace` values still work
(`SetDataAllSixValidFacesDoNotThrow`).

Verified across all three backends: EasyGL 1980/1982 ctest pass, Bgfx 1915/1915 (100%), Vulkan
`TextureCubeTest` 34/34 pass. A full-suite Vulkan ctest run separately showed 2 failures
(`Vulkan_FillMode_WireFrame`, `Vulkan_DepthBias`) — confirmed via `git stash`/rebuild/retest that
both reproduce identically **without** this task's change, so they are pre-existing and unrelated
(the first hadn't been previously documented; the second was already known —
`DepthBias=-1e6`). Neither investigated further here; see `NEXT.md` §5.

### Texture3D/TextureCube backend support matrix — GetData is a silent no-op on Vulkan/Bgfx (Task 280, Phase 33 — closes the phase)

Added `docs/texture3d-texturecube-support.md`, covering construction, `SetData`/`GetData`,
mip levels, `CubeMapFace` validation, shader sampling, and `DDSFromStreamEXT`, across EasyGL,
Vulkan, and Bgfx. Compiling it required reading parts of the Vulkan and Bgfx `Texture3D`/
`TextureCube` backend implementations that no prior task in this phase had inspected directly
(Tasks 271–279 all worked against EasyGL specifically), and that turned up a significant gap:

**`Texture3D`/`TextureCube::GetData` is a total silent no-op on both Vulkan and Bgfx.** Neither
`VulkanTexture3DBackend`, `VulkanTextureCubeBackend`, `BgfxTexture3DBackend`, nor
`BgfxTextureCubeBackend` overrides `GetData` — all four fall through to
`ITexture3DBackend`/`ITextureCubeBackend`'s empty base-class default
(`virtual void GetData(...) const {}`). Calling `Texture3D::GetData`/`TextureCube::GetData` on
either backend leaves the caller's output buffer completely untouched — not zeroed, just whatever
was already there — with no exception, no error, no log message. This is a "fails silently, not
loudly" gap in the same severity class as Task 663's `DDSFromStreamEXT` stub.

This had gone unnoticed through Tasks 271–279 because `Texture3DTests.cpp`/`TextureCubeTests.cpp`'s
`GetData*` unit tests are argument-guard-only — they check that invalid arguments throw and valid
ones don't, but never assert on the *value* `GetData` returns. Task 279's "Vulkan `TextureCubeTest`
34/34 pass" is entirely consistent with this bug, since none of those 34 tests read back and check
pixel data. Only EasyGL has dedicated pixel-readback integration tests
(`examples/easygl_texturecube_faces_test.cpp` etc.), which is why this was never exposed until this
task cross-referenced the Vulkan/Bgfx backend source directly against the doc's own claims.

Also flagged (not confirmed with a test — marked 🔍 in the doc): Vulkan and Bgfx very likely have
the same "mip level >0 never allocated" bug that Task 276 found and fixed for
`EasyGLTextureCubeBackend`, for **both** `Texture3D` and `TextureCube`. The code shape is identical:
`VulkanGraphicsBackend::CreateTexture3D`/`CreateTextureCube` both take `bool /*mipMap*/` and drop it;
`VkImageCreateInfo::mipLevels` is hardcoded to `1` in both `VulkanTexture3DBackend` and
`VulkanTextureCubeBackend`'s constructors. `BgfxTexture3DBackend`'s constructor takes
`bool /*mipMap*/` and calls `bgfx::createTexture3D(..., /*hasMips=*/false, ...)`;
`BgfxTextureCubeBackend` similarly hardcodes `bgfx::createTextureCube(size, false, 1, ...)`
regardless of its (unused) `mipMap` parameter.

Neither finding was fixed here — both are real feature gaps, not guard fixes, and squarely outside
a documentation task's scope. Tracked as new `plan_graphics.md` Task 864 (Vulkan/Bgfx mip-level
allocation, both texture types) and Task 865 (Vulkan `GetData` readback implementation; Bgfx's lack
of any readback API is treated as an accepted, already-documented, project-wide limitation, not a
bug). **This closes Phase 33 (Tasks 271–280) — all ten tasks are now done.**

### SurfaceFormat canonical enum table — found and fixed a real enum-conformance bug (Task 281, Phase 34, opens the phase)

Phase 34's first task asks for a canonical reference table of all 27 XNA/FNA `SurfaceFormat`
values with their numeric ordinals — a documentation deliverable. Building it directly against
FNA's `SurfaceFormat.cs` (rather than against CNA's existing enum) is what caught this.

**Finding:** CNA's `SurfaceFormat` enum matched FNA exactly for ordinals 0–19, but diverged
completely from ordinal 20 onward. FNA's real values at 20–26 are `ColorBgraEXT`, `ColorSrgbEXT`,
`Dxt5SrgbEXT`, `Bc7EXT`, `Bc7SrgbEXT`, `ByteEXT`, `UShortEXT` — FNA extensions beyond the original
XNA4 enum, but still part of FNA's real, current API surface (per project convention, FNA is the
authoritative reference, not just "original Microsoft XNA4"). CNA instead had 7 invented "Srgb"
variants at those same ordinals (`ColorSrgb`, `Bgr565Srgb`, `Bgra5551Srgb`, `Bgra4444Srgb`,
`Dxt1Srgb`, `Dxt3Srgb`, `Dxt5Srgb`) that don't exist in FNA at all — no sRGB variant of
`Bgr565`/`Bgra5551`/`Bgra4444`/`Dxt1`/`Dxt3` exists in real XNA/FNA; only `ColorSrgbEXT` and
`Dxt5SrgbEXT` do. This is a direct violation of the project's explicit rule that enum names must
match XNA/FNA exactly — not a stylistic quibble, since every backend does
`static_cast<int>(format)` at some point, making the ordinal values load-bearing.

Notably, `SurfaceFormatTests.cpp` already existed with a comment reading "XNA 4.0 ordinal values
(0–19) match FNA source" — pinning exactly the correct range and implicitly flagging (without
saying so directly) that 20+ was unverified. This was a useful, if silent, breadcrumb from
whichever earlier task added that file.

**Fixed** `SurfaceFormat.hpp`: replaced the 7 invented values with FNA's real 7, same order/ordinals,
with Doxygen comments translated from FNA's XML doc comments (matching this project's established
comment-translation convention). Checked blast radius first via grep before touching anything: only
one file outside `SurfaceFormat.hpp`/`SurfaceFormatTests.cpp` referenced the old invented names —
`examples/easygl_surface_format_throws_test.cpp` (Task 176's "unsupported formats throw" test).
That test works regardless of which specific non-`Color` value is used, since
`Texture::ValidateFormat` only special-cases `SurfaceFormat::Color` and throws for literally
everything else — updated the test's 5 references to use real FNA EXT names instead of the
removed ones. No other source file, anywhere in the repo, referenced any of the 7 removed names
(confirmed via `grep -rn` before and after the fix). No `SurfaceFormatHelper` or any other
`switch`-over-`SurfaceFormat` exists yet in the codebase (confirmed via search) — Task 283 will be
the first to add one, against the now-correct enum.

Added 7 new ordinal-pinning unit tests to `SurfaceFormatTests.cpp` (`ColorBgraEXTIs20` through
`UShortEXTIs26`), extending the existing 0–19 tests. Added a new "Canonical `SurfaceFormat` enum
values" section to the top of `docs/surface-format-support.md` (Task 174's pre-existing doc) with
the full 27-row table, and fixed every stale reference to the old invented names throughout the
rest of that doc's format table and priority-work sections.

Verified across all three backends: EasyGL 1987/1989 ctest pass, Bgfx 1922/1922 (100%), Vulkan
1924/1927 (the 3 failures are the already-documented `Vulkan_RenderTargetUsage` flake,
`Vulkan_FillMode_WireFrame` order-dependency, and `Vulkan_DepthBias`'s `-1e6` sub-case — no new
failures from this change).

### Texture::GetBlockSizeSquaredEXT / GetFormatSizeEXT (Task 282, Phase 34)

Task 282 asks for a shared helper that gives the CPU bytes-per-pixel or compressed-block size for
each `SurfaceFormat`. Checked FNA's `Texture.cs` first (region "Static SurfaceFormat Size Methods")
rather than inventing a `SurfaceFormatHelper` class per the plan's guessed name in
`plan_graphics.md` — FNA already has exactly this, as two public static methods directly on
`Texture`: `GetBlockSizeSquaredEXT(SurfaceFormat)` and `GetFormatSizeEXT(SurfaceFormat)`. Both are
real FNA API (not CNA inventions), so ported them onto CNA's `Texture` class with the same names,
matching the project rule to follow FNA's actual API shape over a plan's placeholder wording.

Ported both switch statements line-by-line against FNA source, covering all 27 `SurfaceFormat`
values (the full 0–26 range Task 281 just fixed):

- `GetBlockSizeSquaredEXT` — returns `16` for the 6 block-compressed formats (`Dxt1`, `Dxt3`,
  `Dxt5`, `Dxt5SrgbEXT`, `Bc7EXT`, `Bc7SrgbEXT`, all using 4×4 texel blocks) and `1` for every
  uncompressed format (a "1×1 block").
- `GetFormatSizeEXT` — returns the exact byte size of one block/texel per format: `8` (`Dxt1`),
  `16` (`Dxt3`/`Dxt5`/`Dxt5SrgbEXT`/`Bc7EXT`/`Bc7SrgbEXT`/`Vector4`), `1` (`Alpha8`/`ByteEXT`), `2`
  (`Bgr565`/`Bgra4444`/`Bgra5551`/`HalfSingle`/`NormalizedByte2`/`UShortEXT`), `4`
  (`Color`/`Single`/`Rg32`/`HalfVector2`/`NormalizedByte4`/`Rgba1010102`/`ColorBgraEXT`/
  `ColorSrgbEXT`), `8` (`HalfVector4`/`Rgba64`/`Vector2`/`HdrBlendable`).

Both throw for an unrecognized enum value — FNA's own `default:` case throws `ArgumentException`;
mapped to `std::out_of_range` here, matching the precedent set by Task 279's `CubeMapFace`
validation (an out-of-range enum value is "out of range," not "a bad argument shape").

**Drive-by fix, found while touching this file:** `Texture::ValidateFormat` (a CNA-only extension,
not present in FNA at all) was missing its `NOXNA` tag — a straightforward, zero-behavior-change
conformance fix per the project's explicit "every non-XNA method must be marked NOXNA" rule. Added
the tag and the `CNA/CNAHelper.hpp` include it requires.

Added `tests/Microsoft/Xna/Framework/Graphics/TextureTests.cpp` (new — `Texture` is abstract, but
both new methods are static, so they're exercised directly with no subclass needed): 22 tests,
exhaustive per-format coverage for both methods (grouped by expected return value, matching FNA's
own switch-case grouping) plus an invalid-enum-value test for each. Verified across all three
backends: EasyGL 1998/2000 ctest pass, Vulkan `TextureTest.*` clean, Bgfx 1933/1933 (100%) — no
regressions anywhere.

**Not yet ported — Task 283's scope:** FNA's same region also has `ValidateGetDataFormat` (throws
if `GetFormatSizeEXT(format) % elementSizeInBytes != 0`) and the internal `GetPixelStoreAlignment`
(`Math.Min(8, GetFormatSizeEXT(format))`) — both build on `GetFormatSizeEXT` and are the actual
consumers "required for SetData/GetData" that Task 283's note refers to. Not currently called by
any CNA `SetData`/`GetData` path, since `Texture::ValidateFormat` still blocks every non-`Color`
format before that logic would ever run — but they'll become load-bearing once later Phase 34
tasks add real support for more formats.

### Texture::GetPixelStoreAlignment / ValidateGetDataFormat (Task 283, Phase 34)

Ported the remaining two methods from FNA's "Static SurfaceFormat Size Methods" region
(`Texture.cs`) that Task 282 left for this task: `GetPixelStoreAlignment(SurfaceFormat)` and
`ValidateGetDataFormat(SurfaceFormat, int elementSizeInBytes)`. Both build on Task 282's
`GetFormatSizeEXT`.

- `GetPixelStoreAlignment` returns `min(8, GetFormatSizeEXT(format))` — the OpenGL 2.1 spec caps
  `GL_PACK_ALIGNMENT`/`GL_UNPACK_ALIGNMENT` at 8, so no format's natural byte size can be used
  directly above that.
- `ValidateGetDataFormat` throws unless `elementSizeInBytes` evenly divides
  `GetFormatSizeEXT(format)` — e.g. reading a `Color` (4 bytes/texel) resource into a 3-byte
  element type is invalid; a 4-byte or 2-byte or 1-byte element all divide evenly.

**Intentional visibility deviation, documented here per project convention (not in source
comments):** FNA declares both methods `internal` (assembly-wide visibility in C#). CNA has no
direct equivalent reachable from all 4 real call sites — `Texture3D::GetData`, `TextureCube::
GetData`, and `GraphicsDevice::GetBackBufferData` are not subclasses of `Texture` in CNA (the same
class-hierarchy gap Task 277/863 already documents: `Texture3D`/`TextureCube` inherit
`GraphicsResource` directly, not `Texture`), so a `protected` member would be unreachable from
three of the four places that need it. Made both `public static` on `Texture` instead — the
simplest option that actually works, and low-risk since neither method touches any instance state
(both are pure functions of their parameters).

Wired `ValidateGetDataFormat` into all 4 of FNA's real call sites, matching `Texture2D.cs`
(`GetData`, both the 3-arg and 5-arg overloads), `Texture3D.cs` (`GetData`'s 10-arg overload),
`TextureCube.cs` (`GetData`'s 6-arg overload), and `GraphicsDevice.cs`
(`GetBackBufferData`'s 4-arg rect overload) exactly, using `elementSizeInBytes = 4` at every site
(this project's fixed `Color`/RGBA-raw-bytes convention — CNA has no generic-typed `GetData<T>`
yet, unlike FNA). Since every current caller uses `Color` (always divides evenly by 4), this check
is a no-op in practice everywhere it's wired in today, matching `GetFormatSizeEXT`/
`GetPixelStoreAlignment`'s own status — but it is the exact, correct FNA-conformant
infrastructure for whenever future Phase 34 tasks add real non-`Color` format support or
generic-typed accessors.

Added 6 new unit tests to `TextureTests.cpp`: `GetPixelStoreAlignment` for small formats (own
size), large formats (clamped to 8), and an invalid-format throw; `ValidateGetDataFormat` for
even-division (no throw), uneven-division (`std::invalid_argument`), and invalid-format
(`std::out_of_range`, consistent with `GetBlockSizeSquaredEXT`/`GetFormatSizeEXT`'s precedent from
Task 282). Verified across all three backends after wiring the new check into the 4 existing
`GetData` call sites: EasyGL 2004/2006 ctest pass, Vulkan 1942/1944 (2 pre-existing, unrelated
failures), Bgfx 1939/1939 (100%) — no regressions.

### Color format mapping — found and fixed a real Vulkan gamma bug (Task 284, Phase 34)

Task 284 asks to verify RGBA/BGRA channel-order correctness for `SurfaceFormat::Color` across
EasyGL, Vulkan, and Bgfx. Channel order was already confirmed correct everywhere — dozens of
existing pixel-readback tests across EasyGL and Vulkan check exact colors (Red, Green, Blue,
White, Black, Magenta, Yellow, Cyan) and all pass. But every one of those tests uses only
saturated 0/255 component values. That's an important, easy-to-miss blind spot: 0 and 255 are
both fixed points of the sRGB transfer curve (`srgb_decode(0)=0`, `srgb_decode(255)=255`), so a
texture that's wrongly sample-decoded as sRGB when it should be sampled as linear still round-trips
*exactly* correctly at the extremes — the bug is invisible unless a test uses a genuine mid-range
value. Deliberately tested with 128 instead, which the sRGB curve maps to a very different value
(≈188 when encoding 128/255 linear to sRGB) — large and unmistakable.

**Found two compounding bugs on Vulkan, no equivalent on EasyGL or Bgfx:**

1. `VulkanTextureBackend` (the backend behind `Texture2D`) created its `VkImage` and `VkImageView`
   with `format = VK_FORMAT_R8G8B8A8_SRGB`. This is inconsistent with `VulkanTexture3DBackend`,
   `VulkanTextureCubeBackend`, and the Vulkan render-target color attachment, all of which
   correctly use `VK_FORMAT_R8G8B8A8_UNORM` — and it's wrong regardless of internal consistency:
   confirmed via FNA's `SurfaceFormat.cs` (Task 281's audit) that plain `SurfaceFormat.Color` is
   linear, not sRGB; FNA has a *separate* `SurfaceFormat.ColorSrgbEXT` value for the gamma-encoded
   variant. An `_SRGB`-format Vulkan image applies an automatic sRGB→linear decode on every texture
   sample, which is simply the wrong transform for plain `Color` data.
2. `VulkanGraphicsBackend::CreateSwapchain()` explicitly searched the surface's supported formats
   for `VK_FORMAT_B8G8R8A8_SRGB` and preferred it whenever available (falling back to `fmts[0]`
   otherwise) — applying an automatic linear→sRGB gamma *encode* to every pixel written to the
   swapchain at present time, regardless of what drew it. This looks like a carried-over default
   from generic Vulkan tutorial boilerplate (many Vulkan getting-started guides recommend an sRGB
   swapchain "for accurate color") that was never reconciled with XNA/FNA's actual no-gamma-by-
   default `SurfaceFormat.Color` semantics — none of the fragment shaders anywhere in this codebase
   do any gamma math, and no other color source (vertex colors, `DirectionalLight`, `BasicEffect`'s
   diffuse/emissive/ambient colors) is ever gamma-decoded before use.

**Why this went undetected until now:** the two bugs are approximate inverses of each other for
*textured* content — sample-time sRGB decode, then present-time sRGB encode, is close to an
identity transform (`encode(decode(x)) ≈ x`), so a textured quad's mid-grey byte value read back
looking correct *by coincidence*. But any **non-textured** rendering — plain vertex colors, lit
`BasicEffect`/`EnvironmentMapEffect` output, alpha-blended colors, anything that doesn't sample a
`Texture2D` — only goes through the swapchain's uncompensated encode, with nothing to cancel it.
Confirmed by direct test: a nominal `(128,128,128)` `VertexPositionColor` quad (no texture) read
back as `(188,188,188)` — a 60-unit error, dramatic and unmistakable once the right test is run.

**Fixed both root causes:**
- `VulkanTextureBackend`'s image and view format: `VK_FORMAT_R8G8B8A8_SRGB` → `_UNORM`.
- `CreateSwapchain()`'s format preference: `VK_FORMAT_B8G8R8A8_SRGB` → `_UNORM` (with a source
  comment explaining why, since "avoid the commonly-recommended sRGB swapchain" is a genuinely
  non-obvious constraint a future reader would otherwise question).

Verified the existing BGRA-vs-RGBA channel-swap logic in the backbuffer readback path
(`isBGRA = swapchainFormat_ == VK_FORMAT_B8G8R8A8_UNORM || == VK_FORMAT_B8G8R8A8_SRGB`) already
checks for *both* BGRA variants, so it needed no change and remains correct as a defensive fallback
for devices where the UNORM+`VK_COLOR_SPACE_SRGB_NONLINEAR_KHR` combination isn't available.

Added `examples/vulkan_texture_srgb_test.cpp` (`Vulkan_Texture2D_ColorFormat_Linear` ctest):
renders a mid-grey (128,128,128) full-screen quad two independent ways in the same frame — (a)
`BasicEffect` with `VertexColorEnabled=true`, no texture; (b) `BasicEffect` with a sampled
`Texture2D` filled with the same value, `DiffuseColor=white` (no tint) — and diffs the two
backbuffer center-pixel readbacks. This methodology sidesteps needing to reason about the
swapchain's absolute gamma transform: whatever the swapchain does, it does to both scenes equally,
so a divergence between them isolates exactly the texture-sampling-specific transform. Verified the
test actually catches the bug before applying either fix (diff=60), then confirmed it passes after
(diff=0).

Verified across all three backends: **EasyGL and Bgfx have no equivalent bug** — grepped both
backends' source for any `SRGB`/`Srgb` reference; neither has one anywhere. Full Vulkan ctest suite
after the fix: 1944/1945 (serial) / 1943/1945 (parallel) — only the pre-existing, already-documented
`Vulkan_DepthBias` failure remains consistently; `Vulkan_FillMode_WireFrame`'s already-documented
order-dependent flakiness (found during Task 279) appeared in the parallel run only, unrelated to
this change. No regressions from a fix with this much surface area (it touches the swapchain format
used for literally every pixel Vulkan presents).

---

## `Microsoft::Xna::Framework::Graphics::PackedVector`

All 19 types audited. Missing Vector-form constructors added.

| Class | Status | Notes |
|---|---|---|
| Alpha8 | ✅ | API complete |
| Bgr565 | ✅ | Added `Bgr565(Vector3)` constructor |
| Bgra4444 | ✅ | Added `Bgra4444(Vector4)` constructor |
| Bgra5551 | ✅ | Added `Bgra5551(Vector4)` constructor |
| Byte4 | ✅ | Added `Byte4(Vector4)` constructor |
| HalfSingle | ✅ | API complete |
| HalfTypeHelper | ✅ | API complete |
| HalfVector2 | ✅ | Added `HalfVector2(Vector2)` constructor |
| HalfVector4 | ✅ | Added `HalfVector4(Vector4)` constructor |
| IPackedVector / IPackedVector\<T\> | ✅ | Complete |
| NormalizedByte2 | ✅ | Added `NormalizedByte2(Vector2)` constructor |
| NormalizedByte4 | ✅ | Added `NormalizedByte4(Vector4)` constructor |
| NormalizedShort2 | ✅ | Added `NormalizedShort2(Vector2)` constructor |
| NormalizedShort4 | ✅ | Added `NormalizedShort4(Vector4)` constructor |
| Rg32 | ✅ | Added `Rg32(Vector2)` constructor |
| Rgba1010102 | ✅ | Added `Rgba1010102(Vector4)` constructor |
| Rgba64 | ✅ | Added `Rgba64(Vector4)` constructor |
| Short2 | ✅ | Added `Short2(Vector2)` constructor |
| Short4 | ✅ | Added `Short4(Vector4)` constructor |

---

## `Microsoft::Xna::Framework::Input`

`Status` below is API-surface completeness (every method/property/constant declared and
callable). `Runtime` is separate: whether calling those members produces real, FNA-faithful
behavior wired to SDL3, versus a stub/no-op. Both were closed to "real" for nearly everything
in this namespace by the `feature/input` branch (`plan_input.md`, Phases I1–I6, tasks 700–776);
see that file for full per-task detail and any accepted deviations from FNA.

| Class / Enum | Status | Runtime | Notes |
|---|---|---|---|
| Buttons (enum) | ✅ | N/A | Complete; pure value enum |
| ButtonState (enum) | ✅ | N/A | Complete; pure value enum |
| GamePad | ✅ | Real | `GetState`/`GetCapabilities`/`SetVibration` and all EXT methods (light bar, trigger vibration, gyro, accelerometer) wired to real SDL3 gamepad hardware (Phase I3, tasks 725–740). `PacketNumber` bumps on real connection/button/axis changes (task 729, tracked at the `InputManager` layer rather than by comparing built `GamePadState`s — see in-source note). |
| GamePadButtons | ✅ | Real | Derived from real button state; `GetHashCode` and `FromButtonArray` (renamed from `FromButtons` in task 731) are both FNA-faithful. |
| GamePadCapabilities | ✅ | Real | Populated from real SDL3 `SDL_Gamepad` capability queries (task 730 rework: properties + `NOXNA`-tagged internal setters). |
| GamePadDeadZone (enum) | ✅ | N/A | Complete; pure value enum |
| GamePadDPad | ✅ | Real | Derived from real button state; `GetHashCode` is FNA-faithful; `FromButtonArray` (renamed from `FromButtons`, and reconciled to FNA's `params Buttons[]` signature, task 731) is FNA-faithful. |
| GamePadState | ✅ | Real | `ToString()` matches FNA's `ValueType` default (task 733). `GetHashCode()` (`buttons_.GetHashCode() ^ (packetNumber_ * 31)`) is an accepted deviation, not a literal port: FNA's own `GetHashCode()` is `return base.GetHashCode()`, .NET's non-deterministic reflection-based default, which has no reproducible C++ equivalent. |
| GamePadThumbSticks | ✅ | Real | `Circular`/`IndependentAxes`/`None` dead-zone math all implemented and tested (task 738); `GetHashCode` FNA-faithful (task 732). |
| GamePadTriggers | ✅ | Real | Dead-zone exclusion + clamp implemented and tested (task 739); `GetHashCode` FNA-faithful (task 732). |
| GamePadType (enum) | ✅ | N/A | Complete; pure value enum |
| Keyboard | ✅ | Real | `GetState` wired to real SDL3 key events via `InputManager`; `GetKeyFromScancodeEXT` is a real layout-aware xnaMap/keyMap round-trip with both default and scancode (`FNA_KEYBOARD_USE_SCANCODES`) modes (Phase I5, tasks 760–768). |
| KeyboardState | ✅ | Real | `GetPressedKeys()` ascending order, `GetHashCode()` FNA's 8-word XOR formula, and `ToString()` matching FNA's `ValueType` default are all FNA-faithful (tasks 760, 761, 766). |
| Keys (enum) | ✅ | N/A | Complete; pure value enum; underlying type explicit `int` (task 767, cosmetic) |
| KeyState (enum) | ✅ | N/A | Complete; pure value enum |
| Mouse | ✅ | Real | `SetPosition`, `IsRelativeMouseModeEXT`, `ClickedEXT` all wired to real SDL3 mouse state (Phase I4, tasks 745–749). Known deviation: `SetPosition`'s `SDL_WarpMouseInWindow` target has no inverse logical→window coordinate transform, so it is off by the scale factor on a letterboxed/scaled window (documented in-source in `Mouse.cpp`; needs a graphics-layer addition, out of scope for this branch). |
| MouseCursor | ✅ | Real | MonoGame-derived `NOXNA` extension — no `MouseCursor` type exists in FNA or XNA 4.0. **Status decision (Task 754): kept**, not dropped — it is the standard way MonoGame/FNA-family games set custom and stock OS cursors, and CNA's `Mouse::SetCursor(MouseCursor&)` already depends on it. CHECKLIST-compliant as of Phase I4 tasks 750-753 (correct SPDX, full `NOXNA` tagging, `FromTexture2D`, `Dispose`/`IDisposable`, lazy stock-cursor construction, `WaitCursor`→`WaitArrow` rename). No separate `Handle` property was added: the existing `NOXNA GetSDLCursor() const` (returning `SDL_Cursor*`) already serves as MonoGame's `IntPtr Handle` equivalent — a second handle accessor returning the same pointer reinterpreted as a generic integer would be pure duplication with no current consumer. |
| MouseState | ✅ | Real | Populated from real SDL3 mouse position/button/scroll-wheel state. |
| TextInputEXT | ✅ | Real | Wired to `SDL_EVENT_TEXT_INPUT`/`SDL_EVENT_TEXT_EDITING`, `SDL_StartTextInput`/`StopTextInput`, `SDL_SetTextInputArea` (Phase I1, tasks 700–708). `TextInput` callback is `charcs`/`char16_t` — one UTF-16 code unit per call (astral code points as surrogate pairs), matching FNA's `Action<char>` (task 806). `TextEditing` stays a UTF-8 `std::string` (a separate documented deviation). |

---

## `Microsoft::Xna::Framework::Input::Touch`

See the `Status`/`Runtime` note above the `Input` table — the same distinction applies here.
Touch's runtime behavior went from effectively dead (Phase I2's root cause: SDL finger events
never reached `GestureDetector`) to real and gesture-tested (`plan_input.md` Phase I2, tasks
710–722).

| Class / Enum | Status | Runtime | Notes |
|---|---|---|---|
| GestureSample | ✅ | Real | Constructed by `GestureDetector` from real touch input; both constructors (public 6-arg, internal 8-arg with finger ids) tested. |
| GestureType (enum) | ✅ | N/A | Complete; pure value enum |
| TouchCollection | ✅ | Real | Reflects real touch state via `InputManager`; settable indexer + `begin`/`end` iteration tested (task 718, 722). |
| TouchLocation | ✅ | Real | `ToString`/`GetHashCode` FNA-faithful (task 715); `SetFinger`'s Moved/Released branches carry real previous-state/position so `TryGetPreviousLocation` succeeds (task 713). |
| TouchLocationState (enum) | ✅ | N/A | Complete; pure value enum |
| TouchPanel | ✅ | Real | `SDL_EVENT_FINGER_*` now feed `INTERNAL_onTouchEvent`, `TouchDeviceExists`, and `DisplayWidth`/`DisplayHeight` (from the real back buffer size), which is what makes `GestureDetector`'s Tap/DoubleTap/Hold/Drag/Flick/Pinch/PinchComplete recognition work end-to-end (tasks 710–712). Known deviation: `GetState()` falls back to `InputManager`'s event-driven touch snapshot rather than FNA's per-frame poll population of `touches_` (documented in-source, task 714). Known minor bug (not yet fixed): `GetCapabilities()` passes `MAX_TOUCHES` unconditionally in both branches instead of `0` when disconnected like FNA (noted, not fixed, in task 721). |
| TouchPanelCapabilities | ✅ | Real | Reflects `TouchDeviceExists`/`MAX_TOUCHES` (see the `TouchPanel` gap above for the one known deviation from this). |

---

## `Microsoft::Xna::Framework::Media`

| Class / Enum | Status | Notes |
|---|---|---|
| Album | ✅ | Real thumbnails (genuinely downscaled, `MEDIA-209`) and embedded ID3v2 APIC / FLAC PICTURE cover art (`MEDIA-206`/`207`) as of Phase 16; `HasArt` is asserted to agree exactly with what `GetAlbumArt()` can deliver. Real, from-scratch local-library implementation (FNA itself is a permanent `NotImplementedException` stub — no upstream behavior to match); backed by `MediaLibraryIndex`, grouped by (Name, Artist) |
| AlbumCollection | ✅ | Real; backed by `CNA::Internal::Media::MediaCollectionBase<Album>` |
| Artist | ✅ | Real, from-scratch (see Album's note); case-insensitive name dedup against tag-casing inconsistencies (`plan_media.md` D10) |
| ArtistCollection | ✅ | Real; backed by `MediaCollectionBase<Artist>` |
| Genre | ✅ | Real, from-scratch (see Album's note) |
| GenreCollection | ✅ | Real; backed by `MediaCollectionBase<Genre>` |
| MediaLibrary | ✅ | Real, from-scratch orchestrator: synchronous point-in-time scan of real OS Music/Pictures folders (`CNA::Internal::Media::MediaLibraryPaths`) at construction, builds the whole Song/Album/Artist/Genre/Picture/PictureAlbum/Playlist object graph (`plan_media.md` §4, MEDIA-46..69) |
| MediaPlayer | ✅ | Implemented (SDL3_mixer). Visualization is **genuinely functional as of Phase 16** (`MIX_SetPostMixCallback` PCM tap + a from-scratch radix-2 FFT, `MEDIA-186`..`191`); it was a pure stub before, with a test that asserted the broken behavior as if it were the specification |
| MediaQueue | ✅ | API complete |
| MediaSource | ✅ | Real; `GetAvailableMediaSources()` returns one real `LocalDevice` entry. All 4 XNA members present. `WindowsMediaConnect` device *discovery* is deliberately not implemented — an Xbox 360/WMP-era concept with no desktop equivalent; the enum value itself exists (`MEDIA-212`) |
| MediaSourceType (enum) | ✅ | Complete |
| MediaState (enum) | ✅ | Complete |
| Picture | ✅ | Real downscaled thumbnails as of Phase 16 (`MEDIA-210`; `GetThumbnail()` used to be a synonym for `GetImage()`). Real, from-scratch (see Album's note); dimensions via the existing `CNA::Internal::Graphics::ImageLoader` (reused, not reimplemented) |
| PictureAlbum | ✅ | Real, from-scratch; real filesystem-tree-mirroring parent/child structure |
| PictureAlbumCollection | ✅ | Real; backed by `MediaCollectionBase<PictureAlbum>` |
| PictureCollection | ✅ | Real; backed by `MediaCollectionBase<Picture>` |
| Playlist | ✅ | Real, from-scratch; backed by a real M3U/M3U8 parser (`plan_media.md` D5) |
| PlaylistCollection | ✅ | Real; backed by `MediaCollectionBase<Playlist>` |
| Song | ✅ | API complete **as of Phase 16** — `Album`/`Artist`/`Genre`/`ToString()` were MISSING until `MEDIA-174`/`176` (CNA inherited the omission from FNA's own `Song.cs`; the previous ✅ here was inaccurate). `TrackNumber`/`IsRated`/`Rating` are now real tag-derived values, not hardcoded constants (`MEDIA-181`/`184`) |
| SongCollection | ✅ | API complete; member-level diff against the XNA reference XML confirms no gaps (`MEDIA-213`) |
| Video | ⚠️ | API complete on Linux/macOS (FFmpeg-backed). On Windows/Android/Emscripten, `Video.cpp` itself is excluded from the build (`cmake/CnaLibrary.cmake`'s `CNA_FFMPEG_AVAILABLE` gate) while the public header stays available -- referencing this class there is a link error, not a graceful runtime `NotSupportedException` (found by external code review, `plan_media.md` §10) |
| VideoPlayer | ⚠️ | Implemented (FFmpeg) on Linux/macOS; same Windows/Android/Emscripten link-error caveat as `Video` above |
| VideoSoundtrackType (enum) | ✅ | Complete |
| VisualizationData | ✅ | API complete |

---

## `Microsoft::Xna::Framework::Storage`

| Class | Status | Notes |
|---|---|---|
| StorageContainer | ✅ | API complete |
| StorageDevice | ✅ | API complete |
| StorageDeviceNotConnectedException | ✅ | Complete |

---

## `Microsoft::Xna::Framework::GamerServices`

| Class | Status | Notes |
|---|---|---|
| GamerServicesNotAvailableException | ✅ | Stub added |
| Gamer | ✅ | Full port; abstract base, tests via FriendGamer subclass |
| GamerProfile | ✅ | Full port; tests complete |
| LeaderboardEntry | ✅ | Full port; tests complete; `operator==`/`operator!=` added (NOXNA — required by `ReadOnlyCollection<T>`, not present in FNA) |
| LeaderboardWriter | ✅ | Full port; `GetLeaderboard` always throws (matches FNA stub) |
| LeaderboardReader | ✅ | Full port; tests complete |
| SignedInGamer | ✅ | Full port; tests complete |
| GamerServicesDispatcher | ✅ | Full port: `IsInitialized`, `WindowHandle`, `InstallingTitleUpdate` event, `Initialize()` (creates 4 stub `SignedInGamer`s, fires `OnSignIn`), `Update()`, `UpdateAsync()`. `Initialize()` deliberately not exercised by the automated test suite (sets process-lifetime static state) |
| GamerServicesComponent | ✅ | Full port — wires `Initialize()`/`Update()` to `GamerServicesDispatcher`; no tests (requires a live `Game`, same as `GameComponent`) |
| Guide | ✅ | Full port — replaced the old `DEF_PROP`-based stub (which had an invented, non-FNA `Show(PlayerIndex)` method); tests complete except the always-empty `Show*` no-ops verified only for non-throw |
| AvatarBodyType (enum) | ✅ | Complete |
| AvatarRendererState (enum) | ✅ | Complete |
| AvatarMouth (enum) | ✅ | Complete |
| AvatarEye (enum) | ✅ | Complete |
| AvatarEyebrow (enum) | ✅ | Complete |
| AvatarAnimationPreset (enum) | ✅ | Complete |
| AvatarBone (enum) | ✅ | Complete; explicit numeric values with gaps (0-70 range, 55 named values), verified byte-for-byte against the real reference assembly, not guessed |
| AvatarExpression | ✅ | Full port; plain get/set struct |
| IAvatarAnimation | ✅ | Full port; interface |
| AvatarAnimation | ✅ | Full port. **Note: FNA has zero Avatar implementation** (Avatar required real Xbox Live cloud services FNA never built) — ported from the real, genuine Microsoft `Microsoft.Xna.Framework.Avatar.dll` reference assembly (v4.0.20823.0), decompiled via `monodis` for this port, since FNA's own tree has nothing to verify line-by-line against. Preserves real, verified quirks faithfully: the constructor never reads its `animationPreset` argument for any faithful field (every instance gets identical zero-valued bones and zero `Length`); `Update()` has real clamp logic, not a no-op, though `Length` being permanently zero makes every call collapse `CurrentPosition` back to zero regardless of the `loop` argument. **NOXNA extension added**: `SetRealClipNameEXT`/`GetRealClipNameEXT` (defaults to `AvatarAnimationPresetToClipNameEXT(preset)` — the one place `animationPreset` is now read, only for this new field) |
| AvatarDescription | ✅ | Full port from the decompiled reference assembly (see AvatarAnimation's note — same FNA gap). Preserves a genuinely surprising, verified quirk: `CreateRandom()`/`CreateRandom(AvatarBodyType)`/`EndGetFromGamer()` never actually randomize or populate anything — all three always return an all-zero, invalid (1021-byte) description. `BeginGetFromGamer` invokes its callback synchronously before returning (a real behavior, not present as the actual "fake-async" pattern used elsewhere in this codebase's other Begin/End stubs). The disposed-`Gamer` branch of `BeginGetFromGamer` is not covered by a test — `Gamer` has no publicly/NOXNA-accessible way to become disposed anywhere in this codebase currently |
| AvatarRenderer | ✅ | Full port from the decompiled reference assembly (see AvatarAnimation's note — same FNA gap). Preserves a genuinely surprising, verified quirk: `get_State()` unconditionally forces itself to `Unavailable` on *every single read* (not just an initial value) — nothing anywhere in the class ever sets it to `Ready` or `Loading`, so `BindPose`'s `state != Ready` guard always throws in practice. `ParentBones`' 71 real values decoded byte-for-byte from the assembly's static-array-init blob, not guessed. Both constructors ignore all of their arguments. **NOXNA extension added**: `EnableRealRenderingEXT`/`IsRealRenderingEnabledEXT`/`SetAppearanceEXT`/`DrawRealEXT` opt a game into real GPU-skinned rendering via a `Graphics::SkinnedModelEXT`, fully decoupled from the faithful 71-bone arrays above and from the always-no-op `Draw()` overloads (unaffected, still tested unchanged). See `docs/avatar-real-rendering-ext.md` |
| AvatarAnimationPresetNamesEXT (free function) | ✅ | NOXNA — not part of the XNA 4.0 API. `AvatarAnimationPresetToClipNameEXT` maps each of the 31 `AvatarAnimationPreset` values to its enumerator name, used to look up `SkinnedModelEXT` clips |
| AvatarAppearanceEXT | ✅ | NOXNA — not part of the XNA 4.0 API. CNA-invented skin/hair tint struct used only by the real-rendering extension; explicitly not a reconstruction of the real, undocumented, proprietary 1021-byte `AvatarDescription` format. No clothing customization in this phase |

---

## `Microsoft::Devices::Sensors` and `Microsoft::Devices`

**Note on methodology:** unlike every other section in this file, this namespace
has **no FNA equivalent** — FNA does not implement `Microsoft.Devices` at all
(it's a Windows Phone 7 API, not part of core XNA/FNA). This section was
originally audited against the documented WP7 Mango (OS 7.1) SDK API surface
from general reference knowledge only. **Independently re-verified 2026-07-02**
(`plan_devices_phase2.md` Task P2-2) against archived Microsoft Learn
"previous-versions" MSDN pages (the `microsoft.devices.sensors.*`/
`microsoft.devices.vibratecontroller` doc family — high confidence) plus one
MonoGame cross-check for `SensorState`'s enum values (medium confidence, no
direct MSDN enum page found). Four real gaps were found this pass —
`plan_devices_phase2.md` Phase 7 (Tasks P2-14–P2-17, all now done as of
2026-07-02) has the individual fix history for each; the per-class notes
below still record what each gap was and how it was resolved. Everything
else in the table was confirmed to match the documented API exactly.

### Final precise status table (`plan_devices.md` Phase 10, Task DEVICES-0142, 2026-07-05)

**Supersedes `plan_devices_phase9.md` Task P9-7's table below** — `plan_devices.md`
(143 tasks, Phases 0-10, all closed) gave `Compass`/`Motion` real Android backends and
a working Android APK build/install/launch, both "not implemented"/"not available" as of
Phase 9. The per-class table below (`Status` column, ✅) tracks XNA/WP7 **API
completeness** only. It does not by itself say whether a class's *runtime* is real or
stubbed, whether its concurrency has been sanitizer-verified, whether it cross-compiles
for Android, or whether it has ever run on real hardware — those are four separate,
independent questions, deliberately kept separate here rather than folded into one
"done" verdict, continuing this table's own established convention. Read every cell
below as of what was actually run in a session, not as a general claim.

| Component | API surface | SDL/native runtime | Concurrency (sanitizers) | Android compile | Physical hardware |
|---|---|---|---|---|---|
| `Accelerometer` | Complete, matches documented WP7 SDK (`plan_devices_phase2.md` Task P2-2) | **Real**, SDL3-backed (`SDL_SENSOR_ACCEL`); dispatch/lifetime/concurrency hardened across Phases 5-8; m/s²→g conversion re-confirmed correct (`plan_devices.md` Task DEVICES-0063) | ASan/TSan/UBSan **clean** (Task DEVICES-0140, 2026-07-05 — one confirmed-unrelated pre-existing `sharp-runtime` TSan finding, not this class) | **Passes** (NDK r30, arm64-v8a, API 24) | **App-level real-device run: not yet done.** The Android emulator now works (`/dev/kvm`, Task DEVICES-0126) and `cna_demo_devices` ran live on it — `Accelerometer`'s `DrawEventFlash()` was observed responding to injected emulator sensor values — but Android axis-remap sign convention has never been checked against a real physical tilt on real hardware |
| `Gyroscope` | Complete, matches documented WP7 SDK (no `Calibrate`/`State` on the real API) | **Real**, SDL3-backed (`SDL_SENSOR_GYRO`); identical implementation/hardening to `Accelerometer`; rad/s unit re-confirmed correct, no conversion needed (Task DEVICES-0064) | Same as `Accelerometer` — **clean** | **Passes**, same verification as `Accelerometer` | Same emulator-only status as `Accelerometer` |
| `Compass` | Complete API shell, matches documented WP7 SDK | **Real on Android** (`Detail::AndroidCompassBackend`, NDK-native, no JNI — Phase 7): `TYPE_ROTATION_VECTOR` for `MagneticHeading`, `TYPE_MAGNETIC_FIELD` for `MagnetometerReading`/accuracy/`Calibrate`. Honest `NotSupported` stub on every other platform (SDL3 has no magnetometer API anywhere) | Azimuth/accuracy math unit-tested for self-consistency (11 tests); base-class locking unaffected, still sanitizer-clean | **Passes**, `llvm-nm`-confirmed real `ASensorManager`/math symbols compiled in, not just the stub | **Emulator: app installed/launched/rendered** (screenshot-confirmed), but this emulator's virtual sensor set has no confirmed rotation-vector sensor to inject through — Compass's own Android path was not separately exercised via synthetic values. **Real device: never tried.** `TrueHeading` permanently limited to equal `MagneticHeading` (no `System.Device.Location`) |
| `Motion` | Complete API shell, does **not** require constructing `Accelerometer`/`Compass`/`Gyroscope` (corrected a misleading doc comment, Task DEVICES-0114) | **Real on Android** (`Detail::AndroidMotionBackend`, Phase 8): `TYPE_ROTATION_VECTOR`/`TYPE_GAME_ROTATION_VECTOR` fallback for `Attitude`, `TYPE_GRAVITY`/`TYPE_LINEAR_ACCELERATION`/`TYPE_GYROSCOPE` for the rest (g-force conversion bug found and fixed for the first two, Task DEVICES-0108/0109). Honest `NotSupported` stub elsewhere. `Calibrate` never raised by any backend | Yaw/pitch/roll extraction derived and round-trip-verified against CNA's own tested `Quaternion`/`Matrix` math (9 tests) | **Passes**, `llvm-nm`-confirmed | Same emulator-only status as `Compass` — coordinate-remap for `Gravity`/`DeviceAcceleration`/`DeviceRotationRate`/`Attitude` is an explicit, documented open question (Task DEVICES-0111), not resolved either way |
| `VibrateController` | Complete, matches WP7 instance API plus documented `NOXNA` extensions | **Real**, SDL3 haptic-backed (unchanged this plan — Task DEVICES-0031 decided a native Android bridge is unnecessary: SDL3's own Android haptic backend already reaches `Context.VIBRATOR_SERVICE` with full amplitude control) | Clean; 2 new tests (singleton-across-usage, full unsupported-contract), 40/40 loop clean | Passes (part of the same `CNA` library cross-compile) | **Not verified on a real phone motor or real gamepad.** Software guarantees verified live on this desktop and on the emulator (no haptic hardware in either) |
| `SensorBase<T>` | Complete API surface | N/A (abstract base) | Every field mutex-guarded; 2 new direct base-level tests closed a coverage gap (Task DEVICES-0052/0053); one **new, out-of-scope finding**: `sharp-runtime`'s `EventHandler<T>::Raise()` iterates its live handler list directly, not a snapshot (Task DEVICES-0057) — not currently reachable by any production code path in this namespace, not fixed here | N/A | N/A |
| `Detail::AndroidSensorBridge` | N/A (CNA-internal, not XNA-facing) | **Real** (Phase 6): shared NDK `ASensorManager`/`ASensorEventQueue`/`ALooper` wrapper backing both `Compass` and `Motion`'s Android paths | Inert-on-non-Android path ASan/UBSan-clean; the real `#ifdef __ANDROID__` code cannot execute in this container, only compile | Passes, `llvm-nm`-confirmed | Dispose-mid-callback self-join/detach boundary code-reviewed and cross-compiled, **never runtime-exercised** (real code path can't run here) |
| `System.Device.Location` (GPS) | **Not implemented.** Explicitly out of scope by project decision | N/A | N/A | N/A | N/A — future plan only, see `docs/location-future-plan.md` |

**What "done" now means for this namespace, precisely:** API-complete and
SDL-runtime-hardened for `Accelerometer`/`Gyroscope`/`VibrateController` — yes, unchanged
from Phase 9. **`Compass`/`Motion` now have real, cross-compile-verified, Android-launched
native backends** — new as of this plan. **Physically verified on real Android/iOS
hardware or a real haptic motor/gamepad — still no**, in any session to date, for any
component; the emulator (new this session) closes the "does the software pipeline work"
question, explicitly not the "is it physically correct" one (see
`docs/devices-hardware-checklist.md` §9). iOS: still no Apple toolchain, confirmed fresh
this session (Task DEVICES-0131) — no iOS code exists for `Compass`/`Motion` beyond the
unchanged design sketch in `docs/devices-native-backend-design.md`.

| Class / Enum | Status | Notes |
|---|---|---|
| Accelerometer | ✅ | Real SDL3-backed (`SDL_SENSOR_ACCEL`); Android landscape axis remap. Class shape (`IsSupported`, `State`) confirmed 2026-07-02 against MSDN `ff707531` (corrected 2026-07-06, `plan_devices.md` Task ACCEL-001 — this row previously cited `ff707930`, which is actually `Accelerometer.ReadingChanged`'s own page, not `State`'s). `Dispose()` name-hiding bug fixed and full `AccelerometerTests.cpp` (9 tests) added 2026-07-02 (Task P2-3); `GetTypeNameCPP` corrected to the dot-separated convention 2026-07-02 (Task P2-4); legacy `ReadingChanged` event wired up 2026-07-02 (Task P2-15). **Fixed (2026-07-03, `plan_devices_phase3.md` Task P3-4):** shared static sensor state (`g_sensor_`/`g_sensorId_`/`eventWatchRegistered_`/`startedInstances_`) is now guarded by a `static std::mutex` against the SDL event-watch callback running off-thread (SDL's own `SDL_AddEventWatch()` doc warns it may). **`plan_devices_phase5.md` (2026-07-04) re-audited this class specifically because Phase 4's own fixes turned out to be incomplete:** Task P5-1 fixed a subsystem ref-count leak Task P4-8 itself introduced; Task P5-2 replaced the single-bool callback-quiescence flag (which could under-count concurrent dispatches) with a thread-id vector; Task P5-3 removed the self-dispose-from-own-callback deadlock Task P4-2 had explicitly accepted as permanent; Task P5-4 moved the shared subsystem/event-watch machinery into `Detail::SdlSensorSubsystem<Accelerometer>`, verified byte-for-byte behavior-preserving; Task P5-7 made the Android axis-remap sign math a unit-tested pure function. See that plan's "Audit findings" section for the full root-cause writeups — none of this was found by re-reading this row's own prior claims at face value. **`plan_devices_phase6.md` (2026-07-04) re-audited again, on the same "do not trust Phase 5's own claims" premise, and found Phase 5 had itself left real gaps:** Task P6-1 fixed an unlocked `instanceCount_` check+increment in the constructor (raced against `Dispose()`'s locked decrement) — its own new concurrency test then surfaced a *second*, more serious bug found only by looping the test many times, not a single run: `getIsSupportedProperty()` made real SDL calls with no synchronization, violating SDL3's own documented "`SDL_InitSubSystem()` should only be called on the main thread"/"`SDL_QuitSubSystem()` is not thread safe" contract, reproducibly corrupting the heap under concurrent construction (fixed by locking `subsystem.mutex_` for the whole call). Task P6-2 fixed a subsystem-hold leak on a failed `Start()`. Task P6-3 made `started_`/`state_`/`subsystemHeld_` consistently locked (previously read unlocked in several places) and added `SensorBase::ClaimDisposalOnce()` to prevent a double-dispose race. Task P6-4 added an RAII exception-safety guard so a throwing `CurrentValueChanged`/`ReadingChanged` handler can no longer permanently corrupt dispatch-tracking state and deadlock a future `Dispose()`. Task P6-7 added semantic (tilt-left, face-up/down) axis tests. See `plan_devices_phase6.md`'s "Audit findings" section for full root-cause detail. **`plan_devices_phase7.md` (2026-07-04) found Phase 6's own fixes still had two real gaps in this class:** Task P7-1 found `getIsSupportedProperty()`'s per-class `subsystem.mutex_` (P6-1's addendum) only serialized this class's own SDL calls against itself, not against `Gyroscope`'s identical calls on a *different* mutex — added a process-wide `Detail::GetGlobalSdlSensorMutex()`, verified with a new cross-class stress test (40/40 clean). Task P7-2 found a losing concurrent `Dispose()` call could flip `disposed_` true while the winner's own `Stop()` call was still relying on it being false, causing that `Stop()` call to throw mid-cleanup and leak state — fixed with `SensorBase::WaitForDisposalToComplete()`, confirmed via a temporary revert that reproduced the exact failure. Task P7-3 found and fixed **the most serious bug of this phase**: `SensorEventWatch()`'s dispatch loop could be left holding a dangling pointer if one instance's callback disposed a *different*, not-yet-dispatched instance from the same batch — confirmed as a real, reliably reproducible (5/5) segfault via a deliberate temporary revert, not a theoretical concern. Task P7-4 fixed the one remaining unguarded test-only getter (`GetSubsystemHeldForTesting()`). **`plan_devices_phase8.md` (2026-07-04) found Phase 7's own dispatch-loop fix (Task P7-3) had not covered every use-after-free path:** Task P8-1 found and fixed **the most significant bug of this phase**: a callback destroying (not just `Dispose()`-ing) its own sensor object mid-dispatch could still leave `DispatchToInstances()`'s/`InjectSyntheticSensorUpdate()`'s cleanup guards touching freed memory (they captured the raw instance/`this` and dereferenced it after the callback returned). Fixed by replacing the plain per-instance `dispatchingThreadIds_` vector with `dispatchToken_`, a `std::shared_ptr<std::vector<std::thread::id>>` copied into the cleanup guard *before* invoking the callback. Confirmed via a throwaway ASan build — not a plain run, which did not reproduce the bug at all across repeated attempts — that the reverted code produces a definitive `heap-use-after-free` and the fixed code produces none. **Explicitly documented, not fixed, one remaining boundary specific to this class**: destroying `Accelerometer` from within its own `CurrentValueChanged` handler stays unsafe, because `DispatchSensorReading()` unconditionally calls `getIsDataValidProperty()` again afterward (to decide whether to also raise the legacy `ReadingChanged` event) regardless of whether `ReadingChanged` even has a subscriber — a class-design property (`ReadingChanged` is itself a member of `this`), not a dispatch-bookkeeping gap the token can close; fixing it would require redesigning where the event objects live relative to instance identity, out of scope for this task. Task P8-3 made `getIsSupportedProperty()`'s `ProbeIsSupported()` call (and `Start()`'s `EnsureSubsystemInitialized()`/`OpenDefaultSensorLocked()` calls) require a compile-time lock-proof parameter instead of relying on a doc comment alone to remember the global SDL sensor mutex — verified the guard actually rejects a lock-free call via a throwaway scratch compile. |
| AccelerometerFailedException | ✅ | Full tests (7). **Fixed (2026-07-02, Task P2-16):** gained `ErrorId` via the same `(const char*, SharpRuntime::intcs)` constructor overload added to `SensorFailedException`. Confirmed it does inherit `SensorFailedException` (visible directly in its own `.hpp`; the earlier "unverified" note was overly cautious). |
| AccelerometerReading | ✅ | Full tests. **Fixed (2026-07-03, `plan_devices_phase3.md` Task P3-2):** setters are now `private` + `friend class Accelerometer`, matching the real API's `internal set` (previously fully public, a confirmed deviation). **Fixed (2026-07-03, `plan_devices_phase4.md` Task P4-7):** `Timestamp` is now real wall-clock time (`System::DateTimeOffset::getUtcNowProperty()`); previously derived from `SDL_GetTicksNS()` (monotonic ns since SDL init) fed into a `DateTime(ticks)` constructor expecting .NET-epoch ticks — always produced a value near `0001-01-01`, never the actual reading time. |
| AccelerometerReadingEventArgs | ✅ | WP7 7.0 legacy; class itself is correct and fully tested. **Fixed (2026-07-02, Task P2-15):** `Accelerometer.ReadingChanged` (the real, `[Obsolete]`-tagged event using this type) is now wired up, raised alongside `CurrentValueChanged` from `ProcessSensorUpdateEvent()`. |
| AttitudeReading | ✅ | Full tests; member names (`Pitch`/`Roll`/`Yaw`/`Quaternion`/`RotationMatrix`) confirmed 2026-07-02. **Fixed (2026-07-03, Task P3-2):** setters are now `private` + `friend class Motion` (the class that produces `AttitudeReading` values, as `MotionReading.Attitude`), matching the real API's `internal set`. |
| CalibrationEventArgs | ✅ | Full tests. **Confirmed (2026-07-03, `plan_devices_phase3.md` Task P3-12):** the real class page (MSDN `hh220788`, vs.110) shows exactly one constructor (parameterless) and no class-specific properties/methods beyond what `System.Object` provides — CNA's empty-marker-class implementation matches exactly. |
| Compass | ✅ | **Real on Android (2026-07-05, `plan_devices.md` Phase 7, Tasks DEVICES-0086-0100):** `Detail::AndroidCompassBackend` (NDK-native, no JNI) uses `ASENSOR_TYPE_ROTATION_VECTOR` (OS-fused, avoids reimplementing sensor fusion) for `MagneticHeading`, `ASENSOR_TYPE_MAGNETIC_FIELD` for `MagnetometerReading` and accuracy status → `HeadingAccuracy`/`Calibrate`. `TrueHeading` deliberately left equal to `MagneticHeading` — true heading needs geomagnetic declination from a location source, out of scope (`docs/location-future-plan.md`). Selected via a compile-time `#if defined(__ANDROID__)` switch in `Compass.cpp`; every other platform keeps the exact pre-existing stub behavior unchanged (`getIsSupportedProperty()` hardcoded `false`, `Start()` throws `SensorFailedException`). Class shape (`IsSupported`, `Calibrate`, no legacy event) confirmed 2026-07-02 unchanged. **`getStateProperty()` tagged `NOXNA`** (Task P2-17, real `Compass` has no `State` property, `hh220912`). Azimuth math (`Detail::ConvertRotationVectorToMagneticHeadingDegrees()`) is unit-tested for self-consistency (identity → 0°, monotonic yaw response) but **never checked against real hardware** — same standing caveat as `Accelerometer`/`Gyroscope`'s own axis-remap math. `plan_devices_phase7.md` Task P7-2's `ClaimDisposalOnce()`/`WaitForDisposalToComplete()` restructuring (previously dead code, since `Start()` always threw) is now live/reachable on Android, where `Start()` can genuinely succeed. Full tests, including 6 new fake-backend delegation tests (`CompassTests.cpp`) and 11 new pure-math tests (`AndroidCompassMathTests.cpp`). **Layered status, not a flat "complete" claim:** API surface complete; SDL/native runtime real on Android, still honest `NotSupported` stub everywhere else; sanitizers/Android-compile clean (`llvm-nm`-confirmed real `ASensorManager`/`ConvertRotationVectorToMagneticHeadingDegrees` symbols, not just a stub); physical hardware **never verified** (no Android device in this container — see `docs/devices-hardware-checklist.md` §7). |
| CompassReading | ✅ | Full tests; member names (`HeadingAccuracy`/`MagneticHeading`/`MagnetometerReading`/`TrueHeading`) confirmed 2026-07-02. **Fixed (2026-07-03, Task P3-2):** setters are now `private` + `friend class Compass`, matching the real API's `internal set`. |
| Gyroscope | ✅ | Real SDL3-backed (`SDL_SENSOR_GYRO`), mirrors `Accelerometer`. Full tests. Class shape confirmed 2026-07-02 (no `Calibrate` event, correctly omitted). **`getStateProperty()` tagged `NOXNA` 2026-07-02 (Task P2-17):** confirmed via the class's exact authoritative member-list page (`hh239201`) that real `Gyroscope` has no `State` property — this is a CNA symmetry extension. **Fixed (2026-07-03, `plan_devices_phase3.md` Task P3-4):** same shared-static-state mutex fix as `Accelerometer` (identical duplicated pattern in both classes). **`plan_devices_phase5.md` (2026-07-04):** same re-audit and fixes as `Accelerometer`'s row above (Tasks P5-1/P5-2/P5-3/P5-4/P5-7) — the two classes' implementations were identical, near-copy-pasted, so every finding and fix applied to both. **`plan_devices_phase6.md` (2026-07-04):** same re-audit and fixes as `Accelerometer`'s row above (Tasks P6-1 through P6-4, P6-7) — identical implementation, identical bugs, identical fixes, including the concurrent-construction SDL heap-corruption bug found by stress-testing P6-1's own new test. **`plan_devices_phase7.md` (2026-07-04):** same re-audit and fixes as `Accelerometer`'s row above (Tasks P7-1 through P7-4) — identical implementation, identical bugs, identical fixes, including the cross-class SDL mutex gap (P7-1), the premature-`disposed_` dispose race (P7-2), and the same-batch-dispatch use-after-free confirmed via a 5/5 segfault reproduction (P7-3). **`plan_devices_phase8.md` (2026-07-04):** same `dispatchToken_`/lock-proof-parameter fixes as `Accelerometer`'s row above (Tasks P8-1/P8-3) — **but unlike `Accelerometer`, this class is fully safe against self-destruction from its own `CurrentValueChanged` handler with the Task P8-1 token fix alone**: `Gyroscope::DispatchSensorReading()` has no second (`ReadingChanged`-equivalent) event and raises `CurrentValueChanged` as its last statement, so nothing touches `this` again afterward — confirmed directly with a dedicated regression test (`SelfDestroyingFromOwnCallbackDuringInjectSyntheticSensorUpdateDoesNotUseAfterFree`/`...DuringBatchDispatchDoesNotUseAfterFree`), not just inferred from the class shape. |
| GyroscopeReading | ✅ | Full tests. **Fixed (2026-07-03, Task P3-2):** setters are now `private` + `friend class Gyroscope`, matching the real API's `internal set`. **Fixed (2026-07-03, `plan_devices_phase4.md` Task P4-7):** `Timestamp` is now real wall-clock time — same fix and rationale as `AccelerometerReading`, above. |
| ISensorReading | ✅ | Complete. `Timestamp` member unverified against a direct doc page (inferred from cross-class consistency + tutorial usage, medium confidence) — not treated as a gap. |
| Motion | ✅ | **Real on Android (2026-07-05, `plan_devices.md` Phase 8, Tasks DEVICES-0101-0119):** `Detail::AndroidMotionBackend` (NDK-native, no JNI) uses `ASENSOR_TYPE_ROTATION_VECTOR` (falls back to `ASENSOR_TYPE_GAME_ROTATION_VECTOR` if the plain rotation vector is unavailable — see the drift-difference note below) for `Attitude`, `ASENSOR_TYPE_GRAVITY`/`ASENSOR_TYPE_LINEAR_ACCELERATION`/`ASENSOR_TYPE_GYROSCOPE` (Android's own already-split virtual sensors, no manual filtering) for `Gravity`/`DeviceAcceleration`/`DeviceRotationRate`. **Does not require constructing a live `Accelerometer`/`Compass`/`Gyroscope` instance** — corrected an inaccurate `// TODO` comment that had suggested otherwise; Android's own sensor fusion happens entirely inside the OS (Task DEVICES-0114). Selected via a compile-time `#if defined(__ANDROID__)` switch in `Motion.cpp`; every other platform keeps the exact pre-existing stub behavior unchanged. Class shape (`IsSupported`, `Calibrate` shared with `Compass`) confirmed 2026-07-02 unchanged — `Motion.Calibrate` is never raised by any backend (`Detail::IMotionBackend` has no calibration callback at all, unlike `ICompassBackend`). `getStateProperty()` tagged `NOXNA` (Task P2-17, `hh239189`). Quaternion→matrix→yaw/pitch/roll math (`Detail::ExtractYawPitchRollFromQuaternion()`) is derived from, and round-trip-tested against, CNA's own already-tested `Quaternion::CreateFromYawPitchRoll()`/`Matrix::CreateFromQuaternion()` — internally consistent by construction, but the raw-quaternion-to-XNA-Quaternion axis mapping itself (`ConvertRotationVectorToXnaQuaternion()`, currently a direct passthrough) is **never checked against real hardware**, same standing caveat as `Compass`'s heading math. Full tests, including 5 new fake-backend delegation tests (`MotionTests.cpp`) and 9 new pure-math tests (`AndroidMotionMathTests.cpp`). **Layered status:** API surface complete; SDL/native runtime real on Android, honest `NotSupported` stub everywhere else; Android-compile clean (`llvm-nm`-confirmed real symbols); physical hardware **never verified** (no Android device in this container — see `docs/devices-hardware-checklist.md` §8). |
| MotionReading | ✅ | Full tests; confirmed 2026-07-02 that real member names are `DeviceAcceleration`/`DeviceRotationRate` (not plain `Acceleration`/`RotationRate` as originally guessed) — CNA's existing naming already matches. **Fixed (2026-07-03, Task P3-2):** setters are now `private` + `friend class Motion`, matching the real API's `internal set`. |
| SensorBase\<T\> | ✅ | Complete. Confirmed 2026-07-02: base has only `CurrentValue`/`IsDataValid`/`TimeBetweenUpdates`/`Dispose`/`Start`/`Stop`/`CurrentValueChanged` — no `IsSupported`/`State` on the base (those are per-subclass statics), matching CNA. **Fixed (2026-07-03, `plan_devices_phase3.md` Task P3-1):** `getCurrentValueProperty()` now throws `System::InvalidOperationException` when the owning sensor is unsupported, matching the documented behavior (MSDN `hh239261`); previously it silently returned a default-constructed reading regardless of support. Implemented via a new `protected bool isSupported_` flag set once by each of the 4 derived constructors from their own `getIsSupportedProperty()` result. **Fixed (2026-07-04, `plan_devices_phase5.md` Task P5-2):** `currentValue_`/`isDataValid_` had zero synchronization despite `setCurrentValueProperty()` being called from the SDL sensor callback thread (for `Accelerometer`/`Gyroscope`) while `getCurrentValueProperty()`/`getIsDataValidProperty()` are callable from the game thread at any time — a real, previously-undetected data race, found only once this file's own "Complete"/"Fixed" claims were re-audited against the actual code rather than trusted. Added a private mutex, never held across `CurrentValueChanged.Raise()`. `getCurrentValueProperty()` now returns `TSensorReading` by value instead of `const TSensorReading&` (confirmed no existing call site bound to a reference; also more faithful to the real WP7 API's C# struct/value-type semantics). The shared `eventArgs_` member was removed entirely, replaced by a per-call local temporary, removing its own race without needing a lock around it at all. **`plan_devices_phase5.md`'s own "now correctly thread-safe" claim did not fully hold up under `plan_devices_phase6.md`'s (2026-07-04) re-audit:** `disposed_`/`isSupported_` were written unlocked despite being read under lock (or from another thread) elsewhere — Task P6-3 made both consistently guarded by the same mutex, and added `ClaimDisposalOnce()` to prevent two threads calling `Dispose()` on the same instance from both running derived cleanup logic once each (e.g. double-decrementing a shared instance counter). Task P6-5 added the first-ever tests for `TimeBetweenUpdates`'s default value and change-notification behavior (previously correct but completely untested), via a new `tests/Microsoft/Devices/Sensors/SensorBaseTests.cpp` with a minimal derived test fixture (`TimeBetweenUpdatesChanged` is `protected`, matching the real API, so a plain `TEST()` function cannot subscribe to it directly). **`plan_devices_phase7.md` (2026-07-04) Task P7-2 found `ClaimDisposalOnce()`'s own contract still had a gap:** a losing caller (one that received `false` back) previously fell through to calling the base `Dispose(bool)` itself immediately, flipping `disposed_` true while the winning caller's own cleanup — which may call the public `Stop()`, guarded by that same disposed-state precondition — could still be running, causing the winner's own `Stop()` call to throw `ObjectDisposedException` mid-cleanup and leak whatever state that cleanup hadn't yet released. Added `WaitForDisposalToComplete()` (a condition-variable wait on `disposed_` becoming true): the loser now waits for the winner's cleanup to actually finish instead of racing ahead; only the winner ever calls the base `Dispose(bool)`. Confirmed via a temporary revert that the old behavior reproduced the exact failure predicted. **`plan_devices_phase8.md` (2026-07-04) Task P8-2 found the one remaining unguarded field on this class:** `timeBetweenUpdates_` was read/written with no lock at all, unlike every other field, all fixed across Tasks P5-2/P6-3. `getTimeBetweenUpdatesProperty()` now returns `System::TimeSpan` by value (was `const TimeSpan&`) under lock — matching the identical precedent Task P5-2 already set for `getCurrentValueProperty()`, and not a breaking API change since the real WP7 property is itself a C# value type; `setTimeBetweenUpdatesProperty()` locks around the compare-and-write only, releasing before raising `TimeBetweenUpdatesChanged`. Verified under a throwaway **ThreadSanitizer** build (the first time this project has used TSan) — its only finding was one pre-existing, out-of-scope `sharp-runtime` race in `TimeSpan`'s own copy constructor (an unsynchronized debug `copy_count` counter), not this class's locking; a second, real but test-fixture-only race the same TSan run surfaced (in `SensorBaseTests.cpp`'s own counter, not `SensorBase<T>` itself) was fixed separately (Task P8-4). |
| SensorFailedException | ✅ | Full tests (6). **Fixed (2026-07-02, Task P2-16):** added `getErrorIdProperty()` + a `(const char*, SharpRuntime::intcs errorId)` constructor overload, matching the real API's `ErrorId` property (MSDN `hh239104`). Existing throw sites across the sensor classes still use the message-only constructor (so `ErrorId` reads `0` there) — no real WP7 error-code values are documented anywhere; `0`/unspecified is left as the honest default rather than inventing numbers. |
| SensorReadingEventArgs\<T\> | ✅ | Complete |
| SensorState (enum) | ✅ | Complete — 6 values (`NotSupported`/`Ready`/`Initializing`/`NoData`/`NoPermissions`/`Disabled`) confirmed 2026-07-02 via MonoGame cross-check (medium confidence). |
| VibrateController | ✅ | SDL3 haptic-backed (`Microsoft::Devices` namespace, not `Sensors`). **Fixed (2026-07-02, Task P2-14):** API shape corrected to match the real WP7 instance API — `getDefaultProperty()` returns a never-null singleton pointer (mirrors `Microsoft::Xna::Framework::Audio::Microphone::getDefaultProperty()`'s existing pattern in this codebase); `Start(const System::TimeSpan&)`/`Stop()` are now instance methods (`VibrateController::getDefaultProperty()->Start(...)`), previously fully static. `Start()` now throws `System::ArgumentOutOfRangeException` for `duration` outside the closed interval `[TimeSpan::Zero, TimeSpan::FromSeconds(5)]` (boundary values do not throw), replacing the previous silent clamp. **Fixed (`plan_devices_phase2.md` Task P2-8):** confirmed via the vendored SDL3 Linux haptic backend that a rumble-capable gamepad is enumerated by `SDL_GetHaptics()` independently of `GamePad::SetVibration`'s `SDL_RumbleGamepad` path; `VibrateController.cpp` now skips haptic devices whose name matches a connected joystick, so it never competes with `GamePad` for the same physical motor. **Phase 6 NOXNA extensions added (2026-07-02, Tasks P2-10–P2-13, all instance methods on the singleton per the Phase 7 ordering note):** `Start(const System::TimeSpan&, float intensity)` (clamped `[0,1]`, existing `Start(TimeSpan)` delegates to it with `1.0f`); `getIsSupportedProperty()`/`getDeviceNameProperty()` (probe-only, don't hold a device open as a side effect — share a private `AcquireHapticDeviceForProbe()` helper; empirically confirmed both report "unsupported"/`""` in this dev container, genuinely no haptic hardware); `StartLeftRight(float largeMotor, float smallMotor, const System::TimeSpan&)` via `SDL_HAPTIC_LEFTRIGHT`, gated on `SDL_GetHapticFeatures()` support, with `Stop()` now also destroying the tracked effect ID. Full tests (20, up from 10) cover the new shape, both `Start(TimeSpan)` throw boundaries, and all four Phase 6 additions. Only Task P2-9 (confirm-only, no code change) remains open in Phase 6. **Fixed (2026-07-03, `plan_devices_phase3.md` Task P3-5):** `Start()`/`Start(duration,intensity)` and `StartLeftRight()` now stop each other's SDL haptic effect before starting their own (previously independent effect slots that could run simultaneously) — `Start*` calls `DestroyLeftRightEffectIfAny()`, `StartLeftRight()` calls `SDL_StopHapticRumble()`. 3 new sequence-safety tests, 23 total (up from 20). **Fixed (2026-07-04, `plan_devices_phase4.md` Task P4-9):** `g_haptic`/`g_leftRightEffectId` gained a `std::mutex`, locked for the entire body of every public method. **Fixed (2026-07-04, `plan_devices_phase4.md` Task P4-10):** gamepad-exclusion now correlates by SDL haptic/joystick ID (`SDL_OpenHapticFromJoystick()`), not device-name string matching. **Fixed (2026-07-04, `plan_devices_phase5.md` Task P5-11):** `g_haptic` was left permanently unclosed based on an assumption (Task P4-9) about `SDL_Quit()` ordering that was never actually checked and turned out to be false (this codebase never calls `SDL_Quit()` anywhere) — added `~VibrateController()` to close it and release `SDL_INIT_HAPTIC` at normal process termination; replaced the `SDL_WasInit()` subsystem-init guard with explicit own-state tracking. 29 tests total (up from 27), including repeated-probe and repeated-Start/Stop-sequence coverage. **Re-examined (2026-07-04, `plan_devices_phase6.md` Task P6-6)** with a sharper question than Phase 5 asked (could a *host application* using CNA as a library call the umbrella `SDL_Quit()` independently of CNA's own code?): confirmed this class's per-instance `SDL_InitSubSystem()`/`SDL_QuitSubSystem()` pairing is an established, project-wide convention — `Microsoft::Xna::Framework::Graphics::GraphicsDevice` does the identical thing for `SDL_INIT_VIDEO`. The residual host-app-`SDL_Quit()` risk is therefore shared identically by `GraphicsDevice`, not unique to or fixable by `VibrateController` alone within a `Microsoft::Devices`-only scope — documented directly in the destructor's own doc comments rather than "fixed" narrowly. No behavior change; still 29 tests, all still passing. **Final resource-ownership re-audit (2026-07-04, `plan_devices_phase8.md` Task P8-6):** re-confirmed `g_haptic`/`g_subsystemHeld`/`g_leftRightEffectId`'s lifecycle correct in every live code path; found `~VibrateController()` did not reset `g_leftRightEffectId` to `-1` after closing `g_haptic`, unlike `g_haptic`/`g_subsystemHeld`, both of which are — not a reachable bug (`SDL_CloseHaptic()` already implicitly invalidates any uploaded effect, and this singleton's destructor runs once, at static destruction, with no legitimate code path calling in afterward), just a one-line defensive consistency fix. Still 29 tests, all still passing (re-verified under both the plain build and a throwaway ASan build). |

**Fixed (2026-07-02, `plan_devices_phase2.md` Task P2-3):** `Accelerometer.hpp`
declared `Dispose(bool) override` without `using
SensorBase<AccelerometerReading>::Dispose;`, hiding the inherited public
no-arg `Dispose()` (C++ name-hiding) — `accel.Dispose()` failed to compile
for any caller. The identical bug was found and fixed in `Compass`/`Gyroscope`/
`Motion` earlier in `plan_devices.md`; the same one-line fix was applied here.

**`getStateProperty()` on `Compass`/`Gyroscope`/`Motion` (resolved
2026-07-02, Task P2-17):** each class's exact authoritative "type exposes
the following members" page (`Compass` `hh220912`, `Gyroscope` `hh239201`,
`Motion` `hh239189`) was fetched directly and definitively confirms none of
the three has a `State` property — only `Accelerometer`'s page documents
one, as an `Accelerometer`-specific member (not inherited from
`SensorBase<T>`, which is confirmed to have none). `getStateProperty()` on
all three is now tagged `NOXNA` in their headers (not removed — existing
code/tests depend on it, and it's a harmless symmetry extension mirroring
`Accelerometer`'s real `State`), with a Doxygen note explaining the
deviation.

**Explicitly out of scope** (per project decision, not omissions): camera
(`PhotoCamera`, `CameraButtons`, `CameraCaptureTask`), media/photo pickers
(`PhotoChooserTask`), radio, phone-call APIs, and `Microsoft.Devices.Environment`
(device identity/type info) — none of these are sensor or vibration APIs.

**Event-thread model (documented 2026-07-04, `plan_devices_phase5.md` Task P5-5):**
`Accelerometer`/`Gyroscope`'s `CurrentValueChanged`/`ReadingChanged` are raised
**synchronously, on whatever thread calls `DispatchSensorReading()`.** For the real
SDL event path, that is whatever thread SDL itself invokes the registered
`SDL_AddEventWatch()` callback on — `third_party/SDL/include/SDL3/SDL_events.h`'s own
doc comment warns *"Be very careful of what you do in the event filter function, as
it may run in a different thread!"* — **not guaranteed to be the game's main/`Update()`
thread.** A game subscribing to either event must treat its handler as running on an
unknown thread: no touching non-thread-safe game state without its own synchronization,
same as any other cross-thread callback. `Compass`/`Motion` never raise these events at
all (permanent `NotSupported` stubs), so this only applies to `Accelerometer`/
`Gyroscope`. `VibrateController` has no comparable event.

**Considered, not implemented: a `NOXNA` main-thread dispatch queue/pump.** The
originating task asked to evaluate an opt-in queue (e.g. a
`SetMainThreadDispatchEnabled(bool)` + `PumpMainThreadEvents()` pair games could call
from `Update()` to replay dispatched readings on the calling thread instead). Decided
against adding it in this pass: it's a genuinely new feature (a buffering queue, an
opt-in flag threaded through every dispatch site, its own test suite for ordering/
overflow/thread-safety-of-the-queue-itself) for a need that has no concrete evidence
in this codebase — no test, demo, or reported issue has ever needed cross-thread event
delivery so far, and adding speculative infrastructure for a hypothetical future need
contradicts this project's own "don't design for hypothetical future requirements"
convention. If a real game surfaces a concrete need for main-thread dispatch, it should
be scoped and implemented then, informed by that actual use case, not guessed at now.
Documented instead (above), so the behavior is at least honestly known rather than
silently assumed.

**Phase 0 audit addendum (2026-07-05, `plan_devices.md` Tasks DEVICES-0001–0015):** a
fresh, from-scratch re-audit (not a re-statement of Phase 9's claims) found no
regressions or drift in any of the above — every API matrix, file inventory, and
`NOXNA` inventory still matches this section exactly. Two new, concrete findings:

1. **`Detail::AndroidVibrationBackend`/a custom JNI vibration bridge is not needed.**
   Reading `third_party/SDL/src/haptic/android/SDL_syshaptic.c` and its Java
   counterpart (`SDLControllerManager.java`'s `SDLHapticHandler`/
   `SDLHapticHandler_API26`/`SDLHapticHandler_API31`) directly confirms SDL3's Android
   haptic backend already queries `Context.VIBRATOR_SERVICE` (the phone's own built-in
   vibrator, not just connected-controller vibrators) and already implements amplitude
   control via `VibrationEffect.createOneShot()` (API 26+) / `VibratorManager` (API
   31+), including the exact `intensity == 0.0f → stop()` and `intensity*255` clamped
   to `[1,255]` mapping `VibrateController.cpp`'s own `NOXNA` intensity extension uses.
   `plan_devices.md`'s Phase 3 (native Android vibration backend) is expected to be
   skipped once its own gating task (DEVICES-0031) formally closes on this evidence —
   recorded here so this isn't lost if that task is deferred.
2. **A minor, harmless test-coverage gap:** `Accelerometer`/`Gyroscope`'s `NOXNA`
   `UnregisterStartedInstanceForTesting()` hook has zero call sites in either
   `AccelerometerTests.cpp` or `GyroscopeTests.cpp` — every other test-only hook has
   at least one. Not removed (may be reserved for a not-yet-written test); flagged for
   whoever next touches that test file.

Also worth noting: `/dev/kvm` now exists and is openable in this container (confirmed
via a direct `open()` call, not just `ls`), where every session since
`plan_devices_phase4.md` found it absent — the sole blocker for this repo's one
configured Android AVD (`Medium_Phone`). Re-verify at the time `plan_devices.md`'s
Phase 9 actually attempts the emulator; environments can and do change between
sessions, as this one just did.

**Phase 4 audit addendum (2026-07-05, `plan_devices.md` Tasks DEVICES-0051–0062):**
re-verified `SensorBase<T>`'s lifecycle/dispatch contracts directly; found and closed
two real, small test-coverage gaps (`Start()`-after-`Dispose()` was untested by name
for all 4 concrete sensors despite a stale in-source comment implying otherwise; no
direct `SensorBase<T>`-level test existed for `IsDataValid`'s default or
`getCurrentValueProperty()`'s not-yet-started-but-supported case). One new,
**not fixed here** finding: `sharp-runtime`'s `System::EventHandler<T>::Raise()`
(`sharp-runtime/include/System/EventHandler.hpp`) iterates its live `handlers_`
vector directly rather than over a snapshot, so `Add()`/`Remove()` called reentrantly
from within a handler mutate the same vector `Raise()`'s loop is still iterating —
confirmed via a new `AccelerometerTests.RemovingAnotherNotYetInvokedHandlerDuringDispatchDoesNotThrow`
test that this specific pattern (an earlier handler removing a not-yet-invoked later
one) does not currently crash, but this is observed tolerance of the current
libstdc++ `std::vector::erase()` behavior, not a guaranteed contract — a
`sharp-runtime` concern, out of this repo's scope to fix (same rule as the existing
`TimeSpan` copy-constructor race, above), not currently reachable by any production
code path in this namespace.

---

## `Microsoft::Xna::Framework::Net`

| Class | Status | Notes |
|---|---|---|
| NetworkSessionType (enum) | ✅ | Complete |
| NetworkSessionState (enum) | ✅ | Complete |
| NetworkSessionEndReason (enum) | ✅ | Complete |
| NetworkSessionJoinError (enum) | ✅ | Complete |
| SendDataOptions (enum) | ✅ | Complete; FNA marks `[Flags]` but values are sequential (0-4), not real bit flags — ported plain, no bitwise operators added |
| NetworkSessionProperties | ✅ | Full port; implements `System::Collections::Generic::IList<std::optional<int>>`. Preserves two FNA quirks faithfully: the indexer setter appends instead of extending when given an out-of-range index (FNA's own "TODO: Expand list to index size?"), and `IsReadOnly` always returns `true` despite `Add`/`Remove`/`Clear` being fully functional |
| QualityOfService | ✅ | Full port; all-defaults data class |
| AvailableNetworkSession | ✅ | Full port; `operator==`/`operator!=` added (NOXNA — required by `ReadOnlyCollection<T>`, not present in FNA; compares only scalar fields, excludes non-equatable `QualityOfService`/`NetworkSessionProperties` members) |
| AvailableNetworkSessionCollection | ✅ | Full port; `Dispose()` only flips `IsDisposed` — sharp-runtime's `ReadOnlyCollection<T>` copies into private storage with no derived-class mutator, unlike FNA's reference-wrapping `ReadOnlyCollection<T>` whose `Dispose()` empties the underlying shared list too |
| GameEndedEventArgs | ✅ | Full port |
| GameStartedEventArgs | ✅ | Full port |
| GamerJoinedEventArgs | ✅ | Full port; `NetworkGamer*` stored as forward-declared pointer (`NetworkGamer` not yet ported) |
| GamerLeftEventArgs | ✅ | Full port; `NetworkGamer*` forward-declared |
| HostChangedEventArgs | ✅ | Full port; `NetworkGamer*` forward-declared (OldHost/NewHost) |
| NetworkSessionEndedEventArgs | ✅ | Full port |
| WriteLeaderboardsEventArgs | ✅ | Full port; internal ctor → private + `CreateInternal()` factory |
| NetworkSessionJoinException | ✅ | Full port; `: GamerServices::NetworkException`, mirrors its 4-ctor + protected serialization-ctor pattern exactly |
