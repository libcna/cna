# Engine-layer coverage (`modules/graphics-ext/`)

`plan_modern.md` **MOD-1741**. Measured, not estimated — every number here came out of a real
`--coverage` build on 2026-08-18 and can be reproduced with the recipe at the bottom. This file is a
dated snapshot; it is not regenerated automatically, so re-measure before quoting it.

Note the difference from `docs/coverage.md`: that file is a static *API-surface* estimate for the
whole project ("which XNA classes exist"). This one is machine-measured *line and branch* coverage
for one module.

## Headline

| Metric | Result |
|---|---|
| Lines | **94.2%** (2822 / 2997) |
| Functions | **97.8%** (523 / 535) |
| Branches | **57.5%** (2218 / 3859) |

Build: `cmake-build-cnaext-coverage`, GCC 13, `-DCNA_CNAEXT=ON -DCNA_GRAPHICS_RENDERER=OPENGLES3
-DCNA_PLATFORM=SDL3 --coverage -O0`. Driver: the whole `CnaTests` suite under Xvfb :99 with Mesa
llvmpipe — **7940 ran · 7876 pass · 64 skip · 0 fail**, the same result as the ordinary Debug build,
so nothing here is measured against a degraded run.

## What the branch figure means, and does not

57.5% next to 94.2% lines looks alarming and mostly is not. GCC counts a branch pair for every
implicit control-flow edge C++ generates — each potentially-throwing call site produces an exception
edge that a passing test never takes, and this layer is full of them: RAII guards, `std::vector`
growth, `std::string` construction, every `throw`ing accessor in `RequireCapability`. The files with
the *lowest* branch percentages are the ones with the most such calls, not the least-tested logic.
Where a low branch figure does mean something, it is called out per file below.

## Per file, worst first

| File | Lines | Branches | Functions |
|---|---|---|---|
| `src/AsciiFontAtlas.cpp` | 39% (17/44) | 20% (11/56) | 1/3 |
| `src/BlitPass.cpp` | 62% (8/13) | 17% (1/6) | 3/5 |
| `src/ScopedRenderTarget.cpp` | 71% (12/17) | 64% (9/14) | 3/3 |
| `src/AsciiQuantizer.cpp` | 76% (41/54) | 43% (18/42) | 3/4 |
| `src/FullscreenPass.cpp` | 81% (17/21) | 38% (12/32) | 4/4 |
| `src/InstancedRendererEXT.cpp` | 84% (80/95) | 47% (73/154) | 16/17 |
| `src/ComputeShader.cpp` | 86% (57/66) | 45% (48/106) | 12/12 |
| `src/StorageBuffer.cpp` | 87% (26/30) | 40% (20/50) | 6/6 |
| `src/AsciiPass.cpp` | 89% (24/27) | 57% (16/28) | 6/6 |
| `include/CNA/Graphics/StorageBuffer.hpp` | 89% (42/47) | 46% (11/24) | 15/15 |
| `src/DepthNormalPrepass.cpp` | 90% (111/123) | 55% (100/182) | 18/18 |
| `src/CubeShadowMap.cpp` | 91% (86/95) | 58% (75/129) | 16/17 |
| `src/FxaaPass.cpp` | 92% (34/37) | 59% (20/34) | 8/8 |
| `src/ShadowMap.cpp` | 92% (148/161) | 53% (96/181) | 21/21 |
| `src/AsciiPostProcessEffect.cpp` | 94% (64/68) | 50% (37/74) | 7/8 |
| `src/ShaderDiagnostics.cpp` | 94% (17/18) | 63% (24/38) | 1/1 |
| `src/SpotShadowMap.cpp` | 95% (71/75) | 54% (49/90) | 15/17 |
| `src/RenderPipeline.cpp` | 95% (161/170) | 75% (112/150) | 26/28 |
| `src/TonemapPass.cpp` | 95% (58/61) | 57% (26/46) | 16/16 |
| `src/DepthEffect.cpp` | 96% (55/57) | 61% (45/74) | 11/11 |
| `src/RenderPipelineSettings.cpp` | 97% (143/148) | 66% (218/331) | 47/47 |
| `include/CNA/Graphics/GltfMaterialBridge.hpp` | 97% (64/66) | 52% (60/116) | 4/4 |
| `src/CascadedShadowMap.cpp` | 97% (211/217) | 61% (157/256) | 27/27 |
| `src/BloomPass.cpp` | 97% (107/110) | 54% (86/160) | 14/14 |
| `src/LodGroupEXT.cpp` | 98% (83/85) | 82% (56/68) | 19/19 |
| `src/AutoExposureEXT.cpp` | 98% (56/57) | 70% (31/44) | 13/13 |
| `src/PbrMaterial.cpp` | 99% (154/156) | 75% (83/111) | 60/60 |
| `src/MaterialBinding.cpp` | 99% (177/179) | 51% (271/530) | 11/11 |
| `src/EnvironmentProcessor.cpp` | 99% (279/281) | 63% (179/284) | 20/20 |
| `include/CNA/Graphics/PostProcessPass.hpp` | 100% (2/2) | 100% (0/0) | 2/2 |
| `include/CNA/Internal/Graphics/Ascii/AsciiQuantizer.hpp` | 100% (2/2) | 100% (0/0) | 1/1 |
| `src/CRTEffect.cpp` | 100% (32/32) | 42% (11/26) | 14/14 |
| `src/EffectPass.cpp` | 100% (25/25) | 67% (8/12) | 8/8 |
| `src/EngineException.cpp` | 100% (14/14) | 50% (9/18) | 6/6 |
| `src/EngineLayerVersion.cpp` | 100% (4/4) | 50% (1/2) | 2/2 |
| `src/FrustumCullerEXT.cpp` | 100% (30/30) | 75% (24/32) | 9/9 |
| `src/PostProcessChain.cpp` | 100% (43/43) | 75% (24/32) | 10/10 |
| `src/PostProcessPass.cpp` | 100% (3/3) | 50% (2/4) | 1/1 |
| `src/RenderTargetPool.cpp` | 100% (32/32) | 83% (25/30) | 6/6 |
| `src/RequireCapability.cpp` | 100% (26/26) | 66% (44/67) | 2/2 |
| `src/ShaderEffectFactory.cpp` | 100% (20/20) | 62% (10/16) | 6/6 |
| `src/Skybox.cpp` | 100% (74/74) | 52% (44/84) | 16/16 |
| `src/SsaoPass.cpp` | 100% (112/112) | 57% (72/126) | 17/17 |

