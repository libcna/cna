# BasicEffect Exactness Support Matrix

> **Status update, 2026-07-11:** Tasks 885 (`DirectionalLight1`/`2` + lit-path `EmissiveColor`) and
> 886 (real specular highlights) — listed as open in §4 and the support matrix below — **are now
> implemented**: `BasicEffect.cpp` forwards both additional lights (gated on their own `Enabled`
> flags) and a real `SpecularColor`/`SpecularPower` term. Per `docs/graphics-renderer-feature-matrix.md`,
> "BasicEffect core (MVP, lighting, texture, vertex color)", "`DirectionalLight1`/`2` +
> `EmissiveColor`", and "real specular highlights" are all ✅ on EasyGL/Vulkan/Bgfx with no open
> gaps. This document predates that work (flagged in the feature matrix's own "See also" section)
> and has not been refreshed row-by-row; treat §4 and the matrix below as historical, and
> `docs/graphics-renderer-feature-matrix.md`/`docs/xna-4-api-coverage.md` as current.

Phase 42 (`plans/plan_graphics.md` Tasks 361–370) audited and pixel-verified `BasicEffect` conformance
against FNA across all three graphics renderers (EasyGL, Vulkan, Bgfx). This document summarizes
the findings and closes the phase.

---

## 1. Property/default audit (Tasks 361–363)

Task 361 audited all 22 `BasicEffect` properties line-by-line against FNA's `BasicEffect.cs`.
Two real default-value bugs found and fixed: `VertexColorEnabled` and `DirectionalLight0.Enabled`
defaulted wrong. Task 362 wrote exhaustive default-value tests for all 22 properties and found one
more: `DirectionalLight`'s `Direction` default was `Vector3::Forward` instead of `Vector3.Zero`.
Task 363 confirmed `EnableDefaultLighting()`'s exact constants (ambient color, all 3 lights'
direction/diffuse/specular) match FNA literal-for-literal — no fix needed.

## 2. No-lighting shader paths (Tasks 364–367)

All four combinations of `TextureEnabled`/`VertexColorEnabled` with `LightingEnabled=false` (the
real FNA default) were pixel-verified on all 3 renderers, each with a discriminating,
non-degenerate test (distinct non-white/non-primary colors chosen so partial-product failure modes
are numerically distinguishable from the correct result — never a case where "ignored" and
"correct" would coincidentally look the same):

- **Task 364** (no texture, `VertexColorEnabled` toggle): found and fixed **3 real bugs, one per
  renderer** — `VertexColorEnabled` wasn't honored by any of the 3 renderers' no-texture shaders.
  Also found (not fixed there) that Bgfx's default `RasterizerState` cull state is the only one of
  the 3 that actually matches FNA's real `CullCounterClockwiseFace` default, silently culling the
  standard NDC quad winding used throughout this whole test family unless `RasterizerState::
  CullNone` is set explicitly (tracked as Task 884).
- **Task 365** (`VertexColorEnabled=true`, no texture): verify-only, already correct.
- **Task 366** (`TextureEnabled=true`, no vertex color): verify-only, already correct.
- **Task 367** (`TextureEnabled=true` AND `VertexColorEnabled=true`, the stride-24
  `VertexPositionColorTexture` path): found and fixed **2 real bugs** — EasyGL's and Bgfx's
  stride-24 shader silently dropped `DiffuseColor` entirely (no uniform, no multiply at all);
  Vulkan's `colored_textured3d.vert.glsl` already had it right. Fixing EasyGL's bug also exposed a
  stale pre-existing test (Task 189's combinations test case (d)) that only passed *because of*
  the bug being fixed — corrected to set `VertexColorEnabled=true`, restoring its own stated intent.

## 3. Lighting (Task 368)

Verified `BasicEffect`'s one-directional-light diffuse formula
(`AmbientLightColor + DirectionalLight0.DiffuseColor × max(dot(-Direction,N),0)`, multiplied by
`DiffuseColor`) with a **non-saturating** `NdotL=0.5` test — deliberately not 0 or 1, to prove the
dot product is real math and not a boolean lit/unlit check — plus a back-facing-normal case (proves
the negative-dot clamp) and a `DirectionalLight0.Enabled=false` case (proves the light can be
switched off). Found and fixed **2 real bugs**:

- **Shared C++, all 3 renderers**: `BasicEffect::FillGpuDrawParams()` forwarded
  `DirectionalLight0`'s `Direction`/`DiffuseColor` unconditionally, never checking
  `DirectionalLight0.Enabled` — a disabled light still lit the surface.
- **Bgfx-only, much wider-reaching**: `BgfxRenderer.cpp`'s `MakeBgfxLayout()` never declared
  a `Normal` or `TexCoord0` vertex attribute for any stride except 52 (skinned) — every other
  stride fell through to a `Position`+`Color0`+padding-only layout. For stride 32
  (`VertexPositionNormalTexture`) this left `a_normal` permanently unbound, silently sinking every
  lit pixel to ambient-only regardless of the real per-vertex normal. The same root cause silently
  broke `TexCoord0` interpolation for strides 20/24 too — invisible in every earlier task's tests
  because they all use 1×1 solid-color textures (UV-insensitive). Fixed with a dedicated layout
  branch per stride; re-verified against the *entire* Bgfx test suite given the fix's reach (100%
  pass, zero regressions).

## 4. Ambient + emissive (Task 369)

