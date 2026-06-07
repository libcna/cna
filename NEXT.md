# NEXT.md — CNA project handoff

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model, built on SDL3 with a
pluggable graphics backend layer. It is a framework/runtime, not a game. The goal is to
preserve the `Microsoft::Xna::Framework` public API exactly as defined by FNA while using
modern C++ internals.

**Authoritative API reference:** `/rv/data/library/github.com/FNA-XNA/FNA/src`

**Current phase:** Active API porting. Core infrastructure is stable; stock effects and
content loading are the next major missing pieces.

**Key architectural decisions:**
- Backend selected at compile-time via `CNA_GRAPHICS_BACKEND` CMake option (`SDL_RENDERER` |
  `EASYGL` | `VULKAN` | `BGFX`). The default debug build uses `VULKAN`.
- `EasyGL` is the primary 3D-capable backend (OpenGL ES 3.0). SDL_Renderer throws for all
  3D operations by design. Vulkan backend exists but most new state methods are no-ops there.
- C# properties become `getXProperty()` / `setXProperty()` pairs. This convention is strict.
- All .NET primitives use SharpRuntime aliases (`bytecs`, `intcs`, `Single`, `String`, …).
- Non-XNA extensions inside the `Microsoft::Xna` namespace are tagged with `NOXNA`.

---

## 2. Current status

### Build
- **Clean build** on branch `develop` (after SpriteEffect addition).
- Default debug build: `cmake-build-vulkan/` — backend `VULKAN`.
- Libraries built: `libCNA.a`, `libcna_backend_graphics_vulkan.a`.
- Executables built: `CnaTests`, `cna_demo_2d`, `cna_demo_input`, `cna_demo_sound`,
  `cna_demo_xact`, `cna_house3d_demo`.

### Tests
- `CnaTests` compiles and links cleanly after fixing `GamePadInputTests.cpp` in last session.
- No known failing tests at last build.

### What works
- **Graphics core:** `GraphicsDevice`, `SpriteBatch`, `Texture2D`, `Texture3D`, `TextureCube`,
  `VertexBuffer`, `IndexBuffer`, `DynamicVertexBuffer`, `DynamicIndexBuffer`.
- **Effect hierarchy:** `Effect` → `GraphicsResource`, `EffectTechnique`, `EffectPass`,
  `EffectParameter` with full GetValue/SetValue overloads, `EffectAnnotation`, all collections.
- **BasicEffect** with `IEffectMatrices`, `IEffectFog`, `IEffectLights` (inline overrides on
  public `World`/`View`/`Projection` members for backward compatibility).
- **SpriteEffect** — derives from `Effect`; builds orthographic projection matrix in `OnApply()`
  for use by `SpriteBatch`.
- **Vertex types:** `VertexPositionColor`, `VertexPositionColorTexture`,
  `VertexPositionNormalTexture`, `VertexPositionTexture`.
- **PackedVector:** 18 types — `Alpha8`, `Bgr565`, `Bgra4444`, `Bgra5551`, `Byte4`,
  `HalfSingle`, `HalfVector2`, `HalfVector4`, `HalfTypeHelper`, `NormalizedByte2/4`,
  `NormalizedShort2/4`, `Rg32`, `Rgba1010102`, `Rgba64`, `Short2`, `Short4`.
- **State objects:** `BlendState`, `DepthStencilState`, `RasterizerState`, `SamplerState`,
  `SamplerStateCollection`, `TextureCollection` — stored on `GraphicsDevice`, setters wire
  to `backend_->ApplyBlendState / ApplyDepthStencilState / ApplyRasterizerState`.
- **EasyGL graphics state:** `ApplyBlendState`, `ApplyDepthStencilState`, `ApplyRasterizerState`
  implemented with correct XNA→OpenGL enum mapping.
- **3D rendering:** EasyGL backend — colored primitive drawing, indexed drawing, depth test,
  blend, depth write, WVP matrix upload. `house3d_demo` runs.
- **Input:** Keyboard, Mouse, GamePad (all axes/buttons), Touch, Gesture.
- **Audio:** SoundEffect, SoundEffectInstance, SoundBank/WaveBank (XACT).
- **Video:** FFmpeg-based `VideoPlayer` with per-frame texture update.
- **Storage:** `StorageDevice`, `StorageContainer`.
- **Math:** `Vector2/3/4`, `Matrix`, `Quaternion`, `BoundingBox`, `BoundingSphere`,
  `BoundingFrustum`, `Plane`, `Ray`, `Curve`, `MathHelper`.
