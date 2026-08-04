# NOXNA — CNA Extended Graphics Layer

> **CNA/XNA 4.0 bude stabilní kompatibilní základ. NOXNA bude moderní engine vrstva pro Nova-3D.**

---

## 1. Two API Layers

CNA has two clearly separated API layers:

```
┌─────────────────────────────────────────────────────────────────┐
│  NOXNA Extension Layer  (CNA_NOXNA=ON)                          │
│  PbrMaterial · RenderPipelineSettings · HDR · Bloom · SSAO      │
│  Shadows · IBL · Instancing · LOD · glTF/GLB materials          │
├─────────────────────────────────────────────────────────────────┤
│  XNA 4.0 Compatibility Layer  (always built)                    │
│  GraphicsDevice · Texture2D/3D/Cube · Effect · SpriteBatch      │
│  Model · BasicEffect · RenderTarget2D · BlendState · ...        │
├─────────────────────────────────────────────────────────────────┤
│  Backend  (compile-time selection)                              │
│  EasyGL (OpenGL ES 3.2) · Vulkan · Bgfx · SDL_Renderer          │
└─────────────────────────────────────────────────────────────────┘
```

### Layer 1 — XNA 4.0 Compatibility (always on)

- Namespace: `Microsoft::Xna::Framework`
- Suitable for porting existing XNA/FNA games.
- Must remain compatible with XNA 4.0 concepts and API shapes.
- Does not require modern renderer features (runs on SDL_Renderer).
- All members that are NOT part of XNA 4.0 are tagged `NOXNA` in code but still
  compiled — `NOXNA` is a documentation marker only, not a compile guard.

### Layer 2 — CNA NOXNA Extension (opt-in)

- Namespace: `CNA::Graphics` (NOXNA extension types live here)
- Enabled by CMake option `CNA_NOXNA=ON`.
- Guarded by `#ifdef CNA_NOXNA` in all headers and sources.
- For new CNA/Nova-3D games that want modern 3D rendering.
- Must not break XNA 4.0 compatibility when enabled.
- Must not make Vulkan or any specific backend mandatory.

---

## 2. Intended Use Cases

| Use case | Layer |
|---|---|
| Port an existing XNA 4.0 / FNA game | XNA 4.0 only |
| Simple 2D game with SpriteBatch | XNA 4.0 only |
| Basic 3D with Model / BasicEffect | XNA 4.0 only |
| Shadow maps, render-to-texture | XNA 4.0 (partial) |
| Nova-3D / Urho3D-fork renderer | XNA 4.0 base + NOXNA extensions |
| PBR materials, HDR, post-processing | NOXNA |
| glTF/GLB model loading with materials | NOXNA |
| GPU instancing, LOD | NOXNA |
| Compute shaders, GPU culling | NOXNA (long term) |

---

## 3. Architecture — Nova-3D / Urho3D Fork Integration

```
Nova-3D / Urho3D fork
        │
        ├── Common rendering (mesh, texture, camera, light)
        │       └── CNA XNA 4.0: GraphicsDevice, VertexBuffer, IndexBuffer,
        │                        Texture2D/Cube/3D, Effect, RenderTarget2D
        │
        └── Modern rendering (PBR, HDR, shadows, post-processing)
                └── CNA NOXNA: PbrMaterial, RenderPipelineSettings,
                               HdrRenderTarget, ShadowMap, BloomPass, ...
```

XNA 4.0 covers all of these without NOXNA:

| Nova-3D / Urho3D concept | XNA 4.0 / CNA equivalent |
|---|---|
| VertexBuffer / IndexBuffer | `VertexBuffer` / `IndexBuffer` |
| Texture2D / TextureCube / Texture3D | same names in CNA |
| RenderSurface (render target) | `RenderTarget2D` / `RenderTargetCube` |
| Camera (View + Projection) | `Matrix::CreateLookAt` / `CreatePerspective` |
| Mesh draw call | `GraphicsDevice::DrawIndexedPrimitives` |
| Material (basic) | `Effect` + parameters |
| Technique / Pass | `EffectTechnique` / `EffectPass` |
| SpriteBatch / UI | `SpriteBatch` |
| Debug lines | `BasicEffect` + line primitives |
| Skinned mesh | `SkinnedEffect` |
| Simple shadows | `RenderTarget2D` shadow map + custom effect |

