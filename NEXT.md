# NEXT.md — CNA project handoff

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model, built on SDL3 with a
pluggable graphics backend layer. It is a framework/runtime, not a game. The goal is to
preserve the `Microsoft::Xna::Framework` public API exactly as defined by FNA while using
modern C++ internals.

**Authoritative API reference:** `/rv/data/library/github.com/FNA-XNA/FNA/src`

**Current phase:** Core graphics API is largely complete. The next major gaps are texture
I/O (`Texture2D::FromStream`/`GetData`), SpriteBatch sort modes, and RenderTarget2D backend
wiring.

**Key architectural decisions:**
- Backend selected at compile-time via `CNA_GRAPHICS_BACKEND` CMake option (`SDL_RENDERER` |
  `EASYGL` | `VULKAN` | `BGFX`). The default debug build dir is `cmake-build-vulkan/`.
- `EasyGL` is the primary 3D-capable backend (OpenGL ES 3.0). SDL_Renderer throws for all
  3D operations by design.
- C# properties become `getXProperty()` / `setXProperty()` pairs. This convention is strict.
- All .NET primitives use SharpRuntime aliases (`bytecs`, `intcs`, `Single`, `String`, …).
- Non-XNA extensions inside the `Microsoft::Xna` namespace are tagged with `NOXNA`.
- New `.cpp` files are auto-discovered via `GLOB_RECURSE` — no CMakeLists edits needed.

---

## 2. Current status

### Build
- **Clean build** on both `cmake-build-vulkan` and `cmake-build-easygl`.
- Libraries: `libCNA.a`, `libcna_backend_graphics_vulkan.a` / `libcna_backend_graphics_easygl.a`.
- Executables: `CnaTests`, `cna_demo_2d`, `cna_demo_input`, `cna_demo_sound`,
  `cna_demo_xact`, `cna_house3d_demo`.
- No known failing tests.

### What works
- **Graphics core:** `GraphicsDevice`, `SpriteBatch`, `Texture2D`, `Texture3D`, `TextureCube`,
  `VertexBuffer`, `IndexBuffer` (16- and 32-bit), `DynamicVertexBuffer`, `DynamicIndexBuffer`.
- **All five stock effects:** `SpriteEffect`, `AlphaTestEffect`, `EnvironmentMapEffect`,
  `DualTextureEffect`, `SkinnedEffect` — full XNA API with dirty-flag lazy recomputation.
- **BasicEffect** — `IEffectMatrices`, `IEffectFog`, `IEffectLights`.
- **Effect hierarchy:** `Effect` → `GraphicsResource`, `EffectTechnique`, `EffectPass`,
  `EffectParameter` (full GetValue/SetValue overloads including Texture3D/TextureCube),
  `EffectAnnotation`, all collections.
- **Vertex types:** `VertexPositionColor`, `VertexPositionColorTexture`,
  `VertexPositionNormalTexture`, `VertexPositionTexture`.
- **PackedVector:** 18 types (`Alpha8`, `Bgr565`, `Bgra4444`, `Bgra5551`, `Byte4`,
  `HalfSingle`, `HalfVector2`, `HalfVector4`, `NormalizedByte2/4`, `NormalizedShort2/4`,
  `Rg32`, `Rgba1010102`, `Rgba64`, `Short2`, `Short4`).
- **State objects:** `BlendState`, `DepthStencilState`, `RasterizerState`, `SamplerState`,
  `SamplerStateCollection`, `TextureCollection` — wired to `ApplyBlendState / ApplyDepthStencilState /
  ApplyRasterizerState` on both EasyGL and Vulkan backends.
- **EasyGL shader pipeline:** 4 shader variants by vertex stride (colored / textured /
  col+textured / lit+textured); `DrawPrimitivesEx` / `DrawIndexedPrimitivesEx`.
- **3D rendering:** EasyGL — colored + textured + lit primitive drawing, indexed drawing,
  depth test, blend, depth write, WVP matrix upload. `house3d_demo` runs.
