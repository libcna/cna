# Audit: examples/bgfx_skinnedpbreffect_test.cpp

## Metadata

- Source file: `examples/bgfx_skinnedpbreffect_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `SkinnedPbrEffect` (PBR + skinning combo) pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_skinnedpbreffect …)` / `cna_register_backend_test(NAME Bgfx_SkinnedPbrEffect …)`,
  `cmake/Tests/BgfxTests.cmake:895-897`).
- XNA/FNA relevance: indirect — `SkinnedPbrEffect` is a `NOXNA` CNA extension (glTF-style metallic-roughness PBR
  combined with `IEffectMatrices`/skinning), not an XNA 4.0 stock effect, but its skinning behavior
  (`WeightsPerVertex`, `SetBoneTransforms`) mirrors `SkinnedEffect`'s real XNA semantics.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.cpp`,
  `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` (`MakeBgfxLayout` stride-68 case, lines 2070-2082;
  PBR-skinned dispatch branch, lines 2481-2520), `src/CNA/Internal/Backends/Bgfx/shaders/vs_pbr_skinned3d.sc`,
  `shaders/fs_pbr3d.sc` (shared fragment stage with `PbrEffect`).
- Oracle file: `examples/bgfx_pbreffect_test.cpp` (unskinned `PbrEffect` pixel test — this file's own header
  states its 5 expected values are taken directly from that file, not independently derived).

## Purpose

5-check pixel test proving `SkinnedPbrEffect`'s Bgfx shader path (`vs_pbr_skinned3d.sc` feeding the same
`fs_pbr3d.sc` fragment stage as unskinned `PbrEffect`) renders correctly end-to-end through a real GPU draw,
using the stride-68 `VertexPositionNormalTangentTextureSkinned` layout (Position+Normal+Tangent+TexCoord+
BlendWeight+BlendIndices). All 6 vertices carry a single bone index (0) at weight 1.0 with an identity bind
pose — a mathematical no-op for the skin transform — so the file's own claim is that the rendered pixels must
exactly match `bgfx_pbreffect_test.cpp`'s already-verified unskinned values, proving the skin-matrix multiply in
`vs_pbr_skinned3d.sc` is wired correctly (any bug there would shift/darken the result relative to the unskinned
test) without needing a fresh hand-derivation.

## Executive Verdict

**Healthy** — this audit independently confirmed the file's central claim: all 5 expected `Color` constants
((79,79,79), (91,91,91), (20,0,0), (79,1,1), (51,77,102)) are byte-for-byte identical to
`bgfx_pbreffect_test.cpp`'s own hand-derived-and-cross-checked BRDF values, and the stride-68 vertex layout,
`SkinnedPbrGpuVertex` struct, and `MakeBgfxLayout(68)` field order all match exactly. One real, if modest,
coverage gap: no pixel test anywhere in the codebase (this file, `easygl_skinnedpbreffect_golden_test.cpp`, or
any other backend's `*_skinnedpbreffect_test.cpp`) exercises a **non-identity** bone transform for
`SkinnedPbrEffect`, unlike plain `SkinnedEffect` which has dedicated `bgfx_skinnedeffect_translation_bone_test.cpp`
and `bgfx_skinnedeffect_twobone_blend_test.cpp`.

## Checklist Results

### API / XNA / FNA parity
N/A in the strict XNA sense (`SkinnedPbrEffect` is a `NOXNA` extension), but its skinning-related surface
(`setWeightsPerVertexProperty`, `SetBoneTransforms`) mirrors `SkinnedEffect`'s real XNA API shape. Confirmed in
`SkinnedPbrEffect.cpp`: `setWeightsPerVertexProperty` rejects anything other than 1/2/4
(`std::out_of_range`, lines 260-266), matching the test's `fx.setWeightsPerVertexProperty(1)` call with no
expected throw.

### Behavioral correctness
Cross-checked line-by-line against `bgfx_pbreffect_test.cpp`:
- Camera rig identical: `CreateLookAt((0,0,3), Zero, (0,1,0))`, `CreatePerspectiveFieldOfView(PiOver4, 1, 0.1,
  100)`.
- Material/light rig identical per check: (A) rough=1.0/metallic=0.0/white/ambient=0 → (79,79,79); (B)
  rough=0.5 → (91,91,91) and asserted strictly brighter than (A); (C) metallic=1.0/red → (20,0,0); (D)
  metallic=0.0/red → (79,1,1), asserted `!matches(d,c,8)`; (E) tilted normal map + ambient=(0.2,0.3,0.4) →
  (51,77,102).
- `SkinnedPbrGpuVertex` (68 bytes: 3+3+4+2+4 floats + 4×`uint8_t`) matches `MakeBgfxLayout`'s stride-68 branch
  field-for-field (`BgfxGraphicsBackend.cpp:2070-2082`): Position(12)+Normal(12)+Tangent(16)+TexCoord0(8)+
  Weight(16)+Indices(4) = 68.
- The dispatch in `BgfxGraphicsBackend.cpp` correctly routes `params.pbr && params.skinned` to
  `pbrSkinned3DProgram_` (lines 2481-2520) **before** the plain `params.pbr` branch (lines 2521+), so
  `SkinnedPbrEffect::FillGpuDrawParams()` setting both `p.pbr=true` and `p.skinned=true` (lines 346-348) is
  guaranteed to hit the intended shader pair.
- `SkinnedPbrEffect::SetBoneTransforms({identity})` with only 1 matrix, then `FillGpuDrawParams()` calling
  `GetBoneTransforms(MaxBones=72)`: verified `EffectParameter::GetValueMatrixArray(count)`
  (`EffectParameter.cpp:73-83`) bounds its loop to `(i+1)*16 <= floatData_.size()`, so it safely returns a
  1-element vector rather than reading uninitialized/garbage memory for the other 71 bones — no UB, and since
  every vertex's `BlendIndices` is 0, bones 1-71 are never referenced by the shader regardless.

### Logic
The header comment's "identity bind pose is a no-op" claim was independently verified: with `weightsPerVertex=1`
and `BlendIndices=(0,0,0,0)` on every vertex, `vs_pbr_skinned3d.sc`'s `skinMat = u_bones[0]*1.0` collapses to the
identity matrix exactly, making `skinnedPos`/`skinnedNormal`/`skinnedTangent` identical to the unskinned
`vs_pbr3d.sc` inputs — the oracle-reuse technique is sound, not just asserted.

### Cross-file consistency
`vs_pbr_skinned3d.sc`'s own comment states it applies "an EXTRA World-space normal/tangent transform after
skinning... an intentional divergence from the regular SkinnedEffect shader" (using `mul(u_world, ...)` directly
rather than the precomputed inverse-transpose normal matrix that unskinned `vs_pbr3d.sc` uses via
`u_normalMatrix`/`ComputeNormalMatrix3x3`). This audit independently confirmed the claim against
`EasyGLGraphicsBackend.cpp::EnsurePbrSkinnedProgram()` (line ~3778: `vNormal=normalize(mat3(uWorld)*(skinNormalMat*aNormal))`)
— the simplification is genuinely cross-backend-consistent and documented, not a Bgfx-only shortcut or a stale
comment. It is invisible in this test because `World=Identity` throughout (uniform scale trivially), so this
file cannot itself demonstrate whether the simplification's known limitation (incorrect normals under
non-uniform World scale) is a live concern — see Missing Tests below.

