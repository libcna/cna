# SPDX-License-Identifier: MS-PL
"""Material / PBR fixtures -- owning group ``materials`` (plan_gltf.md §24.2).

Proves **D7**: a metallic-roughness material expressed purely as factors, with no texture maps at
all, is downgraded to an untextured white `BasicEffect`. ``baseColorFactor``, ``alphaMode``,
``alphaCutoff`` and ``doubleSided`` have nowhere to live in `MeshOut` and are never carried
anywhere -- so a gold, half-transparent, double-sided surface imports as opaque white.

Specification: §3.9.2 ``metallic-roughness-material``, §3.9.4 ``alpha-coverage``,
§3.9.5 ``double-sided``.
"""

from __future__ import annotations

from ..builder import TRIANGLES, UNSIGNED_BYTE, UNSIGNED_SHORT, GltfBuilder
from ..manifest import Defect, Fixture, l3_primitive, world_positions
from ..png import encode_png
from .common import TRIANGLE_INDICES, TRIANGLE_NORMALS, TRIANGLE_POSITIONS

#: A gold-ish, half-transparent base colour. Deliberately not white, not opaque, and not a value
#: any default could coincide with.
_BASE_COLOR_FACTOR = [1.0, 0.72, 0.315, 0.5]
#: Every scalar factor is deliberately different from both the glTF default AND from `MeshOut`'s
#: own field default (metallic 1.0, roughness 1.0, emissive zero). Without that, a lost factor
#: would coincide with the value CNA happens to fall back to and the fixture would prove nothing.
_METALLIC_FACTOR = 0.9
_ROUGHNESS_FACTOR = 0.35
_EMISSIVE_FACTOR = [0.25, 0.1, 0.0]
#: GLTF-343/344's factor-only extension witness. IOR 2 gives an exact 1/9 dielectric base F0;
#: blue deliberately exceeds the specular-colour unit range, which Khronos permits, so the
#: required clamp-before-strength order produces 0.3 instead of 0.4.
_IOR = 2.0
_SPECULAR_FACTOR = 0.3
_SPECULAR_COLOR_FACTOR = [0.25, 1.0, 12.0]
_DIELECTRIC_F0 = [1.0 / 120.0, 1.0 / 30.0, 0.3]

#: GLTF-218's two independent operands. The texture is encoded mid-grey, so its linear value is
#: about 0.21586 rather than 0.5; the factor is already linear and must not be decoded. Their
#: product encodes to byte 92. Applying the factor after output encoding gives 64, ignoring it
#: gives 128, and decoding it as though it were a texture gives 61, so one pixel distinguishes all
#: four orders.
_FACTOR_TEXTURE_BASE_COLOR = [0.5, 0.5, 0.5, 1.0]
_FACTOR_TEXTURE_TEXCOORDS = [(0.0, 0.0), (1.0, 0.0), (0.0, 1.0)]
_FACTOR_TEXTURE_TANGENTS = [(1.0, 0.0, 0.0, 1.0)] * 3


def mat_factor_only_gold() -> Fixture:
    """f8 -- a factor-only metallic-roughness material. Proves **D7**."""
    b = GltfBuilder("mat-factor-only-gold")
    b.declare_extensions(used=["KHR_materials_ior", "KHR_materials_specular"])
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    material = b.add_material({
        "name": "Gold",
        "pbrMetallicRoughness": {
            "baseColorFactor": _BASE_COLOR_FACTOR,
            "metallicFactor": _METALLIC_FACTOR,
            "roughnessFactor": _ROUGHNESS_FACTOR,
        },
        "emissiveFactor": _EMISSIVE_FACTOR,
        "alphaMode": "BLEND",
        "doubleSided": True,
        "extensions": {
            "KHR_materials_ior": {"ior": _IOR},
            "KHR_materials_specular": {
                "specularFactor": _SPECULAR_FACTOR,
                "specularColorFactor": _SPECULAR_COLOR_FACTOR,
            },
        },
    })
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal},
        "indices": indices,
        "material": material,
        "mode": TRIANGLES,
    }], name="GoldTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    expected_material = {
        "index": material,
        "name": "Gold",
        "baseColorFactor": _BASE_COLOR_FACTOR,
        "metallicFactor": _METALLIC_FACTOR,
        "roughnessFactor": _ROUGHNESS_FACTOR,
        "ior": _IOR,
        "specularFactor": _SPECULAR_FACTOR,
        "specularColorFactor": _SPECULAR_COLOR_FACTOR,
        "dielectricF0": _DIELECTRIC_F0,
        "dielectricF90": _SPECULAR_FACTOR,
        "emissiveFactor": _EMISSIVE_FACTOR,
        "alphaMode": "BLEND",
        "alphaCutoff": 0.5,
        "doubleSided": True,
        "hasBaseColorTexture": False,
        "hasNormalTexture": False,
        "hasMetallicRoughnessTexture": False,
        "hasOcclusionTexture": False,
        "hasEmissiveTexture": False,
    }
    return Fixture(
        id="mat-factor-only-gold", audit_fixture="f8", owning_group="materials",
        description="A metallic-roughness material with no texture maps at all: gold "
                    "baseColorFactor at 50% alpha, non-default metallic/roughness/emissive "
                    "factors, alphaMode BLEND, doubleSided, plus factor-only IOR/specular "
                    "extensions. Every one of those is a first-class glTF material property. The scalar "
                    "factors were added when promoting the audit's f8, which authored only the "
                    "base colour, alpha mode and double-sidedness -- they widen the same defect "
                    "without changing it.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        features=["pbrMetallicRoughness factors", "baseColorFactor", "alphaMode BLEND",
                  "doubleSided", "KHR_materials_ior", "KHR_materials_specular",
                  "factor-only dielectric F0", "no texture maps"],
        spec_anchors=["metallic-roughness-material", "alpha-coverage", "double-sided",
                      "extensions"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="GoldTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES,
            material=expected_material)]},
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
        defects=[Defect(
            id="D7", owner="GLTF-MATERIAL", first_divergent_layer="L3",
            summary="A factor-only metallic-roughness material was downgraded to BasicEffect and "
                    "NOT ONE material property survived. The selection rule asked which texture "
                    "MAPS were present (`normalImage || metallicRoughnessImage`), so a material "
                    "with every PBR factor and no map could never select PbrEffect -- and because "
                    "the factor assignments sat behind that same guard, even the fields MeshOut "
                    "could carry were left at their defaults. GLTF-215 replaced the rule with the "
                    "material MODEL the file declares, GLTF-217 gave a primitive with no material "
                    "glTF's own default (which IS metallic-roughness), GLTF-216 added "
                    "baseColorFactor and carried it to PbrEffect's DiffuseColor/Alpha, and "
                    "GLTF-219/GLTF-221 ungated the scalar factors. The alpha and sidedness state "
                    "-- alphaMode, alphaCutoff, doubleSided -- has no MeshOut field or effect "
                    "parameter yet and is owned by GLTF-228/GLTF-229/GLTF-231.",
            owning_tasks=["GLTF-215", "GLTF-216", "GLTF-217", "GLTF-219", "GLTF-221",
                          "GLTF-228", "GLTF-229", "GLTF-231"],
            closed_tasks=["GLTF-215", "GLTF-216", "GLTF-217", "GLTF-219", "GLTF-221",
                          "GLTF-228", "GLTF-229", "GLTF-231"],
            status="fixed",
            divergent_fields=[],
            current_actual={
                "usePbr": True,
                "stride": 48,
                "effect": "PbrEffect",
                "carriedFields": ["baseColorFactor", "metallicFactor", "roughnessFactor",
                                  "emissiveFactor", "alphaMode", "alphaCutoff", "doubleSided"],
                "lostFields": [],
                "baseColorFactor": list(_BASE_COLOR_FACTOR),
                "metallicFactor": _METALLIC_FACTOR,
                "roughnessFactor": _ROUGHNESS_FACTOR,
                "emissiveFactor": list(_EMISSIVE_FACTOR),
                "alphaMode": "BLEND",
                "alphaCutoff": 0.5,
                "doubleSided": True,
                "note": "Not one material property is lost any more. The material selects "
                        "PbrEffect; the gold base colour reaches DiffuseColor with its 0.5 alpha; "
                        "the metallic/roughness/emissive factors are read for any "
                        "metallic-roughness material rather than only one that carried a map; and "
                        "the alpha and sidedness state reaches AlphaModeEXT/AlphaCutoff/"
                        "DoubleSided on both PBR effects and through the .cnj. The L5 golden is "
                        "byte-exact at stride 48, which is what proves the effect switch rather "
                        "than merely asserting it. One boundary is deliberate and recorded in "
                        "docs/gltf-api-change-review.md §1.4: doubleSided is CARRIED, not applied "
                        "-- culling is a RasterizerState the application sets. GLTF-230 proves "
                        "the analogous application-owned blend and draw-order boundary without "
                        "making Model::Draw seize global device state.",
            },
            prior_actual={
                "usePbr": False,
                "stride": 32,
                "effect": "BasicEffect",
                "carriedFields": [],
                "lostFields": ["baseColorFactor", "metallicFactor", "roughnessFactor",
                               "emissiveFactor", "alphaMode", "alphaCutoff", "doubleSided"],
                "metallicFactor": 1.0,
                "roughnessFactor": 1.0,
                "emissiveFactor": [0.0, 0.0, 0.0],
                "measuredOn": "fb3728267e8f2179d43b96357ff372ae712b7e7f",
                "note": "What the forensic audit measured: usePbr required a normal map or a "
                        "metallic-roughness map, so a factor-only material could never select "
                        "PbrEffect, and because the factor assignments sat behind that same guard "
                        "even the three fields MeshOut could carry were left at their defaults. "
                        "The surface rendered opaque white under default lighting.",
            },
        )],
    )