- **Model system:** `Model`, `ModelBone`, `ModelMesh`, `ModelMeshPart` — stub-level,
  no asset loading yet.
- **Content:** `ContentManager` + `ContentTypeReader<T>` — custom system (no XNB).

### What does NOT work yet
- **Stock Effects:** All five stock effects implemented: `SpriteEffect`, `AlphaTestEffect`,
  `EnvironmentMapEffect`, `DualTextureEffect`, `SkinnedEffect`.
- **`DrawUserIndexedPrimitives`** — throws `std::runtime_error` (not implemented).
- **`OcclusionQuery`** — stub only, `Begin()`/`End()` are no-ops.
- **`IVertexBufferBackend`** — only handles the 16-byte `VertexPositionColor` stride on EasyGL;
  other vertex layouts require the `VertexPositionColor`-compatible layout or break silently.
- **`Model::Draw`** — implemented but no effect/shader binding; does nothing useful.
- **`SpriteBatch::Begin(SpriteSortMode, BlendState)`** — ignores the `BlendState` parameter
  (does not forward it to `GraphicsDevice`).
- **Vulkan / BGFX backends** — `ApplyBlendState / ApplyDepthStencilState / ApplyRasterizerState`
  are inherited no-ops; no state is actually applied.
- **`FillMode::WireFrame`** — silently ignored in EasyGL (OpenGL ES does not support
  `glPolygonMode`).
- **`SpriteFont`** — header exists; no implementation (font rendering not wired).

---

## 3. Recent changes

### Current session — Implement GraphicsAdapter PCI IDs (Task 9)
- **Modified:** `GraphicsAdapter.hpp` — added private `vendorId_` / `deviceId_` members;
  extended constructor signature to accept them; added `static queryPciIds()` declaration.
- **Modified:** `GraphicsAdapter.cpp` — added `queryPciIds()` which on Linux reads
  `/sys/class/drm/card{0..3}/device/vendor` and `device` (hex) into `vendorId`/`deviceId`;
  falls back to 0 on other platforms or if the files are missing.
  `AdaptersChanged()` calls `queryPciIds()` once and passes the result to every adapter
  constructor (all displays share the same GPU).
  `getDeviceIdProperty()` and `getVendorIdProperty()` now return stored members;
  `getRevisionProperty()` and `getSubSystemIdProperty()` return 0 (not available via SDL3).
- Build: `cmake-build-vulkan` — `libCNA.a` links cleanly.

### Current session — EasyGL shader pipeline (Task 10)
- **Modified:** `IGraphicsBackend.hpp` — added `GpuDrawParams` struct (texture0, diffuseColor,
  ambientColor, light0Dir, light0Diffuse, worldColMajor, textureEnabled, vertexColorEnabled,
  lightingEnabled) + `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` virtual methods with default
  fallback to colored-only path.
- **Modified:** `EasyGLGraphicsBackend.hpp` — replaced single `program3d_` + raw fields with
  `Prog3D` struct (prog + ready + uniform locs); added 4 program instances (colored/textured/
  col+textured/lit+textured), default_white_texture_, helper method declarations.
- **Modified:** `EasyGLGraphicsBackend.cpp` — added `CompileAndLink` helper; implemented
  `EnsureTextured3DProgram`, `EnsureColoredTextured3DProgram`, `EnsureLit3DProgram`,
  `EnsureDefaultWhiteTexture`, `SelectProgram(stride)`, `BindDrawParams`; implemented
  `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` that dispatch to the correct shader by stride;
  updated context-loss handler to reset all 4 programs.
- **Modified:** `GraphicsDevice.cpp` — added `BuildGpuDrawParams(BasicEffect*)` helper that
  reads texture, diffuseColor, alpha, ambientColor, light0 from the current effect; updated
  `DrawPrimitives`/`DrawIndexedPrimitives` to call `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`.
- **Shader variants by stride:**
  - 16 → colored (aPos + aColor → vColor)
  - 20 → textured (aPos + aUV → texture × uDiffuseColor)
  - 24 → col+textured (aPos + aColor + aUV → texture × vColor)
  - 32 → lit+textured (aPos + aNormal + aUV → Phong: ambient + NdotL × light0Diffuse)
