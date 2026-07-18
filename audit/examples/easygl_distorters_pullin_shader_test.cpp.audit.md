# Audit: examples/easygl_distorters_pullin_shader_test.cpp

## Metadata

- Source file: `examples/easygl_distorters_pullin_shader_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend shader-port pixel-readback test
- File type: C++ example/integration-test executable (`EasyGLDistortersPullInTest : Game`, `main()`)
- Related production code: `ShaderEffect` (`ShaderEffect.cpp`), `GraphicsDevice::DrawIndexedPrimitives`
  (`GraphicsDevice.cpp`), `Matrix::CreateRotationY`/`ToColumnMajor` (`Matrix.cpp:700-716`, `1234-1249`),
  `MathHelper::PiOver4`
- XNA/FNA relevance: ports `DistortionSample_4_0`'s `Distorters.fx`'s `PullIn` technique (used by the sample's
  "Dude" distorter, `Game.cs:78-86`) — the last of the file's three actually-used techniques, confirmed against the
  actual sample source on disk. `ZeroDisplacement` (also in `Distorters.fx`) is confirmed, by direct inspection of
  both `Distorters.fx`'s own comment ("provided for reference") and `Game.cs` (never references
  `DistortionTechnique.ZeroDisplacement`), to be genuinely unused by the sample and correctly not ported.
- FNA reference: N/A directly (sample content); exercises real XNA `VertexPositionNormalTexture` and
  `GraphicsDevice.DrawIndexedPrimitives`.
- Main related tests: sibling to `easygl_distorters_displacementmapped_shader_test.cpp` and
  `easygl_distorters_heathaze_shader_test.cpp` (both in this batch).
- Registered as `cna_test_easygl_distorters_pullin_shader` / `EasyGL_Distorters_PullIn_Shader`
  (`EasyGLTests.cmake:383-388`, TIMEOUT 30s).

## Purpose

Proves the GLSL port of `Distorters.fx`'s `PullIn` technique is correct: a view-space vertex normal, transformed by
a `WorldView` uniform (a *second*, independently-supplied pre-combined matrix alongside `WorldViewProjection`), is
used to compute a displacement whose magnitude and direction depend on how directly the surface faces the camera —
specifically checking that the sign of the `World` rotation genuinely flips the sign of the resulting displacement,
not just its magnitude.

## Executive Verdict

**Healthy** — the ported vertex/fragment shaders match the actual `Distorters.fx` `PullIn_VertexShader`/
`DisplacementPassthrough_PixelShader` exactly, and this audit independently re-derived
`Matrix::CreateRotationY`'s actual row/column layout from source and confirmed both of the test's worked rotation
results (`(sin45,0,cos45)` and `(-sin45,0,cos45)`) are exactly what the real matrix produces under XNA's row-vector
convention — not merely asserted by the test author.

## Checklist Results

### API / XNA / FNA parity
`Matrix::CreateRotationY(float radians)` and `MathHelper::PiOver4` are real XNA 4.0 members, used correctly. The
test's use of two independently-uploaded pre-combined matrix uniforms (`WorldViewProjection` and `WorldView`) rather
than separate `World`/`View`/`Projection` uniforms is a deliberate, explicitly-justified match to
`Distorters.fx`'s own declared uniform shape (`float4x4 WorldViewProjection; float4x4 WorldView;` at the top of the
real file, confirmed lines 9-10) — distinct from, e.g., the sibling `easygl_basiceffect_*` family's use of
`IEffectMatrices`-style separate matrices (not part of this batch, but consistent with this project's general
convention of matching each ported shader's own actual declared uniform shape rather than forcing a single uniform
convention across all ports).

### Behavioral correctness
Cross-checked against `Distorters.fx` (lines 114-152 of the actual sample source):
- HLSL `PullIn_VertexShader`: `output.Position = mul(input.Position, WorldViewProjection); float3 normalWV =
  mul(input.Normal, WorldView); normalWV.y = -normalWV.y; float amount = dot(normalWV, float3(0,0,1)) *
  DistortionScale; output.Displacement = float2(.5,.5) + float2(amount * normalWV.xy);` → ported GLSL (lines
  89-95): `gl_Position = WorldViewProjection * vec4(aPosition,1.0); vec3 normalWV = mat3(WorldView) * aNormal;
  normalWV.y = -normalWV.y; float amount = dot(normalWV, vec3(0,0,1)) * DistortionScale; vDisplacement =
  vec2(0.5,0.5) + amount * normalWV.xy;` — term-for-term match, including the `mat3(WorldView)` truncation (correct
  for transforming a direction/normal, discarding translation) and the deliberate `normalWV.y` sign flip (a
  Y-axis-convention adjustment present in the original, faithfully preserved rather than "fixed away").
- HLSL `DisplacementPassthrough_PixelShader`: `return float4(displacement, 0, 1);` → GLSL `FragColor =
  vec4(vDisplacement, 0.0, 1.0);` — exact match.
- **`Matrix::CreateRotationY` independently re-derived from source** (`Matrix.cpp:707-716`): for row-vector
  convention (`v' = v*M`), `CreateRotationY(θ)` sets `M11=cosθ, M13=-sinθ, M31=sinθ, M33=cosθ` (all else identity).
  For input normal `(0,0,1)`: `v'.x = 0·M11+0·M21+1·M31 = sinθ`, `v'.y=0`, `v'.z = 0·M13+0·M23+1·M33 = cosθ` — i.e.
  `v' = (sinθ, 0, cosθ)`. For `θ=+45°` (`MathHelper::PiOver4`): `(0.70711, 0, 0.70711)` — exactly the file's own
  Check A comment. For `θ=-45°`: `(sin(-45°), 0, cos(-45°)) = (-0.70711, 0, 0.70711)` — exactly the file's own Check
  B comment. Both independently re-derived from the real `CreateRotationY` implementation, not merely trusted.
