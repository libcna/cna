# Audit: examples/vulkan_dualtextureeffect_null_texture0_test.cpp

## Metadata

- Source file: `examples/vulkan_dualtextureeffect_null_texture0_test.cpp` (150 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `DualTextureEffect.Texture` (slot 0) null-texture
  fallback, Vulkan backend
- File type: standalone `Game`-subclass executable (`VulkanDualTextureNullTexture0Test`),
  CTest-registered, "verify-only" per its own header (Task 386)
- XNA/FNA relevance: indirect/behavioral — real XNA/FNA does not define a `Texture=null` render
  contract for stock effects at the API level; this is a CNA robustness behavior
  (fall back to opaque white rather than sampling garbage/reusing stale GPU state)
- Related production code: `src/Microsoft/Xna/Framework/Graphics/DualTextureEffect.cpp`
  (`FillGpuDrawParams()` lines 248-275: `if (texture_) p.texture0 = &texture_->GetBackend();` —
  `p.texture0` stays at its default/null when `texture_` is null),
  `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp` (`DrawPrimitivesEx`'s
  `needsDualTex` branch, lines 7532-7541, and its duplicate in `DrawIndexedPrimitivesEx`,
  lines ~7770-7778)

## Purpose

Two-check regression test: draw 1 establishes a "previous draw" state with a distinctive,
non-fallback `Texture` (slot 0); draw 2 sets `Texture=nullptr` while keeping a real `Texture2`.
The test asserts the resulting pixel color matches "white × Texture2" (the intended null
fallback) and explicitly asserts it does **not** match draw 1's distinctive color (proving no
stale GPU descriptor/state leak between draws).

## Executive Verdict

**Needs attention** — the underlying fallback behavior and its numeric result are correct and
independently re-derived to match exactly, but the file's own header comment misidentifies which
backend function implements the behavior it is testing (see F1); this is a documentation
accuracy issue, not a functional defect.

## Checklist Results

### API / XNA / FNA parity
N/A at the XNA level (this is a CNA robustness/`NOXNA` behavior around null-texture handling, not
an XNA-specified contract). Consistent with the analogous EasyGL/Bgfx sibling tests referenced in
the header comment.

### Behavioral correctness
Re-derived the expected pixel value against the live `dual_texture3d.frag.glsl`
(`tex1.rgb *= 2.0; outColor = tex1 * tex2 * fragTint;`, no fog in this test so
`fragFogFactor=1`→ no-op `mix`): `Texture=null` falls back to `defaultWhiteView_` (confirmed at
`VulkanGraphicsBackend.cpp:7535`: `VkImageView v0 = vs0 ? vs0->GetVkImageView() :
defaultWhiteView_;`), so `tex1=white(1,1,1,1)`, `tex1.rgb*=2 → (2,2,2)`. `tex2 = kTex2(80,40,120,
255)/255 = (0.3137,0.1569,0.4706,1)`. `fragTint` = `DualTextureEffect`'s default `DiffuseColor`
`(1,1,1)*alpha(1) = (1,1,1,1)`. `outColor.rgb = (2*0.3137, 2*0.1569, 2*0.4706) =
(0.6274,0.3138,0.9412)` → bytes `(160,80,240)`. This matches the test's asserted expectation
`Color(160,80,240,255)` (line 124) to within rounding — an exact, non-coincidental match, not
just "close enough" under the `±20` tolerance.

### Logic
The descriptor-set cache key used by `GetOrCreateDualTexDescSet()`
(`VulkanGraphicsBackend.cpp:3859-3862`) hashes `view0`/`view1`/`sampler0`/`sampler1` together, so
draw 2's `defaultWhiteView_` produces a genuinely different cache key than draw 1's distinctive
`texPrev` view — confirmed this is not a frame-index-only cache that could silently reuse draw
1's bound images for draw 2.

### Robustness
The second assertion (`!colourMatch(got, kDistinctivePrev)`, line 127) is a well-chosen
differential check: it doesn't just assert *a* plausible color, it actively rules out the
specific failure mode (stale/leaked previous-draw texture binding) that a caching bug would
produce.

## Detailed Findings

