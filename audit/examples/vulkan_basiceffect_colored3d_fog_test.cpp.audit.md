# Audit: examples/vulkan_basiceffect_colored3d_fog_test.cpp

## Metadata

- Source file: `examples/vulkan_basiceffect_colored3d_fog_test.cpp` (167 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `BasicEffect` linear-fog pixel test, `colored3d` pipeline
  (stride-16 `VertexPositionColor`, `TextureEnabled=false`, `VertexColorEnabled=false`)
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_basiceffect_colored3d_fog …)` /
  `cna_register_backend_test(NAME Vulkan_BasicEffect_Colored3D_Fog …)`,
  `cmake/Tests/VulkanTests.cmake:619-621`).
- XNA/FNA relevance: direct — `BasicEffect.FogEnabled`/`FogColor`/`FogStart`/`FogEnd` with
  `LightingEnabled=false`.
- FNA reference: `Graphics/Effect/StockEffects/EffectHelpers.cs::SetWorldViewProjAndFog()` (real
  view-space `FogVector`, sensitive to `World`/`View`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp` (fog handling shares
  the same `IEffectFog` pattern as `AlphaTestEffect`), `src/CNA/Internal/Backends/Vulkan/shaders/
  colored3d.vert.glsl` (fog factor from raw `inPos.z`), `colored3d.frag.glsl` (`mix(fogColor, fragColor,
  fragFogFactor)`), `VulkanGraphicsBackend.cpp` (shared `FogParams` dynamic UBO, set=0 binding=1, added by
  Task 899 for `colored3d`/`textured3d`/`colored_textured3d`).
- git corroboration: `c2386302`/`beb83b20` "feat(Task 899): add fog to Vulkan's
  colored3d/textured3d/colored_textured3d/dual_texture3d/skinned3d (Vulkan core scope)" — matches this
  file's header comment exactly, including the stated reason ("`colored3d` was one of 5 Vulkan pipelines
  sharing `FillExtPushConst()`'s fully-packed 128-byte push constant — zero spare bytes for fog").

## Purpose

Proves `BasicEffect`'s linear fog blend on the `colored3d` (untextured, no-vertex-color) pipeline via a
3-case sweep: fog disabled (pure material color), 50% fog (Z=0.5 between `FogStart=0`/`FogEnd=1`), and
full fog (`FogEnd=0.5`, `Z=0.9` — beyond the fog end, fully fogged). Uses a green vertex color
(`vc(0,255,0,255)`) deliberately paired with `VertexColorEnabled=false` to prove that channel is correctly
*ignored* while isolating the fog blend to a pure blue `DiffuseColor`.

## Executive Verdict

**Healthy** (with the same shared, already-documented fog-formula scope limitation noted for
`vulkan_alphatest_fog_test.cpp` in this batch — see F1 below, which cross-references rather than repeats
that file's full derivation). All three pixel assertions were independently re-derived from the actual
`colored3d.vert.glsl`/`colored3d.frag.glsl` formula and match exactly.

## Checklist Results

### API / XNA / FNA parity
`setLightingEnabledProperty(false)`, `setTextureEnabledProperty(false)`, `fx.VertexColorEnabled = false`
(line 97 — **note**: `VertexColorEnabled` is a public field on `BasicEffect`, not a
`getVertexColorEnabledProperty()`/`setVertexColorEnabledProperty()` pair — see F2),
`setDiffuseColorProperty`, `setFogEnabledProperty`/`setFogColorProperty`/`setFogStartProperty`/
`setFogEndProperty` are all correctly-named FNA `BasicEffect` members. `setWorldProperty`/
`setViewProperty`/`setProjectionProperty` are explicitly set to `Matrix::getIdentityProperty()` (lines
92-94) — better practice than the sibling `vulkan_alphatest_fog_test.cpp`, which relies on an implicit
default (see that file's F2).

### Behavioral correctness
Re-derived against `colored3d.vert.glsl:40-42`/`colored3d.frag.glsl:19`:
`fragFogFactor = clamp((fogEnd - inPos.z)/(fogEnd - fogStart), 0, 1)`; `outColor.rgb = mix(fogColor,
fragColor, fragFogFactor)` where `fragColor = diffuseColor` (since `VertexColorEnabled=false`, line 38 of
the vert shader takes the `else` branch).
- (a) `fogEnabled=false` → `fragFogFactor` forced to `1.0` (vert shader line 40's ternary: fog UBO's
  `fogColorEnabled.w` gates this) → `mix(_, blue, 1) = blue` = `(0,0,255)` — matches `kBlue`.
- (b) `Z=0.5`, `FogStart=0`, `FogEnd=1`, `FogColor=red`: `factor=clamp((1-0.5)/1,0,1)=0.5` →
  `mix(red,blue,0.5) = (0.5,0,0.5)*255 = (128,0,128)` — matches the file's own expected
  `Color(128,0,128,255)` exactly.
- (c) `FogEnd=0.5`, `Z=0.9`: `factor=clamp((0.5-0.9)/0.5,0,1)=clamp(-0.8,0,1)=0` → `mix(red,blue,0)=red`
  = `(255,0,0)` — matches `kRed`.
All three independently confirmed exact.

### Logic
`renderQuad()` correctly re-sets `RasterizerState::CullNone` and `BlendState::Opaque` inside the retry
loop each iteration (lines 117-120), consistent with the rest of this shard's pattern (state is not
expected to survive `dev.Clear()`, so re-applying per iteration is the safe choice, not redundant
defensiveness).

### C++ correctness
`tol=30` in `matches()` (line 73, default parameter) is noticeably looser than the `±8` used by the
`AlphaTestEffect` fog test in this same batch — reasonable given fog-blended values here involve a full
0-255 color mix rather than a near-exact texture sample, but worth noting the tolerance is wide enough
that a ~10% formula error (e.g. a fog factor off by 0.1) could still slip through undetected on case (b)
specifically (128±30 spans 98-158, a fairly wide window for a supposedly-exact 50% mix). Rated LOW/INFO
since case (b)'s independently-recomputed exact value (128,0,128) matches the assertion to zero
discrepancy in this audit's own derivation — the looseness is unused headroom here, not a sign of an
actual imprecision problem.

### Robustness
The green-vertex-color-with-`VertexColorEnabled=false` setup (line 107) is a well-chosen negative check:
if the `colored3d` shader's `vertexColorEnabled` gate were broken (always multiplying in vertex color
regardless of the flag), the resulting color would show a visible green tint contamination that the exact
`kBlue`/`kRed`/`(128,0,128)` checks would catch.