def mat_basecolor_factor_times_texture() -> Fixture:
    """GLTF-218 -- baseColorFactor multiplies the decoded baseColorTexture sample."""
    b = GltfBuilder("mat-basecolor-factor-times-texture")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS,
                                   accessor_type="VEC3")
    tangent = b.add_packed_accessor(usage="TANGENT", values=_FACTOR_TEXTURE_TANGENTS,
                                    accessor_type="VEC4")
    texcoord = b.add_packed_accessor(usage="TEXCOORD_0", values=_FACTOR_TEXTURE_TEXCOORDS,
                                     accessor_type="VEC2")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    image = b.add_image(encode_png(1, 1, [[(128, 128, 128, 255)]]), name="MidGrey")
    texture = b.add_texture(source=image, name="MidGreyBaseColor")
    material = b.add_material({
        "name": "HalfMidGrey",
        "pbrMetallicRoughness": {
            "baseColorFactor": _FACTOR_TEXTURE_BASE_COLOR,
            "baseColorTexture": {"index": texture},
            "metallicFactor": 0.0,
            "roughnessFactor": 1.0,
        },
    })
    mesh = b.add_mesh([{
        "attributes": {
            "POSITION": position,
            "NORMAL": normal,
            "TANGENT": tangent,
            "TEXCOORD_0": texcoord,
        },
        "indices": indices,
        "material": material,
        "mode": TRIANGLES,
    }], name="FactorTimesTextureTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    expected_material = {
        "index": material,
        "name": "HalfMidGrey",
        "baseColorFactor": list(_FACTOR_TEXTURE_BASE_COLOR),
        "metallicFactor": 0.0,
        "roughnessFactor": 1.0,
        "emissiveFactor": [0.0, 0.0, 0.0],
        "alphaMode": "OPAQUE",
        "alphaCutoff": 0.5,
        "doubleSided": False,
        "hasBaseColorTexture": True,
        "hasNormalTexture": False,
        "hasMetallicRoughnessTexture": False,
        "hasOcclusionTexture": False,
        "hasEmissiveTexture": False,
    }
    return Fixture(
        id="mat-basecolor-factor-times-texture", audit_fixture=None,
        owning_group="materials",
        description="A one-pixel sRGB mid-grey base-colour texture multiplied by a linear 0.5 "
                    "baseColorFactor. The correct linear-space product encodes to byte 92; "
                    "dropping either operand or applying either transfer in the wrong order "
                    "produces a different byte.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4", "L5"],
        features=["baseColorFactor times baseColorTexture", "sRGB mid-grey sample",
                  "linear-space material multiplication", "analytic L7 byte 92"],
        spec_anchors=["metallic-roughness-material", "textures"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="FactorTimesTextureTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS,
            tangents=_FACTOR_TEXTURE_TANGENTS, texcoords=_FACTOR_TEXTURE_TEXCOORDS,
            indices=TRIANGLE_INDICES, material=expected_material)]},
        l4={**world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
            "baseColorComposition": {
                "textureByteSrgb": 128,
                "decodedTextureLinear": 0.21586050011389926,
                "factorLinear": 0.5,
                "productLinear": 0.10793025005694963,
                "encodedProductByte": 92,
            }},
    )


#: The emissive factor and strength of `mat-emissive-strength`. The product deliberately exceeds
#: 1 in two channels, which is the whole point of the extension: `emissiveFactor` alone is clamped
#: to [0,1] by the schema, and real HDR-authored content routinely wants more than that.
_EMISSIVE_STRENGTH_FACTOR = [0.4, 0.2, 0.1]
_EMISSIVE_STRENGTH = 5.0


def mat_emissive_strength() -> Fixture:
    """``KHR_materials_emissive_strength`` on a factor-only material. Owns **GLTF-222**.

    §3.9.3's ``emissiveFactor`` is a `[0,1]` value; the extension multiplies it, and the product is
    what a renderer must use. Both numbers are stated separately in the manifest alongside their
    product, so a reader that applied the factor but dropped the strength -- the exact shape of the
    defect, which used to sit behind the ``usePbr`` guard -- fails against a value the fixture
    states rather than one a test computed for itself.

    The material carries no texture map at all, which is deliberate: the strength must survive on a
    material that has nothing else to select PBR by.
    """
    b = GltfBuilder("mat-emissive-strength")
    b.declare_extensions(used=["KHR_materials_emissive_strength"])
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    material = b.add_material({
        "name": "Ember",
        "pbrMetallicRoughness": {
            "baseColorFactor": [0.1, 0.1, 0.1, 1.0],
            "metallicFactor": 0.0,
            "roughnessFactor": 0.8,
        },
        "emissiveFactor": _EMISSIVE_STRENGTH_FACTOR,
        "extensions": {
            "KHR_materials_emissive_strength": {"emissiveStrength": _EMISSIVE_STRENGTH},
        },
    })
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal},
        "indices": indices,
        "material": material,
        "mode": TRIANGLES,
    }], name="EmberTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)

    product = [c * _EMISSIVE_STRENGTH for c in _EMISSIVE_STRENGTH_FACTOR]
    expected_material = {
        "index": material,
        "name": "Ember",
        "baseColorFactor": [0.1, 0.1, 0.1, 1.0],
        "metallicFactor": 0.0,
        "roughnessFactor": 0.8,
        # The authored value, the multiplier, and the product a renderer must use -- all three
        # stated, because a fixture that gave only the product could not tell "strength applied"
        # from "a factor authored at that value in the first place".
        "emissiveFactor": _EMISSIVE_STRENGTH_FACTOR,
        "emissiveStrength": _EMISSIVE_STRENGTH,
        "emissiveFactorTimesStrength": product,
        "alphaMode": "OPAQUE",
        "alphaCutoff": 0.5,
        "doubleSided": False,
        "hasBaseColorTexture": False,
        "hasNormalTexture": False,
        "hasMetallicRoughnessTexture": False,
        "hasOcclusionTexture": False,
        "hasEmissiveTexture": False,
    }
    return Fixture(
        id="mat-emissive-strength", audit_fixture=None, owning_group="materials",
        description="A factor-only metallic-roughness material carrying "
                    "KHR_materials_emissive_strength. emissiveFactor [0.4,0.2,0.1] times a "
                    "strength of 5 gives [2.0,1.0,0.5] -- two channels above 1, which is what the "
                    "extension exists for and what a clamped or dropped strength would destroy.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        features=["KHR_materials_emissive_strength", "emissiveFactor", "HDR emissive above 1",
                  "no texture maps"],
        spec_anchors=["additional-textures", "metallic-roughness-material"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="EmberTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES,
            material=expected_material)]},
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


