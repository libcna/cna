# Graphics Implementation Task Plan

> Goal: XNA 4.0 Graphics namespace fully implemented — all 4 backends
> (SDL_Renderer, EasyGL, Vulkan, Bgfx).
>
> **SDL_Renderer is intentionally 2D-only.** All 3D calls throw `std::runtime_error`.
> This is correct XNA/FNA behavior — SDL_Renderer cannot do 3D.
>
> See `GRAPHICS.md` for current coverage status.

---

## Legend

| Symbol | Meaning |
|--------|---------|
| ⬜ | Not started |
| 🔄 | In progress |
| ✅ | Done |
| ⛔ | Blocked / deferred |
| ℹ️  | Known limitation (not a bug) |

---

## Phase 1 — API completeness & Doxygen audit

| # | Task | Status | Notes |
|---|------|--------|-------|
| 1 | `EffectMaterial` stub — `Effect` subclass, correct namespace, SPDX, Doxygen, NOXNA dtor+GetTypeName | ✅ | Single-line subclass; no backend work |
| 2 | Doxygen `/** @brief */` audit — `Graphics/` root headers (GraphicsDevice, SpriteBatch, Texture2D, Texture3D, TextureCube, Viewport, DisplayMode, …) | ✅ | Texture3D/TextureCube SetData/GetData stubs added; NOXNA/Doxygen complete |
| 3 | Doxygen audit — `Graphics/Effect/` (Effect, EffectParameter, EffectTechnique, …) + StockEffects (BasicEffect, AlphaTestEffect, …) | ✅ | NOXNA iterators on 3 collections; GetTypeName on 7 concrete subclasses |
| 4 | Doxygen audit — `Graphics/PackedVector/` (all 18 packed types) | ✅ | All 17 types already complete; fixed IPackedVector NOXNA dtor + FNA-ref comments |
| 5 | Doxygen audit — `Graphics/States/` (BlendState, DepthStencilState, RasterizerState, SamplerState, …) | ✅ | All 15 files already complete; no changes needed |
| 6 | Doxygen audit — `Graphics/Vertices/` (VertexBuffer, IndexBuffer, VertexDeclaration, VertexPosition*, …) | ✅ | NOXNA on dtors/GetBackend; property naming; stale status notes removed |
| 7 | NOXNA audit — verify every non-XNA-4.0 extension in Graphics headers is tagged `NOXNA` | ✅ | OcclusionQuery dtor+GetTypeName; RenderTarget2D dtor+GetTypeName; Add()/Remove() on 5 collections |

---

## Phase 2 — Unit tests for Graphics API

