# CNAEXT engine layer (`CNA::Graphics`)

## Status

The **CNAEXT engine layer** is the opt-in `CNA::Graphics` namespace that sits *above* CNA's XNA 4.0
API and orchestrates frame-level work the XNA 4.0 contract has no concept of: an HDR render
pipeline, post-processing passes, shadow maps, skybox/IBL, and (long term) compute.

**The HDR pipeline works; the scene-level subsystems do not exist yet.** As of 2026-08-17 a game
can wrap its drawing in `RenderPipeline`, render into a float scene target, and get ambient
occlusion, bloom, tonemapping and FXAA — verified on EasyGL against Mesa's software renderer. What
is still only designed: shadow maps, the skybox, image-based lighting, compute shaders, and the
instancing/LOD helpers. Do not describe those as available. The design is
[`../CNAEXT.md`](../CNAEXT.md); the task backlog and its evidence trail are
[`../plan_modern.md`](../plan_modern.md).

Enable it with:

```bash
cmake --preset cnaext          # == -DCNA_GRAPHICS_RENDERER=OPENGLES3 -DCNA_CNAEXT=ON -DCNA_BUILD_TESTS=ON
cmake --build --preset cnaext --target CnaTests
```

`CNA_CNAEXT` defaults to **OFF**. With it off, every header of this layer declares nothing at all,
so an XNA 4.0 port cannot accidentally depend on it.

## The two things called "CNAEXT"

| | `CNAEXT` marker convention | `CNA_CNAEXT` engine layer |
|---|---|---|
| What | The `CNAEXT` macro (`CNA/CNAHelper.hpp`) and the `*EXT` name suffix | A CMake option gating the whole `CNA::Graphics` namespace |
| Compiled | **Always** — a documentation/lint marker, never a compile guard | **Opt-in** — everything is inside `#ifdef CNA_CNAEXT` |
| Lives in | `Microsoft::Xna::Framework::…`, beside the XNA types it extends | `CNA::Graphics` |
| Examples | `PbrEffect`, `SkinnedPbrEffect`, `ShaderEffect`, `MorphTargetDataEXT` | `RenderPipelineSettings`, `PbrMaterial`, `DepthEffect`, `CRTEffect` |

The rule: a per-object/per-draw shading extension that takes the shape of an XNA `Effect`, vertex
format, or `GraphicsDevice` member ships as an always-compiled `CNAEXT` member. Frame-level
orchestration that pulls in extra render targets and GPU memory lives in the gated engine layer.

## What exists today

| Type | Header | Purpose |
|---|---|---|
| `RenderPipelineSettings` | `CNA/Graphics/RenderPipelineSettings.hpp` | Configuration bag (HDR, exposure, gamma, tonemapping, bloom, SSAO, quality, shadows). **No consumer yet** — `plan_modern.md` Phase 7 builds it. |
| `TonemappingMode`, `RenderQuality`, `ShadowQuality` | same directory | Enumerations used by the settings bag. |
| `PbrMaterial` (+ `PbrTextureSlot`) | `CNA/Graphics/PbrMaterial.hpp` | A lossless, comparable value description of everything `PbrEffect` renders. |
| `applyMaterial`, `extractMaterial`, `applyMaterialState` | `CNA/Graphics/MaterialBinding.hpp` | Moves a material onto an effect and back, and applies the device state it implies. |
| `materialFromGltfEXT` | `CNA/Graphics/GltfMaterialBridge.hpp` | Builds a material from the glTF importer's decoded record. |
| `DepthEffect` (+ `DepthEffectMode`, `DitherMode`) | `CNA/Graphics/DepthEffect.hpp` | Colour-depth-reduction post-process (RGB565/RGB332, 4/2/1-bit greyscale, palettes, ordered dithering). |
| `CRTEffect` (+ `CRTMaskType`) | `CNA/Graphics/CRTEffect.hpp` | Scanlines, RGB sub-pixel mask, barrel curvature, vignette. |
| `AsciiPostProcessEffect` (+ `AsciiQuantizeMode`) | `CNA/Graphics/AsciiPostProcessEffect.hpp` | Renderer-neutral ASCII/glyph-grid post-process (see [`ascii-post-process-effect.md`](ascii-post-process-effect.md)). |
| `RenderPipeline` | `CNA/Graphics/RenderPipeline.hpp` | Frame-level orchestrator: `begin`/`end` around a game's drawing, an HDR scene target, and the fixed pass chain. |
| `PostProcessPass`, `PostProcessContext`, `PostProcessChain` | same directory | The pass abstraction, its one invocation struct, and the chain that ping-pongs between intermediates. |
| `SsaoPass`, `BloomPass`, `TonemapPass`, `FxaaPass`, `BlitPass` | same directory | The built-in passes, in pipeline order. |
| `FullscreenPass`, `RenderTargetPool` | same directory | The screen-covering draw and the intermediate-target cache every pass shares. |
| `ShadowMap`, `DirectionalLightEXT` | `CNA/Graphics/ShadowMap.hpp` | Directional shadow-map generation: fits the light's volume to the scene, opens a pass the app draws its casters into. |
| `CascadedShadowMap` | `CNA/Graphics/CascadedShadowMap.hpp` | The same, split into 2-4 depth ranges so a large scene keeps resolution near the camera. |
| `CubeShadowMap`, `SpotShadowMap`, `PointLightEXT`, `SpotLightEXT` | `CNA/Graphics/CubeShadowMap.hpp`, `SpotShadowMap.hpp` | Punctual-light shadows: six cube faces for a point light, one perspective map for a spot. |
| `ComputeShader`, `StorageBuffer`, `StorageBufferT<T>` | `CNA/Graphics/ComputeShader.hpp`, `StorageBuffer.hpp` | Compute programs and the buffers they read and write. |
| `AutoExposureEXT` | `CNA/Graphics/AutoExposureEXT.hpp` | Compute log-average luminance reduction, and the adaptation that turns it into an exposure. |
| `InstancedRendererEXT` | `CNA/Graphics/InstancedRendererEXT.hpp` | Draws one mesh part many times in a single call, owning the per-instance transform stream. |
| `LodGroupEXT` (+ `LodSelectionMode`) | `CNA/Graphics/LodGroupEXT.hpp` | Levels of detail selected by distance or projected screen size, with optional hysteresis. |
| `FrustumCullerEXT` | `CNA/Graphics/FrustumCullerEXT.hpp` | Filters bounds — or transforms — down to what a camera can see. |
| `Skybox` | `CNA/Graphics/Skybox.hpp` | Draws an environment cube map as the sky, in one fullscreen pass. |
| `ImageBasedLightEXT` | `Microsoft/Xna/Framework/Graphics/ImageBasedLightEXT.hpp` | The three split-sum products as a lit effect consumes them (XNA namespace, always compiled). |
| `EnvironmentProcessor` | `CNA/Graphics/EnvironmentProcessor.hpp` | Turns an equirectangular panorama into a cube map, and an environment cube into the three IBL products (irradiance, prefiltered specular, BRDF LUT). |
| `CNAEXT.hpp` | `CNA/Graphics/CNAEXT.hpp` | Master include — pulls in every public type above. |

