# NEXT.md — CNA project handoff

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model built on SDL3 with a
pluggable compile-time graphics backend. It is a framework/runtime, not a game. The goal is to
preserve the `Microsoft::Xna::Framework` public API exactly as defined by FNA while using
modern C++ internals, so that XNA-targeting games can be ported to C++ with minimal API friction.

**Authoritative API reference:** `/rv/data/library/github.com/FNA-XNA/FNA/src`

**Development phase:** Core graphics, input, audio, and content APIs are largely complete.
The next phase is filling remaining content-pipeline gaps (SpriteFont and Model file loading)
and hardening existing systems.

**Key architectural decisions:**
- Backend selected at compile-time via `CNA_GRAPHICS_BACKEND` CMake option
  (`SDL_RENDERER` | `EASYGL` | `VULKAN` | `BGFX`). Default debug builds use `cmake-build-vulkan/`.
- `EasyGL` is the primary 3D-capable backend (OpenGL ES 3.0). SDL_Renderer throws on all 3D ops.
- C# properties → `getXProperty()` / `setXProperty()`. This convention is strict everywhere.
- All .NET primitives use SharpRuntime aliases (`bytecs`, `intcs`, `Single`, `String`, …).
- Non-XNA extensions inside the `Microsoft::Xna` namespace are tagged `NOXNA`.
- New `.cpp` files are auto-discovered via `GLOB_RECURSE` — no CMakeLists edits needed.
- The class hierarchy mirrors FNA: `RenderTarget2D : Texture2D : Texture : GraphicsResource`.

---

## 2. Current status

### Build
- **Clean build** on both `cmake-build-vulkan` and `cmake-build-easygl`.
- Libraries: `libCNA.a`, `libcna_backend_graphics_vulkan.a` / `libcna_backend_graphics_easygl.a`.
- Executables: `CnaTests`, `cna_demo_2d`, `cna_demo_input`, `cna_demo_sound`,
  `cna_demo_xact`, `cna_house3d_demo`.
- No known failing tests.

### What works
- **Graphics core:** `GraphicsDevice` (full state API, render targets, back-buffer readback on EasyGL),
  `SpriteBatch` (all sort modes, transform matrix, custom ShaderEffect),
  `Texture2D` (load, GetData/SetData, all mip levels, SaveAsPng/SaveAsJpeg with streams),
  `Texture3D`, `TextureCube`, 18 PackedVector types.
- **Texture hierarchy:** `Texture2D : Texture : GraphicsResource : Object` — correct FNA-matching
  inheritance. `RenderTarget2D : Texture2D, IRenderTarget` — RTs are usable as sampler textures.
- **Effects:** `BasicEffect`, `AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`,
  `SkinnedEffect`, `SpriteEffect`. Full `IEffectMatrices` / `IEffectFog` / `IEffectLights`.
  `EffectParameter` complete (all GetValue/SetValue overloads).
- **Vertices/Buffers:** `VertexBuffer`, `DynamicVertexBuffer`, `IndexBuffer` (16- and 32-bit),
  `DynamicIndexBuffer`, all 4 VertexPosition* types.
- **States:** `BlendState`, `DepthStencilState`, `RasterizerState`, `SamplerState` — all wired
  to `ApplyBlendState / ApplyDepthStencilState / ApplyRasterizerState` on EasyGL and Vulkan.
- **RenderTarget2D:** EasyGL (FBO + depth renderbuffer) and Vulkan (off-screen images, RT render
  pass, deferred command recording) backends both implemented.
- **3D rendering (EasyGL):** 4 shader variants by vertex stride, depth test/write, blend,
  cull mode, WVP matrix upload. `house3d_demo` runs.
- **3D rendering (Vulkan):** Deferred via `Pending3DDraw` queue; blend/depth/cull state
  wired; uses push constants for MVP. Functional for colored and indexed primitives.
- **Model system:** `Model::Draw` — bone transforms, `IEffectMatrices` binding, `ModelMesh::Draw`
  issues `DrawIndexedPrimitives` per effect pass.
