# Audit: examples/vulkan_render_target_usage_test.cpp

## Metadata

- Source file: `examples/vulkan_render_target_usage_test.cpp` (245 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `RenderTargetUsage::DiscardContents` vs
  `PreserveContents` fidelity test.
- File type: standalone `Game`-subclass executable (`class VulkanRTUsageTest`).
- XNA/FNA relevance: direct — `Microsoft::Xna::Framework::Graphics::RenderTargetUsage`
  (`DiscardContents`/`PreserveContents`) and the `RenderTarget2D` constructor overload that takes
  it.
- Related production code:
  `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp`,
  `VulkanGraphicsBackend::GetOrCreateRTRenderPass(VkFormat, bool discardContents)` (lines
  1733–1786): `colorAtt.loadOp = discardContents ? VK_ATTACHMENT_LOAD_OP_CLEAR :
  VK_ATTACHMENT_LOAD_OP_LOAD`.
- Task references: Task 178 (this file's own header), Task 896 (`RasterizerState::CullNone`
  workaround, cross-referenced from this file's own comment and corroborated by `git log`:
  `b6a00bc6 fix(Task 896): push GraphicsDevice's real default RasterizerState to all 3 backends`),
  Task 364/361 (`VertexColorEnabled` fix, this file's own comment lines 144–146).

## Purpose

Two-`Draw()`-frame test proving Vulkan's deferred render-pass model genuinely honours
`RenderTargetUsage` at the `VkAttachmentLoadOp` level, not just accepting-and-ignoring the
constructor parameter:
- **Frame 0**: fills `rt_discard` (constructed with `RenderTargetUsage::DiscardContents`) full-screen
  Red, and `rt_preserve` (constructed with `PreserveContents`) full-screen Green.
- **Frame 1**: re-binds each RT and draws only a **left-half** Blue quad (forcing a new render pass
  for each, since a bound-but-undrawn RT alone wouldn't necessarily trigger recording — see Task 875
  cross-reference below), then blits each RT to the backbuffer via `SpriteBatch` and samples the
  **right half** (untouched by the left-half draw): `rt_discard`'s right half must now read black
  (cleared, old Red content discarded), `rt_preserve`'s right half must still read green (old
  content preserved via `LOAD_OP_LOAD`).

## Executive Verdict

**Healthy** — the discard-vs-preserve differential is genuinely discriminating (a backend that
ignored `RenderTargetUsage` entirely and always cleared, or always loaded, would fail exactly one
of the two assertions), and the `VK_ATTACHMENT_LOAD_OP_CLEAR`/`_LOAD` selection in
`GetOrCreateRTRenderPass` was independently confirmed to be gated on the same `discardContents`
flag this test's constructor calls set.

## Checklist Results

### API / XNA / FNA parity — PASS
`RenderTarget2D(dev, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
RenderTargetUsage::DiscardContents)` / `...PreserveContents` (lines 120–125) uses the full 7-arg
XNA constructor overload including the `usage` parameter — the actual member under test — not a
shorter overload that would default it.

### Behavioral correctness — PASS
Traced `GetOrCreateRTRenderPass(depthFmt, discardContents)` directly: `colorAtt.loadOp =
discardContents ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD` (line 1746), with a
correctly-paired `initialLayout` (`UNDEFINED` for discard, `SHADER_READ_ONLY_OPTIMAL` for preserve
— matching the image's actual state after a previous RT pass's `finalLayout`, line 1753) — this is
exactly the mechanism the test's own header comment describes, and it is genuinely keyed off the
per-instance `RenderTargetUsage` the RT was constructed with (confirmed by checking the cache key,
`auto& cache = discardContents ? rtRenderPassByDepthFmt_ : rtRenderPassLoadByDepthFmt_;`, line
1735 — two independent render-pass caches, not a single shared one with a runtime branch that could
silently collapse to one behaviour).

### Logic — PASS
The two-frame structure is real (unlike the occlusion-query test's single-`Draw()`-call polling
loop in a sibling file): `frame_` is a member incremented once per real `Game::Draw()` invocation,
and `EndDraw()`'s automatic `Present()` (implied by the Game loop, not shown in this file) genuinely
flushes frame 0's fills before frame 1 re-binds and reads. `readBB()`'s own comment ("GetBackBufferData
triggers Present() if there are pending draws") is consistent with the same `ReadBackbuffer()`
flush-on-demand behaviour this audit traced for the sibling occlusion-query test in this shard.

### C++ correctness — PASS
`BasicEffect fx(dev)` is kept alive for the whole `Draw()` call body via a stack local (line 138,
with an explicit comment explaining why: "Keep the BasicEffect alive for the entire frame so
currentEffect_ stays valid") — a correct, deliberate lifetime choice rather than an accidental one.

### Robustness — PASS
`check()` (lines 91–95) accumulates `pass_`/`fail_` and sets `result_=1` on any failure without
early-exiting — all 3 checks (`DiscardContents: not red`, `DiscardContents: is black`,
`PreserveContents: is green`) always run and are always reported, so a single failing assertion
doesn't hide information about the others.

### Testing — PASS
The `!colourMatch(pxD, Color::Red)` check (line 193) plus the separate near-black check (line 195)
for `DiscardContents` are usefully redundant in a good way: the first alone would also pass if the
discarded RT read back as, say, stale garbage memory (still "not red"); the second specifically
pins the actual expected clear colour (black), closing that gap.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — `PreserveContents` right-half check has a wider effective RGB decision band than `DiscardContents`

- Severity: LOW
- Confidence: MEDIUM
- Category: test-coverage / robustness
- Location/symbol: `check(pxP.getRProperty() < 30 && pxP.getGProperty() > 100 && pxP.getBProperty() < 30, ...)`
  (line 220)
- Evidence: the `DiscardContents` black check uses a tight `<30` band on all three channels (a
  legitimately narrow "is this actually near-zero" test), but the `PreserveContents` green check
  only requires `G > 100` with no upper bound — so a badly-blended or gamma-mismatched green (e.g.
  `R=25,G=254,B=25`, or even an accidentally-almost-white green-ish colour) would still pass. The
  file's own comment (line 215-216) anticipates a specific sRGB-encoded value (~188), which sits
  comfortably inside the current assertion, but the assertion itself doesn't pin that specific
  expected value the way the `DiscardContents` checks pin black.
- Why it matters: a regression that made `PreserveContents` load garbage or a wrong-but-still-green-
  dominant colour (e.g. a channel-swapped read, or an off-by-one on which mip/layer got loaded)
  could still satisfy `G>100 && R<30 && B<30` without being the *correct* preserved green — this is
  a real, if narrow, gap relative to the more precisely-pinned discard-side checks in the same file.
- FNA/XNA comparison: N/A — a test-precision observation, not an XNA/FNA behavior question.
- Suggested action (not implemented by this audit): tighten the check to also assert an upper bound
  on G (e.g. `G < 220`) or assert closeness to the specific expected sRGB value the comment already
  states, matching the tighter pattern used for the discard-side black check in the same file.

## Cross-File Observations

- This file's own comment (lines 133–135, 144–146) documents two *other* Vulkan bugs it depends on
  being fixed (Task 896's `RasterizerState::CullNone` default-winding issue, Task 364's
  `VertexColorEnabled` flag being ignored by the colored3D pipeline) — both independently
  corroborated by `git log` (`b6a00bc6 fix(Task 896)`, and Task 364/361 referenced consistently
  across this shard's other files, e.g. `vulkan_occlusionquery_pixelcount_test.cpp`'s own
  `CullNone` usage). This is a genuinely cross-checked, non-fabricated dependency chain, not a
  stale claim.
- Shares the exact same `drawFullScreen`/`drawLeftHalf` NDC-quad-construction pattern (and the same
  `RasterizerState::CullNone` requirement) as
  `vulkan_rendertarget_depthformat_fidelity_test.cpp`'s `drawFullScreenZ` — consistent, intentional
  reuse of an established test idiom across this shard, not independent reinvention with a risk of
  divergence.

## Missing or Weak Tests

- No check in this file exercises `RenderTargetUsage::PlatformContents` (the third XNA enum value,
  though FNA itself treats it identically to `DiscardContents` per XNA's own documented behaviour on
  most platforms) — a minor, low-priority coverage gap since `PlatformContents` has no distinct
  required behaviour to test against in FNA's own semantics.
- Depth-buffer interaction with `RenderTargetUsage` (e.g. does a `DiscardContents` RT with a real
  depth format also get `LOAD_OP_CLEAR` on its depth attachment, and does `PreserveContents` load or
  discard depth?) is exercised implicitly by `GetOrCreateRTRenderPass`'s own logic (confirmed: depth
  uses `DONT_CARE`/`UNDEFINED` even in the `PreserveContents` case, per this audit's reading of lines
  1759–1764, with an explicit comment justifying it: "the depth image starts in UNDEFINED and its
  previous content is never needed across RT passes") but this specific file uses
  `DepthFormat::None` for both RTs (lines 121, 124), so it does not itself verify that
  depth-attachment behaviour — likely intentionally out of scope for a colour-usage-focused test,
  but worth noting as an adjacent, currently-uncovered claim.

## Positive Findings

- The differential design (fill both RTs with different colours in frame 0, touch only the left
  half in frame 1, read only the right/untouched half) is a clean way to isolate "did the loadOp
  discard-vs-preserve the *previous* frame's content" from "did this frame's own draw work at all" —
  a genuinely well-thought-out test structure.
- The file's own comments about *why* `CullNone` and `VertexColorEnabled=true` are both necessary
  here (cross-referencing specific fixed Vulkan bugs by task number) were independently verified
  against `git log` rather than taken at face value, and check out.

## Final Assessment

A correctly-targeted, genuinely discriminating test of `RenderTargetUsage` fidelity on the Vulkan
backend; one minor test-precision gap (F1, LOW) on the green-preservation assertion's tolerance
band, not a functional defect in the code under test.
