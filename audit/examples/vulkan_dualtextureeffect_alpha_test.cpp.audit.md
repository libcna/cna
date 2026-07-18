# Audit: examples/vulkan_dualtextureeffect_alpha_test.cpp

## Metadata

- Source file: `examples/vulkan_dualtextureeffect_alpha_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `DualTextureEffect.Alpha` forwarding pixel test
  (Task 385)
- File type: standalone `Game`-subclass executable, CTest-registered integration test
- XNA/FNA relevance: direct — `DualTextureEffect.Alpha`/`DiffuseColor` alpha-premultiplication.
- FNA reference: `DualTextureEffect.cs`'s `OnApply()` (`diffuseColorParam.SetValue(new
  Vector4(diffuseColor * alpha, alpha))`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/DualTextureEffect.cpp`,
  `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp`
  (`FillBlendAttachmentState()` line 3081, `GetOrCreatePipelineDualTex3D()` line 3909),
  `tests/Microsoft/Xna/Framework/Graphics/DualTextureEffectTests.cpp`.

## Purpose

Deliberately narrow, self-documenting test: verifies `DualTextureEffect.Alpha=0.5` forwards through the
real GPU dispatch to the output alpha channel, checking **only** the alpha channel of the readback (not
RGB). The file's own header comment (lines 1-27) explains why: it claims Vulkan's `BlendState` support
is "already known to be almost entirely fake (Task 868/870)... every Vulkan pipeline in this codebase
hardcodes colorBlendFactor=SRC_ALPHA/ONE_MINUS_SRC_ALPHA regardless of the GraphicsDevice.BlendState
actually requested," and that a full RGB-premultiplication pixel test (as the EasyGL sibling does)
would spuriously fail on Vulkan for a reason unrelated to `DualTextureEffect` itself. It asserts the
*alpha* channel specifically is safe to check because the hardcoded Vulkan blend pipelines allegedly
use `srcAlphaBlendFactor=ONE`/`dstAlphaBlendFactor=ZERO` (matching `BlendState.Opaque`'s real alpha
semantics) "by coincidence."

## Executive Verdict