- **SpriteFont:** Full glyph data model, `MeasureString`, `SpriteBatch::DrawString` (3 overloads).
- **OcclusionQuery:** EasyGL uses `GL_ANY_SAMPLES_PASSED`; others stub-fallback.
- **Input:** Keyboard, Mouse, GamePad (all axes/buttons), Touch, Gesture.
- **Audio:** SoundEffect, SoundEffectInstance, SoundBank/WaveBank (XACT). AudioEngine is a stub.
- **Video:** FFmpeg-based `VideoPlayer` with per-frame RGBA texture update.
- **Storage:** `StorageDevice`, `StorageContainer`.
- **Math:** `Vector2/3/4`, `Matrix`, `Quaternion`, `BoundingBox`, `BoundingSphere`,
  `BoundingFrustum`, `Plane`, `Ray`, `Curve`, `MathHelper`.
- **Game infrastructure:** `Game`, `GameComponent`, `DrawableGameComponent`,
  `GameWindow` (resize event wired), `GraphicsDeviceManager`, `GameComponentCollection`.
- **Content:** `ContentManager` + `ContentTypeReader<T>` — readers for `Texture2D`,
  `SoundEffect`, `Effect` (`.shader.json`), `Song`, `Video`. No XNB pipeline.
- **DrawUserPrimitives / DrawUserIndexedPrimitives** — all 4 typed vertex variants.
- **Viewport::Project / Unproject** — fully implemented.
- **GetBackBufferData<Color>** — EasyGL: `glReadPixels` + Y-flip. Vulkan: throws.

### What does NOT work yet

| Area | Status |
|------|--------|
| `ContentManager::Load<SpriteFont>` | ✅ Done — `SpriteFontTypeReader` via `.font.json`. |
| `ContentManager::Load<Model>` | ✅ Done — `ModelTypeReader` via `.model.json` + binary vertex/index files. |
| `GetBackBufferData` on Vulkan | Throws — staging-buffer readback not wired. |
| `DrawInstancedPrimitives` | Completely absent from `GraphicsDevice`. |
| `FillMode::WireFrame` | Silently ignored — OpenGL ES 3.0 has no `glPolygonMode`. |
| `Media::MediaLibrary` | Fully stubbed — all methods throw `runtime_error`. |
| `Audio::AudioEngine` | Mostly stubbed. |
| Vulkan `GetBackBufferData` | Throws. |

---

## 3. Recent changes

### Task 28 — `RenderTarget2D` Vulkan backend
- `VulkanRenderTargetBackend`: color image (`swapchainFormat_`), depth image, framebuffer,
  descriptor set for texture sampling, `rtRenderPass_` with `SHADER_READ_ONLY_OPTIMAL` final layout.
- `RecordCommandBuffer` refactored: Phase 1 draws to each RT; Phase 2 draws to backbuffer.
- `activeBatches_` changed to `vector<pair<VulkanSpriteBatchBackend*, VulkanRenderTargetBackend*>>`.

### Task 29 — `Texture2D` stream API
- `SaveAsPng(Stream*, w, h)` — XNA 4.0 stream overload via `SDL_IOFromDynamicMem` + `IMG_SavePNG_IO`.
- `Texture2D(GraphicsDevice&, w, h, bool mipMap, SurfaceFormat)` full constructor.
- `getFormatProperty()` and `getLevelCountProperty()` exposed (now inherited from `Texture`).

### Task 30 — `ContentManager` Song/Video readers
- `SongTypeReader` — extensions: `.mp3 .ogg .wav .flac .opus .aac .wma`.
- `VideoTypeReader` — extensions: `.mp4 .ogv .webm .mkv .avi .mov`.

### Task 31 — `GraphicsDevice::GetBackBufferData<Color>`
- `IGraphicsBackend::ReadBackbuffer(x, y, w, h, pixels)` — default throws.
- `EasyGLGraphicsBackend::ReadBackbuffer` — `device.read_pixels(GL_RGBA, UNSIGNED_BYTE)` + Y-flip.
- `GraphicsDevice` gains 3 XNA 4.0 overloads: full buffer, with startIndex, with Rectangle.

### Task 32 — `RenderTarget2D : Texture2D` hierarchy fix
- `Texture2D` now inherits `Texture : GraphicsResource : Object` (FNA-matching hierarchy).
- Removed duplicate `format_`, `levelCount_`, `device_` from `Texture2D` (inherited from `Texture`).
- Added `GetTypeName()` override (required by `System::Object`).
- Added protected ctor `Texture2D(device, w, h, fmt, levelCount, shared_ptr<ITextureBackend>)`.
- `RenderTarget2D : Texture2D, IRenderTarget` — RT is now IS-A Texture2D. `rtBackend_` is a
  non-owning raw pointer; ownership lives in `Texture2D::backend_` as `shared_ptr`.