## Conventions for this layer

These are the rules every `CNA::Graphics` type follows. They differ deliberately from the XNA layer,
which must instead match XNA 4.0 exactly.

- **Naming.** Verbs are `lowerCamelCase` (`apply`, `resize`, `begin`). Properties use
  `getX()`/`setX()`, or `isX()` for booleans — matching the existing `RenderPipelineSettings` and
  `PbrMaterial`. There is no XNA name to preserve here, so the C++ house style wins.
- **Guarding.** Every file in `modules/graphics-ext/{include,src}` opens with `#ifdef CNA_CNAEXT`
  and closes with `#endif // CNA_CNAEXT`. `scripts/check_cnaext_guards.sh` enforces this.
- **Documentation.** Full Doxygen (`@brief`, `@param`, `@return`) on every public member, exactly as
  CLAUDE.md requires of the XNA layer.
- **Renderer access.** Engine-layer code talks to the GPU only through `GraphicsDevice`, `Effect`,
  `RenderTarget2D`/`RenderTargetCube` and `IGraphicsRenderer` — never raw GL/VK/D3D.
- **Capability-gated, never crashing.** Each subsystem checks
  `GraphicsDevice::SupportsCapability()` and documents its fallback. No renderer is mandatory, and
  no subsystem may make one so.
- **Shader profile: GLSL ES 3.00, and ES 3.10 for compute.** `plan_modern.md` `MOD-15`. Every pass,
  caster and lighting shader in this layer is written to that floor, and `ShaderEffect` owns the
  `#version` line and the down-level rewriting — a pass never writes one and never branches on the
  profile. The floor is ES 3.00 rather than desktop GL because the WebGL2 and OpenGL ES 3 renderers
  are in the committed scope and desktop GL 3.3 accepts everything ES 3.00 expresses, so the
  cheapest common denominator is also the widest. Compute is a separate, higher floor because
  ES 3.10 is where compute shaders and storage buffers first exist at all.

  Two consequences a pass author has to live with. **Below the floor**, on the ES 1.00 renderers
  (`OpenGLES2`, `WebGL1`), `ShaderEffect`'s transformation is real but not total: there is no
  `textureLod` (so a rough IBL reflection reads the base mip instead of its roughness mip), no
  dynamic indexing of a uniform array, and loops must be statically countable. A shader that needs
  any of those does not silently degrade — it fails to compile, and the subsystem reports `false`.
  **Above the floor**, nothing in this layer may *require* a higher profile without a capability to
  ask about first; that is what keeps ES 3.00 a floor rather than a fiction.

## Float render targets: formats, and what each one means

`plan_modern.md` `MOD-109`–`MOD-111`, `MOD-140`. The HDR pipeline rests entirely on one question —
will this renderer give me a render target that keeps values above 1.0 — so the answer is written
down per format rather than per renderer, and the mapping is stated once here instead of being
inferred from each renderer's source.

### Ask before you allocate

Two queries, and they are not interchangeable:

```cpp
device.SupportsCapability(CNA::GraphicsCapability::FloatRenderTargets);      // 32-bit float
device.SupportsCapability(CNA::GraphicsCapability::HalfFloatRenderTargets);  // 16-bit float
device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HalfVector4);   // this exact format
```

The two capabilities are **derived**: they are not a renderer's own capability switch (many of those
end `default: return true`, which would have every renderer claim float targets it has never heard
of) but the per-format query asked about a representative format — `Vector4` for the full-float
capability, `HdrBlendable` for the half-float one. So the capability and the format query can never
disagree, and neither can disagree with `RenderTarget2D`'s constructor: all three go through the same
`ClassifyRenderTargetFormatEXT` verdict.

**A refused format is refused, not substituted.** A renderer that cannot do `Vector4` throws from the
constructor rather than handing back an 8-bit `Color` target. A caller given a silently downgraded
target has no way to discover it — the values simply clamp, somewhere, later. That is the failure the
whole phase exists to remove, so there is no downgrade policy to choose between.

### The format table

The internal formats below are EasyGL's, which is the reference implementation; the D3D and Vulkan
columns name the format an implementation is expected to choose when it lands, and are **not** claims
that it has.

| `SurfaceFormat` | Channels × bits | EasyGL internal format | Bytes/texel | Vulkan equivalent | D3D equivalent |
|---|---|---|---|---|---|
| `Color` | 4 × 8 unorm | `RGBA8` | 4 | `R8G8B8A8_UNORM` | `R8G8B8A8_UNORM` |
| `HalfSingle` | 1 × 16 float | `R16F` | 2 | `R16_SFLOAT` | `R16_FLOAT` |
| `HalfVector2` | 2 × 16 float | `RG16F` | 4 | `R16G16_SFLOAT` | `R16G16_FLOAT` |
| `HalfVector4` | 4 × 16 float | `RGBA16F` | 8 | `R16G16B16A16_SFLOAT` | `R16G16B16A16_FLOAT` |
| `HdrBlendable` | 4 × 16 float | `RGBA16F` | 8 | `R16G16B16A16_SFLOAT` | `R16G16B16A16_FLOAT` |
| `Single` | 1 × 32 float | `R32F` | 4 | `R32_SFLOAT` | `R32_FLOAT` |
| `Vector2` | 2 × 32 float | `RG32F` | 8 | `R32G32_SFLOAT` | `R32G32_FLOAT` |
| `Vector4` | 4 × 32 float | `RGBA32F` | 16 | `R32G32B32A32_SFLOAT` | `R32G32B32A32_FLOAT` |

Every other `SurfaceFormat` — the compressed formats, `Bgra5551`, `Rgba1010102`, `Rg32`, `Rgba64`
and the rest — is **not** a render-target format in CNA, on any renderer, and is refused.

### `HdrBlendable` is `HalfVector4`

`MOD-110`. XNA's `HdrBlendable` was "the float format you can alpha-blend into", which on Windows
meant `RGBA16F`. CNA makes that equivalence explicit rather than inventing a third meaning for it:
the two map to the same storage, answer the same capability query, and behave identically. Prefer
`HdrBlendable` in code that means "an HDR target for the frame" — it says the intent — and
`HalfVector4` where the exact layout is what matters.

### Blending on float targets

`MOD-111`. Alpha blending into `HalfVector4`/`HdrBlendable` is what the format is *for* and works
wherever the format does. Blending into `Single` and `Vector2` is **renderer- and driver-dependent**:
GL requires `EXT_float_blend` for 32-bit float blending, and a single-channel target has no alpha to
blend with in the first place.

