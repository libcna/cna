# glTF limitations (`GLTF-447`)

What CNA's glTF import **cannot** carry, what it **approximates**, and where each of those is
*reported* rather than dropped in silence. It is the inverse of `docs/gltf-conventions.md`: that
file records what CNA decided to do, this one records what it cannot do and how you find out.

The policy is to report semantic loss even when the image looks plausible. The tables identify
known counters, named attributes and validation/report routes. A declaration or source-table
entry alone does not prove complete reporting on every path; gaps must be recorded as such.

## How this file is kept true

Current source/registry/report contracts were checked on 2026-11-01. Three tests help keep
selected parts synchronized:

- ExtensionTableAgreesWithTheRegistry checks extension names, classification, claimed state and
  task attribution against GltfExtensionRegistryEXT. It prints a replacement table on failure,
  but does not compare the evidence/note prose or regenerate this file automatically.
- EveryReportFieldNamedHereExistsInTheHeader checks identifiers in the report columns of §2/§3
  against declarations in GltfImportCore.hpp. It does not prove every execution path emits them.
- EveryApproximatedOrUnsupportedExtensionIsInTheApproximationTables checks that each approximated
  extension is mentioned beyond its registry row. Its name is broader than its current predicate;
  it is not a complete audit of every unsupported extension.

The report fields below describe internal accounting routes. Source/runtime tests are still
needed to establish their meaning and coverage; declaration existence alone is insufficient.

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
| `KHR_materials_unlit` | **IMPLEMENTED_WITH_A_NAMED_LIMIT** | yes | BasicEffect disables lighting and uses baseColorFactor. SkinnedEffect cannot disable its lighting shader; CNA uses white ambient and parked, disabled directional slots. A fallback occlusion map on an unlit material is ignored (see below). | `GLTF-337` |
| `KHR_materials_pbrSpecularGlossiness` | **APPROXIMATED_AND_REPORTED** | no | Archived by Khronos but present in older assets, so converted rather than refused: diffuse becomes the base colour, metallic 0, roughness 1 - glossiness. Not claimed, because specularFactor -- a coloured specular reflection -- has no metallic-roughness equivalent, so a file REQUIRING the extension is asking for something the conversion cannot deliver. | `GLTF-349` |
| `KHR_materials_variants` | **IMPLEMENTED_AND_TESTED** | yes | The source-order variant table and sparse primitive mappings are preserved. Model's CNAEXT selection API swaps the complete material-dependent part state, including effect, vertex layout, textures and samplers, on both direct glTF and offline .cnj paths while leaving the default mapping unchanged. | `GLTF-341` |
| `KHR_materials_ior` | **IMPLEMENTED_AND_TESTED** | yes | IOR is converted to dielectric F0/F90 and consumed by rigid and skinned PBR shaders on every PBR renderer. Analytic factor-only and grazing pixel witnesses cover the core default and authored endpoints. | `GLTF-343` |
| `KHR_materials_specular` | **IMPLEMENTED_WITH_A_NAMED_LIMIT** | no | Factor and colour are consumed by every PBR renderer. Both optional texture inputs survive direct import and offline `.cnj`, including independent UV, transform, sampler and colour-space state, and every PBR renderer samples both. Required use remains refused and optional use is warned by name. | `GLTF-344` |
| `KHR_materials_clearcoat` | **PARSED_BUT_IGNORED** | no | A second specular lobe -- a large shader change. | `GLTF-345` |
| `KHR_materials_sheen` | **PARSED_BUT_IGNORED** | no | A third BRDF lobe, same shape of change as clearcoat. | `GLTF-346` |
| `KHR_materials_volume` | **PARSED_BUT_IGNORED** | no | Meaningless without a real transmission pass, which CNA does not have. | `GLTF-347` |
| `EXT_meshopt_compression` | **UNSUPPORTED** | no | cgltf validates the compression metadata but decoding needs a caller-supplied hook CNA does not provide, and without one an accessor over a compressed view reads undefined bytes rather than failing. Refused at validation instead. | `GLTF-351` |
| `EXT_mesh_gpu_instancing` | **UNSUPPORTED** | no | Each node's own single placement is imported and the per-instance transforms are not, so the file renders one copy where it describes many. Reported per file. | `GLTF-352` |
| `KHR_materials_iridescence` | **NOT_DESIRED** | no | A thin-film interference term with no counterpart in any CNA stock effect. Not planned: the shader cost falls on every PBR material to serve a rare one. | `GLTF-348` |
| `KHR_materials_anisotropy` | **NOT_DESIRED** | no | Needs a tangent-aligned specular lobe, and therefore a reliable tangent basis on every affected primitive -- which GLTF-086 shows CNA cannot carry at most strides. | `GLTF-348` |
| `KHR_materials_dispersion` | **NOT_DESIRED** | no | Wavelength-dependent refraction, which presupposes the refraction pass KHR_materials_transmission is explicitly approximated instead of implementing. | `GLTF-348` |

