# Audit: examples/easygl_vertexlighting_diffuse_shader_test.cpp

## Metadata

- Source file: `examples/easygl_vertexlighting_diffuse_shader_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — HLSL→GLSL shader-conversion proof for `PerPixelLighting` sample's
  `VertexLighting.fx`, technique `PerVertexDiffuse` (Task 947 / Phase 78)
- File type: C++ example/integration-test executable (`EasyGLVertexLightingDiffuseTest :
  Microsoft::Xna::Framework::Game`, `main()`), embedding hand-written GLSL vertex/fragment shader source as string
  literals and loading them through the `.cnj` content pipeline
- Related production code: `ContentManager::Load<std::shared_ptr<Effect>>` (`.cnj` "Effect" type loader),
  `ShaderEffect::OnApply()`/`Bind()` (`ShaderEffect.cpp:95-100`), `EasyGLEffectBackend::SetUniformVec3/Vec4`
  (`EasyGLGraphicsBackend.cpp:297-321`, backed by classic `glUniform3f`/`glUniform4f` in `easy-gl`'s
  `Program::set_uniform`, confirmed via `/rv/data/development/github.com/openeggbert/easy-gl/src/Program.cpp:279-336`)
- XNA/FNA relevance: ports a real XNA 4.0 sample shader technique (`PerPixelLightingSample_4_0`'s
  `VertexLighting.fx`, `PerVertexDiffuse` = `VertexDiffuse` vertex shader + `SimplePixelShader`). The sample source
  itself is not present in the local FNA reference tree (`/rv/data/library/github.com/FNA-XNA/FNA/src` contains only
  the FNA engine, not the XNA sample content) — the quoted HLSL in this file's header comment could not be
  independently verified against a canonical local copy in this audit; only its internal mathematical consistency
  and its GLSL translation's fidelity to *that quoted* HLSL were checked.
- Main related tests: sibling `easygl_vertexlighting_diffusephong_shader_test.cpp` and
  `easygl_vertexlighting_directional_shader_test.cpp` (both audited in this same batch); referenced but
  out-of-batch siblings `easygl_perpixellighting_shader_test.cpp` /
  `easygl_perpixellighting_vertexdiffuse_pixelphong_shader_test.cpp`.

## Purpose

Proves the EasyGL backend's GLSL translation of `VertexLighting.fx`'s `PerVertexDiffuse` technique (all lighting
computed in the vertex shader, interpolated as a plain `COLOR` varying, pixel shader is a pure passthrough) produces
the mathematically correct per-pixel color for two `World` matrices (Identity, RotationY(180°)) applied to a quad
centered at the origin under a point light at `(0,0,5)`.

## Executive Verdict

**Healthy** — every numeric claim in this file's header comment (diffuse intensity, resulting color, both Check A
and Check B) was independently re-derived by hand in this audit from first principles (dot products, vector
normalization) and matches exactly; the GLSL shader is a faithful line-for-line translation of the quoted HLSL; and
the two-`World`-matrix design genuinely proves the shader consumes `World` (not just a hardcoded color), not merely
that *something* renders.

## Checklist Results

### API / XNA / FNA parity
Uses `Effect`/`ShaderEffect` (`NOXNA`, per `ShaderEffect.hpp:25`) loaded via the real `.cnj` content-pipeline
convention (`"type": "Effect"`, `"vertex"`, `"fragment"` keys) — confirmed a real, established loader path (not
invented by this file) by cross-referencing `ContentManager.hpp`'s `.cnj` envelope documentation.
`IEffectMatrices`-style `setWorldProperty`/`setViewProperty`/`setProjectionProperty` and the NOXNA
`SetUniformVec3`/`SetUniformVec4` custom-uniform setters are all real, correctly-`NOXNA`-marked `ShaderEffect`
members (`ShaderEffect.hpp:43-58`).

### Behavioral correctness
Independently re-derived both checks by hand:
- **Check A** (`World=Identity`): for corner `(0.5,0.5,0)`, `directionToLight = normalize((0,0,5)-(0.5,0.5,0)) =
  (-0.5,-0.5,5)/√25.5`; `dot` with `worldNormal=(0,0,1)` gives `5/√25.5 ≈ 0.99015`, matching the file's claimed
  `diffuseIntensity=0.99015` exactly. `diffuse = (0.4,0.3,0.2)×0.99015 = (0.396,0.297,0.198)`; `+ambient
  (0.1,0.05,0.02) = (0.496,0.347,0.218)`; `×255 ≈ (127,89,56)` — matches the file's claimed expected value exactly.
  By the quad's symmetry (all four corners equidistant from the light along Z), every corner computes this same
  value, so the interpolated center-pixel color equals it exactly, as claimed.
- **Check B** (`World=RotationY(180°)`): `RotationY(π)` maps `(x,y,z)→(-x,y,-z)`; the normal `(0,0,1)` maps to
  `(0,0,-1)`. Recomputing `directionToLight` for the rotated corner position and re-dotting with the flipped normal
  gives `dot = -0.99015`, clamped to `0` by `clamp(...,0.0,1.0)` — confirmed the diffuse term vanishes exactly as
  claimed, leaving `color = ambient only = (0.1,0.05,0.02)×255 ≈ (26,13,5)`, matching the file's claim exactly.
- These two results are meaningfully distinct (not just "renders something"), which specifically proves the `World`
  matrix genuinely reaches the vertex shader's lighting computation rather than the test coincidentally passing
  under a no-op transform.

### Logic
The embedded GLSL vertex shader (`kVertSrc`, lines 78-100) is a faithful translation of the quoted HLSL: `worldPos4
= World*vec4(aPosition,1.0)`, homogeneous divide `worldPosition = worldPos4.xyz/worldPos4.w` (matching the HLSL's
explicit `worldPosition = worldPosition / worldPosition.w;`, itself redundant for an affine `World` matrix but
faithfully preserved rather than "optimized away"), `worldNormal = mat3(World)*aNormal`, `clamp(dot(...),0.0,1.0)`
correctly implementing HLSL's `saturate()`, and `vColor.a = 1.0` matching `output.Color.a = 1.0;` in the quoted
source (a real, effective assignment here — unlike the *different*, buggy `VertexLighting.fx` file audited in the
sibling `easygl_vertexlighting_directional_shader_test.cpp` report, where the equivalent line mutates an
already-copied local variable and has no effect; this file's own quoted HLSL assigns `output.Color.a` directly, so
no such bug applies here).

### Memory/resource lifetime
Shader source/`.cnj` files are written to a per-instance temp directory keyed by `reinterpret_cast<std::uintptr_t>
(this)` (lines 127-129) — avoids collisions between concurrently-run test binaries, a sound and commonly-used
pattern across this shard's `.cnj`-loading tests. `vb_`/`ib_` (`std::unique_ptr`) are constructed once in
`Initialize()` and reused across both `DrawOnce()` calls — correct, no re-upload needed since geometry doesn't
change between checks.

### C++ correctness
`dynamic_cast<ShaderEffect*>(fxBase_.get())` (line 171, 197) is checked for null (`if (!fx || !fx->IsEffectValid())`,
line 198) before use in `Draw()`, but **not** re-checked inside `DrawOnce()` (line 171: `fx->setWorldProperty(...)`
dereferences the raw pointer unconditionally). This is safe in practice only because `Draw()` already validated
`fx` and `IsEffectValid()` before ever calling `DrawOnce()` — traced the call order and confirmed no path reaches
`DrawOnce()` with a null/invalid `fx`.

### Thread safety
N/A — single-threaded game-loop test, no concurrent access.

### Testing
This file is itself a test; see Positive Findings for its verified rigor.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings — every claim in this file was checked and found correct. One LOW/INFO
observation:

### F1 — `dynamic_cast` result unchecked for null within `DrawOnce()` itself

- Severity: LOW
- Confidence: HIGH
- Category: robustness (defense-in-depth only — not a live bug given the traced call order)
- Location/symbol: `DrawOnce(const Matrix&)`, line 171 (`auto* fx = dynamic_cast<ShaderEffect*>(fxBase_.get());`)
  used unconditionally afterward
- Evidence: `DrawOnce` performs its own `dynamic_cast` and uses the result without a null check, relying on the
  caller (`Draw()`) having already validated it earlier in the same object's lifetime.
- Why it matters: purely a local-robustness/defense-in-depth observation — if `DrawOnce` were ever called from a
  different path in a future edit (or reordered), the missing check would surface as a null-pointer dereference
  rather than a clean failure message. Not a live defect in the code as it exists today.
- FNA/XNA comparison: N/A.
- Suggested future action: none required; noted for completeness only.

## Cross-File Observations

- `ShaderEffect::OnApply()` (confirmed via `ShaderEffect.cpp:95-100`) calls `effectBackend_->Bind()` (i.e.
  `glUseProgram`) synchronously and immediately when `fx->Apply()` is called (line 177) — **before** this file's
  subsequent `SetUniformVec3`/`SetUniformVec4` calls (lines 178-180). This ordering is load-bearing: `easy-gl`'s
  `Program::set_uniform` uses classic `glUniform3f`/`glUniform4f` (confirmed via
  `easy-gl/src/Program.cpp:279-336`), which operate on whichever program is *currently bound*, not
  necessarily this effect's own program, unless `Bind()` has already run. Traced this specific ordering concern in
  this audit and confirmed it is safe as written (`Apply()` binds first, custom uniforms are set second, draw call
  third) — recorded here as a verified-safe pattern worth the `ShaderEffect`/EasyGL-backend audits reconfirming for
  any future file that might call `SetUniformVec3` etc. *before* `Apply()`.
- Referenced sibling `easygl_perpixellighting_vertexdiffuse_pixelphong_shader_test.cpp` (not in this batch) is
  mentioned by the diffusephong sibling file, not this one — no direct dependency from this file.

## Missing or Weak Tests

- Only two `World` matrices are tested (Identity, RotationY 180°); no test of `View`/`Projection` variation, nor of
  a light position that isn't symmetric relative to the quad (which would make different corners contribute
  different diffuse values before interpolation, a stronger test of the *per-vertex* — as opposed to per-pixel —
  nature of this technique). The current symmetric setup cannot distinguish "correct per-vertex lighting" from "the
  vertex shader coincidentally applies a uniform value to all 4 corners," though the deliberate choice of a
  point light (not directional) does at least prove *some* position-dependent computation occurs (a pure
  directional light would trivially cancel any position sensitivity).

## Positive Findings

- Every numeric expectation in this file's header comment was independently re-derived by hand in this audit
  (not just re-stated) and found correct to the precision claimed — genuinely rigorous, verifiable test design.
- Correctly identifies and preserves the *effective* (non-buggy) `output.Color.a = 1.0` assignment pattern from its
  own source file, in contrast to the different, genuinely-buggy dead-code pattern in the sibling
  `VertexLightingSample`'s own `VertexLighting.fx` (see the `easygl_vertexlighting_directional_shader_test.cpp`
  report) — the two sibling files correctly distinguish between two same-named but behaviorally different upstream
  shader files rather than conflating them.
- The two-`World`-matrix, symmetric-quad design is a genuinely well-thought-out way to prove `World` reaches the
  vertex shader while keeping the by-hand math tractable.

## Final Assessment

A rigorously-verified, mathematically correct shader-conversion proof test with no substantive defects found;
its only gap is a reasonable, disclosed scope limitation (symmetric geometry can't fully distinguish per-vertex
from per-pixel evaluation), not an error.
