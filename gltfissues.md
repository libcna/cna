# Analysis of Incorrect glTF Rendering in CNA

Analysis date: 2026-07-28

> **HISTORICAL — superseded as a working document by `plan_gltf.md` (`GLTF-450`).**
>
> Every finding below was **reproduced, made executable, and fixed** by the glTF correctness
> campaign, which began from this analysis. The document is kept unrewritten as the evidence of what
> was measured on 2026-07-28; **do not read its present tense as a description of CNA today.**
>
> Where each finding went, so the two records cannot contradict each other:
>
> | This document says | Campaign record | State today |
> |---|---|---|
> | §1 "CNA discards glTF node transforms" | audit defects **D1/D2/D3**, tasks `GLTF-107`/`GLTF-113`/`GLTF-114` | **Fixed.** The scene graph reaches `Model` as a real bone hierarchy; `GltfConformanceL4` asserts every fixture's world geometry against a spec-derived oracle. |
> | §2 "does not preserve `baseColorFactor`, downgrades to `BasicEffect`" | audit defect **D7**, tasks `GLTF-215`/`GLTF-216`/`GLTF-217`/`GLTF-219`/`GLTF-221` | **Fixed.** Effect selection follows the material *model* the file declares, not which texture maps happen to be present, and every factor is asserted at the effect boundary (L6), not only at import. |
> | §3 "PBR parts have no lighting" | `GLTF-242`/`GLTF-325`/`GLTF-424` | **Split, deliberately.** The importer's half is fixed: `KHR_lights_punctual` is imported and a file with no lights is *reported* as such. Calling `EnableDefaultLighting()` when an import contributed no light is the **viewer's** decision and lives in `cna-gltf-viewer` (`GLTF-424`). |
> | §4 "`KHR_materials_transmission` is ignored" | `GLTF-339` | **Approximated and reported**, not implemented: `alpha = 1 - transmissionFactor`, with the extension deliberately **not claimed**, so a file that *requires* transmission is refused rather than drawn as tinted alpha. `docs/gltf-limitations.md` states the four ways it is not physical. |
> | §5's further losses — rigid animation, samplers, UV sets, variants, colour space, culling, camera | `GLTF-294`, `GLTF-202`/`GLTF-203`, `GLTF-181`/`GLTF-188`, `GLTF-341`, `GLTF-209`–`GLTF-213`, `GLTF-231`, `GLTF-317`–`GLTF-322` | Mixed by design, and each one is now either fixed or a **named** limitation with the report field that says so. `KHR_materials_variants` remains `PARSED_BUT_IGNORED`; culling is carried and not applied. |
> | "Missing Regression Tests" | the whole campaign | The corpus is 71 generated assets and a nine-rung oracle ladder (`docs/gltf-conformance.md`). |
>
> The current state of any of the above is `plan_gltf.md` (the row), `docs/gltf-limitations.md`
> (what is still lost, and where it is reported) and `known_bugs.md` (the defect ledger). If this
> document and one of those ever disagree, those are right and this one is 2026-07-28.

## Executive Summary

The incorrect rendering of the watch is not primarily a bug in the `easy-gl`
or `meta-gl` libraries. Critical information is already lost or
misinterpreted in the CNA layer before the data reaches OpenGL.

Four main issues contribute to the result shown in the screenshot:

1. **CNA discards glTF node transforms.** The backplate and all hands are
   therefore rendered in mesh-local coordinates instead of their transformed
   scene coordinates.
2. **CNA does not preserve `baseColorFactor` and downgrades most standard glTF
   materials to `BasicEffect`.** These parts then use the default white color
   instead of black, gold, or gray.
3. **PBR parts have no lighting.** The file contains no lights, `PbrEffect`
   defaults to zero ambient light with all three directional lights disabled,
   and the viewer does not call `EnableDefaultLighting()`. The resulting PBR
   surfaces are black.
4. **`KHR_materials_transmission` is ignored.** The watch glass has
   `transmissionFactor = 1`, but is converted to an opaque white `BasicEffect`
   that obscures the elements underneath it.

The black-and-white appearance on the right side of the screenshot exactly
matches the combination of 15 white `BasicEffect` primitives and 4 unlit black
`PbrEffect` primitives in the generated `scene.cnj`.

`cna-gltf-viewer` bears some responsibility for the missing fallback lighting
and certain presentation choices. However, the CNA converter irreversibly
damages the geometry and materials before the CNJ is loaded, so the viewer
cannot repair them afterward.

## Exact Reproduction Analyzed

- Screenshot:
  `/home/robertvokac/Pictures/Screenshots/Screenshot From 2026-07-28 20-09-05.png`
  - timestamp: `2026-07-28 20:09:05 +02:00`
