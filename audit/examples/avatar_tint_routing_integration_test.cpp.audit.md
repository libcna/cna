# Audit: examples/avatar_tint_routing_integration_test.cpp

## Metadata

- Source file: `examples/avatar_tint_routing_integration_test.cpp`
- Audit status: AUDITED — **empirically built and executed on both registered backends
  during this audit** (EasyGL and Vulkan), not just statically reviewed. This is the
  centerpiece file of this batch: a genuinely cross-backend regression test whose two
  registered runs were found to produce materially different pixel output on this exact
  scenario, for two independent, verifiable reasons traced to production code on each side.
- Subsystem: `examples-tests-generic` shard — `AvatarRenderer::PartTintEXT` per-part tint
  routing regression test (Task 11.24).
- File type: standalone `Game`-subclass executable, CTest-registered for **two** backends
  from the same unmodified source: `cna_easygl_test(cna_test_avatar_tint_routing …)` /
  `EasyGL_AvatarRenderer_TintRouting` (`cmake/Tests/EasyGLTests.cmake:253-258`, gated on
  `CNA_ENABLE_NET`) and `cna_vulkan_test(cna_test_vulkan_avatar_tint_routing …)` /
  `Vulkan_AvatarRenderer_TintRouting` (`cmake/Tests/VulkanTests.cmake:715-719`).
- XNA/FNA relevance: NOXNA — `AvatarRenderer::PartTintEXT`/`AvatarAppearanceEXT` are CNA
  extensions; the underlying `SkinnedEffect` lighting math they drive is real XNA/FNA
  behavior (`StockEffects/EffectHelpers.cs`'s `SetMaterialColor`).
- Related production code: `src/Microsoft/Xna/Framework/GamerServices/AvatarRenderer.cpp`
  (`PartTintEXT` lines 169-176, `DrawRealEXT` lines 178-228), `src/Microsoft/Xna/Framework/
  Graphics/SkinnedEffect.cpp` (`FillGpuDrawParams` lines 320-404, esp. 335-338),
  `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp` (`EnsureSkinnedVertexLitProgram`
  shader, lines 3445-3525), `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp`
  (`FillExtPushConst` lines 3575-3592), `src/CNA/Internal/Backends/Vulkan/shaders/
  skinned3d_vertexlit.vert.glsl`.

## Purpose

Builds two single-bone quads ("CNAAvatarHair"/"CNAAvatarShirt", both textured pure white so
tint alone determines rendered color), sets a non-default `AvatarAppearanceEXT` with distinct
`HairColor`/`ShirtColor`, draws through `AvatarRenderer::DrawRealEXT`, and checks each part's
rendered pixel is close (±20 tolerance) to its expected appearance tint. Its stated purpose
(per its own header comment) is a regression guard against the historical Task-11.17 bug
where `PartTintEXT` matched part names by exact equality against a lowercase literal
(`"hair"`), which never matched real content-pipeline names (`"CNAAvatarHair"`) — a real,
previously-shipped defect this file is explicitly designed to catch automatically.

## Checklist Results

### API / XNA / FNA parity, Behavioral correctness — the routing logic itself
Independently confirmed `AvatarRenderer::PartTintEXT()`'s **current** implementation
(`AvatarRenderer.cpp` lines 169-176) uses `partName.find("Hair") != std::string::npos` /
`"Shirt"` / `"Pants"` / `"Shoes"` substring matching — correctly matching `"CNAAvatarHair"`/
`"CNAAvatarShirt"`, and correctly *not* the pre-Task-11.17 exact-lowercase-equality bug this
file's header describes. This specific claim (the one the file's name and header comment
center on) is genuinely true of the current code and is a real, working regression guard for
that historical class of bug — confirmed by direct reading, not merely by the file's own
comment.

### Behavioral correctness — the test's own expected values vs. what the code actually renders (empirically verified)

This is the substantive finding of this report. Built and ran both CTest-registered variants
of this exact file against the current `develop`/`feature/audit` source tree in this sandbox:

```
$ ./cmake-build-tests/cna_test_avatar_tint_routing         # EasyGL
[FAIL] AvatarTintRoutingIntegration: left=(81,51,31) right=(41,181,255)
       expected: left=HairColor(40,25,15), right=ShirtColor(20,90,155)
$ ctest -R AvatarRenderer_TintRouting   →  35 - EasyGL_AvatarRenderer_TintRouting (Failed)

$ ./cmake-build-vulkan/cna_test_vulkan_avatar_tint_routing  # Vulkan
[PASS] AvatarTintRoutingIntegration: left=(41,26,16) right=(21,91,156)
$ ctest -R AvatarRenderer_TintRouting   →  108 - Vulkan_AvatarRenderer_TintRouting (Passed)
```

