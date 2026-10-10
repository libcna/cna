# Known bugs — measurement records

**This is not a bug list.** The one authoritative list of current known bugs and limitations is
[`NEXT.md`](../NEXT.md) §5, and only that list says whether something is open. This file keeps the
measurements behind some of its entries: what was measured, against which reference, and what the
correction is. When the §5 entry is closed, delete its record here in the same commit. Section
numbers are stable references, so a removed record leaves a gap.

Capabilities CNA lacks by design are recorded the same way in [`known_gaps.md`](known_gaps.md).

Removed on 2026-10-10 because they were found fixed: §2 EasyGL letterbox viewport and readback
(`7bcf3f496`, `1dbc94f28`), §3 parity fixtures on GL ES profiles (`570d76677`), §4
`Window.CurrentOrientation` following the OS window (`0285061fa`). Their records are in Git history.

---


## 1. `RasterizerState.DepthBias` is not scaled into the renderer's depth units

**Found:** 2026-09-08, through SAMPLE-073 (SoccerPitch).
**Fixed in:** EasyGL (`2ce1cf2ff`) and DirectX 11 (`23e8edbee`, DX-256); SDL_GPU, WebGPU and
Metal scale it as well.
**Open in:** `vulkan` (`NEXT.md` §5.1 item 1).

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
| `vulkan` | `VulkanRenderer::ApplyRasterizerState`, recorded with `vkCmdSetDepthBias` | unscaled into `depthBiasConstantFactor` |

DirectX 11 used to round the value away entirely; `23e8edbee` (DX-256) fixed it with
`XnaDepthBiasToD3D`, the same arithmetic the Vulkan fix needs. `software` and the 2D-only renderers
have no depth bias to get wrong. The fix needs the bound depth buffer's precision, which
`DepthBufferBitsEXT()` already provides; test it through the private runner on a real GPU.

`CLAUDE.md`: where XNA and FNA disagree, XNA wins. This is measured against real XNA.

---

## 5. The XACT audio suite's failures rotate, so a stable failure COUNT hides a changing set

**Found:** 2026-09-11, while comparing two full-suite runs across the `vulkan` merge.
**Open in:** `modules/audio` — `CueTest`, `SoundBankTest`, `WaveBankTest`.
**Not caused by any renderer work:** neither the `dx` nor the `vulkan` branch changed a single file
under `modules/audio/`, verified by diff against the merge base.

### What was measured

Three full runs of the same 10 563-test suite, on three commits, each reporting **32 failures** —
and each time a *different* subset of the audio family was among them:

| Run | Audio cases that failed |
|---|---|
| after the `dx` merge (`c1c017cd7`) | `CueTest.PlayingCueNaturallyTransitionsToStoppedAfterPlaybackFinishes`, `CueTest.PauseAfterNaturalCompletionIsANoOp` |
| during the `vulkan` merge | the two above, plus `SoundBankTest.IsInUseFalseSoonAfterFireAndForgetCueNaturallyFinishes` and `WaveBankTest.IsInUseFalseSoonAfterCueNaturallyFinishesWithoutExplicitStop` |
| after the `vulkan` merge (`93ca4ffdf`) | `CueTest.PauseAfterDisposeIsANoOp` — a case that had passed in both earlier runs — plus the same `SoundBankTest` and `WaveBankTest` |

Every one of them passes when run alone or in a small serial set, and fails under load. They assert
that a cue has *naturally finished* after a wall-clock wait, so they measure the machine's audio
timing as much as CNA's bookkeeping.

### Why this is written down rather than left as "flaky"

The failure count is stable while the membership is not. A summary that says *"32 failures, same as
the baseline"* is therefore true and useless at the same time: it reads as "nothing changed" for as
long as nobody diffs the NAMES. This merge's real regressions were found only because both
directions of the comparison were taken — what appeared **and** what disappeared — and the same
diff is what exposed this.

**So: never conclude "unchanged" from a matching failure count.** Compare the sets.

### What the correction would be

Not a retry loop, which would hide it again. Either drive the audio clock deterministically in
these tests so "naturally finished" is a fact rather than a race, or mark the family as requiring
an exclusive machine the way `TwoProcessLoopbackTest` and the `ENet*` suites already are, so a
loaded run does not report a product defect. The first is better; the second is honest.