Re-derived FNA's authoritative `EffectHelpers.SetMaterialColor()` formula directly from source
(not assumed): the disabled-lighting branch is `(DiffuseColor + EmissiveColor) * Alpha`, folded
into a single forwarded parameter; the enabled-lighting branch bakes ambient into a second,
confusingly-named GPU "EmissiveColor" parameter. Confirmed algebraically that CNA's
independently-structured lit formula (ambient forwarded as its own raw uniform, verified correct in
Task 368) is mathematically identical to FNA's once a plain `+EmissiveColor` term is added after
the ambient/diffuse multiply.

**Fixed** (shared C++, all 3 renderers, no shader changes needed): `FillGpuDrawParams()` forwarded
`DiffuseColor*Alpha` alone in every case, always silently dropping `EmissiveColor` in the
no-lighting path — the exact gap Task 366 had deferred.

**Deliberately not fixed**, scoped into 2 new dedicated tasks after auditing the real size of the
remaining work (mirroring this project's precedent of not bundling large, multi-pipeline-site
changes into a single task):

- **Task 885** — the *lit*-path `+EmissiveColor` term, plus `DirectionalLight1`/`DirectionalLight2`
  forwarding (still completely unforwarded, unchanged since Task 361). EasyGL/Bgfx just need a new
  uniform; **Vulkan needs to expand the shared 128-byte `pipelineLayoutExt3D_` push-constant
  budget** (`FillExtPushConst()`'s `float[32]`), which is also reused byte-for-byte by
  `SkinnedEffect`'s draw path — a genuine shared-architecture change, not a Vulkan-shader-only
  tweak.
- **Task 886** — real specular highlights. Confirmed **zero specular infrastructure exists
  anywhere** for `BasicEffect` (no `GpuDrawParams` fields for `SpecularColor`/`SpecularPower`/
  per-light specular; no eye-position wiring in `BasicEffect::FillGpuDrawParams()`, though
  `EnvironmentMapEffect`/`SkinnedEffect` already have prior art to reuse). A new feature, not a bug
  fix, sized similarly to the already-tracked Task 868/870/878/879 renderer-parity items.

## 5. Cross-renderer consistency (Task 370)

Closed the phase with a capstone test combining everything Tasks 364–369 verified individually —
`TextureEnabled` + `VertexColorEnabled` + `DiffuseColor` + `EmissiveColor`, `LightingEnabled=false`
— to prove the fixes compose correctly together, not just in isolation. Used, for the first time in
any `BasicEffect` pixel test, a **real 2×2 multi-texel texture** (every prior task used a 1×1
solid color) sampled at all 4 texel centers via 4 separate draws, deliberately exercising the exact
`TexCoord0`-binding path Task 368 found and fixed on Bgfx.

**Result: all 3 renderers produced byte-identical pixel output**, matching the FNA-derived expected
formula (`TextureColor × VertexColor × (DiffuseColor+EmissiveColor)`) at all 4 sample points. No
new bugs found — this was pure integration verification, and it passed cleanly on the first attempt
thanks to Tasks 364–369's fixes already being in place.

## Support matrix

| Feature | EasyGL | Vulkan | Bgfx |
|---|---|---|---|
| Property defaults (22/22) | ✅ Task 361/362 | ✅ (shared C++) | ✅ (shared C++) |
| `EnableDefaultLighting()` exact constants | ✅ Task 363 | ✅ (shared C++) | ✅ (shared C++) |
| No-texture, `VertexColorEnabled` toggle | ✅ fixed Task 364 | ✅ fixed Task 364 | ✅ fixed Task 364 |
| Texture × diffuse (no vertex color) | ✅ Task 366 | ✅ Task 366 | ✅ Task 366 |
| Texture × vertex color × diffuse (stride 24) | ✅ fixed Task 367 | ✅ already correct | ✅ fixed Task 367 |
| One directional light, diffuse + ambient | ✅ Task 368 | ✅ Task 368 | ✅ fixed Task 368 (layout bug) |
| `DirectionalLight0.Enabled` gating | ✅ fixed Task 368 | ✅ fixed Task 368 | ✅ fixed Task 368 |
| `DiffuseColor+EmissiveColor`, no lighting | ✅ fixed Task 369 | ✅ fixed Task 369 | ✅ fixed Task 369 |
| `EmissiveColor` while lit | ✅ fixed Task 885 | ✅ fixed Task 885 | ✅ fixed Task 885 |
| `DirectionalLight1`/`2` (multi-light) | ✅ fixed Task 885 | ✅ fixed Task 885 | ✅ fixed Task 885 |
| Real specular highlights | ✅ fixed Task 886 | ✅ fixed Task 886 | ✅ fixed Task 886 |
| Cross-renderer pixel consistency | ✅ Task 370 | ✅ Task 370 | ✅ Task 370 |

Legend: ✅ verified working · ❌ confirmed not implemented (historical — see status banner at top).

## Open, tracked follow-up work

Phase 42 opened 2 new tracked tasks, both since closed:

- ~~**Task 885**~~ — **fixed.** Lit-path `EmissiveColor` + `DirectionalLight1`/`DirectionalLight2`
  forwarding now implemented on all 3 renderers.
- ~~**Task 886**~~ — **fixed.** Real specular highlights (`SpecularColor`/`SpecularPower`) now
  implemented on all 3 renderers.

This closes Phase 42 (`plans/plan_graphics.md` Tasks 361–370) in full. Note `BasicEffect` is unrelated to
`EnvironmentMapEffect`/`SkinnedEffect`, whose own `DirectionalLight1`/`2` forwarding gaps (Tasks
890/891/893) remain genuinely open — see `NEXT.md` §5.
