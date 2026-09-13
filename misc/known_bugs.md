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

## 2. EasyGL mishandles a default letterbox viewport and physical backbuffer readback

**Found:** 2026-09-09, by merging `webgpu` into `next` and running that branch's
renderer-neutral presentation tests in an EasyGL build for the first time.
**Re-audited:** 2026-09-11 by `plans/plan_sdlgpu.md` `SDLGPU-120`; the original
measurement conflated rendered pixels with failed readback bytes.
**Open in:** `easygl` (measured on both the original `OPENGLES3` profile and current `OPENGL33`).
**Not affected:** `webgpu`, which passes the same test (`plans/plan_webgpu.md` `WEBGPU-162`).
SDL GPU also passes the same current test (`plans/plan_sdlgpu.md` `SDLGPU-68/120`).

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
| Coarse scanner's apparent non-clear bounds | `(0,245)-(792,476)` — **not ink**; these are zero-filled failed reads |
| Corrected ink-matching scanner | `EMPTY`; the public readback path cannot observe the actual ink rows |
| Centre of the expected rectangle | `(0,0,0)` instead of ink `(230,70,40)` |

The renderer's own `GetDefaultViewportRect()` answer is right, so the mode, virtual resolution and
rectangle computation are sound. There are two downstream defects:

1. `EasyGLSpriteBatchRenderer::FlushBatch()` calls any GL viewport different from the complete
   physical target “custom.” A default letterbox rectangle necessarily differs. It consequently
   uses the physical 480x480 rectangle as the sprite projection extent, so a logical 240x240 sprite
   covers only half of that rectangle. The compiled-effect sprite route is wrong in the opposite
   direction: it resets the viewport to the entire 800x480 drawable, including the bars.
2. `EasyGLRenderer::ReadBackbuffer()` flips top-left coordinates using `GetViewportSize()`. That is
   the logical 240-pixel virtual height here, not the physical 480-pixel default framebuffer. Reads
   at public `y >= 240` therefore use a negative GL Y and leave the zero-initialised transfer bytes
   untouched. The diagnostic scanner treated every value merely different from the clear colour as
   ink, so those failed zero reads produced the reported `(0,245)-(792,476)` rectangle.

Thus the original pass/fail verdict was correct but the reported “where the ink landed” evidence
was not. No renderer output has been used to claim an exact ink bounding box while its own readback
path is invalid for half of the framebuffer.

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

So the vertical half of the error is gone and the horizontal half is not.

**Measured at flush time on the second pass** (`CNA_EASYGL_SPRITE_VIEWPORT_DEBUG=1`, an env-gated
print left in the sprite flush because the next person needs it too):

```
[spritevp] entry=(160,0,480x480) def=(160,0,480x480) custom=0 full=(800x480) log=(240x240)
           atDraw=(160,0,480x480)
```

Everything the renderer does is now right: the rasterizer viewport IS the letterbox rectangle and
the projection IS the logical size.

**The remaining failure is the test's, and it is measured.** With `CNA_BACKBUFFER_READ_TRACE=1`:

```
[GFX-165] GetBackBufferData backbuffer=800x480 viewport=240x240 region=(400,240,1x1)
```

The back buffer is 800x480 while the renderer's logical size is 240x240 — a state **no game can
produce**. `GraphicsDevice::SetVirtualResolution()` sets both the renderer's virtual resolution and
`PresentationParameters.BackBufferWidth/Height` together, so in any real game the back buffer IS the
logical surface. That is also the XNA-faithful reading: the back buffer is what the game asked for.
This test bypasses it, calling `renderer.SetVirtualResolution()` directly because the device's own
setter is private, and then reads physical drawable coordinates out of a back buffer a real game
would have made 240x240.

XNA cannot arbitrate between the two spaces, because it has no virtual resolution: its back buffer
and its drawable are the same thing. Worth establishing rather than assuming — it is why the answer
has to come from CNA's own contract instead.

**So the correction belongs in the test, not in `GetBackBufferData`**: give it a way to set the
virtual resolution through `GraphicsDevice` so the two stay in step, then read in back-buffer
coordinates. Do not change the readback to satisfy an assertion built on a state the framework
cannot reach.

**A second, real defect is still open, and it is the one a user sees.** SAMPLE-077 at 960x800 now
draws in the right place — the content moved from the drawable origin back to the letterbox
rectangle — but the layout *inside* the rectangle is still wrong: the checkerboard panel renders as
a narrow vertical strip and the four menu ellipses do not render at all. The sample sets no
`Viewport`, no scissor and no `RenderTarget2D`, so none of those explain it; the next suspect is the
batch `transform_` or a second draw path. Reproduce with
`SAMPLE-077 .../scripts/probe-resize.sh 960 800`, compare `evidence/resize-final.png` against
`evidence/original-windows-hidef-diagnostic/01-page1.png`.

Full suite after the change: `CnaGraphicsTests` 2369 passed, this one still failing, nothing else
regressed. `SAMPLE-077` at its native 480x800 renders byte-identically to before the change.