An extension absent from this registry is unclaimed by CNA, even if cgltf happens to recognize
its metadata. Required use is refused by name; optional use is warned. Parsing metadata alone
is not an implementation claim. Adding one requires a registry record *and* a corpus fixture — `GLTF-335`, enforced by
`GltfExtensionRegistry.EveryExtensionTheCorpusDeclaresIsClassified` and its converse.

---

## 2. Approximations, generated data and fallback behavior

This table covers core conversion/generation as well as selected extension approximations.
It names the internal report route for each; exact topology conversion is distinct from a loss.
See [conventions](gltf-conventions.md) for transform and generation policy.

| What the file asked for | What CNA does | Report field | Task |
|---|---|---|---|
| A primitive with no `NORMAL` | Computes §3.7.2.1's flat normals: a real geometric normal per face, and a vertex shared between differently oriented faces is **duplicated** once per orientation so each face carries its own. Faces whose unit normals agree to within ~0.081° share one copy and get their area-weighted sum, which is a reproducibility floor rather than a smoothing threshold — topology decided by float noise would make the generated corpus non-reproducible — and the vertices where that tolerance actually merged non-identical faces are counted separately. An authored `TANGENT` is ignored, as the same sentence requires, and a basis is generated from the computed normals. | `generatedNormalsEXT`, `flatNormalDuplicatedVertexCountEXT`, `flatNormalMergedVertexCountEXT`, `ignoredTangentForGeneratedNormalsEXT` | `GLTF-173`, `GLTF-461` |
| A primitive with no `NORMAL` **and** morph targets | §3.7.2.2 requires flat normals for each morph target, and a `POSITION` delta can rotate a face — so the normals are a function of the weights and cannot be baked. Every corner is split into its own vertex at import (a rest-pose split cannot serve every reachable pose) and `BlendMorphTargetsEXT` recomputes the face normals from the morphed positions on every weight change. Tangents are the one approximation left: §3.7.2.2's SHOULD is a full MikkTSpace regeneration per target, and CNA instead re-orthogonalises the generated basis against the recomputed normal, which preserves the property tangent-space normal mapping depends on without re-solving the UV gradients. A `NORMAL` delta on such a target is not legal (§3.7.2.2 requires an original attribute) and is dropped rather than blended. | `morphedFlatNormalsEXT`, `ignoredMorphNormalDeltasForGeneratedNormalsEXT`, `ignoredTangentForGeneratedNormalsEXT` | `GLTF-461` |
| `TRIANGLE_STRIP` / `TRIANGLE_FAN` / `LINE_LOOP` | Triangle strip/fan become triangle lists with winding preserved; LINE_LOOP becomes a line strip with its closing segment appended. Source topology is retained. These are exact topology conversions, not a promise that every renderer draws every point/line mode. | *(exact; reported at debug severity)* | `GLTF-081` |
| An index run that does not complete a primitive | The trailing remainder is dropped and counted. §3.7.2.1 requires a whole number of primitives; `cgltf_validate` does not check it, and neither reading nor refusing the remainder is safe by default. | `droppedIncompleteIndicesEXT` | `GLTF-079` |
| Joint weights that do not sum to 1 | Renormalised, with the worst deviation recorded so a quantised exporter (a few 1e-3) is distinguishable from a broken file. An all-zero weight set is left alone — `0/0` is not a normalisation — and counted separately. | `renormalisedWeightVertexCountEXT`, `worstWeightSumDeviationEXT`, `zeroWeightVertexCountEXT` | `GLTF-256` |
| More than four joint influences per vertex | Truncated to four, which is what `BlendIndices`/`BlendWeight` carry. The count alone does not say whether it matters, so the largest discarded share of a vertex's influence is recorded too. | `extraInfluenceSetsEXT`, `worstDroppedInfluenceEXT` | `GLTF-095`, `GLTF-257` |
| More than three punctual lights (`KHR_lights_punctual`) | The first three become XNA's three directional lights; the rest are dropped and counted. Point and spot lights become directional lights aimed at the origin; ranges and cone angles are ignored; an out-of-gamut intensity is clamped, with the worst pre-clamp channel recorded. | `droppedLightCount`, `approximatedPointLightCount`, `approximatedSpotLightCount`, `ignoredRangeCount`, `ignoredConeAngleCount`, `clampedIntensityLightCount`, `worstPreClampChannelEXT` | `GLTF-325`, `GLTF-326` |
| An animation channel on a path CNA does not import, or on a node outside the scene | Skipped and counted, per animation, rather than silently producing a clip that plays less than the file describes. | `skippedUnsupportedPathChannels`, `skippedOutOfSceneChannels` | `GLTF-310`, `GLTF-313` |
| A sampler whose input times need resampling to a shared track | Resampled, with the track count and the duplicate-input-time count recorded. Playback is linear between keys, so a STEP channel gets a hold key before each step and a CUBICSPLINE interval is sampled at least every 1/60 s (CNA-DEMOS-ISSUE-031); `anim-cubicspline` then plays within 0.0021 units of its spline instead of 0.962 off. This is a bound for that fixture, not a universal one, because authored tangents may bend a spline more sharply. Equal adjacent times are kept (an exporter writes them for a hard cut); a **decreasing** time is refused outright, because it has two authored values for one time. | `resampledTrackCount`, `duplicateInputTimeCount` | `GLTF-297`, `GLTF-310`, `GLTF-313` |
| A mirrored node transform | The composed world 3×3 has a negative determinant, so §3.7.4 requires the winding to be reversed for front faces to stay front-facing. CNA marks the placement but leaves shared vertex/index winding unchanged. The application reverses its cull choice per placement; the public report names mirrored-winding-unapplied. | `mirroredEXT` | `GLTF-116`, `GLTF-117` |
| A material sampling a third distinct authored `TEXCOORD` set | The first two distinct sampled sets are packed as channels 0/1 and selected independently per map. A map needing a third set falls back to packed channel 0 and is listed by name. | `uvSetMismatchedMapsEXT` | `GLTF-182`, `GLTF-188` |
| A sampled PNG/JPEG map with an explicit `*_MIPMAP_*` minFilter | Imported with one texture level; every LOD samples level zero. Generic generation is deferred because colour, normal and packed-data maps require different downsampling rules. | `mipmappedSamplerMapsWithoutMipChainEXT` | `GLTF-206` |
| A morph target carrying `TEXCOORD_n` or `COLOR_n` deltas | Not morphed. §3.7.2.2 asks a client to support `POSITION`, `NORMAL` and `TANGENT` and makes morphed texture coordinates and vertex colours a **MAY**; CNA carries the three required semantics only. The scope cut is permitted — importing as though the file had asked for nothing was not, which is what this field fixes. | `ignoredMorphAttributesEXT` | `GLTF-466` |
| `KHR_materials_transmission` above 0 | `alpha = 1 - transmissionFactor`, multiplied into the material's own alpha, with `alphaMode` forced to `Blend`. Explicitly not physical: no refraction, no roughness-driven blur, no thickness, and no view-dependent Fresnel. The application draws opaque geometry first, then sorts this straight-alpha approximation back-to-front with `BlendState::NonPremultiplied`; `EasyGL_Gltf_TransmissionOrdering` proves the opposite order hides the surface behind the nearer glass depth. | `transmissionApproximatedEXT`, `transmissionFactorEXT`, `transmissionHasTextureEXT` | `GLTF-339`, `GLTF-340` |
| `KHR_materials_pbrSpecularGlossiness` | Converted to metallic-roughness: diffuse to base colour, metallic 0, roughness `1 - glossiness`. The discarded term is the coloured specular, and its largest channel is recorded so a near-dielectric conversion is distinguishable from a lossy one. | `convertedFromSpecularGlossinessEXT`, `droppedSpecularStrengthEXT` | `GLTF-349` |
| A skinned `KHR_materials_unlit` material | `SkinnedEffect` refuses `LightingEnabled = false`; CNA instead configures white ambient and disables/parks every directional slot, including zero specular colour. This is the current shader configuration, not an all-renderer exact-unlit qualification. | `unlitEXT` | `GLTF-337` |
| An unskinned, uncoloured unlit material with base-colour and occlusion images | The occlusion map is a lighting-model input that KHR_materials_unlit excludes from the base-colour term, so it is ignored: the material stays `BasicEffect` with lighting off on stride 32 and does not select `DualTextureEffect` or add a second UV stream (CNA-FIX-D07). `GltfUnlitMaterial.AnOcclusionMapDoesNotChangeTheUnlitEffectOrVertexLayout` pins it; metallic-roughness materials keep their real PBR occlusion map. | `unlitEXT` | `GLTF-337` |

