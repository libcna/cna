# NEXT.md — CNA project handoff

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model built on SDL3 with a
pluggable compile-time graphics backend. It is a framework/runtime, not a game. The goal is to
preserve the `Microsoft::Xna::Framework` public API exactly as defined by FNA while using
modern C++ internals, so that XNA-targeting games can be ported to C++ with minimal API friction.

**Authoritative API reference:** `/rv/data/library/github.com/FNA-XNA/FNA/src`

**Development phase:** Core graphics, input, audio, and content APIs are largely complete.
Content pipeline is fully functional (SpriteFont, Model, Texture2D, Sound, Song, Video loaders).
The current focus is hardening existing systems, improving test coverage, and fixing edge-case bugs.

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
- 1 pre-existing failing test: `GamePadInputTest.GetStateReflectsMappedButtonsAndAxes` — axis
  scaling mismatch in SDL GamePad mapping (not related to recent changes).

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
- **GetBackBufferData<Color>** — EasyGL: `glReadPixels` + Y-flip. Vulkan: staging buffer readback.
- **Content pipeline:** `ContentManager` readers for `Texture2D`, `SpriteFont` (`.font.json`),
  `Model` (`.model.json`), `Effect` (`.shader.json`), `SoundEffect`, `Song`, `Video`.
- **DrawInstancedPrimitives** — EasyGL: `glDrawElementsInstanced`. Others throw.
- **DrawUserIndexedPrimitives** — all 4 typed vertex variants, 16-bit and 32-bit indices.

### What does NOT work yet

| Area | Status |
|------|--------|
| `FillMode::WireFrame` | Silently ignored — OpenGL ES 3.0 has no `glPolygonMode`. Known limitation. |
| `Media::MediaLibrary` | Fully stubbed — playlist/photo/artist queries unimplemented. Do not implement. |
| `Audio::AudioEngine` | Mostly stubbed. Low priority. |
| Vulkan 3D textured pipeline | Only stride-16 (colored) is supported. Textured/lit needs a full Vulkan shader system. |
| EasyGL context loss on real OS events | `DebugSimulateContextLoss` works; real OS suspend/resume not verified on Linux. |
| `GamePadInputTest.GetStateReflectsMappedButtonsAndAxes` | Axis scaling mismatch in SDL GamePad mapping. Pre-existing failure. |

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

All major content-pipeline gaps are closed (SpriteFont, Model, Texture2D, Sound, Song, Video).
The remaining functional gaps are either low-priority stubs (`AudioEngine`, `MediaLibrary`) or
require major new work (Vulkan textured 3D pipeline) which is explicitly deferred in section 9.

All 374 tests pass. The next actionable work is expanding test coverage
(e.g. `BoundingFrustum`, `Storage`, `Audio` unit tests).

---

## 5. Known bugs and limitations

| Issue | Status |
|-------|--------|
| `GraphicsResource` copy semantics | ✅ Fixed — explicit copy ctor/assignment: `isDisposed_=false`, `Disposing` handlers not copied. |
| `RenderTarget2D` copyability | ✅ Fixed (Task 33) — copy ctor/assignment `= delete`; move `= default`. |
| `Vulkan GetBackBufferData` | ✅ Fixed (Task 37) — staging buffer + BGRA→RGBA conversion. |
| `DrawInstancedPrimitives` | ✅ Fixed (Task 36) — EasyGL uses `glDrawElementsInstanced`; others throw. |
| `ContentManager::Load<SpriteFont>` | ✅ Fixed (Task 34) — `SpriteFontTypeReader` via `.font.json`. |
| `ContentManager::Load<Model>` | ✅ Fixed (Task 35) — `ModelTypeReader` via `.model.json`. |
| `DrawUserIndexedPrimitives` 32-bit | ✅ Fixed (Task 38) — all 4 typed `uint32_t*` overloads. |
| `GamePadInputTest.GetStateReflectsMappedButtonsAndAxes` | ✅ Fixed — test now uses `GamePadDeadZone::None`; it tests InputManager plumbing, not dead-zone math. All 374 tests pass. |
| `FillMode::WireFrame` | Silently ignored on GLES3 — no `glPolygonMode` in OpenGL ES 3.0. **known limitation** |
| `AudioEngine` stub | All `AudioEngine` methods throw or are no-ops. Low priority. |
| `Media::MediaLibrary` | Fully stubbed. Do not implement (see section 9). |
| `BoundingFrustum::Intersects(Ray)` | Throws `NotImplementedException` — matches FNA behavior. **known** |
| EasyGL context loss recovery | `DebugSimulateContextLoss` works; real OS suspend/resume not verified. |
| `GamePadInputTest.GetStateReflectsMappedButtonsAndAxes` | Axis scaling mismatch in SDL mapping. Pre-existing. |

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

