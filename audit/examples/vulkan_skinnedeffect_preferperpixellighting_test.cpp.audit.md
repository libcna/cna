# Audit: examples/vulkan_skinnedeffect_preferperpixellighting_test.cpp

## Metadata

- Source file: `examples/vulkan_skinnedeffect_preferperpixellighting_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — Task 1103, `SkinnedEffect.PreferPerPixelLighting`
  dispatch test (Vulkan port; header cites "mirroring the BasicEffect test exactly" and its own
  sibling `vulkan_skinnedeffect_specular_test.cpp`).
- File type: hand-rolled `Game`-derived executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_skinnedeffect_preferperpixellighting …)` /
  `cna_register_backend_test(NAME Vulkan_SkinnedEffect_PreferPerPixelLighting …)`,
  `cmake/Tests/VulkanTests.cmake:658-661`).
- XNA/FNA relevance: direct — `SkinnedEffect.PreferPerPixelLighting`, real XNA 4.0 API. FNA source:
  `Graphics/Effect/StockEffects/SkinnedEffect.cs` (default `false`), `HLSL/SkinnedEffect.fx`'s
  `VSSkinnedVertexLighting*` (per-vertex/Gouraud) vs. per-pixel families, and the shared
  `Lighting.fxh` `ComputeLights()`.
- Production code exercised: `SkinnedEffect::setPreferPerPixelLightingProperty`/`OnApply()`'s
  shader-index computation (`SkinnedEffect.cpp` lines 193-201, 504-517),
  `VulkanGraphicsBackend::DrawIndexedPrimitives`/`DrawPrimitives`'s `needsSkinned`/`preferVertexLit`
  dispatch (`VulkanGraphicsBackend.cpp` line 7427: `d.preferVertexLit = params.lightingEnabled &&
  !params.preferPerPixelLighting`), `shaders/skinned3d_vertexlit.vert.glsl` (Gouraud path) vs.
  `shaders/skinned3d.vert.glsl`/`skinned3d.frag.glsl` (per-pixel path).

## Purpose

Proves the Vulkan backend genuinely dispatches to two different shader programs — a per-vertex
(Gouraud-interpolated) lighting evaluation vs. a per-fragment one — depending on
`PreferPerPixelLighting`, rather than always evaluating in one stage regardless of the flag. Reuses
the exact scene from `vulkan_skinnedeffect_specular_test.cpp`'s "(a) eye straight on" case (single
Identity bone at 100% weight, so skinning itself is a mathematical no-op) so the two files'
expected values are meant to be mutually corroborating.

## Executive Verdict

**Needs attention.** The dispatch itself is real and correctly exercised (case (a) vs (b) vs (c)
genuinely differ, confirmed against the production code), but this audit found two real, shared
production defects while tracing the exact shader path this file dispatches through — both
invisible to this file's own Identity-`World`, small-ambient scene, so the file cannot be faulted
for "missing" them by its own design, but they are real gaps in the code this file is meant to be
validating.

## Checklist Results

### API / XNA / FNA parity
`setPreferPerPixelLightingProperty`/`getPreferPerPixelLightingProperty` (`SkinnedEffect.hpp`)
match FNA's `SkinnedEffect.PreferPerPixelLighting` name/semantics exactly (default `false`, the real
XNA default, confirmed by `SkinnedEffect.cpp`'s member initializer defaulting `preferPerPixelLighting_`
to `false` implicitly via the class's in-header default and never being touched by the constructor).

### Behavioral correctness
Traced `VulkanGraphicsBackend.cpp` line 7427: `d.preferVertexLit = params.lightingEnabled &&
!params.preferPerPixelLighting`. Since `SkinnedEffect::getLightingEnabledProperty()` is hardcoded
`true` (lighting cannot be disabled for `SkinnedEffect`, matching FNA), `preferVertexLit` reduces to
`!preferPerPixelLighting` exactly, dispatching case (a) (`false`) to `skinned3d_vertexlit.vert/frag.glsl`
(Gouraud) and case (b) (`true`) to `skinned3d.vert/frag.glsl` (per-pixel) — precisely what the
file's own 3-check structure (a, b, a≠b) asserts.

