# Audit: examples/webgpu_pbr3d_test.cpp

## Metadata

- Source file: `examples/webgpu_pbr3d_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-webgpu` shard — unskinned `PbrEffect` (glTF 2.0 metallic-roughness BRDF) test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_webgpu_test(cna_test_webgpu_pbr3d examples/webgpu_pbr3d_test.cpp)` /
  `cna_register_backend_test(NAME WebGPU_Pbr3D …)`, `cmake/Tests/WebGpuTests.cmake:101-102`).
- XNA/FNA relevance: `PbrEffect` is explicitly **NOXNA** — real XNA 4.0 predates the glTF PBR content
  pipeline (`include/Microsoft/Xna/Framework/Graphics/PbrEffect.hpp` line 24: "`@note NOXNA — not part of the
  XNA 4.0 API. Real XNA predates the PBR content pipeline...`"). FNA reference: N/A directly; the BRDF itself
  is the glTF 2.0 spec's own reference implementation (Appendix B.3.2-B.3.4), not an XNA/FNA behavior.
- Related production code: `src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp`
  (`CreatePbrResources()` lines 6919-7121, `pbrLight()`/`fs_main` lines 7004-7061, `QueuePbrDraw()` lines
  7243+, `FillLitLightUniforms()`/`ComputeNormalMatrix3x3()` lines 423-467 — the CPU-side
  `WorldInverseTranspose`-equivalent normal matrix this shader actually uses).

## Purpose

Five-check pixel test proving the WebGPU backend's unskinned `PbrEffect` shader (`pbr3d.wgsl`,
`GetOrCreatePipelinePbr3D()`/`DrawPrimitivesEx()` dispatch for stride-48
`VertexPositionNormalTangentTexture` draws) implements a genuine GGX+Smith-Schlick-GGX+Schlick-Fresnel BRDF,
not a stub: (A) ambient-only renders white (also proves the stride-48 dispatch reaches a real pipeline, not
a silent stride-16 fallback), (B) a facing light produces real non-black energy, (C) the same light behind
the surface produces black (NdotL clamp), (D) a normal map that tilts the surface 90° away from the light
zeroes NdotL (proves the normal map/TBN basis is genuinely sampled, not ignored), (E) `MetallicFactor=1`
renders strictly darker in the red channel than `MetallicFactor=0` for a red base color (proves
`MetallicFactor` actually reaches the BRDF, not just cosmetically stored).

## Executive Verdict

**Healthy** — this audit independently re-derived the exact BRDF arithmetic the header comment claims (D, k,
G, F, specular, and both Check B's and Check E's final `Lo` values) directly from `pbr3d.wgsl`'s own
`pbrLight()`/`fs_main` source and got the same numbers the comment states, to 3 significant figures. No
correctness defect found in this file or the unskinned PBR shader path it exercises; unlike the plain
`SkinnedEffect`/`SkinnedPbrEffect` siblings in this same batch, this shader correctly composes the true
`WorldInverseTranspose`-equivalent normal matrix (see F1 discussion in the sibling skinned reports — this
file is unaffected).

## Checklist Results

### API / XNA / FNA parity

N/A in the XNA-compliance sense (NOXNA extension) — verified instead against its own internal contract:
`setRoughnessFactorProperty`/`setMetallicFactorProperty`/`setNormalMapProperty`/`setAmbientLightColorProperty`
map onto real `PbrEffect` members declared `NOXNA` in the header, consistent with `CLAUDE.md`'s requirement
that non-XNA `Microsoft::Xna`-namespace members be tagged.

### Behavioral correctness

Independently re-derived the BRDF for Check A/B (`roughness=1, metallic=0`, white albedo, `N=V=L=H=(0,0,-1)`
giving `NdotL=NdotV=NdotH=VdotH=1`):
- `a2 = roughness^4 = 1`; `dTerm = 1`; `D = a2/(π·dTerm² ) = 1/π ≈ 0.3183` — matches header.
- `k = (roughness+1)²/8 = 0.5`; `G = (1/(1·0.5+0.5))·(1/(1·0.5+0.5)) = 1` — matches header.
- `F = F0 + (1-F0)·(1-VdotH)^5 = F0 = 0.04` (mix(0.04, albedo, metallic=0) = 0.04) — matches header.
- `specular = D·G·F/(4·NdotV·NdotL) = 0.3183·1·0.04/4 ≈ 0.003183`.
- `diffuseColor = albedo·(1-metallic) = 1`; `kd = 1-F = 0.96`; `Lo = (kd·diffuseColor/π + specular)·
  lightColor·NdotL = 0.96/π + 0.003183 ≈ 0.3088` — matches the header's stated `≈0.309` almost exactly. This
  independently confirms `pbr3d.wgsl`'s `fs_main`/`pbrLight()` is the real formula claimed, not a
  hand-waved approximation.
- Check E's red/metallic derivation: for `MetallicFactor=1`, `F0=albedo=(1,0,0)`; the diffuse term is
  `albedo·(1-metallic)=0` in every channel (exactly zero, not merely small); `Lo_red = D·G·F0_R/4 ≈ 0.0796`
  (G/B channels exactly 0, since `F0_G=F0_B=0` there too). For `MetallicFactor=0` (dielectric) with the same
  red albedo, `F0=(0.04,0.04,0.04)` uniformly (via `mix(0.04, albedo, 0)`), giving the same `≈0.309` red
  value as Check B's white case. `0.0796 < 0.309` confirms Check E's assertion
  (`metallicRed < dielectricRed`) independently, not merely internally self-consistent.
- Check D's normal-map derivation: with `Normal=(0,0,-1)`, `Tangent=(1,0,0,1)`, the TBN basis is
  `t0=(1,0,0)`, `b0=cross(n0,t0)·sign=(0,-1,0)`, `n0=(0,0,-1)`. The encoded normal-map texel `(255,128,128)`
  decodes to `(1, ≈0.004, ≈0.004) ≈ (1,0,0)`, and `finalNormal = normalize(tbn·sampledNormal) ≈ t0 = (1,0,0)`
  — exactly perpendicular to the light direction `(0,0,-1)`, giving `NdotL=0` and a black result, matching
  the header's derivation and Check D's assertion.

### Logic

`Check A` doubles as a dispatch-correctness proof (48-byte stride reaching a real pipeline rather than
silently matching `colored3d.wgsl`'s stride-16 layout) — a good design choice; a wrong-layout fallback would
almost certainly not render a clean white square, catching a real regression class beyond pure BRDF
correctness.

### C++ correctness

`PbrGpuVertex` has a `static_assert(sizeof(...) == 48, ...)` — a good, cheap layout-drift guard consistent
with this batch's other stride-sensitive vertex structs.

### Memory/resource lifetime

`whiteTex_`/`redTex_`/`tiltedNormalTex_` are plain `Texture2D` value members constructed in `LoadContent()`
via `CreateFromPixels()`; ordinary value lifetime, no leak/UAF risk.

### Performance

N/A — one-shot test, not a hot path.

### Architecture

Correctly isolates the unskinned PBR path from the skinned variant (`webgpu_skinnedpbr3d_test.cpp`, this same
batch) — the header explicitly scopes itself as "UNSKINNED only," consistent with the two shaders being
genuinely separate WGSL modules in the backend (`pbr3d.wgsl` vs. `skinned_pbr3d.wgsl`).

### Maintainability

The final pass/fail gate hardcodes `passCount == 5` (line 292) against exactly 5 `check()` calls (A, B, C, D,
E) — currently correct, same LOW stylistic observation as `webgpu_msaa_test.cpp` (a self-tallying counter
would be more drift-resistant, as used by `webgpu_skinned3d_test.cpp`/`webgpu_skinnedpbr3d_test.cpp` in this
same batch), not a live defect.

### Portability

N/A.

### Robustness

N/A — test file, not input-validating production code.

### Testing

Strong: every check's assertion was independently re-derivable from the shader source by hand, and this
audit did so for the two most numerically substantive claims (A/B's `≈0.309` and E's metallic-vs-dielectric
comparison), both confirmed correct to the stated precision.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM/LOW findings against this file or the unskinned PBR shader path it exercises beyond
the one stylistic maintainability observation above.

## Cross-File Observations

- This file's `CreatePbrResources()`/`FillLitLightUniforms()` correctly compute and forward the true
  `WorldInverseTranspose`-equivalent normal matrix (`ComputeNormalMatrix3x3()`, lines 423-441, explicitly
  cross-checked in its own comment against `BgfxGraphicsBackend::ComputeNormalMatrix3x3()` and FNA's
  `Lighting.fxh` convention) — this is the same code path `lit_textured3d.wgsl`/`env_map3d.wgsl` use, and it
  is genuinely correct, unlike the skinned-normal-transform defect confirmed in `webgpu_skinned3d_test.cpp`'s
  and (in a milder, self-documented form) `webgpu_skinnedpbr3d_test.cpp`'s own reports in this same batch —
  this file is the useful "known-good" contrast case for that finding.
- No `EmissiveColor*DiffuseColor` re-multiplication bug (the Bgfx/Vulkan `EnvironmentMapEffect` cross-cutting
  finding): `fs_main`'s `emissive = lp.emissiveColor.xyz * textureSample(emissiveTex,...)` is added unscaled
  to `ambient + lo`, not multiplied into albedo — correct per FNA's `Lighting.fxh` convention.
- No fog-formula bug applicable: this shader (like every other WebGPU 3D shader so far, per this backend's
  own comments) has fog deliberately deferred entirely, so the EasyGL/Bgfx/Vulkan fog-formula cross-cutting
  defect has no analog here yet.

## Missing or Weak Tests

None identified for the unskinned PBR feature surface this file targets. (The skinned variant's own gaps are
covered in `webgpu_skinnedpbr3d_test.cpp`'s report.)

## Positive Findings

- A rare case in this audit where a test file's own hand-derived numeric justification was independently
  reproduced by re-reading the actual shader source line-by-line and matched to 3 significant figures for
  two separate scenarios (ambient/light-only and metallic-vs-dielectric).
- Check D is a well-constructed, minimal proof that the normal map is genuinely sampled and composed through
  a real TBN basis, not silently ignored — a common failure mode this specific check would catch.

## Final Assessment

A correct, thoroughly-verified test of a correct, thoroughly-verified shader. No defects found in either the
test or the unskinned PBR production path; this is the "control" file in this batch against which the
skinned-normal-transform findings in the other reports are contrasted.
