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
| `Texture2D::SaveAsJpeg` | Not implemented. |
| `Texture2D` mip levels > 0 | `GetData`/`SetData` only support mip level 0. `cpuPixels_` stores one level. |
| `SpriteBatch` custom effect | `customEffect_` is stored and cleared in `Begin`/`End` but not yet forwarded to the EasyGL draw path. |
| `RenderTarget2D` Vulkan backend | EasyGL FBO is wired and works. The Vulkan backend still returns `nullptr` from `CreateRenderTarget2D` (no-op). |
| `FillMode::WireFrame` | Silently ignored — OpenGL ES does not expose `glPolygonMode`. |
| Vulkan / BGFX 3D draw | Vulkan backend has state (blend/depth/cull) but no real 3D draw call path. BGFX throws on all 3D calls. |
| Model asset loading | No pipeline to load a `Model` from a file; `Model::Draw` is complete but models must be constructed manually via the NOXNA constructor. |

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

### Task 24 — `BoundingBox` / `BoundingSphere` / `BoundingFrustum` intersection methods

**Goal:** verify and complete the intersection/containment API on the three bounding volume types,
which games use for culling and picking.

FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/BoundingBox.cs`,
`BoundingSphere.cs`, `BoundingFrustum.cs`

Steps:
1. Check which `Contains` / `Intersects` overloads are already present.
2. Add any missing overloads (e.g. `BoundingBox.Contains(BoundingFrustum)`,
   `BoundingFrustum.Intersects(BoundingSphere)`, etc.).

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
