# Audit: examples/vulkan_basiceffect_combined_test.cpp

## Metadata

- Source file: `examples/vulkan_basiceffect_combined_test.cpp` (160 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `BasicEffect` cross-backend image comparison capstone
  (Phase 42 close)
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_basiceffect_combined …)` /
  `cna_register_backend_test(NAME Vulkan_BasicEffect_Combined …)`, `cmake/Tests/VulkanTests.cmake:564-566`).
- XNA/FNA relevance: direct — `BasicEffect.TextureEnabled`/`VertexColorEnabled`/`DiffuseColor`/
  `EmissiveColor` combined, `LightingEnabled=false`.
- FNA reference: `Graphics/Effect/StockEffects/EffectHelpers.cs::SetMaterialColor()`, the
  `lightingEnabled==false` branch (lines ~211-224 per this batch's own re-derivation): `diffuse =
  (DiffuseColor + EmissiveColor) * Alpha`, with the shader then multiplying texture × vertex-color × this
  value directly.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp`
  (`FillGpuDrawParams()` lines 66-75: `forwardedDiffuse = lightingEnabled_ ? diffuseColor_ :
  (diffuseColor_ + emissiveColor_)`), `src/CNA/Internal/Backends/Vulkan/shaders/
  colored_textured3d.vert.glsl`/`.frag.glsl` (stride-24 `VertexPositionColorTexture` pipeline).
- git corroboration: `4544d921`/`dda9a7b1` "test(Task 370): cross-backend BasicEffect image comparison,
  closes Phase 42" — matches this file's own header comment attribution exactly.
- Note: this exact scene (texels, vertex color, diffuse/emissive values, and all 4 expected sample
  colors) is identical to `examples/easygl_basiceffect_combined_test.cpp`, already audited in a prior
  batch (`audit/examples/easygl_basiceffect_combined_test.cpp.audit.md`) — this report independently
  re-derives the same math for the Vulkan-specific file rather than assuming the prior batch's conclusion
  transfers unchecked.

## Purpose

The capstone `BasicEffect` no-lighting test: combines `TextureEnabled=true` (real 2×2 multi-texel
texture), `VertexColorEnabled=true` (a fixed vertex color), and `DiffuseColor+EmissiveColor` into one
formula, sampling each of the 4 texels via 4 separate draws (constant UV per draw, sampled at texel
centers `0.25`/`0.75` to avoid bilinear-filtering ambiguity between adjacent texels).

## Executive Verdict

**Healthy** — independently re-derived all 4 expected sample colors from
`TextureColor × VertexColor/255 × (DiffuseColor + EmissiveColor)` and matched every one exactly (to
within <1 unit of rounding, well inside the `±8` tolerance). Cross-checked the underlying formula against
both FNA's `EffectHelpers.SetMaterialColor()` no-lighting branch and CNA's `BasicEffect::
FillGpuDrawParams()` — the two agree precisely, confirming this isn't merely an internally-consistent test
but a genuinely FNA-faithful one.

## Checklist Results

