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
| Missing from CNA | 1 public type (`EffectMaterial`) |
| Internal FNA types (not needed in CNA) | 9 |
| CNA-only extensions (NOXNA) | 1 (`ShaderEffect`) |

---

## What works (as of 2026-06-13)

| Feature | EasyGL | Vulkan | SDL_Renderer | Bgfx |
|---------|:------:|:------:|:------------:|:----:|
| 2D SpriteBatch (all sort modes) | ✓ | ✓ | ✓ | stub |
| Texture2D upload/GetData/SetData | ✓ | ✓ | ✓ | ✓ |
| Texture3D | ✓ | stub | ✗ | ✗ |
| TextureCube | ✓ | stub | ✗ | ✗ |
| RenderTarget2D | ✓ (FBO) | ✓ | ✗ | ✗ |
| RenderTargetCube | ✗ | ✗ | ✗ | ✗ |
| Multiple render targets (MRT) | ✗ | ✗ | ✗ | ✗ |
| 3D draw — colored primitives | ✓ | ✓ | throws | stub |
| 3D draw — textured/lit (GpuDrawParams) | ✓ | ✗ | throws | ✗ |
| DrawInstancedPrimitives | ✓ | throws | throws | ✗ |
| DrawUserPrimitives / DrawUserIndexedPrimitives | ✓ | ✓ | throws | ✗ |
| BlendState | ✓ | ✓ | ✗ | ✗ |
| DepthStencilState | ✓ | ✓ | ✗ | ✗ |
| RasterizerState | ✓ | ✓ | ✗ | ✗ |
| SamplerState per slot | ✓ | ✗ | ✗ | ✗ |
| ScissorRectangle | ✗ | ✗ | ✗ | ✗ |
| OcclusionQuery | ✓ | throws | throws | ✗ |
| GetBackBufferData | ✓ | ✓ | ✗ | ✗ |
| Effects (Basic/Alpha/Dual/Env/Skinned/Sprite) | ✓ | partial | ✗ | ✗ |
| Model::Draw | ✓ | partial | ✗ | ✗ |
| SpriteFont + DrawString | ✓ | ✓ | ✓ | stub |
| Viewport::Project/Unproject | ✓ | ✓ | ✓ | ✓ |
| Context-loss recovery | ✓ | N/A | N/A | N/A |
| FillMode::WireFrame | GLES3 limit | ✗ | ✗ | ✗ |

---

## Missing Public Type

| FNA file | Class | Notes |
|----------|-------|-------|
| `Effect/EffectMaterial.cs` | `EffectMaterial` | Trivial `Effect` subclass for material cloning. No games use it directly. One-liner stub sufficient. |

---

## Internal FNA Types (no CNA equivalent needed)

| FNA file | Reason not ported |
|----------|-------------------|
| `DxtUtil.cs` | Internal DXT decompression — CNA handles this inline |
| `FNA3D.cs` | Internal P/Invoke — CNA uses own backend system |
| `PipelineCache.cs` | Internal pipeline state cache — backend-internal concern |
| `ProfileCapabilities.cs` | Internal capability detection |
| `X360TexUtil.cs` | Xbox 360 format — not a target platform |
| `Effect/Resources.cs` | Embedded compiled shader binaries — CNA uses GLSL/SPIR-V |
| `Effect/EffectHelpers.cs` | Internal compilation utilities |
| `Effect/StockEffects/EffectHelpers.cs` | Same |
| `Vertices/VertexDeclarationCache.cs` | Internal metadata cache |

---

## Backend Summary

### SDL_Renderer
- **Role:** 2D-only. All 3D calls throw `std::runtime_error` (correct, by contract).
- **Gap:** No BlendState, DepthStencilState, RenderTarget2D wired. SamplerState could be wired to `SDL_SetTextureScaleMode`. ScissorRectangle could use `SDL_SetRenderClipRect`.

### EasyGL
- **Role:** Primary 3D backend (OpenGL ES 3.0). Most complete.
- **Gap:** ScissorRectangle not wired. RenderTargetCube / MRT not implemented. Full stencil ops incomplete. FillMode::WireFrame impossible on GLES3.

### Vulkan
- **Role:** 2D + deferred colored 3D.
- **Gap:** No textured/lit 3D pipeline (only stride-16 colored). No instancing, OcclusionQuery. SpriteBatch works (own backend). GetBackBufferData works (staging buffer).

### Bgfx
- **Role:** Prototype only — VBO/IBO stubs, texture upload partial, no real draw pipeline.
- **Gap:** Everything except basic texture upload.

---

## Detailed task plan → see `GRAPHICS_TASKS.md`