#: Vertex colours for `mat-vertex-color-pbr`. Distinct per vertex so a dropped or reordered stream
#: is visible, and normalized UNSIGNED_BYTE because that is what real exporters emit.
_VERTEX_COLORS = [(255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255)]


def mat_vertex_color_pbr() -> Fixture:
    """``COLOR_0`` on a primitive whose material is metallic-roughness. Owns **GLTF-241**.

    This is the one material combination CNA cannot import as the file asks: no vertex layout
    carries a Color alongside a Tangent, and no PBR shader reads a colour stream. The primitive is
    imported through ``BasicEffect`` with its vertex colours intact and the material's factors and
    maps are *not applied* -- which used to happen in complete silence.

    The material authors every factor away from both glTF's default and CNA's own fallback, so the
    manifest can state exactly what is lost rather than describing it.
    """
    b = GltfBuilder("mat-vertex-color-pbr")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    color = b.add_packed_accessor(usage="COLOR_0", values=_VERTEX_COLORS, accessor_type="VEC4",
                                  component_type=UNSIGNED_BYTE, normalized=True)
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    material = b.add_material({
        "name": "ColoredMetal",
        "pbrMetallicRoughness": {
            "baseColorFactor": [0.2, 0.4, 0.8, 1.0],
            "metallicFactor": 0.85,
            "roughnessFactor": 0.15,
        },
        "emissiveFactor": [0.05, 0.0, 0.2],
    })
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal, "COLOR_0": color},
        "indices": indices,
        "material": material,
        "mode": TRIANGLES,
    }], name="ColoredMetalTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)

    expected_material = {
        "index": material,
        "name": "ColoredMetal",
        "baseColorFactor": [0.2, 0.4, 0.8, 1.0],
        "metallicFactor": 0.85,
        "roughnessFactor": 0.15,
        "emissiveFactor": [0.05, 0.0, 0.2],
        "alphaMode": "OPAQUE",
        "alphaCutoff": 0.5,
        "doubleSided": False,
        "hasBaseColorTexture": False,
        "hasNormalTexture": False,
        "hasMetallicRoughnessTexture": False,
        "hasOcclusionTexture": False,
        "hasEmissiveTexture": False,
        # GLTF-241: what CNA does with this combination, stated so the limitation is a value a test
        # asserts rather than a sentence in a comment.
        "unsupportedMaterialModel": "metallic-roughness",
        "importedEffect": "BasicEffect",
        "vertexColorsPreserved": True,
        "materialPropertiesApplied": False,
        "note": "No CNA vertex layout carries a Color alongside a Tangent and no PBR shader reads "
                "a colour stream, so supporting this means a new stride plus a shader variant on "
                "every renderer. The primitive keeps its vertex colours and the material is "
                "reported as dropped -- the other outcome GLTF-241's acceptance allows.",
    }
    return Fixture(
        id="mat-vertex-color-pbr", audit_fixture=None, owning_group="materials",
        description="A primitive with COLOR_0 and a metallic-roughness material -- the one "
                    "combination CNA cannot import as the file asks. It arrives as a BasicEffect "
                    "with its vertex colours and without its material, and now says so instead of "
                    "downgrading in silence.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        features=["COLOR_0 with a PBR material", "unsupported material model", "import report"],
        spec_anchors=["metallic-roughness-material", "meshes-overview"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="ColoredMetalTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS,
            colors=[[c / 255.0 for c in v] for v in _VERTEX_COLORS],
            indices=TRIANGLE_INDICES, material=expected_material)]},
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
        defects=[Defect(
            id="GLTF-241", owner="GLTF-MATERIAL", first_divergent_layer="L3",
            summary="A primitive with COLOR_0 and a metallic-roughness material cannot be imported "
                    "as the file asks, and the loss runs one layer deeper than the material: the "
                    "stride-24 layout a coloured primitive lands on has no Normal slot either, so "
                    "an authored NORMAL is discarded and the primitive cannot be lit at all. "
                    "Supporting the combination means a new stride plus a shader variant on every "
                    "renderer, the same blast radius that ruled out colour-space option A. "
                    "GLTF-241's acceptance allows the other outcome -- REPORTED, not silently "
                    "downgraded -- and that is what landed: MeshOut names both losses and both "
                    "loaders log them.",
            owning_tasks=["GLTF-238", "GLTF-241"], closed_tasks=["GLTF-241"],
            remaining_tasks=["GLTF-238"],
            status="partially-remediated",
            divergent_fields=["normals"],
            current_actual={
                "usePbr": False,
                "stride": 24,
                "effect": "BasicEffect",
                "vertexColorsPreserved": True,
                "unsupportedMaterialModel": "metallic-roughness",
                "droppedNormalForStride": True,
                "normalsImported": 0,
                "note": "The primitive keeps its vertex colours and loses both its material and "
                        "its normals. Both are now named by MeshOut and logged by both loaders, so "
                        "the downgrade is visible at import rather than only in the rendered "
                        "result. GLTF-238 owns actually supporting the combination.",
            },
            prior_actual={
                "usePbr": False,
                "stride": 24,
                "effect": "BasicEffect",
                "vertexColorsPreserved": True,
                "unsupportedMaterialModel": None,
                "droppedNormalForStride": None,
                "note": "The same geometry, imported in complete silence: nothing recorded that "
                        "the material had been dropped, and nothing recorded that the normals had "
                        "been dropped with it.",
            },
        )],
    )


#: `mat-normal-occlusion-scale`'s two scalars. Both deliberately away from 1 -- the value both the
#: specification default and CNA's own fallback use -- and away from each other, so a swap is
#: visible as well as a drop.
_NORMAL_SCALE = 0.35
_OCCLUSION_STRENGTH = 0.8


