# Audit: examples/webgpu_basiceffect_preferperpixellighting_test.cpp

## Metadata

- Source file: `examples/webgpu_basiceffect_preferperpixellighting_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-webgpu` shard — `BasicEffect.PreferPerPixelLighting` dispatch test,
  WebGPU backend (experimental, per `CLAUDE.md`).
- Test executable: `cna_test_webgpu_basiceffect_preferperpixellighting`, CTest target
  `WebGPU_BasicEffect_PreferPerPixelLighting` (`cmake/Tests/WebGpuTests.cmake:69-70`).
- XNA/FNA relevance: direct — `BasicEffect.PreferPerPixelLighting`, `IEffectLights` (ambient/diffuse/
  specular/directional light 0).
- FNA reference: `HLSL/Lighting.fxh` (`ComputeLights()` — the same half-vector Blinn-Phong formula
  used per-vertex or per-pixel depending on the technique selected).
- Related production code: `src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp`
  (`CreateLitTexturedResources()` per-pixel shader ~lines 2888-2973, per-vertex sibling
  ~lines 2992-3074, dispatch `command.preferVertexLit = params.lightingEnabled &&
  !params.preferPerPixelLighting;` at line 6522), git-attributed to `9c7b3398 feat(Task 1105):
  WebGPU real per-vertex-lit shader + PreferPerPixelLighting dispatch`.
- git log for this file: single commit `9c7b3398` (introduced together with the production feature it
  tests — no drift/staleness risk yet).

## Purpose

Three-check test proving `BasicEffect.PreferPerPixelLighting` genuinely dispatches to two different,
real WGSL shaders on this backend, using the same discriminating-scene technique already used by the
equivalent EasyGL/Vulkan/Bgfx tests referenced in this file's own header comment (a single-normal quad
where diffuse is spatially constant but specular is position-dependent, sampled exactly on the
inter-triangle diagonal seam so vertex-lit Gouraud-averaging and per-pixel evaluation provably differ).
The header comment is explicit and unusually well-reasoned about *why* this backend's expected pixel
values (187/203) look nothing like the raw ~127/~152 the sibling backends read: this backend's
swapchain prefers an sRGB surface format, so the same linear shader math gets gamma-encoded on write.

## Executive Verdict

