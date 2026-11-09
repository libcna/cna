# `BasicEffect` current contract

Current-state reconciliation: 2026-11-01.

`BasicEffect` implements the XNA 4.0 stock-effect contract and also exposes explicitly marked CNA
extensions for owned textures, shadows and punctual lights. This page describes the behavior that
callers and renderer implementations may rely on now. It is not a list of completed implementation
tasks.

## Sources of truth

| Concern | Authoritative project evidence |
|---|---|
| Public API and derived draw parameters | `BasicEffect.hpp`, `BasicEffect.cpp` and `BasicEffectTests.cpp` |
| Normal readable behavior reference | FNA's `BasicEffect.cs`, `BasicEffect.fx`, `Lighting.fxh`, `Structures.fxh` and `Common.fxh` in the local FNA checkout |
| Measured Microsoft XNA disagreements | [`xna-fna-behavior-decisions.md`](xna-fna-behavior-decisions.md) |
| Shared rendered evidence | [`cross-renderer-parity-fixtures.md`](cross-renderer-parity-fixtures.md) and `modules/graphics/examples/parity/ParityFixtures.cmake` |
| Renderer capability boundary | [`graphics-renderer-feature-matrix.md`](graphics-renderer-feature-matrix.md) and each renderer's own page |
| Current defects and limitations | [KNOWN_ISSUES.md](../KNOWN_ISSUES.md) |

The generated XNA runtime-member report answers whether the public API exists. This page answers how
the effect behaves; it deliberately avoids fixed test totals, which become stale whenever coverage
grows.

## XNA public state

The constructor starts with the following XNA-compatible state:

| Property group | Initial value |
|---|---|
| `World`, `View`, `Projection` | `Matrix::Identity` |
| `DiffuseColor` | `Vector3::One` |
| `EmissiveColor`, `AmbientLightColor` | `Vector3::Zero` |
| `Alpha` | `1` |
| `SpecularColor`, `SpecularPower` | `Vector3::One`, `16` |
| `LightingEnabled`, `PreferPerPixelLighting` | `false`, `false` |
| `DirectionalLight0/1/2.Enabled` | `true`, `false`, `false` |
| `FogEnabled`, `FogStart`, `FogEnd`, `FogColor` | `false`, `0`, `1`, `Vector3::Zero` |
| `TextureEnabled`, `Texture` | `false`, null |
| `VertexColorEnabled` | `false` |

The three directional-light objects exist regardless of their enabled state. Disabled lights
contribute zero rather than leaking their stored direction or colors into a draw.
`EnableDefaultLighting()` installs the XNA/FNA ambient term and all three standard directional-light
directions and colors. Cloning copies the effect state and preserves the referenced texture resource
identity; it does not clone the texture.

## Shader selection

The classic effect has 32 logical variants. Within each eight-variant block, independent selector
bits choose texture use, vertex-color use and fog-disabled versus fog-enabled code:

| Selector range | Lighting path |
|---|---|
| 0-7 | Lighting disabled |
| 8-15 | Per-vertex lighting with all three directional lights available |
| 16-23 | Per-vertex one-light optimization when lights 1 and 2 are disabled |
| 24-31 | Per-pixel lighting |

`PreferPerPixelLighting=false`, the default, computes lighting at vertices and interpolates the
result. `true` evaluates lighting in the fragment stage. These are observably different over a
coarsely tessellated surface, so renderers must implement the selector rather than treating it as a
quality hint. The CNA shadow and punctual-light extensions are additional renderer inputs; they do
not redefine this 32-variant XNA selector.

## Material and lighting

With lighting disabled, the material factor is:

```text
(DiffuseColor + EmissiveColor) * Alpha
```

With lighting enabled, the diffuse and emissive inputs are equivalent to FNA's material folding:

```text
diffuse  = DiffuseColor * Alpha
emissive = (EmissiveColor + AmbientLightColor * DiffuseColor) * Alpha
```

Each enabled directional light adds its diffuse color scaled by
`max(dot(-Direction, normal), 0)`. Its specular color uses the stock half-vector Blinn-Phong term
and `SpecularPower`; the final specular contribution is also scaled by the material alpha. Vertex
color, when enabled, participates at the stage selected by the stock shader rather than being
applied as a renderer-wide post-multiply.

The ordinary `Opaque` blend path therefore still observes `Alpha`: stock effects premultiply their
RGB material inputs by it before blending. Above-one intermediate values follow the XNA saturation
rule described below, not an eager public-property clamp.

## Texture and vertex inputs

When `TextureEnabled=true`, the effect requires texture coordinates and publishes sampler slot 0
for the draw. A non-null `Texture` supplies the sampled resource. A null texture deliberately
samples as opaque black, matching measured Microsoft XNA behavior. When texturing is disabled,
`BasicEffect` does not read or replace the slot.

Vertex requirements are semantic, not inferred from byte stride:

