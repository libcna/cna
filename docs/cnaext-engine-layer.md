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
| Skybox + IBL | ⬜ | ⬜ | ⬜ | ⬜ |
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

Legend: ✅ implemented and verified · 🟨 partial · ⬜ not implemented · ⛔ deliberately unsupported.

## Related documents

- [`../CNAEXT.md`](../CNAEXT.md) — the design of this layer (what it is, what it is not, why).
- [`../plan_modern.md`](../plan_modern.md) — the task backlog implementing that design.
- [`ascii-post-process-effect.md`](ascii-post-process-effect.md) — the ASCII effect in detail.
- [`graphics-renderer-feature-matrix.md`](graphics-renderer-feature-matrix.md) — the XNA-level
  per-renderer feature matrix this one sits above.