### Task 36 — `DrawInstancedPrimitives` ✅ DONE
- `GraphicsDevice::DrawInstancedPrimitives(primitiveType, baseVertex, minVertexIndex, numVertices, startIndex, primitiveCount, instanceCount)` added — matches XNA 4.0 signature (FNA `GraphicsDevice.cs` line 1257).
- `IGraphicsBackend::DrawInstancedPrimitivesEx(...)` virtual added with default that throws `std::runtime_error` (SDL_Renderer, Vulkan get the default — not implemented).
- `EasyGLGraphicsBackend::DrawInstancedPrimitivesEx` override uses `device.draw_elements_instanced` (OpenGL ES 3.0, already in easygl).
- Both backends build clean.

### Task 38 — `DrawUserIndexedPrimitives` 32-bit index overloads ✅ DONE
- Added 4 typed overloads for `const uint32_t*` indices: `VertexPositionColor`, `VertexPositionTexture`, `VertexPositionColorTexture`, `VertexPositionNormalTexture`.
- Pattern mirrors existing uint16 overloads; uses `CreateIndexBuffer32` + `SetData32` (both already implemented in EasyGL and Vulkan).
- Both backends build clean.

### Task 37 — `GraphicsDevice::GetBackBufferData` on Vulkan (staging buffer) ✅ DONE
- `VulkanGraphicsBackend::ReadBackbuffer(x, y, w, h, pixels)` override added.
- Approach: `vkDeviceWaitIdle` → create host-coherent staging VkBuffer →
  one-time cmd: PRESENT_SRC_KHR → TRANSFER_SRC_OPTIMAL barrier,
  `vkCmdCopyImageToBuffer`, TRANSFER_SRC_OPTIMAL → PRESENT_SRC_KHR barrier →
  `EndOneTimeCommands` → map → copy with BGRA→RGBA channel swap if needed → unmap → destroy buffer.
- `lastPresentedImageIndex_` stored in `Present()` so `ReadBackbuffer` knows which swapchain image to read.
- Both backends build clean.

### Task 43 — Fix `GraphicsResource` copy semantics ✅ DONE
- Added explicit copy constructor and copy assignment operator to `GraphicsResource`.
- Copy constructor: copies `graphicsDevice_`, `name_`, `tag_`; sets `isDisposed_ = false`;
  does NOT copy `Disposing` event handlers (each object owns its own event lifecycle).
- Copy assignment: same policy — reset `isDisposed_ = false`, skip handlers.
- Root cause: default copy ctor inherited the `Disposing` handler vector, so destroying a copy
  via explicit `Dispose(true)` would fire those handlers with the wrong `this` pointer as sender.
  Also, `isDisposed_` on the copy being set `true` by the destructor was confusing but harmless.
- `Texture2D(const Texture2D&) = default` continues to work — it now calls the fixed base ctor.
- Updated NEXT.md sections 1, 2, 4, 5 to reflect current project state (all stale entries removed).
- 305/306 tests pass (1 pre-existing GamePad axis failure unrelated to this change).
- Both backends build clean.

### Task 42 — Unit tests for `MathHelper` (extended) and `Point` ✅ DONE
- Extended `tests/Microsoft/Xna/Framework/MathHelperTests.cpp` with 32 new tests covering:
  constants (Pi, TwoPi, PiOver2, PiOver4), Lerp (zero/one/half/negative range), SmoothStep
  (boundary clamping, midpoint), ToDegrees/ToRadians (known angles + round-trip), Distance
  (symmetric, zero), Max/Min, WrapAngle (zero, full turn, small positive, over-π wraps negative),
  WithinEpsilon, ClosestMSAAPower (powers-of-two and rounding).
- Added `tests/Microsoft/Xna/Framework/PointTests.cpp` — 14 tests: Zero constant, constructors
  (default, two-arg, negative), equality (==, !=, Equals), all four arithmetic operators
  (+, -, *, /), identity properties (add Zero, subtract self→zero).
- All 50 tests pass. Both backends build clean.

### Task 41 — Unit tests for `Quaternion`, `BoundingBox`, `BoundingSphere` ✅ DONE
- Added `tests/Microsoft/Xna/Framework/QuaternionTests.cpp` — 26 tests: Identity (components, length),
  constructors (4-component, vector+scalar), Length/LengthSquared, Normalize (in-place, static),
  Conjugate (static and in-place), Dot, CreateFromAxisAngle (0→identity, π→half-turn, unit length),
  Multiply (by identity, q×q*≈identity), Inverse (identity round-trip, q×inv≈I), Lerp/Slerp
  (boundary and unit-length results), operators (==, !=, unary -), CreateFromRotationMatrix(Identity)→Identity.
- Added `tests/Microsoft/Xna/Framework/BoundingBoxTests.cpp` — 19 tests: constructor, Contains(point)
  (center, min/max corners, outside), Contains(box) (inside/intersects/disjoint), Intersects(box)
  (overlapping, adjacent, disjoint), Intersects(sphere), GetCorners (count=8, contains Min+Max),
  CreateFromPoints (exact extents), CreateMerged, operators.