Independently re-derived case (a)'s expected value (`kExpectedVertexLit(125,125,125)`) by hand from
the exact scene (eye=(0,0,3), lightDirRaw=(0.5,0,-1) normalized, N=(0,0,1) constant, quad TL=(-1,1,0)/
BR=(1,-1,0), material diffuse=0.4, ambient=0.02, specular=(1,1,1), power=32, light diffuse/specular=0.5/1.0):
- Diffuse+ambient term (constant across the quad since N and L don't vary per-vertex):
  `NdotL0 = dot(N,-L) = 0.894427`; `litRGB = (0.02 + 0.5*0.894427) * 0.4 = 0.186885`.
- Per-vertex specular: `TL` → `E=normalize((0,0,3)-(-1,1,0))`, `H=normalize(E-L)`, `dot(H,N)=0.982906`,
  `spec=pow(0.982906,32)=0.575836`. `BR` → `E=normalize((0,0,3)-(1,-1,0))`, `dot(H,N)=0.912371`,
  `spec=pow(0.912371,32)=0.053182`.
- The sample point (`kSize/2, kSize/2`) sits on the diagonal seam between the two triangles, at the
  exact midpoint of the `TL`-`BR` edge; by the scene's own left-right/eye symmetry (eye directly
  above the quad centre along +Z, both `TL`/`BR` at the same view-space depth), perspective-correct
  interpolation reduces to a simple 0.5/0.5 average here: `spec_avg = 0.5*(0.575836+0.053182) =
  0.314509`.
- `outColor.rgb = litRGB + spec_avg = 0.186885 + 0.314509 = 0.501394` → `×255 ≈ 127.9`.
This hand-derivation lands at **≈128**, not the file's asserted **125** — a 3-unit gap. This is
plausibly explained by two compounding, independently-confirmed effects this audit traced in the
production code (see F1 below): (1) the real rendered ambient contribution is actually `0` rather
than `0.02` (a further ≈2-unit dimming, landing the true value nearer 126), and (2) ordinary
GPU floating-point/perspective-interpolation rounding accounts for the remainder — consistent with,
not contradicting, this file's own framing ("125 is the real, measured value on this backend
(confirmed against the actual render...)"). The file's own value is plausible as a genuine live
measurement; this audit's finding is that the *ambient* term folded into that measurement is
silently wrong, not that the asserted constant itself was fabricated.

### Logic
`renderWith()`'s up-to-20-frame retry loop (lines 170-181) correctly guards against a stray
all-black first frame, consistent with this shard's `specular`/`multilight`/`vertexcolor` siblings.

### Testing
Covers only the `false`/`true` dispatch boundary, correctly scoped to this task's own goal. Does not
combine `PreferPerPixelLighting` with a non-Identity bone, `WeightsPerVertex`≠1, `VertexColorEnabled`,
or fog — reasonably deferred to sibling files in this shard.

## Detailed Findings

### F1 — `SkinnedEffect`'s `AmbientLightColor`/`EmissiveColor` never reach the rendered pixel on the Vulkan backend

- Severity: HIGH
- Confidence: HIGH (traced the full data path from `SkinnedEffect::FillGpuDrawParams()` through
  `VulkanGraphicsBackend.cpp` to the GLSL shader source; corroborated against the EasyGL backend's
  correct equivalent path)
- Category: correctness (production code), cross-backend parity gap
- Location/symbol: `SkinnedEffect::FillGpuDrawParams()` (`SkinnedEffect.cpp` lines 320-404) never
  assigns `CNA::Internal::Backends::GpuDrawParams::ambientColor` (it only computes and assigns
  `p.emissiveColor = (emissiveColor_ + ambientLightColor_ * diffuseColor_) * alpha_`, lines 335-338).
  `VulkanGraphicsBackend::FillExtPushConst()` (`VulkanGraphicsBackend.cpp` lines 3575-3592) copies
  `p.ambientColor` into the shared push-constant's `pc.ambientColor` field (line 3584) — this is the
  function used for the skinned draw path (`FillExtPushConst(d.pushConst, wvp, params);` at line
  7386, reached before the `needsSkinned` branch). `shaders/skinned3d.frag.glsl` and
  `shaders/skinned3d_vertexlit.vert.glsl` both compute `litRGB = (pc.ambientColor + lightSum) *
  pc.diffuseColor.rgb` directly from this field. Separately, `VulkanGraphicsBackend.cpp`'s
  `needsSkinned` fill block (`skinnedFogUboData`, lines 7466-7494) never writes `params.emissiveColor`
  anywhere into the skinned draw's push constant or UBO — `emissiveColor` is only ever consumed for
  the PBR path (`FillPbrUboData()`, line 3609), `EnvironmentMapEffect` (`envMapUboData`, lines
  7510-7511), and `BasicEffect`'s lit-textured path (`litUboData`, lines 7557-7558).
