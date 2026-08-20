# D3D9 renderer — divergence report (plans/plan_dx9.md D9-121)

**Status: 2026-07-15. Corpus: 31 scenes, `D9-A5`. Result: 0 measured pixel divergences from real
XNA 4.0, at `--tolerance 0` (exact match), across the entire current corpus.**

This is the deliverable `plans/plan_dx9.md`'s own Phase D9-12 asks for: not a feature checklist, but a
measurement. "Indistinguishable from XNA 4.0" is a claim this renderer makes; this document is the
evidence for it, and — just as importantly — the explicit boundary of what has and has not been
measured yet. A short divergence list is a triumph. A short list produced by not looking hard
enough, or by a loosened tolerance, would not be — so this report also states plainly what the
31-scene corpus does **not** yet exercise, rather than let an empty "divergences found" list imply
total coverage.

## How this was measured

Every scene in `tools/xna-oracle/scenes/*.scene` is rendered twice: once by the real XNA 4.0
runtime (`tools/xna-oracle/Oracle.cs`, compiled by the real in-prefix `csc.exe`, running under
Wine with DXVK installed into that prefix) and once by CNA's own D3D9 renderer
(`tools/xna-oracle/CnaOracleRender.cpp`, through CNA's real public
`Game`/`GraphicsDeviceManager`/`GraphicsDevice`/`Texture2D`/effect API — never the raw renderer
interface). Both sides execute through the same DXVK Direct3D 9-over-Vulkan implementation, which
is what makes a pixel diff meaningful: without that, a diff would silently measure a *driver*
difference and misattribute it to CNA. `scripts/xna-diff.py --tolerance 0` (the default) compares
every one of the 65,536 pixels per scene; `0/65536` is required for a PASS, not "close enough."

`D3D9_XNA_Diff` (`D9-120`, this same commit) promotes this into a real, checked-in CTest —
`tools/xna-oracle/reference/*.png` holds the 31 real-XNA renders, captured once by hand and
committed, so the CTest itself needs only the D3D9 Wine prefix (not the XNA one) to keep
validating against them on every future run.

## Result: 0/31 scenes diverge

Every scene in the current corpus passes at `tolerance=0`. Represented, at least once, all
pixel-perfect:

- **All 5 XNA Stock Effects** (`BasicEffect`, `AlphaTestEffect`, `DualTextureEffect`,
  `EnvironmentMapEffect`, `SkinnedEffect`), including `BasicEffect`'s multi-light summation bucket.
- **`IEffectFog`**, shared across all 5 effects.
- **ALL 8** `AlphaTestEffect.AlphaFunction` values, across both real pixel-shader buckets
  (`PSAlphaTestLtGt` and `PSAlphaTestEqNe`) — compare-function coverage complete.
- **`EnvironmentMapEffect.FresnelFactor`**, with a genuine per-vertex gradient (non-specular
  bucket only — see "Not yet measured" below).
- **ALL 3** `SkinnedEffect.WeightsPerVertex` values (1/2/4 bones) — weighting coverage complete.
- **`SpriteBatch`**: core draw path (destination/source rect, rotation, origin, `SpriteEffects`
  flip), the D3D9 half-pixel offset (`D9-91`), sampler address modes (`Wrap`/`Mirror`), 3 of 5
  `SpriteSortMode` values (`Deferred`/`BackToFront`/`FrontToBack`), and multi-texture
  `FlushBatch()`-on-texture-change batching.
- **ALL 4** `PrimitiveType` values (`TriangleList`/`TriangleStrip`/`LineList`/`LineStrip`) —
  coverage complete.
- **`GraphicsProfile.Reach`/`.HiDef`**: profile-support queries and resource-creation-time
  enforcement, verified through the real public API (not just renderer-direct construction).

## Two real renderer bugs this measurement process found (both fixed, both now in the 0/31)

Listed here because *finding real divergences and fixing them* is what this whole exercise is
for — a divergence report with nothing in this section would be a weaker one, not a stronger one.

