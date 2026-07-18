# Audit: examples/bgfx_skinnedeffect_translation_bone_test.cpp

## Metadata

- Source file: `examples/bgfx_skinnedeffect_translation_bone_test.cpp` (161 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `SkinnedEffect` single-bone-translation pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_skinnedeffect_translation_bone …)` /
  `cna_register_backend_test(NAME Bgfx_SkinnedEffect_TranslationBone …)`, `cmake/Tests/BgfxTests.cmake:243-247`).
- XNA/FNA relevance: direct — `SkinnedEffect::SetBoneTransforms()`/`Skin(vin, boneCount)` single-bone
  translation.
- FNA reference: `HLSL/SkinnedEffect.fx`'s `Skin(vin, boneCount)` (`vin.Position.xyz = mul(vin.Position, skinning)`).
- Related production code: `SkinnedEffect.cpp` (`SetBoneTransforms`/`FillGpuDrawParams` lines
  294-302, 386-395), `vs_skinned3d.sc` (skinning math), `EffectParameter.cpp`
  (`SetValue(const std::vector<Matrix>&)`/`GetValueMatrixArray`, lines 73-83, 175-186).

## Purpose

Task 407's pixel test: a single custom bone (`Matrix::CreateTranslation(0.5f, 0, 0)`) is bound via
`SetBoneTransforms({bone})` with `WeightsPerVertex=1`, applied to the same left-half NDC quad used
by the identity-bones sibling. A pure `+0.5` X-translation shifts the quad from `x∈[-1,0]` to
`x∈[-0.5,0.5]`, so the three sample points (`W/8`→NDC `-0.75`, `W/2`→NDC `0`, `7W/8`→NDC `+0.75`)
flip from "left=textured, centre=textured, right=green" (identity-bones case) to "left=green,
centre=textured, right=green" — directly proving the bone transform actually reaches the GPU and
moves the mesh, not merely that the property setter stores a value.

## Executive Verdict

**Healthy** — the geometry/region math is correct (independently re-derived below), the vertex
struct matches the production stride-52 layout, and the single-bone `SetBoneTransforms` call is
safe in this specific test given `WeightsPerVertex=1` and all vertex indices addressing bone 0 only
(see Cross-File Observations for a related, currently-dormant `EffectParameter` behavior worth
flagging for whoever owns that file).

## Checklist Results

### API / XNA / FNA parity
`fx.SetBoneTransforms(std::vector<Matrix>{ Matrix::CreateTranslation(0.5f, 0, 0) })` — a
single-element vector — matches FNA's own permissive `SetBoneTransforms(Matrix[] boneTransforms)`
contract (only throws for a null/empty array or `Length > MaxBones`; a shorter-than-`MaxBones`
array is explicitly legal real XNA usage for skeletons with fewer bones than the maximum).

### Behavioral correctness
Re-derived the shift: original quad `x∈[-1,0]`; bone applies `+0.5` translation to every vertex
(all vertices reference bone index 0 with weight 1.0, `WeightsPerVertex=1`); shifted quad
`x∈[-0.5,0.5]`. Sample points: `leftReg` NDC `-0.75` (outside `[-0.5,0.5]` → green background,
matches `leftOk = G>R`); `centReg` NDC `0.0` (inside `[-0.5,0.5]` → red-dominant textured, matches
`centOk = R>G && R>50`); `rightReg` NDC `+0.75` (outside → green, matches `rightOk = G>R`). This
audit independently confirms all three assertions are geometrically correct for the stated bone
transform.

### Logic
Uses the same per-region `renderAndRead()` (fresh `Clear`+`Draw`+`GetBackBufferData` per checkpoint,
20-iteration blank-frame retry) as the identity-bones sibling — the documented-safe Bgfx readback
pattern.

### C++ correctness
`SkinnedGpuVertex` (52 bytes) matches `MakeBgfxLayout`'s `stride==52` case exactly, verified against
`BgfxGraphicsBackend.cpp` lines 2012-2026.

### Robustness
See Cross-File Observations: `SetBoneTransforms` with a 1-element vector, combined with
`EffectParameter::SetValue(const std::vector<Matrix>&)`'s `floatData_.clear()`-then-rebuild
behavior (`EffectParameter.cpp` lines 175-186), means `bonesParam_`'s underlying storage shrinks to
exactly 16 floats (one matrix) after this call — `SkinnedEffect::GetBoneTransforms(MaxBones)` (called
internally by `FillGpuDrawParams()`) will then return a vector of size **1**, not 72, because
`GetValueMatrixArray(count)`'s loop condition `(i+1)*16 <= floatData_.size()` only satisfies `i=0`.
`FillGpuDrawParams()` sets `p.boneCount = bones.size()` accordingly (`= 1`), so only bone slot 0 is
uploaded to the GPU's `u_bones[72]` uniform array this frame. This is harmless for *this specific
test* because every vertex's `a_indices` field is `(0,0,0,0)` and `WeightsPerVertex=1` (only slot 0
is ever read by the shader), but it is a latent divergence from real FNA's simpler mental model
(where `GetBoneTransforms(count)` returns `count` valid matrices regardless of how many were last
`Set`, with untouched slots retaining their previous value rather than being truncated away). Not
raised as a finding against *this* file (it does not misbehave), but flagged for
`EffectParameter.cpp`/`SkinnedEffect.cpp`'s own audit shard.

### Testing
Effective, minimal, single-purpose test — proves bone-transform data genuinely reaches the GPU
skinning stage (as opposed to being silently dropped, matching what a real `SetBoneTransforms` bug
would look like).

## Detailed Findings

None at MEDIUM/HIGH/CRITICAL severity for this file. See Cross-File Observations for a dormant,
out-of-scope-for-this-file `EffectParameter` behavior worth a follow-up elsewhere.

## Cross-File Observations

- Same `EffectParameter::SetValue(std::vector<Matrix>)` truncation behavior noted here also applies
  to `bgfx_skinnedeffect_twobone_blend_test.cpp` (2-element vector),
  `bgfx_skinnedeffect_specular_test.cpp`/`bgfx_skinnedeffect_preferperpixellighting_test.cpp`
  (1-element identity-bone vector), and `bgfx_skinnedeffect_vertexcolor_test.cpp` (1-element
  identity-bone vector) — all in this same batch. In every one of these 5 files, the referenced
  vertex bone indices stay strictly within the truncated `boneCount`, so none of them currently
  exhibits incorrect rendering from this behavior. It is documented once here in detail and
  referenced (not re-derived) from the other files' own reports.
- `bgfx_skinnedeffect_identity_bones_test.cpp` avoids this entirely by never calling
  `SetBoneTransforms()` at all (relying on the constructor's full 72-identity-matrix
  initialization), which is the more robust test-authoring choice among this shard's siblings.

## Missing or Weak Tests

None specific to this file's own scope.

## Positive Findings

- Correctly isolates a single-bone translation from the multi-bone blend case (covered separately
  by the two-bone-blend sibling), giving clean, independent regression coverage per skinning
  scenario.
- The expected-result inversion relative to the identity-bones test (`left` flips from
  textured→green, `right` stays green, `centre` stays textured) is a well-chosen discriminating
  design: a broken bone-upload path would very likely leave the quad at its unmoved position,
  which this test's `leftOk`/`centOk` pairing would catch immediately.

## Final Assessment

Correct, minimal, well-targeted test. No defects found.