CNA does not enforce this at run time, matching XNA's own laxity: a blend state that a driver cannot
honour is silently ignored by the driver rather than reported. So the rule is a rule for callers, not
a check — the engine layer's own passes blend only into `HdrBlendable`/`HalfVector4` targets, and a
pass that needs a `Single` accumulation buffer writes it with blending off.

### Per-renderer status

Only two renderers answer this question with their own verdict today; every other renderer defers,
and the framework's own rule then applies — `Color`, and nothing else. That is the literal truth for a
renderer that has implemented no other render-target format, which is why the default is honest
rather than merely conservative.

| Renderer | Float render targets | Note |
|---|---|---|
| `OpenGLES3`, `OpenGL33`, `OpenGLES2`, `OpenGL4`, `WebGL1`, `WebGL2` (EasyGL) | ✅ all seven float formats | Verified against Mesa llvmpipe (ES 3.2) by `HdrRenderTargetRoundTripTests`. What a *driver* supports is still asked at run time, so an ES 2 profile that lacks float FBOs reports false rather than failing later. |
| `Skia` | ✅ all seven float formats | The surprise of this table, and worth stating rather than assuming from "CPU raster": Skia's own `ClassifyRenderTargetFormatEXT` reports every float format supported (`SKIA-142`). Its raster surface has no hardware format restriction, so the list is deliberately held to what real XNA/FNA hardware reports renderable rather than to whatever Skia would accept. |
| `Vulkan` | ⬜ defers → `Color` only | Measured, not assumed (`MOD-1610`): its `HdrRenderTargetRoundTrip` cases skip on a real lavapipe device. |
| Every other renderer | ⬜ defers → `Color` only | No `ClassifyRenderTargetFormatEXT` override, so the framework rule applies. The 2D-only and fixed-function identities are ⛔ by their own nature — see the per-identity matrix below. |

## Per-renderer support matrix

The engine layer's subsystems are rolled out per renderer, exactly as the PBR effects were. This
matrix is authoritative and is updated by each task that changes a cell; a subsystem with no
implemented feature has no row yet.

The renderer identities are those of `CNA::GraphicsRendererType` (49 on `next`). Note that a build
can now contain several renderer families and pick one at run time
([`runtime-renderer-selection.md`](runtime-renderer-selection.md)), so engine-layer code asks the
live `GraphicsDevice` for a capability and never a compile-time `CNA_RENDERER_*` macro.

| Subsystem | EasyGL (reference) | Vulkan | D3D11 | Other renderers |
|---|---|---|---|---|
| Post-process effects (`DepthEffect`, `CRTEffect`) | ✅ GLSL | ⛔ its `ShaderEffect` takes SPIR-V, not the passes' GLSL | ⬜ | `AsciiPostProcessEffect` is CPU-side and runs everywhere |
| Float/HDR render targets | ✅ RGBA16F + RGBA32F, runtime-probed | ⬜ measured: `Color` only today | ⬜ | ⬜ — each reports `false` and `RenderTarget2D` refuses the format rather than substituting `Color` |
| `RenderPipeline` + post-process passes | ✅ | 🟨 runs and copies through — measured, frame identical to no pipeline | ⬜ | The passes need `GraphicsCapability::CustomEffects`; without it each copies its input and the frame still renders |
| Shadow maps (directional, PCF) | ✅ generation + reception on all four lit effects | ⬜ `SupportsShadowSamplingEXT()` false | ⬜ | ⬜ — an effect accepts the shadow state and a renderer without the shader ignores it, so the frame renders unshadowed rather than failing |
| Cascaded shadow maps (2-4, atlas) | ✅ same four programs, one shared shader path | ⬜ | ⬜ | ⬜ — same accepted-and-ignored convention |
| Point / spot lights + shadows | ✅ punctual lighting and its cube/spot lookup on all four lit programs | ⬜ | ⬜ | ⬜ — same accepted-and-ignored convention |
| Skybox | ✅ one fullscreen pass; needs `CustomEffects` | ⬜ | ⬜ | ⬜ — where the shader will not compile the sky is skipped and logged once |
| Image-based lighting | ✅ CPU precompute (works on every renderer) + split-sum shading | 🟨 precompute works; `SupportsImageBasedLightingEXT()` false | ⬜ | ⬜ — the precompute runs anywhere; the shading needs the renderer's own shader path |
| Materials (`PbrMaterial` ↔ `PbrEffect`) | ✅ | ✅ | ✅ | ✅ — no renderer code at all: it moves values between two existing objects |
| Instancing / LOD / culling | ✅ | ✅ measured — `cnaext_instancing_lod_test` passes 5/5 on a real Vulkan device | ⬜ | ⬜ — `LodGroupEXT` and `FrustumCullerEXT` are renderer-free and run everywhere; `InstancedRendererEXT` needs `GraphicsCapability::Instancing` and otherwise refuses (or falls back, on request) |
| Compute / storage buffers | ✅ GL ES ≥ 3.1 / GL ≥ 4.3, runtime-probed; image bindings desktop-GL only | ⬜ not implemented; reports false and both wrappers refuse | ⬜ | ⬜ — both wrappers throw `System::NotSupportedException` naming the renderer |

**Asking a renderer what it will actually do.** Three questions, and they are not the same question:

| Question | Answers |
|---|---|
| `SupportsCapability(GraphicsCapability::CustomEffects)` | whether the renderer can compile *some* custom effect — not that it takes this layer's shader language |
| `SupportsShadowSamplingEXT()` | whether its lit shaders really *sample* the shadow state every effect accepts |
| `SupportsImageBasedLightingEXT()` | whether its PBR shader really shades from a bound environment |

The distinction is not academic: the Vulkan renderer answers **true** to the first and **false** to
the other two, because its `ShaderEffect` takes SPIR-V bytecode while this layer's passes and
shadow casters hand it GLSL source. Before those two queries existed, the shadow example on Vulkan
did not fail — it crashed, because the caster's effect failed to compile and the draw proceeded with
no effect applied. Ask all three.

### Every renderer identity

`plan_modern.md` `MOD-1698`. The table above compares subsystems across the three renderers this
plan committed to; this one leaves nobody out. A renderer missing from a matrix reads as "fine",
which is exactly what an unexamined renderer is not — so `scripts/check_cnaext_matrix.py` derives
the list from `CNA::GraphicsRendererType` itself and fails if an identity has no row or no status.
It runs as the ctest `CNAEXT_MatrixCompleteness`.

Legend: ✅ verified in this repository · 🟨 partial, or verified only by sharing an implementation ·
⬜ not implemented, or not verified here · ⛔ deliberately unsupported by what the identity *is*
(2D-only or fixed-function hardware, where a shadow map or a compute dispatch has no meaning).