---

## 3. Data CNA cannot carry at all

These are not approximations. The data is decoded correctly and then has nowhere to go, because
XNA's vertex layouts and stock effects have no slot for it. Every one is **counted or named**,
which is what distinguishes a documented limitation from a silent drop.

| Data | Why it cannot be carried | Report field | Task |
|---|---|---|---|
| `COLOR_1` and beyond | XNA's vertex layouts carry exactly one colour channel. | `extraColorSetsEXT` | `GLTF-091` |
| `_*` custom attributes | Unrecognized custom attributes are named. The recognized legacy _TANGENT/_BINORMAL VEC3 float pair is a separate basis path when NORMAL exists and standard TANGENT does not; it is not automatically included in the ignored list. | `ignoredCustomAttributesEXT` | `GLTF-092` |
| An authored `TANGENT` on a non-PBR layout | Current PBR layouts 48/60/68/76/80 carry a tangent; other selected layouts can drop it. | `droppedTangentForStrideEXT` | `GLTF-086` |
| An authored `NORMAL` on a layout without that slot | Strides 24/28 have no Normal slot. The dedicated drop flag currently checks 24/legacy-20; current dual-UV stride 28 uses the layout rule's generic unrepresentable report instead. A field's existence does not mean that flag covers every stride. | `droppedNormalForStrideEXT`, `unrepresentableForStrideEXT` | `GLTF-241`, `GLTF-462` |
| A texture whose image is KTX2/Basis or WebP, with no fallback `source` | No decoder. When the file provides a plain PNG/JPEG fallback it is used instead and nothing is lost. | `unsupportedTextureSourcesEXT` | `GLTF-200`, `GLTF-350` |
| A third distinct sampled UV set (`TEXCOORD_n`) | PBR layouts carry two packed UV channels. A material sampling three distinct authored sets cannot represent the third; see §2's `uvSetMismatchedMapsEXT` row. | `uvSetMismatchedMapsEXT` | `GLTF-188` |
| Per-instance transforms of `EXT_mesh_gpu_instancing` | The node's own single placement is imported; the file renders one copy where it describes many. | `gpuInstancedNodeCount` | `GLTF-352` |

