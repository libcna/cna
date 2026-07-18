# Audit: examples/bgfx_render_target_sample_test.cpp

## Metadata

- Source file: `examples/bgfx_render_target_sample_test.cpp` (99 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — RenderTarget2D-sampled-as-Texture2D-after-unbinding smoke test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_test_bgfx_render_target_sample` / `Bgfx_RenderTarget2D_SampleAfterUnbind`,
  `cmake/Tests/BgfxTests.cmake:261-264`)
- XNA/FNA relevance: direct — sampling a `RenderTarget2D` as a `Texture2D` after `SetRenderTarget(null)`
  is standard, load-bearing XNA usage (post-process, render-to-texture).
- Related production code: `include/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.hpp` (`IBgfxSamplable`,
  `BgfxTextureBackend`, `BgfxRenderTargetBackend`),
  `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` (`BgfxSpriteBatchBackend::Draw`, lines 938-966).
- Authoring commit: `872dd8ee` ("verify(Task P39-333): confirm RenderTarget2D sampling after unbind,
  find Bgfx handle-cast bug", 2026-07-05).

## Purpose

Task 333: this test's own header comment states its purpose is to *confirm* that
`BgfxSpriteBatchBackend::Draw` performs an unsafe `static_cast<const BgfxTextureBackend&>` on whatever
`ITextureBackend` it is handed — and since `BgfxRenderTargetBackend` (a `RenderTarget2D`'s real backend)
is an *unrelated sibling class*, not a subclass of `BgfxTextureBackend`, this cast is claimed to be
undefined behaviour when a `RenderTarget2D` is sampled through `SpriteBatch` after unbinding. Exit code 0
is framed as "did not crash", explicitly *not* a pixel-correctness assertion, because the file's header
additionally claims "Bgfx has no usable GPU pixel-readback path in this project."

## Executive Verdict

**Needs attention (stale claims, not a live defect)** — this audit traced the actual current production
code and found **both of this file's central claims are no longer true**: the unsafe cast it describes
was fixed by a later commit, and Bgfx *does* have a working pixel-readback path, demonstrated by 5+
sibling files in this exact shard. The underlying feature (RenderTarget2D-as-Texture2D sampling) is
correct today; this file's own justification for why it can't verify that is outdated.

## Checklist Results

### API / XNA / FNA parity
The scenario under test (bind RT, clear, unbind, sample via `SpriteBatch::Draw`) is standard, correct
XNA usage and the file constructs it correctly.

### Behavioral correctness / Logic
Traced `BgfxSpriteBatchBackend::Draw` (`BgfxGraphicsBackend.cpp:938-966`) as it exists **today**:
```cpp
const auto* samplable = dynamic_cast<const IBgfxSamplable*>(&texture);
bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
if (samplable) handle = samplable->GetBgfxTextureHandle();
```
This is **not** the unsafe `static_cast<const BgfxTextureBackend&>` this test's header describes. Both
`BgfxTextureBackend` and `BgfxRenderTargetBackend` implement the `IBgfxSamplable` interface
(`BgfxGraphicsBackend.hpp:158,217`), each correctly reporting their own real sampleable handle
(`textureHandle` vs. `colorTex` respectively) via `GetBgfxTextureHandle()`. The interface's own doc
comment at `BgfxGraphicsBackend.hpp:125-134` states explicitly: *"Task 878/879 fix (closes Task 873):
`BgfxSpriteBatchBackend::Draw` previously did an unsafe `static_cast<const BgfxTextureBackend&>`... This
accessor lets each concrete backend report its own real sampleable texture handle."*

### Cross-file / git-history verification (this audit's own check, not present in the file)
Confirmed via `git log` that this fix (commit `bda07bac`, "feat(Task 878/879): implement RenderTarget2D
MSAA on Vulkan and Bgfx", 2026-07-07) **post-dates** this test file's own authoring commit (`872dd8ee`,
2026-07-05) by two days. The bug this file exists to "confirm" was real *when the file was written*, but
was fixed shortly afterward, and this test file's header comment was never updated to reflect that.

### Robustness — the second stale claim
The file's header also asserts "Bgfx has no usable GPU pixel-readback path in this project" as the
reason no pixel-correctness assertion is attempted. This audit found this is **also no longer true**:
`examples/bgfx_rendertarget2d_mip_test.cpp` (same shard, same commit era) performs the *exact* scenario
this file avoids — render into a `RenderTarget2D`, unbind it, sample it via `SpriteBatch::Draw`, call
`GraphicsDevice::GetBackBufferData` — and gets back real, numerically-correct pixel content (verified
independently by this audit; see that file's own report). `bgfx_pbreffect_test.cpp` and the three
`bgfx_rasterizerstate_*_test.cpp` files in this same shard also successfully read back real pixel data
via `GetBackBufferData` after 3D draws. Pixel-level verification of this exact scenario is demonstrably
possible on this backend today.

## Detailed Findings

### F1 — Header comment describes an already-fixed bug as an open, unconfirmed-but-suspected UB risk

- Severity: MEDIUM
- Confidence: HIGH (confirmed by reading the current `BgfxSpriteBatchBackend::Draw` implementation and
  cross-checking commit dates via `git log`)
- Category: test-coverage / stale-comment
- Location/symbol: file header comment (lines 1-20); the "Task 873" bug it describes was fixed by
  `IBgfxSamplable`/`BgfxSpriteBatchBackend::Draw`'s `dynamic_cast` (commit `bda07bac`, Task 878/879)
- Evidence: production code no longer contains the described `static_cast`; the interface's own comment
  cross-references "Task 878/879 fix (closes Task 873)"; commit-date ordering confirms the fix post-dates
  this test file.
- Why it matters: a reader (or a future contributor) trusting this file's header comment today would
  incorrectly believe RenderTarget2D-as-Texture2D sampling is still unverified/risky UB on Bgfx, and that
  no pixel-level regression test is possible — both false. The test itself still passes (exit 0, no
  crash) and provides *some* residual regression value (it would still catch a reintroduced type-confusion
  bug via a crash/ASan failure), but its own stated rationale for being merely a smoke test is outdated,
  and the opportunity to upgrade it into a real pixel-correctness regression test (matching
  `easygl_render_target_test.cpp`/`vulkan_rt2d_test.cpp`'s already-established sibling coverage, which
  this file's own header cites as the standard other backends meet) has not been taken even though the
  blocking bug is gone.
- FNA/XNA comparison: N/A (test-authoring/documentation issue, not an XNA/FNA behavior question — the
  underlying RenderTarget2D-as-Texture2D behavior is now correct).
- Related files: `bgfx_render_target_cube_sample_test.cpp` and `bgfx_render_target_usage_test.cpp` in
  this same batch have the same stale-comment pattern for the cube-map and RenderTargetUsage-smoke-test
  cases respectively — see their own reports.
- Suggested future action (not implemented by this audit): update the header comment to reflect the
  Task 878/879 fix, and/or upgrade this test to assert real pixel content (green, matching the RT's
  `Clear(Color(0,255,0,255))`) now that both blocking issues (the cast bug and the readback path) are
  resolved.

## Cross-File Observations

- This file's own justification chain ("SpriteBatch casts are unsafe" + "no pixel readback is possible")
  is directly falsified by two other files in this exact shard
  (`bgfx_rendertarget2d_mip_test.cpp`, `bgfx_pbreffect_test.cpp`) doing precisely what this file says
  can't be done. This is the same shape of issue flagged in the sibling EasyGL batch's stale-constant
  finding (see `audit/examples/easygl_basiceffect_specular_test.cpp.audit.md`), applied here to an
  entire test-design rationale rather than a single numeric constant.

## Missing or Weak Tests

See F1 — this test could and should be upgraded to assert the RT's actual green content is correctly
sampled, not merely that the draw call doesn't crash.

## Positive Findings

- The test's core mechanical setup (bind → clear → unbind → sample-as-texture) is correct and exercises
  a real, previously-broken code path that is now demonstrably fixed.
- As a pure regression guard against a *reintroduced* unrelated-type `static_cast`, this test retains
  some value even with a stale comment — a regression would very likely still crash or trip an ASan
  finding under this exact call sequence.

## Final Assessment

The underlying feature this file tests is correct today. The file itself is stuck describing a bug that
was fixed two days after it was written, understating both the current correctness of the Bgfx backend
and the pixel-verification capability that now exists elsewhere in this same shard.
