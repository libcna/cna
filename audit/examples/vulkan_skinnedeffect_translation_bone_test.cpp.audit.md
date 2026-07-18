# Audit: examples/vulkan_skinnedeffect_translation_bone_test.cpp

## Metadata

- Source file: `examples/vulkan_skinnedeffect_translation_bone_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — Task 407, `SkinnedEffect` single translation bone
  pixel test (Vulkan port; header cites "examples/easygl_skinnedeffect_translation_bone_test.cpp
  for the full derivation").
- File type: hand-rolled `Game`-derived executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_skinnedeffect_translation_bone …)` /
  `cna_register_backend_test(NAME Vulkan_SkinnedEffect_TranslationBone …)`,
  `cmake/Tests/VulkanTests.cmake:325-328`).
- XNA/FNA relevance: direct — `SkinnedEffect.SetBoneTransforms`, real XNA 4.0 API. FNA source:
  `HLSL/SkinnedEffect.fx`'s `Skin(vin, boneCount)`.
- Production code exercised: `SkinnedEffect::SetBoneTransforms`/`setWeightsPerVertexProperty`,
  `shaders/skinned3d_vertexlit.vert.glsl`'s skin-matrix build (lines 54-58, since
  `EnableDefaultLighting()` + the real XNA `PreferPerPixelLighting=false` default routes here).

## Purpose

All 6 vertices are 100%-weighted to bone 0 (`w0=1`, others 0), and bone 0 is a genuine, non-Identity
`Matrix::CreateTranslation(0.5f, 0, 0)` — so `skinMat = 1 * Bones[0] = Translate(+0.5,0,0)`, and the
quad (authored covering NDC `x:-1..0`) should visibly shift to NDC `x:-0.5..0.5` after the vertex
shader applies it.

## Executive Verdict

**Healthy.** The test's own arithmetic (`skinMat = Bones[0]`, translation `+0.5` in X, NDC-to-screen
mapping) is correct, matches the Vulkan pipeline's real stride-52 attribute layout byte-for-byte,
and the three-sample-point (left/centre/right) design gives genuine positional discrimination rather
than a single ambiguous sample.

## Checklist Results

### API / XNA / FNA parity
`fx.SetBoneTransforms(std::vector<Matrix>{Matrix::CreateTranslation(0.5f,0,0)})` and
`fx.setWeightsPerVertexProperty(1)` match FNA's `SkinnedEffect.SetBoneTransforms(Matrix[])`. `1`
is also the class default, so the explicit call is self-documenting rather than functionally
necessary. `MaxBones=72` bound (`SkinnedEffect.hpp` line 28) — a 1-element vector is well within
range; `SetBoneTransforms` (`SkinnedEffect.cpp` lines 294-302) correctly rejects empty/over-`MaxBones`
vectors, neither exercised here (reasonably, a positional test rather than an argument-validation
test).

### Behavioral correctness
`SkinnedGpuVertex`'s field layout (`px,py,pz,nx,ny,nz,u,v,w0..w3,i0..i3`,
`static_assert(sizeof==52)`, lines 30-38) verified field-by-field against
`VulkanGraphicsBackend.cpp`'s `GetOrCreatePipelineSkinned3DVertexLit` attribute descriptions
(offsets 0/12/24/32/48 for pos/normal/uv/weights/indices) — byte-exact match, including the
`R8G8B8A8_UINT` (not `UNORM`) format for `aBoneIndices`, correctly matching the shader's `uvec4`
declaration.

Screen-space math: quad authored at NDC `x∈[-1,0], y∈[-1,1]` (lines 84-91); bone 0 translates `+0.5`
in X; `World=View=Projection=Identity` (lines 74-76), so clip-space = the raw vertex position after
skinning, i.e. the shifted quad occupies NDC `x∈[-0.5,0.5]`. Sample points at `W/8` (NDC≈-0.75,
outside both pre- and post-shift extents), `W/2` (NDC≈0.0, inside the shifted quad), `7W/8`
(NDC≈+0.75, outside) — correctly placed so a shader that silently ignored the bone transform (quad
still at `[-1,0]`) would make the `left` sample land inside the (un-shifted) quad instead of green
background, which the test's `leftOk = leftPx.G > leftPx.R` check would correctly flag as `false`.