The **general** statement of this table is `unrepresentableForStrideEXT`: it comes from the
`GLTF-099` layout decision table's own row rather than being re-derived per caller, so every
downgrade CNA performs names itself in one place. `CNA/Internal/Graphics/VertexDeclarationFidelity.hpp`
(`InferredLayoutForStride`) is the query side of the same table — never hardcode a stride's offsets.

`KHR_materials_specular`'s optional `specularTexture` and `specularColorTexture` now have dedicated
PBR-effect and `GpuDrawParams` slots. Direct and offline import retain each map's independent UV,
transform and sampler; the colour map also retains its sRGB declaration. All six current PBR families sample both maps; the source-policy partition has no factor-only
remainder. Import validation still leaves KHR_materials_specular unclaimed and warns optional
use. Renderer support does not silently promote the registry's required-extension contract.
See [PBR bindings/evidence](gltf-renderer-pbr-fallbacks.md); old shader-size/driver snapshots are
not current qualification.

---

## 4. Carried by CNA, applied by the application

State that survives import intact and reaches the effect, while CNA deliberately leaves the final
per-draw device choice to the application. This is narrower than "unsupported": the data and the
consumer path are both proven, but `Model::Draw` does not silently mutate global state or invent a
transparent sort order.

| State | Where it lives | Required application action | Task |
|---|---|---|---|
| `alphaMode: BLEND` | `PbrEffect::getAlphaModeEXTProperty()` | Select `BlendState::NonPremultiplied` and issue transparent parts back-to-front. CNA does not sort by default. `EasyGL_Gltf_AlphaBlend` proves the real imported material against Opaque and premultiplied controls on OPENGLES3. | `GLTF-230` |
| `doubleSided` | `PbrEffect::getDoubleSidedEXTProperty()` | Select `RasterizerState::CullNone`; otherwise keep the glTF front-face state (reversed for mirrored placement). The same test proves a back-facing imported triangle is culled by the single-sided control and visible under the property-derived state on OPENGLES3. | `GLTF-231`, `GLTF-230` |

