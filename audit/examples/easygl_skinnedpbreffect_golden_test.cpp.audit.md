# Audit: examples/easygl_skinnedpbreffect_golden_test.cpp

## Metadata

- Source file: `examples/easygl_skinnedpbreffect_golden_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — `SkinnedPbrEffect` (PBR + skinning combo) golden-image test
- File type: `PixelTestGame`-derived executable, CTest-registered
  (`cna_easygl_test(cna_test_easygl_skinnedpbreffect_golden …)` /
  `cna_register_backend_test(NAME EasyGL_SkinnedPbrEffect_Golden …)`,
  `cmake/Tests/EasyGLTests.cmake:146-149`).
- XNA/FNA relevance: **NOXNA** — `SkinnedPbrEffect` is not part of the XNA 4.0 API (real XNA predates
  both PBR and this combination), correctly documented as such in `SkinnedPbrEffect.hpp`'s own class
  doc comment (lines 24-35, explicitly citing the "distinct classes per major shader variant" precedent
  from `BasicEffect`/`SkinnedEffect`). No FNA reference exists for this effect's own math; the glTF 2.0
  metallic-roughness BRDF (Appendix B) is the correct reference instead, already used consistently for
  the unskinned `PbrEffect` sibling.
- Golden fixtures: **reuses** `easygl_pbreffect_golden_test_{a,b,c,d}.png` (confirmed present on disk)
  rather than having its own dedicated golden images — an intentional design choice (see Purpose).
- Production code exercised: stride-68 `SkinnedPbrGpuVertex` layout vs. `ApplyLayout`'s stride==68 case
  (`EasyGLGraphicsBackend.cpp` lines 2325-2344), `EnsurePbrSkinnedProgram()` (lines 3745-3890),
  compared against `EnsurePbrProgram()` (lines 3597-3739, the unskinned sibling).

## Purpose

Proves the stride-68 `VertexPositionNormalTangentTextureSkinned` layout, the bone-palette skin
transform applied to Position/Normal/Tangent, and the PBR BRDF fragment stage all work end-to-end via
a real GPU draw, by reusing `easygl_pbreffect_golden_test.cpp`'s exact 4-quad scene (same camera,
lights, per-quad material setup: flat-normal white/rough, tilted-normal-mapped, metallic red, dielectric
red) through `SkinnedPbrEffect` with a single Identity bone. Since an Identity bind pose is a
mathematical no-op for the skin transform, the rendered pixels *must* be identical to `PbrEffect`'s own
already-verified golden values — an efficient design that avoids re-deriving a second independent set of
PBR expected values, and one that specifically isolates "does the skin matrix multiply wire correctly"
as the one new variable, since everything else about the scene is asserted to be unchanged.

## Executive Verdict

**Needs attention.** The test's own reused-oracle logic is sound *given its stated premise* (Identity
bind pose ⇒ skin transform is a no-op ⇒ output must match `PbrEffect`'s), but this audit found a real
divergence between `EnsurePbrSkinnedProgram()`'s and `EnsurePbrProgram()`'s normal-transform math that
the test's Identity-`World`, Identity-bone scene cannot detect: the skinned variant transforms the
normal/tangent using the raw `World` matrix rather than `PbrEffect`'s own correct inverse-transpose
normal matrix. For this specific scene the two are numerically identical (Identity has a trivial
inverse-transpose), so the test's "must exactly match PbrEffect" premise is not actually violated *here*
— but the test's own reused-oracle design guarantees it can never be extended to a non-Identity `World`
without either (a) exposing this real divergence as a spurious mismatch against `PbrEffect`'s own
(correctly-computed) values, or (b) silently validating the wrong formula if `PbrEffect`'s own values
were naively reused for a rotated/scaled scene too.

## Checklist Results

### API / XNA / FNA parity
`SkinnedPbrEffect`'s public surface used here (`setWorldProperty`/`setViewProperty`/
`setProjectionProperty`, `SetBoneTransforms`, `setWeightsPerVertexProperty`, `EnableDefaultLighting`,
`setTextureProperty`/`setNormalMapProperty`/`setRoughnessFactorProperty`/`setMetallicFactorProperty`)
correctly mirrors `PbrEffect`'s own property surface with skinning added — consistent, NOXNA-correct
API shape (`SkinnedPbrEffect.hpp` lines 37-217, all NOXNA-tagged where appropriate, XNA-shaped
`IEffectMatrices`/`IEffectFog`/`IEffectLights` interface members left untagged since those genuinely are
XNA interfaces this NOXNA class implements).

### Behavioral correctness
`SkinnedPbrGpuVertex` (lines 39-48, `static_assert(sizeof==68)`) matches `ApplyLayout`'s stride-68 case
exactly: the stride-48 `Position+Normal+Tangent+TextureCoordinate` layout (offsets 0/12/24/40) with the
stride-52/56-family's `BlendWeight`/`BlendIndices` suffix appended at offsets 48/64 — verified
field-by-field against `EasyGLGraphicsBackend.cpp` lines 2332-2343, matching the file's own comment
about "append rather than insert" being consistent with the stride-52/56 precedent.

Compared `EnsurePbrSkinnedProgram()`'s fragment shader (lines 3786-3858) against `EnsurePbrProgram()`'s
(lines 3634-3708) character-by-character: **byte-for-byte identical** BRDF math (`PbrLight()`, Fresnel,
GGX distribution, geometry term, ambient/emissive/occlusion composition, alpha test, fog) — confirming
the "additive, not a new algorithm" framing in the file's own production-code comment (lines 3741-3744)
is accurate for the fragment stage. This is the correct part of the design: reusing `PbrEffect`'s own
already-verified BRDF fragment code for the skinned variant, rather than a second hand-copied
implementation that could drift, is genuinely good engineering.

The vertex stage, however, diverges in a way this test cannot see. See F1.

### Testing
Reuses `PbrEffect`'s own golden PNGs and `ExpectPixel` values verbatim (lines 137-153) rather than
generating dedicated `SkinnedPbrEffect` fixtures — an efficient, defensible choice *specifically because*
the scene is designed to make skinning a mathematical no-op, but it means this file provides **zero**
independent test coverage of any scenario where `SkinnedPbrEffect`'s skinning actually does something
(a non-Identity bone, or a non-Identity `World`) — every such scenario is, by construction, out of this
file's own reach, and (as of this audit pass) not covered by any other file in this batch either.

## Detailed Findings

### F1 — `EnsurePbrSkinnedProgram()`'s vertex shader transforms the normal/tangent by the raw `World` matrix instead of `PbrEffect`'s own correct inverse-transpose normal matrix; this test's Identity-only scene cannot detect the divergence

- Severity: HIGH
- Confidence: HIGH
- Category: correctness (production shader), test-coverage gap
- Location/symbol: `EasyGLGraphicsBackend::EnsurePbrSkinnedProgram()` vertex stage
  (`EasyGLGraphicsBackend.cpp` lines 3777-3779:
  `mat3 skinNormalMat=mat3(skinMat); vNormal=normalize(mat3(uWorld)*(skinNormalMat*aNormal));
  vTangent=mat3(uWorld)*(skinNormalMat*aTangent.xyz);`) vs.
  `EnsurePbrProgram()`'s vertex stage (lines 3609/3622: `uniform mat3 uNormalMatrix; ... vNormal=
  uNormalMatrix*aNormal;`), where `uNormalMatrix` is `BindDrawParams()`'s own cofactor/determinant
  inverse-transpose of `World`'s upper-left 3×3 (lines 3993-4010, itself citing a prior "Task 398 fix"
  for exactly this class of bug on the *unskinned* path). `EnsurePbrSkinnedProgram()` never declares a
  `uNormalMatrix` uniform at all, and `Prog3D::loc_normalmat` (`EasyGLGraphicsBackend.hpp` line 408,
  default `-1`) is never set for `prog_pbr_skinned_` (confirmed: no `p.loc_normalmat = ...` line
  anywhere in `EnsurePbrSkinnedProgram()`, lines 3860-3889) — so even if this shader *did* need it,
  `BindDrawParams()`'s `if (p.loc_normalmat >= 0)` guard (line 3997) would simply skip uploading it.
- Evidence: for a uniform-scale or pure-rotation `World`, `mat3(World)` and its inverse-transpose
  coincide, so this divergence would not visibly manifest — but for any **non-uniform-scale** `World`
  (a common real-world case: a stretched/squashed skinned character), `EnsurePbrProgram()` correctly
  compensates (per its own Task-398-referenced fix) while `EnsurePbrSkinnedProgram()` does not, meaning
  `SkinnedPbrEffect` would render visibly skewed/incorrect normals (and therefore incorrect specular
  highlights and Fresnel response) under exactly the transform class `PbrEffect`'s own code was
  specifically hardened against.
- Why this test cannot catch it: this file always uses `fx.setWorldProperty(Matrix::getIdentityProperty())`
  (line 84) — `mat3(Identity)` and its inverse-transpose are both trivially `Identity`, so the two
  formulas coincide exactly for this scene, and the test's own "must exactly match `PbrEffect`'s golden
  values" design would in fact *still pass* even if this exact bug were freshly introduced, or even if it
  were fixed — the test has no power to distinguish the two implementations from each other under its
  current scene.
- FNA/XNA comparison: N/A (NOXNA effect) — judged against `PbrEffect`'s own already-established correct
  behavior within this same codebase instead, which is the appropriate reference given `SkinnedPbrEffect`'s
  own doc comment explicitly frames it as `PbrEffect`'s "GPU-skinned sibling."
- Related files: a related but distinct issue affects the non-PBR `SkinnedEffect` shaders
  (`EnsureSkinnedProgram()`/`EnsureSkinnedVertexLitProgram()` skip the world-space normal transform
  *entirely*, rather than substituting the wrong matrix for it) — see this shard's
  `easygl_skinnedeffect_preferperpixellighting_test.cpp.audit.md` F1 for the full analysis of that
  sibling defect. Not verified whether Vulkan/Bgfx/D3D9/D3D11/D3D12/SdlGpu's own
  `SkinnedPbrEffect`/`PbrEffect` shader pairs have the equivalent divergence (out of this shard's scope;
  flagged for those backends' own audits, per `plans/plan_cnj.md` CNB-103..109's cross-backend PBR+skinning
  port).
- Suggested future action (not implemented by this audit): give `EnsurePbrSkinnedProgram()` its own
  `uNormalMatrix` uniform (mirroring `EnsurePbrProgram()`'s), bind it in `BindDrawParams()` the same way,
  and use it (composed with the skin normal matrix) for both `vNormal` and `vTangent`; add a dedicated
  `SkinnedPbrEffect` test with a non-uniform-scale `World` (and, ideally, a non-Identity bone) so this
  class of regression becomes observable, since reusing `PbrEffect`'s golden values structurally cannot
  cover it.

## Cross-File Observations

- The "reuse `PbrEffect`'s own golden values, since Identity bind pose is a no-op" design is sound and
  efficient for its *stated* goal (prove the skin-matrix wiring doesn't corrupt the position/vertex
  pipeline) but, as F1 shows, cannot be relied on as *general* evidence that `SkinnedPbrEffect`'s vertex
  shader is behaviorally equivalent to `PbrEffect`'s beyond that one Identity case — worth keeping this
  distinction explicit if this test's own header comment or scope is ever expanded.
- Fragment-shader byte-identity between `EnsurePbrSkinnedProgram()` and `EnsurePbrProgram()` (confirmed
  by direct comparison in this audit) is a genuine strength: it means any future BRDF-formula fix made to
  one is trivially verifiable for "was the other one updated too" by a simple diff, and today they are in
  sync.
- Independently-duplicated stride-68 vertex struct is a first occurrence of this exact stride in this
  shard (unlike the stride-52/56 structs shared across five other files) — no duplication risk yet, but
  the same "magic stride, three backends each redefine it" pattern already flagged for stride-52 in
  `EasyGLGraphicsBackend.cpp`'s own comment likely applies here too, worth checking when this shard's
  Bgfx/Vulkan/SdlGpu `SkinnedPbrEffect` tests are audited.

## Missing or Weak Tests

- See F1 — no test anywhere in this batch exercises `SkinnedPbrEffect` with a non-Identity `World`
  and/or a non-Identity bone transform; this is the single most valuable missing test case for this
  effect.
- No test exercises `SkinnedPbrEffect` with `WeightsPerVertex` > 1 (this file always uses a single
  Identity bone at weight 1) — the plain `SkinnedEffect` sibling tests in this shard cover multi-bone
  blending and `WeightsPerVertex` enforcement thoroughly, but that coverage does not transfer to the PBR
  variant's own shader, which has its own independent `uWeightsPerVertex` gating
  (`EnsurePbrSkinnedProgram()` lines 3773-3774) that is currently untested in isolation.

## Positive Findings

- Reusing an already-verified sibling test's golden images and expected pixel values, with an explicit,
  correct mathematical justification (Identity bind pose ⇒ no-op), is an efficient way to avoid
  duplicating PBR-derivation effort — a good pattern when the underlying premise genuinely holds, which
  this audit confirms it does for the fragment stage.
- The fragment-shader reuse between the skinned and unskinned PBR programs (verified byte-identical) is
  a strong piece of positive evidence that the BRDF math itself is consistent between the two effects.

## Final Assessment

A well-motivated, cleverly-designed reused-oracle test whose premise holds for the specific Identity
scene it uses, but which — by that same design choice — cannot detect a real, confirmed vertex-shader
divergence (F1, HIGH) between `SkinnedPbrEffect` and its own `PbrEffect` sibling that would surface under
any non-uniform-scale `World` transform.
