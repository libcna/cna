# Audit: examples/vulkan_scissor_test.cpp

## Metadata

- Source file: `examples/vulkan_scissor_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `RasterizerState.ScissorTestEnable` +
  `GraphicsDevice.ScissorRectangle` interaction, Vulkan backend
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_scissor …)` / `cna_register_backend_test(NAME
  Vulkan_ScissorTest …)`, `cmake/Tests/VulkanTests.cmake:444-447`).
- XNA/FNA relevance: direct — `RasterizerState.ScissorTestEnable`, `GraphicsDevice.
  ScissorRectangle`.
- FNA reference: `Graphics/GraphicsDevice.cs` (`ScissorRectangle` property, line 151; default set
  to `Viewport.Bounds` at device creation, line 470 — i.e. scissor rect always starts as the full
  viewport even before any explicit set).
- Related production code: `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp`
  (`ApplyRasterizerState()` line 7950, `SetScissorRect()` line 7964, `RecordCommandBuffer()`'s
  scissor consumption at lines 6792-6796), `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
  (`setRasterizerStateProperty`/`setScissorRectangleProperty`, lines 1715-1733).

## Purpose

Two-frame test (`frame_` state machine driven by successive `Draw()` calls) proving
`ScissorTestEnable` + `ScissorRectangle` actually clip 3D draws on Vulkan. Frame 0: no scissor,
full-screen red quad, both a top-left and bottom-right sample point must read red. Frame 1: scissor
enabled to the top-left 32×32 quadrant of a 64×64 backbuffer, same full-screen red quad; top-left
sample must stay red (inside), bottom-right sample must revert to the green clear colour (outside).
Both frames retry up to 20 times to work around an documented intermittent AMD/RADV driver flake
that occasionally returns the clear colour on the very first `GetBackBufferData` call.

## Executive Verdict

**Healthy** — the scissor mechanism this test exercises is real (traced end-to-end from
`RasterizerState`/`ScissorRectangle` setters through to `vkCmdSetScissor`), and the test's own
sample-point geometry (16,16 and 48,48 against a 64×64 backbuffer with a 32×32 top-left scissor) is
correct. One MEDIUM-severity scope observation: the test only exercises scissor against the
**backbuffer** pass; this backend's scissor state is hardcoded to full-size for render-target
passes (see F1), a gap this specific test cannot surface since it never targets a render target.

## Checklist Results

### API / XNA / FNA parity
`RasterizerState::setScissorTestEnableProperty(true)` / `GraphicsDevice::
setScissorRectangleProperty(Rectangle)` match FNA's `RasterizerState.ScissorTestEnable` and
`GraphicsDevice.ScissorRectangle` naming (mapped through this project's established
get/set-Property convention). Confirmed FNA's own default: `ScissorRectangle` starts equal to the
viewport bounds (`GraphicsDevice.cs:470`), consistent with this test's Frame 0 "no scissor set,
full screen is red" baseline (CNA's own default, not directly inspected here since Frame 0 never
touches `ScissorRectangle` at all, relying instead on `ScissorTestEnable=false` — the default
`RasterizerState()` used at line 128 — to bypass the rect entirely regardless of its value).

### Behavioral correctness
Traced the full plumbing: `GraphicsDevice::setRasterizerStateProperty()` (line 1715) forwards
`ScissorTestEnable` to `backend_->ApplyRasterizerState(...)`, which stores it as `scissorEnabled_`
(`VulkanGraphicsBackend.cpp:7957`); `GraphicsDevice::setScissorRectangleProperty()` (line 1728)
forwards the rectangle to `backend_->SetScissorRect(x,y,w,h)`, storing `scissorX_/Y_/W_/H_`
independently. Both are read together only at `RecordCommandBuffer` time
(`VulkanGraphicsBackend.cpp:6792-6796`): `if (scissorEnabled_ && scissorW_>0 && scissorH_>0) sc =
{...}` else full swapchain extent. This confirms: (a) the two setters can be called in either
order without correctness risk (both are pure stored state, combined only at record time, matching
this test's actual call order — `setScissorRectangleProperty` once before the loop,
`setRasterizerStateProperty` every iteration inside it); (b) `ScissorTestEnable=false` genuinely
bypasses the rectangle entirely (full swapchain extent used), matching FNA/XNA semantics where
`ScissorRectangle` has no effect unless `ScissorTestEnable` is set.

### Logic
Sample-point arithmetic verified: `kSize=64`; `tlX,tlY = kSize/4,kSize/4 = (16,16)`;
`brX,brY = kSize*3/4,kSize*3/4 = (48,48)`; Frame 1's scissor rect is
`Rectangle(0,0,kSize/2,kSize/2) = (0,0,32,32)`. `(16,16)` is strictly inside `[0,32)×[0,32)`;
`(48,48)` is strictly outside it (48 ≥ 32 on both axes) — the two sample points are correctly
positioned to land unambiguously on opposite sides of the scissor boundary, with comfortable
margin (16px) rather than sitting near an edge where off-by-one clipping errors could produce a
false pass.

### Robustness
The documented blank-frame retry loop (`for (int i=0;i<20;++i) { … if (!isBlack(tl) ||
!isBlack(br)) break; }`) is a reasonable, narrowly-scoped workaround for a named driver flake
(intermittent AMD/RADV clear-colour-only readback) rather than a blanket retry-until-pass loop —
it only accepts a frame once it sees *some* non-background content, not once it sees the expected
answer, so it cannot paper over a genuine scissor-clipping regression (a wrongly-scissored frame
would still be non-black and would still exit the retry loop, just with the wrong colour at one of
the two sample points, correctly triggering the `check()` failure).

### Testing
4 total assertions across both frames (`isRed`/`isRed` for Frame 0, `isRed`/`isGreen` for Frame 1),
each with generous ±60/8-bit-channel thresholds appropriate for a solid-fill scissor test.

## Detailed Findings

### F1 — This backend's scissor rectangle only applies to the backbuffer pass; render-target passes are hardcoded to full-target-size scissor, a gap this test cannot detect (out of its own scope)

- Severity: MEDIUM
- Confidence: HIGH (directly read both code paths in `RecordCommandBuffer()`)
- Category: test-coverage / architecture
- Location/symbol: `VulkanGraphicsBackend.cpp:6741-6743` (Phase-1 RT-pass loop: `VkRect2D rtSc{
  {0,0}, {rtW, rtH} }; vkCmdSetScissor(cb, 0, 1, &rtSc);` — always full RT extent, never consults
  `scissorEnabled_`/`scissorX_`/`scissorY_`/`scissorW_`/`scissorH_`) vs.
  `VulkanGraphicsBackend.cpp:6792-6796` (Phase-2 backbuffer pass: genuinely conditional on
  `scissorEnabled_`).
- Evidence: direct code comparison — the RT-pass loop's scissor rect is built from
  `rt->GetWidth()`/`GetHeight()` only, with no reference to any of the scissor state fields the
  backbuffer path reads; `SetViewport()`'s own comment (line 7975-7979) independently confirms this
  is a known, deliberate architectural limitation ("Only the backbuffer pass reads this state — RT
  passes stay hardcoded to each RT's own full size... since the deferred, potentially-multi-RT-
  per-frame recording model cannot recover 'what Viewport was active when each RT's draws were
  issued' from a single frame-global stored value"), and the same reasoning applies identically to
  scissor (stored the same way, consumed the same way).
- Why it matters: an XNA/FNA game that sets `ScissorRectangle`+`ScissorTestEnable` while a
  `RenderTarget2D`/`RenderTargetCube` is bound (a legal, real-world pattern — e.g. clipped UI
  rendered into an off-screen composite target) would silently get an unclipped, full-target draw
  on Vulkan, diverging from FNA's real per-draw scissor semantics. This is a real behavioral gap in
  the production backend, not a flaw in this test file — but it does mean this specific test
  (which only ever targets the backbuffer, never an RT) provides **zero** coverage of that gap,
  despite being this shard's only dedicated scissor test.
- FNA/XNA comparison: FNA's `ScissorRectangle` has no such carve-out — it applies uniformly
  regardless of which render target is currently bound.
- Related files: `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp` (production gap, not
  this test file).
- Suggested future action (not implemented by this audit): either extend this file (or add a
  sibling `vulkan_scissor_rendertarget_test.cpp`) that sets a `RenderTarget2D`, enables scissor, and
  asserts the RT's own sampled content is clipped — which would either confirm the gap needs fixing
  in `VulkanGraphicsBackend.cpp`, or (if the architecture is intentionally backbuffer-only for now)
  document that as an explicit, tested limitation rather than an untested one.

## Missing or Weak Tests

See F1 — RT-target scissor clipping is untested anywhere in this shard as far as this file's scope
extends.

## Positive Findings

- The scissor plumbing (`RasterizerState`→backend `ApplyRasterizerState`/`SetScissorRect`→
  `RecordCommandBuffer`'s conditional `vkCmdSetScissor`) was traced end-to-end and found to work
  exactly as the test assumes, for the backbuffer case it actually exercises.
- Sample-point placement (16,16 / 48,48 against a 32×32 scissor boundary on a 64×64 backbuffer) has
  a comfortable 16px margin on both sides of the boundary, avoiding edge-precision false
  positives/negatives.
- The blank-frame retry loop is narrowly scoped to a named, specific driver flake and cannot mask a
  genuine scissor regression, since it only skips frames with zero drawn content, not frames with
  wrong content.

## Final Assessment

A correct, well-targeted test of the one scenario it covers (backbuffer scissor clipping). The
audit's main contribution here is F1: an independently-confirmed, real architectural gap
(render-target passes ignore `ScissorRectangle` entirely) that this test's scope cannot surface —
worth flagging for whoever owns Vulkan render-target/scissor interaction, since it is a genuine
XNA-behavior divergence, not merely a test-coverage nicety.