- Build: `cmake-build-vulkan` and `cmake-build-easygl` — both `libCNA.a` link cleanly.

### Previous session — Implement DrawUserIndexedPrimitives (Task 8)
- **Modified:** `GraphicsDevice::DrawUserIndexedPrimitives` in `GraphicsDevice.cpp` — replaced
  unconditional `throw` with a real implementation:
  - Computes index count from `primitiveType` + `primitiveCount` (TriangleList×3, Strip+2, etc.)
  - Packs `VertexPositionColor` vertices (with vtable) into compact 16-byte `GpuVertex`
  - Copies 16-bit indices with `indexOffset` applied
  - Creates temporary VB + IB via `backend_->CreateVertexBuffer / CreateIndexBuffer16`
  - Calls `backend_->DrawIndexedColoredPrimitives` with `currentEffect_` matrices
- Build: `cmake-build-vulkan` — `libCNA.a` links cleanly.

### Previous session — Wire SpriteBatch::Begin BlendState (Task 7)
- **Modified:** `SpriteBatch` now stores `GraphicsDevice*` (added `graphicsDevice_` member to
  header; constructor sets it from the constructor parameter).
- **Modified:** `Begin(SpriteSortMode, BlendState)` — forwards `blend_state` to
  `graphicsDevice_->setBlendStateProperty(blend_state)` before calling `Begin()`, so that
  `Begin(SpriteSortMode::Immediate, BlendState::Additive)` now actually applies the blend mode
  via the backend's `ApplyBlendState` path.
- Build: `cmake-build-vulkan` — `libCNA.a` links cleanly.

### Previous session — Fix EasyGL vertex buffer stride (Task 6)
- **Modified:** `EasyGLVertexBufferBackend::InitializeLayout()` — now only creates VBO+VAO
  (no attribute pointers); attribute config deferred to new `ApplyLayout(stride)`.
- **Added:** `EasyGLVertexBufferBackend::ApplyLayout(std::size_t stride)` — stride-dispatch
  configures the VAO for all four built-in vertex layouts:
  - 16 → VertexPositionColor: float3@0 + ubyte4-normalized@12
  - 20 → VertexPositionTexture: float3@0 + float2@12
  - 24 → VertexPositionColorTexture: float3@0 + ubyte4-normalized@12 + float2@16
  - 32 → VertexPositionNormalTexture: float3@0 + float3@12 + float2@24
- **Modified:** `SetData()` calls `ApplyLayout(stride_in_bytes_)` after each upload.
- **Modified:** `recreate_gl_resource()` calls `ApplyLayout(stride_in_bytes_)` after context restore.
- **Added:** `VertexBuffer::SetData` overloads for `VertexPositionColorTexture`,
  `VertexPositionNormalTexture`, `VertexPositionTexture` — each packs the source struct
  (which carries a vtable pointer from `IVertexType`) into the compact GPU layout.
- Build: `cmake-build-vulkan` and `cmake-build-easygl` — both `libCNA.a` link cleanly.

### Previous session — Implement `SkinnedEffect`
- **Added:** `include/Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp` and
  `src/Microsoft/Xna/Framework/Graphics/SkinnedEffect.cpp`.
- Derives from `Effect`, `IEffectMatrices`, `IEffectLights`, `IEffectFog`.
- `MaxBones = 72` public const; `SetBoneTransforms` / `GetBoneTransforms(int count)`.
- `WeightsPerVertex` validates 1/2/4, throws `std::out_of_range` otherwise.
- `LightingEnabled` always returns `true`; setting to `false` throws `std::runtime_error`.
- `OnApply()`: WorldViewProj, fog vector, world+world-inverse-transpose, eye position,
  diffuse+emissive material color (with ambient), one-light opt, shader index
  (fog × weightsPerVertex × perPixel/oneLight = multiple variants matching FNA).
- `GetBoneTransforms` restores `M44 = 1` to mirror FNA's 4x3 bone storage behaviour.
- Texture stored as direct member pointer; specular color/power backed by member + param.
- Build: `cmake-build-vulkan` — `libCNA.a` links cleanly.

### Previous session — Implement `DualTextureEffect` and fix EasyGL vertex stride
- **Added:** `include/Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp` and
  `src/Microsoft/Xna/Framework/Graphics/DualTextureEffect.cpp`.
