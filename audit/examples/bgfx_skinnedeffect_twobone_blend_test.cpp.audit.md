# Audit: examples/bgfx_skinnedeffect_twobone_blend_test.cpp

## Metadata

- Source file: `examples/bgfx_skinnedeffect_twobone_blend_test.cpp` (164 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `SkinnedEffect` two-bone weighted-blend pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_skinnedeffect_twobone_blend …)` /
  `cna_register_backend_test(NAME Bgfx_SkinnedEffect_TwoBoneBlend …)`, `cmake/Tests/BgfxTests.cmake:249-253`).
- XNA/FNA relevance: direct — `SkinnedEffect`'s linear-blend-skinning weighted sum of 2 bones,
  `WeightsPerVertex=2`.
- FNA reference: `HLSL/SkinnedEffect.fx`'s `Skin(vin, boneCount)`:
  `skinning += Bones[vin.Indices[i]] * vin.Weights[i]` summed for `i < boneCount`.
- Related production code: `SkinnedEffect.cpp` (`FillGpuDrawParams` line 395,
  `p.weightsPerVertex = weightsPerVertex_`), `vs_skinned3d.sc` lines 16-23 (`if (weightsPerVertex >=
  2.0) skinMat += u_bones[a_indices.y] * a_weight.y`).

## Purpose

Task 408's pixel test: two bones (`Translate(-0.5,0,0)` at index 0, `Translate(1.5,0,0)` at index
1) are blended per-vertex with weights `0.5/0.5` (`WeightsPerVertex=2`). A correct linear blend of
two pure-translation matrices with weights summing to 1 is itself a pure translation by the
weighted average: `0.5*(-0.5) + 0.5*(1.5) = +0.5`, identical to the single-bone-translation
sibling's own `+0.5` shift — the same expected pixel pattern (`left=green, centre=textured,
right=green`) is reused deliberately so this test isolates *only* "does the 2-bone weighted-sum
math work", holding the net displacement constant across the two test files for direct comparison.

## Executive Verdict

**Healthy** — the weighted-blend math is correct and was independently re-derived by this audit;
the vertex data (`w0=w1=0.5`, `i0=0,i1=1`) is a genuine 2-slot exercise (not a disguised 1-slot
case), and `WeightsPerVertex=2` gates the shader to read exactly those two slots
(`vs_skinned3d.sc` line 20 `if (weightsPerVertex >= 2.0)`), matching FNA's `Skin(vin, 2)` semantics.

## Checklist Results

### API / XNA / FNA parity
`fx.SetBoneTransforms({bone0, bone1})` (2-element vector) plus `fx.setWeightsPerVertexProperty(2)`
matches FNA's real per-vertex bone-count semantics precisely — real XNA content authored for
2-bone skinning would set exactly this combination.

### Behavioral correctness
Re-derived: `skinMat = Bones[0]*0.5 + Bones[1]*0.5`. For pure-translation affine matrices, a
weighted sum with weights summing to 1 is algebraically identical to interpolating the translation
components directly (the rotation/scale 3×3 block is `Identity*0.5 + Identity*0.5 = Identity`,
translation `= -0.5*0.5 + 1.5*0.5 = 0.5`). Applying `mul(skinMat, vec4(pos,1))` to the original
`x∈[-1,0]` quad yields a shifted quad at `x∈[-0.5,0.5]` — identical to the single-bone-translation
test's own result, confirming the file's own stated design intent ("matching Task 407's own result"
— independently verified true, not merely asserted). Sample-point checks
(`left`=green/`centre`=textured/`right`=green) follow directly and were independently re-confirmed
correct by this audit using the same NDC-to-screen mapping as the sibling reports in this batch.

### Logic
Same safe `renderAndRead()` per-region full redraw pattern as the identity-bones/
translation-bone siblings (fresh `Clear`+`Draw`+`GetBackBufferData` per checkpoint, 20-iteration
blank-frame retry).

### C++ correctness
`SkinnedGpuVertex` (52 bytes) matches `MakeBgfxLayout`'s `stride==52` layout exactly. UV values in
this file's vertex array (`0.5f,0.5f` for `w0`,`w1`, `0,0` for `w2`,`w3`) are correctly positioned in
the `w0..w3` fields (not accidentally shifted), and `i0=0,i1=1,i2=0,i3=0` correctly index the
2-element bone vector at slots 0/1 only.

### Robustness
The same `EffectParameter::SetValue(std::vector<Matrix>)` truncation behavior documented in detail
in `bgfx_skinnedeffect_translation_bone_test.cpp.audit.md`'s Robustness section applies here too:
`bonesParam_`'s storage shrinks to exactly 2 matrices' worth of floats after this call, so
`p.boneCount = 2` inside `FillGpuDrawParams()`. Harmless for this test because indices `0` and `1`
are both `< 2`.

### Testing
Effective isolation of the weighted-blend arithmetic itself (as opposed to the single-bone-address
or `WeightsPerVertex`-gating concerns covered by this shard's other siblings).

## Detailed Findings

None at MEDIUM/HIGH/CRITICAL severity.

## Cross-File Observations

- This file's own header comment explicitly states its expected results should numerically match
  `bgfx_skinnedeffect_translation_bone_test.cpp`'s own case — this audit independently confirmed
  that claim is mathematically true for pure-translation bones with weights summing to 1 (not just
  "close by tolerance," but exactly identical net displacement).
- `bgfx_skinnedeffect_weightspervertex_test.cpp` (this same batch) reuses this exact 2-bone
  net-`+0.5`-shift scenario as its own baseline, then adds a 3rd "garbage" bone in slots 2/3 to
  prove `WeightsPerVertex=2` actually gates those slots out — see that file's own report for a
  documented, currently-open pre-existing test failure in that specific file.

## Missing or Weak Tests

None specific to this file's own scope — the "what if weights don't sum to 1" or "3+ bone blend"
cases are reasonably left to other tests/production-code unit tests rather than duplicated here.

## Positive Findings

- Correct, deliberately comparable-by-design test: reusing the exact `+0.5` net shift from the
  single-bone sibling turns this into a genuine differential test of the weighted-sum arithmetic
  specifically, rather than re-testing bone-upload plumbing from scratch.
- Weight values (`0.5/0.5`) are non-trivial (not `1.0/0.0`), so a shader bug that only applied the
  first weight slot (ignoring the second) would produce a visibly different (uncentered) result,
  not accidentally still pass.

## Final Assessment

Correct, well-designed differential test. No defects found.