NOXNA is needed for anything beyond this list.

---

## 4. Planned NOXNA Features

### 4.1 Material System

| Feature | Status |
|---|---|
| `PbrMaterial` — albedo, normal, metallic/roughness, AO, emissive | 🟡 Scaffolding |
| Albedo color tint, metallic factor, roughness factor | 🟡 Scaffolding |
| Material serialization / glTF material mapping | ⬜ Not started |
| Material graph / node-based materials | ⬜ Long term |

### 4.2 Render Pipeline Settings

| Feature | Status |
|---|---|
| `RenderPipelineSettings` — HDR, bloom, SSAO, tonemapping, quality | 🟡 Scaffolding |
| HDR render targets (RGBA16F / RGBA32F) | ⬜ Not started |
| Tonemapping (Reinhard, Filmic, ACES) | ⬜ Not started |
| Bloom post-process pass | ⬜ Not started |
| SSAO post-process pass | ⬜ Not started |
| Exposure / gamma control | ⬜ Not started |

### 4.3 Lighting & Shadows

| Feature | Status |
|---|---|
| Directional shadow maps (basic) | ⬜ Not started |
| Cascaded shadow maps (CSM) | ⬜ Long term |
| Point light shadow maps (cube shadow) | ⬜ Long term |
| Image-based lighting (IBL) with prefiltered env map | ⬜ Not started |
| Skybox / environment map rendering | ⬜ Not started |
| BRDF LUT generation | ⬜ Not started |

### 4.4 Geometry & Instancing

| Feature | Status |
|---|---|
| GPU instancing (`DrawInstancedPrimitives`) | ⬜ Not started |
| Level-of-detail (LOD) selection | ⬜ Not started |
| Frustum / occlusion culling helpers | ⬜ Long term |

### 4.5 glTF / GLB Support

| Feature | Status |
|---|---|
| glTF 2.0 model loader (geometry only) | ⬜ Not started |
| glTF PBR material mapping to `PbrMaterial` | ⬜ Not started |
| glTF animation / skin support | ⬜ Long term |

### 4.6 Compute & Advanced GPU

| Feature | Status |
|---|---|
| Compute shaders (Vulkan / GL compute) | ⬜ Long term |
| GPU particle systems | ⬜ Long term |
| Bindless textures / descriptor indexing | ⬜ Long term |
| Mesh shaders | ⬜ Very long term |

---

## 5. Code Example — Intended Future Style

```cpp
#ifdef CNA_NOXNA
using namespace CNA::Graphics;

// PBR material setup
PbrMaterial mat;
mat.setAlbedoTexture(albedo);
mat.setNormalTexture(normal);
mat.setMetallicRoughnessTexture(metallicRoughness);
mat.setAmbientOcclusionTexture(ao);
mat.setEmissiveTexture(emissive);
mat.setMetallicFactor(0.0f);
mat.setRoughnessFactor(0.75f);

// Render pipeline configuration
auto& pipeline = graphicsDevice.GetRenderPipelineSettings();
pipeline.setHDREnabled(true);
pipeline.setBloomEnabled(true);
pipeline.setSSAOEnabled(true);
pipeline.setTonemappingMode(TonemappingMode::Filmic);
pipeline.setRenderQuality(RenderQuality::High);
pipeline.setShadowQuality(ShadowQuality::High);
pipeline.setExposure(1.0f);
#endif
```

The classic XNA 4.0 path continues to work unchanged:

```cpp
// Still works — XNA 4.0 compat, no NOXNA needed
spriteBatch.Begin();
spriteBatch.Draw(texture, position, Color::White);
spriteBatch.End();

basicEffect.setWorldProperty(world);
basicEffect.setViewProperty(view);
basicEffect.setProjectionProperty(projection);
graphicsDevice.DrawUserIndexedPrimitives(...);
```

