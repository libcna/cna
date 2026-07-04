# CNA XNA 4.0 API Audit

Systematic per-class, per-method comparison: FNA (reference) vs CNA (implementation).

**Legend:**
- ✅ Done — class fully audited, all missing methods added as stubs or implemented
- 🔄 In progress
- ⬜ Not yet audited

For intentionally excluded items see `docs/xna-4-api-coverage.md`.

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

| Class / Enum | Status | Notes |
|---|---|---|
| AudioCategory | ✅ | API complete |
| AudioChannels (enum) | ✅ | Complete |
| AudioEmitter | ✅ | API complete |
| AudioEngine | ✅ | API complete (stub behavior) |
| AudioListener | ✅ | API complete |
| AudioStopOptions (enum) | ✅ | Complete |
| Cue | ✅ | API complete (stub behavior) |
| DynamicSoundEffectInstance | ✅ | API complete |
| InstancePlayLimitException | ✅ | Complete |
| Microphone | ✅ | API complete (stub behavior) |
| MicrophoneState (enum) | ✅ | Complete |
| NoAudioHardwareException | ✅ | Complete |
| NoMicrophoneConnectedException | ✅ | Complete |
| RendererDetail | ✅ | API complete |
| SoundBank | ✅ | API complete (stub behavior) |
| SoundEffect | ✅ | Implemented (SDL_mixer) |
| SoundEffectInstance | ✅ | Implemented (SDL_mixer) |
| SoundState (enum) | ✅ | Complete |
| WaveBank | ✅ | API complete (stub behavior) |

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
| BlendState | ✅ | API complete |
| BufferUsage (enum) | ✅ | Complete |
| ClearOptions (enum) | ✅ | Complete |
| ColorWriteChannels (enum) | ✅ | Complete |
| CompareFunction (enum) | ✅ | Complete |
| CubeMapFace (enum) | ✅ | Complete |
| CullMode (enum) | ✅ | Complete |
| DepthFormat (enum) | ✅ | Complete |
| DepthStencilState | ✅ | API complete |
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
| IndexElementSize (enum) | ✅ | Complete |
| IRenderTarget | ✅ | Complete |
| IVertexType | ✅ | Complete |
| Model | ✅ | API complete |
| ModelBone | ✅ | API complete |
| ModelBoneCollection | ✅ | API complete |
| ModelEffectCollection | ✅ | API complete |
| ModelMesh | ✅ | API complete |
| ModelMeshCollection | ✅ | API complete |
| ModelMeshPart | ✅ | API complete |
| ModelMeshPartCollection | ✅ | API complete |
| NoSuitableGraphicsDeviceException | ✅ | Complete |
| OcclusionQuery | ✅ | API complete |
| PresentationParameters | ✅ | API complete |
| PresentInterval (enum) | ✅ | Complete |
| PrimitiveType (enum) | ✅ | Complete |
| RasterizerState | ✅ | API complete |
| RenderTarget2D | ✅ | API complete |
| RenderTargetBinding | ✅ | API complete |
| RenderTargetCube | ✅ | API complete |
| RenderTargetUsage (enum) | ✅ | Complete |
| ResourceCreatedEventArgs | ✅ | API complete |
| ResourceDestroyedEventArgs | ✅ | API complete |
| SamplerState | ✅ | API complete |
| SamplerStateCollection | ✅ | API complete |
| SetDataOptions (enum) | ✅ | Complete |
| SkinnedEffect | ✅ | API surface present (stub behavior) |
| SpriteBatch | ✅ | Missing Draw overloads added as stubs |
| SpriteEffect | ✅ | API surface present |
| SpriteEffects (enum) | ✅ | Complete |
| SpriteFont | ✅ | API complete |
| SpriteSortMode (enum) | ✅ | Complete |
| StencilOperation (enum) | ✅ | Complete |
| SurfaceFormat (enum) | ✅ | Complete |
| Texture | ✅ | API complete |
| Texture2D | ✅ | API complete |
| Texture3D | ✅ | API complete |
| TextureAddressMode (enum) | ✅ | Complete |
| TextureCollection | ✅ | API complete |
| TextureCube | ✅ | API complete |
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
| VertexPositionTexture | ✅ | API complete |
| Viewport | ✅ | API complete |

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

