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

## HDR render targets

`plan_modern.md` `MOD-138`. What the HDR pipeline's *first* decision costs, before any pass runs:
a scene target in a float format rather than 8-bit `Color`.

Reproduce: `./cmake-build-cnaext/cna_test_cnaext_hdr_target --benchmark`.

| Target format | Bytes/texel | 1280×720 memory | Fill, ms/frame | vs `Color` |
|---|---|---|---|---|
| `Color` (RGBA8) | 4 | 3.52 MiB | 6.73 | — |
| `HdrBlendable` / `HalfVector4` (RGBA16F) | 8 | 7.03 MiB | 7.27 | +8% |
| `Vector4` (RGBA32F) | 16 | 14.06 MiB | 7.50 | +11% |

The fill is a full-target textured quad, not a clear, **and the measurement had to be forced to
happen.** With a clear alone, all three formats timed identically — llvmpipe queues the tile work and
nothing had ever touched the memory, so the number measured the API rather than the memory traffic.
Reading back a single texel after each fill makes the driver do the work inside the timed region.
All three rows pay that same one-texel readback, so the comparison stays fair.

**What to take from this.** Doubling bytes per texel does not double the cost: at 1280×720 the step
from 8-bit to RGBA16F is 8%, and to RGBA32F 11%, on a rasteriser where fill is expensive. Memory is
the honest reason to prefer `HdrBlendable` over `Vector4` for a scene target — half the bytes, and a
post-process chain holds several targets at once — not frame time. That is the justification behind
`RenderQuality` choosing `HdrBlendable`, and it is a measurement rather than an assumption.

## The whole frame

`plan_modern.md` `MOD-742`. Every other table here times one pass in isolation. This is what a game
actually pays: a frame drawn the way a game draws it — a `SpriteBatch` sprite and a `BasicEffect`
triangle — with and without the pipeline around it. Both sides pay the same back-buffer read (which
is what forces the frame to happen inside the timed region), so the *difference* is the pipeline.

At 96×96, `cnaext_render_pipeline_test --benchmark`:

| Frame | ms | Over direct |
|---|---|---|
| Direct rendering, no pipeline | 0.76 | — |
| Pipeline present, nothing enabled | 0.84 | +0.07 |
| HDR + bloom + tonemap + FXAA, `Low` | 3.25 | +2.48 |
| HDR + bloom + tonemap + FXAA, `Medium` | 3.76 | +3.00 |
| HDR + bloom + tonemap + FXAA, `High` | 4.80 | +4.04 |
| HDR + bloom + tonemap + FXAA, `Ultra` | 4.78 | +4.02 |

Two things worth taking from it.

**An inert pipeline costs about 9%** at this size — 0.07 ms — and that is the *whole* overhead of
having the object in the frame, since with nothing enabled it allocates no scene target and runs no
passes. At a real resolution the fixed part does not grow, so the fraction falls.

**The stack costs three to four times the scene**, which sounds alarming and is an artefact of the
scene: this one is a sprite and a triangle at 96×96, so the passes — which are fullscreen and do not
care how little geometry there was — dominate completely. A frame with real geometry pays the same
few milliseconds of post-processing against a much larger base. That is the shape to plan for: the
engine layer's cost is close to constant per pixel and independent of scene complexity, which is the
opposite of how the scene itself behaves.

`High` and `Ultra` are within noise of each other, for the reason `MOD-416` measured: the extra
bloom levels are a quarter of the one before and cost almost nothing.

SSAO is not in this table. It needs a depth/normal prepass this scene does not have, and it is
measured on its own above — where it costs more than everything here put together.

## Post-process passes

`plan_modern.md` `MOD-230`. What each pass costs over a full frame, and — more usefully — what each
costs *over a plain copy*, since a copy is the floor any pass has to clear.

Reproduce: `./cmake-build-cnaext/cna_test_cnaext_postprocess_chain --benchmark`.