- **Model system:** `Model::Draw` — computes absolute bone transforms, binds
  `IEffectMatrices` (World per bone × world arg, View, Projection), calls `ModelMesh::Draw`
  which sets VB/IB and issues `DrawIndexedPrimitives` per effect pass.
- **SpriteFont:** full glyph data model + `MeasureString`; `SpriteBatch::DrawString`
  (3 overloads) renders glyphs via per-character source/dest rects.
- **OcclusionQuery:** `IOcclusionQueryBackend`; EasyGL uses `GL_ANY_SAMPLES_PASSED`
  (GLES3: PixelCount = 0 or 1); other backends stub-fallback.
- **Input:** Keyboard, Mouse, GamePad (all axes/buttons), Touch, Gesture.
- **Audio:** SoundEffect, SoundEffectInstance, SoundBank/WaveBank (XACT).
- **Video:** FFmpeg-based `VideoPlayer` with per-frame texture update.
- **Storage:** `StorageDevice`, `StorageContainer`.
- **Math:** `Vector2/3/4`, `Matrix`, `Quaternion`, `BoundingBox`, `BoundingSphere`,
  `BoundingFrustum`, `Plane`, `Ray`, `Curve`, `MathHelper`.
- **Game infrastructure:** `Game`, `GameComponent`, `DrawableGameComponent`,
  `GameComponentCollection`.
- **Content:** `ContentManager` + `ContentTypeReader<T>` — custom system (no XNB).
- **`DrawUserIndexedPrimitives`** — packs vertices + 16-bit indices into temp buffers.
- **`DrawUserPrimitives`** — implemented in `GraphicsDevice.cpp`.
- **`GraphicsAdapter`:** `VendorId`/`DeviceId` read from `/sys/class/drm/card*/device/` on
  Linux (fallback 0). `Revision`/`SubSystemId` return 0 (not available via SDL3).

### What does NOT work yet

| Area | Status |
|------|--------|
| `Texture2D::SaveAsJpeg` | Implemented via `IMG_SaveJPG_IO` with scaling support. |
| `Texture2D` mip levels > 0 | Supported: `GetData`/`SetData` handle any level; EasyGL uploads via `set_image_2d(level, …)`. |
| `SpriteBatch` custom effect | Forwarded: `ShaderEffect` compiles/caches its GLSL program; non-ShaderEffect effects call `Apply()` only. |
| `RenderTarget2D` Vulkan backend | Implemented: off-screen color+depth images, framebuffer, descriptor set for texture sampling, RT render pass before backbuffer pass. |
| `FillMode::WireFrame` | Silently ignored — OpenGL ES does not expose `glPolygonMode`. |
| Vulkan / BGFX 3D draw | Vulkan backend has state (blend/depth/cull) but no real 3D draw call path. BGFX throws on all 3D calls. |
| Model asset loading | No pipeline to load a `Model` from a file; `Model::Draw` is complete but models must be constructed manually via the NOXNA constructor. |
| `RenderTarget2D` as `Texture2D` | Fixed: RT now inherits `Texture2D` (→ `Texture` → `GraphicsResource`). Can be used as a texture. |
| `DrawInstancedPrimitives` | Not implemented. |
| `GraphicsDevice::GetBackBufferData<T>` | Implemented for `Color*` on EasyGL (glReadPixels + Y-flip); Vulkan throws (staging readback not implemented). |

---

## 3. Recent changes (last session)

### Task 15 — Implement `Model::Draw`
- **Added:** `src/.../Model.cpp` — `CopyAbsoluteBoneTransformsTo` (parent-relative product),
  `CopyBoneTransformsFrom/To`, and `Draw(world, view, projection)`: shared bone matrix buffer,
  absolute transform computation, effect `IEffectMatrices` binding per mesh, calls `mesh->Draw()`.
- **Added:** `src/.../ModelMesh.cpp` — `Draw()` mirrors FNA: sets VB + IB on `GraphicsDevice`,
  iterates `currentTechnique.Passes`, calls `pass.Apply()` then `DrawIndexedPrimitives` per pass.
  NOXNA constructor accepts `GraphicsDevice*` + part list, wires `part->parent_`.
