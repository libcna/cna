# Audit: examples/easygl_shipgame_particle_shader_test.cpp

## Metadata

- Source file: `examples/easygl_shipgame_particle_shader_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — custom NOXNA `ShaderEffect` shader-conversion proof
- File type: C++ example/integration test (single translation unit, `main()`-driven)
- Related production code: `ShaderEffect.hpp`/`.cpp` (GLSL uniform/texture binding),
  `EasyGLGraphicsBackend.cpp`'s `PrimitiveType::PointListEXT -> GL_POINTS` dispatch (confirmed at
  line ~1879/1893 of that file)
- XNA/FNA relevance: indirect — ports `ShipGame_4_0/ShipGame/Content/shaders/Particle.fx`, a real
  Microsoft XNA 4.0 sample shader not present in the FNA framework source tree (FNA doesn't ship
  sample content). HLSL reproduced verbatim in the header comment as the port's reference.
- Main related tests: standalone — `EasyGLShipGameParticleTest` is itself the test.

## Purpose

Proves CNA's GPU point-sprite path (`gl_PointSize` output + `gl_PointCoord` input, GLSL ES 3.00
native, no `GL_PROGRAM_POINT_SIZE` enable needed) correctly executes a ported HLSL `PSIZE`-based
particle shader, including per-particle lifecycle (`ElapsedTime`/`ParticleTime` time-warping,
"not yet alive" culling via a huge position offset) and color lerp. Distinct from
`ParticleEffect.fx`'s earlier quad-corner-billboard technique (different file, noted in the
header) — this is a genuinely different rendering technique (real point primitives), not a
re-test of the same code path under a new name.

## Executive Verdict

**Healthy.** All 4 GLSL constructs central to the shader (`time<0` culling, `mod()`-based
looping, `mix()` color lerp, `gl_PointSize` sizing) are exercised by a distinct check, each with a
documented, hand-traceable expected value, and 2 real mutation tests (argument-order swap on the
color lerp, and removing the culling branch) are recorded as having correctly failed the
corresponding check before being reverted — the file demonstrates its own discriminating power
rather than asserting it.

## Checklist Results

### API / XNA / FNA parity
N/A directly (`ShaderEffect` is `NOXNA`); shader semantics judged against the quoted
`ShipGame_4_0/.../Particle.fx` HLSL (header lines 18-49), since FNA's own tree has no ShipGame
sample content to diff against.

### Behavioral correctness
Traced all 4 checks against the GLSL (lines 157-229):
- **Check A** (centre, `ElapsedTime=0`): `time=0`, not `<0` so no culling; `norm_time=0` ->
  `color_factor = 1-(1-0)*1 = 0` -> `vColor = mix(StartColor, EndColor, 0) = StartColor = red`.
  Matches expected `(255,0,0,255)` (line 345-346).
- **Check B** (off-sprite, same `ElapsedTime=0`): sample point `(4, H/2)` is well outside a
  10px-diameter sprite centred at `(W/2, H/2)` on a 64x64 viewport — correctly proves
  `gl_PointSize` bounds the sprite rather than covering the screen. Expected clear colour
  `(10,10,10,255)`, matching `device.Clear(Color(10,10,10,255))` at line 287.
- **Check C** (`ElapsedTime=-10`): `time=-10<0` -> `pos.xyz = vec3(1e10)` (line 181-183) *before*
  the `mod()`/integral adjustment, so the tiny subsequent per-frame position nudge
  (`VelocityScale*norm_vel*integral*ParticleTime`, magnitude ~4e-5 per the header's own derivation)
  is utterly negligible against `1e10` — the point is genuinely displaced far outside any
  plausible view frustum. Expected clear colour, correctly distinguishing "moved away" from "just
  didn't render".
- **Check D** (`ElapsedTime=0.5`): `norm_time=0.5` -> `color_factor=0.5` ->
  `mix(red,blue,0.5)=(0.5,0,0.5,1)` -> byte `~(128,0,128,255)`, matching expected (line 351-352).
  Distinct from Check A's pure red, proving the lifecycle-driven lerp is genuinely time-dependent.

### Logic
`mul(norm_vel, (float3x3)WorldViewProj)` (header line 35, standard `mul(v,M)` row-vector
convention, the *opposite* order from the sibling `NormalMapping.fx` test in this same batch) is
ported as `mat3(WorldViewProj) * norm_vel` (kVertSrc line 199) — correctly using the
vector-times-matrix convention consistent with the header's own note (line 53-54) that this
shader uses the "standard" `mul(v,M)` order, unlike `NormalMapping.fx`'s `mul(M,v)`. This is a
real, easy-to-invert detail and it's ported correctly.

Velocity-aligned rotation (`OutRotation`/`ParticlePS`'s texcoord rotation) is deliberately not
independently value-checked — the test uses a solid-white particle texture specifically so
`Color * white == Color` regardless of the (unverified) rotated `tc`, isolating the checks above
from needing to hand-derive the rotation matrix (header lines 59-66). This is an honest, disclosed
scope limitation, not a silently-skipped code path — the GLSL for rotation (`kFragSrc` lines
222-226) is still present and exercised (it runs on every draw), just not independently asserted.

### Memory/resource lifetime
Same temp-directory-write-no-cleanup pattern as every sibling shader test in this shard — accepted
convention, not flagged per-file repeatedly.

### C++ correctness
`#pragma pack(push,1)` + `static_assert(sizeof(ParticlePointVertex) == 32)` (lines 147-155)
correctly matches the `VertexDeclaration(32, {...})` (lines 263-267: Position@0, Normal@12 [really
Velocity, reusing the Normal semantic slot — see Cross-File Observations], TextureCoordinate@24).