- Derives from `Effect`, `IEffectMatrices`, `IEffectFog`.
- Exposes: `World/View/Projection`, `DiffuseColor`, `Alpha`, `Texture`, `Texture2`,
  `VertexColorEnabled`, and all fog properties.
- `OnApply()`: dirty-flag lazy recomputation of WorldViewProj, fog vector, diffuse/alpha
  packed color, and shader index (fog + vertexColor flags).
- Both textures stored as direct member pointers (Texture2D does not inherit from Texture).
- Build: `cmake-build-vulkan` — `libCNA.a` links cleanly.

### Previous session — Implement `EnvironmentMapEffect`
- **Added:** `include/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp` and
  `src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp`.
- Derives from `Effect`, `IEffectMatrices`, `IEffectLights`, `IEffectFog`.
- Implements all XNA properties: `World/View/Projection`, `DiffuseColor`, `EmissiveColor`,
  `Alpha`, `AmbientLightColor`, three `DirectionalLight` members, fog properties, `Texture`,
  `EnvironmentMap` (TextureCube), `EnvironmentMapAmount`, `EnvironmentMapSpecular`, `FresnelFactor`.
- `LightingEnabled` always returns `true`; setting to `false` throws `std::runtime_error`.
- `OnApply()`: dirty-flag lazy recomputation of WorldViewProj, fog vector, world/world-inverse-
  transpose, eye position, diffuse+emissive material color, one-light optimisation, shader index.
- Texture/EnvironmentMap stored as direct member pointers (same pattern as AlphaTestEffect).
- Build: `cmake-build-vulkan` — `libCNA.a` links cleanly.

### Previous session — Implement `AlphaTestEffect`
- **Added:** `include/Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp` and
  `src/Microsoft/Xna/Framework/Graphics/AlphaTestEffect.cpp`.
- `AlphaTestEffect` derives from `Effect`, `IEffectMatrices`, `IEffectFog`.
- Implements lazy dirty-flag recomputation of WorldViewProj, fog vector, diffuse/alpha color,
  alpha test vector (all 8 CompareFunction modes), and shader index — matching FNA behavior.
- **Note:** `Texture2D` in CNA does not inherit from `Texture`, so the EffectParameter texture
  storage path is unusable; `Texture` property backed by a direct member pointer instead.
- Build: `cmake-build-vulkan` — `libCNA.a` links cleanly with no errors.

### Previous session — Implement `SpriteEffect`
- **Added:** `include/Microsoft/Xna/Framework/Graphics/SpriteEffect.hpp` and
  `src/Microsoft/Xna/Framework/Graphics/SpriteEffect.cpp`.
- `SpriteEffect` derives from `Effect`, caches the `MatrixTransform` parameter, and in
  `OnApply()` builds an orthographic off-center projection plus −0.5 px half-pixel offset
  matrix matching FNA's behavior exactly.
- Build: `cmake-build-vulkan` — `libCNA.a` links cleanly with no errors.

### Commit `93946e5` — Port Graphics.PackedVector types and wire EasyGL graphics state
- **Added:** 18 PackedVector header-only types under
  `include/Microsoft/Xna/Framework/Graphics/PackedVector/`.
- **Added:** `ApplyBlendState`, `ApplyDepthStencilState`, `ApplyRasterizerState` to
  `IGraphicsBackend` (default no-ops) and implemented in `EasyGLGraphicsBackend` with
  correct XNA→OpenGL enum mapping helpers.
- **Modified:** `GraphicsDevice` setters now call backend `Apply*` immediately on state change.
- **Fixed:** `house3d_demo.cpp` — `CurrentTechnique().Passes()` → `getCurrentTechniqueProperty()->getPassesProperty()`.
- **Fixed:** `GamePadInputTests.cpp` — corrected DPad and trigger access to use proper XNA API.

### Commit `34ae601` — Expand Effect/EffectTechnique API and add vertex type headers
- Effect hierarchy complete: `Effect`, `EffectTechnique`, `EffectPass`, `EffectParameter`,
  `EffectParameterCollection`, `EffectTechniqueCollection`, `EffectPassCollection`,
  `EffectAnnotation`, `EffectAnnotationCollection`.
- Added `VertexPositionColorTexture`, `VertexPositionNormalTexture`, `VertexPositionTexture`.
- Added `DynamicVertexBuffer`, `DynamicIndexBuffer`, `RenderTargetCube`.
- `GraphicsDevice` expanded with render target, scissor, blend factor, stencil, texture
  collection accessors.