def mat_normal_occlusion_scale() -> Fixture:
    """``normalTexture.scale`` and ``occlusionTexture.strength``. Owns **GLTF-224**/**GLTF-225**.

    Neither was ever read, so a material that dialled its normal map down to a subtle 0.35 got the
    full-strength 1.0 instead -- not a subtle difference.

    A glTF texture-info object requires an ``index``; a view containing only the scalar is invalid,
    even though cgltf historically accepted it. The fixture therefore binds one tiny linear map
    through both slots. The pixel values are not this fixture's oracle -- the two independently
    authored scalars remain the discriminating state -- but the source asset itself is valid.
    """
    b = GltfBuilder("mat-normal-occlusion-scale")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    tangent = b.add_packed_accessor(usage="TANGENT", values=_FACTOR_TEXTURE_TANGENTS,
                                    accessor_type="VEC4")
    texcoord = b.add_packed_accessor(usage="TEXCOORD_0", values=_FACTOR_TEXTURE_TEXCOORDS,
                                     accessor_type="VEC2")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    image = b.add_image(encode_png(1, 1, [[(255, 128, 191, 255)]]), name="LinearMap")
    texture = b.add_texture(source=image, name="NormalAndOcclusionMap")
    material = b.add_material({
        "name": "ScaledMaps",
        "pbrMetallicRoughness": {"metallicFactor": 0.0, "roughnessFactor": 0.6},
        "normalTexture": {"index": texture, "scale": _NORMAL_SCALE},
        "occlusionTexture": {"index": texture, "strength": _OCCLUSION_STRENGTH},
    })
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal,
                       "TANGENT": tangent, "TEXCOORD_0": texcoord},
        "indices": indices,
        "material": material,
        "mode": TRIANGLES,
    }], name="ScaledTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)

    expected_material = {
        "index": material,
        "name": "ScaledMaps",
        "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
        "metallicFactor": 0.0,
        "roughnessFactor": 0.6,
        "emissiveFactor": [0.0, 0.0, 0.0],
        "normalScale": _NORMAL_SCALE,
        "occlusionStrength": _OCCLUSION_STRENGTH,
        "alphaMode": "OPAQUE",
        "alphaCutoff": 0.5,
        "doubleSided": False,
        "hasBaseColorTexture": False,
        "hasNormalTexture": True,
        "hasMetallicRoughnessTexture": False,
        "hasOcclusionTexture": True,
        "hasEmissiveTexture": False,
        "occlusionRule": "1 + strength * (sampled - 1). At strength 0 the result is 1 -- no "
                         "occlusion at all, whatever the map holds. Multiplying by the strength "
                         "instead would darken everything to black, which is the plausible wrong "
                         "formula.",
        "normalScaleRule": "Scales the sampled tangent-space normal's X and Y only. Scaling Z too "
                           "would merely rescale the vector, which normalization undoes -- the "
                           "perturbation would not change at all.",
    }
    return Fixture(
        id="mat-normal-occlusion-scale", audit_fixture=None, owning_group="materials",
        description="A material declaring normalTexture.scale 0.35 and occlusionTexture.strength "
                    "0.8. Both were never read, so both arrived as 1. Authored away from 1 and "
                    "away from each other, so a dropped value and a swapped pair are different "
                    "failures.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        features=["normalTexture.scale", "occlusionTexture.strength",
                  "schema-valid shared linear map"],
        spec_anchors=["additional-textures", "metallic-roughness-material"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="ScaledTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS,
            tangents=_FACTOR_TEXTURE_TANGENTS, texcoords=_FACTOR_TEXTURE_TEXCOORDS,
            indices=TRIANGLE_INDICES,
            material=expected_material)]},
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


def mat_default() -> Fixture:
    """A primitive with **no** `material` at all -- glTF's own default material.

    §3.7.2 is explicit: a primitive without a material uses the default, and the default *is*
    metallic-roughness with `baseColorFactor` white, `metallic` 1 and `roughness` 1. It is not "no
    material" and it is not `BasicEffect`. Getting that wrong is half of defect **D7**: the effect
    selection asked which texture maps were present, and a primitive with no material has none, so
    it could never reach the PBR path.
    """
    b = GltfBuilder("mat-default")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="DefaultMaterialTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    expected_material = {
        # No `index`: there is no material in the file. Every value below is §3.7.2's default,
        # which a conforming reader must supply rather than invent its own.
        "name": "",
        "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
        "metallicFactor": 1.0,
        "roughnessFactor": 1.0,
        "emissiveFactor": [0.0, 0.0, 0.0],
        "alphaMode": "OPAQUE",
        "alphaCutoff": 0.5,
        "doubleSided": False,
        "hasBaseColorTexture": False,
        "hasNormalTexture": False,
        "hasMetallicRoughnessTexture": False,
        "hasOcclusionTexture": False,
        "hasEmissiveTexture": False,
        "defaultMaterialRule": "A primitive with no material uses glTF's default, which IS "
                               "metallic-roughness -- so it selects PbrEffect, not BasicEffect. "
                               "GLTF-217. The wrong reading (no material means no PBR) is half of "
                               "defect D7.",
    }
    return Fixture(
        id="mat-default", audit_fixture=None, owning_group="materials",
        description="A primitive that declares no material at all. glTF's default material is "
                    "metallic-roughness with a white base colour and metallic/roughness of 1, so "
                    "this is the fixture that separates 'no material' from 'not PBR' -- a "
                    "distinction defect D7 collapsed.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        features=["no material", "glTF default material", "metallic-roughness by default"],
        spec_anchors=["meshes-overview", "metallic-roughness-material"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="DefaultMaterialTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES,
            material=expected_material)]},
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


#: An emissive factor with three different non-zero channels, none of them 1: a channel dropped,
#: swapped or clamped is a different colour rather than a dimmer one.
_EMISSIVE_ONLY = [0.35, 0.7, 0.05]


def mat_emissive_factor() -> Fixture:
    """`emissiveFactor` without the strength extension -- the plain §3.9.3 case."""
    b = GltfBuilder("mat-emissive-factor")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    material = b.add_material({
        "name": "Glow",
        "pbrMetallicRoughness": {
            "baseColorFactor": [0.05, 0.05, 0.05, 1.0],
            "metallicFactor": 0.0,
            "roughnessFactor": 0.9,
        },
        "emissiveFactor": _EMISSIVE_ONLY,
    })
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal},
        "indices": indices,
        "material": material,
        "mode": TRIANGLES,
    }], name="GlowTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    expected_material = {
        "index": material,
        "name": "Glow",
        "baseColorFactor": [0.05, 0.05, 0.05, 1.0],
        "metallicFactor": 0.0,
        "roughnessFactor": 0.9,
        "emissiveFactor": _EMISSIVE_ONLY,
        "alphaMode": "OPAQUE",
        "alphaCutoff": 0.5,
        "doubleSided": False,
        "hasBaseColorTexture": False,
        "hasNormalTexture": False,
        "hasMetallicRoughnessTexture": False,
        "hasOcclusionTexture": False,
        "hasEmissiveTexture": False,
    }
    return Fixture(
        id="mat-emissive-factor", audit_fixture=None, owning_group="materials",
        description="A near-black material whose emissiveFactor is [0.35,0.7,0.05] -- three "
                    "different non-zero channels, none of them 1. The pair with "
                    "mat-emissive-strength: this is the plain §3.9.3 term with no multiplier, so "
                    "an implementation that applied a strength of 1 twice, or read the factor "
                    "through the extension's code path only, diverges on exactly one of the two.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        features=["emissiveFactor without the strength extension", "dark base colour"],
        spec_anchors=["metallic-roughness-material", "additional-textures"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="GlowTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES,
            material=expected_material)]},
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


#: The mask cutoff of `mat-alpha-mask-cutoff`. Deliberately NOT 0.5: that is glTF's own default
#: for `alphaCutoff` AND what `PbrEffect` falls back to, so a cutoff that never arrived would
#: coincide with the expected value and the fixture would prove nothing.
_MASK_CUTOFF = 0.75
#: The material's own alpha, authored above the cutoff and away from it. A shader handed the base
#: colour's alpha where it wanted the cutoff -- the plausible wrong wiring, since both are alpha
#: quantities on the same material -- produces 0.875 instead of 0.75 and is caught by value.
_MASK_BASE_COLOR_FACTOR = [0.2, 0.6, 0.55, 0.875]


