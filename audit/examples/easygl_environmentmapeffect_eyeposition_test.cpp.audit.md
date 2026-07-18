# Audit: examples/easygl_environmentmapeffect_eyeposition_test.cpp

## Metadata

- Source file: `examples/easygl_environmentmapeffect_eyeposition_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test for `EnvironmentMapEffect` (`examples-tests-easygl` shard)
- File type: C++ example/integration test (`Game`-subclass, hand-rolled `main()`, no GoogleTest)
- Related production code: `include/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp`,
  `src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp`,
  `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp` (`EnsureEnvMapped3DProgram()`, lines 3145-3270)
- XNA/FNA relevance: exercises `Microsoft::Xna::Framework::Graphics::EnvironmentMapEffect`'s `EyePosition`
  derivation, an XNA-facing behavior.
- FNA reference: `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/Effect/StockEffects/HLSL/EnvironmentMapEffect.fx`
  (`ComputeEnvMapVSOutput`: `eyeVector = normalize(EyePosition - pos_ws.xyz)`, `EnvCoord = reflect(-eyeVector,
  worldNormal)`), `EffectHelpers.cs`/`EnvironmentMapEffect.cs` (`EyePosition` set from `Matrix.Invert(view).Translation`
  in `SetLightingMatrices`).
- Registered as CTest target: `EasyGL_EnvironmentMapEffect_EyePosition` (`cmake/Tests/EasyGLTests.cmake:598-600`).

## Purpose

Task 397 test. Verifies that `EnvironmentMapEffect`'s reflection-vector sampling genuinely depends on the
effect's derived `EyePosition` (from `Matrix::Invert(view_).Translation`, confirmed at
`EnvironmentMapEffect.cpp:347-355`/`:452-453`), not merely on the fixed vertex normal. Uses a 6-color
(one per cube face) `TextureCube` built by `makeDistinctCube()` so a wrong reflection vector reads back a
visibly wrong face color, unlike prior same-phase tests (393-396) that used solid-color cubes and could not
distinguish "hit the right face" from "hit any face."

## Executive Verdict

**Healthy.** The two camera geometries and their expected dominant cube faces are independently
hand-derivable (verified below) and match both the actual `EnvironmentMapEffect::OnApply()` eye-position
code path and the EasyGL fragment shader's `reflect(-E,N)` computation. This is a genuine geometric
discriminating test, not a compiles-and-runs check.

## Checklist Results

### API / XNA / FNA parity
`PASS`. `EnvironmentMapEffect` construction and property calls (`setTextureProperty`,
`setEnvironmentMapProperty`, `setEmissiveColorProperty`, `setEnvironmentMapAmountProperty`,
`setEnvironmentMapSpecularProperty`, `setFresnelFactorProperty`, `setWorldProperty`/`setViewProperty`/
`setProjectionProperty`) all match the XNA property surface (`Texture`, `EnvironmentMap`, `EmissiveColor`,
`EnvironmentMapAmount`, `EnvironmentMapSpecular`, `FresnelFactor`, `World`/`View`/`Projection` in FNA's
`EnvironmentMapEffect.cs`) under this project's `get*Property`/`set*Property` convention.

### Behavioral correctness
`PASS`, verified by hand-derivation. Case (a): eye at `(0,0,3)` looking at the quad centered at the origin
with normal `(0,0,1)` gives `eyeVector = (0,0,1)`, and `reflect((0,0,-1),(0,0,1)) = (0,0,1)` exactly (dot
product term is `-1`, `reflect = I - 2*dot(I,N)*N = (0,0,-1)+(0,0,2) = (0,0,1)`) — dominant `+Z`, i.e. the
`PositiveZ` (blue) face, matching the test's `check()` at line 167-169. Case (b): eye at `(5,0,0.5)` gives
`eyeVector ≈ (0.995,0,0.0995)` (magnitude `sqrt(25.25)≈5.025`), and
`reflect(-eyeVector,(0,0,1)) = (-0.995,0,-0.0995) - 2*(-0.0995)*(0,0,1) = (-0.995,0,0.0995)` — dominant `-X`
by ~10:1, i.e. `NegativeX` (cyan), matching line 174-176. Both derivations check out against the file's own
stated comments and are independently reproducible.

### Logic
`PASS`. `EnvironmentMapAmount=1` and `FresnelFactor=0` (line 124/126) deliberately flatten the Fresnel blend
factor to a constant `1.0` (per `EnsureEnvMapped3DProgram`'s vertex shader: `vFresnel=(uFresnelEnabled>0.5)
? ... : uEnvMapAmount;`, and `fresnelEnabled_` is only set when `fresnelFactor_ != 0`, confirmed at
`EnvironmentMapEffect.cpp:272-284`), isolating this test to pure reflection-vector correctness as claimed.
`EmissiveColor=0` with no lights explicitly disabled relies on `DirectionalLight0`'s *default*
`diffuseColor_`/`direction_` being `(0,0,0)` (verified: `DirectionalLight.hpp:77-80` default-constructs both
`Vector3` fields, and `Vector3`'s default ctor zero-initializes) — see Cross-File Observations below for a
note on this implicit-defaults dependency shared by several sibling tests.

### Memory/resource lifetime
`PASS`. `makeDistinctCube()` returns `std::unique_ptr<TextureCube>` by value (RVO), `cube` and `tex` are
stack/local-scope for the duration of both `renderWith()` calls; no dangling-pointer risk since both cube
and texture outlive the `EnvironmentMapEffect fx` instances constructed inside `renderWith()`.

### C++ correctness
`PASS`. `renderWith()` takes `const VertexPositionNormalTexture (&quad)[6]` — a reference to a
fixed-size array, avoiding an unnecessary copy/decay-to-pointer; correct signature for a 6-vertex quad passed
twice.

### Performance
`N/A` for a one-shot test file — not hot-path code.

### Architecture
`PASS`. Correct layering: only talks to the public XNA-facing `EnvironmentMapEffect`/`GraphicsDevice` API,
no direct backend calls.

### Maintainability
`PASS`. The file header comment (lines 1-27) is unusually thorough — it derives both expected results by
hand before the code exists, which is exactly what makes this file auditable without re-deriving the math
from scratch. `check()`/`closeTo()`/`colourMatch()` helpers are shared boilerplate identical across every
sibling `EnvironmentMapEffect` test in this shard (see Cross-File Observations).

### Robustness
`N/A` — a single-shot integration test, not user-facing input-handling code.

### Testing
This *is* a test file; see "Missing or Weak Tests" below for coverage gaps in what it does *not* check.

### Cross-file consistency
`PASS`. Matches sibling tests' shared conventions (`kSize=64`, `readCenter()`, `colourMatch(tol=20)`,
`RasterizerState::CullNone` with the "Task 896" comment) byte-for-byte in structure.

## Detailed Findings

No CRITICAL/HIGH findings.

### F1 — Implicit reliance on `DirectionalLight0`'s default zero diffuse color, never verified in-test

- Severity: LOW
- Confidence: HIGH
- Category: test-robustness
- Location/symbol: `Draw()` (does not call `DirectionalLight0.setEnabledProperty(false)` or
  `setDiffuseColorProperty(Vector3::Zero)` anywhere), relies on `EnvironmentMapEffect`'s constructor-set
  `DirectionalLight0.setEnabledProperty(true)` (`EnvironmentMapEffect.cpp:40`) combined with
  `DirectionalLight0`'s own default-constructed `diffuseColor_ = (0,0,0)`.
- Evidence: `EmissiveColor=0` alone does not zero the lit term — `FillGpuDrawParams()`
  (`EnvironmentMapEffect.cpp:433-437`) still forwards `DirectionalLight0`'s direction/diffuse to the GPU
  since it's enabled; the shader's `litRGB=lightSum*uDiffuseColor.rgb+uEmissiveColor` (line 3229 of
  `EasyGLGraphicsBackend.cpp`) would only be silently zero because `DirectionalLight0`'s diffuse color
  itself defaults to black, not because the test explicitly disabled/zeroed it.
- Why it matters: if `DirectionalLight0`'s default diffuse color or direction were ever changed (e.g. to
  match `EnableDefaultLighting()`'s key-light values, a plausible future "fix the default to look less
  flat" change), this test — and several siblings that share the same pattern (worldtransform, golden) —
  would start reading a non-zero lit contribution mixed into the reflection-only signal, without any
  visible signal in the test file itself that a hidden assumption broke.
- FNA/XNA comparison: FNA's own `DirectionalLight` fields (`Vector3 Direction`, `Vector3 DiffuseColor`) also
  default to `Vector3.Zero`, so this is not a CNA-vs-FNA divergence — it's a latent test-fragility risk
  shared with FNA's own default field values, not a behavioral bug.
- Suggested future action (not implemented by this audit): explicitly call
  `fx.DirectionalLight0.setDiffuseColorProperty(Vector3::Zero)` (or `setEnabledProperty(false)`) in this and
  the sibling tests that rely on it, making the isolation intent self-documenting rather than
  default-dependent.

## Cross-File Observations

- Shares its `check()`/`closeTo()`/`colourMatch()`/`readCenter()` helper boilerplate verbatim with every
  other `easygl_environmentmapeffect_*_test.cpp` file in this batch — a legitimate, low-risk duplication for
  8 independent single-file executables (each is its own translation unit/binary), not a maintainability
  defect, but a candidate for a shared test-helper header if this pattern grows further (see also
  `PixelTestGame.hpp`, which some newer sibling tests, e.g. `_golden_test.cpp`, already migrated to).
- The `Matrix::CreateLookAt` + `Matrix::CreatePerspectiveFieldOfView` combination used for case (b) is the
  identical camera-setup idiom used by `_fresnel_test.cpp`/`_specular_test.cpp`'s head-on case; consistent
  across the shard.

## Missing or Weak Tests

- Only tests 2 camera positions/2 faces; does not test a case where the eye and quad geometry combine to
  produce a near-tie between two adjacent faces (an edge/corner-blend scenario), which is the one case the
  `_worldtransform_test.cpp`'s own header comment explicitly calls out as an artifact risk with a 1x1-texel
  cube map. Not a defect in this file, just an uncovered edge case worth noting for the subsystem's overall
  coverage picture.
- Does not test `EyePosition` changing *without* a `View` change (e.g. via a hypothetical direct
  `EyePosition` setter) — moot here since `EnvironmentMapEffect` has no such public setter (`EyePosition` is
  purely derived), so this is correctly not applicable rather than a gap.

## Positive Findings

- The file's own header comment pre-derives both expected results with the exact reflection-vector algebra
  before any code runs, which is genuinely useful for anyone re-verifying this test later (and made this
  audit's own verification straightforward and fast).
- Correctly isolates the variable under test (reflection vector) from every other `EnvironmentMapEffect`
  term (Fresnel, specular, lighting) via `FresnelFactor=0`/`EmissiveColor=0`/`EnvironmentMapSpecular=0`,
  rather than testing everything at once.

## Final Assessment

A well-targeted, geometrically-verified regression test for `EnvironmentMapEffect`'s `EyePosition`-driven
reflection vector. Both expected colors were independently re-derived during this audit and matched the
file's own comments and the actual EasyGL shader logic (`EnsureEnvMapped3DProgram`,
`EasyGLGraphicsBackend.cpp:3145-3243`). No correctness defects found; only a shared, low-severity
default-dependency fragility noted (F1).