**Needs attention** — the test's own alpha-only check is correct and passes for the right reason, but
its header comment's central justification (Vulkan blend state being "known-fake"/hardcoded) is
**stale**: Task 868 (dated *after* this file's creation) implemented real per-`BlendState` factor/op
mapping on Vulkan, which this audit independently confirmed is live in the exact pipeline function
(`GetOrCreatePipelineDualTex3D`) this test's own draw goes through. The now-obsolete rationale left
behind a genuine, avoidable test-coverage gap: no Vulkan example in this entire codebase pixel-tests a
non-Opaque `BlendState` end-to-end.

## Checklist Results

### API / XNA / FNA parity
`fx.setAlphaProperty(0.5f)`/`setDiffuseColorProperty(Vector3(1,1,1))` (lines 95-96) are standard
`getX/setXProperty()` usage, correctly applied.

### Behavioral correctness
Re-derived the expected alpha value: `DualTextureEffect::FillGpuDrawParams()`
(`DualTextureEffect.cpp:260-263`) sets `p.diffuseColor[3] = alpha_` (the *unscaled* alpha, per FNA's own
`OnApply()` formula `new Vector4(diffuseColor*alpha, alpha)` — confirmed identical structure).
`dual_texture3d.frag.glsl`: `outColor = tex1 * tex2 * fragTint;` with both textures solid white
(alpha=1 each) and `fragTint.a = 0.5`, so `outColor.a = 1×1×0.5 = 0.5 → 128` (255×0.5=127.5, rounds to
128) — matches the test's `expect≈128` with `±20` tolerance (line 105) exactly.

### Logic / Testing — the file's central claim
This is the substantive finding. Traced the actual current blend-mapping code in
`VulkanGraphicsBackend.cpp`:
- `FillBlendAttachmentState()` (lines 3081-3093, comment: *"Task 868: fills a
  VkPipelineColorBlendAttachmentState's real blend factors/op from BlendKeyParams... Previously every
  call site hardcoded BlendState.NonPremultiplied's own equation here whenever blend was true."*) sets
  `cba.srcColorBlendFactor/dstColorBlendFactor/colorBlendOp/srcAlphaBlendFactor/dstAlphaBlendFactor/
  alphaBlendOp` all from the caller's real `BlendKeyParams`, derived from the actual
  `GraphicsDevice.BlendState` in force.
- `GetOrCreatePipelineDualTex3D()` (the exact pipeline `DualTextureEffect` draws through on Vulkan)
  calls `FillBlendAttachmentState(ba, blend, blendParams)` per-attachment (line 4000) — **not** a
  hardcoded factor pair.
- `git log` dates confirm the chronology directly: this file's creation commit
  (`346b3b50 test(Task 385): verify DualTextureEffect Alpha premultiplication, all 3 backends`, dated
  **2026-07-06**) predates both `fix(Task 870): real DepthBufferFunction + full stencil-test support on
  Vulkan` (**2026-07-08**) and `fix(Task 868): implement real per-Blend/BlendFunction mapping on
  Vulkan` (**2026-07-09**). This file's only other touch (`b6a00bc6`, Task 896, **2026-07-07**) also
  predates both. **The file was never revisited after Task 868/870 actually landed**, so its rationale
  for avoiding a full RGB check is describing a bug that has since been fixed, not a currently-true
  fact.
- Corroborated further: the identical stale rationale is duplicated verbatim in
  `tests/Microsoft/Xna/Framework/Graphics/DualTextureEffectTests.cpp`'s own header comment (line
  ~286-289: *"because Vulkan's BlendState support is known-fake/hardcoded (Task 868/870)"*) — the same
  Task-385-era comment was copy-pasted into a second file and neither was ever updated, strengthening
  confidence this is a genuine, unnoticed staleness rather than a one-off.
- Consequence: searched the entire `examples/vulkan_*.cpp` population for any pixel test that exercises
  a non-`Opaque` `BlendState` (`NonPremultiplied`/`Additive`/custom `BlendFunction`) end-to-end via GPU
  readback — **found none**. By contrast, EasyGL has 5 such tests
  (`easygl_blendstate_nonpremultiplied_test.cpp`, `_additive_test.cpp`, `_additive_golden_test.cpp`,
  `_separate_functions_test.cpp`, `_separate_factors_test.cpp`) and Bgfx has 3
  (`bgfx_blendstate_nonpremultiplied_test.cpp`, `_additive_test.cpp`, `_separate_functions_test.cpp`).
  Task 868's real per-`BlendState` Vulkan mapping — the very code this test's own comment says
  shouldn't be trusted — has **zero** GPU pixel-level regression coverage anywhere in this codebase.

### C++ correctness
No file-specific issues; standard pixel-test-family structure (`readCenter()`, `done_`/`result_` guard).

### Robustness
`SetDepthTestEnabled(false)` (line 73) is an appropriate simplification for a single full-screen quad.

## Detailed Findings

### F1 — Header comment's "Vulkan BlendState support is almost entirely fake" rationale is stale (fixed by Task 868, which predates this file's only two revisions... no, postdates them) and leaves real Vulkan `BlendState` blend-factor mapping with zero GPU pixel-test coverage
- Severity: MEDIUM
- Confidence: HIGH (independently read the current blend-mapping code and confirmed it is real
  per-`BlendState` mapping, not hardcoded; independently confirmed via `git log` timestamps that Task
  868/870 postdate every revision of this file; independently searched the whole `examples/` tree and
  confirmed zero Vulkan non-Opaque blend pixel tests exist, versus 5 on EasyGL and 3 on Bgfx)
- Category: test-coverage / stale-documentation
- Location/symbol: file header comment lines 6-18 (the "Why not the full check" rationale);
  corroborating duplicate in `tests/.../DualTextureEffectTests.cpp` lines ~286-289
- Evidence: see Logic/Testing section above — `FillBlendAttachmentState()`'s own comment explicitly
  says it *replaces* "the previous hardcoded BlendState.NonPremultiplied-equivalent equation," and this
  replacement (Task 868, commit dated 2026-07-09) is chronologically *after* this test file's creation
  (2026-07-06) and its only subsequent edit (2026-07-07). The rationale was accurate when written and
  was never revisited once the underlying bug it describes was fixed.
- Why it matters: two consequences. (1) Documentation-accuracy: a reader of this file today is told a
  false fact about the current state of the Vulkan backend's blend-state support, which could misdirect
  future debugging effort toward "the blend factors are still fake" when investigating an unrelated
  Vulkan rendering issue. (2) Real test-coverage gap: because the original (now-resolved) concern was
  never revisited, nobody added the fuller RGB-premultiplication blend test that Task 868's fix made
  safe to write — so Task 868's actual blend-factor/op mapping code (`ToVkBlendFactor`/`ToVkBlendOp`,
  `FillBlendAttachmentState`) has no GPU-level pixel verification anywhere in the Vulkan test
  population, only unit-level `FillGpuDrawParams()` checks (which only verify the CPU-side formula, not
  that the Vulkan blend equation itself is wired correctly end-to-end).