- **Added:** `src/.../ModelMeshPart.cpp` — `setEffectProperty` implements FNA's
  add/remove-from-parent-Effects-collection logic.
- **Added:** `.cpp` for all five collections (`ModelBoneCollection`, `ModelMeshCollection`,
  `ModelMeshPartCollection`, `ModelEffectCollection`, `ModelBone`).
- **Updated headers:** `ModelEffectCollection` gains `Add/Remove/Contains` + iterators;
  all collections gain range-for support; `ModelMeshPart` gains `Tag` + `parent_`;
  `ModelMesh` gains `GraphicsDevice*`, `BoundingSphere`, `Tag`, NOXNA constructor;
  `Model` gains `Tag`, `CopyBoneTransformsFrom/To`, NOXNA constructor, static
  `sharedDrawBoneMatrices_`; `ModelBone` gains `AddChild`.
- Build: `cmake-build-vulkan` and `cmake-build-easygl` — both `libCNA.a` link cleanly.

---

## 4. Next smallest tasks

### ~~Task 16 — `Texture2D::FromStream` + `GetData`~~ **DONE**

### Task 16 (completed) — summary
- `ImageLoader::LoadFromMemory` — SDL3 `SDL_IOFromConstMem` + `IMG_Load_IO`
- `Texture2D::FromStream(GraphicsDevice&, Stream&)` — reads full stream into buffer, decodes via ImageLoader
- `Texture2D::GetData(Color*, startIndex, elementCount)` — reads CPU-side `cpuPixels_` copy
- `Texture2D::GetData(Color*, elementCount)` — convenience overload
- `Texture2D::SaveAsPng(const std::string& filename)` — creates SDL_Surface from CPU pixels, calls `IMG_SavePNG`
- `Texture2D::getWidthProperty()` / `getHeightProperty()` — were missing, added
- All constructors now populate `cpuPixels_` so `GetData` works immediately after load
- Build: `cmake-build-vulkan` and `cmake-build-easygl` — both clean

### ~~Task 17 — `SpriteBatch` deferred sort modes~~ **DONE**

- Private `SpriteInfo` struct added to header (destRect, srcRect, color, rotation, origin, effects, layerDepth).
- `pushSprite()` helper centralises buffering vs. immediate flush.
- `Begin(sortMode, blendState)` stores `sortMode_` and clears `spriteQueue_`.
- All `Draw()` overloads and all `DrawString()` glyphs now go through `pushSprite()`.
- `End()` calls `flushBatch()` (sorts + replays) before `backend_->End()`.
- Sort order: `BackToFront` → `stable_sort` descending layerDepth; `FrontToBack` → ascending; `Texture` → by texture pointer; `Deferred` → submission order; `Immediate` → no queue, direct flush per call.
- Build: both backends clean.

### ~~Task 18 — `RenderTarget2D` EasyGL backend wiring~~ **DONE**

- Added `virtual void BindGL() const {}` to `ITextureBackend` (all static_casts removed).
- Added `IRenderTargetBackend : public ITextureBackend` with `BindAsRenderTarget()` / `UnbindAsRenderTarget()`.
- Added `CreateRenderTarget2D(w, h, hasDepth)` and `SetRenderTarget2D(rt)` to `IGraphicsBackend` (default no-op).
- Implemented `EasyGLRenderTargetBackend`: FBO + color texture (RGBA8) + optional depth renderbuffer (DepthComponent24).
- Fixed 3 `static_cast<EasyGLTextureBackend>` sites in `EasyGLGraphicsBackend.cpp` to use virtual `BindGL()` / `GetWidth()` / `GetHeight()`.
- `EasyGLSpriteBatchBackend::current_texture_` type widened from `const EasyGLTextureBackend*` to `const ITextureBackend*`.
- `RenderTarget2D` now owns `unique_ptr<IRenderTargetBackend> rtBackend_`, created in its constructor.
- `GraphicsDevice::SetRenderTarget(RenderTarget2D*)` calls `backend_->SetRenderTarget2D(...)`.
- Build: both `cmake-build-easygl` and `cmake-build-vulkan` — clean.

