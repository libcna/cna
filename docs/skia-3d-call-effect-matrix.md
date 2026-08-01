# Skia 3D call and effect requirement matrix

This is the SKIA-95 decision input. It refines the `3d` section of
`skia-easygl-test-matrix.md`; it does not claim that the raster backend implements a 3D
pipeline. Run

```sh
python3 scripts/validate_skia_test_matrix.py --dump-3d
```

to print the exact, live entry-to-requirement expansion. The normal audit fails when a `3d`
entry has no specific mapping, when a mapped feature is missing from this document, or when a
feature below has no live evidence. Consequently a new EasyGL 3D test cannot inherit a vague
"3D unsupported" classification without identifying the contract it adds.

## Scope and interpretation

The required set contains all entries whose primary classification is `3d`. It also contains a
closed set of device-dependent cross-cuts whose capability is part of the 3D decision: depth
formats, MSAA/depth combinations, anisotropic stock-effect sampling, cube-target samples and
occlusion queries. Window sizing, presentation interval, reset events, native handles and DXT1
upload remain in their existing device/format phases and are not silently pulled into SKIA-95.

Requirements accumulate. For example, an environment-map fog fixture maps to projected
geometry, transforms, cube sampling, EnvironmentMapEffect, lighting and fog. A property-only
stock-effect fixture may map only to its effect family and validation. The mapping deliberately
uses both the stable entry name and the existing route/evidence text, so human rationale and the
machine audit stay adjacent.

"Unsupported" below means unsupported by the current public Skia backend, not impossible in
principle. CPU cube/volume storage, fragment-only SkSL and atomic MRT/MSAA refusals already exist,
but none supplies the missing vertex/depth pipeline. No row permits setting
`GraphicsCapability::ThreeD` before SKIA-101.

## Required feature vocabulary

