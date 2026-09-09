# Known bugs

Defects that are **measured and reproducible** but deliberately left unfixed, with the reason. A
row leaves this file when it is fixed, or when it is disproved.

Capabilities CNA simply does not have are **not** bugs and live in
[`known_gaps.md`](known_gaps.md) beside this file.

This is not a backlog of suspicions. Every entry names what was measured, against what reference,
and what the correction would be — so picking one up is a matter of doing the work, not of
rediscovering the problem.

---

## 1. `RasterizerState.DepthBias` is not scaled into the renderer's depth units

**Found:** 2026-09-08, through SAMPLE-073 (SoccerPitch).
**Fixed in:** EasyGL only (`2ce1cf2ff`).
**Open in:** `directx11`, `vulkan`, `magnum`, `opengl2`, `opengl1`, `portablegl`.

### The false premise

CNA was written on the premise, stated as fact in `D3D11StateObjectCache.cpp` and referenced from
the Vulkan and EasyGL renderers, that

> XNA's `RasterizerState.DepthBias` is a float already expressed in units of `r`, the depth buffer
> format's minimum resolvable difference.

**That is false.** XNA is Direct3D 9, and `D3DRS_DEPTHBIAS` there is a **normalized depth value in
[0, 1] added straight to the depth**. The `r`-scaled convention the premise describes arrived with
Direct3D 10; it appears to be that later convention read back onto XNA.

A renderer whose API counts bias in multiples of the smallest resolvable depth step — OpenGL's
`glPolygonOffset` `units`, Vulkan's `depthBiasConstantFactor`, D3D10+'s integer `DepthBias` — must
therefore multiply XNA's value by `2^bits` before passing it on. Passing it through unconverted
applies roughly `2^-24` of what the game asked for, which is indistinguishable from applying no
bias at all.

### The measurement

SAMPLE-073 lifts its flattened ball shadow off the pitch with `DepthBias = -0.0001f`
(`SoccerPitchGame.cs:143-144`), and the port sets exactly that (`SoccerPitchGame.cpp:98`, applied
at `:191`).

| | Shadow |
|---|---|
| Real XNA on D3D9, under Wine/DXVK | a solid black ellipse |
| CNA/EasyGL before the fix | shredded into horizontal scanlines, grass and the white marking showing through |
| CNA/EasyGL after the fix | solid, indistinguishable in character from XNA's |

`-0.0001` reached the rasterizer as about `-6e-12`.

Captures: `/rv/tmp/samples/SAMPLE-073-SoccerPitchSample_4_0/evidence/`, original under
`xna4-original-windows-reach-diagnostic/`. Note that this sample's camera is a pure function of
elapsed game time, so only captures taken at the same elapsed time can be compared at all — see
`samples/SoccerPitch/missing.md` in `cna-samples`.

### The correction, as taken in EasyGL

`EasyGLDepthBiasToPolygonOffsetUnits(depthBias, bits)` multiplies by `2^bits`, where `bits` is the
precision of **whatever depth buffer is bound**: a render target's own `DepthFormat` through the
new `IRenderTargetRenderer::DepthBufferBitsEXT()` / `IRenderTargetCubeRenderer::DepthBufferBitsEXT()`
(defaulted to 24, so no renderer is obliged to implement it), otherwise the backbuffer's, which
EasyGL tracks from `UpdatePresentationFormatEXT`.

`SlopeScaleDepthBias` needs **no** conversion: GL's `factor` multiplies the polygon's depth slope,
which is the same quantity `D3DRS_SLOPESCALEDEPTHBIAS` multiplies.

Tests: `EasyGLDepthBias.IsScaledByTheDepthBuffersOwnResolution`,
`EasyGLDepthBias.DepthFormatOrdinalsMapToTheirRealPrecision`.

### What is still wrong, and where