- **Check A arithmetic re-verified**: `normalWV=(0.70711,0,0.70711)`, `normalWV.y=-0=0` (no-op here, correctly noted
  in the file's own comment as coincidental given `y=0` regardless of the sign flip). `amount = dot(normalWV,(0,0,1))
  *DistortionScale = 0.70711*0.2 = 0.141421`. `Displacement = (0.5,0.5) + 0.141421*(0.70711,0) = (0.5+0.1, 0.5) =
  (0.6,0.5)` (`0.141421*0.70711 = 0.1` exactly, since `sin45°·cos45°·0.2 = 0.5·sin90°·0.2 = 0.1` — a clean identity,
  not a coincidental floating-point near-match) → `(153,128,0,255)`. Matches `aOk` check (tolerance ±6).
- **Check B arithmetic re-verified**: `normalWV=(-0.70711,0,0.70711)` (x sign flips, z unaffected — `dot(normalWV,
  (0,0,1))` only reads `z`, so `amount` is unchanged at `0.141421`). `Displacement = (0.5,0.5) +
  0.141421*(-0.70711,0) = (0.4,0.5)` → `(102,128,0,255)`. Matches `bOk` check. The R-channel flip (153→102) while G
  stays fixed at 128 is a clean, correctly-reasoned discriminator that `World`'s rotation direction genuinely
  reaches and flips the sign of the horizontal displacement component, not merely its presence.

### Logic
`DrawOnce(const Matrix& world)` (lines 156-191) is a clean, reusable single-draw helper; `Draw()` calls it twice
with `±PiOver4` rotations and compares. The comment at lines 168-171 correctly explains why `View=Projection=
Identity` reduces `WorldView` to `World` alone for this specific test, isolating the normal-transform formula from
camera-setup complexity — the same test-simplification pattern used consistently across this task's 3D shader
ports.

### Memory/resource lifetime
`vb_`/`ib_` constructed once in `Initialize()`, reused across both `DrawOnce()` calls — correct, since only `World`
(and derived `WorldView`/`WorldViewProjection`) varies between checks, not the geometry.

### C++ correctness
`const Matrix worldView = world; const Matrix wvp = world;` (lines 171-172) are plain copies, not aliases —
correctly named to make the *reduction* (`WorldView == World`, `WorldViewProjection == World` given
`View=Projection=Identity`) explicit and self-documenting at the call site, rather than passing `world` to both
uniform uploads directly with no naming to explain why that's correct here.

### Robustness
Same `!fx || !fx->IsEffectValid()` guard pattern as every sibling shader test in this batch.

### Testing
Thoroughly and correctly exercises the sign-sensitivity of the `World`→normal-transform→displacement pipeline via
two opposite rotations, and (as an incidental consequence of `normalWV.y` being `0` in both cases, since `y`-axis
rotation cannot change a `(0,0,1)` normal's `y`-component) never actually exercises whether the shader's explicit
`normalWV.y = -normalWV.y` sign-flip statement is present/correct at all — a shader that omitted this line entirely
would produce numerically identical results to the current port for both of this file's checks.

## Detailed Findings

No HIGH or CRITICAL findings.

### F1 — The `normalWV.y = -normalWV.y` sign-flip is never exercised by either check (both checks keep `y=0`)

- Severity: MEDIUM
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: vertex shader `normalWV.y = -normalWV.y;` (line 92); both checks use `Matrix::CreateRotationY`
  rotations of a `(0,0,1)` normal, which by construction always yields `normalWV.y = 0` regardless of the rotation
  angle (a Y-axis rotation cannot move a vector's Y-component when that vector starts with `y=0`).
- Evidence: this audit's own independent re-derivation of `CreateRotationY`'s effect on `(0,0,1)` (see Behavioral
  correctness above) shows `v'.y` is identically `0` for *any* rotation angle around Y when the input normal has
  `y=0` — meaning the sign-flip statement's presence or absence, or even a wrong-axis mistake (e.g. flipping `.x`
  instead of `.y`), cannot be distinguished by either of this file's two checks, since `amount = dot(normalWV,
  (0,0,1))` never reads `normalWV.y` at all, and `Displacement.y = 0.5 + amount*normalWV.y` (the `.y`-component of
  `output.Displacement`) is always `0.5 + amount*0 = 0.5` regardless of the flip — confirmed both checks' expected
  `Displacement.y` is `0.5` in both Check A and Check B (the file's own comments state `(...,0.5,...)` for both),
  which is exactly the value a shader with the sign-flip *removed* would also produce.
- Why it matters: this line exists in the original HLSL specifically to correct a coordinate-convention mismatch
  (documented behavior worth preserving faithfully, per this project's own "match XNA/FNA behavior" mandate) — it
  is real, non-dead code in the port, but this specific test provides zero regression protection for it. A rotation
  around the X or Z axis instead (which *would* move a `(0,0,1)` normal's Y-component) would be needed to actually
  exercise this line.
- FNA/XNA comparison: N/A (this is testing the port's fidelity to the original HLSL's own explicit sign-flip
  statement, not an XNA API-parity question).
- Related files: none beyond this file itself.
- Suggested future action (not implemented by this audit): add a third check using `Matrix::CreateRotationX` (or
  `CreateRotationZ`) instead of `CreateRotationY`, so the test normal's Y-component becomes genuinely nonzero before
  the flip, with a worked expected `Displacement.y` value that would differ depending on whether the flip is present
  — the single most concrete, actionable gap in this file.

## Cross-File Observations

- Shares the two-pre-combined-uniform (`WorldViewProjection` + `WorldView`) convention, correctly matching
  `Distorters.fx`'s own declared uniform shape — same convention used (with `WorldViewProjection` alone) in the
  sibling `DisplacementMapped`/`HeatHaze` tests, each correctly scoped to only the uniforms its own shader actually
  declares.
- Like the sibling `HeatHaze` test (Finding F1 there), this file also has exactly one real coverage gap where a
  specific line of the ported shader (there: the `.x`-displacement three-factor product at its degeneracy point;
  here: the `normalWV.y` sign flip) is exercised only in a configuration where that line's effect is mathematically
  forced to be a no-op — a recurring pattern worth flagging across this whole Task 947 shader-port test population:
  each individual port is faithful and each test's *stated* checks are arithmetically sound, but the choice of test
  geometry (rotations/positions that zero out one axis for simplicity) tends to also zero out exactly the one
  cross-axis interaction term each shader has.

## Missing or Weak Tests

- See F1 (the `normalWV.y` sign-flip is never exercised in a configuration where it has an observable effect) — the
  concrete, actionable gap in this file.
- No test of `DistortionScale`'s scaling behavior independently of the fixed `0.2` value used in both checks (a
  shader that dropped the `*DistortionScale` multiply and used a different fixed constant coincidentally close to
  0.2 could not be distinguished from this) — lower priority, single-line scalar multiply.

## Positive Findings

- The R-channel sign-flip discriminator (153↔102 between the two checks, G held fixed) is a genuine, independently
  re-derived proof that `World`'s rotation direction reaches the normal transform and flips the *sign* of the
  horizontal displacement, not just its magnitude — a meaningfully stronger check than testing only one rotation
  direction would have been.
- The `0.141421*0.70711 = 0.1` identity used to derive the expected displacement is mathematically exact
  (`sin45°·cos45°·0.2 = 0.5·sin90°·0.2 = 0.1`), not a coincidental floating-point near-match — this audit
  independently re-derived the identity and confirms the file's own comment is precise, not approximate.

## Final Assessment

An accurate port of `PullIn_VertexShader`/`DisplacementPassthrough_PixelShader`, with a well-constructed
sign-sensitivity proof for the dominant `dot(normalWV,(0,0,1))`/`normalWV.xy` displacement terms. The one concrete
gap found (F1) is that the shader's explicit `normalWV.y` sign-flip statement — real, intentional, faithfully-ported
code — happens to be mathematically unobservable under both of this file's chosen rotation axes, leaving that one
line without any actual regression protection from this test.
