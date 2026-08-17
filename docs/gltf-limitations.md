# glTF limitations (`GLTF-447`)

What CNA's glTF import **cannot** carry, what it **approximates**, and where each of those is
*reported* rather than dropped in silence. It is the inverse of `docs/gltf-conventions.md`: that
file records what CNA decided to do, this one records what it cannot do and how you find out.

The campaign's thesis is that a silent drop is a bug even when the render looks fine. So the
organising rule here is not "list the missing features" — it is **every entry names the report
field that says so at run time**. If a row has no report entry, the loss is invisible to a caller,
and that is itself the defect the row should be recording.

## How this file is kept true

Two mechanical checks, because a limitations document that drifts is worse than none — it reads as
a promise.

* **§1's table is generated.** Its source of truth is `GltfExtensionRegistryEXT()` in
  `modules/content/src/GltfImport/GltfImportCore.cpp`, the same registry the `extensionsRequired`
  gate reads. `GltfLimitationsDoc.ExtensionTableAgreesWithTheRegistry` compares this file against
  it row by row and **prints the corrected table on failure** — paste that, do not edit the rows by
  hand. (`plan_gltf.md` §19 carries the same table and the same rule; the two are generated from
  one registry, which is why they cannot disagree.)
* **Every report field named below must exist.**
  `GltfLimitationsDoc.EveryReportFieldNamedHereExistsInTheHeader` reads
  `modules/content/include/CNA/Internal/GltfImport/GltfImportCore.hpp` and fails on any
  `…EXT`/report identifier this file cites that the header no longer declares. A renamed field
  therefore breaks the document that promised it, rather than leaving a dead name behind.

The fine-grained source reports remain internal (`CNA::Internal::GltfImport`) because they carry
cgltf-oriented intermediate state. They now feed the public
`Model::getGltfImportReportEXTProperty()` on both direct `.gltf`/`.glb` loads and Model `.cnj`
documents emitted by `gltf_to_cnj` (`GLTF-034`/`GLTF-035`). Each public diagnostic has a stable
`Code`; its `Message` is display text and may change. The public kinds distinguish exact
information, generated data, invalid source data, approximations, dropped data and unsupported
optional features. Old `.cnj` files and non-glTF models return an empty report.

---

## 1. Extensions

`extensionsUsed` is advisory; **`extensionsRequired` is normative**. A file that *requires* an
extension CNA does not claim is refused by name (`GLTF-333`) rather than imported wrongly.

**Classification** is how completely CNA implements an extension. **Claimed** is whether a file
listing it in `extensionsRequired` is accepted. They are deliberately separate axes: transmission
is approximated and *not* claimed, because a file requiring refraction is asking for something the
approximation cannot deliver; punctual lights are approximated and *are* claimed, because refusing
every lit file would be far worse than the approximation.

