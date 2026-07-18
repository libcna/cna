# Audit: examples/bgfx_environmentmapeffect_eyeposition_test.cpp

## Metadata

- Source file: `examples/bgfx_environmentmapeffect_eyeposition_test.cpp` (187 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `EnvironmentMapEffect`'s reflection vector responding to
  the derived `EyePosition`, Bgfx backend, Task 397.
- CTest registration: `cna_bgfx_test(cna_test_bgfx_environmentmapeffect_eyeposition …)` /
  `cna_register_backend_test(NAME Bgfx_EnvironmentMapEffect_EyePosition …)`
  (`cmake/Tests/BgfxTests.cmake:219-221`).
- XNA/FNA relevance: direct — `EnvironmentMapEffect.EyePosition` is not itself a public settable
  property; it is derived internally from `View` (`IEffectMatrices`), matching
  `EffectHelpers.SetLightingMatrices` in FNA.
- FNA reference: `HLSL/EnvironmentMapEffect.fx`'s `ComputeEnvMapVSOutput`: `eyeVector =
  normalize(EyePosition - pos_ws.xyz)`, `EnvCoord = reflect(-eyeVector, worldNormal)`.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp`
  (`OnApply()`/`FillGpuDrawParams()` deriving `eyePositionWorld` from
  `Matrix::Invert(view_).getTranslationProperty()`, lines 346-355 and 451-456),
  `src/CNA/Internal/Backends/Bgfx/shaders/vs_env_map3d.sc` (`v_eyeDir = u_eyePos.xyz - worldPos`),
  `src/CNA/Internal/Backends/Bgfx/shaders/fs_env_map3d.sc` (per-pixel `reflect(-E,N)`).

## Purpose

Two-check pixel test proving the cube-map reflection direction genuinely tracks `EyePosition`
(derived from `View`), not a hardcoded constant. `EnvironmentMapAmount=1`,
`EnvironmentMapSpecular=(0,0,0)`, `FresnelFactor=0` (line 107-110) so the blend factor is a flat `1`
and the rendered pixel becomes *exactly* the sampled cube-map texel (no lit-texture contribution
mixed in). Check (a) places the eye straight on at `(0,0,3)`; check (b) moves it off-axis to
`(5,0,0.5)`, expecting a different cube face to be selected.

## Executive Verdict

**Healthy** — both expected cube faces were independently re-derived via the actual reflection
vector math and match exactly (not merely "close enough" under the `±20` tolerance).

## Checklist Results

### API / XNA / FNA parity
`EyePosition` is correctly *not* exposed as a settable property here (matching FNA, where it is
computed, never assigned by user code) — the test drives it indirectly via `setViewProperty()`
(lines 112, 156, 161), which is the only correct way to influence it, matching
`EnvironmentMapEffect::setViewProperty()`'s dirty-flag wiring (`DirtyEyePosition`, `.cpp:127-131`).

### Behavioral correctness — full independent re-derivation
With `emissiveColor_=(0,0,0)` and `DirectionalLight0`'s default diffuse `Vector3::Zero`
(`DirectionalLight.cpp:7`, despite `Enabled=true`), `litRGB=(0,0,0)`, so `baseColor=(0,0,0)`
regardless of the white `1×1` `Texture2D` (line 137-140). With `blendFactor=1` (Fresnel disabled,
`EnvironmentMapAmount=1`), `fs_env_map3d.sc`'s `mix(baseColor, envSample*combinedAlpha, 1)` reduces
to exactly `envSample` (`combinedAlpha=1` since `DiffuseColor` stays at its default `(1,1,1,1)` and
the texture is opaque white) — the rendered pixel is *precisely* the sampled cube-map texel, no
approximation needed to reason about it.
- Check (a): `eye=(0,0,3)`, `target=Zero`, `up=(0,1,0)`, quad normal `N=(0,0,1)`. At the sampled
  fragment (screen center, world position ≈ `(0,0,0)` on the quad), `E=normalize(eyePos-worldPos)
  ≈(0,0,1)`. `reflect(-E,N) = -E - 2·dot(-E,N)·N = (0,0,-1) - 2·(-1)·(0,0,1) = (0,0,1)` →
  `PositiveZ` face → **blue** `(0,0,255)`. Matches `Color(0,0,255,255)` (line 157-159) exactly.
- Check (b): `eye=(5,0,0.5)`. `E≈normalize((5,0,0.5))≈(0.9938,0,0.0994)`. `dot(-E,N)=-E.z=-0.0994`.
  `reflect(-E,N)=-E-2·(-0.0994)·N=(-0.9938,0,-0.0994)+(0,0,0.1988)=(-0.9938,0,0.0994)` — dominant
  component `-X` → `NegativeX` face → **cyan** `(0,255,255)`. Matches `Color(0,255,255,255)` (line
  163-165) exactly.

Both checks are exact algebraic derivations, not tolerance-assisted approximations — a genuinely
strong, discriminating test (a broken `EyePosition` wiring, e.g. one hardcoded to a fixed value or
never updated from `View`, would make check (b) render the *same* face as check (a), which the test
would correctly catch since the two expected colors are distinct primaries).

### Logic
`makeDistinctCube()` (lines 82-98) assigns a different solid color to each of the 6 cube faces —
the correct minimal fixture to make "which face got selected" independently observable per pixel,
without needing a gradient or multi-texel face.

### C++ correctness
`renderWith()`'s retry loop (lines 116-128, up to 20 iterations, breaking on the first non-black
readback) is a standard anti-flakiness idiom used throughout this shard; since both expected colors
are non-black in at least one channel, a genuine rendering failure (all-black output) would exhaust
the loop and correctly fail the subsequent `colourMatch` check rather than false-passing.

