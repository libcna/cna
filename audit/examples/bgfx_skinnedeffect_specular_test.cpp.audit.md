# Audit: examples/bgfx_skinnedeffect_specular_test.cpp

## Metadata

- Source file: `examples/bgfx_skinnedeffect_specular_test.cpp` (214 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `SkinnedEffect` specular-highlight pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_skinnedeffect_specular …)` /
  `cna_register_backend_test(NAME Bgfx_SkinnedEffect_Specular …)`, `cmake/Tests/BgfxTests.cmake:455-459`).
- XNA/FNA relevance: direct — `SkinnedEffect.SpecularColor`/`SpecularPower`/
  `DirectionalLight0.SpecularColor`, half-vector Blinn-Phong specular (`IEffectLights`).
- FNA reference: `HLSL/Lighting.fxh`'s `ComputeLights()` half-vector term, `Common.fxh`'s
  `AddSpecular`, `SkinnedEffect.fx`'s vertex-lit vs. pixel-lit shader split.
- Related production code: `SkinnedEffect.cpp::FillGpuDrawParams()` lines 362-376 (per-light
  specular forwarding, Task 894), `vs_skinned3d_vertexlit.sc`/`fs_skinned3d_vertexlit.sc` (real
  XNA default per-vertex-lit path), `vs_skinned3d.sc`/`fs_skinned3d.sc` (pixel-lit path).

## Purpose

Task 894's 4-check pixel test proving `SkinnedEffect`'s real half-vector Blinn-Phong specular
model: (a) baseline eye position; (b) a different `EyePosition` (via a different camera position)
producing a different specular value, proving eye-dependence; (c) `SpecularColor=(0,0,0)` →
pure diffuse+ambient, proving the material gate; (d) `DirectionalLight0.Enabled=false` → zeroes
*both* diffuse and specular, proving the light's own `Enabled` gate covers specular too. A single
identity bone at 100% weight isolates skinning from the specular formula under test.

## Executive Verdict

**Needs attention** — checks (a)/(c)/(d) are well-designed and their current constants are
plausible for the now-correct per-vertex-lit (Gouraud) code path this scene actually exercises
(`PreferPerPixelLighting` is never set, so it defaults to XNA's real `false`). Check (b), however,
has the **exact same defect** previously found and documented (by this audit) for this project's
sibling `easygl_basiceffect_specular_test.cpp`: its expected constant
(`kExpectedOffAxisEye=68`) is the *stale, pre-Task-1104 always-pixel-lit value* — this audit's own
re-derivation of the *current* vertex-lit (Gouraud) formula for the off-axis-eye scene gives
`≈61`, not `68`; the check only passes because `|61-68|=7` falls within the test's own `±10`
tolerance (see F1).

## Checklist Results

### API / XNA / FNA parity
`setSpecularColorProperty`/`setSpecularPowerProperty`/`DirectionalLight0.setSpecularColorProperty`
map correctly onto FNA's `IEffectLights` surface. `EyePosition` is (correctly) never set directly —
it is derived internally from `View` via `Matrix::Invert(view_).getTranslationProperty()`
(`SkinnedEffect.cpp` line 378-379), matching FNA's own `EyePosition` derivation.

### Behavioral correctness
Re-derived by hand for check (a) (`kEyeStraightOn=(0,0,3)`, `kLightDirRaw=(0.5,0,-1)` normalized,
`N=(0,0,1)`, `SpecularPower=32`): diffuse `= (0.02+0.5*0.8944)*0.4 = 0.18689` (independent of eye
position, matches all 4 checks' shared diffuse term). Per-vertex specular at `TL=(-1,1,0)`:
`h≈(-0.0796,-0.1648,0.9832)`, `dot(h,N)≈0.9832`, `spec≈0.582`; at `BR=(1,-1,0)`: `h≈(-0.3798,
0.1529,0.9125)`, `dot(h,N)≈0.9125`, `spec≈0.0533`. Gouraud average `≈0.3177`; `total≈0.1869+
0.3177=0.5046→×255≈128.7`, rendered constant is `126` — a small (~3-unit) gap this audit attributes
to ordinary hand-arithmetic/GPU-interpolation imprecision (consistent with the same order of
discrepancy already accepted, without being flagged as a defect, for the analogous `BasicEffect`
scene in this project's own prior EasyGL specular-test audit).

Re-derived check (b) (`kEyeOffAxis=(3,0,1)`): per-vertex `E`/`h`/`dot(h,N)` at `TL`: `≈0.8995`,
`spec≈0.0337`; at `BR`: `≈0.9212`, `spec≈0.0723`. Gouraud average spec `≈0.0530`; `total≈0.1869+
0.0530=0.2399→×255≈61.2→61`. The file's own inline comment for this constant reads
`// dotH=0.9239, spec=0.0794` — this audit independently re-derived *that specific* number too, and
it corresponds to a **per-pixel** (not per-vertex/Gouraud) evaluation at the interpolated centre
point: `worldPos_center=(0,0,0)` (midpoint of `TL`/`BR`), `E=normalize((3,0,1))=(0.9487,0,0.3162)`,
`h=normalize(E-lightDir)≈(0.3828,0,0.9241)`, `dot(h,N)≈0.9241` (matches "dotH=0.9239" to within
hand-arithmetic rounding), `spec=pow(0.9241,32)≈0.0799` (matches "spec=0.0794"),
`total≈0.1869+0.0799=0.2668→×255≈68.0` — an exact match for the asserted `68`. In other words: `68`
is the *correct value for the pixel-lit code path*, but this scene (which never sets
`PreferPerPixelLighting`, defaulting to XNA's real vertex-lit `false`) actually renders via the
Gouraud-averaged vertex-lit path, whose genuinely-correct value this audit computed as `≈61`, not
`68`.

### Logic
Check (b)'s own numeric derivation was not updated when the vertex/pixel-lit dispatch split was
introduced (Task 1104) — see F1.

### C++ correctness
`matches()`'s `closeTo(...,10)` tolerance (lines 99-106) is the mechanism that makes check (b) pass
despite the stale constant — identical mechanism to the sibling EasyGL finding this audit
previously documented for `BasicEffect`.

### Robustness
Checks (c)/(d) correctly isolate two independently-failable hypotheses (does the material's own
`SpecularColor` gate the term vs. does a disabled light's own specular get zeroed too) — both are
covered and both were independently re-confirmed correct by this audit via the same diffuse-term
derivation shared across all 4 checks.

### Testing
Three of four checks are strong, evidence-backed pixel assertions with constants this audit
independently re-derived and confirmed correct for the current code path. The fourth (check b) is
present mainly to prove `EyePosition`-dependence via `!matches(b,a)` (line 183), which still holds
(`61`/`68` both clearly differ from `126`) — so the file's overall discriminating goal for check (b)
is not defeated, but the specific `matches(b, kExpectedOffAxisEye)` assertion (line 181-182) is
verifying the wrong number and passing for the wrong reason.

## Detailed Findings

### F1 — Check (b)'s expected constant is the stale pre-Task-1104 pixel-lit value; the check passes only by tolerance overlap, not because the asserted value is correct for the code path this scene actually exercises

- Severity: MEDIUM
- Confidence: HIGH — independently re-derived from first principles (not merely pattern-matched
  against the sibling EasyGL finding), and directly confirmed against this file's own git history:
  commit `0cb4a591` ("feat(Task 1104): Bgfx real per-vertex-lit shader + PreferPerPixelLighting
  dispatch") updated *only* `kExpectedStraightOn` (`155→126`, with an explicit comment documenting
  why), leaving `kExpectedOffAxisEye`'s value (`68`) and its stale `// dotH=0.9239, spec=0.0794`
  comment completely untouched.
- Category: test-coverage / correctness-of-test
- Location/symbol: `kExpectedOffAxisEye(68, 68, 68, 255)` (line 73); check `(b)` (lines 180-183)
- Evidence: `git show 0cb4a591 -- examples/bgfx_skinnedeffect_specular_test.cpp` shows the diff
  touches only the `kExpectedStraightOn` line and its comment; `kExpectedOffAxisEye`'s line and
  comment are unchanged in that diff, meaning the value `68`/`dotH=0.9239` documented alongside it
  is the *pre-Task-1104* per-pixel-lit derivation, exactly as this audit's own independent
  per-pixel re-derivation for the interpolated centre point reproduces (`dotH≈0.9241`,
  `spec≈0.0799`, `total≈68.0`) — while the *actual*, current, correct vertex-lit (Gouraud) value
  this audit computed for the same off-axis-eye scene is `≈61`.
- Why it matters: this check does not actually verify the current per-vertex-lit formula's output
  at the off-axis eye position — it verifies that the output is *within 10* of a different,
  outdated formula's output. A regression that shifted the real per-vertex value from `61` to,
  say, `65` would still pass (coincidentally, not because it is correct); a regression that moved
  it outside `[58,78]` would fail, but for the wrong stated reason (comparing to a value that was
  never the intended target in the first place).
- FNA/XNA comparison: N/A (test-authoring issue, not an XNA/FNA behavior question — the underlying
  vertex-lit `SkinnedEffect` specular behavior itself was independently confirmed correct for this
  scene via check (a)'s re-derivation and via the dedicated
  `bgfx_skinnedeffect_preferperpixellighting_test.cpp` sibling, which explicitly and correctly
  derives both the vertex-lit and pixel-lit values for the *same* straight-on-eye scene).
- Related files: `bgfx_skinnedeffect_preferperpixellighting_test.cpp` (same batch) demonstrates the
  correct methodology for deriving both lighting-mode values side-by-side; this file's check (b)
  simply was not revisited when Task 1104 changed which formula this scene's default configuration
  actually exercises.
- Suggested future action (not implemented by this audit): update `kExpectedOffAxisEye` to the
  currently-correct per-vertex-lit value (`≈61`, ideally re-derived to more decimal precision via
  the project's own established offline-Python-script convention already used for this file's other
  constants) so the check verifies actual current behavior rather than passing by accidental
  tolerance overlap with a superseded formula.

## Cross-File Observations

- This is the **second occurrence** of this exact defect pattern found by this audit across the
  project: the same "one stale off-axis specular constant, left behind after a per-vertex/
  per-pixel dispatch fix updated only the straight-on-eye constant" shape was previously documented
  for `examples/easygl_basiceffect_specular_test.cpp` (a different backend, different effect,
  different task number — Task 1102 there vs. Task 1104 here). This suggests the "update forward"
  step in this family of dispatch-introducing tasks has a systematic blind spot for the *second*
  (off-axis) constant in these specular test files specifically, worth a dedicated sweep across all
  `*_specular_test.cpp` files in this project (EasyGL/Vulkan/D3D9/Bgfx) rather than treating each
  occurrence as an isolated one-off.
- `bgfx_skinnedeffect_preferperpixellighting_test.cpp` (same batch) independently re-derives and
  correctly asserts both the vertex-lit (`125`) and pixel-lit (`155`) values for the *straight-on*
  eye case using the same underlying scene — that file has no equivalent off-axis check, so it does
  not share this specific defect.
- Uses a single Identity bone at 100% weight (`SetBoneTransforms(std::vector<Matrix>{
  Matrix::getIdentityProperty() })`), triggering the same dormant `EffectParameter`-truncation
  behavior documented in `bgfx_skinnedeffect_translation_bone_test.cpp.audit.md` — harmless here
  since all vertex indices are `0`.

## Missing or Weak Tests

See F1 — check (b)'s own expected value should be refreshed to match the currently-computed
per-vertex-lit result rather than the historical per-pixel one.

## Positive Findings

- Checks (a)/(c)/(d) are precise, correctly-scoped, and their numeric derivations were
  independently confirmed by this audit against both FNA's `Lighting.fxh` formula and the live
  shader source.
- `DirectionalLight0.Enabled=false` zeroing *both* diffuse and specular (check d) is a genuinely
  easy-to-miss XNA behavior and this test explicitly isolates it, with a value (`2,2,2`, ambient
  only) this audit confirms is consistent with the shared diffuse-term derivation.

## Final Assessment

A well-designed specular test whose (a)/(c)/(d) checks are solid and independently re-verified by
this audit, undermined by one stale numeric constant (check b) that silently reverted to testing a
superseded (pixel-lit) formula when the per-vertex/pixel-lit dispatch was introduced — passing only
by accidental tolerance overlap, not correctness. This is the second instance of this exact pattern
found across the project by this audit, suggesting it is worth a dedicated cross-backend sweep
rather than a one-off fix.