def mat_alpha_mask_cutoff() -> Fixture:
    """``alphaMode: MASK`` with a non-default cutoff. Owns **GLTF-372**.

    `GLTF-228`/`GLTF-229` gave the alpha state somewhere to live on the effect; this fixture is
    about the step after that -- whether the cutoff reaches the fragment program that has to
    perform the cut. Every PBR shader already evaluates a ``uAlphaTest`` vector and discards on it,
    and nothing filled it in, so a MASK material rendered exactly like an OPAQUE one: no cutout at
    all, in complete silence.

    Factor-only, like `mat-factor-only-gold`, so the fixture is about the cutoff rather than about
    texture loading: the alpha a mask compares comes from ``baseColorFactor.a`` alone here.
    """
    b = GltfBuilder("mat-alpha-mask-cutoff")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    material = b.add_material({
        "name": "Foliage",
        "pbrMetallicRoughness": {
            "baseColorFactor": _MASK_BASE_COLOR_FACTOR,
            "metallicFactor": 0.0,
            "roughnessFactor": 0.7,
        },
        "alphaMode": "MASK",
        "alphaCutoff": _MASK_CUTOFF,
    })
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal},
        "indices": indices,
        "material": material,
        "mode": TRIANGLES,
    }], name="FoliageTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)

    expected_material = {
        "index": material,
        "name": "Foliage",
        "baseColorFactor": _MASK_BASE_COLOR_FACTOR,
        "metallicFactor": 0.0,
        "roughnessFactor": 0.7,
        "emissiveFactor": [0.0, 0.0, 0.0],
        "alphaMode": "MASK",
        "alphaCutoff": _MASK_CUTOFF,
        "doubleSided": False,
        "hasBaseColorTexture": False,
        "hasNormalTexture": False,
        "hasMetallicRoughnessTexture": False,
        "hasOcclusionTexture": False,
        "hasEmissiveTexture": False,
        # GLTF-372: the cutoff's destination, stated as the four numbers a renderer receives rather
        # than as prose. The shaders evaluate
        #   at = (y > 0) ? (|a-x| < y ? z : w) : ((a < x) ? z : w);  if (at < 0) discard;
        # so a mask is {cutoff, 0, -1, +1} -- alpha below the cutoff selects the negative weight.
        # Getting z/w the wrong way round inverts the cutout, which is why the signs are stated.
        "gpuAlphaTest": [_MASK_CUTOFF, 0.0, -1.0, 1.0],
        "alphaTestRule": "OPAQUE and BLEND never discard, so both keep GpuDrawParams' own "
                         "{0,0,1,1} default; only MASK writes a reference. BLEND's compositing "
                         "and OPAQUE's ignore-alpha rule are BlendState, which the application "
                         "owns per draw (docs/gltf-api-change-review.md §1.3).",
    }
    return Fixture(
        id="mat-alpha-mask-cutoff", audit_fixture=None, owning_group="materials",
        description="A factor-only metallic-roughness material with alphaMode MASK and a cutoff of "
                    "0.75 -- neither glTF's default nor the material's own alpha (0.875), so a "
                    "cutoff that never arrived and one wired to the wrong alpha quantity are both "
                    "visible as values. Every PBR shader already discards on uAlphaTest; until "
                    "GLTF-372 nothing filled it in and a mask rendered as an opaque surface.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        features=["alphaMode MASK", "non-default alphaCutoff", "alpha test reaches the shader",
                  "no texture maps"],
        spec_anchors=["alpha-coverage", "metallic-roughness-material"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="FoliageTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES,
            material=expected_material)]},
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


#: The three extensions `mat-unimplemented-extensions` declares. All three are classified
#: `PARSED_BUT_IGNORED` by the registry, each for a stated reason, and this fixture is what makes
#: "ignored" mean *reported* rather than *silent* (`GLTF-345`/`GLTF-346`/`GLTF-347`).
_UNIMPLEMENTED_EXTENSIONS = [
    "KHR_materials_clearcoat",
    "KHR_materials_sheen",
    "KHR_materials_volume",
]


def mat_unimplemented_extensions() -> Fixture:
    """A material declaring clearcoat, sheen and volume at once. Owns **`GLTF-345`–`GLTF-347`**.

    Each of the three is a BRDF lobe or a volumetric term CNA's stock effects do not have, and each
    is deliberately **deferred rather than implemented** -- the registry says so with a reason. What
    the three rows require of a deferral is that it be *reported*: an extension listed in
    ``extensionsUsed`` and ignored has to produce a warning naming it, because "loaded fine" and
    "loaded as authored" are different claims and only one of them is true here.

    Declared in ``extensionsUsed`` and **not** in ``extensionsRequired``, which is the whole
    distinction §3.12 draws: none of the three is claimed, so a file that *required* one would be
    refused outright (`GLTF-333`) rather than warned about. The material also carries real
    parameter blocks for each, so the fixture proves cgltf parses them and CNA still ignores them --
    rather than proving only that an unknown name is passed through.
    """
    b = GltfBuilder("mat-unimplemented-extensions")
    b.declare_extensions(used=_UNIMPLEMENTED_EXTENSIONS)
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    material = b.add_material({
        "name": "Lacquered",
        "pbrMetallicRoughness": {
            "baseColorFactor": [0.6, 0.1, 0.1, 1.0],
            "metallicFactor": 0.0,
            "roughnessFactor": 0.4,
        },
        "extensions": {
            # Authored with non-default values throughout: a reader that "supported" these by
            # reading them and applying nothing would be indistinguishable from one that ignored
            # them if every value were the extension's own default.
            "KHR_materials_clearcoat": {"clearcoatFactor": 0.8, "clearcoatRoughnessFactor": 0.05},
            "KHR_materials_sheen": {"sheenColorFactor": [0.9, 0.7, 0.4],
                                     "sheenRoughnessFactor": 0.3},
            "KHR_materials_volume": {"thicknessFactor": 2.5, "attenuationDistance": 1.5,
                                      "attenuationColor": [0.2, 0.9, 0.6]},
        },
    })
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal},
        "indices": indices,
        "material": material,
        "mode": TRIANGLES,
    }], name="LacqueredTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    expected_material = {
        "index": material,
        "name": "Lacquered",
        "baseColorFactor": [0.6, 0.1, 0.1, 1.0],
        "metallicFactor": 0.0,
        "roughnessFactor": 0.4,
        "emissiveFactor": [0.0, 0.0, 0.0],
        "alphaMode": "OPAQUE",
        "alphaCutoff": 0.5,
        "doubleSided": False,
        "hasBaseColorTexture": False,
        "hasNormalTexture": False,
        "hasMetallicRoughnessTexture": False,
        "hasOcclusionTexture": False,
        "hasEmissiveTexture": False,
        # The deferral, stated as data: what the file asked for, and what arrives instead.
        "ignoredExtensions": list(_UNIMPLEMENTED_EXTENSIONS),
        "ignoredExtensionValues": {
            "KHR_materials_clearcoat": {"clearcoatFactor": 0.8, "clearcoatRoughnessFactor": 0.05},
            "KHR_materials_sheen": {"sheenColorFactor": [0.9, 0.7, 0.4],
                                     "sheenRoughnessFactor": 0.3},
            "KHR_materials_volume": {"thicknessFactor": 2.5, "attenuationDistance": 1.5,
                                      "attenuationColor": [0.2, 0.9, 0.6]},
        },
        "deferralRule": "Each is a BRDF lobe or volumetric term no CNA stock effect has, so each "
                        "is PARSED_BUT_IGNORED with a reason rather than half-implemented. The "
                        "requirement the deferral must meet is that it is REPORTED: an ignored "
                        "extensionsUsed entry produces a warning naming it. None is claimed, so a "
                        "file that REQUIRED one is refused outright instead (GLTF-333).",
    }
    return Fixture(
        id="mat-unimplemented-extensions", audit_fixture=None, owning_group="materials",
        description="A metallic-roughness material declaring KHR_materials_clearcoat, "
                    "KHR_materials_sheen and KHR_materials_volume at once, each with non-default "
                    "parameters. All three are deferred by decision, and the fixture exists to "
                    "prove the deferral is reported by name rather than silent -- three warnings, "
                    "one per extension, while the material itself imports normally.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        features=["KHR_materials_clearcoat", "KHR_materials_sheen", "KHR_materials_volume",
                  "ignored extension reporting"],
        spec_anchors=["metallic-roughness-material", "extensions"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="LacqueredTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES,
            material=expected_material)]},
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


