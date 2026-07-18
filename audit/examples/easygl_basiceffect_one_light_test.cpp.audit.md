# Audit: examples/easygl_basiceffect_one_light_test.cpp

## Metadata

- Source file: `examples/easygl_basiceffect_one_light_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — `BasicEffect` single-directional-light pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_easygl_test(cna_test_easygl_basiceffect_one_light …)` /
  `cna_register_backend_test(NAME EasyGL_BasicEffect_OneLight …)`, `cmake/Tests/EasyGLTests.cmake:1097-1099`).
- XNA/FNA relevance: direct — exercises `BasicEffect`'s `LightingEnabled`/`AmbientLightColor`/
  `DirectionalLight0` (`IEffectLights`).
- FNA reference: `Graphics/Effect/StockEffects/BasicEffect.cs` (`OnApply()` shader-index selection),
  `HLSL/Lighting.fxh` (`ComputeLights`), `EffectHelpers.cs` (`SetMaterialColor`),
  `DirectionalLight.cs` (`Enabled` setter zeroing GPU-facing color).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp`
  (`FillGpuDrawParams()` lines 51-141), `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`
  (`EnsureLit3DVertexLitProgram()` lines 2893-3010, `SelectProgram()` lines 3960-3979).

## Purpose

`BasicEffectOneLightTest` renders a full-screen textured (flat-white 1×1 texture) quad with
`LightingEnabled=true` and only `DirectionalLight0` active, using a non-saturating normal
(`NdotL=0.5`) to prove the diffuse term is a real dot product, a back-facing normal to prove the
negative-dot clamp, and `DirectionalLight0.Enabled=false` to prove a disabled light contributes
exactly zero. The file's own header comment (lines 24-34) documents a bug this test found and fixed:
`BasicEffect::FillGpuDrawParams()` previously forwarded `DirectionalLight0`'s color unconditionally,
ignoring `Enabled`.

## Executive Verdict

**Healthy** — every expected pixel value was independently re-derived from FNA's actual lighting
formula and the current `EnsureLit3DVertexLitProgram()` GLSL, and both match exactly (see F1). The
claimed "found and fixed" `DirectionalLight0.Enabled` gating bug is confirmed present and correct in
the current `BasicEffect.cpp` (lines 85-91), consistent with git history (`0ad49974 fix(Task 368):
honor DirectionalLight0.Enabled`).

## Checklist Results

### API / XNA / FNA parity
`setLightingEnabledProperty`/`setAmbientLightColorProperty`/`setDiffuseColorProperty`/
`DirectionalLight0.setEnabledProperty`/`setDirectionProperty`/`setDiffuseColorProperty` (lines
133-138) all map 1:1 to FNA's `IEffectLights`/`DirectionalLight` properties. `PreferPerPixelLighting`
is deliberately left at its real default (`false`), which routes this test through the per-vertex-lit
shader family rather than `EnsureLit3DProgram()`'s per-pixel family — correctly not exercised here
(covered by the sibling `preferperpixellighting` test instead).

### Behavioral correctness
Re-derived the FNA "Desired lighting model" directly from `EffectHelpers.cs` line 192
(`((AmbientLightColor + sum(diffuse directional light)) * DiffuseColor) + EmissiveColor`) and
confirmed `EnsureLit3DVertexLitProgram()`'s vertex shader (line 2953-2954:
`vec3 lightSum=uAmbientColor+uLight0Diffuse*NdotL0+...; vLitRGB=lightSum*uDiffuseColor.rgb+uEmissiveColor;`)
computes the algebraically identical quantity to FNA's alpha-premultiplied, ambient-folded-into-emissive
optimization (`EffectHelpers.SetMaterialColor`'s lit branch, lines 211-226) — verified by full
expansion, not just visual similarity (see this shard's cross-cutting note below).
With `kNormalLit=(0.8660254,0,-0.5)` and `kLightDir=(0,0,1)`: `dot(-lightDir,N)=0.5` exactly, giving
`litRGB=(0.1,0.1,0.1)+(1.0,0.6,0.2)*0.5)*  (0.5,0.5,1.0) = (0.6,0.4,0.2)*(0.5,0.5,1.0) =
(0.3,0.2,0.2)` → `×255 = (76.5,51,51)`, matching `kExpectedLit(77,51,51,255)` within the fixed ±8
tolerance (line 111-118) — the 0.5-unit rounding gap is FP/GPU-rounding, not a formula error.
`kNormalBackFacing` produces `dot(-lightDir,N)=-0.5`, correctly clamped to 0 by `NdotL0=max(dotL0,0.0)`
in the shader (line 2950), giving ambient-only `(0.1,0.1,0.1)*(0.5,0.5,1.0)*255=(12.75,12.75,25.5)`,
matching `kExpectedAmbientOnly(13,13,26,255)`. `DirectionalLight0.Enabled=false` reuses the lit
normal but must zero the light entirely via `FillGpuDrawParams()`'s `light0On` gate (line 85-86) —
same expected constant, correctly asserted.

### Logic
`renderWith()`'s per-call `RasterizerState::CullNone` override (line 156, "Task 896 finding") is the
same established workaround used across this shard's sibling `BasicEffect` tests. The 20-iteration
"skip blank/black frames" retry loop (lines 149-161) is likewise the shard's standard idiom.

### C++ correctness
`matches()`'s `closeTo(...,8)` tolerance (lines 111-118) is checked against the theoretical minimum
gap between `kExpectedLit`/`kExpectedAmbientOnly`/`kFullySaturated`/`kAmbientIgnored` — the closest
pair is `kExpectedLit.G=51` vs `kAmbientIgnored.G=38` (gap 13) and `kExpectedLit.B=51` vs
`kFullySaturated.B=77` (gap 26); both comfortably exceed 2×8, so no false-pass risk from tolerance
overlap between the "correct" and "wrong-formula" reference constants.

### Memory/resource lifetime
`gdm_` is a `std::unique_ptr<GraphicsDeviceManager>` constructed in the test's own constructor —
standard pattern; `BasicEffect fx(dev)` and `Texture2D tex(dev,1,1)` are stack-local per `Draw()` call
with no ownership hazards.

### Architecture
Correctly scoped to the EasyGL backend only; no backend-specific leakage into the assertions
(`renderWith()`'s abstractions are all XNA-facing `GraphicsDevice`/`BasicEffect` calls).

### Robustness
The three negative assertions (`!matches(litGot, kFullySaturated)`, `!matches(litGot,
kAmbientIgnored)`) are the right technique for proving the dot product is real rather than a
saturated boolean — this is exactly the anti-boilerplate bar the audit checklist asks for, not just
"pixel is non-black."

### Testing
Genuinely discriminates three plausible wrong implementations (boolean lit/unlit, ambient dropped,
back-face not clamped) from the correct formula, in addition to the `Enabled` gate. No boundary case
for `NdotL` exactly `0` (perpendicular light) is tested, but that is a reasonable, proportionate scope
choice for a file whose stated purpose is the non-saturating middle case.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — Lit-path formula and "Enabled" gate independently re-verified against FNA + current shader source

- Severity: INFO
- Confidence: HIGH
- Category: correctness (verification note)
- Location/symbol: `kExpectedLit`/`kExpectedAmbientOnly`/`kFullySaturated`/`kAmbientIgnored` (lines
  80-87); `BasicEffect::FillGpuDrawParams()` (`BasicEffect.cpp` lines 85-91);
  `EnsureLit3DVertexLitProgram()` (`EasyGLGraphicsBackend.cpp` lines 2950-2954)
- Evidence: see Behavioral correctness above; cross-checked against `EffectHelpers.cs` lines
  190-226 and `DirectionalLight.cs`'s `Enabled` setter semantics (light must zero both diffuse and
  specular contributions when disabled, not merely skip being drawn).
- Why it matters: recorded so the next audit pass doesn't need to re-derive this; the file's own
  claimed bug-fix narrative (lines 24-34) is corroborated by both the live source and
  `git log` (`0ad49974 fix(Task 368): honor DirectionalLight0.Enabled`).

## Cross-File Observations

- `BasicEffect::FillGpuDrawParams()`'s lit branch computes `p.ambientColor`/`p.emissiveColor`
  unmixed with alpha, relying on `p.diffuseColor` already carrying the alpha factor so that the
  shader's `lightSum*uDiffuseColor.rgb` multiply correctly re-introduces alpha into the ambient term
  — this is a different (but provably equivalent) factoring than FNA's own C#-side
  ambient-folded-into-emissive optimization. Worth noting in the `BasicEffect.cpp`/`xna-graphics`
  shard audit as a deliberate, verified-correct architectural deviation, not an oversight.
- This file, `easygl_basiceffect_specular_test.cpp`, and
  `easygl_basiceffect_preferperpixellighting_test.cpp` all independently exercise the same
  `EnsureLit3DVertexLitProgram()`/`EnsureLit3DProgram()` pair from different angles (diffuse-only,
  specular, and dispatch-selection respectively) — collectively good coverage of that shared code
  path, worth noting in whatever cross-cutting doc tracks `BasicEffect` lit-path coverage.

## Missing or Weak Tests

No dedicated case for `DirectionalLight1`/`DirectionalLight2` contributing alongside `DirectionalLight0`
(all three lights summed) — reasonable to defer to `EnableDefaultLighting()`'s own dedicated test
(`BasicEffectTests.cpp`), not a gap specific to this "one light" file's stated scope.

## Positive Findings

- Deliberate choice of a non-saturating `NdotL=0.5` angle (rather than the easier 0/1 degenerate
  case) is exactly the right test design to catch a boolean-lit regression that a saturated test
  would miss.
- The `Enabled=false` check reusing the *lit* geometry (rather than a trivially-dark scene) correctly
  isolates "light contributes zero" from "nothing is lit at all."

## Final Assessment

A precise, well-targeted pixel test whose expected values check out exactly against both FNA's own
lighting-model derivation and the current EasyGL shader source; its claimed bug narrative matches
the actual commit history.
