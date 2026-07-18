# Audit: examples/vulkan_skinnedeffect_multilight_test.cpp

## Metadata

- Source file: `examples/vulkan_skinnedeffect_multilight_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — Task 893, `SkinnedEffect`
  `DirectionalLight1`/`DirectionalLight2` forwarding pixel test (Vulkan port; header cites
  "examples/easygl_environmentmapeffect_multilight_test.cpp and
  examples/easygl_basiceffect_multilight_emissive_test.cpp for the full FNA-reference derivation").
- File type: hand-rolled `Game`-derived executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_skinnedeffect_multilight …)` /
  `cna_register_backend_test(NAME Vulkan_SkinnedEffect_MultiLight …)`,
  `cmake/Tests/VulkanTests.cmake:645-648`).
- XNA/FNA relevance: real XNA `SkinnedEffect` implements `IEffectLights` with 3 independent
  `DirectionalLight` slots; FNA's `Lighting.fxh` `ComputeLights()` sums all 3 lights' diffuse
  contributions, not just light 0.
- Production code exercised: `SkinnedEffect::FillGpuDrawParams()` (`SkinnedEffect.cpp` lines
  340-371, the 3-light diffuse forwarding), `shaders/skinned3d.frag.glsl`'s `lightSum` computation
  (lines 46-48) — dispatched here since this test never sets `PreferPerPixelLighting`, so it should
  route to `skinned3d_vertexlit.vert.glsl` instead (see Behavioral correctness).

## Purpose

Verifies `SkinnedEffect::FillGpuDrawParams()` forwards **all 3** directional lights by assigning
each a distinct, single-channel diffuse color (red/green/blue) and checking that disabling or
rotating any one light drops exactly its own channel from the summed result, while the other two
remain unaffected. Uses an identity bone palette (weight=1 on bone 0, defaulting to Identity) to
isolate skinning from the lighting formula.

## Executive Verdict

**Healthy.** All 3 checks' expected values were independently re-derived from the real geometry and
the actual shader formula, and every one matches exactly (not merely within tolerance) — a
carefully constructed, channel-isolated discrimination test, matching the rigor of its already-
audited EasyGL sibling.

## Checklist Results

### API / XNA / FNA parity
`fx.DirectionalLight0/1/2.setEnabledProperty/setDirectionProperty/setDiffuseColorProperty` are
genuine XNA `IEffectLights`/`DirectionalLight` API members, correctly used here.

### Behavioral correctness
Independently re-derived the geometry: `kNx=0.8660254 (√3/2), kNy=0, kNz=-0.5`, `kLightDir=(0,0,1)`.
`dot(N,-kLightDir) = dot((0.866,0,-0.5),(0,0,-1)) = 0.5` — confirmed matches the comment's own claim
(line 57).

