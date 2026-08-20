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
| Indirect draws | ✅ GL ES ≥ 3.1 / GL ≥ 4.0, runtime-probed; both routes, including per-instance streams | ⬜ | ⬜ | ⬜ — `SupportsIndirectDrawEXT()` is false by default and `GraphicsDevice` refuses the draw naming the renderer |
| GPU culling into an indirect draw | ✅ needs compute, indirect draw, executed effect source and a vertex-stage SSBO — all four probed | ⬜ | ⬜ | ⬜ — `GpuInstanceCuller` refuses and names the missing requirement; there is no fallback, because a CPU path would not remove the stall |
| Particles | ✅ GPU simulation + instanced billboards | 🟨 CPU simulation and the stock-effect draw work anywhere | 🟨 same | 🟨 — `ParticleSystem` falls back to its CPU path and the same particles appear, more slowly |

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
| `SdlRenderer` | ⛔ 2D-only, and now measured | **0 engine-layer failures**; no `ThreeD`, so `RenderPipeline` passes through and every 3D subsystem reports false. Two fixtures that built a `VertexBuffer` unconditionally now gate on `ThreeD` because of it. |
| `SdlGpu` | 🟨 measured | Engine-layer suites run against it: **0 failures**. Accepts a custom effect and does not execute its source, so every shader-based subsystem reports false and copies through. No float targets, no compute. It also answers `Instancing: yes` while `DrawInstancedPrimitives` refuses — a promise it does not keep, which is why `InstancedRendererEXT` now asks `MultiStreamVertexInput` as well. Needs `libshaderc-dev` to build. |
| `Bgfx` | 🟨 measured | **0 engine-layer failures** on its OpenGL backend. Accepts an effect without executing its source; no float targets, no compute, no shadow sampling, no IBL. Its descriptor did not compile until 2026-08-19. No `shaderc` regeneration was needed to measure it — the passes never reach bgfx's shader pipeline — but implementing them would need it. |
| `WebGPU` | 🟨 measured | **0 engine-layer failures** on native `wgpu-native` (Vulkan/lavapipe), after it found two engine-layer bugs — `DepthNormalPrepass` trusting the `MultipleRenderTargets` capability WebGPU promises and does not keep, and a chain test asking only `CustomEffects`. Still depends on `WEBGPU-76` for custom WGSL. |
| `Magnum` | 🟨 measured | **0 engine-layer failures** on desktop GL 4.5. Accepts an effect without executing its source. One of only two renderers measured that reports `MultiStreamVertexInput`. Single-context: a second `MagnumRenderer` is now refused by name rather than aborting the process. |
| `DirectX11` | 🟨 measured | **0 engine-layer failures**, run under Wine on a real D3D11 device at feature level `0xB100` — the first Windows renderer this layer has been measured on. No float targets, shadow sampling, IBL or compute; every gated suite skips. |
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
| `Blend2D` | ⛔ 2D-only, and now measured | **0 engine-layer failures**; answers no to every capability the layer asks about. |
| `OpenVg` | ⛔ 2D-only, and now measured | **0 engine-layer failures**; the one renderer that supports *nothing at all*, which is what showed `RequireCapabilityTest` had assumed every renderer supports something. Needs `libglu1-mesa-dev`. |
| `Gdi` | ⛔ 2D-only by identity |  |
| `Glide` | ⛔ fixed-function by identity |  |
| `FreeDirect` | ⬜ not verified |  |
| `TinyGL` | ⛔ fixed-function, and now measured | **0 engine-layer failures**. No shaders, render targets, stencil or scissor; 1-bit colour-key transparency. Single-context by design, and the only one of the three that already refused a second device cleanly. |
| `PortableGL` | ⛔ measured, and refused | **0 engine-layer failures**: rasters 3D, accepts no custom effect. `MOD-1617` decided against float targets there — a CPU rasterizer that cannot run shader source has nothing downstream to use them. |
| `OpenGL4` | 🟨 measured | **0 engine-layer failures**. Accepts an effect without executing its source; no float targets, compute, shadow sampling or IBL. Its own renderer, not EasyGL. |
| `OpenGL2` | 🟨 measured | **0 engine-layer failures**; identical answers to `OpenGL4`. Float targets would need `ARB_texture_float`. |
| `OpenGL1` | ⛔ fixed-function, and now measured | **0 engine-layer failures**: rasters 3D, accepts no custom effect. Its descriptor did not compile at all until 2026-08-19, so the identity could not be selected. |
| `OpenGLES1` | ⛔ fixed-function; not measurable here | The container's GLX offers only an ES 3.2 profile, so an ES 1.x context request fails with `BadAlloc` before any CNA code runs. |
| `Sokol` | 🟨 measured | **0 engine-layer failures**. Accepts an effect without executing its source. Single-context: `sg_setup` asserts and aborts on a second renderer, so `Sokol::CreateGraphicsRenderer` now refuses by name. |
| `Diligent` | 🟨 measured | **0 engine-layer failures** on the Vulkan device it selects here. The only renderer measured that answers `CustomEffects: no` outright while still rasterizing 3D, so there is no promise/behaviour gap to catch. Measured on one native API, not the two the row wants. |
| `Llgl` | 🟨 measured | **0 engine-layer failures**, after its descriptor was made to compile at all. Refuses a `RenderTargetCube` from the constructor, which is why three tests now gate on cube render targets. A second device leaves LLGL's globals broken and the teardown terminates — recorded for `plan_llgl.md`. |
| `Igl` | 🟨 partial | Capabilities measured (accepts effects, executes no source; reports `MultiStreamVertexInput`). The suite cannot finish: IGL's own *"Dangling IContext reference left behind"* assert raises `SIGTRAP` in Debug and segfaults in Release whenever a device is destroyed — including on the copy-through path. Needs `-DENABLE_OPT=0`. |
| `Metal` | ⛔ macOS only | Hard configure gate; no cross-compilation route from Linux. |
| `Fna3d` | ⬜ builds, cannot run here | `FNA3D_CreateDevice` fails for every driver in this container, so nothing can be measured. |
| `Wicked` | ⬜ not built here | Needs a hand-supplied Wicked Engine clone (`CNA_WICKED_ROOT`). |

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

### Threading: owner-thread only

`plan_modern.md` `MOD-744`. Construct a `RenderPipeline`, configure it, call `begin`/`end` and
destroy it **on the thread that owns the `GraphicsDevice`**, and nowhere else. The same applies to
every other type in this layer.

This is not caution, it is the only thing that would be true: the engine layer is a thin arrangement
of `GraphicsDevice`, `SpriteBatch` and `Effect` calls, and none of those is thread-safe. Adding a
lock here would make the *layer's* own state safe while every call it makes underneath stayed
unsafe — which is worse than no lock, because it reads as a guarantee.

One place where this could surprise: `RenderPipeline` subscribes to `GraphicsDevice::DeviceReset`
(`MOD-715`), so its handler runs on whichever thread raised that event. For every renderer today
that is the owner thread, and a renderer that changed it would need to say so.

### The depth/normal prepass, and what a game has to do

`plan_modern.md` `MOD-500`–`MOD-507`, `MOD-529`. Screen-space effects need to know the *shape* of
the scene, not its colour. `DepthNormalPrepass` produces the two images that describe it — how far
each pixel is, and which way it faces — and the app drives it, the same way it drives `ShadowMap`.

```cpp
CNA::Graphics::DepthNormalPrepass prepass(device, width, height);

for (int pass = 0; pass < prepass.getPassCount(); ++pass)
{
    prepass.begin(pass, view, projection, nearPlane, farPlane);
    DrawSceneGeometry(prepass.getPrepassEffect());        // skinned meshes:
    DrawSkinnedGeometry(prepass.getSkinnedPrepassEffect());
    prepass.end();
}

pipeline.setDepthNormalInputs(prepass.getDepthTexture(), prepass.getNormalTexture());
```

**Why a prepass and not the depth attachment** (`MOD-500`). Sampling the depth buffer the scene has
already written would be free, and CNA cannot do it portably: several renderers never expose their
depth attachment as a texture, the ones that do disagree about its precision and about whether it is
readable while still bound, and the value in it is non-linear differently per API. Drawing the
geometry a second time costs a pass and is identical everywhere — the trade this layer makes
throughout.