### API / XNA / FNA parity
`setTextureEnabledProperty`, `setTextureProperty`, `fx.VertexColorEnabled = true` (line 112 — public
field, see F1), `setDiffuseColorProperty`, `setEmissiveColorProperty` are all correct FNA `BasicEffect`
members. `LightingEnabled` is left at its default `false` (never called) — confirmed matching FNA's own
default (`BasicEffect.hpp:367`: `bool lightingEnabled_ = false;` vs. FNA's C# default `bool` = `false`).

### Behavioral correctness — full re-derivation of all 4 samples
`kDiffuse=(0.6,0.4,0.8)`, `kEmissive=(0.1,0.2,0.05)`, sum `=(0.7,0.6,0.85)` (per
`BasicEffect.cpp:71`'s `forwardedDiffuse = diffuseColor_ + emissiveColor_` when `!lightingEnabled_`).
`kVertexColor=(180,220,140)/255=(0.7059,0.8627,0.5490)`. Default `Alpha=1`.
- **top-left** texel `(200,100,50)/255=(0.7843,0.3922,0.1961)`:
  `R=0.7843*0.7059*0.7*255=98.85≈99`; `G=0.3922*0.8627*0.6*255=51.77≈52`; `B=0.1961*0.5490*0.85*255=23.33≈23`
  → `(99,52,23)` — matches `kSamples[0].expected` exactly.
- **top-right** texel `(50,200,100)/255`:
  `R=0.1961*0.7059*0.7*255≈24.71≈25`; `G=0.7843*0.8627*0.6*255≈103.5≈104`; `B=0.3922*0.5490*0.85*255≈46.68≈47`
  → `(25,104,47)` — matches `kSamples[1].expected` exactly.
- **bottom-left** texel `(100,50,200)/255`:
  `R=0.3922*0.7059*0.7*255≈49.4≈49`; `G=0.1961*0.8627*0.6*255≈25.9≈26`; `B=0.7843*0.5490*0.85*255≈93.3≈93`
  → `(49,26,93)` — matches `kSamples[2].expected` exactly.
- **bottom-right** texel `(150,150,150)/255`:
  `R=0.5882*0.7059*0.7*255≈74.1≈74`; `G=0.5882*0.8627*0.6*255≈77.6≈78`; `B=0.5882*0.5490*0.85*255≈70.0≈70`
  → `(74,78,70)` — matches `kSamples[3].expected` exactly.
All 4 samples independently re-derived and confirmed correct with no rounding ambiguity.

### Logic
Per-sample loop (lines 107-138) constructs a fresh `BasicEffect fx(dev)` for each of the 4 texel samples
(line 109, inside the `for (const auto& s : kSamples)` loop) — correctly avoids state leakage between
samples, and correctly recreates the vertex buffer per sample with the sample's own constant UV (lines
116-119) so each draw samples exactly one texel across the whole quad.

### C++ correctness
`fx.Apply()` is called inside the retry loop (line 130), not once before it — meaning if the loop retries
(blank first frame), `Apply()` re-runs on the same effect instance. This is harmless here since `OnApply()`
is idempotent for unchanged parameters (dirty flags are already cleared after the first call, so
re-applying recomputes nothing but the same GPU state), but it is a minor inefficiency worth noting as
LOW/INFO, not a defect — see F2.

### Robustness
Sampling texel centers at `0.25`/`0.75` (not `0.0`/`0.5`/`1.0`, which risk landing exactly on a
bilinear-filter seam) is a deliberate, well-reasoned choice that the file's own header comment states
explicitly and this audit confirms is the correct technique for a 2×2 texture with a linear-filtering
sampler.

### Testing
4 independently-derived, non-redundant assertions (one per texel, each with a distinct expected RGB
triple) — genuinely discriminating, not a repeated pattern with only cosmetic variation.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — `fx.VertexColorEnabled = true` (line 112) uses `BasicEffect`'s public-field property
- Severity: LOW
- Confidence: HIGH
- Category: architecture / API-convention (production code, not this file's own defect)
- Location: `include/Microsoft/Xna/Framework/Graphics/BasicEffect.hpp:48`; consumed here at line 112
- Evidence/why it matters: identical finding to `vulkan_basiceffect_colored3d_fog_test.cpp.audit.md`'s F2
  in this same batch — see that report for the full analysis. Not attributable to this test file.

### F2 — `fx.Apply()` is called inside the up-to-20-iteration blank-frame retry loop rather than once before it
- Severity: LOW
- Confidence: HIGH
- Category: performance (theoretical, not likely-significant)
- Location: line 130, inside the `for (int i = 0; i < 20; ++i)` loop (lines 122-135)
- Evidence: `OnApply()` is dirty-flag-gated (confirmed in `BasicEffect.cpp`'s general `Effect::Apply()`
  pattern shared with `AlphaTestEffect`), so repeated calls with unchanged parameters are cheap no-ops
  after the first — this is not a correctness issue, only a very minor redundant-call pattern that could
  in principle be hoisted above the loop.
- Why it matters: negligible in practice (test-only code, runs a handful of times per process), flagged
  only for completeness per the checklist's "redundant state changes" performance criterion.

## Cross-File Observations

- This file's scene (texels, vertex color, diffuse/emissive constants, all 4 expected samples) is
  byte-for-byte identical to the already-audited `easygl_basiceffect_combined_test.cpp` — this is
  intentional, deliberate cross-backend parity testing (the file's own header comment says "cross-backend
  image comparison suite"), not accidental duplication. This audit's independent re-derivation matching
  both files' identical expected constants is a meaningful double-confirmation that the underlying
  `BasicEffect::FillGpuDrawParams()` formula (shared C++ code across backends) is correct, since two
  independently-audited GPU backends (EasyGL and Vulkan) both produce the FNA-faithful result from the
  same shared computation.
- Confirms `BasicEffect::FillGpuDrawParams()`'s no-lighting emissive-folding behavior
  (`diffuseColor_ + emissiveColor_` when `!lightingEnabled_`) is the single shared implementation this
  test and `vulkan_basiceffect_emissive_test.cpp` (also in this batch) both exercise — both independently
  confirm the same formula from different angles (this file via texture+vertex-color combination, the
  emissive file via texture-only isolation).

## Missing or Weak Tests

None material found. Coverage of the no-lighting `TextureEnabled`+`VertexColorEnabled`+
`DiffuseColor`+`EmissiveColor` combination is thorough and precise.

## Positive Findings

- All 4 independently-derived expected colors match exactly — a strong, precise confirmation of the
  shared `BasicEffect::FillGpuDrawParams()` no-lighting formula's FNA fidelity.
- Deliberate cross-backend parity design (identical scene reused across EasyGL and Vulkan test files)
  gives real double-confirmation value rather than being redundant duplication.
- Correct bilinear-filter-safe texel sampling technique (0.25/0.75 UV offsets).
- Clean per-sample effect/vertex-buffer reconstruction avoiding any cross-sample state leakage.

## Final Assessment

A precise, thoroughly-verified capstone test. All 4 pixel assertions were independently re-derived from
first principles and matched exactly against both the FNA reference formula and the actual CNA production
code. No defects found in this file.
