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
| TextInputEXT | ✅ | Real | Wired to `SDL_EVENT_TEXT_INPUT`/`SDL_EVENT_TEXT_EDITING`, `SDL_StartTextInput`/`StopTextInput`, `SDL_SetTextInputArea` (Phase I1, tasks 700–708). Known deviation: `TextInput` callback is `char`-based, so text is forwarded per UTF-8 byte rather than per UTF-16 code unit like FNA (documented in-source). |

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
