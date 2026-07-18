# Audit: examples/bgfx_basiceffect_preferperpixellighting_test.cpp

## Metadata

- Source file: `examples/bgfx_basiceffect_preferperpixellighting_test.cpp`
- Audit status: AUDITED (static; Bgfx is not in the D-P4 opportunistic-build feasibility list for this
  sandbox — no `cmake-build*` directory exists here)
- Subsystem: `examples-tests-bgfx` shard — `BasicEffect.PreferPerPixelLighting` real-dispatch-selector pixel test
- File type: standalone `Game`-subclass executable, CTest-registered (`cna_bgfx_test(cna_test_bgfx_basiceffect_preferperpixellighting …)` / `cna_register_backend_test(NAME Bgfx_BasicEffect_PreferPerPixelLighting …)`, `cmake/Tests/BgfxTests.cmake:323-326`)
- XNA/FNA relevance: direct — `BasicEffect.PreferPerPixelLighting`, `IEffectLights`, the
  vertex-lit-vs-pixel-lit shader-selection dichotomy XNA's `BasicEffect`/`SkinnedEffect` implement
- FNA reference: `BasicEffect.fx` `VSIndices`/`PSIndices` tables (rows 8-15 select
  `VSBasicVertexLighting*`/`PSBasicVertexLighting*` when `PreferPerPixelLighting=false`, the real XNA default;
  rows 16-19 select `VSBasicPixelLighting*`/`PSBasicPixelLighting*` when `true`)
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp` (`preferPerPixelLighting_`
  default `false`, `BasicEffect.hpp` line 368), `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`
  (dispatch lines 2807-2808/3315-3316: `(!params.preferPerPixelLighting && bgfx::isValid(litTextured3DVertexLitProgram_)) ? litTextured3DVertexLitProgram_ : litTextured3DProgram_`)

## Purpose

3-check pixel test proving `PreferPerPixelLighting` is a genuine, live shader-dispatch selector on Bgfx, not a
decorative no-op — per the file's own header, this backend evaluated lighting per-pixel unconditionally before
Task 1104, which is the *opposite* of XNA's real default (`false` → per-vertex/Gouraud-interpolated), and
`GpuDrawParams` did not even carry the flag until Task 1100. The test reuses the exact scene from
`bgfx_basiceffect_specular_test.cpp`'s "(a) eye straight on" case: a spatially-uniform diffuse term (one shared
vertex normal) combined with a spatially-varying specular term (the half-vector depends on the per-fragment
view direction), sampled exactly at the seam between the quad's two triangles — a genuinely well-chosen
discriminating scene, since Gouraud-averaging two per-vertex specular values at that seam produces a
measurably different number than re-evaluating specular fresh at that exact point.

## Executive Verdict

**Healthy** — both checks' expected constants were independently re-derived by this audit via the actual
half-vector Blinn-Phong formula and matched almost exactly (within normal floating-point/interpolation
rounding), and check (c)'s "these two differ" assertion is a valid, load-bearing proof that the flag actually
changes behavior. The file shares the batch's stale cull-state comment (F1).

## Checklist Results

### API / XNA / FNA parity
`fx.setPreferPerPixelLightingProperty(preferPerPixelLighting)` (line 121) maps directly to FNA's
`BasicEffect.PreferPerPixelLighting`. The test correctly exercises the real XNA default (`false`) as case (a),
not an arbitrarily-chosen baseline.

### Behavioral correctness
Independently re-derived both expected values by hand, replicating (not merely trusting) the file's own
scene: `kAmbient=0.02`, `kMaterialDiffuse=0.4`, `kLightDiffuse=kLightSpecular=kSpecularColor=(uniform)`,
`kSpecularPower=32`, `kLightDirRaw=(0.5,0,-1)` normalized, `kNormal=(0,0,1)`, `kEyeStraightOn=(0,0,3)`, quad
corners at `(±1,±1,0)`.
- **Diffuse** (spatially constant, since `N` and light direction don't vary across the quad):
  `NdotL=dot(-lightDir,N)=0.8944`; `diffuse=(0.02+0.8944*0.5)*0.4=0.18688`.
- **Vertex-lit (Gouraud) case (a)**: computed per-vertex specular at the two vertices (`TL=(-1,1,0)`,
  `BR=(1,-1,0)`) that dominate the sampled center pixel (which sits exactly on the shared diagonal, so only
  these two vertices' values are interpolated, with the third vertex of each triangle contributing zero
  weight at that exact point): `eyeVector_TL≈(0.3015,-0.3015,0.9045)`, `halfVector_TL≈(-0.0796,-0.1648,0.9831)`,
  `dot(h,N)≈0.9831`, `spec_TL=pow(0.9831,32)≈0.5794`; `eyeVector_BR≈(-0.3015,0.3015,0.9045)`,
  `halfVector_BR≈(-0.3798,0.1529,0.9125)` (normalized), `dot(h,N)≈0.9125`, `spec_BR=pow(0.9125,32)≈0.0534`.
  Gouraud average `≈0.3164`. Total `=0.18688+0.3164=0.50328×255≈128.3`, rounding to **128** — the file's
  asserted `kExpectedVertexLit(127,127,127)` is 1 unit off this hand-derived value, attributable to ordinary
  GPU floating-point/interpolation precision (the same class of ~1-unit gap the EasyGL sibling test explicitly
  documents and this audit independently confirmed for that file too), not a computational error.
- **Pixel-lit case (b)**: fresh per-fragment evaluation at the exact center `worldPos=(0,0,0)`:
  `eyeVector=(0,0,1)`, `halfVector=normalize((0,0,1)-(0.4472,0,-0.8944))≈(-0.2298,0,0.9732)`,
  `dot(h,N)≈0.9732`, `spec=pow(0.9732,32)≈0.4193`. Total `=0.18688+0.4193=0.6062×255≈154.6`, rounding to
  **155** — exactly matching `kExpectedPixelLit(155,155,155)`.
Both derivations independently confirm the file's asserted constants are correct for the current shader
implementation (not stale, unlike the analogous off-axis-eye check in `bgfx_basiceffect_specular_test.cpp` —
see that file's own audit report for the contrasting case).

### Logic
`renderWith(dev, tex, preferPerPixelLighting)` (lines 112-161) correctly toggles only the one flag under test
between the two calls (lines 171, 176), keeping every other scene parameter fixed — a clean, single-variable
comparison.

### Robustness
Check (c) (`!matches(vertexLit, pixelLit)`, line 181) is the important structural check: it proves the flag
is a live dispatch selector rather than a decorative field that's stored but never read. Combined with (a)
and (b) each asserting a *specific* value (not just "different from each other"), a regression that made both
paths silently converge to the *same* (wrong) value would be caught by (a) or (b) failing individually, while
a regression that made them differ for the wrong reason would likely also fail (a) or (b). This 3-check
structure is a well-reasoned, mutually-reinforcing set.

### Testing
Directly and specifically validates the Task 1104 dispatch fix using a scene deliberately shaped to make the
distinction observable — genuinely testing behavior, not merely "does it compile / does it not crash."

### Cross-file consistency
Confirmed the dispatch condition in `BgfxGraphicsBackend.cpp` (`!params.preferPerPixelLighting &&
bgfx::isValid(litTextured3DVertexLitProgram_)) ? litTextured3DVertexLitProgram_ : litTextured3DProgram_`)
correctly falls back to the per-pixel-lit program if the vertex-lit one failed to compile on a given renderer,
which the comment (line 2804-2806) explicitly notes as intentional graceful degradation rather than a silent
default flip. This design means a genuinely broken vertex-lit shader on some Bgfx renderer backend would
silently make case (a) actually execute the per-pixel path instead — this test's own check (c) would still
pass in that broken scenario (since case (a) would then equal case (b)'s dispatch, not literally the same
value unless PreferPerPixelLighting were also flipped, so (c) would actually **fail** since both would render
the per-pixel value ~155, making `vertexLit==pixelLit`), correctly surfacing that failure mode rather than
masking it — a good robustness property of the 3-check design, not a coincidence.

## Detailed Findings

### F1 — Header comment's cull-state claim is stale (shared with 7 sibling files)

- Severity: MEDIUM
- Confidence: HIGH
- Category: documentation-accuracy / stale-comment
- Location/symbol: inline comment lines 151-152 (`"Task 364/896 finding: Bgfx's default
  CullCounterClockwiseFace-matching cull state culls this quad's winding unless explicitly disabled"`)