- FNA/XNA comparison: N/A for the specific staleness (a documentation/process issue), but the missing
  coverage means a real regression in `ToVkBlendFactor`/`ToVkBlendOp`'s XNA→Vulkan enum mapping (e.g. a
  transposed `SourceColor`/`InverseSourceColor` pair) would currently go undetected by any Vulkan
  example test.
- Related files: `tests/Microsoft/Xna/Framework/Graphics/DualTextureEffectTests.cpp` (duplicate stale
  comment); the EasyGL/Bgfx `*_blendstate_*_test.cpp` families (existing templates a Vulkan equivalent
  could mechanically adapt from, the same way `easygl_depth_bias_test.cpp` adapted from this shard's
  own `vulkan_depth_bias_test.cpp`).
- Suggested future action (not implemented by this audit — out of scope for an audit-only task): (a)
  update both comments to describe Task 868 as a resolved historical constraint rather than a present
  one; (b) add a `vulkan_blendstate_nonpremultiplied_test.cpp` (and ideally the other 2-4 variants)
  mirroring the EasyGL/Bgfx originals, now that the real per-`BlendState` mapping exists to test; (c)
  once such a test exists, consider whether this file's alpha-only scope should be widened back to the
  full RGB check the EasyGL sibling already does.

## Cross-File Observations

- This is the inverse case of `vulkan_basiceffect_textured_msaa_test.cpp` in this same shard: that
  file's "known bug, since fixed" narrative *was* kept accurate (independently confirmed against
  current code and git history); this file's "known bug, not yet fixed" narrative was *not* kept in
  sync once the bug actually got fixed. Both are the same underlying failure mode this audit was
  specifically asked to watch for (stale claims about "known bug"/"currently broken" that need
  re-verification against current code and git log, per this batch's task instructions) — one file
  passed that check, this one did not.
- The duplicate stale comment in `tests/.../DualTextureEffectTests.cpp` (outside this audit batch's
  scope, in the `tests-xna-graphics` shard) means this finding, if acted on, should be fixed in both
  places together.

## Missing or Weak Tests

See F1: the real gap is at the shard/subsystem level (no Vulkan `BlendState` pixel test exists at all),
not a defect in this specific file's own narrow, correctly-scoped alpha check.

## Positive Findings

- The alpha-only check itself is precisely reasoned and correctly implemented: it correctly identifies
  that `BlendState.Opaque`'s alpha-channel semantics (`ONE`/`ZERO`) are trivial enough that even a
  "hardcoded" implementation would get them right, making the alpha channel a safe thing to check
  regardless of whether the RGB blend-factor mapping was trustworthy at the time — good defensive test
  design, just resting on a premise that has since changed.
- The header comment is unusually explicit about its own reasoning and limitations (a good practice
  seen elsewhere in this codebase too, e.g. the EasyGL specular test's F1 in a prior audit batch) — it
  is exactly this transparency that made independently verifying (and refuting) the premise
  straightforward.

## Final Assessment

A correctly-implemented, narrowly-scoped test whose underlying justification has quietly gone stale.
The alpha check itself is not wrong, but the file (and its duplicate rationale in
`DualTextureEffectTests.cpp`) should be updated now that Task 868 has made a fuller Vulkan blend-state
pixel test both meaningful and safe to add — currently, real per-`BlendState` Vulkan blend-factor
mapping has no GPU-level regression coverage anywhere in this codebase.
