# `AlphaTestEffect` current contract

Current-state reconciliation: 2026-11-01.

`AlphaTestEffect` is XNA's textured stock effect for rejecting fragments by their fully composed
alpha. This page describes the behavior that callers and renderer implementations may rely on now.
It is not a chronology of the implementation campaign that established the behavior.

## Sources of truth

| Concern | Authoritative project evidence |
|---|---|
| Public API and derived draw parameters | `AlphaTestEffect.hpp`, `AlphaTestEffect.cpp` and `AlphaTestEffectTests.cpp` |
| Normal readable behavior reference | FNA's `AlphaTestEffect.cs`, `AlphaTestEffect.fx`, `Common.fxh` and `Structures.fxh` in the local FNA checkout |
| Measured Microsoft XNA disagreements | [`xna-fna-behavior-decisions.md`](xna-fna-behavior-decisions.md) |
| Shared rendered evidence | [`cross-renderer-parity-fixtures.md`](cross-renderer-parity-fixtures.md) and `modules/graphics/examples/parity/ParityFixtures.cmake` |
| Renderer capability boundary | [`graphics-renderer-feature-matrix.md`](graphics-renderer-feature-matrix.md) and each renderer's own page |
| Current defects and limitations | [KNOWN_ISSUES.md](../KNOWN_ISSUES.md) |

The generated XNA runtime-member report owns API presence. This page owns the behavioral contract
and deliberately avoids fixed test totals, which drift whenever coverage grows.

## XNA public state

The constructor starts with the following XNA-compatible state:

| Property group | Initial value |
|---|---|
| `World`, `View`, `Projection` | `Matrix::Identity` |
| `DiffuseColor`, `Alpha` | `Vector3::One`, `1` |
| `FogEnabled`, `FogStart`, `FogEnd`, `FogColor` | `false`, `0`, `1`, `Vector3::Zero` |
| `Texture` | null |
| `VertexColorEnabled` | `false` |
| `AlphaFunction`, `ReferenceAlpha` | `CompareFunction::Greater`, `0` |

The setters store their inputs without public clamping. In particular, `ReferenceAlpha` is retained
as an `int`, including values outside the usual byte range, and is divided by 255 only when the
comparison parameters are derived. Cloning copies all public settings and preserves the referenced
texture resource identity; it does not clone the texture.

## Shader selection

The stock effect has eight logical variants. The shader index is the sum of three independent
selector bits:

| Bit | Condition | Result |
|---|---|---|
| `1` | `FogEnabled == false` | Select the no-fog vertex and pixel path |
| `2` | `VertexColorEnabled == true` | Select the vertex-color vertex path |
| `4` | `AlphaFunction` is `Equal` or `NotEqual` | Select the absolute-difference pixel comparison |

The remaining six comparison functions use the less/greater pixel form. This maps the eight
logical variants onto four vertex shaders and four pixel shaders in the XNA/FNA stock artifact.
Renderers must preserve the observable selection and discard behavior even when their native shader
packaging differs.

## Material, texture and vertex inputs

Every variant samples texture slot 0; there is no `TextureEnabled` property. Before the comparison,
the fragment color is composed as:

```text
sample(Texture) * float4(DiffuseColor * Alpha, Alpha) * optionalVertexColor
```