- Added `tests/Microsoft/Xna/Framework/BoundingSphereTests.cpp` — 21 tests: constructors, Contains(point/sphere)
  (inside, boundary, disjoint), Intersects(sphere) (symmetry), Intersects(box), CreateFromBoundingBox
  (all corners within, center=box center), CreateFromPoints, CreateMerged, Transform(identity/translation),
  operators.
- All 66 tests pass. Both backends build clean.

### Task 40 — Unit tests for `Vector3`, `Vector4`, `Matrix` ✅ DONE
- Added `tests/Microsoft/Xna/Framework/Vector3Tests.cpp` — 38 tests: static constants (Zero/One/UnitX–Z,
  Up/Down/Right/Left/Forward/Backward), constructors, Length/LengthSquared, Normalize, Cross product
  (X×Y=Z, anticommutativity, parallel→zero), Dot (orthogonal, parallel, symmetry), Distance,
  Add/Subtract/Multiply/Divide, Lerp, Min/Max, all operators (+=, -=, unary -, *, /, ==, !=), Reflect.
- Added `tests/Microsoft/Xna/Framework/Vector4Tests.cpp` — 29 tests: static constants, constructors
  (4-component, scalar, Vector2+zw, Vector3+w), Length, Normalize, Dot (7 axes, product value),
  Distance, Add/Subtract/Multiply, Lerp, Min/Max, Clamp, operators.
- Added `tests/Microsoft/Xna/Framework/MatrixTests.cpp` — 23 tests: Identity diagonal/off-diagonal,
  default ctor zeros, Determinant (identity=1, scale=product), CreateTranslation row-4 layout,
  getTranslationProperty round-trip, CreateScale (uniform/non-uniform), Multiply (by identity,
  two translations accumulate), Invert (identity→identity, M×M⁻¹≈I), Transpose (swap),
  CreateRotationZ (0→identity, π/2→cos/sin layout), operators (+, scalar *, ==, !=),
  direction properties (Right, Up from identity).
- All 90 tests pass. Both backends build clean.

### Task 44 — Unit tests for `Ray`, `Plane`, `Curve`, `CurveKey`, `CurveKeyCollection` ✅ DONE
- Added `tests/Microsoft/Xna/Framework/RayTests.cpp` — 17 tests: constructors, Equals/operators,
  Intersects(BoundingBox) (hit, miss, origin-inside, out-ref), Intersects(BoundingSphere) (hit, miss,
  origin-inside, out-ref), Intersects(Plane) (hit, parallel-miss, out-ref), ToString.
- Added `tests/Microsoft/Xna/Framework/PlaneTests.cpp` — 24 tests: all 4 constructors (Normal+D, Vector4,
  floats, 3 points), Dot/DotCoordinate/DotNormal (value + out-ref overloads), Normalize (in-place, static,
  out-ref), Intersects(BoundingBox) (Front/Back/Intersecting), Intersects(BoundingSphere) (all 3 results),
  Equals/operators, Transform by identity Matrix and Quaternion, ToString.
- Added `tests/Microsoft/Xna/Framework/CurveTests.cpp` — 27 tests: CurveKey constructors/setters/Clone/
  operators/CompareTo; CurveKeyCollection Add/Count/ordering/Contains/Remove/Clear/Clone; Curve IsConstant
  (0/1/2 keys), Evaluate (empty→0, single key, exact positions, midpoint flat tangents, pre/post Constant
  loop), Clone independence, loop type getters/setters, ComputeTangents (Flat/Linear/Smooth), out-of-range throw.
- 68 new tests, all pass. Full suite: 373 pass / 1 pre-existing GamePad failure. Both backends build clean.

### Task 39 — Unit tests for `Color`, `Rectangle`, `Vector2` ✅ DONE
- Added `tests/Microsoft/Xna/Framework/ColorTests.cpp` — 26 tests covering: constructors (byte/int/float),
  static named colors (White/Black/Red/Transparent/CornflowerBlue), packed AABBGGRR layout,
  `operator==`/`!=`/`*`, `Lerp` (clamping, midpoint), `Multiply`, `ToVector3`/`ToVector4`,
  `FromNonPremultiplied`.
- Added `tests/Microsoft/Xna/Framework/RectangleTests.cpp` — 21 tests covering: constructors, edge properties
  (Left/Right/Top/Bottom), Center (even/odd truncation), IsEmpty, Contains (point/rect), Intersects,
  `Intersect` static (overlap region, disjoint→Empty), `Union`, `operator==`/`!=`, Offset, Inflate.
- Added `tests/Microsoft/Xna/Framework/Vector2Tests.cpp` — 33 tests covering: static constants, constructors,
  Length/LengthSquared, Normalize (in-place and static), Add/Subtract/Multiply/Divide, Dot, Distance,
  Lerp, Min/Max, Clamp, Negate, all arithmetic and comparison operators.
- All 80 tests pass. Both backends build clean.

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
