# Audit: examples/easygl_skinnedeffect_weightspervertex_test.cpp

## Metadata

- Source file: `examples/easygl_skinnedeffect_weightspervertex_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — `SkinnedEffect.WeightsPerVertex` GPU-enforcement test
- File type: hand-rolled `Game`-derived executable, CTest-registered
  (`cna_easygl_test(cna_test_easygl_skinnedeffect_weightspervertex …)` /
  `cna_register_backend_test(NAME EasyGL_SkinnedEffect_WeightsPerVertex …)`,
  `cmake/Tests/EasyGLTests.cmake:648-651`).
- XNA/FNA relevance: direct — `SkinnedEffect.WeightsPerVertex`, real XNA 4.0 API. FNA source:
  `HLSL/SkinnedEffect.fx`'s `Skin(vin, boneCount)`, where `boneCount` (1/2/4) genuinely bounds how many
  weight/index pairs are read — slots beyond `boneCount` are never touched by the real shader.
- Production code exercised: `SkinnedEffect::setWeightsPerVertexProperty`/`FillGpuDrawParams`
  (`p.weightsPerVertex = weightsPerVertex_;`, `SkinnedEffect.cpp` line 395),
  `EnsureSkinnedVertexLitProgram()`'s `uWeightsPerVertex`-gated summation
  (`EasyGLGraphicsBackend.cpp` lines 3490-3491).

## Purpose

Task 895's test that `WeightsPerVertex` is a real, GPU-enforced constraint, not merely a C++-side
validated property that never reaches the shader. Per its own header comment, "before this task, CNA's
skinning shaders always summed all 4 weight/index pairs unconditionally, ignoring WeightsPerVertex
entirely." Reuses Task 408's exact 2-bone blend (bones 0/1, weights 0.5/0.5, net shift `+0.5`) but
deliberately populates the otherwise-unused slots 2/3 with non-zero "garbage" weights (0.5 each)
pointing at a third bone with a huge, unmistakable `+100` X translation — so a shader that (bug)
unconditionally sums all 4 slots produces a wildly different, easily-distinguished result (`+100.5`
shift, quad pushed off-screen) from a shader that (correct) honors `WeightsPerVertex=2` and only reads
the first 2 slots (`+0.5` shift, quad at screen centre).

## Executive Verdict

**Healthy.** This is the strongest-designed test in this shard's `SkinnedEffect` group: rather than
merely asserting a value and hoping a latent bug would produce something different, it engineers the
"garbage" bone-2 translation specifically so the "bug" and "correct" behaviors are separated by 100
NDC units (on-screen center vs. entirely off-screen) — a margin no plausible float-precision or
tolerance issue could accidentally paper over.

## Checklist Results

### API / XNA / FNA parity
`fx.setWeightsPerVertexProperty(2)` — verified `SkinnedEffect::setWeightsPerVertexProperty` (lines
286-292) throws `std::out_of_range` for anything other than `1`/`2`/`4`, matching FNA's validated range
(`SkinnedEffect.cs`'s `WeightsPerVertex` setter, same three legal values). The comment's framing —
"WeightsPerVertex is a real GPU-enforced constraint" — is the right way to describe this: FNA's `.fx`
file has three genuinely separate compiled shader permutations (`VSSkinnedVertexLightingOneBone`/
`TwoBones`/`FourBones`, each calling `Skin(vin, N)` with `N` baked into the `[unroll]` loop bound at
*compile* time, not a runtime branch) — CNA's single-shader-with-runtime-`if` approach
(`if(uWeightsPerVertex>=2) ...`) is a legitimate, behaviorally-equivalent simplification (same
observable effect: slots beyond `WeightsPerVertex` never contribute), not a parity gap, and this test
correctly validates the *observable* behavior rather than the internal compiled-shader-variant
mechanism.

### Behavioral correctness
Verified the vertex data (`w0=w1=w2=w3=0.5`, `i0=0,i1=1,i2=2,i3=2`, lines 114-119) against the shader:
with `uWeightsPerVertex=2`, `skinMat = uBones[0]*0.5 + uBones[1]*0.5` (bone 2's `0.5*0.5=0.25` weighted
contributions from slots 2/3 are never added, since both the `>=2` and `>=4` gates correctly separate
slot-pair 0/1 from slot-pair 2/3 — confirmed the `>=4` branch, not `>=2`, gates slots z/w, so setting
`WeightsPerVertex=2` genuinely excludes slots 2/3 rather than partially including them). Net correct
shift: `+0.5` (identical to Task 408's own derivation, reused correctly here). Bug-case shift if all 4
were summed unconditionally: `0.5*(-0.5) + 0.5*(1.5) + 0.5*100 + 0.5*100 = 0.5 + 100 = +100.5` — matches
the header comment's own claimed bug-case arithmetic exactly (verified independently, not just
copied).

The pass condition (`centOk = centPx.R > centPx.G && centPx.R > 50`) correctly captures "quad rendered
at the *correct*, small shift" vs. the failure comment's own description ("centre=green would mean
slots 2/3's garbage bone-2 weights leaked in, pushing the quad off-screen") — a `+100.5`-shifted quad
would leave the centre sample point sitting on plain green background (`leftPx`-style all-green), which
the `centOk` check would correctly flag as `false` (green channel dominates, `R` does not exceed `50`).

### Testing
A genuinely adversarial test — not just "does this work" but "does this work when fed exactly the
input that would expose a specific known-fixed bug shape (summing beyond `WeightsPerVertex`)." This is
exactly the kind of regression test that should exist for a fix like Task 895's and is correctly placed
in this shard rather than only as a unit test (a GPU-shader-dispatch bug like the one being guarded
against here would not be caught by a C++-only unit test of `FillGpuDrawParams()` alone, since
`p.weightsPerVertex` being correctly *set* on the CPU side says nothing about whether the shader
actually *reads* it — this pixel test closes exactly that gap).

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — No retry-until-rendered guard against a black first frame

- Severity: LOW
- Confidence: MEDIUM
- Category: robustness / test flakiness
- Location/symbol: `Draw()` (lines 74-161) — same single-shot render/readback shape as this shard's
  `translation_bone`/`twobone_blend` sibling files (see those reports' identical F1 for full reasoning).
- Suggested future action (not implemented by this audit): adopt this shard's retry-until-nonblack
  convention for consistency.

## Cross-File Observations

- Directly and explicitly built on `easygl_skinnedeffect_twobone_blend_test.cpp`'s own bone/weight
  setup (same header comment cross-reference, lines 14-16) — confirmed the shared `-0.5`/`+1.5`
  translation values and `0.5`/`0.5` weight split are byte-identical between the two files, making this
  file's "garbage slot" addition the only meaningful delta, exactly as its own comment claims.
- Same Identity-`World` convention as the rest of this shard; the missing-world-space-normal
  production finding documented elsewhere in this shard (see
  `easygl_skinnedeffect_preferperpixellighting_test.cpp.audit.md`'s F1) is present in the same shader
  this file dispatches to but irrelevant to this file's own position-only pass condition.
- Same independently-duplicated stride-52 `SkinnedGpuVertex` struct as the other files in this group —
  see the maintainability note already recorded in the `translation_bone` report.

## Missing or Weak Tests

- See F1.
- Does not test `WeightsPerVertex=4` with a similar garbage-slot-beyond-4 stress (not directly
  possible, since 4 is the max valid slot count and the vertex format itself only carries 4 weight/index
  pairs) — `WeightsPerVertex=1` with garbage in slots 1-3 would be the remaining untested combination
  in this "garbage slot" family, reasonably out of this file's own single-case scope but worth noting as
  a gap in the shard as a whole.

## Positive Findings

- Excellent adversarial test design: the "garbage" bone's translation magnitude (`+100`) is chosen
  specifically to make the bug/correct-behavior distinction unmissable (on-screen vs. off-screen)
  rather than a subtle color-tolerance difference — removes any risk of the test accidentally passing
  due to a coincidentally-small numeric difference.
- The header comment states the pre-fix bug's exact behavior ("CNA's skinning shaders always summed
  all 4 weight/index pairs unconditionally") and this audit independently confirmed, by reading the
  actual shader source, that the described fix (`if(uWeightsPerVertex>=2)`/`if(uWeightsPerVertex>=4)`
  gating) is genuinely present and correctly gates the described slots.

## Final Assessment

The strongest-designed `SkinnedEffect` test in this shard — a genuinely adversarial regression test for
a specific, previously-real bug, with margins large enough that no plausible tolerance/precision issue
could mask a regression. Only gap is the shared, low-severity missing retry-guard (F1).
