# Audit: examples/vulkan_texture_srgb_test.cpp

## Metadata

- Source file: `examples/vulkan_texture_srgb_test.cpp` (154 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `Texture2D`/`SurfaceFormat.Color` linear-vs-sRGB decode test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_texture_srgb …)` /
  `cna_register_backend_test(NAME Vulkan_Texture2D_ColorFormat_Linear …)`,
  `cmake/Tests/VulkanTests.cmake:42-44`).
- XNA/FNA relevance: direct — `SurfaceFormat.Color` is explicitly linear in FNA/XNA (a distinct
  `SurfaceFormat.ColorSrgbEXT` exists for the gamma-encoded variant); `BasicEffect.VertexColorEnabled`/
  `TextureEnabled`/`DiffuseColor`.
- Related production code: `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp` — swapchain format
  selection in `CreateSwapchain()` (lines 1560-1576), all `Texture2D`/render-target image/view format
  assignments (`VK_FORMAT_R8G8B8A8_UNORM`, confirmed at every image/view-creation site in the file — no
  `_SRGB` format used anywhere).
- git corroboration: `98fb67ba`/`1c50a307` "fix(Task 284): Vulkan applied unwanted sRGB gamma to Texture2D
  and swapchain" (authored 2026-07-03 19:24), matching this file's own header attribution.

## Purpose

Renders two otherwise-identical mid-grey (128,128,128) full-screen quads — scene (a) via
`BasicEffect.VertexColorEnabled` (no texture, so only whatever transform the swapchain itself applies can
affect the result), scene (b) via a 1×1 `Texture2D` sampled with `BasicEffect.TextureEnabled`+white
`DiffuseColor` (adds the texture-sampling path on top of the same swapchain transform) — and asserts both
scenes read back the same backbuffer value (within `diff <= 5`). The file's own reasoning is sound: prior
saturated-0/255-only tests could never detect an sRGB decode mixup, since 0 and 255 are both fixed points
of the sRGB transfer curve; using mid-grey 128 (whose sRGB-decoded value is dramatically different, ~55)
makes a real defect unmistakable.

## Executive Verdict

**Healthy** — the "no sRGB format used anywhere" claim was independently verified by grepping *every*
image/view format assignment in the ~9000-line backend file, not just the one texture-creation path this
test exercises; genuinely proves there is no sRGB decode mismatch on the current Vulkan backend for
`SurfaceFormat.Color`. Shares this batch's recurring blank-frame-retry robustness gap (F1), and surfaces
(without causing) a pre-existing `BasicEffect.VertexColorEnabled` XNA-property-convention deviation worth
flagging for whichever audit covers `BasicEffect.hpp` (F2, informational).

## Checklist Results

### API / XNA / FNA parity — PASS, with one cross-file convention note (F2)
`fx.VertexColorEnabled = true;`/`fx.setTextureEnabledProperty(false);` (lines 80-81) and
`fx.VertexColorEnabled = false;`/`fx.setTextureEnabledProperty(true);` (lines 110-111) both compile against
`BasicEffect.hpp`'s actual current API, which exposes `VertexColorEnabled` as a **public field** rather
than the project's own `getXProperty()`/`setXProperty()` convention used for every other property on the
same class (`setTextureEnabledProperty`, `setDiffuseColorProperty`, etc., all present in this same file).
FNA's real `BasicEffect.VertexColorEnabled` (`BasicEffect.cs:337-347`) is a genuine C# property with
`get`/`set` accessors (the `set` also marks `EffectDirtyFlags.ShaderIndex`), so per this project's own
`CLAUDE.md` convention it should be `getVertexColorEnabledProperty()`/`setVertexColorEnabledProperty()`.
This is **not** a defect in this test file — the test correctly uses the actual current, real API surface —
but it is worth flagging as a pre-existing inconsistency in `BasicEffect.hpp` itself (out of this batch's
scope; see F2).
`Texture2D::CreateFromPixels(device, 1, 1, greyBytes)` (line 107) is a `NOXNA` convenience constructor, used
appropriately here for raw-byte texture setup.

### Behavioral correctness — PASS (independently verified)
- Grepped every `VK_FORMAT_R8G8B8A8_UNORM`/`_SRGB` occurrence in `VulkanGraphicsBackend.cpp`: **all**
  Texture2D/render-target image and image-view format assignments use `_UNORM` (linear); no `_SRGB` format
  literal appears anywhere in the file for these resources — directly confirming scene (b)'s texture path
  cannot receive an automatic sRGB decode.
- Traced `CreateSwapchain()` (lines 1560-1576): explicitly **prefers**
  `VK_FORMAT_B8G8R8A8_UNORM`+`VK_COLOR_SPACE_SRGB_NONLINEAR_KHR` over whatever `fmts[0]` happens to be,
  with an in-line comment citing exactly this test's own reasoning ("An SRGB swapchain format would apply
  an automatic linear-to-sRGB encode to every presented pixel ... which XNA/FNA does not do by default").
  This confirms the swapchain-format risk the test's own header raises ("any transform the swapchain itself
  applies... affects both equally and cancels out") is a real, anticipated concern in production code, not
  a hypothetical the test author invented — and that production code already defends against it by
  preferring a UNORM surface format (note: `_NONLINEAR_KHR` color-space tag here only affects how the
  *display* interprets the values, not an automatic conversion on write, since the format itself is UNORM).
- `diff = |vertexColorPixel.R - texturePixel.R| <= 5` (lines 131-133) is a tight, appropriate tolerance
  given a genuine sRGB-decode mixup would produce roughly a `128 vs. ~55` gap (~73), two orders of magnitude
  larger than the 5-unit tolerance — no risk of a real bug passing by accident.
- `RasterizerState::CullNone` (line 72) is the same recurring "Task 896" winding workaround seen elsewhere
  in this batch, applied once for both scenes (device state persists across the two blocks in the same
  `Draw()` call).

### Logic — PASS
Both scenes share the same `Clear`/`DepthTest=false`/`BlendState::Opaque`/`RasterizerState::CullNone`
device state, isolating the texture-sampling path as the only difference between them — correct
experimental design for attributing any divergence specifically to texture sampling rather than to some
other per-scene state difference.

### Robustness — WEAK (see F1; shared with sibling files in this batch)
No retry loop around either `device.GetBackBufferData(&centReg, ..., 0, 1)` call (lines 97, 128), despite
this file being authored (2026-07-03) after the AMD/RADV blank-frame flake mitigation pattern was
established (`vulkan_scissor_test.cpp`, 2026-06-29).

### Testing — PASS for its own stated scope
A genuinely discriminating pixel comparison (not a saturated 0/255 check that could hide the exact class of
bug being tested), targeting the real Task 284 defect class.

### Cross-file consistency — PASS (with F2 noted)
`BasicEffect` usage (`Apply()`, matrix properties, `TextureEnabled`/`DiffuseColor`) is otherwise consistent
with sibling Vulkan test files in this batch (`vulkan_vertex_format_test.cpp`, same
`RasterizerState::CullNone` pattern, same `BasicEffect.Apply()` call sequence).

## Detailed Findings

### F1 — No blank-frame retry loop around `GetBackBufferData`

- Severity: MEDIUM
- Confidence: HIGH
- Category: test-robustness / flakiness-risk
- Location/symbol: `Draw()` lines 97 and 128 (`device.GetBackBufferData(&centReg, &vertexColorPixel, 0, 1)`
  / `device.GetBackBufferData(&centReg, &texturePixel, 0, 1)`), neither wrapped in a retry loop.
- Evidence / why it matters / suggested action: identical to F1 in this batch's other reports —
  `VulkanGraphicsBackend::ReadBackbuffer()` (`VulkanGraphicsBackend.cpp:6982-7011`) explicitly zeroes its
  output and expects a caller retry on a documented "common on first frame under Wayland/RADV" condition; a
  spurious all-zero readback for scene (a) or (b) here would report either a false FAIL (both scenes
  differ) or, worse, a false PASS if *both* scenes happen to be zeroed on the same blank-frame occurrence
  (`|0-0|=0 <= 5`), silently defeating the test's entire purpose on exactly the hardware this codebase
  documents as actually flaky.
- FNA/XNA comparison: N/A.
- Related files: same as the sibling findings above.

### F2 — `BasicEffect.VertexColorEnabled` is a public field, not `getX`/`setX`, deviating from this project's own C#-property convention (informational; not a defect in this test file)

- Severity: LOW (scoped as informational for this file; the underlying API itself is out of this batch's
  file list)
- Confidence: HIGH
- Category: cross-file-consistency / API-convention
- Location/symbol: `include/Microsoft/Xna/Framework/Graphics/BasicEffect.hpp:48` (`bool
  VertexColorEnabled = false;`), used by this test at lines 80, 110.
- Evidence: `CLAUDE.md`'s "C# Properties → C++ Convention" section requires `getXProperty()`/
  `setXProperty()` for every C# property; FNA's own `VertexColorEnabled`
  (`BasicEffect.cs:337-347`) is a real `get`/`set` property (whose setter also flips a dirty flag), not a
  plain field — yet every *other* `BasicEffect` property this same test file touches
  (`TextureEnabled`, `DiffuseColor`) correctly follows the `setXProperty()` pattern.
- Why it matters: this is a pre-existing inconsistency in `BasicEffect.hpp`'s public surface, not something
  this test introduced or could reasonably work around — flagged here only because this test happens to be
  one of the files in this batch that exercises the field directly, and per this audit's own cross-file
  observation duty. It belongs to whichever shard covers `include/Microsoft/Xna/Framework/Graphics/BasicEffect.hpp`
  for an actual fix decision.
- Related files: `include/Microsoft/Xna/Framework/Graphics/BasicEffect.hpp`.

## Cross-File Observations

- Shares this batch's F1 gap with `vulkan_spritebatch_multi_begin_end_test.cpp`,
  `vulkan_texture_address_mode_test.cpp`, and `vulkan_texture_mip_filter_effect_test.cpp`.
- The `VertexColorEnabled` public-field pattern (F2) also appears in `vulkan_vertex_format_test.cpp`
  (`testStride16`/`testStride24`) in this same batch — same underlying API, same non-defect-in-this-file
  observation.
- The `RasterizerState::CullNone` "Task 896" comment recurs verbatim across this file,
  `vulkan_texture_mip_filter_effect_test.cpp`, and `vulkan_vertex_format_test.cpp`.

## Missing or Weak Tests

- Only tests `SurfaceFormat.Color` (the default); `SurfaceFormat.ColorSrgbEXT` (the FNA/XNA-extension
  explicitly-sRGB format this test's own header references as the "correct" counterpart) is not exercised
  here to prove the *positive* case (that requesting the sRGB format *does* produce a decode) — reasonable
  to leave out of scope for a test titled and scoped around the linear-format regression, but worth noting
  as a natural follow-up test.
- See F1.

## Positive Findings

- The choice of mid-grey (128) rather than saturated 0/255 test colors is a genuinely clever, well-reasoned
  test design choice that this audit confirmed is necessary (0/255 are both sRGB-curve fixed points) and
  sufficient (the ~73-unit gap a real bug would produce is far outside the 5-unit tolerance).
- The two-scene (vertex-color-only vs. textured) structure correctly isolates the swapchain's own possible
  gamma handling from the texture-sampler's, and this audit independently confirmed via
  `CreateSwapchain()` that the swapchain-format risk this test defends against is a real, already-mitigated
  concern in production code, not a strawman.
- Exhaustively confirmed (by grep across the entire ~9000-line backend file) that no `_SRGB` Vulkan format
  is used for any Texture2D/render-target resource, giving high confidence the underlying Task 284 fix is
  genuinely still in effect, not just locally around the one code path this specific test happens to touch.

## Final Assessment

A well-designed, independently-verified regression test for a real and easy-to-miss class of GPU-backend
bug (sRGB/linear texture-format mixups), whose core numeric-design choice (mid-grey rather than saturated
colors) this audit confirmed is actually necessary to detect the target defect. Its only shortcomings are
the shared blank-frame-retry gap (F1) and an informational note about a pre-existing API-convention
deviation in `BasicEffect.hpp` that the test merely exposes rather than causes (F2).