**Healthy.** Independently re-verified both the dispatch wiring (`command.preferVertexLit` at
`WebGPUGraphicsBackend.cpp:6522` correctly matches XNA's real `PreferPerPixelLighting=false` default)
and the header comment's own central claim — that 187/203 are the sRGB encode of the same ~127/~152
linear values the other backends read — by direct calculation; both check to within one 8-bit unit.

## Checklist Results

### API / XNA / FNA parity
`setPreferPerPixelLightingProperty(bool)` (line 127), `setLightingEnabledProperty(true)`,
`setSpecularColorProperty`/`setSpecularPowerProperty`, `DirectionalLight0.setEnabledProperty/
setDirectionProperty/setDiffuseColorProperty/setSpecularColorProperty` (lines 126-139) all map
directly onto FNA's `BasicEffect`/`IEffectLights` surface — no divergence found.

### Behavioral correctness
Independently recomputed the sRGB encode of the linear values the sibling-backend tests use for this
identical scene (127/255 and 152/255, cited in this file's own comment, lines 170-175):
- `sRGB(127/255=0.4980)`: `0.4980^(1/2.4)=0.7478`; `1.055*0.7478-0.055=0.7339`; `×255=187.1` —
  matches `kExpected=187` (line 179) to within rounding.
- `sRGB(152/255=0.5961)`: `0.5961^(1/2.4)=0.8059`; `1.055*0.8059-0.055=0.7952`; `×255=202.8` —
  matches `kExpected=203` (line 185) to within rounding.
This independently confirms the header comment's own derivation is correct, not just internally
self-consistent — a genuinely verified, non-boilerplate numeric claim.

### Logic
Confirmed the sRGB-preference premise itself by reading `ConfigureSurface()`
(`WebGPUGraphicsBackend.cpp` lines 2079-2093): `preferredFormats[]` lists
`BGRA8UnormSrgb`/`RGBA8UnormSrgb` **before** the non-sRGB equivalents, so whenever the platform's
surface capabilities offer an sRGB-capable format (the common case), this backend's swapchain really
does write through an implicit linear→sRGB encode on store — corroborating this file's own claim
rather than taking it on faith. Also confirmed `command.preferVertexLit = params.lightingEnabled &&
!params.preferPerPixelLighting` (line 6522) — `renderWith(dev, false)` (check a) yields
`preferVertexLit=true` (real XNA default, vertex-lit), `renderWith(dev, true)` (check b) yields
`preferVertexLit=false` (per-pixel) — correct polarity, not inverted.

### C++ correctness
`renderWith()` is called twice per `Draw()` (lines 176, 182), each time constructing a fresh
`BasicEffect`/`VertexBuffer` — no state leakage between the vertex-lit and pixel-lit renders that could
make check (c)'s difference spurious.

### Architecture
Confirmed (by direct read of the two WGSL shader bodies, `CreateLitTexturedResources()` lines
2888-2973 vs. 2992-3074) that the vertex-lit and per-pixel-lit paths are genuinely two different
shader modules performing the lighting math in different shader stages (vertex vs. fragment), not one
shader with a runtime branch that always takes the same path — check (c)'s "these are provably
different values" claim (header, line 19) is architecturally real, not incidental.

### Testing
Good, honest 3-check design: (a)/(b) assert specific values, (c) is a genuinely necessary
differential check that would catch a regression where the flag silently stopped mattering (e.g. both
paths collapsing to the same shader) even if (a) and (b) individually still passed by coincidence.

### Cross-file consistency
The vertex-lit shader's Blinn-Phong math (lines 3050-3062: `ndotl0`, half-vector `h0=normalize(e-nl0)`,
`spec0=pow(max(dot(h0,n),0),power)`, `AddSpecular`-equivalent `color.rgb += specularRGB*color.a` in the
fragment at line 3072) matches FNA's `Lighting.fxh`/`Common.fxh` formula exactly, and both the
vertex-lit and per-pixel-lit shaders use a real `normalMatrixCol0/1/2` (a proper world-space normal
matrix, not the raw `World` matrix) for the surface normal — this file's scene uses `World=Identity`,
so it cannot distinguish a correct inverse-transpose from a buggy raw-`World` normal transform, but
unlike the confirmed `SkinnedEffect` bug (see below), the plain `BasicEffect` lit-textured shader here
genuinely does use a dedicated normal-matrix uniform (`ComputeNormalMatrix3x3()`, called from
`FillLitLightUniforms()`), so this is not the same defect class.

## Detailed Findings

None at HIGH or above.

## Cross-File Observations

- Per this audit's cross-cutting mandate: this file exercises the *unskinned* `BasicEffect`
  lit-textured shader, not `SkinnedEffect`, so the confirmed skinned-effect world-space-normal-transform
  bug in `WebGPUGraphicsBackend::CreateSkinnedResources()` does not apply to this file. The fog-formula
  bug (EasyGL/Bgfx/Vulkan) also does not apply: this file never enables fog, and — independently
  confirmed by reading both `CreateLitTexturedResources()` shader bodies in full — neither the
  per-vertex-lit nor per-pixel-lit WGSL shader implements fog at all (no `fogColor`/`fogEnabled`
  reference anywhere in either shader string), consistent with `plans/plan_webgpu.md`'s explicit, tracked
  statement that fog is "deliberately deferred" on every WebGPU 3D shader except
  `EnvironmentMapEffect`'s. This is a known, documented backend limitation, not a new finding — noted
  here only because it means a hypothetical `FogEnabled=true` variant of this exact test would
  currently render identically to `FogEnabled=false`, silently.
- The sRGB-swapchain-preference architecture point is a genuine, verified cross-backend visual-parity
  gap worth flagging for the subsystem synthesis: **this backend's 3D rendering is not guaranteed to
  produce the same raw pixel values as EasyGL/Vulkan/Bgfx/D3D9 for an identical scene**, because those
  backends do not prefer an sRGB swapchain format the way this one does. This file and its sibling
  (referenced in its own header, `webgpu_littextured3d_test.cpp`) both correctly compensate by using
  backend-local expected constants rather than comparing against another backend's values — the right
  engineering response — but it means a future cross-backend "golden image" comparison harness would
  need to either force a non-sRGB format for this backend or apply a gamma-correction step before
  comparing, or it will misreport a false mismatch. Recorded as an architecture observation, not a
  defect in this file.

## Missing or Weak Tests

None specific to this file — the 3-check design is appropriately minimal and each check is doing real
work (no redundant check found).

## Positive Findings

- Rare case of a test file whose own header comment makes a precise, checkable quantitative claim
  (the sRGB gamma-encode relationship to the sibling backends' linear values) that this audit was able
  to independently verify to within rounding, rather than merely restate.
- Correct, verified-polarity dispatch flag wiring (`preferVertexLit = lightingEnabled &&
  !preferPerPixelLighting`), matching XNA's real default.

## Final Assessment

A well-designed, numerically self-verifying test with no defects found in either its own assertions or
the production dispatch/shader code it exercises. Its main contribution beyond correctness is
documenting (accurately) a genuine cross-backend visual-parity characteristic of this experimental
backend's sRGB-preferring swapchain, which is worth carrying into the subsystem-level synthesis as an
architecture note rather than a per-file defect.