### Logic
`RasterizerState::CullNone` (line 70) is set with the same Task-896 rationale comment as every
sibling file in this batch.

### C++ correctness
`static_assert(sizeof(SkinnedGpuVertex) == 52, ...)` — consistent with the shard-wide layout.

### Testing
Single-bone, single-frame, no-lighting-precision assertion (checks color dominance, not exact RGB
values) — an appropriately narrow test for "does a non-Identity bone transform actually move the
vertex," distinct in scope from `vulkan_skinnedeffect_twobone_blend_test.cpp`'s multi-bone blend and
`vulkan_skinnedeffect_weightspervertex_test.cpp`'s garbage-slot-ignoring test. No `Draw()`
retry-until-nonblack loop here (unlike the specular/preferperpixellighting/multilight/vertexcolor
tests in this shard) — see F1.

## Detailed Findings

### F1 — No retry-until-rendered guard against a black first frame, unlike sibling files in this shard

- Severity: LOW
- Confidence: MEDIUM
- Category: robustness / test flakiness
- Location/symbol: `Draw()` (lines 55-127) — single `Clear()`/`DrawPrimitives()`/readback, no loop.
- Evidence: `vulkan_skinnedeffect_specular_test.cpp`, `..._preferperpixellighting_test.cpp`,
  `..._multilight_test.cpp`, and `..._vertexcolor_test.cpp` (this same shard) all wrap their
  draw/readback in an up-to-20-iteration loop specifically to skip a transient all-black frame. This
  file has no equivalent — if the very first `Draw()` call reads back before the swapchain/backbuffer
  is actually populated, `leftPx`/`centPx`/`rightPx` would all read `(0,0,0)`, `leftOk`/`rightOk`
  would both be `false` (green-dominance check fails on all-zero), and the test would report
  `[FAIL]`. Identical to the EasyGL sibling's own F1 (`easygl_skinnedeffect_translation_bone_test.cpp.audit.md`).
- Why it matters: not a logic bug in this file's own math, but an inconsistency in defensive coding
  across near-identical sibling test files in the same shard, increasing the chance of an
  intermittent, non-reproducible CI failure being misdiagnosed as a real regression.
- Suggested future action (not implemented by this audit): adopt the same retry-until-nonblack loop
  convention already established by this shard's other files.

## Cross-File Observations

- Shares this shard's Identity-`World` convention, so — like every other file in this batch — it
  cannot exercise or detect the missing world-space normal-transform defect (F2) or the
  ambient/emissive-forwarding defect (F1) documented in
  `vulkan_skinnedeffect_preferperpixellighting_test.cpp.audit.md`. Low practical relevance to this
  specific file, since its own pass condition only checks color-channel dominance for position, not
  an exact lit value.
- `SkinnedGpuVertex`'s stride-52 layout is independently re-declared (not shared via a common header)
  in at least 5 files across this shard — consistent today (all verified byte-identical), the same
  maintainability observation already recorded for the EasyGL shard's equivalent files applies here.

## Missing or Weak Tests

- See F1 (no retry-until-rendered guard).
- No test in this file exercises a bone index other than 0, or a weight other than exactly 1.0/0.0 —
  covered instead by the two-bone/weights-per-vertex sibling files.

## Positive Findings

- Three-sample-point design (left/centre/right) gives genuine positional proof, not just "centre
  pixel looks textured" — actively distinguishes "shader ignored the bone transform" from "shader
  applied it correctly."
- Vertex attribute layout independently verified byte-exact against the real Vulkan pipeline
  creation code.

## Final Assessment

A correct, well-targeted test for single-bone translation, faithfully ported from its EasyGL
sibling; its only real gap is the missing retry-until-rendered guard (F1, LOW), a flakiness-hardening
inconsistency rather than a logic defect.