| Renderer | Engine layer | Notes |
|---|---|---|
| `OpenGLES3` | ✅ reference | Every subsystem lands here first and is verified here (Mesa llvmpipe, ES 3.2). |
| `OpenGL33` | 🟨 shares EasyGL | Same implementation as `OpenGLES3`; not separately exercised in this environment. |
| `OpenGLES2` | 🟨 shares EasyGL | GLSL ES 1.00: the shaders are transformed, so no `textureLod` (rough IBL reflections read the base mip), no dynamic uniform-array indexing, statically countable loops only. |
| `WebGL1` | 🟨 shares EasyGL | As `OpenGLES2`, plus: no compute in any WebGL version. |
| `WebGL2` | 🟨 shares EasyGL | No compute; otherwise the ES 3.00 path. |
| `Vulkan` | 🟨 measured | Verified against a real device (Mesa lavapipe 1.4): instancing works; float targets, the post-process passes, shadow sampling, IBL shading and compute do not. Its `ShaderEffect` takes SPIR-V, not this layer's GLSL. |
| `Headless` | ✅ measured | Whole suite run against it: 0 engine-layer failures. Three limits it does not advertise — no render-target readback, no cube-face storage, and a cube-face bind that records the face without making it current. The engine layer constructs and passes through; tests probe the three rather than assume them. |
| `Software` | ✅ measured | Whole suite run against it: 0 engine-layer failures. Render targets, readback and cube storage all work; what does **not** is custom shader source — it is accepted and then ignored, which is why every shader-based subsystem asks `ExecutesShaderEffectSourceEXT()` as well as `CustomEffects`. |
| `Stub` | ✅ measured | Whole suite run against it: 0 engine-layer failures. Stricter than `Headless` — it refuses to bind a `RenderTarget2D` at all, so a pipeline stops before it renders. Everything above that point constructs and reports false. |
| `SdlRenderer` | ⛔ 2D-only by identity | No `ThreeD`; `RenderPipeline` passes through and every 3D subsystem reports false. |
| `SdlGpu` | ⬜ not implemented |  |
| `Bgfx` | ⬜ not implemented | Needs `shaderc` regeneration for any new shader. |
| `WebGPU` | ⬜ not implemented | Depends on `WEBGPU-76` (custom WGSL through `ShaderEffect`). |
| `Magnum` | ⬜ not implemented |  |
| `DirectX11` | ⬜ not implemented | In the committed renderer scope; needs Wine+DXVK to verify here. |
| `DirectX12` | ⬜ not implemented |  |
| `DirectX10` | ⬜ not implemented |  |
| `DirectX9` | ⬜ not implemented | SM3 limits apply to any pass ported there. |
| `DirectX8` | ⛔ fixed-function by identity |  |
| `DirectX7` | ⛔ fixed-function by identity |  |
| `DirectX6` | ⛔ fixed-function by identity |  |
| `DirectX5` | ⛔ fixed-function by identity |  |
| `DirectX3` | ⛔ fixed-function by identity |  |
| `DirectX2` | ⛔ fixed-function by identity |  |
| `DirectX1` | ⛔ 2D-only by identity | DirectDraw v1; no 3D at all. |
| `Direct2D` | ⛔ 2D-only by identity |  |
| `Canvas` | ⛔ 2D-only by identity |  |
| `HtmlDom` | ⛔ 2D-only by identity |  |
| `SvgDom` | ⛔ 2D-only by identity |  |
| `PixiJs` | ⛔ 2D-only by identity | Emscripten-only, and not yet built on any real toolchain (`plan_pixijs.md`). |
| `Skia` | ⛔ 2D-only by identity | CPU raster; advertises no 3D/depth/MSAA/MRT. |
| `Blend2D` | ⛔ 2D-only by identity |  |
| `OpenVg` | ⛔ 2D-only by identity |  |
| `Gdi` | ⛔ 2D-only by identity |  |
| `Glide` | ⛔ fixed-function by identity |  |
| `FreeDirect` | ⬜ not verified |  |
| `TinyGL` | ⛔ fixed-function by identity | No shaders, render targets, stencil or scissor; 1-bit colour-key transparency. |
| `PortableGL` | ⬜ not verified | Shader-era CPU GL; `MOD-1617` decides whether float targets are worth it there. |
| `OpenGL4` | ⬜ not implemented | Its own renderer, not EasyGL. |
| `OpenGL2` | ⬜ not implemented | Float targets only via `ARB_texture_float`. |
| `OpenGL1` | ⛔ fixed-function by identity |  |
| `OpenGLES1` | ⛔ fixed-function by identity |  |
| `Sokol` | ⬜ not implemented |  |
| `Diligent` | ⬜ not implemented | Its native API is chosen at runtime; would need verifying on at least two. |
| `Llgl` | ⬜ not implemented |  |
| `Igl` | ⬜ not implemented | Backend fixed by `CNA_IGL_BACKEND` before the renderer exists. |
| `Metal` | ⬜ not implemented | Apple platforms only. |
| `Fna3d` | ⬜ not implemented | Already overrides `CreateRenderTarget2DEXT`; float formats unverified. |
| `Wicked` | ⬜ not implemented |  |

### Using it

```cpp
CNA::Graphics::RenderPipeline pipeline(GraphicsDevice());
pipeline.resize(backBufferWidth, backBufferHeight);      // and again on every resize

auto& settings = pipeline.getSettings();
settings.setHDREnabled(true);
settings.setTonemappingMode(CNA::Graphics::TonemappingMode::Aces);
settings.setBloomEnabled(true);

// Per frame:
pipeline.begin(Color::CornflowerBlue);
//   ... every SpriteBatch, Model and Effect draw the game already makes ...
pipeline.end();
```

With default settings this allocates nothing and renders exactly what the game rendered before,
which is what makes it safe to add to an existing title before deciding whether to use it.

Ambient occlusion needs one thing the pipeline cannot do for a game: scene depth and view-space
normals, which means drawing the geometry a second time with a different effect. Supply them with
`setDepthNormalInputs()`; without them SSAO renders an unoccluded frame rather than failing.

### Writing your own pass

`plan_modern.md` `MOD-233`. There are two routes, and the shorter one is right more often than it
looks.

**If you have a shader, you do not need a class.** `EffectPass` runs any `Effect` as a fullscreen
pass, which is all most passes are:

```cpp
auto tint = std::make_unique<Microsoft::Xna::Framework::Graphics::ShaderEffect>(
    device, kVertexSource, kFragmentSource);
pipeline.addOwnedPass(std::make_unique<CNA::Graphics::EffectPass>(device, std::move(tint), "Tint"));
```

That is also how the three CNAEXT effects that predate this layer — `DepthEffect`, `CRTEffect` and
any `ShaderEffect` of your own — get into a chain: they are `Effect` subclasses and needed no change
at all. (`AsciiPostProcessEffect` is the exception, and an instructive one: it is *not* an `Effect` —
it reads the frame back to the CPU and re-uploads a glyph grid — so it has its own `AsciiPass`.)

