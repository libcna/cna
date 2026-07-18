# Audit: examples/easygl_cartooneffect_lambert_shader_test.cpp

## Metadata

- Source file: `examples/easygl_cartooneffect_lambert_shader_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend integration test
- File type: C++ example/integration-test executable (`EasyGLCartoonEffectLambertTest : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::ShaderEffect` (`IEffectMatrices`
  implementation), `CNA::Internal::Backends::EasyGLGraphicsBackend::BindCustomEffectMatrices`
  (`EasyGLGraphicsBackend.cpp` lines 4507-4524), `ContentManager::EffectTypeReader::ReadCustomGlslEffect`
  (`ContentManager.cpp` lines 765-785).
- XNA/FNA relevance: exercises a real 3D indexed draw with `VertexPositionNormalTexture` (Task 1079 capability)
  driven by a custom `Effect` loaded via `.cnj`/`ContentManager` — real XNA 4.0-shaped rendering. The specific
  shader (`CartoonEffect.Fx`'s `Lambert` technique) is from the Microsoft XNA Game Studio
  **NonPhotoRealisticSample**, not FNA.
- FNA reference: N/A for the shader body — `NonPhotoRealisticSample_4_0` is not present in the local FNA reference
  tree (confirmed by search). This audit independently re-derived both of the file's expected pixel values from the
  ported GLSL and confirmed they match exactly; the underlying HLSL transcription itself could not be checked
  against Microsoft's original source.
- Main related tests: uses the same `.cnj`/`ContentManager::Load<Effect>` path first exercised by
  `easygl_bloom_extract_test.cpp` in this same batch.

## Purpose

Task 947 (Phase 78): ports `CartoonEffect.Fx`'s `LightingVertexShader`+`LambertPixelShader` pair (the "flat/Lambert"
shading mode `NonPhotoRealisticSample` falls back to when cartoon/toon shading is toggled off) to GLSL, exercising
Task 1079's `VertexPositionNormalTexture` support and per-object `World` matrix reaching a per-vertex normal
transform. Correctly placed as an `easygl_`-prefixed integration test.

## Executive Verdict

**Healthy** — both checks' expected pixel values were independently re-derived by this audit from the ported GLSL
and matched exactly, and the two-check design (Identity vs. 180°-Y-rotation) genuinely proves the vertex shader's
`World`-to-normal transform and the pixel shader's `saturate()` clamp are both live, not coincidentally-passing
no-ops. One real, disclosed limitation: `mat3(World)` (no inverse-transpose) means this shader — like, per the test's
own reasoning, apparently the original sample itself — would produce incorrect normals under non-uniform scale, a
case this test cannot exercise since it only uses pure rotations.

## Checklist Results

### API / XNA / FNA parity
`IEffectMatrices`'s `setWorldProperty`/`setViewProperty`/`setProjectionProperty` (used at lines 183-187) are
correctly named per CNA's getX/setX convention. `Matrix::CreateRotationY`, `Matrix::CreateLookAt`,
`Matrix::CreatePerspectiveFieldOfView`, `MathHelper::PiOver4`/`Pi` are all real, correctly-used XNA 4.0 static
members.

### Behavioral correctness — independently re-derived, both checks
Traced the vertex-shader translation (`kVertSrc`, lines 82-99): `gl_Position = Projection * View * World *
vec4(aPosition, 1.0)` correctly matches the HLSL comment's `mul(mul(mul(input.Position, World), View),
Projection)` under the standard row-vector(HLSL)-to-column-vector(GLSL) transposition convention this codebase
uses consistently elsewhere (per this same batch's `easygl_blur_shader_test.cpp` observations). `vec3 worldNormal =
mat3(World) * aNormal;` matches the HLSL's `mul(input.Normal, World)` under the same convention, using only the
3×3 rotation/scale sub-matrix (correct for a translation-only difference, but see the non-uniform-scale caveat
below). `vLightAmount = dot(worldNormal, LightDirection)` — correctly **not** saturated in the vertex shader,
matching the HLSL comment's explicit note ("NOT saturated here", header line 15) — the `clamp()` only appears in the
fragment shader (`kFragSrc` line 113), exactly matching `LambertPixelShader`'s `saturate(input.LightAmount)`.

Re-derived both checks independently:
- **Check A** (`World=Identity`): normal `(0,0,1)` unchanged by `mat3(Identity)`. `LightDirection=normalize(1,1,1)
  ≈ (0.57735,0.57735,0.57735)`. `dot((0,0,1), LightDirection) = 0.57735` (already in `[0,1]`, so `saturate` is a
  no-op here, as the header itself notes, line 29). `color.rgb *= clamp(0.57735,0,1)*0.5 + 0.5 = 0.788675`.
  `texColor=(200,100,50)/255=(0.7843,0.3922,0.1961)`. Result: `0.7843*0.788675=0.6185*255≈158`;
  `0.3922*0.788675=0.3093*255≈79`; `0.1961*0.788675=0.1546*255≈39` → `(158,79,39)` — matches exactly (line 32
  expectation, `close()` tolerance `±6`).
- **Check B** (`World=RotationY(π)`): `R_y(π) = diag(-1,1,-1)` (since `cos(π)=-1, sin(π)=0`), so normal `(0,0,1)`
  → `(0,0,-1)`. `dot((0,0,-1), LightDirection) = -0.57735` → `clamp(...,0,1) = 0` (this is where `saturate()`
  genuinely engages, unlike Check A). `color.rgb *= 0*0.5 + 0.5 = 0.5`. Result: `(200,100,50)*0.5 = (100,50,25)` —
  matches exactly (line 34 expectation). This independently confirms the clamp is a real, exercised code path in
  this test, not one that happens to be inert both times.

Both re-derivations are exact matches, not near-tolerance passes — a strong, correctly-designed pair of checks.

### Logic
`DrawOnce(world)` (lines 170-204) is a clean, reusable single-draw-and-readback helper, called exactly twice with
the two `World` matrices under test — matching `easygl_blur_shader_test.cpp`'s good pattern in this same batch
(vs. some other sibling files in this shard that inline each pass separately).

### Memory/resource lifetime
`fxBase_` (`std::shared_ptr<Effect>`, loaded via `ContentManager::Load`), `vb_`/`ib_`
(`std::unique_ptr<VertexBuffer>`/`std::unique_ptr<IndexBuffer>`) are all constructed once in `Initialize()` and
reused across both `DrawOnce()` calls — correct, efficient resource reuse.

### C++ correctness
`dynamic_cast<ShaderEffect*>(fxBase_.get())` (lines 182, 211) is used correctly and null-checked at the point that
matters (line 212, before dereferencing) — though note line 182 (`fx->setWorldProperty(...)`) dereferences the
result of `DrawOnce`'s own `dynamic_cast` call without a null check inside `DrawOnce` itself; this is safe in
practice only because `Draw()` (the sole caller) already validated `fx`/`fx->IsEffectValid()` before either
`DrawOnce()` call (lines 211-217, 219-220) — not a live bug, but a latent fragility if `DrawOnce` were ever called
from a new path without that precondition.

### Performance
N/A — single-frame test with a single quad.

### Architecture
Correctly reuses the `.cnj`/`ContentManager` Effect-loading path already established and verified by
`easygl_bloom_extract_test.cpp` in this same batch, rather than duplicating a separate ad-hoc loading mechanism —
consistent, DRY test-infrastructure reuse across the shard.

### Robustness
`IsEffectValid()` guard (lines 211-217) fails loud with a printed message and non-zero exit rather than proceeding
with an invalid program — consistent with every other well-behaved file in this batch.

### Testing
This file is itself a test; see the non-uniform-scale gap noted below (inherited from the shader's own design, not
a CNA-specific bug).

## Detailed Findings

No MEDIUM-or-higher findings. One LOW item:

### F1 — `mat3(World)` normal transform is only exercised under pure rotation, not non-uniform scale

- Severity: LOW
- Confidence: MEDIUM (the shape of the limitation is certain; whether it reflects a genuine upstream sample
  simplification or a transcription choice cannot be confirmed without the original `.fx` source, per Metadata)
- Category: test-coverage
- Location/symbol: `kVertSrc` line 96 (`vec3 worldNormal = mat3(World) * aNormal;`); `DrawOnce` call sites (lines
  219-220, `Matrix::getIdentityProperty()` and `Matrix::CreateRotationY(MathHelper::Pi)` — both pure rotations,
  no scale component)
- Evidence: `mat3(World)` (the upper-left 3×3 of the world matrix) correctly transforms normals only when `World`
  contains no non-uniform scale — the mathematically correct general-case normal transform is the
  inverse-transpose of that 3×3 block. Both of this test's `World` matrices (`Identity`, `RotationY(π)`) are pure
  rotations, for which `mat3(World)` and its inverse-transpose coincide, so this test cannot distinguish "correctly
  implements the general normal transform" from "happens to work because no scale was tested."
- Why it matters: if `CartoonEffect`/this GLSL port is ever reused with a non-uniformly-scaled `World` (a plausible
  future use of the same `.cnj`/shader pair beyond this specific test), lighting would be visibly wrong — though
  this may be a faithful port of the original sample's own simplification (a common simplification in older XNA
  toon-shading samples) rather than a CNA-introduced defect; not verifiable against ground truth in this sandbox.
- FNA/XNA comparison: N/A (sample-project shader, not FNA).
- Related files: none in this batch.
- Suggested future action (not implemented by this audit): if this shader is ever reused for a non-uniformly-scaled
  object, either confirm the original sample also uses the simplified `mat3(World)` transform (accepting the same
  limitation deliberately) or upgrade to a proper inverse-transpose normal matrix.

## Cross-File Observations

- Reuses the exact `.cnj` Effect-loading pattern (`WriteFile` helper, `cnjVersion`/`type`/`vertex`/`fragment` JSON
  shape) established by `easygl_bloom_extract_test.cpp` in this batch — consistent infrastructure across the shard.
- Shares the uncleaned-temp-directory pattern noted in `easygl_bloom_extract_test.cpp.audit.md` (Finding F1 there) —
  `Initialize()` (lines 135-138) creates a per-run temp directory that is never removed. Same LOW-severity hygiene
  note applies here; not re-elevated to its own finding in this report to avoid duplicate bookkeeping across the
  shard.

## Missing or Weak Tests

- See F1 — no non-uniform-scale case is (or, arguably, should be, if the limitation is upstream-faithful) tested.
- No test exercises `TextureEnabled=false` (the `LambertPixelShader`'s other HLSL branch, `color = TextureEnabled ?
  tex2D(...) : 0`) — this file always sets `TextureEnabled=1` (line 193); the untextured branch (`color=0`, i.e. the
  cartoon effect drawing flat unlit black before the lighting multiply) is untested here.

## Positive Findings

- Both checks' expected pixel values were independently re-derived by this audit and matched exactly — a genuinely
  correct, non-approximated test.
- The two-`World`-matrix design specifically isolates and proves the vertex-to-pixel-shader `LightAmount` pipeline
  is live end-to-end (Check A: clamp is a no-op; Check B: clamp genuinely engages) rather than relying on a single
  configuration that could pass for the wrong reason (e.g. a shader that ignored `World`/`Normal` entirely and
  returned a constant would fail Check B, not just look plausible).

## Final Assessment

An accurate, well-designed test whose two checks were independently confirmed correct down to exact pixel values,
with one real but likely-inherited-from-the-original-sample limitation (no non-uniform-scale normal-transform
coverage) and one minor infrastructure hygiene note shared with a sibling file in this batch.
