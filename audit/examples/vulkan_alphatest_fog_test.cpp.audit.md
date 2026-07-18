# Audit: examples/vulkan_alphatest_fog_test.cpp

## Metadata

- Source file: `examples/vulkan_alphatest_fog_test.cpp` (178 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `AlphaTestEffect` linear-fog pixel integration test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_alphatest_fog …)` /
  `cna_register_backend_test(NAME Vulkan_AlphaTest_Fog …)`, `cmake/Tests/VulkanTests.cmake:552-554`).
- XNA/FNA relevance: direct — `AlphaTestEffect.FogEnabled`/`FogColor`/`FogStart`/`FogEnd` (`IEffectFog`).
- FNA reference: `Graphics/Effect/StockEffects/EffectHelpers.cs::SetWorldViewProjAndFog()` (the real
  `FogVector` derivation, dotted against `worldView_`'s row 3 — i.e. genuinely **view-space** Z, not raw
  object-space Z), `HLSL/Common.fxh::ApplyFog()`.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/AlphaTestEffect.cpp` (`OnApply()` lines
  198-228 compute the FNA-faithful `FogVector`; `FillGpuDrawParams()` lines 329-335 instead forward plain
  `fogStart_`/`fogEnd_`/`fogColor` — the two are **not** the same computation, see F1),
  `src/CNA/Internal/Backends/Vulkan/shaders/alpha_test3d.vert.glsl` (`fragFogFactor` computed from raw
  `inPos.z`), `alpha_test3d.frag.glsl` (`mix(pc.fogColor, outColor.rgb, fragFogFactor)`).
- git corroboration: `401deaed`/`a42ae950` "feat(Task 888): real fog rendering on Bgfx (full) and Vulkan
  (partial)" matches this file's header comment's own "Task 888" attribution.

## Purpose

Isolates `AlphaTestEffect`'s linear-fog blend from its alpha-discard logic by using an always-passing
alpha configuration (default `AlphaFunction=Greater`/`ReferenceAlpha=0`, combined alpha always 1.0) and
sweeping the quad's Z coordinate through three points: `z=FogStart` (expect unblended material color),
`z=FogEnd` (expect pure fog color), and `z=0.5` (expect a genuine 50/50 blend, not an on/off snap). World/
View/Projection are left at `Identity` (never explicitly set — see F2), so raw vertex Z passed to the
vertex shader is also the value fed into `ComputeCommonVSOutput`'s effective "depth" for fog purposes.

## Executive Verdict

**Needs attention** — the three pixel assertions are internally consistent and their expected constants
were independently re-derived and confirmed correct *for the currently-implemented object-space-Z fog
formula*. However, that formula is a simplified stand-in for FNA's real `FogVector` (which is derived from
`World`/`View` via `worldView_.M13/M23/M33`, not raw object-space Z) — a limitation this test's own header
comment discloses candidly ("same methodology... as `easygl_alphatest_fog_test.cpp`") but does not itself
flag as a parity gap. Because the test never sets a non-identity `World`/`View`, it cannot and does not
distinguish "fog computed correctly from the view-space distance" from "fog computed from raw object-space
Z that happens to equal view-space Z here" — see F1.

## Checklist Results

### API / XNA / FNA parity
`setFogEnabledProperty`/`setFogColorProperty`/`setFogStartProperty`/`setFogEndProperty` (lines 106-109)
are the correct FNA `IEffectFog` surface. Values are plausible (`kFogStart=0.05`, `kFogEnd=0.95`,
kept inside Vulkan's `[0,1]` clip-space Z range per the file's own comment, lines 50-52).

### Behavioral correctness
Re-derived by hand against the *actual* implemented formula
(`alpha_test3d.vert.glsl:36-38`: `fragFogFactor = clamp((fogEnd - inPos.z) / (fogEnd - fogStart), 0, 1)`;
`alpha_test3d.frag.glsl:46`: `outColor.rgb = mix(fogColor, outColor.rgb, fragFogFactor)`):
- `z=kFogStart=0.05`: `factor = clamp((0.95-0.05)/(0.95-0.05),0,1) = 1.0` → `mix(fog, mat, 1) = mat` =
  `DiffuseColor*255 = (0.8,0.2,0.4)*255 = (204,51,102)` — matches `kExpectedNoFog` exactly.