### Testing
See "Missing or Weak Tests" below — a cross-cutting production-code defect this file's own
configuration happens not to exercise.

## Detailed Findings

### F1 — `EnvironmentMapEffect`'s Bgfx (and Vulkan) fragment shader re-multiplies `EmissiveColor` by `DiffuseColor`, diverging from FNA whenever `DiffuseColor != white`; masked here because this test never varies `DiffuseColor`

- Severity: HIGH
- Confidence: HIGH
- Category: fna-parity (production code exercised by, but not exposed by, this test)
- Location/symbol: `src/CNA/Internal/Backends/Bgfx/shaders/fs_env_map3d.sc:28` — `vec3 litRGB =
  (u_emissiveColor.xyz + lightSum) * u_diffuseColor.xyz;`; the identical defect is independently
  reproduced in `src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.frag.glsl:39` — `vec3 litRGB =
  (ep.emissive_em.xyz + lightSum) * ep.diffuseColor.rgb;`
- Evidence: FNA's real formula (`HLSL/Lighting.fxh`'s `ComputeLights()`: `result.Diffuse =
  mul(diffuse, lightDiffuse) * DiffuseColor.rgb + EmissiveColor;`) multiplies *only* the per-light
  diffuse sum by the GPU `DiffuseColor` constant and adds the (already pre-combined) `EmissiveColor`
  constant afterward, unscaled. `EnvironmentMapEffect::FillGpuDrawParams()` correctly prepares both
  GPU-facing values to match this (`p.diffuseColor = diffuseColor_*alpha_` and `p.emissiveColor =
  (emissiveColor_ + ambientLightColor_*diffuseColor_)*alpha_`, `.cpp:418-426`) — the bug is
  purely in the shader recombining them. Concretely, with `DiffuseColor=(0,0,0)` (fully "unlit"
  black material) and `EmissiveColor=(1,1,1)` (pure glow), real FNA renders white (`0·lightSum +
  (1,1,1)`); this shader instead computes `(1,1,1)·(0,0,0) = black`, extinguishing the emissive term
  entirely.
- Why it matters: any game setting a non-default `DiffuseColor` together with non-zero
  `EmissiveColor`/`AmbientLightColor` on `EnvironmentMapEffect` renders visibly wrong (tinted or
  blacked-out emissive/ambient) on the Bgfx and Vulkan backends relative to real XNA/FNA.
- This file's own coverage: this test sets `EmissiveColor=(0,0,0)` (line 107) and never calls
  `setDiffuseColorProperty()` at all (stays at its ctor default `Vector3::One`), so
  `(emissive+lightSum)*diffuseColor` and `lightSum*diffuseColor+emissive` are numerically identical
  here (`0` either way, since emissive is itself zero and `diffuseColor=1` is a no-op multiplier) —
  this test cannot detect the defect, by construction, not by oversight of its own stated purpose
  (it targets `EyePosition`, not `DiffuseColor`/`EmissiveColor` interaction).
- FNA/XNA comparison: direct formula mismatch against `Lighting.fxh`/`EnvironmentMapEffect.fx`
  (cited above).
- Related files: `src/CNA/Internal/Backends/Bgfx/shaders/fs_env_map3d.sc`,
  `src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.frag.glsl` (both outside this audit batch's
  file list; consulted per the audit's instruction to check production code a test exercises).
- Suggested future action (not implemented by this audit): change the shader line to `litRGB =
  lightSum * u_diffuseColor.xyz + u_emissiveColor.xyz`.
- Not previously tracked: `git log` on `fs_env_map3d.sc` shows Tasks 278/394/395/396/890/891/899, none
  of which touch this specific diffuse/emissive recombination; not present in `known_bugs.md`.

## Cross-File Observations

- Per Task 364/884 (comment lines 6-10), Bgfx's default `RasterizerState` cull mode is
  `CullCounterClockwiseFace` (verified: `RasterizerState.cpp:11` sets this as the ctor default, and
  `BgfxGraphicsBackend.cpp:1781-1782` maps it to `BGFX_STATE_CULL_CCW`) — the real FNA default — so
  this test's standard quad winding needs the explicit `RasterizerState::CullNone` workaround (line
  122) that every file in this shard applies identically. This claim was independently re-verified
  against current code (not merely trusted from the comment), consistent with prior findings in the
  sibling EasyGL/Vulkan shards about stale claims needing re-verification — this one holds up.
- Same production code (`EnvironmentMapEffect.cpp`, `vs_env_map3d.sc`, `fs_env_map3d.sc`) is shared
  by all six `bgfx_environmentmapeffect_*_test.cpp` files in this batch; F1 recurs identically across
  all of them (each with its own local masking reason).

## Missing or Weak Tests

- See F1 — no file in this six-file family varies `DiffuseColor` away from its default, so the
  emissive/diffuse recombination defect is systemically uncaught by this entire test family.

## Positive Findings

- Both checks are exact (not tolerance-assisted) algebraic derivations of the real reflection-vector
  math, genuinely discriminating a broken `EyePosition`→reflection wiring.
- `makeDistinctCube()`'s six-distinct-color fixture is the correct minimal design to make "which
  face was selected" unambiguously observable from a single central pixel read.

## Final Assessment

A precise, correctly-designed `EyePosition` test whose own two assertions are exactly right; this
audit's independent review of the underlying `EnvironmentMapEffect` shader surfaced a genuine,
previously untracked `DiffuseColor`/`EmissiveColor` recombination bug (F1) that this file's own
scene (by design, not oversight) does not exercise.