### ~~Task 19 — `SpriteBatch::Begin` transform matrix and custom effect overloads~~ **DONE**

- All `Begin()` overloads now funnel to the full 7-arg XNA `Begin(sortMode, blendState, samplerState, depthStencilState, rasterizerState, effect, transformMatrix)`.
- `Matrix transformMatrix_` and `Effect* customEffect_` fields added to `SpriteBatch`.
- `ISpriteBatchBackend::SetTransformMatrix(const Matrix&)` added (default no-op); called from `Begin`.
- `EasyGLSpriteBatchBackend`: `transform_` stored; `FlushBatch` now computes `Matrix::CreateOrthographicOffCenter(…) * transform_` instead of a hardcoded float array.
- `customEffect_` stored and cleared per Begin/End cycle — forwarding to the draw path is Task 20.
- Build: both backends clean.

### ~~Task 20 — `Texture2D::GetData` full overload + `SetData` with rectangle~~ **DONE**

- Added `GetData(int level, const Rectangle* rect, Color* data, int startIndex, int elementCount)`:
  delegates to the flat overload when `rect == nullptr`, otherwise copies row-by-row from `cpuPixels_`.
- Added `SetData(int level, const Rectangle* rect, const Color* data, int startIndex, int elementCount)`:
  writes row-by-row into `cpuPixels_`, then calls `UpdatePixels` on the backend (or recreates the texture if no backend yet).
- Both overloads throw `std::runtime_error` for `level != 0` (only mip 0 stored in `cpuPixels_`).
- Build: both backends clean.

### ~~Task 21 — `DrawUserPrimitives` / `DrawUserIndexedPrimitives` with all vertex types~~ **DONE**

- Added typed overloads for all 4 vertex types (`VPC`, `VPT`, `VPCT`, `VPNT`) for both
  `DrawUserPrimitives` and `DrawUserIndexedPrimitives`.
- Each overload packs vertices into a layout-compatible GPU struct (`GpuVPC/T/CT/NT`) with
  static_asserts on stride (16/20/24/32), then calls `DrawPrimitivesEx` / `DrawIndexedPrimitivesEx`
  with `BuildGpuDrawParams(currentEffect_)` for correct texture/color/lighting shader selection.
- Packing is always explicit (never raw pointer cast) because vertex types inherit `IVertexType`
  (vtable) and `Color` inherits `IPackedVector` (vtable).
- `DrawUserIndexedPrimitives` uses 16-bit indices. 32-bit variant deferred.
- Build: both backends clean.

### ~~Task 22 — `GameWindow` resize event wiring~~ **DONE**

- `Game.cpp` event loop now handles `SDL_EVENT_WINDOW_RESIZED` and
  `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` → calls `Window_.updateFromSDL()`.
- `GraphicsDeviceManager::registerServices()` now subscribes `INTERNAL_OnClientSizeChanged`
  to `game_->getWindowProperty().ClientSizeChanged` so viewport + back buffer are updated
  automatically when the window is resized.
- `GameWindow::AllowUserResizing` / `IsBorderlessEXT` / `ClientSizeChanged` were already
  implemented; only the SDL event handling and subscription were missing.
- Build: both backends clean.

### ~~Task 23 — `Viewport::Project` / `Unproject`~~ **DONE**

- Added `Project(source, projection, view, world) → Vector3`: computes WVP, transforms point,
  perspective-divides, maps to viewport pixel coords. Faithful to FNA.
- Added `Unproject(source, projection, view, world) → Vector3`: inverts WVP, maps screen pixels
  to NDC, transforms back, perspective-divides.
- Added missing Viewport API: `Viewport(x,y,w,h)` constructor, `Viewport(Rectangle)` constructor,
  `getAspectRatioProperty()`, `getBoundsProperty()` / `setBoundsProperty()`,
  `getTitleSafeAreaProperty()`.
- Build: both backends clean.

### ~~Task 24 — `BoundingBox` / `BoundingSphere` / `BoundingFrustum` intersection methods~~ **DONE**

