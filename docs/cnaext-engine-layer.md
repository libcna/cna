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
| Shadow maps | ⬜ | ⬜ | ⬜ | ⬜ |
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

Legend: ✅ implemented and verified · 🟨 partial · ⬜ not implemented · ⛔ deliberately unsupported.

## Related documents

- [`../CNAEXT.md`](../CNAEXT.md) — the design of this layer (what it is, what it is not, why).
- [`../plan_modern.md`](../plan_modern.md) — the task backlog implementing that design.
- [`ascii-post-process-effect.md`](ascii-post-process-effect.md) — the ASCII effect in detail.
- [`graphics-renderer-feature-matrix.md`](graphics-renderer-feature-matrix.md) — the XNA-level
  per-renderer feature matrix this one sits above.