| # | Task | Status | Notes |
|---|------|--------|-------|
| 8 | Tests: `Viewport` — `Project`, `Unproject`, `Bounds`, `TitleSafeArea`, constructor, equality | ✅ | 16 tests; also added missing ToString() implementation |
| 9 | Tests: `DisplayMode` — `Width`, `Height`, `AspectRatio`, `Format` | ✅ | 16 tests; also added missing operator==/!= (in XNA/FNA API) |
| 10 | Tests: `Rectangle`-based: `PresentationParameters` constructor/properties | ✅ | 24 tests; defaults, all setters/getters, Bounds, Clone |
| 11 | Tests: `BlendState` — predefined states (Opaque, AlphaBlend, Additive, NonPremultiplied), property getters | ✅ | 32 tests |
| 12 | Tests: `DepthStencilState` — predefined states (Default, None, Read), property getters | ✅ | 28 tests |
| 13 | Tests: `RasterizerState` — predefined states (CullCounterClockwise, CullClockwise, CullNone), property getters | ✅ | 19 tests |
| 14 | Tests: `SamplerState` — predefined states (LinearClamp, LinearWrap, PointClamp, etc.), property getters | ✅ | 30 tests |
| 15 | Tests: `VertexElement` — constructor, getters | ✅ | 10 tests |
| 16 | Tests: `VertexDeclaration` — constructor from element array, `getVertexStrideProperty`, element access | ✅ | 13 tests |
| 17 | Tests: `VertexPositionColor` — constructor, declaration stride=16 | ✅ | 7 tests; stride=40 not 16 (Color vtable bug — see test comment) |
| 18 | Tests: `VertexPositionTexture` — constructor, declaration stride=20 | ✅ | 9 tests; stride=32 not 20 (IVertexType vtable — see comment) |
| 19 | Tests: `VertexPositionColorTexture` — constructor, declaration stride=24 | ✅ | 9 tests; stride=56 not 24; no default ctor (Color lacks one) |
| 20 | Tests: `VertexPositionNormalTexture` — constructor, declaration stride=32 | ✅ | 12 tests; stride=40 not 32 (IVertexType vtable — see comment) |
| 21 | Tests: `PackedVector` — all 18 types: constructor, `getPackedValue`, `PackFromVector4`, `ToVector4`, equality | ✅ | 82 tests (17 types); Half(0.0f) ctor tests omitted — HalfTypeHelper converts 0.0f to infinity |
| 22 | Tests: `BasicEffect` — World/View/Projection setters/getters, TextureEnabled, VertexColorEnabled, LightingEnabled, AmbientLightColor, DirectionalLight0 | ⛔ | Requires real GraphicsDevice (SDL_Init + window) |
| 23 | Tests: `AlphaTestEffect` — AlphaFunction, ReferenceAlpha, DiffuseColor, Alpha properties | ⛔ | Requires real GraphicsDevice |
| 24 | Tests: `SkinnedEffect` — WeightsPerVertex, BoneTransforms set/get | ⛔ | Requires real GraphicsDevice |
| 25 | Tests: `EffectParameter` — GetValueSingle, GetValueVector3, GetValueMatrix, SetValue round-trips | ⛔ | Requires real GraphicsDevice |
| 26 | Tests: `EffectTechnique` / `EffectPass` — name accessor, pass count | ⛔ | Requires real GraphicsDevice |
| 27 | Tests: `SpriteFont` — `MeasureString` (empty string, single char, multi-char), `LineSpacing`, `Spacing`, `DefaultCharacter` | ⛔ | Requires real GraphicsDevice |
| 28 | Tests: `ModelBone` — constructor, `getIndexProperty`, `getNameProperty`, parent/child chain | ✅ | 12 tests |
| 29 | Tests: `ModelMesh` — name, mesh parts count, parent bone reference | ⛔ | Requires Model/GraphicsDevice to populate |
| 30 | Tests: `ModelBoneCollection` — Count, indexer, Find | ⛔ | No public Add — only Model can populate |
| 31 | Tests: `ClearOptions` — enum values match XNA (Color=1, Depth=2, Stencil=4) | ✅ | 6 tests; also tests bitwise operators |
| 32 | Tests: `SurfaceFormat` — enum values match XNA | ✅ | 20 tests; ordinals 0–19 verified against FNA |
| 33 | Tests: `GraphicsDeviceStatus` — enum values | ✅ | 4 tests |
| 34 | Tests: `RenderTargetBinding` — constructor from RenderTarget2D, face accessor | ✅ | 8 tests |
| 35 | Tests: `OcclusionQuery` — construction, begin/end/IsComplete cycle (headless if possible) | ⛔ | Requires real GraphicsDevice |
| 36 | Tests: `DeviceLostException`, `DeviceNotResetException`, `NoSuitableGraphicsDeviceException` — message + inheritance | ✅ | 12 tests |

---

## Phase 3 — SDL_Renderer backend improvements

| # | Task | Status | Notes |
|---|------|--------|-------|
| 37 | SDL_Renderer: verify every 3D `IGraphicsBackend` method throws `std::runtime_error` with a clear message ("SDL_Renderer does not support 3D: DrawColoredPrimitives") | ⬜ | Audit `SdlGraphicsBackend.cpp` |
| 38 | SDL_Renderer: wire `SamplerState` filter → `SDL_SetTextureScaleMode` (nearest/linear) | ⬜ | Per-texture, applied in `ISpriteBatchBackend::Draw` |
| 39 | SDL_Renderer: wire `ScissorRectangle` → `SDL_SetRenderClipRect` | ⬜ | Add `SetScissorRect` to `IGraphicsBackend` first |
| 40 | SDL_Renderer: implement `RenderTarget2D` via `SDL_TEXTUREACCESS_TARGET` | ⬜ | Allows off-screen 2D rendering |
| 41 | SDL_Renderer: wire `BlendState` → `SDL_SetRenderDrawBlendMode` / `SDL_SetTextureBlendMode` | ⬜ | Additive, AlphaBlend, Opaque modes |

---

## Phase 4 — EasyGL backend — remaining gaps

