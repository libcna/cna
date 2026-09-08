# Known bugs

Defects that are **measured and reproducible** but deliberately left unfixed, with the reason. A
row leaves this file when it is fixed, or when it is disproved.

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