**`EasyGL_AvatarRenderer_TintRouting` is a currently-failing CTest with no `WILL_FAIL`/skip
annotation** (confirmed via `cmake/TestHelpers.cmake`'s `cna_register_backend_test()` — no
such property is ever set for this test). This is not a flaky/environment-specific failure:
it is a deterministic, reproducible consequence of this scene's own chosen lighting
parameters, traced as follows.

`AvatarRenderer::DrawRealEXT()` (lines 198-210) calls `realEffect_->EnableDefaultLighting()`
— which sets FNA's real canonical three-point rig on **all three** lights (`SkinnedEffect.cpp`
lines 222-240: key/fill/back, all `Enabled=true`) — and only *afterward* overrides
`AmbientLightColor` (to this test's `Vector3(1,1,1)`) and `DirectionalLight0`'s
`Direction`/`DiffuseColor` (to `(0,0,-1)`/`(1,1,1)`). `DirectionalLight1`/`DirectionalLight2`
are never disabled by this test or by `AvatarRenderer`, so FNA's fill/back-light defaults
(directions `(0.7198,0.342,0.604)`/`(0.4545,-0.766,0.4545)`, both with positive Z) remain
active; for this scene's flat, camera-facing normal `(0,0,1)`, both produce `dot(N,-Direction)
< 0` and are clamped to zero contribution — so only the overridden `DirectionalLight0`
actually contributes diffuse light here (`lightSum = (1,1,1)`).

FNA's own documented lighting formula (`EffectHelpers.cs` lines 190-192, verified against
this exact source): *`((AmbientLightColor + sum(diffuse directional light)) * DiffuseColor) +
EmissiveColor`*. With `AmbientLightColor=(1,1,1)`, `lightSum=(1,1,1)`, `EmissiveColor=0`
(never set), the FNA-correct rendered value for the (white-textured) hair quad is
`hairColor * (ambient + lightSum) = hairColor * 2`. For `hairColor=(40,25,15)`, that predicts
`≈(80,50,30)` — matching the **empirically measured EasyGL output, `(81,51,31)`, almost
exactly** (the 1-unit residual is ordinary GPU float/interpolation rounding). For
`shirtColor=(20,90,155)`, the same formula predicts `≈(40,180,255-clamped)` — again matching
the **measured `(41,181,255)` almost exactly**. **EasyGL's rendering is FNA-formula-correct**;
it is this test's own `ColorCloseTo(…, 20)` assertion against the *raw, unscaled* appearance
tint that is checking the wrong number, given the scene's own chosen (fully-saturated
ambient **and** fully-saturated single-light) parameters double the intensity by design of
the formula, not by any bug.

Vulkan's measured output (`(41,26,16)`/`(21,91,156)`) is instead almost exactly the **raw,
un-doubled** appearance tint — because of a *second*, independently-confirmed defect:
`SkinnedEffect::FillGpuDrawParams()` never populates `GpuDrawParams::ambientColor` at all (it
only folds `ambientLightColor_ * diffuseColor_` into `p.emissiveColor`, per its own comment at
lines 335-338 — a valid *EasyGL-side* representation, since EasyGL's shader adds
`uEmissiveColor` as a separate term: `vLitRGB = lightSum*uDiffuseColor.rgb + uEmissiveColor`,
`EasyGLGraphicsBackend.cpp` line 3520), while `VulkanGraphicsBackend::FillExtPushConst()`
(used for the skinned/ext draw path, confirmed via call sites at `VulkanGraphicsBackend.cpp`
lines 7386/7624) forwards `p.ambientColor` (which stays `{0,0,0}` for every `SkinnedEffect`
draw, since it's never written) into the shader's `pc.ambientColor` push-constant field, and
has **no push-constant slot for `p.emissiveColor` at all** in its 32-float `PC` layout
(confirmed by reading `skinned3d_vertexlit.vert.glsl`'s `PC` struct, which has no
`emissiveColor` field). The net effect: on Vulkan, `AmbientLightColor` (and, by the same
mechanism, `EmissiveColor`) is a **complete, silent no-op** for `SkinnedEffect`/
`AvatarRenderer` real-rendering draws — this audit's independent code trace matches, and
extends to this exact file, the identical defect already confirmed by three prior
`vulkan_skinnedeffect_*` audits in this project (see
`vulkan_skinnedeffect_vertexcolor_test.cpp.audit.md` F1, `_preferperpixellighting_test.cpp`
F1, and `AUDIT_CROSS_CUTTING_FINDINGS.md`'s "Systematic FNA parity gaps" section).

**The two facts combine into a genuine, empirically-observed cross-backend divergence**: this
exact scene, run through the exact same source file and appearance/light parameters, produces
`~2×`-too-bright, FNA-formula-*correct* output on EasyGL (which the test's own `±20` tolerance
rejects, since the test was evidently authored/tuned without accounting for
`AmbientLightColor`'s multiplicative interaction with `DiffuseColor`), and produces output
close to the *raw* appearance tint on Vulkan (which the test's tolerance accepts) — **not
because Vulkan is more correct, but because Vulkan's own separate, independently-confirmed
ambient-forwarding bug happens to cancel out this test's own lighting-magnitude miscalibration
by coincidence.** Neither backend's "pass"/"fail" outcome for this specific test is currently
trustworthy evidence of `AvatarRenderer`/`SkinnedEffect` tint-routing correctness in isolation
from these two compounding issues.

### Logic
The routing logic itself (`PartTintEXT`'s substring match) is correct and is not implicated in
either backend's divergence — both backends compute `diffuseColor_ = hairColor`/`shirtColor`
correctly per part; the divergence is purely in how much *lighting* scales that base color,
not in *which* color is selected.

### Robustness
The `±20` tolerance (with the file's own comment claiming "observed up to ~17 on one
channel") does not match this audit's empirical measurement on EasyGL (actual deviation up to
41 on the R channel for hair, up to 100 on the B channel for shirt, both far exceeding the
claimed "~17") — see F1.

### Testing
This file is itself test code; its core positive property (guards against the Task-11.17
exact-match regression) is real and independently confirmed. Its numeric tolerance/expected
values, however, do not currently hold up under direct execution on the backend (EasyGL) this
audit could most readily build and run — this is the central, actionable finding.

## Detailed Findings

### F1 — `EasyGL_AvatarRenderer_TintRouting` is a currently-failing CTest (confirmed by direct execution), because the test's own tolerance was tuned against an unaccounted-for lighting-magnitude assumption

- Severity: HIGH
- Confidence: HIGH (directly built and executed both CTest binaries in this sandbox during
  this audit; exit codes, printed pixel values, and `ctest -R` pass/fail status all confirmed
  first-hand, not inferred)
- Category: correctness — test currently red, unguarded by any expected-failure annotation
- Location/symbol: `ColorCloseTo(leftPx, hairColor, 20)` / `ColorCloseTo(rightPx, shirtColor,
  20)` (lines 161-162); header comment lines 157-160 claiming "observed up to ~17 on one
  channel"; `AvatarRenderer::DrawRealEXT()`'s `EnableDefaultLighting()` +
  `AmbientLightColor=(1,1,1)` + single fully-saturated `LightColor=(1,1,1)` combination
  (`AvatarRenderer.cpp` lines 198-210; this test's own `Draw()`, lines 146-148)
- Evidence: see the full derivation and empirical transcript above. Both the EasyGL failure
  and the Vulkan pass were reproduced directly in this sandbox; the FNA-formula prediction
  (`EffectHelpers.cs` lines 190-192) matches the EasyGL measurement to within 1 unit per
  channel, and the "ambient entirely dropped" prediction matches the Vulkan measurement
  equally closely.
- Why it matters: `EasyGL_AvatarRenderer_TintRouting` is registered as an ordinary
  (expected-to-pass) CTest and is failing right now on this codebase's own `feature/audit`
  checkout — a genuine, previously-unreported red test, not a hypothetical risk. Per this
  project's own documented "CI-masking risk" pattern (`AUDIT_CROSS_CUTTING_FINDINGS.md`,
  citing two prior instances in Bgfx), this is now a **third confirmed instance** of a
  registered-but-currently-failing test with no `WILL_FAIL` annotation. Additionally, the
  Vulkan variant's "pass" is not reassuring: it passes *because of*, not despite, a separate
  confirmed production defect (ambient/emissive dropped for `SkinnedEffect` on Vulkan) — so
  neither backend's current CTest result for this file should be read as validating
  `AvatarRenderer`'s tint-routing-plus-lighting behavior end-to-end.
- FNA/XNA comparison: EasyGL's actual rendered output is the one that is FNA-formula-correct
  (`EffectHelpers.cs`'s documented ambient/diffuse-color multiplicative model); the test's own
  expected/tolerance values are the ones that need correcting, not the EasyGL rendering
  path — this is the reverse of the usual "backend is wrong, test correctly caught it" shape,
  and is worth stating explicitly so a future fix doesn't "fix" EasyGL's (correct) lighting
  math to chase this test's (miscalibrated) numbers instead.
- Suggested future action (not implemented by this audit, per the audit-only scope of this
  task): re-tune this file's expected/tolerance values to account for the real
  `(AmbientLightColor + lightSum) * DiffuseColor` formula (i.e. expect roughly double the raw
  appearance tint, clamped, given this scene's specific `Ambient=White`/`Light0=White`
  choice) — or, more robustly, choose less-saturated `AmbientLightColor`/`LightColor` values
  in the test itself so the expected value stays comfortably below 255 and isn't sensitive to
  clamping (the shirt-blue channel currently saturates to 255 on EasyGL, discarding
  information). Separately (already tracked by three prior audits in this project, not new
  to this report but now further corroborated), fix `SkinnedEffect::FillGpuDrawParams()`/
  `VulkanGraphicsBackend::FillExtPushConst()` so `AmbientLightColor`/`EmissiveColor` actually
  reach Vulkan's skinned pipeline — after which this test's Vulkan variant would very likely
  start failing too, for the same (currently EasyGL-only-visible) reason, unless the
  tolerance/expected-value fix above is applied first or at the same time.

## Cross-File Observations

- This file, together with `avatar_real_render_integration_test.cpp` and
  `avatar_attach_part_integration_test.cpp` (both this batch), are the only three files in the
  entire `examples/` tree registered against two different graphics backends from one
  unmodified source. This file is the only one of the three whose assertion shape (white
  texture, tight numeric tolerance against an exact appearance color) is actually sensitive
  enough to reveal the cross-backend divergence the other two structurally cannot detect (see
  both of their own audit reports' F1/Robustness sections) — direct empirical confirmation of
  the value this audit's own task framing anticipated ("cross-backend test files … may be
  uniquely positioned to show backend-to-backend divergence").
- Directly extends (with a concrete, executable, currently-reproducible example) the
  `AUDIT_CROSS_CUTTING_FINDINGS.md` entry: *"NEW, Vulkan-specific: `SkinnedEffect::FillGpuDrawParams()`
  never sets `ambientColor`, and Vulkan's skinned shaders never consume `emissiveColor`… so
  `AmbientLightColor`/`EmissiveColor` are silently no-ops for skinned models on Vulkan
  specifically."* Prior confirmations of that entry (three `vulkan_skinnedeffect_*.cpp` audits)
  all involved a test that had **already routed around the bug** (deliberately setting
  `AmbientLightColor=0`, per that finding's own text: *"this file's own author was evidently
  already aware this exact code path was suspect and chose to route around it"*). This file
  is the **first confirmed case in this project's audit where a test does NOT route around
  the bug** — and the result is a test that passes on Vulkan for the wrong reason, rather
  than a test that simply never exercises the buggy path.

## Missing or Weak Tests

- See F1 in full — the test's tolerance/expected-value calibration does not match either
  backend's actual current behavior in a way that would meaningfully catch a *routing*
  regression (the property this file's name and header comment claim to guard) independent of
  the *lighting-magnitude* question the file inadvertently also depends on.
- No variant of this scenario isolates "does tint routing send the right color" from "is the
  lighting magnitude correct" — e.g. a scene with `AmbientLightColor=(1,1,1)`,
  `LightColor=(0,0,0)` (light fully off) would make the expected value exactly the raw
  appearance tint on a correctly-implemented backend, without the ×2 ambiguity this scene's
  parameter choice introduces, and would still fully exercise `PartTintEXT`'s substring
  routing.

## Positive Findings

- The core regression this file's name and header comment describe (Task-11.17's
  exact-lowercase-match bug) is real, historically accurate, and the current
  `PartTintEXT()` implementation genuinely does not have that specific defect — confirmed by
  direct reading.
- The file's own header comment's honesty about tolerance ("Tolerance of 20 absorbs ordinary
  shading/rounding noise (observed up to ~17 on one channel)") is a good practice in
  principle (see the template audit's praise for the same habit in
  `easygl_basiceffect_specular_test.cpp`) — the actual numbers no longer match current
  behavior (see F1), but the intent to document the tolerance's basis, rather than picking an
  unexplained round number, is sound authoring practice worth preserving once corrected.
- Using an all-white texture to isolate tint from any texture contribution is the correct
  test-design choice for what this file is trying to prove.

## Final Assessment

Significant correctness risk. This audit's own empirical build-and-run (not just static
reading) confirmed `EasyGL_AvatarRenderer_TintRouting` is a currently-failing CTest, root-caused
to the test's own lighting-magnitude miscalibration rather than the routing logic it names
itself after (which is genuinely correct); it further confirmed the Vulkan variant currently
passes only because a separate, independently-known production defect (ambient/emissive
dropped for `SkinnedEffect` on Vulkan) happens to cancel out that same miscalibration — a
textbook case of two wrongs making a CTest green. Both the test's calibration and the
Vulkan-side ambient-forwarding defect need attention; fixing only one without the other would
very likely just move which backend's variant of this exact test fails.