---

## 4. Current blocker / main problem

**No hard compilation blocker — both backends build cleanly.**

The most impactful functional gap is **`ContentManager::Load<SpriteFont>`**: games universally
load fonts via `Content.Load<SpriteFont>("fonts/Arial")`. The infrastructure is in place
(`SpriteFont` class is complete, `ContentTypeReader<T>` exists), but there is no
`SpriteFontTypeReader` and no agreed descriptor file format (`.font.json`).

The descriptor must encode:
- Path to the glyph atlas PNG
- Per-glyph source rectangles (glyphBounds)
- Per-glyph cropping rectangles
- Character list (unicode code points)
- LineSpacing, Spacing (float)
- Kerning data (Vector3 per glyph: left bearing, advance width, right bearing)
- Optional default character

Until this reader exists, any game that loads a SpriteFont via ContentManager will fail at
runtime with a "no reader registered" exception.

---

## 5. Known bugs and limitations

| Issue | Status |
|-------|--------|
| `Texture2D` copy semantics with `GraphicsResource` base | `GraphicsResource::~GraphicsResource()` calls `Dispose(false)` on every temporary, setting `isDisposed_ = true` on each copy that gets destroyed. The live copy is unaffected since fields are separate. Benign in practice but semantically surprising. **suspected bug** |
| `RenderTarget2D` default copy ctor copies `rtBackend_` raw ptr | Two RT objects pointing at the same backend is dangerous if either is destroyed. **confirmed design issue** — RT should not be copyable; add `= delete`. |
| Vulkan `GetBackBufferData` | Throws `runtime_error`. All screenshot/readback attempts on Vulkan fail. **incomplete** |
| `DrawInstancedPrimitives` | Missing from `GraphicsDevice` and all backends. **incomplete** |
| `ContentManager::Load<SpriteFont>` | No reader. **incomplete** |
| `ContentManager::Load<Model>` | ✅ Done — `ModelTypeReader` via `.model.json`. |
| `SpriteFont::textureValue_` | ✅ Fixed — now `Texture2D` by value (owned). No lifetime risk. |
| `FillMode::WireFrame` | Silently ignored on GLES3. **known limitation** |
| `AudioEngine` stub | All `AudioEngine` methods throw or are no-ops. **incomplete** |
| `Media::MediaLibrary` | Fully stubbed — playlist/photo/artist queries unimplemented. **incomplete** |
| `BoundingFrustum::Intersects(Ray)` | Throws `NotImplementedException` — matches FNA behavior. **known** |
| `DrawUserIndexedPrimitives` 32-bit indices | Only `uint16_t` variants exist; no 32-bit index overloads. **incomplete** |
| EasyGL context loss recovery | Implemented via `DebugSimulateContextLoss` but not triggered by real OS events on Linux. **needs verification** |

---

## 6. Architecture notes

### Main modules

```
Microsoft::Xna::Framework::           — public XNA 4.0 API (must match FNA exactly)
  Graphics::                          — GraphicsDevice, textures, effects, vertices, Model
    Texture : GraphicsResource        — base for all textures; holds format_, levelCount_
    Texture2D : Texture               — 2D texture + CPU-side pixel cache
    RenderTarget2D : Texture2D, IRenderTarget — RT is a Texture2D; shares backend_ ptr
    PackedVector::                    — 18 packed pixel types (header-only)
  Input::                             — Keyboard, Mouse, GamePad, Touch, Gesture
  Content::                           — ContentManager + ContentTypeReader<T> (no XNB)
  Audio::                             — SoundEffect, SoundBank, WaveBank (XACT)
  Media::                             — VideoPlayer (FFmpeg), Song, MediaPlayer

CNA::                                 — project-specific internals
  Internal::Backends::Common::        — IGraphicsBackend, ITextureBackend, IVertexBufferBackend
  Internal::Backends::EasyGL::        — OpenGL ES 3.0 (primary 3D backend)
  Internal::Backends::SDL_Renderer::  — 2D only; throws on all 3D calls
  Internal::Backends::Vulkan::        — 2D + deferred 3D via Pending3DDraw queue
  Internal::Input::InputManager::     — SDL events → XNA Input state
  Internal::Graphics::ImageLoader::   — PNG/BMP/… → RGBA via stb_image/SDL3_image
```

