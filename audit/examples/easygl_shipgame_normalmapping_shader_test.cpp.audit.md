# Audit: examples/easygl_shipgame_normalmapping_shader_test.cpp

## Metadata

- Source file: `examples/easygl_shipgame_normalmapping_shader_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — custom NOXNA `ShaderEffect`/content-pipeline
  shader-conversion proof, not a stock XNA effect test
- File type: C++ example/integration test (single translation unit, `main()`-driven)
- Related production code: `include/Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp` /
  `src/.../ShaderEffect.cpp` (GLSL uniform/texture binding surface exercised here),
  `Microsoft::Xna::Framework::Content::ContentManager::Load<std::shared_ptr<Effect>>` (`.cnj`
  JSON descriptor loader), `Microsoft::Xna::Framework::Graphics::TextureCube`
- XNA/FNA relevance: indirect. `ShaderEffect` itself is a `NOXNA` CNA extension (raw GLSL, not an
  XNA `Effect` subclass with XNA-shaped properties), but the shader being ported —
  `ShipGame_4_0/ShipGame/Content/shaders/NormalMapping.fx` — is a real Microsoft XNA 4.0 sample
  shader (not part of FNA's own source tree; FNA doesn't ship XNA sample content, only the
  framework). The file's header comment reproduces all 3 HLSL techniques verbatim as the
  authoritative reference for the port.
- FNA reference: N/A directly (ShipGame is a Microsoft XNA sample, not FNA framework source); the
  HLSL is quoted in-file instead.
- Main related tests: standalone — this is itself the test (`EasyGLShipGameNormalMappingTest`).

## Purpose

Proves CNA's `ShaderEffect`/content pipeline can correctly compile and execute a hand-ported
GLSL translation of a real XNA sample shader with 3 techniques folded into one program
(`mode` uniform: 0=PlainMapping, 1=NormalMapping, 2=ViewMapping), including a `TextureCube`
reflection-map sample. Placement (`examples/`, `easygl_` prefix) matches the shard's own naming
convention for per-backend integration tests.

## Executive Verdict

**Healthy.** The GLSL in the test (lines 221-312) is a faithful line-by-line port of the HLSL
quoted in the header comment (lines 9-64) — checked term-by-term for `NormalMappingVS`/`PS` and
`ViewMappingVS`/`PS` — and the expected pixel values in `Check A/B/C` are derived from that same
formula, not copied from a previous run. The file also documents (and the audit spot-checked) a
provably-inert mutation (the `normal.xyz - 0.5` vs `*2-1` unpacking choice) alongside a genuinely
discriminating one (swapping `Tangent`/`Normal` in the TBN projection), which is a stronger
standard of self-verification than most test files in this shard show.

## Checklist Results

### API / XNA / FNA parity
N/A in the strict sense (`ShaderEffect` is `NOXNA`), but the *shader semantics* being validated
are XNA-sample-derived — judged against the quoted HLSL, not FNA (ShipGame isn't in the FNA tree).

### Behavioral correctness
- `Check A` (`mode=0`): `FragColor = texture(TextureSampler, vTexCoord)` — direct pass-through of
  `(200,100,50,255)`, no lighting math at all (kFragSrc line 271-274). Test expects exactly that
  (line 478-479, tolerance ±6). Correct — the simplest and least error-prone of the 3 checks.
- `Check C` (`mode=2`): `facing` is squared twice (`facing*=facing` x2, kFragSrc line 276-278),
  giving `Facing^4`, matching `ViewMappingPS`'s HLSL (`Facing *= Facing; Facing *= Facing;`
  header line 60) exactly — not a simplified `pow(facing,4)` that could round differently.
  Samples `TextureSamplerClamp` with `PointClamp` explicitly set for this one draw
  (`Draw` line 434, guarding against bilinear blending at the gradient's own texel boundary that
  would otherwise pull the sample toward the wrong half). The gradient texture is deliberately
  10+10 texels wide (not 1+1), per the file's own documented earlier failure with a 2-texel
  version — a real, acknowledged test-authoring iteration, not a hidden retry.
- `Check B` (`mode=1`): full `NormalMappingPS` port (kFragSrc line 284-311) matches every
  term of the header's HLSL (`ambient`, `specular.xyz *= LightColor*pow(ndoth,...)`, `diffuse.xyz
  *= LightColor*ndotl`, `reflect *= (1-normal.w)`, `glow_intensity`) verified clause-by-clause
  against lines 40-49 of the header comment.

### Logic
The vertex shader's world-to-tangent-space projection (`vLightDir`/`vViewDir` as 3 explicit dot
products against `aTangent`/`aBinormal`/`aNormal`, kVertSrc lines 240-243) is the correct port of
HLSL's `mul(tangent_space, v)` (matrix-times-vector, row-major, header lines 79-89) — this is the
*opposite* multiplication order from a naive `mat3(T,B,N) * v` GLSL translation, which would
silently compute the wrong (transpose) result; the test file's own header explicitly calls this
out as the one non-obvious construct most likely to be ported wrong, and the code matches the
explanation.

### Memory/resource lifetime
`WriteFile()` writes 3 files (`.vert.glsl`/`.frag.glsl`/`.cnj`) to a PID/pointer-namespaced temp
directory (line 336-337) — no cleanup call, but this matches every sibling shader-conversion test
in this shard (consistent, accepted convention: OS temp-dir GC or CI ephemeral filesystem handles
it). `cubeTex_`/`vb_`/`ib_` are `unique_ptr` members with normal RAII teardown; `fxBase_` is a
`shared_ptr<Effect>` owned by the test, `dynamic_cast<ShaderEffect*>` is used read-only, no
lifetime issue.

### C++ correctness
`#pragma pack(push, 1)` + `static_assert(sizeof(ShipNormalMapVertex) == 56)` (lines 209-219)
correctly pins the vertex layout to match the `VertexDeclaration(56, {...})` built at line
352-358 — both the struct and declaration were checked field-by-field (Position@0, TexCoord@12,
Normal@20, Binormal@32, Tangent@44) and agree.