| Extension | Classification | Claimed | Evidence / note | Task |
|---|---|---|---|---|
| `KHR_texture_transform` | **IMPLEMENTED_AND_TESTED** | yes | All five PBR maps retain independent offset/rotation/scale and texCoord state through direct and offline loading. Every PBR renderer applies the resulting affine rows, and a discriminating EasyGL L7 fixture proves two maps sharing one authored UV stream can use different transforms. | `GLTF-336` |
| `KHR_mesh_quantization` | **IMPLEMENTED_AND_TESTED** | yes | Integer mesh attributes are decoded through the same normalized-accessor path as core formats and repacked into CNA's float vertex layouts. BYTE and SHORT normal witnesses pin the signed clamp and the extension's 4-byte VEC3 element alignment. | `GLTF-084` |
| `KHR_materials_emissive_strength` | **IMPLEMENTED_WITH_A_NAMED_LIMIT** | yes | Applied on the PBR path. A non-PBR material has no emissive term to scale, so the strength has nowhere to go there. | `GLTF-222` |
| `KHR_lights_punctual` | **APPROXIMATED_AND_REPORTED** | yes | Up to three directional lights, which is XNA's whole lighting model. Point and spot become directional lights aimed at the origin, ranges and cones are ignored, and an out-of-gamut intensity clamps -- every loss counted. | `GLTF-325` |
| `KHR_draco_mesh_compression` | **IMPLEMENTED_AND_TESTED** | build-dependent | Decoded when the build has libdraco. Claimed only in such a build: claiming it without the decoder would accept a file whose geometry then arrives empty. | `GLTF-353` |
| `KHR_materials_transmission` | **APPROXIMATED_AND_REPORTED** | no | alpha = 1 - transmissionFactor, multiplied into the material's own alpha. Not physical in four named ways, so a file that REQUIRES transmission is refused rather than loaded with its glass drawn as tinted alpha. | `GLTF-339` |
| `KHR_texture_basisu` | **UNSUPPORTED** | no | No KTX2 decoder. A texture's plain PNG/JPEG fallback is used when the file provides one, and the loss is named per map when it does not. | `GLTF-350` |
| `EXT_texture_webp` | **UNSUPPORTED** | no | No WebP decoder; same three outcomes as KHR_texture_basisu. | `GLTF-350` |
| `KHR_materials_unlit` | **IMPLEMENTED_WITH_A_NAMED_LIMIT** | yes | Maps to LightingEnabled = false on BasicEffect, with baseColorFactor as the diffuse colour. SkinnedEffect has no such flag -- real XNA's has none either -- so a skinned unlit material is approximated with an all-white ambient and no directional light, which is unlit apart from any specular term. | `GLTF-337` |
| `KHR_materials_pbrSpecularGlossiness` | **APPROXIMATED_AND_REPORTED** | no | Archived by Khronos but present in older assets, so converted rather than refused: diffuse becomes the base colour, metallic 0, roughness 1 - glossiness. Not claimed, because specularFactor -- a coloured specular reflection -- has no metallic-roughness equivalent, so a file REQUIRING the extension is asking for something the conversion cannot deliver. | `GLTF-349` |
| `KHR_materials_variants` | **IMPLEMENTED_AND_TESTED** | yes | The source-order variant table and sparse primitive mappings are preserved. Model's CNAEXT selection API swaps the complete material-dependent part state, including effect, vertex layout, textures and samplers, on both direct glTF and offline .cnj paths while leaving the default mapping unchanged. | `GLTF-341` |
| `KHR_materials_ior` | **IMPLEMENTED_AND_TESTED** | yes | IOR is converted to dielectric F0/F90 and consumed by rigid and skinned PBR shaders on all 15 PBR renderers. Analytic factor-only and grazing pixel witnesses cover the core default and authored endpoints. | `GLTF-343` |
| `KHR_materials_specular` | **IMPLEMENTED_WITH_A_NAMED_LIMIT** | no | Factor and colour are consumed by all 15 PBR renderers. Both optional texture inputs survive direct import and offline `.cnj`, including independent UV, transform, sampler and colour-space state; EasyGL, OpenGL2, OpenGL4, DirectX9/11/12, Bgfx, Diligent, Magnum, SDL GPU and Vulkan sample them, while 4 PBR renderer bindings remain pending. Required use therefore remains refused and optional use is warned by name. | `GLTF-344` |
| `KHR_materials_clearcoat` | **PARSED_BUT_IGNORED** | no | A second specular lobe -- a large shader change. | `GLTF-345` |
| `KHR_materials_sheen` | **PARSED_BUT_IGNORED** | no | A third BRDF lobe, same shape of change as clearcoat. | `GLTF-346` |
| `KHR_materials_volume` | **PARSED_BUT_IGNORED** | no | Meaningless without a real transmission pass, which CNA does not have. | `GLTF-347` |
| `EXT_meshopt_compression` | **UNSUPPORTED** | no | cgltf validates the compression metadata but decoding needs a caller-supplied hook CNA does not provide, and without one an accessor over a compressed view reads undefined bytes rather than failing. Refused at validation instead. | `GLTF-351` |
| `EXT_mesh_gpu_instancing` | **UNSUPPORTED** | no | Each node's own single placement is imported and the per-instance transforms are not, so the file renders one copy where it describes many. Reported per file. | `GLTF-352` |
| `KHR_materials_iridescence` | **NOT_DESIRED** | no | A thin-film interference term with no counterpart in any CNA stock effect. Not planned: the shader cost falls on every PBR material to serve a rare one. | `GLTF-348` |
| `KHR_materials_anisotropy` | **NOT_DESIRED** | no | Needs a tangent-aligned specular lobe, and therefore a reliable tangent basis on every affected primitive -- which GLTF-086 shows CNA cannot carry at most strides. | `GLTF-348` |
| `KHR_materials_dispersion` | **NOT_DESIRED** | no | Wavelength-dependent refraction, which presupposes the refraction pass KHR_materials_transmission is explicitly approximated instead of implementing. | `GLTF-348` |