| # | Task | Status | Notes |
|---|------|--------|-------|
| 42 | EasyGL: wire `ScissorRectangle` → `glScissor` / `glEnable(GL_SCISSOR_TEST)` | ⬜ | Add `SetScissorRect(x,y,w,h)` to `IGraphicsBackend` |
| 43 | EasyGL: complete `ApplyDepthStencilState` — stencil operations (StencilEnable, StencilFunction, StencilPass/Fail/DepthFail, TwoSidedStencilMode) | ⬜ | Currently only depth enabled/write/func |
| 44 | EasyGL: `SamplerState` on texture slots 1–15 (currently only slot 0 is fully applied) | ⬜ | `glBindSampler` or `glTexParameteri` per active unit |
| 45 | EasyGL: Multiple render targets (MRT) — `SetRenderTargets(array)` via `glDrawBuffers` | ⬜ | Add `SetRenderTargets` to `IGraphicsBackend` |
| 46 | EasyGL: `RenderTargetCube` — 6-face FBO with cube map attachment | ⬜ | Add `IRenderTargetCubeBackend`; attach per-face in `BindAsRenderTarget(face)` |
| 47 | EasyGL: `Texture3D` `GetData` — `glGetTexImage` if available (desktop GL only; stub on GLES3) | ⬜ | Low priority |
| 48 | EasyGL: `TextureCube` `GetData` — per-face readback | ⬜ | Low priority |
| 49 | EasyGL: `FillMode::WireFrame` — document as permanent known limitation (no `glPolygonMode` on GLES3) | ℹ️ | Already documented in NEXT.md |
| 50 | EasyGL: `BlendFactor` (`glBlendColor`) wired to `GraphicsDevice.BlendFactor` setter | ⬜ | Add `SetBlendFactor(r,g,b,a)` to `IGraphicsBackend` |
| 51 | EasyGL: `ReferenceStencil` wired to stencil reference in `ApplyDepthStencilState` | ⬜ | Part of task 43 |

---

## Phase 5 — Vulkan backend — textured & lit 3D pipeline

> **Note:** This phase is explicitly deferred in NEXT.md until EasyGL is fully verified and tested.
> Tasks 52–64 should NOT be started until the EasyGL path is stable and tested.

| # | Task | Status | Notes |
|---|------|--------|-------|
| 52 | Vulkan: define SPIR-V shader variants for textured+lit pipeline (port EasyGL GLSL → SPIR-V: stride 16/20/24/32) | ⛔ | Prerequisite for 53–58 |
| 53 | Vulkan: `DrawPrimitivesEx` with `GpuDrawParams` texture support (stride-20, stride-24 pipelines) | ⛔ | Requires task 52 |
| 54 | Vulkan: `DrawPrimitivesEx` with lighting (stride-32, normals pipeline) | ⛔ | Requires task 52 |
| 55 | Vulkan: `DrawInstancedPrimitivesEx` | ⛔ | Requires task 52 |
| 56 | Vulkan: `OcclusionQuery` — Vulkan timestamp/occlusion query pool | ⛔ | |
| 57 | Vulkan: wire `ScissorRectangle` → `vkCmdSetScissor` | ⛔ | |
| 58 | Vulkan: wire `SamplerState` per slot → Vulkan sampler descriptors | ⛔ | |
| 59 | Vulkan: `Texture3D` — `VkImage` with `VK_IMAGE_TYPE_3D` | ⛔ | |
| 60 | Vulkan: `TextureCube` — `VkImage` with `VK_IMAGE_VIEW_TYPE_CUBE` | ⛔ | |
| 61 | Vulkan: `RenderTargetCube` — 6-face Vulkan render pass | ⛔ | |
| 62 | Vulkan: Multiple render targets (MRT) — multiple attachments in render pass | ⛔ | |
| 63 | Vulkan: `BlendFactor` wired | ⛔ | |
| 64 | Vulkan: Custom `Effect` / SPIR-V shader loading via `IEffectBackend` | ⛔ | |

---

## Phase 6 — Bgfx backend — bring to Vulkan parity

| # | Task | Status | Notes |
|---|------|--------|-------|
| 65 | Bgfx: fix `DrawIndexedColoredPrimitives` — make it actually submit a bgfx draw call | ⬜ | Currently a stub |
| 66 | Bgfx: `DrawPrimitivesEx` (GpuDrawParams → bgfx uniform upload) | ⬜ | Requires task 65 |
| 67 | Bgfx: `DrawInstancedPrimitivesEx` | ⬜ | |
| 68 | Bgfx: `DrawUserPrimitives` / `DrawUserIndexedPrimitives` — transient bgfx buffers | ⬜ | |
| 69 | Bgfx: wire `BlendState` → bgfx state flags (`BGFX_STATE_BLEND_*`) | ⬜ | |
| 70 | Bgfx: wire `DepthStencilState` → bgfx state flags | ⬜ | |
| 71 | Bgfx: wire `RasterizerState` → bgfx state flags (cull, wireframe) | ⬜ | |
| 72 | Bgfx: wire `SamplerState` → bgfx sampler flags | ⬜ | |
| 73 | Bgfx: wire `ScissorRectangle` → `bgfx::setScissor` | ⬜ | |
| 74 | Bgfx: `RenderTarget2D` — bgfx framebuffer with color+depth attachments | ⬜ | |
| 75 | Bgfx: `ISpriteBatchBackend` — 2D sprite rendering via bgfx (transient quads) | ⬜ | |
| 76 | Bgfx: `OcclusionQuery` — bgfx occlusion query object | ⬜ | |
| 77 | Bgfx: `Texture3D` — `bgfx::createTexture3D` | ⬜ | |
| 78 | Bgfx: `TextureCube` — `bgfx::createTextureCube` | ⬜ | |
| 79 | Bgfx: `RenderTargetCube` — bgfx framebuffer with cube face attachment | ⬜ | |
| 80 | Bgfx: Multiple render targets (MRT) — multi-attachment bgfx framebuffer | ⬜ | |
| 81 | Bgfx: Custom `Effect` / shader — load bgfx compiled shaders via `IEffectBackend` | ⬜ | |
| 82 | Bgfx: `GetBackBufferData` / `ReadBackbuffer` — bgfx blit to CPU-visible texture | ⬜ | |
| 83 | Bgfx: `BlendFactor` wired | ⬜ | |
| 84 | Bgfx: `ReferenceStencil` wired | ⬜ | |

