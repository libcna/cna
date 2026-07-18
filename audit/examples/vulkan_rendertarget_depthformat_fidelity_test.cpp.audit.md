# Audit: examples/vulkan_rendertarget_depthformat_fidelity_test.cpp

## Metadata

- Source file: `examples/vulkan_rendertarget_depthformat_fidelity_test.cpp` (192 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — per-instance `DepthFormat` fidelity test.
- File type: standalone `Game`-subclass executable (`class VulkanRTDepthFormatFidelityTest`).
- XNA/FNA relevance: direct — `RenderTarget2D(device, w, h, mipMap, SurfaceFormat,
  DepthFormat, ...)`'s `DepthFormat` parameter (`DepthFormat::None`/`Depth24Stencil8`/`Depth16`).
- Related production code:
  `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp`,
  `VulkanRenderTargetBackend::VulkanRenderTargetBackend(int w, int h, int depthFormat, ...)` (line
  394 onward — only allocates `depthImage_` when a real format was requested), `GetOrCreateRTRenderPass`'s
  `targetDepthFmt`-keyed pipeline/render-pass cache.
- Task references: Task 911 (`git log`: `8b41cbab fix(Task 911): give Vulkan render targets true
  per-instance DepthStencilFormat fidelity`).

## Purpose

Proves that, post-Task-911, each `RenderTarget2D` genuinely gets its *own* requested
`DepthFormat`'s depth buffer (or none at all) rather than every RT silently sharing the Vulkan
backend's single device-wide `depthFormat_` regardless of what was actually requested at
construction time. Three RTs are constructed in one frame — `rt_none` (`DepthFormat::None`),
`rt_default` (`Depth24Stencil8`), `rt_depth16` (`Depth16`) — each receives a near Red quad (z=0.2)
then a far Lime quad (z=0.8) with depth testing enabled (`LessEqual`, XNA's real default
`DepthBufferFunction`): `rt_none` must show Lime (no real depth buffer, so the later draw always
wins regardless of z), while both `rt_default` and `rt_depth16` must show Red (a genuine depth test
correctly rejects the farther Lime quad) — proving a *non-default* depth format (`Depth16`) also
gets its own genuinely functioning depth buffer, not just the common default.

## Executive Verdict

**Healthy** — the per-instance depth-format allocation this test targets was independently traced
in the production constructor and confirmed to match the test's premise exactly: `rtNone_` gets no
`depthImage_` at all, while `rtDefault_`/`rtDepth16_` each get their own, format-correct depth image
— and the pre-Task-911 regression scenario the file's header comment describes (`rt_none` would
have incorrectly rendered RED) is a genuine, traceable differential, not a hypothetical.

## Checklist Results

### API / XNA / FNA parity — PASS
`RenderTarget2D(dev, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None)` /
`...Depth24Stencil8)` / `...Depth16)` (lines 111–116) use the standard XNA constructor overload
with only the `DepthFormat` argument varied — the exact member this task/test is about.
`dev.SetDepthTestEnabled(true)`/`SetDepthWriteEnabled(true)` (lines 122–123) are `NOXNA`
CNA-convenience wrappers over `DepthStencilState`, consistent with this shard's established
pattern (also used identically in `vulkan_occlusionquery_pixelcount_test.cpp` via the
`DepthStencilState` object form — a stylistic, not semantic, difference between the two files).