### Performance
N/A / not relevant at this scale (single quad, single frame, 3 draws).

### Thread safety
N/A — single-threaded `Game`-loop test, same as every sibling.

### Architecture
Correctly exercises the `NOXNA` `ShaderEffect` extension point rather than misusing an XNA-named
effect class for non-XNA content — consistent with the project's own layering rules.

### Maintainability
The header comment (163 lines) is unusually long relative to the code (roughly 350 lines) but is
substantive, not padding — it documents the actual derivation, the two verified mutations, and an
explicit, honestly-recorded case where a mutation is *provably* undetectable (the normal-unpacking
convention, lines 148-160), rather than silently omitting that limitation. This is exactly the
kind of self-aware test documentation the anti-boilerplate rule wants to see, not a defect.

### Portability
`#version 300 es` GLSL ES shaders — consistent with every sibling test in this shard; no
raw desktop-GL-only constructs used.

### Robustness
`Draw()` checks `fx->IsEffectValid()` before proceeding and fails loudly with a `[FAIL]` message
plus early `Exit()` if the `.cnj`/GLSL compile step failed (lines 465-471) — correctly avoids a
false pass from a effect that silently failed to load.

### Testing
This file *is* the test; no separate coverage file exists (correct — it's an integration/example
test, not library code needing a unit-test companion per `CHECKLIST.md`).

### Cross-file consistency
Confirmed `ShaderEffect::SetUniformMat4/Vec4/Vec3/Int`, `SetTexture(int, Texture2D&)`,
`SetTexture(int, TextureCube&)` all exist with matching signatures in
`include/.../ShaderEffect.hpp` (lines 43-93) — no phantom API usage.
`GraphicsDevice::DrawIndexedPrimitives(PrimitiveType, baseVertex, minVertexIndex, numVertices,
startIndex, primitiveCount)` signature (confirmed in `GraphicsDevice.hpp` lines 370-372) matches
the call `DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2)` — `primitiveCount=2`
for the quad's 2 triangles, not the past-observed vertex/index-count confusion mistake.

## Detailed Findings

No MEDIUM+ findings. Two minor observations:

- **LOW / INFO** — `Check B`'s expected byte value `(205,140,105,210)` is annotated in the header
  as accurate only within "this test's usual +-6 tolerance," itself acknowledged as absorbing a
  real (if small, ~3%) per-corner-position deviation from the hand-derivation's single-point
  approximation (header lines 104-142). This is disclosed, not hidden, and the ±6 tolerance
  (`Draw()` line 477) is wide enough to cover it — not a defect, just worth noting for anyone
  tightening tolerances later without re-reading this file's own derivation.
- **LOW** — `reinterpret_cast<std::uintptr_t>(this)` (line 337) used to namespace the temp
  directory: harmless in this single-instance-per-process test, standard pattern reused from
  every sibling shader test in the shard.

## Cross-File Observations

Shares the "custom GLSL shader ported from an HLSL `.fx`, validated via a `mode` uniform selecting
between techniques" pattern with `easygl_shipgame_particle_shader_test.cpp` (this same batch) and
the wider ShipGame-shader-porting effort referenced in the header (`AnimSprite.fx`, `Blur.fx`).
Worth confirming during a future `docs/` audit pass that any ShipGame-porting-status doc lists
all 4 shaders (`AnimSprite`, `Blur`, `NormalMapping`, `Particle`) as accounted for, matching this
file's own claim (header line 5-6).

## Missing or Weak Tests

The file's own header (lines 59-66) documents that this session's `ParticleEffect.fx`-family
"rotation not independently exercised" scope note does *not* apply here (this shader has no
rotation output) — so no gap to record for this specific file beyond what's already disclosed
above (Check B's tolerance absorbing a documented small deviation).

## Positive Findings

- Genuinely rare level of self-verification: the header documents *two* actual mutation-testing
  passes (one algebraically proven undetectable, one that correctly failed Check B and was
  reverted), which is exactly the kind of evidence the anti-boilerplate/coverage-validation intent
  of this audit is looking for, done by the original test author rather than left to the audit to
  discover.
- Correct, careful handling of the one genuinely tricky HLSL construct in this shader
  (`mul(matrix, vector)` vs. `mul(vector, matrix)` operand-order ambiguity), with the reasoning
  spelled out rather than just "trust me, it's right."

## Final Assessment

A well-constructed, evidence-backed shader-conversion test. No correctness defects found; the
only notes are pre-existing, disclosed precision-tolerance choices, not gaps this audit had to
surface.
