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
| SpriteBatch | ✅ | Missing Draw overloads added as stubs; `SamplerState`/`TextureAddressMode` now actually applied on EasyGL (Task 269) — see "NPOT textures and SpriteBatch edge sampling" below. Still a no-op on Vulkan/Bgfx. |
| SpriteEffect | ✅ | API surface present |
| SpriteEffects (enum) | ✅ | Complete |
| SpriteFont | ✅ | API complete |
| SpriteSortMode (enum) | ✅ | Complete |
| StencilOperation (enum) | ✅ | Complete |
| SurfaceFormat (enum) | ✅ | Complete |
| Texture | ✅ | API complete |
| Texture2D | 🔄 | Detailed re-audit (Task 261, Phase 32); 2 memory-safety bugs fixed (Task 266); missing `FromStream(w,h,zoom)` overload added + format support verified (Task 262); `SaveAsPng`/`SaveAsJpeg` round-trip verified + JPEG quality fixed (Tasks 263–264); missing `NOXNA` tags fixed. Still open: missing `SetDataPointerEXT`/`GetDataPointerEXT`/`TextureDataFromStreamEXT`/`DDSFromStreamEXT`, and Color-only format support — see below |
| Texture3D | ✅ | Detailed audit (Task 271, Phase 33): fixed `LevelCount` hardcoded to 1 (ignored `mipMap`), fixed missing null/count/startIndex/box-bounds guards on `SetData`/`GetData` (crash + OOB read/write risks), fixed missing `Dispose(bool)` override (GPU resource leak on explicit Dispose). See below. |
| TextureAddressMode (enum) | ✅ | Complete |
| TextureCollection | ✅ | API complete |
| TextureCube | ✅ | Detailed audit (Task 272, Phase 33): fixed the same 3 bug classes as `Texture3D` (hardcoded `LevelCount`, missing `SetData`/`GetData` guards, missing `Dispose(bool)`), plus 2 `TextureCube`-specific findings: a missing `SetData`/`GetData(face,data,startIndex,elementCount)` overload (added), and a `rect==nullptr`-at-`level>0` bug that ignored mip-level dimensions entirely (fixed). `DDSFromStreamEXT` confirmed to be a non-functional stub — documented, not fixed, recommended as an urgent follow-up. See below. |
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
   rather than loudly) — not yet given its own `GRAPHICS_TASKS.md` number; add one before Phase 33
   is considered complete.

#### Confirmed limitations (documented, not fixed, matching established precedent)

- **`CubeMapFace` values are never validated.** `static_cast<int>(face)` is passed straight to the
  backend with no range check, for both the pre-existing and newly-added overloads. This is already
  explicitly tracked as its own task: `GRAPHICS_TASKS.md` Task 279, "Add validation for invalid
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

**Not fixed in this task, flagged as a follow-up (`GRAPHICS_TASKS.md` Task 862):**
`EasyGLTexture3DBackend`'s constructor has the identical single-level-only pattern (only level 0
allocated via `set_image_3d`, `SetData` writes via `set_sub_image_3d`), so `Texture3D::SetData` at
`level>0` on a mipmapped volume almost certainly has the same silent-failure bug. Task 271's audit
already documented that EasyGL ignores `Texture3D`'s `mipMap` parameter in general, but did not
specifically reproduce a level>0 `SetData` failure with a test — this session's finding gives that
documented limitation a concrete, fixable root cause and a matching fix shape (mirror this task's
constructor change), left for a follow-up task since it's outside Task 276's `TextureCube` scope.

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
