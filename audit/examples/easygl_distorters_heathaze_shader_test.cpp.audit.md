# Audit: examples/easygl_distorters_heathaze_shader_test.cpp

## Metadata

- Source file: `examples/easygl_distorters_heathaze_shader_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend shader-port pixel-readback test
- File type: C++ example/integration-test executable (`EasyGLDistortersHeatHazeTest : Game`, `main()`)
- Related production code: `ShaderEffect` (`ShaderEffect.cpp`), `GraphicsDevice::DrawIndexedPrimitives`
  (`GraphicsDevice.cpp`), `Matrix::CreateRotationY`/`ToColumnMajor` (`Matrix.cpp`)
- XNA/FNA relevance: ports `DistortionSample_4_0`'s `Distorters.fx`'s `HeatHaze` technique (used by the sample's
  "Cylinder" distorter, `Game.cs:87-95`) — confirmed against the actual sample source on disk.
- FNA reference: N/A directly (sample content); exercises real XNA `VertexPositionNormalTexture` and
  `GraphicsDevice.DrawIndexedPrimitives`.
- Main related tests: sibling to `easygl_distorters_displacementmapped_shader_test.cpp` and
  `easygl_distorters_pullin_shader_test.cpp` (all in this batch) — together covering `Distorters.fx`'s three
  actually-used techniques.
- Registered as `cna_test_easygl_distorters_heathaze_shader` / `EasyGL_Distorters_HeatHaze_Shader`
  (`EasyGLTests.cmake:377-381`, TIMEOUT 30s).

## Purpose

Proves the GLSL port of `Distorters.fx`'s `HeatHaze` technique is correct: a *procedurally generated* distortion
value (derived from screen position and a `Time` uniform, not sampled from any texture) matches the original HLSL's
specific trig formula — including the un-obvious reuse of the raw, pre-perspective-divide clip-space position as an
interpolated `TEXCOORD` varying rather than any NDC/screen-space value.

## Executive Verdict

**Healthy** — the ported vertex/fragment shaders were checked line-for-line against the actual `Distorters.fx`
source, the deliberately chosen test geometry (`World=View=Projection=Identity`) was confirmed to make the
raw-clip-space-as-varying trick exactly equal to local vertex coordinates (eliminating the one genuinely subtle
aspect of this shader from the test's own complexity), and both checks' trig arithmetic was independently
recomputed and matches.

## Checklist Results

### API / XNA / FNA parity
`VertexPositionNormalTexture` (used with a uniform up-facing normal `(0,0,1)` for all four quad vertices, lines
153-159) is a real XNA vertex type, used correctly even though this particular shader's vertex input only actually
needs `POSITION` (the normal/texcoord channels are present in the C++ vertex struct but unused by
`TransformAndCopyPosition_VertexShader`, which only reads `position : POSITION` per the original HLSL, confirmed at
`Distorters.fx` line 76) — a reasonable convenience choice (reusing an existing supported vertex format rather than
introducing a position-only one) rather than a mismatch, since the GLSL vertex shader's `layout(location=0) in
vec3 aPosition;` (line 92) correctly reads only the position channel and never declares a normal/texcoord input,
so the unused vertex fields are simply never read by the shader — no functional issue.

### Behavioral correctness
Cross-checked against `Distorters.fx` (lines 67-105 of the actual sample source):
- HLSL `TransformAndCopyPosition_VertexShader`: `output.Position = mul(position, WorldViewProjection);
  output.PositionAsTexCoord = output.Position;` (the deliberately unusual reuse of the *raw, pre-divide clip-space*
  position as a plain interpolated `TEXCOORD`, not `gl_Position`'s own automatic NDC/screen mapping) → ported GLSL
  (lines 90-98): `gl_Position = WorldViewProjection * vec4(aPosition,1.0); vPositionAsTexCoord = gl_Position;` —
  exact structural match, correctly reusing the same computed value for both outputs rather than re-deriving
  screen-space coordinates some other way.
- HLSL `HeatHaze_PixelShader`: `displacement.x = sin(position.x/60 + Time*1.5) * sin(position.x/10) *
  cos(position.x/50); displacement.y = sin(position.y/50 - Time*2.75); displacement *= DistortionScale; displacement
  = (displacement + float2(1,1))/2;` → ported GLSL (lines 108-116) — term-for-term identical, including operator
  precedence (the three-factor product for `.x`, the single `sin` for `.y`) and the final `(d+1)/2` remap to
  `[0,1]`.
- **Test geometry validity, independently re-derived**: with `World=View=Projection=Identity`,
  `gl_Position = vec4(localPos, 1.0)` exactly, so the interpolated `vPositionAsTexCoord` at any rasterized point
  equals that point's own local `(x,y,z,1)` — confirmed this is *not* a general property of the raw-clip-space
  trick (in general, `PositionAsTexCoord` is genuinely pre-divide clip space, distinct from NDC/local coordinates
  whenever `W≠1`), and the test's own header comment (lines 28-31) correctly flags this as the deliberate reason
  for choosing an identity camera — this is exactly the right test-simplification move: it isolates the trig-formula
  correctness question from the (separately true, and separately risky if gotten wrong in a *non-identity* camera
  scenario) semantics of "interpolate pre-divide clip space, not screen space."
- **Check A arithmetic re-verified** (`Time=0`, centre pixel, local `(x,y)=(0,0)`): `displacement.x = sin(0/60+0) *
  sin(0/10) * cos(0/50) = sin(0)*sin(0)*cos(0) = 0*0*1 = 0` (the middle `sin(x/10)` factor being exactly 0 at `x=0`
  is what pins this to 0 regardless of `Time`, since `Time` only appears in the *other* two factors — correctly
  identified in the file's own comment, lines 39-41). `displacement.y = sin(0/50 - 0) = 0`. Both components 0 →
  output `(0.5, 0.5, 0, 1)` → `(128,128,0,255)`. Matches `aOk` check (`close(R,128) && close(G,128) && B==0`, ±6
  tolerance).
- **Check B arithmetic re-verified** (`Time = π/2/2.75`, chosen so `Time*2.75 = π/2` exactly): `displacement.x`
  unaffected (still forced to 0 by the `x=0` term, independent of `Time`). `displacement.y = sin(0 - π/2) =
  sin(-π/2) = -1`. Output `.g = (-1+1)/2 = 0` → `(128, 0, 0, 255)`. Matches `bOk` check. The R channel staying
  fixed at 128 across both checks while G moves from 128→0 is the correct, deliberate discriminator proving the
  `Time` uniform genuinely reaches and drives the shader (specifically the `.y` term) without being confounded by
  the `.x` term, which by construction of the test geometry cannot vary here.

### Logic
`DrawOnce(float time)` (lines 168-196) is a clean, reusable single-draw helper parameterized only by `Time`; `Draw()`
calls it twice and compares. No loops/branches beyond the standard effect-validity guard.

### Memory/resource lifetime
`vb_`/`ib_` constructed once in `Initialize()`, reused (not reconstructed) across both `DrawOnce()` calls — correct,
since the geometry itself (a static quad) doesn't change between Check A and Check B, only the `Time` uniform does.

### C++ correctness
`std::numbers::pi` (C++20, `<numbers>`) used for `timeB` computation (line 212) — modern, correct, avoids a
hand-rolled `M_PI`/magic-constant pi value.

### Robustness
Same `!fx || !fx->IsEffectValid()` guard pattern as every sibling shader test in this batch.

### Testing
Thoroughly covers the trig formula's `Time`-dependence via the G channel, and (implicitly, by construction of the
test geometry) sidesteps rather than tests the raw-clip-space-vs-NDC distinction that would matter under a
non-identity camera — a reasonable, explicitly-acknowledged scope choice (the file's own comment states this
directly) rather than an oversight. Does not test `DistortionScale`'s scaling effect independently of `Time` (both
checks use `DistortionScale=1.0`) or test the `.x`-term's `Time*1.5`/`sin(x/10)`/`cos(x/50)` three-factor product
away from the `x=0` degeneracy point — a real gap, since the interesting three-factor-product part of the formula
is never actually exercised at a nonzero `x`.

## Detailed Findings

No HIGH or CRITICAL findings.

### F1 — The `.x`-displacement's three-factor product formula is never exercised away from its `x=0` degeneracy

- Severity: MEDIUM
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: fragment shader `displacement.x = sin(position.x/60+Time*1.5) * sin(position.x/10) *
  cos(position.x/50);` (line 110-112); both test checks sample only the centre pixel, where local `x=0`.
- Evidence: at `x=0`, the middle factor `sin(x/10)` is identically `0` for *any* `Time`, forcing the entire
  three-factor product to `0` regardless of whether the first (`sin(x/60+Time*1.5)`) or third (`cos(x/50)`) factors
  are computed correctly, in the right order, with the right divisors, or even present at all. A shader that
  dropped the `Time*1.5` term, used the wrong divisor (e.g. `x/50` instead of `x/60`), swapped `sin`/`cos` on the
  third factor, or omitted the third factor's `cos` entirely would *still* produce `displacement.x=0` at the centre
  pixel and pass both of this file's checks — the `.x` formula's correctness beyond "it's some formula that's 0 at
  x=0" is effectively unverified.
- Why it matters: `HeatHaze`'s horizontal-displacement behavior (visually, the left-right "shimmer" component of the
  heat-haze effect) is exactly the part of this shader this test cannot currently distinguish from a broken/
  simplified port, since both of its probe points share the one coordinate (`x=0`) that makes the formula
  degenerate.
- FNA/XNA comparison: N/A (GLSL-side coverage gap, not an API-parity issue — the HLSL source's `.x` formula was
  itself correctly transcribed; the gap is purely in what this *test* exercises).
- Related files: none beyond this file itself.
- Suggested future action (not implemented by this audit): add a third probe point away from screen centre (e.g.
  `(W/4, H/2)`, local `x=-0.25` given this test's quad spans `-0.5..0.5`) and a worked-through expected `.x` value
  at a nonzero `Time`, to actually exercise the three-factor product's divisors/coefficients/sign.

## Cross-File Observations

- Shares the `WorldViewProjection`-as-single-combined-uniform convention with the sibling `DisplacementMapped`/
  `PullIn` tests, correctly limited here to just `WorldViewProjection` (no `WorldView` needed, matching this
  shader's own declared uniform set — `HeatHaze_PixelShader` never touches normals).
- The test-geometry simplification (`World=View=Projection=Identity` making raw-clip-space equal local coordinates)
  is the same technique used across this task's 3D shader ports, consistently and correctly reasoned about in each
  file's own header comment.

## Missing or Weak Tests

- See F1 (the `.x`-displacement three-factor formula's divisors/coefficients are unverified away from its `x=0`
  degeneracy point) — the single most concrete, actionable gap found in this batch of shader-port tests.
- No test of `DistortionScale`'s scaling behavior independently from `1.0` (both checks use `DistortionScale=1.0`,
  so a shader that dropped the `*= DistortionScale` multiply entirely would still pass, since `1.0` is the
  multiplicative identity) — related to, but distinct from, F1; lower priority since it's a one-line scalar
  multiply rather than a three-term divisor/coefficient formula.

## Positive Findings

- The `Time`-dependence discriminator (G channel moving from 128→0 between the two checks while R stays fixed) is a
  genuine, correctly-reasoned proof that the `Time` uniform reaches the shader and drives the `.y` term
  specifically — not a coincidental pass.
- The deliberate choice of `World=View=Projection=Identity` to make the raw-clip-space-as-varying trick tractable is
  a sound test-design decision, explicitly justified in the file's own comment and independently re-verified during
  this audit.

## Final Assessment

An accurate port of `HeatHaze_PixelShader`'s structure and a genuinely-verified `Time`-dependence proof for the `.y`
term, but with one concrete, fixable test-coverage gap (F1): the more complex three-factor `.x`-displacement formula
is only ever exercised at its `x=0` degeneracy point, where it is trivially zero regardless of whether the formula's
divisors, coefficients, or even the presence of two of its three factors are correct.