- Model:
  `../cna-gltf-viewer/cmake-build-debug/ChronographWatch.glb`
  - size: 7,446,368 bytes
  - SHA-256:
    `8e875fcd83efb433afed9ef1c18b2c2b2e075e2bf48371cadfd2a3cf529f1aef`
- Converter output used immediately before the screenshot:
  `/tmp/cna-gltf-viewer/import-357122912198457-0/scene.cnj`
  - timestamp: `2026-07-28 20:08:51 +02:00`, approximately 14 seconds before
    the screenshot
- Backend in the viewer cache: `CNA_GRAPHICS_BACKEND=EASYGL`
- The viewer was built against `../cnaaudit`, commit
  `32639a135b718916f93ac3f64fa83bc11099ffac`. The current `cnagltf` checkout
  is at the same commit, so the problem is not caused by a difference between
  these two branches.
- Lower-level components actually linked:
  - `easy-gl`: `62c0a248a6c4144abaf92c0530cc2a2395e5fd37`
  - `meta-gl`: `d51fcd7f455de8e3df3549edbed84f2b5527f18a`

There were already unrelated uncommitted changes in `cna-gltf-viewer` and
other checkouts. This analysis did not modify them.

The analyzed GLB contains:

- 1 scene;
- 14 nodes;
- 13 meshes with 19 primitives in total;
- 29 material records, of which 12 distinct materials are used as a default
  material by at least one primitive;
- 8 textures;
- 1 animation;
- no skins, lights, or cameras;
- the `KHR_materials_transmission`, `KHR_materials_variants`, and
  `KHR_texture_transform` extensions.

## Data Flow and Point of Loss

The data flow is:

```text
ChronographWatch.glb
    -> cna_tool_gltf_to_cnj
    -> CNA GltfImportCore
    -> scene.cnj + binary sidecars + PNG
    -> CNA ContentManager / Model
    -> cna-gltf-viewer
    -> CNA EasyGL backend
    -> easy-gl
    -> meta-gl
    -> OpenGL
```

The critical loss occurs in the first three steps:

- `include/CNA/Internal/GltfImport/GltfImportCore.hpp:163` defines
  `MeshGroup` only as a `skin` and an array of `cgltf_mesh*`. It contains
  neither the node nor its transform.
- `src/CNA/Internal/GltfImport/GltfImportCore.cpp:1231`
  (`CollectMeshGroups`) stores only `node.mesh` for each node.
- `tools/gltf_to_cnj/gltf_to_cnj.cpp:203` then iterates only over meshes and
  primitives.
- `src/CNA/Internal/GltfImport/GltfImportCore.cpp:1100` writes only local
  `POSITION * unitScale` into the vertex buffer.
- `tools/gltf_to_cnj/gltf_to_cnj.cpp:455` serializes the mesh array without
  transforms or node associations.
- `src/Microsoft/Xna/Framework/Content/ContentManager.cpp:2126` creates an
  identity root, and `ContentManager.cpp:2398` creates an identity child bone
  for each mesh; the CNJ contains no transform that could be loaded.
- `../cna-gltf-viewer/src/ViewerGame.cpp:204` finally calls
  `model.Draw(Matrix::Identity, view, projection)`.

Switching the viewer from offline conversion to direct
`ContentManager.Load<Model>(glTF)` loading would not solve the problem. The
direct path uses the same `CollectMeshGroups`, and
`src/Microsoft/Xna/Framework/Content/ContentManager.cpp:1885` again iterates
only over `cgltf_mesh*`, creating identity mesh bones for them.

## 1. Discarded Node Transforms

In glTF, a mesh's global transform is determined by the node that instantiates
it, and a node's global transform is the product of its ancestors' transforms
and its local transform. CNA discards this information for static meshes.

Examples directly from the watch:

- `Backplate Khronos` has translation `[0, 0, 0.01]` and a rotation of
  approximately `-90°` around X.
- The parent node `Hands` has translation
  `[0.112724, 0.158261, 0.758009]` and a rotation of approximately `-90°`
  around X.
- Each hand also has its own local translation.

Quantitative evidence:

| Part | CNA/exported local bounds | Correct world bounds |
|---|---|---|
| Backplate | Y `-0.0710 .. -0.0066`, Z `-1.9022 .. 1.9834` | Y `-1.9022 .. 1.9834`, Z `0.0166 .. 0.0810` |
| Hour hand | Y `-0.7620 .. -0.7315`, Z `-0.2314 .. 0.3759` | Y `-0.2314 .. 0.3759`, Z `0.7371 .. 0.7676` |
| Minute hand | Y `-0.8000 .. -0.7684`, Z `-0.2629 .. 0.6765` | Y `-0.2629 .. 0.6765`, Z `0.7740 .. 0.8056` |

