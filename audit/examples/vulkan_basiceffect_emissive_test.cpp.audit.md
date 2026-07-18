# Audit: examples/vulkan_basiceffect_emissive_test.cpp

## Metadata

- Source file: `examples/vulkan_basiceffect_emissive_test.cpp` (150 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `BasicEffect` `DiffuseColor`+`EmissiveColor` combination
  test, `LightingEnabled=false`
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_basiceffect_emissive …)` /
  `cna_register_backend_test(NAME Vulkan_BasicEffect_Emissive …)`, `cmake/Tests/VulkanTests.cmake:558-560`).
- XNA/FNA relevance: direct — `BasicEffect.EmissiveColor` in the no-lighting path (the specific bug this
  file's header comment says it originally found: "the `EmissiveColor`-dropped bug this test found and
  fixed in the shared `BasicEffect::FillGpuDrawParams()`").
- FNA reference: `Graphics/Effect/StockEffects/EffectHelpers.cs::SetMaterialColor()`,
  `lightingEnabled==false` branch: `diffuse = (DiffuseColor + EmissiveColor) * Alpha`.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp:66-75`
  (`forwardedDiffuse = lightingEnabled_ ? diffuseColor_ : (diffuseColor_ + emissiveColor_)`, with the
  file's own inline comment explicitly documenting the historical bug: "lights are never computed at all,
  so `EmissiveColor` has to be baked directly into the forwarded diffuse color... otherwise it would be
  silently dropped").
- git corroboration: `e4c60e26`/`ccb957a0` "fix(Task 369): honor EmissiveColor in BasicEffect's
  no-lighting diffuse formula" — matches this file's header comment ("Task 369") exactly, and the
  production code's own comment (`BasicEffect.cpp` lines 66-70) independently corroborates the bug
  narrative from the *implementation* side, not just the test's claim.

## Purpose

Verifies the Task 369 fix with 3 assertions on a single scene: (1) the rendered pixel equals
`TextureColor*(DiffuseColor+EmissiveColor)` — the correct FNA no-lighting formula; (2) the pixel does
*not* equal `TextureColor*DiffuseColor` alone (proving `EmissiveColor` is not silently dropped — the
historical bug); (3) the pixel does *not* equal `TextureColor*EmissiveColor` alone (proving `DiffuseColor`
is not accidentally dropped instead — the complementary, equally-plausible regression this fix could have
introduced).

## Executive Verdict

**Healthy** — this audit independently re-derived all three expected/rejected colors from first
principles and confirms all three assertions are exact and mutually far apart (no risk of the negative
checks passing vacuously due to tolerance overlap). The two negative assertions are a genuinely
well-designed pair, each isolating a different possible single-point-of-failure in the fix.

## Checklist Results

### API / XNA / FNA parity
`setTextureEnabledProperty(true)`, `setTextureProperty(&tex)`, `setDiffuseColorProperty`,
`setEmissiveColorProperty` — all correct FNA `BasicEffect` members. `LightingEnabled` left at its FNA-
matching default `false` (never set explicitly — same as the `combined` test in this batch; confirmed
consistent with `BasicEffect.hpp:367`'s `bool lightingEnabled_ = false;` default).

### Behavioral correctness — full re-derivation
`kTexColor=(200,100,50)/255=(0.7843,0.3922,0.1961)`, `kDiffuse=(0.3,0.2,0.1)`, `kEmissive=(0.2,0.1,0.4)`,
sum `=(0.5,0.3,0.5)`.
- **Expected (correct)**: `R=0.7843*0.5*255=100.0`; `G=0.3922*0.3*255=30.0`; `B=0.1961*0.5*255=25.0` →
  `(100,30,25)` — matches `kExpected` exactly (zero rounding gap).
- **Diffuse-alone (bug scenario A — EmissiveColor dropped)**: `R=0.7843*0.3*255=60.0`;
  `G=0.3922*0.2*255=20.0`; `B=0.1961*0.1*255=5.0` → `(60,20,5)` — matches `kEmissiveIgnored` exactly
  (correctly named: this is the color you'd get *if EmissiveColor were ignored*).
- **Emissive-alone (bug scenario B — DiffuseColor dropped)**: `R=0.7843*0.2*255=40.0`;
  `G=0.3922*0.1*255=10.0`; `B=0.1961*0.4*255=20.0` → `(40,10,20)` — matches `kDiffuseIgnored` exactly.
All three values are pairwise far apart (deltas of 20-40 units per channel between any two), so the
`±8`-tolerance `matches()`/`!matches()` checks (lines 67-71) cannot pass or fail ambiguously — a
regression to either buggy formula would be caught cleanly, and the correct formula would not be
mistaken for either buggy alternative.

### Logic
Single `BasicEffect fx(dev)` constructed once (line 90) and `Apply()`'d inside the retry loop (line 113) —
same minor "Apply() inside the loop" pattern noted as F2 in this batch's `combined` test report, equally
inconsequential here (single scene, no per-iteration parameter change).

### C++ correctness
No issues found specific to this file.

### Robustness
The dual-negative-check design (rejecting *both* single-component-only hypotheses, not just one) is
exactly the right test structure for a "did we correctly combine two things" bug class, where a naive
single positive+single negative test could miss a fix that swapped which field got dropped.

### Testing
3 assertions, all genuinely discriminating, none redundant with each other (each rules out a distinct
failure mode).

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — Minor: `fx.Apply()` called inside the retry loop rather than once beforehand
- Severity: LOW
- Confidence: HIGH
- Category: performance (theoretical)
- Location: line 113
- Evidence/why it matters: identical, inconsequential pattern to F2 in this batch's
  `vulkan_basiceffect_combined_test.cpp.audit.md` — not repeated in full detail here.

## Cross-File Observations

- This file and `vulkan_basiceffect_combined_test.cpp` (also in this batch) both independently exercise
  and confirm the same `BasicEffect::FillGpuDrawParams()` no-lighting emissive-folding line
  (`BasicEffect.cpp:71`), from different angles: this file isolates diffuse+emissive with no vertex color
  and no fog, the `combined` file layers vertex color and multi-texel texture sampling on top. Together
  they give strong, non-redundant confirmation of the same shared formula.
- The production code's own inline comment (`BasicEffect.cpp:66-70`) documents the historical bug this
  test targets — a rare, welcome case in this codebase where the *implementation* comment and the *test*
  header comment independently corroborate the same bug narrative without either copying the other
  verbatim, giving genuine cross-source confidence the Task 369 narrative is accurate rather than a
  self-reinforcing but unverified claim.
- Unlike the `AlphaTestEffect`/`colored3d`/`colored_textured3d` fog tests in this batch, this file has no
  fog dimension at all (`FogEnabled` never set, defaults to `false`) — correctly out of scope for what
  this file is testing, and appropriately not conflated with the fog-formula limitation (F1) documented in
  those other reports.

## Missing or Weak Tests

None material. The dual-negative-check design already closes the most obvious regression-shape gap for
this specific historical bug.

## Positive Findings

- All three expected/rejected colors independently re-derived and confirmed exact, with wide, unambiguous
  separation between them — a well-constructed regression test.
- The two negative assertions target two distinct, independently-plausible bug shapes (each field being
  the one silently dropped), not just a single generic "not black" sanity check.
- Cross-corroborated bug narrative: the production code's own comment and this test's header comment
  independently agree on the Task 369 story, and `git log` confirms the dated fix commit matches both.

## Final Assessment

A precise, well-targeted regression test for a real, previously-fixed, well-documented defect
(Task 369). All three assertions were independently re-derived and confirmed correct; no issues found in
this file beyond a negligible, purely theoretical performance nit shared with its sibling test in this
batch.
