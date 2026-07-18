# Audit: examples/vulkan_skinnedeffect_combined_test.cpp

## Metadata

- Source file: `examples/vulkan_skinnedeffect_combined_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — cross-backend `SkinnedEffect` capstone (identity +
  single-bone + two-bone blend), Vulkan backend
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_skinnedeffect_combined …)` / `cna_register_backend_test(NAME
  Vulkan_SkinnedEffect_Combined …)`, `cmake/Tests/VulkanTests.cmake:337-340`, Task 409).
- XNA/FNA relevance: direct — `SkinnedEffect.SetBoneTransforms`, `WeightsPerVertex`,
  `EnableDefaultLighting`.
- FNA reference: `Graphics/Effect/StockEffects/SkinnedEffect.cs` (`SetBoneTransforms`,
  `WeightsPerVertex`, `EnableDefaultLighting` → `EffectHelpers.EnableDefaultLighting`),
  `Graphics/Effect/StockEffects/HLSL/SkinnedEffect.fx` (`Skin()` weighted-blend function),
  `Graphics/Effect/StockEffects/EffectHelpers.cs` (`SetMaterialColor`: ambient/emissive
  pre-combination formula).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/SkinnedEffect.cpp`
  (`FillGpuDrawParams()` lines 318-410), `src/CNA/Internal/Backends/Vulkan/shaders/
  skinned3d_vertexlit.{vert,frag}.glsl` and `skinned3d.{vert,frag}.glsl` (both lighting variants),
  `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp` (`FillExtPushConst()` lines
  3575-3592, `preferVertexLit` dispatch lines 7425-7427/7663-7665).

## Purpose

Renders 3 adjacent quads from a single 18-vertex/6-triangle draw call, each carrying different
per-vertex bone weight/index data against a shared 4-matrix bone palette
(`identity, +0.75, +1.0, +2.0` translations along X): quad A uses only bone 0 (identity, stays at
its authored `x∈[-1.0,-0.5]`), quad B uses only bone 1 (`+0.75` translation, moving to
`x∈[-0.25,0.25]`), quad C blends bones 2+3 at weight 0.5/0.5 (`WeightsPerVertex=2`, average
translation `+1.5`, moving to `x∈[0.5,1.0]`). Samples one pixel per quad at NDC `x≈-0.75/0.00/
+0.75` and asserts each reads "red-dominant" (texture is solid red). `EnableDefaultLighting()` is
called, engaging all 3 XNA default directional lights plus a nonzero `AmbientLightColor`.

## Executive Verdict

**Needs attention** — the geometric/positional half of the test (does bone-weight blending
actually move vertices to the mathematically-correct blended position) is genuinely load-bearing
and was independently confirmed correct, a real strength. However, its colour assertions are loose
enough (`R > G && R > 50`, checked at F1) that they cannot detect a confirmed, real defect in this
exact code path: `SkinnedEffect`'s `AmbientLightColor` and `EmissiveColor` are silently dropped on
the Vulkan backend (both properties this test's own `EnableDefaultLighting()` call sets to nonzero
values), and this test would pass identically whether or not that defect existed.

## Checklist Results

### API / XNA / FNA parity
`fx.SetBoneTransforms(bones)` / `fx.setWeightsPerVertexProperty(2)` / `fx.EnableDefaultLighting()`
all match FNA's `SkinnedEffect` API surface directly. Confirmed
`SkinnedEffect::EnableDefaultLighting()` (`SkinnedEffect.cpp:222-240`) sets a nonzero
`AmbientLightColor` (`(0.0533, 0.0988, 0.1820)`), matching FNA's own
`EffectHelpers.EnableDefaultLighting` rig values.

### Behavioral correctness
Independently re-derived the expected blended X-ranges from the bone/weight data and confirmed the
sample points land unambiguously inside each intended quad, not merely "somewhere red":
- Quad A (`appendQuad(…, 1,0,0,0, 0,0,0,0)`): weight 1.0 on bone index 0 (identity) →
  authored range `x∈[-1.0,-0.5]` unchanged. Sample at `W/8` → NDC `x≈-0.75` — inside.
- Quad B (`appendQuad(…, 1,0,0,0, 1,0,0,0)`): weight 1.0 on bone index 1
  (`CreateTranslation(0.75,0,0)`) → range shifts to `[-0.25,0.25]`. Sample at `W/2` → NDC
  `x≈0.00` — inside.
- Quad C (`appendQuad(…, 0.5,0.5,0,0, 2,3,0,0)`, `WeightsPerVertex=2`): bones 2
  (`CreateTranslation(1.0,0,0)`) and 3 (`CreateTranslation(2.0,0,0)`) each weighted 0.5 →
  average translation `+1.5` → range shifts to `[0.5,1.0]`. Sample at `7*W/8` → NDC `x≈+0.75` —
  inside.
  This means a broken blend (e.g. using only the first weight/index pair and ignoring
  `WeightsPerVertex`, which would leave quad C at bone 2's own `+1.0` translation, range
  `[0,0.5]`) would place the `x≈+0.75` sample **outside** quad C entirely, landing on the green
  clear colour instead of the red texture — a genuinely discriminating check, not merely "is it
  red somewhere on screen."
- Confirmed `weightsPerVertex_` is threaded through to the shader's `>=2.0`/`>=4.0` gating
  (`skinned3d_vertexlit.vert.glsl`: `if (weightsPerVertex >= 2.0) skinMat += bones[y]*w.y;`), so
  this test's `WeightsPerVertex=2` setting is genuinely exercised, not silently ignored.

### Logic
Traced `SkinnedEffect::FillGpuDrawParams()` (`SkinnedEffect.cpp:318-410`) and the Vulkan
`skinned3d_vertexlit.{vert,frag}.glsl` pair (the pipeline actually selected here, since
`preferPerPixelLighting_` defaults to `false` and `FillExtPushConst`'s shared push-constant layout
only carries an `ambientColor` field, never `emissiveColor`) and found: `SkinnedEffect::
FillGpuDrawParams()` never assigns `p.ambientColor` anywhere (confirmed via `grep -n
"ambientColor" SkinnedEffect.cpp` — zero matches), leaving it at `GpuDrawParams`'s
zero-initialized default (`float ambientColor[3] = {0,0,0};`,
`IGraphicsBackend.hpp:369`). Instead it writes the FNA-style pre-combined
`ambient*diffuse+emissive` value into `p.emissiveColor` (lines 335-338, mirroring the *comment*
that says "matches FNA's colour upload"). But no code path anywhere forwards `params.emissiveColor`
into either `skinned3d.frag.glsl` or `skinned3d_vertexlit.frag.glsl`'s push constant or `FogParams`
UBO — neither shader declares an emissive field at all. Net effect: **on Vulkan, `SkinnedEffect.
AmbientLightColor` and `.EmissiveColor` are both silently no-ops** — this is a real, confirmed
defect, independently traced end-to-end by this audit (not merely inferred from a comment).

### Robustness
See F1 — the specific defect above cannot be observed through this test's own assertions, because
the 1×1 texture is pure red (`(255,0,0,255)`), which forces `outColor.g`/`.b` to exactly zero
*regardless* of the lighting formula's correctness (any positive-valued `litRGB.r` component,
whether ambient contributes or not, multiplies through to a positive red and zero green/blue),
making `R > G` trivially true and `R > 50` true across a wide range of possible (correct or
incorrect) lighting outcomes.

## Detailed Findings

### F1 — Test's loose "reddish" colour assertions cannot detect the confirmed SkinnedEffect AmbientLightColor/EmissiveColor-dropped-on-Vulkan defect, despite calling EnableDefaultLighting()

- Severity: MEDIUM
- Confidence: HIGH (independently traced `SkinnedEffect::FillGpuDrawParams()`,
  `VulkanGraphicsBackend::FillExtPushConst()`, and both `skinned3d*.frag.glsl` variants line by
  line; confirmed neither shader has any field the CPU side could even feed an emissive/ambient
  value into beyond the unused-by-SkinnedEffect `ambientColor` push-constant slot)
- Category: test-coverage / correctness-of-test (root defect is in production code, out of this
  file's own scope, but this file is the one place in the Vulkan shard that calls
  `EnableDefaultLighting()` on `SkinnedEffect` and could plausibly have caught it)
- Location/symbol: `aOk`/`bOk`/`cOk` assertions (lines 122-124): `(xPx.getRProperty() >
  xPx.getGProperty() && xPx.getRProperty() > 50)`; root cause in
  `src/Microsoft/Xna/Framework/Graphics/SkinnedEffect.cpp` (no `p.ambientColor` assignment
  anywhere in `FillGpuDrawParams()`) and `src/CNA/Internal/Backends/Vulkan/shaders/
  skinned3d_vertexlit.frag.glsl` / `skinned3d.frag.glsl` (neither declares an emissive-colour
  uniform in any UBO or push constant it reads).
- Evidence: manual worked calculation for quad A's expected pixel with `EnableDefaultLighting()`'s
  real light rig (`DirectionalLight0.Direction=(-0.5265,-0.5736,-0.6275)`,
  `.DiffuseColor=(1,0.9608,0.8078)`; lights 1/2 point away from the quad's `+Z` normal and
  contribute nothing): with ambient genuinely applied, `litRGB ≈ (0.053+0.627, 0.099+0.602,
  0.182+0.508) ≈ (0.680, 0.701, 0.690)`; with ambient silently dropped (the actual current Vulkan
  behavior), `litRGB ≈ (0.627, 0.602, 0.508)`. Multiplying either result by the texture's `(1,0,0)`
  red channel and zeroing green/blue (`tex.rgb=(1,0,0)`) yields `R≈160` either way with `G=B=0` —
  both numbers comfortably satisfy `R>G && R>50`, so the test's own PASS/FAIL outcome is *identical*
  whether or not the ambient-drop defect is present.
- Why it matters: this file is explicitly framed (per its own header comment referencing
  `easygl_skinnedeffect_combined_test.cpp`) as *the* cross-backend "capstone" test meant to
  validate `SkinnedEffect`'s combined behavior, and it is the one Vulkan-shard file that actually
  invokes `EnableDefaultLighting()` (which sets a materially nonzero `AmbientLightColor`) — yet due
  to the pure-red texture choice, it provides zero actual signal on whether ambient/emissive
  lighting is correctly forwarded. A regression that completely zeroed out ambient (as is in fact
  already the case here) or a hypothetical regression that doubled it would both pass unnoticed.
- FNA/XNA comparison: FNA's `SkinnedEffect.fx` (via `EffectHelpers.SetMaterialColor` +
  `Lighting.fxh`'s `ComputeLights`) applies `(sum(diffuse directional light) * DiffuseColor) +
  EmissiveColor`, where the uploaded `EmissiveColor` parameter already includes
  `ambient*diffuse`. CNA's Vulkan `SkinnedEffect` path neither reproduces this formula's ambient
  term nor forwards its emissive term — a genuine XNA-behavior gap, independently confirmed by
  this audit beyond what any comment in this batch's files already claimed for the *sibling*
  fog test (see that file's own audit report for the same defect, reached via a different,
  narrower test design that deliberately avoids relying on ambient/emissive).
- Related files: `src/Microsoft/Xna/Framework/Graphics/SkinnedEffect.cpp`,
  `src/CNA/Internal/Backends/Vulkan/shaders/skinned3d_vertexlit.frag.glsl`,
  `src/CNA/Internal/Backends/Vulkan/shaders/skinned3d.frag.glsl`,
  `examples/vulkan_skinnedeffect_fog_test.cpp` (audited separately in this same batch; its own
  header comment already flags "AmbientLightColor... always 0 on Vulkan" as a known, unreported
  gap, which this audit independently confirmed as accurate).
- Suggested future action (not implemented by this audit): fix `SkinnedEffect::
  FillGpuDrawParams()` to populate `p.ambientColor` from `ambientLightColor_` (mirroring
  `BasicEffect::FillGpuDrawParams()`'s existing correct pattern) and add an `emissiveColor` field
  to the skinned3d push-constant/UBO contract (mirroring `lit_textured3d.frag.glsl`'s
  `LitLightParams.emissiveColor_pad` UBO field) — then strengthen this test (or add a new
  ambient/emissive-specific one) using a non-primary-colour texture or a black-texture-plus-
  ambient-only scenario so the assertion can actually distinguish "ambient reached the shader" from
  "ambient was dropped."

## Missing or Weak Tests

See F1. Additionally, no test in this Vulkan shard directly isolates `SkinnedEffect.
AmbientLightColor`/`EmissiveColor` the way, e.g., `easygl_basiceffect_specular_test.cpp` isolates
`SpecularColor` for `BasicEffect` (per this audit's cited example report) — a targeted
ambient/emissive test for `SkinnedEffect` would be a natural, valuable addition, and would very
likely fail against the current Vulkan implementation, correctly surfacing F1's root cause.

## Positive Findings

- The bone-weight-blend positional verification is genuinely strong: this audit's independent
  re-derivation confirms a broken `WeightsPerVertex` gate or a broken weighted-average blend would
  visibly move the sampled quad's geometry out from under its sample point, not just change its
  colour — a real, load-bearing geometric assertion, not a decorative one.
- Correctly threads `WeightsPerVertex=2` through to the shader's conditional weight-summing gate,
  confirmed by direct inspection of `skinned3d_vertexlit.vert.glsl`.
- Applies the same Task 896 `RasterizerState::CullNone` fix as its sibling files in this shard,
  with an accurate inline citation.

## Final Assessment

Strong on geometry, weak on lighting-fidelity coverage. The bone-blend positional logic this file
was named for is correctly and rigorously tested. But its incidental use of `EnableDefaultLighting
()` combined with a pure-red texture creates only the appearance of lighting coverage — this audit
traced the actual data flow and confirmed the test cannot distinguish correct from silently-broken
ambient/emissive forwarding, which independently turns out to be broken on this backend right now
(F1).
