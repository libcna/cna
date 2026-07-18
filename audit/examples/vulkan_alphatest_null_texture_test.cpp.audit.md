# Audit: examples/vulkan_alphatest_null_texture_test.cpp

## Metadata

- Source file: `examples/vulkan_alphatest_null_texture_test.cpp` (150 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `AlphaTestEffect` null/no-texture fallback behavior
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_alphatest_null_texture …)` /
  `cna_register_backend_test(NAME Vulkan_AlphaTest_NullTexture …)`, `cmake/Tests/VulkanTests.cmake:576-578`).
- XNA/FNA relevance: direct — `AlphaTestEffect.Texture = null` behavior (FNA has no explicit contract
  for null textures beyond "shader samples whatever is bound"; CNA's own established convention, per the
  file's comment, is a deliberate opaque-white fallback rather than leaving the previous draw's texture
  bound).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/AlphaTestEffect.cpp`
  (`getTextureProperty()`/`setTextureProperty()` lines 154-155, `FillGpuDrawParams()` line 317:
  `p.textureEnabled = (texture_ != nullptr)`), `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp`
  (`defaultWhiteDescSet_`/`defaultWhiteImage_`, allocated in `EnsureDefaultWhiteTexture()` lines 3429-3509;
  consumed at line 6514: `draw.useAlphaTest ... ? draw.descSet : defaultWhiteDescSet_`).
- git corroboration: `59e70d66`/`b3946135` "fix(Task 379): fall back to opaque white when
  AlphaTestEffect texture is null (Bgfx)" — this file's own header comment states Vulkan "already had the
  correct white-texture fallback... before this task — no bug found here", consistent with the fix commits
  being scoped to Bgfx, not Vulkan.

## Purpose

Proves two things about `AlphaTestEffect.Texture = nullptr`: (1) the fallback is genuinely opaque white
`(1,1,1,1)`, not some other placeholder, by checking the resulting pixel equals
`DiffuseColor` unmodified; and (2) the fallback is *fresh* each draw, not a stale leftover of the
*previous* draw's real texture (a plausible bug shape if a backend's descriptor-set caching keyed only on
"has a texture changed" rather than "is a texture bound at all").

## Executive Verdict

**Healthy** — both expected constants were independently re-derived from `AlphaTestEffect`'s premultiplied-
alpha `DiffuseColor` formula and the Vulkan backend's genuine `defaultWhiteDescSet_` (a real 1×1
`(255,255,255,255)` `VK_FORMAT_R8G8B8A8_UNORM` image, confirmed by direct source inspection of
`EnsureDefaultWhiteTexture()`), and both match this test's asserted values exactly. The stale-texture
discriminator (checking the null-texture result is *not* equal to the with-texture result) is a genuinely
useful negative assertion, not a redundant restatement of the positive check.

## Checklist Results

### API / XNA / FNA parity
`setTextureProperty(Texture2D*)` (line 81) taking a raw pointer and accepting `nullptr` matches
`AlphaTestEffect.hpp`'s declared signature (`void setTextureProperty(Texture2D* value)`); FNA's own
`Texture` property setter accepts a `Texture2D` reference type, for which `null` is likewise a valid C#
value — the mapping is faithful.

### Behavioral correctness
Re-derived by hand: `kTexColor=(200,100,50)/255=(0.7843,0.3922,0.1961)`, `kDiffuse=(0.6,0.4,0.8)`,
default `Alpha=1` (never set in this file). `AlphaTestEffect::OnApply()`/`FillGpuDrawParams()` compute
`diffuseColor = DiffuseColor*Alpha = (0.6,0.4,0.8)` (premultiplied, alpha unused here since `Alpha=1`).
- **With texture**: `outColor = TexColor * diffuseColor = (0.7843*0.6, 0.3922*0.4, 0.1961*0.8)*255 =
  (120.0, 40.0, 40.0)` — matches `kExpectedWithTexture(120,40,40)` exactly.
- **Null texture**: fallback white `(1,1,1,1)` ⇒ `outColor = 1 * diffuseColor = (0.6,0.4,0.8)*255 =
  (153,102,204)` — matches `kExpectedNullTexture(153,102,204)` exactly.
Both derivations are exact (no rounding ambiguity), and neither result overlaps the other (deltas of
33/62/164 units respectively, far outside the `±8` tolerance), so the third assertion
(`!matches(nullTexGot, kExpectedWithTexture)`) is a real, non-trivial negative check, not one that would
pass vacuously.

