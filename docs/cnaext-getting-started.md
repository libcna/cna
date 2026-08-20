# Getting started with the CNAEXT engine layer

`plans/plan_modern.md` `MOD-1802`/`MOD-1803`/`MOD-1805`. From an ordinary CNA game to a bloomed HDR frame,
in the order you actually do it. Everything here is verified by tests in
`modules/graphics-ext/tests/` and by the examples named beside each step; nothing is aspirational.

## 0. Nothing changes unless you opt in

This is the layer's first promise, and it is worth stating before the instructions. The engine layer
is behind a CMake option that is **OFF by default**, every file in `modules/graphics-ext/` is wrapped
in `#ifdef CNA_CNAEXT`, and a test (`CNAEXT_GuardDiscipline`) fails if one ever is not. An existing
game rebuilt against a CNA that contains this layer gets the same binary behaviour it had before —
and even with the layer compiled *in*, a `RenderPipeline` with default settings allocates no
off-screen target and renders straight to the back buffer.

That is not a claim, it is a test: `RenderPipelineTest.AnInertPipelineNeverAllocatesASceneTarget`.

## 1. Build with the layer

```sh
cmake -S . -B cmake-build-cnaext -G Ninja \
      -DCNA_CNAEXT=ON \
      -DCNA_GRAPHICS_RENDERER=OPENGLES3
cmake --build cmake-build-cnaext -j4
```

`OPENGLES3` (EasyGL) is the reference renderer: every subsystem lands there first. Other renderers
report `false` from the matching capability and take a documented fallback — see the matrix in
[`cnaext-engine-layer.md`](cnaext-engine-layer.md).

## 2. Wrap what you already draw

```cpp
#include "CNA/Graphics/CNAEXT.hpp"          // one include for the whole layer

CNA::Graphics::RenderPipeline pipeline(GraphicsDevice());
pipeline.resize(backBufferWidth, backBufferHeight);   // and again whenever that changes

// ... in Draw():
pipeline.begin(Color::CornflowerBlue);
spriteBatch.Begin();  /* everything the game already drew */  spriteBatch.End();
pipeline.end();
```

At this point the frame is identical to the one before, because nothing is enabled yet. That is the
intended intermediate state: get the two calls in place, confirm the game looks unchanged, then turn
things on one at a time.

## 3. Turn on HDR and bloom

```cpp
auto& settings = pipeline.getSettings();
settings.setHDREnabled(true);        // an RGBA16F scene target where the renderer has one
settings.setBloomEnabled(true);
settings.setBloomIntensity(1.0f);
settings.setTonemappingMode(CNA::Graphics::TonemappingMode::Aces);
settings.setExposure(1.0f);
```

Bloom needs values above 1.0 to bloom *from*, which is what HDR is for: draw with colours above
white (a `SpriteBatch` tint of `Color(255, 255, 255) * 4.0f`, an emissive material, a bright light)
and they will spill. On a renderer with no float targets the pipeline reports `Color` from
`getSceneTargetFormat()` and the frame still renders — dimmer bloom, not a crash.

**Ten lines is the whole 2D story.** `cnaext_bloom_test` is that program.

## 4. Add the rest, in the order they pay off

| Want | Add | Costs |
|---|---|---|
| Softer edges | `settings.setFXAAEnabled(true)` | one fullscreen pass |
| Contact shadows | `settings.setSSAOEnabled(true)` + `pipeline.setDepthNormalInputs(depth, normals)` | you must render depth and view-space normals yourself |
| Sun shadows | `ShadowMap` + `pipeline.setShadowScene(...)` | your scene is drawn twice per frame |
| Large outdoor scenes | `CascadedShadowMap` | drawn once per cascade |
| A lamp or a torch | `PointLightEXT` / `SpotLightEXT` + `CubeShadowMap` / `SpotShadowMap` | a point light's shadow is six passes — measure it |
| A sky | `Skybox` + `pipeline.setSkybox(...)` | one fullscreen pass |
| Real ambient light | `EnvironmentProcessor` → `ImageBasedLightEXT` → `PbrEffect::setImageBasedLightEXT` | seconds at load, ~3 % per frame |
| Thousands of objects | `InstancedRendererEXT` (+ `FrustumCullerEXT`, `LodGroupEXT`) | one draw call instead of thousands |
| GPU work of your own | `ComputeShader` + `StorageBufferT<T>` | needs GL ES ≥ 3.1 / GL ≥ 4.3 |

Each has its own section in [`cnaext-engine-layer.md`](cnaext-engine-layer.md) with the contract it
puts on your game — several of them (SSAO's inputs, shadows' second draw) are contracts, not just
switches.

## 5. Writing your own pass

A custom pass implements `CNA::Graphics::PostProcessPass` and is appended with
`pipeline.addUserPass(&pass)`. Three rules, and they are the ones that go wrong:

- **The UV origin is the renderer's, not yours.** A render target's rows may be bottom-up. The stock
  passes multiply their V by the renderer's own flag rather than assuming; a shader that hardcodes
  `1.0 - v` works on one renderer and renders upside down on the next.
- **Sample your input, do not read the target you are writing.** The chain ping-pongs between two
  intermediates from `RenderTargetPool` precisely so a pass never does both to one texture.
- **Declare what you need and check it.** `GraphicsCapability::CustomEffects` is what lets a pass
  compile GLSL at all; without it your pass must copy its input through and say so once, exactly as
  the built-in passes do. A pass that silently does nothing is the failure this layer works hardest
  to avoid.

The uniform and sampler contract a pass gets is documented on `PostProcessContext`; the built-in
passes (`BloomPass`, `FxaaPass`, `TonemapPass`) are the worked examples.

## Where to look next

- [`cnaext-engine-layer.md`](cnaext-engine-layer.md) — what each subsystem does, and its limits.
- [`cnaext-perf.md`](cnaext-perf.md) — what each of them costs, and how that was measured.
- `../CNAEXT.md` — why the layer is shaped the way it is.
- `../plans/plan_modern.md` — the backlog, including every deviation from the original design.
