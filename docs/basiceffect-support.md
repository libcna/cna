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

## 6. `DiffuseColor` above 1 is saturated per vertex (`plans/plan_vulkan.md` VULKAN-197, 2026-09-07)

`BasicEffect.DiffuseColor` is a `Vector3` with no clamp in its setter (`BasicEffect.cs:117`), and
`Alpha` is an unclamped `float`, so a game can hand the shader a value above 1. XNA still renders it
as 1 — not because the effect clamps, but because the **unlit** path writes the value to
`vout.Diffuse`, which `Structures.fxh` declares `COLOR0`, and **Direct3D 9 saturates a vertex
shader's colour output registers before interpolation**. It is the same `oD0`/`oD1` rule
`plans/plan_fx.md` FX-123 measured for the vertex-lit programs and `VULKAN-196` measured for
`EnvironmentMapEffect`'s blend factor.

**Measured on the real XNA 4.0 runtime** (`spikes/xna-diffuse-color-clamp-spike/`), with a **grey**
`(100,100,100)` texture — never white, or both answers saturate at the output and the probe could
not fail:

| `DiffuseColor` | 0.5 | 1.0 | 2.0 | 3.0 |
|---|---|---|---|---|
| real XNA | `(50,50,50)` | `(100,100,100)` | `(100,100,100)` | `(100,100,100)` |

**Where it stops being invisible: a colour gradient.** A flat, untextured quad cannot separate the
two orders at all, because the render-target write saturates either way. Two vertices that disagree
can — which is what FX-123's argument is actually about. Left edge white, right edge 20 % grey,
sampled at the centre:

| `DiffuseColor` | 1.0 | 2.0 |
|---|---|---|
| real XNA | `(153,153,153)` | `(178,178,178)` |

`178` is the midpoint of `saturate(2.0) = 255` and `saturate(0.4) = 102`. Interpolating the raw
product first gives `1.2` and clips to `255` — the two orders are **77 levels** apart there.

**Renderer status.** The Vulkan renderer clamps ten vertex shaders at the vertex stage as of
`VULKAN-197` and reproduces every number above. **EasyGL does not**, and its structure is different
rather than merely unfixed: its unlit programs apply `uDiffuseColor` in the **fragment** stage
(`FragColor = vc * uDiffuseColor`, `EasyGLRenderer.cpp:7768`), so there is no colour varying for the
saturate to apply to. `FX-123` fixed only its two vertex-**lit** programs. That half belongs to
`plans/plan_fx.md`, with the spike above as its oracle.

**Not applied to the per-pixel lit path, on purpose.** FNA's `VSBasicPixelLightingTx` writes
`vout.Diffuse = float4(1, 1, 1, DiffuseColor.a)` and applies the material colour in the **pixel**
shader, unclamped, with only the render-target write saturating it. Clamping CNA's equivalent
varying would make it *more* clamped than XNA.

Guarded by `modules/graphics/examples/basiceffect_diffuse_color_clamp_test.cpp`, registered as
`Vulkan_BasicEffect_DiffuseColorClamp`.

## 7. Vertex layouts a lit `BasicEffect` accepts (`plans/plan_vulkan.md` VULKAN-198/199/200, 2026-09-07)

XNA selects a stock `BasicEffect` shader from what the vertex *declaration* says, not from the
buffer's stride — and the two disagree more often than they look like they would. Two ordinary XNA
layouts are 24 and 36 bytes:

| Declaration | Bytes | Where it comes from |
|---|---|---|
| Position + Normal | 24 | Microsoft's Primitives3D sample (`SAMPLE-002`) — the **same stride** as `VertexPositionColorTexture` |
| Position + Normal + Colour + TexCoord | 36 | what the stock `ModelProcessor` emits for a mesh with a colour channel (`plans/plan_fx.md` `FX-125`) |

**EasyGL** renders both: `SelectStockProgramShape` matches the first as a declaration test and the
second as a stride case, and its lit programs carry a colour attribute. `FX-125` is the record of
what it cost to find out — a mesh with a colour channel fell onto the *unlit* program and
`SAMPLE-047`'s sphere rendered as a flat green disc.

**Vulkan** refused both outright until `VULKAN-199`/`VULKAN-200`: its lit family was selected by
`stride == 32` and existed for exactly one layout, so the declaration-fidelity guard refused the
draw rather than reading a normal's twelve bytes as a packed colour and a UV. Refusing is the right
failure — but a game using either layout could not draw at all. Both now render, and the selection
is set-exact: a declaration is matched as a whole element set, because a renderer's "is this layout
complete" test asks whether every input the *shader* consumes was supplied and cannot notice a
declared element the shader ignores.

**The vertex-colour ordering is the part that is easy to get subtly wrong**, and it differs between
the two lighting families:

* **per-vertex** (XNA's default): `VSBasicVertexLightingTxVc` does `vout.Diffuse *= vin.Color`
  **before** the value reaches `oD0`, so the colour is *inside* Direct3D 9's saturate. Clamping the
  lit sum first and scaling by the colour afterwards is a different picture, not a rounding
  difference — a lit sum of 1.8 with a colour of 0.5 gives 0.9 the right way and 0.5 the wrong way.
* **per-pixel**: `VSBasicPixelLightingTxVc` carries the colour to the fragment stage, where
  `PSBasicPixelLightingTx` multiplies it into the **whole** lit bracket, emissive included, before
  `AddSpecular` scales by the resulting alpha.

**One implementation note that cost a debugging round**, kept here because the next renderer to
grow a colour input will meet it: a stock program's input table is written in the **shader's
location order**, not the record's byte order, and for this layout the two differ — the colour sits
at byte 24 and the UV at 28, while `aUV` is location 2 (inherited from the colourless sibling) and
`aColor` is location 3. Listing them the record's way binds the UV's two floats to `aColor`, and
`vec4(uv, 0, 1)` at the centre of the quad is `(0.5, 0.5, 0, 1)` — half of which is a perfectly
plausible `(64,64,0)` that is *neither* the unlit vertex colour *nor* lit-but-colourless. It passes
both negative checks. That is why the test asserts the exact value the arithmetic predicts.

Guarded by `Vulkan_BasicEffect_PositionNormal` (EasyGL's `SAMPLE-002` source),
`Vulkan_BasicEffectLitVertexColor` (EasyGL's `FX-125` source) and
`modules/graphics/examples/basiceffect_lit_vertex_color_perpixel_test.cpp`. The last exists because
the `FX-125` source sets `PreferPerPixelLighting(false)` in its own body, so registering it alone
would leave the per-pixel half of the fix untested.

The **unlit** 36-byte record draws too, since `VULKAN-201` — routed to the ordinary unlit
colour-and-texture program, so it reaches the same D3D9 `oD0` saturate every other unlit coloured
record reaches. Sending it to the coloured *lit* shaders' unlit branch would have been a smaller
change and a worse one: that branch does not clamp at the vertex, and the two answers differ by
**77 levels** across a colour gradient at `DiffuseColor = 2` (midpoint 178 against 255).

**Still refused on Vulkan:** any lit declaration outside the sets above.

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