The bounds of the actual `scene_mesh*_verts.bin` files match the local
left-hand column. This is therefore not merely an incorrectly chosen camera
or OpenGL matrix.

Additional consequences:

- `--scale` scales local vertices and bone translations, but naturally cannot
  scale the discarded translations of regular glTF nodes.
- Automatic framing in the viewer reads the same local sidecars
  (`ViewerGame.cpp:72`) and computes the camera from an already incorrect
  coordinate space.
- A shared mesh instantiated by multiple nodes is exported multiple times,
  but every instance receives the same identity transform.

**Responsibility: unambiguously the CNA glTF import/CNJ model pipeline.**

## 2. Lost Colors and Incorrect Effect Selection

glTF uses metallic-roughness PBR even when values are provided only as factors
and no maps are present. CNA uses the following condition in
`GltfImportCore.cpp:909`:

```cpp
out.usePbr = (!out.colored) &&
             (out.normalImage != nullptr ||
              out.metallicRoughnessImage != nullptr);
```

PBR is therefore selected only when a normal or metallic-roughness map is
present. A material with `baseColorFactor`, `metallicFactor`, and
`roughnessFactor`, but without these maps, falls back to `BasicEffect`.

At the same time, `MeshOut` preserves PBR metallic, roughness, and emissive
factors, but does not preserve the RGBA `baseColorFactor` at all. The converter
therefore cannot write it to CNJ, and the loader cannot apply it through the
existing `BasicEffect::setDiffuseColorProperty()` or
`PbrEffect::setDiffuseColorProperty()`.

Specific losses in the default gold variant:

| Material | Correct glTF data | CNA result |
|---|---|---|
| Band Carbon Fiber Gold | base `[1, 0.72, 0.315]`, metallic `1`, roughness `0.4`, normal map | PBR, but the base factor is lost |
| Band Plastic Gold | base `[0.03, 0.03, 0.03]`, metallic `0`, roughness `0.3` | white `BasicEffect` |
| Bezel Frame Gold | base `[1, 0.72, 0.315]`, metallic `1`, roughness `0.3` | white `BasicEffect` |
| Button Metal | base `[0.762, 0.743, 0.787]`, metallic `1`, roughness `0.3` | white `BasicEffect` |
| Button Plastic Gold | base `[0.03, 0.03, 0.03]`, metallic `0`, roughness `0.3` | white `BasicEffect` |
| Hand Plastic/Metal/Gold | black, metallic gray, and gold | white `BasicEffect` parts |

The generated CNJ contains:

- 15× `BasicEffect`;
- 4× `PbrEffect`;
- no `baseColorFactor`, `diffuseColor`, or per-material alpha.

`BasicEffect` defaults to `DiffuseColor=(1,1,1)` with lighting disabled, so
untextured parts become solid white. This exactly explains the white part of
the render on the right.

**Responsibility: unambiguously the CNA glTF import, CNJ serialization, and
both CNA model loaders.**

## 3. Black PBR Parts Are a Deterministic Result of Zero Lighting

The model contains neither `KHR_lights_punctual` nor any other light. This is
not itself a model defect; conventional glTF viewers provide their own
environment lighting.

However, CNA creates a `PbrEffect` whose default state is:

- `AmbientLightColor = (0,0,0)` in `PbrEffect.hpp:251`;
- all `DirectionalLight` instances are disabled by default
  (`DirectionalLight.cpp:6`);
- neither the importer nor the CNJ loader calls
  `PbrEffect::EnableDefaultLighting()`;
- the viewer does not configure loaded effects in any way.

`PbrEffect::FillGpuDrawParams()` sets `lightingEnabled=true`, but passes zero
ambient and zero light colors (`PbrEffect.cpp:295`). The EasyGL PBR shader at
`EasyGLGraphicsBackend.cpp:4121` computes:

```text
Lo = light0 + light1 + light2 = 0
ambient = ambientColor * albedo * occlusion = 0
emissive = emissiveFactor * emissiveMap = 0
output = ambient + Lo + emissive = black
```

The four black PBR primitives in the CNJ are:

- backplate;
- carbon band;
- clasp;
- watch face.

This is not evidence of a defective draw call in `easy-gl` or `meta-gl`. The
EasyGL shader renders exactly the zero-valued parameters it receives from the
CNA effect.

