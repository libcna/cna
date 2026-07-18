# Audit: examples/easygl_shattereffect_shader_test.cpp

## Metadata

- Source file: `examples/easygl_shattereffect_shader_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — Task 947 (Phase 78): HLSL→GLSL conversion proof for
  `ShatterEffectSample_4_0`'s `ShatterEffect.fx` (per-triangle rotation-and-fall shatter animation + Phong
  lighting), using Task 1080's custom-vertex-layout capability (stride 56, matching none of the 5 built-in
  strides)
- File type: raw `Game`-derived executable, two checks (A/B), with 2 documented mutation-testing passes including
  one that describes a genuine test "blind spot" the author found and worked around
- XNA/FNA relevance: direct 1:1 HLSL→GLSL port, including two intentionally-preserved HLSL quirks (unrenormalized
  `WorldNormal`, a locally-constructed rotation matrix built from raw per-vertex Euler-angle attributes rather
  than an uploaded uniform).
- FNA reference: `ShatterEffect.fx`'s `ShatterVS`/`PhongPS`, quoted in the header comment — `ShatterEffectSample_4_0`
  is **not present** in the local FNA reference tree (community sample, not core FNA runtime), so this audit
  could not independently diff the quoted HLSL against a local original; instead, the *ported GLSL itself* was
  independently re-derived from first principles for both checks (see Logic) and found self-consistent and
  arithmetically exact.
- Related production files: `VertexDeclaration.hpp`/`.cpp`, `VertexElement.hpp`, `EasyGLGraphicsBackend.cpp`
  (`ApplyLayout()`'s generic-declaration branch, shared with `easygl_shadereffect_custom_vertex_layout_test.cpp`).

## Purpose

Ports `ShatterEffect.fx`'s vertex shader (per-triangle rotation about a `TriangleCenter`, built from a
locally-constructed yaw-pitch-roll matrix driven by per-vertex `RotationalVelocity` × a `RotationAmount` uniform,
plus Gouraud-style diffuse lighting) and its Phong pixel shader (specular via `reflect()`), using a 56-byte custom
vertex layout (Position/Normal/TexCoords/TriangleCenter/RotationalVelocity) bound through Task 1080's generic
`VertexDeclaration` path.

## Executive Verdict

**Healthy.** This audit independently re-derived both checks' expected byte values from the raw GLSL source and
uniform values (not from the file's own worked derivation) and obtained `(218,128,83,255)` and `(20,10,5,255)`
exactly, matching the file's own assertions. The file's own documented "blind spot" discovery (certain rotation-
matrix element swaps were invisible to this test's own checks) is a genuinely valuable, honestly-reported test
limitation, not a hidden gap.

## Checklist Results

### API / XNA / FNA parity
`ShatterVertex` (56 bytes: 3×`Vector3` + `Vector2` + 3-float×2 more `Vector3`s) is bound via `VertexDeclaration(56,
{...})` with `VertexElementUsage::TextureCoordinate` reused at usage indices 0/1/2 for TexCoords/TriangleCenter/
RotationalVelocity (lines 320-326) — since `TriangleCenter`/`RotationalVelocity` have no dedicated
`VertexElementUsage` value in XNA's own enum, and CNA's generic `ApplyLayout()` binds purely by the element's
index within the declaration list (not by usage), this is a legitimate, functionally-correct labeling choice, not
a semantic error — confirmed against the same `ApplyLayout()`/`DescribeVertexElementFormat()` code path already
verified in the sibling `easygl_shadereffect_custom_vertex_layout_test.cpp` report.

### Behavioral correctness
Two HLSL quirks explicitly preserved rather than "fixed," both plausible and consistent with the quoted FNA
source: (1) `WorldNormal` is never re-normalized after the rotation+`World` transform in either the diffuse (VS)
or specular (PS) `dot()`/`reflect()` calls; (2) `CreateYawPitchRollMatrix` is built per-vertex from raw attribute
data (`rotateByYawPitchRoll()`, lines 218-232) rather than an uploaded uniform matrix, ported as an explicit
weighted-sum GLSL function rather than a `mat3` construction, to avoid row/column-major ambiguity — a reasonable,
explicitly-justified implementation choice for a matrix that never leaves the shader.

### Logic
**Independently re-derived from the raw GLSL (not the file's own summarized derivation), for both checks:**
- `rotateByYawPitchRoll` at `x=0,z=0,y=π/2`: `cos(y)=0,sin(y)=1`, all `x`/`z` trig terms collapse to `0`/`1`
  exactly (no irrational-approximation risk). Working through all 9 element formulas (lines 219-227) gives
  `m00=0,m01=0,m02=1, m10=0,m11=1,m12=0, m20=-1,m21=0,m22=0` — matches the file's own claimed values exactly.
  Applying the function's own row-vector weighted sum: `rotate(v) = (-v.z, v.y, v.x)` — confirmed
  `rotate((0,0,1))=(-1,0,0)`.
- Check A (`RotationAmount=0`): `rotVel=(0,0,0)` → `rotateByYawPitchRoll` at all-zero angles is the identity
  matrix (all `cos`→1, `sin`→0 substituted into the same 9 formulas) → `WorldNormal=(0,0,1)` unchanged (`World`
  =Identity). `TriangleCenter==Position` for every vertex (test setup, lines 330-335), so the rotation's *position*
  contribution is exactly zero regardless of angle, and the quad's screen footprint/`vWorldPosition≈(0,0,0)` at
  the sampled centre pixel is unaffected by rotation. With `lightPosition=(0,0,5000)`: `directionToLight≈(0,0,1)`,
  `diffuseIntensity=dot((0,0,1),(0,0,1))=1` → `vColor = diffuseColor×1 + ambientColor = (0.9,0.9,0.9,1.0)`.
  Specular (evaluated per-pixel from smoothly-interpolated `vWorldPosition`, no Gouraud-averaging risk):
  `reflect((0,0,-1),(0,0,1)) = (0,0,-1) - 2×(-1)×(0,0,1) = (0,0,1)`; `directionToCamera≈(0,0,1)`; `dot=1`;
  `pow(1, 8.0)=1` (exact, no precision risk at this edge value); `specular=(0.15,0.15,0.15,0.15)`. Final:
  `color = (0.7843,0.3922,0.1961,1.0)×(0.9,0.9,0.9,1.0) + (0.15,0.15,0.15,0.15) = (0.8559,0.5030,0.3265,1.15)`,
  `color.a` forced to `1.0` → byte **(218, 128, 83, 255)** — matches the file's assertion exactly, independently
  reproduced digit-for-digit.
- Check B (`RotationAmount=1`): `rotVel=(0,π/2,0)` reaches `rotateByYawPitchRoll` unchanged → `WorldNormal =
  rotate((0,0,1)) = (-1,0,0)`. `diffuseIntensity = clamp(dot((0,0,1),(-1,0,0)),0,1) = 0` → `vColor = ambientColor
  = (0.1,0.1,0.1,0)`. Specular: `dot(WorldNormal, -directionToLight) = dot((-1,0,0),(0,0,-1)) = 0` →
  `reflect = I - 0 = (0,0,-1)` unchanged; `dot(reflectionVector, directionToCamera) = dot((0,0,-1),(0,0,1)) = -1`,
  clamped to `0`; `pow(0, 8.0) = 0` (exact) → `specular=(0,0,0,0)`. Final: `color = (0.7843,0.3922,0.1961,1)×
  (0.1,0.1,0.1,0) = (0.07843,0.03922,0.01961,0)`, `color.a` forced to `1.0` → byte **(20, 10, 5, 255)** — matches
  the file's assertion exactly, independently reproduced.
Both re-derivations match the file's own expected values exactly; the dramatic R/G/B swing between checks is
genuine evidence `CreateYawPitchRollMatrix`/rotation reaches and changes `WorldNormal` (and both lighting terms),
not a fluke at the identity case.

### Memory/resource lifetime
`vb_`/`ib_` are `std::unique_ptr`s with clean RAII lifetime; `diffuseTex_` follows the same
default-construct-then-`CreateFromPixels()`-assign pattern as every sibling file. Temp directory never cleaned up
— see F1.

### C++ correctness
`ShatterVertex` (lines 187-196) is `#pragma pack(push,1)`-packed with `static_assert(sizeof(...) == 56)`, matching
its `VertexDeclaration`'s offsets — same correctly-applied safety idiom as the sibling custom-vertex-layout test.

