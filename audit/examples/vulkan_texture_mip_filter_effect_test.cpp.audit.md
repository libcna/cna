# Audit: examples/vulkan_texture_mip_filter_effect_test.cpp

## Metadata

- Source file: `examples/vulkan_texture_mip_filter_effect_test.cpp` (164 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `DualTextureEffect` mipmap-filter (`TextureFilter`) selection test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_texture_mip_filter_effect …)` /
  `cna_register_backend_test(NAME Vulkan_TextureMipFilter_DualTextureEffect …)`,
  `cmake/Tests/VulkanTests.cmake:87-89`).
- XNA/FNA relevance: direct — `SamplerState.Filter` (`TextureFilter.LinearMipPoint`/`Point`/etc.), mip
  selection semantics for `DualTextureEffect`'s `Texture` parameter.
- Related production code: `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp` —
  `VulkanGraphicsBackend::ApplySamplerState()` (lines 2196-2260, `TextureFilter`→`VkFilter`/
  `VkSamplerMipmapMode` mapping), `VulkanTextureBackend` constructor (lines 120-146,
  `levelCount_`/`imgInfo.mipLevels` from `data.mipLevels`), `VulkanTextureBackend::SetData()`
  (per-level image transitions, lines ~340+).
- git corroboration: `4be5626c`/`da996046` "fix(Task 925): implement real Texture2D mip-level
  upload/allocation on Vulkan" (authored 2026-07-09 21:54), matching this file's own header attribution;
  the file's claim about the EasyGL sibling's Point-filter limitation is corroborated by that file's own
  audited behavior (this batch's sibling reports).

## Purpose

Builds a 128×128 mipmapped `Texture2D` where mip levels 0-2 are solid Red and levels 3-7 are solid Green,
then draws an 8×8-pixel on-screen quad sampling that texture (heavy minification, forcing the GPU to select
a high mip level) under two filters: `LinearMipPoint` and plain `Point`. Both are expected to sample a high
(Green) mip level — the file's header explicitly documents that, unlike its EasyGL sibling test, Vulkan's
`Point` filter was *always* mapped mip-aware (`VK_SAMPLER_MIPMAP_MODE_NEAREST`), and that the real
pre-Task-925 bug was that `VkImageCreateInfo::mipLevels`/`VkImageViewCreateInfo::subresourceRange.levelCount`
were hardcoded to 1, so there was no higher mip level for that already-correct sampler mapping to select.

## Executive Verdict

**Healthy** — every claim in the header comment was independently traced against current production code
and confirmed true (not merely restated), including the specific and easy-to-get-backwards claim that
`Point` was never actually "no mip selection" on this backend, only the image-side level count was the real
bug. Shares this batch's recurring blank-frame-retry robustness gap (F1).

## Checklist Results

### API / XNA / FNA parity — PASS
`MakeSampler()` (lines 48-55) builds a real XNA `SamplerState` with `Filter`/`AddressU`/`AddressV`
properties. `TextureFilter::LinearMipPoint` and `TextureFilter::Point` are real FNA enum values.
`Texture2D(device, 128, 128, /*mipMap=*/true, SurfaceFormat::Color)` (line 74) matches the
`Texture2D(GraphicsDevice, int, int, bool, SurfaceFormat)` mipmap-enabled constructor overload.
`mipTex_.SetData(level, nullptr, px.data(), 0, count)` (line 81) matches the per-level `SetData(int level,
Rectangle*, T[], int, int)` overload used to seed each of the 8 mip levels independently.

### Behavioral correctness — PASS (independently traced)
- Traced `ApplySamplerState()`'s filter `switch` (lines 2213-2226): case `3` (`LinearMipPoint`) sets
  `magF=minF=VK_FILTER_LINEAR`, `mipMode=VK_SAMPLER_MIPMAP_MODE_NEAREST` — correct per XNA's
  `<Min><Mag>Mip<MipFilter>` naming convention (Min/Mag both Linear via the base name, Mip explicitly
  Point/Nearest). Case `1` (`Point`) sets `magF=minF=VK_FILTER_NEAREST`, `mipMode=VK_SAMPLER_MIPMAP_MODE_NEAREST`
  — i.e., nearest-neighbor **mip level selection**, not "no mip selection" — confirming the header's claim
  that Vulkan's `Point` mapping was never the EasyGL-style "no `_MIPMAP_` suffix at all" limitation.
- Traced `ci.maxLod = VK_LOD_CLAMP_NONE;` (line 2248, with its own comment referencing Task 878) — without
  this, every sampler variant would clamp to mip level 0 regardless of the image's real level count,
  which would silently defeat this exact test; confirmed present.
- Traced `VulkanTextureBackend`'s constructor (`levelCount_(data.mipLevels > 0 ? data.mipLevels : 1)`,
  line 122; `imgInfo.mipLevels = static_cast<uint32_t>(levelCount_);`, line 146;
  `viewInfo.subresourceRange.levelCount = static_cast<uint32_t>(levelCount_);`, line 211) — confirms the
  image and its view now both request the real level count derived from the texture's own `mipMap` flag,
  not a hardcoded `1`, corroborating the Task 925 fix this file's header describes.
- `DualTextureEffect` combine semantics (`Texture` × `Texture2` × 2, both saturated to `[0,1]`): `whiteTex_`
  is pure white `(255,255,255,255)`, so the doubled product of a saturated Red/Green `mipTex_` sample with
  white remains clamped at the same pure Red/Green value — the `whiteTex_` second-texture slot does not
  perturb the pass/fail signal, which is the correct design choice for isolating mip selection alone.
- `RasterizerState::CullNone` (line 115) is required because the quad's `TL,BL,BR / TL,BR,TR` winding
  (`Vector3(ndcLeft, 1.0f, ...)` etc., lines 105-112) is back-facing under CNA's real default
  `RasterizerState` — this exact "Task 896" cull-mode caveat recurs verbatim across several files in this
  batch (`vulkan_texture_srgb_test.cpp`, `vulkan_vertex_format_test.cpp`) and was not re-derived from first
  principles here, but its consistent, explicitly-commented reuse across multiple independently-authored
  files in the same suite is itself corroborating evidence it is a real, previously-diagnosed property of
  the shared rasterizer defaults rather than a one-off guess.
- 8×8-pixel quad sampling a 128×128 texture with full `[0,1]` UV range across those 8 pixels ⇒ GPU mip LOD
  ≈ `log2(128/8) = 4`, comfortably inside the Green range (levels 3-7) for both filters — the test's chosen
  quad size is not a boundary case that could flip between Red/Green mip bands under minor LOD-bias
  rounding differences.

### Logic — PASS
`IsGreen()`/`IsRed()` (lines 57-58) use asymmetric thresholds (`>=200`/`<=40`) tolerant of GPU rounding
without being loose enough to blur Red vs. Green.

### Robustness — WEAK (see F1; shared with sibling files in this batch)
No retry loop around `device.GetBackBufferData(&reg, &c, 0, 1)` (line 120), despite this file being
authored (2026-07-09) well after the AMD/RADV blank-frame flake mitigation pattern was established
(`vulkan_scissor_test.cpp`, 2026-06-29).

### Testing — PASS for its own stated scope
Two genuine, GPU-driven mip-selection assertions (not "compiles and runs"); explicitly targets the
specific fix (image-level-count allocation) rather than re-testing the sampler-mapping table alone.

### Cross-file consistency — PASS
Correctly and explicitly distinguishes its own expectations from `easygl_texture_mip_filter_effect_test.cpp`'s
(that file's "Point" check asserts Point *never* selects a higher mip, a real EasyGL-specific limitation);
this file's header explains *why* the two backends' expected results differ rather than silently copying
one backend's assertions onto the other — a materially more careful adaptation than a naive backend port.

## Detailed Findings

### F1 — No blank-frame retry loop around `GetBackBufferData`

- Severity: MEDIUM
- Confidence: HIGH
- Category: test-robustness / flakiness-risk
- Location/symbol: `DrawTinyQuadAndSample()` line 120 (`device.GetBackBufferData(&reg, &c, 0, 1)`), called
  twice from `Draw()` (lines 137-138), neither wrapped in a retry loop.
- Evidence / why it matters / suggested action: identical to F1 in this batch's
  `vulkan_spritebatch_multi_begin_end_test.cpp.audit.md` and `vulkan_texture_address_mode_test.cpp.audit.md`
  — `VulkanGraphicsBackend::ReadBackbuffer()` (`VulkanGraphicsBackend.cpp:6982-7011`) explicitly zeroes its
  output and expects a caller retry on a documented "common on first frame under Wayland/RADV" swapchain
  out-of-date condition; this file's two readbacks have no such retry, so a spurious all-zero (neither Red
  nor Green — both `IsGreen`/`IsRed` would read false) result could fail the test for a reason unrelated to
  Task 925's actual mip-level-count fix.
- FNA/XNA comparison: N/A.
- Related files: same as the sibling findings above.

## Cross-File Observations

- Shares this batch's F1 gap with `vulkan_spritebatch_multi_begin_end_test.cpp`,
  `vulkan_texture_address_mode_test.cpp`, and `vulkan_texture_srgb_test.cpp`.
- The `RasterizerState::CullNone` "Task 896" workaround recurs identically in this file, in
  `vulkan_texture_srgb_test.cpp`, and in `vulkan_vertex_format_test.cpp` — consistent, not contradictory,
  across all three.
- The claim "Vulkan's `ApplySamplerState` has never shared [EasyGL's] limitation" is a comparative claim
  about a *different* backend's behavior; this audit did not re-verify EasyGL's own filter-mapping code in
  this batch (out of scope — `EasyGLGraphicsBackend.cpp` belongs to a different shard), so that specific
  half of the comparison is taken as previously-established context rather than independently re-confirmed
  here.

## Missing or Weak Tests

- Only `LinearMipPoint` and `Point` are exercised; the other seven `TextureFilter` values (e.g.
  `PointMipLinear`, `MinLinearMagPointMipLinear`) are not covered by this file (may be covered elsewhere in
  the suite — not verified as part of this batch).
- See F1.

## Positive Findings

- This is one of the stronger files in this batch: every specific technical claim in its own header
  (mip-aware Point mapping already correct pre-925; the real bug being image/view level-count allocation,
  not sampler mapping) was independently traced against the actual `ApplySamplerState()` and
  `VulkanTextureBackend` constructor code and confirmed accurate, including the subtle point that a
  "correct sampler mapping with no data to select" looks identical to "wrong sampler mapping" from a
  pixel-readback test's point of view unless the underlying allocation bug is understood — which this
  file's header correctly does.
- The choice to use a genuinely mipmapped, independently-seeded-per-level texture (not merely a
  1x1-vs-128x128 pair) makes the `IsGreen`/`IsRed` result a real signal of GPU-driven LOD selection rather
  than a hardcoded stand-in.

## Final Assessment

A carefully-reasoned, well-corroborated regression test whose header commentary about a real, subtle
architecture-level bug (image mip-level allocation, not sampler filter mapping) was independently verified
against production code rather than taken on faith. Its only shortcoming, shared with most of this batch,
is the absence of a blank-frame retry safety net for a documented real-hardware Vulkan present flake.
