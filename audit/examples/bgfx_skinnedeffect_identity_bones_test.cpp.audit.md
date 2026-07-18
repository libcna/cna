# Audit: examples/bgfx_skinnedeffect_identity_bones_test.cpp

## Metadata

- Source file: `examples/bgfx_skinnedeffect_identity_bones_test.cpp` (158 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `SkinnedEffect` identity-bone-palette pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_skinnedeffect_identity_bones …)` /
  `cna_register_backend_test(NAME Bgfx_SkinnedEffect_IdentityBones …)`, `cmake/Tests/BgfxTests.cmake:237-241`).
- XNA/FNA relevance: direct — `SkinnedEffect`'s default 72-identity-matrix bone palette
  (`SkinnedEffect(GraphicsDevice)` constructor) must leave an unskinned mesh visually unmoved.
- FNA reference: `Graphics/Effect/StockEffects/SkinnedEffect.cs` constructor (`identityBones` loop,
  `SetBoneTransforms`), `HLSL/SkinnedEffect.fx`'s `Skin(vin, boneCount)`.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/SkinnedEffect.cpp` (constructor,
  `GetBoneTransforms`/`SetBoneTransforms`, `FillGpuDrawParams` lines 386-390), `vs_skinned3d.sc`
  (skinning math lines 16-23), `BgfxGraphicsBackend.cpp`'s `MakeBgfxLayout(stride==52)` (lines
  2012-2026).

## Purpose

A 3-point pixel test (`Task 406`) proving that `SkinnedEffect`'s default bone palette (72 identity
matrices, set in the constructor) is a genuine no-op transform: a red-textured quad occupying NDC
`x ∈ [-1, 0]` (screen left half) is drawn with `WeightsPerVertex=1`, `EnableDefaultLighting()`, and
default (identity) bones, and three 1×1 regions are sampled — left (`W/8`, inside the quad),
centre (`3W/8`, also inside the quad since it spans the whole left half), and right (`7W/8`,
outside the quad, background green). Both `left` and `centre` are asserted red-dominant
(`R > G && R > 50`), and `right` is asserted green-dominant — proving the quad rendered exactly
where it was authored, undisturbed by the identity bone-palette pass.

## Executive Verdict

**Healthy** — the test's own geometry/region math is internally consistent (both sample points at
NDC `-0.75` and `-0.25` fall inside the quad spanning `[-1, 0]`), the vertex struct
(`SkinnedGpuVertex`, 52 bytes) matches `MakeBgfxLayout`'s `stride==52` case field-for-field, and the
identity-bone code path (default constructor, no `SetBoneTransforms` call) is genuinely exercised
end-to-end. The red/green coarse-threshold check is a deliberately weak but appropriately-scoped
assertion for "did the quad move" — it does not (and does not need to) verify exact lighting math,
which is covered by the dedicated specular/multilight/preferperpixellighting siblings in this same
shard.

## Checklist Results

### API / XNA / FNA parity
`fx.setWeightsPerVertexProperty(1)`, `fx.EnableDefaultLighting()`, `fx.setTextureProperty(&tex_)`
all map directly to FNA's `SkinnedEffect` public surface. The test deliberately never calls
`SetBoneTransforms()`, exercising the constructor's own identity-bone initialization path
(`SkinnedEffect.cpp` lines 47-49: `std::vector<Matrix> identityBones(MaxBones, Matrix::getIdentityProperty()); SetBoneTransforms(identityBones);`) — this is the correct way to test "default bones", not a shortcut.