An extension **not in this table at all** is one cgltf does not parse and CNA has never seen. Such
a name in `extensionsRequired` is refused for that reason; in `extensionsUsed` it produces a
warning. Adding one requires a registry record *and* a corpus fixture — `GLTF-335`, enforced by
`GltfExtensionRegistry.EveryExtensionTheCorpusDeclaresIsClassified` and its converse.

---

## 2. Approximations in core glTF

Not extensions — parts of glTF 2.0 itself that CNA converts into something XNA can express. Each
one is a deliberate decision recorded in `docs/gltf-conventions.md`; what this table adds is **the
field that tells a caller it happened**.

| What the file asked for | What CNA does | Report field | Task |
|---|---|---|---|
| A primitive with no `NORMAL` | Computes §3.7.2.1's flat normals: a real geometric normal per face, and a vertex shared between differently oriented faces is **duplicated** once per orientation so each face carries its own. Faces whose unit normals agree to within ~0.081° share one copy and get their area-weighted sum, which is a reproducibility floor rather than a smoothing threshold — topology decided by float noise would make the generated corpus non-reproducible — and the vertices where that tolerance actually merged non-identical faces are counted separately. An authored `TANGENT` is ignored, as the same sentence requires, and a basis is generated from the computed normals. | `generatedNormalsEXT`, `flatNormalDuplicatedVertexCountEXT`, `flatNormalMergedVertexCountEXT`, `ignoredTangentForGeneratedNormalsEXT` | `GLTF-173`, `GLTF-461` |
| A primitive with no `NORMAL` **and** morph targets | §3.7.2.2 requires flat normals for each morph target, and a `POSITION` delta can rotate a face — so the normals are a function of the weights and cannot be baked. Every corner is split into its own vertex at import (a rest-pose split cannot serve every reachable pose) and `BlendMorphTargetsEXT` recomputes the face normals from the morphed positions on every weight change. Tangents are the one approximation left: §3.7.2.2's SHOULD is a full MikkTSpace regeneration per target, and CNA instead re-orthogonalises the generated basis against the recomputed normal, which preserves the property tangent-space normal mapping depends on without re-solving the UV gradients. A `NORMAL` delta on such a target is not legal (§3.7.2.2 requires an original attribute) and is dropped rather than blended. | `morphedFlatNormalsEXT`, `ignoredMorphNormalDeltasForGeneratedNormalsEXT`, `ignoredTangentForGeneratedNormalsEXT` | `GLTF-461` |
| `COLOR_0` on a **skinned** metallic-roughness primitive | The rigid case is carried in full — stride 60's four reserved discriminator bytes are the packed colour, so Position, Normal, Tangent, two UV sets and `COLOR_0` all fit and the colour multiplies base colour. The skinned record (stride 76) is exactly its seven fields and has no bytes to reuse, so a skinned vertex-coloured primitive keeps `SkinnedEffect`: its `NORMAL` and its colours arrive, and the material's metallic-roughness factors and maps are not applied. Named, not silent. | `unsupportedMaterialModelEXT` | `GLTF-241`, `GLTF-462`, `GLTF-463` |
| `TRIANGLE_STRIP` / `TRIANGLE_FAN` / `LINE_LOOP` | Converted to the equivalent list at import, so no renderer needs the topology. The conversion is exact — the same triangles, in the same winding — and the source mode is still carried, so the conversion is checkable rather than assumed. | *(exact; reported at debug severity)* | `GLTF-081` |
| An index run that does not complete a primitive | The trailing remainder is dropped and counted. §3.7.2.1 requires a whole number of primitives; `cgltf_validate` does not check it, and neither reading nor refusing the remainder is safe by default. | `droppedIncompleteIndicesEXT` | `GLTF-079` |
| Joint weights that do not sum to 1 | Renormalised, with the worst deviation recorded so a quantised exporter (a few 1e-3) is distinguishable from a broken file. An all-zero weight set is left alone — `0/0` is not a normalisation — and counted separately. | `renormalisedWeightVertexCountEXT`, `worstWeightSumDeviationEXT`, `zeroWeightVertexCountEXT` | `GLTF-256` |
| More than four joint influences per vertex | Truncated to four, which is what `BlendIndices`/`BlendWeight` carry. The count alone does not say whether it matters, so the largest discarded share of a vertex's influence is recorded too. | `extraInfluenceSetsEXT`, `worstDroppedInfluenceEXT` | `GLTF-095`, `GLTF-257` |
| More than three punctual lights (`KHR_lights_punctual`) | The first three become XNA's three directional lights; the rest are dropped and counted. Point and spot lights become directional lights aimed at the origin; ranges and cone angles are ignored; an out-of-gamut intensity is clamped, with the worst pre-clamp channel recorded. | `droppedLightCount`, `approximatedPointLightCount`, `approximatedSpotLightCount`, `ignoredRangeCount`, `ignoredConeAngleCount`, `clampedIntensityLightCount`, `worstPreClampChannelEXT` | `GLTF-325`, `GLTF-326` |
| An animation channel on a path CNA does not import, or on a node outside the scene | Skipped and counted, per animation, rather than silently producing a clip that plays less than the file describes. | `skippedUnsupportedPathChannels`, `skippedOutOfSceneChannels` | `GLTF-310`, `GLTF-313` |
| A sampler whose input times need resampling to a shared track | Resampled, with the track count and the duplicate-input-time count recorded. A CUBICSPLINE is exact at the baked union keys but piecewise-linear/spherical between them: the zero-tangent `anim-cubicspline` error is 0.9375 units (9.375% of its value span) at `t=.25` and at most 0.962250449 units (9.622504%) on that fixture. This is not a universal bound because authored tangents may overshoot the endpoint span. Equal adjacent times are kept (an exporter writes them for a hard cut); a **decreasing** time is refused outright, because it has two authored values for one time. | `resampledTrackCount`, `duplicateInputTimeCount` | `GLTF-297`, `GLTF-310`, `GLTF-313` |
| A mirrored node transform | The composed world 3×3 has a negative determinant, so §3.7.4 requires the winding to be reversed for front faces to stay front-facing. CNA reverses it and marks the placement, rather than leaving the model inside-out. | `mirroredEXT` | `GLTF-116`, `GLTF-117` |
| A material sampling a third distinct authored `TEXCOORD` set | The first two distinct sampled sets are packed as channels 0/1 and selected independently per map. A map needing a third set falls back to packed channel 0 and is listed by name. | `uvSetMismatchedMapsEXT` | `GLTF-182`, `GLTF-188` |
| A sampled PNG/JPEG map with an explicit `*_MIPMAP_*` minFilter | Imported with one texture level; every LOD samples level zero. Generic generation is deferred because colour, normal and packed-data maps require different downsampling rules. | `mipmappedSamplerMapsWithoutMipChainEXT` | `GLTF-206` |
| A morph target carrying `TEXCOORD_n` or `COLOR_n` deltas | Not morphed. §3.7.2.2 asks a client to support `POSITION`, `NORMAL` and `TANGENT` and makes morphed texture coordinates and vertex colours a **MAY**; CNA carries the three required semantics only. The scope cut is permitted — importing as though the file had asked for nothing was not, which is what this field fixes. | `ignoredMorphAttributesEXT` | `GLTF-466` |
| `KHR_materials_transmission` above 0 | `alpha = 1 - transmissionFactor`, multiplied into the material's own alpha, with `alphaMode` forced to `Blend`. Explicitly not physical: no refraction, no roughness-driven blur, no thickness, and no view-dependent Fresnel. The application draws opaque geometry first, then sorts this straight-alpha approximation back-to-front with `BlendState::NonPremultiplied`; `EasyGL_Gltf_TransmissionOrdering` proves the opposite order hides the surface behind the nearer glass depth. | `transmissionApproximatedEXT`, `transmissionFactorEXT`, `transmissionHasTextureEXT` | `GLTF-339`, `GLTF-340` |
| `KHR_materials_pbrSpecularGlossiness` | Converted to metallic-roughness: diffuse to base colour, metallic 0, roughness `1 - glossiness`. The discarded term is the coloured specular, and its largest channel is recorded so a near-dielectric conversion is distinguishable from a lossy one. | `convertedFromSpecularGlossinessEXT`, `droppedSpecularStrengthEXT` | `GLTF-349` |
| A skinned `KHR_materials_unlit` material | `SkinnedEffect` has no `LightingEnabled = false` — real XNA's has none either — so the surface is approximated with an all-white ambient and no directional light, which is unlit apart from any specular term. | `unlitEXT` | `GLTF-337` |