- Evidence: same as recorded for the other 7 files in this batch — `b6a00bc6 fix(Task 896): push
  GraphicsDevice's real default RasterizerState to all 3 backends` is confirmed (`git merge-base
  --is-ancestor`) as an ancestor of the current `HEAD`, and `GraphicsDevice.cpp` line 207 confirms the fix
  (pushing the FNA-correct default to all 3 backends from the constructor) is live. This file's last content
  change is commit `0cb4a591` (Jul 16 12:39), which — notably — **postdates** `b6a00bc6` (Jul 7 19:39) by
  over a week, meaning this specific file's author had every opportunity to know the fix had landed and
  either update or remove the now-inaccurate framing, yet the comment (unlike, e.g., the multilight-emissive
  file, whose content predates the fix) still frames this as a Bgfx-only, unaddressed quirk.
- Why it matters: same as recorded elsewhere in this batch — the workaround itself (`RasterizerState::CullNone`,
  line 153) remains correct and necessary, but the comment's causal story is now inaccurate, and in this
  particular file the staleness is less excusable than in its older siblings since this file was actually
  authored/touched after the fix landed.
- FNA/XNA comparison: N/A (documentation-accuracy).
- Related files: shared with all 7 other files in this batch.
- Suggested future action (not implemented by this audit): refresh the comment.