### Performance
N/A — single-shot test.

### Robustness
`RasterizerState::CullNone` + disabled depth test, consistent with every other 3D test in this batch.

### Testing
**A genuinely valuable, honestly-reported test limitation, independently assessed as accurate**: the file's own
comment states that 2 earlier mutation attempts (swapping `m01`/`m02`, then `m20`/`m21`) were *invisible* to this
test's checks, because with light and camera both aligned along world Z, any `WorldNormal` confined to the XY
plane (zero Z-component) gives identical — zero — diffuse *and* specular contributions regardless of its exact
X/Y direction. Verified this reasoning: at Check B's `WorldNormal=(-1,0,0)` (already XY-plane-confined), and Check
A's `WorldNormal=(0,0,1)` (pure Z), a mutation confined to the X/Y *output* components of `rotateByYawPitchRoll`
cannot change either check's Z-component-driven outcome — genuinely a real, structural blind spot of this
specific two-check design, not a false claim. The mutation that *does* corrupt the Z-output component (`m20`
substituted for `m22`) was correctly shown to break Check A specifically (since `m22=1,m20=0` at the identity
case), which the file notes and reasons about correctly.

### Cross-file consistency
Reuses the same `VertexDeclaration`/`ApplyLayout()` generic-binding mechanism as
`easygl_shadereffect_custom_vertex_layout_test.cpp` (both stride ≠ any of the 5 built-in cases) — a shared defect
in that mechanism would affect both files identically; this audit traced the mechanism once and confirmed it
applies consistently to both.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — Temp directory (3 files) written per test run, never cleaned up