**What a game must do, and what happens if it does not.** SSAO reads what the pipeline was given. If
`setDepthNormalInputs` was never called, or was given nulls, `SsaoPass::apply` copies its input
through: **the frame renders, without ambient occlusion**. It does not throw, and it does not draw a
black screen. That is deliberate and matches every other pass in the layer — but it also means a
missing prepass looks like "SSAO is not doing much" rather than like an error, so an app that
expects AO and does not see it should check here first.

**`isSupported()` will not tell you.** This paragraph used to say the pass answers
`isSupported() == false` without its inputs, and that was never true: the method takes a
`GraphicsDevice` and nothing else, so it cannot see a frame's inputs at all
(`plan_modern.md` `MOD-2006`). The division is worth stating plainly, because a game that gates its
prepass on `isSupported()` gets `true` and then wonders why the effect does nothing:

| Question | Asked of | Answered by |
|---|---|---|
| Can this renderer run the pass at all? | the device | `isSupported(device)` — false, and the pass is skipped |
| Does this frame carry what the pass needs? | the `PostProcessContext` | `apply()` — the input is copied through |

The same split applies to every pass that reads the prepass, `SsrPass` included, and to `SsrPass`'s
camera: a pipeline that never called `setCamera` still reports the pass as supported, and the frame
comes back unreflected.

**The loop is not decoration.** With `MultipleRenderTargets` the prepass writes both images in one
pass and `getPassCount()` is 1; without, it writes them in two passes over the same geometry and the
count is 2. Writing the loop is what makes an app correct on both, and on renderers that have MRT it
costs one iteration.

**The encodings**, both stated because a consumer has to match them exactly:

| Image | Encoding | Cleared to |
|---|---|---|
| Depth | Linear view depth, normalised by the far plane, in `HalfSingle` — or packed across the four channels of a `Color` target where float targets are missing (`MOD-507`) | white: "nothing here, infinitely far" |
| Normals | View-space, `n * 0.5 + 0.5`, in `Color` | `(128, 128, 255)`: facing the camera |

Both clear values are chosen so that an *unwritten* texel is harmless. Black depth would make every
empty pixel the nearest possible occluder and darken the whole frame; a black normal decodes to
`(-1,-1,-1)`, a direction no visible surface has, and SSAO reading it manufactures occlusion out of
empty space.

**Consumers must not write the decoder themselves** (`MOD-504`). `getDepthDecodeGlsl(packed)` returns
the GLSL that reads this prepass: `cnaDecodeLinearDepth(vec4)` and
`cnaViewPositionFromDepth(vec2, float, mat4)`. The encoding and its inverse have to agree, and two
copies that happen to agree today are one edit away from an SSAO that darkens the wrong pixels.

Two limits worth knowing. Packed depth reaches only *one texel short of* 1.0 — `fract(1.0)` is zero,
so an unclamped far-plane depth would pack to all-zeroes and read back as the nearest possible
surface, inverting the most common value in the buffer. And the normal is transformed by the upper
3×3 of the world matrix rather than by its inverse transpose, so **non-uniform scale skews it**;
correcting that needs an inverse per draw that nothing else in this layer pays for.

### FXAA, and when to prefer MSAA instead

`plan_modern.md` `MOD-604`, `MOD-609`.

The two solve the same problem from opposite ends, and the choice is usually made by the renderer
rather than by taste:

| | MSAA | FXAA |
|---|---|---|
| Where it works | Geometry edges | Any edge, including inside a texture or produced by a shader |
| What it needs | `MultiSampleAntiAliasing`, and a multisampled *render target* if the scene is not drawn to the back buffer | `CustomEffects`, and a renderer that runs the shader source |
| Cost | Memory and bandwidth, paid on every pixel of the frame whether it has an edge or not | ~3.5 ms at 720p, ~8.2 ms at 1080p on the reference renderer |
| What it costs you | Nothing in sharpness | Some texture detail: it cannot tell an edge from a fine pattern |
| HDR | Resolves before tonemapping, so highlights average in scene-referred space | Runs *after* tonemapping, on displayed pixels, which is why `RenderPipeline` orders it last |

**Prefer MSAA where the renderer has it and the scene is geometry-heavy.** It is sharper, and it is
the only one of the two that antialiases correctly in HDR — averaging four samples of a highlight
before the tonemapper is not the same as blurring the tonemapped result, and the difference is
visible on a bright edge.

**Prefer FXAA where MSAA is unavailable or the aliasing is not geometric.** Alpha-tested foliage,
a shader-produced pattern, a normal map at a grazing angle: MSAA does nothing for any of them,
because there is no geometric edge to sample.

**Using both is legitimate** and not double work: MSAA removes the geometric aliasing, FXAA catches
what is left. The pipeline does not stop you, and on the reference renderer the combination costs
MSAA's memory plus FXAA's 3.5 ms.

Per renderer, the practical position today: EasyGL has both (MSAA up to 4×, and it runs the FXAA
shader). Vulkan reports `MultiSampleAntiAliasing` but its `ShaderEffect` takes SPIR-V, so FXAA is
unavailable there and MSAA is the only option. The 2D-only identities have neither and
`RenderPipeline` passes through. The per-identity matrix below is the authority; this row is the
guidance that goes with it.

### SSAO: what it approximates, and where AO is applied

`plan_modern.md` `MOD-520`, `MOD-521`, `MOD-522`, `MOD-523`.

**AO is multiplied into the frame, not into the ambient term.** A physically-motivated ambient
occlusion darkens only *ambient* light — the sky, the environment map — and leaves direct light
alone, because a lamp shining into a crevice still lights it. CNA's SSAO runs as a post-process on
the composed frame, so it darkens everything, including directly-lit pixels. That is the standard
screen-space approximation and it is stated here because the difference shows up exactly where AO is
most visible: a bright key light raking across a contact region gets darkened when it should not be.
Keep `ssaoIntensity` modest in directly-lit scenes.

**Why AO is not fed into `PbrEffect`'s occlusion slot** (`MOD-521`, refused). That slot exists and
would be the physically right place — the PBR shader multiplies it into the ambient term only. It
cannot take this buffer: the slot is sampled at the mesh's **UV coordinates**, because it is a
per-material texture in texture space, while screen-space AO is indexed by screen position. Binding
one to the other would sample the AO buffer with the model's UVs, which is wrong in a way no tuning
recovers. Doing it properly needs three things this layer does not have: a `gl_FragCoord`-based
sampler added to the PBR shader **in every PBR-capable renderer**, the AO buffer ready *before* the
forward pass rather than after it (so, a second prepass or a deferred pipeline), and a way to say
"this occlusion is screen-space" that the glTF material model has no field for. Recorded as refused
rather than deferred, because the screen-space multiply is a deliberate approximation with a
documented cost, not a placeholder for this.

**Quality presets** (`MOD-522`) map to hemisphere sample counts: `Low` 8, `Medium` 16, `High` 32,
`Ultra` 64. This one is a real performance dial — 8 to 64 samples is 3.4× the time, because the
shader loops over the kernel per texel — which makes it unlike bloom's level count, where the preset
buys width rather than time. Applied by `RenderPipelineSettings::applyRenderQualityPresetEXT()`, the
same explicit call bloom uses.

**Half-resolution AO** (`MOD-523`) is available and **off by default**. AO is a low-frequency signal,
so computing it at half resolution and letting the compose pass's bilinear read upsample it costs
much less quality than the pixel count suggests — but thin contact shadows lose definition, which is
exactly where AO earns its keep. Both paths are asserted to produce occlusion rather than only the
default one, since a half-resolution path that silently produced nothing would look like AO merely
being weak.

### Screen-space reflections, and the two things they cannot do

`plan_modern.md` `MOD-2000`–`MOD-2009`. `SsrPass` reflects the scene in itself: it walks the
reflected ray forward in view space, projects each step back to a screen position, and asks the
depth image whether anything is standing there. It reads the same two images SSAO does and needs one
more thing SSAO does not — a camera, supplied by `RenderPipeline::setCamera`.

```cpp
pipeline.setCamera(view, projection, nearPlane, farPlane);
pipeline.setDepthNormalInputs(prepass.getDepthTexture(), prepass.getNormalTexture());
pipeline.getSettings().setSSREnabled(true);
```

**Only what is already on screen can be reflected.** A surface facing away from the camera, an
object outside the viewport, and anything hidden behind nearer geometry have no colour in the source
image, so they have no reflection. This is the defining limit of the technique, not a shortcoming of
this implementation, and no amount of tuning moves it: a scene that needs to reflect off-screen
content wants an environment map — `ImageBasedLightEXT`, which this pass does not replace and is
not a fallback for.