## Cross-File Observations

- This is the file with the clearest, most explicit forward-reference relationship to
  `bgfx_basiceffect_specular_test.cpp` (deliberately reuses its exact scene) — cross-checking both files'
  numbers together strongly corroborates both are internally consistent and independently correct for the
  vertex-lit case, since they were derived (and, per the header, "confirmed against this backend's own real
  render") from the same underlying geometry and material parameters.
- Unlike `bgfx_basiceffect_specular_test.cpp`'s own off-axis-eye check, neither of this file's two checks
  shows any sign of a stale, pre-Task-1104 constant — both values independently re-derive correctly for the
  *current* code.

## Missing or Weak Tests

None found specific to this file — the 3-check structure (value-a, value-b, a≠b) is a complete, appropriately
minimal test of a boolean dispatch flag's two branches.

## Positive Findings

- Both numeric expectations were independently re-derived via the actual Blinn-Phong half-vector formula and
  matched (127 within 1 unit of a 128.3 hand-calc, attributable to ordinary float/interpolation rounding; 155
  matching a 154.6 hand-calc almost exactly).
- The 3-check structure (specific-value, specific-value, inequality) is a stronger design than a bare
  inequality check alone would be, and was shown above to correctly surface a hypothetical
  vertex-lit-shader-compile-failure fallback scenario rather than silently passing through it.
- Good traceability: the file's header explicitly documents the EasyGL/Vulkan cross-backend measured values
  (~127/~155) that this Bgfx test's own values were checked against before being accepted, per its own commit
  history — a healthy practice this audit was able to independently corroborate via direct math rather than
  needing to take on faith.

## Final Assessment

A strong, numerically-verified dispatch-selector test with no functional defect found — the one weakness
found (F1) is a documentation-accuracy issue shared across the whole batch, made slightly more notable here
since this particular file was touched after the underlying fix (Task 896) had already landed.
