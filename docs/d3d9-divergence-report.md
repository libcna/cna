# D3D9 backend — divergence report (plan_dx9.md D9-121)

**Status: 2026-07-15. Corpus: 31 scenes, `D9-A5`. Result: 0 measured pixel divergences from real
XNA 4.0, at `--tolerance 0` (exact match), across the entire current corpus.**

This is the deliverable `plan_dx9.md`'s own Phase D9-12 asks for: not a feature checklist, but a
measurement. "Indistinguishable from XNA 4.0" is a claim this backend makes; this document is the
evidence for it, and — just as importantly — the explicit boundary of what has and has not been
measured yet. A short divergence list is a triumph. A short list produced by not looking hard
enough, or by a loosened tolerance, would not be — so this report also states plainly what the
31-scene corpus does **not** yet exercise, rather than let an empty "divergences found" list imply
total coverage.

## How this was measured

Every scene in `tools/xna-oracle/scenes/*.scene` is rendered twice: once by the real XNA 4.0
runtime (`tools/xna-oracle/Oracle.cs`, compiled by the real in-prefix `csc.exe`, running under
Wine with DXVK installed into that prefix) and once by CNA's own D3D9 backend
(`tools/xna-oracle/CnaOracleRender.cpp`, through CNA's real public
`Game`/`GraphicsDeviceManager`/`GraphicsDevice`/`Texture2D`/effect API — never the raw backend
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
  enforcement, verified through the real public API (not just backend-direct construction).

## Two real backend bugs this measurement process found (both fixed, both now in the 0/31)

Listed here because *finding real divergences and fixing them* is what this whole exercise is
for — a divergence report with nothing in this section would be a weaker one, not a stronger one.

1. **`SpriteSortMode` Z-clipping (`D9-93`).** `D3D9SpriteBatchBackend::BuildMatrixTransformEXT`'s
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
   `Reach` regardless. This is shared, cross-backend code (`GraphicsDeviceManager.cpp`), not
   D3D9-specific; found while building `D9-103`'s own tests, which is exactly the kind of thing
   a feature that never gets tested through the real public API stays broken indefinitely.

Neither bug is a "CNA vs. XNA divergence" in the sense `plan_dx9.md`'s own six-divergences section
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
| `EnvironmentMapEffect`'s specular variants, `PreferPerPixelLighting` (`BasicEffect`/`EnvironmentMapEffect`/`SkinnedEffect`) | Blocked on `D9-81`'s own confirmed `GpuDrawParams` gaps (this plan's own **Divergence 1**, below) — cross-cutting across all 10 CNA backends, explicitly out of this plan's authority to fix (see Boundaries). |
| `SpriteSortMode.Immediate` | Its only real behavioral difference from `Deferred` (per-`Draw()` GPU submission instead of batching until `End()`) is not pixel-observable by a raster-diff methodology at all — a batched multi-quad draw call and N individual draw calls in the same order produce the identical final image. No scene for it would add verification value. |
| `SpriteSortMode.Texture` | Confirmed **not viable** as a deterministic oracle scene, not merely deferred: real FNA's own `SpriteBatch.cs` `TextureComparer.Compare` sorts by `texture.GetHashCode()` — `Texture`'s inherited default `Object.GetHashCode()`, an implementation-defined identity hash with no documented, predictable ordering between two arbitrary textures. |
| NPOT-wrap-on-`Reach` | Real XNA's own enforcement timing/behavior here is undocumented, and FNA implements none of it either (confirmed via `D9-100`'s own research into `ProfileCapabilities.cs`) — inventing an enforcement point without a reference to verify against would be asserting behavior this project cannot actually check. |
| Hardware instancing's `HiDef`-only gate | Noted as a Phase D9-10 follow-up when `D9-83` closed; not yet wired. |
| `D3DCULL` winding (`D9-21`) | Deferred to `D9-84`'s own draw-path validation sweep; the mapping table itself is done and source-verified, the pixel proof is not yet landed. |

## The six project-wide CNA-vs-XNA divergences — status as measured by this backend

`plan_dx9.md`'s own "CNA's divergences from XNA 4.0" section names six confirmed divergences
present on **every** CNA backend, discovered by taking XNA seriously as the specification before
writing D3D9 backend code. They are cross-cutting (`GpuDrawParams` + shader-variant work spanning
all 10 backends) and explicitly **not this plan's to fix** — see Boundaries. What follows is their
status as *measured*, specifically by D3D9's own oracle work, not a re-litigation of whether to
fix them:

1. **`PreferPerPixelLighting` ignored; CNA always renders per-pixel, XNA defaults per-vertex.**
   Still fully present and unmeasured on D3D9 — no per-vertex lighting shader exists anywhere in
   CNA, D3D9 included. **Not exercised by the corpus** (see table above). Still the highest-severity
   divergence in the project.
2. **`oneLight` inferred, not carried in `GpuDrawParams`.** Resolved for D3D9's own *dispatch*
   specifically (`D9-82b`'s own finding: a provably-lossless derivation from existing
   `GpuDrawParams` fields, not a `GpuDrawParams` change) — but this is a shader-selection
   correctness fix, not a measurement of the divergence's pixel impact, which `plan_dx9.md`'s own
   text already predicts is likely zero (a black light contributes nothing either way).
3. **`GraphicsProfile` was decorative.** **No longer true on D3D9** — this is what Phase D9-10
   (this session) closed: `IsProfileSupported()`, `QueryRenderTargetFormat()`/
   `QueryBackBufferFormat()`, and resource-creation-time enforcement (`Texture2D`/`TextureCube`/
   `Texture3D` size ceilings, `MaxRenderTargets`) are all real now, verified through the real
   public API. Still decorative on the other 9 backends, correctly so — `plan_dx9.md`'s own text
   already anticipated this is D3D9-only fixable (no `D3DCAPS9` exists elsewhere to consult).
4. **`SpriteBatch`'s D3D9 half-texel convention was never modeled or measured.** **Now measured
   and confirmed correct on D3D9** — `D9-91`'s own half-pixel offset formula, oracle-verified AND
   mutation-verified (a 1×1-texture scene was found to be structurally incapable of detecting a
   missing offset; a dedicated multi-texel boundary-color check was added specifically because the
   first attempt would have been a false-positive "closed" claim). This divergence is now
   *closed*, not just measured, for this backend specifically — it was never a gap on modern-API
   backends in the first place (design decision 10).
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
backend (eliminating driver-difference noise as an explanation), and the corpus already caught and
forced fixes for two real implementation bugs along the way (proof the methodology can and does
find real problems, not just rubber-stamp passing results). The honest boundary is the corpus's
own size and the six project-wide divergences above (four of them unmeasured or only partially
measured on this backend, one now closed here specifically, one resolved for dispatch purposes
only) — not a claim that D3D9 is finished. `D9-84`/`D9-A5` (grow the corpus further) and `D9-140`
(real hardware) are what would narrow that boundary next.