**Subclass when the pass needs state, several draws, or its own targets.** A complete pass:

```cpp
class VignettePass : public CNA::Graphics::PostProcessPass
{
public:
    explicit VignettePass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<CNA::Graphics::FullscreenPass>(device))
    {
        if (isSupported(device))
            effect_ = std::make_unique<ShaderEffect>(device, kVertex, kFragment);
    }

    void apply(const CNA::Graphics::PostProcessContext& context) override
    {
        if (effect_)
            effect_->SetUniformFloat("uStrength", strength_);
        // A null effect draws the source through unchanged, which is the fallback every pass takes.
        fullscreen_->draw(context.source, context.destination, effect_.get(),
                          context.width, context.height);
    }

    [[nodiscard]] const std::string& getName() const override
    {
        static const std::string name = "Vignette";
        return name;
    }

private:
    std::unique_ptr<CNA::Graphics::FullscreenPass> fullscreen_;
    std::unique_ptr<ShaderEffect>                  effect_;
    float                                          strength_ = 0.5f;
};
```

Four rules the layer holds itself to, and a pass that breaks them will misbehave in ways that are
hard to see:

- **Allocate in the constructor, never in `apply`.** Effects, intermediate targets, buffers. A pass
  that allocates per frame is a pass that stutters. Where you need a scratch target, take it from the
  pipeline's `RenderTargetPool` rather than making your own.
- **Never throw from `apply` because the renderer cannot do something.** Answer `isSupported(device)`
  false and copy the input through. A game that enables your pass on a 2D-only renderer should get an
  unaffected frame, not an exception. Refuse only genuinely invalid *arguments*.
- **Ask the two-part question.** The base class's `isSupported` already does: a renderer that
  *accepts* an effect is not necessarily one that *runs your source*. If your pass carries GLSL,
  inherit that default. If it runs a compiled or stock effect, override — that is exactly what
  `EffectPass` does, and its test says so, so nobody tidies it back.
- **Do not leave a render target bound.** `FullscreenPass::draw` handles this for you through
  `ScopedRenderTarget`; if you bind a target yourself, use the same class. A destination left bound
  after a throw does not look like an error — the frame simply stops updating, because everything
  drawn afterwards goes into your intermediate.

### Shadows, and the contract they put on the app

Shadows cost the app something no library can pay on its behalf: **the scene has to be drawable
twice per frame** — once from the light, to fill the map, and once from the camera, to shade it.
That is inherent to shadow mapping, not to this implementation, and it is the reason the split
below exists rather than a single `enableShadows(true)`.

There are two ways to run the pass, and the plain one is the default:

```cpp
// 1. App-driven. ShadowMap is usable entirely on its own.
CNA::Graphics::ShadowMap shadowMap(device, CNA::Graphics::ShadowQuality::High);

shadowMap.begin(sun, sceneBounds);
drawEveryCaster();                       // geometry only -- no clear, no target change
shadowMap.end();
```

```cpp
// 2. Registered with the pipeline, which then runs it inside begin(), before the scene target is
//    bound. Same two calls, made for you, in the right order.
pipeline.getSettings().setShadowsEnabled(true);
pipeline.setShadowScene(&shadowMap, sun, sceneBounds, [&] { drawEveryCaster(); });
```

Either way, the receiving half is the app's: shadows arrive at a surface through the effect that
shades it, so each lit effect is told about the map.

```cpp
effect.setShadowMapEXT(shadowMap.getShadowTexture());
effect.setLightViewProjectionEXT(shadowMap.getLightViewProjection());   // after begin() has run
effect.setShadowFilterRadiusEXT(shadowMap.getFilterRadius());
effect.setShadowsEnabledEXT(true);
```

`BasicEffect`, `SkinnedEffect`, `PbrEffect` and `SkinnedPbrEffect` all implement
`IShadowReceiverEXT`, so a shadow subsystem can hold an `IShadowReceiverEXT&` and not care which.
That interface is always compiled, unlike everything else on this page: receiving a shadow is a
per-draw property of a material, and an effect's public surface must not change with a build flag.

Three things about it are worth knowing before the first frame looks wrong:

- **The map holds light-space distance, not depth.** CNA cannot sample a depth attachment as a
  texture on every renderer, so the caster shader writes distance into an ordinary colour target.
  A `Single` (R32F) target where the renderer has one, `Color` otherwise — 256 distinguishable
  depths, which is enough for a demo and visibly stepped in anything larger.
- **`getLightViewProjection()` is only valid after `begin()`.** The matrix is computed from the
  light and the scene bounds when the pass opens, so an effect configured before the first pass
  carries an identity matrix for one frame.
- **Bias is a trade, and the default sits in the middle of it.** At `0` a surface shadows itself:
  in the acne scene the test suite renders, 55% of a plane goes dark. At the default `0.0015`
  only the real shadow remains (9%). At `0.2` the shadow leaves its caster entirely (0%). Raising
  the bias to remove an artefact is never free.

Outside the light's fitted volume nothing is shadowed, by an explicit range check rather than a
clamped sampler — clamping would smear the caster's silhouette outward to the edges of the world.

### Cascades, and what the second contract costs

One map stretched over an outdoor view spends most of its texels on ground nobody looks at closely.
`CascadedShadowMap` splits the camera frustum by distance and fits a map to each slice.

**The app-side contract grows: the casting geometry is drawn once per cascade** — a two-cascade set
draws it twice, a four-cascade set four times, on top of the camera pass. That is inherent to
cascaded shadow mapping and is the reason the cascade count is a quality setting rather than
something the library picks.

```cpp
CNA::Graphics::CascadedShadowMap cascades(device, ShadowQuality::High, 3);
cascades.setBlendBand(4.0f);                 // cross-fade width, in view-depth units

// Per frame:
cascades.update(sun, cameraView, cameraProjection);
for (int i = 0; i < cascades.getCascadeCount(); ++i)
{
    cascades.begin(i);
    drawCastersFor(i);                       // geometry only; cull to this cascade if you like
    cascades.end();
}
cascades.applyToReceiver(effect);            // atlas, matrices, splits, camera, band — together
effect.setShadowsEnabledEXT(true);
```

`applyToReceiver` sets everything at once on purpose. These values are only meaningful together: a
matrix from this frame beside a split from the last one puts fragments in the wrong cascade, which
reads as a resolution artefact rather than as the torn update it is.

Worth knowing:

- **Each cascade is a full-resolution map**, not a share of one. `High` with four cascades is four
  times the memory of `High` with one — never a quarter of the resolution.
- **Storage is an atlas**, one `RenderTarget2D` with the cascades side by side, because CNA's
  renderer interface has no array-texture concept. Each cascade's slice is baked into its own
  matrix, so there is no separate UV offset to apply to the wrong cascade.