**The second limit is the depth image's silence about thickness.** It records where a surface is and
nothing about how deep the object behind it goes, so `thickness` stands in for that. Too small and
rays pass through everything; too large and a ray reflects off a surface it flew well behind.

Where the pass sits, and why: **after SSAO, before the tonemapper.** After SSAO because what a
mirror shows should be the shaded scene rather than the unshaded one; before tonemapping because a
reflection carries scene-referred colour, and mixing it in after the range is compressed makes a
reflected highlight indistinguishable from a reflected white wall.

| Setting | Meaning | Failure when set wrong |
|---|---|---|
| `SSRMaxDistance` | how far a ray travels, in world units | short: reflections stop mid-surface |
| `SSRStepCount` | steps the ray is marched in | low: thin objects are stepped over entirely |
| `SSRThickness` | how far behind a surface a hit still counts | see above |
| `SSRDepthBias` | how far past a surface a hit must be | too small: **every mirror reflects its own colour** |
| `SSREdgeFade` | fade width at the frame border, in screen fractions | zero: reflections stop along a hard line down the screen edge |
| `SSRRoughnessBlur` | widest spread at roughness 1, in screen fractions | — |
| `SSRIntensity` | how strongly the reflection is mixed in | — |

**Roughness comes from the prepass, and its default is a mirror.** A rough surface reflects a cone
rather than a point, and roughness lives in the material rather than in the geometry, so
`DepthNormalPrepass::setRoughness` carries it — in the alpha of the normal target, which held
nothing before. Set it between draws inside an open pass, the way a scene with more than one
material describes itself. Its default is **0**, not glTF's fully-rough 1, so an app that never
calls it gets sharp reflections rather than a silently blurred frame. The spread itself is four taps
widened by roughness: the difference between a mirror and a brushed floor, not the BRDF integral a
real cone asks for.

**The step count does not move the reflection.** The march ends holding a bracket — the last point
in front of the surface and the first behind it — and six bisections close it, so raising the step
count sharpens what is found rather than shifting where it is found. Without that, a coarse march
stair-steps every reflected edge and halving the step moves the whole reflection.

### Depth of field, in the units a photographer uses

`plan_modern.md` `MOD-2010`–`MOD-2015`. `DepthOfFieldPass` blurs each pixel by the **circle of
confusion** a thin lens would produce at its distance, so the settings mean what they mean on a
camera: a 135 mm lens at f/1.4 focused two metres away has a shallow depth of field here for the
reason it does in the world.

```cpp
pipeline.setCamera(view, projection, nearPlane, farPlane);
pipeline.setDepthNormalInputs(prepass.getDepthTexture(), prepass.getNormalTexture());
auto& settings = pipeline.getSettings();
settings.setDOFEnabled(true);
settings.setDOFFocusDistance(2.0f);   // world units
settings.setDOFFocalLength(135.0f);   // millimetres
settings.setDOFFNumber(1.4f);
```

**Units, and the assumption inside them.** Focal length is in millimetres, as printed on a lens.
Focus distance is in world units, and the pass assumes **one world unit is one metre**. A game
measuring in centimetres wants a focus distance a hundred times larger — not a different setting,
and not a different focal length.

**The circle is smaller than it feels.** The same optics that make f/1.4 dramatic on a camera
produce, for a 50 mm lens focused at 5 m, a background circle around 0.7% of the frame height. That
is fifteen pixels at 1080p and *under half a pixel* at 64 — which is why this pass's own tests use a
long lens, and why a game that sees "no blur" should check its frame size before its f-number.
`DepthOfFieldPass::circleOfConfusionMillimetres` is public so the number can be computed rather than
guessed at.

**Where it sits: before bloom, and that is a decision.** An out-of-focus highlight should bloom as
the spread circle it became, not as the point it was. Blooming first and blurring the glow afterwards
is wrong in the same way tonemapping before bloom would be.

**What keeps a focused subject's silhouette.** A gather weighted only by the centre pixel's blur
pulls the sharp half's colour into the soft one, and an in-focus subject visibly smears into the
background beside it. A tap contributes here only if its **own** circle of confusion is wide enough
to reach the pixel doing the gathering, which is what scattering light onto a neighbour actually
means. It is the difference between depth of field and a depth-weighted blur.

`maxRadius` caps the gather. It is a budget rather than a look: raising it costs nothing until
something is far enough out of focus to reach it, and lowering it below what the optics ask for
makes the frame sharper than the lens would.

### Bloom: what the numbers mean, and what they do not

`plan_modern.md` `MOD-417`, `MOD-405`, `MOD-409`.

**Bloom here is not physically normalised, and `intensity` is an artistic dial.** A physically based
bloom would conserve energy: light spread into the halo would be light removed from the source, and
the total would be unchanged. CNA's does not do that. It extracts the pixels above a threshold,
blurs them, and **adds** the result back on top of an untouched scene — so raising `intensity` adds
light to the frame rather than redistributing it. That is what almost every game engine does and
what artists expect from the control; it is written down here because "intensity 1.0" looks like it
ought to mean something physical and does not.

The consequence worth planning around: **`intensity` is not portable as a number.** The same value
produces the same look across renderers *for the same scene*, because the maths is the same
everywhere — but it is not comparable with another engine's bloom slider, and it interacts with
exposure. Tune it against a tonemapped frame, not against the raw scene target.

**The threshold is a soft knee, not a cliff.** A hard cut-off makes bloom pop in and out as a
highlight crosses it, which is far more visible in motion than the missing energy just below it. The
knee is half the threshold, and the contribution is squared across it. A threshold above everything
in the scene removes the glow completely — that is what separates bloom from a blur applied to
everything, and `cnaext_bloom_test` checks it.

**The pyramid is walked back up** (`MOD-405`). Each level is half the previous one and holds the
blur of everything above it; the upward walk adds each level into the one above before the final
composite. A single composite of the smallest level — the simpler thing, and what an early draft
did — gives a wide but *flat* glow, because the tighter core the larger levels still carry was
thrown away on the way down. The measurable difference is reach: with the upward walk, more levels
put light further from the source, which is asserted rather than described (`BloomPyramidTest`).

**Where float textures cannot be linearly filtered** (`MOD-407`), the upsample averages four taps by
hand instead of relying on the sampler. That is a box filter rather than the hardware's bilinear one,
so the result is slightly blockier — the alternative, a nearest sample, makes the upsample visibly
stair-step. The pass asks `GraphicsCapability::HalfFloatTextureLinearFiltering` once at construction
and takes the fallback silently; there is no setting for it, because there is no reason to prefer the
worse path where the better one exists.

### Volumetrics: air you can see, and three passes that do it differently

`plan_modern.md` `MOD-2050`–`MOD-2054`. Three passes put light *in the air between things* rather
than on the things themselves, and they are not alternatives to each other -- they cost different
amounts and answer different questions. All three are **off when their own amount is zero**, which
is the default, and each reads its numbers from `RenderPipelineSettings` when the pipeline supplies
one.

| Pass | What it models | What it needs | Cost |
|------|----------------|---------------|------|
| `HeightFogPass` | A medium whose density falls off exponentially with height | Depth, camera | One fullscreen pass, closed form |
| `LightShaftPass` | The bright streaks a screen-space light throws past occluders | Colour only, plus the light's screen position | One fullscreen pass, radial walk |
| `VolumetricFogPass` | A lit, shadowed medium marched in 3D | Depth, camera, light, optionally a shadow map | A slice atlas plus a fullscreen composite |

```cpp
settings.setHeightFogDensity(0.15f);      // 0 is off; this is the switch
settings.setHeightFogFalloff(0.08f);      // how fast density thins with height
settings.setHeightFogBaseHeight(0.0f);    // the world height the density is quoted at

settings.setLightShaftIntensity(0.8f);    // 0 is off
settings.setLightShaftThreshold(0.7f);    // only pixels brighter than this seed a shaft
settings.setLightShaftDecay(0.92f);       // per-step multiplier along the walk

settings.setVolumetricFogDensity(0.3f);   // 0 is off
```

