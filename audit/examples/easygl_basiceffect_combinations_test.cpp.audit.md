# Audit: examples/easygl_basiceffect_combinations_test.cpp

## Metadata

- Source file: `examples/easygl_basiceffect_combinations_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend `BasicEffect` pixel integration test
- File type: C++ example/integration-test executable (`BasicEffectCombinationsTest : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::BasicEffect` (`BasicEffect.cpp`/`.hpp`),
  `CNA::Internal::Backends::EasyGL::EasyGLGraphicsBackend` (`EasyGLGraphicsBackend.cpp`, GLSL shader strings +
  `BindDrawParams`)
- XNA/FNA relevance: `BasicEffect.VertexColorEnabled`, `.TextureEnabled`/`.Texture`, `.DiffuseColor`,
  `.LightingEnabled`, `.DirectionalLight0` are all real XNA 4.0 members; behavior judged against
  `FNA/src/Graphics/Effect/StockEffects/BasicEffect.cs` + `EffectHelpers.cs` + `HLSL/Lighting.fxh`.
- Main related tests: this file (Task 189); overlaps conceptually with `easygl_basiceffect_combined_test.cpp`
  (Task 370, tests texture×vertexColor×diffuse+emissive together) and `easygl_basiceffect_multilight_emissive_test.cpp`
  (Task 885, multi-light + emissive) but this file's 5 sub-cases are each a single isolated feature, not a
  composition.

## Purpose

Five independent pixel-readback sub-tests, each isolating one `BasicEffect` rendering mode: (a) vertex-color-only,
(b) texture-only, (c) diffuse-color tint over a white texture, (d) vertex-color × texture, (e) one directional light.
Each draws a full-screen NDC quad with `DrawUserPrimitives`, reads the center pixel via `GetBackBufferData`, and
compares against an expected flat color with `colourMatch()`'s ±40 per-channel tolerance. Placement is correct per
`AUDIT_SCOPE.md`'s `examples-tests-easygl` shard convention.

## Executive Verdict

**Healthy** — all 5 expected-color derivations were independently re-derived from the real `BasicEffect::
FillGpuDrawParams()` and EasyGL's actual bound GLSL shader formulas during this audit and matched exactly; the file
also documents (and its own code demonstrates awareness of) a genuine historical shader bug it once masked (see
Positive Findings / F1).

## Checklist Results

### API / XNA / FNA parity
`fx.VertexColorEnabled = true` (line 118) accesses `BasicEffect::VertexColorEnabled` as a **public field**, not a
`getXProperty()`/`setXProperty()` pair — this matches `BasicEffect.hpp`'s actual declaration (`bool
VertexColorEnabled = false;`, line 48), so the test is using the real production API correctly; the field-vs-property
mapping choice itself belongs to `BasicEffect.hpp`'s own audit, not this file's. `setTextureEnabledProperty`,
`setTextureProperty`, `setDiffuseColorProperty`, `setLightingEnabledProperty`, `setAmbientLightColorProperty`,
`DirectionalLight0.setEnabledProperty/setDirectionProperty/setDiffuseColorProperty` are all real, correctly-named
XNA members used with correct signatures.

### Behavioral correctness — verified against production code
- **(a) VertexColor-only red**: `VertexColorEnabled=true`, default `diffuseColor_=(1,1,1)`, no texture. Traced
  `BasicEffect::FillGpuDrawParams()` (lines 55-141): `p.vertexColorEnabled = true`, `p.textureEnabled = false`.
  EasyGL's `colored3D`-family shader multiplies the (red) vertex color straight through — expected red confirmed.
- **(b) Texture-only blue**: default `VertexColorEnabled=false` (re-set explicitly at line 137 for clarity, matches
  default anyway), `textureEnabled=true`, blue 1×1 texture, default `diffuseColor_=(1,1,1)` (white, i.e.
  non-tinting) — `FragColor = texture(blueTex) * diffuseColor(1,1,1,1)` → blue. Confirmed against
  `EasyGLGraphicsBackend.cpp`'s untextured/textured-no-lighting shader pattern (`FragColor=texture(uTexture,vUV)*
  uDiffuseColor`, seen at multiple shader-string sites, e.g. near line 2627's sibling blocks).
- **(c) Diffuse-tint red**: white texture × `diffuseColor_=(1,0,0)` → `FragColor = white * (1,0,0,1)` = red.
  Confirmed by the same formula.
- **(d) Color×Texture green**: `VertexColorEnabled=true` (explicitly set, correctly — the file's own comment at
  lines 176-179 documents a **real historical bug** here, Task 367: the stride-24 shader once multiplied by vertex
  color unconditionally regardless of the `VertexColorEnabled` flag, so this exact test previously "passed" for the
  wrong reason). White texture × green vertex color × default white diffuse → green. This sub-case is a genuine
  regression guard for that fixed bug, not boilerplate.
- **(e) DirectionalLight red**: `ambientLightColor_=(0,0,0)`, `diffuseColor_=(1,1,1)`, `DirectionalLight0`
  direction `(0,0,1)`, diffuse `(1,0,0)`; vertex normal `(0,0,-1)`. Re-derived the math independently: `NdotL =
  max(dot(N,-Dir),0) = max(dot((0,0,-1),(0,0,-1)),0) = 1`. Confirmed `DirectionalLight1`/`DirectionalLight2` default
  to `enabled_=false` (`DirectionalLight.hpp` in-class initializer) so they contribute zero — matches
  `BasicEffect::FillGpuDrawParams()`'s explicit `light1On`/`light2On` gating (lines 93-107) that zeroes an unset
  light's diffuse/specular regardless of its `DiffuseColor` field value. `lightSum = ambient(0,0,0) +
  light0Diffuse(1,0,0)*1 = (1,0,0)`; `litRGB = lightSum*diffuseColor(1,1,1) + emissive(0,0,0 default) = (1,0,0)`;
  texture is white → final = red. Matches the shader formula at `EasyGLGraphicsBackend.cpp:2836-2837`
  (`vec3 lightSum=uAmbientColor+uLight0Diffuse*NdotL0+...; litRGB=lightSum*uDiffuseColor.rgb+uEmissiveColor;`)
  exactly.

All 5 expected colors are correct, not just plausible.

### Logic
`RasterizerState::CullNone` is set once (line 103) before all 5 sub-cases, with an explicit code comment (lines
100-102) documenting a real, previously-found bug ("Task 896 finding") that this shared quad's winding is
back-facing under the real default `RasterizerState` — i.e., this line is not defensive boilerplate, it is fixing a
concrete regression the authors found. `dev.Clear(kBlack)` between every sub-case correctly isolates each readback
from the previous draw.

### Memory/resource lifetime
`Texture2D blueTex`/`whiteTex` are stack-local, constructed once and reused across sub-cases (b)/(c)/(e) via
pointer (`&blueTex`/`&whiteTex`) — both outlive every `fx.setTextureProperty()` call that references them (same
scope, destroyed only at the end of `Draw()`, after all `DrawUserPrimitives` calls complete). No dangling-pointer
risk.

### C++ correctness
`colourMatch()` (lines 44-49) uses `(int)got.getRProperty()` casts before `std::abs` — `getRProperty()` already
returns an integral byte type per `Color`'s XNA convention, so the cast is a no-op widening, not a truncation risk;
correctly guards against `std::abs` ambiguity/UB on unsigned byte subtraction that could otherwise wrap.

### Performance
N/A — single-frame test, five small draws.

### Architecture
Uses only the public `Microsoft::Xna::Framework::Graphics` API; no direct backend coupling. Appropriate layering
for an examples-level integration test.

### Robustness
`readCenter()` (lines 77-84) computes the center pixel rectangle from the *actual* live `Viewport` dimensions
(`vp.getWidthProperty()/2`, `vp.getHeightProperty()/2`) rather than a hardcoded constant — correctly robust to
`GraphicsDeviceManager`'s preferred size actually being honored (200×200, set in the constructor at lines 238-239).

### Testing
This file *is* a test; see Missing or Weak Tests below for what it doesn't cover.

## Detailed Findings

No HIGH/CRITICAL findings. One LOW-severity observation:

### F1 — Fog is explicitly out of scope, correctly documented, and consistent with cross-file design

- Severity: INFO
- Confidence: HIGH
- Category: architecture / test-scope
- Location/symbol: file header comment, line 11 ("Fog is not tested: EasyGL's GpuDrawParams has no fog fields.")
- Evidence: `GpuDrawParams` (per `IGraphicsBackend.hpp`'s audit) does in fact carry `fogEnabled`/`fogColor`/
  `fogStart`/`fogEnd` fields (confirmed while cross-checking `BasicEffect::FillGpuDrawParams()` lines 135-140,
  which populate `p.fogEnabled`/`p.fogColor`/`p.fogStart`/`p.fogEnd` unconditionally) — so the comment's specific
  claim ("no fog fields") is stale/inaccurate as a statement about `GpuDrawParams` today, even though the practical
  conclusion (fog isn't tested by *this* file) is correct and reasonable, since `easygl_basiceffect_fog_test.cpp`
  (audited separately in this same batch) already covers fog exhaustively.
- Why it matters: purely a stale-comment/documentation-accuracy nit — no functional impact, since this file
  genuinely doesn't need to test fog and another file already does. Recorded as INFO rather than a real defect.
- FNA/XNA comparison: N/A.
- Suggested future action: none required; noted for completeness since the comment is technically inaccurate about
  current `GpuDrawParams` contents.

## Cross-File Observations

- Shares the exact `RasterizerState::CullNone` / "Task 896 finding" workaround comment verbatim with
  `easygl_basiceffect_combined_test.cpp`, `easygl_basiceffect_emissive_test.cpp`,
  `easygl_basiceffect_fog_test.cpp`, `easygl_basiceffect_multilight_emissive_test.cpp`, and
  `easygl_basiceffect_golden_test.cpp` — all audited in this same batch, all consistent with each other and with
  the real default-rasterizer-state culling behavior traced in this audit.
- Sub-case (d)'s comment about the Task 367 shader bug (unconditional vertex-color multiply regardless of
  `VertexColorEnabled`) is corroborated independently by this audit's own reading of
  `easygl_basiceffect_combined_test.cpp`'s near-identical Task 364-370 provenance comments — consistent, not a
  one-off unverifiable claim.

## Missing or Weak Tests

- No sub-case combines texture + vertex color + lighting all at once (that gap is filled by
  `easygl_basiceffect_combined_test.cpp` for texture×color×diffuse+emissive, and by
  `easygl_basiceffect_multilight_emissive_test.cpp` for multi-light+emissive, but neither combines *all three* of
  texture+vertexColor+lighting simultaneously in one draw) — a reasonable gap to flag for the graphics-backend
  capability matrix, not a defect in this file.
- `EnableDefaultLighting()`'s three-light rig is not exercised here (that's `easygl_basiceffect_default_lighting_test.cpp`'s
  job, audited separately in this batch) — correct division of responsibility, not a gap.
- No sub-case tests `Alpha < 1.0` interacting with any of these 5 modes (this file always uses opaque colors and
  `BlendState::Opaque`).

## Positive Findings

- Every expected pixel color in this file was independently re-derived from the real `FillGpuDrawParams()` +
  EasyGL shader source during this audit and found to be numerically correct, not just plausible — this is a
  genuine correctness test, not a "compiles and doesn't crash" placeholder.
- Sub-case (d)'s comment about the Task 367 bug demonstrates the test was written to guard a real regression, not
  retrofitted boilerplate.
- Consistent, well-reasoned tolerance (`tol=40`, line 44) appropriate for flat unblended colors read back through a
  full render pipeline.

## Final Assessment

A genuinely correct, well-derived 5-case pixel test for `BasicEffect`'s independent rendering modes on EasyGL, with
one stale-but-harmless comment (F1) as the only nit found.