1. **`SpriteSortMode` Z-clipping (`D9-93`).** `D3D9SpriteBatchRenderer::BuildMatrixTransformEXT`'s
   projection used `zFarPlane=1`, which maps any `layerDepth > 0` outside Direct3D 9's valid
   `[0,1]` clip-space Z range — silently clipping any sprite drawn with a nonzero `layerDepth`
   away entirely. Undetected until the `D9-93` scenes were the first in the whole corpus to draw
   with `layerDepth != 0`. Root-caused by comparing against the real XNA oracle (which rendered
   correctly) and fixed with `zFarPlane=-1`.
2. **`GraphicsProfile` never reaching the real device (Phase D9-10).** `Game`'s own
   `GraphicsDevice_` member is eagerly default-constructed (hardcoded `GraphicsProfile::Reach`)
   before `GraphicsDeviceManager` even exists, and a game's own
   `graphics.GraphicsProfile = GraphicsProfile.HiDef; graphics.ApplyChanges();` request had no
   path to ever reach the live device — `GraphicsDevice.GraphicsProfile` silently kept reporting
   `Reach` regardless. This is shared, cross-renderer code (`GraphicsDeviceManager.cpp`), not
   D3D9-specific; found while building `D9-103`'s own tests, which is exactly the kind of thing
   a feature that never gets tested through the real public API stays broken indefinitely.

Neither bug is a "CNA vs. XNA divergence" in the sense `plans/plan_dx9.md`'s own six-divergences section
means (a deliberate or historical CNA design choice diverging from documented XNA behavior) — both
were straightforward implementation bugs, caught precisely because this project insists on
testing through the real public API against a real oracle rather than trusting the implementation.

## Not yet measured — explicit, not silently assumed covered

The corpus is 31 scenes deep, not exhaustive. These are real gaps, each with a concrete reason it
isn't closed yet, not just "not gotten to":

| Area | Why it's not in the corpus |
|---|---|
| Render targets (`RenderTarget2D`/`RenderTargetCube` as a sampled texture) | **Blocked on a real, reproducible crash** (`dxvk::DxvkError`, uncaught, on what appears to be a DXVK async shader-compiler thread) the moment a `D3DUSAGE_RENDERTARGET`-flagged texture exists in-process alongside any subsequent draw call. Documented in `NEXT.md` §4; needs Vulkan validation layers or DXVK-internals debugging to root-cause, not another oracle-scene attempt. |
| Every `SurfaceFormat` besides `Color` | CNA's own `Texture2D::SetData`/`GetData` C++ API is `Color`-only (no generic `SetData<T>` matching real XNA's own generic API) — needs new CNA API surface before non-`Color` formats can even be exercised through the oracle. |
| `EnvironmentMapEffect`'s specular variants, `PreferPerPixelLighting` (`BasicEffect`/`EnvironmentMapEffect`/`SkinnedEffect`) | Blocked on `D9-81`'s own confirmed `GpuDrawParams` gaps (this plan's own **Divergence 1**, below) — cross-cutting across all 10 CNA renderers, explicitly out of this plan's authority to fix (see Boundaries). |
| `SpriteSortMode.Immediate` | Its only real behavioral difference from `Deferred` (per-`Draw()` GPU submission instead of batching until `End()`) is not pixel-observable by a raster-diff methodology at all — a batched multi-quad draw call and N individual draw calls in the same order produce the identical final image. No scene for it would add verification value. |
| `SpriteSortMode.Texture` | Confirmed **not viable** as a deterministic oracle scene, not merely deferred: real FNA's own `SpriteBatch.cs` `TextureComparer.Compare` sorts by `texture.GetHashCode()` — `Texture`'s inherited default `Object.GetHashCode()`, an implementation-defined identity hash with no documented, predictable ordering between two arbitrary textures. |
| NPOT-wrap-on-`Reach` | Real XNA's own enforcement timing/behavior here is undocumented, and FNA implements none of it either (confirmed via `D9-100`'s own research into `ProfileCapabilities.cs`) — inventing an enforcement point without a reference to verify against would be asserting behavior this project cannot actually check. |
| Hardware instancing's `HiDef`-only gate | Noted as a Phase D9-10 follow-up when `D9-83` closed; not yet wired. |
| `D3DCULL` winding (`D9-21`) | Deferred to `D9-84`'s own draw-path validation sweep; the mapping table itself is done and source-verified, the pixel proof is not yet landed. |