### Performance
N/A at this scale.

### Architecture
Same `NOXNA ShaderEffect` extension-point usage as the sibling normal-mapping test — correct
layering, no XNA-namespace pollution.

### Maintainability
Header comment thoroughly documents 2 "deliberate scope reductions" (rotation, burst-mode sizing)
plus the reasoning for choosing a tiny non-zero `Velocity=(0,0,0.0001)` specifically to dodge
`normalize((0,0,0))`'s NaN while keeping the resulting position drift numerically negligible
(header lines 68-74) — a concrete, checkable engineering justification, not a hand-wave.

### Robustness
Same `IsEffectValid()` gate before proceeding as the sibling test (`Draw()` lines 326-332).

### Testing
This file is itself the test (correct, matches project convention for `examples/*_test.cpp`).

### Cross-file consistency
`VertexElementUsage::Normal` is reused to carry the particle's `Velocity` field in the
`VertexDeclaration` (line 265: `VertexElement(12, Vector3, VertexElementUsage::Normal, 0)`) purely
as a attribute-slot label — semantically this data is velocity, not a surface normal. This is
consistent with how `VertexElementUsage` values are generally just attribute-location tags in
CNA's declaration API (not enforced to match the shader's actual semantic use), and the comment at
line 146 ("Matches ParticleVS's own field order: Position, Velocity, TexCoord") makes the intent
clear at the point of use — not a hidden trap, but worth a one-line note for anyone skimming the
declaration in isolation without the adjacent comment.

## Detailed Findings

No MEDIUM+ findings.

- **LOW / INFO** — `VertexElementUsage::Normal` used as a stand-in slot for `Velocity` data (see
  Cross-File Observations above). Purely a labeling/readability nit; the actual shader attribute
  binding is by `location=` index, not by `VertexElementUsage` semantics, so there is no functional
  risk — flagging only because a future reader unfamiliar with this file's convention could be
  briefly confused.

## Cross-File Observations

Companion to `easygl_shipgame_normalmapping_shader_test.cpp` (audited alongside this file in the
same batch) — both port real ShipGame HLSL shaders 1:1 into a single parameterized/combined GLSL
program and both document genuine mutation-testing passes. Together with the header's own
reference to `AnimSprite.fx`/`Blur.fx` (ported earlier in the same effort), this confirms (per the
header's own claim, line 3-6) all 4 of ShipGame's distinct custom shaders are now covered by a
dedicated EasyGL test.

## Missing or Weak Tests

Rotation (`OutRotation`) and burst-mode sizing (`TotalTime==ParticleTime`) are executed but not
independently asserted (see Logic section) — an honest, disclosed gap. A follow-up test using a
non-white, non-symmetric particle texture plus a known velocity/WVP combination could close this
gap if the rotation math is ever suspected of regressing; not required by this file's own stated
scope.

## Positive Findings

- Two genuine mutation tests recorded with their actual (correct) failure signatures (header lines
  96-103), not just claimed — this is real evidence of discriminating power, matching this shard's
  best examples.
- Careful, correct handling of the `mul(v,M)` vs. `mul(M,v)` distinction, cross-referenced
  explicitly against the sibling normal-mapping shader's opposite convention.

## Final Assessment

A carefully constructed, self-verifying point-sprite shader test with disclosed (not hidden) scope
limitations around rotation/burst-sizing. No correctness defects found.
