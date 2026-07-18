# Audit: examples/vulkan_skinnedeffect_twobone_blend_test.cpp

## Metadata

- Source file: `examples/vulkan_skinnedeffect_twobone_blend_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — Task 408, `SkinnedEffect` two-bone weighted blend
  pixel test (Vulkan port; header cites "examples/easygl_skinnedeffect_twobone_blend_test.cpp for
  the full derivation").
- File type: hand-rolled `Game`-derived executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_skinnedeffect_twobone_blend …)` /
  `cna_register_backend_test(NAME Vulkan_SkinnedEffect_TwoBoneBlend …)`,
  `cmake/Tests/VulkanTests.cmake:331-334`).
- XNA/FNA relevance: direct — `SkinnedEffect.SetBoneTransforms`/`WeightsPerVertex=2`. FNA source:
  `HLSL/SkinnedEffect.fx`'s `Skin(vin, boneCount=2)` (`skinning += Bones[Indices[i]] * Weights[i]`
  summed over the first 2 slots).
- Production code exercised: `SkinnedEffect::setWeightsPerVertexProperty`/`SetBoneTransforms`,
  `shaders/skinned3d_vertexlit.vert.glsl`'s skin-matrix blend (lines 54-56:
  `mat4 skinMat = bb.bones[aBoneIndices.x] * aBoneWeights.x; if (weightsPerVertex >= 2.0) skinMat +=
  bb.bones[aBoneIndices.y] * aBoneWeights.y;`).

## Purpose

Tests that the GPU skin matrix is a genuine **weighted sum** of two bones, not a single-bone
selection bug in disguise. Bone 0 = `Translate(-0.5,0,0)`, Bone 1 = `Translate(+1.5,0,0)`, weights
0.5/0.5. Since matrix multiplication is linear, the blended shift is `0.5*(-0.5) + 0.5*(1.5) = +0.5`
— a value distinct from either bone's own individual shift, making a "picked one bone instead of
blending" bug observably different from the correct result.

## Executive Verdict

**Healthy.** The blend arithmetic is correct and the test is specifically designed (via the choice
of bone values) to distinguish "genuinely blends 2 weighted bones" from "picks one bone" — a
meaningfully stronger design than an equal-source/target-value blend would be.

## Checklist Results

### API / XNA / FNA parity
`fx.setWeightsPerVertexProperty(2)` and `fx.SetBoneTransforms({Translate(-0.5,0,0),
Translate(1.5,0,0)})` correctly map to FNA's `SkinnedEffect.WeightsPerVertex`/`SetBoneTransforms`.
`setWeightsPerVertexProperty`'s validation (`SkinnedEffect.cpp` lines 286-292) accepts `2` (only
`1`/`2`/`4` are valid) — a legitimate, in-range value, distinct from
`weightspervertex_test.cpp`'s deliberate garbage-slot stress case.

### Behavioral correctness
Vertex layout: `w0=0.5, w1=0.5, w2=w3=0`, `i0=0, i1=1, i2=i3=0` (lines 88-93) — matches
`skinned3d_vertexlit.vert.glsl`'s formula exactly: with `weightsPerVertex=2`, `skinMat =
bb.bones[0]*0.5 + bb.bones[1]*0.5` (the `>=4.0` branch skipped since `2<4`). Independently verified
the arithmetic: `skinMat` applied to a point `p` gives `0.5*(p+(-0.5,0,0)) + 0.5*(p+(1.5,0,0)) = p +
0.5*(-0.5+1.5) = p + 0.5` — confirms the test's own claimed `+0.5` net shift, identical to Task 407's
single-bone result but reached via a genuinely different mechanism. Read in isolation, this file
alone proves "the shader can be fed a 2-bone weighted blend and produce the mathematically-correct
combined shift"; it does not (and, per its own header comment lines 16-18, does not claim to) prove
the shader wouldn't produce the same net shift via some other bug (e.g. leaking unweighted garbage
from unused slots) — that discriminating power is provided by the sibling
`vulkan_skinnedeffect_weightspervertex_test.cpp`.

### Logic
Same `RasterizerState::CullNone` requirement/comment pattern (lines 68-70) as sibling files in this
shard, correctly applied before `fx.Apply()`/`DrawPrimitives()`.

### Testing
Narrowly and correctly scoped to 2-bone blending with in-range `WeightsPerVertex`. Does not itself
prove the shader ignores garbage in unused weight/index slots (reasonably deferred to
`vulkan_skinnedeffect_weightspervertex_test.cpp`, since here slots 2/3 are already zero-weighted).
No `Draw()` retry-until-nonblack loop — see F1.

## Detailed Findings

No CRITICAL/HIGH findings specific to this file's own logic.

### F1 — No retry-until-rendered guard against a black first frame

- Severity: LOW
- Confidence: MEDIUM
- Category: robustness / test flakiness
- Location/symbol: `Draw()` (lines 55-141) — single-shot render/readback, same shape as
  `vulkan_skinnedeffect_translation_bone_test.cpp`'s F1 (see that report for the full analysis);
  identical to the EasyGL sibling's own F1
  (`easygl_skinnedeffect_twobone_blend_test.cpp.audit.md`).
- Suggested future action (not implemented by this audit): adopt this shard's retry-until-nonblack
  convention for consistency.

## Cross-File Observations

- Same Identity-`World` convention as the rest of this shard — inherits, but does not itself
  meaningfully expose, the missing world-space normal-transform (F2) and ambient/emissive-forwarding
  (F1) production defects documented in
  `vulkan_skinnedeffect_preferperpixellighting_test.cpp.audit.md` (irrelevant here since this test
  only checks color-channel dominance for position, not an exact lit value).
- Deliberately, and correctly per its own header comment, designed as a complementary pair with
  `vulkan_skinnedeffect_weightspervertex_test.cpp` (same bones 0/1, same net shift, but that file
  adds a third "garbage" bone in the otherwise-unused slots 2/3 to actually force a divergent result
  if `WeightsPerVertex` were ignored).
- Same independently-duplicated stride-52 `SkinnedGpuVertex` struct as other files in this shard.

## Missing or Weak Tests

- See F1.
- No test combines a 2-bone blend with unequal (non-0.5/0.5) weights — every weighted-blend test in
  this shard uses an even split; an uneven split would additionally confirm the weights themselves
  (not just the presence of two nonzero indices) are read and applied correctly.

## Positive Findings

- Choice of bone translation values (`-0.5` and `+1.5`, blending to `+0.5`) is deliberately designed
  so that a "picked one bone" bug and a "correctly blended both bones" implementation produce
  visibly different results.
- Explicit, correct acknowledgment (in the file's own comment lineage, mirrored from the EasyGL
  original) of what this file does and does not prove on its own, and how it complements a sibling
  file.

## Final Assessment

A correct, well-designed 2-bone blend test whose only weaknesses are the shared, low-severity
retry-guard inconsistency (F1) and an untested uneven-weight-split case; its core arithmetic and
shader dispatch match FNA's real `Skin()` semantics.