- Severity: LOW
- Confidence: HIGH
- Category: resource-hygiene
- Location/symbol: `Initialize()`, lines 304-316
- Evidence: no cleanup call in the file.
- Why it matters: same shared low-priority gap as the rest of this batch.
- Suggested future action (not implemented by this audit): clean up on success or failure.

### F2 — Documented test blind spot: X/Y-confined `WorldNormal` mutations are undetectable by this file's own checks

- Severity: LOW
- Confidence: HIGH (the file's own comment states this, and this audit independently confirmed the geometric
  reasoning is correct)
- Category: test-coverage
- Location/symbol: `rotateByYawPitchRoll()` (lines 218-232), Check A/B design (lines 105-124 of the header
  comment)
- Evidence: with `lightPosition`/`eyePosition` both aligned along world Z and both checks' `WorldNormal` values
  lying entirely in a plane containing Z=0 or pure-Z, any rotation-matrix corruption confined to the X/Y output
  components of `rotateByYawPitchRoll` produces identical (zero) diffuse and specular results for both checks,
  regardless of the corruption's exact nature.
- Why it matters: a real regression in the X/Y-component formulas of a locally-reconstructed rotation matrix
  (e.g. `m00`/`m01`/`m10`/`m11`/`m21` swapped or corrupted in a way that doesn't touch the Z row/column) could
  ship undetected by this specific test, even though the test's own header comment is explicit that this was
  discovered and is a known limitation of the current 2-check design, not an unknown risk.
- Suggested future action (not implemented by this audit): a third check with `lightPosition`/`eyePosition` off
  the Z-axis (e.g. along X or Y) would give X/Y-confined normal corruptions a non-zero lighting signature and close
  this gap.

## Cross-File Observations

- Shares the `VertexDeclaration`/generic `ApplyLayout()` binding path with
  `easygl_shadereffect_custom_vertex_layout_test.cpp` — both independently confirmed correct against the same
  production code in this audit pass.
- The Gouraud-vs-per-pixel diffuse evaluation subtlety documented in this file's header (moving `lightPosition`
  from `(0,0,5)` to `(0,0,5000)` to make the per-vertex-then-interpolated diffuse term numerically indistinguishable
  from a per-pixel evaluation) is a genuinely non-obvious, correctly-diagnosed numerical-precision issue — this
  audit's independent re-derivation (see Logic) used the same "light at effective infinity" simplification and
  obtained exact matches, corroborating that the fix was necessary and sufficient.

## Missing or Weak Tests

- See F2 — no check in this file (or apparently elsewhere in this batch) exercises a rotation with
  light/eye positions off the Z-axis, leaving X/Y-confined rotation-matrix corruptions undetectable by this
  specific test.
- FNA sample source (`ShatterEffect.fx`) could not be located locally to independently diff the quoted HLSL —
  disclosed as an audit-scope limitation.

## Positive Findings

- Both checks were independently re-derived by this audit from the raw GLSL source and match the file's own
  assertions exactly, including a non-trivial Phong specular term and a hand-rolled per-vertex rotation matrix.
- The file's own documentation of a genuine test blind spot (discovered via real mutation testing, not
  hypothesized) is exemplary test-authoring transparency, matching the same high bar set by the sibling
  texture-cube test's decoy-texture discovery.
- `TriangleCenter == Position` as a deliberate test simplification (isolating the rotation's effect on
  `WorldNormal` without needing to hand-derive a rotated vertex position) is a well-reasoned way to reduce
  incidental complexity while still proving the rotation mechanism itself.

## Final Assessment

A rigorous, independently-verified port of `ShatterEffect.fx`'s vertex/pixel shaders, including a non-trivial
locally-constructed rotation matrix and Phong specular term — both checks were reproduced exactly from first
principles by this audit. The one substantive finding (F2) is a real, already-disclosed test-coverage gap, not a
hidden defect; F1 is the shared low-priority temp-file hygiene note common to this batch.