def mat_material_variants() -> Fixture:
    """Two mapped KHR_materials_variants plus one sparse fallback. Owns GLTF-340--342.

    The default is an opaque red PBR material, variant 0 a blue PBR material with 0.5 transmission,
    and variant 1 unlit. The first pair is also the licence-clean GLTF-340 layering witness: render
    the red surface as a dial, then the blue transmission approximation as nearer glass. The latter
    variant deliberately changes the vertex layout from stride 48 to stride 32, proving that
    selection swaps the complete part state rather than only an Effect pointer. Variant 2 has no
    mapping on the primitive: selecting it must restore the default, which separates the
    extension's sparse fallback rule from stale state left by the prior selection.
    """
    b = GltfBuilder("mat-material-variants")
    variant_names = ["Ocean blue", "Unlit green", "No mapping"]
    # Both extensions are required deliberately: the fixture proves CNA claims variants only once
    # the selection semantics and the unlit alternative are both available, rather than merely
    # noticing the mapping and importing the default material.
    b.declare_extensions(
        used=["KHR_materials_transmission"],
        required=["KHR_materials_variants", "KHR_materials_unlit"])
    b.add_root_extension("KHR_materials_variants", {
        "variants": [{"name": name} for name in variant_names],
    })

    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS,
                                   accessor_type="VEC3")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)

    default_material = b.add_material({
        "name": "DefaultRed",
        "pbrMetallicRoughness": {
            "baseColorFactor": [0.6, 0.1, 0.1, 1.0],
            "metallicFactor": 0.0,
            "roughnessFactor": 0.4,
        },
    })
    blue_variant = b.add_material({
        "name": "OceanBlue",
        "pbrMetallicRoughness": {
            "baseColorFactor": [0.05, 0.2, 0.9, 1.0],
            "metallicFactor": 0.65,
            "roughnessFactor": 0.25,
        },
        # CNA deliberately approximates this as straight alpha 1 - transmissionFactor. Keeping
        # the authored base alpha at one makes the expected 0.5 exact and gives GLTF-340 a
        # discriminating framebuffer composition without adding another corpus identity.
        "extensions": {
            "KHR_materials_transmission": {"transmissionFactor": 0.5},
        },
    })
    unlit_variant = b.add_material({
        "name": "UnlitGreen",
        "pbrMetallicRoughness": {
            "baseColorFactor": [0.1, 0.8, 0.2, 0.75],
            "metallicFactor": 0.0,
            "roughnessFactor": 1.0,
        },
        "extensions": {"KHR_materials_unlit": {}},
        "alphaMode": "BLEND",
    })
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal},
        "indices": indices,
        "material": default_material,
        "mode": TRIANGLES,
        "extensions": {"KHR_materials_variants": {"mappings": [
            {"material": blue_variant, "variants": [0]},
            {"material": unlit_variant, "variants": [1]},
        ]}},
    }], name="VariantTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)

    expected_material = {
        "index": default_material,
        "name": "DefaultRed",
        "baseColorFactor": [0.6, 0.1, 0.1, 1.0],
        "metallicFactor": 0.0,
        "roughnessFactor": 0.4,
        "emissiveFactor": [0.0, 0.0, 0.0],
        "alphaMode": "OPAQUE",
        "alphaCutoff": 0.5,
        "doubleSided": False,
        "hasBaseColorTexture": False,
        "hasNormalTexture": False,
        "hasMetallicRoughnessTexture": False,
        "hasOcclusionTexture": False,
        "hasEmissiveTexture": False,
    }
    return Fixture(
        id="mat-material-variants", audit_fixture=None, owning_group="materials",
        description="An opaque red default PBR material, a blue PBR variant with 0.5 "
                    "KHR_materials_transmission, an unlit green variant that changes effect class "
                    "and vertex stride, and a third variant intentionally unmapped on the "
                    "primitive. The first pair provides a licence-clean dial/glass ordering "
                    "witness; fresh load and sparse selection keep the default, while selection "
                    "by source index swaps the complete part state.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4", "L5"],
        features=["KHR_materials_variants", "KHR_materials_transmission",
                  "transmission alpha ordering", "source-order variant identity",
                  "sparse variant mapping", "PBR-to-unlit variant", "default material reset"],
        spec_anchors=["extensions", "metallic-roughness-material"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="VariantTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES,
            material=expected_material)]},
        l4={**world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
            "materialVariants": {
                "names": variant_names,
                "defaultMaterial": default_material,
                "defaultBaseColorFactor": [0.6, 0.1, 0.1, 1.0],
                "mappings": [
                    {"variant": 0, "material": blue_variant,
                     "effect": "PbrEffect", "vertexStride": 48,
                     "baseColorFactor": [0.05, 0.2, 0.9, 0.5],
                     "alphaMode": "BLEND", "transmissionFactor": 0.5,
                     "transmissionApproximation": "straight alpha = 1 - factor"},
                    {"variant": 1, "material": unlit_variant,
                     "effect": "BasicEffect", "vertexStride": 32,
                     "baseColorFactor": [0.1, 0.8, 0.2, 0.75]},
                ],
                "unmappedVariant": 2,
                "defaultSelection": -1,
                "selectionRule": "Selecting a variant resets every primitive to its core material "
                                 "first, then applies that variant's sparse mappings. Variant 2 has "
                                 "no mapping and therefore produces the exact default effect and "
                                 "vertex buffer rather than retaining variant 1's unlit state.",
            }},
    )


#: A UV set with three distinct, non-degenerate coordinates, so a stride whose TextureCoordinate
#: slot were mis-offset produces visibly wrong bytes rather than three copies of (0,0).
_UNLIT_TEXCOORDS = [(0.0, 0.0), (0.75, 0.125), (0.25, 0.875)]