This test never sets `PreferPerPixelLighting`, so per this shard's real XNA default (`false`), it
should dispatch to `skinned3d_vertexlit.vert.glsl` (Gouraud), not `skinned3d.frag.glsl` (per-pixel) —
traced against `VulkanGraphicsBackend.cpp` line 7427 (`d.preferVertexLit = params.lightingEnabled &&
!params.preferPerPixelLighting`), confirming the dispatch does route to the vertex-lit shader. Since
`N` is constant across every vertex in this quad (unlike the specular test's varying eye vector, the
diffuse-only formula here has no per-vertex-varying inputs at all — `N`, `L0/L1/L2` are each uniform
across the whole quad), the Gouraud-interpolated (vertex-lit) result is numerically identical to
what a per-pixel evaluation would produce for this specific scene: there is nothing to interpolate
between, since every vertex computes the same value. This means the test's own numeric derivation
(reconstructed from `skinned3d.frag.glsl`'s formula, structurally identical to
`skinned3d_vertexlit.vert.glsl`'s diffuse term) is valid regardless of which of the two shader
variants actually executes.

Traced the formula: `lightSum = light0Diffuse*NdotL0 + light1Diffuse*NdotL1 + light2Diffuse*NdotL2`;
`litRGB = (ambientColor + lightSum) * diffuseColor.rgb` (default `diffuseColor_=(1,1,1)`,
`ambientLightColor_=(0,0,0)`, never set by this test).
- **Check 1** (all 3 lights sharing `kLightDir`, all enabled): `NdotL0=NdotL1=NdotL2=0.5`.
  `lightSum = 0.6*0.5*(1,0,0) + 0.6*0.5*(0,1,0) + 0.6*0.5*(0,0,1) = (0.3,0.3,0.3)`; `litRGB=(0.3,0.3,0.3)`
  → `×255≈(76.5,76.5,76.5)≈(77,77,77)` — matches `kExpectedAllLights(77,77,77,255)` exactly.
- **Check 2** (light2 disabled): `FillGpuDrawParams()`'s `light2On ? ... : Vector3::Zero` gate
  (`SkinnedEffect.cpp` line 356-357) forces `light2Diffuse=(0,0,0)` — blue channel's `0.3` term
  drops, R/G unaffected. Matches `kExpectedLight2Disabled(77,77,0,255)` exactly.
- **Check 3** (light1 rotated to `kLight1DirOffAxis=(1,0,0)`): `dot(N,-dir1)=dot((0.866,0,-0.5),
  (-1,0,0))=-0.866`, negative → `NdotL1=0` — green channel's term drops, R/B unaffected. Matches
  `kExpectedLight1OffAxis(77,0,77,255)` exactly.
Since `ambientColor` in this test defaults to `(0,0,0)` and is never set, the shared, real
Vulkan-specific ambient/emissive-forwarding defect documented in
`vulkan_skinnedeffect_preferperpixellighting_test.cpp.audit.md`'s F1 is a coincidental no-op here —
this file cannot detect it either way, since ambient is meant to be zero in this scene regardless of
whether the bug is present.

### Logic
`renderWith()`'s retry loop (lines 139-149) guards against reading a stale/blank frame — same
pattern as the `specular`/`preferperpixellighting`/`vertexcolor` siblings in this shard.

### Memory/resource lifetime
Fresh `SkinnedEffect fx(dev)` per `renderWith()` call — no cross-check state leak.

### C++ correctness
`static_assert(sizeof(SkinnedGpuVertex) == 52)` — consistent with the shard's shared layout,
independently re-verified against `VulkanGraphicsBackend.cpp`'s pipeline attribute descriptions in
this batch's `identity_bones` report.

### Architecture
Explicit `GraphicsDeviceManager` construction with `kSize=64` — matches this shard's
`specular`/`preferperpixellighting`/`vertexcolor` siblings' style.

### Testing
This file is itself the test; the 3-check design (all-on baseline, disable one, rotate one) is a
well-chosen minimal set proving per-light independence for both the `Enabled` gate and the
direction-dependent `NdotL` clamp.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings specific to this file's own logic. This is one of the more
rigorously self-verified files in this batch: every check's expected value was independently
reconstructed from first principles and matched exactly, with no reliance on tolerance-absorbed
approximation.

## Cross-File Observations

- Companion to a not-in-this-batch `vulkan_skinnedeffect_fog_test.cpp` (both registered adjacently
  in `cmake/Tests/VulkanTests.cmake` around Task 893/899) — both target distinct fields
  `FillGpuDrawParams()` previously failed to forward.
- Since this file defaults `ambientLightColor_` to `(0,0,0)`, it is one of the files in this shard
  structurally unable to detect the ambient/emissive-forwarding defect documented in
  `vulkan_skinnedeffect_preferperpixellighting_test.cpp.audit.md`'s F1 — noted here for completeness,
  not as a defect in this file's own design (a multi-light test correctly wants ambient held at zero
  to isolate the per-light terms it's actually checking).
- Shares the same independently-duplicated stride-52 `SkinnedGpuVertex` struct as every sibling in
  this shard.

## Missing or Weak Tests

None found specific to this file — the 3-check design is well-chosen for its stated goal.

## Positive Findings

- Exceptionally well-designed channel-isolation technique: assigning each of the 3 lights its own
  RGB channel makes "did light N's own contribution genuinely drop" directly legible in the pixel
  read-back.
- Every expected value matches exactly (not just within a generous tolerance) — the strongest form
  of evidence available short of instrumenting the shader directly.
- Correctly distinguishes disabling a light (Check 2) from rotating it off-axis (Check 3) — two
  different code paths (the `Enabled` gate vs. the `NdotL` clamp), both independently exercised.

## Final Assessment

One of the most rigorously self-verified files in this batch. No defects found in its own logic; the
single-channel-per-light test design is a genuinely clever way to make a 3-light summation formula
fully analytically checkable at the pixel level, and this audit's independent re-derivation confirms
it is correct for the current Vulkan shader dispatch.
