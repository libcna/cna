# D3D9 renderer — divergence report (DX9 D9-121)

This is the measurement behind `DIRECTX9`'s claim of pixel indistinguishability from XNA 4.0, and
the explicit boundary of what it does not yet measure. **Result: every active oracle scene matches
real XNA 4.0 exactly, at `--tolerance 0`.** The validation environment is Wine+DXVK only; that
caveat, and real-hardware verification (`D9-140`), are owned by
[`directx9-renderer.md`](directx9-renderer.md#the-one-caveat-every-result-above-inherits).

## How this is measured

Every scene in `tools/xna-oracle/scenes/*.scene` is rendered twice: once by the real XNA 4.0
runtime (`tools/xna-oracle/Oracle.cs`, compiled by the real in-prefix `csc.exe`, running under
Wine with DXVK installed into that prefix) and once by CNA's own D3D9 renderer
(`tools/xna-oracle/CnaOracleRender.cpp`, through CNA's public
`Game`/`GraphicsDeviceManager`/`GraphicsDevice`/`Texture2D`/effect API — never the raw renderer
interface). Both sides execute through the same DXVK Direct3D 9-over-Vulkan implementation, so a
pixel diff measures CNA rather than a driver difference. `scripts/xna-diff.py --tolerance 0` (the
default) compares all 65,536 pixels per scene; `0/65536` is required for a PASS.

`D3D9_XNA_Diff` (`D9-120`) runs the corpus as a CTest against the checked-in real-XNA renders in
`tools/xna-oracle/reference/*.png`, so it needs only the D3D9 Wine prefix, not the XNA one.
`D3D9_XNA_Manifest` (`scripts/check_xna_oracle_manifest.py`) requires exactly one same-stem
reference per scene: 40 active top-level scenes plus 7 auxiliary pairs in subdirectories.

## What the corpus covers

All pixel-exact against real XNA 4.0:

- **All 5 XNA stock effects** (`BasicEffect`, `AlphaTestEffect`, `DualTextureEffect`,
  `EnvironmentMapEffect`, `SkinnedEffect`), including `BasicEffect`'s multi-light summation,
  vertex-lit and `PreferPerPixelLighting` paths (`lit_textured_quad`,
  `lit_textured_quad_pixellighting`, `skinned_pixellighting_*`).
- **`IEffectFog`**, shared across all 5 effects, including a negative-`FogEnd` gradient
  (`fog_gradient_quad`).
- **All 8** `AlphaTestEffect.AlphaFunction` values, across both pixel-shader buckets
  (`PSAlphaTestLtGt` and `PSAlphaTestEqNe`).
- **`EnvironmentMapEffect`**: `FresnelFactor` with a per-vertex gradient (`envmap_fresnel_quad`)
  and the specular bucket (`envmap_specular_quad`).
- **All 3** `SkinnedEffect.WeightsPerVertex` values (1/2/4 bones).
- **`SpriteBatch`**: destination/source rect, rotation, origin, `SpriteEffects` flip, the D3D9
  half-pixel offset (`D9-91`), `Wrap`/`Mirror` addressing, `Deferred`/`BackToFront`/`FrontToBack`
  sorting with nonzero `layerDepth`, and texture-change batching.
- **All 4** `PrimitiveType` values and **`D3DCULL` winding** (`cullmode_*`).
- **Render-to-texture**: `rendertarget_sampled_quad` samples a `RenderTarget2D` through the public
  API.
- **`GraphicsProfile.Reach`/`.HiDef`**: profile-support queries and resource-creation-time
  enforcement through the public API.

## Not measured by the oracle

| Area | Why it is not in the corpus |
|---|---|
| Cube render targets, target mip generation, MSAA | Covered by focused D3D9 CTests (`DirectX9_RenderTarget_SamplingOrientation`, `_ProducerConsumer`, `_EffectSource`, `DirectX9FloatRenderTargetTests.cpp`) rather than oracle scenes. |
| Every `SurfaceFormat` besides `Color` | Ordinary D3D9 textures have a device-qualified classic/float mapping with byte-exact transfer checks through the generic `Texture2D::SetData<T>`/`GetData<T>` (`SOFTWARE-276`); render targets accept `Color` and, where `CheckDeviceFormat` allows, the float/half formats (`GAP-10`). No dedicated oracle scenes sweep them. Native sRGB/BC7 textures are rejected. |
| `SpriteSortMode.Immediate` | Its only behavioral difference from `Deferred` (per-`Draw()` submission instead of batching until `End()`) is not pixel-observable: N draws in the same order produce the identical image. |
| `SpriteSortMode.Texture` | Not viable as a deterministic scene: FNA's `TextureComparer.Compare` sorts by `texture.GetHashCode()`, an implementation-defined identity hash with no predictable ordering. |
| NPOT-wrap-on-`Reach` | XNA's enforcement timing is undocumented and FNA implements none (`ProfileCapabilities.cs`, `D9-100`); there is no reference to verify an enforcement point against. |
| Hardware instancing on `Reach` | Not a gap: XNA's `DrawInstancedPrimitives` (IL_003d–0064, IL_007c–00a3) has no separate desktop-Reach instancing prohibition; shared code checks the primitive and instance counts against the profile ceiling independently (`GraphicsProfileDrawValidationTest`). |

## The six project-wide CNA-vs-XNA divergences

The DX9 plan named six divergences found by treating XNA as the specification. Their current status:

1. **`PreferPerPixelLighting` ignored (CNA always lit per pixel).** Resolved on every rasterizing
   renderer: `GpuDrawParams` carries `preferPerPixelLighting`, and EasyGL, WebGPU, SDLGPU,
   Software, DirectX9 and FNA3D select vertex-lit or per-pixel paths from it. On D3D9 both
   `lit_textured_quad` (vertex-lit default) and the per-pixel scenes match XNA exactly.
2. **`oneLight` inferred, not carried in `GpuDrawParams`.** D3D9's dispatch derives it losslessly
   from the existing light fields (`D9-82b`). That is a shader-selection fix, not a pixel
   measurement; the pixel impact is expected to be zero (a black light contributes nothing).
3. **`GraphicsProfile` was decorative.** Resolved as an API contract: XNA's Reach/HiDef rules —
   resource size and format limits, draw and MRT ceilings, Reach-only refusals — are enforced in
   shared code for every renderer (`ProfileValidation.cpp`, `GraphicsDevice`; pinned by the
   `GraphicsProfile*Tests`). Only the *hardware* answer is renderer-specific: DirectX9's adapter
   hooks answer `IsProfileSupported`, `QueryRenderTargetFormat` and `QueryBackBufferFormat` from
   `D3DCAPS9`; a renderer without an `isProfileSupported` hook reports every valid profile as
   supported rather than inventing a capability table (`D9-101`).
4. **`SpriteBatch`'s D3D9 half-texel convention.** Closed on D3D9: `D9-91`'s offset is
   oracle- and mutation-verified with a multi-texel boundary check (a 1×1-texture scene cannot
   detect a missing offset). Modern-API renderers never had this gap (design decision 10).
5. **Runtime uniform branching vs. XNA's compile-time shader permutations.** Every passing scene
   is evidence the paths it covers are benign; no dedicated sweep has targeted it.
6. **Self-declared "numerically equivalent" shader-math deviations** (e.g. `AmbientLightColor`
   folded into `DiffuseColor`). The lit scenes pass at `tolerance=0`, which supports the
   ambient-folding claim where ambient light is exercised, but no deliberate sweep covers every
   such claim.

## Cross-renderer measurement (D9-A6)

The same corpus, `CnaOracleRender.cpp` and reference images also run through EasyGL:
`cna_oracle_render_easygl` is the same source built natively, and
`scripts/run-oracle-corpus-diff-easygl.sh <path-to-cna_oracle_render_easygl> [scene ...]` renders
and diffs each scene at `--tolerance 0` (no Wine). The whole-corpus run is a measurement, not a
gate: EasyGL renders through a different API and driver stack, so a diff there mixes rendering
differences with driver differences that the D3D9 setup rules out.

Findings from that measurement became fixes and regressions rather than a standing divergence list:

- `fog_gradient_quad` (negative `FogEnd`) exposed EasyGL's fog formula; it now uses FNA's
  view-space `FogVector` (Task 1111, `EasyGL_*_Fog` tests).
- `envmap_fresnel_quad` exposed per-fragment Fresnel evaluation; it is now per vertex and
  interpolated (Task 1112, `EasyGL_EnvironmentMapEffect_Fresnel_Gradient`).
- The aliased-line scenes `colored_linelist_quad` and `colored_linestrip_quad` are byte-exact on
  EasyGL and Software (`SOFTWARE-344`); `EasyGL_XnaLineCoverage` runs the script on those two
  scenes as a CTest.
- Triangle edge coverage depends on the D3D9 pixel-centre emulation; its remaining boundary is the
  "Pixel-centre bias" entry in [KNOWN_ISSUES.md](../KNOWN_ISSUES.md).