---

## 3. Data CNA cannot carry at all

These are not approximations. The data is decoded correctly and then has nowhere to go, because
XNA's vertex layouts and stock effects have no slot for it. Every one is **counted or named**,
which is what distinguishes a documented limitation from a silent drop.

| Data | Why it cannot be carried | Report field | Task |
|---|---|---|---|
| `COLOR_1` and beyond | XNA's vertex layouts carry exactly one colour channel. | `extraColorSetsEXT` | `GLTF-091` |
| `_*` custom attributes | §3.7.2.1 reserves the underscore prefix so a reader *may* ignore them, and ignoring one is not an error — but a file whose geometry depends on `_BATCHID` deserves to be told. | `ignoredCustomAttributesEXT` | `GLTF-092` |
| An authored `TANGENT` on a non-PBR layout | Only strides 48/60 and 68/76 carry a tangent, and those are the PBR layouts. | `droppedTangentForStrideEXT` | `GLTF-086` |
| An authored `NORMAL` on a coloured or dual-texture layout | Strides 20 and 24 have no Normal slot, so such a primitive cannot be lit at all. | `droppedNormalForStrideEXT` | `GLTF-241` |
| A metallic-roughness material on a primitive with `COLOR_0` | No CNA vertex layout carries a colour alongside a tangent and no PBR shader reads a colour stream. The primitive imports through `BasicEffect` with its vertex colours intact and **without** its material. | `unsupportedMaterialModelEXT`, `unrepresentableForStrideEXT` | `GLTF-241` |
| A texture whose image is KTX2/Basis or WebP, with no fallback `source` | No decoder. When the file provides a plain PNG/JPEG fallback it is used instead and nothing is lost. | `unsupportedTextureSourcesEXT` | `GLTF-200`, `GLTF-350` |
| A third distinct sampled UV set (`TEXCOORD_n`) | PBR layouts carry two packed UV channels. A material sampling three distinct authored sets cannot represent the third; see §2's `uvSetMismatchedMapsEXT` row. | `uvSetMismatchedMapsEXT` | `GLTF-188` |
| Per-instance transforms of `EXT_mesh_gpu_instancing` | The node's own single placement is imported; the file renders one copy where it describes many. | `gpuInstancedNodeCount` | `GLTF-352` |

