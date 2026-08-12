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
                        "-- culling is a RasterizerState the application sets, and GLTF-230 owns "
                        "making it automatic alongside blend state and draw ordering.",
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

    The material declares both texture *views* so the scalars have somewhere to live, and neither
    view names a texture: the corpus has no image support yet (``GLTF-190``), and the scalars are
    material state that reaches the effect whether or not a map is bound. That keeps this fixture
    about the two numbers rather than about texture loading.
    """
    b = GltfBuilder("mat-normal-occlusion-scale")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    material = b.add_material({
        "name": "ScaledMaps",
        "pbrMetallicRoughness": {"metallicFactor": 0.0, "roughnessFactor": 0.6},
        "normalTexture": {"scale": _NORMAL_SCALE},
        "occlusionTexture": {"strength": _OCCLUSION_STRENGTH},
    })
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal},
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
        "hasNormalTexture": False,
        "hasMetallicRoughnessTexture": False,
        "hasOcclusionTexture": False,
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
        features=["normalTexture.scale", "occlusionTexture.strength", "texture view without a texture"],
        spec_anchors=["additional-textures", "metallic-roughness-material"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="ScaledTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES,
            material=expected_material)]},
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
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

    The extension is declared as *used* rather than *required*: CNA does not implement unlit
    shading (`GLTF-215` only keeps such a material off the PBR path), and a file requiring it would
    be refused at validation before producing any buffer to compare.
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
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
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


FIXTURES = [mat_factor_only_gold, mat_emissive_strength, mat_vertex_color_pbr,
            mat_normal_occlusion_scale, mat_unlit, mat_authored_tangent]
