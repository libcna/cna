# Audit: examples/easygl_skinnedeffect_twobone_blend_test.cpp

## Metadata

- Source file: `examples/easygl_skinnedeffect_twobone_blend_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — `SkinnedEffect` two-bone weighted blend test
- File type: hand-rolled `Game`-derived executable, CTest-registered
  (`cna_easygl_test(cna_test_easygl_skinnedeffect_twobone_blend …)` /
  `cna_register_backend_test(NAME EasyGL_SkinnedEffect_TwoBoneBlend …)`,
  `cmake/Tests/EasyGLTests.cmake:666-669`).
- XNA/FNA relevance: direct — `SkinnedEffect.SetBoneTransforms`/`WeightsPerVertex=2`, real XNA 4.0 API.
  FNA source: `HLSL/SkinnedEffect.fx`'s `Skin(vin, boneCount=2)`
  (`skinning += Bones[Indices[i]] * Weights[i]` summed over the first 2 slots).
- Production code exercised: `SkinnedEffect::setWeightsPerVertexProperty`/`SetBoneTransforms`,
  `EnsureSkinnedVertexLitProgram()`'s skin-matrix blend
  (`EasyGLGraphicsBackend.cpp` lines 3489-3491:
  `mat4 skinMat=uBones[aBoneIndices.x]*aBoneWeights.x; if(uWeightsPerVertex>=2)
  skinMat+=uBones[aBoneIndices.y]*aBoneWeights.y;`).

## Purpose

Task 408's test that the GPU skin matrix is a genuine **weighted sum** of two bones, not a
single-bone selection bug in disguise. Bone 0 = `Translate(-0.5,0,0)`, Bone 1 = `Translate(+1.5,0,0)`,
weights 0.5/0.5. Because matrix multiplication is linear in the matrix, `mul(Position, skinMat)`
distributes: `Weights[0]*mul(Position,Bones[0]) + Weights[1]*mul(Position,Bones[1])`, so the blended
shift is `0.5*(-0.5) + 0.5*(1.5) = +0.5` — a value distinct from either bone's own individual shift,
making a "picked one bone instead of blending" bug observably different from the correct result.

## Executive Verdict

**Healthy.** The blend arithmetic is correct and the test is specifically designed (via the choice of
bone values) to distinguish "genuinely blends 2 weighted bones" from "picks one bone" — a meaningfully
stronger design than an equal-weight blend would be, since with equal source/target values a
bone-selection bug could coincidentally produce the same answer as a real blend.

## Checklist Results

### API / XNA / FNA parity
`fx.setWeightsPerVertexProperty(2)` and `fx.SetBoneTransforms({Translate(-0.5,0,0), Translate(1.5,0,0)})`
correctly map to FNA's `SkinnedEffect.WeightsPerVertex`/`SetBoneTransforms`. Verified
`setWeightsPerVertexProperty`'s validation (`SkinnedEffect.cpp` lines 286-292) accepts `2` (only `1`,
`2`, `4` are valid; this test's own choice of `2` is a legitimate, in-range value, unlike
`weightspervertex_test.cpp`'s deliberate garbage-slot stress case — appropriately different designs
for different goals).

### Behavioral correctness
Vertex layout: `w0=0.5, w1=0.5, w2=w3=0`, `i0=0, i1=1, i2=i3=0` (lines 110-115) — matches
`EnsureSkinnedVertexLitProgram()`'s shader exactly: with `uWeightsPerVertex=2`, `skinMat =
uBones[0]*0.5 + uBones[1]*0.5` (the `>=4` branch is skipped since `2 < 4`), i.e. exactly the blend the
test's own header comment (lines 6-14) describes. Independently verified the arithmetic:
`skinMat` applied to a point `p` gives `0.5*(p+(-0.5,0,0)) + 0.5*(p+(1.5,0,0)) = p + 0.5*(-0.5+1.5) =
p + 0.5` — confirms the test's own claimed `+0.5` net shift.

The comment (lines 16-18) correctly notes this numerically matches Task 407's (`translation_bone_test`)
single-bone `+0.5` result "but is reached via a genuinely different mechanism" — an important
distinction the test's own three-sample-point readback (left/center/right) can only prove *positionally*
(the quad ends up in the same place either way); it does **not** and cannot, by itself, distinguish "one
bone with weight 1" from "two bones blended to net +0.5" purely from this file's own assertions — that
discriminating power actually comes from contrasting this file's PASS against a hypothetical buggy
build where slots 2/3 leak in unweighted garbage (which is exactly what the sibling
`weightspervertex_test.cpp` stresses with non-zero bone-2 weights in the unused slots). Read in
isolation, this file alone proves "the shader can be fed a 2-bone weighted blend and produce the
mathematically-correct combined shift," which is meaningful and correctly labeled, but the stronger
"proves genuine summation, not selection" claim in the header comment is only fully established in
combination with the sibling weights-per-vertex test, not by this file alone.

### Logic
Same `RasterizerState::CullNone` requirement/comment pattern (lines 100-103) as sibling files in this
shard, correctly applied (set before `fx.Apply()`/`DrawPrimitives()`).

### Testing
Narrowly and correctly scoped to 2-bone blending with in-range `WeightsPerVertex`. Does not itself
prove the shader ignores garbage in unused weight/index slots (reasonably deferred to
`easygl_skinnedeffect_weightspervertex_test.cpp`, since here slots 2/3 are already zero-weighted, so
summing all 4 vs. only the first 2 gives the same answer either way in this file — explicitly
acknowledged in the header comment, lines 19-20).

## Detailed Findings

No CRITICAL/HIGH findings specific to this file's own logic.

### F1 — No retry-until-rendered guard against a black first frame

- Severity: LOW
- Confidence: MEDIUM
- Category: robustness / test flakiness
- Location/symbol: `Draw()` (lines 70-155) — single-shot render/readback, same shape as
  `easygl_skinnedeffect_translation_bone_test.cpp`'s F1.
- Evidence/why it matters: identical reasoning to that file's F1 — see that report for the full
  analysis. This file's own pass condition (`leftPx.getGProperty() > leftPx.getRProperty()`, etc.)
  would likewise read `false` on an all-black stray first frame, unlike the sibling specular/
  preferperpixellighting tests which explicitly loop to skip such a frame.
- Suggested future action (not implemented by this audit): adopt the same retry convention as this
  shard's other files, for consistency and flake resistance.

## Cross-File Observations

- Same Identity-`World` convention as the rest of this shard (`Matrix::getIdentityProperty()`, line
  86) — inherits, but does not itself meaningfully expose, the missing world-space normal-transform
  production issue documented in this shard's specular/preferperpixellighting reports (irrelevant here
  since this test only checks color-channel dominance for position, not an exact lit value).
- The independently-duplicated stride-52 `SkinnedGpuVertex` struct (same layout, same
  `static_assert(sizeof==52)`) appears identically in this file and at least four siblings — see the
  maintainability observation already recorded in
  `easygl_skinnedeffect_translation_bone_test.cpp.audit.md`'s Cross-File Observations; applies equally
  here.
- Deliberately, and correctly per its own header comment, designed as a complementary pair with
  `easygl_skinnedeffect_weightspervertex_test.cpp` (same bones 0/1, same net shift, but that file adds
  a third "garbage" bone in the otherwise-unused slots 2/3 to actually force a divergent result if
  `WeightsPerVertex` were ignored) — a good example of one test file building the base case and a
  sibling stressing the edge case, rather than duplicating full coverage in each file.

## Missing or Weak Tests

- See F1.
- No test combines a 2-bone blend with unequal (non-0.5/0.5) weights — every weighted-blend test in
  this shard uses an even split; an uneven split (e.g. 0.25/0.75) would additionally confirm the
  weights themselves (not just the presence of two nonzero indices) are read and applied correctly,
  rather than e.g. a bug that always averages two nonzero-weighted bones 50/50 regardless of their
  actual weight values.

## Positive Findings

- Choice of bone translation values (`-0.5` and `+1.5`, blending to `+0.5`) is deliberately designed so
  that a "picked one bone" bug and a "correctly blended both bones" implementation produce visibly
  different results — a stronger test-design decision than using values where a blend bug might
  coincidentally still land in the same screen region.
- Explicit, correct acknowledgment in the header comment of what this file does and does not prove on
  its own, and how it complements a sibling file — good test-suite hygiene.

## Final Assessment

A correct, well-designed 2-bone blend test whose only weaknesses are the shared, low-severity
retry-guard inconsistency (F1) and an untested uneven-weight-split case; its core arithmetic and shader
dispatch match FNA's real `Skin()` semantics.