---

## 6. Compile-Time Conventions

### CMake

```cmake
# Enable NOXNA extension layer (default OFF for XNA compat)
cmake -DCNA_NOXNA=ON -DCNA_GRAPHICS_BACKEND=EASYGL ..

# Pure XNA 4.0 mode (default)
cmake -DCNA_NOXNA=OFF -DCNA_GRAPHICS_BACKEND=EASYGL ..
```

### C++ guards

```cpp
// In headers that add NOXNA-only members to existing XNA classes:
#ifdef CNA_NOXNA
    NOXNA void SetReceiveShadows(bool value);
    NOXNA void SetNormalMap(Texture2D* texture);
#endif

// Standalone NOXNA classes live in namespace CNA::Graphics.
// (Do not use namespace CNA::NOXNA — NOXNA is a preprocessor macro.)
#ifdef CNA_NOXNA
namespace CNA::Graphics {
    class PbrMaterial { ... };
}
#endif
```

### File layout

```
include/CNA/Graphics/
    NOXNA.hpp                  ← master include (includes all below)
    TonemappingMode.hpp        ← enum class TonemappingMode
    RenderQuality.hpp          ← enum class RenderQuality
    ShadowQuality.hpp          ← enum class ShadowQuality
    RenderPipelineSettings.hpp ← pipeline config class
    PbrMaterial.hpp            ← PBR material class

src/CNA/Graphics/
    RenderPipelineSettings.cpp
    PbrMaterial.cpp
```

> **Note on namespace naming**: NOXNA extension types live in `namespace CNA::Graphics`,
> not `namespace CNA::NOXNA`. The identifier `NOXNA` is a preprocessor macro
> (`#define NOXNA` in `CNAHelper.hpp`) and cannot be used as a namespace name
> without macro expansion breaking the syntax.

---

## 7. Task Backlog

Tasks are listed in roughly recommended implementation order.
"Foundation" tasks must be done before dependent tasks.

### Foundation (do first)

| # | Task | Status |
|---|---|---|
| N01 | CMake `CNA_NOXNA` option; all code still builds with and without it | ✅ |
| N02 | `TonemappingMode.hpp`, `RenderQuality.hpp`, `ShadowQuality.hpp` — one enum per file, `namespace CNA::Graphics` | ✅ |
| N03 | `include/CNA/Graphics/RenderPipelineSettings.hpp` + `.cpp` — config-only, no renderer | ✅ |
| N04 | `include/CNA/Graphics/PbrMaterial.hpp` + `.cpp` — texture slots + scalar factors, no renderer | ✅ |
| N05 | `include/CNA/Graphics/NOXNA.hpp` — master include, example compiles | ✅ |
| N06 | Compile test / example `examples/noxna_settings_example.cpp` | ✅ |

### Material & Shading

| # | Task | Status |
|---|---|---|
| N10 | NOXNA PBR GLSL shader (albedo + normal + metallic/roughness + AO + emissive) for EasyGL | ⬜ |
| N11 | `PbrEffect` — NOXNA `Effect` subclass that uses the PBR shader | ⬜ |
| N12 | `PbrMaterial` → `PbrEffect` binding (apply material to effect before draw) | ⬜ |
| N13 | Prefiltered env map + BRDF LUT helper for IBL | ⬜ |
| N14 | PBR Vulkan SPIR-V shader variant | ⬜ |

### HDR & Post-Processing

