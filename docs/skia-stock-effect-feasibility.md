# Skia CPU stock-effect feasibility (SKIA-100)

## Decision

One bounded stock-effect route is feasible below the public backend: unlit, no-fog, textured
`BasicEffect` geometry with optional vertex colour. The CPU bridge owns already-clipped geometry,
reciprocal-W interpolation, point sampling, material colour, depth and the final RGBA8 image. Skia
only receives that completed image through `SkiaSurface::WritePixels`.

This is not sufficient to expose any stock 3D effect. Lighting, fog, exact production coverage,
sampler LOD, instancing, public resource ownership and mixed 2D/3D ordering remain outside the
prototype. The other effect families add discard, a second texture, cube sampling, normal-space
lighting, bone palettes or a PBR tangent-space BRDF. They are separately classified below rather
than inferred from the successful BasicEffect pixels.

Status terms used by the matrices:

- **Reusable**: an independently tested component already exists, but is not necessarily wired to
  a stock-effect CPU draw.
- **Prototype**: exercised by `Skia_CpuStockEffect_Spike` or the preceding isolated CPU bridge.
- **Gap**: required for public compatibility and not proven as a complete CPU renderer component.

No row below changes `GraphicsCapability::ThreeD`, depth/stencil, wireframe or custom-effect
reporting. Those capabilities remain false.

## Textured BasicEffect prototype

`examples/skia_cpu_stock_effect_spike_test.cpp` adds a bounded RGBA8+float-depth target and a
bounded immutable RGBA8 texture. Axes are restricted to 1..16384 and each resource refuses storage
above 256 MiB before allocation. The route accepts only finite, positive-W vertices wholly inside
clip space; a missing texture, lighting, fog or a vertex requiring homogeneous clipping rejects
before any colour/depth mutation.

The supported equation is the disabled-lighting BasicEffect formula used by FNA and current
EasyGL:

```text
fragment = texture
         * (VertexColorEnabled ? interpolatedVertexColor : 1)
         * ((DiffuseColor + EmissiveColor) * Alpha, Alpha)
```

Four quadrants reuse the exact inputs and expected pixels from
`EasyGL_BasicEffect_Combined`: `(99,52,23)`, `(25,104,47)`, `(49,26,93)`, and `(74,78,70)`.
Their UVs address every texel of a 2x2 image. A separate unequal-W triangle distinguishes
perspective-correct texture interpolation from affine interpolation: at pixel `(24,24)`, the
retained reciprocal W selects the first red texel, while affine UV would select the second green
texel. Depth remains `0.5`, and the completed image reaches `SkiaSurface` byte-exactly without a
second shading or quantization pass.

This spike deliberately uses point+clamp sampling. Existing Skia 2D tests prove point/linear and
Clamp/Wrap/Mirror in SpriteBatch, but geometry derivatives, minification, mip LOD and anisotropy
are different requirements and remain gaps.

## BasicEffect matrix

| Requirement | Status | Exact evidence or remaining work |
|---|---|---|
| Fixed/custom vertex decoding and primitive assembly | Prototype | SKIA-99 covers all declared inputs/topologies; this spike consumes PCT clip vertices. |
| WVP, viewport, reciprocal-W varyings and depth | Prototype | SKIA-96/97 plus the unequal-W textured triangle; homogeneous clipping and production edge rules remain absent. |
| Texture2D + vertex-colour + disabled-lighting material equation | Prototype | All four current EasyGL combined-oracle pixels match exactly. |
| Texture disabled and vertex colour variants | Reusable | White-source and vertex-colour selection are explicit in the route; the public variant matrix is not connected. |
| Normal inverse-transpose | Gap | Required for stride-32 lighting under non-uniform world transforms; no CPU normal pipeline exists. |
| Three lights, ambient/diffuse/emissive/specular | Gap | `BasicEffect::FillGpuDrawParams` forwards the values, but neither per-vertex nor per-pixel CPU lighting is integrated. |
| `PreferPerPixelLighting` variants | Gap | Current EasyGL has distinct vertex-lit/pixel-lit dispatch. A single CPU formula cannot silently replace both. |
| Fog vector and blend | Gap | Common code derives the view-space fog vector; its vertex evaluation/interpolation and fragment mix are not connected. |
| Sampler LOD, exact coverage, instancing and mixed ordering | Gap | Required by the EasyGL matrix; no production CPU contract yet. |
| Public effect/buffer/draw/resource lifetime | Gap | The test is isolated and `ThreeD` remains false. |

## AlphaTestEffect matrix

| Requirement | Status | Exact evidence or remaining work |
|---|---|---|
| Transform, PCT input, texture, vertex colour and material alpha | Reusable | SKIA-99 and the BasicEffect route provide the input/interpolation pieces. |
| All eight compare functions and reference scaling | Reusable | SKIA-93 covers below/equal/above decisions; SKIA-94 covers public property forwarding. |
| True discard rather than transparent replacement | Reusable | SKIA-93's `clipShader` preserves the destination and proves transparent source is not equivalent. |
| Discard integrated before depth/stencil/pass operations | Gap | The CPU stencil bridge has branch ordering, but alpha coverage has not been connected to it. |
| Fog, production triangle coverage and public draw | Gap | No integrated stock-effect geometry route; public calls still reject. |

## DualTextureEffect matrix