## The six project-wide CNA-vs-XNA divergences — status as measured by this renderer

`plans/plan_dx9.md`'s own "CNA's divergences from XNA 4.0" section names six confirmed divergences
present on **every** CNA renderer, discovered by taking XNA seriously as the specification before
writing D3D9 renderer code. They are cross-cutting (`GpuDrawParams` + shader-variant work spanning
all 10 renderers) and explicitly **not this plan's to fix** — see Boundaries. What follows is their
status as *measured*, specifically by D3D9's own oracle work, not a re-litigation of whether to
fix them:

1. **`PreferPerPixelLighting` ignored; CNA always renders per-pixel, XNA defaults per-vertex.**
   Still fully present and unmeasured on D3D9 — no per-vertex lighting shader exists anywhere in
   CNA, D3D9 included. **Not exercised by the corpus** (see table above). Still the highest-severity
   divergence in the project.
2. **`oneLight` inferred, not carried in `GpuDrawParams`.** Resolved for D3D9's own *dispatch*
   specifically (`D9-82b`'s own finding: a provably-lossless derivation from existing
   `GpuDrawParams` fields, not a `GpuDrawParams` change) — but this is a shader-selection
   correctness fix, not a measurement of the divergence's pixel impact, which `plans/plan_dx9.md`'s own
   text already predicts is likely zero (a black light contributes nothing either way).
3. **`GraphicsProfile` was decorative.** **No longer true on D3D9** — this is what Phase D9-10
   (this session) closed: `IsProfileSupported()`, `QueryRenderTargetFormat()`/
   `QueryBackBufferFormat()`, and resource-creation-time enforcement (`Texture2D`/`TextureCube`/
   `Texture3D` size ceilings, `MaxRenderTargets`) are all real now, verified through the real
   public API. Still decorative on the other 9 renderers, correctly so — `plans/plan_dx9.md`'s own text
   already anticipated this is D3D9-only fixable (no `D3DCAPS9` exists elsewhere to consult).
4. **`SpriteBatch`'s D3D9 half-texel convention was never modeled or measured.** **Now measured
   and confirmed correct on D3D9** — `D9-91`'s own half-pixel offset formula, oracle-verified AND
   mutation-verified (a 1×1-texture scene was found to be structurally incapable of detecting a
   missing offset; a dedicated multi-texel boundary-color check was added specifically because the
   first attempt would have been a false-positive "closed" claim). This divergence is now
   *closed*, not just measured, for this renderer specifically — it was never a gap on modern-API
   renderers in the first place (design decision 10).
5. **Runtime uniform branching vs. XNA's compile-time shader permutations.** Unmeasured pixel-wise
   beyond what the 31-scene corpus already implicitly exercises (every passing scene is evidence
   this divergence is benign for the paths it covers, but no dedicated sweep has targeted it).
6. **Self-declared "numerically equivalent" shader-math deviations** (e.g. `GpuDrawParams`'
   `AmbientLightColor`-folded-into-`DiffuseColor` claim). Not specifically swept for by `D9-A6`
   (still open, `⬜`) — the 31-scene corpus's lit scenes (`lit_textured_quad`,
   `multilight_textured_quad`, `envmap_quad`, `skinned_quad`, ...) all pass at `tolerance=0`, which
   is evidence *for* the specific ambient-folding claim in the scenes that exercise ambient light,
   but not a dedicated, deliberate sweep for every such self-declared claim in the codebase.

## The one caveat every result above inherits (`D9-105`)

