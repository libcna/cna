# Audit: examples/sdlgpu_pbreffect_test.cpp

## Metadata

- Source file: `examples/sdlgpu_pbreffect_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlgpu` shard — `PbrEffect` (glTF metallic-roughness BRDF) proof for
  the SDL_GPU backend
- File type: standalone `Game`-subclass executable, CTest-registered (`SdlGpu_PbrEffect`,
  `cmake/Tests/SdlGpuTests.cmake:84-86`, `TIMEOUT 60 LABELS "SdlGpu"`)
- XNA/FNA relevance: NOXNA — `PbrEffect` is a CNA-only extension (class-level `NOXNA` marker,
  `include/Microsoft/Xna/Framework/Graphics/PbrEffect.hpp` line 24: "not part of the XNA 4.0 API.
  Real XNA predates the PBR content pipeline"); it stays in `Microsoft::Xna::Framework::Graphics`
  (an intentional project convention for effect-family extensions, matching `SkinnedEffect`'s own
  NOXNA members). No FNA equivalent exists.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/PbrEffect.cpp`
  (`FillGpuDrawParams`, lines 295-362), `src/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.cpp`
  (`CreatePbrResources`/`GetOrCreatePipelinePbr3D`/`IssuePbrDraw`, lines 2701-2821, 3262-3318),
  `src/CNA/Internal/Backends/SdlGpu/shaders/pbr3d.vert.glsl`, `pbr3d.frag.glsl`.

## Purpose

Three-quad, single-frame pixel test proving the SDL_GPU PBR pipeline end-to-end: the stride-48
`VertexPositionNormalTangentTexture` layout, TBN (tangent/bitangent/normal) construction, and the
metallic-roughness BRDF itself. Unlike `easygl_pbreffect_golden_test.cpp` (whose `View=Identity`
scene places the eye in a degenerate grazing-angle position the header comment there calls
impractical to hand-derive), this file deliberately places the quad at `z=-0.5` with
`DirectionalLight0` pointing straight down `-Z`, making `N=V=L=H=(0,0,1)` exactly at the sampled
centre pixel — a fully analytic case. Quad A (white/rough/non-metallic) proves the base BRDF; Quad
B (red/fully-metallic) proves `MetallicFactor` genuinely zeroes the diffuse term and tints the
specular lobe; Quad C (tilted normal map) proves the normal map is actually sampled and perturbs
lighting, not silently ignored.

## Executive Verdict

**Healthy.** All three expected pixel values were independently re-derived by this audit directly
from the shader's own BRDF formula (`pbr3d.frag.glsl`'s `PbrLight()`) and the production
`PbrEffect::FillGpuDrawParams()`'s actual default values — not merely checked for internal
self-consistency — and match the file's literals to within the stated tolerance in every case,
including one case (Quad B) matched to the exact integer.

## Checklist Results

### API / XNA / FNA parity

N/A for XNA compliance (whole class is `NOXNA`), but this audit did verify the effect's *property
defaults* the test silently relies on are correctly the class's own documented defaults, not
happenstance: `PbrEffect.hpp` lines 249-254 default-member-initialize `diffuseColor_ =
Vector3{1,1,1}`, `alpha_ = 1.0f`, `ambientLightColor_ = Vector3::Zero`, `emissiveFactor_ =
Vector3::Zero`, `metallicFactor_ = 1.0f`, `roughnessFactor_ = 1.0f`. The test's own header comment
("AmbientLightColor/EmissiveFactor default to Vector3::Zero ... left untouched so this test's own
light math has no hidden extra terms," lines 22-23) is **confirmed accurate** by these default
member initializers — `PbrEffect::EnableDefaultLighting()` (the method that would otherwise set a
non-zero ambient rig, `PbrEffect.cpp` lines 147-161) is never called by this test, so the
constructor's zero defaults genuinely apply.

### Behavioral correctness

Independently re-derived all three checks by hand against `pbr3d.frag.glsl`'s `PbrLight()` (lines
59-75) and `main()` (lines 88-117), using `View=Identity` ⇒ `eyePos=(0,0,0)`
(`PbrEffect::FillGpuDrawParams()` lines 347-351: `eyePos = Invert(view_).getTranslationProperty()`)
and quad depth `z=-0.5` ⇒ `V = normalize(eyePos - fragWorldPos) = (0,0,1)`, matching the header's
own claimed analytic case:

- **Quad A** (white, `Roughness=1`, `Metallic=0`): `NdotL=NdotV=NdotH=VdotH=1`; `a2=1`; `D=1/π≈
  0.3183`; `k=(1+1)²/8=0.5` ⇒ `G=1`; `F0=mix(0.04,albedo,0)=0.04`; `F=F0+(1-F0)·0⁵=0.04`;
  `specular=(D·G·F)/(4·1·1)≈0.003183`; BRDF-diffuse `=albedo·(1-metallic)=1`; `kd=1-F=0.96`;
  `Lo=(0.96·1/π+0.003183)·1·1≈0.30878`. Ambient/emissive are both 0 (per the defaults above), so
  `outColor.rgb=0.30878 → 0.30878×255≈79`. **Exactly matches** the file's `Color(79,79,79,255)`
  (line 180).
- **Quad B** (red, `Metallic=1`, roughness unchanged at 1.0): BRDF-diffuse `=albedo·(1-1)=0` (the
  metallic gate correctly zeroes the diffuse lobe entirely, independent of `kd`); `F0=mix(0.04,
  albedo,1)=albedo=(1,0,0)`; `specular=(D·G·F)/(4·1·1)=(1/π·1·(1,0,0))/4≈(0.0796,0,0)`;
  `Lo=specular·(1,1,1)·1=(0.0796,0,0)` → `0.0796×255≈20.3→20`. **Exactly matches**
  `Color(20,0,0,255)` (line 186), independently proving `MetallicFactor` both zeroes the diffuse
  term and colors the specular lobe by albedo, as the header comment claims.
- **Quad C** (tilted normal map, `Metallic=0`): the perturbed normal's `NdotL` collapses to
  `max(dot(N,L),0)=0`; `Lo`'s outer `*NdotL` factor forces the entire lit term to exactly 0
  regardless of the `specular` term's own `1/max(4·NdotV·NdotL,1e-4)` denominator (the `1e-4`
  floor only prevents a divide-by-zero — it does not leak a nonzero value past the outer `*NdotL`
  multiply, `pbr3d.frag.glsl` line 74). Ambient/emissive both 0 ⇒ `outColor=(0,0,0,1)`. **Exactly
  matches** `Color(0,0,0,255)` (line 194).

All three derivations used the actual default `alpha_=1.0f`/`diffuseColor_={1,1,1}` (Quad A/C) and
the test's own explicit `setTextureProperty(redTex_)` (Quad B, `albedo=(1,0,0)`), not assumed
values.

### Logic

`RenderAndSampleCenter` (lines 111-127) reuses the same `PbrEffect& fx` object across all three
quads, only mutating the specific properties each quad changes (`setTextureProperty`,
`setMetallicFactorProperty`, `setNormalMapProperty`) — correctly relies on `roughnessFactor_`
staying at `1.0f` for Quad B/C since neither call resets it, matching the header comment's "same
roughness" claim (lines 17, 20) exactly.

### Memory/resource lifetime

`RenderAndSampleCenter` binds `rt_` (line 113), draws, unbinds (`SetRenderTarget(nullptr)`, line
121), then calls `rt_->GetData()` (line 125) — `rt_` is a member (line 94), outliving each
individual `Draw()`-call scope, so no risk of the local-render-target use-after-free pattern this
shard's `sdlgpu_rendertarget_lifetime_test.cpp`/`sdlgpu_mrt_test.cpp` both discuss.

### C++ correctness

`static bool done` guard in `Draw()` (lines 154-156) is the single-shot idiom used throughout this
shard — correct here since all three quads render within one `Draw()` call before `Exit()`.
`WithinTolerance` (lines 82-88) correctly checks all 4 channels including alpha, appropriate since
`PbrEffect`'s alpha is independently tracked from the lit RGB response
(`PbrEffect::FillGpuDrawParams()`'s own comment, `PbrEffect.cpp` lines 309-312, confirms alpha is
deliberately not premultiplied into the BRDF).

### Testing

A materially stronger test than a typical single-scene pixel check: three quads isolate three
independent BRDF axes (base response, metallic gate, normal-map perturbation) with values verified
analytically rather than empirically pasted from a single run — directly comparable in rigor to
this shard's template example (`easygl_basiceffect_specular_test.cpp`)'s own hand-derivation
standard, and, unlike that file's Check (b), every one of this file's three checks was independently
confirmed correct, not merely internally consistent with a stale reference value.

## Cross-File Observations

- Confirmed via `PbrEffect.cpp`/`.hpp` that `AmbientLightColor`/`EmissiveFactor` truly default to
  `Vector3::Zero` (see API/parity section) — this specific claim in the test's own header comment
  is the kind of self-reported fact this audit's instructions call for independent
  re-verification of, and it checks out.
- Fog and skinned-normal-transform cross-cutting bugs
  (`AUDIT_CROSS_CUTTING_FINDINGS.md`) are **not applicable**: this file never enables
  `FogEnabled` (`pbr3d.vert.glsl`/`.frag.glsl` compute no fog term at all — no `fogEnabled`/
  `fogColor` uniform is even declared in either shader), and exercises only the non-skinned PBR
  vertex shader (`pbrVertexShader_`, not `pbrSkinnedVertexShader_` — the file never calls a
  `SkinnedPbrEffect` API). `pbr3d.vert.glsl`'s normal-matrix computation (lines 51-52:
  `mat3 normalMatrix = transpose(inverse(mat3(lp.world)))`) is, incidentally, the *correct*
  World-inverse-transpose form the cross-cutting skinned-effect bug is specifically about
  omitting — confirming this non-skinned PBR path does not share that defect (as expected, since
  it has no per-vertex skin matrix to compose with in the first place).
- The Bgfx/Vulkan `EnvironmentMapEffect` EmissiveColor×DiffuseColor bug
  (`AUDIT_CROSS_CUTTING_FINDINGS.md`) has no analogous instance here: `pbr3d.frag.glsl`'s ambient
  term (`ambient = pc.ambientColor * albedo * occlusion`, line 113) and emissive term (`emissive =
  emissiveColor * texture(uEmissiveMap,...)`, line 114) are additively combined, with emissive
  never multiplied by diffuse/albedo — the correct, unscaled-emissive convention.
- `PbrEffect::FillGpuDrawParams()` (`PbrEffect.cpp` lines 326-327) forwards `metallicFactor_`/
  `roughnessFactor_` into `GpuDrawParams`, and `SdlGpuGraphicsBackend`'s `FillPbrParams()` (lines
  346-352) packs them into the fragment-stage `PbrParams` UBO consumed at `pbr3d.frag.glsl` lines
  49-54/100-102 — traced the full parameter path end-to-end with no drop or field-order mismatch.

## Missing or Weak Tests

None found specific to this file's own scope — the three quads already isolate diffuse response,
metallic gate, and normal-map perturbation independently. A natural (not currently present)
follow-up would be a fourth quad varying `Roughness` alone (holding metallic/normal fixed) to
directly exercise the GGX `D`/`G` terms' roughness dependence, currently only indirectly implied by
Quad A/B sharing `Roughness=1`.

## Positive Findings

- All three expected pixel values were derived independently by this audit from first principles
  (the shader's own BRDF formula plus the effect's real default property values) and matched the
  file's literals, in one case (Quad B) to the exact integer — the strongest form of verification
  this checklist recognizes.
- Quad B's design (fully metallic, non-white albedo) is a well-chosen differential: it isolates
  `MetallicFactor`'s effect on *both* the diffuse-zeroing and the specular-tinting halves of the
  BRDF in one assertion, rather than needing two separate quads.
- Quad C's design (tilted normal collapsing `NdotL` to exactly 0) is a clean, unambiguous
  discriminator for "is the normal map actually sampled," immune to interpolation/precision noise
  that a near-grazing (rather than exactly-zero) angle would introduce.

## Final Assessment

A rigorous, independently-verifiable PBR test: every expected constant traces cleanly to the
shader's real BRDF formula and the effect's actual default property values, with no stale or
unverified numeric claims found. No defects identified in this file or the production code paths
it exercises.