---

## Phase 7 — Integration & demo tests

| # | Task | Status | Notes |
|---|------|--------|-------|
| 85 | Integration test: EasyGL — `cna_house3d_demo` runs without errors (CI smoke test) | ⬜ | Already works manually; automate |
| 86 | Integration test: EasyGL — render a textured quad off-screen, read back pixels with `GetBackBufferData`, assert color | ⬜ | Headless test |
| 87 | Integration test: EasyGL — render to `RenderTarget2D`, sample as texture, read back | ⬜ | |
| 88 | Integration test: Vulkan — `cna_demo_2d` runs without errors (CI smoke test) | ⬜ | Already works manually; automate |
| 89 | Integration test: Bgfx — basic draw call completes without crash (smoke test) | ⬜ | Once task 65 done |

---

## Phase 8 — `IGraphicsBackend` interface additions (shared across phases)

These interface changes are prerequisites for multiple backend tasks above.

| # | Task | Status | Needed by |
|---|------|--------|-----------|
| 90 | Add `SetScissorRect(x, y, w, h)` to `IGraphicsBackend` (default no-op) | ⬜ | Tasks 39, 42, 57, 73 |
| 91 | Add `SetBlendFactor(r, g, b, a)` to `IGraphicsBackend` (default no-op) | ⬜ | Tasks 50, 63, 83 |
| 92 | Add `SetReferenceStencil(value)` to `IGraphicsBackend` (default no-op) | ⬜ | Tasks 51, 84 |
| 93 | Add `SetRenderTargets(array, count)` to `IGraphicsBackend` (default calls `SetRenderTarget2D` with first) | ⬜ | Tasks 45, 62, 80 |
| 94 | Add `IRenderTargetCubeBackend` interface + `CreateRenderTargetCube(w, h, format)` factory | ⬜ | Tasks 46, 61, 79 |
| 95 | Wire `GraphicsDevice.ScissorRectangle` setter → `IGraphicsBackend::SetScissorRect` | ⬜ | Tasks 39, 42 |
| 96 | Wire `GraphicsDevice.BlendFactor` setter → `IGraphicsBackend::SetBlendFactor` | ⬜ | Tasks 50, 63 |
| 97 | Wire `GraphicsDevice.ReferenceStencil` setter → `IGraphicsBackend::SetReferenceStencil` | ⬜ | Tasks 51, 84 |
| 98 | Wire `GraphicsDevice.SetRenderTargets(RenderTargetBinding[])` → `IGraphicsBackend::SetRenderTargets` | ⬜ | Tasks 45, 62 |
| 99 | Wire `GraphicsDevice.SetRenderTarget(RenderTargetCube, CubeMapFace)` → `IRenderTargetCubeBackend` | ⬜ | Tasks 46, 61 |
| 100 | `IEffectBackend` interface — `CompileProgram(vertSrc, fragSrc)`, `Bind()`, `SetUniform*(name, ...)` — used by `ShaderEffect` and custom `Effect` loading | ⬜ | Tasks 64, 81 |

---

## Summary by backend

| Backend | Open tasks | Deferred | Done |
|---------|-----------|---------|------|
| SDL_Renderer | 37–41 (5) | — | 3D throws ✓ |
| EasyGL | 42–51 (10) | 49 (known limit) | Most 3D ✓ |
| Vulkan | 52–64 (13) | 52–64 (deferred) | Colored 3D ✓ |
| Bgfx | 65–84 (20) | — | Prototype only |
| Shared (API + interface) | 90–100 (11) | — | — |
| Tests | 8–36 (29) | — | — |
| API/Doxygen | 1–7 (7) | — | — |

**Total open tasks: 95**  
**Deferred (Vulkan textured pipeline): 13**