- `IGraphicsBackend` extended with default-no-op state methods.

### Commit `6536c4b` — Namespace audit: Framework, Graphics, Input
- Added `KeyboardState(std::initializer_list<Keys>)` constructor matching FNA `params Keys[]`.
- Various API compliance fixes.

---

## 4. Current blocker / main problem

No hard build blocker exists. The next work is **implementing the five missing XNA stock
effects** (`AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect`,
`SpriteEffect`). These are public XNA API types; the FNA source is at:

```
/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/Effect/StockEffects/
```

All prerequisite infrastructure (`Effect`, `IEffectFog`, `IEffectLights`, `IEffectMatrices`,
`DirectionalLight`, `EffectParameter`, `EffectTechnique`, `GraphicsDevice`) is already present.

A secondary limitation: **the EasyGL vertex buffer only works with the VertexPositionColor
stride (16 bytes)**. Other layouts (e.g. `VertexPositionColorTexture` = 24 bytes) would
silently misread vertex data. Fixing this requires the backend to read stride from the
`VertexDeclaration` and configure the VAO layout dynamically.

---

## 5. Known bugs and limitations

| Status | Issue |
|--------|-------|
| done | `SpriteEffect` — implemented (orthographic projection matrix for SpriteBatch) |
| done | `AlphaTestEffect` — implemented (alpha test vector + fog + WVP, all CompareFunction modes) |
| done | `EnvironmentMapEffect` — implemented (env map, fresnel, specular, lighting matrices, fog, one-light opt.) |
| done | `DualTextureEffect` — implemented (two-texture blend, fog, vertex color, diffuse/alpha) |
| done | `SkinnedEffect` — implemented (72 bones, weights 1/2/4, lighting matrices, perPixel/oneLight shader index) |
| incomplete | `OcclusionQuery::Begin()/End()` — stub, always returns `isComplete_=false, pixelCount_=0` |
| incomplete | `Model::Draw` — no effect binding, does nothing useful |
| incomplete | `SpriteFont` — header only, no glyph rendering |
| done | `SpriteBatch::Begin(SpriteSortMode, BlendState)` — forwards `BlendState` to `GraphicsDevice::setBlendStateProperty` |
| done | EasyGL vertex buffer stride — `ApplyLayout(stride)` configures VAO for all four built-in vertex types; `VertexBuffer::SetData` overloads added for all types |
| done | `DrawUserIndexedPrimitives` — implemented: packs VertexPositionColor + 16-bit indices into temp buffers, calls `DrawIndexedColoredPrimitives` |
| incomplete | Vulkan / BGFX backends — `ApplyBlendState / ApplyDepthStencilState / ApplyRasterizerState` are no-ops |
| incomplete | `FillMode::WireFrame` silently ignored (OpenGL ES limitation) |
| done | `GraphicsAdapter` — `VendorId`/`DeviceId` read from `/sys/class/drm/card*/device/` on Linux (fallback 0); `Revision`/`SubSystemId` return 0 (not available via SDL3) |
| unknown | `IndexBuffer` — 32-bit index size (`IndexElementSize::ThirtyTwoBits`) is documented as partial |
| needs verification | `EffectParameter::GetValueTexture2D/Texture3D/TextureCube` — pointer cast to `Texture*`, no type-safety guarantee |

---

## 6. Architecture notes

### Main modules

```
Microsoft::Xna::Framework::           — public XNA API (must match FNA exactly)
  Graphics::                          — GraphicsDevice, textures, effects, vertices
  Graphics::PackedVector::            — 18 packed types (header-only)
  Input::                             — Keyboard, Mouse, GamePad, Touch, Gesture
  Content::                           — ContentManager + ContentTypeReader<T>
  Audio::                             — SoundEffect, SoundBank, WaveBank
  Media::                             — VideoPlayer (FFmpeg), Song, MediaPlayer

CNA::                                 — project-specific internals (NOXNA tagged)
  Internal::Backends::Common::        — IGraphicsBackend, IVertexBufferBackend, …
  Internal::Backends::EasyGL::        — OpenGL ES 3.0 implementation
  Internal::Backends::SDL_Renderer::  — 2D only; throws on all 3D calls
  Internal::Backends::Vulkan::        — skeleton; state methods are no-ops
  Internal::Input::InputManager::     — maps SDL events to XNA Input state
```