### Behavioral correctness — PASS
Traced `VulkanRenderTargetBackend`'s constructor (starting line 394): the depth image/view/barrier
block is only entered when the requested `depthFormat` maps to a real `VkFormat` (confirmed via the
constructor's own comment, "independent of the backbuffer's own depthFormat_," line 403) — for
`DepthFormat::None` this path is skipped entirely, so `rtNone_` genuinely has no depth attachment,
meaning its render pass has `pDepthStencilAttachment = nullptr` and depth testing cannot occur
regardless of the `DepthStencilState` bound at draw time — matching the test's premise that the
farther Lime quad always wins on `rt_none`. For `rtDefault_`/`rtDepth16_`, each gets its own
`depthImage_` sized/formatted per its own requested `DepthFormat`, and `GetOrCreateRTRenderPass`
caches render passes/pipelines keyed by `targetDepthFmt` (confirmed in the sibling
`vulkan_render_target_usage_test.cpp` audit's tracing of the same function) — so `Depth24Stencil8`
and `Depth16` genuinely get *different*, independently-cached render passes/pipelines, not a shared
one silently reusing whichever format was created first.

### Logic — PASS
The single `for (auto* rt : { rtNone_.get(), rtDefault_.get(), rtDepth16_.get() })` loop (line 140)
draws the identical near-red/far-lime pair into all three RTs with identical `DepthStencilState`
(the real default, since `dev.SetDepthTestEnabled(true)`/`SetDepthWriteEnabled(true)` are set once,
line 122–123, before the loop) — this is the correct design: only the RT's own depth-format
allocation differs between the three draws, isolating exactly the variable under test.

### C++ correctness — PASS
`rtNone_`/`rtDefault_`/`rtDepth16_` are `unique_ptr<RenderTarget2D>` members constructed once in
`Initialize()` (lines 111–116) and used for the whole test's single `Draw()` call — no lifetime
concerns.

### Robustness — PASS
`isRed()`/`isGreen()` (lines 88–95) both require the *other* two channels to be low (`<=60`) in
addition to the dominant channel being high (`>=200`) — correctly rejects an ambiguous
yellow/orange blend as neither red nor green, rather than a single-channel threshold that could
accidentally accept a wrong-but-similar hue.

### Testing — PASS
Testing both a *default* (`Depth24Stencil8`) and a *non-default* (`Depth16`) real format in the same
frame (rather than only the default) is exactly the right additional check per the file's own
stated rationale — a hypothetical implementation that special-cased only the single most-common
depth format (while still failing to generalize per-instance fidelity for any other real format)
would pass a `Depth24Stencil8`-only test but fail this file's `Depth16` check, and this audit
confirmed via `GetOrCreateRTRenderPass`'s format-keyed cache (traced in the sibling
`vulkan_render_target_usage_test.cpp` report) that the two real formats do in fact get independently
allocated render-pass/pipeline state, not a hardcoded single-format path.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — `RasterizerState::CullNone` justification comment cross-references a sibling file rather than restating the reasoning

- Severity: INFO
- Confidence: HIGH
- Category: maintainability
- Location/symbol: lines 125–126 — "Task 896 finding (see vulkan_render_target_usage_test.cpp):
  this quad winding is back-facing under CNA's real default RasterizerState -- needs CullNone."
- Evidence: this is simply an observation, not a defect — the cross-reference was independently
  followed and confirmed accurate (both files do use the identical NDC quad-winding convention and
  both correctly apply `CullNone`).
- Why it matters: not a correctness issue; noted only because the anti-boilerplate audit
  instructions call for checking such claims rather than assuming them — this one holds up.

## Cross-File Observations

- Shares its `drawFullScreenZ` helper's quad-winding convention and `CullNone` requirement with
  `vulkan_render_target_usage_test.cpp`'s `drawFullScreen`/`drawLeftHalf` — consistent,
  intentionally-reused test idiom across this shard.
- The pre-Task-911 regression scenario this file's header comment describes ("rt_none would have
  incorrectly rendered RED") is corroborated by the production constructor's own comment
  ("Task 911: only created when a real depth format was requested — DepthFormat::None correctly
  gets no depth attachment at all now," found identically worded in the `RenderTargetCube`
  constructor at line ~8688) — i.e. the same fix and the same regression narrative are documented
  consistently on both the 2D and cube-map render-target backends, not just asserted once
  unverifiably in this one test file.

## Missing or Weak Tests

- No check in this file exercises `DepthFormat::Depth24` (the third concrete XNA `DepthFormat`
  value, depth-only without stencil) — only `Depth24Stencil8` (the "default") and `Depth16` (a
  "distinct, non-default" format) are covered. This is a minor, low-priority gap: the test's own
  stated purpose is proving *per-instance* fidelity in principle (any two genuinely different
  requested formats coexisting correctly), which `Depth24Stencil8` + `Depth16` already
  demonstrates; adding `Depth24` would strengthen coverage marginally but is not required to prove
  the mechanism this file targets.

## Positive Findings

- The three-RT, single-frame design (rather than three separate test runs) is an efficient and
  more rigorous way to prove all three depth-format configurations coexist correctly
  *simultaneously* — a subtler bug (e.g. a render-pass/pipeline cache collision between two
  different depth formats sharing a key) would only be caught by exactly this kind of
  same-frame, multi-format test, not by three isolated single-format runs.
- The regression narrative in this file's header comment was independently corroborated against the
  actual production constructor logic and against an identically-worded comment on the sibling
  `RenderTargetCube` backend — a consistent, cross-checked claim rather than an isolated one.

## Final Assessment

A well-targeted, correctly-isolated fidelity test; the per-instance depth-format allocation logic it
exercises was independently traced and confirmed correct in the production Vulkan backend. No
issues found beyond a minor, non-blocking coverage suggestion (untested `DepthFormat::Depth24`).
