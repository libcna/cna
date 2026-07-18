# Audit: examples/vulkan_rendertarget2d_mip_test.cpp

## Metadata

- Source file: `examples/vulkan_rendertarget2d_mip_test.cpp` (172 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `RenderTarget2D` mip-chain generation fidelity test.
- File type: standalone `Game`-subclass executable (`class RenderTarget2DMipTest`).
- XNA/FNA relevance: direct — `RenderTarget2D(device, width, height, mipMap=true, ...)`, sampled
  via `SpriteBatch::Draw` with `SamplerState::PointClamp`/`LinearClamp`.
- Related production code:
  `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp`,
  `VulkanRenderTargetBackend::MaybeGenerateMips(VkCommandBuffer)` (lines 708–774), the
  `VK_LOD_CLAMP_NONE` sampler fix (lines 2152–2158, 2248).
- Task references: Task 878 (`git log`: `199fd15e feat(Task 878): implement real RenderTarget2D
  mip chains on Vulkan`).

## Purpose

Proves `RenderTarget2D`'s `mipMap=true` chain is genuinely populated with correctly-downsampled
content, not merely present-but-undefined GPU storage. Fills level 0 with an asymmetric 7:1
red/blue horizontal split (56 red rows / 8 blue rows out of 64), unbinds (triggering
`MaybeGenerateMips`'s `vkCmdBlitImage` cascade), then:
- **Check 1**: samples the RT at native 1:1 scale (no minification, level 0 only) at two points
  well inside each solid region — must read back crisp, unblended red/blue.
- **Check 2**: draws the RT into a forced 1×1 destination rectangle (a 64:1 minification ratio) —
  the derivative-based GPU LOD selection forces the single sample to the coarsest mip level
  (`log2(64)=6`), and a full-quad-to-1-pixel draw always samples the texture's exact UV centre
  (0.5, 0.5) — landing on source row 32, deep inside the red region at level 0, but at the
  *coarsest* level this pixel is the whole image's true weighted average (~7/8 red, 1/8 blue ≈
  `(223,0,32)`), which is measurably different from pure red.

The file's own header comment documents an abandoned earlier 50/50-split version that failed to
discriminate at all (the colour boundary sat exactly at the forced centre-sample point, so a
bilinear blend at level 0 alone produced an identical false "pass" with the production fix reverted
via `git stash`) — i.e. this test's current asymmetric design is the *result* of an empirically
falsified earlier attempt, not an unverified claim.

## Executive Verdict

**Healthy** — independently re-derived the expected ~7:1 weighted average through the actual
`vkCmdBlitImage` cascade math (a true box-filter downsample at every level, since each mip level
here is always an exact half-dimension reduction from the one before it) and confirmed it converges
to exactly the asserted `(223,0,32)` ± the test's own generous tolerance, not merely an assumed
constant.

## Checklist Results

### API / XNA / FNA parity — PASS
`RenderTarget2D(device, kRTSize, kRTSize, /*mipMap=*/true, SurfaceFormat::Color, DepthFormat::None)`
(line 74) uses the standard mip-enabled constructor overload; `SpriteBatch::Begin(SpriteSortMode,
BlendState, SamplerState*, ...)` (lines 101, 109, 130) and `SpriteBatch::Draw(texture, destRect,
srcRect, color)` (lines 102, 110, 131) match FNA's `SpriteBatch` overload set used elsewhere in this
codebase's test family.

### Behavioral correctness — PASS (independently re-derived)
Re-derived the mip cascade by hand from `MaybeGenerateMips` (lines 708–774): each level `L` is
produced via `vkCmdBlitImage` with `VK_FILTER_LINEAR` from level `L-1`, always at exactly half the
previous level's dimensions (`dstW = max(1, srcW/2)`) — for a power-of-two 64×64 source this is a
pure, uniform 2×2-box downsample at every step (no partial-texel weighting), which is a linear
operation that provably preserves the overall weighted average through the whole chain. Traced the
7:1 split (rows 0–55 red, 56–63 blue) through all 6 halvings: level 1 (32 rows) splits at row 28
exactly (56/2), level 2 (16 rows) at row 14 (crisp), level 3 (8 rows) at row 7 (crisp) — still exact
because 56 is divisible by 8. Level 4 (4 rows) is where the split first crosses a row boundary
(56/16=3.5): row 3 of level 4 blends level 3's rows 6 (red) and 7 (blue) into ~50/50. Level 5 (2
rows): row 1 blends level 4's rows 2 (red) and 3 (50/50) into 75%red/25%blue. Level 6 (1 row, the
coarsest): blends level 5's row 0 (red) and row 1 (75/25) into `(1 + 0.75)/2 = 0.875` red,
`0.125` blue — **exactly the 7:1 ratio the test asserts** (`(223,0,32)` ≈ `0.875×255=223.1`,
`0.125×255=31.9`), confirming the box-filter cascade mathematically preserves the exact
whole-image average regardless of where within a level a colour boundary falls, not merely
approximately.