## The five gaps worth acting on

1. **`AsciiFontAtlas.cpp` — 39% lines, 20% branches, and the number is misleading.** The two
   uncovered functions, `GetAsciiGlyphBitmap()` and `BuildAsciiFontAtlas(GraphicsDevice&)`, *are*
   exercised — by `modules/graphics-ext/examples/ascii_fontatlas_test.cpp`, which is a standalone
   example rather than a `CnaTests` case, and this measurement drives `CnaTests` only. That is a
   general caveat, not a one-file one: **anything this module proves through an example program
   rather than a GTest case reads as uncovered here.** Whether those checks should migrate into
   `CnaTests` is a separate question from whether they exist.
2. **`BlitPass.cpp` — `getName()` and `isSupported()` were never called.** `apply()` is well covered
   through `PostProcessChainTests`, but nothing asked the pass to name itself or to answer the
   support question, even though both are public API and both are required to be tested by
   `CLAUDE.md`'s test rules. **Closed by `MOD-1742`**; the percentages in the table above are the
   pre-fix measurement and this file will read differently when it is next regenerated.
3. **`ScopedRenderTarget.cpp` — both `catch` blocks are unreached.** Deliberately: they exist for a
   renderer that throws from `GetRenderTargets()` or from a restore on the unwind path, and EasyGL
   does neither. Reaching them needs a device double rather than a real renderer.
4. **`InstancedRendererEXT.cpp` — 84% lines, 47% branches.** The uncovered block is the
   no-hardware-instancing fallback loop: EasyGL advertises instancing, so the per-instance path
   never runs here. This one is a genuine renderer-coverage gap rather than an untested line, and
   it closes when a renderer without instancing runs the suite.
5. **`ComputeShader.cpp` / `StorageBuffer.cpp` — 86% lines, 45%/40% branches.** The refusal paths
   for a device without compute. Same shape as (4): reachable, but only from a renderer that says
   no.

Nothing in the list is an untested *behaviour* that a test could reach today and does not, except
(2), which is a one-test fix.

## Reproducing

```bash
cmake -S . -B cmake-build-cnaext-coverage -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug -DCNA_CNAEXT=ON \
      -DCNA_GRAPHICS_RENDERER=OPENGLES3 -DCNA_PLATFORM=SDL3 \
      -DCMAKE_CXX_FLAGS="--coverage -O0 -g0" \
      -DCMAKE_C_FLAGS="--coverage -O0 -g0" \
      -DCMAKE_EXE_LINKER_FLAGS="--coverage"
cmake --build cmake-build-cnaext-coverage --target CnaTests -j4

# From the repository root, with a display -- fixtures resolve relative to the CWD.
DISPLAY=:99 ./cmake-build-cnaext-coverage/CnaTests

pip install gcovr
gcovr --root . --filter 'modules/graphics-ext/(src|include)/' \
      --exclude-unreachable-branches --print-summary \
      --txt coverage.txt cmake-build-cnaext-coverage
```

`-O0` matters: at `-O2` GCC's line attribution drifts far enough that per-line "missing" lists stop
being trustworthy. `ccache` does not help this build — `--coverage` embeds absolute `.gcno` paths —
so budget a full compile.