| Class / Enum | Status | Notes |
|---|---|---|
| Buttons (enum) | ✅ | Complete |
| ButtonState (enum) | ✅ | Complete |
| GamePad | ✅ | API complete |
| GamePadButtons | ✅ | API complete |
| GamePadCapabilities | ✅ | API complete |
| GamePadDeadZone (enum) | ✅ | Complete |
| GamePadDPad | ✅ | API complete |
| GamePadState | ✅ | API complete |
| GamePadThumbSticks | ✅ | API complete |
| GamePadTriggers | ✅ | API complete |
| GamePadType (enum) | ✅ | Complete |
| Keyboard | ✅ | API complete |
| KeyboardState | ✅ | API complete |
| Keys (enum) | ✅ | Complete |
| KeyState (enum) | ✅ | Complete |
| Mouse | ✅ | API complete |
| MouseState | ✅ | API complete |
| TextInputEXT | ✅ | API complete |

---

## `Microsoft::Xna::Framework::Input::Touch`

| Class / Enum | Status | Notes |
|---|---|---|
| GestureSample | ✅ | API complete |
| GestureType (enum) | ✅ | Complete |
| TouchCollection | ✅ | API complete |
| TouchLocation | ✅ | API complete |
| TouchLocationState (enum) | ✅ | Complete |
| TouchPanel | ✅ | API complete (stub behavior) |
| TouchPanelCapabilities | ✅ | API complete |

---

## `Microsoft::Xna::Framework::Media`

| Class / Enum | Status | Notes |
|---|---|---|
| Album | ✅ | API complete (stub behavior) |
| AlbumCollection | ✅ | API complete |
| Artist | ✅ | API complete (stub behavior) |
| ArtistCollection | ✅ | API complete |
| Genre | ✅ | API complete (stub behavior) |
| GenreCollection | ✅ | API complete |
| MediaLibrary | ✅ | API complete (stub behavior) |
| MediaPlayer | ✅ | Implemented (SDL_mixer) |
| MediaQueue | ✅ | API complete |
| MediaSource | ✅ | API complete |
| MediaSourceType (enum) | ✅ | Complete |
| MediaState (enum) | ✅ | Complete |
| Picture | ✅ | API complete (stub behavior) |
| PictureAlbum | ✅ | API complete (stub behavior) |
| PictureAlbumCollection | ✅ | API complete |
| PictureCollection | ✅ | API complete |
| Playlist | ✅ | API complete (stub behavior) |
| PlaylistCollection | ✅ | API complete |
| Song | ✅ | API complete |
| SongCollection | ✅ | API complete |
| Video | ✅ | API complete |
| VideoPlayer | ✅ | Implemented (FFmpeg) |
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
| Guide | ✅ | Stub |
| GamerServicesComponent | ✅ | Stub added |
| GamerServicesNotAvailableException | ✅ | Stub added |

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

