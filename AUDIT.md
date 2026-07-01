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
| Texture2D | 🔄 | Detailed re-audit (Task 261, Phase 32); 2 memory-safety bugs fixed (Task 266); missing `FromStream(w,h,zoom)` overload added + format support verified (Task 262). Still open: missing `NOXNA` tags, missing `SetDataPointerEXT`/`GetDataPointerEXT`/`TextureDataFromStreamEXT`/`DDSFromStreamEXT`, and Color-only format support — see below |
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

#### Missing `NOXNA` tags (CLAUDE.md compliance)

8. `Texture2D(const std::string& assetName)` and
   `Texture2D(const std::string& assetName, GraphicsDevice& graphicsDevice)`
   (`Texture2D.hpp:40,46`) are **not part of the FNA/XNA 4.0 `Texture2D` API** — real XNA loads
   textures via `Texture2D.FromStream` or the content pipeline, never a direct filename constructor.
   These are CNA-only conveniences and per CLAUDE.md must be wrapped in `NOXNA`, exactly like the
   project's own established precedent: `SoundEffect(const std::string& assetName)`
   (`Audio/SoundEffect.hpp:49`) **is** correctly marked `NOXNA explicit`. The two `Texture2D`
   constructors are missing this marker — a straightforward, mechanical fix.

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
11. `SaveAsJpeg` hardcodes JPEG quality to 100 (`IMG_SaveJPG_IO(..., 100)`), ignoring FNA's
    `FNA_GRAPHICS_JPEG_SAVE_QUALITY` environment-variable override. Likely an acceptable
    simplification, but noted since Phase 32 Task 264 asks to verify `SaveAsJpeg`.

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