### Behavioral correctness
Geometry check: quad vertices span `x ∈ [-1, 0]`, `y ∈ [-1, 1]` (two triangles,
`TL(-1,1)-BL(-1,-1)-BR(0,-1)` and `TL(-1,1)-BR(0,-1)-TR(0,1)`). NDC→screen mapping:
`screenX = (ndcX+1)/2 * W`. `leftReg` at `W/8` → NDC `x = -0.75` (inside `[-1,0]`); `centReg` at
`3W/8` → NDC `x = -0.25` (inside `[-1,0]`); `rightReg` at `7W/8` → NDC `x = +0.75` (outside
`[-1,0]`). This matches the file's own asserted expectations (`left=textured, centre=textured
(quad unmoved), right=green`) exactly — independently re-derived by this audit, not merely taken on
faith.

### Logic
`renderAndRead()` performs a fresh `Clear`+`Apply`+`SetVertexBuffer`+`Draw`+`GetBackBufferData` for
*each* of the 3 sampled regions (called 3 separate times from `Draw()`), each wrapped in its own
20-iteration retry loop that breaks once a non-black pixel is read. This is the safe pattern
documented by this shard's own header comment (Task 406 finding: "Bgfx's readback only reliably
reflects a single fresh `GetBackBufferData()` call per rendered frame") — each checkpoint gets its
own full render pass, unlike `bgfx_skinnedeffect_weightspervertex_test.cpp` in this same batch
(see that file's own report for a finding about the one sibling that does *not* follow this
pattern).

### C++ correctness
`SkinnedGpuVertex` is `static_assert`-verified at 52 bytes, matching `MakeBgfxLayout`'s
`stride==52` layout (`Position`×3f, `Normal`×3f, `TexCoord0`×2f, `Weight`×4f, `Indices`×4×u8) field
order exactly — cross-checked directly against `BgfxGraphicsBackend.cpp` lines 2012-2026.

### Robustness
The 20-iteration "skip blank/black frames" retry loop is a pragmatic defense against a possibly
slow-to-appear first frame on this backend (matches the pattern used throughout this shard), not
a correctness workaround for a production bug.

### Testing
This file exercises: default bone-palette identity transform, `WeightsPerVertex=1` dispatch (only
the first weight/index slot contributes, though trivially so here since only bone 0 is ever
addressed), stride-52 vertex layout, and `EnableDefaultLighting()`'s three-light rig producing a
plausible lit/textured (red-dominant) result. It intentionally does *not* verify exact pixel values
— that responsibility sits with `bgfx_skinnedeffect_specular_test.cpp` /
`bgfx_skinnedeffect_multilight_test.cpp` / `bgfx_skinnedeffect_preferperpixellighting_test.cpp` in
this same batch, all three of which this audit independently verified numerically.

## Detailed Findings

None at HIGH/CRITICAL severity. No MEDIUM findings identified against this specific file (the
coarse-threshold assertion style is a deliberate, appropriate design choice for a "did the geometry
move" test, not a defect).

## Cross-File Observations

- The file's header comment ("Task 364/884... Bgfx's default RasterizerState cull state is the
  only one of the 3 backends that actually matches FNA's real `CullCounterClockwiseFace` default")
  was independently spot-checked against `BgfxGraphicsBackend.cpp`'s `ApplyRasterizerState()`
  (lines 1773-1782: `CullMode::CullClockwiseFace(1)→BGFX_STATE_CULL_CW`,
  `CullCounterClockwiseFace(2)→BGFX_STATE_CULL_CCW`) — the mapping is correct, and the comment's
  characterization (Bgfx being *more* FNA-correct than its EasyGL/Vulkan siblings, hence needing an
  explicit `CullNone` workaround for this test family's NDC quad winding) is accurate, not a stale
  claim.
- Shares its `SkinnedGpuVertex` struct definition verbatim (byte-for-byte, including field order)
  with `bgfx_skinnedeffect_translation_bone_test.cpp` and
  `bgfx_skinnedeffect_twobone_blend_test.cpp` in this same batch — a maintenance duplication (4
  independent copies of the same 52-byte struct across this shard alone) but not a correctness
  issue; each copy was independently re-verified against `MakeBgfxLayout` in this audit.

## Missing or Weak Tests

None beyond what's already covered by this file's siblings in the same shard (specular/multilight/
preferperpixellighting own the precise numeric coverage; this file's job is specifically the
identity-bone geometric no-op, which it fulfills).

## Positive Findings

- The 3-region NDC math was independently re-derived by this audit and found correct without
  discrepancy — a genuinely careful, self-consistent geometric test design.
- Explicitly isolates the identity-bone-palette code path (no `SetBoneTransforms()` call at all)
  rather than passing an explicit 72-identity-matrix vector, which would additionally have
  exercised `EffectParameter::SetValue(std::vector<Matrix>)` at full `MaxBones` length — a subtlety
  worth flagging for the shard's translation/two-bone tests instead (see their own reports).

## Final Assessment

A clean, correctly-derived, appropriately-scoped geometric regression test. No defects found in
either the test file or the identity-bone code path it exercises.
