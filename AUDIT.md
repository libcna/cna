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
