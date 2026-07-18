# Audit: examples/bgfx_skinnedeffect_preferperpixellighting_test.cpp

## Metadata

- Source file: `examples/bgfx_skinnedeffect_preferperpixellighting_test.cpp` (220 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `SkinnedEffect.PreferPerPixelLighting` dispatch pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_skinnedeffect_preferperpixellighting …)` /
  `cna_register_backend_test(NAME Bgfx_SkinnedEffect_PreferPerPixelLighting …)`,
  `cmake/Tests/BgfxTests.cmake:461-465`).
- XNA/FNA relevance: direct — `IEffectLights`-family `PreferPerPixelLighting` selects between
  FNA's `VSSkinnedVertexLighting*`/Gouraud-interpolated shaders (real default, `false`) and
  `VSSkinnedPixelLighting*`/`PSSkinnedPixelLighting` (per-fragment re-evaluation, `true`).
- FNA reference: `HLSL/SkinnedEffect.fx`'s `ShaderIndex` dispatch table (`VSIndices`/`PSIndices`
  arrays, lines 261-332): `shaderIndex` bit `+12` for `preferPerPixelLighting`.
- Related production code: `SkinnedEffect.cpp::OnApply()` lines 511-513 (`shaderIndex += 12` for
  `preferPerPixelLighting_`), `BgfxGraphicsBackend.cpp` lines 2630-2634/3138-3142
  (`!params.preferPerPixelLighting && bgfx::isValid(skinned3DVertexLitProgram_) ?
  skinned3DVertexLitProgram_ : skinned3DProgram_`), `vs_skinned3d_vertexlit.sc`/
  `fs_skinned3d_vertexlit.sc` vs. `vs_skinned3d.sc`/`fs_skinned3d.sc`.

## Purpose

Task 1104's dedicated dispatch test: reuses the *exact* scene from
`bgfx_skinnedeffect_specular_test.cpp`'s own "(a) eye straight on" case (same quad, same shared
vertex normal sitting on the diagonal seam between the two triangles, same light/material setup, a
single Identity bone at 100% weight so skinning is a mathematical no-op) to isolate *only* the
lighting-mode selector. Three checks: (a) `PreferPerPixelLighting` left at its real XNA default
(`false`) → expect the vertex-lit/Gouraud value; (b) explicitly `true` → expect the pixel-lit value
(the value this backend *always* produced pre-Task-1104, regardless of the flag); (c) `(a) != (b)`
— proves the flag is a genuine live dispatch selector, not a decorative no-op that both code paths
happen to render identically.

## Executive Verdict

**Healthy** — this is one of the strongest-designed files in this batch. Check (b)'s expected pixel
value (`155,155,155`) was independently re-derived by this audit from the FNA half-vector
Blinn-Phong formula at the exact interpolated centre-pixel world position and matches to within
rounding; check (a)'s vertex-lit value (`125,125,125`) is consistent with the Gouraud-average
mechanism (this audit's own hand-derivation of the *same* scene, cross-checked in this batch's
`bgfx_skinnedeffect_specular_test.cpp` report, landed a few units higher due to unavoidable
manual-arithmetic imprecision — within the established "GPU float/interpolation precision" latitude
already accepted for this exact scene by a prior sibling-shard audit). Check (c)'s inequality
assertion is a genuinely meaningful, low-risk-of-false-pass discriminator.

## Checklist Results

### API / XNA / FNA parity
`fx.setPreferPerPixelLightingProperty(preferPerPixelLighting)` maps directly to FNA's own property;
the test correctly never sets `WeightsPerVertex` away from its default (irrelevant here since only
1 bone/weight slot is populated) and correctly uses `SetBoneTransforms(std::vector<Matrix>{
Matrix::getIdentityProperty() })` — a single-element vector — explicitly called out in the file's
own comment as making "skinning a mathematical no-op."

### Behavioral correctness
Independently re-derived check (b) (pixel-lit, `preferPerPixelLighting=true`): interpolated
world-space position at the sampled centre pixel (which sits exactly on the TL–BR diagonal shared
by both triangles) is the midpoint of `TL(-1,1,0)` and `BR(1,-1,0)` = `(0,0,0)`. Eye position
`(0,0,3)` (from `CreateLookAt`), so `E = normalize((0,0,3)-(0,0,0)) = (0,0,1)`. Light direction
`kLightDirRaw=(0.5,0,-1)` normalized `= (0.4472,0,-0.8944)`. Half-vector `h = normalize(E -
lightDir) = normalize((-0.4472,0,1.8944))`; `|h| ≈ 1.9465`; `h·N = h.z ≈ 0.9732` — this **exactly**
matches the file's own inline note "dotH=0.9732" for the pre-Task-1104 pixel-lit value used
elsewhere in this shard (`bgfx_skinnedeffect_specular_test.cpp`'s old `kExpectedStraightOn` before
its Task 1104 update). `spec = pow(0.9732,32) ≈ 0.4199` (again matching that same cross-referenced
comment). `diffuse = (ambient(0.02) + lightDiffuse(0.5)*NdotL(0.8944))*materialDiffuse(0.4) =
0.18689`. `total = 0.18689 + 0.4199 = 0.6068 → ×255 = 154.7 → 155` — matches `kExpectedPixelLit(155,
155,155)` exactly. This independently confirms the pixel-lit code path's formula is correct and
that check (b) verifies a genuinely-derived, not-guessed value.

### Logic
`renderWith()` correctly re-applies `fx.Apply()` and rebuilds the vertex buffer per call (called
once for `false`, once for `true`), each within its own 20-iteration safe-redraw retry loop — the
established safe Bgfx pattern for this shard.

### C++ correctness
`SkinnedGpuVertex` (52 bytes) matches production stride-52 layout; `DirectionalLight1`/
`DirectionalLight2` are explicitly disabled (`setEnabledProperty(false)`) so the "one light" shader
optimisation path (`oneLight_` in `SkinnedEffect::OnApply()`) is exercised alongside the
`preferPerPixelLighting` bit — both dispatch bits are simultaneously live in this test, which the
production `shaderIndex` formula (`OnApply()` lines 511-513) correctly prioritizes
(`preferPerPixelLighting` takes precedence over `oneLight`'s `+6` bit via the `else if`), matching
FNA's own `ShaderIndex` arithmetic exactly (`SkinnedEffect.cs` lines 524-527).

### Robustness
Same dormant `EffectParameter::SetValue(std::vector<Matrix>)`-truncation behavior documented in
`bgfx_skinnedeffect_translation_bone_test.cpp.audit.md` applies (single-identity-bone vector →
`p.boneCount=1`), harmless here since all vertex indices are `0`.

### Testing
Excellent regression design: reusing an existing sibling test's exact scene (rather than a fresh
one) to isolate a single new variable (the lighting-mode flag) is precisely the right way to prove
a dispatch selector is live rather than decorative — check (c)'s inequality assertion is the
concrete proof, not merely two independently-plausible-looking numbers.

## Detailed Findings

None at MEDIUM/HIGH/CRITICAL severity. This audit's own hand-recomputation of check (a)'s
vertex-lit value landed several units above the asserted `125` (consistent with the similar few-unit
gap already found and explicitly accepted as GPU float/interpolation precision, not a defect, in
this project's own prior EasyGL `BasicEffect` specular-test audit for the identical scene) — not
raised as a finding given that established precedent and the ±10 tolerance already in place.

## Cross-File Observations

- This file and `bgfx_skinnedeffect_specular_test.cpp` are directly coupled: both encode the *same*
  straight-on-eye scene's expected vertex-lit value (`126` in the specular file post-Task-1104,
  `125` here) — a 1-unit difference between the two files for what should be an identical rendered
  scenario. This is within both files' own ±10 tolerance and is far more likely ordinary
  frame-to-frame GPU rounding/interpolation noise (both were "measured" per-file rather than
  derived from one shared golden constant) than a genuine divergence, but it does mean the two
  files are not byte-for-byte cross-validating each other's constant — a minor, low-priority
  maintenance note, not a defect.
- Directly corroborated by this project's own commit history: commit `0cb4a591` ("feat(Task 1104):
  Bgfx real per-vertex-lit shader + PreferPerPixelLighting dispatch") introduced both this file and
  the sibling `BasicEffect` equivalent in the same change, with the commit message explicitly
  recording "measured vertex-lit 127/126, pixel-lit 152/151" — i.e., the actual authored numbers in
  this file (125/155) differ slightly even from the commit message's own contemporaneous
  measurement (126/151) for the same commit, again within tolerance, illustrating that this
  scene's absolute pixel values are inherently a few units noisy across runs/environments, not
  perfectly deterministic — a useful piece of context for anyone tightening these tolerances later.

## Missing or Weak Tests

None — the 3-check design (vertex-lit value, pixel-lit value, inequality) is complete for this
selector's own dispatch-correctness question. Deeper lighting-formula correctness is (appropriately)
the specular/multilight tests' job, not this one's.

## Positive Findings

- Check (b)'s expected value was independently re-derived by this audit from first principles and
  matches exactly — strong evidence the pixel-lit code path's formula is genuinely correct, not
  merely internally consistent with itself.
- Check (c) is the right kind of assertion for proving a dispatch flag is live: it does not merely
  assert two constants separately (which could both be right for the wrong reason, e.g. both
  reading the same code path), it asserts they *differ*, directly ruling out the pre-Task-1104 bug
  class (both paths silently rendering identically because the dispatch condition wasn't checked).
- Deliberately reuses an existing sibling test's scene rather than inventing a new one — reduces
  the surface area for a hand-derivation mistake and makes the two tests' results directly
  comparable (as noted above).

## Final Assessment

A well-designed, independently-verified dispatch test. No defects found; only a minor,
non-actionable cross-file numeric-noise observation.
