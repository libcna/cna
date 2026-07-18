# Audit: examples/vulkan_rtcube_test.cpp

## Metadata

- Source file: `examples/vulkan_rtcube_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `RenderTargetCube` per-face rendering, Vulkan backend
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_rtcube …)` / `cna_register_backend_test(NAME
  Vulkan_RenderTargetCube_PerFace …)`, `cmake/Tests/VulkanTests.cmake:417-420`).
- XNA/FNA relevance: direct — `RenderTargetCube`, `CubeMapFace`, `GraphicsDevice.SetRenderTarget
  (RenderTargetCube, CubeMapFace)`.
- FNA reference: `Graphics/GraphicsDevice.cs` (`SetRenderTarget(RenderTargetCube renderTarget,
  CubeMapFace cubeMapFace)`, lines 921-932), `Graphics/RenderTargetCube.cs`.
- Related production code: `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp`
  (`VulkanRenderTargetCubeBackend` per-face `VkFramebuffer` construction, lines 8574-8825;
  `RecordCommandBuffer()` Phase-1 RT loop, lines 6694-6750).

## Purpose

Verifies that `VulkanRenderTargetCubeBackend` allocates 6 genuinely distinct per-face
`VkFramebuffer`s (not one shared/aliased buffer) and that 6 sequential RT passes into those faces
do not corrupt the Phase-2 backbuffer pass. Fills every face solid red via `SpriteBatch`, then
clears the backbuffer to blue *after* all 6 `SetRenderTarget(nullptr)` unbinds, and asserts the
backbuffer centre reads blue. Unlike `vulkan_rendertargetcube_sample_test.cpp` (also in this
batch), it does **not** sample the cube's own rendered faces back — it only proves the 6 RT passes
completed without leaking into or corrupting the un-related backbuffer pass.

## Executive Verdict

**Healthy** — correctly scoped (explicitly does not claim to verify per-face content correctness,
only pass isolation), and its own ordering-dependent comment about `Clear()` placement was verified
against the actual shared-clear-colour-scalar architecture.

## Checklist Results

### API / XNA / FNA parity
`SetRenderTarget(rtc_.get(), static_cast<CubeMapFace>(face))` matches FNA's
`GraphicsDevice.SetRenderTarget(RenderTargetCube, CubeMapFace)` overload exactly (verified against
`GraphicsDevice.cs:921`); `include/Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp:302`
declares the identical `void SetRenderTarget(RenderTargetCube* renderTarget, CubeMapFace
cubeMapFace);` signature (pointer instead of reference, the established CNA convention for
optional/nullable XNA reference-type parameters).

### Behavioral correctness
The comment *"Clear must be called AFTER all SetRenderTarget(null) so that clearR_/G_/B_/A_ is set
to blue when RecordCommandBuffer processes Phase 2"* (lines 94-95) is an accurate description of
the shared-clear-colour-scalar architecture this audit independently confirmed while reviewing
`vulkan_rt2d_test.cpp` and `vulkan_rt_roundtrip_test.cpp` in this same batch — `Clear()` only ever
writes one global `clearR_/G_/B_/A_` tuple, read at `RecordCommandBuffer` time for whichever pass
is being recorded, so ordering it after the last face unbind (rather than, say, at the very start
of `Draw()`) is a real, necessary constraint, not defensive-but-unnecessary caution.

### Logic
Because `Initialize()` constructs `rtc_` with `DepthFormat::Depth24` (not `DepthFormat::None`,
unlike `vulkan_rendertargetcube_sample_test.cpp`'s otherwise-similar setup), the per-face
framebuffers include a real depth attachment — the `VulkanRenderTargetCubeBackend` constructor's
depth-image branch (`VulkanGraphicsBackend.cpp:8695-8730`) is exercised by this test but not by
its `DepthFormat::None` sibling, giving this batch complementary depth-format coverage across the
two `RenderTargetCube` tests rather than duplicate coverage.

### Testing
Single centre-pixel assertion on the backbuffer only (`R<=50, G<=50, B>=200`, matching the shard's
established blue/red/green threshold convention). The test explicitly disclaims verifying each
face's own rendered content — its own header comment (lines 4-8) frames the goal narrowly as
"distinct per-face framebuffers" + "backbuffer pass is unaffected," and the assertion matches that
narrower scope; it does not overclaim.

### Cross-file consistency
Forms a clean pair with `vulkan_rendertargetcube_sample_test.cpp`: this file proves *isolation*
(6 RT passes don't corrupt the backbuffer), the other proves *content* (a specific face's actual
pixel data is sampleable afterward as a cube). Between them the two tests cover both halves of
"does `RenderTargetCube` genuinely work on Vulkan," without meaningful duplication.

## Detailed Findings

None — no CRITICAL/HIGH/MEDIUM findings identified.

## Missing or Weak Tests

- No assertion that each of the 6 faces itself contains red (this is by design, per the file's own
  stated scope, and is covered from a different angle by
  `vulkan_rendertargetcube_sample_test.cpp`'s single-face-content check via
  `EnvironmentMapEffect` — though that file only round-trips through one arbitrary sampled
  direction, not all 6 faces individually). No Vulkan test in this shard directly asserts *all 6*
  distinct face contents in one pass (e.g. via 6 separate `EnvironmentMapEffect` reflect
  directions or a `GetData` per face) — a legitimate, if narrow, coverage gap for "face N really
  contains what was drawn into face N, not face M's data" for `N != previously-checked-direction`.
  This is a coverage observation, not a defect in this specific file (which never claimed to check
  that).

## Positive Findings

- Correctly scoped: the header comment's own stated goals match exactly what the assertions check,
  neither overclaiming nor underclaiming.
- The `DepthFormat::Depth24` choice here (vs. `DepthFormat::None` in the sibling sample test)
  gives this batch non-duplicate depth-attachment coverage for `RenderTargetCube`'s per-face
  framebuffer construction.
- The `Clear()`-ordering comment reflects genuine, correctly-reasoned understanding of this
  backend's shared-clear-colour-scalar architecture, independently confirmed by this audit against
  the same mechanism examined in two sibling files.

## Final Assessment

A correctly-scoped, narrowly-focused isolation test that does exactly what it claims and nothing
more. No changes recommended; the one identified gap (no per-face content verification across all
6 faces) is better addressed by a dedicated new test than a fix to this file, since it would be
out of this file's own stated scope.
