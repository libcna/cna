# Audit: examples/bgfx_dualtextureeffect_alpha_test.cpp

## Metadata

- Source file: `examples/bgfx_dualtextureeffect_alpha_test.cpp` (154 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `DualTextureEffect.Alpha` premultiplication +
  `BlendState.AlphaBlend` pixel test (Bgfx backend)
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_dualtextureeffect_alpha …)` /
  `cna_register_backend_test(NAME Bgfx_DualTextureEffect_Alpha …)`,
  `cmake/Tests/BgfxTests.cmake:402-404`).
- XNA/FNA relevance: direct — `DualTextureEffect.Alpha`/`.DiffuseColor`,
  `BlendState.AlphaBlend`.
- FNA reference: `src/Graphics/Effect/StockEffects/DualTextureEffect.cs`
  (`OnApply`: `diffuseColorParam.SetValue(new Vector4(diffuseColor * alpha, alpha));`),
  `src/Graphics/BlendState.cs` (`AlphaBlend` preset).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/DualTextureEffect.cpp`
  (`OnApply`... actually `FillGpuDrawParams`, lines 260-263),
  `src/Microsoft/Xna/Framework/Graphics/BlendState.cpp:7`
  (`BlendState::AlphaBlend{"...", Blend::One, Blend::One, Blend::InverseSourceAlpha,
  Blend::InverseSourceAlpha}`), `fs_dual_texture3d.sc`.

## Purpose