def mat_unlit() -> Fixture:
    """A `KHR_materials_unlit` material -- the fixture that reaches vertex stride 32.

    plan_gltf.md `GLTF-149`. Every other unskinned material fixture lands on the PBR stride (48),
    because metallic-roughness is glTF's default in two separate ways. Stride 32 --
    Position+Normal+TextureCoordinate, the plain `BasicEffect` layout -- is reached only by a
    material declaring a model CNA's PBR shaders do not implement, and it is the layout most
    non-PBR content ends up on. Without this fixture the widest stride in the ABI had no golden
    bytes at all.

    Since `GLTF-337` the extension is also **implemented**: it maps to ``LightingEnabled = false``
    on `BasicEffect` with ``baseColorFactor`` as the diffuse colour, which is exactly what "shade
    this surface with its base colour and nothing else" means. The base colour is authored neither
    white nor grey precisely so a material that arrived unlit but at the effect's own default white
    is a different, detectable result from one that arrived correctly.
    """
    b = GltfBuilder("mat-unlit")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    texcoord = b.add_packed_accessor(usage="TEXCOORD_0", values=_UNLIT_TEXCOORDS,
                                     accessor_type="VEC2")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    material = b.add_material({
        "name": "Unlit",
        "pbrMetallicRoughness": {"baseColorFactor": [0.2, 0.6, 0.9, 1.0]},
        "extensions": {"KHR_materials_unlit": {}},
    })
    b.declare_extensions(used=["KHR_materials_unlit"])
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal, "TEXCOORD_0": texcoord},
        "indices": indices,
        "material": material,
        "mode": TRIANGLES,
    }], name="UnlitTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    expected_material = {
        "index": material,
        "name": "Unlit",
        "model": "unlit",
        "baseColorFactor": [0.2, 0.6, 0.9, 1.0],
        "metallicFactor": 1.0,
        "roughnessFactor": 1.0,
        "emissiveFactor": [0.0, 0.0, 0.0],
        "alphaMode": "OPAQUE",
        "alphaCutoff": 0.5,
        "doubleSided": False,
        "hasBaseColorTexture": False,
        "hasNormalTexture": False,
        "hasMetallicRoughnessTexture": False,
        "hasOcclusionTexture": False,
        "hasEmissiveTexture": False,
    }
    return Fixture(
        id="mat-unlit", audit_fixture=None, owning_group="materials",
        description="A material declaring KHR_materials_unlit. CNA has no unlit shader, so the "
                    "primitive imports through BasicEffect on the stride-32 "
                    "Position+Normal+TextureCoordinate layout -- the one unskinned stride no other "
                    "fixture reaches, and the one most non-PBR content uses.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4", "L5"],
        features=["KHR_materials_unlit", "non-PBR material model", "vertex stride 32"],
        spec_anchors=["metallic-roughness-material"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="UnlitTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS,
            texcoords=_UNLIT_TEXCOORDS, indices=TRIANGLE_INDICES,
            material=expected_material)]},
        l4={**world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
            "unlit": {
                "declared": True,
                "effect": "BasicEffect",
                "lightingEnabled": False,
                "diffuseColor": [0.2, 0.6, 0.9],
                "alpha": 1.0,
                "rule": "KHR_materials_unlit means 'shade this surface with its base colour and "
                        "nothing else', and LightingEnabled = false is precisely that -- one of "
                        "the few extensions that maps rather than approximates (GLTF-337).",
                "colourRule": "The base colour must come from baseColorFactor. On the non-PBR "
                              "path nothing else reads it, so an unlit material would otherwise "
                              "import unlit AND white -- which is why this one is neither white "
                              "nor grey.",
                "lightingRigRule": "The punctual-light rig is SKIPPED for an unlit primitive. "
                                   "Every path through it ends with lighting on -- the no-lights "
                                   "fallback calls EnableDefaultLighting() -- so applying it "
                                   "would immediately undo the flag.",
            }},
    )


#: An authored tangent basis: +X with the handedness sign deliberately -1, which is NOT the value a
#: generator falls back to, so a dropped or regenerated basis is visible in the sign alone.
_AUTHORED_TANGENTS = [(1.0, 0.0, 0.0, -1.0), (1.0, 0.0, 0.0, -1.0), (1.0, 0.0, 0.0, -1.0)]
_AUTHORED_TANGENT_UVS = [(0.0, 0.0), (1.0, 0.0), (0.0, 1.0)]


def mat_authored_tangent() -> Fixture:
    """A primitive authoring its own `TANGENT` -- the fixture `GLTF-178` compares tangents on.

    Every other corpus fixture either has no tangent slot or gets a generated basis, and a
    generated one cannot be a conformance expectation: the algorithm is CNA's own, not the
    specification's. An authored basis is the opposite -- §3.7.2.1 says exactly what it means, so
    it can be compared numerically, and it is the case a regression would actually break. The
    handedness is -1 rather than the +1 a generator falls back to, so "the authored basis was
    dropped and regenerated" shows up in the sign alone.
    """
    b = GltfBuilder("mat-authored-tangent")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    tangent = b.add_packed_accessor(usage="TANGENT", values=_AUTHORED_TANGENTS,
                                    accessor_type="VEC4")
    texcoord = b.add_packed_accessor(usage="TEXCOORD_0", values=_AUTHORED_TANGENT_UVS,
                                     accessor_type="VEC2")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    material = b.add_material({
        "name": "TangentMat",
        "pbrMetallicRoughness": {"metallicFactor": 0.25, "roughnessFactor": 0.75},
    })
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal, "TANGENT": tangent,
                       "TEXCOORD_0": texcoord},
        "indices": indices,
        "material": material,
        "mode": TRIANGLES,
    }], name="TangentTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    expected_material = {
        "index": material,
        "name": "TangentMat",
        "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
        "metallicFactor": 0.25,
        "roughnessFactor": 0.75,
        "emissiveFactor": [0.0, 0.0, 0.0],
        "alphaMode": "OPAQUE",
        "alphaCutoff": 0.5,
        "doubleSided": False,
        "hasBaseColorTexture": False,
        "hasNormalTexture": False,
        "hasMetallicRoughnessTexture": False,
        "hasOcclusionTexture": False,
        "hasEmissiveTexture": False,
    }
    return Fixture(
        id="mat-authored-tangent", audit_fixture=None, owning_group="materials",
        description="A metallic-roughness primitive authoring POSITION, NORMAL, TANGENT and "
                    "TEXCOORD_0. The authored tangent basis must survive to the stride-48 vertex "
                    "buffer unchanged, handedness sign included.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4", "L5"],
        features=["authored TANGENT", "tangent handedness", "vertex stride 48"],
        spec_anchors=["meshes-overview", "metallic-roughness-material"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="TangentTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS,
            tangents=_AUTHORED_TANGENTS, texcoords=_AUTHORED_TANGENT_UVS,
            indices=TRIANGLE_INDICES, material=expected_material)]},
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


#: `mat-unlit-vertex-color-alpha`'s per-vertex colours: three saturated, mutually distinct RGBs at
#: three distinct alphas, so a dropped colour stream, a swapped channel order and a dropped alpha
#: are each separately visible rather than collectively "looks wrong".
_UNLIT_VERTEX_COLORS = [(1.0, 0.0, 0.0, 1.0), (0.0, 1.0, 0.0, 0.5), (0.0, 0.0, 1.0, 0.25)]


def mat_unlit_vertex_color_alpha() -> Fixture:
    """`KHR_materials_unlit` with `COLOR_0` and a transparent base colour. Owns **GLTF-338**.

    The two things an unlit material is most often combined with, and the two that most easily get
    lost when "turn lighting off" is implemented as a special case rather than as a flag:

    * **Vertex colour.** ``COLOR_0`` multiplies the diffuse colour in the shader exactly as it does
      for a lit `BasicEffect`, so unlit needs no separate path -- but a material selection that
      treated unlit as its own effect type would have to reimplement it, and would forget the
      alpha channel first. Both are asserted.
    * **Alpha.** The extension does not exempt a surface from ``alphaMode``: an unlit material with
      a transparent base colour is the ordinary way to author a decal or a UI plane, and it is
      exactly the case where "unlit" is most likely to be read as "no material state at all".

    ``baseColorFactor``'s alpha is 0.5 and the material declares ``BLEND``, so the surface's final
    alpha is a product of two authored numbers rather than either one -- a reader that dropped
    one would still produce a translucent surface, at the wrong translucency.
    """
    b = GltfBuilder("mat-unlit-vertex-color-alpha")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    # No NORMAL, deliberately. §3.7.2.1 makes it optional, an unlit surface has no lighting term to
    # use one for, and a coloured primitive lands on the stride-24 Position+Color layout which has
    # no normal slot anyway -- so authoring one would add an irrelevant loss to a fixture that is
    # about exactly two things.
    color = b.add_packed_accessor(usage="COLOR_0", values=_UNLIT_VERTEX_COLORS,
                                  accessor_type="VEC4")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    material = b.add_material({
        "name": "UnlitDecal",
        "pbrMetallicRoughness": {"baseColorFactor": [1.0, 0.8, 0.4, 0.5]},
        "alphaMode": "BLEND",
        "extensions": {"KHR_materials_unlit": {}},
    })
    b.declare_extensions(used=["KHR_materials_unlit"])
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "COLOR_0": color},
        "indices": indices,
        "material": material,
        "mode": TRIANGLES,
    }], name="UnlitDecalTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)

    expected_material = {
        "index": material,
        "name": "UnlitDecal",
        "model": "unlit",
        "baseColorFactor": [1.0, 0.8, 0.4, 0.5],
        "metallicFactor": 1.0,
        "roughnessFactor": 1.0,
        "emissiveFactor": [0.0, 0.0, 0.0],
        "alphaMode": "BLEND",
        "alphaCutoff": 0.5,
        "doubleSided": False,
        "hasBaseColorTexture": False,
        "hasNormalTexture": False,
        "hasMetallicRoughnessTexture": False,
        "hasOcclusionTexture": False,
        "hasEmissiveTexture": False,
    }
    return Fixture(
        id="mat-unlit-vertex-color-alpha", audit_fixture=None, owning_group="materials",
        description="An unlit material with a translucent base colour on a primitive carrying "
                    "COLOR_0. Vertex colour and alpha are the two things an unlit material is most "
                    "often combined with and the two most easily lost when 'lighting off' is "
                    "implemented as a special case rather than as a flag.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4", "L5"],
        features=["KHR_materials_unlit", "COLOR_0", "translucent baseColorFactor", "alphaMode BLEND",
                  "non-PBR material model"],
        spec_anchors=["metallic-roughness-material", "alpha-coverage"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="UnlitDecalTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS,
            colors=_UNLIT_VERTEX_COLORS, indices=TRIANGLE_INDICES,
            material=expected_material)]},
        l4={**world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
            "unlit": {
                "declared": True,
                "effect": "BasicEffect",
                "lightingEnabled": False,
                "vertexColorEnabled": True,
                "diffuseColor": [1.0, 0.8, 0.4],
                "alpha": 0.5,
                "vertexColors": [list(c) for c in _UNLIT_VERTEX_COLORS],
                "vertexColorRule": "COLOR_0 multiplies the diffuse colour in the shader exactly as "
                                   "it does for a lit BasicEffect, so unlit needs no separate path "
                                   "-- but an implementation that made unlit its own effect type "
                                   "would have to reimplement it, and would drop the alpha channel "
                                   "first.",
                "alphaRule": "The extension does not exempt a surface from alphaMode. The final "
                             "alpha is baseColorFactor.a TIMES the vertex alpha, so a reader that "
                             "dropped either still produces a translucent surface -- at the wrong "
                             "translucency.",
            }},
    )