### Testing
Three genuinely discriminating assertions (disabled/half/full fog), reusing the well-established 3-point
sweep pattern from this shard's `AlphaTestEffect` fog test.

## Detailed Findings

No CRITICAL/HIGH findings.

### F1 — Shares the `AlphaTestEffect` fog test's object-space-Z fog limitation; no non-identity-`World` variant exists to detect a future view-space-fog regression fix done incorrectly
- Severity: MEDIUM
- Confidence: HIGH
- Category: test-coverage / FNA-parity (production limitation, not a defect in this file)
- Location: `colored3d.vert.glsl:40-42` (production); this test file uses `Matrix::getIdentityProperty()`
  for World/View/Projection throughout (lines 92-94), so object-space Z and view-space Z coincide here
- Evidence: see the full derivation in this batch's `vulkan_alphatest_fog_test.cpp.audit.md` (F1) — the
  same conclusion applies verbatim to `colored3d.vert.glsl`, which uses the identical
  `clamp((fogEnd-inPos.z)/(fogEnd-fogStart),0,1)` object-space formula rather than FNA's real
  `World`/`View`-derived `FogVector`.
- Why it matters: identical reasoning to the cross-referenced file — not repeated in full here to avoid
  duplication.

### F2 — `BasicEffect.VertexColorEnabled` is a public field, not a `getXProperty()`/`setXProperty()` pair, inconsistent with the CNA-wide C# property convention used by every *other* property on the same class
- Severity: LOW
- Confidence: HIGH
- Category: architecture / API-convention (production code, not this test file's own defect)
- Location: `include/Microsoft/Xna/Framework/Graphics/BasicEffect.hpp:48` (`bool VertexColorEnabled =
  false;`), consumed here at line 97 (`fx.VertexColorEnabled = false;`)
- Evidence: `BasicEffect.hpp` uses `getXProperty()`/`setXProperty()` for every other settable property
  (`DiffuseColor`, `EmissiveColor`, `TextureEnabled`, `FogEnabled`, `LightingEnabled`, etc. — 15+ pairs
  confirmed by direct inspection), but `World`, `View`, `Projection`, and `VertexColorEnabled` are bare
  public fields. This is the same characteristic previously noted (without objection) in a prior EasyGL-
  shard audit (`easygl_basiceffect_combined_test.cpp.audit.md`), which described it neutrally as
  "matches `BasicEffect.hpp`" rather than flagging the inconsistency itself.
- Why it matters: this is a real, pre-existing deviation from `CLAUDE.md`'s stated convention ("Do not
  replace C# properties with public fields unless the type already establishes that style") — but since
  three *other* properties (`World`/`View`/`Projection`) on the same class already establish exactly this
  public-field style, `VertexColorEnabled` is at worst consistent-with-itself rather than a novel one-off
  violation. Flagged here for completeness and cross-file traceability, but the fix (if any) belongs to
  `BasicEffect.hpp`'s own audit (a different shard, not yet run at the time of this report), not to this
  test file, which is simply a correct consumer of the existing API surface.

## Cross-File Observations

- F1 is identical in substance to the corresponding finding in `vulkan_alphatest_fog_test.cpp.audit.md`
  and `vulkan_basiceffect_coloredtextured3d_fog_test.cpp.audit.md` (also in this batch) — all three Vulkan
  fog tests share the same object-space-only production limitation and the same missing non-identity-
  `World` coverage gap.
- F2 applies identically to `vulkan_basiceffect_coloredtextured3d_fog_test.cpp` and
  `vulkan_basiceffect_combined_test.cpp` (also in this batch, both also use `fx.VertexColorEnabled = …`).
  Recorded in full once here; referenced by the other two reports rather than repeated verbatim.
- This test's explicit `Matrix::getIdentityProperty()` calls (rather than relying on constructor defaults)
  are a better test-authoring practice than the sibling `vulkan_alphatest_fog_test.cpp`, which never sets
  these matrices at all.

## Missing or Weak Tests

See F1 — a non-identity-`World` fog variant would close the same coverage gap noted for the
`AlphaTestEffect` fog test.

## Positive Findings

- All three fog-blend assertions independently re-derived and confirmed exact against the actual
  `colored3d.vert.glsl`/`colored3d.frag.glsl` source.
- The green-vertex-color-with-`VertexColorEnabled=false` negative check is a well-chosen, genuinely
  discriminating design against a plausible "vertex color gate ignored" regression.
- Explicit `Matrix::getIdentityProperty()` usage rather than implicit defaults — a small but real
  test-quality improvement over some siblings in this shard.

## Final Assessment

A correct, well-verified fog test for the `colored3d` pipeline, sharing the same (already-documented,
production-level, not test-authoring) object-space fog scope limitation as its `AlphaTestEffect` sibling
in this batch. No defects found in this file itself.
