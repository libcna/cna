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
| `PbrMaterial` | `CNA/Graphics/PbrMaterial.hpp` | Serialization-friendly PBR material description. |
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
| Post-process effects (`DepthEffect`, `CRTEffect`) | ✅ GLSL | ⬜ | ⬜ | `AsciiPostProcessEffect` is CPU-side and runs everywhere |
| Float/HDR render targets | ✅ RGBA16F + RGBA32F, runtime-probed | ⬜ | ⬜ | ⬜ — each reports `false` and `RenderTarget2D` refuses the format rather than substituting `Color` |
| `RenderPipeline` + post-process passes | ✅ | ⬜ | ⬜ | The passes need `GraphicsCapability::CustomEffects`; without it each copies its input and the frame still renders |
| Shadow maps (directional, PCF) | ✅ generation + reception on all four lit effects | ⬜ | ⬜ | ⬜ — an effect accepts the shadow state and a renderer without the shader ignores it, so the frame renders unshadowed rather than failing |
| Cascaded shadow maps (2-4, atlas) | ✅ same four programs, one shared shader path | ⬜ | ⬜ | ⬜ — same accepted-and-ignored convention |
| Point / spot lights + shadows | ✅ punctual lighting and its cube/spot lookup on all four lit programs | ⬜ | ⬜ | ⬜ — same accepted-and-ignored convention |
| Skybox | ✅ one fullscreen pass; needs `CustomEffects` | ⬜ | ⬜ | ⬜ — where the shader will not compile the sky is skipped and logged once |
| Compute / storage buffers | ⬜ | ⬜ | ⬜ | ⬜ |

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
  offers. Doing it on the CPU makes the precompute work identically on every renderer, including
  Headless, and removes the capability gate entirely. The price is the time below.
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

Legend: ✅ implemented and verified · 🟨 partial · ⬜ not implemented · ⛔ deliberately unsupported.

## Related documents

- [`../CNAEXT.md`](../CNAEXT.md) — the design of this layer (what it is, what it is not, why).
- [`../plan_modern.md`](../plan_modern.md) — the task backlog implementing that design.
- [`ascii-post-process-effect.md`](ascii-post-process-effect.md) — the ASCII effect in detail.
- [`graphics-renderer-feature-matrix.md`](graphics-renderer-feature-matrix.md) — the XNA-level
  per-renderer feature matrix this one sits above.
