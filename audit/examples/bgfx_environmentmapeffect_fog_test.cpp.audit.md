# Audit: examples/bgfx_environmentmapeffect_fog_test.cpp

## Metadata

- Source file: `examples/bgfx_environmentmapeffect_fog_test.cpp` (193 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `EnvironmentMapEffect`'s linear-fog pixel integration,
  Bgfx backend, Task 899 (direct port of `examples/easygl_environmentmapeffect_fog_test.cpp`, Task
  900).
- CTest registration: `cna_bgfx_test(cna_test_bgfx_environmentmapeffect_fog …)` /
  `cna_register_backend_test(NAME Bgfx_EnvironmentMapEffect_Fog …)`
  (`cmake/Tests/BgfxTests.cmake:438-440`).
- XNA/FNA relevance: direct — `IEffectFog.FogEnabled/FogColor/FogStart/FogEnd`.
- FNA reference: `HLSL/Common.fxh`'s `ComputeFogFactor`/`ApplyFog` (real formula uses the
  `FogVector` dot-product against clip-space position, a *view-space* linear fog, not raw
  object-space `Z`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp`
  (`OnApply()` computing `fogVectorParam_` from `worldView_`'s `M13/M23/M33/M43` terms and
  `fogStart_`/`fogEnd_`, lines 298-328 — this parameter is prepared correctly but never consumed),
  `src/CNA/Internal/Backends/Bgfx/shaders/vs_env_map3d.sc` (`v_fogFactor` computed from raw
  `a_position.z`, lines 26-30), `fs_env_map3d.sc` (`rgb = mix(u_fogColor.xyz, rgb, v_fogFactor)`,
  line 43).

## Purpose

Three-check pixel test verifying `EnvironmentMapEffect`'s fog blend on Bgfx, added because "Bgfx's
env_map3d shader pipeline never implemented fog at all" before this task (header comment lines
6-10). `EnvironmentMapAmount=0` and `EnvironmentMapSpecular=(0,0,0)` (lines 118-119) remove the
cube-map's RGB and specular-alpha contributions entirely, so the pre-fog color reduces to exactly
`EmissiveColor` — isolating the fog blend cleanly. Check (a): fog disabled → pure emissive blue.
Check (b): fog at 50% between red `FogColor` and blue emissive → purple mix. Check (c): fog fully
saturated → pure `FogColor` red.

## Executive Verdict

**Needs attention** — all three checks are exactly, independently re-derivable against the
*current* per-pixel object-space-Z fog formula and pass; however, that current formula itself is a
confirmed, documented simplification that diverges from FNA's real view-space linear fog whenever
`World`/`View` are non-identity (F1) — this file's own scene (`World=View=Projection=Identity`)
cannot distinguish the simplified formula from the real one, so it validates the implementation as
it stands today, not full FNA fog parity.

## Checklist Results

### Behavioral correctness — full independent re-derivation
With `World=View=Projection=Matrix::Identity` (lines 113-115), `DirectionalLight0`'s default zero
diffuse (`DirectionalLight.cpp:7`), and `DiffuseColor` left at its default `Vector3::One`:
`litRGB = (emissiveColor + 0) * (1,1,1) = emissiveColor = (0,0,1)` (blue, line 120).
`texColor=(1,1,1,1)` (white, line 156-157), so `baseColor = litRGB*texColor = (0,0,1)`.
`EnvironmentMapAmount=0` forces `blendFactor=0` regardless of the Fresnel branch taken (the
multiply-by-literal-zero masks any finite intermediate value), so `rgb` before fog = `baseColor =
(0,0,1)` for every check, independent of `z`.
- Check (a): `fogEnabled=false` → `vs_env_map3d.sc`'s `v_fogFactor = 1.0` (line 28-30, the `else`
  branch) → `rgb = mix(fogColor, baseColor, 1) = baseColor = (0,0,1)` → **blue** `(0,0,255)`.
  Matches `kBlue` (line 161-162) exactly.
- Check (b): `z=0.5`, `FogStart=0`, `FogEnd=1`, `FogColor=(1,0,0)` red. `v_fogFactor =
  clamp((1-0.5)/(1-0),0,1) = 0.5`. `rgb = mix((1,0,0), (0,0,1), 0.5) = (0.5,0,0.5)` → `(128,0,128)`
  (rounding `127.5`→`128`). Matches `Color(128,0,128,255)` (line 165-167) exactly.
- Check (c): `z=0.9`, `FogEnd=0.5`. `v_fogFactor = clamp((0.5-0.9)/(0.5-0.0),0,1) =
  clamp(-0.8,0,1) = 0`. `rgb = mix((1,0,0), (0,0,1), 0) = (1,0,0)` → **red** `(255,0,0)`. Matches
  `kRed` (line 170-171) exactly.

All three are exact, not approximate, and correctly match the comment's own stated formula (header
lines 12-14). This is a genuinely well-derived test of the formula as implemented today.

### Logic
The retry loop (lines 137-148) is the same anti-flakiness idiom used throughout this shard;
harmless here since none of the three expected colors is pure black.

### Robustness
With `EyePosition` derived at world `(0,0,0)` (`View=Identity`'s inverse translation) and the
sampled fragment's world position at `z` values `0`, `0.5`, `0.9` (never exactly `0` at the *read*
pixel, since the `64`-px backbuffer's pixel-center sampling offset shifts the read location off the
true geometric quad center by roughly one texel in `x`/`y` — `z` is unaffected either way), the
per-pixel `eyeVector`/`viewAngle` computation in `fs_env_map3d.sc` (used only for the Fresnel gate,
irrelevant here since `blendFactor` is forced to `0` by `EnvironmentMapAmount=0`) has no live NaN
risk in this specific scene. Noted as a theoretical (not observed) fragility class shared by every
file in this family that uses `View=Identity`: if a future variant used a *non-zero* `Amount` with
`View=Identity` and read the exact geometric center of a `z=0` quad, `eyeDir` could evaluate to
`(0,0,0)` and `normalize()` of it is undefined/`NaN` per GLSL spec — not triggered here because
`Amount=0` masks it and the read pixel isn't the exact center. LOW severity / theoretical, not
actionable for this file specifically.

## Detailed Findings

### F1 — EnvironmentMapEffect's fog is computed from raw object-space Z, not FNA's real view-space linear fog; a known-class defect (already flagged for EasyGL) reproduced identically here

- Severity: MEDIUM
- Confidence: HIGH
- Category: fna-parity
- Location/symbol: `src/CNA/Internal/Backends/Bgfx/shaders/vs_env_map3d.sc:26-30` — `v_fogFactor =
  (u_fogParams.x > 0.5) ? clamp((u_fogParams.z - a_position.z) / max(u_fogParams.z -
  u_fogParams.y, 1e-6), 0.0, 1.0) : 1.0;` uses the raw incoming vertex `Z` directly, entirely
  ignoring `World`/`View`.
- Evidence: real FNA fog (`HLSL/Common.fxh`) computes `ComputeFogFactor(position)` from a dot
  product of the object-space position against a precomputed `FogVector` (four floats derived from
  the combined `World*View` matrix's `M13/M23/M33/M43` and `FogStart`/`FogEnd`,
  `EffectHelpers.SetWorldViewProjAndFog` — mirrored correctly on the CPU side in this project's own
  `EnvironmentMapEffect::OnApply()`, `.cpp:298-320`, whose `fogVectorParam_` is computed but never
  actually forwarded to/consumed by the GPU-side uniform this shader reads). The shader-side formula
  instead treats the *raw, untransformed* vertex `Z` as if it were already view-space depth — correct
  only when `World`/`View` are Identity (as in this very test, lines 113-115) and wrong for any
  camera transform, translation, or rotation.
- Why it matters: two scenes with identical camera-relative distance from the fogged surface but
  different literal object-space `Z` values (e.g. a rotated or translated quad) will fog differently
  from real FNA/XNA, and a moving camera will not produce the expected fog falloff at all (object
  `Z` never changes as the camera moves).
- FNA/XNA comparison: `HLSL/Common.fxh` (`ComputeFogFactor`), `EnvironmentMapEffect.cpp:298-320`
  (`fogVectorParam_`, prepared correctly but orphaned).
- Related files: this exact simplification is also used by `vs_lit_textured3d_vertexlit.sc`
  (header comment lines 6-10 cites it as "matches Task 888's established formula exactly" and "the
  established pattern"), i.e. this is a project-wide, intentional (if XNA-incorrect) simplification,
  not unique to `EnvironmentMapEffect` or to Bgfx — a prior audit already flagged the identical
  defect for EasyGL (`feedback_easygl_fog_object_space_only`-class finding); this is the same
  systemic gap reproduced in the Bgfx backend.
- Why not previously caught by this file: this test deliberately uses `World=View=Identity` (lines
  113-115), the one configuration where raw object-space `Z` and true view-space depth coincide, so
  it cannot distinguish the simplified formula from the correct one — the test genuinely validates
  the formula *as implemented*, but the formula itself has a real, cross-cutting FNA-parity gap this
  scene cannot surface.
- Suggested future action (not implemented by this audit): wire the already-computed
  `fogVectorParam_`/`FogVector` through to the GPU (dot product against clip/view-space position)
  instead of raw object-space `Z`, project-wide.

## Cross-File Observations

- Per Task 364/896 (comment lines 19-21), the standard quad winding needs `RasterizerState::CullNone`
  under Bgfx's real `CullCounterClockwiseFace` default — independently re-verified against
  `RasterizerState.cpp:11` and `BgfxGraphicsBackend.cpp:1781-1782`, consistent with the sibling
  `eyeposition`/`fresnel`/etc. files in this batch.
- This file also exercises the same `EnvironmentMapEffect`/`fs_env_map3d.sc` production path as the
  other five files in this batch; the `DiffuseColor`/`EmissiveColor` recombination defect documented
  as F1 in the sibling `eyeposition` report is present here too but is masked identically (this file
  never varies `DiffuseColor` away from its default `(1,1,1,1)`, line range 113-124).

## Missing or Weak Tests

- See F1 — no scene in this file (or, per the header comment, in its EasyGL/Vulkan siblings using
  the same formula) exercises a non-Identity `World`/`View`, so the object-space-vs-view-space fog
  gap is untested by this entire family.

## Positive Findings

- All three checks are exact, non-tolerance-assisted derivations of the currently-implemented
  formula and correctly isolate fog from the cube-map/specular terms via `Amount=0`/`Specular=0`.
- The header comment's own formula statement (lines 12-14) is accurate and verified against the
  actual shader source, not merely asserted.

## Final Assessment

A correct, precisely-derived test of the fog blend *as currently implemented*; this audit's
cross-check against FNA's real fog model surfaced a genuine, already-known-elsewhere-in-spirit
(object-space-only fog) parity gap that this specific `Identity`-transform scene cannot by
construction detect.
