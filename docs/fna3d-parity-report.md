# FNA3D vs real XNA 4.0 — oracle corpus parity report

**Status: 2026-08-11.** Same 39-scene corpus (`tools/xna-oracle/scenes/*.scene`), same checked-in
real XNA 4.0 reference images (`tools/xna-oracle/reference/*.png`), same `scripts/xna-diff.py`
tolerance-0 comparison the D3D9 (`D9-A6`), EasyGL and OpenGLES1 (`OPENGLES1-78`) measurements
already use — now also run through the FNA3D renderer (`plan_fna3d.md` FNA3D-26).

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

## Where FNA3D is worse than the EasyGL baseline — open findings

| scene | EasyGL | FNA3D | status |
| --- | --- | --- | --- |
| `skinned_pixellighting_twobone_quad` | 7956–8569 | **21880** | **Unexplained — open (FNA3D-27a).** |
| `skinned_pixellighting_fourbone_quad` | 7956–8569 | **19378** | **Unexplained — open (FNA3D-27a).** |

`skinned_pixellighting_quad` (one bone) is at 8263, inside the EasyGL band, and the non-pixel-lit
`skinned_twobone_quad` (8110) and `skinned_fourbone_quad` (5878) are too. The divergence appears
only where per-pixel lighting and multi-bone skinning combine, which points at the `SkinnedEffect`
`ShaderIndex` selection for that combination rather than at bone transforms or lighting alone.
**Not yet diagnosed. Not counted as passing.**

## Honest limits of this measurement

- Software rasteriser (Mesa llvmpipe), not vendor hardware, so rasterisation tie-breaking and
  interpolation may differ from a real GPU.
- FNA3D's OpenGL driver only. The SDL_GPU and Direct3D 11 drivers execute the same MojoShader-
  translated stock effects through different rasterisers and are unmeasured here (no Vulkan ICD,
  no Windows on this host). Whether they hit the same numbers is an open external gate.
- The two open rows above are findings, not accepted costs. Widening the tolerance to turn a red
  row green without a per-scene reason is not acceptable — `scripts/xna-diff.py`'s own standing
  warning.