- Verified all `Contains` / `Intersects` overloads against FNA for all three types.
- All XNA overloads are present and correctly implemented:
  - `BoundingBox`: `Contains(BoundingBox/BoundingSphere/BoundingFrustum/Vector3)`, `Intersects(BoundingBox/BoundingSphere/BoundingFrustum/Plane/Ray)` + ref/out variants.
  - `BoundingSphere`: `Contains(BoundingBox/BoundingSphere/BoundingFrustum/Vector3)`, `Intersects(BoundingBox/BoundingSphere/BoundingFrustum/Plane/Ray)` + ref/out variants.
  - `BoundingFrustum`: `Contains(BoundingBox/BoundingSphere/BoundingFrustum/Vector3)`, `Intersects(BoundingBox/BoundingSphere/BoundingFrustum/Plane/Ray)` + ref/out variants.
- `BoundingFrustum::Intersects(Ray)` throws `NotImplementedException` — matches FNA (same unimplemented TODO in FNA source).
- No code changes needed; build clean on both backends.

### ~~Task 25 — `SpriteBatch` custom effect forwarding~~ **DONE**

- Added forward declaration of `Effect` and `using Effect = ...` alias to `IGraphicsBackend.hpp`.
- Added `virtual void SetCustomEffect(Effect* effect) {}` to `ISpriteBatchBackend`.
- `SpriteBatch::Begin` (7-arg) now calls `backend_->SetCustomEffect(customEffect_)` before `Begin()`.
- `SpriteBatch::End` resets the backend's custom effect to `nullptr` after `End()`.
- `EasyGLSpriteBatchBackend`: added `customEffect_`, `customProgram_`, `compiledFor_` fields.
- `SetCustomEffect`: flushes pending batch on effect change, stores new effect.
- `FlushBatch`: if a `ShaderEffect` is set, lazily compiles and caches its GLSL program (only recompiles when the effect pointer changes); uses `customProgram_` instead of `program_`. Calls `effect->Apply()` before drawing so `OnApply()` runs. The projection uniform is uploaded to whichever program is active; `uniform_location` returns -1 gracefully if the custom shader doesn't have it.
- Non-`ShaderEffect` custom effects (e.g. `BasicEffect`) still call `Apply()` but use the built-in sprite program.
- `release_gl_handle_only`: also resets `customProgram_`.
- `recreate_gl_resource`: clears `compiledFor_` so the custom program is recompiled after context loss.
- Build: both `cmake-build-easygl` and `cmake-build-vulkan` — clean.

### ~~Task 26 — `Texture2D::SaveAsJpeg`~~ **DONE**

- Added `void SaveAsJpeg(System::IO::Stream* stream, int width, int height) const` to `Texture2D.hpp` (XNA 4.0 API).
- Added `NOXNA void SaveAsJpeg(const std::string& filename) const` file-path convenience overload.
- Implementation in `Texture2D.cpp`:
  - Creates `SDL_Surface` from `cpuPixels_` (RGBA32).
  - If `targetWidth/Height` differ from texture size, uses `SDL_ScaleSurface` (linear filter) to create a scaled surface.
  - Encodes via `IMG_SaveJPG_IO` into `SDL_IOFromDynamicMem()` at quality 100 (matches FNA default).
  - Reads back the encoded buffer via `SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER` + `SDL_TellIO`, writes to the `System::IO::Stream`.
  - File-path overload uses `IMG_SaveJPG` directly (no memory buffer needed).
- Build: both backends clean.

### ~~Task 27 — `Texture2D` mip levels > 0 for `GetData` / `SetData`~~ **DONE**

- Added `extraMipLevels_` (`std::vector<std::vector<uint8_t>>`) for levels 1+; level 0 stays in `cpuPixels_`.
- Added `getMipBuffer(level)` (lazy allocates at half-size per level) and `getMipBufferConst(level)`.
- Added `mipDim(base, level) = max(1, base >> level)` helper — drives level width/height.
- `SetData(level, rect, data, start, count)`: removed `level != 0` throw; uses `getMipBuffer(level)` and `mipDim` for correct dimensions; for level > 0 calls `backend_->UpdatePixelsLevel(level, buf, w, h)`.
- `GetData(level, rect, data, start, count)`: removed `level != 0` throw; uses `getMipBufferConst(level)` and `mipDim`.
- Added `virtual void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) {}` to `ITextureBackend` (default no-op).
- Added `void UpdatePixels(…)` override to `EasyGLTextureBackend` (was previously no-op from base; now does `set_image_2d` at level 0 and updates `image_data_`).
- Added `void UpdatePixelsLevel(…)` override to `EasyGLTextureBackend`: calls `texture.set_image_2d(target, level, w, h, data)`.
- Build: both backends clean.

