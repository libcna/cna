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
| Accelerometer | ✅ | Real SDL3-backed (`SDL_SENSOR_ACCEL`); Android landscape axis remap. Class shape (`IsSupported`, `State`) confirmed 2026-07-02 against MSDN `ff707930`. `Dispose()` name-hiding bug fixed and full `AccelerometerTests.cpp` (9 tests) added 2026-07-02 (Task P2-3); `GetTypeNameCPP` corrected to the dot-separated convention 2026-07-02 (Task P2-4); legacy `ReadingChanged` event wired up 2026-07-02 (Task P2-15). **Fixed (2026-07-03, `plan_devices_phase3.md` Task P3-4):** shared static sensor state (`g_sensor_`/`g_sensorId_`/`eventWatchRegistered_`/`startedInstances_`) is now guarded by a `static std::mutex` against the SDL event-watch callback running off-thread (SDL's own `SDL_AddEventWatch()` doc warns it may). |
| AccelerometerFailedException | ✅ | Full tests (7). **Fixed (2026-07-02, Task P2-16):** gained `ErrorId` via the same `(const char*, SharpRuntime::intcs)` constructor overload added to `SensorFailedException`. Confirmed it does inherit `SensorFailedException` (visible directly in its own `.hpp`; the earlier "unverified" note was overly cautious). |
| AccelerometerReading | ✅ | Full tests |
| AccelerometerReadingEventArgs | ✅ | WP7 7.0 legacy; class itself is correct and fully tested. **Fixed (2026-07-02, Task P2-15):** `Accelerometer.ReadingChanged` (the real, `[Obsolete]`-tagged event using this type) is now wired up, raised alongside `CurrentValueChanged` from `ProcessSensorUpdateEvent()`. |
| AttitudeReading | ✅ | Full tests; member names (`Pitch`/`Roll`/`Yaw`/`Quaternion`/`RotationMatrix`) confirmed 2026-07-02. |
| CalibrationEventArgs | ✅ | Full tests |
| Compass | ✅ | Stub — SDL3 exposes no magnetometer API on any platform; always `NotSupported`. Full tests. Class shape (`IsSupported`, `Calibrate`, no legacy event) confirmed 2026-07-02. **`getStateProperty()` tagged `NOXNA` 2026-07-02 (Task P2-17):** confirmed via the class's exact authoritative member-list page (`hh220912`) that real `Compass` has no `State` property — this is a CNA symmetry extension. |
| CompassReading | ✅ | Full tests; member names (`HeadingAccuracy`/`MagneticHeading`/`MagnetometerReading`/`TrueHeading`) confirmed 2026-07-02. |
| Gyroscope | ✅ | Real SDL3-backed (`SDL_SENSOR_GYRO`), mirrors `Accelerometer`. Full tests. Class shape confirmed 2026-07-02 (no `Calibrate` event, correctly omitted). **`getStateProperty()` tagged `NOXNA` 2026-07-02 (Task P2-17):** confirmed via the class's exact authoritative member-list page (`hh239201`) that real `Gyroscope` has no `State` property — this is a CNA symmetry extension. **Fixed (2026-07-03, `plan_devices_phase3.md` Task P3-4):** same shared-static-state mutex fix as `Accelerometer` (identical duplicated pattern in both classes). |
| GyroscopeReading | ✅ | Full tests |
| ISensorReading | ✅ | Complete. `Timestamp` member unverified against a direct doc page (inferred from cross-class consistency + tutorial usage, medium confidence) — not treated as a gap. |
| Motion | ✅ | Stub — requires Compass (unsupported); always `NotSupported`. `// TODO` marks where sensor fusion should be wired once compass support exists. Full tests. Class shape (`IsSupported`, `Calibrate` shared with `Compass`) confirmed 2026-07-02. **`getStateProperty()` tagged `NOXNA` 2026-07-02 (Task P2-17):** confirmed via the class's exact authoritative member-list page (`hh239189`) that real `Motion` has no `State` property — this is a CNA symmetry extension. |
| MotionReading | ✅ | Full tests; confirmed 2026-07-02 that real member names are `DeviceAcceleration`/`DeviceRotationRate` (not plain `Acceleration`/`RotationRate` as originally guessed) — CNA's existing naming already matches. |
| SensorBase\<T\> | ✅ | Complete. Confirmed 2026-07-02: base has only `CurrentValue`/`IsDataValid`/`TimeBetweenUpdates`/`Dispose`/`Start`/`Stop`/`CurrentValueChanged` — no `IsSupported`/`State` on the base (those are per-subclass statics), matching CNA. **Fixed (2026-07-03, `plan_devices_phase3.md` Task P3-1):** `getCurrentValueProperty()` now throws `System::InvalidOperationException` when the owning sensor is unsupported, matching the documented behavior (MSDN `hh239261`); previously it silently returned a default-constructed reading regardless of support. Implemented via a new `protected bool isSupported_` flag set once by each of the 4 derived constructors from their own `getIsSupportedProperty()` result. |
| SensorFailedException | ✅ | Full tests (6). **Fixed (2026-07-02, Task P2-16):** added `getErrorIdProperty()` + a `(const char*, SharpRuntime::intcs errorId)` constructor overload, matching the real API's `ErrorId` property (MSDN `hh239104`). Existing throw sites across the sensor classes still use the message-only constructor (so `ErrorId` reads `0` there) — no real WP7 error-code values are documented anywhere; `0`/unspecified is left as the honest default rather than inventing numbers. |
| SensorReadingEventArgs\<T\> | ✅ | Complete |
| SensorState (enum) | ✅ | Complete — 6 values (`NotSupported`/`Ready`/`Initializing`/`NoData`/`NoPermissions`/`Disabled`) confirmed 2026-07-02 via MonoGame cross-check (medium confidence). |
| VibrateController | ✅ | SDL3 haptic-backed (`Microsoft::Devices` namespace, not `Sensors`). **Fixed (2026-07-02, Task P2-14):** API shape corrected to match the real WP7 instance API — `getDefaultProperty()` returns a never-null singleton pointer (mirrors `Microsoft::Xna::Framework::Audio::Microphone::getDefaultProperty()`'s existing pattern in this codebase); `Start(const System::TimeSpan&)`/`Stop()` are now instance methods (`VibrateController::getDefaultProperty()->Start(...)`), previously fully static. `Start()` now throws `System::ArgumentOutOfRangeException` for `duration` outside the closed interval `[TimeSpan::Zero, TimeSpan::FromSeconds(5)]` (boundary values do not throw), replacing the previous silent clamp. **Fixed (`plan_devices_phase2.md` Task P2-8):** confirmed via the vendored SDL3 Linux haptic backend that a rumble-capable gamepad is enumerated by `SDL_GetHaptics()` independently of `GamePad::SetVibration`'s `SDL_RumbleGamepad` path; `VibrateController.cpp` now skips haptic devices whose name matches a connected joystick, so it never competes with `GamePad` for the same physical motor. **Phase 6 NOXNA extensions added (2026-07-02, Tasks P2-10–P2-13, all instance methods on the singleton per the Phase 7 ordering note):** `Start(const System::TimeSpan&, float intensity)` (clamped `[0,1]`, existing `Start(TimeSpan)` delegates to it with `1.0f`); `getIsSupportedProperty()`/`getDeviceNameProperty()` (probe-only, don't hold a device open as a side effect — share a private `AcquireHapticDeviceForProbe()` helper; empirically confirmed both report "unsupported"/`""` in this dev container, genuinely no haptic hardware); `StartLeftRight(float largeMotor, float smallMotor, const System::TimeSpan&)` via `SDL_HAPTIC_LEFTRIGHT`, gated on `SDL_GetHapticFeatures()` support, with `Stop()` now also destroying the tracked effect ID. Full tests (20, up from 10) cover the new shape, both `Start(TimeSpan)` throw boundaries, and all four Phase 6 additions. Only Task P2-9 (confirm-only, no code change) remains open in Phase 6. |

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