Every comparison in this report ran under Wine+DXVK, on this dev machine's own GPU
(`AMD Radeon 780M`, RADV). `D3DCAPS9` in this loop is **synthesized by DXVK**, not reported by an
authentic XNA-era (~2006–2013) Direct3D 9 driver — Phase D9-10's `IsProfileSupported(HiDef)`
returning `true` here proves the comparison *logic* is correct, not that a real `HiDef`-class GPU
exists in this loop (it doesn't). Every "0/65536 pixels differ" result above is real and
reproducible on this machine, through this DXVK version — it is not yet validated against a real
Windows box with a real XNA-era (or modern) GPU and a real Direct3D 9 driver. That is `D9-140`,
`needs_human`, still open. Until then, this report's claim is precisely: **CNA/D3D9 is
indistinguishable from real XNA 4.0 running through the same DXVK Direct3D-9-over-Vulkan path, on
this machine, for the 31 scenes measured** — a real, strong, reproducible result, and a narrower
one than "authentically indistinguishable from XNA on real hardware."

## Verdict

Zero measured divergences is a genuine result, not an artifact of a loose test: `tolerance=0` is
the strictest setting the diff tool supports, both sides execute through the identical DXVK
renderer (eliminating driver-difference noise as an explanation), and the corpus already caught and
forced fixes for two real implementation bugs along the way (proof the methodology can and does
find real problems, not just rubber-stamp passing results). The honest boundary is the corpus's
own size and the six project-wide divergences above (four of them unmeasured or only partially
measured on this renderer, one now closed here specifically, one resolved for dispatch purposes
only) — not a claim that D3D9 is finished. `D9-84`/`D9-A5` (grow the corpus further) and `D9-140`
(real hardware) are what would narrow that boundary next.

## Cross-renderer measurement (D9-A6)

**Status: 2026-07-16. Same 31-scene corpus, same `tools/xna-oracle/CnaOracleRender.cpp`, same
`tools/xna-oracle/reference/*.png`, same `xna-diff.py --tolerance 0` — now also run through the
EasyGL renderer. Result: 10/31 pixel-perfect, 21/31 diverge.**

`plans/plan_dx9.md`'s own `D9-A6` row calls this "free": the D3D9 half of the corpus already exists
(`D9-A3`/`D9-A5`), and `CnaOracleRender.cpp` was already renderer-agnostic (verified before touching
anything — a grep for D3D9-specific code found only two cosmetic `printf` strings and one comment,
all now parameterized/updated; see "What was built" below). The only genuinely new work was a
purely-additive CMake registration and a non-Wine driver script; the measurement itself just runs
the existing tool against the existing reference images through a different renderer. Unlike the
D3D9 side, this is **not** promoted to a CTest (`D9-A6`'s own task text: "record the deltas", not
"add a new permanently-enforced gate") — EasyGL is not expected to be pixel-identical to real XNA
the way CNA/D3D9-over-DXVK is (both the D3D9 oracle comparison and the real XNA reference render go
through the *same* DXVK D3D9-over-Vulkan implementation; EasyGL goes through an entirely different
GPU API and driver stack — Mesa OpenGL ES/RADV on this machine — so any diff here conflates real
rendering differences with GPU/driver-stack differences, exactly the confound the D3D9 report's own
"How this was measured" section is careful to rule out for the D3D9 numbers above).

### What was built

- `CMakeLists.txt`: one new `cna_easygl_test(cna_oracle_render_easygl tools/xna-oracle/CnaOracleRender.cpp)`
  call inside the existing `CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES3"` test section (same section that
  already builds `cna_diag_easygl`), building the exact same source file as a plain native
  executable — no Wine, no cross-compilation, not registered as a CTest. The existing D3D9
  `cna_oracle_render` registration (`cna_directx9_test(...)`, D3D9 CTest section) is untouched.
- `tools/xna-oracle/CnaOracleRender.cpp`: added a small `OracleRendererName()` helper selecting a
  string from whichever `CNA_RENDERER_*` compile definition (`CMakeLists.txt`'s own
  `add_compile_definitions(CNA_RENDERER_*)`, one per renderer) is active, and pointed both
  `CNA-XNA-ORACLE-OK renderer=...` `printf`s at it instead of the hardcoded `"D3D9"` literal. No
  other line changed — confirmed before starting that the file has no `#ifdef CNA_RENDERER_DIRECTX9`,
  no raw D3D9 casts, and no other renderer-specific code path; it already only calls the public
  `Game`/`GraphicsDeviceManager`/`GraphicsDevice`/effect/`SpriteBatch`/`Texture2D` API.
- `scripts/run-oracle-corpus-diff-easygl.sh`: a new, non-Wine twin of
  `scripts/run-oracle-corpus-diff.sh` — same scene loop, same `xna-diff.py --tolerance 0` call, same
  pass/fail accounting, but invokes the renderer directly (no `run-wine-dxvk9.sh` wrapper) with
  `SDL_VIDEODRIVER=x11`/`DISPLAY` set (the same environment every other EasyGL CTest in
  `CMakeLists.txt` already needs for its real X11 window). The original D3D9 script is untouched.

Build verified clean (`cmake --build cmake-build-debug --target cna_oracle_render_easygl`, EasyGL
build dir) and the binary was run against all 31 scenes with a real X11 `DISPLAY` on this machine.

### Result: 10/31 pixel-perfect

Passing at `tolerance=0`, identically to the D3D9 corpus: `alphatest_never_quad` and all 9
`sprite_*` scenes (`sprite_basic_quad`, `sprite_flipped_quad`, `sprite_mirror_quad`,
`sprite_multitexture_quad`, `sprite_rotated_quad`, `sprite_sortmode_backtofront_quad`,
`sprite_sortmode_deferred_quad`, `sprite_sortmode_fronttoback_quad`, `sprite_wrap_quad`) — i.e. the
entire `SpriteBatch` slice of the corpus, plus the one `AlphaTestEffect` scene whose entire quad is
discarded (nothing to rasterize wrong). `0/65536` on every one of these, both renderers.

### 21/31 diverge — three distinct patterns, not one bug

Per-scene pixel counts were histogrammed into "small" (`<=3` per-channel delta — visually
imperceptible) vs "large" (`>3`) differing pixels, plus the location/values of the single largest
delta, to distinguish genuinely different root causes rather than reporting one flat "21 fail"
number. **None of these were investigated further or fixed, per this task's own explicit rule** —
each is logged here with its observed evidence and a best-guess cause for whoever picks up the
matching `plans/plan_graphics.md` item next.

**Pattern A — boundary/edge-only divergence, 17 scenes.** Every differing pixel sits in a thin band
at a primitive's silhouette edge; the interior is bit-exact in every one of these. Values toggle
between the primitive's fully-covered color and the clear color (or, for the two `AlphaTestEffect`
compare-boundary cases, between two different texel colors), consistent with a rasterization
fill-rule / pixel-center / edge-coverage convention difference between EasyGL's OpenGL/Mesa
rasterizer and the D3D9-over-DXVK rasterizer both the CNA/D3D9 oracle side and the real-XNA
reference side render through — i.e. likely a genuine EasyGL-vs-D3D9 rasterization-boundary
convention gap, not a shading/math bug:

| Scene | Differing px | Max Δ | Example (loc: ref → cna) |
|---|---|---|---|
| `textured_quad` | 459/65536 | 255 | (128,52): `(255,0,0,255)` → `(255,255,255,255)` |
| `alphatest_quad` | 382/65536 | 255 | (52,128): `(255,0,0,255)` → `(255,255,255,255)` |
| `alphatest_always_quad` | 459/65536 | 255 | (128,52): `(255,0,0,255)` → `(255,255,255,1)` |
| `alphatest_greaterequal_quad` | 382/65536 | 255 | (52,128): `(255,0,0,128)` → `(255,255,255,129)` |
| `alphatest_lessequal_quad` | 382/65536 | 255 | (128,52): `(255,0,0,128)` → `(255,255,255,127)` |
| `alphatest_less_quad` | 153/65536 | 254 | (128,51): clear → `(255,255,255,1)` |
| `alphatest_equal_quad` | 305/65536 | 237 | (51,51): clear → `(255,0,0,128)` |
| `alphatest_notequal_quad` | 306/65536 | 155 | (128,51): clear → `(255,255,255,127)` |
| `lit_textured_quad` | 459/65536 | 237 | (51,51): clear → `(128,0,0,255)` |
| `dualtexture_quad` | 307/65536 | 217 | (51,51): clear → `(100,60,20,255)` |
| `envmap_quad` | 307/65536 | 148 | (51,51): clear → `(164,114,89,255)` |
| `multilight_textured_quad` | 307/65536 | 109 | (51,51): clear → `(128,128,128,255)` |
| `skinned_quad` | 307/65536 | 109 | (51,51): clear → `(128,128,128,255)` |
| `skinned_twobone_quad` | 306/65536 | 109 | (77,51): clear → `(128,128,128,255)` |
| `skinned_fourbone_quad` | 306/65536 | 109 | (67,47): clear → `(128,128,128,255)` |
| `colored_linestrip_quad` | 308/65536 | 237 | (25,51): clear → `(255,0,0,255)` |
| `colored_linelist_quad` | 816/65536 | 237 | (26,63): clear → `(255,0,0,255)` |

