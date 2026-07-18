# Audit: examples/bgfx_dual_texture_test.cpp

## Metadata

- Source file: `examples/bgfx_dual_texture_test.cpp` (139 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `DualTextureEffect` two-texture-multiply pixel test
  (Bgfx backend)
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_dual_texture …)`, `cmake/Tests/BgfxTests.cmake:396-397`, matched
  against a `Bgfx_DualTexture` `cna_register_backend_test` row in the same file).
- XNA/FNA relevance: direct — `Microsoft.Xna.Framework.Graphics.DualTextureEffect`.
- FNA reference: `src/Graphics/Effect/StockEffects/HLSL/DualTextureEffect.fx`
  (`PSDualTexture`: `color.rgb *= 2; color *= overlay * pin.Diffuse;`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/DualTextureEffect.cpp`
  (`FillGpuDrawParams`, lines 248-275), `src/CNA/Internal/Backends/Bgfx/shaders/
  fs_dual_texture3d.sc` (the actual fragment shader executed on this backend).

## Purpose

Task 384: closes the one remaining Bgfx gap in `DualTextureEffect`'s basic magenta×yellow blend
coverage (already present for EasyGL/Vulkan per Tasks 133/135). Texture 0 = solid magenta
`(1,0,1,1)`, texture 1 = solid yellow `(1,1,0,1)`, `DiffuseColor`/`Alpha` left at their XNA defaults
(white/1.0). The file's own header comment explicitly and correctly notes this specific color pair
*cannot* discriminate the `color.rgb *= 2` doubling-factor bug (Task 383) — both channels already
saturate to `0` or `1` regardless of the factor of 2 — and defers that discrimination to
`bgfx_dualtextureeffect_doubling_test.cpp`'s gray/white case.

## Executive Verdict

**Healthy** — this audit independently re-derived the expected fragment color against both FNA's
actual `PSDualTexture` HLSL and this project's own `fs_dual_texture3d.sc` bgfx port (byte-for-byte
equivalent formula), confirming the test's magenta×yellow→red expectation is correct, and
independently confirmed the file's own self-aware caveat (that this case can't catch the doubling
bug) is accurate rather than a hollow disclaimer.

## Checklist Results

### API / XNA / FNA parity
`Texture2D::CreateFromPixels` (line 60-61) is a `NOXNA`-marked non-XNA convenience factory (verified
against `include/Microsoft/Xna/Framework/Graphics/Texture2D.hpp:228`, declared
`NOXNA static Texture2D CreateFromPixels(...)`) — correctly wrapped per this project's own
`CLAUDE.md` convention for non-XNA API additions inside the `Microsoft::Xna` namespace.
`setTextureProperty`/`setTexture2Property` (lines 72-73) match `DualTextureEffect.hpp`'s public
surface.

### Behavioral correctness
Independently re-derived the fragment math against the actual bgfx shader source
(`fs_dual_texture3d.sc`):
```
vec4 base = texture2D(s_texColor, v_texcoord0);
base.rgb *= 2.0;
vec4 color = base * texture2D(s_texColor2, v_texcoord0) * v_color0;
```
With `tex0=magenta(1,0,1)`, `tex1=yellow(1,1,0)`, `v_color0=diffuseColor*alpha=(1,1,1,1)` (XNA
default, confirmed against `DualTextureEffect.hpp:269-270`,
`Vector3 diffuseColor_ = Vector3{1,1,1}; float alpha_ = 1.0f;`):
`base.rgb = (2,0,2)`; `color = (2·1·1, 0·1·1, 2·0·1) = (2,0,0)`, clamped by the GPU's 8-bit render
target write to `(1,0,0)` = pure red. This matches the test's own assertion
(`centPx.getRProperty() >= 200 && centPx.getGProperty() <= 50 && centPx.getBProperty() <= 50`,
lines 104-106) exactly, and confirms the header comment's own claim that this particular color pair
saturates regardless of the ×2 doubling factor (`2×1=2` and `2×0=0` both clamp/stay identical to the
un-doubled `1` and `0` results) — i.e. this test genuinely cannot discriminate Task 383's bug, as it
honestly states, rather than silently being a weaker test than its name implies.

### Logic
The retry loop (lines 87-101) clears to **green** `(0,255,0,255)` each iteration (not black), and
breaks on the first non-`(0,0,0)` read — since the expected final result (red) and the background
(green) are both non-zero, this is a valid distinguishing condition for "stale/blank Bgfx readback"
vs. "real rendered content," consistent with the rest of this project's established idiom.

### C++ correctness
`Texture2D tex0_`/`tex1_` are plain (non-pointer) `Texture2D` members constructed via
`Texture2D::CreateFromPixels` in `Initialize()` (lines 53-61) and referenced by address in `Draw()`
(`fx.setTextureProperty(&tex0_)`) — object lifetime is tied to the `Game` subclass instance, which
outlives the single `Draw()` call that uses it; no dangling-pointer risk.

### Robustness
N/A beyond what's covered above — this is a single fixed-scenario pixel test, not an API surface
with malformed-input paths to validate.

### Testing
Fills a real, previously-missing gap (per the header comment: "Bgfx never had this specific magenta
x yellow test") without duplicating Task 191's 4-combination EasyGL-only coverage or this project's
separate doubling-factor test.

## Detailed Findings

None. No HIGH/CRITICAL/MEDIUM findings.

## Cross-File Observations

- The doubling-factor formula (`color.rgb *= 2` before the two-texture multiply) is verified
  identical between FNA's original `DualTextureEffect.fx` HLSL and this project's
  `fs_dual_texture3d.sc` bgfx shader — confirming the Bgfx backend is a faithful, not approximate,
  port of the real stock-effect pixel shader for this feature.
- `git log` confirms the referenced sibling file `bgfx_dualtextureeffect_doubling_test.cpp` exists
  and Task 383 (`1ed85909`/`235b0d6c`, "fix(Task 383): DualTextureEffect missing FNA's
  `color.rgb*=2` doubling factor (all 3 backends)") is a real, not fabricated, prior commit — the
  cross-reference in this file's header comment is accurate.

## Missing or Weak Tests

None for this file's narrow, explicitly-scoped purpose (closing the Bgfx magenta×yellow gap);
broader multi-combination and doubling-factor-discriminating coverage is correctly delegated to
sibling files, not silently missing.

## Positive Findings

- The header comment's explicit, falsifiable claim about what this specific color combination can
  and cannot discriminate was independently checked and found accurate — a genuinely useful level of
  test-authoring self-awareness, matching the standard set by the strongest reports in this audit's
  prior batches.
- Correctly reuses `NOXNA`-marked `Texture2D::CreateFromPixels` rather than inlining ad-hoc pixel
  upload logic.

## Final Assessment

A small, precisely-scoped, and independently-verified pixel test that closes a genuine Bgfx coverage
gap without overclaiming what it proves. No defects found.
