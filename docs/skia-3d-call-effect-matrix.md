# Skia 3D call and effect requirement matrix

This began as the SKIA-95 decision input and is now the live vocabulary audited by the accepted
SKIA-101 2D-only ADR. It refines the `3d` section of
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
but none supplies the missing vertex/depth pipeline. The accepted
`docs/skia-3d-emulation-adr.md` rejects public 3D support; no row permits setting
`GraphicsCapability::ThreeD` without replacing that ADR and funding a successor plan.

## Required feature vocabulary

| Feature ID | EasyGL contract represented | Current Skia result and decision task |
|---|---|---|
| `3D-VERTEX-LAYOUT` | Fixed position/colour/texture/normal layouts plus custom declarations and exact strides. | SKIA-99 decodes all seven built-ins, 12 formats and 13 usages in isolation; no public vertex input. |
| `3D-VERTEX-BUFFER` | Static/dynamic vertex uploads, binding, ranges, readback, usage and lifetime. | SKIA-99 proves bounded CPU replacement uploads and hints; public creation/readback/lifetime remain unsupported. |
| `3D-INDEX-16` | Indexed draws with the ordinary 16-bit index path. | SKIA-99 preserves 16-bit upload, offsets and fetch in isolation; no public index path. |
| `3D-INDEX-32` | 32-bit buffer, model and DrawUser index paths without truncation. | SKIA-99 fetches indices 70000+ without truncation in isolation; no public index path. |
| `3D-DRAW-USER` | Typed and declaration-driven DrawUser calls with range validation. | SKIA-99 proves raw, four typed, and 16/32-bit indexed input assembly; public calls still reject. |
| `3D-PRIMITIVE-TRIANGLE` | Triangle-list assembly and coverage. | SKIA-99 proves list assembly/culling/wire expansion; SKIA-97 coverage remains non-production. |
| `3D-PRIMITIVE-STRIP` | Triangle-strip assembly and alternating winding. | SKIA-99 proves alternating-winding list expansion; coverage remains non-production. |
| `3D-PRIMITIVE-LINE` | Line-list and line-strip endpoint/raster rules. | SKIA-99 proves assembly only; SKIA-101 rejects funding exact line/point raster rules. |
| `3D-INSTANCING` | Multiple vertex streams, instance frequency and indexed instanced draws. | Not covered by SKIA-99 and rejected by the SKIA-101 2D-only decision. |
| `3D-TRANSFORM` | World/view/projection, normal/bone transforms and viewport mapping. | No vertex stage; projection semantics are investigated by SKIA-96. |
| `3D-CLIP-INTERPOLATE` | Homogeneous clipping, perspective-correct varyings, winding after projection and viewport depth. | SkVertices is not an XNA clip-space pipeline; exact boundary is SKIA-96. |
| `3D-TEXTURE-2D` | Effect-driven 2D sampling on geometry and render-target producer/consumer use. | CPU images exist; their 3D coordinates/LOD depend on SKIA-96/SKIA-100. |
| `3D-TEXTURE-CUBE` | Direction-vector cube sampling, cube render targets and face orientation. | CPU face storage stays transfer-only; SKIA-101 rejects direction sampling. |
| `3D-TEXTURE-VOLUME` | 3D coordinates, volume filtering and custom-effect binding. | CPU volume storage stays transfer-only; SKIA-101 rejects 3D-coordinate sampling. |
| `3D-SAMPLER-MIP` | Independent slots, address/filter/mip selection and LOD behavior. | Raster 2D sampling is bounded; 3D/mip contract is unsupported, SKIA-99/SKIA-100. |
| `3D-SAMPLER-ANISOTROPY` | Probed anisotropic filtering for stock-effect and texture paths. | Capability is false; SpriteBatch level-zero sampling has an exact Linear fallback, while the stock-3D sampler route rejects. |
| `3D-STATE-DEPTH` | Depth storage, clear, compare/write, target persistence and bias interaction. | No depth attachment; CPU feasibility is conditional SKIA-97. |
| `3D-STATE-STENCIL` | Reference/masks, compare, operations, two-sided winding and colour-write interaction. | No stencil attachment; conditional on depth bridge, SKIA-98. |
| `3D-STATE-CULL-FILL` | Front-face convention, all cull modes, solid/wireframe and raster bias. | SKIA-99 proves post-projection winding, all cull modes and wire expansion; depth bias/pixel rules remain absent. |
| `3D-STATE-BLEND-COLOR` | Effect alpha/vertex colour, blend and colour-write interaction on geometry. | SKIA-100 proves opaque unlit material/vertex-colour bytes only; general blend/state ordering remains absent. |
| `3D-STATE-MRT` | Multiple simultaneous colour outputs with atomic target binding. | Not representable by one raster canvas; exact refusal is proven by SKIA-87. |
| `3D-STATE-MSAA` | Sample-count negotiation, depth coupling, resolve, mip readback and cube faces. | Raster requests above one sample reject; measured in SKIA-76/SKIA-77. |
| `3D-STATE-ORDER` | Deferred source lifetime, target/backbuffer pass order and SpriteBatch/3D boundaries. | 2D ordering exists; mixed 3D ordering remains unsupported, SKIA-99/SKIA-102. |
| `3D-VIEWPORT-SCISSOR` | Viewport/scissor application to projected geometry and deferred draws. | 2D canvas route exists; 3D mapping depends on SKIA-96/SKIA-99. |
| `3D-FX-BASIC` | BasicEffect texture, vertex colour, alpha, lighting and shader combinations. | SKIA-100 proves one unlit/no-fog textured PCT route; normal/lighting/fog/variants/public draw remain gaps. |
| `3D-FX-ALPHATEST` | AlphaTestEffect compare modes, reference alpha, vertex colour and fog on geometry. | Compare/discard pieces stay prototype-only; SKIA-101 rejects the incomplete public route. |
| `3D-FX-DUAL` | Two independent 2D samplers, doubling equation, vertex colour, alpha and fog. | Two-child formula/address pieces are reusable; per-slot geometry sampling, fog, coverage and public draw remain gaps. |
| `3D-FX-ENVMAP` | EnvironmentMapEffect cube reflection, Fresnel, amount, eye/normal space and lighting. | SKIA-100 matrix confirms cube direction sampling, normal/eye transforms, lighting/Fresnel and public draw are absent. |
| `3D-FX-SKINNED` | SkinnedEffect bone weights/matrices, vertex colour, lighting and fog. | Layout/palette data exist; weighted position/normal/fog/lighting and public integration are absent. |
| `3D-FX-PBR` | PbrEffect and SkinnedPbrEffect material, lighting and skinning variants. | Layout/storage pieces exist; TBN, five samplers, BRDF, optional skinning and public integration are absent. |
| `3D-FX-CUSTOM` | Arbitrary EasyGL GLSL vertex+fragment programs and custom varyings/layouts. | Fragment-only tagged SkSL stays 2D-only; SKIA-101 rejects a GLSL vertex/compiler emulator. |
| `3D-SHADE-LIGHT` | Normal transforms, directional/multiple lights, emissive, specular, Phong/Lambert and per-pixel variants. | SKIA-100 confirms forwarding alone is insufficient; no integrated CPU normal/lighting variant pipeline exists. |
| `3D-SHADE-FOG` | View-space fog and effect-specific fog interpolation/composition. | Fog vectors exist in common code, but their pre/post-skin vertex evaluation, interpolation and fragment mix are absent. |
| `3D-MODEL-SKIN` | Model meshes/effects, hierarchy, animation, avatars, skeletons and skin weights. | Layout/palette storage exists; SKIA-100 confirms weighted position/normal/tangent rendering is absent. |
| `3D-QUERY-OCCLUSION` | Begin/End lifecycle, availability/nonblocking result and visible/depth-occluded PixelCount. | SKIA-104 proves raster emulation unsound; SKIA-105 retains false/zero properties, throwing lifecycle and false capability. |
| `3D-RESOURCE-CONTRACT` | Missing bindings, null resources, disposed/dynamic lifetime, descriptor capacity and transfer ranges. | Existing failures must remain atomic; exhaustive uniform refusal is SKIA-102. |
| `3D-TARGET-PASS` | Render-target/depth/cube binding, first use, clear, readback and producer/consumer transitions. | Colour-only 2D pieces exist; 3D attachment/pass behavior remains unsupported. |

## Hard gates

- SKIA-96 proved only one projected PCT triangle-list slice. It cannot satisfy strip/line,
  buffer, depth, custom vertex stage, stock-effect, model, instancing or query rows.
- SKIA-97 and SKIA-98 are bounded depth/stencil evidence, not permission to weaken attachment or
  state semantics.
- Cube/volume CPU storage is not sampling support. Fragment-only SkSL is not an EasyGL vertex
  program. Transparent output is not alpha-test discard. One canvas is not MRT.
- `Skia_3DDecision_Audit` requires the accepted SKIA-101 ADR to account for every live feature ID
  in this document. The complete emulator was rejected, so SKIA-102 supplies uniform public
  failures. SKIA-103 is obsolete unless a replacement ADR first accepts and funds a successor.