The minimum presentation fix belongs in `cna-gltf-viewer`: configure
reasonable default lighting when the imported scene has no lights. However,
three directional lights are insufficient to faithfully match the reference
viewer. The watch's PBR metal also requires image-based lighting, environment
reflections, correct exposure, and tone mapping. That is a broader capability
of CNA `PbrEffect` and its backends.

**Responsibility for the black screenshot: primarily the viewer/light policy,
and partly CNA PBR limitations.**

## 4. The Glass Is Incorrectly Opaque

The `Glass Face` material has:

- `metallicFactor = 0`;
- `roughnessFactor = 0`;
- `KHR_materials_transmission.transmissionFactor = 1`.

CNA does not process `KHR_materials_transmission` anywhere. Because the
material also has neither a normal map nor a metallic-roughness map, the
`usePbr` condition routes it to `BasicEffect`. The result is a white opaque
surface with depth writes that can obscure the dial and hands.

Simply changing alpha to zero is not a correct fix: the glTF specification
explicitly separates alpha coverage (`alphaMode`) from physical transmission.
A temporary alpha-blend approximation could be a useful fallback, but it must
be documented as an approximation. A correct implementation requires a
transmission shader and a transparent render pass with appropriate sorting.

**Responsibility: CNA material import, PBR effect, and rendering orchestration;
the viewer cannot reconstruct the information after conversion.**

## 5. Other Proven Losses and Limitations

### Rigid Hand Animation

The GLB contains `Anim_0`, which animates the rotation of node 11
(`Hand Seconds`). The file has no skin.

`ExtractClips()` maps animation targets only through
`SkeletonResult::nodeToNewIndex` and skips other nodes
(`GltfImportCore.cpp:521`). In addition, the converter calls `ExtractClips()`
only when `hasSkin` is true (`gltf_to_cnj.cpp:391`). The resulting CNJ
therefore contains no animation.

The viewer also declares that it does not currently play animations. Even
after playback is added, this animation would not be available in the CNJ.

### Texture Samplers

The model uses a sampler with `CLAMP_TO_EDGE` for most textures and a trilinear
minification filter. CNA stores only the image, not the glTF sampler. The
default CNA `SamplerStateCollection` sets every slot to `LinearWrap`
(`SamplerStateCollection.cpp:8`).

This is particularly important here because `KHR_texture_transform` uses, for
example, offset `[-0.5, -3.6]` and scale `[2, 8]`. `CLAMP_TO_EDGE` and `WRAP`
produce fundamentally different images for such UV coordinates.

### Multiple UV Sets and Independent Texture Transforms

`PbrEffect` has only one shared UV channel for all maps.

- `Band Carbon Fiber Gold` uses `TEXCOORD_1` for occlusion, while the other
  maps use `TEXCOORD_0`.
- `Clasp DGG` has the same problem.

The `pbrUv2Mismatch` condition is true for both primitives, and the converter
can emit a warning, but it does not correct the data. CNA also bakes only the
base-color texture transform into the shared UV coordinates. An independent
transform of another map using the same `texCoord` is not preserved and is not
fully covered by the current mismatch check.

### Material Variants

The four variants are:

- Surgical White;
- Midnight Gold;
- Commerce Green;
- Khronos Red.

CNA does not import `KHR_materials_variants`, and the viewer provides no way
to select them. This is not the main cause of the current screenshot: the
default `primitive.material` already references the gold materials. It is
nevertheless a loss of model functionality.

### Color Space

glTF requires RGB base-color and emissive textures to be decoded from sRGB to
linear space, while normal, metallic-roughness, and occlusion data are linear.
The EasyGL path creates regular `RGBA8` textures and the PBR shader uses the
samples directly; neither the importer nor the shader performs sRGB decoding.
Brightness and material deviations will therefore remain after the primary
blockers are fixed.

### Culling

The viewer globally sets `RasterizerState::CullNone`
(`ViewerGame.cpp:188`), whereas a glTF material is single-sided by default
unless `doubleSided=true`. This particular model does not set `doubleSided`,
so the viewer may also display back-facing or interior surfaces. This does not
cause the black-and-white materials, but it can degrade the silhouette.

### Camera

The viewer uses its own perspective orbit camera with default yaw `0.7` and
pitch `0.35`, while the reference image on the left is almost front-facing.
Some of the visual difference is therefore only the viewing angle. The camera
does not explain the lost colors, opaque glass, or local bounds of transformed
meshes.

## Assessment of Individual Projects