### ~~Task 28 — `RenderTarget2D` Vulkan backend~~ **DONE**

- Added `VulkanRenderTargetBackend` class: color image (`swapchainFormat_` for pipeline compatibility),
  dedicated depth image (`depthFormat_`), framebuffer, descriptor set for sampling as texture.
- Color image is always created with `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT`.
- Created `rtRenderPass_`: identical to `renderPass_` but color `finalLayout = SHADER_READ_ONLY_OPTIMAL` —
  Vulkan render pass compatibility means existing `pipeline2D_` and `pipelines3D_` can be reused unchanged.
- `CreateRTRenderPass()` is called lazily on first RT construction.
- `RecordCommandBuffer` refactored: Phase 1 iterates unique RTs from `activeBatches_` + `pending3D_`,
  opens an `rtRenderPass_` for each RT, draws sprites + 3D targeting it, closes it. Phase 2 is the
  unchanged backbuffer pass.
- `activeBatches_` changed to `vector<pair<VulkanSpriteBatchBackend*, VulkanRenderTargetBackend*>>`;
  each batch records its target RT at `Begin()` time (`backend_->currentRT_`).
- `Pending3DDraw` gains `rt` field; both `DrawColoredPrimitives` and `DrawIndexedColoredPrimitives`
  set `d.rt = currentRT_`.
- `SetRenderTarget2D(rt)` sets `currentRT_`; `SetRenderTarget2D(nullptr)` clears it.
- `TransitionImageLayout` extended: UNDEFINED → SHADER_READ_ONLY_OPTIMAL and
  SHADER_READ_ONLY_OPTIMAL → COLOR_ATTACHMENT_OPTIMAL variants added.
- Destructor cleans up `liveRenderTargets_` and `rtRenderPass_`.
- Build: both `cmake-build-vulkan` and `cmake-build-easygl` — clean.

### Task 29 — `Texture2D` — stream `SaveAsPng`, full constructor, `Format`/`LevelCount`

**Goal:** fill the remaining XNA API gaps on `Texture2D`.

Steps:
1. Add `SaveAsPng(System::IO::Stream* stream, int width, int height)` — the true XNA 4.0 stream overload (was only file-path before).
2. Add `Texture2D(GraphicsDevice&, int, int, bool mipMap, SurfaceFormat format)` — full constructor; stores `format_` and computes `levelCount_`.
3. Expose `getFormatProperty()` → `SurfaceFormat` and `getLevelCountProperty()` → `int` on `Texture2D`.

#### ~~Task 29 — DONE~~

- Added `SaveAsPng(Stream*, int, int)`: uses `SDL_IOFromDynamicMem` + `IMG_SavePNG_IO` + read-back, same pattern as `SaveAsJpeg`.  Scaling via `SDL_ScaleSurface` when target dimensions differ.
- Added `Texture2D(GraphicsDevice&, int, int, bool mipMap, SurfaceFormat format)`: stores `format_`, computes `levelCount_ = mipMap ? CalculateMipLevels(w,h) : 1`.
- Added `format_` (`SurfaceFormat::Color`) and `levelCount_` (1) private fields; `getFormatProperty()` and `getLevelCountProperty()` expose them.
- Static helper `CalculateMipLevels(w,h)` halves dimensions until 1×1.
- Build: both backends clean.

### ~~Task 30 — `ContentManager` — `Song` and `Video` type readers~~ **DONE**

- Added `SongTypeReader`: tries extensions `.mp3 .ogg .wav .flac .opus .aac .wma`; constructs
  `Media::Song(path, stem)` so `Content.Load<Song>("music")` works without specifying extension.
