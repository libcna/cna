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
- **Clean build** as of commit `93946e5` on branch `develop`.
- Default debug build: `cmake-build-debug/` — backend `VULKAN`.
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
- **Stock Effects:** `AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`,
  `SkinnedEffect`, `SpriteEffect` — none implemented.
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
| incomplete | Stock effects `AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect`, `SpriteEffect` — not implemented |
| incomplete | `OcclusionQuery::Begin()/End()` — stub, always returns `isComplete_=false, pixelCount_=0` |
| incomplete | `Model::Draw` — no effect binding, does nothing useful |
| incomplete | `SpriteFont` — header only, no glyph rendering |
| incomplete | `SpriteBatch::Begin(SpriteSortMode, BlendState)` — ignores `BlendState` |
| confirmed bug | EasyGL `IVertexBufferBackend` always uses 16-byte stride VAO layout, breaks any vertex type other than `VertexPositionColor` |
| incomplete | `DrawUserIndexedPrimitives` — throws `std::runtime_error` |
| incomplete | Vulkan / BGFX backends — `ApplyBlendState / ApplyDepthStencilState / ApplyRasterizerState` are no-ops |
| incomplete | `FillMode::WireFrame` silently ignored (OpenGL ES limitation) |
| incomplete | `GraphicsAdapter` — `DeviceId`, `Revision`, `SubSystemId`, `VendorId` throw `std::logic_error` |
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

### Task 1 — Implement `SpriteEffect`
**Goal:** Port `SpriteEffect` (XNA internal effect used by SpriteBatch) from FNA.
**Files:** `include/.../Graphics/SpriteEffect.hpp`, `src/.../Graphics/SpriteEffect.cpp`.
**Reference:** `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/Effect/StockEffects/SpriteEffect.cs`
**Verify:** `cmake --build cmake-build-debug --target CNA`

### Task 2 — Implement `AlphaTestEffect`
**Goal:** Port `AlphaTestEffect` from FNA — alpha-reference blending effect.
**Files:** `include/.../Graphics/AlphaTestEffect.hpp`, `src/.../Graphics/AlphaTestEffect.cpp`.
**Reference:** FNA `StockEffects/AlphaTestEffect.cs`
**Verify:** `cmake --build cmake-build-debug --target CNA`

### Task 3 — Implement `EnvironmentMapEffect`
**Goal:** Port `EnvironmentMapEffect` — single texture + environment map + fog.
**Files:** `include/.../Graphics/EnvironmentMapEffect.hpp`, `src/.../EnvironmentMapEffect.cpp`.
**Reference:** FNA `StockEffects/EnvironmentMapEffect.cs`
**Verify:** `cmake --build cmake-build-debug --target CNA`

### Task 4 — Implement `DualTextureEffect`
**Goal:** Port `DualTextureEffect` — two-texture blending effect.
**Files:** `include/.../Graphics/DualTextureEffect.hpp`, `src/.../DualTextureEffect.cpp`.
**Reference:** FNA `StockEffects/DualTextureEffect.cs`
**Verify:** `cmake --build cmake-build-debug --target CNA`

### Task 5 — Implement `SkinnedEffect`
**Goal:** Port `SkinnedEffect` — skinned mesh effect with up to 72 bone matrices.
**Files:** `include/.../Graphics/SkinnedEffect.hpp`, `src/.../SkinnedEffect.cpp`.
**Reference:** FNA `StockEffects/SkinnedEffect.cs`
**Verify:** `cmake --build cmake-build-debug --target CNA`

### Task 6 — Fix EasyGL vertex buffer stride
**Goal:** Make `EasyGLVertexBufferBackend::SetData` configure the VAO layout from
`stride_in_bytes` rather than hard-coding 16 bytes. This unblocks `VertexPositionColorTexture`
and `VertexPositionNormalTexture` in 3D rendering.
**Files:** `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp` —
`EasyGLVertexBufferBackend::InitializeLayout()` and `SetData()`.
**Verify:** Manual draw with `VertexPositionColorTexture` in EasyGL demo.

### Task 7 — Wire `SpriteBatch::Begin(SpriteSortMode, BlendState)` to backend
**Goal:** Forward `BlendState` to `GraphicsDevice::setBlendStateProperty` so that
`Begin(SpriteSortMode::Immediate, BlendState::Additive)` actually applies the blend mode.
**Files:** `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp`
**Verify:** `cmake --build cmake-build-debug --target CNA`

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

The next task is Task 1 from section 8: implement SpriteEffect in CNA.

Steps:
1. Read the FNA reference: /rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/Effect/StockEffects/SpriteEffect.cs
2. Read the existing Effect base class: include/Microsoft/Xna/Framework/Graphics/Effect.hpp
3. Create include/Microsoft/Xna/Framework/Graphics/SpriteEffect.hpp and
   src/Microsoft/Xna/Framework/Graphics/SpriteEffect.cpp following CLAUDE.md rules.
4. Build: cmake --build cmake-build-debug --target CNA
5. Fix any errors, then update NEXT.md to reflect the completed task.

Do not refactor any unrelated code. Do not add features beyond what SpriteEffect requires.
```