`alphaMode: MASK` **used** to be in this table and is not any more: the cutoff is fragment-program
work rather than device state. `GLTF-372` wired it into `GpuDrawParams::alphaTest`; the subsequent
cross-renderer audit in `GLTF-379` found that eight PBR implementations did not consume that vector
and five renderers instead selected their non-PBR alpha-test program. Both failures are fixed: a
PBR `MASK` draw stays PBR and every PBR fragment path evaluates the same coverage expression.
`docs/gltf-api-change-review.md` §1.3 records where the line falls and why the other two stay on this
side of it; `docs/gltf-renderer-pbr-fallbacks.md` records the renderer matrix and evidence.

COLOR_0 remains part of the metallic-roughness material: rigid stride 60 and skinned stride 80
carry RGB/alpha, with VertexColorEnabledEXT controlling multiplication. All six current PBR
families implement it; the old “all but one” refusal count is obsolete. An explicit application
opt-out is distinct from silently substituting white.

A valid route must evaluate the material or refuse it before incompatible layout interpretation/
submission. Source partitions and focused live draws cover known routes, not every possible
asset/native program. No universal “nothing can render with different semantics” claim follows.
See [PBR contract and evidence](gltf-renderer-pbr-fallbacks.md).

---

## 4.1 EXT_mesh_gpu_instancing — current unsupported boundary

Required use is refused. For optional use, CNA imports the node's ordinary placement and counts
gpuInstancedNodeCount; it does not apply the extension's per-instance transforms. Native
DrawInstancedPrimitives support does not provide an importer/model instance representation
automatically, nor is that renderer feature universally available.

The old public-member/buffer sketch was a proposal, not implemented API or an environment blocker
that can be assumed permanent. A future change needs metadata/transform checks, a model/draw
contract and capability/refusal behavior, plus selected live/pixel witnesses. L3–L6 can test
instance metadata/dispatch; L7 is complementary, not the only possible verification layer.
No instance-buffer member or new API is authorized by this limitations page.

---

## 5. Environment-dependent

| Capability | Condition | Behaviour when absent |
|---|---|---|
| `KHR_draco_mesh_compression` | `CNA_ENABLE_DRACO=ON`, the default except under Emscripten (the decoder is built from the pinned `third_party/draco` submodule, or taken from the system with `CNA_USE_SYSTEM_DRACO=ON`) | The extension is **not claimed**, so a file requiring it is refused by name, and a Draco-compressed primitive throws at import rather than producing empty geometry. |
| L7 image comparison | A rasterizing renderer with usable color readback, fixture policy and required external reference inputs | STUB has no 3D; HEADLESS's ThreeD is validation-only. Neither supplies rendered-pixel evidence. Capability skips, committed goldens and fresh captures are different results. |

Regardless of decoder availability, the Draco extension's primitive-mode restriction is enforced
before decoding: only `TRIANGLES` and `TRIANGLE_STRIP` are accepted. `POINTS`, all three line modes
and `TRIANGLE_FAN` are rejected with the allowed modes named; decoded strip connectivity is taken
from Draco's explicit face list and is not reinterpreted as a second strip (`GLTF-080`, `GLTF-362`).

---

## Audit verification

The existing Content target builds. All 55 selected registry/document, report, topology, mirroring
and light-budget cases pass privately with HEADLESS selected; source/validation checks do not
imply a new full-corpus L7 or hardware run.
The point/spot-to-directional approximation remains in ExtractPunctualLightsEXT even though CNA
extensions elsewhere can represent punctual lights. It is the current import path, not a claim
that native APIs can never represent them.

The extension table's no-KTX2/WebP statements concern this glTF import route; unrelated texture
stream/container support does not implement KHR_texture_basisu decoding here. Report preservation
above names direct glTF and the .cnj route, not every content format.

## What this file is not

It is not a roadmap. A row here says what happens today and where the loss is reported; whether it
*should* change is the owning task's question, and several deliberately answer "no" —
`KHR_materials_iridescence` is `NOT_DESIRED` rather than pending, and `doubleSided` being carried
rather than applied is a scope decision recorded in the API-change review, not an omission.

See also: `docs/gltf-conventions.md` (every decision with its rationale),
`docs/gltf-conformance.md` (the oracle ladder and the corpus), `docs/gltf-api-change-review.md`
(what may become public API), and `docs/gltf-campaign-retrospective.md` (the campaign; its row-by-row
record is in Git history).