- position semantic 0 is always required;
- normal semantic 0 is required when lighting is enabled;
- color semantic 0 is required when vertex color is enabled;
- texture-coordinate semantic 0 is required when texturing is enabled.

The standard 24-byte position-plus-normal declaration and 36-byte
position-plus-normal-plus-color-plus-texture-coordinate declaration are both valid. A declaration
that omits an input required by the active variant must be rejected instead of silently selecting an
unrelated shader shape. General declaration and format rules are in
[`vertex-format-support.md`](vertex-format-support.md).

## Fog

Stock fog uses FNA's view-space dot-product `FogVector`, including the world-view transform. When
fog is disabled, the effect resets that vector and selects a no-fog shader variant. When enabled,
`FogStart == FogEnd` uses the stock degenerate vector and produces full fog rather than a
division-by-zero artifact. Fog blends RGB toward `FogColor * composedAlpha` while preserving
the fragment alpha. `composedAlpha` includes the effect's `Alpha` and any enabled texture and
vertex-color alpha contributions; it is not just the `Alpha` property.

CNA-FIX-D01 corrects that multiplication in SDLGPU's stock fragments. The shared pixel regression
checks effect/vertex alpha and lit/unlit textured BasicEffect paths, alongside the other stock
effects; see [sdl-gpu-renderer.md](sdl-gpu-renderer.md#parity-with-easygl) for the bounded evidence.

## D3D9 vertex-color-output saturation

Microsoft XNA's stock D3D9 shaders write per-vertex diffuse and specular values through `COLOR0`
and `COLOR1`. D3D9 saturates those vertex-shader color outputs before interpolation. CNA preserves
that observable rule for unlit and per-vertex-lit variants, including above-one material values and
vertex-color products.

Ordering matters on a gradient. For example, a vertex-lit value of `1.8` multiplied by vertex color
`0.5` must reach the interpolator as `0.9`; saturating the light sum first would incorrectly produce
`0.5`. The per-pixel-lit path is intentionally different: its material operation occurs in the
fragment stage and is saturated only by the later output path. These rules come from real XNA probes
and are guarded by the diffuse-color and vertex-lighting saturation regressions.

## Renderer boundary

| Renderer family | Current `BasicEffect` evidence boundary |
|---|---|
| EasyGL (`OPENGLES3`, `OPENGL33`, `WEBGL2`) | Native stock-shader paths plus the shared parity and conformance corpora |
| WebGPU | Native stock WGSL paths plus the shared parity and conformance corpora |
| SDLGPU | Native stock shader packages plus the shared parity and conformance corpora |
| Software | CPU stock-effect execution and renderer-neutral conformance tests |
| DirectX9 | Microsoft stock bytecode where available, with logic/oracle-equivalent handling for the remaining programs |
| FNA3D | Compiled stock-effect artifacts; final execution remains graphics-driver dependent |
| HEADLESS, STUB | Validation/resource behavior only; no raster-output claim |

The feature matrix and renderer pages own platform qualification, conditional capabilities and
known limitations. This page does not promote a family based only on another renderer's pixels.

## CNA extensions

The following additions are outside the XNA 4.0 `BasicEffect` surface and are marked `CNAEXT`:

- owned-texture publication for internal/resource-loading paths;
- directional and cascaded shadow-receiver state;
- point- and spot-light state through `PunctualLightEXT`.

`FillGpuDrawParams()` transports this state through the shared draw contract. Whether a particular
renderer executes an extension, and under which limits, belongs to that renderer's capability page
and focused tests. The extensions do not change the defaults or semantics of the classic XNA
properties.

## Executable evidence

The stable evidence names are more useful than totals:

- `BasicEffectDefaultsTest`, `BasicEffectVariantTest` and
  `BasicEffectVertexLightingSaturationTest` cover defaults, derived parameters, cloning, the 32-way
  selector and D3D9-style saturation;
- `StockEffectNullTextureTest` covers opaque-black null sampling and the disabled-texture slot rule;
- parity fixtures `basic_effect_light_terms`, `basic_effect_vertex_color` and
  `basic_effect_alpha_scale` compare shared rendered behavior;
- conformance programs `basiceffect_textured_msaa`, `basic_effect_declaration_inputs`,
  `basiceffect_lit_vertex_color_perpixel` and `basiceffect_unlit_vertex_color_36byte` exercise
  texture, declaration and lighting-path boundaries;
- focused renderer tests cover fog, vertex-versus-pixel lighting, diffuse saturation, shadows and
  punctual lights.

Use the private GPU runner described in the repository instructions for every rendered test.

## Historical task map

Tasks 361-370 established the original property, unlit, lighting, material and integration
coverage. Tasks 885/886 completed lit emissive, three-light and specular transport; Tasks 1102 and
1111 corrected vertex/pixel dispatch and fog; FX-123/124/125 captured vertex-output saturation,
large-world specular precision and lit color declarations. All of that work is closed. Git history
is the detailed task narrative; this page and the executable evidence above are the maintained
contract.
