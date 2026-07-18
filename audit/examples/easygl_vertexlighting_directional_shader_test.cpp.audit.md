# Audit: examples/easygl_vertexlighting_directional_shader_test.cpp

## Metadata

- Source file: `examples/easygl_vertexlighting_directional_shader_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — HLSL→GLSL shader-conversion proof for `VertexLightingSample`'s **own**
  `VertexLighting.fx` (technique `VertexLighting` = `DiffuseLighting` + `SimplePixelShader`), Task 947 / Phase 78
- File type: C++ example/integration-test executable (`EasyGLVertexLightingDirectionalTest :
  Microsoft::Xna::Framework::Game`, `main()`)
- Related production code: same `.cnj`/`ShaderEffect`/`EasyGLEffectBackend` infrastructure as the two sibling
  lighting-shader files in this batch
- XNA/FNA relevance: ports `VertexLightingSample_4_0/VertexLighting/Content/VertexLighting.fx`'s `DiffuseLighting`
  technique — a **different** upstream file from the same-named `VertexLighting.fx` used by the
  `PerPixelLighting` sample (ported by the two sibling files in this batch); this file's own header comment
  explicitly notes the two are "confirmed via `diff`" to be genuinely different files, not a naming collision.
  Neither sample's source is present in the local FNA reference tree — verification here is limited to internal
  mathematical consistency and GLSL-vs-quoted-HLSL fidelity, same caveat as the two sibling reports.
- Main related tests: siblings `easygl_vertexlighting_diffuse_shader_test.cpp` and
  `easygl_vertexlighting_diffusephong_shader_test.cpp` (both audited in this same batch, both port the *other*
  `VertexLighting.fx`).

## Purpose

Proves the EasyGL GLSL translation of `VertexLightingSample`'s own single-directional-light `DiffuseLighting`
technique, **and** deliberately verifies that a real, pre-existing dead-code bug in the original HLSL (an
alpha-clamp assignment that mutates an already-copied local variable and has no effect on the actual output) is
faithfully preserved rather than silently "fixed" during the port — checked via the rendered alpha channel, which
this file uniquely (among the three lighting-shader files in this batch) asserts on.

## Executive Verdict

**Healthy** — the file's central, unusual claim (a genuine upstream HLSL bug is being faithfully preserved, not
introduced or accidentally fixed) was independently verified in this audit by inspecting the actual GLSL source
(confirmed no `vColor.a = 1.0` clamp exists anywhere in it) and by hand-deriving that the two checks' *distinct*,
non-1.0-summing alpha values (128 vs. 51) are only observable precisely because the clamp is absent — this is
genuinely careful, XNA/FNA-behavior-fidelity-first engineering exactly matching the project's own `CLAUDE.md`
principle ("Match XNA/FNA behavior over personal C++ preference").

## Checklist Results

### API / XNA / FNA parity
Same `ShaderEffect`/`.cnj` NOXNA infrastructure as the two sibling files, used identically and correctly.

### Behavioral correctness
Independently re-derived both checks by hand:
- **Check A** (`World=Identity`): `worldNormal=(0,0,1)`, `lightDirection=(0,0,-1)` so `-lightDirection=(0,0,1)`;
  `dot((0,0,1),(0,0,1))=1`, un-clamped (already in `[0,1]`) so `diffuseIntensity=1`. `diffuseColor =
  lightColor×1 = (0.5,0.4,0.3,0.3)`. `color = diffuseColor + ambientColor(0.1,0.05,0.02,0.2) =
  (0.6,0.45,0.32,0.5)`; `×255 = (153,114.75,81.6,127.5)`, rounding to `(153,115,82,128)` — matches the file's claimed
  expected value exactly (well within its own `±6` tolerance regardless of rounding direction).
- **Check B** (`World=RotationY(180°)`): `RotationY(π)` flips the normal to `(0,0,-1)`;
  `dot((0,0,1),(0,0,-1))=-1`, clamped to `0` — `diffuseIntensity=0` so `diffuseColor=(0,0,0,0)` (the whole `vec4`
  scales by the scalar `diffuseIntensity`, **including alpha**, which is the mechanism that makes this check
  meaningful — see below). `color = ambientColor only = (0.1,0.05,0.02,0.2)×255 = (25.5,12.75,5.1,51)`, rounding to
  `(26,13,5,51)` — matches the file's claim exactly.
- **The alpha-bug-preservation claim**: confirmed by direct inspection of `kVertSrc` (lines 89-112) that the GLSL
  never contains any `vColor.a = 1.0` (or equivalent) statement — `vColor = diffuseColor + ambientColor;` is the
  final assignment to the varying, with an explanatory comment (lines 108-111) stating this is deliberate. Cross-
  checked the file's own quoted HLSL excerpt (lines 9-19): `output.Color = diffuseColor + ambientColor;` is assigned
  first, and only *afterward* does `diffuseColor.a = 1.0;` execute — mutating the **local** `diffuseColor` variable,
  which (in HLSL, a value-type struct) was already copied into `output.Color` on the preceding line and is never
  read again. This is a real, classic "assign then mutate the already-copied source" bug pattern, and the port
  correctly omits any equivalent clamp, faithfully reproducing the *actual* (buggy) rendered behavior rather than
  the *documented-but-ineffective* intent. The two checks' alpha values (128 for Check A, 51 for Check B) are
  distinct specifically *because* the values are unclamped — had the port "fixed" the bug by actually clamping
  alpha to 1.0, both checks would read alpha=255, and the test's own `close(a.getAProperty(),128)` /
  `close(b.getAProperty(),51)` assertions (lines 221-224) would immediately fail, meaning this test is a genuine,
  self-verifying regression guard against someone "helpfully" fixing this preserved bug in the future.

### Logic
`diffuseIntensity` correctly scales the **entire** `vec4 diffuseColor` including its alpha channel
(`lightColor * diffuseIntensity`, where `lightColor` is itself a `vec4` with alpha=0.3) — this is a faithful GLSL
translation of the HLSL's `float4 diffuseColor = lightColor * diffuseIntensity;` (a `float4 * float` operation,
which in HLSL broadcasts the scalar across all four components, including alpha) — confirmed this component-wise
broadcast is what makes Check B's alpha collapse to exactly `0` (not just RGB) when `diffuseIntensity` clamps to 0.

### C++ correctness
Same `dynamic_cast<ShaderEffect*>` pattern as both sibling files (checked once in `Draw()`, unchecked inside
`DrawOnce()`) — same LOW/defense-in-depth-only observation as noted in the diffuse sibling's report, not repeated
as a separate finding.

### Testing
This file's alpha-channel assertions are the load-bearing proof of its central "preserved bug, not fixed" claim —
see Positive Findings.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

## Cross-File Observations

- This file's header comment explicitly and correctly distinguishes its own `VertexLighting.fx` (from
  `VertexLightingSample`, a single directional light, `lightDirection`/`lightColor`) from the *different*,
  same-named `VertexLighting.fx` ported by the two sibling files in this batch (from `PerPixelLighting`, a point
  light with `lightPosition`/`diffuseLightColor`) — states this was "confirmed via `diff`," i.e. an actual
  file-content comparison was performed by whoever authored this test, not an assumption based on the filename
  alone. This is exactly the kind of cross-file diligence this audit values.
- Unlike its two siblings, this file is the only one of the three lighting-shader tests in this batch that asserts
  on the alpha channel — a deliberate, well-justified exception (see Behavioral correctness above), not an
  inconsistency.
- Shares the same `ShaderEffect::OnApply()`-binds-before-custom-uniforms ordering-safety property discussed in the
  diffuse sibling's report.

## Missing or Weak Tests

- Same symmetric-quad, two-`World`-matrix-only scope as both siblings — a consistent, reasonable limitation across
  all three files in this family, not specific to this one.
- No test exists (in this file or elsewhere found in this batch) that would catch a *regression* attempt to "fix"
  this preserved bug in a way that keeps RGB correct but only partially changes alpha (e.g. clamping alpha to some
  value other than 1.0) — the current assertions (`close(..., 128)`/`close(..., 51)`, tolerance ±6) would catch a
  full clamp-to-255 "fix" but a partial, coincidentally-in-tolerance change could theoretically slip through. This
  is a low-probability, low-severity gap given how specific such a regression would have to be.

## Positive Findings

- The central design of this file — proving a real upstream HLSL bug is faithfully preserved via an alpha-channel
  assertion specifically chosen to be sensitive to that bug — is genuinely sophisticated test engineering, directly
  aligned with the project's own stated `CLAUDE.md` principle to match XNA/FNA behavior "over personal C++
  preference" and to preserve original behavior "including the original author's own bugs."
  This audit independently confirmed both (a) that the described bug is real in the quoted HLSL (a classic
  assign-then-mutate-local-copy pattern) and (b) that the GLSL port correctly omits any equivalent effect, by
  direct source inspection — not merely accepting the file's own comment at face value.
- The `diff`-confirmed distinction between two same-named-but-different upstream shader files is a real, verifiable
  diligence step, not an assumption.

## Final Assessment

A rigorously self-verifying test whose unusual central claim — a faithfully-preserved upstream bug, provable via
alpha-channel assertions — was independently confirmed correct in this audit at both the mathematical and the
source-code level. No substantive defects found.