| # | Task | Status |
|---|---|---|
| N20 | RGBA16F `RenderTarget2D` support in EasyGL (`SurfaceFormat::HdrColor` NOXNA enum) | ⬜ |
| N21 | Tonemapping fullscreen pass (Reinhard / Filmic / ACES) | ⬜ |
| N22 | Bloom pass (downsample + Gaussian blur + upsample + composite) | ⬜ |
| N23 | SSAO pass (hemisphere sampling + blur) | ⬜ |
| N24 | Exposure / gamma post-pass | ⬜ |
| N25 | `RenderPipelineSettings` wired to actual backend passes | ⬜ |
| N26 | `DepthEffect` — colour-depth-reduction post-process (`ShaderEffect` subclass): 16-bit/8-bit colour, 4/2/1-bit greyscale, GLSL for EasyGL | ✅ |
| N27 | `DepthEffect::DitherMode` — ordered (Bayer 4x4/8x8) dithering before quantization. Error-diffusion (Floyd-Steinberg/Atkinson) deliberately not offered — inherently sequential, not single-pass-GPU-friendly without compute shaders (see N70) | ✅ |
| N28 | `DepthEffect` `Palette256`/`Palette16` modes — real nearest-colour match against a fixed 216-entry web-safe palette / classic 16-entry EGA/CGA palette (lookup texture + fragment-shader search), composes with `DitherMode` | ✅ |

### Shadows

| # | Task | Status |
|---|---|---|
| N30 | Directional shadow map (depth `RenderTarget2D`, PCF sampling) | ⬜ |
| N31 | `BasicEffect` NOXNA shadow receiver option | ⬜ |
| N32 | `PbrEffect` shadow receiver | ⬜ |
| N33 | Cascaded shadow maps (CSM, 3-4 cascades) | ⬜ |
| N34 | Point light cube shadow maps | ⬜ |

### Skybox & Environment Lighting

| # | Task | Status |
|---|---|---|
| N40 | Skybox renderer (cube map, fullscreen sky pass) | ⬜ |
| N41 | IBL diffuse irradiance map (precompute from env cubemap) | ⬜ |
| N42 | IBL specular prefiltered env map (split-sum approximation) | ⬜ |
| N43 | `GraphicsDevice::SetSkybox(TextureCube*)` NOXNA method | ⬜ |

### Geometry & Instancing

| # | Task | Status |
|---|---|---|
| N50 | `GraphicsDevice::DrawInstancedPrimitives` NOXNA overload | ⬜ |
| N51 | Instance data `VertexBuffer` streaming helper | ⬜ |
| N52 | LOD selection helper in `Model` NOXNA extension | ⬜ |

### glTF / GLB

| # | Task | Status |
|---|---|---|
| N60 | glTF 2.0 loader → CNA `Model` / `Mesh` / `VertexBuffer` / `IndexBuffer` | ⬜ |
| N61 | glTF PBR material → `PbrMaterial` mapping | ⬜ |
| N62 | glTF skin / animation import | ⬜ |

### Compute & Long Term

| # | Task | Status |
|---|---|---|
| N70 | `ComputeShader` NOXNA class (EasyGL compute) | ⬜ |
| N71 | `StorageBuffer` / SSBO NOXNA class | ⬜ |
| N72 | GPU particle system via compute | ⬜ |
| N73 | GPU frustum / occlusion culling | ⬜ |

---

## 8. What NOXNA Is Not

- **Not a replacement for Unreal Engine.** It is an optional extension layer that lets CNA grow beyond XNA 4.0 over time.
- **Not a forced upgrade.** XNA 4.0 ports continue to compile and run unchanged.
- **Not backend-specific.** NOXNA abstractions must stay backend-agnostic; implementation is inside the backend.
- **Not an ABI guarantee.** NOXNA API can change until it stabilizes.

---

## 9. Relationship to Nova-3D

Nova-3D is a planned CNA-based 3D game framework / Urho3D-like renderer. It will use:

- **CNA XNA 4.0 layer** for mesh, texture, camera, sprite, UI, audio.
- **CNA NOXNA layer** for PBR, HDR, shadows, IBL, post-processing, glTF.

Nova-3D will not call OpenGL/Vulkan/bgfx directly. All GPU access goes through the CNA backend interface.

---

## 10. Quick Start (once N01–N06 are done)

```bash
# Build with NOXNA enabled
cmake -B cmake-build-noxna \
      -DCNA_GRAPHICS_BACKEND=EASYGL \
      -DCNA_NOXNA=ON \
      -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-noxna

# Run the NOXNA compile/runtime example
DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cmake-build-noxna/cna_example_noxna_settings

# Standard XNA compat build (NOXNA off by default)
cmake -B cmake-build-debug -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-debug
```
