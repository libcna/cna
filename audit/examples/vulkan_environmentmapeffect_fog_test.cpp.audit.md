# Audit: examples/vulkan_environmentmapeffect_fog_test.cpp

## Metadata

- Source file: `examples/vulkan_environmentmapeffect_fog_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `EnvironmentMapEffect` linear-fog pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_environmentmapeffect_fog …)` /
  `cna_register_backend_test(NAME Vulkan_EnvironmentMapEffect_Fog …)`, `cmake/Tests/VulkanTests.cmake:670-672`).
- XNA/FNA relevance: direct — `EnvironmentMapEffect.FogEnabled`/`FogColor`/`FogStart`/`FogEnd` (`IEffectFog`).
- FNA reference: `HLSL/EnvironmentMapEffect.fx` (`ComputeFogFactor`/`ApplyFog` via `Common.fxh`),
  `EnvironmentMapEffect.cs` (`OnApply()` → `EffectHelpers.SetWorldViewProjAndFog`, which derives `FogVector`
  from the `World*View` matrix so that `dot(vin.Position, FogVector)` yields view-space depth).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp` (`OnApply()`
  lines 298-328, `FillGpuDrawParams()` lines 460-466), `src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.vert.glsl`
  (lines 41-44), `env_map3d.frag.glsl` (line 54).

## Purpose

Three-check pixel test asserting `EnvironmentMapEffect`'s linear-fog blend: (a) fog disabled → pure emissive
blue; (b) fog enabled, `Z=0.5` between `FogStart=0`/`FogEnd=1` → 50% mix toward a red `FogColor`; (c) `Z=0.9`
past `FogEnd=0.5` → fog fully saturated, pure red. `EnvironmentMapAmount=0`/`EnvironmentMapSpecular=(0,0,0)`
deliberately zero out the reflection terms so only the emissive+fog path is exercised, and the green solid
cube map (`makeSolidCube`) is present purely as a "would leak if `Amount!=0`" canary, never sampled into the
final result by design.

## Executive Verdict

**Needs attention** — all three of this file's own pixel assertions are numerically correct and independently
re-derived by this audit to match exactly (not just within tolerance), so the fog *blend* math itself
(`clamp((FogEnd-Z)/(FogEnd-FogStart),0,1)` combined via `mix(FogColor, color, factor)`) is right. However, every
one of the three checks renders with `World=Identity` and `View=Identity`, and the production vertex shader
(`env_map3d.vert.glsl`) computes the fog factor from **`aPos.z`, the raw untransformed object-space input
attribute** — not from `worldPos.z` (already computed two lines earlier in the same function for the eye-vector
term) and not from any view-space depth. Under `World=View=Identity`, object space, world space, and view space
all coincide, so this file's own three assertions cannot distinguish "fog computed correctly from camera-relative
depth" from "fog computed from the raw local mesh Z coordinate, ignoring World/View entirely" (see F1). This is a
real, verified production-code correctness gap, not merely a hypothetical.

## Checklist Results

### API / XNA / FNA parity
`setFogEnabledProperty`/`setFogColorProperty`/`setFogStartProperty`/`setFogEndProperty` (lines 122-125) map
correctly to FNA's `IEffectFog` surface, matching `EnvironmentMapEffect.cs`'s `FogEnabled`/`FogColor`/`FogStart`/
`FogEnd` properties one-for-one.

### Behavioral correctness
Re-derived all three checks against the production formula:
- (a) fog off, `EmissiveColor=(0,0,1)`, `DiffuseColor` default `(1,1,1)`: `litRGB=(0,0,1)`, `texColor=kWhite`,
  `baseColor=(0,0,1)`; `combinedAlpha=1`; `vFogFactor=1` (fog disabled path in the vertex shader, line 44) →
  `mix(fogColor, rgb, 1)=rgb=(0,0,255)` — matches `kBlue` exactly.
- (b) `Z=0.5`, `FogStart=0`, `FogEnd=1`: `vFogFactor=clamp((1-0.5)/(1-0),0,1)=0.5` → `mix((1,0,0),(0,0,1),0.5)
  =(0.5,0,0.5)=(128,0,128)` — matches the file's own expected `Color(128,0,128,255)` exactly.
- (c) `Z=0.9`, `FogEnd=0.5`, `FogStart=0`: `vFogFactor=clamp((0.5-0.9)/(0.5-0),0,1)=clamp(-0.8,0,1)=0` →
  `mix((1,0,0),(0,0,1),0)=(1,0,0)=(255,0,0)` — matches `kRed` exactly.
All three reproduce the file's stated expectations bit-for-bit given the *current* shader formula — but see F1
for why that formula itself is suspect once `World`/`View` stop being the identity.

### Logic
`renderQuad()`'s up-to-20-frame retry loop (lines 138-147, "skip blank/black frames") is a defensive pattern
against a transient all-black first frame; harmless here since each of the 3 calls constructs a brand-new
`EnvironmentMapEffect` and redraws from scratch every retry, so a skipped frame cannot leak stale state into
the eventual accepted one.

### C++ correctness
No unusual casts/lifetime issues. `makeSolidCube`'s returned `unique_ptr<TextureCube>` outlives its use inside
`Draw()` (stored in a local `cube` before the three `renderQuad` calls); no dangling-reference risk.

### Robustness
`matches()`'s tolerance is `30` (line 82, default param) — loose enough to absorb the GPU/interpolation noise
inherent in an interpolated-per-vertex fog factor, without being so loose it would mask a formula error of the
kind in F1 (a genuine view-space-vs-object-space Z bug would produce a *systematically* wrong value in a scene
with non-identity transforms, not a small rounding delta this tolerance would hide).

### Testing
See F1 — this is fundamentally a test-design gap: the file's own goal ("prove the fog formula") is only
half-verified, because it never exercises a `World` or `View` transform that would separate object-space Z from
camera-relative depth.

## Detailed Findings

### F1 — Vulkan `EnvironmentMapEffect` fog factor is computed from raw object-space Z, ignoring World/View; this test's exclusive use of identity transforms cannot detect the gap

- Severity: HIGH
- Confidence: HIGH (read the shader source directly; the failing input is concrete: any non-identity `World`
  or non-origin/rotated `View` causes wrong fog immediately)
- Category: correctness / test-coverage-gap
- Location/symbol: `src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.vert.glsl` lines 34-44
  (`vFogFactor = ... clamp((ep.fogStartEnd.y - aPos.z) / ..., 0.0, 1.0)`); this test file's `renderQuad()`
  (lines 110-149), which never calls `fx.setWorldProperty`/`setViewProperty` with anything but
  `Matrix::getIdentityProperty()`.
- Evidence: the vertex shader's `main()` computes `worldPos = (pc.world * vec4(aPos, 1.0)).xyz;` on line 36
  (used for `vEyeDir`), but the very next fog computation on lines 42-44 uses `aPos.z` — the raw, untransformed
  input attribute — instead of `worldPos.z`, and applies no `View`-space transform at all. FNA's real formula
  (`EnvironmentMapEffect.cs` `OnApply()` → `EffectHelpers.SetWorldViewProjAndFog`, mirroring
  `ComputeFogFactor(vin.Position)` = `dot(position, FogVector)` in `Common.fxh`) derives `FogVector` from the
  combined `World*View` matrix specifically so the fog factor tracks true camera-relative depth, not a raw
  model-space coordinate. Under this test's `World=Identity`/`View=Identity` setup, `aPos.z == worldPos.z ==`
  (view-space Z), so the three checks in this file cannot tell the two formulas apart — every one of the
  independently re-derived values above holds for *both* the correct and the buggy formula in this specific
  scene.
- Why it matters: any real game object drawn with this effect that has a non-identity `World` (moved, rotated,
  or scaled away from the coordinate origin) or a camera not sitting exactly at the world origin looking down
  +Z will get a fog factor computed from the object's *local mesh* Z coordinate rather than its actual distance
  from the camera. A rotated quad, a translated prop, or literally any populated 3D scene will show fog that
  slides across the surface as the mesh rotates, or that is completely wrong once the object is moved away
  from `Z=0`, rather than uniform fog banding by camera distance.
- FNA/XNA comparison: FNA's `ComputeFogFactor` explicitly takes the vertex position and dots it with a
  `FogVector` baked from `World*View`, i.e. genuine view-space depth — this Vulkan shader's `aPos.z` does not
  correspond to that at all once `World`/`View` diverge from identity.
- Related files: this is the same class of defect already documented in project memory for the sibling EasyGL
  backend (`feedback_easygl_fog_object_space_only.md`: "fog shader reads raw local vertex Z, ignores World/View
  entirely"). This audit independently confirms the identical pattern is present in the Vulkan backend too, and
  that it is systemic, not unique to `env_map3d.vert.glsl`: the same `aPos.z`/`inPos.z`-based clamp expression is
  present verbatim in `colored3d.vert.glsl`, `textured3d.vert.glsl`, `colored_textured3d.vert.glsl`,
  `dual_texture3d.vert.glsl`, `dual_texture_colored3d.vert.glsl`, `lit_textured3d.vert.glsl`,
  `lit_textured3d_vertexlit.vert.glsl`, `skinned3d.vert.glsl`, `skinned3d_color.vert.glsl`,
  `skinned3d_vertexlit.vert.glsl`, `skinned3d_vertexlit_color.vert.glsl`, `pbr3d.vert.glsl`, and
  `pbr3d_skinned.vert.glsl` — i.e. essentially every fog-capable Vulkan 3D pipeline. This test file is simply the
  one in this batch that specifically claims to verify fog, so the gap is reported here; the actual defect lives
  in the shared shader pattern, not in this `.cpp` file, which is why this audit does not mark the file itself
  incorrect — only its coverage.
- Suggested future action (not implemented by this audit): re-render check (b) or (c) with a non-identity
  `World` (e.g. a translation along Z) or a non-origin `View`, expecting the fog factor to still track true
  camera distance — such a test would fail immediately against the current shader and give a concrete,
  reproducible regression target for fixing the underlying `aPos.z`/`inPos.z` pattern project-wide.

## Cross-File Observations

- `EnvironmentMapEffect::FillGpuDrawParams()` (C++) itself does nothing wrong here — it forwards `fogStart_`/
  `fogEnd_`/`fogColor` faithfully; the defect is purely in how the Vulkan vertex shader consumes
  `aPos.z` instead of a properly transformed depth. The C++-side `OnApply()`'s own `FogVector` computation
  (lines 298-320, mirroring FNA's real `World*View`-based derivation) is in fact *not even read* by the Vulkan
  backend's `env_map3d` shaders at all — the Vulkan path recomputes its own (buggy) factor independently in
  GLSL rather than consuming the correctly-computed `FogVector` effect parameter the C++ layer already
  produces. That parameter exists and is correct; it's simply unused by this backend.
- The header comment's Task 899/900 provenance ("mirroring `lit_textured3d.vert.glsl`/`.frag.glsl`'s identical
  'fog packed into an existing UBO's spare tail' pattern") is corroborated by `git log` and by direct inspection
  of those sibling shader files — the *packing* mechanism claim is accurate; it is the underlying Z-source
  choice that is the systemic problem, inherited unchanged across every one of those files.
- No `RasterizerState::CullNone` override is used here, and the header comment explicitly justifies that via
  the Task 364/896 finding (Vulkan's default cull state already behaves like EasyGL's effectively-no-culling
  default) — this claim was spot-checked against `RasterizerState.cpp`'s default `CullCounterClockwiseFace` and
  is plausible for this quad's winding, consistent with the sibling multilight/specular/fresnel/worldtransform
  tests in this same batch, which *do* need the explicit override for their own (different) windings.

## Missing or Weak Tests

- See F1 — no check in this file (or, by inspection, in any sibling Vulkan `EnvironmentMapEffect`/`BasicEffect`/
  `SkinnedEffect` fog test found during this pass) exercises a non-identity `World` or `View` matrix together
  with fog, so the object-space-Z-only defect is currently invisible to the whole Vulkan fog test family.

## Positive Findings

- The three numeric assertions that *are* made are exactly correct for the formula as currently implemented —
  independently re-derived by this audit to match the file's literals bit-for-bit, not merely "close enough."
- `EnvironmentMapAmount=0`/`EnvironmentMapSpecular=0` is a clean, minimal isolation technique that removes two
  entire other code paths (env-map lerp, specular add) from interfering with the fog assertion — a well-designed
  test in every respect except the World/View-identity blind spot.

## Final Assessment

The fog *blend* arithmetic this file checks is correct and well-isolated, but the file inadvertently cannot
detect a real, independently-verified defect in the Vulkan backend's fog-factor *source*: every Vulkan 3D
fog-capable shader reads the raw object-space input Z rather than a World/View-transformed depth, mirroring an
already-documented EasyGL-backend finding. This is a genuine production correctness gap that will visibly
misbehave in any populated 3D scene using this effect with fog enabled, not merely a theoretical edge case.
