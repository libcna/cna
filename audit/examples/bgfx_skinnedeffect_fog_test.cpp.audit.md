# Audit: examples/bgfx_skinnedeffect_fog_test.cpp

## Metadata

- Source file: `examples/bgfx_skinnedeffect_fog_test.cpp` (188 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `SkinnedEffect` linear-fog pixel integration test
- File type: standalone `Game`-subclass executable, CTest-registered as `Bgfx_SkinnedEffect_Fog`
  (`cmake/Tests/BgfxTests.cmake:443-446`)
- XNA/FNA relevance: direct — `SkinnedEffect.FogEnabled`/`FogColor`/`FogStart`/`FogEnd` (`IEffectFog`).
- FNA reference: `StockEffects/SkinnedEffect.cs` constructor (`DirectionalLight0.Enabled = true`, always,
  unconditionally, lines 380-396); `IEffectLights.LightingEnabled`'s hard-throw-on-`false` setter
  (lines 365-368) confirming SkinnedEffect's lighting can never be turned off.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/SkinnedEffect.cpp` (constructor lines
  36-50, `DirectionalLight` default-construction chain), `src/Microsoft/Xna/Framework/Graphics/DirectionalLight.cpp`
  (default member values, lines 6-12), `src/CNA/Internal/Backends/Bgfx/shaders/vs_skinned3d_vertexlit.sc`
  (lines 55-73, the per-vertex lighting math this test's default-light state feeds into), `fs_skinned3d_vertexlit.sc`
  (fog blend, line 26).

## Purpose

Isolates fog from skinning/lighting by using an identity bone palette (weight `1.0` on bone `0`, which
defaults to `Matrix::Identity`, so geometry is undeformed) and `EmissiveColor` as the sole material
colour source, deliberately **not** calling `EnableDefaultLighting()` so `DirectionalLight0` contributes
nothing. Three checks against the documented formula
`fogFactor = clamp((FogEnd - Z) / (FogEnd - FogStart), 0, 1); finalRGB = mix(FogColor, geomRGB, fogFactor)`:
(a) fog disabled at `Z=0` → pure emissive blue; (b) fog at 50% (`Z=0.5, FogStart=0, FogEnd=1`, red fog)
→ `mix(red, blue, 0.5) = (128,0,128)`; (c) full fog (`FogEnd=0.5, Z=0.9` → `fogFactor=clamp(-0.8,0,1)=0`)
→ pure red.

## Executive Verdict

**Needs attention** — this audit independently re-derived all three fog checks against the actual
`vs_skinned3d_vertexlit.sc`/`fs_skinned3d_vertexlit.sc` formula and confirmed the arithmetic matches
exactly (see Checklist below). However, tracing *why* this test's chosen approach to "isolate fog from
lighting" is actually safe surfaced a real, source-grounded latent risk (F1): the test relies on
`DirectionalLight0`'s default `Direction=Vector3::Zero` combined with default `DiffuseColor=Vector3::Zero`
to make the always-on `DirectionalLight0.Enabled=true` light a genuine no-op, but the vertex shader
unconditionally calls `normalize()` on that zero-length direction vector before the zero diffuse colour
ever gets multiplied in — a theoretically NaN-producing operation that depends on GPU/driver-specific
"multiply-by-zero-ignores-NaN" behaviour to actually resolve to the harmless zero contribution the test
assumes. This is flagged as MEDIUM/LOW-confidence since there is no direct evidence (this audit did not
rebuild/run bgfx in this sandbox) that it currently manifests as a visible failure — the identical
pattern is shared, unmodified, across the EasyGL/Vulkan ports of this same test, which this audit did
not find flagged as broken anywhere in the project's own tracking docs.

## Checklist Results

### API / XNA / FNA parity
`fx.setFogEnabledProperty()`/`setFogColorProperty()`/`setFogStartProperty()`/`setFogEndProperty()`
(lines 115-118) match FNA's `IEffectFog` surface on `SkinnedEffect`. Confirmed via FNA's actual
`StockEffects/SkinnedEffect.cs` (lines 380-396) that the real class's constructor unconditionally sets
`DirectionalLight0.Enabled = true` and that `IEffectLights.LightingEnabled`'s setter is a hard
`NotSupportedException` when set to `false` (lines 365-368) — i.e. this is not a CNA deviation, it is
faithful, intentional XNA/FNA behavior that `SkinnedEffect` always computes lighting, unlike
`BasicEffect`. This test's whole "isolate fog from lighting by relying on default DiffuseColor=0" design
is a direct, correct consequence of that real API contract (there is no `LightingEnabled=false` escape
hatch to use instead).

### Behavioral correctness
Independently re-derived all three checks against `vs_skinned3d_vertexlit.sc`'s fog-factor formula
(lines 76-78, `v_fogFactor = (u_fogParams.x > 0.5) ? clamp((FogEnd - Z)/(FogEnd - FogStart), 0, 1) : 1.0`)
and `fs_skinned3d_vertexlit.sc`'s blend (line 26, `gl_FragColor.rgb = mix(u_fogColor.xyz, gl_FragColor.rgb, v_fogFactor)`):
- (a) `fogEnabled=false` → `v_fogFactor=1.0` → `mix(fogColor, blue, 1.0) = blue`. Matches `kBlue`.
- (b) `Z=0.5, FogStart=0, FogEnd=1` → `fogFactor = (1-0.5)/(1-0) = 0.5` → `mix(red, blue, 0.5) =
  (127.5, 0, 127.5) ≈ (128,0,128)`. Matches the asserted `Color(128,0,128,255)` with the test's own ±30
  tolerance (line 91-96) comfortably covering the 0.5-rounding difference.
- (c) `FogStart=0, FogEnd=0.5, Z=0.9` → `fogFactor = clamp((0.5-0.9)/(0.5-0), 0, 1) = clamp(-0.8,0,1) = 0`
  → `mix(red, blue, 0) = red`. Matches `kRed`.
All three match the shader's real, current formula exactly, not merely the file's own comment's restated
formula (independently substituted the concrete numbers rather than trusting the comment's claim that
they "match").

### Logic — see F1
Traced *why* "no directional light contribution" (line 20 of the file's header) is actually true given
`SkinnedEffect`'s FNA-mandated always-on `DirectionalLight0.Enabled=true`, and found the chain relies on
two independent zero defaults (`Direction` and `DiffuseColor`, both `Vector3::Zero` per
`DirectionalLight.cpp:6-12`) rather than the `Enabled` flag itself gating anything at the GPU level for
direction. This is architecturally consistent with `BasicEffect`'s identical `light0On ? ld :
Vector3::Zero` gating pattern (independently seen in `SkinnedEffect.cpp:344-348`, which forwards `dir`
to the GPU **unconditionally**, regardless of `light0On`) — so this is a systemic pattern across this
effect family's CPU→GPU parameter forwarding, not unique to this file or to Bgfx.

### C++ correctness
N/A for this file directly (the risk in F1 lives in shader/GPU floating-point semantics, not C++ code),
but flagged because this file is the one member of this shard's `SkinnedEffect` tests that actually
exercises the zero-`Direction`/zero-`DiffuseColor` default path (the combined test in this same shard
calls `EnableDefaultLighting()`, which sets a real non-zero `Direction`, sidestepping this entirely).

### Robustness
See F1 — the CPU-side code has no explicit guard (e.g. skipping the `normalize()` or substituting a
safe placeholder direction when a light's diffuse/specular contribution is already known to be zero);
correctness currently depends entirely on the GPU/shader-compiler's own IEEE-754/fast-math handling of
`normalize(vec3(0,0,0))` combined with a subsequent multiply-by-zero.

### Testing
The three checks correctly and tightly bracket the fog formula's three interesting regions (0%, 50%,
saturated-to-0%) with exact hand-derivable expected values — a strong, well-targeted test for its stated
purpose.

## Detailed Findings

### F1 — Test's "no light contribution" isolation depends on GPU-specific NaN-suppression for `normalize(vec3(0,0,0))`, not an explicit CPU-side guard
- Severity: MEDIUM
- Confidence: LOW (plausible, source-grounded concern; not independently reproduced by running the
  actual bgfx renderer in this sandbox — no bgfx build tree is available per this audit's D-6
  reference-only scoping of external upstream libraries — and the identical, unmodified pattern is
  shared across the EasyGL/Vulkan ports of this same test file without any documented failure)
- Category: robustness / correctness (edge-case, floating-point)
- Location/symbol: `SkinnedEffect.cpp:344-348` (`p.light0Dir` set unconditionally from
  `DirectionalLight0.getDirectionProperty()`, regardless of `light0On`); `vs_skinned3d_vertexlit.sc:56`
  (`vec3 nL0 = normalize(u_light0Dir.xyz);`); this test's own omission of `EnableDefaultLighting()`
  (contrast with `bgfx_skinnedeffect_combined_test.cpp`, which does call it)
- Evidence: `DirectionalLight::DirectionalLight()` (`DirectionalLight.cpp:6-12`) default-constructs
  `direction_(Vector3::Zero)` and `diffuseColor_(Vector3::Zero)`; `SkinnedEffect`'s constructor
  (`SkinnedEffect.cpp:42`) sets `DirectionalLight0.setEnabledProperty(true)` but never touches
  `Direction`/`DiffuseColor` unless `EnableDefaultLighting()` is separately called (which sets real
  values at lines 224-238) — this test calls neither. `normalize(vec3(0,0,0))` is `0 * (1/sqrt(0))`,
  and `1/sqrt(0) = +Inf` under IEEE-754; `0 * Inf = NaN` (not `0`) under strict IEEE-754 semantics, with
  no exception for a subsequent `NaN * 0.0` (`u_light0Diffuse.xyz = (0,0,0)`) — that product is also
  `NaN`, not `0`, under strict semantics.
- Why it matters: if a future compiler/driver/optimization-flag change (or a different backend/GPU
  combination) ever stops silently suppressing this NaN (e.g. via a `-ffast-math`-style "multiply by a
  literal/known-zero is always zero" simplification, which many GLSL/HLSL compilers apply but which is
  not guaranteed by any specification for a *runtime* uniform value of zero), this test's checks (b) and
  (c) — and any real game relying on the same "`SkinnedEffect` + never-configured `DirectionalLight0`
  + no `EnableDefaultLighting()`" combination — could silently start rendering NaN-polluted pixels
  (typically visually black, fully transparent, or otherwise corrupted, depending on how the specific
  GPU/driver handles NaN during rasterization/blending) instead of the intended "light contributes
  nothing" result. This is not a CNA-introduced deviation from FNA — the same unconditional
  `Direction`-forwarding-regardless-of-enabled-diffuse pattern is inherited from FNA's own
  `Lighting.fxh`/`ComputeLights()` contract — so a permanent fix would need to originate at the shared
  effect-parameter level (e.g. also zeroing `Direction` to a safe non-degenerate unit vector, or gating
  the `normalize()` itself, whenever a light's own diffuse+specular contribution is already known to be
  zero), not something specific to this test file or the Bgfx backend.
- FNA/XNA comparison: this is a genuine XNA/FNA-inherited API design trait (SkinnedEffect always has
  `DirectionalLight0.Enabled=true` with an unset `Direction` until configured), not a CNA deviation —
  real XNA/D3D9 HLSL shader hardware has the identical IEEE-754 `normalize(0)`→NaN mathematical
  exposure, so this risk (if it manifests at all) would be inherent to the original XNA design too, not
  something CNA introduced while porting.
- Related files: `bgfx_skinnedeffect_combined_test.cpp` (audited separately in this batch) sidesteps
  this entirely by calling `EnableDefaultLighting()`, giving `Direction` a real non-zero value.
- Suggested follow-up (not implemented by this audit, per the audit-only mandate): none required
  immediately given no confirmed reproduction; worth a note in `plans/plan_graphics.md`/`known_bugs.md` as a
  "watch for" item if any future SkinnedEffect/BasicEffect/EnvironmentMapEffect pixel test starts
  showing unexplained black/NaN-like output on a light left `Enabled` with a default-zero `Direction`.

## Cross-File Observations

- Directly complements `bgfx_skinnedeffect_combined_test.cpp` — see that file's report for the contrast
  (`EnableDefaultLighting()` called there, not here) that motivated this finding.
- Shares the `SkinnedGpuVertex` 52-byte layout and general per-check `renderQuad`/retry-loop structure
  with the combined test in this same shard; both were independently verified rather than assuming
  identical code implies identical correctness.

## Missing or Weak Tests

None beyond the latent risk already covered in F1 — the three fog-formula checks themselves are
complete and well-targeted for their stated purpose.

## Positive Findings

- All three fog-formula checks were independently hand-derived against the real current shader source
  and match exactly — a genuinely verified, not just plausible-looking, set of assertions.
- The identity-bone-palette + emissive-only isolation technique is a clean, well-reasoned way to strip
  skinning and lighting out of a fog-focused test, and is correctly justified against `SkinnedEffect`'s
  real "lighting can never be fully disabled" API contract (confirmed against FNA source) rather than an
  incorrect assumption that lighting could simply be turned off.

## Final Assessment

The three fog-formula assertions are correct and independently verified against the real shader source.
This audit's deeper trace of *why* the test's lighting-isolation technique is safe surfaced a
theoretical, FNA-inherited floating-point edge case (F1) worth recording for future attention, though
this audit found no direct evidence it currently causes a visible failure in this or the sibling
EasyGL/Vulkan ports of the same test.
