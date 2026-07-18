# Audit: examples/vulkan_basiceffect_texture_vertexcolor_enabled_test.cpp

## Metadata

- Source file: `examples/vulkan_basiceffect_texture_vertexcolor_enabled_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — Vulkan backend `BasicEffect.TextureEnabled` +
  `.VertexColorEnabled` pixel test (Task 367), stride-24 `VertexPositionColorTexture` / `colored_textured3d`
  pipeline
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_test_vulkan_basiceffect_texture_vertexcolor_enabled` / `Vulkan_BasicEffect_TextureVertexColorEnabled`,
  `cmake/Tests/VulkanTests.cmake:509-511`)
- XNA/FNA relevance: direct — `BasicEffect.TextureEnabled`, `.VertexColorEnabled`, shader-index-7 path
  (`VSBasicTxVcNoFog`/`PSBasicTxNoFog`)
- FNA reference: FNA's stock-effect shader-index dispatch selecting `VSBasicTxVc*` when both flags are set
- Related production code: `src/CNA/Internal/Backends/Vulkan/shaders/colored_textured3d.vert.glsl`
  (`fragTint = (vertexColorEnabled>0.5) ? inColor*diffuseColor : diffuseColor`) and
  `colored_textured3d.frag.glsl` (`outColor = tex*fragTint`).

## Purpose

7-check pixel test proving `TextureEnabled=true` AND `VertexColorEnabled=true` together produce
`TextureColor*VertexColor*DiffuseColor*Alpha` on the stride-24 pipeline — the header notes this is the one
Vulkan pipeline that, unlike the EasyGL/Bgfx siblings, already computed `fragTint` correctly *before* this task
(confirmed here by pixel readback, not a fix). Uses three distinctly-valued colors (`kTexColor(200,100,50)`,
`kVertexColor(150,200,100)`, `kDiffuse(0.8,0.4,0.6)`) and disproves six plausible partial-product variants
(texture×diffuse only, vertex×diffuse only, texture×vertex only, texture alone, vertex alone, diffuse alone) in
addition to asserting the correct triple product.

## Executive Verdict

**Healthy** — the correct triple product and all six disproof targets were independently re-derived by hand and
confirmed exactly correct against the actual `colored_textured3d.vert.glsl`/`.frag.glsl` shader source, which
matches the header's own claim precisely.

## Checklist Results

