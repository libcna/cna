# Audit: examples/vulkan_dualtextureeffect_doubling_test.cpp

## Metadata

- Source file: `examples/vulkan_dualtextureeffect_doubling_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `DualTextureEffect` `color.rgb *= 2` doubling-factor
  regression test (Task 383)
- File type: standalone `Game`-subclass executable, CTest-registered integration test
- XNA/FNA relevance: direct — the FNA-specific `color.rgb *= 2` factor in `DualTextureEffect`'s pixel
  shader, easy to miss when porting since it is invisible whenever both textures happen to be
  saturated (0 or 1).
- FNA reference: `Graphics/Effect/StockEffects/HLSL/DualTextureEffect.fx` (`PSDualTexture`:
  `color.rgb *= 2; color *= overlay * pin.Diffuse;`).
- Related production code: `src/CNA/Internal/Backends/Vulkan/shaders/dual_texture3d.frag.glsl`.

## Purpose

The file's own header comment states this test found and fixed a real bug: Vulkan/EasyGL/Bgfx's
`DualTextureEffect` shaders all originally computed `texture0 × texture1 × diffuse` directly, silently
missing FNA's `color.rgb *= 2` factor on texture0's RGB channels — invisible to every prior
`DualTextureEffect` test (Tasks 133/135/191/293/294/296/297) because they all used pure 0/1-saturated
texture values, where a missing `×2` clamps right back to the same saturated result. This test
deliberately uses a non-saturated `gray(100)` texture0 value specifically to make the missing factor
observable, plus a second check (`(b)`) preserving the "two white textures, diffuse=red → red" scenario
from the original Task 135 test's title.

## Executive Verdict

**Healthy** — the doubling factor is confirmed present and correctly implemented in the current
fragment shader; the test's own expected values were independently re-derived and match.

## Checklist Results

### API / XNA / FNA parity
No API-surface issues; standard `getX/setXProperty()` usage.

### Behavioral correctness
Re-derived check (a): `texGray=gray(100,100,100)` as `Texture` (texture0), `texWhite` as `Texture2`
(texture1), default `DiffuseColor=(1,1,1)`. Current `dual_texture3d.frag.glsl`:
`tex1.rgb *= 2.0; outColor = tex1 * tex2 * fragTint;` → `(100/255×2)×1×1×255 = 200` per channel — matches
`Color(200,200,200,255)` (line 114-115) exactly, and the test's own comment states this precisely: "the
original bug (missing `*2`) would have produced `gray(100)` here instead of `gray(200)`" — a
maximally-discriminating check, since a regression that dropped the doubling factor would produce a
value (100) *outside* the `colourMatch(..., tol=20)` tolerance band around 200, guaranteeing a real
failure rather than a marginal one.
Re-derived check (b): two white textures (`1×2×1=2`, clamped to `1` at the fixed-function output stage
since both channels are already saturated) × `DiffuseColor=(1,0,0)` → pure red `(255,0,0)` — matches
line 128-129 exactly. This check specifically preserves parity with the *original* Task 135 scenario
(mentioned by name in the comment as "the original task-title case"), guarding against a fix for the
doubling factor accidentally breaking the simpler saturated-input case that all the earlier tests
(133/135/etc.) already relied on.

### Logic
The two checks are complementary in exactly the way the header comment claims: (a) is sensitive to the
doubling factor specifically because its inputs are *not* saturated (a regression is directly visible as
a wrong absolute value, not just a clamped-away difference); (b) reconfirms the simpler, saturated-input
case still works after fixing (a)'s scenario, which matters because the fix (`tex1.rgb *= 2.0;` added to
the shader) could plausibly have been implemented incorrectly in a way that broke saturated inputs (e.g.
doubling in the wrong colour space, or doubling after the multiply instead of before) while still passing
a doubling-only check — genuinely good regression-test design rather than a redundant duplicate check.

### C++ correctness
`colourMatch()`'s default tolerance (`tol=20`, line 39) is looser than most siblings in this shard
(typically ±8), presumably chosen because both checks in this file work with fairly coarse expected
values (200, 255) rather than fine per-channel discrimination — reasonable given what each check needs
to distinguish (a 100-unit gap for (a), a fully-saturated channel for (b)).

### Testing
Both checks are genuine, well-targeted regression assertions directly tied to a real, previously-fixed
defect (confirmed via this shard's own cross-file evidence — see Cross-File Observations).

## Detailed Findings

None at MEDIUM or above. No HIGH/CRITICAL findings.

## Cross-File Observations

- This is the file that (per its own header comment, corroborated by the shader source itself: `//
  Task 383: verify DualTextureEffect's two-texture blend formula on Vulkan, including FNA's `color.rgb
  *= 2` doubling factor`) originally found the missing-doubling-factor bug that
  `vulkan_dual_texture_test.cpp` and `vulkan_dualtextureeffect_combined_test.cpp` (both audited in this
  same batch) now rely on being fixed. Confirmed the fix is genuinely present in the current
  `dual_texture3d.frag.glsl` (`tex1.rgb *= 2.0;`, independently verified by this audit while reviewing
  those sibling files, not merely assumed from this file's comment).
- `vulkan_dualtextureeffect_combined_test.cpp`'s use of a mid-range gray `Texture2` (rather than white)
  builds directly on the design insight this file established — using non-saturated inputs to make the
  doubling factor observable.

## Missing or Weak Tests

None identified for this file's stated, narrow purpose.

## Positive Findings

- A genuinely valuable regression test: it documents a real bug it caught (verified true via the
  current shader's `tex1.rgb *= 2.0;` line and this file's own precise historical account), and its
  test-input design (deliberately non-saturated gray) is exactly the right technique to make an
  invisible-under-saturation bug visible.
- Check (b)'s inclusion specifically to avoid regressing the simpler, historically-relied-upon
  saturated-input case is good defensive test engineering, not incidental duplication.

## Final Assessment

A well-designed, historically load-bearing regression test with no defects found. Both of its expected
values were independently re-derived and matched exactly, and its narrative about the real bug it
originally caught was corroborated against the current shader source rather than taken on faith.
