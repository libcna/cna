# Graphics Namespace — XNA 4.0 Coverage Audit

> Date: 2026-06-13  
> Branch: develop  
> FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/`  
> CNA headers: `include/Microsoft/Xna/Framework/Graphics/`

---

## Coverage Summary

| Metric | Count |
|--------|-------|
| FNA public types (Graphics + subdirs) | 114 |
| Present in CNA | 113 (99.1 %) |
| Missing from CNA | 1 public type |
| Internal FNA types (not needed in CNA) | 9 |
| CNA-only extensions (NOXNA) | 1 (`ShaderEffect`) |

---

## FNA Subdirectories vs CNA

| FNA subdirectory | FNA public types | CNA .hpp files | Status |
|------------------|-----------------|----------------|--------|
| `Graphics/` (root) | 47 | 46 | EffectMaterial missing |
| `Graphics/Effect/` | 15 | 15 | complete |
| `Graphics/Effect/StockEffects/` | 6 | 6 | complete |
| `Graphics/PackedVector/` | 18 | 18 | complete (+ HalfTypeHelper) |
| `Graphics/States/` | 12 | 12 | complete |
| `Graphics/Vertices/` | 16 | 16 | complete |

---

## Missing Public Type

| FNA file | Class | Priority | Notes |
|----------|-------|----------|-------|
| `Effect/EffectMaterial.cs` | `EffectMaterial` | Low | Trivial subclass of `Effect`; FNA uses it internally for material cloning. No games use it directly. One-liner stub is sufficient. |

---

## Internal FNA Types (no CNA equivalent needed)

These are FNA implementation details, not part of the XNA 4.0 public API:

| FNA file | Reason not ported |
|----------|-------------------|
| `DxtUtil.cs` | Internal DXT decompression — CNA does CPU decoding inline |
| `FNA3D.cs` | Internal P/Invoke wrapper for FNA3D native lib — CNA uses own backends |
| `PipelineCache.cs` | Internal pipeline state cache — backend-internal concern |
| `ProfileCapabilities.cs` | Internal capability detection |
| `X360TexUtil.cs` | Xbox 360 texture format conversion — not a target platform |
| `Effect/Resources.cs` | Embedded compiled shader binaries — CNA uses GLSL/SPIR-V |
| `Effect/EffectHelpers.cs` | Internal compilation utilities |
| `Effect/StockEffects/EffectHelpers.cs` | Same as above |
| `Vertices/VertexDeclarationCache.cs` | Internal metadata cache |

---

## Key Class Completeness Spot-Check

| Class | FNA public members | CNA header lines | Notes |
|-------|--------------------|-----------------|-------|
| `GraphicsDevice` | 58 public members | 631 lines | Appears complete; see backend gaps below |
| `SpriteBatch` | 6 `Draw` + 6 `DrawString` + `Begin`/`End` | 383 lines | Appears complete |
| `BasicEffect` | ~39 properties/methods | 340 lines | Complete |
| `Texture2D` | ~40 members | 262 lines | Complete |
| `Effect` | ~15 members | 111 lines | Complete |
| `Model` | 4 properties + 4 methods | 109 lines | Complete |
| `SpriteFont` | 4 properties + 2 `MeasureString` | Present | Complete |
| `BlendState` | ~14 properties | 175 lines | Complete |
| `VertexBuffer` | ~16 members | 117 lines | Complete |
| `OcclusionQuery` | Begin/End/IsComplete/PixelCount | Present | Complete |

---

## Backend Overview

### SDL_Renderer (`SdlGraphicsBackend`)
- **API**: SDL3 `SDL_Renderer`
- **Target**: 2D games, desktop (Windows/Linux/macOS)
- **Implements**:
  - `ISpriteBatchBackend` — full sprite rendering
  - `ITextureBackend` — SDL_Texture upload
  - `IVertexBufferBackend` / `IIndexBufferBackend` — stubs only (throw on draw)
  - `IGraphicsBackend::Clear`, `Present`, logical presentation/scaling
- **Does NOT implement**: 3D draw calls, render targets, blend/depth/raster state, occlusion queries

### EasyGL (`EasyGLGraphicsBackend`)
- **API**: OpenGL ES 3.0 via MetaGL; Emscripten/WebGL support
- **Target**: Web (Emscripten), mobile, desktop GL
- **Implements**:
  - Full `IGraphicsBackend` including all 3D draw calls
  - `IVertexBufferBackend` / `IIndexBufferBackend` (16-bit and 32-bit)
  - `ITextureBackend` with `UpdatePixels`, `UpdatePixelsLevel`, context-loss recovery
  - `IRenderTargetBackend` — off-screen FBO
  - `IOcclusionQueryBackend` — GL_ANY_SAMPLES_PASSED
  - `ApplyBlendState`, `ApplyDepthStencilState`, `ApplyRasterizerState`
  - `DrawPrimitivesEx`, `DrawIndexedPrimitivesEx`, `DrawInstancedPrimitivesEx`
  - `ReadBackbuffer` (pixel readback)
  - GLSL shader variants via `GpuDrawParams` (BasicEffect-equivalent)
- **Most complete backend**

### Vulkan (`VulkanGraphicsBackend`)
- **API**: Vulkan
- **Target**: Desktop (high-performance)
- **Implements**:
  - `IVertexBufferBackend` / `IIndexBufferBackend` (16-bit and 32-bit)
  - `ITextureBackend` — Vulkan image upload
  - `IRenderTargetBackend` — render-to-texture
  - `DrawIndexedColoredPrimitives` — indexed draw
  - `ApplyBlendState`, `ApplyDepthStencilState`, `ApplyRasterizerState`
- **Does NOT implement**: `DrawPrimitivesEx`, `DrawInstancedPrimitivesEx`, `ReadBackbuffer`, occlusion queries, `ISpriteBatchBackend`

### Bgfx (`BgfxGraphicsBackend`)
- **API**: bgfx (abstracts GL/D3D11/Metal/Vulkan)
- **Target**: Cross-platform
- **Implements**:
  - `IVertexBufferBackend` / `IIndexBufferBackend` — stubs (created but not functional)
  - `ITextureBackend` — bgfx texture upload
  - `DrawIndexedColoredPrimitives` — stub/partial
- **Least complete backend; effectively prototype-level**

---

## Gaps in `IGraphicsBackend` Interface

The following XNA 4.0 GraphicsDevice features have **no counterpart in IGraphicsBackend** and would require interface additions + per-backend implementation:

| Missing feature | Affects XNA API | Backend work needed |
|-----------------|----------------|---------------------|
| `CreateTexture3D` | `Texture3D.SetData` | New `ITexture3DBackend` or extended `ITextureBackend` |
| `CreateTextureCube` | `TextureCube.SetData` | New `ITextureCubeBackend` |
| `CreateRenderTargetCube` | `GraphicsDevice.SetRenderTarget(cube, face)` | New `IRenderTargetCubeBackend` |
| Multiple render targets | `GraphicsDevice.SetRenderTargets(…)` | `IGraphicsBackend::SetRenderTargets` array overload |
| `SetScissorRect` | `GraphicsDevice.ScissorRectangle` | `IGraphicsBackend::SetScissorRect(x,y,w,h)` |
| `ApplySamplerState` | `GraphicsDevice.SamplerStates[i]` | Per-slot sampler state (wrap, filter, aniso) |
| `ApplyBlendFactor` | `GraphicsDevice.BlendFactor` | `IGraphicsBackend::SetBlendFactor(r,g,b,a)` |
| `ApplyReferenceStencil` | `GraphicsDevice.ReferenceStencil` | Part of depth/stencil apply |
| `GetBackBufferData` | `GraphicsDevice.GetBackBufferData<T>` | `ReadBackbuffer` exists only in EasyGL |
| Custom `Effect` execution | `Effect.Apply()` / shader programs | `IEffectBackend` not defined; only `GpuDrawParams` for BasicEffect-equivalent shaders |
| `DrawUserPrimitives<T>` | `GraphicsDevice.DrawUserPrimitives` | Client-side VBO upload per-call; backend needs `UpdateVertexData` |
| `DrawUserIndexedPrimitives<T>` | `GraphicsDevice.DrawUserIndexedPrimitives` | Same as above + index upload |
| `DrawColoredPrimitives` (non-indexed) | `GraphicsDevice.DrawPrimitives` | EasyGL only; Vulkan/SDL stubs |

---

## Per-Backend Gap Table

| Feature | SDL_Renderer | EasyGL | Vulkan | Bgfx |
|---------|:---:|:---:|:---:|:---:|
| 2D SpriteBatch | ✓ | ✓ | ✗ | ✗ |
| 3D draw (VBO+IBO) | ✗ | ✓ | ✓ | stub |
| DrawPrimitivesEx (BasicEffect params) | ✗ | ✓ | ✗ | ✗ |
| DrawInstancedPrimitives | ✗ | ✓ | ✗ | ✗ |
| DrawUserPrimitives | ✗ | ✗ | ✗ | ✗ |
| Render target 2D | ✗ | ✓ | ✓ | ✗ |
| Render target Cube | ✗ | ✗ | ✗ | ✗ |
| Texture2D upload | ✓ | ✓ | ✓ | ✓ |
| Texture3D upload | ✗ | ✗ | ✗ | ✗ |
| TextureCube upload | ✗ | ✗ | ✗ | ✗ |
| Texture GetData (readback) | ✗ | ✓ (backbuffer) | ✗ | ✗ |
| BlendState | ✗ | ✓ | ✓ | ✗ |
| DepthStencilState | ✗ | ✓ | ✓ | ✗ |
| RasterizerState | ✗ | ✓ | ✓ | ✗ |
| SamplerState per slot | ✗ | ✗ | ✗ | ✗ |
| ScissorRectangle | ✗ | ✗ | ✗ | ✗ |
| OcclusionQuery | ✗ | ✓ | ✗ | ✗ |
| Custom Effect/GLSL | ✗ | partial (ShaderEffect) | ✗ | ✗ |
| Context-loss recovery | N/A | ✓ | N/A | N/A |

---

## Recommended Next Steps (Graphics)

### High priority (needed for most games)
1. **`DrawUserPrimitives<T>` / `DrawUserIndexedPrimitives<T>`** — add `UpdateVertexData` / `UpdateIndexData` to `IVertexBufferBackend` / `IIndexBufferBackend`; implement on EasyGL + Vulkan.
2. **`SamplerState` application** — add `ApplySamplerState` to `IGraphicsBackend`; EasyGL + Vulkan.
3. **`ScissorRectangle`** — add `SetScissorRect` to `IGraphicsBackend`; EasyGL + Vulkan.
4. **Custom `Effect` execution** — define `IEffectBackend`; EasyGL GLSL path already partially works via `ShaderEffect`.

### Medium priority
5. **`Texture3D`** — add `ITexture3DBackend` or extend `ITextureBackend`; EasyGL.
6. **`TextureCube`** — add `ITextureCubeBackend`; EasyGL.
7. **`RenderTargetCube`** — add `IRenderTargetCubeBackend`; EasyGL.
8. **Multiple render targets** — add `SetRenderTargets` array overload to `IGraphicsBackend`; EasyGL.

### Low priority
9. **`EffectMaterial` stub** — one-liner subclass of `Effect`, no backend work.
10. **Bgfx backend** — bring up to parity with Vulkan/EasyGL.
11. **Vulkan `DrawPrimitivesEx`** — implement `GpuDrawParams`-aware shader selection.
12. **SDL_Renderer 3D stubs → proper errors** — replace silent no-ops with `std::runtime_error`.
