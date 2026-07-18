# Audit: examples/vulkan_skinnedeffect_weightspervertex_test.cpp

## Metadata

- Source file: `examples/vulkan_skinnedeffect_weightspervertex_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — Task 895, `SkinnedEffect.WeightsPerVertex`
  GPU-enforcement test (Vulkan port).
- File type: hand-rolled `Game`-derived executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_skinnedeffect_weightspervertex …)` /
  `cna_register_backend_test(NAME Vulkan_SkinnedEffect_WeightsPerVertex …)`,
  `cmake/Tests/VulkanTests.cmake:664-667`).
- XNA/FNA relevance: direct — `SkinnedEffect.WeightsPerVertex`. FNA source:
  `HLSL/SkinnedEffect.fx`'s `Skin(vin, boneCount)`, where `boneCount` (1/2/4) genuinely bounds how
  many weight/index pairs are read.
- Production code exercised: `SkinnedEffect::setWeightsPerVertexProperty`/`FillGpuDrawParams`
  (`p.weightsPerVertex = weightsPerVertex_;`, `SkinnedEffect.cpp` line 395),
  `shaders/skinned3d_vertexlit.vert.glsl`'s `weightsPerVertex`-gated summation (lines 53-56:
  `if (weightsPerVertex >= 2.0) skinMat += ...; if (weightsPerVertex >= 4.0) skinMat += ...;`).

## Purpose

Tests that `WeightsPerVertex` is a real, GPU-enforced constraint, not merely a C++-side validated
property that never reaches the shader. Reuses Task 408's exact 2-bone blend (bones 0/1, weights
0.5/0.5, net shift `+0.5`) but deliberately populates the otherwise-unused slots 2/3 with non-zero
"garbage" weights (0.5 each) pointing at a third bone with a huge, unmistakable `+100` X translation
— so a shader that (bug) unconditionally sums all 4 slots produces a wildly different, easily
distinguished result (`+100.5` shift, quad pushed off-screen) from a shader that (correct) honors
`WeightsPerVertex=2` (`+0.5` shift, quad at screen centre).

## Executive Verdict

**Healthy.** The strongest-designed test in this shard's `SkinnedEffect` group: it engineers the
"garbage" bone-2 translation specifically so the buggy and correct behaviors are separated by 100
NDC units (on-screen center vs. entirely off-screen) — a margin no plausible float-precision or
tolerance issue could accidentally paper over.

## Checklist Results

### API / XNA / FNA parity
`fx.setWeightsPerVertexProperty(2)` — verified `SkinnedEffect::setWeightsPerVertexProperty` (lines
286-292) throws `std::out_of_range` for anything other than `1`/`2`/`4`, matching FNA's validated
range. FNA's `.fx` file has three genuinely separate compiled shader permutations
(`VSSkinnedVertexLightingOneBone`/`TwoBones`/`FourBones`, each with a compile-time `[unroll]` loop
bound); the Vulkan backend's single-shader-with-runtime-`if` approach
(`if (weightsPerVertex >= 2.0) ...`) is a legitimate, behaviorally-equivalent simplification (same
observable effect: slots beyond `WeightsPerVertex` never contribute), not a parity gap, and this
test correctly validates the *observable* behavior rather than the internal shader-variant
mechanism.