The **general** statement of this table is `unrepresentableForStrideEXT`: it comes from the
`GLTF-099` layout decision table's own row rather than being re-derived per caller, so every
downgrade CNA performs names itself in one place. `CNA/Internal/Graphics/VertexDeclarationFidelity.hpp`
(`InferredLayoutForStride`) is the query side of the same table — never hardcode a stride's offsets.

`KHR_materials_specular`'s optional `specularTexture` and `specularColorTexture` now have dedicated
PBR-effect and `GpuDrawParams` slots. Direct and offline import retain each map's independent UV,
transform and sampler; the colour map also retains its sRGB declaration. EasyGL, OpenGL2,
OpenGL4, DirectX9/11/12, Bgfx, Diligent, Magnum, SDL GPU and Vulkan apply the extension's multiply-before-clamp
Fresnel equation.
The modern DirectX pair additionally uses white t5/t6 fallbacks and its stride-60/76 dual-UV
variants, with every HLSL variant regenerated through Microsoft's D3D compiler.
SDL GPU regenerates a seven-sampler SPIR-V fragment and keeps each imported sampler state distinct;
its rigid, skinned and analytic Fresnel pixel witnesses pass under Xvfb. Vulkan carries seven
independent image/sampler pairs through rigid and skinned descriptor sets, including dual-UV
selectors and separate extension transforms; its 22-check texture program and its rigid/skinned
golden and Fresnel programs pass on validation-clean lavapipe under Xvfb.
Diligent carries the same seven named resources, independent sampler slots, four extension-transform
rows and seven-bit UV selector through a 76-float constant block; its rigid/skinned stride-48/60/68/76
programs pass 21/21 material-map, 22/22 texture-slot, 7/7 analytic Fresnel and 12/12 sRGB pixels on
both Vulkan and OpenGL under Xvfb. Bgfx binds identity-white stages 5/6, carries independent
extension transforms plus the seven-bit UV selector through rigid/skinned stride-48/60/68/76
programs, and compiles all four shader dialects. Its OpenGL path passes 21/21 material-map, 22/22
texture-slot, 7/7 analytic Fresnel and 12/12 sRGB pixels under Xvfb. Validation continues to name
the extension until the remaining four PBR renderers consume the same state (`GLTF-344`).

