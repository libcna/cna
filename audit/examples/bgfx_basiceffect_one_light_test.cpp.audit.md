# Audit: examples/bgfx_basiceffect_one_light_test.cpp

## Metadata

- Source file: `examples/bgfx_basiceffect_one_light_test.cpp`
- Audit status: AUDITED (static; Bgfx is not in the D-P4 opportunistic-build feasibility list for this
  sandbox — no `cmake-build*` directory exists here)
- Subsystem: `examples-tests-bgfx` shard — `BasicEffect` single-`DirectionalLight` pixel test
- File type: standalone `Game`-subclass executable, CTest-registered (`cna_bgfx_test(cna_test_bgfx_basiceffect_one_light …)` / `cna_register_backend_test(NAME Bgfx_BasicEffect_OneLight …)`, `cmake/Tests/BgfxTests.cmake:305-308`)
- XNA/FNA relevance: direct — `BasicEffect.LightingEnabled`, `DirectionalLight0.Enabled`/`DiffuseColor`, the
  ambient+diffuse lighting sum
- FNA reference: `HLSL/Lighting.fxh` (`ComputeLights`'s `zeroL*dotL` clamp for back-facing normals),
  `DirectionalLight.cs` (`Enabled` setter zeroes `DiffuseColor`/`SpecularColor` at the GPU-parameter level)
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp` (`FillGpuDrawParams()`
  lines 81-91), `src/CNA/Internal/Backends/Bgfx/shaders/fs_lit_textured3d.sc` (lines 30, 33-35)

## Purpose

4-check pixel test proving `BasicEffect`'s single-light diffuse lighting model: (1) a non-saturating
`NdotL=0.5` value produces `(Ambient+0.5*LightDiffuse)*MaterialDiffuse` (proving the dot product is a real
scalar, not a boolean on/off gate — explicitly cross-checked against both a fully-saturated `NdotL=1` value
and an ambient-dropped value to rule out those two specific wrong implementations), (2) a back-facing normal
(`kNormalBackFacing`, the negation of `kNormalLit`) clamps the negative dot product to 0, leaving ambient-only,
(3) `DirectionalLight0.Enabled=false` also produces ambient-only. Per the file's own header, this is the Bgfx
counterpart of the EasyGL test that originally found and fixed the `DirectionalLight0.Enabled` bug in the
shared `FillGpuDrawParams()` (common C++ code, so one fix covers all 3 backends).

## Executive Verdict

**Healthy** — all four numeric expectations were independently re-derived from FNA's lighting formula and
matched exactly; the negative-assertion checks (`!matches(litGot, kFullySaturated)`,
`!matches(litGot, kAmbientIgnored)`) are a genuinely good test-design choice that rules out two specific wrong
implementations, not just "any value different from zero." The file shares the batch's stale cull-state
comment (F1).

## Checklist Results

### API / XNA / FNA parity
`fx.DirectionalLight0.setEnabledProperty(light0Enabled)` (line 104) / `setDirectionProperty`/
`setDiffuseColorProperty` map directly to FNA's `DirectionalLight` property surface. `LightingEnabled` (line
101) matches `IEffectLights.LightingEnabled`.

### Behavioral correctness
Re-derived all four values: `kAmbient=(0.1,0.1,0.1)`, `kMaterialDiffuse=(0.5,0.5,1.0)`,
`kLightDiffuse=(1.0,0.6,0.2)`, `kNormalLit=(0.8660254,0,-0.5)`, `kLightDir=(0,0,1)`.
`NdotL = dot(-lightDir, normal) = dot((0,0,-1),(0.866,0,-0.5)) = 0.5`, matching the file's own comment.
- `kExpectedLit`: `R=(0.1+0.5*1.0)*0.5=0.3→76.5→77`; `G=(0.1+0.5*0.6)*0.5=0.2→51`;
  `B=(0.1+0.5*0.2)*1.0=0.2→51` → **(77,51,51)**, exact match.
- `kFullySaturated` (hypothetical `NdotL=1`, used only as a negative check, never actually rendered):
  `R=(0.1+1.0)*0.5=0.55→140.25→140`; `G=(0.1+0.6)*0.5=0.35→89.25→89`; `B=(0.1+0.2)*1.0=0.3→76.5→77` →
  **(140,89,77)**, matches the file's own constant — confirms the negative check
  (`!matches(litGot, kFullySaturated)`) is comparing against a value that itself was correctly derived, not
  an arbitrary "obviously wrong" placeholder.
- `kAmbientIgnored` (hypothetical "ambient term dropped", also a negative-check-only constant):
  `R=0.5*1.0*0.5=0.25→63.75→64`; `G=0.5*0.6*0.5=0.15→38.25→38`; `B=0.5*0.2*1.0=0.1→25.5→26` →
  **(64,38,26)**, matches.
- `kExpectedAmbientOnly` (used for both the back-facing-normal and the `Enabled=false` cases):
  `R=0.1*0.5=0.05→12.75→13`; `G=0.1*0.5=0.05→13`; `B=0.1*1.0=0.1→25.5→26` → **(13,13,26)**, matches exactly.
All four independently-derived values match the file's asserted constants exactly.

### Logic
`renderWith()` (lines 96-131) parameterizes both `normal` and `light0Enabled`, letting a single helper cover
all three scene variants (lit / back-facing / disabled) cleanly without duplicated draw-loop code.

### C++ correctness
No issues found; consistent with the established pattern across this test family.

### Robustness
The two negative checks (`!matches(litGot, kFullySaturated)`, `!matches(litGot, kAmbientIgnored)`) are the
standout design feature of this file: they don't just assert "the pixel is approximately X," they actively
rule out two specific, plausible wrong implementations (saturating the dot product to a boolean; dropping the
additive ambient term) that a regression could independently introduce. This is a stronger test than a bare
positive assertion alone would be, since a regression that happened to produce a value close to 77 for the
wrong reason (e.g., coincidentally landing near 77 via a saturated dot-product bug) would still be caught if
it also drifted `kFullySaturated`'s or `kAmbientIgnored`'s own predicted values — though in practice here the
three constants (77 vs 140 vs 64) are different enough that this scenario is unlikely to matter in practice;
it is still good defensive test design.

### Testing
4 checks covering: partial-lit (non-saturating), back-facing-normal clamp, and `Enabled=false` gating — a
solid, non-redundant set for the single-light diffuse feature this file is named for.

### Cross-file consistency
`FillGpuDrawParams()` lines 85-87 (`BasicEffect.cpp`): `light0On ? DirectionalLight0.getDiffuseColorProperty()
: Vector3::Zero` — correctly reproduces FNA's `DirectionalLight.Enabled` setter's side effect (zeroing the
GPU-facing diffuse/specular parameters) at the point CNA's own `DirectionalLight` class (which has no such
side effect per the code comment) is read, rather than at assignment time — an intentional, documented
architectural deviation from FNA's push-based model, and this test correctly proves the net *effect* is
equivalent regardless of *when* the zeroing happens.

## Detailed Findings

### F1 — Header comment's cull-state "not fixed there or here" claim is stale (shared with 7 sibling files)

- Severity: MEDIUM
- Confidence: HIGH
- Category: documentation-accuracy / stale-comment
- Location/symbol: header comment lines 9-13 (`"tracked as Task 884, not fixed there or here"`)
- Evidence: this file uses the tracking number **884**, not 896 — and `git log --oneline --all | grep "Task
  884"` shows the real Task 884 is `75aefb7b fix(Task 884): EffectParameterCollection/EffectPassCollection
  dangling-pointer hazard`, an entirely unrelated fix (not the cull-state default-push issue this comment
  describes). The actual fix for the cull-state issue landed later as Task 896 (`b6a00bc6`, confirmed via
  `git merge-base --is-ancestor` to be an ancestor of the current `HEAD`), and `GraphicsDevice.cpp` line 207
  confirms it is live in this checkout (`setRasterizerStateProperty(rasterizerState_)` in the constructor,
  pushing FNA's real `CullCounterClockwiseFace` default to all three backends). This file's last content
  change is commit `0ad49974` (Jul 6 19:12), predating `b6a00bc6` (Jul 7 19:39) by about a day.
- Why it matters: two separate accuracy problems compound here — (1) the task number **884** cited never
  referred to this bug at all (a documentation cross-reference error present from this file's authoring,
  independent of any later fix), and (2) even setting the wrong number aside, the underlying claim that this
  is an unaddressed, Bgfx-only architecture gap is now stale, since Task 896 closed it for all three backends.
  The explicit `RasterizerState::CullNone` workaround (line 123) remains correct and necessary regardless.
- FNA/XNA comparison: N/A (documentation-accuracy, not a behavior question).
- Related files: the wrong-task-number variant of this comment ("tracked as Task 884") also appears in
  `bgfx_basiceffect_texture_enabled_test.cpp` and `bgfx_basiceffect_texture_vertexcolor_enabled_test.cpp`
  (and originates in `bgfx_basiceffect_vertexcolor_disabled_test.cpp`, which coined "tracked as new Task 884");
  the corrected-number variant ("tracked as Task 896") appears in the other 4 files in this batch. Both
  variants are equally stale as to "fixed" status.
- Suggested future action (not implemented by this audit): correct the task-number cross-reference and note
  Task 896 (`b6a00bc6`) as the actual closing commit.

## Cross-File Observations

- Along with `bgfx_basiceffect_texture_enabled_test.cpp` and `bgfx_basiceffect_texture_vertexcolor_enabled_test.cpp`,
  this file is one of three in the batch citing the incorrect "Task 884" tracking number (see F1) rather than
  the later-corrected "Task 896."
- Shares its lighting-formula validation approach (independently re-derived, exact-match constants) with
  `bgfx_basiceffect_multilight_emissive_test.cpp` — both hold up well under hand recomputation.

## Missing or Weak Tests

None found specific to this file. A one-light `SpecularColor`/`Enabled` interaction test does not exist here
(this file only exercises diffuse), but that gap is covered by `bgfx_basiceffect_specular_test.cpp`'s own
check (d), so it is not a missing capability for the shard as a whole.

## Positive Findings

- All four numeric constants (one positive-expected, two negative-check-only, one shared ambient-only value)
  were independently re-derived and matched exactly — a thorough, hand-verifiable test.
- The two negative checks against specific plausible-wrong-implementation values (saturated dot product;
  dropped ambient term) are a notably stronger test-design pattern than a bare positive assertion.

## Final Assessment

A correct, well-designed single-light diffuse test with no functional defect found. Its only issues are
documentation accuracy: an incorrect task-number cross-reference from its original authoring (F1), compounded
by the same shared staleness (Task 896 having since closed the underlying issue) as the rest of this batch.