| Requirement | Status | Exact evidence or remaining work |
|---|---|---|
| Transform, PCT input, vertex colour and material alpha | Reusable | SKIA-99 plus current BasicEffect input/material pieces. |
| Two independently addressed Texture2D sources | Reusable | SKIA-93 samples two Skia children with discriminating Repeat/Mirror coordinates. |
| `texture0.rgb *= 2`, then texture1/material multiply | Reusable | SKIA-93 matches the current EasyGL/FNA dual-texture formula in one runtime shader. |
| Per-slot filter/LOD, render-target orientation and null fallback | Gap | 2D components exist, but the complete geometry sampler contract is not integrated. |
| Fog, production coverage and public draw | Gap | No complete stock-effect route; public calls still reject. |

## EnvironmentMapEffect matrix

| Requirement | Status | Exact evidence or remaining work |
|---|---|---|
| Position/normal/UV layout and ordinary texture | Reusable | SKIA-99 decodes the layout; CPU Texture2D and perspective UV pieces exist. |
| Six-face cube storage | Reusable | SKIA-80--86 prove bounded face/mip storage and renderable level-zero faces. |
| Direction-to-face cube sampling and filter/LOD | Gap | A bounded fragment-only cube sampler (`cnaSampleCubeEXT`, SKIA-144–151) now consumes this storage and performs direction-to-face lookup with mip selection and Point/Linear filtering, but only as a standalone 2D SkSL extension bound through `SetTexture(1, TextureCube)` -- not wired to this stock effect's real 3D vertex/normal/eye-space geometry. |
| World position, inverse-transpose normal and eye vector | Gap | Required before reflection; no integrated CPU normal-space stage. |
| Three-light diffuse/emissive result | Gap | Effect fields forward in common code, but CPU lighting remains absent. |
| Per-vertex Fresnel, reflection, lerp and alpha-scaled specular | Gap | Current EasyGL semantics are more than a fragment colour filter and are not prototyped. |
| Fog, production coverage and public draw | Gap | No complete route. A bounded cube-sampling capability now exists and is documented (`docs/skia-cube-volume-sampling-contract.md`), but this effect additionally needs the still-absent normal/eye-space, lighting, Fresnel and public 3D draw pieces above before it could be considered. |

## SkinnedEffect matrix

| Requirement | Status | Exact evidence or remaining work |
|---|---|---|
| Position/normal/UV/weights/indices layout | Reusable | SKIA-99 decodes all 52-byte elements, usages and formats without ABI copying. |
| Palette bounds and 1/2/4 weights-per-vertex | Reusable | Public effect code bounds 72 matrices and forwards the selected weight count. |
| Weighted position and normal transformation | Gap | No isolated CPU palette evaluation or normal transform is connected to raster input. |
| Post-skin fog coordinate | Gap | EasyGL evaluates fog after skinning; pre-skin reuse would be observably wrong. |
| Three-light vertex/pixel variants and specular | Gap | Same lighting gap as BasicEffect, after the additional skin/normal stage. |
| Texture, vertex colour, production coverage and public draw | Gap | Individual input pieces exist; no integrated stock-effect draw or lifetime contract exists. |

## PbrEffect matrix

| Requirement | Status | Exact evidence or remaining work |
|---|---|---|
| Position/normal/tangent/UV layout | Reusable | SKIA-99 decodes the 48-byte declaration, including tangent usage. |
| Base colour Texture2D and material factors | Reusable | CPU texture/material plumbing exists, but BasicEffect's alpha-premultiplied equation is not the PBR equation. |
| Normal, metallic-roughness, emissive and occlusion maps | Gap | Storage exists; four additional geometry sampler bindings/orientation/LOD paths do not. |
| TBN construction and tangent-space normal map | Gap | Requires inverse-transpose normal/tangent handling and handedness; not a 2D filter. |
| glTF metallic-roughness BRDF and three lights | Gap | Fresnel/geometry/distribution terms and base-colour alpha separation are not prototyped. |
| Fog, production coverage and public draw | Gap | No complete route; `PbrEffect` remains a 3D requirement. |

## SkinnedPbrEffect matrix

| Requirement | Status | Exact evidence or remaining work |
|---|---|---|
| Position/normal/tangent/UV/weights/indices layout | Reusable | SKIA-99 decodes the 68-byte declaration. |
| Palette bounds and material/texture storage | Reusable | Common effect/resource code retains the data; no CPU consumer is implied. |
| Weighted position, normal and tangent basis | Gap | Skinning must transform the complete PBR basis before world inverse-transpose handling. |
| Five-texture sampling and metallic-roughness BRDF | Gap | All unskinned PBR gaps remain and must work with post-skin varyings. |
| Fog, production coverage and public draw | Gap | No complete route; this family cannot be inferred from BasicEffect or SkinnedEffect alone. |

## Closed inventory and SKIA-101 decision

The executable carries the same seven-family inventory as disjoint `reusable`, `prototype`, and
`gap` bitsets over 21 requirement classes. It fails if a requirement is unclassified, classified
twice, a family name is duplicated, a requirement class disappears from the inventory, or any
family loses the explicit exact-coverage/public-integration/mixed-ordering gaps.

SKIA-101 evaluated the whole design, not only whether one more formula could be written. The
accepted `docs/skia-3d-emulation-adr.md` rejects full emulation after accounting for:

- homogeneous clipping and production top-left/line/point coverage;
- normal transforms, vertex-lit versus pixel-lit variants and fog interpolation;
- cube direction sampling, mip/LOD/filtering and render-target orientation;
- bone palette evaluation including post-skin normal/tangent/fog semantics;
- PBR TBN construction, five texture inputs and the full BRDF;
- instancing, public buffer/effect/resource lifetime, state ordering and mixed 2D/3D draws.

The correct observable Skia result remains deterministic refusal of public 3D draws and false
capability reporting. SKIA-102 now makes that refusal exhaustive and uniform.