### Behavioral correctness
Verified the vertex data (`w0=w1=w2=w3=0.5`, `i0=0,i1=1,i2=2,i3=2`, lines 113-118) against the
shader: with `weightsPerVertex=2`, `skinMat = bb.bones[0]*0.5 + bb.bones[1]*0.5` (bone 2's
`0.5*0.5=0.25` weighted contributions from slots 2/3 are never added, since the `>=4.0` gate — not
`>=2.0` — correctly separates slot-pair 0/1 from slot-pair 2/3). Net correct shift: `+0.5` (identical
to Task 408's derivation). Bug-case shift if all 4 were summed unconditionally: `0.5*(-0.5) +
0.5*(1.5) + 0.5*100 + 0.5*100 = 0.5 + 100 = +100.5` — matches the header comment's own claimed
bug-case arithmetic exactly (independently verified, not just copied).

The pass condition (`centOk = centPx.R > centPx.G && centPx.R > 50`) correctly captures "quad
rendered at the correct, small shift" vs. the failure comment's own description ("centre=green would
mean slots 2/3's garbage bone-2 weights leaked in, pushing the quad off-screen") — a `+100.5`-shifted
quad leaves the centre sample point on plain green background, which `centOk` correctly flags as
`false`.

### Testing
A genuinely adversarial test — not just "does this work" but "does this work when fed exactly the
input that would expose a specific known-fixed bug shape (summing beyond `WeightsPerVertex`)." A
GPU-shader-dispatch bug like the one guarded against here would not be caught by a C++-only unit
test of `FillGpuDrawParams()` alone, since `p.weightsPerVertex` being correctly *set* on the CPU side
says nothing about whether the shader actually *reads* it — this pixel test closes exactly that gap.
No `Draw()` retry-until-nonblack loop — see F1.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — No retry-until-rendered guard against a black first frame

- Severity: LOW
- Confidence: MEDIUM
- Category: robustness / test flakiness
- Location/symbol: `Draw()` (lines 74-161) — same single-shot render/readback shape as this shard's
  `translation_bone`/`twobone_blend` sibling files (see those reports' identical F1 for full
  reasoning); identical to the EasyGL sibling's own F1
  (`easygl_skinnedeffect_weightspervertex_test.cpp.audit.md`).
- Suggested future action (not implemented by this audit): adopt this shard's retry-until-nonblack
  convention for consistency.

## Cross-File Observations

- Directly and explicitly built on `vulkan_skinnedeffect_twobone_blend_test.cpp`'s own bone/weight
  setup — confirmed the shared `-0.5`/`+1.5` translation values and `0.5`/`0.5` weight split are
  byte-identical between the two files, making this file's "garbage slot" addition the only
  meaningful delta.
- Same Identity-`World` convention as the rest of this shard; the missing-world-space-normal (F2)
  and ambient/emissive-forwarding (F1) production defects documented in
  `vulkan_skinnedeffect_preferperpixellighting_test.cpp.audit.md` are present in the same shader
  this file dispatches to but irrelevant to this file's own position-only pass condition.
- Same independently-duplicated stride-52 `SkinnedGpuVertex` struct as the other files in this
  group.
- Correctly ports the EasyGL original's core insight (a `+100` "unmistakable" garbage translation)
  unchanged, including the identical vertex data and bone setup, only substituting the Vulkan
  device/pipeline plumbing.

## Missing or Weak Tests

- See F1.
- Does not test `WeightsPerVertex=4` with a similar garbage-slot-beyond-4 stress (not directly
  possible, since 4 is the max valid slot count) — `WeightsPerVertex=1` with garbage in slots 1-3
  would be the remaining untested combination in this "garbage slot" family, reasonably out of this
  file's own single-case scope.

## Positive Findings

- Excellent adversarial test design: the "garbage" bone's translation magnitude (`+100`) is chosen
  specifically to make the bug/correct-behavior distinction unmissable (on-screen vs. off-screen)
  rather than a subtle color-tolerance difference.
- The header comment states the pre-fix bug's exact behavior ("CNA's skinning shaders always summed
  all 4 weight/index pairs unconditionally") and this audit independently confirmed, by reading the
  actual Vulkan shader source, that the described fix (`if (weightsPerVertex >= 2.0)`/`if
  (weightsPerVertex >= 4.0)` gating) is genuinely present and correctly gates the described slots.

## Final Assessment

The strongest-designed `SkinnedEffect` test in this shard — a genuinely adversarial regression test
for a specific, previously-real bug, with margins large enough that no plausible tolerance/precision
issue could mask a regression. Only gap is the shared, low-severity missing retry-guard (F1).
