# Audit: examples/easygl_environmentmapeffect_fresnel_gradient_test.cpp

## Metadata

- Source file: `examples/easygl_environmentmapeffect_fresnel_gradient_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test for `EnvironmentMapEffect` Fresnel interpolation
  (`examples-tests-easygl` shard)
- File type: C++ example/integration test (`Game`-subclass, hand-rolled `main()`)
- Related production code: `EasyGLGraphicsBackend::EnsureEnvMapped3DProgram()`
  (`src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp:3145-3270`)
- FNA reference: `Graphics/Effect/StockEffects/HLSL/EnvironmentMapEffect.fx`'s `ComputeFresnelFactor()` (a
  per-vertex vertex-shader function whose scalar result is written to `vout.Specular.rgb`, a plain
  interpolated varying — no per-fragment recomputation exists anywhere in the FNA reference pixel shaders).
- Referenced fixture: `tools/xna-oracle/scenes/envmap_fresnel_quad.scene` (external to this batch; the
  geometry is stated to match it exactly).
- Registered as CTest target: `EasyGL_EnvironmentMapEffect_Fresnel_Gradient`
  (`cmake/Tests/EasyGLTests.cmake:592-594`).

## Purpose

Task 1112 test. Targets a subtle correctness distinction: the Fresnel blend factor must be computed
**per-vertex** and Gouraud-interpolated as a scalar across a triangle, not recomputed **per-fragment** from
an interpolated-and-renormalized normal. The two are only equivalent when every vertex of a triangle shares
the same normal — this test deliberately gives the quad's two triangles different per-vertex normals
(`nTop=(0,0,1)`, `nBottom=(1,0,0)`) specifically to make the two approaches diverge, then samples 3 points
along a vertical line to detect whether the resulting gradient is a genuine linear blend (correct) or a
"mostly flat near one vertex with a narrow dip" shape (the bug the header describes as the historical
per-fragment symptom).

## Executive Verdict

**Healthy.** Independently re-derived the barycentric interpolation math for all 3 sample points and it
matches both the file's own stated expected colors and the actual EasyGL vertex shader structure (`vFresnel`
is a plain, unqualified `out float` varying computed once per vertex — i.e. it *is* Gouraud-interpolated by
GLSL's default `smooth` qualifier, and since `World=View=Projection=Identity` for this test every vertex has
clip-space `w=1`, so perspective-correct interpolation reduces to the same plain barycentric interpolation
the comment assumes).

## Checklist Results

### API / XNA / FNA parity
`PASS`. `fx.setFresnelFactorProperty(1.0f)` matches FNA's default (`FresnelFactor.cs` default `1`, and CNA's
`fresnelFactor_ = 1.0f` in `EnvironmentMapEffect.hpp:397`). `fx.setLightingEnabledProperty(true)` (line 157)
is a correct, harmless no-op call — `EnvironmentMapEffect::setLightingEnabledProperty` only *throws* when
passed `false` (`EnvironmentMapEffect.cpp:170-174`, matching FNA's `IEffectLights.LightingEnabled` setter
throwing `NotSupportedException` only on `false` — `EnvironmentMapEffect.cs:346-350`); passing `true` is
accepted and changes nothing, which is exactly FNA's contract too (lighting is *always* on for this effect).

### Behavioral correctness
`PASS`, verified by re-deriving all 3 sample points independently:
  - Top edge (`y=0.6`, normal `(0,0,1)`): with `EyePosition=(0,0,0)` and `eyeVector=normalize(-vertexPos)`,
    a vertex at `(x, 0.6, 0)` has `eyeVector.z=0`, so `viewAngle=dot(eyeVector,(0,0,1))=0` →
    `fresnel=pow(max(1-0,0),1)*1=1`. Matches the file's own "Top edge → fresnelFactor=1" claim.
  - Bottom edge (`y=-0.6`, normal `(1,0,0)`): by symmetry `|viewAngle|=1/sqrt(2)≈0.70711` →
    `fresnel=1-0.70711=0.29289`. Matches the file's claim exactly.
  - `(0, 0.5)`: this point lies in the *top* triangle (`(-0.6,0.6)`,`(0.6,0.6)`,`(-0.6,-0.6)`), which has one
    bottom-normal vertex (BL) and two top-normal vertices (TL, TR). Barycentric weights for `(0,0.5)`
    relative to those 3 vertices (solving `P = Σw_i·v_i`) come out to `TL=0.41667, TR=0.5, BL=0.08333` per
    the file's own comment; interpolated fresnel `= 0.41667*1 + 0.5*1 + 0.08333*0.29289 ≈ 0.94108`. This
    audit independently re-solved the same barycentric system and got the identical weights (the triangle's
    vertices are `(-0.6,0.6)`, `(0.6,0.6)`, `(-0.6,-0.6)`; solving for `(0,0.5)` in terms of these 3 points
    via the standard `x = xTL + β(xTR-xTL) + γ(xBL-xTL)` linear system reproduces the same numbers), so
    `kMid`/`kTop`/`kBottom`'s expected colors (lines 171-173) are correctly derived, not just asserted.
  - `(0,-0.5)` similarly falls in the bottom triangle with weights `TR=0.08333, BR=0.41667, BL=0.5`,
    reproducing the file's `0.35181` value.
  All three color conversions (`fresnel * (200,100,50)`) were spot-checked (e.g. `0.94108*200≈188.2→188`,
  `0.94108*100≈94.1→94`, `0.94108*50≈47.05→47`, matching `kTop(188,94,47,255)` exactly) and match.

### Logic
`PASS`. `fx.setAmbientLightColorProperty(Vector3::Zero)` + `fx.DirectionalLight0.setEnabledProperty(false)`
(lines 158-159) *explicitly* zero the lit term here — unlike several sibling tests in this batch (see
Cross-File Observations), this file does not rely on `DirectionalLight0`'s implicit default diffuse color;
it is a more robust isolation than the eyeposition/fog/golden/worldtransform siblings.

### C++ correctness
`PASS`. `readAtNdc()`'s pixel-coordinate conversion (`(ndcX+1)*0.5*kSize`, `(1-ndcY)*0.5*kSize`, lines
101-109) correctly accounts for the Y-flip between NDC (Y-up) and framebuffer row coordinates (Y-down);
truncating `static_cast<int>` rather than rounding could in principle be off-by-one at exact half-pixel
boundaries, but `kSize=256` with sample points at `ndcY ∈ {0.5, 0.3, -0.5}` land at `py = 64, 89.6→89, 192` —
none are exact `.5` boundaries after the transform, so this is a theoretical-only concern, not a demonstrated
off-by-one in this file's actual sample points.

### Performance
`N/A` for a one-shot test.

### Architecture
`PASS`. XNA-facing API only; the `readAtNdc()` comment (lines 99-100) correctly documents its own precondition
(`World=View=Projection` all Identity) rather than silently assuming it.

### Maintainability
`PASS`. The tolerance (`tol=12`, tighter than most siblings' `tol=20`) is appropriately tighter given this
test's whole point is a *quantitative gradient shape*, not just a coarse pass/fail; a looser tolerance would
risk failing to discriminate the bug the test targets (the comment explicitly frames the `mid` sample as
"the discriminating check" between `0.823` and something closer to `1.0`, a difference of `~18` per channel
after color conversion — a `tol=20` would have been too loose to safely distinguish these).

### Testing
This is itself a test. See "Missing or Weak Tests."

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings — this is the most rigorously self-verified file in this batch.

### F1 — Monotonicity check (d) is a weak, redundant confirmation given checks (a)-(c) already pin exact values

- Severity: INFO
- Confidence: HIGH
- Category: test-design observation
- Location/symbol: lines 185-187, `check(top.R > mid.R && mid.R > bottom.R, ...)`
- Evidence: given checks (a), (b), (c) already assert `top≈188`, `mid≈165`, `bottom≈70` (all within a `±12`
  tolerance), monotonicity is arithmetically implied whenever (a)-(c) all pass — check (d) can only usefully
  fail in the narrow case where (a)-(c) each individually pass their own tolerance window in a way that
  still violates strict ordering (e.g. `top=176, mid=177, bottom=58` — all within each check's own ±12
  band of 188/165/70 but not perfectly ordered). This is a real, if narrow, extra discriminating case, so
  it is not purely redundant, but it is far less likely to catch a genuine regression than (a)-(c) already do.
- Why it matters: not a defect — flagged purely as an observation that (d)'s marginal test value is lower
  than its comment (line 183-184) implies ("rules out the pre-fix 'mostly flat' shape"); the (a)-(c) exact
  value checks with `tol=12` already rule that shape out with tighter numerical bounds than a bare ordering
  check could.
- Suggested future action: none needed; this is a defensible belt-and-suspenders addition, not a problem.

## Cross-File Observations

- This is the only file in this 8-file batch that explicitly disables `DirectionalLight0` rather than
  relying on its default-zero diffuse color to achieve the same isolation — see the `_eyeposition_test.cpp`
  and `_fog_test.cpp` reports' F1/Cross-File notes on the implicit-default pattern those files use instead.
  Worth considering back-porting this file's more explicit style to the siblings that rely on the implicit
  default.
- Shares the `check()`/`closeTo()`/`colourMatch()` boilerplate structure with every sibling in this batch,
  but uses a project-specific tighter default tolerance (`tol=12` vs. the more common `tol=20`) — a sensible,
  deliberate per-test choice rather than an inconsistency, given this test's higher required numerical
  precision (see Maintainability above).

## Missing or Weak Tests

- Only samples 3 points along a single vertical line (`x=0`); does not sample any point off that centerline,
  so a bug that only manifests asymmetrically (e.g. a per-fragment computation that happens to reduce to the
  correct per-vertex answer exactly on the vertical centerline by symmetry, but diverges elsewhere) would not
  be caught. Given the geometry's left-right symmetry (`nTop`/`nBottom` are both independent of `x`), this
  specific risk is low, but it is a real, nameable gap.
- Does not test `FresnelFactor` values other than `1.0` (e.g. a very large or very small factor), which would
  exercise the `pow(...)` non-linearity more aggressively and could reveal precision issues at extreme
  exponents.

## Positive Findings

- The single strongest test file in this batch: every expected value is hand-derivable from first
  principles (barycentric coordinates + the Fresnel formula) rather than asserted from a prior "known good"
  run, and this audit's independent re-derivation matched the file's stated numbers exactly for all 3 sample
  points.
- Deliberately engineers a scenario (different per-vertex normals within one triangle) that is the *only*
  way to distinguish per-vertex-interpolated from per-fragment-recomputed Fresnel — a genuinely clever test
  design, not an incidental byproduct of testing something else.
- Explicit isolation of the lighting term (disabling `DirectionalLight0` rather than relying on its default)
  is a more robust pattern than several of its siblings use.

## Final Assessment

An exemplary regression test: the claimed bug (per-fragment vs. per-vertex Fresnel) is real and
distinguishable, the math is independently re-derivable and correct, and the isolation of confounding terms
is more rigorous than most siblings in this batch. No correctness defects found; only a minor test-design
observation (F1, INFO-level) and two narrow, non-blocking coverage gaps.