---

## 4. Carried by CNA, applied by the application

State that survives import intact and reaches the effect, while CNA deliberately leaves the final
per-draw device choice to the application. This is narrower than "unsupported": the data and the
consumer path are both proven, but `Model::Draw` does not silently mutate global state or invent a
transparent sort order.

| State | Where it lives | Required application action | Task |
|---|---|---|---|
| `alphaMode: BLEND` | `PbrEffect::getAlphaModeEXTProperty()` | Select `BlendState::NonPremultiplied` and issue transparent parts back-to-front. CNA does not sort by default. `EasyGL_Gltf_AlphaBlend` proves the real imported material against Opaque and premultiplied controls on OPENGLES2/3. | `GLTF-230` |
| `doubleSided` | `PbrEffect::getDoubleSidedEXTProperty()` | Select `RasterizerState::CullNone`; otherwise keep the glTF front-face state (reversed for mirrored placement). The same test proves a back-facing imported triangle is culled by the single-sided control and visible under the property-derived state on OPENGLES2/3. | `GLTF-231`, `GLTF-230` |

`alphaMode: MASK` **used** to be in this table and is not any more: the cutoff is fragment-program
work rather than device state. `GLTF-372` wired it into `GpuDrawParams::alphaTest`; the subsequent
cross-renderer audit in `GLTF-379` found that eight PBR implementations did not consume that vector
and five renderers instead selected their non-PBR alpha-test program. Both failures are fixed: a
PBR `MASK` draw stays PBR and every PBR fragment path evaluates the same coverage expression.
`docs/gltf-api-change-review.md` §1.3 records where the line falls and why the other two stay on this
side of it; `docs/gltf-renderer-pbr-fallbacks.md` records the renderer matrix and evidence.