Verifies that `DualTextureEffect.Alpha=0.5` produces a correctly *premultiplied* fragment which,
composited with `BlendState.AlphaBlend` over a known background, yields the expected blended color.
Texture 0 = solid white, texture 1 = solid gray `(128,128,128)` (chosen, per the header comment
carried over from the EasyGL/Vulkan siblings, so `Texture2` cancels the `color.rgb *= 2` doubling
factor — see `bgfx_dual_texture_test.cpp`'s report for the same formula), `DiffuseColor=(1,0,0)`
(pure red), `Alpha=0.5`, background = pure blue. Single check: centre pixel should read
`≈(128,0,128)`.

## Executive Verdict

**Healthy** — this audit independently re-derived the entire pipeline (shader fragment math →
premultiplied-alpha value → `BlendState.AlphaBlend` compositing over blue) from first principles
against both FNA's `DualTextureEffect.cs`/`BlendState.cs` and this project's own bgfx shader/blend
constant sources, and the derivation converges on the test's asserted `(128,0,128)` value.

## Checklist Results

### API / XNA / FNA parity
`setDiffuseColorProperty`/`setAlphaProperty` (lines 113-114) match `DualTextureEffect.hpp`.
`BlendState::AlphaBlend` (line 121) — this audit confirmed its actual constant values in
`BlendState.cpp:7`: `{Blend::One, Blend::One, Blend::InverseSourceAlpha,
Blend::InverseSourceAlpha}` for
`(ColorSourceBlend, AlphaSourceBlend, ColorDestinationBlend, AlphaDestinationBlend)` — this is the
correct XNA `BlendState.AlphaBlend` preset (premultiplied-alpha "One/InverseSourceAlpha" convention,
not a straight-alpha "SourceAlpha/InverseSourceAlpha" convention), matching the file header
comment's own description.

### Behavioral correctness
Full independent re-derivation:
1. `DualTextureEffect::FillGpuDrawParams` (`DualTextureEffect.cpp:260-263`) sets
   `diffuseColor[0..3] = (diffuseColor_.X*alpha_, Y*alpha_, Z*alpha_, alpha_)` =
   `(1×0.5, 0×0.5, 0×0.5, 0.5)` = `(0.5, 0, 0, 0.5)` — this audit independently confirmed this
   matches FNA's own `DualTextureEffect.cs` `OnApply()`:
   `diffuseColorParam.SetValue(new Vector4(diffuseColor * alpha, alpha))` exactly (same formula,
   read directly from the FNA source tree).
2. Fragment shader (`fs_dual_texture3d.sc`): `base.rgb = white.rgb*2 = (2,2,2)`;
   `color = base * gray(0.50196,0.50196,0.50196,1) * v_color0(0.5,0,0,0.5)`:
   `color.r = 2×0.50196×0.5 = 0.50196`, `color.g = 2×0.50196×0.0 = 0`, `color.b = 0` (v_color0.b=0),
   `color.a = 1×1×0.5 = 0.5`. So the fragment written is `(0.50196, 0, 0, alpha=0.5)`.
3. `BlendState.AlphaBlend` compositing over blue `(0,0,1)`:
   `result = src.rgb×One + dst.rgb×(1-srcAlpha)` = `(0.50196,0,0) + (0,0,1)×0.5` =
   `(0.50196, 0, 0.5)` → 8-bit: **(128, 0, 128)**.
   This exactly matches the test's asserted `Color(128, 0, 128, 255)` (line 131), confirmed within
   the file's own `colourMatch(..., tol=20)` (line 48-53).
This is a genuinely independent re-derivation (not merely restating the file's own comment) that
traced the value through the actual `DualTextureEffect::FillGpuDrawParams` C++ code, the actual
`.sc` shader source, and the actual `BlendState::AlphaBlend` constant — all three matched FNA at
each step.

### Logic
The retry loop's break condition (`got.getRProperty()!=0 || got.getGProperty()!=0 ||
got.getBProperty()!=255`, line 127) is the logical negation of "still showing the pure-blue
background" — i.e. it keeps retrying only while the readback is exactly the untouched clear color,
and stops as soon as anything else (including the correctly-blended result) appears. This correctly
targets the specific failure mode of interest here (early frames not yet showing the rendered quad)
without needing a black-vs-non-black distinction like the depth/stencil tests in this batch use.

### C++ correctness
`Texture2D texWhite(dev, 1, 1); texWhite.SetData(&kWhite, 1);` (lines 98-99) — direct-construction +
`SetData` pattern, distinct from but equally valid to the sibling `bgfx_dual_texture_test.cpp`'s
`CreateFromPixels` factory; both are legitimate `Texture2D` construction idioms already present
elsewhere in this codebase.

### Robustness
`tol=20` (line 48) on 8-bit channel values is reasonable given the observed exact derivation lands
within 0.2 of the asserted values (128 vs 128, 0 vs 0, 128 vs 127.5) — no meaningful risk of a false
pass masking a real regression at this tolerance, since a genuinely broken premultiplication (e.g.
forgetting to multiply by `alpha` at all, giving `diffuseColor=(1,0,0)` undiminished) would produce
a very different result, `(255,0,~191)`-ish, well outside tolerance.

### Testing
Single, well-targeted check exercising the full `Alpha` → premultiply → shader → blend pipeline in
one pass; the file's header comment's claim that "Bgfx's `BlendState` support genuinely respects the
requested blend factors (no known hardcoding gap)" unlike the Vulkan sibling is consistent with this
audit's confirmation that `BlendState::AlphaBlend`'s actual constants are used verbatim (this audit
did not additionally verify the Vulkan backend's blend-factor handling, out of scope for this file).

## Detailed Findings

None. No HIGH/CRITICAL/MEDIUM findings.

## Cross-File Observations

- Shares the gray-texture-cancels-doubling-factor technique with `bgfx_dual_texture_test.cpp` and
  `bgfx_dualtextureeffect_combined_test.cpp` in this same batch — all three were independently
  verified against the same underlying `fs_dual_texture3d.sc` formula and are mutually consistent.
- The `RasterizerState::CullNone` workaround comment (lines 15-19) again cites "Task 364's finding
  (tracked as Task 884, not fixed there or here)" — this audit traced Task 884's actual git history
  and found the number was later reused by an unrelated commit
  (`75aefb7b`/`6e3b41a5`, "fix(Task 884): EffectParameterCollection/EffectPassCollection
  dangling-pointer hazard"). This is a **project task-tracking/numbering hygiene issue** (the same
  number `884` was assigned to two unrelated pieces of work), not a defect in this test file — the
  underlying claim itself (Bgfx's default `RasterizerState` cull state matches FNA's real
  `CullCounterClockwiseFace` default, unlike EasyGL/Vulkan) is independently confirmed still true and
  still unaddressed by any later commit found in `git log`. Worth flagging to whoever owns
  `NEXT.md`/task-tracking hygiene, but out of scope to fix from this examples-file audit.

## Missing or Weak Tests

None identified for this file's single, well-defined scenario.

## Positive Findings

- A rare example in this batch where the full value chain (effect parameter math → shader source →
  blend-state constants) was traceable end-to-end through actual first-party source in three
  different files, and every step matched FNA — a strong confirmation this Bgfx pixel test
  genuinely validates real XNA-compatible behavior, not just "renders something."
- Correct, explicit acknowledgment that the Vulkan sibling test cannot do the same full-blend check
  due to a separate, already-known Vulkan-specific blend-factor gap — shows awareness of
  cross-backend asymmetry rather than assuming uniform capability.

## Final Assessment

A rigorously and independently verifiable test: this audit traced its expected `(128,0,128)` value
through the real effect, shader, and blend-state source and confirmed it matches FNA's actual
formulas at every step. No defects found in the test; one task-numbering hygiene observation (Task
884 reuse) is noted for the project's tracking documents, not the code.
