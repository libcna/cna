# Audit: examples/vulkan_basiceffect_textured_msaa_test.cpp

## Metadata

- Source file: `examples/vulkan_basiceffect_textured_msaa_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `BasicEffect` textured pixel test under forced backbuffer MSAA
- File type: standalone `Game`-subclass executable, CTest-registered integration test
- XNA/FNA relevance: direct — `BasicEffect.TextureEnabled`/`Texture`/`DiffuseColor`, plus the NOXNA
  `GraphicsDevice::RecreateBackendForMultiSampleCount()` test hook (not an XNA API — MSAA is normally
  configured through `GraphicsDeviceManager.PreferMultiSampling`, which this file's own header comment
  says never actually reaches the Vulkan backend, Task 902).
- FNA reference: none needed beyond `BasicEffect.cs`'s texture×diffuse formula — this file is really a
  Vulkan-backend render-pass/pipeline-compatibility regression test, not an XNA behavior test.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp`
  (`FillGpuDrawParams()`), `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp`
  (`GetOrCreatePipelineFogTex3D()` lines 4914-5046, `PickRTPipelineRenderPass()` lines 2019-2028),
  `src/CNA/Internal/Backends/Vulkan/shaders/textured3d.frag.glsl`.

## Purpose

Single-check pixel test (Task 904) that forces real backbuffer MSAA=8 via the NOXNA hook
`RecreateBackendForMultiSampleCount(8)`, then draws a textured, unlit `BasicEffect` quad and reads back
the centre pixel. The file's own header comment documents the bug this test was written to catch:
`GetOrCreatePipelineFogTex3D` (the stride-20/24 "textured3d"/"colored_textured3d" pipeline bundle
BasicEffect+Texture2D dispatches to) allegedly computed `ms.rasterizationSamples` correctly for MSAA
but still unconditionally set `pci.renderPass = renderPass_` (the 1-sample render pass), a genuine
sample-count/render-pass mismatch that every other Vulkan 3D pipeline avoided.

## Executive Verdict