- **Height fog is an integral, not a fade.** What reaches the camera is the medium's density
  *integrated along the view ray*, so a valley fills while the hilltop above it stays clear, and a
  view down through the valley fogs correctly because the integral knows the ray crossed the thick
  part. `HeightFogPass::opticalDepth` is public and is the same formula the shader runs, so the
  optics can be checked without a frame. A **level look is a separate branch** rather than a nudged
  general one: the general form divides by the ray's climb, and pushing that away from zero would
  make a level view's fog depend on the size of the nudge.
- **A shaft is the shape of an occluder.** The pass walks from each pixel towards the light
  gathering brightness, so what you see is where the *bright* pixels were blocked -- the streaks
  are the gaps. The light's screen position is the application's to supply
  (`LightShaftPass::setLightScreenPosition`), because the layer does not know which of a scene's lights
  is the sun and the application already holds the matrices. **Positions outside [0, 1] are
  meaningful and are not clamped**: a light just past the edge still throws shafts inward, and the
  effect fades with how far outside it is rather than cutting off at the border.
- **Volumetric fog earns its cost with the shadow map.** Without one the medium is lit wherever the
  light points, which is haze; with one the beams have edges. It fills a **slice atlas** -- a 2D
  render target holding the depth slices side by side, the same layout `ColorGradePass` reads a 3D
  lookup table from -- because CNA has a `Texture3D` a shader can sample and no render target that
  writes into one, and filling a volume with compute needs the image stores GL ES refuses
  (`MOD-1514`). Slices are spaced quadratically, so the near ones are thinner and the resolution
  stays where the eye is.
- **Order in the chain**: shafts, then volumetric fog, then height fog, all before motion blur.
  Shafts are light travelling through air, so the fog that dims distance should dim them too; and
  fog is part of what the shutter collected, so a moving camera smears the fogged image rather than
  fogging a smeared one.
- **What a dense medium does to a lit scene is *darken* it.** Extinction removes the source's light
  faster than in-scattering adds the medium's own, which is why the tests for volumetric fog start
  from a black frame and measure only what the medium put there. A test that asserts "the frame got
  brighter" is asking the wrong question and will pass for the wrong reasons.

### Motion blur, and the half of it that is not here

`plan_modern.md` `MOD-2030`–`MOD-2034`. `MotionBlurPass` works out where each pixel used to be
rather than storing it: the depth image and the camera give a world position, putting that position
through the **previous frame's** camera says where it was on screen, and the difference is the
pixel's velocity. It needs no new render target.

```cpp
pipeline.setCamera(view, projection, nearPlane, farPlane);   // every frame, before begin()
pipeline.getSettings().setMotionBlurStrength(0.5f);
```

**Camera motion works with no extra effort; object motion is opt-in and has a price.** A turning or
advancing camera blurs correctly out of the box — nothing more is needed than `setCamera` each frame.
A car crossing a static shot does *not* blur unless you ask for it, because nothing in a depth image
says the car moved rather than the world; the two are indistinguishable from one frame.

```cpp
prepass.setVelocityEnabledEXT(true);                 // MOD-2033; off by default
prepass.setPreviousCameraEXT(lastView, lastProjection);
for (int pass = 0; pass < prepass.getPassCount(); ++pass) {
    prepass.begin(pass, view, projection, nearPlane, farPlane);
    for (const auto& object : scene) {
        prepass.setPreviousWorldEXT(object.lastFrameWorld);   // the obligation, per draw
        DrawWith(prepass.getPrepassEffect(), object);
    }
    prepass.end();
}
pipeline.setVelocityInputEXT(prepass.getVelocityTextureEXT());
```

**The obligation is the whole of the cost, and it is on the application.** Every object has to carry
last frame's world matrix, because the prepass draws whatever it is handed and cannot tell one object
from the next. An object that has not moved passes the same matrix twice; a newly spawned one should
pass its *current* matrix, since the identity would give it a one-frame smear from the world origin.
Turn the feature on and supply nothing and the image says everything is stationary — which is exactly
what you had before, so nothing breaks and nothing is gained either.

**With MRT it is a third target in the same pass; without MRT it is a third pass over the geometry.**
That is why it is off by default: on a renderer with no MRT this doubles-and-a-half the prepass's
geometry cost for an effect many games do not need.

**Motion blur treats it as a per-pixel upgrade, not a second mode.** The stored velocity already
contains the camera's contribution, so where it is present it replaces the reconstruction; where the
velocity image has no coverage — the sky, an object drawn without a previous world — the camera path
still runs. Mixing the two per pixel is what stops the sky from acquiring a hole.

**A skinned mesh's velocity is its object's motion, not its deformation.** The previous *pose* is not
reconstructed: doing so needs the previous frame's entire bone set as a second uniform array, which
is an obligation an order of magnitude larger than one matrix per draw.

**Strength is a shutter angle in disguise.** 1 smears the whole distance travelled since the last
frame, which is what a 360-degree shutter records; a real shutter is open for part of the frame, so
lower values are the physical ones.

**`maxDistance` is not a look, it is a hitch guard.** One slow frame makes every velocity enormous,
and without a cap a single stutter smears the whole image — which reads as a defect in the blur
rather than as the dropped frame it is.

**The history advances once per frame, in `end()`.** A game that sets the camera twice in a frame,
or once every other frame, still compares against the camera the previous frame was actually drawn
with. The first frame after a start or a resize has no history at all and is left alone: blurring it
along an arbitrary direction would put a one-frame glitch on every cut.

### The lens and the grade: four passes and where each one belongs

`plan_modern.md` `MOD-2020`–`MOD-2027`. Four small passes, all **off by default**, whose positions in
the chain are decided by one question: is this describing the lens the scene was shot through, or
the image the viewer is looking at?

| Pass | Runs | Because |
|---|---|---|
| `LensFlarePass` | before bloom, scene-referred | its threshold separates a bright *light* from a white *wall*, and after tonemapping those are the same number; a ghost is a real image of the light, so it should bloom as one |
| `ColorGradePass` | after tonemapping, before FXAA | a lookup table indexed by scene-referred values is asked about numbers past 1.0, where it has nothing to say; the edge filter should see the contrast the viewer will see |
| `ChromaticAberrationPass` | after tonemapping | it is the lens in front of the viewer, not the one the scene was shot through |
| `FilmGrainPass` | last, after FXAA | an edge filter handed fresh noise spends its budget smoothing the grain instead of the edges |

**Grading is a table, not a list of knobs.** A colourist's decisions arrive as a 3D LUT, and anything
a table can express — a curve, a tint, a bleach bypass, a whole film emulation — costs the same one
lookup. The table is a strip of `size` slices of `size` by `size`, so a 32-entry table is a 1024 by
32 texture; that is what every grading tool exports to and the only layout a renderer without 3D
textures can sample. Anything that is not such a strip is **refused by name**: read at the wrong
slice count a strip grades the frame into colours nothing in the table names, which is a wrong image
that looks deliberate. `ColorGradePass::createIdentityLut` gives a starting point that changes
nothing.

**Aberration is radial, and that is the whole effect.** The offset scales with distance from the
axis, so the centre of the frame stays sharp however strong the setting is and the corners fringe. A
constant offset would fringe the middle too and reads immediately as a bug.

**Grain lives in the midtones.** Uniform noise across the range reads as a broken sensor; real grain
is buried in blacks and invisible in blown highlights, because that is where the emulsion stops
responding. The pattern is a function of the pixel and of `PostProcessContext::elapsedSeconds` and
nothing else, so a rendered sequence is reproducible rather than merely noisy.

**Ghosts land on the far side of the centre.** A bright window at the top of the frame throws its
reflections along the bottom, because that is what a reflection between lens elements does. It is the
one property that makes flare read as a lens rather than as a smear.

### Tonemapping: what goes in, what comes out, and what CNA does not do

`plan_modern.md` `MOD-316`, `MOD-320`.