| Pass | ms/frame at 1280×720 | Over a copy |
|---|---|---|
| Blit (the floor: one fullscreen textured draw) | 6.22 | — |
| Tonemap | 6.90 | ×1.11 |
| FXAA | 12.99 | ×2.09 |
| Bloom | 28.61 | ×4.60 |

### FXAA, and why its preset is not a performance dial

`plan_modern.md` `MOD-608`. Reproduce: `./cmake-build-cnaext/cna_test_cnaext_fxaa --benchmark`.

Measured on two deliberately opposite images: a flat field, where the shader's early exit is taken
on every texel, and a one-pixel checkerboard, where it is taken nowhere.

| Preset | Threshold | Flat, 1280×720 | Checkerboard, 1280×720 |
|---|---|---|---|
| `Low` | 0.2500 | 3.61 ms | 3.49 ms |
| `Medium` | 0.1250 | 3.64 ms | 3.82 ms |
| `High` | 0.0625 | 3.24 ms | 3.34 ms |
| `Ultra` | 0.0312 | 4.02 ms | 3.55 ms |

At 1920×1080, `Medium`: 8.20 ms flat, 8.13 ms checkerboard.

**Neither axis separates.** The presets do not, and the two images do not — 3.2 to 4.0 ms is the
run-to-run noise of a 20-frame sample on this rasteriser, not a trend. That makes FXAA the one
subsystem here whose quality preset buys **look and nothing else**, and it is worth knowing before
someone reaches for it to save a frame.

The reason is in the shader rather than in the driver: five of its eleven texture samples happen
*before* the threshold test, because the test needs the neighbourhood it is testing. The early exit
saves the later six, and on a fill-bound pass that is not enough to show up. A version that saved
more would have to test on a cheaper signal than the samples themselves — which is a different
algorithm, not a tuning of this one.

What FXAA *does* cost is consistent and modest: about 3.5 ms at 720p and 8.2 ms at 1080p, which is
half of bloom at its default and a fraction of SSAO at any preset. If it needs to be cheaper, the
answer is `setFXAAEnabled(false)`.

### SSAO, per quality preset

`plan_modern.md` `MOD-528`. Reproduce: `./cmake-build-cnaext/cna_test_cnaext_ssao --benchmark`.

| Preset | Samples | SSAO at 1280×720 | SSAO at 1920×1080 |
|---|---|---|---|
| `Low` | 8 | 28.71 ms | 63.71 ms |
| `Medium` | 16 | 45.62 ms | 86.96 ms |
| `High` | 32 | 58.85 ms | 135.83 ms |
| `Ultra` | 64 | 97.39 ms | 222.07 ms |

**SSAO is by a wide margin the most expensive pass in this layer** — `Medium` costs more than bloom
at `Ultra`, and roughly six times the tonemapper. That is inherent rather than a tuning failure: the
shader loops over the kernel *per texel*, so every sample is a dependent texture read across the
whole frame.

Unlike bloom's level count, **the sample count is a real dial**: 8 → 64 samples is 3.4× the time.
Sub-linear, because there is a fixed per-texel cost around the loop, but close enough that a quality
preset genuinely buys frame time here. Two settings before reaching for a lower preset: the
half-resolution option (`MOD-523`, off by default) quarters the texels the loop runs over, and a
smaller radius does not help at all — the cost is the sample count, not the distance.

The prepass itself measures **~0.01 ms**, and that number is a *floor* rather than a cost: the
benchmark draws no geometry into it, so what is timed is the bind and the clear. In a real scene the
prepass is a second pass over the whole geometry and scales with it. It is recorded because it
settles which half to look at — if a prepass is expensive, it is the geometry, and the answer is
fewer draws rather than a cheaper prepass.

### Bloom, per quality preset

`plan_modern.md` `MOD-416`. Reproduce: `./cmake-build-cnaext/cna_test_cnaext_bloom --benchmark`.