- **The fit is sphere-based and texel-snapped**, which is what stops shadow edges shimmering as the
  camera turns and crawling as it walks. Both are asserted numerically, because both look correct
  in any single frame.
- **`setDebugTintEnabled(true)`** tints each cascade a distinct colour. It is the fastest way to
  see whether the splits are where the scene wants them.
- **Cost** (`cnaext_csm_test --benchmark`, 6 casting triangles, Mesa llvmpipe): a single Medium map
  0.12 ms, two cascades 0.20 ms, three 0.49 ms, four 0.43 ms per frame. Software-rasterizer
  figures, so a recording rather than a budget — the shape, roughly linear in cascade count with
  the per-pass overhead dominating at this triangle count, is what transfers.

### Point and spot lights

XNA's lit effects carry three *directional* lights and nothing else, so a point light's shadow
would have had nothing to attenuate. `PunctualLightEXT` therefore carries a light **and** its
shadow, and the four lit effects gained a punctual lighting term for it to modulate — point or
spot, with an inverse-square falloff windowed to zero at the light's range, so the light ends
exactly where its shadow map ends.

```cpp
CNA::Graphics::CubeShadowMap cube(device, ShadowQuality::Medium);
CNA::Graphics::PointLightEXT lamp;
lamp.Position = /* ... */;  lamp.Range = 40.0f;

cube.update(lamp);
for (int face = 0; face < CNA::Graphics::CubeShadowMap::kFaceCount; ++face)
{
    cube.begin(face);
    drawCasters();                       // the scene, six more times
    cube.end();
}

PunctualLightEXT punctual;
punctual.Kind       = PunctualLightKindEXT::Point;
punctual.Position   = lamp.Position;
punctual.Range      = lamp.Range;
punctual.ShadowCube = cube.getShadowTexture();
effect.setPunctualLightEXT(punctual);
```

- **One punctual light per draw**, alongside the three directional slots. A deliberate ceiling, not
  an accident: each shadowed light is another generation pass, and six of them for a point light.
- **Both maps store distance from the light over its range**, not projected depth. A cube face's
  projected depth is defined by that face's own projection, so comparing against it means
  recovering which face a direction came from; distance over range is the same number whichever
  face it landed on. The range you light with must be the range the map was generated with.
- **The cube face size is capped at 1024** whatever the quality asks. Six faces at 4096 is a
  hundred million texels for one light.
- **The cube lookup takes a single tap** while the spot map gets 3×3 PCF. Filtering across a cube
  face's edge needs seamless sampling, which is not available on every profile these shaders
  compile in, and a tap that wrapped to the wrong face would draw a stripe along every seam.
- **Cost** (`cnaext_pointshadow_test --benchmark`, 2 casting triangles, Mesa llvmpipe): one
  directional map 0.05 ms, a spot map 0.04 ms, **a point light's six faces 5.42 ms**. That is not
  six times a single map, it is a hundred times: each face rebinds a different cube attachment and
  clears it, so per-pass overhead dominates completely at low triangle counts. It is also the
  whole reason point shadows are something a game opts into per light.

### The sky

```cpp
CNA::Graphics::EnvironmentProcessor processor(device);
auto cube = processor.convertEquirectangular(panoramaTexture, 512);   // load time, once

CNA::Graphics::Skybox sky(device, nullptr);
sky.setOwnedEnvironment(std::move(cube));    // generated, so the skybox takes it
sky.setYaw(1.2f);                            // line the sun up with the scene's key light
sky.setIntensity(3.0f);                      // meaningful above 1 into a float scene target

pipeline.setSkybox(&sky);
pipeline.setSkyboxCamera(view, projection);  // the pipeline has no camera of its own
```

- **One fullscreen draw, no cube mesh.** The view ray comes from the inverse of the rotation-only
  view-projection, and the direction it produces *is* the cube-map lookup — so there is no mesh to
  orient and no seam where faces meet.
- **The view's translation is stripped**, so walking does not move the sky and turning does.
- **Drawn first, not last.** The sky goes in immediately after the scene target is cleared, before
  the game's geometry. The usual technique — draw last at the far plane with `LessEqual` and depth
  writes off — needs a depth configuration the engine layer's `SpriteBatch`-based fullscreen
  mechanism does not carry. The guarantee is identical and is asserted; what is given up is
  skipping sky pixels the scene will cover.
- **Cube coordinate convention.** `EnvironmentProcessor::faceDirection` is where it is written
  down, and `directionToEquirectangular` is its counterpart: longitude across, latitude down, with
  **−Z at the centre of the panorama** — where a camera at its default orientation looks. The two
  are tested as inverses, so the converter cannot disagree with itself.
- **Cost** (`cnaext_skybox_test --benchmark`, 128×128, Mesa llvmpipe): 0.020 ms per frame against
  0.005 ms for a clear alone — one fullscreen pass, which is what it should be.

### Image-based lighting: the precompute

The same `EnvironmentProcessor` turns an environment cube into the three products the split-sum
approximation needs. All three are load-time work, generated once and reused for the run:

```cpp
CNA::Graphics::EnvironmentProcessor processor(device);
auto cube = processor.convertEquirectangular(panoramaTexture, 512);

auto irradiance  = processor.generateIrradiance(cube.get(), 32, 32);          // diffuse
auto specular    = processor.generatePrefilteredSpecular(cube.get(), 128, 5, 64);  // one mip per roughness
auto brdfLut     = processor.generateBrdfLut(128, 128);                       // scene-independent
```

- **Owned by the caller.** Each generator returns a `std::unique_ptr`; the processor keeps no state
  and can be destroyed the moment loading finishes.
- **CPU, not render-to-cube.** A GPU implementation would need float render targets, cube render
  targets and custom effects all present at once — a combination no renderer in the committed scope
  offers. Doing it on the CPU removes the capability gate from the *arithmetic*, which is the part
  that would otherwise have needed one. The price is the time below.
- **It still needs somewhere to put the result, and that is not free.** The convolutions are pure
  CPU maths, but every generator returns a `TextureCube` or `Texture2D` and has to write into it.
  Headless and Stub accept `TextureCube::SetData` and store nothing, so the precompute cannot run
  there and the generators throw (`MOD-1696`). An earlier revision of this document claimed the
  precompute "works identically on every renderer, including Headless"; measuring it showed that is
  false, and the engine-layer tests now probe for cube storage rather than assume it.
- **Quality is `sampleCount`, and the cost is quadratic** for irradiance (it is a sweep over two
  angles) and linear for the other two. The defaults are chosen for quality, not speed.
- **Roughness ↔ mip is one function.** `mipForRoughness` and `roughnessForMip` are public statics
  and are inverses; generation calls one, the sampling shader calls the other, so the two cannot
  drift apart.
- **Seamless by construction.** The sampler picks the cube face *from the direction*, so a filter
  kernel crossing a face edge reads the neighbour rather than clamping to a border. There are no
  seams at any mip, on any renderer, with no `SeamlessCubeMapFilter` capability required.