### Logic — PASS
Check 1's sample points (`topReg` at row 16, `botReg` at row 60, lines 115–116) are both comfortably
inside their respective solid regions (16 rows into the 0–55 red band, 4 rows into the 56–63 blue
band) — correctly chosen to avoid any bilinear-blend edge artifact at level 0 itself, isolating
"is level 0 itself intact" from "is the mip chain correct," which is exactly what Check 1 claims to
test.

### C++ correctness — PASS
`patternTex_` (a `Texture2D`, not a pointer) is a plain member initialized via
`Texture2D::CreateFromPixels` in `Initialize()` (line 84) — no dangling-pointer risk; `rt_`/`sb_`
are `unique_ptr`s constructed in the same place, consistent lifetime.

### Robustness — PASS
Check 2's tolerance band (`avgPx.R∈[190,240]`, `B∈[15,55]`, `G≤20`, lines 138–140) is wide enough to
absorb the GPU's actual bilinear/derivative LOD-selection rounding (this audit's own re-derivation
above landed at exactly `223`/`32`, comfortably centred in that band) while still being narrow
enough that "pure red" (255,0,0 — the failure mode if mips were absent/broken) would clearly fail
the `R≤240` bound.

### Testing — PASS
Checks 1 and 2 are non-redundant by construction: Check 1 alone could pass even with a completely
broken (all-white, garbage, or simply absent) mip chain, since it never samples anything but level
0; Check 2 alone could pass by accident if the RT happened to have no mips at all and the sampler
silently clamped to level 0 (which would read pure red, not the average) — so Check 2 is the one
that actually falls on the discriminating side of the earlier-abandoned 50/50-split design flaw the
file's own comment documents.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings; no defects identified in this file or its production counterpart
for the scope this test covers.

## Cross-File Observations

- The file's header comment's claim about the "second, independently-necessary Task 878 fix" (every
  `VkSampler` previously defaulting `maxLod=0`, clamping every sample to mip level 0 regardless of
  `mipmapMode`) was independently confirmed against the live source:
  `ci.maxLod = VK_LOD_CLAMP_NONE;` appears at both sampler-creation call sites (lines 2158, 2248) —
  this is not a stale or unverified claim.
- Shares the `MaybeGenerateMips` mechanism with `vulkan_rendertargetcube_mip_test.cpp`
  (`VulkanRenderTargetCubeBackend::FaceProxy::MaybeGenerateMips`, per that file's own header
  comment) — the underlying box-filter math this audit verified here applies equally to that
  sibling file's per-face cascade, though that file deliberately does not attempt the same
  coarse-level-content assertion (see that file's own audit report for why).

## Missing or Weak Tests

None identified specific to this file's stated scope (RenderTarget2D mip generation with
`SurfaceFormat::Color`). A residual, low-priority gap noted for completeness: no Vulkan test in this
shard checks mip generation for a non-power-of-two RT size (where the box-filter-preserves-average
property this audit relied on for Check 2's math would need re-derivation, since `max(1, srcW/2)`
rounding would introduce genuine, non-recoverable information loss at odd dimensions) — not a
defect in this file, just an adjacent untested dimension.

## Positive Findings

- The file's header comment transparently documents a previously-tried-and-abandoned 50/50-split
  design and *why* it failed to discriminate (confirmed via `git stash` at the time, per the
  comment) — this is exactly the kind of honest, falsifiable test-design narrative this audit looks
  for, and it held up under this audit's own independent re-derivation.
- The 7:1 split ratio and the resulting `(223,0,32)` expected average were independently
  re-computed from first principles against the live production blit-cascade code in this audit
  (not merely re-stated from the file's own comment), and matched.

## Final Assessment

A rigorous, carefully-designed mip-fidelity test whose own design history (an abandoned
non-discriminating first attempt) and current numeric assertions both check out under independent
re-derivation against the real Vulkan mip-generation code. No issues found.