| Renderer | Where | What `-0.0001f` becomes |
|---|---|---|
| `directx11` | `D3D11StateObjectCache.cpp`, `D3D11RasterizerStateCache::GetOrCreate` | **`0`** — `lround(-0.0001)` rounds it away, so the bias is dropped entirely |
| `vulkan` | `VulkanRenderer::ApplyRasterizerState` | unscaled into `vkCmdSetDepthBias`'s `depthBiasConstantFactor` |
| `magnum` | `MagnumRenderer.cpp:874` | unscaled into `Renderer::setPolygonOffset` |
| `opengl2` | `OpenGL2Renderer.cpp:3831` | unscaled into `glPolygonOffset` |
| `opengl1` | `OpenGL1Renderer.cpp:377` | unscaled into `glPolygonOffset` |
| `portablegl` | `PortableGLRenderer.cpp:1348` | unscaled into `glPolygonOffset` |

`opengles1` is **exempt**: ES 1.1 has no `glPolygonOffset` and it already discards both values
explicitly (`OpenGLES1Renderer.cpp:2613`).

`software`, `tinygl` and the 2D-only renderers have no depth bias to get wrong.

### Why they were not fixed with EasyGL

None of them can be run on this host, and an unverified change to a renderer's rasterizer state is
what this project's rules exclude. The arithmetic is identical in every case; what each one still
needs is the bound depth buffer's precision, which is exactly what `DepthBufferBitsEXT()` already
provides.

Two of them are reachable with effort rather than new hardware, and are the natural place to
start: **Vulkan** runs here on the real display (AMD 780M + RADV), and **directx11** runs under
Wine with DXVK — the recipe is in the memory notes and in `docs/`. `directx11` is also the worst
of the six, because it does not merely weaken the bias, it deletes it.

`CLAUDE.md`: where XNA and FNA disagree, XNA wins. This is measured against real XNA.

---

## 2. EasyGL does not fill the letterbox rectangle with a default viewport

**Found:** 2026-09-09, by merging `webgpu` into `next` and running that branch's
renderer-neutral presentation tests in an EasyGL build for the first time.
**Open in:** `easygl` (measured on the `OPENGLES3` profile).
**Not affected:** `webgpu`, which passes the same test (`plans/plan_webgpu.md` `WEBGPU-162`).

### The measurement

`PresentationRectangleTest.ALetterboxedDefaultViewportIsNotACustomSubViewport`
(`modules/graphics/tests/CNA/Internal/Renderers/Common/PresentationRectangleTests.cpp`), OPENGLES3
Debug `build/`, Xvfb `:99`, an 800x480 drawable:

| | |
|---|---|
| Presentation mode | `Letterbox`, virtual resolution 240x240 |
| Rectangle the renderer itself reports | `(160,0,480x480)` — **correct** |
| `GraphicsDevice.Viewport`, in logical units | `(0,0,240,240)` |
| Sprite drawn | `Rectangle(0,0,240,240)`, i.e. the whole logical area |
| Where the ink landed | `(0,245)-(792,476)` — the full width of the drawable, bottom half |

The renderer's own `GetDefaultViewportRect()` answer is right, which rules out the presentation
state: the mode, the virtual resolution and the computed rectangle are all what they should be. The
error is downstream of that, between the logical viewport and the rasterizer.

### Why it was not found before

The test is renderer-neutral but its sixth case is new with `WEBGPU-162`, and that row's evidence is
a **WEBGPU** build (`6/6`) plus the five older cases on `OPENGL33`. The `webgpu` branch never ran
this case against EasyGL, and `next` did not have the case. The merge is the first time the two met.
Every file the test exercises — `GraphicsDevice.cpp`, `SpriteBatch.cpp` and all of
`modules/renderers/easygl` — is byte-identical to `next`, so this is `next`'s EasyGL behaviour, not
a merge interaction.

### Partly fixed on 2026-09-09, and the rest is measured now

