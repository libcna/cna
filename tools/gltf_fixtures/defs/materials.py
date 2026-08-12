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

from ..builder import TRIANGLES, UNSIGNED_SHORT, GltfBuilder
from ..manifest import Defect, Fixture, l3_primitive, world_positions
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


def mat_factor_only_gold() -> Fixture:
    """f8 -- a factor-only metallic-roughness material. Proves **D7**."""
    b = GltfBuilder("mat-factor-only-gold")
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
                    "factors, alphaMode BLEND, doubleSided. Every one of those is a first-class "
                    "glTF material property and none of them survives import today. The scalar "
                    "factors were added when promoting the audit's f8, which authored only the "
                    "base colour, alpha mode and double-sidedness -- they widen the same defect "
                    "without changing it.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        features=["pbrMetallicRoughness factors", "baseColorFactor", "alphaMode BLEND",
                  "doubleSided", "no texture maps"],
        spec_anchors=["metallic-roughness-material", "alpha-coverage", "double-sided"],
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
            closed_tasks=["GLTF-215", "GLTF-216", "GLTF-217", "GLTF-219", "GLTF-221"],
            remaining_tasks=["GLTF-228", "GLTF-229", "GLTF-231"],
            status="partially-remediated",
            divergent_fields=["material"],
            current_actual={
                "usePbr": True,
                "stride": 48,
                "effect": "PbrEffect",
                "carriedFields": ["baseColorFactor", "metallicFactor", "roughnessFactor",
                                  "emissiveFactor"],
                "lostFields": ["alphaMode", "alphaCutoff", "doubleSided"],
                "baseColorFactor": list(_BASE_COLOR_FACTOR),
                "metallicFactor": _METALLIC_FACTOR,
                "roughnessFactor": _ROUGHNESS_FACTOR,
                "emissiveFactor": list(_EMISSIVE_FACTOR),
                "note": "The material now selects PbrEffect and every authored FACTOR survives: "
                        "the gold base colour reaches DiffuseColor with its 0.5 alpha, and the "
                        "metallic/roughness/emissive factors are read for any metallic-roughness "
                        "material rather than only one that also carried a map. The L5 golden is "
                        "byte-exact at stride 48, which is what proves the switch rather than "
                        "merely asserting it. What is still lost is the alpha and sidedness "
                        "state: MeshOut has no field for alphaMode, alphaCutoff or doubleSided, "
                        "and PbrEffect has no parameter to put them in -- GLTF-228/229/231 add "
                        "both, behind the GLTF-025 API-change gate.",
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


FIXTURES = [mat_factor_only_gold]
