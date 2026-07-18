# Audit: examples/vulkan_texture_address_mode_test.cpp

## Metadata

- Source file: `examples/vulkan_texture_address_mode_test.cpp` (117 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `SpriteBatch` `TextureAddressMode` edge-sampling test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_texture_address_mode …)` /
  `cna_register_backend_test(NAME Vulkan_TextureAddressMode …)`, `cmake/Tests/VulkanTests.cmake:455-457`).
- XNA/FNA relevance: direct — `SamplerState.AddressU`/`AddressV` (`TextureAddressMode.Wrap`/`Clamp`) and
  `SpriteBatch.Draw`'s `sourceRectangle`-derived UV computation.
- FNA reference: `Graphics/SpriteBatch.cs` lines 365-376 (UV = `sourceRectangle / textureSize`, divided
  straight through with **no** `[0,1]` clamp).
- Related production code: `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp` —
  `VulkanSpriteBatchBackend::Draw()` (lines 857-931, UV computation at 872-889), `FlushTexture()`
  (lines 795-810), `VulkanGraphicsBackend::ApplySamplerState()` (lines 2196-2260).
- git corroboration: `76e49c4c`/`1c20d985` "fix(Task 665): Vulkan SpriteBatch SamplerState no-op, closes
  Phase 47" (authored 2026-07-07 08:09), matching this file's own header attribution.

## Purpose

Draws a 2×1 `[Red, Blue]` texture via `SpriteBatch` with a `sourceRectangle` (`Rectangle(0,0,4,1)`) twice
the texture's own width, so the UV span reaches `[0,2]`. Samples the pixel at screen-space fraction 5/8
(⇒ U=1.25) once under `SamplerState::PointWrap` (expects wrap-around to the *first* texel, Red) and once
under `SamplerState::PointClamp` (expects clamp-to-edge on the *last* texel, Blue) — proving both that UVs
past `[0,1]` are not silently clamped on the CPU side, and that the bound `SamplerState`'s addressing mode
genuinely reaches the GPU sampler.

## Executive Verdict

**Healthy** — this test's own math was independently re-derived and confirmed exact, and both halves of the
claim in its header comment (no CPU-side UV clamp; `SamplerState.Filter`/`AddressMode` genuinely reaching
the GPU sampler) were independently traced and confirmed true in current production code, not merely
asserted. Shares this batch's one recurring robustness gap: no blank-frame retry loop (see F1, shared
with sibling files in this batch).

## Checklist Results

### API / XNA / FNA parity — PASS
`Texture2D::CreateFromPixels(device, 2, 1, px)` (line 62) is a `NOXNA` CNA convenience constructor, used
correctly here to build raw RGBA byte data (not an XNA-standard factory, but appropriately used for test
setup). `SamplerState::PointWrap`/`PointClamp` (lines 91-92) are real FNA static instances
(`SamplerState.cs`'s built-in presets). `sb_->Draw(tex_, Rectangle(0,0,W,H), Rectangle(0,0,4,1),
Color::White)` (line 77) matches the standard `Draw(texture, destinationRectangle, sourceRectangle, color)`
overload.

### Behavioral correctness — PASS (independently re-derived)
- `SampleAtUOnePointTwoFive()` draws the 2×1 texture across the full viewport (`destRect = (0,0,W,H)`)
  with `sourceRectangle = (0,0,4,1)`; texture width is 2, so `u2 = (0+4)/2 = 2.0`, `u1 = 0`, matching the
  file's own "U spans [0,2]" claim exactly.
- Sample point `x = W*5/8` ⇒ screen fraction `5/8` ⇒ `U = 0 + 0.625 * 2.0 = 1.25`, matching the header's
  "U=1.25" claim exactly.
- `fract(1.25) = 0.25`, which in a 2-texel-wide texture (texel 0 spans `[0,0.5)`, texel 1 spans `[0.5,1)`)
  falls inside texel 0 ⇒ Red under wrap addressing — matches `wrapPass` expecting
  `(getRProperty()==255 && getBProperty()==0)`.
- Clamped to `1.0` (the last valid coordinate before the edge) ⇒ texel 1 ⇒ Blue under clamp addressing —
  matches `clampPass` expecting `(getRProperty()==0 && getBProperty()==255)`.
- Traced `VulkanSpriteBatchBackend::Draw()` (lines 857-931): `float u1 = src.X/tw; ... u2 = (src.X+src.Width)/tw;`
  (lines 881-884) — **no clamp** to `[0,1]` anywhere in this function, matching both this test's own
  in-line comment ("Task 665 fix: no [0,1] clamp here — matches FNA") and FNA's own
  `SpriteBatch.cs:372-375` (`sourceX = sourceRectangle.Value.X / (float)texture.Width;` etc., also
  unclamped).
- Traced `ApplySamplerState()` (lines 2196-2260): the `toAddr()` lambda (lines 2229-2235) maps XNA's
  `TextureAddressMode` int values `0=Wrap→VK_SAMPLER_ADDRESS_MODE_REPEAT`,
  `1=Clamp→VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE` correctly, and `FlushTexture()` (lines 795-810) calls
  `backend_->ApplySamplerState(0, pendingFilter_, pendingAddressU_, pendingAddressV_, 1)` before building
  the texture's descriptor set — confirming the bound `SamplerState` genuinely reaches the sampler used for
  this draw, not a stale pre-baked one (the exact defect this test's header says Task 665 fixed).

### Logic — PASS
Point filtering (`SamplerState::PointWrap`/`PointClamp` are both `TextureFilter::Point`) is the correct
choice to keep the sampled value crisp/exact for integer comparison — no bilinear blending risk at the
U=1.25 sample point.

### Robustness — WEAK (see F1; shared with sibling files in this batch)
No retry loop around the two `GetBackBufferData` calls inside `SampleAtUOnePointTwoFive()` (line 82),
despite this file being authored (2026-07-07 08:09) after the AMD/RADV blank-frame flake mitigation
pattern was already established elsewhere in the same test suite (`vulkan_scissor_test.cpp`, 2026-06-29).

### Testing — PASS for its own stated scope
Two genuine, independently-confirmed pixel assertions distinguishing Wrap from Clamp addressing at an
out-of-`[0,1]` UV — not a "compiles and runs" test.

### Cross-file consistency — PASS
Directly analogous to (and explicitly derived from, per its own header) `easygl_texture_address_mode_test.cpp`
for the EasyGL backend; same scene, same expected values, same technique. `Mirror` addressing
(`VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT`, `toAddr()` case `2`) is not exercised by this file but is a
plausible gap only in the sense that it's simply out of this file's stated 2-mode scope, not a defect.

## Detailed Findings

### F1 — No blank-frame retry loop around `GetBackBufferData`

- Severity: MEDIUM
- Confidence: HIGH
- Category: test-robustness / flakiness-risk
- Location/symbol: `SampleAtUOnePointTwoFive()` line 82 (`device.GetBackBufferData(&region, &pixel, 0, 1)`),
  called twice from `Draw()` (lines 91-92), neither call wrapped in a retry loop.
- Evidence / why it matters / suggested action: identical to F1 in this batch's
  `vulkan_spritebatch_multi_begin_end_test.cpp.audit.md` — `VulkanGraphicsBackend::ReadBackbuffer()`
  (`VulkanGraphicsBackend.cpp:6982-7011`) explicitly zeroes its output buffer and expects a caller retry
  when the swapchain is transiently out-of-date ("common on first frame under Wayland/RADV" per its own
  comment), a scenario `vulkan_scissor_test.cpp`/`vulkan_viewport_subregion_test.cpp` already guard against
  with a 20-attempt retry loop. This file has neither call wrapped, so on the documented flaky hardware a
  spurious all-zero readback would make `wrapPass`/`clampPass` both fail for a reason unrelated to Task 665.
- FNA/XNA comparison: N/A.
- Related files: same as the sibling finding above.

## Cross-File Observations

- Shares this batch's F1 gap with `vulkan_spritebatch_multi_begin_end_test.cpp`,
  `vulkan_texture_srgb_test.cpp`, and `vulkan_texture_mip_filter_effect_test.cpp`.
- The `pendingFilter_`/`pendingAddressU_`/`pendingAddressV_` fields consumed by `FlushTexture()` are set
  by `SpriteBatch::Begin()`'s `samplerState` parameter path (not re-inspected line-by-line in this report,
  since it belongs to `SpriteBatch.cpp`/`GraphicsDevice.cpp`, outside this batch) — the Vulkan-side half of
  that plumbing (`ApplySamplerState`) was confirmed correct here.

## Missing or Weak Tests

- `TextureAddressMode::Mirror` is untested on Vulkan by this file (though it may be covered elsewhere in
  the suite — not verified as part of this batch).
- See F1.

## Positive Findings

- Every numeric claim in the file's own header comment (U-span, U=1.25 sample point, wrap→Red,
  clamp→Blue) was independently re-derived from the actual `Draw()`/sampler-mapping code, not merely
  trusted — all confirmed exact.
- The "before Task 665" failure-mode description in the header (CPU-side clamp defeating Wrap addressing
  regardless of sampler, plus a totally no-op `SetSamplerFilter`/`SetSamplerAddressMode`) is corroborated by
  the `FlushTexture()`/`ApplySamplerState()` code actually doing real work today, and by the matching git
  commit `76e49c4c`.

## Final Assessment

A precise, well-verified regression test for a real Vulkan sampler-state wiring defect; its assertions
were independently confirmed to match both the current production implementation and the FNA reference
(`SpriteBatch.cs`'s unclamped UV division). The only shortcoming, shared across most of this batch, is the
missing blank-frame retry safety net for a real, already-documented driver flake.