| Preset | Levels | 1280×720 | 1920×1080 |
|---|---|---|---|
| `Low` | 2 | 11.92 ms | 24.23 ms |
| `Medium` | 3 | 12.70 ms | 26.95 ms |
| `High` | 5 | 14.82 ms | 26.47 ms |
| `Ultra` | 7 | 15.50 ms | 28.89 ms |

**The finding is that the level count is a weak dial for cost**, and it is the opposite of what the
preset table was drafted assuming. Low to Ultra — two levels against seven — is a 30% difference at
720p, not the threefold one a "quality" setting suggests. The reason is structural: the first level
is half-resolution and dominates, and every level after it is a quarter of the one before, so the
pyramid's tail costs almost nothing. (The 1080p column is noisier — `High` measures below `Medium`
there — which is what a 10-frame sample on a software rasteriser looks like; the 720p column has the
signal.)

So the preset buys **halo width**, not frame time. A frame that cannot afford bloom cannot afford it
at `Low` either, and the answer there is `setBloomEnabled(false)`, which costs exactly nothing
because a disabled pass is removed from the chain rather than run with its effect off (`MOD-209`).

Two more things a reader should know before comparing these numbers with the per-pass table above:
the 28.61 ms recorded there is bloom at its **default** four iterations, and these runs put a real
HDR (`HdrBlendable`) target under the pass, which the per-pass table did not.

Tonemap again at two resolutions (`cnaext_tonemap_test --benchmark`, `MOD-319`), because it is the
one pass a game cannot switch off and so the one whose scaling matters most:

| Resolution | Tonemap, ms/frame |
|---|---|
| 1280×720 | 7.78 |
| 1920×1080 | 16.82 |

1920×1080 is 2.25× the pixels of 1280×720 and costs 2.16× the time — it scales with pixel count and
with nothing else, which is what one dependent texture read and a curve per texel should do. There is
no resolution at which tonemapping becomes the problem.

The shape is the useful part and it survives hardware, even though the absolute numbers do not:

- **Tonemap is nearly free.** It is one dependent texture read and some arithmetic per texel — the
  same work a copy does, plus a curve. There is no performance reason to switch it off.
- **FXAA costs about a second copy.** It reads a neighbourhood rather than a texel, so it pays for
  the extra taps and nothing else.
- **Bloom is the expensive one, by a factor of four or five**, and it is expensive for a structural
  reason rather than a tuning one: it is not one pass but a pyramid — threshold extract, then
  downsample and upsample per mip, then composite. `bloomIterations` is therefore the setting a
  quality preset should reach for first; the other three passes together cost less than bloom alone.

These are software-rasteriser numbers and fill-rate-dominated, so hardware compresses all four
substantially — but the *ordering* (bloom ≫ FXAA > tonemap ≈ copy) is a property of how many texels
each pass touches, which no GPU changes.

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

**Corrected 2026-08-19 (`MOD-1906`).** This section used to say bloom, tonemapping, SSAO and FXAA
had no recorded per-pass cost, and cited four row IDs that were never about performance at all
(`MOD-407` is the linear-filter fallback, `MOD-317` the tonemapping ordinal test, `MOD-523`
half-resolution AO, `MOD-606` an FXAA image check — all closed, none of them a benchmark). All four
passes *are* measured above: bloom, tonemapping and FXAA in **Post-process passes**, SSAO in **SSAO,
per quality preset**. The stale paragraph is left visible rather than deleted, because a "not
measured" note that is wrong is worse than none — it sends a reader looking for numbers that are
two screens up.

What genuinely has no numbers here:

- **Every renderer except EasyGL.** Phase 16 measured fifteen renderers for *behaviour*; none of
  them for cost, and on all but one the passes do not run at all (they report `isSupported() ==
  false` and copy through), so a timing would measure the copy. Only a renderer that executes
  shader source can be timed against this table.
- **Any GPU.** Everything here is Mesa llvmpipe. The ratios between passes carry over; the absolute
  numbers do not.
