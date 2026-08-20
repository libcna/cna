# Audit: examples/webgpu_alphatest3d_test.cpp

## Metadata

- Source file: `examples/webgpu_alphatest3d_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-webgpu` shard — `AlphaTestEffect` alpha-discard pixel test, WebGPU backend
  (experimental, per `CLAUDE.md`).
- Test executable: `cna_test_webgpu_alphatest3d`, CTest target `WebGPU_AlphaTest3D`
  (`cmake/Tests/WebGpuTests.cmake:75-76`).
- XNA/FNA relevance: direct — `AlphaTestEffect.AlphaFunction`/`ReferenceAlpha`/`VertexColorEnabled`.
- FNA reference: `AlphaTestEffect.cs` (`AlphaFunction`/`ReferenceAlpha` semantics); this project's own
  `AlphaTestEffect.cpp::FillGpuDrawParams()` (lines 313-380) is the shared, backend-agnostic source of
  truth for the alpha-test-as-inequality encoding this file's shader consumes.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/AlphaTestEffect.cpp` (`FillGpuDrawParams`),
  `src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp`
  (`CreateAlphaTestResources()` lines 3299-3417, `GetOrCreatePipelineAlphaTest3D()` lines 3419-3543,
  `DrawPrimitivesEx()`/`DrawIndexedPrimitivesEx()` dispatch lines 6006-6164, `FillAlphaTestUniforms()`
  lines 514-523).

## Purpose

Four-check pixel test proving `AlphaTestEffect`'s per-pixel alpha discard genuinely runs on this
backend: (A) a passing `Greater` test shows the texture's own colour; (B) a failing `Greater` test
leaves the background clear colour visible (proving `discard` actually executes, not "always draw");
(C) the stride-24 (`VertexPositionColorTexture`) combined vertex-colour + alpha-test shader variant;
(D) the `DrawIndexedPrimitives` counterpart of a discarding case. Correctly placed under
`examples-tests-webgpu` alongside the WebGPU-specific `alpha_test3d.wgsl`/`alpha_test_colored3d`
shader pair it exercises.

## Executive Verdict

**Healthy.** Independently re-derived the alpha-test-as-inequality math for both checks A and B against
the actual current `AlphaTestEffect::FillGpuDrawParams()` and the WGSL shader's `discard` logic; both
values are correct, and the dispatch chain (stride/effect-flag routing in `DrawPrimitivesEx`/
`DrawIndexedPrimitivesEx`, `GetOrCreatePipelineAlphaTest3D`'s stride-20/24 shader-module selection) is
internally consistent with the shared `AlphaTestEffect.cpp` production code.

## Checklist Results

### API / XNA / FNA parity
`setAlphaFunctionProperty(CompareFunction::Greater)`/`setReferenceAlphaProperty(128)`/
`setVertexColorEnabledProperty(true)` (lines 167-168, 201-203) map directly to FNA's `AlphaTestEffect`
surface. `PosTexDecl()`/`PosColorTexDecl()` (lines 77-107) hand-declare `VertexDeclaration`s matching
`VertexPositionTexture`/`VertexPositionColorTexture`'s real FNA byte layouts (offsets 0/12/16, stride
20/24) rather than relying on the typed struct's own static declaration — a defensible choice here
since the point is exercising the *generic* `VertexDeclaration`-driven vertex-buffer path, not the
typed-vertex convenience path.

### Behavioral correctness
Re-derived `AlphaTestEffect::FillGpuDrawParams()`'s (lines 339-379) `Greater` branch by hand:
`reference=128/255=0.501961`, `kThresh=0.5/255=0.0019608`, so `alphaTest.x = reference+kThresh
= 0.503922`, `alphaTest.z=-1` (weight when `lessTest` true), `alphaTest.w=1` (weight when
`lessTest` false). The WGSL fragment (`CreateAlphaTestResources()` lines 3336-3348) computes
`lessTest = alpha < alphaTest.x`, `w = select(alphaTest.w, alphaTest.z, lessTest)`, discards when
`w<0`.
- Check A: texture alpha `200/255=0.7843` is **not** `< 0.503922` → `lessTest=false` → `w=alphaTest.w=1`
  → kept. Matches the test's own expectation (`Color::Lime`, i.e. the texture's real green shows).
- Check B: texture alpha `50/255=0.19608` **is** `< 0.503922` → `lessTest=true` → `w=alphaTest.z=-1`
  → discarded, background (`Color::Red`) stays visible. Matches the test's expectation exactly.
This independently confirms the test is asserting the *actual current* formula's output, not a
coincidentally-close stale constant (contrast with the known `easygl_basiceffect_specular_test.cpp`
F1 pattern this audit has seen elsewhere in this codebase).