---

## 4.1 `EXT_mesh_gpu_instancing` — the design, and why it is not built (`GLTF-352`)

The one unsupported extension with an obvious implementation, so the sketch is worth writing down
rather than rediscovering.

**What the file says.** A node carries `EXT_mesh_gpu_instancing` with `TRANSLATION`, `ROTATION` and
`SCALE` accessors of equal length; the node's mesh is drawn once per element, each with its own
TRS composed *after* the node's own transform.

**What CNA already has.** `GraphicsDevice::DrawInstancedPrimitives` is real XNA 4.0 API and is
implemented on every GPU renderer; `GpuDrawParams::instanceCount` and a per-instance
`VertexBuffer` stream already exist (`plan_cnj.md` §3.3). So the GPU half is not the work.

**The sketch.**

1. `ExtractSceneMeshesEXT` reads the three accessors and emits **one placement carrying N
   instance transforms**, instead of the one placement it emits today. `NodeGraphReportEXT`
   already counts such nodes (`gpuInstancedNodeCount`), which is what makes the current loss
   visible.
2. The transforms become a per-instance vertex buffer (four `Vector4` rows of a 4×3 affine, the
   layout `DrawInstancedPrimitives` already streams) built once at import, not per frame.
3. `ModelMeshPart` grows a CNAEXT instance-buffer property, and `ModelMesh::Draw` calls
   `DrawInstancedPrimitives` when it is present. That is the API-change-review row, and it is the
   real cost: a new public member plus a branch in the draw path every XNA application shares.
4. The fallback matters as much as the path: a renderer without instancing must draw N times or
   refuse **by name** — never draw once and look nearly right, which is exactly today's failure.

**Why it is not built.** It is `GLTF ROBUST` scope, and every layer that could *verify* it is L7:
the numbers at L3–L5 are identical whether the instance transforms arrive or not, because they
describe one mesh either way. Building an instancing path this environment cannot render is how
`GLTF-157`'s lesson repeats. Until then the loss is counted per file and named here, which is the
difference between a limitation and a bug.

---

## 5. Environment-dependent

| Capability | Condition | Behaviour when absent |
|---|---|---|
| `KHR_draco_mesh_compression` | `libdraco` present at configure time (`CNA_DRACO_AVAILABLE`) | The extension is **not claimed**, so a file requiring it is refused by name, and a Draco-compressed primitive throws at import rather than producing empty geometry. |
| L7 image comparison | A renderer with a real 3D pipeline | The `STUB` renderer reports `GraphicsCapability::ThreeD == false` and the tests that need pixels skip rather than pass vacuously (`GLTF-009`). |

Regardless of decoder availability, the Draco extension's primitive-mode restriction is enforced
before decoding: only `TRIANGLES` and `TRIANGLE_STRIP` are accepted. `POINTS`, all three line modes
and `TRIANGLE_FAN` are rejected with the allowed modes named; decoded strip connectivity is taken
from Draco's explicit face list and is not reinterpreted as a second strip (`GLTF-080`, `GLTF-362`).

---

## What this file is not

It is not a roadmap. A row here says what happens today and where the loss is reported; whether it
*should* change is the owning task's question, and several deliberately answer "no" —
`KHR_materials_iridescence` is `NOT_DESIRED` rather than pending, and `doubleSided` being carried
rather than applied is a scope decision recorded in the API-change review, not an omission.

See also: `docs/gltf-conventions.md` (every decision with its rationale),
`docs/gltf-conformance.md` (the oracle ladder and the corpus), `docs/gltf-api-change-review.md`
(what may become public API), and `plan_gltf.md` (the 460-row campaign record).
