# Standalone CNA graphics extensions

`CNA_CNAEXT=ON` enables the public declarations and implementations in `modules/graphics-ext`;
the option defaults to OFF. CMake registers the module target in either mode, so the existence of
`cna_graphics_ext` alone does not prove that these guarded APIs are enabled.

The retained public classes and enums are:

| Public API | Purpose |
|---|---|
| `AsciiPostProcessEffect`, `AsciiQuantizeMode` | CPU quantization and glyph rendering through a direct `Draw` API. |
| `CRTEffect`, `CRTMaskType` | Portable CRT stylization shader, usable as a SpriteBatch effect. |
| `DepthEffect`, `DepthEffectMode`, `DitherMode` | Colour-depth/palette reduction and ordered dithering, usable as a SpriteBatch effect. |
| `DebugDraw` | Batched wireframe lines, boxes, spheres, frusta and crosses using core `BasicEffect`. |
| `ShaderCodeEXT`, `ShaderPackageEXT`, `ShaderPackageSelectionEXT` | Portable shader payloads, variant selection and diagnostics used by the retained effects and core `ShaderEffect` package overloads. |
| `ShaderBindingTypeEXT`, `ShaderBindingRequirementEXT` | Resource-binding descriptions inside a portable shader package. |

The ASCII font atlas and quantizer are internal. The umbrella header is `CNA/Graphics/CNAEXT.hpp`;
the native target composition `CNA::CnaExt` includes both graphics and device extension modules.
Individual public headers also use the `CNA_CNAEXT` guard. Core XNA types remain in their original
namespace; the utilities above use `CNA::Graphics`.

The CRT and depth packages contain GLSL ES, desktop GLSL, SPIR-V and WGSL variants. Payload presence
does not promise shader execution on every renderer. Use the selected renderer's shader/effect
capability boundary. Direct rendering follows the ordinary render-target plus SpriteBatch/effect
pattern; no post-process chain is required.

ASCII processing reads the full source through `Texture2D::GetData`, quantizes on the CPU and draws
the resulting glyph grid. It requires usable source readback and SpriteBatch support, allocates
scratch storage per draw and is not a GPU-only post-process. See
[ascii-post-process-effect.md](ascii-post-process-effect.md) for controls and limitations.
`DebugDraw` submits line geometry through core `BasicEffect`; it needs the corresponding 3D path.

The C adapter's build availability, owned handles and renderer-dependent behavior are documented
in [c-api/GRAPHICS_EXT.md](c-api/GRAPHICS_EXT.md). The C++ module's presence does not replace that
C API contract.

## Retired scope and future constraint

The former HDR render pipeline, pass framework, shadow engine, clustered rendering, probes,
compute-driven engine helpers and PBR material abstraction were removed in the alpha-stage scope
reduction. `PbrEffect`, `SkinnedPbrEffect`, glTF and CNB/CNJ remain in core graphics/content.

`DebugGizmos` was removed with that engine layer because its API depended on engine-specific light,
probe and shadow types. Any future lightweight replacement should build on `DebugDraw` and accept
generic geometric data such as positions, directions, ranges, angles, matrices, frusta and colours.
This preserves a design constraint, not an implemented API or an active implementation plan.

The legacy filename is retained so existing scope/retirement links resolve. Completed removal
details belong to Git history; this page inventories the surviving module.

## Verification

`CnaExtMasterIncludeTest.RetainedTypesAreComplete` compiles the retained umbrella types.
The module's ASCII, CRT, depth, DebugDraw and shader-infrastructure suites exercise separate
behavior/capability paths. They can be built with the focused `CnaGraphicsExtTests` target and run
through the private GPU runner; a skipped shader path is not a rendering qualification.
