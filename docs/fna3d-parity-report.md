# FNA3D vs real XNA 4.0 — oracle corpus parity report

**Status: 2026-08-11.** Same 39-scene corpus (`tools/xna-oracle/scenes/*.scene`), same checked-in
real XNA 4.0 reference images (`tools/xna-oracle/reference/*.png`), same `scripts/xna-diff.py`
tolerance-0 comparison the D3D9 (`D9-A6`), EasyGL and OpenGLES1 (`OPENGLES1-78`) measurements
already use — now also run through the FNA3D renderer (`plans/plan_fna3d.md` FNA3D-26).

Run: `scripts/run-oracle-corpus-diff-fna3d.sh ./cmake-build-fna3d/cna_oracle_render_fna3d`,
Linux / Xvfb / Mesa 25.2.8 llvmpipe, `FNA3D_FORCE_DRIVER=OpenGL`.

| renderer | tolerance 0 |
| --- | --- |
| EasyGL (already-verified GL renderer, same machine/driver stack) | 10/39 exact |
| OPENGLES1 | 11/39 exact |
| **FNA3D** | **10/39 exact, 29 differing, 0 failed to render** |

**Every one of the 39 scenes rendered.** That alone is new information: this corpus is the only
thing in the repository that drives `DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect`,
the `BasicEffect` lighting variants (one-light, three-light, per-pixel), fog and the eight
`AlphaTestEffect` comparison functions through this renderer at all. Before FNA3D-26 none of those
three effect families had ever been rendered by FNA3D in any test.

## This is a measurement, not a gate

The reference images were captured against the real XNA 4.0 runtime on a different rasteriser, so
the corpus carries a host-wide divergence that is not renderer-specific — which is exactly why the
EasyGL and OpenGLES1 measurements are framed the same way. A renderer at 10/39 on this machine is
at the established baseline. The useful signal is the **per-scene** comparison below.

## Where FNA3D is byte-identical to the EasyGL baseline

Same differing-pixel count as EasyGL, i.e. the residual difference from XNA is the shared
GL-versus-XNA divergence rather than anything FNA3D-specific:

`textured_quad` (459), `lit_textured_quad` (459), `multilight_textured_quad` (307),
`envmap_quad` (307), `envmap_specular_quad` (307), `dualtexture_quad` (307), `skinned_quad` (307),
`colored3d` (12859), `cullmode_ccwface_quad` / `cullmode_none_quad` (12859),
`colored_trianglestrip_quad` (23716), and the six scenes both render exactly
(`alphatest_never_quad`, `cullmode_cwface_quad`, `sprite_basic_quad`, `sprite_mirror_quad`,
`sprite_multitexture_quad`, `sprite_wrap_quad`).

`dualtexture_quad` at 307 is worth calling out: that is the scene whose second UV set OpenGLES1
silently dropped (`OPENGLES1-81`, a real defect found only by this corpus). FNA3D matches the
EasyGL number, so its dual-UV path carries the second coordinate set correctly.

## Where FNA3D is better than the EasyGL baseline

| scene(s) | EasyGL | FNA3D |
| --- | --- | --- |
| `envmap_fresnel_quad` | 18820 | **17596** |
| `fog_gradient_quad` | 19661 | **19202** |
| `sprite_sortmode_deferred_quad`, `sprite_sortmode_backtofront_quad`, `sprite_sortmode_fronttoback_quad` | delta 1 | **exact** |

The three sort-mode scenes are what lift FNA3D's exact count: EasyGL differs from XNA by a
sub-LSB rounding step on them, FNA3D does not.

## The defect this corpus found — FNA3D-27a, fixed

The first run put `skinned_pixellighting_twobone_quad` at 21880 differing pixels and
`skinned_pixellighting_fourbone_quad` at 19378, well outside EasyGL's 7956–8569 band. Measuring the
rendered geometry rather than guessing located it immediately: real XNA puts the quad at x-extent
(77, 230) and FNA3D put it at (51, 204) — 26 pixels left, and the scene's `bone1translate=0.4` at
weight 0.5 is 0.2 world units, which at ~127.5 px/unit is 25.5 px. **The bone translation was not
being applied at all.**

Root cause: `Fna3dStockEffect::SetMatrix4x3Array` copied the first twelve floats of each flattened
matrix straight into the `float4x3` parameter. `Matrix::ToColumnMajor` — despite its name — emits
the ROW order `M11,M12,M13,M14, M21,...`, so those twelve floats are the first three ROWS, while
FNA's own `EffectParameter.SetValue(Matrix[])` for a 4-column/3-row parameter writes the three
COLUMNS: `M11,M21,M31,M41, M12,M22,M32,M42, M13,M23,M33,M43`. The copy therefore both transposed
wrongly and dropped `M41..M43`, the translation row, entirely — silently turning every translation
bone into an identity bone.

It is worth being precise about why nothing else caught this. A wrongly transposed identity is
still an identity, and a bone palette is overwhelmingly identities, so single-bone scenes and the
`ShaderIndex` unit tests all passed. Only a bone carrying a translation reveals it, and this corpus
is the only thing in the repository that renders one.

| scene | before | after | EasyGL |
| --- | --- | --- | --- |
| `skinned_twobone_quad` | 8110 | **306** | 306 |
| `skinned_fourbone_quad` | 5878 | **306** | 306 |
| `skinned_pixellighting_twobone_quad` | 21880 | **7650** | 7956 |
| `skinned_pixellighting_fourbone_quad` | 19378 | **7650** | 7956 |

All six skinned scenes are now at or better than the EasyGL baseline. `Fna3dMatrixPackingTests`
(4 cases) pins the arithmetic as a pure function so a regression is a red unit test rather than a
red image; those tests fail against the old code.

## Where FNA3D is worse than the EasyGL baseline

Nothing outstanding.

## Honest limits of this measurement

- Software rasteriser (Mesa llvmpipe), not vendor hardware, so rasterisation tie-breaking and
  interpolation may differ from a real GPU.
- FNA3D's OpenGL driver only. The SDL_GPU and Direct3D 11 drivers execute the same MojoShader-
  translated stock effects through different rasterisers and are unmeasured here (no Vulkan ICD,
  no Windows on this host). Whether they hit the same numbers is an open external gate.
- Widening the tolerance to turn a red row green without a per-scene reason is not acceptable —
  `scripts/xna-diff.py`'s own standing warning. FNA3D-27a was fixed, not tolerated.