The five "lit/skinned/multilight/dualtexture/envmap" rows here are the interesting negative result:
`plans/plan_dx9.md`'s own prediction for `D9-A6` was that the `PreferPerPixelLighting` gap (design
decision 8: CNA always lights per-pixel, XNA defaults per-vertex) would produce "at least one
guaranteed hit" — but these five scenes all use a spatially **uniform** normal/light direction
across every vertex (by their own scene-file design, e.g. `lit_textured_quad`'s comment: "every
vertex faces the camera"), so per-vertex and per-pixel evaluation are mathematically identical for
them and structurally incapable of detecting that divergence — the exact same "structurally
incapable of detecting X" trap this document's own `D9-91` half-pixel-offset finding already named
for a different scene. Their divergence here is boundary-only, same as the unlit scenes.

**Pattern B — whole-primitive but imperceptible, plus the same boundary effect, 2 scenes.**
`colored3d` (12,859/65,536 differing; 12,693 of those `<=3` delta, only 166 `>3`) and
`colored_trianglestrip_quad` (23,716/65,536; 23,409 `<=3`, only 307 `>3`) both interpolate raw
vertex color (Gouraud) across the whole primitive with no effect math involved. Sampled interior
points differ by 1-2 per channel almost everywhere (e.g. `colored3d` at (128,150):
`(85,84,85,255)` vs `(85,83,87,255)`) — consistent with ordinary GPU/driver floating-point rounding
differences between Mesa/RADV (EasyGL) and DXVK (the D3D9 side both reference images were produced
through), not a rendering bug; `xna-diff.py --diff-out`'s own visualization draws *any* nonzero
delta as full-red regardless of magnitude, so a diff image of either of these scenes reads as "the
whole shape is wrong" when 99%+ of that area is a 1-2-value rounding difference. The remaining
166/307 `>3` pixels are the same Pattern-A edge-coverage effect, at the triangle's/quad's own
silhouette (`colored3d`'s single largest delta, 237, sits at (127,39) — clear color vs the top
vertex's near-pure color — right at the sharp apex tip).

**Pattern C — real, large, whole-primitive divergence, 2 scenes.** These are not edge noise or
rounding — the two are genuinely, visibly wrong across nearly the entire primitive, and are the
real, concrete "opens a `plans/plan_graphics.md` bug" result this task's own notes predicted:

- **`fog_gradient_quad`** (23,716/65,536 differing; 23,410 of those `>3`). The scene is deliberately
  built (`FogStart=0`, `FogEnd=-1`, negative on purpose — see the scene file's own extensive
  derivation comment) to produce a linear white-to-black gradient from the near edge (`z=0`) to the
  far edge (`z=1`). Real XNA renders exactly that gradient (sampled along `x=128`: `(249,249,249)`
  at `y=55` fading smoothly to `(10,10,10)` at `y=199`). **EasyGL renders the entire quad as solid
  black — `(0,0,0,255)` — at every one of those same sample points, including right at the near
  (`z=0`) edge that should be nearly white.** Best-guess root cause: EasyGL's fog-factor computation
  mishandles this `FogEnd < FogStart` (negative-`FogEnd`) configuration and saturates to "fully
  fogged" everywhere, rather than reproducing the `fogFactor = z * scale + fogStart * scale` formula
  `EffectHelpers.SetFogVector`/`Common.fxh` implement (and which D3D9 — this exact scene, these exact
  `FogStart`/`FogEnd` values — already renders correctly, 0/65536 diff, per the main report above).
  Because the D3D9 side already proves this exact configuration is *correctly specified*, this looks
  like an EasyGL-renderer-local shader/uniform bug, not one of the six project-wide `GpuDrawParams`
  divergences this plan tracks.
- **`envmap_fresnel_quad`** (23,263/65,536 differing; 20,511 of those `>3`). The scene assigns
  different per-vertex normals to its top edge (`fresnelFactor=1` exactly) and bottom edge
  (`fresnelFactor≈0.29289`) specifically to force a genuine spatial gradient in a lighting-adjacent,
  per-vertex-computed quantity — the one scene in the whole corpus actually capable of exposing the
  `PreferPerPixelLighting`-class gap (unlike the five Pattern-A "lit" scenes above). Real XNA
  produces the intended smooth linear gradient (sampled along `x=128`: `(196,98,49)` at `y=55` fading
  to `(72,36,18)` at `y=190`). **EasyGL instead renders almost the entire quad at close to the
  top edge's bright value (`~(198,99,49)`–`(200,100,50)`) regardless of `y`**, with only a narrow dip
  near the diagonal seam between the quad's two triangles (`(171,86,43)` at `y=130`) — i.e. the
  intended Gouraud gradient is essentially absent; most of the surface reads as if only the bright
  vertex's Fresnel value applies. Best-guess root cause: EasyGL evaluates `EnvironmentMapEffect`'s
  Fresnel term in a way that doesn't correctly Gouraud-interpolate the per-vertex-computed scalar
  across the primitive (possibly evaluating it from an interpolated per-fragment normal instead of
  interpolating the already-computed per-vertex Fresnel value, or a flat/qualifier issue on the
  varying that carries it) — consistent with, and a concrete confirmation of, `plans/plan_dx9.md`'s own
  design-decision-8 prediction, in the one scene actually shaped to detect it. Same caveat as
  `fog_gradient_quad`: D3D9 already renders this exact scene correctly (0/65536), so this is
  EasyGL-renderer-local, not a shared cross-renderer `GpuDrawParams` gap.

### Verdict

This measurement cost nothing beyond reusing existing tooling, and it did exactly what `D9-A6`'s own
notes predicted: it converted a standing assumption ("EasyGL is probably close to XNA-correct,
nobody has actually measured it") into a real number (10/31 pixel-perfect, three named, evidenced
divergence patterns for the rest) and surfaced two concrete, previously-unmeasured `plans/plan_graphics.md`
candidates (`fog_gradient_quad`'s negative-`FogEnd` handling, `envmap_fresnel_quad`'s Fresnel
interpolation) plus one systemic rasterization-boundary gap (Pattern A, 17 scenes) — all **logged
here, not fixed**, per this task's own explicit scope boundary. `Vulkan`/`D3D11` remain unmeasured by
this exercise; repeating this same recipe (new `cna_*_test`-style CMake registration +
`run-oracle-corpus-diff-<renderer>.sh` twin) against them is the natural next step, not attempted here
to keep this task to its own stated scope.