- Added `VideoTypeReader`: tries extensions `.mp4 .ogv .webm .mkv .avi .mov`; constructs
  `Media::Video(path, &device)` so `Content.Load<Video>("cutscene")` works.
- Both readers registered in `RegisterBuiltinLoaders()`.
- Build: both backends clean.

### ~~Task 31 — `GraphicsDevice::GetBackBufferData<Color>`~~ **DONE**

- Added `virtual void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)` to
  `IGraphicsBackend` (default throws — backends that don't support readback remain safe).
- `EasyGLGraphicsBackend::ReadBackbuffer`: calls `device.read_pixels(metagl::PixelFormat::Rgba,
  metagl::PixelType::UnsignedByte)`, flips rows vertically (GL bottom-left → XNA top-left).
  Y coordinate converted: `glY = vpH - y - h`.
- `GraphicsDevice` gains three XNA 4.0 overloads:
  - `GetBackBufferData(Color*, elementCount)` — full backbuffer
  - `GetBackBufferData(Color*, startIndex, elementCount)` — with offset
  - `GetBackBufferData(const Rectangle*, Color*, startIndex, elementCount)` — with region
  All funnel to the rect overload; rect==nullptr reads the full viewport.
- Vulkan backend inherits the throwing default — throws `runtime_error` (staging readback deferred).
- Build: both `cmake-build-vulkan` and `cmake-build-easygl` — clean.

### ~~Task 32 — `RenderTarget2D : Texture2D` — correct inheritance hierarchy~~ **DONE**

- **Problem:** `RenderTarget2D` was inheriting `GraphicsResource, IRenderTarget` — it was NOT a
  `Texture2D`, so an RT could not be bound as a sampler texture. This diverged from FNA where
  `RenderTarget2D : Texture2D : Texture : GraphicsResource`.
- **`Texture2D`** now inherits `Texture` (→ `GraphicsResource` → `Object`):
  - Removed `format_`, `levelCount_` private fields — now use protected `format_` / `levelCount_`
    from `Texture`.
  - Removed `device_` — now uses `graphicsDevice_` from `GraphicsResource`.
  - Added `GetTypeName()` override (required by `System::Object`).
  - Added protected constructor `Texture2D(device, w, h, fmt, levelCount, shared_ptr<ITextureBackend>)`
    for `RenderTarget2D` to use.
  - Added protected `GetBackendRaw()` — lets `RenderTarget2D` retrieve the raw `IRenderTargetBackend*`
    from the shared_ptr stored in `backend_`.
- **`RenderTarget2D`** now inherits `Texture2D, IRenderTarget`:
  - Removed `width_`, `height_`, `format_`, `levelCount_` — all inherited from `Texture2D`/`Texture`.
  - Constructor calls the protected `Texture2D` ctor; passes a `shared_ptr<IRenderTargetBackend>`
    (converted from the `unique_ptr` returned by `CreateRenderTarget2D`).
  - `rtBackend_` is now a raw non-owning ptr obtained via `static_cast<IRenderTargetBackend*>(GetBackendRaw())`.
  - `IRenderTarget` pure virtuals (`getWidthProperty`, `getHeightProperty`, `getLevelCountProperty`)
    satisfied via inline forwarders that delegate to `Texture2D`/`Texture`.
- Build: both backends clean.

---

## 5. Do not do yet

- **No Vulkan 3D implementation** — Vulkan is state-only. Do not add real 3D draw calls there
  until the EasyGL path is fully verified.
- **No XNB content pipeline** — CNA uses its own content system. Do not port FNA's 40+
  `ContentReaders` unless a specific game integration requires XNB loading.
- **No `Design::*Converter` types** — TypeConverter subclasses for IDE tooling have no meaning
  in a C++ runtime.
- **No FNA platform internals** — `FNA3D`, `FNAPlatform`, `GestureDetector`, `DxtUtil`,
  `X360TexUtil` are FNA implementation details, not XNA API.
- **No mass refactor** of existing working code.
- **No API renames** — never rename a method/class for C++ aesthetics if it diverges from FNA.

---

## 6. Architecture notes

### Main modules

```
Microsoft::Xna::Framework::           — public XNA API (must match FNA exactly)
  Graphics::                          — GraphicsDevice, textures, effects, vertices, Model
  Graphics::PackedVector::            — 18 packed types (header-only)
  Input::                             — Keyboard, Mouse, GamePad, Touch, Gesture
  Content::                           — ContentManager + ContentTypeReader<T>
  Audio::                             — SoundEffect, SoundBank, WaveBank
  Media::                             — VideoPlayer (FFmpeg), Song, MediaPlayer

CNA::                                 — project-specific internals (NOXNA tagged)
  Internal::Backends::Common::        — IGraphicsBackend, IVertexBufferBackend, …
  Internal::Backends::EasyGL::        — OpenGL ES 3.0 implementation
  Internal::Backends::SDL_Renderer::  — 2D only; throws on all 3D calls
  Internal::Backends::Vulkan::        — state wired; no real 3D draw path
  Internal::Input::InputManager::     — maps SDL events to XNA Input state
  Internal::Graphics::ImageLoader::   — loads PNG/BMP/… into RGBA via stb_image
```

### Data flow (3D draw)
```
Game::Draw()
  → GraphicsDevice::DrawIndexedPrimitives()
      → IGraphicsBackend::DrawIndexedPrimitivesEx(GpuDrawParams)
          → EasyGLGraphicsBackend: select shader by stride, bind VAO, glDrawElements
```

### Data flow (Model::Draw)
```
Model::Draw(world, view, projection)
  → CopyAbsoluteBoneTransformsTo(sharedDrawBoneMatrices_)
  → for each mesh: cast effect → IEffectMatrices, set World/View/Projection
  → ModelMesh::Draw()
      → GraphicsDevice::SetVertexBuffer / setIndicesProperty
      → for each pass: pass.Apply() → GraphicsDevice::DrawIndexedPrimitives()
```

### State flow
```
GraphicsDevice::setBlendStateProperty(bs)
  → backend_->ApplyBlendState(…)
      // EasyGL: glBlendFuncSeparate + glBlendEquationSeparate
      // Vulkan:  updates blendEnabled_ → baked into pipeline key
      // BGFX:    no-op
```

### Invariants that must not be broken
- **XNA API names:** Class/method/enum names must match FNA exactly.
- **Namespace:** Original XNA types stay in `Microsoft::Xna::Framework::*`. `CNA::` is for internals.
- **Property convention:** `getXProperty()` / `setXProperty()` — strict.
- **SharpRuntime types:** `bytecs`, `intcs`, `Single`, `String` etc. — never raw C++ in the XNA surface.
- **SDL_Renderer 3D:** Must always throw (by contract).

---

## 7. Useful commands

```bash
# Configure Vulkan build (default)
cmake -B cmake-build-vulkan -DCNA_GRAPHICS_BACKEND=VULKAN -DCMAKE_BUILD_TYPE=Debug

# Configure EasyGL build (for 3D testing)
cmake -B cmake-build-easygl -DCNA_GRAPHICS_BACKEND=EASYGL -DCMAKE_BUILD_TYPE=Debug

# Build library only
cmake --build cmake-build-vulkan --target CNA

# Build everything
cmake --build cmake-build-vulkan

# Run tests
./cmake-build-vulkan/CnaTests

# Run 3D demo (EasyGL)
./cmake-build-easygl/cna_house3d_demo

# Run 2D demo
./cmake-build-vulkan/cna_demo_2d
```

---

## 8. Resume prompt

```
Read NEXT.md first to understand the current state of the CNA project.

The next tasks are in section 4:
- Task 16: Texture2D::FromStream + GetData (requires System::IO::Stream in sharp-runtime)
- Task 17: SpriteBatch deferred sort modes (BackToFront, FrontToBack, Texture)
- Task 18: RenderTarget2D EasyGL backend wiring (FBO + SetRenderTarget)

The "What does NOT work yet" table in section 2 lists remaining gaps.
Pick the task most relevant to the current game integration need.
```