### Data flow (3D draw, EasyGL)
```
Game::Draw()
  → GraphicsDevice::DrawIndexedPrimitives()
      → IGraphicsBackend::DrawIndexedPrimitivesEx(GpuDrawParams)
          → EasyGLGraphicsBackend: select shader by stride, bind VAO, glDrawElements
```

### Data flow (RenderTarget usage)
```
GraphicsDevice::SetRenderTarget(&rt)
  → IGraphicsBackend::SetRenderTarget2D(rt.GetRenderTargetBackend())
      → EasyGL: glBindFramebuffer(FBO) / Vulkan: sets currentRT_

SpriteBatch::Begin() records target RT.
On End() / Present: RT content is in rt.backend_ (shared_ptr<ITextureBackend>).
device.Textures[0] = &rt;          // valid — RT IS-A Texture2D IS-A Texture
```

### Important invariants

- **XNA API names:** Class/method/enum names must match FNA exactly.
- **Namespace:** XNA types stay in `Microsoft::Xna::Framework::*`. `CNA::` is for internals.
- **Property convention:** `getXProperty()` / `setXProperty()` — never raw public fields.
- **SharpRuntime types:** `bytecs`, `intcs`, `Single`, `String` etc. — never raw C++ in XNA surface.
- **SDL_Renderer 3D:** Must always throw (by contract).
- **`Texture2D` backend_:** Private `shared_ptr<ITextureBackend>`. For `RenderTarget2D` the RT
  backend lives here; `rtBackend_` is non-owning. Do not store a second `shared_ptr` to the same
  backend from `RenderTarget2D`.
- **FNA hierarchy:** `RenderTarget2D : Texture2D : Texture : GraphicsResource`. Do not flatten this.

---

## 7. Useful commands

```bash
# Configure Vulkan build (default)
cmake -B cmake-build-vulkan -DCNA_GRAPHICS_BACKEND=VULKAN -DCMAKE_BUILD_TYPE=Debug

# Configure EasyGL build (3D demos, tests)
cmake -B cmake-build-easygl -DCNA_GRAPHICS_BACKEND=EASYGL -DCMAKE_BUILD_TYPE=Debug

# Build library only
cmake --build cmake-build-vulkan --target CNA
cmake --build cmake-build-easygl --target CNA

# Build everything (both backends)
cmake --build cmake-build-vulkan
cmake --build cmake-build-easygl

# Run tests
./cmake-build-easygl/CnaTests
./cmake-build-vulkan/CnaTests   # if available

# Run 3D demo (EasyGL — requires display)
./cmake-build-easygl/cna_house3d_demo

# Run 2D demo (Vulkan)
./cmake-build-vulkan/cna_demo_2d

# Run sound demo
./cmake-build-vulkan/cna_demo_sound

# FNA reference source
ls /rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/
```

---

## 8. Next smallest tasks

### Task 33 — `RenderTarget2D`: delete copy constructor / assignment ✅ DONE
- Added `= delete` for copy ctor/assignment, `= default` for move in `RenderTarget2D.hpp`.
- Verified: no existing code copies RT by value. Both backends build clean.

### Task 34 — `ContentManager::Load<SpriteFont>` via `.font.json` descriptor ✅ DONE
- `SpriteFont::textureValue_` changed from raw `Texture2D*` to `Texture2D` (owned by value).
  Constructor now takes `Texture2D` by value (moved in). Eliminates dangling-pointer risk.
- `SpriteFontTypeReader` added to `ContentManager.cpp` (anonymous namespace):
  - Parses `.font.json` descriptor (texture path, lineSpacing, spacing, defaultCharacter, glyphs array).
  - Loads atlas via `cm.Load<Texture2D>(textureName)` — atlas is cached and lifetime-safe.
  - Parses each glyph: `{ "char": N, "source":[x,y,w,h], "crop":[x,y,w,h], "kerning":[l,a,r] }`.
- Registered in `RegisterBuiltinLoaders()`.
- Both backends build clean.

### Task 35 — `ContentManager::Load<Model>` via `.model.json` descriptor
- **Goal:** `Content.Load<SpriteFont>("fonts/Arial")` works end-to-end.
- **Descriptor format** (`.font.json` alongside the atlas PNG):
  ```json
  {
    "texture": "fonts/Arial_atlas.png",
    "lineSpacing": 20,
    "spacing": 0.0,
    "defaultCharacter": "?",
    "glyphs": [
      { "char": 32, "source": [0,0,5,20], "crop": [0,0,5,20], "kerning": [0,5,0] },
      ...
    ]
  }
  ```