- `z=kFogEnd=0.95`: `factor = clamp(0/(0.9),0,1) = 0` → `mix(fog, mat, 0) = fog` =
  `kFogColor*255 = (0.1,0.6,0.9)*255 = (25.5,153,229.5)` ≈ `(26,153,230)` — matches `kExpectedFullFog`
  (rounding to nearest is consistent with the file's own values).
- `z=0.5`: `factor = clamp((0.95-0.5)/0.9,0,1) = 0.5` → `mix = 0.5*fog + 0.5*mat` =
  `0.5*(25.5,153,229.5) + 0.5*(204,51,102) = (114.75,102,165.75)` ≈ `(115,102,166)` — matches
  `kExpectedHalfFog` exactly.
All three constants are independently confirmed correct for the current implementation.

### Logic
`renderAtZ()`'s retry loop (lines 125-133, up to 20 iterations skipping all-black frames) is a pragmatic
defense against a transient blank first frame, consistent with the pattern used across this entire shard
— but see F3 for a latent risk this pattern carries.

### C++ correctness
`matches()`'s `±8` tolerance (line 88) is proportionate to the ~1-unit float/rounding slop expected from
GPU interpolation and 8-bit quantization; none of the three checks is anywhere near a tolerance-driven
false pass (worst-case rounding gap observed above is <1 unit).

### Robustness
The three-point sweep (start/end/midpoint) is the right minimal design to prove interpolation rather than
a step function — reused correctly from the established EasyGL methodology this file's own comment cites.

### Testing
Genuinely discriminating for the *implemented* formula. Does **not** discriminate the implemented
object-space formula from FNA's real view-space `FogVector` formula, because `World`/`View` are never
set away from `Identity` in this scene (in which case object-space Z and view-space Z coincide by
construction) — see F1.

## Detailed Findings

### F1 — Fog is computed from raw object-space Z, not FNA's real view-space FogVector; this test cannot detect that gap because it never applies a non-identity World/View transform
- Severity: MEDIUM
- Confidence: HIGH (traced both formulas directly: `AlphaTestEffect::OnApply()` lines 198-219 correctly
  compute the FNA-faithful `FogVector` — `worldView_.M13*scale` etc. — via `worldViewProjParam_`/
  `fogVectorParam_`, i.e. the *shared XNA-shape effect parameters* are right; but
  `FillGpuDrawParams()` (the function that actually feeds the Vulkan/EasyGL backend push-constants used
  by this test) instead forwards plain `fogStart_`/`fogEnd_`/`getFogColorProperty()` at lines 329-335,
  which the Vulkan vertex shader combines with raw `inPos.z` — never consulting `World`/`View` at all)
- Category: correctness / FNA-parity (production code, not the test itself)
- Location: `AlphaTestEffect.cpp:329-335` (production); `alpha_test3d.vert.glsl:36-38` (Vulkan shader);
  this test file exercises the gap but does not surface it (no non-identity `World`/`View` case)
- Evidence: FNA's real formula (`EffectHelpers.cs::SetWorldViewProjAndFog`) is
  `fogVector = worldView.M13/M23/M33 * scale` dotted implicitly against the vertex's *object-space*
  position inside the vertex shader (`HLSL/Common.fxh`'s `mul(dot(...), FogVector)`-style computation) —
  which **is** sensitive to `World`'s translation/rotation, because `worldView_` bakes `World` in. The
  Vulkan implementation instead uses `inPos.z` directly with no `World`/`View` multiplication at all. With
  `World=View=Projection=Identity` (as used by every scene in this file), the two formulas happen to
  produce numerically identical results, which is exactly why this test cannot tell them apart. This
  mirrors a previously-documented finding for the EasyGL backend (project memory:
  `feedback_easygl_fog_object_space_only.md` — "fog shader reads raw local vertex Z, ignores World/View
  entirely"); this audit confirms the same limitation exists in the Vulkan `alpha_test3d`/`colored3d`/
  `colored_textured3d` shader family via direct source inspection.
- Why it matters: any real scene that translates or rotates geometry away from the origin (the normal
  case — this test's `Identity` World is the degenerate case) would fog incorrectly relative to actual
  camera distance under the current Vulkan implementation, and no test in this shard would catch it,
  because every fog test in this family (`vulkan_alphatest_fog_test.cpp`,
  `vulkan_basiceffect_colored3d_fog_test.cpp`, `vulkan_basiceffect_coloredtextured3d_fog_test.cpp`, and
  their sibling backends) uses `Identity` transforms exclusively.
- FNA/XNA comparison: FNA's fog is genuinely camera-distance-based (view-space); CNA's Vulkan
  implementation is object-space-only. This is a real, if narrow-scope, XNA behavioral deviation, not a
  test-authoring issue — the fix belongs in the production shader/backend code, not in this file.
- Related files: `src/Microsoft/Xna/Framework/Graphics/AlphaTestEffect.cpp`,
  `src/CNA/Internal/Backends/Vulkan/shaders/alpha_test3d.vert.glsl` and its `colored3d`/
  `colored_textured3d` siblings (all share the identical `Task 888`/`899` object-space formula per their
  own header comments).
- Suggested future action (not implemented by this audit): add one variant of this test with a
  non-identity `World` (e.g. a translation along Z) and a hand-derived expected fog factor using FNA's
  real view-space formula, to make the current object-space simplification visible as a known limitation
  rather than silently passing every existing test.

### F2 — World/View/Projection are never set on the `AlphaTestEffect` in this test; correctness relies on their default being `Identity`
- Severity: LOW
- Confidence: HIGH
- Category: test-authoring / API-default-reliance
- Location: `renderAtZ()` (lines 101-135) — no `setWorldProperty`/`setViewProperty`/`setProjectionProperty`
  calls anywhere in the file
- Evidence: `AlphaTestEffect`'s default-constructed `world_`/`view_`/`projection_` fields
  (`AlphaTestEffect.hpp`, not shown here but implied by `AlphaTestEffect(GraphicsDevice&)`'s constructor
  never assigning them) are presumably zero-initialized `Matrix` members, not `Matrix::Identity`, unless
  `Matrix`'s default constructor itself is identity. This audit did not re-verify `Matrix`'s default
  constructor value in this batch; if it defaults to all-zero rather than identity, this test (and its
  siblings using the same pattern) would silently render nothing rather than the expected quad — worth a
  follow-up spot-check, though the fact all three assertions pass with sensible non-zero pixel values
  strongly implies the default is indeed effectively `Identity` (or close enough for this identity-camera
  scene) in practice.
- Why it matters: relying on an implicit default rather than explicit `Matrix::getIdentityProperty()`
  calls (as `vulkan_basiceffect_colored3d_fog_test.cpp` in this same batch does explicitly) is slightly
  less defensive test-authoring, though not itself a proven defect.

### F3 — The up-to-20-iteration "skip blank frames" retry loop could mask a genuine one-shot rendering bug as a transient blank frame
- Severity: LOW
- Confidence: MEDIUM
- Category: test-robustness
- Location: `renderAtZ()` lines 125-133
- Evidence: the loop breaks as soon as *any* non-black pixel is read, including a wrong-but-nonzero
  color — it does not require the value to stabilize or match the expected color before breaking, so a
  bug that intermittently renders a wrong-but-nonzero pixel would be captured and checked (good), but a
  bug that renders correctly only every-other-frame due to a genuine race would be indistinguishable from
  "the usual first-frame blank" in the printed output.
- Why it matters: minor; this is the same established pattern used across this entire test shard (not
  unique to this file), and is a reasonable pragmatic tradeoff for pixel-readback tests immediately after
  device creation.

## Cross-File Observations

- F1's object-space-fog limitation is shared verbatim (same formula, same header-comment wording) across
  `vulkan_alphatest_fog_test.cpp`, `vulkan_basiceffect_colored3d_fog_test.cpp`, and
  `vulkan_basiceffect_coloredtextured3d_fog_test.cpp` — all three exercised in this batch use `Identity`
  transforms exclusively, so the same coverage gap applies to all three uniformly. This is recorded once
  here in detail and referenced (not repeated in full) in the other two files' reports.
- `AlphaTestEffect::OnApply()`'s real `FogVector` computation (lines 198-219) is dead weight for the
  Vulkan/EasyGL backends specifically, since `FillGpuDrawParams()` bypasses it entirely — worth flagging
  for the eventual `AlphaTestEffect.cpp` audit (different shard) as a candidate for consolidation once the
  Vulkan/EasyGL shaders are upgraded to consume the real `FogVector`, if that's ever prioritized.

## Missing or Weak Tests

See F1 — a non-identity-`World` fog variant would meaningfully strengthen this shard's fog coverage and
surface the object-space-vs-view-space gap as an explicit, visible finding rather than an implicit
limitation only discoverable by reading shader source.

## Positive Findings

- All three pixel assertions were independently re-derived from the actual GLSL formula and match to
  within normal GPU rounding — this is a correct, well-designed test for the formula that is actually
  implemented.
- The three-point Z sweep (start/end/midpoint) correctly proves a genuine linear interpolation rather than
  a binary on/off fog switch, which is exactly the right test design to catch a "fog is just a boolean"
  regression.
- The file's own header comment is transparent about reusing an established (if simplified) formula
  rather than presenting it as a full FNA-parity implementation, which made this audit's independent
  parity check straightforward.

## Final Assessment

A correct and well-verified test of the fog formula CNA's Vulkan backend actually implements, but that
formula itself is a known simplification (object-space Z, ignoring `World`/`View`) relative to FNA's real
camera-distance-based fog — a limitation this file shares with its `BasicEffect` siblings in this batch
and does not itself introduce or misrepresent, but also does not have coverage to detect if it were ever
fixed incorrectly (no non-identity-transform variant exists anywhere in this shard for `AlphaTestEffect`
fog).
