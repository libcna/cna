# Standalone CNA graphics extensions

`CNA_CNAEXT=ON` builds `modules/graphics-ext`. This module contains exactly these public
classes and enums:

| Public API | Purpose |
|---|---|
| `AsciiPostProcessEffect`, `AsciiQuantizeMode` | CPU quantization and glyph rendering through a direct `Draw` API. |
| `CRTEffect`, `CRTMaskType` | Portable CRT stylization shader, usable as a SpriteBatch effect. |
| `DepthEffect`, `DepthEffectMode`, `DitherMode` | Colour-depth/palette reduction and ordered dithering, usable as a SpriteBatch effect. |
| `DebugDraw` | Batched wireframe lines, boxes, spheres, frusta and crosses using core `BasicEffect`. |
| `ShaderCodeEXT`, `ShaderPackageEXT`, `ShaderPackageSelectionEXT` | Portable shader payloads, variant selection and diagnostics used by the retained effects and core `ShaderEffect` package overloads. |
| `ShaderBindingTypeEXT`, `ShaderBindingRequirementEXT` | Resource-binding descriptions inside a portable shader package. |

The ASCII font atlas and quantizer are internal. The CRT and depth shaders retain GLSL ES,
desktop GLSL, SPIR-V, WGSL and HLSL variants. Direct rendering follows the ordinary
render-target plus SpriteBatch/effect pattern; no post-process chain is required.

The former HDR render pipeline, pass framework, shadow engine, clustered rendering, probes,
compute-driven engine helpers and PBR material abstraction were removed in the alpha-stage scope
reduction. `PbrEffect`, `SkinnedPbrEffect`, glTF and CNB/CNJ remain in core graphics/content.
See [the scope audit](graphics-ext-scope-reduction.md) for the dependency classification and the
future DebugGizmos design intent.