| Feature ID | EasyGL contract represented | Current Skia result and decision task |
|---|---|---|
| `3D-VERTEX-LAYOUT` | Fixed position/colour/texture/normal layouts plus custom declarations and exact strides. | No public vertex input; first bounded PCT layout is SKIA-96, full set is conditional SKIA-99. |
| `3D-VERTEX-BUFFER` | Static/dynamic vertex uploads, binding, ranges, readback, usage and lifetime. | Creation rejects as no-3D; conditional SKIA-99. |
| `3D-INDEX-16` | Indexed draws with the ordinary 16-bit index path. | No public index buffer/draw path; conditional SKIA-99. |
| `3D-INDEX-32` | 32-bit buffer, model and DrawUser index paths without truncation. | No public index path; conditional SKIA-99. |
| `3D-DRAW-USER` | Typed and declaration-driven DrawUser calls with range validation. | Refuses atomically at buffer creation; conditional SKIA-99. |
| `3D-PRIMITIVE-TRIANGLE` | Triangle-list assembly and coverage. | One projected triangle-list spike only in SKIA-96; not a capability claim. |
| `3D-PRIMITIVE-STRIP` | Triangle-strip assembly and alternating winding. | Unsupported; conditional SKIA-99. |
| `3D-PRIMITIVE-LINE` | Line-list and line-strip endpoint/raster rules. | Unsupported; conditional SKIA-99. |
| `3D-INSTANCING` | Multiple vertex streams, instance frequency and indexed instanced draws. | SkCanvas has no matching input model; conditional SKIA-99/SKIA-101. |
| `3D-TRANSFORM` | World/view/projection, normal/bone transforms and viewport mapping. | No vertex stage; projection semantics are investigated by SKIA-96. |
| `3D-CLIP-INTERPOLATE` | Homogeneous clipping, perspective-correct varyings, winding after projection and viewport depth. | SkVertices is not an XNA clip-space pipeline; exact boundary is SKIA-96. |
| `3D-TEXTURE-2D` | Effect-driven 2D sampling on geometry and render-target producer/consumer use. | CPU images exist; their 3D coordinates/LOD depend on SKIA-96/SKIA-100. |
| `3D-TEXTURE-CUBE` | Direction-vector cube sampling, cube render targets and face orientation. | CPU face storage exists, sampling does not; decision in SKIA-100/SKIA-101. |
| `3D-TEXTURE-VOLUME` | 3D coordinates, volume filtering and custom-effect binding. | CPU volume storage exists, sampling does not; decision in SKIA-100/SKIA-101. |
| `3D-SAMPLER-MIP` | Independent slots, address/filter/mip selection and LOD behavior. | Raster 2D sampling is bounded; 3D/mip contract is unsupported, SKIA-99/SKIA-100. |
| `3D-SAMPLER-ANISOTROPY` | Probed anisotropic filtering for stock-effect and texture paths. | Deterministically rejected by SKIA-79; remains device-dependent. |
| `3D-STATE-DEPTH` | Depth storage, clear, compare/write, target persistence and bias interaction. | No depth attachment; CPU feasibility is conditional SKIA-97. |
| `3D-STATE-STENCIL` | Reference/masks, compare, operations, two-sided winding and colour-write interaction. | No stencil attachment; conditional on depth bridge, SKIA-98. |
| `3D-STATE-CULL-FILL` | Front-face convention, all cull modes, solid/wireframe and raster bias. | No projected triangle raster state; conditional SKIA-96/SKIA-99. |
| `3D-STATE-BLEND-COLOR` | Effect alpha/vertex colour, blend and colour-write interaction on geometry. | 2D blend pieces exist but 3D coverage/varyings do not; SKIA-99/SKIA-100. |
| `3D-STATE-MRT` | Multiple simultaneous colour outputs with atomic target binding. | Not representable by one raster canvas; exact refusal is proven by SKIA-87. |
| `3D-STATE-MSAA` | Sample-count negotiation, depth coupling, resolve, mip readback and cube faces. | Raster requests above one sample reject; measured in SKIA-76/SKIA-77. |
| `3D-STATE-ORDER` | Deferred source lifetime, target/backbuffer pass order and SpriteBatch/3D boundaries. | 2D ordering exists; mixed 3D ordering remains unsupported, SKIA-99/SKIA-102. |
| `3D-VIEWPORT-SCISSOR` | Viewport/scissor application to projected geometry and deferred draws. | 2D canvas route exists; 3D mapping depends on SKIA-96/SKIA-99. |
| `3D-FX-BASIC` | BasicEffect texture, vertex colour, alpha, lighting and shader combinations. | Properties exist; public draw path rejects. Family evaluation is SKIA-100. |
| `3D-FX-ALPHATEST` | AlphaTestEffect compare modes, reference alpha, vertex colour and fog on geometry. | Fragment decision is proven only in isolation; complete route is SKIA-100. |
| `3D-FX-DUAL` | Two independent 2D samplers, doubling equation, vertex colour, alpha and fog. | Fragment equation is proven only in isolation; complete route is SKIA-100. |
| `3D-FX-ENVMAP` | EnvironmentMapEffect cube reflection, Fresnel, amount, eye/normal space and lighting. | Cube sampling and vertex transforms are absent; SKIA-100/SKIA-101. |
| `3D-FX-SKINNED` | SkinnedEffect bone weights/matrices, vertex colour, lighting and fog. | No skinned vertex stage; SKIA-100/SKIA-101. |
| `3D-FX-PBR` | PbrEffect and SkinnedPbrEffect material, lighting and skinning variants. | No PBR/skinned 3D pipeline; SKIA-100/SKIA-101. |
| `3D-FX-CUSTOM` | Arbitrary EasyGL GLSL vertex+fragment programs and custom varyings/layouts. | Fragment-only tagged SkSL cannot preserve this contract; reject unless SKIA-101 funds a compiler/emulator. |
| `3D-SHADE-LIGHT` | Normal transforms, directional/multiple lights, emissive, specular, Phong/Lambert and per-pixel variants. | No normal-varying/lighting pipeline; family evaluation is SKIA-100. |
| `3D-SHADE-FOG` | View-space fog and effect-specific fog interpolation/composition. | No vertex-derived fog coordinate; family evaluation is SKIA-100. |
| `3D-MODEL-SKIN` | Model meshes/effects, hierarchy, animation, avatars, skeletons and skin weights. | Content may load but its rendering path is absent; SKIA-99/SKIA-100. |
| `3D-QUERY-OCCLUSION` | Begin/End lifecycle, availability/nonblocking result and visible/depth-occluded PixelCount. | Capability is false; feasibility decision is SKIA-104, implementation/refusal is SKIA-105. |
| `3D-RESOURCE-CONTRACT` | Missing bindings, null resources, disposed/dynamic lifetime, descriptor capacity and transfer ranges. | Existing failures must remain atomic; exhaustive uniform refusal is SKIA-102. |
| `3D-TARGET-PASS` | Render-target/depth/cube binding, first use, clear, readback and producer/consumer transitions. | Colour-only 2D pieces exist; 3D attachment/pass behavior remains unsupported. |

## Hard gates

- SKIA-96 may prove only one projected PCT triangle-list slice. It cannot satisfy strip/line,
  buffer, depth, custom vertex stage, stock-effect, model, instancing or query rows.
- SKIA-97 and SKIA-98 run only if the previous bridge is measurably exact and bounded. Failure is
  evidence for SKIA-101, not permission to weaken depth/stencil semantics.
- Cube/volume CPU storage is not sampling support. Fragment-only SkSL is not an EasyGL vertex
  program. Transparent output is not alpha-test discard. One canvas is not MRT.
- SKIA-101 must account for every live feature ID in this document. If it rejects the complete
  emulator, SKIA-102 supplies uniform public failures; if it accepts, SKIA-103 creates a successor
  implementation plan before capability changes.

