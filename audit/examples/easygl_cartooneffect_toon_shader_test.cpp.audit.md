# Audit: examples/easygl_cartooneffect_toon_shader_test.cpp

## Metadata

- Source file: `examples/easygl_cartooneffect_toon_shader_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard)
- File type: C++ example/integration test, registered as CTest `EasyGL_CartoonEffect_Toon_Shader`
  (`cmake/Tests/EasyGLTests.cmake:404-406`, target `cna_test_easygl_cartooneffect_toon_shader`)
- Related production code: `ShaderEffect::SetUniformVec3`/`SetUniformFloatArray`/`SetUniformInt`/
  `SetTexture` (`ShaderEffect.cpp:43-76`), `Texture2D::CreateFromPixels`,
  `GraphicsDevice::ExtractMatrices`/`DrawIndexedPrimitives`
- XNA/FNA relevance: ports `NonPhotoRealisticSample_4_0/NonPhotoRealistic/Content/CartoonEffect.Fx`'s
  `Toon` technique (`ToonPixelShader`, banded cartoon shading used by `Game.cs:251`)
- Main related tests: sibling `easygl_cartooneffect_lambert_shader_test.cpp` (shares the vertex shader
  verbatim) and `easygl_cartooneffect_normaldepth_shader_test.cpp` (this batch)

## Purpose

`EasyGLCartoonEffectToonTest` proves the EasyGL backend correctly executes a hand-ported GLSL
translation of `CartoonEffect.Fx`'s `Toon` technique — a 3-band brightness quantization driven by
`ToonThresholds`/`ToonBrightnessLevels` array uniforms and a per-pixel Lambertian `LightAmount`. It
draws a textured quad lit from `normalize(1,1,1)` twice (`World=Identity`, `World=RotationY(180°)`),
expecting the two draws to land in different brightness bands.

## Executive Verdict

**Healthy.** The fragment shader is a correct, direct port of the FNA `.Fx` source including its one
documented quirk (raw, unsaturated `LightAmount`), and the two checks are engineered to land in
genuinely different bands (middle vs. lowest) rather than coincidentally both landing in the same one,
which would have silently hidden a broken threshold comparison.

## Checklist Results

### API / XNA / FNA parity
Uses `ShaderEffect::SetUniformVec3("LightDirection", …)`, `SetUniformFloatArray("ToonThresholds", …, 2)`,
`SetUniformFloatArray("ToonBrightnessLevels", …, 3)`, `SetUniformInt("TextureEnabled", 1)`, and
`SetTexture(0, tex_)` — all correct `NOXNA`-tagged `ShaderEffect` extension APIs
(`ShaderEffect.hpp:48-75`), forwarding straight to `IEffectBackend` (`ShaderEffect.cpp:43-76`), which is
the intended, documented mechanism for a hand-authored shader's non-`IEffectMatrices` uniforms.

### Behavioral correctness
Hand-verified the FNA reference (header lines 9-20) against the ported GLSL:
```
float ToonThresholds[2] = { 0.8, 0.4 };
float ToonBrightnessLevels[3] = { 1.3, 0.9, 0.5 };
if (LightAmount > Thresholds[0]) light = Brightness[0];
else if (LightAmount > Thresholds[1]) light = Brightness[1];
else light = Brightness[2];
color.rgb *= light;
```
matches `kFragSrc` (lines 111-119) exactly, including reusing the sibling Lambert technique's own
`LightingVertexShader` verbatim (confirmed: `kVertSrc`, lines 82-98, computes
`vLightAmount = dot(worldNormal, LightDirection)` with **no** `saturate()`/`clamp()` call — correctly
preserving the header comment's own claim (lines 22-24) that `LightAmount` is deliberately used raw
here, unlike the sibling Lambert technique, so it can legitimately go negative and still correctly
select the lowest band via the `else` fall-through rather than an explicit `< 0` branch.

Check A (`World=Identity`): `worldNormal=(0,0,1)`, `LightDirection=normalize(1,1,1)`,
`LightAmount = 1/√3 ≈ 0.57735`. `0.57735 > 0.8`? No. `> 0.4`? Yes → middle band, `light=0.9`.
`texColor=(200,100,50)/255 * 0.9 ≈ (180,90,45)` — matches the test's expected value exactly.
Check B (`World=RotationY(180°)`): flips normal to `(0,0,-1)`, `LightAmount ≈ -0.57735`. Not `> 0.8`,
not `> 0.4` → falls to `else`, lowest band, `light=0.5`. `texColor * 0.5 ≈ (100,50,25)` — matches
exactly. The two bands genuinely differ (middle 0.9 vs. lowest 0.5), which is the correct
discriminator for "did the threshold comparison actually select a different band," not just
"did the shader compile."

### Logic
`close()` (line 228) uses ±6 tolerance, consistent with the sibling NormalDepth test. Both checks use
independently-derived expected values (not copy-pasted from one another), and Check B's expected
`(100,50,25)` is exactly half of Check A's un-scaled `texColor` — a genuine, distinguishable third data
point (0.9 vs. 0.5 multiplier) rather than a coincidental near-match.

### Memory/resource lifetime
`tex_` is a `Texture2D` by value (not `unique_ptr`) constructed via `Texture2D::CreateFromPixels` in
`Initialize()` — consistent with `Texture2D`'s established value-type ownership model elsewhere in the
suite (backed by a `shared_ptr`-style internal handle, not raw GPU state owned by this class). `vb_`/
`ib_` are `unique_ptr`s, correctly scoped to the test's lifetime.

### C++ correctness
Same redundant-but-harmless double `dynamic_cast<ShaderEffect*>` pattern as the sibling NormalDepth
test (`Draw()` line 217, `DrawOnce()` line 186, the latter with no null check) — same LOW-severity,
not-currently-exploitable observation as noted there; not re-raised as a separate finding since it's
the identical, already-flagged pattern across this batch's sibling file.

### Performance / Thread safety
N/A — single-frame test.

### Architecture
Correct XNA/NOXNA API surface usage only.

### Maintainability
Header comment (lines 1-38) explicitly documents the one intentional deviation from the sibling
Lambert test's own convention (raw vs. saturated `LightAmount`) and explains *why* it's preserved
faithfully rather than "fixed" — exactly the kind of documented-deviation discipline the project's own
`CLAUDE.md` asks for.

### Portability
No platform-specific code.

### Robustness
Correct `IsEffectValid()` failure path (lines 218-223) mirroring the sibling test.

### Testing
Two checks, both with real discriminating power (different brightness bands). No test drives
`LightAmount` to exactly `0.8` or `0.4` (the threshold boundary values themselves) — the FNA source's
own comparisons are strict `>` (not `>=`), so an exact-boundary case would be the single most valuable
additional check (a `>=`/`>` swap bug would only be caught there), but this is a reasonable scope
choice for a "prove the port compiles and picks distinguishable bands" test rather than an exhaustive
boundary-condition suite.

### Cross-file consistency
Vertex shader is byte-for-byte identical to the sibling Lambert test's own (confirmed by comparing
`kVertSrc` structurally — both compute `worldNormal` via `mat3(World) * aNormal` and
`vLightAmount = dot(worldNormal, LightDirection)` with the same uniform names), matching the header
comment's own claim (line 4-5) that both techniques share `LightingVertexShader` verbatim.

## Detailed Findings

No HIGH/MEDIUM findings. One LOW/INFO observation (not re-reported in full — see C++ correctness above
for the shared redundant-`dynamic_cast` pattern, already raised as F1-equivalent in the sibling
NormalDepth report).

### F1 — Threshold boundary values (0.8, 0.4) are never tested

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `ToonPixelShader`'s `>` comparisons (`kFragSrc` lines 114-116); test's two
  `LightAmount` values (`≈0.577`, `≈-0.577`) are both comfortably clear of either threshold.
- Evidence: Neither check drives `LightAmount` to exactly `0.8` or `0.4`, so a hypothetical `>=`/`>`
  swap in a future edit to this shader (or its GLSL port) would not be caught by this test.
- Why it matters: strict-vs-inclusive comparison bugs are exactly the class of off-by-one error this
  kind of port is most at risk of, and the sibling `easygl_depthstencilstate_compare_function_test.cpp`
  in this same batch explicitly designs a boundary case (`LessEqual` at exactly `0.5`) for precisely
  this reason — this file doesn't apply the same discipline to its own threshold logic.
- Suggested action (not implemented by this audit): add a third `LightDirection`/`World` combination
  that drives `LightAmount` to exactly (or within float epsilon of) `0.8` or `0.4`, if this file is
  revisited.

## Cross-File Observations

- Shares `LightingVertexShader` verbatim with the sibling Lambert technique test — worth reviewing both
  together if either vertex shader is ever changed, since a divergence would break the "shared verbatim"
  invariant this file's own comment asserts.

## Missing or Weak Tests

- See F1 — no boundary-value case for `ToonThresholds`.

## Positive Findings

- Correctly preserves the FNA source's one deliberate non-obvious behavior (unsaturated `LightAmount`)
  rather than "fixing" it, with an explicit comment explaining why.
- Two checks with genuine, well-separated discriminating power (different brightness bands, not just
  different raw pixel values).
- Consistent, correct use of `ShaderEffect`'s array-uniform (`SetUniformFloatArray`) and texture-binding
  (`SetTexture`) extension APIs.

## Final Assessment

A correctly-implemented, well-documented shader-port proof that faithfully preserves the FNA source's
one intentional quirk and genuinely discriminates between brightness bands; its only gap is not
exercising the exact threshold boundary values, a minor, low-risk omission given the test's stated scope.