### Logic
`renderWith()`'s per-call `AlphaTestEffect fx(dev)` construction (line 80) with a fresh instance for both
the "with texture" and "null texture" draws correctly avoids any possibility of the *test itself*
leaking effect-side state between the two cases — the only thing under test is the *backend's* descriptor-
set caching behavior, isolated cleanly.

### C++ correctness
No `AlphaFunction`/`ReferenceAlpha` set explicitly — relies on FNA's documented defaults
(`AlphaFunction=Greater`, `ReferenceAlpha=0`, confirmed against `AlphaTestEffect.cs` lines 47-48), which
always pass here since both draws have full opaque alpha (`1.0 > 0/255`ish threshold) — correctly isolates
color-fallback from alpha-discard logic, consistent with this file's design.

### Robustness
The 20-iteration blank-frame retry (lines 90-99) is the same established pattern used across this shard
(see the sibling `vulkan_alphatest_fog_test.cpp` report for the one associated minor risk noted there);
not repeated in detail here.

### Testing
Genuinely discriminating: covers "does null-texture even work" (positive) and "is it a fresh fallback,
not the stale previous descriptor set" (negative) — a materially different assertion from the positive
check, given the two expected colors are far apart.

## Detailed Findings

No CRITICAL/HIGH findings. No MEDIUM findings — the fallback path was already independently confirmed
correct via direct source inspection, and the file's own claim ("no bug found here") is corroborated
rather than merely repeated.

### F1 — `AlphaFunction`/`ReferenceAlpha` left at FNA defaults rather than set explicitly
- Severity: LOW
- Confidence: HIGH
- Category: test-authoring clarity
- Location: `renderWith()` (lines 78-100) — no `setAlphaFunctionProperty`/`setReferenceAlphaProperty`
  calls
- Evidence: relies on `AlphaTestEffect.cs`'s documented default `AlphaFunction=Greater`,
  `ReferenceAlpha=0` (confirmed identical in `AlphaTestEffect.hpp`'s field initializers, matching this
  audit's earlier cross-check in `vulkan_alphatest_comparefunction_sweep_test.cpp`'s report region).
- Why it matters: purely stylistic — an explicit `setAlphaFunctionProperty(CompareFunction::Always)`
  would make the "alpha test is intentionally a non-factor here" intent self-documenting in the test code
  itself rather than requiring the reader to know FNA's default. Not a defect.

## Cross-File Observations

- Confirms, via direct inspection of `VulkanGraphicsBackend.cpp:3429-3509`
  (`EnsureDefaultWhiteTexture()`) and its consumption at line 6514, that the Vulkan backend's null-texture
  fallback is a real dedicated 1×1 white image + dedicated descriptor set — not, e.g., a reused "default
  normal map" texture (`EnsureDefaultFlatNormalTexture()`, a *different* fallback with pixel value
  `(128,128,255,255)` used only for PBR tangent-space normals) which would have been a plausible
  copy-paste risk given the two functions sit adjacent in the source file and share nearly identical
  boilerplate.
- This is the one file in this 8-file batch whose own header comment claims "no bug found" rather than
  documenting a fix — and it is the one case in the batch where that claim was independently verified
  true by this audit (as opposed to `vulkan_alphatest_fog_test.cpp`'s comment, which is accurate about its
  own narrower scope but does not disclose the object-space-vs-view-space fog limitation this audit found
  in F1 of that file's report).

## Missing or Weak Tests

None material. F1 is a clarity nit, not a coverage gap.

## Positive Findings

- Both expected pixel colors were independently re-derived from first principles (premultiplied
  `DiffuseColor*Alpha` formula) and matched exactly, with no reliance on the file's own stated formula
  being trusted uncritically.
- The stale-state negative check (`!matches(nullTexGot, kExpectedWithTexture)`) is a well-chosen defense
  against a specific plausible bug shape (descriptor-set caching missing the "texture removed" transition)
  that the positive check alone would not catch.
- Direct backend source inspection confirms the file's own claim that Vulkan's null-texture fallback was
  already correct before this task — a rare case in this audit where a file's self-assessment holds up
  fully under independent scrutiny.

## Final Assessment

A clean, correctly-verified test with no defects found in either the test file or the production code
paths (`AlphaTestEffect::FillGpuDrawParams()`, `VulkanGraphicsBackend::EnsureDefaultWhiteTexture()`) it
exercises. Both its positive and negative assertions are genuine and non-redundant.