**Why it matters more than it did this morning.** Letterbox became the default presentation mode on
2026-09-08 (`f13701188`), so this is no longer a corner a game opts into. `SAMPLE-077` (DynamicMenu)
is a shipped `✅` sample and its menu is misplaced badly enough to be unusable in any window that is
not exactly its 480x800 back buffer — the owner reported it as "the menu is cut off and the third
item cannot be launched". Reproduction:
`/rv/tmp/samples/SAMPLE-077-DynamicMenu_4_0/scripts/probe-resize.sh 960 800`.

### The two causes found so far

1. **The discriminator** asked whether the GL viewport differed from the whole drawable. Under
   Letterbox the default viewport is the presentation rectangle, which differs by construction, so
   the default viewport was classified as a game-set sub-viewport. Fixed.
2. **Staleness.** A window resize moves the presentation rectangle while the GL viewport still
   holds the previous one, so *any* comparison of live GL state against the fresh rectangle reads
   the stale viewport as a custom one — and then preserves it. That is what left SAMPLE-077 drawing
   at the old rectangle's origin after a resize. Fixed by deciding default-versus-custom inside
   `SetViewport()`, while the rectangle is the one the call was derived from, and exposing it as
   `EasyGLRenderer::ViewportIsDefaultEXT()`.

### What the rest of the correction is likely to be — **not** measured

The stock and compiled SpriteBatch routes must both preserve the physical presentation rectangle,
classify it as the default rather than a caller sub-viewport, and project logical sprite coordinates
over the logical extent. `ReadBackbuffer()` must independently use the physical default-framebuffer
height for its Y flip. These are EasyGL-local tasks; SDL GPU and WebGPU must not be changed to match
the defects.

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


---

## 4. `Window.CurrentOrientation` follows the OS window, not the surface the game draws into

**Found:** 2026-09-09, through SAMPLE-077 (DynamicMenu).
**Open in:** `modules/runtime/src/GameWindow.cpp` (`refreshCachedPlatformState` →
`orientationFromBounds(clientBounds_)`).

`GameWindow` derives the current orientation from the **platform window's client bounds**. A game
with a virtual resolution does not draw into those bounds: it draws into the logical surface, which
`GraphicsDevice` letterboxes inside them. So widening a window past its own height reports
`LandscapeLeft` while the drawing surface stays portrait, and a game that believes the report lays
itself out for a shape it does not have.

### The measurement

SAMPLE-077 has a 480x800 back buffer and its own portrait and landscape layouts
(`PhoneScreen::UpdateOrientation`). Counting non-background pixels in the lower half of the frame,
where its menu panel belongs:

| window | shape | menu panel |
|---|---|---|
| 700x900 | taller than wide | **12 656** samples — renders correctly, letterboxed and scaled |
| 960x800 | wider than tall | **200** samples — the panel is off the logical area |

Nothing about the two differs except which side is longer. The renderer is doing its job in both:
the sprite flush reports `def=(240,0,480x800) custom=0 log=(480x800) atDraw=(240,0,480x800)` on
every one of 3 300 flushes in the landscape case. The sample is doing its job too — it was told
landscape and moved its containers to `HorizontalContainer2Left/Top`, which is off a 480-wide
logical surface.

Reproduce: `/rv/tmp/samples/SAMPLE-077-DynamicMenu_4_0/scripts/probe-resize.sh 960 800` against
`700 900`.

### What XNA does

On Windows Phone an orientation change rotates the **back buffer** with the device — `XNA` swaps
`PreferredBackBufferWidth`/`Height` for a supported orientation, so the reported orientation and the
drawing surface always agree. CNA keeps the virtual resolution fixed and letterboxes, so reporting
the change without swapping tells the game something its own surface contradicts.

### Fixed on 2026-09-09

`GameWindow` now takes the shape from the surface the game draws into: `GraphicsDeviceManager`
installs a logical-size provider at construction — not in `ApplyChanges()`, which returns early
whenever nothing changed and so cannot be relied on to run — and `refreshCachedPlatformState` asks
it before deriving the orientation, falling back to the client bounds when nothing answers.

| window | menu-panel pixels, before | after |
|---|---|---|
| 480x800 (native) | 10 000 | 10 000 |
| 700x900 (portrait) | 12 656 | 12 656 |
| 960x800 (landscape) | **200** | **10 000** |

Option 1 below was taken. The argument that settled it is that a desktop window resize is not a
device rotation: XNA swaps the back buffer when the *device* rotates, and reports nothing for a
window that merely got wider. Option 2 stays the right answer for a real mobile target, and is
written up below for whoever builds one.

### The correction — a decision, not just a patch

Two coherent answers, and they are not equivalent:

1. **Derive the orientation from the logical surface** whenever a virtual resolution is in effect.
   Narrow, fixes this, and means a desktop window resize never reports an orientation change — which
   is arguably right, since resizing a window is not rotating a device.
2. **Swap the virtual resolution with the orientation**, as XNA swaps the back buffer. Faithful to
   XNA, and much larger: every letterbox rectangle, input mapping and content layout follows.

There is a third question underneath both: `Window.ClientBounds` currently reports the OS window
too, and XNA's is the back-buffer area. If `ClientBounds` became the logical surface, the
orientation would follow it for free — but every other consumer of `ClientBounds` changes with it.

Not chosen here: the sample campaign found it, the answer changes framework semantics, and it wants
an owner decision rather than whichever patch makes SAMPLE-077 look right.

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
