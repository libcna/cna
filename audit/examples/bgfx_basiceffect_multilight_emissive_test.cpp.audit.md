# Audit: examples/bgfx_basiceffect_multilight_emissive_test.cpp

## Metadata

- Source file: `examples/bgfx_basiceffect_multilight_emissive_test.cpp`
- Audit status: AUDITED (static; Bgfx is not in the D-P4 opportunistic-build feasibility list for this
  sandbox — no `cmake-build*` directory exists here, so this file is judged by source/cross-file reading only,
  not by executing it)
- Subsystem: `examples-tests-bgfx` shard — `BasicEffect` multi-light + `EmissiveColor` forwarding pixel test
- File type: standalone `Game`-subclass executable, CTest-registered (`cna_bgfx_test(cna_test_bgfx_basiceffect_multilight_emissive …)` / `cna_register_backend_test(NAME Bgfx_BasicEffect_MultiLightEmissive …)`, `cmake/Tests/BgfxTests.cmake:311-314`)
- XNA/FNA relevance: direct — `BasicEffect.DirectionalLight1`/`DirectionalLight2`, `EmissiveColor`, `IEffectLights`
- FNA reference: `HLSL/Lighting.fxh` (`ComputeLights`'s per-light diffuse sum + `+ EmissiveColor` after the
  `DiffuseColor` multiply), `DirectionalLight.cs` (`Enabled` setter zeroes `DiffuseColor`/`SpecularColor`)
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp` (`FillGpuDrawParams()`
  lines 51-141, light1/light2 forwarding lines 93-107), `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`
  (lit-textured draw path lines 2740-2809), `src/CNA/Internal/Backends/Bgfx/shaders/vs_lit_textured3d_vertexlit.sc`
  / `fs_lit_textured3d_vertexlit.sc`

## Purpose

3-check pixel test proving that `BasicEffect::FillGpuDrawParams()` and the Bgfx lit shader actually forward
`DirectionalLight1`/`DirectionalLight2` and `EmissiveColor` on the lit path — per the file's own header, this
was previously missing and was fixed in Task 885 (shared C++ code, so the fix covers EasyGL/Bgfx; Vulkan is
a separate follow-up per the header note). The scene deliberately uses one shared vertex normal
`kNormal(0.8660254, 0, -0.5)` chosen so `dot(-lightDir, N) = 0.5` for all three lights when they share the
same direction `kLightDir(0,0,1)` — a clean half-strength value, not a saturating 0 or 1, so the test can
distinguish "light contributes something" from "light contributes nothing" and from "light is fully on."
Check 1 turns on all three lights (R from L0, G from L1, B from L2, by construction of the three lights'
disjoint-channel `DiffuseColor`s) plus `EmissiveColor`; check 2 disables `DirectionalLight2` (blue channel's
contribution should drop to just emissive); check 3 rotates `DirectionalLight1`'s direction off-axis so its
`NdotL` becomes 0 (green channel's contribution should drop).

## Executive Verdict

**Healthy** — all three numeric expected constants were independently re-derived by this audit from FNA's own
`ComputeLights` formula and matched exactly (to the rounding digit) against the file's own asserted values, and
the underlying `FillGpuDrawParams()`/shader forwarding code was traced end-to-end and found correct. The one
real, evidence-based issue is a **shared, cross-file stale-comment problem** (see F1), present in this file
and 6 of its 7 siblings in this batch: the header's claim that the Bgfx cull-state default is an unfixed,
Bgfx-only quirk is now factually superseded by a later commit already present in this checkout.

## Checklist Results

### API / XNA / FNA parity
`fx.DirectionalLight1`/`fx.DirectionalLight2` (lines 119-125) map directly to FNA's `BasicEffect.DirectionalLight1`/
`DirectionalLight2` (`IEffectLights`). `setEmissiveColorProperty` (line 113) matches `EmissiveColor`. No
`Microsoft::Xna`-facing API misuse found.

### Behavioral correctness
Independently re-derived all three checks from FNA's `Lighting.fxh`:
`ComputeLights`: `result.Diffuse = mul(diffuse, lightDiffuse) * DiffuseColor.rgb + EmissiveColor`. With
`kMaterialDiffuse=(1,1,1)`, `kAmbient=(0.05,0.05,0.05)`, and `NdotL=0.5` for every light sharing `kLightDir`:
- Check 1 (all 3 on): per channel, `ambient + 0.5*L_i.diffuse` where each light's diffuse hits only its own
  channel (`L0=(0.6,0,0)`, `L1=(0,0.6,0)`, `L2=(0,0,0.6)`) → `(0.35,0.35,0.35)`, `+Emissive(0.10,0.05,0.02)`
  → `(0.45,0.40,0.37)` → `×255` → `(114.75, 102, 94.35)` → rounds to **(115,102,94)**, exactly matching
  `kExpectedAllLights`.
- Check 2 (`DirectionalLight2.Enabled=false`): blue channel's `0.5*0.6` term drops → `B=0.05+0.02=0.07*255=17.85`
  → **18**, matching `kExpectedLight2Disabled(115,102,18)` (R/G unaffected, correctly unchanged).
- Check 3 (`DirectionalLight1` rotated to `kLight1DirOffAxis(1,0,0)`): `dot(-{1,0,0}, kNormal)=-0.866`, clamped
  to 0 by the shader's `max(dotL,0.0)` — green channel's `0.5*0.6` term drops → `G=0.05+0.05=0.10*255=25.5`
  → **26**, matching `kExpectedLight1OffAxis(115,26,94)`.
All three independently re-derived values match the file's asserted constants exactly, not just "close within
tolerance" — a materially stronger result than a tolerance-only pass.

### Logic
`renderWith()` (lines 105-150) correctly re-applies all three lights fresh on every call (no state leakage
between the 3 `Draw()` calls sharing one `BasicEffect fx` construction per call — a NEW `BasicEffect fx(dev)`
is constructed inside `renderWith()` each time, lines 107), avoiding any risk of a stale `DirectionalLight2`
state bleeding from check 1 into check 2's disabled-light assertion.

### C++ correctness
`readCenter()`/`matches()`/`closeTo()` mirror the established pattern from every other file in this family;
no UB, no dangling references. `Color` accessors (`getRProperty()` etc.) used consistently.

### Robustness
The retry loop (lines 136-148, `for i<20 … break` on first nonzero pixel) is the same idiom used across this
whole test family; it correctly treats a genuinely-black expected result as a `count==20` full-loop scenario,
which is a non-issue here since none of this file's 3 expected outputs are pure black.

### Testing
Three checks, each isolating a genuinely distinct hypothesis (both-forwarded, L2-disable-gates-diffuse,
L1-uses-its-own-Direction) rather than one generic "lights work" smoke check. Good discriminating design:
each channel of the RGB output is assigned to a different light by construction, so a regression that broke
just `DirectionalLight1` forwarding (leaving `DirectionalLight0`/`DirectionalLight2` fine) would show up as a
green-channel-only failure, not a total failure — genuinely useful for future debugging.

### Cross-file consistency
`FillGpuDrawParams()` (`BasicEffect.cpp` lines 93-107) forwards `light1Dir`/`light1Diffuse`/`light1Specular`
and `light2Dir`/`light2Diffuse`/`light2Specular` unconditionally, gated per-light by `Enabled` (zeroing diffuse
and specular when disabled, matching FNA's `DirectionalLight.Enabled` setter side effect exactly — CNA's
`DirectionalLight` class has no such side effect itself, so `BasicEffect.cpp`'s comment (lines 81-84) correctly
notes the gating must happen at forward time). The Bgfx lit-textured draw path (`BgfxGraphicsBackend.cpp` lines
2740-2809) uploads `u_light1Dir`/`u_light1Diffuse`/`u_light2Dir`/`u_light2Diffuse` and both shader variants
(`fs_lit_textured3d.sc` lines 27-34, `vs_lit_textured3d_vertexlit.sc` lines 45-51) sum all 3 lights' `NdL*Diffuse`
terms — matches FNA's 3-light `ComputeLights(eyeVector, worldNormal, 3)` call for `VSBasicVertexLightingTx`
exactly (not the 1-light `numLights=1` overload). Since this test never sets a per-light `SpecularColor` (all
default to `Vector3::Zero` per `DirectionalLight.hpp`'s uninitialized-to-zero `specularColor_`), there is no
per-vertex-lit-vs-per-pixel-lit divergence risk here despite Task 1104 now defaulting this scene to the
vertex-lit shader (`litTextured3DVertexLitProgram_`, since `PreferPerPixelLighting` is never set) — confirmed
by hand: with all per-light `SpecularColor`s zero, the specular sum is 0 regardless of which shader variant
evaluates it, so both variants would produce byte-identical output for this scene.

## Detailed Findings

### F1 — Header comment's cull-state "not fixed there or here" claim is stale; the underlying default-push bug was actually fixed in a commit already present in this checkout

- Severity: MEDIUM
- Confidence: HIGH (verified via `git log`/`git merge-base --is-ancestor` and by reading the current
  `GraphicsDevice.cpp` and `BgfxGraphicsBackend.hpp` source)
- Category: documentation-accuracy / stale-comment
- Location/symbol: header comment lines 11-16 (`"tracked as Task 896, not fixed there or here"`)
- Evidence: `include/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.hpp:396` still hardcodes
  `uint64_t cullFlags_ = BGFX_STATE_CULL_CCW;` as Bgfx's own internal default, exactly as the comment
  describes. However, `git log --oneline --all | grep "Task 896"` shows commit `b6a00bc6 fix(Task 896): push
  GraphicsDevice's real default RasterizerState to all 3 backends`, and `git merge-base --is-ancestor b6a00bc6
  HEAD` confirms it **is** an ancestor of the currently checked-out commit. Reading `GraphicsDevice.cpp` line
  207 confirms the fix is live: `setRasterizerStateProperty(rasterizerState_);` is called right after
  `createBackend()`/`UpdateViewportFromWindow()` in the constructor, which per `setRasterizerStateProperty`'s
  own implementation (line 1715) pushes `rasterizerState_` (constructed as `RasterizerState::CullCounterClockwise`,
  line 162) down to whichever backend is active — EasyGL and Vulkan included, not just Bgfx. This file's last
  content change (commit `11cdb1f4`, Jul 7 09:56) predates `b6a00bc6` (Jul 7 19:39) by about 10 hours, so the
  comment was accurate when written but has not been revisited since the fix landed.
- Why it matters: the comment currently asserts, as present-tense fact, "Bgfx's default RasterizerState cull
  state... is the only one of the 3 backends that actually matches FNA's real `CullCounterClockwiseFace`
  default" — implying EasyGL/Vulkan still silently default to no-culling and only Bgfx needs the explicit
  `RasterizerState::CullNone` workaround. This is no longer true: after Task 896, all three backends now start
  from the same FNA-correct default (`CullCounterClockwiseFace`), pushed centrally from `GraphicsDevice`'s own
  constructor. The functional workaround in this file (`dev.setRasterizerStateProperty(RasterizerState::CullNone);`,
  line 142) is still both correct and necessary — this quad's winding genuinely needs `CullNone` under the
  real FNA default on every backend now — but the comment's causal story about *why* (a Bgfx-specific
  architectural gap, still open) is now wrong, and a future reader auditing "is Task 896 done yet" from this
  comment alone would be misled into re-investigating a closed issue.
- FNA/XNA comparison: N/A (documentation-accuracy issue, not an XNA/FNA behavior question — the underlying
  cull-mode default itself, `CullCounterClockwiseFace`, was independently confirmed against
  `RasterizerState.cs` lines 125-130 and matches).
- Related files: identical stale claim (with either "Task 884" or "Task 896" as the tracking number) appears
  in all 7 sibling files in this batch — this is a single shared historical artifact, not 8 independent bugs.
- Suggested future action (not implemented by this audit): update this comment block (and its siblings') to
  note that Task 896 (`b6a00bc6`) closed the cross-backend default-push gap, and that the explicit `CullNone`
  call here now documents "this quad's winding needs no-culling under FNA's real default," not "working around
  a Bgfx-only quirk."

## Cross-File Observations

- Shares its stale cull-state comment pattern with all 7 other files in this batch (see F1); this audit
  treats it as one shared finding, reported once per file for the summary's per-file completeness.
- Shares `BasicEffect::FillGpuDrawParams()`'s light-forwarding logic with
  `bgfx_basiceffect_one_light_test.cpp` (1-light path) and `bgfx_basiceffect_specular_test.cpp`/
  `bgfx_basiceffect_preferperpixellighting_test.cpp` (specular path) — all traced consistently correct.
- The task-885 fix this file exists to verify ("DirectionalLight1/2 + EmissiveColor forwarding") was
  corroborated directly in `BasicEffect.cpp` (not merely trusted from the header comment's narrative).

## Missing or Weak Tests

None found specific to this file — the 3 checks cover forwarding, per-light `Enabled` gating, and per-light
`Direction` independence, which is a reasonably complete set for this feature's XNA-facing surface. A test for
`DirectionalLight1`/`DirectionalLight2`'s own `SpecularColor` forwarding (as opposed to `DiffuseColor`) does
not exist in this file, but is implicitly covered by `bgfx_basiceffect_specular_test.cpp`'s single-light
specular checks (`DirectionalLight0` only) — no test in this shard exercises `DirectionalLight1`/`2`'s
specular term specifically, a minor coverage gap worth noting rather than a defect.

## Positive Findings

- All three expected pixel values were independently re-derived from FNA's actual `ComputeLights` formula and
  matched exactly, not merely self-consistently — a materially stronger validation of this test than the
  `easygl_basiceffect_specular_test.cpp` example report's own check (b), which only matched "within tolerance."
- Good discriminating test design: each RGB channel is assigned to a different light so a per-light-specific
  regression produces a per-channel-specific failure, aiding future debugging.
- Constructs a fresh `BasicEffect` per `renderWith()` call, correctly avoiding any risk of light-state leakage
  between the three assertions sharing one `Draw()` frame.

## Final Assessment

A well-constructed, numerically-verified multi-light/emissive forwarding test with no functional defect found.
Its only flaw is a shared, historically-accurate-when-written but now-superseded comment about the Bgfx
cull-state default, which should be refreshed to reflect Task 896's later fix.