| Class / Enum | Status | Notes |
|---|---|---|
| Accelerometer | ✅ | Real SDL3-backed (`SDL_SENSOR_ACCEL`); Android landscape axis remap. Class shape (`IsSupported`, `State`) confirmed 2026-07-02 against MSDN `ff707930`. `Dispose()` name-hiding bug fixed and full `AccelerometerTests.cpp` (9 tests) added 2026-07-02 (Task P2-3); `GetTypeNameCPP` corrected to the dot-separated convention 2026-07-02 (Task P2-4); legacy `ReadingChanged` event wired up 2026-07-02 (Task P2-15). **Fixed (2026-07-03, `plan_devices_phase3.md` Task P3-4):** shared static sensor state (`g_sensor_`/`g_sensorId_`/`eventWatchRegistered_`/`startedInstances_`) is now guarded by a `static std::mutex` against the SDL event-watch callback running off-thread (SDL's own `SDL_AddEventWatch()` doc warns it may). **`plan_devices_phase5.md` (2026-07-04) re-audited this class specifically because Phase 4's own fixes turned out to be incomplete:** Task P5-1 fixed a subsystem ref-count leak Task P4-8 itself introduced; Task P5-2 replaced the single-bool callback-quiescence flag (which could under-count concurrent dispatches) with a thread-id vector; Task P5-3 removed the self-dispose-from-own-callback deadlock Task P4-2 had explicitly accepted as permanent; Task P5-4 moved the shared subsystem/event-watch machinery into `Detail::SdlSensorSubsystem<Accelerometer>`, verified byte-for-byte behavior-preserving; Task P5-7 made the Android axis-remap sign math a unit-tested pure function. See that plan's "Audit findings" section for the full root-cause writeups — none of this was found by re-reading this row's own prior claims at face value. **`plan_devices_phase6.md` (2026-07-04) re-audited again, on the same "do not trust Phase 5's own claims" premise, and found Phase 5 had itself left real gaps:** Task P6-1 fixed an unlocked `instanceCount_` check+increment in the constructor (raced against `Dispose()`'s locked decrement) — its own new concurrency test then surfaced a *second*, more serious bug found only by looping the test many times, not a single run: `getIsSupportedProperty()` made real SDL calls with no synchronization, violating SDL3's own documented "`SDL_InitSubSystem()` should only be called on the main thread"/"`SDL_QuitSubSystem()` is not thread safe" contract, reproducibly corrupting the heap under concurrent construction (fixed by locking `subsystem.mutex_` for the whole call). Task P6-2 fixed a subsystem-hold leak on a failed `Start()`. Task P6-3 made `started_`/`state_`/`subsystemHeld_` consistently locked (previously read unlocked in several places) and added `SensorBase::ClaimDisposalOnce()` to prevent a double-dispose race. Task P6-4 added an RAII exception-safety guard so a throwing `CurrentValueChanged`/`ReadingChanged` handler can no longer permanently corrupt dispatch-tracking state and deadlock a future `Dispose()`. Task P6-7 added semantic (tilt-left, face-up/down) axis tests. See `plan_devices_phase6.md`'s "Audit findings" section for full root-cause detail. **`plan_devices_phase7.md` (2026-07-04) found Phase 6's own fixes still had two real gaps in this class:** Task P7-1 found `getIsSupportedProperty()`'s per-class `subsystem.mutex_` (P6-1's addendum) only serialized this class's own SDL calls against itself, not against `Gyroscope`'s identical calls on a *different* mutex — added a process-wide `Detail::GetGlobalSdlSensorMutex()`, verified with a new cross-class stress test (40/40 clean). Task P7-2 found a losing concurrent `Dispose()` call could flip `disposed_` true while the winner's own `Stop()` call was still relying on it being false, causing that `Stop()` call to throw mid-cleanup and leak state — fixed with `SensorBase::WaitForDisposalToComplete()`, confirmed via a temporary revert that reproduced the exact failure. Task P7-3 found and fixed **the most serious bug of this phase**: `SensorEventWatch()`'s dispatch loop could be left holding a dangling pointer if one instance's callback disposed a *different*, not-yet-dispatched instance from the same batch — confirmed as a real, reliably reproducible (5/5) segfault via a deliberate temporary revert, not a theoretical concern. Task P7-4 fixed the one remaining unguarded test-only getter (`GetSubsystemHeldForTesting()`). **`plan_devices_phase8.md` (2026-07-04) found Phase 7's own dispatch-loop fix (Task P7-3) had not covered every use-after-free path:** Task P8-1 found and fixed **the most significant bug of this phase**: a callback destroying (not just `Dispose()`-ing) its own sensor object mid-dispatch could still leave `DispatchToInstances()`'s/`InjectSyntheticSensorUpdate()`'s cleanup guards touching freed memory (they captured the raw instance/`this` and dereferenced it after the callback returned). Fixed by replacing the plain per-instance `dispatchingThreadIds_` vector with `dispatchToken_`, a `std::shared_ptr<std::vector<std::thread::id>>` copied into the cleanup guard *before* invoking the callback. Confirmed via a throwaway ASan build — not a plain run, which did not reproduce the bug at all across repeated attempts — that the reverted code produces a definitive `heap-use-after-free` and the fixed code produces none. **Explicitly documented, not fixed, one remaining boundary specific to this class**: destroying `Accelerometer` from within its own `CurrentValueChanged` handler stays unsafe, because `DispatchSensorReading()` unconditionally calls `getIsDataValidProperty()` again afterward (to decide whether to also raise the legacy `ReadingChanged` event) regardless of whether `ReadingChanged` even has a subscriber — a class-design property (`ReadingChanged` is itself a member of `this`), not a dispatch-bookkeeping gap the token can close; fixing it would require redesigning where the event objects live relative to instance identity, out of scope for this task. Task P8-3 made `getIsSupportedProperty()`'s `ProbeIsSupported()` call (and `Start()`'s `EnsureSubsystemInitialized()`/`OpenDefaultSensorLocked()` calls) require a compile-time lock-proof parameter instead of relying on a doc comment alone to remember the global SDL sensor mutex — verified the guard actually rejects a lock-free call via a throwaway scratch compile. |
| AccelerometerFailedException | ✅ | Full tests (7). **Fixed (2026-07-02, Task P2-16):** gained `ErrorId` via the same `(const char*, SharpRuntime::intcs)` constructor overload added to `SensorFailedException`. Confirmed it does inherit `SensorFailedException` (visible directly in its own `.hpp`; the earlier "unverified" note was overly cautious). |
| AccelerometerReading | ✅ | Full tests. **Fixed (2026-07-03, `plan_devices_phase3.md` Task P3-2):** setters are now `private` + `friend class Accelerometer`, matching the real API's `internal set` (previously fully public, a confirmed deviation). **Fixed (2026-07-03, `plan_devices_phase4.md` Task P4-7):** `Timestamp` is now real wall-clock time (`System::DateTimeOffset::getUtcNowProperty()`); previously derived from `SDL_GetTicksNS()` (monotonic ns since SDL init) fed into a `DateTime(ticks)` constructor expecting .NET-epoch ticks — always produced a value near `0001-01-01`, never the actual reading time. |
| AccelerometerReadingEventArgs | ✅ | WP7 7.0 legacy; class itself is correct and fully tested. **Fixed (2026-07-02, Task P2-15):** `Accelerometer.ReadingChanged` (the real, `[Obsolete]`-tagged event using this type) is now wired up, raised alongside `CurrentValueChanged` from `ProcessSensorUpdateEvent()`. |
| AttitudeReading | ✅ | Full tests; member names (`Pitch`/`Roll`/`Yaw`/`Quaternion`/`RotationMatrix`) confirmed 2026-07-02. **Fixed (2026-07-03, Task P3-2):** setters are now `private` + `friend class Motion` (the class that produces `AttitudeReading` values, as `MotionReading.Attitude`), matching the real API's `internal set`. |
| CalibrationEventArgs | ✅ | Full tests. **Confirmed (2026-07-03, `plan_devices_phase3.md` Task P3-12):** the real class page (MSDN `hh220788`, vs.110) shows exactly one constructor (parameterless) and no class-specific properties/methods beyond what `System.Object` provides — CNA's empty-marker-class implementation matches exactly. |
| Compass | ✅ | Stub — SDL3 exposes no magnetometer API on any platform; always `NotSupported`. Full tests. Class shape (`IsSupported`, `Calibrate`, no legacy event) confirmed 2026-07-02. **`getStateProperty()` tagged `NOXNA` 2026-07-02 (Task P2-17):** confirmed via the class's exact authoritative member-list page (`hh220912`) that real `Compass` has no `State` property — this is a CNA symmetry extension. **`plan_devices_phase6.md` (2026-07-04):** re-confirmed still an honest stub (Task P6-8, no fake data synthesis); Task P6-1 also fixed an unlocked `instanceCount_` (same class of bug as `Accelerometer`/`Gyroscope`, though without their SDL-concurrency complication — `getIsSupportedProperty()` here is a hardcoded `return false;` with no SDL calls, confirmed not affected). Task P6-8 sketched (documentation only, not implemented) an `ICompassBackend` interface shape for a future native Android/iOS backend. **`plan_devices_phase7.md` (2026-07-04) Task P7-2:** applied the same `ClaimDisposalOnce()`/`WaitForDisposalToComplete()` restructuring as `Accelerometer`/`Gyroscope` for consistency, though the underlying race is currently latent here (not observable) since `Start()` always throws before ever setting `started_` true, so `Dispose(bool)`'s `Stop()` call is dead code today — fixed anyway so the same bug can't resurface if this class ever gains a real backend. |
| CompassReading | ✅ | Full tests; member names (`HeadingAccuracy`/`MagneticHeading`/`MagnetometerReading`/`TrueHeading`) confirmed 2026-07-02. **Fixed (2026-07-03, Task P3-2):** setters are now `private` + `friend class Compass`, matching the real API's `internal set`. |
| Gyroscope | ✅ | Real SDL3-backed (`SDL_SENSOR_GYRO`), mirrors `Accelerometer`. Full tests. Class shape confirmed 2026-07-02 (no `Calibrate` event, correctly omitted). **`getStateProperty()` tagged `NOXNA` 2026-07-02 (Task P2-17):** confirmed via the class's exact authoritative member-list page (`hh239201`) that real `Gyroscope` has no `State` property — this is a CNA symmetry extension. **Fixed (2026-07-03, `plan_devices_phase3.md` Task P3-4):** same shared-static-state mutex fix as `Accelerometer` (identical duplicated pattern in both classes). **`plan_devices_phase5.md` (2026-07-04):** same re-audit and fixes as `Accelerometer`'s row above (Tasks P5-1/P5-2/P5-3/P5-4/P5-7) — the two classes' implementations were identical, near-copy-pasted, so every finding and fix applied to both. **`plan_devices_phase6.md` (2026-07-04):** same re-audit and fixes as `Accelerometer`'s row above (Tasks P6-1 through P6-4, P6-7) — identical implementation, identical bugs, identical fixes, including the concurrent-construction SDL heap-corruption bug found by stress-testing P6-1's own new test. **`plan_devices_phase7.md` (2026-07-04):** same re-audit and fixes as `Accelerometer`'s row above (Tasks P7-1 through P7-4) — identical implementation, identical bugs, identical fixes, including the cross-class SDL mutex gap (P7-1), the premature-`disposed_` dispose race (P7-2), and the same-batch-dispatch use-after-free confirmed via a 5/5 segfault reproduction (P7-3). **`plan_devices_phase8.md` (2026-07-04):** same `dispatchToken_`/lock-proof-parameter fixes as `Accelerometer`'s row above (Tasks P8-1/P8-3) — **but unlike `Accelerometer`, this class is fully safe against self-destruction from its own `CurrentValueChanged` handler with the Task P8-1 token fix alone**: `Gyroscope::DispatchSensorReading()` has no second (`ReadingChanged`-equivalent) event and raises `CurrentValueChanged` as its last statement, so nothing touches `this` again afterward — confirmed directly with a dedicated regression test (`SelfDestroyingFromOwnCallbackDuringInjectSyntheticSensorUpdateDoesNotUseAfterFree`/`...DuringBatchDispatchDoesNotUseAfterFree`), not just inferred from the class shape. |
| GyroscopeReading | ✅ | Full tests. **Fixed (2026-07-03, Task P3-2):** setters are now `private` + `friend class Gyroscope`, matching the real API's `internal set`. **Fixed (2026-07-03, `plan_devices_phase4.md` Task P4-7):** `Timestamp` is now real wall-clock time — same fix and rationale as `AccelerometerReading`, above. |
| ISensorReading | ✅ | Complete. `Timestamp` member unverified against a direct doc page (inferred from cross-class consistency + tutorial usage, medium confidence) — not treated as a gap. |
| Motion | ✅ | Stub — requires Compass (unsupported); always `NotSupported`. `// TODO` marks where sensor fusion should be wired once compass support exists. Full tests. Class shape (`IsSupported`, `Calibrate` shared with `Compass`) confirmed 2026-07-02. **`getStateProperty()` tagged `NOXNA` 2026-07-02 (Task P2-17):** confirmed via the class's exact authoritative member-list page (`hh239189`) that real `Motion` has no `State` property — this is a CNA symmetry extension. **`plan_devices_phase6.md` (2026-07-04):** same re-confirmation and fixes as `Compass`'s row above (Tasks P6-1/P6-8), plus an `IMotionBackend` interface sketch (documentation only). **`plan_devices_phase7.md` (2026-07-04) Task P7-2:** same restructuring and same "currently latent, fixed for consistency" reasoning as `Compass`'s row above. |
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