### Logic
Check C's stride-24 shader (`coloredShaderSource`, lines 3364-3406) mixes `input.color * u.diffuseColor`
before sampling, gated on `u.extra.x` (`vertexColorEnabled`), matching `FillAlphaTestUniforms()`'s
`out[24] = p.vertexColorEnabled` placement (float index 24 = the `extra` vec4's first component, since
the uniform struct is `mvp(16)+diffuseColor(4)+alphaTest(4)+extra(4)`). Check C's blue vertex colour
over a white (`whiteAlpha200_`) texture, alpha=200 passing the same `Greater(128)` test as check A,
correctly renders blue.

### C++ correctness
`colorNear()`'s tolerance of 16 (line 62) is generous but proportionate to 8-bit colour readback noise;
no correctness concern found. `MakePosTexQuad`/`MakePosColorTexQuad` (lines 85-98, 109-122) both use
the standard two-triangle CCW quad winding consistent with `RasterizerState::CullNone` being set
(line 158) before any draw — culling is correctly neutralized so winding order cannot cause a spurious
discard-via-culling false negative.

### Robustness
Check D specifically isolates the indexed-draw path (`DrawIndexedPrimitives`) from the non-indexed
path already covered by A/B/C — this is the correct technique to catch a bug where only one of
`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`'s alpha-test branch was wired up (both were independently
verified present at lines 6025 and 6110 of the production file).

### Testing
Good coverage of the two shader variants (stride 20/24) and both draw call shapes (indexed/non-indexed),
for the `Greater` `CompareFunction` specifically. Not covered by this file (acceptable — no claim is
made otherwise): the other seven `CompareFunction` values (`Less`, `LessEqual`, `GreaterEqual`, `Equal`,
`NotEqual`, `Never`, `Always`), the `Equal`/`NotEqual` tolerance-band branch (`u.alphaTest.y > 0.0`,
never exercised by any of this file's four checks since only `Greater` is used), and the stride-32
(`VertexPositionNormalTexture`) alpha-test shader variant that `GetOrCreatePipelineAlphaTest3D()`
explicitly supports (lines 3456-3470) via a shared un-coloured shader module. These are gaps in
*this specific file's* scope, not proof of a production defect — the tolerance-band code path in
particular (`toleranceTest = abs(alpha - u.alphaTest.x) < u.alphaTest.y`) is entirely unexercised by
any check here.

### Architecture / Memory / Performance / Thread safety / Portability
No file-specific concerns. Follows the same one-shot `Game`/`Draw()`-guard/`Exit()` idiom as every
other file in this shard.

## Detailed Findings

None at HIGH or above. See Missing or Weak Tests below for the one real coverage gap.

## Cross-File Observations

- Confirms `AlphaTestEffect.cpp`'s alpha-test-as-inequality encoding (shared across all backends,
  not WebGPU-specific) is correctly consumed by this backend's WGSL translation — no
  backend-specific mismatch found in the two branches this file actually exercises.
- Per this audit's cross-cutting mandate: this file uses `AlphaTestEffect` (no lighting), so the
  confirmed skinned-effect world-space-normal-transform bug (`WebGPUGraphicsBackend.cpp`'s
  `CreateSkinnedResources()`) does not apply here. `AlphaTestEffect` has no fog support surfaced in
  this backend either way (`CreateAlphaTestResources()`'s shader has no fog term at all,
  matching `plans/plan_webgpu.md`'s explicit, tracked "no fog, same deliberate deferral as every other
  [WebGPU 3D] shader" statement for `alpha_test3d.wgsl` specifically) — not a hidden regression,
  a documented scope boundary, and this file makes no claim about fog either way.

## Missing or Weak Tests

- No `CompareFunction::Equal`/`NotEqual` coverage anywhere in this file, meaning the shader's
  tolerance-band branch (`u.alphaTest.y`) is entirely unverified by this test (or, so far as this
  audit found, by any other `examples-tests-webgpu` file — not independently confirmed by
  exhaustive search of the other 21 files in the shard).
- No stride-32 (`VertexPositionNormalTexture`) alpha-test coverage, despite
  `GetOrCreatePipelineAlphaTest3D()` explicitly branching for it.

## Positive Findings

- Both numerically-asserted checks (A, B) were independently re-derived against the current
  production formula and found correct — not merely internally self-consistent.
- Check D's deliberate isolation of the indexed-draw dispatch path is good test design, catching a
  real class of bug (one dispatch function updated, its indexed sibling forgotten) that this audit
  has seen actually occur elsewhere in this codebase's history.

## Final Assessment

A correct, well-targeted test with no defects found in either the test's own logic or the production
code paths (`AlphaTestEffect::FillGpuDrawParams()`, the WebGPU alpha-test WGSL shaders, and the
`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` dispatch) it exercises. The main opportunity is broadening
`CompareFunction` coverage, not fixing anything.