- **8 bits, and that is a real limit.** CNA's `Texture2D`/`TextureCube` accept
  `SurfaceFormat::Color` only, so the BRDF table is quantised to 8 bits per term and an environment
  brighter than 1.0 has to carry its intensity as a separate scalar rather than in its texels.
- **Cost** (Debug build, single thread, `GenerationCostIsLoadTimeWork`): irradiance 32/32 **3.31 s**,
  prefilter 128/5/64 **2.38 s**, BRDF LUT 128/128 **0.49 s**. An optimised build is several times
  faster. Generate at load, never per frame; halve `sampleCount` where a load screen is unwelcome.

### Image-based lighting: lighting with it

The three products reach a shader through one struct on the effect:

```cpp
ImageBasedLightEXT environment;
environment.Irradiance          = irradiance.get();
environment.PrefilteredSpecular = specular.get();
environment.BrdfLut             = brdfLut.get();
environment.PrefilteredMipCount = 5;      // what generatePrefilteredSpecular was asked for
environment.Intensity           = 1.0f;   // the environment's brightness lives here, not in texels

pbrEffect.setImageBasedLightEXT(environment);   // SkinnedPbrEffect has the same setter
```

- **`ImageBasedLightEXT` is in the XNA namespace**, beside `PunctualLightEXT` and
  `ShadowCascadeStateEXT`, and always compiled. An effect's public surface must not change with a
  build flag, and an always-compiled XNA header cannot include one that exists only under
  `CNA_CNAEXT`. The engine layer generates the products; the XNA layer consumes them.
- **All three or none.** A bundle missing any texture is inert and the flat `AmbientLightColor`
  stays in charge — two thirds of a split sum is a wrong answer, not a partial one.
- **Flat ambient and IBL are exclusive, never summed.** Both stand for light arriving from the
  environment, so adding them counts it twice. Binding a valid bundle zeroes the flat term for that
  draw; detaching it restores exactly what the game set.
- **Occlusion multiplies the environment term, not the direct light** — and a shadow does the
  opposite. A shadow map answers the visibility of *one* light; an occlusion map describes how much
  of the surrounding sky reaches a crevice. Both are asserted in `cnaext_ibl_test`.
- **Roughness is a mip level**, `roughness * (mipCount - 1)`, the same formula the generator used.
  On GLSL ES 1.00 profiles (WebGL1, GLES2) a fragment shader has no `textureLod`, so those read the
  base level and a rough surface reflects a sharp environment. That is a real, visible limitation
  of those two profiles rather than a silent one.
- **White furnace** (`cnaext_ibl_test`, environment at half intensity, albedo 1, no lights;
  128/255 would be exact energy conservation): roughness 0.1 → **159**, 0.4 → **139**, 0.7 → **129**,
  1.0 → **155**. The split sum with 8-bit products and an 8-sample irradiance sweep gains a little
  energy at both ends of the roughness range and is nearly exact in the middle.
- **Cost** (`cnaext_ibl_test --benchmark`, 96×96, Mesa llvmpipe): flat ambient **0.064 ms/frame**,
  image-based **0.066 ms/frame** — three per-fragment texture reads more, and it shows as about 3 %.

### Materials: `PbrMaterial` and the effect it describes

`PbrMaterial` is a value: storable, comparable, hashable, and — the point of Phase 13 — a
**lossless** description of what `PbrEffect` can render.

```cpp
CNA::Graphics::PbrMaterial material;
material.setAlbedoTexture(albedo);
material.setMetallicFactor(0.0f);
material.setRoughnessFactor(0.35f);
material.setEmissiveFactor(Vector3(4.0f, 4.0f, 4.0f));   // above 1: HDR emissive
material.setAlphaMode(AlphaModeEXT::Mask);
material.setAlphaCutoff(0.4f);
material.setDoubleSided(true);

applyMaterial(material, effect);            // every field lands on the effect
applyMaterialState(material, device);       // blending and culling, if the app wants them applied
assert(extractMaterial(effect) == material);  // exact, and asserted in the tests
```

The mapping, field for field (`MOD-1300`):

| `PbrMaterial` | `PbrEffect` |
|---|---|
| `AlbedoTexture` … `SpecularColorTexture` (7 slots) | `Texture`, `NormalMap`, `MetallicRoughnessMap`, `EmissiveMap`, `OcclusionMap`, `SpecularMapEXT`, `SpecularColorMapEXT` |
| `AlbedoColor` (`Color`) | `DiffuseColor` (RGB) + `Alpha` |
| `MetallicFactor`, `RoughnessFactor` | `MetallicFactor`, `RoughnessFactor` |
| `EmissiveFactor` (`Vector3`) | `EmissiveFactor` |
| `NormalScale`, `OcclusionStrength` | `NormalScaleEXT`, `OcclusionStrengthEXT` |
| `Ior`, `SpecularFactor`, `SpecularColorFactor` | `IorEXT`, `SpecularFactorEXT`, `SpecularColorFactorEXT` |
| `AlphaMode`, `AlphaCutoff`, `DoubleSided` | `AlphaModeEXT`, `AlphaCutoffEXT`, `DoubleSidedEXT` |
| `TextureCoordinateSet(slot)` ×7 | `TextureCoordinateSetsEXT[0..4]` + the two specular selectors |
| `TextureTransform(slot)` ×7 | `TextureTransformsEXT[0..4]` + the two specular transforms |
| `BaseColorTextureSrgb`, `EmissiveTextureSrgb`, `SpecularColorTextureSrgb`, `OutputEncodedToSrgb` | the four `*IsSrgbEXT` / `EncodeOutputToSrgbEXT` properties |

- **Not in the material, deliberately**: matrices, lights, fog, shadows and image-based lighting.
  Those describe the scene a material stands in; putting them here would make two draws of the same
  material in two places two materials. `applyMaterial` leaves them untouched.
- **Ownership** (`MOD-1314`): a material never owns its textures. It is a description, and one that
  owned GPU resources could not be held by value in a container of materials. Keeping a texture
  alive is `PbrEffect::SetOwned*`'s job, and it stays on the effect.
- **Alpha mode and sidedness are device state**, so `applyMaterialState` is a separate call an
  application makes when it wants them applied: `Blend` → `BlendState::NonPremultiplied` (PBR
  effects emit straight RGB, and CNA does not sort), `Mask` and `Opaque` → `BlendState::Opaque`
  (a cutout is a discard in the shader, not blending), `doubleSided` → `RasterizerState::CullNone`.
- **From glTF**: `materialFromGltfEXT(importedMaterial, textures)` turns the importer's own decoded
  record into a material. It is a template over a concept rather than a function of
  `GltfImport::MaterialOut` by name, so the engine layer needs neither the content module nor
  `cgltf` to build. The importer's runtime path is unchanged; this is a second, optional reading.
