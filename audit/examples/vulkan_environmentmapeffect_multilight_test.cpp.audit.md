# Audit: examples/vulkan_environmentmapeffect_multilight_test.cpp

## Metadata

- Source file: `examples/vulkan_environmentmapeffect_multilight_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `EnvironmentMapEffect` multi-directional-light forwarding pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_environmentmapeffect_multilight …)` /
  `cna_register_backend_test(NAME Vulkan_EnvironmentMapEffect_MultiLight …)`, `cmake/Tests/VulkanTests.cmake:269-271`).
- XNA/FNA relevance: direct — `EnvironmentMapEffect.DirectionalLight1`/`DirectionalLight2` forwarding
  (`IEffectLights`), matching FNA's `Lighting.fxh` `ComputeLights()` summing all 3 lights.
- FNA reference: `HLSL/EnvironmentMapEffect.fx` (`ComputeEnvMapVSOutput(..., numLights)` → `ComputeLights` in
  `Lighting.fxh`), `DirectionalLight.cs` (`Enabled` setter zeroing GPU-facing diffuse/specular).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp`
  (`FillGpuDrawParams()` lines 428-449 — Task 890's DirectionalLight1/2 forwarding fix),
  `src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.frag.glsl` (lines 33-38, per-light `NdotL`/diffuse sum).

## Purpose

Three-check pixel test proving all 3 `DirectionalLight` slots are actually summed into the final diffuse term
(not just `DirectionalLight0`, which the header comment states was the pre-Task-890 bug): (1) all three lights
enabled and co-aligned with the surface normal at the same `NdotL=0.5` → their distinct per-channel diffuse
colors (`kLight0Diffuse`=red-channel, `kLight1Diffuse`=green-channel, `kLight2Diffuse`=blue-channel) sum to a
uniform gray; (2) `DirectionalLight2.Enabled=false` → the blue channel's contribution drops out; (3)
`DirectionalLight1`'s direction rotated off-axis (`NdotL1=0`) → the green channel's contribution drops out
because that light's own `Direction` field is honored independently, not shared with light 0.

## Executive Verdict

**Healthy** — all three checks were independently re-derived by this audit from the dot-product/lighting
formula and the exact enable/direction gating in `FillGpuDrawParams()`, and every one matches the file's
expected constants exactly (not merely within the `±8` tolerance used).

## Checklist Results

### API / XNA / FNA parity
`fx.DirectionalLight1.setEnabledProperty(…)`/`setDirectionProperty(…)`/`setDiffuseColorProperty(…)` and the
`DirectionalLight2` equivalents (lines 122-128) map directly onto FNA's `IEffectLights.DirectionalLight1`/
`DirectionalLight2` properties; the test correctly treats each light's `Direction` as independently settable
(matching `DirectionalLight.cs`'s per-instance `Direction` field), which is precisely what check 3 is designed
to prove is honored per-light rather than aliased to light 0.

### Behavioral correctness
Re-derived by hand using `kNormal=(0.8660254,0,-0.5)`:
- `dot(-kLightDir,N)` where `kLightDir=(0,0,1)`: `-kLightDir=(0,0,-1)`; `dot(N,(0,0,-1))=0.5` → `NdotL=0.5` for
  any light sharing `kLightDir`, matching the file's own comment.
- Off-axis: `-kLight1DirOffAxis=(-1,0,0)`; `dot(N,(-1,0,0))=-0.8660254`; `max(-0.866,0)=0` → `NdotL1=0` exactly
  as the comment claims when `DirectionalLight1`'s direction is rotated to `(1,0,0)`.
- All-lights case: `lightSum=(0.6,0,0)*0.5 + (0,0.6,0)*0.5 + (0,0,0.6)*0.5=(0.3,0.3,0.3)`;
  `litRGB=lightSum*diffuseColor(default 1,1,1)=(0.3,0.3,0.3)`; texture is `kWhite`, `EnvironmentMapAmount=0` so
  the reflection term is fully suppressed; `rgb=(0.3,0.3,0.3)*255=76.5≈77` per channel — matches
  `kExpectedAllLights(77,77,77,255)` exactly.
- `DirectionalLight2.Enabled=false`: `FillGpuDrawParams()` (lines 445-446) forces `ld2=Vector3::Zero` when
  disabled, dropping the blue term → `(0.3,0.3,0)*255≈(77,77,0)` — matches `kExpectedLight2Disabled` exactly.
- `DirectionalLight1` off-axis: `NdotL1=0` drops the green term → `(0.3,0,0.3)*255≈(77,0,77)` — matches
  `kExpectedLight1OffAxis` exactly.

### Logic
The Vulkan frag shader (`env_map3d.frag.glsl` lines 33-38) sums all three lights' `NdotL*diffuse` terms
unconditionally, with no "one-light" shader-variant branch (unlike the C++ `EnvironmentMapEffect::OnApply()`'s
`oneLight_`/`shaderIndex` bookkeeping, which appears to be vestigial for this backend — the Vulkan path does
not consume `ShaderIndex` at all, it just always evaluates all 3 lights, relying on disabled/off-axis lights
correctly contributing zero rather than being skipped by a shader variant). This is functionally correct
(zero-contribution lights are indistinguishable from "not summed") but means the `shaderIndex`/`oneLight`
machinery computed in `OnApply()` is dead weight for this specific backend — see Cross-File Observations.

### Robustness
Disabling `DirectionalLight2` (check 2) and rotating `DirectionalLight1`'s direction (check 3) are two
independently-failing hypotheses a naive "only DirectionalLight0 forwarded" implementation would pass on check
2 (since it never reads light 2 at all, "disabling" it wouldn't change anything either way — actually a naive
single-light impl would still read light2 as zero, coincidentally passing check 2 for the wrong reason) — see
F1 for a nuance this raises about check 2's actual discriminating power.

### Testing
Two of three checks (1 and 3) cleanly discriminate the DirectionalLight1/2-forwarding fix from the old
single-light behavior. Check 2 is weaker at doing so — see F1.

## Detailed Findings

### F1 — Check 2 (`DirectionalLight2.Enabled=false`) does not, by itself, prove `DirectionalLight2` is summed at all — a hypothetical "only light0 ever read" implementation would also pass it

- Severity: LOW
- Confidence: HIGH (traced the exact expected values for both the real fix and the hypothetical old bug)
- Category: test-coverage / discriminating-power
- Location/symbol: check 2 (lines 168-171, `kExpectedLight2Disabled`)
- Evidence: under the *pre-Task-890* hypothesis (only `DirectionalLight0` ever contributes, `DirectionalLight1`/
  `DirectionalLight2` never read regardless of their state), the rendered result for "light2 disabled" would be
  identical to "light2 enabled" would be under that same hypothesis (light2's state is never consulted either
  way) — but that hypothetical broken result would be `kExpectedLight0Only=(0.3,0,0,255)≈(77,0,0,255)` for
  *both* the "all lights" and "light2 disabled" calls, so check 2's actual numeric assertion
  (`kExpectedLight2Disabled=(77,77,0,255)`) would already fail under that hypothesis at the *check 2* call
  itself (since a light0-only implementation gives `(77,0,0)`, not `(77,77,0)`) — so on reflection this check
  *does* still discriminate correctly, because it also implicitly requires light1's contribution to already be
  present (green channel = 77) for the assertion to hold. This is a self-correcting concern rather than an
  actual gap: re-verifying it, check 2 is fine as constructed. Downgraded from a potential MEDIUM concern to a
  documented non-issue after full re-derivation — recorded here per the audit's evidence-based
  verify-before-flag discipline, since it was a plausible-looking gap on first read that did not survive
  independent numeric re-derivation.
- Why it matters: none — this is a "checked and found not to be a real issue" entry, kept to show the concern
  was investigated rather than silently dropped after an initial superficial impression.
- FNA/XNA comparison: N/A.

*(No further CRITICAL/HIGH/MEDIUM findings in this file.)*

## Cross-File Observations

- `EnvironmentMapEffect::OnApply()`'s `oneLight_`/`shaderIndex` computation (`EnvironmentMapEffect.cpp` lines
  376-395) exists to select among 4 D3D9-style HLSL shader variants (`VSEnvMap`/`VSEnvMapOneLight`/etc., per
  `EnvironmentMapEffect.fx`'s `VSArray`/`VSIndices`) — the Vulkan backend has exactly one `env_map3d` shader
  that always sums all 3 lights unconditionally, so `ShaderIndex`'s `oneLight` bit is computed but never
  consumed on this backend. Not a defect (the always-3-lights approach is behaviorally equivalent when unused
  lights are correctly zeroed, as confirmed here), but worth noting as an architectural difference from the
  D3D9 stock-effect model this dirty-flag machinery was originally designed to serve.
- This file, `vulkan_environmentmapeffect_specular_test.cpp`, and `vulkan_environmentmapeffect_fresnel_test.cpp`
  all correctly apply `RasterizerState::CullNone` for the quad's winding, consistent with the shared Task 896
  finding cited across this whole test family.
- The header comment defers full derivation/discrimination-trick rationale to
  `examples/easygl_environmentmapeffect_multilight_test.cpp` — this audit did not re-open that sibling file in
  this pass, but the numeric values in this Vulkan port were independently re-derived from first principles
  above and hold regardless of that sibling's own narrative.

## Missing or Weak Tests

None beyond the self-resolved F1 nuance above — the three checks together are a strong, discriminating test of
per-light enable/direction/diffuse forwarding.

## Positive Findings

- All three checks' expected constants were independently confirmed by this audit's own dot-product
  re-derivation to match exactly, not merely within the stated `±8` tolerance.
- Check 3 (light1 rotated off-axis) is a particularly well-chosen discriminator: it is the one check that could
  not pass under *any* implementation that conflates all lights' directions into a single shared direction
  field, since it specifically diverges `DirectionalLight1`'s direction from the other two.
- `FillGpuDrawParams()`'s zeroing of a disabled light's diffuse contribution (lines 433-434, 439-440, 445-446)
  correctly mirrors FNA's `DirectionalLight.Enabled` setter semantics for all 3 lights uniformly, not just light
  0 — this test is the one in the family that specifically proves that symmetry holds for lights 1 and 2 too.

## Final Assessment

A well-constructed, numerically precise three-check test that correctly proves the Task 890 DirectionalLight1/2
forwarding fix is real and working in the Vulkan backend, with values independently re-derived and confirmed
exact by this audit.