### Data flow (3D draw call)
```
Game::Draw()
  → GraphicsDevice::DrawPrimitives()
      → IGraphicsBackend::DrawColoredPrimitives()
          → EasyGLVertexBufferBackend VAO bind + glDrawArrays
```

### State flow
```
GraphicsDevice::setBlendStateProperty(bs)
  → backend_->ApplyBlendState(int…) // EasyGL: glBlendFuncSeparate + glBlendEquationSeparate
                                     // Vulkan/BGFX: no-op
```

### Invariants that must not be broken
- **XNA API names:** Class/method/enum names must match FNA exactly. Never rename for
  C++ aesthetic reasons.
- **Namespace:** Original XNA types stay in `Microsoft::Xna::Framework::*`. `CNA::` is for
  internals only.
- **Property convention:** `getXProperty()` / `setXProperty()` for all C# properties.
- **SharpRuntime types:** Use `bytecs`, `intcs`, `Single`, `String` etc. — never raw C++ types
  in the XNA API surface.
- **GLOB_RECURSE in CMake:** New `.cpp` files are auto-discovered; no CMakeLists edits needed
  for new source files under existing directories.
- **SDL_Renderer 3D:** Must always throw (by contract). Do not add 3D support there.

---

## 7. Useful commands

```bash
# Configure (debug, Vulkan backend — default)
cmake -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug

# Configure (debug, EasyGL backend — for 3D testing)
cmake -B cmake-build-easygl -DCNA_GRAPHICS_BACKEND=EASYGL -DCMAKE_BUILD_TYPE=Debug

# Build library only
cmake --build cmake-build-debug --target CNA

# Build everything
cmake --build cmake-build-debug

# Run tests
./cmake-build-debug/CnaTests

# Run house3d demo (requires EasyGL build)
./cmake-build-easygl/cna_house3d_demo

# Run 2D demo
./cmake-build-debug/cna_demo_2d
```

---

## 8. Next smallest tasks

### ~~Task 1 — Implement `SpriteEffect`~~ **DONE**

### ~~Task 2 — Implement `AlphaTestEffect`~~ **DONE**

### ~~Task 3 — Implement `EnvironmentMapEffect`~~ **DONE**

### ~~Task 4 — Implement `DualTextureEffect`~~ **DONE**

### ~~Task 5 — Implement `SkinnedEffect`~~ **DONE**

### ~~Task 6 — Fix EasyGL vertex buffer stride~~ **DONE**

### ~~Task 7 — Wire `SpriteBatch::Begin(SpriteSortMode, BlendState)` to backend~~ **DONE**

---

## 9. Do not do yet

- **No Vulkan 3D implementation** — the Vulkan backend is a skeleton. Do not add real 3D
  draw calls there until the EasyGL path is fully verified.
- **No XNB content pipeline** — CNA uses its own content system. Do not port FNA's
  `*Reader` content classes (the 40+ `ContentReaders` types) unless a specific game
  integration requires XNB loading.
- **No `Design::*Converter` types** — `TypeConverter` subclasses for IDE tooling have no
  meaning in a C++ runtime port.
- **No FNA platform internals** — `FNA3D`, `FNAPlatform`, `SDL2_FNAPlatform`,
  `SDL3_FNAPlatform`, `GestureDetector`, `DxtUtil`, `X360TexUtil` are FNA-specific
  implementation details, not XNA API.
- **No mass refactor** of existing working code (Effect, GraphicsDevice, EasyGL backend).
- **No API renames** — never rename a method/class for C++ aesthetics if it diverges from FNA.
- **No new CMakeLists edits** for adding `.cpp` files — `GLOB_RECURSE` handles it automatically.
- **No premature Model loading** — `Model::Draw` stub is fine until a proper asset pipeline
  and effect-binding path are designed end-to-end.

---

## 10. Resume prompt

```
Read NEXT.md first to understand the current state of the CNA project.

Tasks 1–7 from section 8 are all complete. There is no pre-defined Task 8 yet.
Consult the "Known bugs and limitations" table in section 5 for the next best target.
Good candidates (incomplete items):
- OcclusionQuery::Begin()/End() — stub-only, always returns isComplete_=false
- SpriteFont — header only, no glyph rendering
- DrawUserIndexedPrimitives — throws std::runtime_error
- Model::Draw — no effect binding
```