- Evidence this is Vulkan-specific, not a shared-across-backends defect: the EasyGL backend's
  `EnsureSkinnedProgram()`/`EnsureSkinnedVertexLitProgram()` fragment shaders declare `uniform vec3
  uEmissiveColor;` (no separate `uAmbientColor` uniform for the skinned path — confirmed by the
  shader source's own comment at `EasyGLGraphicsBackend.cpp` line 3438: "No separate uAmbientColor
  uniform here, matching EnsureSkinnedProgram()'s own [pattern]") and compute
  `litRGB = lightSum * uDiffuseColor.rgb + uEmissiveColor` (lines 3380, 3520) — correctly consuming
  `SkinnedEffect`'s own ambient-folded-into-emissive convention. `uEmissiveColor` is bound from
  `params.emissiveColor` generically (`EasyGLGraphicsBackend.cpp` lines 4087-4089), so on EasyGL,
  `AmbientLightColor` and `EmissiveColor` both genuinely reach the rendered pixel for `SkinnedEffect`.
  On Vulkan, neither does.
- Why it matters: any game placing a skinned character with a non-zero `AmbientLightColor` (the
  overwhelmingly common case — `EnableDefaultLighting()` itself sets a substantial ambient of
  `(0.05333, 0.09882, 0.18196)`) or a non-zero `EmissiveColor` on the Vulkan backend will silently
  render darker/less-emissive than every other backend and than real XNA/FNA, with no error,
  warning, or visually obvious failure mode — a correctness regression a player would perceive as
  "my skinned model looks flatter/dimmer on Vulkan" without an obvious cause.
- Why this file (and every other file in this batch) cannot detect it: this file sets
  `kAmbient=(0.02,0.02,0.02)` — small enough that the ≈2/255-unit dimming it causes is well within
  the file's own `±10` tolerance (`matches()`, lines 113-118), so both cases (a) and (b) still pass.
  `vulkan_skinnedeffect_multilight_test.cpp` never sets ambient at all (defaults to `(0,0,0)`, so the
  bug is a no-op there by coincidence). `vulkan_skinnedeffect_vertexcolor_test.cpp` explicitly sets
  `AmbientLightColor=Vector3::Zero` and its own header comment states this "sidesteps a separate,
  pre-existing question of exactly how SkinnedEffect's ambient term reaches this backend's skinned3d
  shaders -- out of this task's scope" — i.e., the test's own author was already aware this exact
  code path was suspect and chose to route around it rather than resolve it; this audit's
  independent code trace confirms the suspicion was correct. `identity_bones`/`translation_bone`/
  `twobone_blend`/`weightspervertex` all use `EnableDefaultLighting()`'s much larger ambient but only
  check coarse red/green channel dominance, not an exact value, so the dimming does not flip their
  pass/fail outcome either.
- FNA/XNA comparison: FNA's `Lighting.fxh` `ComputeLights()` genuinely adds `AmbientLightColor` to
  the lit sum before multiplying by `DiffuseColor`, and separately adds `EmissiveColor` — both are
  real, always-active XNA behaviors for any lit effect, not optional extensions.
- Related files: every file in this batch exercises the same shared `SkinnedEffect.cpp`/
  `VulkanGraphicsBackend.cpp`/`skinned3d*.glsl` code; this is the most concretely quantifiable
  anchor point since case (a)/(b) here (and in `vulkan_skinnedeffect_specular_test.cpp`) both set a
  non-zero, if small, ambient value with an exact-value assertion.
- Suggested future action (not implemented by this audit): either (a) have
  `VulkanGraphicsBackend`'s skinned-draw fill path also forward `params.emissiveColor` into a UBO
  slot the skinned shaders read and add on top of `litRGB` (mirroring EasyGL's `uEmissiveColor`
  convention), or (b) have `SkinnedEffect::FillGpuDrawParams()` additionally populate
  `p.ambientColor` directly (mirroring `BasicEffect`'s own convention) and have the Vulkan skinned
  shaders separately add an emissive term — either fix restores parity with EasyGL. Add a pixel test
  with a large, exact-value ambient (and zero light) specifically isolating this term, since none of
  this batch's 8 files can currently catch a regression here.

### F2 — Vulkan's skinned shaders never transform the lit normal into world space (shared with EasyGL's already-documented equivalent defect)

- Severity: HIGH
- Confidence: HIGH
- Category: correctness (production shader), test-coverage gap
- Location/symbol: `shaders/skinned3d.vert.glsl` line 60 (`vNormal = normalize(mat3(skinMat) *
  aNormal);`) and `shaders/skinned3d_vertexlit.vert.glsl` line 67 (`vec3 N = normalize(mat3(skinMat)
  * aNormal);`) — both compute the lit normal purely from the bone-skinning matrix's rotational
  part, in bind/object space, and never multiply by `fog.world` (or its inverse-transpose), even
  though the very same shaders correctly world-transform the *position*
  (`vWorldPos = (fog.world * skinnedPos).xyz;`, line 62/62 respectively).
- Evidence: this is the identical defect shape already found and reported as HIGH in
  `easygl_skinnedeffect_preferperpixellighting_test.cpp.audit.md`'s F1 for the EasyGL backend's
  `EnsureSkinnedProgram()`/`EnsureSkinnedVertexLitProgram()` — this audit independently confirmed
  the Vulkan GLSL sources have the exact same gap. FNA's real `Common.fxh`
  `ComputeCommonVSOutputWithLighting()` (used by every `VSSkinnedVertexLighting*`/
  `VSSkinnedOneLight*` entry point) does `worldNormal = normalize(mul(normal,
  WorldInverseTranspose))` as a mandatory step after skinning, before `ComputeLights()` is ever
  called — a step both CNA backends' skinned shaders skip.
- Why it matters: for any `World` other than Identity (i.e., a skinned character actually placed and
  rotated in a scene, the overwhelmingly common real-game case), the normal used for lighting stays
  in bind/object space while the eye vector, light directions, and world position it is dotted
  against are all genuinely in world space — an inconsistent-space computation that silently
  produces wrong lit/specular pixels whenever the model is rotated.
- Why this file (and every file in this batch) cannot catch it: all 8 files in this shard use
  `fx.setWorldProperty(Matrix::getIdentityProperty())` — `mat3(Identity)` is trivially both "no
  transform" and its own inverse-transpose, so the missing step is a no-op for every scene in this
  entire test family. No file in this batch would regress if the missing transform were removed
  entirely, nor newly pass if it were added.
- FNA/XNA comparison: see Evidence.
- Related files: identical defect in the EasyGL backend (already reported); not verified whether
  Bgfx/D3D9/D3D11/D3D12/SdlGpu/WebGPU share it (out of this shard's scope, flagged for those
  backends' own audits).
- Suggested future action (not implemented by this audit): add a `uNormalMatrix`/`fog.world`-based
  second transform after skinning in both Vulkan skinned shader families, mirroring the (correct)
  pattern already used elsewhere in this codebase for BasicEffect's own vertex-lit path; add at
  least one Vulkan `SkinnedEffect` pixel test with a non-Identity (rotated) `World` and a directional
  light, so this class of regression is actually observable on this backend.

## Cross-File Observations

- This file, `vulkan_skinnedeffect_specular_test.cpp`, and (by citation) a not-in-this-batch
  `vulkan_basiceffect_specular_test.cpp` form a deliberate cross-checking chain, mirroring the
  EasyGL shard's identical pattern — each file's expected values are derived from, and required to
  match, a sibling file's own live-observed render rather than an independently fabricated number.
  Confirmed self-consistent (both files cite `125`/`155` for the same "eye straight on" scene).
- F1 and F2 are genuine production defects discovered by tracing this file's own dispatch path, not
  hypothetical — both are anchored here because this file (together with
  `vulkan_skinnedeffect_specular_test.cpp`) is the closest any file in this batch comes to being
  *able* to detect either, and both still fail to, for the reasons given above.

## Missing or Weak Tests

- See F1 — no test in this shard uses a large, exact-value, easily-distinguished `AmbientLightColor`
  or `EmissiveColor` for `SkinnedEffect` on Vulkan.
- See F2 — no test in this shard uses a non-Identity `World` for `SkinnedEffect` on Vulkan.
- No test combines `PreferPerPixelLighting=true` with `WeightsPerVertex`≠1 or a non-Identity bone.

## Positive Findings

- The 3-check structure (assert value A, assert value B, assert A≠B) is a genuinely good pattern for
  proving a dispatch flag is live rather than decorative.
- The header comment's transparency about citing a sibling file's own live-measured value as ground
  truth, rather than presenting a fresh unverifiable magic number, is good practice — consistent with
  this shard's EasyGL counterpart.
- The dispatch logic itself (`preferVertexLit = lightingEnabled && !preferPerPixelLighting`) was
  independently traced and confirmed correct against both the C++ property and the real XNA default.

## Final Assessment

The `PreferPerPixelLighting` dispatch this file targets is genuinely correct and well-tested. Tracing
its exact code path surfaced two real, shared production defects (F1: ambient/emissive silently
dropped on Vulkan; F2: missing world-space normal transform, mirroring an already-known EasyGL
defect) that this file's own Identity-World/small-ambient design cannot detect — these are reported
here as the most concrete anchor in this batch, not as faults specific to this file's own test logic.