**Healthy** — the exact bug the header comment describes was independently confirmed fixed in the
current code (Task 904, further generalized by Task 911's `PickRTPipelineRenderPass()`), and the
git commit history validates the described bug genuinely existed and was fixed in that order.

## Checklist Results

### API / XNA / FNA parity
`BasicEffect.TextureEnabled`/`Texture`/`DiffuseColor` usage (lines 106-108) is standard and matches
`BasicEffect.cpp`. `RecreateBackendForMultiSampleCount` (`GraphicsDevice.hpp:804`) is correctly
`NOXNA`-tagged — it is not part of the XNA API surface, only a CNA test hook.

### Behavioral correctness
Re-derived the expected pixel value: `BasicEffect::FillGpuDrawParams()` with `lightingEnabled_=false`
(default) forwards `(diffuseColor_ + emissiveColor_) * alpha_` (`BasicEffect.cpp:71-75`); with
`emissiveColor_` and `alpha_` both at their defaults (`Vector3::Zero`, `1.0f`), this is just
`kDiffuse=(0.8,0.4,0.6)`. `textured3d.frag.glsl` computes `outColor = tex * fragTint` where
`fragTint = pc.diffuseColor` (`textured3d.vert.glsl:34`). `kTexColor(200,100,50)` × `(0.8,0.4,0.6)` =
exactly `(160, 40, 30)` with no rounding ambiguity (200×0.8=160, 100×0.4=40, 50×0.6=30) — matches
`kExpected` exactly, a well-chosen test value.

### Logic — the specific bug being regression-tested
Read `GetOrCreatePipelineFogTex3D` end to end (`VulkanGraphicsBackend.cpp:4914-5046`):
`ms.rasterizationSamples = (msaa && colorAttachmentCount <= 1) ? sampleCount_ : VK_SAMPLE_COUNT_1_BIT;`
(line 4988) and, critically, `pci.renderPass = PickRTPipelineRenderPass(colorAttachmentCount, msaa,
targetDepthFmt);` (line 5034) — **not** a hardcoded `renderPass_`. `PickRTPipelineRenderPass()`
(lines 2019-2028) returns `(msaa && renderPassMsaa_) ? renderPassMsaa_ : renderPass_` when the
target's depth format matches the device's own (the common case, which this test hits). This
confirms the render-pass/sample-count mismatch the header describes is fixed in the *currently
checked-out* code, not merely asserted to be fixed.
- `git log` cross-check: `fa815881 fix(Task 904): Vulkan GetOrCreatePipelineFogTex3D missing msaa
  render-pass check` (2026-07-08) exists, and a later commit `8b41cbab fix(Task 911): give Vulkan
  render targets true per-instance DepthStencilFormat fidelity` (2026-07-09) is what introduced the
  more general `PickRTPipelineRenderPass()` seen today. The header comment's own narrative ("found
  while implementing Task 878/879, tracked as this task") is corroborated, not stale — unlike a
  sibling finding in this shard (see `vulkan_dualtextureeffect_alpha_test.cpp`'s audit report) where
  a similar "known bug" comment was *not* kept in sync with a later fix.
- Confirmed the MSAA→backbuffer resolve path that this test's readback depends on:
  `CreateFramebuffers()` (`VulkanGraphicsBackend.cpp:2030-2055`) binds `msaaColorView_` as attachment
  0 and the swapchain image view as the resolve attachment (attachment 1) when `msaa` is true, and the
  render-pass-creation code marks the resolve attachment `storeOp=STORE`,
  `finalLayout=PRESENT_SRC_KHR` (lines ~2799-2807, ~2858). `GetBackBufferData` therefore reads the
  already-resolved, non-MSAA swapchain image — architecturally correct for this test's single-pixel
  readback.
- `PickSampleCount()` (line 1044) clamps the requested `8` to whatever
  `VkPhysicalDeviceLimits::framebufferColorSampleCounts` actually supports on the running GPU, so the
  test does not strictly guarantee 8x MSAA ran — only "some MSAA level the hardware supports" — but
  since the test only checks colour correctness (not AA edge quality), this doesn't weaken the check
  it's actually making.

### C++ correctness
No new mechanisms in this file beyond the shared pixel-test-family pattern (`closeTo`/`matches` with a
tolerance of 8, `readCenter()` via `GetBackBufferData`); nothing file-specific to flag.

### Testing
This test verifies pipeline/render-pass *compatibility* under MSAA (the thing it says it verifies), not
MSAA edge-antialiasing *quality* — a fully-covering quad's centre pixel is unaffected by multisample
resolve either way, so a genuine AA-quality regression (e.g. wrong resolve averaging) would not be
caught here. That is outside this test's stated scope (Task 904 was specifically about the render-pass
selection bug), so this is a scope observation, not a defect.

## Detailed Findings

### F1 — Retry-until-non-blank loop's magic number (20) is undocumented in this file specifically
- Severity: LOW
- Confidence: MEDIUM
- Category: maintainability
- Location/symbol: `for (int i = 0; i < 20; ++i)` (line 119)
- Evidence: this file's loop comment is just `// skip blank/black frames`, whereas the sibling
  `vulkan_depth_bias_test.cpp` in this same shard documents the specific AMD RADV driver flake this
  retry works around in detail (its own header comment, lines 143-148).
- Why it matters: a future maintainer reading only this file has no way to know whether 20 is a
  calibrated value or an arbitrary guess, or whether the same flake this masks could also mask a real
  MSAA-specific timing bug (e.g. a resolve race) rather than the driver's plain blank-frame flake.
- Suggested action (not implemented by this audit): fold in a one-line pointer to the depth-bias test's
  fuller explanation, or centralize the retry rationale in one place all pixel tests in this family
  could reference.

## Cross-File Observations

- Directly ports `vulkan_basiceffect_texture_enabled_test.cpp` (Task 366) with only the MSAA-forcing
  hook added, per its own header comment — consistent with the rest of this shard's naming/derivation
  conventions.
- This is a good counter-example to the stale-comment pattern found elsewhere in this shard (see
  `vulkan_dualtextureeffect_alpha_test.cpp`): its "known bug, now fixed" narrative was independently
  re-verified against current code and git history and found to still be accurate.

## Missing or Weak Tests

None beyond the AA-quality-vs-compatibility scope note above (not a gap in this file's own stated
purpose).

## Positive Findings

- The single numeric check is well-chosen: `200×0.8=160`, `100×0.4=40`, `50×0.6=30` are exact integers,
  so this test cannot silently pass via floating-point/rounding coincidence the way a less carefully
  chosen constant could.
- Cross-referencing `PickRTPipelineRenderPass()` and the MSAA framebuffer/render-pass creation code
  confirms the specific defect this test targets is genuinely fixed in the current tree, not just
  claimed fixed by a comment.

## Final Assessment

A narrow, well-targeted regression test whose own historical bug narrative was independently confirmed
accurate against the current codebase and git log. No correctness issues found; only a minor
maintainability note about the undocumented retry-loop constant.
