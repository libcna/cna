# SPDX-License-Identifier: MS-PL
"""Skinning fixtures -- owning group ``skinning`` (plan_gltf.md §15.4, §24.2).

Proves **D8**, the second independent collapse mechanism. ``BuildSkeleton`` walks parent links only
*within the skin's own joint set*, so any transform on an armature node above the joints is dropped
from the bind pose -- while the file-authored ``inverseBindMatrices``, which *do* include that
transform, are kept verbatim. The joint matrix is therefore left multiplied by the inverse of the
dropped ancestor transform.

Specification: §3.7.3 ``skins``, §3.7.3.2 ``joint-hierarchy``, §3.7.3.3 ``skinned-mesh-attributes``.
"""

from __future__ import annotations

from ..builder import FLOAT, TRIANGLES, UNSIGNED_BYTE, UNSIGNED_SHORT, GltfBuilder, pack
from ..manifest import (Defect, Fixture, l3_primitive, mat_identity, mat_translation, mat_mul,
                        world_positions)
from .common import TRIANGLE_INDICES, TRIANGLE_POSITIONS

#: The armature's own translation. 100 units so the resulting error is unmistakable at every layer.
_ARMATURE_TRANSLATION = [0.0, 100.0, 0.0]

#: Every vertex is bound entirely to joint 0, so the joint matrix *is* the skinning result and no
#: weight blending can mask an error in it.
_JOINTS = [(0, 0, 0, 0)] * 3
_WEIGHTS = [(1.0, 0.0, 0.0, 0.0)] * 3


def skin_armature_ancestor() -> Fixture:
    """f9 -- a joint under a translated armature node. Proves **D8**.

    ``Joint0`` has no local transform of its own, so its global transform is entirely inherited
    from ``Armature``. The authored inverse bind matrix is the true inverse of that global
    transform, which makes the correct joint matrix exactly the identity: a vertex bound to
    ``Joint0`` must not move at all. Any deviation from the identity is the dropped ancestor.

    The skinned mesh node deliberately sits at the scene root rather than under the armature, so
    this fixture isolates the ancestor-chain defect from the separate mesh-node-transform question
    that ``GLTF-247`` and ``GLTF-260`` own.
    """
    b = GltfBuilder("skin-armature-ancestor")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    joints = b.add_packed_accessor(usage="JOINTS_0", values=_JOINTS, accessor_type="VEC4",
                                   component_type=UNSIGNED_BYTE)
    weights = b.add_packed_accessor(usage="WEIGHTS_0", values=_WEIGHTS, accessor_type="VEC4")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)

    joint_global = mat_translation(_ARMATURE_TRANSLATION)
    inverse_bind = mat_translation([-c for c in _ARMATURE_TRANSLATION])
    ibm_offset = b.append_bytes(pack(inverse_bind, FLOAT), alignment=4)
    ibm = b.add_accessor(usage="inverseBindMatrices", component_type=FLOAT, accessor_type="MAT4",
                         count=1, expected=list(inverse_bind),
                         buffer_view=b.add_buffer_view(ibm_offset, 64))

    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "JOINTS_0": joints, "WEIGHTS_0": weights},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="SkinnedTri")

    joint_node = b.add_node(name="Joint0")
    armature_node = b.add_node(name="Armature", children=[joint_node],
                               translation=_ARMATURE_TRANSLATION)
    mesh_node = b.add_node(name="SkinnedMeshNode", mesh=mesh, skin=0)
    b.add_skin({"name": "Skin", "joints": [joint_node], "inverseBindMatrices": ibm})
    b.add_scene([armature_node, mesh_node], name="Scene")
    b.set_default_scene(0)

    joint_matrix = mat_mul(joint_global, inverse_bind)  # meshNodeWorld is the identity here
    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["skin"] = {
        "jointCount": 1,
        "meshNodeWorldColumnMajor": mat_identity(),
        "joints": [{
            "joint": 0,
            "node": joint_node,
            "nodeName": "Joint0",
            "parentJoint": -1,
            "jointGlobalColumnMajor": joint_global,
            "inverseBindMatrixColumnMajor": inverse_bind,
            "jointMatrixColumnMajor": joint_matrix,
        }],
        "skinnedPositions": [list(p) for p in TRIANGLE_POSITIONS],
        "note": "jointMatrix = inverse(globalTransform(meshNode)) * globalTransform(joint) * "
                "inverseBindMatrix. Here that is I * T(0,100,0) * T(0,-100,0) = identity, so the "
                "skinned positions equal the mesh-local positions exactly.",
    }
    return Fixture(
        id="skin-armature-ancestor", audit_fixture="f9", owning_group="skinning",
        description="A single joint whose global transform comes entirely from a translated "
                    "armature node above it, with a correctly authored inverse bind matrix. The "
                    "correct joint matrix is the identity; CNA produces translate(0,-100,0).",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["skin.joints", "skin.inverseBindMatrices", "armature ancestor above the joint set",
                  "JOINTS_0 / WEIGHTS_0"],
        spec_anchors=["skins", "joint-hierarchy", "skinned-mesh-attributes"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="SkinnedTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, joints=_JOINTS, weights=_WEIGHTS,
            indices=TRIANGLE_INDICES)]},
        l4=l4,
        defects=[Defect(
            id="D8", owner="GLTF-SKIN", first_divergent_layer="L4",
            summary="BuildSkeleton walks parent links only inside the skin's own joint set, so the "
                    "armature transform above the joints is dropped from the bind pose while the "
                    "authored inverseBindMatrices still contain it. The joint matrix ends up "
                    "multiplied by the inverse of the dropped transform.",
            owning_tasks=["GLTF-245", "GLTF-247", "GLTF-248", "GLTF-260"],
            divergent_fields=["skin"],
            current_actual={
                "boneCount": 1,
                "parentIndex": -1,
                "bindPoseLocalTranslation": [0.0, 0.0, 0.0],
                "inverseBindGlobalTranslation": [0.0, -100.0, 0.0],
                "bindPoseGlobalTranslation": [0.0, 0.0, 0.0],
                "skinTransformTranslation": [0.0, -100.0, 0.0],
                "note": "bindPoseGlobal * inverseBindGlobal = I * T(0,-100,0), so every skinned "
                        "vertex is displaced by -100 on Y. With a uniform scale on the armature "
                        "instead of a translation, the same mechanism multiplies every vertex by "
                        "the reciprocal of that scale -- the character collapses toward the origin.",
            },
        )],
    )


FIXTURES = [skin_armature_ancestor]