#: `mat-specular-glossiness`'s authored values. The diffuse is strongly coloured so its survival is
#: visible; the glossiness is 0.8, giving a roughness of exactly 0.2 -- a number no default is; and
#: the specular is a saturated gold, the one term the conversion cannot carry, so what is lost is
#: as visible as what survives.
_SG_DIFFUSE = [0.1, 0.35, 0.7, 1.0]
_SG_SPECULAR = [1.0, 0.766, 0.336]
_SG_GLOSSINESS = 0.8


def mat_specular_glossiness() -> Fixture:
    """An archived `KHR_materials_pbrSpecularGlossiness` material. Owns **GLTF-349**.

    Khronos archived the extension, but it is what a decade of older assets are authored in, so
    refusing it would reject content that is otherwise perfectly importable. CNA **converts** it to
    metallic-roughness with the standard mapping -- diffuse becomes the base colour, the surface
    becomes a dielectric, and roughness is glossiness inverted -- and the fixture states all three
    so a conversion that dropped or inverted one is a numeric mismatch rather than a plausible
    material.

    The conversion is a genuine approximation and the fixture is built to show its edge:
    ``specularFactor`` is authored as a saturated gold, which metallic-roughness cannot express at
    all. A coloured specular reflection is only reachable there by making the surface metal, which
    would also tint the diffuse -- a different material, not a closer one. So the specular is
    dropped, and both the fact and its magnitude are reported.

    Glossiness 0.8 gives roughness exactly 0.2, which is neither the glTF default (1.0) nor
    ``MeshOut``'s own: a conversion that forgot to invert would produce 0.8, and one that forgot
    entirely would produce 1.0, and the three are separable.
    """
    b = GltfBuilder("mat-specular-glossiness")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    material = b.add_material({
        "name": "LegacyBrass",
        "extensions": {"KHR_materials_pbrSpecularGlossiness": {
            "diffuseFactor": list(_SG_DIFFUSE),
            "specularFactor": list(_SG_SPECULAR),
            "glossinessFactor": _SG_GLOSSINESS,
        }},
    })
    b.declare_extensions(used=["KHR_materials_pbrSpecularGlossiness"])
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal},
        "indices": indices,
        "material": material,
        "mode": TRIANGLES,
    }], name="LegacyTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)

    converted_roughness = round(1.0 - _SG_GLOSSINESS, 6)
    expected_material = {
        "index": material,
        "name": "LegacyBrass",
        "model": "specular-glossiness",
        # The CONVERTED values, which is what the importer's own material record holds: L3 is what
        # the importer understood the material to be, not what the file spelled.
        "baseColorFactor": list(_SG_DIFFUSE),
        "metallicFactor": 0.0,
        "roughnessFactor": converted_roughness,
        "emissiveFactor": [0.0, 0.0, 0.0],
        "alphaMode": "OPAQUE",
        "alphaCutoff": 0.5,
        "doubleSided": False,
        "hasBaseColorTexture": False,
        "hasNormalTexture": False,
        "hasMetallicRoughnessTexture": False,
        "hasOcclusionTexture": False,
        "hasEmissiveTexture": False,
    }
    return Fixture(
        id="mat-specular-glossiness", audit_fixture=None, owning_group="materials",
        description="An archived KHR_materials_pbrSpecularGlossiness material, converted to "
                    "metallic-roughness rather than refused. Diffuse survives, metallic becomes 0, "
                    "roughness is glossiness inverted, and the saturated gold specularFactor is "
                    "dropped -- the one term the target model cannot express.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4", "L5"],
        features=["KHR_materials_pbrSpecularGlossiness", "archived extension",
                  "converted to metallic-roughness", "dropped specular tint"],
        spec_anchors=["metallic-roughness-material"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="LegacyTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES,
            material=expected_material)]},
        l4={**world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
            "specularGlossiness": {
                "authoredDiffuseFactor": list(_SG_DIFFUSE),
                "authoredSpecularFactor": list(_SG_SPECULAR),
                "authoredGlossinessFactor": _SG_GLOSSINESS,
                "convertedBaseColorFactor": list(_SG_DIFFUSE),
                "convertedMetallicFactor": 0.0,
                "convertedRoughnessFactor": converted_roughness,
                "droppedSpecularStrength": max(_SG_SPECULAR),
                "converted": True,
                "usesPbrPath": True,
                "conversionRule": "diffuse -> base colour, metallic -> 0, roughness -> "
                                  "1 - glossiness. Glossiness 0.8 gives roughness 0.2, which is "
                                  "neither the glTF default (1.0) nor the un-inverted 0.8, so a "
                                  "conversion that forgot to invert and one that forgot entirely "
                                  "are separable from a correct one.",
                "lossRule": "specularFactor has no metallic-roughness equivalent: a coloured "
                            "specular reflection is only reachable by making the surface metal, "
                            "which would also tint the diffuse -- a different material, not a "
                            "closer one. Dropped, with its magnitude reported.",
                "claimRule": "Converted but NOT claimed, so a file listing the extension in "
                             "extensionsRequired is still refused. An approximation is not an "
                             "implementation, and a file that requires specular-glossiness is "
                             "asking for the very term the conversion discards.",
            }},
    )


FIXTURES = [mat_default, mat_factor_only_gold, mat_basecolor_factor_times_texture,
            mat_emissive_factor, mat_emissive_strength,
            mat_vertex_color_pbr,
            mat_normal_occlusion_scale, mat_alpha_mask_cutoff, mat_unimplemented_extensions,
            mat_material_variants, mat_unlit, mat_unlit_vertex_color_alpha,
            mat_specular_glossiness, mat_authored_tangent]