### Testing
Strong: 5 checks, tolerance `±8` matching the oracle file's own tolerance, `check(b.R > a.R, …)` and
`check(!matches(d,c,8), …)` used as differential assertions (not just absolute-value checks) exactly as in the
oracle, `readCenter`'s up-to-20-frame retry loop for Bgfx's known "first read per frame" readback quirk is
present and correctly gated on non-black pixel detection.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM defects found in this file. One LOW/testing-coverage observation:

### F1 — No pixel-level test anywhere exercises a non-identity bone transform for `SkinnedPbrEffect`
- Severity: LOW
- Confidence: HIGH (verified by grep across `examples/*.cpp` for `SkinnedPbrEffect` combined with
  `CreateRotation`/`CreateTranslation`/`CreateScale` — no matches; `easygl_skinnedpbreffect_golden_test.cpp`,
  the cross-backend "golden" reference this family reuses, is also identity-bone-only)
- Category: test-coverage
- Location/symbol: whole file (by design, per its own header comment)
- Evidence: `bones = { Matrix::getIdentityProperty() }` (line 107) is the only bone transform ever exercised
  in this file or its EasyGL sibling; `SkinnedEffect` (non-PBR) by contrast has dedicated
  `bgfx_skinnedeffect_translation_bone_test.cpp` and `bgfx_skinnedeffect_twobone_blend_test.cpp`.
- Why it matters: the arithmetic correctness of the bone-palette skin transform *applied to Position, Normal,
  and Tangent together* under a genuinely non-trivial bone matrix (e.g. a rotation) is never validated at the
  pixel level for the PBR+skinning combo specifically — only the CPU-side property API
  (`SetBoneTransforms`/`GetBoneTransforms` round-trip, MaxBones bounds) is unit-tested
  (`tests/Microsoft/Xna/Framework/Graphics/SkinnedPbrEffectTests.cpp`). A bug that scaled or rotated
  `skinnedNormal`/`skinnedTangent` incorrectly relative to `skinnedPos` (e.g. an indexing mismatch between the
  three per-attribute skin-matrix multiplies in `vs_pbr_skinned3d.sc` lines 27-44) would not be caught by any
  current test, since an identity bone is a no-op for all three attributes simultaneously and can't distinguish
  a correctly-applied-per-attribute skin from a broken one.
- FNA/XNA comparison: N/A (`SkinnedPbrEffect` is a `NOXNA` extension; the closest XNA analogue, `SkinnedEffect`,
  *is* covered with non-identity bones elsewhere in this codebase).
- Suggested future action (not implemented by this audit): add one more check reusing a non-identity single
  bone (e.g. a small rotation) with a hand-derivable or golden-captured expected pixel, mirroring the existing
  `bgfx_skinnedeffect_translation_bone_test.cpp` pattern but for the PBR shader pair.

## Missing or Weak Tests

See F1. Otherwise the 5 checks in this file are proportionate to its stated, narrower purpose (proving the
skin-transform wiring is a no-op under identity, not proving skin-transform correctness in general).

## Positive Findings

- The "reuse a proven oracle" test-design technique is executed correctly and its precondition (identity bind
  pose ⇒ mathematically no-op skin transform) was independently verified rather than taken on faith.
- Cross-backend consistency between Bgfx's `vs_pbr_skinned3d.sc` and EasyGL's `EnsurePbrSkinnedProgram()` for
  the documented normal-matrix simplification was independently confirmed, refuting (in the sense of finding
  no evidence for) the possibility that this is a Bgfx-only shortcut or a stale/inaccurate comment — a pattern
  this audit was specifically primed to look for.
- The stride-68 vertex layout is verified consistent across the test's own `static_assert`, the CPU-side
  `SkinnedPbrGpuVertex` field order, and `MakeBgfxLayout`'s stride-68 branch.

## Final Assessment

A well-targeted, honestly-scoped regression test for the Bgfx PBR+skinning shader combo; its one gap (no
non-identity bone coverage) is a real absence but is consistent with the same gap in every other backend's
equivalent test, so it is a project-wide test-family limitation rather than a defect specific to this file.
