# CNAEXT engine layer — measured performance

`plan_modern.md` `MOD-1712`. Every perf row in that plan records its numbers here, in one format, so
they can be compared with each other and re-measured later. A number without its recipe is not a
measurement, so the recipe comes first.

## How these were measured

Unless a row says otherwise:

- **Machine**: the project's Linux container, software rasterisation only — **Mesa llvmpipe**
  reporting **OpenGL ES 3.2** to EasyGL, under `Xvfb :99 -screen 0 1280x720x24`.
- **Build**: `cmake-build-cnaext`, `CMAKE_BUILD_TYPE=Debug`, `CNA_CNAEXT=ON`,
  `CNA_GRAPHICS_RENDERER=OPENGLES3`.
- **Method**: each example takes its own timing with `std::chrono::steady_clock` around a warm loop,
  after discarding warm-up frames, and divides by the frame count. Read-back is excluded where it is
  not the thing being measured, because `GetBackBufferData` costs more than most of these passes.
- **Reproduce**: run the example named in the row with `--benchmark`, e.g.

  ```sh
  Xvfb :99 -screen 0 1280x720x24 &
  DISPLAY=:99 SDL_VIDEODRIVER=x11 ./cmake-build-cnaext/cna_test_cnaext_skybox --benchmark
  ```

**These are software-rasteriser numbers.** They are meaningful *against each other* — a pass that is
ten times another here is doing roughly ten times the work — and they are not predictions of what
any GPU will do. Where a number is dominated by fill rate (shadow maps, the sky) hardware changes it
completely; where it is dominated by call count (instancing) the ratio survives.

## Shadows

| What | Measurement | Source |
|---|---|---|
| Shadow pass, `ShadowQuality::Low` (512²) | **0.10 ms** | `cnaext_shadowmap_test --benchmark` (`MOD-857`) |
| Shadow pass, `Medium` (1024²) | **0.12 ms** | same |
| Shadow pass, `High` (2048²) | **0.20 ms** | same |
| Shadow pass, `Ultra` (4096²) | **0.52 ms** | same |
| Cascades: 1 cascade | **0.12 ms** | `cnaext_csm_test --benchmark` (`MOD-915`) |
| Cascades: 2 | **0.20 ms** | same |
| Cascades: 3 | **0.49 ms** | same |
| Cascades: 4 | **0.43 ms** | same — non-monotonic because each cascade's fitted volume shrinks as the count rises |
| Directional shadow generation | **0.05 ms** | `cnaext_pointshadow_test --benchmark` (`MOD-1012`) |
| Spot shadow generation | **0.04 ms** | same |
| Point shadow generation (6 cube faces) | **5.42 ms** | same — the number that justifies one shadowed punctual light per draw |

## Sky and image-based lighting

| What | Measurement | Source |
|---|---|---|
| Skybox, one fullscreen pass | **0.020 ms/frame** (against 0.005 ms for a clear alone) | `cnaext_skybox_test --benchmark` (`MOD-1114`) |
| IBL precompute: irradiance 32², 32 samples | **3.31 s** | `EnvironmentProcessorTest.GenerationCostIsLoadTimeWork` (`MOD-1211`) |
| IBL precompute: prefiltered specular 128², 5 mips, 64 samples | **2.38 s** | same |
| IBL precompute: BRDF LUT 128², 128 samples | **0.49 s** | same |
| Lighting: flat ambient | **0.064 ms/frame** | `cnaext_ibl_test --benchmark` (`MOD-1246`) |
| Lighting: image-based | **0.066 ms/frame** (≈ +3 %) | same |

The precompute numbers are **Debug** CPU work and an optimised build is several times faster; they
are load-time either way, which is the point of recording them.

## Instancing

| What | Measurement | Source |
|---|---|---|
| 1 000 cubes, instanced | **0.96 ms/frame** | `cnaext_instancing_lod_test --benchmark` (`MOD-1413`) |
| 1 000 cubes, one draw call each | **51.50 ms/frame** (**54×**) | same |
| 10 000 cubes, instanced | **22.65 ms/frame** | same |
| 10 000 cubes, one draw call each | **538.48 ms/frame** (**24×**) | same |

The ratio falls with count because by ten thousand cubes the software rasteriser is fill-bound
rather than call-bound — the instanced path stops being the bottleneck.

## Compute

| What | Measurement | Source |
|---|---|---|
| 100 000 particles, one integration step on the GPU | **0.881 ms** | `cnaext_compute_particles_test --benchmark` (`MOD-1550`) |
| The same step on the CPU | **2.401 ms** | same |
| Reading 100 000 particles (1.6 MB) back to the CPU | **0.806 ms** | same |

That last row is the cost of a missing API rather than of any work: a storage buffer cannot be bound
as a vertex stream, so a GPU-resident particle system has to come back through the CPU before it can
be drawn. It is also the measurement behind `MOD-1553`'s refusal of a compute bloom downsample — at
roughly 0.5 ms per megabyte, bringing a 720p downsample result back costs about ninety times the
0.020 ms fullscreen raster pass it would have replaced.

## Not measured yet

The bloom, tonemapping, SSAO and FXAA passes have no recorded per-pass cost (`MOD-407`, `MOD-317`,
`MOD-523`, `MOD-606` are open). When they are measured they belong in this file, in this format.
