# Audit: examples/easygl_basiceffect_texture_enabled_test.cpp

## Metadata

- Source file: `examples/easygl_basiceffect_texture_enabled_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — `BasicEffect.TextureEnabled` (no vertex color) pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_easygl_test(cna_test_easygl_basiceffect_texture_enabled …)` /
  `cna_register_backend_test(NAME EasyGL_BasicEffect_TextureEnabled …)`,
  `cmake/Tests/EasyGLTests.cmake:1085-1087`).
- XNA/FNA relevance: direct — `BasicEffect.TextureEnabled`/`DiffuseColor` combination
  (`VSBasicTxNoFog`/`PSBasicTxNoFog` shader family).
- FNA reference: `BasicEffect.cs` (shader-index selection), `HLSL/Common.fxh`
  (`ComputeCommonVSOutput`), `EffectHelpers.cs` (`SetMaterialColor`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp`
  (`FillGpuDrawParams()` line 71), `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`
  (`EnsureTextured3DProgram()` lines 2643-2700).

## Purpose

Proves `TextureEnabled=true` with `VertexColorEnabled=false`/`LightingEnabled=false` (both real FNA
defaults) produces `TextureColor * DiffuseColor` — a genuine component-wise product, not either input
alone. Explicitly closes a coverage gap the file's own comment (lines 22-26) identifies in the
pre-existing `easygl_basiceffect_combinations_test.cpp`: that file's "texture only" and "diffuse tint"
cases never combine a *non-white* texture with a *non-white* diffuse, so neither can distinguish
"texture ignored," "diffuse ignored," or "both correctly multiplied."

## Executive Verdict

**Mostly healthy** — the core pixel assertion is correct and independently re-verified against the
live `EnsureTextured3DProgram()` shader; the file's own "NOTE (deferred...)" comment about a missing
`+EmissiveColor` term is **stale** and no longer describes the current codebase (see F1), though this
does not affect the test's own correctness since it uses the default `EmissiveColor=(0,0,0)`.

## Checklist Results

### API / XNA / FNA parity
`setTextureEnabledProperty(true)`/`setTextureProperty(&tex)`/`setDiffuseColorProperty(kDiffuse)`
(lines 118-120) map correctly to FNA's `BasicEffect.TextureEnabled`/`Texture`/`DiffuseColor`.
`VertexPositionTexture` (no color attribute) correctly forces stride-20 dispatch.

### Behavioral correctness
Confirmed `EnsureTextured3DProgram()`'s fragment shader (line 2683):
`FragColor=texture(uTexture,vUV)*uDiffuseColor;` — a direct component-wise product, matching the
test's own derivation. With `kTexColor=(200,100,50,255)` and `kDiffuse=(0.8,0.4,0.6)`:
`(200/255)*0.8*255=160`, `(100/255)*0.4*255=40`, `(50/255)*0.6*255=30` — exactly `kExpected(160,40,30)`
(line 65). `kDiffuseOnly(204,102,153)` (`=kDiffuse*255`, the value a "texture ignored" bug would
produce) and `kTextureOnly(200,100,50)` (the value a "diffuse ignored" bug would produce) are both
correctly distinct from `kExpected` by a wide margin (closest gap `160` vs `200`=40, well outside the
`±8` tolerance), so the three assertions (lines 146-151) genuinely discriminate all three hypotheses.

### Logic
Dispatch confirmed via `SelectProgram()`'s stride-20 case (`EasyGLGraphicsBackend.cpp` line 3962):
`VertexPositionTexture` (stride 20, no lighting/skinning/PBR/dual-texture/env-map flags set) routes
unambiguously to `EnsureTextured3DProgram()`.

### C++ correctness
Tolerance/constant separation checked as above — no risk of accidental pass via overlap.

### Robustness
No boundary case for `Alpha≠1.0` interacting with the texture's own alpha channel — reasonable scope
omission since `Alpha` is a separate, dedicated concern better tested by a fog/alpha-specific sibling.

### Testing
Genuinely improves on the pre-existing `combinations_test.cpp` gap it identifies — a real, specific,
verifiable coverage claim (not just "adds another test"), and this audit confirms the gap claim is
accurate: `TextureColor*DiffuseColor` with both non-white/non-default *is* the only scene shape that
can distinguish all three hypotheses simultaneously.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — "NOTE (deferred...)" comment about a missing `+EmissiveColor` term is stale; the gap it describes was already fixed by Task 369

- Severity: LOW
- Confidence: HIGH
- Category: maintainability (stale documentation, not a functional defect)
- Location/symbol: header comment lines 16-20; `BasicEffect::FillGpuDrawParams()`
  (`BasicEffect.cpp` line 71)
- Evidence: the comment states *"CNA's BasicEffect::FillGpuDrawParams() currently computes only
  DiffuseColor * Alpha, omitting + EmissiveColor ... tracked as part of Task 369."* The current
  `BasicEffect.cpp` (line 71) reads:
  `const Vector3 forwardedDiffuse = lightingEnabled_ ? diffuseColor_ : (diffuseColor_ +
  emissiveColor_);` — i.e. the disabled-lighting branch (exactly the branch this test exercises,
  since `LightingEnabled` is left at its default `false`) *does* add `emissiveColor_`, matching
  FNA's `EffectHelpers.SetMaterialColor`'s disabled-lighting formula
  `(DiffuseColor + EmissiveColor) * Alpha` exactly. `git log` confirms the ordering: this test file
  is Task 366 (`f8c70725 test(Task 366): verify BasicEffect TextureEnabled=true pixel output on all 3
  backends`), and the gap it describes was subsequently closed by
  `e4c60e26 fix(Task 369): honor EmissiveColor in BasicEffect's no-lighting diffuse formula` — a
  later commit whose fix was never back-ported into this file's own header comment.
- Why it matters: purely a documentation-freshness issue — the test itself is unaffected because it
  leaves `EmissiveColor` at its default `(0,0,0)`, so the comment's claimed gap was never actually
  exercised or masked by this file. A future reader relying on this comment to decide "is
  EmissiveColor honored here" would be misled into thinking a known gap still exists.
- FNA/XNA comparison: current CNA behavior matches FNA's `EffectHelpers.SetMaterialColor` disabled-
  lighting branch exactly (see Evidence).
- Related files: the identical stale note also appears in the sibling
  `easygl_basiceffect_texture_vertexcolor_enabled_test.cpp` (Task 367) — flagged separately in that
  file's own report, not duplicated here beyond this cross-reference.
- Suggested future action (not implemented by this audit): update or remove the stale "NOTE
  (deferred...)" comment block in both files now that Task 369 has landed, to avoid a future
  contributor re-investigating an already-closed gap.

## Cross-File Observations

- Shares the identical stale-comment pattern with `easygl_basiceffect_texture_vertexcolor_enabled_test.cpp`
  (both reference "Tasks 364-366"/"Task 369" the same way) — worth a single combined doc-cleanup
  pass across both files rather than two separate edits.
- `EnsureTextured3DProgram()` and `EnsureColoredTextured3DProgram()` (the sibling file's shader) share
  the same `uDiffuseColor` uniform-binding convention — consistent, no divergence found.

## Missing or Weak Tests

None beyond F1 (a documentation issue, not a test-coverage gap) — the three assertions already fully
discriminate the three plausible failure modes for this exact code path.

## Positive Findings

- The header comment's own coverage-gap analysis (identifying exactly why the pre-existing
  `combinations_test.cpp` couldn't distinguish "texture ignored" from "diffuse ignored" from
  "correctly multiplied") is precise and independently verifiable — a genuine example of a test
  author reasoning about discriminating power rather than just adding another assertion.
- Reuses the same numeric pair (texture `(200,100,50)`, diffuse `(0.8,0.4,0.6)`) as the sibling
  vertex-color test, making the two files' expected constants cross-checkable against each other,
  which this audit exploited.

## Final Assessment

A correct, well-targeted test whose only issue is an outdated inline comment describing a bug that a
later task (369) already fixed — a documentation-freshness note, not a functional or test-coverage
defect.
