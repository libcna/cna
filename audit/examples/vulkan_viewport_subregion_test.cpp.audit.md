# Audit: examples/vulkan_viewport_subregion_test.cpp

## Metadata

- Source file: `examples/vulkan_viewport_subregion_test.cpp` (205 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `GraphicsDevice.Viewport` GPU-wiring (split-screen sub-region)
  test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_viewport_subregion …)` /
  `cna_register_backend_test(NAME Vulkan_Viewport_Subregion …)`, `cmake/Tests/VulkanTests.cmake:613-615`).
- XNA/FNA relevance: direct — `GraphicsDevice.Viewport` get/set and its effect on rendered output
  (`Viewport.X`/`Y`/`Width`/`Height`).
- Related production code: `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp` —
  `VulkanGraphicsBackend::SetViewport()` (lines 7972-7988, storage-only) and its consumption inside
  `RecordCommandBuffer()`'s backbuffer pass (lines 6775-6790, the `vkCmdSetViewport` call).
- git corroboration: `98e1c32d`/`86226bc3` "fix(Task 880): wire GraphicsDevice.Viewport to a real GPU
  viewport on all 3 backends" (authored 2026-07-07 14:03), matching this file's own header attribution.

## Purpose

Proves `GraphicsDevice.Viewport` has genuine GPU effect on the Vulkan backend (previously a complete no-op:
`RecordCommandBuffer` always hardcoded `vkCmdSetViewport` to the full swapchain extent). Frame 0 sets a
left-half sub-region `Viewport` and draws a full-NDC-range (`-1..1` on both axes) red quad, expecting the
quad to fill exactly that sub-region (left-half sample red, right-half sample still black). Frame 1 restores
the full `Viewport`, draws a green full-NDC quad expecting both halves green, then separately round-trips an
arbitrary `Viewport(10,20,300,200)` through the getter/setter.

## Executive Verdict

**Healthy** — the single strongest file in this batch on process-robustness (it is the only one of the six
that already implements the blank-frame retry loop the other five lack or partially lack), and its central
geometric claim (a full-NDC quad exactly fills whatever `Viewport` rectangle is bound, with no scissor
dependency) was independently verified as sound GPU behavior, not merely assumed. One real, if narrow, scope
gap: the test (and the production fix it exercises) is explicitly backbuffer-only; render-target Viewport
support remains an acknowledged, unfixed gap in the same production code the test's own header does not
call out (see F1).

## Checklist Results

### API / XNA / FNA parity — PASS
`dev.setViewportProperty(Viewport(0, 0, leftHalfW, H))` / `dev.getViewportProperty()` (lines 137, 118, 154,
173-174) match FNA's `GraphicsDevice.Viewport` property surface via this project's `getXProperty`/
`setXProperty` convention. `Viewport(x, y, w, h)` 4-arg constructor and `getXProperty`/`getYProperty`/
`getWidthProperty`/`getHeightProperty` (lines 175-179) are consistent with the real `Viewport` struct shape.

### Behavioral correctness — PASS (independently verified)
- Traced `SetViewport()` (lines 7972-7988): storage-only (`viewportX_`/`Y_`/`W_`/`H_`/`viewportSet_`), with
  an explicit, honest comment that "RT passes stay hardcoded to each RT's own full size" — i.e. the fix is
  scoped to the backbuffer only (see F1).
- Traced the backbuffer pass in `RecordCommandBuffer()` (lines 6775-6790): `if (viewportSet_ &&
  viewportW_ > 0 && viewportH_ > 0)` binds the custom sub-region via `vkCmdSetViewport`; otherwise falls
  back to the full `swapchainExtent_` — directly confirms the Task 880 fix and matches this test's premise.
- Verified the geometric claim independently: a full-NDC quad (`(-1,1)..(1,-1)`, `drawQuad()` lines 91-102)
  is mapped by the Vulkan viewport transform linearly from clip-space `[-1,1]` to `[vp.x, vp.x+vp.width]` /
  `[vp.y, vp.y+vp.height]` — so the quad's rasterized footprint is bounded to *exactly* the bound viewport
  rectangle regardless of the rectangle's position/size, with **no dependency on a separately-set scissor
  rectangle** (Vulkan's scissor and viewport are independent state; this quad's own geometry, not a scissor
  clip, is what confines it to the left half). This confirms the test's core premise is real GPU behavior,
  not an assumption that happens to work by coincidence with an unrelated scissor default.
- Frame 0's `check(isRed(left), ...)`/`check(isBlack(right), ...)` (lines 145-146) and Frame 1's
  `check(isGreen(left), ...)`/`check(isGreen(right), ...)` (lines 169-170) correctly isolate "did the custom
  viewport confine drawing to the left half" from "does restoring the full viewport un-confine it" as two
  independently-falsifiable hypotheses — a bug that always drew everywhere, or a bug that always confined to
  the last-set sub-region even after restoring the full viewport, would each be caught by a different one of
  these four checks.
- `RasterizerState::CullNone` is set (lines 136, 161) for both frames; `drawQuad()`'s
  `(-1,1),(1,1),(-1,-1) / (-1,-1),(1,1),(1,-1)` winding is the same "Task 896" back-facing-under-default-
  culling situation seen elsewhere in this batch — handled consistently here too.

### Logic — PASS
`isRed`/`isGreen`/`isBlack` (lines 53-64) use non-overlapping threshold bands (`>=200` for the dominant
channel, `<30`/`<=60` for the others) that cannot simultaneously classify a single pixel as two different
colors, avoiding ambiguous readback classification.

### Robustness — PASS (best in this batch)
Both frames wrap their `Clear`+draw+readback sequence in an explicit `for (int i = 0; i < 20; ++i) { ...; if
(!isBlack(left) || !isBlack(right)) break; }` retry loop (lines 132-144, 157-168) with a header comment
explicitly citing "the intermittent AMD/RADV driver flake that returns the clear colour on the first
`GetBackBufferData` call after a swapchain present" — this is exactly the mitigation this audit found
missing from the other five files in this batch (`vulkan_spritebatch_multi_begin_end_test.cpp`,
`vulkan_texture_address_mode_test.cpp`, `vulkan_texture_mip_filter_effect_test.cpp`,
`vulkan_texture_srgb_test.cpp`, and — chronologically excusably — `vulkan_vertex_format_test.cpp`).
The retry-break condition (`!isBlack(left) || !isBlack(right)`) is a reasonable heuristic: it exits as soon
as *either* sample differs from the clear color, which correctly handles both the "bug present" case
(right also turns non-black immediately, breaking on iteration 1, then failing the subsequent `check()` for
the right reason) and the "no bug, no flake" case (left turns red/green immediately) without needing the
loop to itself pre-judge correctness — the actual pass/fail determination happens afterward via `check()`,
not via the loop's break condition.

### Testing — PASS
Four genuine pixel-color assertions plus a getter/setter round-trip check
(`Viewport(10,20,300,200)`/`getXProperty()`/etc., lines 172-179), covering both the GPU-wiring behavior and
the plain property-storage contract in one file.

### Cross-file consistency — PASS with one noted gap
Structurally mirrors `vulkan_scissor_test.cpp`'s frame-state-machine design (explicitly acknowledged in this
file's own header) and shares that file's retry-loop robustness pattern — the two together form a
consistent pair. See F1 for the one place production code (and, by extension, this test's scope) stops
short of full XNA `Viewport` parity.

## Detailed Findings

### F1 — Render-target `Viewport` support is a known, production-code-acknowledged gap that this file's own header does not mention

- Severity: LOW (accurately scoped test; the gap is in production code and is already honestly documented
  there — this is a documentation/completeness observation about the test's header, not a defect in the
  test's actual assertions)
- Confidence: HIGH
- Category: test-coverage / documentation-completeness
- Location/symbol: `VulkanGraphicsBackend::SetViewport()` (lines 7972-7988): "Only the backbuffer pass reads
  this state — RT passes stay hardcoded to each RT's own full size... since the deferred, potentially
  multi-RT-per-frame recording model cannot recover 'what Viewport was active when each RT's draws were
  issued' from a single frame-global stored value."
- Evidence: read `SetViewport()`'s own comment directly; confirmed there is no equivalent
  `viewportSet_`-style check anywhere in the render-target pass code paths inside `RecordCommandBuffer()`
  (only the backbuffer branch at lines 6775-6790 reads `viewportSet_`). This file's own header, by contrast,
  describes the fix simply as "GraphicsDevice.Viewport GPU wiring" without noting this backbuffer-only
  scoping — a reader of just this file's header (not the production source) could reasonably assume
  `Viewport` sub-regions now work uniformly for `RenderTarget2D` draws too, which is not the case.
- Why it matters: a real XNA game rendering to an off-screen `RenderTarget2D` with a custom sub-region
  `Viewport` (e.g. a render-to-texture minimap, or a deferred-rendering G-buffer pass using a partial
  viewport) would silently get the RT's full extent instead on this backend — a real behavior gap, just one
  this test does not claim to cover and is not asked to fix.
- FNA/XNA comparison: FNA's `GraphicsDevice.Viewport` applies uniformly to whatever render target is
  currently bound (backbuffer or not); this backend's real current behavior is backbuffer-only, a
  documented but non-obvious (from this test alone) parity gap.
- Related files: none beyond `VulkanGraphicsBackend.cpp` itself; no separate RT-viewport test exists in
  this batch to cross-reference.
- Suggested future action (not implemented by this audit): either add a one-line header note in this file
  acknowledging the backbuffer-only scope (so a reader doesn't need to separately discover
  `SetViewport()`'s own comment), or — as a production fix, out of this audit's scope — extend the
  per-RT-pass recording to also honor a per-RT-bound Viewport if the deferred-recording architecture can be
  made to track it.

## Cross-File Observations

- This file is the reference point this batch's other five files are compared against for the blank-frame
  retry-loop robustness gap (F1 in each of their reports) — it demonstrates the mitigation is
  straightforward to apply and was already established in the same test suite before most of those other
  files were authored.
- Shares the "Task 896" `RasterizerState::CullNone` winding workaround with
  `vulkan_texture_srgb_test.cpp`/`vulkan_texture_mip_filter_effect_test.cpp`/`vulkan_vertex_format_test.cpp`
  in this same batch.

## Missing or Weak Tests

- No render-target-bound `Viewport` sub-region test exists (see F1) — reasonable to treat as a separate,
  not-yet-written test given the production code's own explicit architectural limitation, rather than a gap
  in this file specifically.
- No test of an out-of-bounds or negative-origin `Viewport` (e.g. `Viewport(-10, -10, W, H)`) to check
  clamping/error behavior, though FNA itself does not strongly constrain this either.

## Positive Findings

- The best-designed test in this batch for process robustness: correctly anticipates and defends against a
  real, independently-corroborated (via `NEXT.md`'s "AMD Radeon 780M (RADV PHOENIX)" reference and
  `VulkanGraphicsBackend.cpp`'s own "common on first frame under Wayland/RADV" comment) hardware/driver
  flake that affects most of this batch's other files.
- The choice of a full-NDC-range quad (rather than, say, a quad sized to the window and relying on a
  scissor rectangle) is the geometrically correct way to prove viewport-driven confinement independent of
  scissor state, and this audit confirmed that choice is sound Vulkan-viewport-transform behavior rather
  than an assumption.
- Combining GPU-wiring proof (frames 0-1) with a plain getter/setter round-trip
  (`Viewport(10,20,300,200)`) in one file is an efficient, still-clearly-separated use of a single test
  executable for two related but distinct claims.

## Final Assessment

A rigorous, well-verified test of the real Task 880 Vulkan-viewport-wiring fix, and the strongest file in
this batch on test-infrastructure robustness. Its only shortcoming is an omission of context (not
correctness): it does not mention, even though production code openly acknowledges, that the fix it proves
is backbuffer-only and does not yet extend to render-target-bound `Viewport` sub-regions.
