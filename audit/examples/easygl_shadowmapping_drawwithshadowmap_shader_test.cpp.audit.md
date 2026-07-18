# Audit: examples/easygl_shadowmapping_drawwithshadowmap_shader_test.cpp

## Metadata

- Source file: `examples/easygl_shadowmapping_drawwithshadowmap_shader_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — Task 947 (Phase 78): HLSL→GLSL conversion proof for
  `ShadowMappingSample_4_0`'s `DrawModel.fx`, `DrawWithShadowMap` technique — sibling/completion of
  `easygl_shadowmapping_createshadowmap_shader_test.cpp`
- File type: raw `Game`-derived executable, four checks (A/B/C/D), with 2 documented mutation-testing passes
- XNA/FNA relevance: direct 1:1 HLSL→GLSL port of a real XNA sample shader, including a deliberately-preserved
  HLSL quirk (alpha zeroing in the shadow branch).
- FNA reference: `DrawModel.fx`'s `DrawWithShadowMap_VertexShader`/`_PixelShader`, quoted in the header comment —
  the `ShadowMappingSample_4_0` sample source is **not present** in the local FNA reference tree
  (`/rv/data/library/github.com/FNA-XNA/FNA/src` contains only the core FNA runtime, not community XNA samples),
  so the quoted HLSL could not be independently diffed against a local original by this audit; the ported GLSL was
  instead checked for internal self-consistency and against the API-level math this audit *can* verify
  (`Matrix::CreateOrthographic`, already confirmed correct in the sibling report).
- Related production files: `Matrix.cpp`, `ShaderEffect.cpp`, `EasyGLGraphicsBackend.cpp`.

## Purpose

Ports the "lit scene, darkened where shadowed" half of the two-pass shadow-mapping technique: per-pixel diffuse
lighting plus an ambient term, darkened by 50% (RGB only, not alpha) wherever a shadow-map depth lookup shows a
closer occluder than the current fragment's own light-space depth.

## Executive Verdict

**Healthy.** All four checks were independently re-derived by this audit from the ported GLSL and the actual
`Matrix::CreateOrthographic` coefficients (not trusted from the comment) and match exactly; the file's own
documented mutation tests (disabling the shadow branch; hardcoding the shadow texcoord) were evaluated for
plausibility and are sound reasoning, not hand-waved.

## Checklist Results

### API / XNA / FNA parity
Ported GLSL (lines 154-206) mirrors the quoted HLSL line-for-line: `mul(input.Normal, World)` (a 4×4×3-vector
multiply, implicitly truncating) → `mat3(World) * aNormal`; `saturate(dot(...))` → `clamp(dot(...), 0.0, 1.0)`;
`tex2D(...)` → `texture(...)`; the shadow-coordinate remap `0.5 * lightingPosition.xy / lightingPosition.w +
float2(0.5,0.5)` and the `ShadowTexCoord.y = 1.0 - ShadowTexCoord.y` V-flip are reproduced exactly, including the
V-flip (a real, necessary GL-vs-D3D texture-coordinate-origin difference, correctly preserved rather than
"corrected away").

### Behavioral correctness
**A faithfully-preserved HLSL quirk, correctly documented as preserved rather than silently fixed**: the shadow
branch `diffuse *= vec4(0.5, 0.5, 0.5, 0.0)` (line 201) also zeroes alpha — verified this is genuinely what the
real HLSL does (`diffuse *= float4(0.5,0.5,0.5,0)`, quoted at line 31) and that the file's own claim (benign only
because the sample/test always draws `BlendState.Opaque`, which ignores alpha) is correct: `BlendState::Opaque`
(src=One, dst=Zero) means the destination-blend factor is always zero and the source contributes fully regardless
of its own alpha, so a zeroed alpha channel has no visible blending effect — confirmed sound reasoning, not merely
asserted.

### Logic
Independently re-derived (without relying on the file's own worked numbers) using `World=Translate(0,0,-5)` (fixed
for every check) and `LightViewProjBase = Matrix::CreateOrthographic(10,10,-10,10)` (same coefficients as the
sibling test: `M11=2/10=0.2`, `M33=-0.05`, `M43=0.5`, `M44=1`):
- `vWorldPos.z = -5` (pure Z-translation, local z=0 for every quad corner) → at the screen centre (x=y=0 in local/
  world space by the quad's own symmetry), `lightingPosition = LightViewProjBase * (0,0,-5,1)`: `z_clip =
  -5×(-0.05)+0.5 = 0.75`, `w_clip=1` → `ourdepth = 0.75 - 0.001 = 0.749` — matches the file's own stated value
  exactly.
- Diffuse term: `World` is a pure translation (`mat3(World)=Identity`), so world normal stays `(0,0,1)`;
  `LightDirection=(0,0,1)` → `dot=1`, `diffuseIntensity=1`. With texel `(200,100,50,255)≈(0.7843,0.3922,0.1961,1)`
  and `AmbientColor=(0.15,0.15,0.15,0)`: unshadowed `=(0.9343,0.5422,0.3461,1.0)→byte(238,138,88,255)` — matches.
  Shadowed `= unshadowed × (0.5,0.5,0.5,0) ≈ (0.4672,0.2711,0.1731,0.0) → byte(119,69,44,0)` — matches.
- Check A/B (`shadowWhite_`/`shadowBlack_`, `LightViewProjBase`): `shadowdepth=1.0 ≥ 0.749` (not shadowed, A) and
  `shadowdepth=0.0 < 0.749` (shadowed, B) — both correctly select the branch the assertions expect.
- Check C/D (`shadowSplit_`, `LightViewProjLeft/Right = Translate(∓2.5,0,0) × LightViewProjBase`): under this
  codebase's row-vector-via-left-composition convention (confirmed by independent derivation: `v×(Translate×Base)
  = (v+t)×Base`), the world-space point effectively shifts by `(∓2.5,0,0)` *before* the orthographic projection —
  `clip.x = ∓2.5 × M11(Base) = ∓2.5×0.2 = ∓0.5` → `shadowTexCoord.x = 0.5×(∓0.5)+0.5 = 0.25` (Check C, left
  texel's own centre) or `0.75` (Check D, right texel's own centre) — both exactly at each texel's centre, so
  `GL_LINEAR` filtering resolves to that texel's own colour with zero blend from its neighbour, matching the file's
  own claim that no explicit `PointClamp` override is needed here (unlike the real sample's own code, which needs
  it for a different, non-centre-aligned sampling scenario). Check C → unshadowed value (matches A); Check D →
  shadowed value (matches B).
All four independently re-derived values match the file's own assertions exactly.

### Memory/resource lifetime
Four `Texture2D` value members (`diffuseTex_`, `shadowWhite_`, `shadowBlack_`, `shadowSplit_`), all
default-constructed then assigned via `Texture2D::CreateFromPixels()` — consistent pattern throughout this shard.
Temp directory never cleaned up — see F1.

### C++ correctness
No issues found.

### Performance
N/A.

### Robustness
`device.setBlendStateProperty(BlendState::Opaque)` (line 283) is load-bearing for the "alpha-zeroing is benign"
claim (see Behavioral correctness) — confirmed present in every `DrawOnce()` call, not just assumed globally.

### Testing
**Two documented mutation tests, independently assessed for soundness**: (1) disabling the shadow-darkening branch
entirely — correctly predicted to fail B and D (both would read the unshadowed value) while leaving A/C
unaffected, since A/C's shadow-map lookups already report "not shadowed" regardless of the branch's presence; (2)
hardcoding `shadowTexCoord=(0.5,0.5)` (the exact boundary between the split map's two texels) — correctly reasoned
to produce a 50/50 blended read (~mid-gray, sampling below `ourdepth`) that lands in the *shadowed* branch for
both C and D, so Check C (which expects the *unshadowed* value) would fail while D's own outcome becomes
coincidentally correct under this specific mutation — the file's own comment is careful to note this asymmetry
(only C's failure is meaningful evidence, not D's accidental pass) rather than overclaim the mutation's
discriminating power. Both mutation narratives are logically sound as described.

### Cross-file consistency
The `z=-5`/`Depth=0.75` value is explicitly and correctly cross-referenced from the sibling
`easygl_shadowmapping_createshadowmap_shader_test.cpp`'s own Check B — confirmed identical.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — Temp directory (3 files) written per test run, never cleaned up

- Severity: LOW
- Confidence: HIGH
- Category: resource-hygiene
- Location/symbol: `Initialize()`, lines 228-240
- Evidence: no cleanup call in the file.
- Why it matters: same shared low-priority gap as the rest of this batch.
- Suggested future action (not implemented by this audit): clean up on success or failure.

## Cross-File Observations

- The `ShadowTexCoord.y = 1.0 - ShadowTexCoord.y` V-flip is only exercised at `clip.y=0` for every check (the
  shadow-map texture is 1 texel tall) — the file's own comment correctly and explicitly flags this as an
  intentional, documented scope reduction rather than an accidental gap, mirroring the sibling `CreateShadowMap`
  test's own equivalent scope note.
- Together with the sibling `CreateShadowMap` test, this file completes DEFERRED.md item #11 (per the header
  comment) — this audit did not independently check `DEFERRED.md`'s own bookkeeping accuracy (out of this batch's
  scope) but the technical claim (both `DrawModel.fx` techniques now have GLSL ports) is consistent with what both
  files actually implement.

## Missing or Weak Tests

- The `ShadowTexCoord.y` flip's correctness for a non-degenerate (non-1-texel-tall) case is not covered anywhere
  in this batch — explicitly acknowledged as a scope reduction, not silently missing.
- FNA sample source (`DrawModel.fx`) itself could not be located in the local reference tree to independently diff
  the quoted HLSL byte-for-byte — noted as an audit-scope limitation, not a defect in the file under audit.

## Positive Findings

- All four checks were independently re-derivable from first principles (matrix coefficients + shader logic) and
  matched exactly — a well-constructed, non-circular test.
- The two documented mutation tests are reasoned about with real nuance (explicitly noting when a mutation's
  "pass" on one check is coincidental rather than meaningful), which is a stronger, more honest verification
  standard than most test comments in this codebase attempt.
- The alpha-zeroing HLSL quirk is preserved and explained rather than "corrected," matching this project's stated
  behavior-fidelity principle (match XNA/FNA behavior over personal preference) even for community-sample-derived
  shaders.

## Final Assessment

A rigorous, independently-verified 4-check port of `DrawModel.fx`'s `DrawWithShadowMap` technique; all expected
values were recomputed from first principles by this audit and match. Only the shared low-priority temp-file
cleanup gap (F1) and an inherent, disclosed audit-scope limit (no local copy of the original sample source) are
recorded.