| Layer | Assessment | Reason |
|---|---|---|
| CNA glTF import / `gltf_to_cnj` | **main source of defects** | discards node transforms, base color, rigid animations, transmission, samplers, and some UV information; selects PBR incorrectly |
| CNA `ContentManager` / `PbrEffect` | **contributing cause** | the loader neither receives nor applies the missing data; PBR without lights is black; transmission, IBL, and a correct color-space path are missing |
| `cna-gltf-viewer` | **presentation-level contributing cause** | no fallback lights, no animation, and always uses `CullNone`; otherwise it merely runs the converter and renders its result |
| CNA EasyGL backend | **not the primary cause of the screenshot** | correctly accepts stride 32/48, binds buffers and textures, and its PBR shader deterministically computes from the provided zero-valued lights |
| `easy-gl` | **no evidence of a defect in this case** | generic GL resource/draw layer; it never sees glTF nodes or materials |
| `meta-gl` | **no evidence of a defect in this case** | low-level typed GL functions; the lost scene and material data never reach it |

A separate defect in a lower-level backend cannot be ruled out in general, but
none is required to explain this screenshot: the defective state is already
directly visible in `scene.cnj` and its vertex sidecars before the first GL
draw call.

## Recommended Repair Order

### P0: Correct Scene and Basic Appearance

1. Replace `MeshGroup::meshes` with an instance structure containing at least
   `cgltf_node*`, `cgltf_mesh*`, and the world transform.
2. Preserve hierarchy/mesh-parent transforms in the CNA `Model` and CNJ.
   Alternatively, as a temporary static path, bake the world transform into
   positions, apply the inverse transpose to normals/tangents, and handle a
   negative determinant correctly.
3. Propagate the complete RGBA `baseColorFactor` through `MeshOut`, CNJ, and
   both loaders.
4. Render every standard metallic-roughness material as PBR even without
   maps; map presence must not be a condition for selecting PBR.
5. Add fallback lighting in the viewer when a model has no lights. Add
   IBL/environment lighting and tone mapping for the target quality.
6. Implement `KHR_materials_transmission`, or at minimum provide an explicit,
   documented transparency fallback.

### P1: Texture and Motion Correctness

7. Preserve the glTF sampler for each texture slot.
8. Support multiple UV channels and per-map `texCoord` and
   `KHR_texture_transform`.
9. Import and play regular node TRS animations, not only skin joints.
10. Propagate `doubleSided`, `alphaMode`, `alphaCutoff`, normal scale, and
    occlusion strength.
11. Distinguish sRGB and linear textures throughout the PBR path.

### P2: Complete Asset Functionality

12. Preserve `KHR_materials_variants` and add variant selection to the viewer.
13. After fixing transforms, compute camera bounds in transformed world space.

## Missing Regression Tests

The minimum new test suite should include:

1. a static mesh on a node with translation, rotation, and scale;
2. one mesh instantiated by two nodes with different transforms;
3. a parent transform plus a child mesh transform;
4. a factor-only gold PBR material without textures;
5. a base-color factor multiplying a base-color texture;
6. a PBR model without its own lights rendered in the viewer;
7. transmission glass in front of a colored object;
8. a `CLAMP_TO_EDGE` texture sampler with UV coordinates outside `[0,1]`;
9. two maps with different UV sets and transforms;
10. an animated rotation of a rigid, unskinned node;
11. switching to every `KHR_materials_variants` variant;
12. an sRGB base-color texture versus a linear metallic-roughness control
    texture.

For the watch itself, the following acceptance criteria are appropriate:

- after import, the backplate has world Z approximately
  `0.0166 .. 0.0810`, not `-1.9022 .. 1.9834`;
- the hour hand has world Z approximately `0.7371 .. 0.7676`;
- gold, black, and metallic-gray factors remain in the loaded effects;
- PBR surfaces are not inexplicably black;
- the dial is visible through the glass;
- the second hand plays `Anim_0`;
- sampler clamping and both UV sets produce the same result as a reference glTF
  viewer.

## Authoritative glTF References

- The [glTF 2.0 specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)
  describes node world transforms, PBR factors, multiplication of factors by
  textures, and the sRGB/linear meaning of individual maps.
- [KHR_materials_transmission](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_transmission/README.md)
  defines physical transmission separately from alpha coverage.
- [KHR_texture_transform](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_texture_transform/README.md)
  defines offset, rotation, scale, and the ability to override `texCoord`.
- [KHR_materials_variants](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_variants/README.md)
  defines variants and their per-primitive material mappings.

## Scope and Confidence

The analysis was performed by inspecting the exact GLB, generated CNJ and
vertex sidecars, screenshot, and current source code of all four layers. No
implementation changes or new build/render were performed.

Confidence in the main conclusion is high: the discarded transforms and
material data are demonstrably absent from the converter output on disk, and
the black-and-white result exactly matches the effects' default parameters.
