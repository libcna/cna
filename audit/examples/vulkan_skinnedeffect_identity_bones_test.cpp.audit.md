# Audit: examples/vulkan_skinnedeffect_identity_bones_test.cpp

## Metadata

- Source file: `examples/vulkan_skinnedeffect_identity_bones_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — Task 406, `SkinnedEffect` default-bone-palette pixel
  test (Vulkan port of the EasyGL original)
- File type: hand-rolled `Game`-derived executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_skinnedeffect_identity_bones …)` /
  `cna_register_backend_test(NAME Vulkan_SkinnedEffect_IdentityBones …)`,
  `cmake/Tests/VulkanTests.cmake:319-322`).
- XNA/FNA relevance: confirms `SkinnedEffect`'s default bone-palette behavior (every one of 72
  slots defaults to `Matrix.Identity`) matches real XNA/FNA's own `SkinnedEffect` default, where an
  un-set bone palette is a no-op transform. FNA source: `SkinnedEffect.cs`'s constructor,
  `HLSL/SkinnedEffect.fx`'s `Skin()`.
- Related production code: `SkinnedEffect::SkinnedEffect()` (`SkinnedEffect.cpp` lines 36-50,
  `SetBoneTransforms(identityBones)` seeding all 72 slots), `VulkanGraphicsBackend.cpp`'s
  `needsSkinned` dispatch (lines 7370, 7460-7494) and `GetOrCreatePipelineSkinned3DVertexLit`
  (stride-52 vertex layout, offsets confirmed byte-identical to this file's `SkinnedGpuVertex`),
  `shaders/skinned3d_vertexlit.vert.glsl` (the real default dispatch target since
  `PreferPerPixelLighting=false` and `EnableDefaultLighting()` are both in effect here).

## Purpose

Nearly line-for-line the same test as `examples/easygl_skinnedeffect_identity_bones_test.cpp`
(its own header explicitly says "See examples/easygl_skinnedeffect_identity_bones_test.cpp for the
full derivation"), ported to exercise the Vulkan backend instead. Confirms that constructing a
`SkinnedEffect` and drawing **without ever calling `SetBoneTransforms()`** leaves the mesh
completely undeformed — the quad, 100%-weighted to bone 0 (`w0=1`, others 0), stays at its
authored NDC extent `x:[-1,0], y:[-1,1]` because bone 0's un-set default is `Matrix.Identity`.

## Executive Verdict

**Healthy.** The 3-sample-point design (left/centre inside the un-moved quad, right on background)
correctly proves both "the quad rendered" and "the quad did not move," matching the EasyGL sibling's
already-verified logic byte-for-byte on the CPU-side geometry, with the Vulkan-specific
`RasterizerState::CullNone` requirement (Task 896) applied identically.

## Checklist Results

### API / XNA / FNA parity
Deliberately exercises `SkinnedEffect` *without* calling `SetBoneTransforms` — confirmed against
`SkinnedEffect.cpp` lines 47-49: `std::vector<Matrix> identityBones(MaxBones,
Matrix::getIdentityProperty()); SetBoneTransforms(identityBones);` — the constructor itself seeds
every one of the 72 slots to Identity, matching real XNA's own documented default.

### Behavioral correctness
Quad authored at NDC `x:[-1,0], y:[-1,1]` (lines 81-88), all 6 vertices `w0=1, w1=w2=w3=0, i0=0`.
Traced against `skinned3d_vertexlit.vert.glsl` (the real dispatch target here, since
`EnableDefaultLighting()` implies `lightingEnabled=true` and this file never sets
`PreferPerPixelLighting=true`, so XNA's real default `false` routes to the vertex-lit shader per
`VulkanGraphicsBackend.cpp` line 7427's `preferVertexLit = params.lightingEnabled &&
!params.preferPerPixelLighting`): `skinMat = bb.bones[0]*1.0` (line 54 of the vertex shader, the
`weightsPerVertex>=2.0`/`>=4.0` branches never trigger since `weightsPerVertex_` defaults to `1`
and this file never calls `setWeightsPerVertexProperty`... **actually it does**, line 77:
`fx.setWeightsPerVertexProperty(1)` — explicit but harmless, matching the class default). With bone
0 = Identity, `skinnedPos = aPos` unchanged, so the quad renders exactly at its authored position.
- `leftReg` (NDC ≈ -0.75): inside `[-1,0]` → textured/lit (red-dominant).
- `centReg` (NDC ≈ -0.25, at `3*W/8`): inside `[-1,0]`, with margin from the quad's own right edge
  at NDC=0 — the same deliberate anti-off-by-one placement as the EasyGL original.
- `rightReg` (NDC ≈ +0.75): outside `[-1,0]` → green background.
All three assertions are correctly derived from the geometry and default-bone-palette claim.

### Logic
No `SetBoneTransforms()` call anywhere in this file — confirmed by re-reading the full 135 lines;
the omission is the actual feature under test, matching the header comment's own framing.

### Memory/resource lifetime
`VertexBuffer vb(device, 6)` locally scoped, standard RAII; `Texture2D tex_` is a member initialized
in `Initialize()` and outlives the single `Draw()` call — no dangling-reference risk given the
single-frame, `Exit()`-on-first-`Draw()` lifecycle.

### C++ correctness
`static_assert(sizeof(SkinnedGpuVertex) == 52)` (line 38) — verified against
`VulkanGraphicsBackend.cpp`'s `GetOrCreatePipelineSkinned3D`/`...VertexLit` attribute descriptions
(lines 5214-5218): `aPos`(0,R32G32B32_SFLOAT) / `aNormal`(12,R32G32B32_SFLOAT) /
`aUV`(24,R32G32_SFLOAT) / `aBoneWeights`(32,R32G32B32A32_SFLOAT) /
`aBoneIndices`(48,R8G8B8A8_UINT) — every offset matches this struct's field layout exactly,
including the `uint8_t` (unnormalized `UINT` format, not `UNORM`) treatment of the bone-index
attribute, which is required since the vertex shader declares `aBoneIndices` as `uvec4` (an integer
attribute, not a normalized float) — confirmed correct.

### Architecture
No explicit `GraphicsDeviceManager` construction (relies on `Game`'s fallback device, same as the
EasyGL sibling); reads `vp.getWidthProperty()/getHeightProperty()` dynamically rather than
hardcoding a size, so this is safe regardless of the fallback backbuffer's actual dimensions.

### Maintainability
Clean, single-purpose file; the `EnableDefaultLighting()` + "red-dominant" predicate (rather than an
exact byte match) is a reasonable, disclosed trade-off given the 3-light Phong math's complexity —
same choice the EasyGL sibling makes.

### Robustness
No retry-until-rendered guard (unlike `vulkan_skinnedeffect_specular_test.cpp`,
`..._preferperpixellighting_test.cpp`, `..._multilight_test.cpp`, and `..._vertexcolor_test.cpp`,
all of which loop up to 20 frames skipping an all-black readback). Since this file's pass condition
(`leftOk`/`centOk` require `R>G && R>50`) would read `false` on a stray all-black first frame
(`(0,0,0)` fails `R>50`), a transient swapchain/first-frame flash could produce a false `[FAIL]` —
see F1, shared with three sibling files in this batch.

### Testing
This file is itself the test; correctly complements the not-in-this-batch translation-bone/
two-bone tests by testing the *opposite* configuration (no explicit bones set).

### Cross-file consistency
Carries the same `RasterizerState::CullNone` Task-896 comment as every sibling in this batch and in
the EasyGL original. Not detectable by this file (Identity `World`, loose R-dominance check only),
but two real production concerns surfaced elsewhere in this batch also apply to the shader path this
file exercises — see Cross-File Observations.

## Detailed Findings

### F1 — No retry-until-rendered guard against a black first frame

- Severity: LOW
- Confidence: MEDIUM
- Category: robustness / test flakiness
- Location/symbol: `Draw()` (lines 55-124) — single `Clear()`/`DrawPrimitives()`/readback, no loop.
- Evidence: `vulkan_skinnedeffect_specular_test.cpp`, `..._preferperpixellighting_test.cpp`,
  `..._multilight_test.cpp`, and `..._vertexcolor_test.cpp` in this same shard all wrap their
  draw/readback in an up-to-20-iteration loop specifically to skip a transient all-black frame
  (`if (got.R != 0 || got.G != 0 || got.B != 0) break;`). This file has no equivalent.
- Why it matters: not a logic bug in this file's own math, but an inconsistency in defensive coding
  across sibling test files in the same shard that increases the chance of an intermittent,
  non-reproducible CI failure being misdiagnosed as a real regression.
- Suggested future action (not implemented by this audit): adopt the same retry-until-nonblack loop
  convention already established by this shard's other files.

## Cross-File Observations

- This file, `vulkan_skinnedeffect_translation_bone_test.cpp`, `..._twobone_blend_test.cpp`, and
  `..._weightspervertex_test.cpp` all use `EnableDefaultLighting()` + a loose `R>G && R>50` pass
  condition, so none of them can detect two real production concerns this audit confirmed while
  tracing this shard's shared shader code (both documented in detail in
  `vulkan_skinnedeffect_preferperpixellighting_test.cpp.audit.md`'s Detailed Findings): (1)
  `SkinnedEffect::FillGpuDrawParams()` never populates `GpuDrawParams::ambientColor`, and the Vulkan
  skinned shaders never read `emissiveColor` for the skinned path — so `AmbientLightColor` is always
  rendered as `(0,0,0)` and `EmissiveColor` is silently dropped, unlike the EasyGL backend, which
  correctly forwards both through its `uEmissiveColor` uniform; (2) `skinned3d_vertexlit.vert.glsl`
  computes the lit normal as `mat3(skinMat) * aNormal` with no `World`-space (inverse-transpose)
  transform, an identical bug to the one the EasyGL audit found in
  `easygl_skinnedeffect_preferperpixellighting_test.cpp.audit.md`'s F1 — invisible here (and in
  every file in this batch) only because `World` is always `Matrix::getIdentityProperty()`.
- Independently re-declared `SkinnedGpuVertex` struct (stride-52) appears in at least 5 files in
  this shard, each with its own `static_assert`; consistent today (all verified byte-identical) but
  the same maintainability risk already flagged for the EasyGL shard's equivalent files applies here.

## Missing or Weak Tests

- See F1.
- No test in this file (reasonably, given its narrow scope) exercises anything beyond bone 0 at
  weight 1 — covered by sibling files instead.

## Positive Findings

- Deliberately, verifiably tests an *omission* (no `SetBoneTransforms` call) as the actual feature
  under test, with a 3-point pixel-check design that distinguishes "rendered but moved" from
  "didn't render at all" from "rendered in place."
- Vertex attribute layout independently verified byte-exact against the real Vulkan pipeline
  creation code, not just assumed from the EasyGL sibling's already-verified layout.

## Final Assessment

A correct, well-designed default-value test, faithfully ported from its EasyGL sibling with the
Vulkan-specific `RasterizerState::CullNone` requirement correctly carried over. Its only individual
gap is the missing retry-until-rendered guard (F1, LOW, shared with three siblings); two more
significant production concerns exist in the shared skinned-shader code this file exercises, but are
structurally invisible to this file's own Identity-World/loose-predicate design and are reported at
their most concretely-verifiable anchor point (`vulkan_skinnedeffect_preferperpixellighting_test.cpp.audit.md`).