- **Files:** `src/Microsoft/Xna/Framework/Content/ContentManager.cpp` (add `SpriteFontTypeReader`),
  `include/Microsoft/Xna/Framework/Content/ContentManager.hpp` (register new reader type).
- **Verify:** Unit test or demo that calls `Content.Load<SpriteFont>("myfont")` and draws text.

### Task 35 — `ContentManager::Load<Model>` via `.model.json` descriptor ✅ DONE
- `ModelBone(int index, std::string name)` NOXNA constructor added.
- `ModelMeshPart(VertexBuffer*, IndexBuffer*, numVertices, primCount, startIndex, vertexOffset)` NOXNA constructor added.
- `ModelMesh(GraphicsDevice*, std::string name, vector<ModelMeshPart*>)` overloaded NOXNA constructor added.
- `Model::setOwnedResources(shared_ptr<void>)` NOXNA setter added; `Model` holds `shared_ptr<void> ownedResources_` for type-erased GPU resource lifetime.
- `ModelTypeReader` added to `ContentManager.cpp` (anonymous namespace):
  - Parses `.model.json`: `"meshes"` array + optional `"bones"` (first bone name used as root).
  - Per mesh: reads vertex `.bin` and index `.bin` (uint16 indices); dispatches to typed `VertexBuffer::SetData` by stride (16/20/24/32).
  - Effect: `"BasicEffect"` or empty → `make_shared<BasicEffect>(device)`; otherwise `cm.Load<shared_ptr<Effect>>(name)`.
  - GPU resources owned by `shared_ptr<ModelResources>` stored in `Model::ownedResources_`; copies of Model share the same buffers.
- Both backends build clean.

### Task 36 — `DrawInstancedPrimitives` (at least as a stub with correct API)
- **Goal:** `GraphicsDevice::DrawInstancedPrimitives(...)` exists and matches XNA 4.0 signature.
  EasyGL: implement with `glDrawElementsInstanced`. Others: throw `NotImplementedException`.
- **Files:** `include/Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp`,
  `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`,
  `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp`,
  `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`.
- **FNA reference:** `GraphicsDevice.cs` line 1257.
- **Verify:** `cmake --build cmake-build-easygl --target CNA` clean.

### Task 37 — `GraphicsDevice::GetBackBufferData` on Vulkan (staging buffer)
- **Goal:** Screenshot API works on Vulkan backend.
- **Approach:** After `Present()`, blit swapchain image to a host-visible staging buffer,
  pipeline barrier, map, copy to caller, unmap.
- **Files:** `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp`,
  `include/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.hpp`.
- **Verify:** `./cmake-build-vulkan/cna_demo_2d` + add a screenshot call; check the PNG is correct.

---

## 9. Do not do yet

- **No Vulkan 3D textured/lit pipeline** — Vulkan 3D currently supports only colored primitives
  (stride 16). Do not add a full shader system until the EasyGL path is fully verified and tested.
- **No XNB content pipeline** — CNA uses its own `.json`-descriptor content system.
  Do not port FNA's 40+ XNB `ContentReaders`.
- **No `Texture2D` base-class split** (separating `Texture` into more sub-levels) — the current
  `Texture → Texture2D` hierarchy is sufficient.
- **No `Design::*Converter` types** — TypeConverter subclasses for IDE tooling have no meaning
  in a C++ runtime.
- **No FNA platform internals** — `FNA3D`, `FNAPlatform`, `GestureDetector`, `DxtUtil`, `X360TexUtil`.
- **No mass refactor** of existing working code.
- **No API renames** — never rename a method/class for C++ aesthetics if it diverges from FNA.
- **No broad AudioEngine implementation** until a real game integration requires it.
- **No mass MediaLibrary implementation** — photo/playlist APIs are not needed for games.

---

## 10. Resume prompt

```
Read NEXT.md first to understand the current state of the CNA project.

The FNA authoritative reference is at: /rv/data/library/github.com/FNA-XNA/FNA/src

Start with the first uncompleted task in section 8 (Next smallest tasks).
Inspect only the files relevant to that task — do not read the whole codebase.
Do not refactor unrelated code or clean up nearby code that is not broken.
Make one small, verifiable improvement.
After implementing, run:
  cmake --build cmake-build-vulkan --target CNA
  cmake --build cmake-build-easygl --target CNA
Fix any errors before reporting done.
Update NEXT.md: mark the completed task as DONE in section 3, update section 2 if needed.
```