- **One value quantises**: glTF's `baseColorFactor` is four floats and a material's albedo is a
  `Color`, so importing rounds it to 8 bits per channel. The two paths' draw parameters are
  otherwise identical, and that bound (≤ 1/255 on the base colour, exact everywhere else) is
  asserted rather than assumed.

### Many objects: instancing, LOD and culling

Three small classes that compose into one frame, none of which needs anything the XNA API did not
already have:

```cpp
CNA::Graphics::InstancedRendererEXT renderer(device, meshPart);
CNA::Graphics::FrustumCullerEXT culler;
CNA::Graphics::LodGroupEXT lod;

culler.setCamera(view, projection);
culler.cullTransforms(worldMatrices, worldBounds, visible);  // only what can be seen
renderer.setInstances(visible);                              // one upload, buffer reused
renderer.draw(effect);                                       // one draw call
```

- **The instance stream is four `Vector4`s at `TextureCoordinate` usage indices 1–4**, 64 bytes,
  which is what CNA's renderers already expect and what the stock shaders bind to attribute
  locations 12–15. So an ordinary `BasicEffect` or `PbrEffect` instances unchanged: the effect's own
  `World` still applies, with the per-instance transform on top.
- **The buffer grows and is otherwise reused**, so re-uploading the same count every frame allocates
  nothing — `getInstanceCapacity()` is the assertable form of that.
- **The per-instance fallback is opt-in.** Where a renderer cannot instance, `draw` throws unless
  the caller has enabled the fallback: one draw call per instance is not a slower version of the
  same program, it is a different one, and a game that would rather know than crawl says nothing
  and catches the exception. The fallback needs an `IEffectMatrices` effect, and restores its
  `World` afterwards.
- **The per-instance tint stream is off by default** and needs a `ShaderEffect` that declares it:
  the stock shaders already occupy all sixteen attribute locations XNA's profile guarantees, so
  there is no room for a fifth per-instance element.
- **`LodGroupEXT` orders its levels finest first** — ascending distance in `Distance` mode,
  *descending* pixel size in `ScreenSpaceError` mode, because a size threshold shrinks where a
  distance grows. Optional hysteresis stops an object hovering on a boundary from switching every
  frame; it holds only across the neighbouring boundary, so a teleport across two levels still
  changes level immediately.
- **`FrustumCullerEXT` is a sweep over XNA's own `BoundingFrustum`**, writing indices into a vector
  the caller reuses. A transform with no matching bound is kept, not dropped: losing geometry
  because a caller forgot a bound is the more surprising of the two failures.
- **Cost** (`cnaext_instancing_lod_test --benchmark`, 128×128, Mesa llvmpipe): 1 000 cubes
  instanced **0.96 ms** against **51.5 ms** looped (**54×**); 10 000 cubes instanced **22.7 ms**
  against **538 ms** looped (**24×**).

### Compute shaders and storage buffers

```cpp
if (device.SupportsCapability(GraphicsCapability::ComputeShaders)) {
    CNA::Graphics::StorageBufferT<float> values(device, 1024);
    values.setData(input);

    CNA::Graphics::ComputeShader doubler(device, source);   // GLSL ES 3.10 on EasyGL
    doubler.bindStorageBuffer(0, values.getBuffer());
    doubler.setUniform("uCount", 1024);
    doubler.dispatch(1024 / 64);                            // local_size_x = 64
    const std::vector<float> result = values.getData();
}
```

| Capability | Where |
|---|---|
| `GraphicsCapability::ComputeShaders` | GL ES ≥ 3.1, desktop GL ≥ 4.3. Never WebGL — no version of it has compute. Decided by the **runtime** context, so an EasyGL build that asked for ES 3.0 and received 3.2 gets compute. |
| Storage buffers, dispatch, barriers | Everywhere compute is. |
| `Texture2D` as a compute **image** | Desktop GL only (`ComputeShader::isImageBindingSupported`). GL ES requires an immutable texture (`glTexStorage2D`) and CNA allocates textures mutably, so the binding is refused with that reason rather than issued and silently dropped. |
| Sampling a `Texture2D` **from** compute | Everywhere compute is — sampling has no immutability requirement. This is the route auto-exposure takes. |

- **Nothing is silent.** A renderer without compute makes both wrappers throw
  `System::NotSupportedException` naming it, at construction rather than at some later dispatch that
  quietly did nothing. A dispatch past the device's own work-group limit throws before submission,
  naming the axis and the number.
- **Barriers**: `dispatch` already issues `ShaderStorage | ShaderImageAccess | BufferUpdate`, so a
  read-back or a following dispatch needs nothing from the caller. What the wrapper cannot know is
  how the *rest of the pipeline* will read the data — a buffer about to be drawn as vertices needs
  `VertexAttribArray`, a texture about to be sampled needs `TextureFetch` — and that is
  `ComputeShader::barrier`.
- **`Texture2D::GetData` never shows compute writes.** It answers from the CPU pixels the texture
  was uploaded with. Compute output reaches the CPU through a storage buffer, or reaches the screen
  by being sampled in a draw.
- **The gap worth naming**: a storage buffer cannot be bound as a vertex stream, so a
  GPU-resident particle system has to come back through the CPU. Measured at 100 000 particles on
  llvmpipe: GPU step **0.881 ms**, CPU step **2.401 ms**, read-back **0.806 ms**.

**Auto-exposure** (`AutoExposureEXT`) is the first consumer inside the engine layer, and the reason
`MOD-308` deferred auto-exposure until compute existed:

```cpp
CNA::Graphics::AutoExposureEXT exposure(device);
exposure.update(*pipeline.getSceneTarget(), elapsedSeconds);
exposure.applyTo(pipeline.getSettings());     // TonemapPass already reads getExposure()
```

It reduces a 64×64 sample grid in shared memory to 64 partials and finishes the sum on the CPU —
64 floats cost less to fetch than a second kernel launch costs to start. The average is a
**log**-average, so a handful of very bright pixels cannot crush the frame, and adaptation is
exponential and **asymmetric**: adapting to a brighter scene is fast, to a darker one slow, as an
eye is. The speeds are named for the scene, not the exposure — a brighter scene means a *lower*
exposure, and getting that comparison backwards is invisible until something moves.

Legend: ✅ implemented and verified · 🟨 partial · ⬜ not implemented · ⛔ deliberately unsupported.

## Related documents

- [`../CNAEXT.md`](../CNAEXT.md) — the design of this layer (what it is, what it is not, why).
- [`../plan_modern.md`](../plan_modern.md) — the task backlog implementing that design.
- [`ascii-post-process-effect.md`](ascii-post-process-effect.md) — the ASCII effect in detail.
- [`graphics-renderer-feature-matrix.md`](graphics-renderer-feature-matrix.md) — the XNA-level
  per-renderer feature matrix this one sits above.
