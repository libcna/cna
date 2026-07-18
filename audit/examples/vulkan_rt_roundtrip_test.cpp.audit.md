# Audit: examples/vulkan_rt_roundtrip_test.cpp

## Metadata

- Source file: `examples/vulkan_rt_roundtrip_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — Clear()-only render-target regression test, Vulkan
  backend
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_rt_roundtrip …)` / `cna_register_backend_test(NAME
  Vulkan_RenderTarget2D_ClearOnlyRoundtrip …)`, `cmake/Tests/VulkanTests.cmake:370-373`).
- XNA/FNA relevance: direct — `GraphicsDevice.Clear()`/`SetRenderTarget` semantics: in real
  XNA/FNA, `Clear()` takes effect on whatever target is currently bound regardless of whether a
  draw call follows.
- FNA reference: `Graphics/GraphicsDevice.cs` (`Clear` overloads — no XNA/FNA precondition that a
  draw call must follow a `Clear()` for it to take effect).
- Related production code: `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp`
  (`Clear()` lines 6214-6221, `clearedRTs_` construction into `usedRTs` lines 6694-6708,
  `include/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.hpp:1169` `clearedRTs_` member
  declaration).

## Purpose

Task 875's own regression test: proves that `SetRenderTarget(rt); Clear(color);
SetRenderTarget(nullptr);` — a real, XNA-legal pattern with **no draw call** between bind and
unbind — actually leaves the render target's colour image populated and sampleable on Vulkan. Its
header comment gives an unusually detailed "before/after" account of the bug this test both fixed
and now guards: `VulkanGraphicsBackend::Clear()` previously only recorded a global clear-colour
scalar and never registered the currently-bound RT as "used," so a Clear()-only fill silently
never got a render pass recorded at all, leaving the image at `VK_IMAGE_LAYOUT_UNDEFINED` forever.
`ClearOnlyThenSample()` fills a small (8×8) `RenderTarget2D` via `Clear()` alone, unbinds, forces a
frame boundary via a dummy `GetBackBufferData` call (to make the RT's own render pass execute
while the shared clear-colour scalar still holds the RT's fill colour), then samples the RT back
via `SpriteBatch` onto a neutral black backdrop and reads the centre pixel. Run twice (green, then
blue) to rule out a hardcoded/stale value.

## Executive Verdict

**Healthy** — this is the strongest file in the batch: its own bug narrative was independently
re-derived from the current `VulkanGraphicsBackend.cpp` (not merely trusted), the fix it claims to
exercise is genuinely present and reachable through exactly the code path described, and its
two-colour repeat design is a real defense against a hardcoded-pass shortcut.

## Checklist Results

### Behavioral correctness
Independently traced the fix path claimed by the header comment:
1. `VulkanGraphicsBackend::Clear()` (line 6214): `if (currentRT_ && … ) clearedRTs_.push_back(
   currentRT_);` — this is the exact mechanism the comment describes as the fix ("Task 875: mark
   the currently-bound RT as needing its render pass recorded this frame, even if no draw call
   follows").
2. `RecordCommandBuffer()`'s Phase-1 `usedRTs` construction (lines 6696-6708) folds
   `clearedRTs_` in *first*, before the `activeBatches_`/`pending3D_`-derived entries — so an RT
   that was only `Clear()`-ed (never drawn to) is still included in the render-pass loop that
   follows (line 6709 onward), and therefore still gets its `VK_IMAGE_LAYOUT_UNDEFINED` →
   `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` transition recorded.
3. `clearedRTs_.clear()` at line 6811 resets the list each frame, matching the per-frame semantics
   the comment implies (this is not a permanent "sticky" registration).

This confirms the fix genuinely exists and is genuinely exercised by this exact
`SetRenderTarget→Clear→SetRenderTarget(nullptr)` sequence, not merely claimed.

### Logic
`ClearOnlyThenSample()`'s ordering is deliberate and its own comment explains *why*: the dummy
`GetBackBufferData` call forces `RecordCommandBuffer` to run (and hence record the RT's own
render pass with the shared `clearR_/G_/B_/A_` scalar still holding `fillColor`) *before* the
subsequent `dev.Clear(Color(0,0,0,255))` overwrites that same shared scalar for the backbuffer's
own neutral background. This dependency on Vulkan's single-shared-clear-colour architecture
(rather than a hypothetical per-RT remembered clear value) is called out explicitly and matches
this audit's own independent reading of `Clear()`'s implementation (a single `clearR_`/`G_`/`B_`/
`A_` tuple, no per-RT storage) — an accurate, non-speculative architectural claim.

### Robustness
Running the exact same `ClearOnlyThenSample` helper twice with different colours (green, then
blue) is a real technique for ruling out "the test happens to pass because of some fixed/stale
value coincidentally matching," which a single-colour version of this test would not rule out.
`matches()`'s `±8` per-channel tolerance (line 74-80) is appropriately tight for solid, unblended
fills.

### C++ correctness
`RenderTargetUsage::DiscardContents` (line 87) is the correct choice for a target that is fully
overwritten every use and never needs its previous contents preserved — matches FNA's
`RenderTargetUsage.DiscardContents` semantics and is the same default FNA's own 5-arg
`RenderTargetCube`/`RenderTarget2D` constructors forward to.

### Testing
Two checks (green, blue), `pass_`/`fail_` counters, `getResult()` returns nonzero on any failure —
consistent with the shard's established idiom.

### Cross-file consistency
Explicitly documents its relationship to `easygl_rt_roundtrip_test.cpp` (the EasyGL original this
ports from) and explains *why* it deliberately diverges from that file's "read while bound"
methodology: this test's own comment states `GetBackBufferData`/`ReadBackbuffer` on Vulkan always
reads the swapchain image, never a currently-bound RT (unlike EasyGL, which tracks
`currentRtHeight_` and redirects) — framed as an investigated, not assumed, difference. This audit
did not independently re-verify the EasyGL-side claim (out of scope for this batch), but the
Vulkan-side half of the claim (always reads swapchain) is consistent with this file's own Phase-2
"blit RT via SpriteBatch then read backbuffer" methodology being the only one used throughout this
entire Vulkan shard (`vulkan_rt2d_test.cpp`, `vulkan_rtcube_test.cpp`, etc. all sample the same
way, never "while still bound").

## Detailed Findings

None — no CRITICAL/HIGH/MEDIUM findings. This is a well-constructed regression test whose own
narrative withstood independent verification against the current source and git history.

## Missing or Weak Tests

None specific to this file. A third colour or a deliberately-tiny 1×1 RT edge case could add
marginal robustness, but the two-colour repeat already achieves this test's core anti-hardcoding
goal.

## Positive Findings

- The header comment's claimed root cause, fix mechanism, and current-state accuracy were all
  independently re-derived from the current `VulkanGraphicsBackend.cpp` line-by-line rather than
  taken on faith — every specific claim (global scalar, `usedRTs` registration gap,
  `VK_IMAGE_LAYOUT_UNDEFINED` consequence) checks out.
- The two-colour repeat is a genuine, non-decorative defense against a test that could otherwise
  pass by coincidence.
- Explicitly reasons about *why* it deliberately diverges from its EasyGL sibling's methodology,
  rather than silently doing something different — good engineering transparency.

## Final Assessment

The strongest file audited in this batch. Its own extensive header narrative is not just
plausible — it is accurate, verified against the actual current Vulkan backend source and commit
history, and the test itself is methodologically sound (two independent colours, forced frame
boundary ordering, neutral backdrop). No changes recommended.
