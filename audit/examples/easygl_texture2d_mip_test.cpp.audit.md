# Audit: examples/easygl_texture2d_mip_test.cpp

## Metadata

- Source file: `examples/easygl_texture2d_mip_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — CPU-side round-trip test (no GPU readback, no rendering)
- File type: C++ executable test (`Game` subclass, no gtest), 131 lines
- XNA/FNA relevance: exercises `Texture2D::SetData(int level, Rectangle*, Color*, int, int)` and
  `Texture2D::GetData(int level, Rectangle*, Color*, int, int)` — real XNA 4.0 API overloads
- FNA reference: `Texture2D.SetData<T>(int, Rectangle?, T[], int, int)` — mip-level and CPU-only round-trip
  semantics are a CNA implementation detail (no XNB pipeline), not a direct FNA source diff target
- Production code under test: `src/Microsoft/Xna/Framework/Graphics/Texture2D.cpp:58-61` (`mipDim`), `:75-107`
  (`getMipBuffer`/`getMipBufferConst`), `:152-157` (`CalculateMipLevels`), `:245-316` (`SetData`), `:378-455`
  (`GetData`)
- Naming note: like `easygl_surface_format_throws_test.cpp` in this same batch, this file is backend-agnostic —
  confirmed via `cmake/Tests/EasyGLTests.cmake:834-838` and `cmake/Tests/VulkanTests.cmake:876-880` that it is
  registered as a test under both EasyGL and Vulkan, unmodified

## Purpose

Task 171: verifies that `SetData`/`GetData` correctly target distinct mip levels of a 4×4 `Texture2D` with a full
mip chain (`mipMap=true` → levels 4×4, 2×2, 1×1), writing a distinct solid color to each level (Red/Blue/Green)
and confirming a full read-back of each level returns only its own color with no cross-level bleed. Uses no
framebuffer readback — the comment explicitly notes "pure CPU shadow buffer."

## Executive Verdict

**Healthy.** Independently traced the CPU-side mip-level storage (`cpuPixels_` for level 0, `extraMipLevels_` for
levels ≥1) and confirmed each level is backed by physically distinct storage with no possibility of the bleed this
test checks for; also confirmed `CalculateMipLevels(4,4)` genuinely produces the 3 levels this test's fixture
assumes.

## Checklist Results

### API / XNA / FNA parity
`Texture2D(dev, 4, 4, /*mipMap=*/true, SurfaceFormat::Color)` (line 65) matches `Texture2D.hpp:64-65`'s public
constructor exactly. `SetData(int level, const Rectangle*, const Color*, int, int)` and the matching `GetData`
overload (lines 72-74, 80, 93, 106) match the corresponding declarations in `Texture2D.hpp`.

### Behavioral correctness
Re-derived `CalculateMipLevels(4,4)` (`Texture2D.cpp:152-157`): `levels=1; w=4,h=4 →(loop) w=2,h=2,levels=2
→(loop) w=1,h=1,levels=3 →(exit, w==1&&h==1)`. Result: `3` — matches this test's own assumption ("A 4×4 mipMap
texture has 3 mip levels: 4×4 (mip 0), 2×2 (mip 1), 1×1 (mip 2)," header comment line 3).
Re-derived `mipDim(base, level) = max(1, base >> level)` (`Texture2D.cpp:58-61`) for `base=4`:
`mipDim(4,0)=4, mipDim(4,1)=2, mipDim(4,2)=1` — matches the test's expected pixel counts (16, 4, 1) for each
`SetData`/`GetData` call (lines 72-74, 80, 93, 106).
Traced `getMipBuffer`/`getMipBufferConst` (`Texture2D.cpp:75-107`): level 0 is stored in `cpuPixels_` (a
`shared_ptr<vector<uint8_t>>` member); levels ≥1 are stored in `(*extraMipLevels_)[level-1]`, a **separate**
vector per level inside a `shared_ptr<vector<vector<uint8_t>>>`. These are physically distinct buffers with no
aliasing — a `SetData(1, ...)` write can only ever touch `(*extraMipLevels_)[0]`, never `cpuPixels_` (level 0) or
`(*extraMipLevels_)[1]` (level 2), which is exactly the "no bleed across levels" property this test asserts
(header comment line 14).
For `SetData(int level, ...)` specifically (`Texture2D.cpp:245-316`), with `rect=nullptr` the effective region
defaults to the full `levelW×levelH` (lines 258-263) — matches this test's `SetData(N, nullptr, data, 0, count)`
calls, which write the whole level in one shot.

### Logic
The GL-facing side of this test is intentionally bypassed: for `level != 0`, `SetData` calls
`backend_->UpdatePixelsLevel(level, buf.data(), levelW, levelH)` (`Texture2D.cpp:312-315`) which, per
`EasyGLGraphicsBackend.cpp:496-500`, does perform a real `set_image_2d` GL upload for that level — but since this
test never calls `GetBackBufferData` (it only round-trips through `Texture2D::GetData`, which reads the CPU
shadow buffer directly, `Texture2D.cpp:397-427`), it does not actually verify that the GPU-side mip level was
correctly uploaded, only that the CPU-side shadow was correctly written and read back. This is accurately
reflected in the test's own header comment ("No framebuffer readback — pure CPU shadow buffer") — the scope is
honestly stated, not overclaimed.

### Memory/resource lifetime
`tex` (a local `Texture2D`, not a member) is used entirely within `Initialize()`'s scope and never touched again
before `Exit()` — no lifetime concern. `GraphicsDeviceManager` (`gdm_`) is a member, correctly outliving the
constructor.

### Testing
Confirmed the check helper (`check`, lines 49-56) accumulates into `result_` without early-exiting on the first
failure, so a single run reports every one of the 16+4+1=21 individual pixel checks — useful for pinpointing
exactly which pixel(s), if any, would fail in a regression, rather than stopping at the first mismatch.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — GPU-side mip upload correctness for levels ≥1 is not verified by this test

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `MipTest::Initialize` (entire file uses only `Texture2D::SetData`/`GetData`, never
  `GraphicsDevice::GetBackBufferData`); production GPU-upload call site:
  `Texture2D.cpp:312-315` → `EasyGLGraphicsBackend.cpp:496-500` (`UpdatePixelsLevel`)
- Evidence: this test's `GetData` calls (lines 80, 93, 106) read from `getMipBufferConst`, i.e. the CPU-side
  shadow buffer (`Texture2D.cpp:397-427`), never from the GPU texture via a rendered-and-read-back frame. A bug
  in `EasyGLTextureBackend::UpdatePixelsLevel`'s GL call (e.g. wrong level index, wrong internal format, or a
  stride/row-alignment bug specific to level ≥1's smaller dimensions) would not be caught by this test, only by a
  test that actually samples the GPU texture at each mip level (e.g. via a shader that explicitly selects a mip
  level, or an anisotropic/mipmap-minification sampling test at various view distances).
- Why it matters: this test verifies the CNA-side CPU bookkeeping (`extraMipLevels_` indexing, `mipDim` math,
  `CalculateMipLevels` count) is correct and non-bleeding, which is real, valuable coverage — but it does not
  close the loop on whether the GPU texture the game actually renders with matches that CPU shadow at each level.
- FNA/XNA comparison: N/A — mip-level GPU-readback verification is not part of the XNA API contract itself, this
  is a CNA-internal correctness question.
- Related files: `easygl_texture2d_anisotropic_singlelevel_test.cpp` (audited in this same batch) does perform a
  real GPU-readback test, but only for a *single-level* texture — no file found in this batch (or, as far as this
  audit's scope reveals, elsewhere in this shard) combines a multi-level mip chain with an actual rendered,
  minification-filtered GPU readback verifying the correct level's content is sampled.
- Suggested action (not implemented by this audit): add or locate a companion test that renders a multi-mip
  `Texture2D` at a real minification distance/filter and confirms the sampled color matches the level that should
  be selected — closing the CPU-bookkeeping-vs-GPU-content gap this file's own scope intentionally leaves open.

## Cross-File Observations

- Shares its "pure CPU shadow buffer, no framebuffer readback" scope and multi-backend CMake registration pattern
  with `easygl_texture2d_partial_rect_test.cpp` (audited in this same batch) — both are CPU round-trip tests for
  `Texture2D::SetData`/`GetData`, complementary to (not overlapping with) the GPU-rendering-focused
  `easygl_texture2d_anisotropic_singlelevel_test.cpp`.

## Missing or Weak Tests

- See F1 — no GPU-readback verification of mip-level content for a multi-level texture exists in this batch.
- No test exercises a partial-rectangle `SetData`/`GetData` on a non-zero mip level (this file only uses full-level
  writes via `rect=nullptr`; `easygl_texture2d_partial_rect_test.cpp` only exercises partial rects on level 0) —
  the combination of "partial rect" and "non-zero mip level" is untested by this batch.

## Positive Findings

- The test's own header comment accurately and modestly states its actual scope ("pure CPU shadow buffer... No
  framebuffer readback") rather than implying a GPU-verified result it doesn't provide.
- Independently confirmed the underlying mip-level storage (`cpuPixels_` vs. `extraMipLevels_`) is physically
  separate per level, so the "no bleed" property this test checks is backed by a genuine structural guarantee in
  the production code, not merely by this test's own specific input choices happening not to collide.
- Thorough per-pixel checking (21 individual assertions) rather than a single aggregate comparison, giving precise
  failure localization.

## Final Assessment

A correct, well-scoped CPU-side round-trip test whose predicted mip-level dimensions and level count were
independently re-derived from the production `mipDim`/`CalculateMipLevels` functions and match exactly. Its
honestly-stated scope (CPU shadow buffer only) leaves a real, if modest, gap around GPU-side mip-upload
correctness for levels beyond 0 — a reasonable candidate for a follow-up test, not a defect in this file itself.
