# Audit: examples/vulkan_dualtextureeffect_null_texture2_test.cpp

## Metadata

- Source file: `examples/vulkan_dualtextureeffect_null_texture2_test.cpp` (150 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `DualTextureEffect.Texture2` (slot 1) null-texture
  fallback, Vulkan backend
- File type: standalone `Game`-subclass executable (`VulkanDualTextureNullTexture2Test`),
  CTest-registered, "verify-only on Vulkan" per its own header (Task 387; the real bug found by
  Task 387 was Bgfx-only)
- XNA/FNA relevance: indirect/behavioral, same as its `Texture0` sibling
- Related production code: `src/Microsoft/Xna/Framework/Graphics/DualTextureEffect.cpp`
  (`FillGpuDrawParams()`: `if (texture2_) p.texture1 = &texture2_->GetBackend();`),
  `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp` (`DrawPrimitivesEx`'s
  `needsDualTex` branch, lines 7532-7541)

## Purpose

Mirror image of the `Texture0` test: draw 1 establishes a distinctive `Texture2`, draw 2 sets
`Texture2=nullptr` while keeping a real `Texture` (slot 0). Asserts the resulting pixel matches
"Texture(slot0) × white" and does not match draw 1's distinctive `Texture2` color.

## Executive Verdict

**Needs attention** — same conclusion as the `Texture0` sibling: the fallback behavior and
expected numeric result are correct, but the header comment misattributes the verified fallback
logic to the wrong backend function (F1, identical issue to the sibling file).

## Checklist Results

### Behavioral correctness
Re-derived against `dual_texture3d.frag.glsl`: `Texture2=null` falls back to `defaultWhiteView_`
(confirmed at `VulkanGraphicsBackend.cpp:7536`: `VkImageView v1 = vs1 ? vs1->GetVkImageView() :
defaultWhiteView_;`). `tex1 = kTex(80,40,120,255)/255 = (0.3137,0.1569,0.4706,1)`; `tex1.rgb*=2 →
(0.6274,0.3138,0.9412)`. `tex2 = white(1,1,1,1)` (fallback). `fragTint` default `(1,1,1,1)`.
`outColor.rgb = (0.6274,0.3138,0.9412)*1*1 = (0.6274,0.3138,0.9412)` → bytes `(160,80,240)`. This
matches the test's asserted expectation `Color(160,80,240,255)` (line 124) exactly — and notably
lands on the *same* numeric triple as the `Texture0` sibling's expectation, which makes sense
given the symmetric arithmetic (`2×80/255×255=160` either way `tex1`/`tex2` is assigned the
doubling factor, since multiplication is commutative and one operand is always the neutral
white regardless of which texture slot is null).

### Logic / Cross-file consistency
Same descriptor-set-cache verification as the `Texture0` sibling report:
`GetOrCreateDualTexDescSet()`'s cache key includes both `view0` and `view1`, so draw 2's
`defaultWhiteView_` in slot 1 is a genuinely distinct cache entry from draw 1's real `texPrev`
view in slot 1 — no stale-binding risk found.

## Detailed Findings

### F1 — Header comment names the wrong backend function (`DrawIndexedPrimitivesEx` instead of `DrawPrimitivesEx`)

- Severity: LOW
- Confidence: HIGH
- Category: documentation-accuracy / stale-comment
- Location/symbol: file header, lines 5-8: *"Verify-only on Vulkan (the real bug this task found
  and fixed was Bgfx-only): source-reading confirmed
  VulkanGraphicsBackend::DrawIndexedPrimitivesEx's dual-texture branch already falls back to
  `defaultWhiteView_` for `params.texture1` when null (`v1 = vs1 ? vs1->GetVkImageView() :
  defaultWhiteView_;`)"*
- Evidence: identical root cause to the `Texture0` sibling's F1. This test also draws via
  `dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2)` (the `VertexPositionTexture`
  overload), which resolves to `GraphicsDevice::DrawUserPrimitives(...)` at
  `GraphicsDevice.cpp:895-917`, calling `backend_->DrawPrimitivesEx(...)` (line 916) — not
  `DrawIndexedPrimitivesEx`. The quoted fallback line does exist verbatim, but at
  `VulkanGraphicsBackend.cpp:7534-7536` inside `DrawPrimitivesEx`, not inside
  `DrawIndexedPrimitivesEx` (whose textually-duplicated copy is at lines ~7771-7773 and is never
  reached by this non-indexed test).
- Why it matters: same as the sibling file — the specific function name cited as "source-read to
  confirm the fix" is wrong, even though the described behavior is real; a partial future
  refactor touching only one of the two duplicated branches could silently diverge from what this
  particular (non-indexed) test exercises.
- FNA/XNA comparison: N/A.
- Related files: `examples/vulkan_dualtextureeffect_null_texture0_test.cpp` (identical comment,
  identical inaccuracy — see that file's own report for the shared root-cause analysis).
- Suggested future action (not implemented by this audit): same as sibling — correct to
  `DrawPrimitivesEx`, or broaden to cover both duplicated branches explicitly.

## Cross-File Observations

- This file and `vulkan_dualtextureeffect_null_texture0_test.cpp` are near-identical in structure
  (same helper functions, same descriptor-set-cache reasoning, same comment bug) — reviewed
  together for efficiency, but each got its own report per the audit's per-file mandate.
- `git log` confirms Task 387's real fix was scoped to Bgfx only (`ab26c591 fix(Task 387):
  DualTextureEffect second texture null fallback missing on Bgfx`), consistent with this file's
  own "verify-only on Vulkan" framing — Vulkan's fallback was already correct before Task 387, and
  this test is a corroborating regression guard, not evidence of a Vulkan-side fix.

## Missing or Weak Tests

None beyond F1.

## Positive Findings

- Expected pixel value independently re-derived and matches exactly.
- Correctly reuses the same differential "not the previous draw's texture" technique as its
  `Texture0` sibling to guard against descriptor-cache staleness specifically, which this audit
  confirmed is a real (if currently correctly-handled) risk given how `GetOrCreateDualTexDescSet`
  caches by view/sampler identity.

## Final Assessment

Functionally sound, numerically verified test; only defect found is the same one-line stale
function-name reference as its `Texture0` sibling (F1).