**The discriminator was one of the two causes and is fixed** (`modules/renderers/easygl/src/
EasyGLRenderer.cpp`, sprite flush). It asked *"does the GL viewport differ from the full target?"*,
and under Letterbox the **default** viewport is the presentation rectangle, which differs from the
drawable by construction — so the default viewport was classified as a game-set sub-viewport and
the sprite projection was sized to the rectangle's PHYSICAL extent instead of the logical one. It
now compares against `GetDefaultViewportRect()` (with the same Y flip `SetViewport` applies), keeps
the presentation rectangle as the rasterizer viewport instead of resetting to the whole drawable,
and converts a genuine sub-viewport back into logical units before building the ortho.

Effect on the failing case, 800x480 drawable, virtual 240x240, letterbox `(160,0,480x480)`:

| | sprite bounding box |
|---|---|
| before | `(0,245)-(792,476)` |
| after | `(0,0)-(792,476)` |
| expected | fill `(160,0,480x480)` |

So the vertical half of the error is gone and the horizontal half is not: the sprite still spans the
full drawable width instead of the letterbox rectangle. Whatever sets the GL viewport last before
the batch rasterizes is still handing it the whole drawable — the next step is to measure the GL
viewport at flush time rather than reason about it, which is what the earlier note here should have
said instead of listing candidates.

Full suite after the change: `CnaGraphicsTests` 2369 passed, this one still failing, nothing else
regressed. `SAMPLE-077` at its native 480x800 renders byte-identically to before the change.

**Why it matters more than it did this morning.** Letterbox became the default presentation mode on
2026-09-08 (`f13701188`), so this is no longer a corner a game opts into. `SAMPLE-077` (DynamicMenu)
is a shipped `✅` sample and its menu is misplaced badly enough to be unusable in any window that is
not exactly its 480x800 back buffer — the owner reported it as "the menu is cut off and the third
item cannot be launched". Reproduction:
`/rv/tmp/samples/SAMPLE-077-DynamicMenu_4_0/scripts/probe-resize.sh 960 800`.

### What the rest of the correction is likely to be — **not** measured

`WEBGPU-162` names three parts, and EasyGL demonstrably has the first (`EasyGLSurfaceState::
GetDefaultViewportRect()` predates it and its answer is correct above). The two untested candidates
are the same row's other two: the `customViewport` discriminator asking whether the viewport *is*
the presentation rectangle rather than whether it merely differs from the target extent, and the
sprite bake's divisor, which must be the LOGICAL extent where the rasterizer viewport already
carries the presentation scale. The observed shape — full drawable width, half height — is
consistent with the rectangle not reaching the rasterizer at all, which neither candidate fully
explains. Diagnose before changing anything.

---

## 3. Two parity fixtures are registered on EasyGL profiles that cannot pass them

**Found:** 2026-09-09, same run as §2.
**Open in:** the fixture registration, not in a renderer.

`EasyGL_Parity_sampler_lod_bias` and `EasyGL_Parity_sprite_sampler_state` fail in an `OPENGLES3`
build, and **only on their LOD-bias legs and the discriminators derived from them** — every leg
that does not involve a bias passes, including both fixtures' "an unbiased sprite and an unbiased 3D
quad agree on the natural level" and "`SpriteBatch.Begin`'s `MipMapLevelOfDetailBias` does NOT reach
the sprite" arms. That is not a defect being reported: `EasyGLRenderer.cpp` already states it in the source, at the site that would apply
it, because `GL_TEXTURE_LOD_BIAS` does not exist in OpenGL ES at all. It is desktop-GL only, and
`docs/cross-renderer-parity-fixtures.md`'s own recipe runs the EasyGL half on an `OPENGL33` build.

`cna_register_parity_fixtures()` registers every fixture for the EasyGL family regardless of which
GL profile the build selected, so a build that cannot honour `SamplerState.MipMapLevelOfDetailBias`
still gets a test that asserts it does. The fix belongs to the parity framework, not to EasyGL:
either gate these two on the profile, or give a fixture the documented-divergence route
`fill_mode_wireframe` already uses. Until then these two are expected red in any EasyGL ES build.