### API / XNA / FNA parity
`setTextureEnabledProperty`/`setTextureProperty`, and `fx.VertexColorEnabled = true` (line 104) — note this is a
direct public-field assignment, not a `setVertexColorEnabledProperty(...)` setter call, matching this project's
own established convention that `BasicEffect::VertexColorEnabled` is a public field (`BasicEffect.hpp:48`,
`bool VertexColorEnabled = false;`, matching FNA's own public field, not a C#-property-backed member) — correct
usage, not an API-surface inconsistency.

### Behavioral correctness — independent re-derivation
`kTexColor(200,100,50)`, `kVertexColor(150,200,100)`, `kDiffuse(0.8,0.4,0.6)`, all normalized to `[0,1]` before
the triple multiply and rescaled by `255` at the end (equivalent to multiplying the raw `0-255` byte values and
dividing by `255` once per channel, since two of the three factors are colors and one is a float3):

- `R: 200*150*0.8/255 = 24000/255 ≈ 94.12 ≈ 94`
- `G: 100*200*0.4/255 = 8000/255 ≈ 31.37 ≈ 31`
- `B: 50*100*0.6/255 = 3000/255 ≈ 11.76 ≈ 12`

— **matches `kExpected(94,31,12)` exactly**, and matches the shader's actual formula: `colored_textured3d.
vert.glsl:36` (`fragTint = (vertexColorEnabled>0.5) ? inColor*diffuseColor : diffuseColor`, confirmed the
ternary's `true` branch is taken since this test sets `VertexColorEnabled=true`), `colored_textured3d.
frag.glsl:31` (`outColor = tex * fragTint`).

All six disproof targets independently re-checked:
- `kTextureDiffuseOnly(160,40,30)` — `200*0.8=160,100*0.4=40,50*0.6=30`, confirmed = texture×diffuse alone
  (vertex-color-ignored bug target).
- `kVertexDiffuseOnly(120,80,60)` — `150*0.8=120,200*0.4=80,100*0.6=60`, confirmed = vertex×diffuse alone
  (texture-ignored bug target).
- `kTextureVertexOnly(118,78,20)` — `200*150/255≈117.6≈118, 100*200/255≈78.4≈78, 50*100/255≈19.6≈20`, confirmed
  = texture×vertex alone (diffuse-ignored bug target).
- `kTextureOnly(200,100,50)`, `kVertexOnly(150,200,100)`, `kDiffuseOnly(204,102,153)` (`0.8*255=204` etc.) — all
  confirmed as the three single-factor-only targets.

All seven checks (1 correct-value assertion + 6 disproofs) independently verified consistent and correctly
computed.

### Logic
This is one of the most thorough multiply-order tests in the batch — six distinct partial-product hypotheses are
explicitly ruled out, not just the two most obvious ones, giving strong confidence the shader genuinely computes
a three-way product rather than any pairwise shortcut.

### C++ correctness
No issues; single fresh `BasicEffect`/`Texture2D` per `Draw()` call, standard 20-iteration retry loop.

### Robustness
N/A — no malformed-input path.

### Testing
Comprehensive for its stated scope (multiply-order/completeness of the texture×vertexColor×diffuse product).

## Detailed Findings

No HIGH/CRITICAL/MEDIUM findings — every asserted and disproved value independently re-derived and confirmed
correct.

### F1 — Header's claim that this pipeline was already correct "before this task" is plausible but not independently cross-checked against `git log`/git blame in this audit

- Severity: LOW
- Confidence: MEDIUM
- Category: documentation / historical-claim
- Location/symbol: header comment lines 10-12 ("Unlike EasyGL and Bgfx, Vulkan's stride-24
  `colored_textured3d.vert.glsl` already computed `fragTint`... correctly before this task — confirmed here by
  pixel readback, not fixed.")
- Evidence: this audit confirmed the *current* shader computes the formula correctly (matches all 7 checks
  exactly), which is consistent with the claim, but did not run `git log -p` on `colored_textured3d.vert.glsl`
  specifically to confirm the formula was unchanged by Task 367's own commit (as opposed to having been fixed by
  it and the comment simply being imprecise about "already" vs. "as a side effect of this task").
- Why it matters: very low priority — the claim is about test provenance/history, not about the currently-shipped
  behavior, which is independently confirmed correct regardless of which commit introduced it.
- Suggested future action: none required; noted for completeness since the audit brief calls for treating
  historical claims as needing verification, and this one specifically was not fully chased to its git origin
  given the higher-priority findings elsewhere in this batch.

## Cross-File Observations

- Shares `kTexColor`/`kDiffuse` values and the `(160,40,30)` product with
  `vulkan_basiceffect_texture_enabled_test.cpp` (reused here as `kTextureDiffuseOnly`) — cross-verified
  consistent in both files' independent reports.
- `RasterizerState::CullNone` (line 123, "Task 896" comment) confirmed accurate via the same cross-check
  performed for every sibling file in this batch.
- Confirms `colored_textured3d.vert.glsl`'s `fragTint` ternary is the same "vertex-color × diffuse when enabled,
  else diffuse alone" pattern already independently confirmed for `colored3d.vert.glsl`/`textured3d.vert.glsl`
  during this batch's investigation of the shared fog-UBO bundle (see the two fog-test reports in this batch) —
  consistent shader-family design across all four pipelines sharing that bundle.

## Missing or Weak Tests

- No case tests `Alpha<1.0` (same gap as the `texture_enabled` sibling file).
- No case tests `VertexColorEnabled=true` with `TextureEnabled=false` (i.e. vertex color alone, without texture)
  — a reasonable adjacent case with no dedicated coverage found in this shard.

## Positive Findings

- The most rigorous multiply-order disproof set in this batch (6 distinct wrong-formula targets ruled out, not
  just 1-2) — a strong model for how to test a 3-factor product formula.
- The header's own historical claim ("already correct before this task, confirmed not fixed") is a useful,
  honest distinction from most sibling files' "we fixed X" narratives, and is consistent with what this audit
  independently found in the current source.

## Final Assessment

A thorough, correctly-derived, well-evidenced test of the texture×vertexColor×diffuse triple product — the
strongest multiply-order disproof coverage in this batch. No corrective action needed.