### F1 — Header comment names the wrong backend function (`DrawIndexedPrimitivesEx` instead of `DrawPrimitivesEx`)

- Severity: LOW
- Confidence: HIGH
- Category: documentation-accuracy / stale-comment
- Location/symbol: file header, lines 6-8: *"source-reading confirmed
  VulkanGraphicsBackend::DrawIndexedPrimitivesEx's dual-texture branch already falls back to
  `defaultWhiteView_` for `params.texture0` when null (`v0 = vs0 ? vs0->GetVkImageView() :
  defaultWhiteView_;`)"*
- Evidence: this test draws via `dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2)`
  using the `VertexPositionTexture` overload. Traced the call chain:
  `GraphicsDevice::DrawUserPrimitives(PrimitiveType, const VertexPositionTexture*, int, int)`
  (`GraphicsDevice.cpp:895-917`) calls `backend_->DrawPrimitivesEx(...)` (line 916), **not**
  `DrawIndexedPrimitivesEx`. `DrawIndexedPrimitivesEx` (`VulkanGraphicsBackend.cpp:7595`) is a
  separate, textually-duplicated function reached only via indexed draw calls
  (`DrawIndexedPrimitives`/`DrawUserIndexedPrimitives`), which this test never calls. The literal
  fallback code the comment quotes (`v0 = vs0 ? vs0->GetVkImageView() : defaultWhiteView_;`) does
  exist verbatim, but in `DrawPrimitivesEx` (`VulkanGraphicsBackend.cpp:7533-7536`), with an
  identical copy also present in `DrawIndexedPrimitivesEx` (lines ~7770-7773).
- Why it matters: the comment's factual claim about *which function* was read to confirm the fix
  is wrong, even though the underlying behavioral claim (fallback exists) is correct — because
  both functions independently duplicate the same fallback logic. A future refactor that touched
  only `DrawPrimitivesEx` (the function actually exercised by this specific test) while leaving
  `DrawIndexedPrimitivesEx` unchanged (or vice versa) could silently break what this test
  exercises while a reader trusts the comment's claim that "`DrawIndexedPrimitivesEx`" was the
  verified function.
- FNA/XNA comparison: N/A (test-authoring/comment-accuracy issue, not an XNA/FNA behavior
  question).
- Related files: `examples/vulkan_dualtextureeffect_null_texture2_test.cpp` has the identical
  comment and the identical inaccuracy (see that file's own report).
- Suggested future action (not implemented by this audit): correct the comment to name
  `DrawPrimitivesEx` (the function this non-indexed test actually exercises), or broaden the
  wording to "both `DrawPrimitivesEx` and `DrawIndexedPrimitivesEx`'s duplicate dual-texture
  branches."

## Cross-File Observations

- The identical F1 inaccuracy appears in the sibling `vulkan_dualtextureeffect_null_texture2_test.cpp`
  (same header comment, copy-pasted).
- `git log --oneline` confirms Task 386 (this file) and Task 387 (`null_texture2`'s real Bgfx-only
  bug fix) are genuine, non-fabricated task references (`22566416 test(Task 386): verify
  DualTextureEffect first texture null behavior, all 3 backends`, `ab26c591 fix(Task 387):
  DualTextureEffect second texture null fallback missing on Bgfx`).

## Missing or Weak Tests

None beyond F1's documentation nit — the two assertions (correct fallback value + no state leak)
are the right pair for this kind of regression test.

## Positive Findings

- The expected pixel value (`160,80,240`) was independently re-derived from the live shader
  arithmetic and matches exactly, not just within tolerance.
- The negative assertion (`!colourMatch(got, kDistinctivePrev)`) is a deliberately-chosen
  differential check that would catch a specific class of caching bug a simple positive
  assertion could miss.
- Confirmed the descriptor-set cache correctly keys on the actual bound image views, so this
  test's premise (that a caching bug *could* leak the previous draw's texture) is a real risk the
  test is actually capable of catching, not a strawman scenario.

## Final Assessment

Functionally sound test with a verified-correct expected value; the only defect found is a
one-line comment inaccuracy (F1) that misattributes the verified fallback code to the wrong
function name.
