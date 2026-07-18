# Audit: examples/vulkan_basiceffect_multilight_emissive_test.cpp

## Metadata

- Source file: `examples/vulkan_basiceffect_multilight_emissive_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — Vulkan backend `BasicEffect` 3-light + `EmissiveColor` pixel test
  (Task 897)
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_test_vulkan_basiceffect_multilight_emissive` / `Vulkan_BasicEffect_MultiLightEmissive`,
  `cmake/Tests/VulkanTests.cmake:521-523`)
- XNA/FNA relevance: direct — `BasicEffect.DirectionalLight1`/`DirectionalLight2`, `EmissiveColor`,
  `LightingEnabled=true`
- FNA reference: `HLSL/Lighting.fxh` (`ComputeLights`, multi-light diffuse sum; `result.Diffuse =
  sum*DiffuseColor + EmissiveColor`)
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp::FillGpuDrawParams()`
  (lines 93-119, `DirectionalLight1`/`2` gating and lit-path-only `EmissiveColor` forwarding),
  `src/CNA/Internal/Backends/Vulkan/shaders/lit_textured3d.frag.glsl` (lines 58-70, multi-light sum and
  emissive addition), `VulkanGraphicsBackend.cpp:7549-7559` (`litUboData` population for
  `light1Dir`/`light1Diffuse`/`light2Dir`/`light2Diffuse`/`emissiveColor`).

## Purpose

3-check test proving Vulkan forwards `DirectionalLight1`/`DirectionalLight2` and `EmissiveColor` on
`BasicEffect`'s lit path — a gap the header notes (Task 897) needed new descriptor-set/UBO infrastructure since
the 128-byte push constant shared with strides 20/24/Instanced3D was already fully packed. Checks: (1) all 3
lights + emissive → `(Ambient+0.5*(L0+L1+L2))+Emissive`; (2) `DirectionalLight2.Enabled=false` → blue channel's
`0.5*L2` term drops out, others unaffected; (3) `DirectionalLight1` rotated off-axis (`NdotL1=0`) → green
channel's `0.5*L1` term drops out, others unaffected. The header also documents a Task 908 correction: an earlier
claim that Vulkan's cull state was "effectively CullNone" was falsified by Task 896 pushing the real default
`RasterizerState` to Vulkan, silently culling this test until caught by a full ctest re-run.

## Executive Verdict

**Healthy** — all three expected constants were independently re-derived by hand against the actual
`lit_textured3d.frag.glsl` multi-light-sum formula and confirmed to match exactly, including the
channel-isolation logic (each light's diffuse color only affects one RGB channel, cleanly proving per-light
independence).

## Checklist Results

### API / XNA / FNA parity
`fx.DirectionalLight1`/`fx.DirectionalLight2` (public members, matching FNA's own public
`DirectionalLight0`/`1`/`2` fields), `setEmissiveColorProperty`, `setAmbientLightColorProperty`,
`setDiffuseColorProperty` all correct XNA member usage. `DirectionalLight1`/`2`'s independent `Enabled`/
`Direction`/`DiffuseColor` properties are exercised distinctly from `DirectionalLight0`, correctly proving these
are not aliases of a single light-0 code path.

### Behavioral correctness — independent re-derivation
`kAmbient(0.05,0.05,0.05)`, `kMaterialDiffuse(1,1,1)`, `kEmissive(0.10,0.05,0.02)`, `kLightDir(0,0,1)` shared by
L0/L2 when aligned, `kLight0Diffuse(0.6,0,0)`, `kLight1Diffuse(0,0.6,0)`, `kLight2Diffuse(0,0,0.6)`,
`kNormal(0.8660254,0,-0.5)` chosen so `dot(-kLightDir,N)=0.5` for every light sharing that direction.

- **Check 1** (all lights enabled, aligned): `lightSum = ambient + 0.5*(L0+L1+L2)
  = (0.05,0.05,0.05)+(0.3,0.3,0.3) = (0.35,0.35,0.35)`; `lit = lightSum*materialDiffuse(1,1,1)+emissive
  = (0.45,0.40,0.37)` → `×255 ≈ (114.75,102,94.35) ≈ (115,102,94)` — **matches `kExpectedAllLights(115,102,94)`
  exactly**, and matches `lit_textured3d.frag.glsl`'s actual formula (`lightSum = ambient + NdotL0*light0Diffuse
  + NdotL1*light1Diffuse + NdotL2*light2Diffuse`, `lit = lightSum*fragTint.rgb + emissive`).
- **Check 2** (`DirectionalLight2.Enabled=false`): only the blue channel's `0.5*L2(0.6)=0.3` term drops
  (`BasicEffect.cpp:101-103` forces `ld2=Vector3::Zero` when disabled) → blue `lightSum=0.05`,
  `lit_blue=0.05*1+0.02=0.07` → `×255≈17.85≈18` — **matches `kExpectedLight2Disabled(115,102,18)` exactly**
  (red/green unaffected since `L2` only ever contributes to blue in this scene).
- **Check 3** (`DirectionalLight1` rotated to `(1,0,0)`): `dot(N,-lightDir1off) = dot((0.866,0,-0.5),(-1,0,0))
  = -0.866`, correctly clamped to `0` by `max(dotL1,0.0)` (`lit_textured3d.frag.glsl:56`) — green channel's
  `0.5*L1(0.6)=0.3` term drops → green `lightSum=0.05`, `lit_green=0.05*1+0.05=0.10` → `×255≈25.5≈26` —
  **matches `kExpectedLight1OffAxis(115,26,94)` exactly** (red/blue unaffected).

### Logic
Each of the three lights is deliberately routed to a distinct RGB channel (`L0`→red, `L1`→green, `L2`→blue) —
an elegant test-design choice that makes each check's channel-isolation unambiguous and independently
verifiable, rather than requiring a shared-channel sum to be disentangled algebraically.

### Memory/resource lifetime
No dynamic allocation of note; fresh `BasicEffect` per `renderWith()` call, no state leakage between checks.

### C++ correctness
Same degenerate-eye-position observation as `vulkan_basiceffect_one_light_test.cpp`'s F1: `View`/`Projection`
are never set here either (left at `BasicEffect`'s `Identity` defaults), and the quad's sampled center is at
`worldPos=(0,0,0)=eyePos`, producing the same `normalize(vec3(0))` half-vector degenerate case in the shader.
This test additionally never sets `SpecularColor` on `BasicEffect` (default `Vector3{1,1,1}`,
`BasicEffect.hpp:363`) *or* on any of the three `DirectionalLight`s (each defaults `Vector3::Zero`,
`DirectionalLight.hpp`), so — identically to the `one_light` sibling — the final `specularRGB` term is
multiplied by each light's own zero `SpecularColor`, and resolves to a clean finite `0` given SPIR-V's `FMax`
NaN-avoidance guarantee for the intermediate `max(dot(h,N),0.0)` terms. Not a defect in the current backend;
see the `one_light_test.cpp` report's F1 for the full reasoning (identical here, not repeated as a separate
finding to avoid duplication).

### Performance
N/A — single-frame draws per check, no hot path.

### Robustness
Covered above.

### Testing
All three checks independently confirmed correct; per-light channel isolation is a strong, well-designed test
structure.

## Detailed Findings

No HIGH/CRITICAL/MEDIUM findings — every asserted value independently re-derived and confirmed correct against
the real production shader formula.

## Cross-File Observations

- Shares the identical unset-`View`/`Projection` degenerate-specular-input pattern with
  `vulkan_basiceffect_one_light_test.cpp` (see that report's F1 for the full derivation; not repeated here to
  avoid duplicating the same evidence twice) — both files are safe in practice only because every light's own
  `SpecularColor` defaults to zero here, not because of any deliberate guard in the test or shader.
- Task 908's cull-state correction (documented in this file's own header, lines 13-17) is corroborated: `git log
  --oneline -- examples/vulkan_basiceffect_multilight_emissive_test.cpp` shows
  `078f879d fix(Task 909): add missing RasterizerState::CullNone to 2 Vulkan BasicEffect tests` actually touches
  this file, confirming the header's account of the fix's provenance.
- Shares the shared `lit_textured3d.frag.glsl`/`lit_textured3d_vertexlit` multi-light sum formula with
  `vulkan_basiceffect_one_light_test.cpp` and `vulkan_basiceffect_specular_test.cpp` — this file's independent
  confirmation of the `L0+L1+L2` summation and per-light `Enabled` gating strengthens confidence in the shared
  production code across all three files.

## Missing or Weak Tests

- No case tests all three lights simultaneously disabled (would reduce to pure ambient+emissive, a trivial but
  currently-unexercised combination given the other checks only disable one light at a time).
- No case tests `FogEnabled=true` in combination with multi-light — same reasonable scope-limit note as the
  other lighting-focused files in this batch.

## Positive Findings

- All three checks independently re-derived and confirmed exactly correct.
- The per-light-per-channel test design (L0→red, L1→green, L2→blue) is a genuinely elegant way to prove 3
  independent lights are wired correctly without needing to solve a system of equations to disentangle a shared
  channel — worth calling out as good test-authoring practice relative to some sibling files' more entangled
  channel assertions.
- `EmissiveColor`'s "added after the light-sum×DiffuseColor multiply, not scaled by it" placement (matching FNA's
  `Lighting.fxh`) is correctly exercised and confirmed via check 1's exact numeric match.

## Final Assessment

A well-constructed, fully-verified multi-light + emissive test with no corrective action needed. The only note
carried over from cross-file work is a shared, currently-harmless degenerate-specular-input pattern (also present
in `vulkan_basiceffect_one_light_test.cpp`) that would be worth a defensive guard in the production shader if this
logic is ever ported to a target without SPIR-V's NaN-avoiding `max()` guarantee.
