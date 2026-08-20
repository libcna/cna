# Audit: examples/easygl_pbreffect_golden_test.cpp

## Metadata

- Source file: `examples/easygl_pbreffect_golden_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — golden-image + `ExpectPixel` test for `PbrEffect`'s glTF
  metallic-roughness BRDF shader
- File type: C++ example/integration-test executable (`PbrEffectGoldenTest : CNA::Examples::PixelTestGame`,
  `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::PbrEffect` (`PbrEffect.hpp`/`.cpp`),
  `EasyGLGraphicsBackend::EnsurePbrProgram()` (`EasyGLGraphicsBackend.cpp` lines 3597-3739),
  `EasyGLVertexBufferBackend::ApplyLayout()`'s stride-48 case (lines 2263-2276)
- XNA/FNA relevance: `PbrEffect` is explicitly `NOXNA` (confirmed at `PbrEffect.hpp` line 24, "not part of the
  XNA 4.0 API — real XNA predates the PBR content pipeline") — a CNA-specific glTF-era extension, correctly not
  judged against FNA. Checked instead against the glTF 2.0 spec's own reference BRDF (Appendix B.3.2-B.3.4), which
  the production shader's own comment (`EasyGLGraphicsBackend.cpp` line 3663-3664) explicitly claims to implement.
- Main related tests: this file itself (plans/plan_cnj.md CNB-58/60, Phase 13A); shares its golden-image pattern with
  `easygl_skinnedpbreffect_golden_test.cpp` (same shard, not in this batch) and `easygl_basiceffect_golden_test.cpp`.

## Purpose

Renders 4 side-by-side quads with `PbrEffect` — (A) flat-normal white non-metallic, (B) same as A with a tilted
normal map, (C) fully-metallic red, (D) fully-dielectric red — using the stride-48
`Position+Normal+Tangent+TextureCoordinate` GPU vertex layout, then verifies both hand-derived/qualitative
per-pixel expectations (`ExpectPixel`) and full 8×8-region golden-image comparisons (`CompareGoldenImage`) against
4 checked-in reference PNGs. Placement/namespace matches the shard convention.

## Executive Verdict

**Healthy.** The scene's light-direction/normal math was independently traced against the real
`PbrEffect::EnableDefaultLighting()` values and the real `EnsurePbrProgram()` GLSL fragment shader and found to
match the file's own qualitative claims exactly (only the key light has positive `NdotL` for the flat-normal quads;
the fill and back lights both clamp to zero). The stride-48 vertex layout is confirmed to match the real backend's
`ApplyLayout` stride-48 binding case exactly, attribute-for-attribute. No HIGH/CRITICAL findings; the one real
caveat (BRDF pixel values are captured/confirmed rather than purely analytically derived) is already disclosed by
the file's own header comment and is a documented, accepted limitation shared with this project's other lit-shader
golden tests, not a defect specific to this file.

## Checklist Results

### API / XNA / FNA parity
N/A (see Metadata) — `PbrEffect` is a `NOXNA` extension. `setWorldProperty`/`setViewProperty`/`setProjectionProperty`/
`EnableDefaultLighting()`/`setTextureProperty`/`setNormalMapProperty`/`setRoughnessFactorProperty`/
`setMetallicFactorProperty`/`Apply()` all real, correctly-used `PbrEffect` members (verified against
`PbrEffect.hpp`).

### Behavioral correctness
Independently re-derived the file's own "only the key light has positive NdotL" claim (lines 141-143) against the
real production values: `PbrEffect::EnableDefaultLighting()` (`PbrEffect.cpp` lines 147-160) sets
`DirectionalLight0.Direction = (-0.5265408, -0.5735765, -0.6275069)`,
`DirectionalLight1.Direction = (0.7198464, 0.3420201, 0.6040227)`,
`DirectionalLight2.Direction = (0.4545195, -0.7660444, 0.4545195)`. The fragment shader
(`EnsurePbrProgram`, line 3698-3700) computes `L = normalize(-uLightNDir)` for each light, so with the flat
geometric normal `N=(0,0,1)` used by quads A-D: `NdotL0 = dot(N, -Light0Dir) ≈ 0.6275 > 0`;
`NdotL1 = dot(N, -Light1Dir) ≈ -0.604`, clamped to 0 by the shader's `max(dot(N,L),0.0)` (line 3667);
`NdotL2 = dot(N, -Light2Dir) ≈ -0.4545`, also clamped to 0. This exactly matches the test's own claim that only the
key light contributes and confirms it against the real light-rig numbers and the real shader clamp, not just
asserted by the comment.

### Logic
`AppendQuad()` (lines 54-62) builds each quad as 2 triangles with a flat `(0,0,1)` normal and `(1,0,0,1)` tangent
for all 4 corners — checked against `PbrGpuVertex`'s `static_assert(sizeof==48)` (line 52) and against
`EasyGLVertexBufferBackend::ApplyLayout`'s stride-48 built-in case (`EasyGLGraphicsBackend.cpp` lines 2263-2276):
`vao.set_attribute_pointer(0,3,...,0)` / `(1,3,...,12)` / `(2,4,...,24)` / `(3,2,...,40)` matches
`PbrGpuVertex{px,py,pz(0); nx,ny,nz(12); tx,ty,tz,tw(24); u,v(40)}` field-for-field, and matches
`EnsurePbrProgram`'s vertex shader `layout(location=0..3)` (`aPos`/`aNormal`/`aTangent`/`aUV`) exactly — a fully
cross-file-verified fixture, not an assumed layout.
The 4 quads' X-ranges (`[-1,-0.5]`, `[-0.34,0.16]`, `[0.17,0.67]`, `[0.68,1.0]`) and sample points
(`W*1/8, 3/8, 5/8, 7/8`) were independently checked (with `World=View=Projection=Identity`, so clip space equals
vertex position 1:1): each sample's NDC X (`-0.75, -0.25, 0.25, 0.75` respectively) does fall strictly inside its
own quad's declared X range in every case — the samples are not centred on each quad but are comfortably inside
each footprint, avoiding edge-antialiasing artifacts.
The metallic-vs-dielectric texture-default reasoning (quads C/D leave `normalMap`/`metallicRoughnessMap`/
`emissiveMap`/`occlusionMap` unset) was independently traced through `EasyGLGraphicsBackend.cpp`'s draw-time
texture-unit binding (lines 4184-4227): a null `pbrMetallicRoughnessMap` falls back to `default_white_texture_`
(so `mr.g=mr.b=1`, meaning `roughness=uRoughnessFactor` and `metallic=uMetallicFactor` directly, matching the
test's implicit assumption), and a null `pbrOcclusionMap` also falls back to white (occlusion=1, not 0 — the
ambient term is NOT silently zeroed by leaving the occlusion map unset, which was worth verifying explicitly since
an unbound-texture-reads-zero assumption would have broken every quad's ambient term).

### Memory/resource lifetime
`whiteTex`/`redTex`/`tiltedNormalTex` (`Texture2D::CreateFromPixels`, 1×1 each) and `vb` (`VertexBuffer`, stack
object) are all local to `RunTest()` with straightforward, unambiguous lifetimes; no dangling-pointer or leak risk.

### Robustness
Uses `PixelTestGame`'s standard `ExpectPixel(..., tolerance)` and `CompareGoldenImage(..., tolerance)` — tolerances
of 20-30 are deliberately generous for a real lit BRDF result (as opposed to a flat unblended colour), consistent
with `PixelTestGame.hpp`'s own documented guidance ("pass a non-zero tolerance for any golden image that isn't a
single flat, unblended colour").

### Testing
This file is itself a test; both the `ExpectPixel` numeric cross-checks and the `CompareGoldenImage` region
comparisons are present for all 4 quads. Golden PNGs (`easygl_pbreffect_golden_test_{a,b,c,d}.png`) confirmed
present on disk during this audit (86-88 bytes each, consistent with tiny 8×8 reference images).

## Detailed Findings

No HIGH/CRITICAL findings. One MEDIUM/LOW-confidence observation, consistent with this project's own established
practice for other lit-shader golden tests (e.g. `easygl_skinnedeffect_golden_test.cpp`, referenced by this file's
own header):

### F1 — Quad C/D expected values are captured-and-confirmed, not purely analytically derived

- Severity: LOW
- Confidence: HIGH (this is exactly what the file's own header comment states, lines 7-12, 152-158)
- Category: testing / documentation
- Location/symbol: `ExpectPixel("quadC-metallic-red", ...)` / `ExpectPixel("quadD-dielectric-red", ...)`
  (lines 159-162)
- Evidence: the header comment explains the test camera/quad placement puts the eye "inside the geometry plane",
  making the view vector vary non-trivially across the quad, and explicitly attributes Quad C's low G/B channel
  values to "a known property of the raw Cook-Torrance formula at low NdotV" rather than deriving the exact
  `(45,1,1)`/`(63,2,2)` byte values from first principles.
- Why it matters: this is a real, disclosed limitation of the test's own rigor (a regression that subtly changes
  the specular/Fresnel math in a way that still lands within the ±20/±25 tolerance would not be caught), but it
  is the same accepted trade-off this project's other lit-shader golden tests already make, and the file is
  transparent about it rather than presenting captured values as independently proven.
- FNA/XNA comparison: N/A (`PbrEffect` is `NOXNA`).
- Related files: `easygl_skinnedpbreffect_golden_test.cpp` (same rationale, referenced directly by this file's own
  header).
- Suggested future action (not implemented by this audit): none required beyond what's already documented; a
  future hand-derivation of the full Cook-Torrance specular term for this exact scene would tighten this further
  if ever revisited.

## Cross-File Observations

- The `EnableDefaultLighting()` light-rig values this test depends on are shared verbatim by `BasicEffect`/
  `SkinnedEffect` per `PbrEffect.cpp`'s own comment (line 149) — any change to that shared rig would silently
  affect this file's expected values too; worth a one-line cross-reference note for whichever future task touches
  the shared default-lighting constants.
- Shares the Task 1080-adjacent stride-48 vertex layout convention with `easygl_particleeffect_shader_test.cpp`'s
  custom-`VertexDeclaration` path in this same batch, though this file uses the fixed-stride built-in
  `ApplyLayout` case rather than a declaration-driven one.

## Missing or Weak Tests

- See F1 — Quad C/D's exact expected byte values rely on captured-and-reviewed output rather than an independent
  analytic derivation, a limitation this file shares with, and openly attributes to, its sibling skinned-PBR test.

## Positive Findings

- Independently verified: the "only the key light contributes" lighting claim, the stride-48 vertex-layout wiring,
  and the metallic-roughness/occlusion texture-default fallback behavior (white, not black/zero) all check out
  exactly against the real production code, not just against the test's own comments.
- Combines a numeric per-pixel cross-check with a region-based golden-image comparison for every one of its 4
  quads — the same defense-in-depth pattern already positively noted for `easygl_basiceffect_golden_test.cpp` in
  this shard.

## Final Assessment

A well-constructed, cross-file-verified golden-image test for `PbrEffect`'s BRDF shader. Its scene setup, vertex
layout, and lighting-direction reasoning were all independently traced against the real production code during
this audit and found correct; its one honestly-disclosed limitation (captured rather than fully analytic expected
values for the metallic/dielectric comparison) is a shared, accepted trade-off across this project's lit-shader
golden tests, not a defect unique to this file.