**The contract.** `TonemapPass` takes **linear, scene-referred** colour — values where 1.0 is "as
bright as white paper" and 8.0 is a genuinely eight-times-brighter highlight — and produces
**display-encoded** colour, gamma applied, ready for an 8-bit back buffer. It is the boundary
between the two, and everything else in the chain is on one side of it: passes that reason about
scene values (SSAO, bloom's threshold) run before, passes that reason about displayed pixels (FXAA)
run after. `RenderPipeline` enforces that order and does not let you change it.

**What CNA does not do: colour management.** There is no colour space beyond "linear in, gamma out".
No sRGB primaries versus Display-P3, no white-point adaptation, no ICC profile, no HDR10 or
scRGB output. A game that needs any of those needs a pass of its own. This is stated because the
absence is easy to mistake for a default: a tonemapper is *where* colour management would live in a
renderer that had it, and CNA's does not have it.

**The five operators**, measured on a real gradient by `cnaext_tonemap_test` rather than described:

| Mode | What it does | At a scene value of 8.0 (exposure 4) |
|---|---|---|
| `None` | Nothing but gamma. | clips to 255 |
| `Reinhard` | `x / (1 + x)`; the gentlest, and the one that desaturates highlights most. | 230 |
| `Filmic` | The Hable curve with a toe and shoulder. | 243 |
| `Aces` | The ACES filmic approximation; brightest of the four in the highlights. | 252 |
| `Uncharted2` | The Hable/Uncharted 2 curve, normalised by a white point. | 219 |

All five are monotonic — brighter in is never darker out — which the example asserts across the
whole ramp rather than at a sampled point, because a non-monotonic curve produces banding and
inverted highlights that one sample cannot reveal.

**`RenderQuality` does not affect tonemapping, deliberately** (`MOD-320`). Every other subsystem has
a quality dial: shadow resolution, bloom iterations, SSAO sample count. Tonemapping has none, and it
is worth saying why rather than leaving a gap in the preset table. The operator is a curve applied
once per texel — there is no sample count to reduce and no resolution to lower, since the pass must
touch every pixel of the frame it is display-encoding. Its measured cost is close to a plain copy
(`docs/cnaext-perf.md`), so there would be little to win. And the operator is an **artistic**
choice, not a performance one: silently changing the curve because a player selected "Low" would
change how the game looks, which is not what a quality preset is for.

### Letting the GPU decide how much to draw: indirect draws

`plan_modern.md` `MOD-2090`. An indirect draw takes its vertex count, instance count and offsets out
of a GPU buffer instead of from its arguments. That is the one piece of GPU-driven rendering the
reference renderer's profile floor can actually reach — mesh shaders and the rest of that family do
not exist below GL 4.6 — and what it buys is that a compute shader can decide how much to draw
without the answer travelling back through the CPU, which is a pipeline stall rather than a copy.

```cpp
if (device.SupportsCapability(CNA::GraphicsCapability::IndirectDraw)) {
    CNA::Graphics::StorageBuffer commands(device, sizeof(CNA::IndirectDrawArguments));
    // ... a compute shader writes the counts into `commands` ...
    device.GetRenderer().MemoryBarrierEXT(
        static_cast<int>(CNA::GraphicsMemoryBarrier::IndirectCommand));
    device.SetVertexBuffer(&mesh);
    effect.Apply();
    device.DrawPrimitivesIndirectEXT(PrimitiveType::TriangleList, *commands.getRendererEXT(), 0);
}
```

**The argument layout is the contract, not an implementation detail.** `CNA::IndirectDrawArguments`
(four words) and `CNA::IndirectDrawIndexedArguments` (five) are the exact bytes the GPU reads, in the
order GL, D3D12 and Vulkan all agree on. A compute shader declaring the same words in the same order
lands on the same memory. `BaseInstance` **must be 0 on GL ES**, which has no base-instance
parameter; that cannot be diagnosed anywhere, because by the time the draw runs the value is in GPU
memory.

**The range checks every other draw performs are impossible here.** `GraphicsDevice` rejects a
primitive range that leaves the bound buffers before every other draw route; for this one the range
*is* the GPU's, so a wrong count is undefined behaviour rather than an exception. The shader that
wrote it owns that obligation. What is still checked: a bound vertex buffer, a bound index buffer,
an applied effect, and an argument offset that is 4-byte aligned and leaves room for the command.

**One buffer can hold a frame's worth of commands.** The byte offset selects which one this draw
runs, which is the shape a GPU-driven pass wants.

**Order the command fetch explicitly.** `GraphicsMemoryBarrier::IndirectCommand` is a separate bit
from `ShaderStorage` because writing a count through a storage binding and fetching it as a command
are two different accesses; ordering only the first can let the fetch read the previous frame's
numbers.

**A wrinkle worth knowing before you plan around it.** The only argument buffer CNA has is
`CNA::Graphics::StorageBuffer`, which is an SSBO and needs GL ES 3.1 / desktop GL 4.3. The indirect
draw itself needs only GL 4.0, so on a desktop context between 4.0 and 4.2 the capability truthfully
reports `true` and there is still nothing in CNA able to hold the arguments. Check both capabilities
if you intend to run there.

**Wireframe is ignored on this route.** The fill-mode fallback the ordinary routes take rebuilds a
line list from the primitive count, and this route has no primitive count to rebuild from; an
indirect draw renders filled rather than pretending otherwise. A compiled (FX) effect is refused
outright for the same kind of reason.

### Decals: gluing an image onto geometry that knows nothing about it

`plan_modern.md` `MOD-2094`. Bullet holes, scorch marks, puddles, tyre tracks. `DecalPass` projects a
texture onto whatever the depth prepass says is already there: for each pixel the depth gives a
position, the decal's inverse transform puts that position in the decal's own space, and the pixel is
painted only if it lands **inside the decal's unit box**. No geometry is generated, and the receiving
mesh is not modified or even consulted.

```cpp
CNA::Graphics::DecalPass decals(device);
decals.setPrepassInputs(prepass.getDepthTexture(), prepass.getNormalTexture());
decals.setCamera(view, projection, farPlane);
// ... the scene target is bound ...
decals.draw(&bulletHole, Matrix::CreateScale(0.4f) * hitTransform, width, height);
```

**The box is the contract.** The decal occupies the local cube from -0.5 to +0.5 on every axis, the
texture maps to its local X and Y, and it projects along local **+Z**. So the box's *depth* extent is
what keeps a decal off the wall behind the crate it was meant for: a surface further away than the
box's far face is outside it and is not painted. Scale the Z axis to how far in front of and behind
the target surface the decal should reach.

**Give it the prepass normals.** Without them a decal projected at a glancing angle smears across a
surface nearly parallel to its own axis. With them, `setMaxSlopeAngle` rejects those pixels outright;
the default accepts a good deal of tilt, so a decal still wraps a curved surface.

**A pixel with no surface is not painted.** Where the prepass depth is at the far plane there is
nothing to glue anything to, and painting it is how a decal ends up floating in the sky.

**It composites, and it is the only pass here that does.** Every post-process pass replaces its
destination; a decal blends onto the frame, so it uses `BlendState::NonPremultiplied` and the decal
image's own alpha is the mask. Draw decals after the scene and before the post-process chain, so
they are tonemapped and graded with everything else.

**Cost is one fullscreen pass per decal**, which is what a screen-space projection costs when it is
not batched. `DecalPass::isInsideDecalBox` is offered as a plain static so a game can decide whether
a decal is worth drawing at all before spending one.

### Particles

`plan_modern.md` `MOD-2095`. An emitter, a simulation and a draw. Particles are simulated on the GPU
where the device has compute and on the CPU where it does not, and drawn as camera-facing billboards
in one instanced call.

```cpp
CNA::Graphics::ParticleSystem sparks(device, 2048);
CNA::Graphics::ParticleEmitterSettings settings;
settings.Position = muzzle;  settings.Direction = forward;
settings.EmissionRate = 400.0f;  settings.Lifetime = 0.8f;
sparks.setSettings(settings);
sparks.reset();

sparks.update(elapsedSeconds);
sparks.draw(view, projection, &sparkTexture);
```

**The rate and the lifetime together decide how many particles exist.** `EmissionRate * Lifetime`
slots are live; ask for more than the capacity holds and the count is clamped, with
`isEmissionRateClamped()` saying so rather than the system quietly emitting fewer than the settings
claim. `reset()` staggers ages across one lifetime so emission is continuous from the first frame
rather than arriving as one puff.

**Particles do not die permanently** — a slot whose age passes its lifetime is born again at the
emitter, one generation on, carrying the overshoot into its new age so a long frame does not shorten
every lifetime it spans. Settings changes apply to the next particle born, not to those in flight.

**The two simulations are one simulation.** The GPU and CPU paths are written to the same
specification, down to the float expressions: the spawn values come from an integer hash that is
bit-identical in GLSL and C++, so a comparison between the two paths is meaningful and is asserted
rather than assumed.

**The GPU path reads back nothing.** The vertex shader reads the buffer the compute shader wrote,
which needs a storage buffer readable from a vertex stage (see `GpuInstanceCuller` for why that is
its own requirement) plus `GraphicsCapability::Instancing`. Where any of that is missing the system
falls back to the CPU — and here a fallback is the right answer, unlike for GPU culling: the same
particles appear, only simulated more slowly. `usesCompute()` says which ran.

**`setSimulationOnCpuEXT` pins it to the CPU** on a device that has both. Worth having on a tile GPU
where a dispatch and its barrier cost more than stepping a few hundred particles — and it is how the
fallback gets exercised at all on a machine that does not need it.

**`readParticlesEXT()` is a stall** when the GPU path is running. Tests and tools only.

### Culling that becomes the draw: `GpuInstanceCuller`

`plan_modern.md` `MOD-2091`. A compute shader tests every instance against the frustum, compacts the
survivors, and writes the surviving count straight into an indirect draw command. One call then
draws them. **Nothing about the result is read back to submit the frame** — which is the whole point,
because reading a cull verdict back is the CPU waiting on work it has only just submitted.

```cpp
CNA::Graphics::GpuInstanceCuller culler(device);
if (!culler.isSupported()) { /* culler.getUnsupportedReason() says which requirement is missing */ }

culler.setInstances(instances);                       // world matrix + world-space bounds each
culler.cull(view, projection, indexCountPerInstance); // leaves the answer on the GPU
device.SetVertexBuffer(&mesh);  device.SetIndexBuffer(&meshIndices);
effect.Apply();
culler.draw(PrimitiveType::TriangleList);
```

**Your vertex shader reads its own transform from a storage buffer**, not from a per-instance vertex
stream, because a compute shader cannot write a vertex buffer in this profile. Paste
`GpuInstanceCuller::getInstanceLookupGlsl()` after a `#version 310 es` line and call
`cnaInstanceWorld()`; the matrix arrives in the same layout the `World` uniform does, so it
multiplies the same way.

```glsl
#version 310 es
// ... getInstanceLookupGlsl() ...
void main() { gl_Position = Projection * View * cnaInstanceWorld() * vec4(aPos, 1.0); }
```

**Four requirements, and the fourth is the one that surprises people.** Compute shaders, indirect
draws, an effect source the renderer really executes — and at least one storage buffer readable from
a *vertex* shader. GL ES 3.1 allows `GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS` to be **zero**, so a
context can implement compute completely and still refuse this.

**It refuses rather than falling back**, which is the opposite of what `ClusteredLightCompute` does,
deliberately. That class has a CPU path that is a correct if slower answer; there is no CPU
equivalent of "the draw call itself came from the GPU", so a silent fallback would report success
for a frame that never removed the stall.

**`readVisibleCountEXT()` is a stall.** It is there for tests and tools and is named so it is not
reached for by accident: it waits for the GPU to finish. A frame never needs it — the count's only
consumer is the draw, and that consumer is the GPU.

### HDR display output, and why it is currently a refusal

`plan_modern.md` `MOD-2092`. Two separate things wear the name "HDR", and only one of them has been
in this layer since Phase 1. Rendering in HDR — a float scene target, exposure, tonemapping — is what
`RenderPipeline` has always done. *Presenting* in HDR means handing the display a signal it
interprets as absolute luminance, and that needs a swap chain nobody here has.

**No CNA platform back end offers an HDR swap chain, so `GetDisplayColorSpaceEXT()` answers `Srgb` on
every renderer and `SetDisplayColorSpaceEXT()` refuses anything else.** That is an answer, not a gap:
an HDR swap chain belongs to the presentation path — DXGI, a Vulkan surface format, a platform's own
opt-in — and a renderer that accepted the request without reconfiguring one would have its caller
encode for a display that is not there. PQ-encoded pixels shown as sRGB are washed out and grey.

```cpp
if (device.SupportsDisplayColorSpaceEXT(CNA::DisplayColorSpace::Hdr10)) { /* false today */ }
```

**The encoding exists and is complete**, because it is arithmetic rather than a swap chain.
`HdrDisplayOutput` encodes a scene-referred frame for a chosen space, which is useful for writing an
HDR image to a file or a texture even with no HDR display attached:

```cpp
CNA::Graphics::HdrDisplayOutput display(device);
display.setColorSpace(CNA::DisplayColorSpace::Hdr10);
display.setPaperWhiteNits(200.0f);   // what a scene value of 1.0 is worth
display.setPeakNits(1000.0f);
display.draw(&sceneTarget, &output, width, height);
```

**In `Srgb` it copies through, pixel for pixel**, so the pass can sit at the end of the chain on every
machine: `TonemapPass` has already produced display-encoded sRGB, and a second transfer function
applied to it would be visibly wrong.

**In an HDR space the SDR tonemap is bypassed rather than followed.** The SDR curve exists to fit a
scene into 0..1; an HDR display does not need it. Feed the scene-referred target, not the tonemapped
one.

**`Hdr10` encodes absolute luminance**, which is why `setPaperWhiteNits` exists — the pass must be
told what "white" is worth in the real world before it can encode anything. Highlights roll off
towards `setPeakNits` rather than clipping at it, so a value beyond the display's capability
desaturates instead of becoming a flat white shape. **`Scrgb` is still linear Rec. 709** and only the
scale changes: 1.0 means 80 nits there, so the frame is multiplied by `paperWhite / 80` and needs a
half-float target to hold what comes out.

### Rendering small and showing big: spatial upscaling

`plan_modern.md` `MOD-2093`. `SpatialUpscalePass` is the cheapest performance dial the layer has:
render the scene at a fraction of the output size, and let one pass — the only one that touches
every output pixel — put it on screen at full size.

```cpp
CNA::Graphics::SpatialUpscalePass upscale(device);
device.SetRenderTarget(nullptr);                       // the full-size target
upscale.draw(&lowResScene, 1280, 720, 1920, 1080);
```

**What it is over the hardware's bilinear stretch is edge awareness.** It reads the luma gradient of
the four taps it is already fetching, works out which way the edge runs, and filters *along* that
direction rather than across it — which turns a staircase back into a line. Then a contrast-adaptive
sharpen recovers what any resample softens.

**This is FSR 1's shape, written from the published description of it, not from a vendor SDK.** It
is pure shader arithmetic with nothing to link, which is the reason it can live in this layer at
all, and it is not bit-identical to AMD's reference implementation. Temporal upscalers — DLSS, XeSS,
FSR 2 and above — are a different thing entirely and are out of scope for the same reason TAA is:
they need motion vectors and a history buffer (`MOD-2098`).

**The sharpen is clamped to the neighbourhood it sharpened from**, so it cannot produce a value
brighter or darker than anything around it. That clamp is what separates a sharpener from a ringing
artefact at a hard edge, and it is asserted directly: an image containing only two tones comes out
containing only values between them.

**At a 1:1 scale the pass copies through, pixel for pixel** — exactly, not within a tolerance, and
with sharpening left at whatever the game configured. A pass with nothing to do that changed the
image anyway would make the resolution dial impossible to calibrate: "100%" has to mean the frame
you would have got without the pass in the chain. A 1:1 draw therefore also suppresses the sharpen,
because the pass was asked to change nothing and a sharpen is a change. Sharpen separately, before
the upscale, if that is what you want at full resolution.

**`setEdgeAdaptive(false)` leaves a plain bilinear stretch.** It is there because it is the
comparison a game actually wants to offer, and because a claim that the adaptive path helps is only
worth making if the path it beats can be run beside it.

**The pass reports `isSupported()` false where the renderer does not execute effect source.** Ask it
before building a low-resolution scene target: on a renderer that accepts an effect and ignores it,
the frame would come out stretched by the fixed path with no edge awareness at all, and nothing on
screen would say why.

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

### Many lights: clustered forward shading

`plan_modern.md` `MOD-2040`–`MOD-2048`. The XNA lit effects carry three directional lights plus one
shadowed punctual light, and that is a budget rather than an implementation limit. Clustered forward
lifts it: the view frustum is cut into a grid of cells, each light is sorted into the cells its
volume touches, and a fragment shades with the handful its own cell holds.

```cpp
CNA::Graphics::ClusteredLightSetEXT lights;          // the app's lights, with stable indices
lights.add(pointLight);
lights.add(spotLight);

CNA::Graphics::ClusteredLightGrid grid;              // 16 x 8 x 24 by default
grid.setProjection(projection, nearPlane, farPlane);

CNA::Graphics::ClusteredLightAssignment assignment;  // or ClusteredLightCompute, on the GPU
assignment.assign(grid, view, lights.collectBounds());

CNA::Graphics::ClusteredLightBuffer buffer(device);
buffer.upload(lights, grid, assignment);

effect.begin(world, view, projection, cameraPosition, buffer);
effect.getEffect()->Apply();
device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, triangles);
```

- **Clustered, not deferred**, and the reason is this layer's own history: `DepthNormalPrepass`
  needs two targets and still carries a hand-written two-pass fallback, because
  `MultipleRenderTargets` is a capability some renderers advertise and then refuse — it is probed by
  doing. A G-buffer wants three or four targets, so on such a renderer the fallback becomes three or
  four full geometry passes. Clustered draws the geometry once, keeps transparency lit and keeps
  MSAA working. What it does not buy is deferred's decoupling of shading from geometry, so heavy
  overdraw shades more than once.
- **Slices are spaced exponentially.** Every slice has the same ratio of far to near distance, so
  the near clusters — where geometry is dense and lights are close — are the thin ones. Even spacing
  would put almost every slice past the middle distance, in cells large enough to hold the scene.
- **A cluster's bounds are a box around a frustum shape, and a light's reach is a sphere.** Both
  approximations err the same way: a light may be assigned to a cell it only nearly touches, which
  costs an iteration of the shader's light loop. Neither can drop a light, which would be a hole in
  the lighting.
- **The light list reaches the shader as three textures** — the light data, the cluster table, and
  the index list — with every value stored as the four bytes of its IEEE representation and read
  back with `texelFetch` and `uintBitsToFloat`. That is forced, not chosen: this renderer's textures
  are 8-bit only, and uniform arrays cannot hold 256 lights inside GL ES 3.0's limits. A storage
  buffer would be the natural answer and is not available here, because an SSBO in a *fragment*
  shader needs GLSL ES 3.10 and this layer's shader floor is 3.00.
- **`ClusteredLightCompute` sorts on the GPU and produces the identical list**, element for element,
  not a similar one — everything downstream refers to a light by index. It falls back to the CPU
  where compute is absent and says so. Its per-cluster capacity is fixed, since a GPU cannot grow an
  array; a fuller cluster raises `hasOverflowed()` rather than truncating in silence. The GPU path
  is flat in the light count and the CPU path is not, and the two cross around 128–256 lights on
  this machine — see `docs/cnaext-perf.md`.
- **Shadows are still a small budget.** Clustering removed the limit on how many lights can *light*
  a scene and nothing at all about how many can *shadow* one: a shadow map is a render target and a
  geometry pass, six of them for a point light. `ClusteredShadowPolicyEXT` spends that budget on a
  stated rule — among the lights that asked and are visible, the brightest at the camera win — and
  keeps the selection sticky so a shadow does not blink between two lights whose scores cross.
- **`ClusteredForwardEffect` is a separate effect, not an extension of `PbrEffect`.** `PbrEffect`
  owns no shader source: it fills a `GpuDrawParams` and the renderer generates the program, so a
  light loop there would be a change to EasyGL's built-in effect family — code compiled into every
  game whether `CNA_CNAEXT` is on or off. What a game gives up by using this instead is
  `PbrEffect`'s texture set and its shadowed punctual light; what it gains is the light count.

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

### A sky computed instead of sampled

`plan_modern.md` `MOD-2053`. `AtmosphericSky` is an alternative to `Skybox`, not a replacement: the
cube path above is untouched, and a game that wants an artist's sky keeps using it. What this buys
is that **a time of day becomes a number rather than an asset**.

```cpp
CNA::Graphics::AtmosphericSky sky(device);
sky.setSunDirection(Vector3(0.0f, -0.08f, -1.0f));   // where the light travels; near-horizontal
sky.setTurbidity(2.5f);                              // 1 is aerosol-free air
sky.setIntensity(1.0f);

sky.draw(view, projection, width, height);           // before the scene's geometry, as with Skybox
```

- **The colour is single-scattered Rayleigh and Mie radiance**, computed per view ray. Rayleigh's
  coefficients fall as the fourth power of wavelength, so a clear sky is blue; Mie's do not depend
  on wavelength at all, so haze is white and the glare around the sun has no colour of its own.
  Moving the sun down reddens the sky on its own, because the light has further to travel and the
  blue has been scattered out of it before it arrives.
- **Turbidity is the ratio of the whole atmosphere's optical thickness to the molecular part
  alone**, so 1 means air with no aerosol in it and the Mie term vanishes there. It is clamped to
  [1, 10].
- **`AtmosphericSky::radiance()` is public and static**, and is the same model the shader runs. Use
  it to ask what colour the sky is for an ambient or fog term without drawing one -- and it is what
  lets the physics be tested as ratios between channels and directions rather than against a
  screenshot.
- **Two lengths, doing opposite jobs.** The view path is how much lit air is being looked through,
  so a longer one is *brighter*; the sun path is what the light lost getting in, so a longer one is
  *dimmer and redder*. They must not be summed into one extinction term -- that saturates, and the
  result is a sky whose horizon is darker than its zenith and whose sunset is bluer than its noon.
  It still looks like a sky, which is why it is worth naming.
- **Air mass is Kasten and Young's fit**, taking the zenith angle in degrees; it runs from 1
  overhead to about 38 at the horizon rather than to infinity, so the horizon is finite without a
  clamp doing the work.
- **The sun at the zenith is the brightest thing in the sky, and white.** Both phase functions peak
  forward, so a camera looking straight up under an overhead sun is looking into the aureole. A
  test that wants "the zenith is blue" or "the horizon is brighter than the zenith" has to put the
  sun somewhere other than directly overhead, or it is asking the model to disagree with every
  photograph.

### Area lights, and the two things they do not do

`plan_modern.md` `MOD-2060`–`MOD-2063`. A punctual light is a point, so its highlight is a point and
its shadow has a hard edge. Almost every real light is a *surface*, and the difference is not
brightness — a window is a bright rectangle in a polished floor, and no amount of tuning turns a
point light into one.

```cpp
Microsoft::Xna::Framework::Graphics::AreaLightEXT window;
window.Shape     = AreaLightShapeEXT::Rectangle;
window.Position  = {0.0f, 2.0f, -4.0f};
window.RightAxis = {1.5f, 0.0f, 0.0f};    // half-axes: the light is 3 by 2
window.UpAxis    = {0.0f, 1.0f, 0.0f};
window.Range     = 20.0f;

CNA::Graphics::AreaLightBrdfTable table(device);   // generated at load, ~39 ms
effect.setAreaLight(window, table);
```

- **The diffuse term is exact.** A Lambertian surface reflects a clamped cosine, and the irradiance
  a polygon delivers to one has a closed form: a sum with one term per edge. That identity is what
  linearly transformed cosines are built on, and it needs none of the fitted matrix.
- **The specular term is approximate in shape and exact in energy.** It is a cosine lobe aimed along
  the BRDF's average reflection direction and widened with roughness. A fitted LTC matrix would skew
  the lobe as well as aiming it; what is here does not skew. The magnitude comes from
  `AreaLightBrdfTable`, so a surface is never brighter or darker than it should be — the half a
  viewer notices.
- **The fitted LTC table is not generated, and it is not shipped.** It is the output of a
  Nelder–Mead fit per cell — upwards of a hundred million BRDF evaluations at the published
  resolution, seconds to minutes at every start — and this layer has no asset path to ship a table
  through, the constraint that also refused SMAA (`MOD-610`).
- **Three shapes, one quad.** A rectangle is its own corners; a disc is an area-matched rectangle,
  its axes scaled by `sqrt(pi)/2`; a tube is a quad turned to face whatever it is lighting, which is
  what a cylinder looks like from anywhere. Each is an approximation in outline only, never in
  energy.
- **One area light per draw.** The same budget `PunctualLightEXT` sets for itself, for the same
  reason: an edge sum over a clipped polygon is an order of magnitude more work per fragment than a
  punctual light's dot products. A scene wanting many lights wants them in the cluster grid, and the
  cluster grid holds punctual lights.

**There are no area-light shadows.** A soft shadow needs either many samples of the light's surface
or a ray query, and this layer has neither: an area light lights whatever faces it, whether or not
something stands in the way. This is stated in `AreaLightEXT`'s own header as well as here, because
meeting it without warning looks like a bug in the shadow system rather than a boundary.

**A light with no area is refused rather than drawn.** Two axes that both have length but lie along
the same line enclose nothing, and the form factor answers that with a division by zero rather than
with darkness — `AreaLightEXT::IsValidEXT` is where that is caught. A *tube* is a line with a radius
rather than a surface, so parallel axes are meaningful there and are accepted.

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

### Probe-based indirect light

`plan_modern.md` `MOD-2080`–`MOD-2087`. `ImageBasedLightEXT` lights a whole scene from one
environment, applied uniformly: every surface gets the same ambient whether it stands in the doorway
or at the back of the cellar. A probe grid is the answer to *where*.

```cpp
CNA::Graphics::LightProbeVolumeEXT volume(roomBounds, 8, 4, 8);

CNA::Graphics::LightProbeBaker baker(device);
baker.bakeLight(volume, [&](const Matrix& view, const Matrix& projection) {
    drawEverythingStatic(view, projection);          // the app draws; the layer captures
});
baker.bakeVisibility(volume, [&](const Matrix& view, const Matrix& projection) {
    drawDistanceFromCamera(view, projection);        // a second pass, a different shader
});

effect.setLightProbeVolume(&volume);                 // replaces the flat ambient term
```

- **Nine coefficients per probe, and that is what makes a grid affordable.** An irradiance cube per
  probe would be kilobytes and could not be blended with its neighbours; second-order spherical
  harmonics can be averaged directly, because the projection onto them is linear — the average of
  two probes' coefficients *is* the projection of the average of their light. Nothing else about the
  storage would allow trilinear blending at all.
- **A probe carries irradiance, not outgoing radiance.** A Lambertian surface reflects `albedo / π`
  of it, and the effect applies that. Baking the albedo in would put a surface's colour into a probe
  that has nothing to do with any surface.
- **Light does not leak through walls.** Each probe records how far the geometry is along six axis
  directions as two moments, and a corner's blend weight is multiplied by a Chebyshev test against
  that — a variance shadow map applied to a probe. A flat wall cuts off sharply, a cluttered
  direction fades. Rejecting a corner *renormalises* rather than darkens; a point rejected by every
  corner falls back to the plain blend, so it leaks rather than going black, because a hole in the
  lighting is the more visible mistake. A probe with nothing recorded is trusted completely.
- **The ambient is per-draw, not per-pixel.** Evaluating a volume per fragment means fetching eight
  probes' twenty-seven coefficients each — over two hundred texture reads for the smoothest term in
  the frame. What one probe per object costs is that a large object crossing a lighting boundary
  shows no gradient across itself; split it, or fall back to the flat ambient.
- **The baker's directions come from its own view matrices**, not from a cube-map face layout, so
  there is no convention to agree with and no handedness to get wrong. The visibility capture clears
  to *white*, because an unwritten pixel has to read as the far plane — clearing to black would
  record a wall at the camera in every uncovered direction and reject every probe in the volume.

**What it costs.** A probe is **168 bytes** — nine `Vector3` coefficients, six visibility directions
with two moments each, and a position — so an 8×4×8 grid of 256 probes is **42 KB**. Capture and
projection cost **3.5 ms per probe** at 32×32 per face on Mesa llvmpipe, with a draw that renders
nothing; a real bake adds six scene draws per probe on top, which is what makes baking an offline
operation. (`CnaTests --gtest_filter=LightProbeBakerTest.TheCostOfAProbeGridIsAStatedNumber`.)

**Two things this is not, and both were refused with their reasons.** *Lightmaps* need a UV
unwrapper, an atlas packer and a bake that rasterises into UV space — three mesh-processing problems
a runtime does not gain by adding code, and glTF assets in the wild almost never ship a lightmap UV
to read. *Voxel cone tracing* needs image stores into a 3D texture, which GL ES refuses for CNA's
mutable textures (`MOD-1514`); the slice-atlas workaround that saved froxel fog does not scale here,
since a usable 128³ volume is a 128×16384 atlas and cone tracing needs mipmapped 3D filtering a
slice atlas cannot provide. What the probe volume gives up against either is a glossy bounce and a
sharp indirect shadow; what it gives back is that it lights *moving* objects, which a lightmap never
could.

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

### Material extensions beyond glTF core

`plan_modern.md` `MOD-2070`–`MOD-2077`. `PbrMaterial` describes what `PbrEffect` can render. The
lobes past that — clearcoat, sheen, transmission with its volume, iridescence, and a subsurface
approximation — are carried by a **separate** `PbrMaterialExtensions` and shaded by
`ClusteredForwardEffect`.

```cpp
CNA::Graphics::PbrMaterialExtensions extensions;
extensions.setClearcoatFactor(1.0f);          // 0 is off, and 0 is the default for every lobe
extensions.setClearcoatRoughness(0.1f);
extensions.setSheenColorFactor({0.5f, 0.4f, 0.4f});
effect.setMaterialExtensions(extensions);

// Straight from an imported glTF material, textures resolved by the loader:
auto imported = CNA::Graphics::materialExtensionsFromGltfEXT(material, textures);
```

- **Why a separate type.** `PbrMaterial`'s defining property is that it is *lossless* against
  `PbrEffect`: `applyMaterial` then `extractMaterial` returns an equal material, which is what the
  whole of Phase 13 existed to establish. `PbrEffect` has no state for these lobes and cannot gain
  shading without changing EasyGL's generated program — code compiled into every game whether
  `CNA_CNAEXT` is on or off — so a field on `PbrMaterial` would be silently dropped by that round
  trip and two materials would compare unequal for a reason nothing in the type explains.
- **Every lobe is off when its own factor is zero**, which is its default, so a material that names
  none of them is the material it was before.
- **Clearcoat is a second specular lobe, not a brighter one**, with its own roughness, and it takes
  from the base exactly what it reflects. A rough base under a smooth coat is what brushed metal
  under lacquer looks like, and one roughness cannot describe it.
- **Sheen's distribution is a different shape.** The Charlie lobe peaks where the half-vector is
  *perpendicular* to the normal — the opposite of a specular lobe — which is why it appears as a rim
  at grazing angles. glTF scales the base layer by the sheen's directional albedo from a table; that
  table is not generated here, so a sheened surface is brighter than energy conservation allows by
  that lobe's own small albedo.
- **Transmission needs a copy of the opaque frame, and is refused without one.** It is not
  transparency: the ray *refracts*, so what shows through is displaced. The copy is a real cost —
  the opaque geometry has to be drawn, resolved and copied before any transmissive surface can be
  drawn at all — and the layer does not make it, because only the application knows when its opaque
  pass ended. The volume absorbs by Beer's law; an attenuation distance of 0 means glTF's infinity,
  a medium that absorbs nothing.
- **Iridescence replaces the Fresnel term rather than adding a lobe**: a thin film changes *which
  wavelengths* a surface reflects, not how much. `ThinFilmIridescence` is Belcour and Barla's model,
  written in C++ and GLSL that mirror each other and are compared on the GPU.
- **Subsurface is a wrapped-diffuse approximation and says so.** Light wraps past the terminator and
  a back-scatter term glows when the light is behind the surface. A thin object lit from behind
  glows, and a thick one glows exactly as much, because nothing here knows how thick it is. Real
  diffusion needs a diffuse-only buffer and a depth-aware blur over it, which needs MRT — the
  capability this layer already had to build a fallback for.
- **The maps are carried, not consumed.** `ClusteredForwardEffect` binds no material textures at
  all: it has a base colour, a metallic and a roughness rather than a texture set. Every extension's
  strength/roughness/normal *maps* exist on the extension set for the importer and the round trip,
  and the factors are what shade.

**What the lobes cost.** Each one is behind a dynamic branch on its own factor, so a fragment whose
lobe is off pays a branch rather than the lobe — but the code is compiled into the shader either
way, which costs registers and instruction cache and can reduce occupancy on a real GPU. Measured
here at 64 lights per fragment, 256×256, Mesa llvmpipe (`cna_test_cnaext_clustered_lights
--benchmark`), **the difference is below this machine's noise**: three runs put every configuration,
including all four lobes at once, between 21.0 ms and 24.0 ms with no consistent ordering. That is a
real finding rather than a missing one — with 69 lights in the busiest cluster the light loop
dominates so completely that a per-fragment lobe does not register — and it is also the limit of
what a software rasteriser can say. It is *not* evidence that these lobes are free on hardware,
where the shader's register pressure is charged differently.

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