The vertex-color factor participates only when `VertexColorEnabled=true`. Its alpha therefore
changes whether a fragment passes, rather than merely tinting an already accepted pixel. The stock
shader performs the alpha comparison after all three alpha sources have been multiplied. The
material/vertex factor travels through the stock D3D9 `COLOR0` vertex output and therefore follows
the same [pre-interpolation saturation rule](basiceffect-support.md#d3d9-vertex-color-output-saturation)
as `BasicEffect`; the public property values themselves remain unclamped.

A null `Texture` deliberately samples as opaque black, matching measured Microsoft XNA behavior.
Its alpha is one, so it can pass an alpha test even though its RGB result is black. Implementations
must not substitute white, bypass sampling or accidentally reuse a previously bound texture.

Vertex requirements are semantic rather than stride-selected:

- position semantic 0 is always required;
- texture-coordinate semantic 0 is always required;
- color semantic 0 is required when vertex color is enabled;
- normals are not consumed.

An active variant with a missing required input must be rejected rather than silently routed to an
unrelated declaration shape. General declaration rules are in
[`vertex-format-support.md`](vertex-format-support.md).

## Alpha comparison

Let `a` be the fully composed fragment alpha, `r = ReferenceAlpha / 255` and
`t = 0.5 / 255`. The stock parameter encoding produces these effective keep rules:

| `AlphaFunction` | Fragment is kept when |
|---|---|
| `Always` | always |
| `Never` | never |
| `Less` | `a < r - t` |
| `LessEqual` | `a < r + t` |
| `GreaterEqual` | `a >= r - t` |
| `Greater` | `a >= r + t` |
| `Equal` | `abs(a - r) < t` |
| `NotEqual` | `abs(a - r) >= t` |

The half-byte tolerance makes the floating-point shader comparisons reproduce the intended 8-bit
boundary decisions. Neither `r` nor the derived threshold is clamped when `ReferenceAlpha` lies
outside 0-255.

A rejected fragment writes neither color nor depth, so it cannot occlude a later draw. Accepted
color continues through fog and the ordinary output/blend path.

## Fog

Fog uses FNA's view-space dot-product `FogVector`, with the world-view transform baked into the
vector. When fog is disabled, the effect resets that vector and selects a no-fog shader variant.
When enabled, `FogStart == FogEnd` produces full fog instead of a division-by-zero artifact. Fog is
applied after the alpha test, blends RGB toward `FogColor * composedAlpha`, and preserves alpha; it
cannot change the keep/discard decision.

CNA-FIX-D01 corrects SDLGPU's fog-color alpha multiplication. The shared pixel contract now
checks accepted fragments with combined effect and texture alpha on SDLGPU, EasyGL and Software;
see [sdl-gpu-renderer.md](sdl-gpu-renderer.md#parity-with-easygl) for the evidence boundary.

## Renderer boundary

| Renderer family | Current `AlphaTestEffect` evidence boundary |
|---|---|
| EasyGL (`OPENGLES3`, `OPENGL33`, `WEBGL2`) | Native stock-shader paths, the shared parity corpus and the renderer-neutral contract fixture |
| WebGPU | Native stock WGSL paths plus the shared parity corpus and focused fog coverage |
| SDLGPU | Native stock shader packages plus shared parity and cross-family fixtures |
| Software | CPU stock-effect execution and the renderer-neutral comparison/source/depth contract |
| DirectX9 | Microsoft stock bytecode and focused shader-dispatch, comparison and fog evidence |
| FNA3D | Compiled stock-effect artifacts with focused real discard evidence; final execution remains graphics-driver dependent |
| HEADLESS, STUB | Validation/resource behavior only; no raster-output claim |

The feature matrix and renderer pages own platform qualification, conditional capabilities and
known limitations. This page does not infer one renderer's status from another renderer's pixels.

## CNA extensions

`SetOwnedTexture()` is a `CNAEXT` lifetime adapter for content-loading paths: it makes the effect
share ownership of the selected texture, matching the lifetime of XNA's managed reference. A clone
shares that ownership. `IsStockEffectEXT()`, `GetTypeName()` and `FillGpuDrawParams()` expose
framework/runtime integration without changing the XNA property semantics or the eight stock
variants.

## Executable evidence

Stable fixture names are the maintained evidence:

- `AlphaTestEffectDefaultsTest` and `AlphaTestReferenceScalingTest` cover defaults, setters,
  cloning, owned textures, unclamped references and both comparison-parameter shapes;
- `StockEffectNullTextureTest.AlphaTestEffectSamplesOpaqueBlack` pins the measured null sample;
- parity fixtures `alpha_test_sweep` and `alpha_test_sources` cover every comparison signature,
  null texture and the vertex-color/material sources across EasyGL, WebGPU and SDLGPU;
- `AlphaTestEffectContract` covers the comparison sweep, texture/effect/vertex alpha ordering and
  the rule that discard does not write depth on EasyGL and Software;
- focused renderer tests cover fog, shader dispatch, vertex color, null binding and real discard.

Use the private GPU runner described in the repository instructions for every rendered test.

## Historical task map

Tasks 371-379 established the original property, comparison, material-source, fog and null-texture
coverage. Task 1111 corrected the shared view-space fog formula; SOFTWARE-303 replaced the invented
white null sample with measured opaque black; WEBGPU-174 established the shared rendered parity
fixtures. All of that work is closed. Git history is the detailed narrative; this page and the
executable evidence above are the maintained contract.
