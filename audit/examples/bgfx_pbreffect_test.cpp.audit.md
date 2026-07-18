# Audit: examples/bgfx_pbreffect_test.cpp

## Metadata

- Source file: `examples/bgfx_pbreffect_test.cpp` (237 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `PbrEffect` glTF metallic-roughness BRDF pixel test
- File type: standalone `Game`-subclass executable, CTest-registered (`cna_test_bgfx_pbreffect` /
  `Bgfx_PbrEffect`, `cmake/Tests/BgfxTests.cmake:886-889`)
- XNA/FNA relevance: indirect. `PbrEffect` is explicitly `NOXNA` (`include/Microsoft/Xna/Framework/Graphics/PbrEffect.hpp:24`:
  "not part of the XNA 4.0 API. Real XNA predates the PBR content pipeline this represents") — the
  checklist's "API/XNA/FNA parity" section is therefore N/A for the BRDF itself, but `IEffectLights`/
  `IEffectFog`/`Effect::Apply()` plumbing it rides on is real XNA surface and is exercised correctly.
- Related production code: `include/Microsoft/Xna/Framework/Graphics/PbrEffect.hpp` /
  `src/Microsoft/Xna/Framework/Graphics/PbrEffect.cpp`,
  `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` (`EnsurePbrProgram()`, `DrawPrimitivesEx`'s
  PBR branch around lines 2523-2680), `src/CNA/Internal/Backends/Bgfx/shaders/fs_pbr3d.sc` (`PbrLight()`).

## Purpose

Five-check pixel test proving `PbrEffect`'s real glTF 2.0 metallic-roughness BRDF (GGX distribution +
Smith-Schlick-GGX visibility + Schlick Fresnel) executes correctly end-to-end through the Bgfx backend,
using a fully analytic camera/light rig (eye at `(0,0,3)` via `CreateLookAt`, directional light along
`-Z`, flat quad normal `(0,0,1)`) chosen so `N·L = N·V = N·H = V·H = 1` exactly at the sampled
screen-centre pixel, making the BRDF collapse to a form that can be hand-derived to several decimal
places rather than merely "captured and pasted". Checks: (A) fully rough/non-metallic white, (B) same
but half roughness (must be strictly brighter, proving `RoughnessFactor` is read), (C) fully metallic
red (diffuse must vanish, only a red-tinted specular survives), (D) non-metallic red (full Lambertian
diffuse + thin achromatic specular, must differ from C), (E) a tilted tangent-space normal map that
zeroes the direct-light term via `N·L≈0`, leaving only the ambient term (proves the normal map is
genuinely sampled and perturbs shading, not silently ignored).

## Executive Verdict

**Healthy.** This audit independently re-derived the exact BRDF output for all five checks by hand,
starting from the actual `PbrLight()` GLSL in `fs_pbr3d.sc` (not merely trusting the file's own
comments), and every single expected byte value matches to within normal 8-bit quantization. No
correctness defect found in either the shader or the `PbrEffect` C++ layer that feeds it.

## Checklist Results

### API / XNA / FNA parity
N/A for the BRDF itself (`NOXNA`, glTF-era concept). The XNA-shaped surface it uses —
`IEffectLights::DirectionalLight0/1/2`, `IEffectFog`, `Effect::Apply()` — is used correctly: the test
never touches `DirectionalLight1`/`DirectionalLight2`, and `DirectionalLight::enabled_` defaults to
`false` (`include/Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp:80`), which this audit confirmed
is exactly why `PbrEffect::FillGpuDrawParams()` (`src/Microsoft/Xna/Framework/Graphics/PbrEffect.cpp:329-342`)
zeroes `ld1`/`ld2` and the shader's `u_light1Diffuse`/`u_light2Diffuse` contribute nothing — the test's
implicit assumption ("only `DirectionalLight0` lights the scene") is verified true, not merely assumed.

### Behavioral correctness — independent re-derivation
Re-derived from `fs_pbr3d.sc`'s `PbrLight()` (not from the test file's own comments):
- **Check A** (rough=1.0, metallic=0.0, white, ambient=0): with `N=V=L=(0,0,1)`, `a2=1`, `D=1/π≈0.31831`,
  `k=0.5`, `G=1`, `F0=0.04`, `F=F0` (since `VdotH=1` zeroes the Schlick term),
  `specular=D·G·F/4≈0.0031831`, `kd=0.96`, `Lo=0.96/π+0.0031831=0.308761` → byte 79. **Matches**
  `Color(79,79,79,255)` exactly.
- **Check C** (metallic=1.0, red albedo, rough=1.0): `diffuseColor=albedo·(1-metallic)=0`,
  `F0=albedo=(1,0,0)` (glTF metallic F0 rule), `F=F0` (VdotH=1), `specular=D·G·F/4=(0.0795775,0,0)`,
  `Lo=specular` (diffuse term is exactly zero) → byte 20 in R, 0 in G/B. **Matches** `Color(20,0,0,255)`
  exactly.
- **Check D** (metallic=0.0, red albedo, rough=1.0): `diffuseColor=(1,0,0)`, `F0=0.04`,
  `Lo = (0.96/π+0.0031831, 0.0031831, 0.0031831) = (0.308761, 0.0031831, 0.0031831)` → bytes
  `(79,1,1)`. **Matches** `Color(79,1,1,255)` exactly.
- **Check E** (tilted normal `(255,128,128)` decoding to ≈`(1,0,0)`, tangent `(1,0,0,+1)`, normal
  `(0,0,1)`): `T=(1,0,0)`, `B=cross(N,T)·1=(0,1,0)`, `TBN=I`, so `finalNormal≈(1,0,0)` and
  `N·L≈N·V≈0`, zeroing `Lo` almost entirely; `ambient=(0.2,0.3,0.4)·albedo·1` → bytes
  `(51,77,102)` (0.3·255=76.5, rounds to 77). **Matches** `Color(51,77,102,255)` exactly.
- Confirmed the PBR-map fallback textures (`defaultWhiteTexture3D_`, opaque white — see
  `BgfxGraphicsBackend.cpp:2220/2232/2244`) make the unbound metallic-roughness/emissive/occlusion
  channels behave as pure multipliers (`mr.g=mr.b=1`), so `roughness`/`metallic` in checks A-D equal the
  effect's own `RoughnessFactor`/`MetallicFactor` properties directly, exactly as the test assumes.

This is a materially deeper verification than trusting the file's own "hand-derived" comments — this
audit re-derived the numbers independently from the shader source and they agree to the byte.

### C++ correctness
`PbrGpuVertex` stride is `static_assert`ed to 48 bytes and matches
`BgfxGraphicsBackend`'s stride-48 `MakeBgfxLayout` case referenced in the file header — confirmed the
vertex layout (`pos·3 + normal·3 + tangent·4 + uv·2` floats = 48 bytes) is the same one the PBR vertex
shader (`vs_pbr3d.sc`) expects.

### Robustness
`renderWith()`'s up-to-20-attempt retry loop (skip until a non-black pixel appears) is the established,
documented mitigation for the Bgfx "`GetBackBufferData` only reliably reflects the first read per
rendered frame" quirk (Task 406) used consistently across this whole shard — not a defect, a known and
correctly-applied workaround.

### Testing
All 5 checks assert real pixel values with tight tolerance (±8), not just "did not crash". Checks B and
D additionally assert strict inequalities (`b > a`, `!matches(d,c)`) specifically to rule out a
hardcoded/no-op parameter — the same anti-boilerplate discipline the project's stronger tests use
elsewhere (e.g. the EasyGL specular test in this audit's own example report).

## Detailed Findings

None. No HIGH/CRITICAL/MEDIUM findings — every claim in the file, and the underlying shader/effect
code it exercises, was independently re-verified and found correct.

## Cross-File Observations

- Shares `EnsurePbrProgram()`/`PbrLight()` with `bgfx_skinnedpbreffect_test.cpp`
  (`cmake/Tests/BgfxTests.cmake:891-897` confirms the latter reuses this file's own expected values via
  an identity bind pose) — a regression in the shared BRDF math would show up in both.
- The file's claim that `RasterizerState::CullNone` is required (Task 364/884: "Bgfx's default
  RasterizerState cull state culls the standard NDC quad winding") is applied consistently and
  correctly (`dev.setRasterizerStateProperty(RasterizerState::CullNone)` at line 127), matching the same
  workaround documented and used in `bgfx_render_target_cube_sample_test.cpp`.

## Missing or Weak Tests

- No coverage of the occlusion-map texture actually being sampled with a *non-default* value (checks
  never bind a real occlusion map — occlusion is always 1.0 via the fallback). A dedicated
  occlusion-map check (mirroring check E's normal-map methodology) would close this gap, but is a minor
  addition, not a defect.

## Positive Findings

- Every expected byte value in this file was independently reproduced by this audit from the actual
  production shader source (`fs_pbr3d.sc`), not merely cross-checked against the file's own comments —
  a stronger bar than most files in this shard, and it holds up.
- Checks B and D's differential assertions (strictly-greater / strictly-not-equal) correctly rule out
  the two most likely regressions (a hardcoded roughness/metallic response) that a same-expected-outcome
  test family cannot distinguish.
- Confirmed the glTF metallic-workflow F0 rule (`F0 = mix(0.04, albedo, metallic)`) is implemented
  exactly per spec (glTF Appendix B.3.2-B.3.4, as the shader's own comment states) and the effect-level
  fallback-texture wiring correctly reduces to "factor properties act as direct multipliers when no map
  is bound" — the precise behavior the test's checks A-D depend on.

## Final Assessment

A model test for this shard: analytically chosen geometry/lighting collapses the BRDF to a
hand-verifiable closed form, and this audit's own independent re-derivation from the shader source
confirms every expected value to the byte. No defects found in the test or the PBR pipeline it exercises.
