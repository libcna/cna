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
from .common import TRIANGLE_INDICES, TRIANGLE_NORMALS, TRIANGLE_POSITIONS

#: The armature's own translation. 100 units so the resulting error is unmistakable at every layer.
_ARMATURE_TRANSLATION = [0.0, 100.0, 0.0]

#: Every vertex is bound entirely to joint 0, so the joint matrix *is* the skinning result and no
#: weight blending can mask an error in it.
_JOINTS = [(0, 0, 0, 0)] * 3
_WEIGHTS = [(1.0, 0.0, 0.0, 0.0)] * 3

#: The skinned mesh node's own translation, for the fixture that isolates the mesh-space
#: cancellation. Deliberately on a different axis from the armature's so a leak of either term is
#: attributable to that term alone.
_MESH_NODE_TRANSLATION = [0.0, 0.0, 50.0]


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
            summary="BuildSkeleton walked parent links only inside the skin's own joint set, so the "
                    "armature transform above the joints was dropped from the bind pose while the "
                    "authored inverseBindMatrices still contained it -- leaving every skinned "
                    "vertex multiplied by the inverse of what was lost. GLTF-245 walks the full "
                    "scene ancestry (skin.skeleton is a hint, never a traversal stop) and GLTF-247 "
                    "adds the inverse(globalTransform(meshNode)) term, both carried on each root "
                    "bone's parentWorldPrefix so animating a root joint cannot undo them.",
            owning_tasks=["GLTF-245", "GLTF-247", "GLTF-248", "GLTF-260"],
            closed_tasks=["GLTF-245", "GLTF-247", "GLTF-248", "GLTF-260"], status="fixed",
            divergent_fields=[],
            current_actual={
                "boneCount": 1,
                "parentIndex": -1,
                "jointMatrixTranslation": [0.0, 0.0, 0.0],
                "note": "The joint matrix is exactly the identity, as the specification requires "
                        "for a joint whose authored inverse bind matrix is the true inverse of its "
                        "global transform. Nothing is suppressed any more: GltfSkinSpaces asserts "
                        "the joint matrix and the resulting skinned position through the real "
                        "loader, so D8 reappearing fails an ordinary green test.",
            },
            prior_actual={
                "boneCount": 1,
                "parentIndex": -1,
                "bindPoseLocalTranslation": [0.0, 0.0, 0.0],
                "inverseBindGlobalTranslation": [0.0, -100.0, 0.0],
                "skinTransformTranslation": [0.0, -100.0, 0.0],
                "measuredOn": "fb3728267e8f2179d43b96357ff372ae712b7e7f",
                "note": "What the forensic audit measured before GLTF-245/247: the joint's bind "
                        "pose was its node-local transform (identity, the armature's [0,100,0] "
                        "dropped) while the authored inverse bind matrix still carried [0,-100,0], "
                        "so the joint matrix came out as translate(0,-100,0) -- a 100-unit "
                        "displacement of every skinned vertex.",
            },
        )],
    )


def skin_mesh_node_transform() -> Fixture:
    """A skinned mesh whose own node is transformed. Owns the second half of **GLTF-260**.

    glTF places a skinned mesh entirely through its joints, so the node that instantiates it
    contributes ``inverse(globalTransform(meshNode))`` to the joint matrix -- its transform is
    *cancelled*, never applied. Here ``Joint0`` sits at the scene root with an identity bind pose
    and an identity inverse bind matrix, so the whole joint matrix reduces to that cancellation:
    ``inverse(T(0,0,50))`` = ``T(0,0,-50)``.

    That makes this the fixture that separates the two ways of getting a skinned mesh wrong. If the
    cancellation is missing, the mesh sits 50 units too far along +Z. If the node's bone *also*
    transforms the mesh -- the double application Phase 5's real bone hierarchy makes newly possible
    -- the two cancel by accident and the mesh looks right for the wrong reason, which the world
    positions below detect because they are asserted against the joint matrix, not the eye.
    """
    b = GltfBuilder("skin-mesh-node-transform")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    joints = b.add_packed_accessor(usage="JOINTS_0", values=_JOINTS, accessor_type="VEC4",
                                   component_type=UNSIGNED_BYTE)
    weights = b.add_packed_accessor(usage="WEIGHTS_0", values=_WEIGHTS, accessor_type="VEC4")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)

    inverse_bind = mat_identity()
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
    mesh_node = b.add_node(name="SkinnedMeshNode", mesh=mesh, skin=0,
                           translation=_MESH_NODE_TRANSLATION)
    b.add_skin({"name": "Skin", "joints": [joint_node], "inverseBindMatrices": ibm})
    b.add_scene([joint_node, mesh_node], name="Scene")
    b.set_default_scene(0)

    mesh_node_world = mat_translation(_MESH_NODE_TRANSLATION)
    mesh_node_inverse = mat_translation([-c for c in _MESH_NODE_TRANSLATION])
    joint_global = mat_identity()
    joint_matrix = mat_mul(mesh_node_inverse, mat_mul(joint_global, inverse_bind))
    skinned = [[p[0], p[1], p[2] - _MESH_NODE_TRANSLATION[2]] for p in TRIANGLE_POSITIONS]

    # The mesh node's transform is cancelled, so the mesh's own world placement is NOT its node's.
    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["skin"] = {
        "jointCount": 1,
        "meshNodeWorldColumnMajor": mesh_node_world,
        "joints": [{
            "joint": 0,
            "node": joint_node,
            "nodeName": "Joint0",
            "parentJoint": -1,
            "jointGlobalColumnMajor": joint_global,
            "inverseBindMatrixColumnMajor": inverse_bind,
            "jointMatrixColumnMajor": joint_matrix,
        }],
        "skinnedPositions": skinned,
        "note": "jointMatrix = inverse(globalTransform(meshNode)) * globalTransform(joint) * "
                "inverseBindMatrix = T(0,0,-50) * I * I. The cancellation must be applied exactly "
                "once: omitting it leaves the mesh 50 units along +Z, and applying the mesh node's "
                "bone as well cancels it a second time and leaves the mesh 50 units along -Z.",
    }
    return Fixture(
        id="skin-mesh-node-transform", owning_group="skinning",
        description="A skinned mesh whose instancing node carries translation [0,0,50], with an "
                    "identity joint and an identity inverse bind matrix. The joint matrix is "
                    "exactly the mesh node's inverse, so this isolates the mesh-space cancellation "
                    "from the joint ancestry that skin-armature-ancestor covers.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        referencing_groups=["transforms"],
        features=["skinned mesh node transform", "mesh-space cancellation",
                  "skin.inverseBindMatrices", "JOINTS_0 / WEIGHTS_0"],
        spec_anchors=["skins", "joint-hierarchy", "skinned-mesh-attributes"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="SkinnedTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, joints=_JOINTS, weights=_WEIGHTS,
            indices=TRIANGLE_INDICES)]},
        l4=l4,
    )


#: The static prop's own translation. On -Z so it cannot be confused with either the armature's or
#: the skinned mesh node's own axis in any other skinning fixture.
_STATIC_NODE_TRANSLATION = [0.0, 0.0, -20.0]


def skin_plus_static_mesh() -> Fixture:
    """A skinned mesh and an ordinary static mesh in one file. Owns **GLTF-137**.

    ``CollectMeshGroups`` makes one group per distinct skin plus one for the unskinned meshes, and
    the runtime loader took ``groups.front()`` -- so a file with a character *and* a prop imported
    whichever of the two happened to own the first mesh node and dropped the other without a word.
    This is the smallest file that exhibits it: the skinned node comes first, so the static prop is
    what used to disappear.

    Both meshes are deliberately trivial and exactly placed. The skinned one has a single joint at
    the scene root with an identity inverse bind matrix, so its joint matrix is the identity and its
    world positions equal its mesh-local ones -- nothing about skinning is under test here. The
    static one is translated on -Z, so its presence or absence is unmistakable at L4.
    """
    b = GltfBuilder("skin-plus-static-mesh")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    joints = b.add_packed_accessor(usage="JOINTS_0", values=_JOINTS, accessor_type="VEC4",
                                   component_type=UNSIGNED_BYTE)
    weights = b.add_packed_accessor(usage="WEIGHTS_0", values=_WEIGHTS, accessor_type="VEC4")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)

    inverse_bind = mat_identity()
    ibm_offset = b.append_bytes(pack(inverse_bind, FLOAT), alignment=4)
    ibm = b.add_accessor(usage="inverseBindMatrices", component_type=FLOAT, accessor_type="MAT4",
                         count=1, expected=list(inverse_bind),
                         buffer_view=b.add_buffer_view(ibm_offset, 64))

    skinned_mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "JOINTS_0": joints, "WEIGHTS_0": weights},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="SkinnedTri")

    static_position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                            accessor_type="VEC3", with_bounds=True)
    static_normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS,
                                          accessor_type="VEC3")
    static_indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                           accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    static_mesh = b.add_mesh([{
        "attributes": {"POSITION": static_position, "NORMAL": static_normal},
        "indices": static_indices,
        "mode": TRIANGLES,
    }], name="StaticTri")

    joint_node = b.add_node(name="Joint0")
    # The skinned node is authored BEFORE the static one, because CollectMeshGroups orders groups by
    # first-referencing node: that puts the skin's group at index 0, which is exactly the ordering
    # under which groups.front() lost the prop.
    skinned_node = b.add_node(name="SkinnedMeshNode", mesh=skinned_mesh, skin=0)
    static_node = b.add_node(name="StaticMeshNode", mesh=static_mesh,
                             translation=_STATIC_NODE_TRANSLATION)
    b.add_skin({"name": "Skin", "joints": [joint_node], "inverseBindMatrices": ibm})
    b.add_scene([joint_node, skinned_node, static_node], name="Scene")
    b.set_default_scene(0)

    l4 = world_positions(b, {skinned_mesh: list(TRIANGLE_POSITIONS),
                             static_mesh: list(TRIANGLE_POSITIONS)})
    l4["skin"] = {
        "jointCount": 1,
        "meshNodeWorldColumnMajor": mat_identity(),
        "joints": [{
            "joint": 0,
            "node": joint_node,
            "nodeName": "Joint0",
            "parentJoint": -1,
            "jointGlobalColumnMajor": mat_identity(),
            "inverseBindMatrixColumnMajor": inverse_bind,
            "jointMatrixColumnMajor": mat_identity(),
        }],
        "skinnedPositions": [list(p) for p in TRIANGLE_POSITIONS],
        "note": "Every term of the joint matrix is the identity here on purpose: this fixture is "
                "about which mesh GROUPS survive import, not about skinning arithmetic, so any "
                "divergence it reports is attributable to the group selection alone.",
    }
    l4["groups"] = {
        "count": 2,
        "expectation": "Both groups must be imported. The skin's group owns SkinnedTri and the "
                       "unskinned group owns StaticTri; a loader that keeps only the first drops "
                       "StaticTri entirely, including its node placement at Z = -20.",
    }

    return Fixture(
        id="skin-plus-static-mesh", audit_fixture=None, owning_group="skinning",
        description="One skinned mesh and one ordinary static mesh in the same file -- two mesh "
                    "groups. The runtime loader imported groups.front() and dropped the rest in "
                    "silence, so the static prop never reached the model at all.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["two mesh groups", "skinned and unskinned mesh in one file",
                  "skin.joints", "skin.inverseBindMatrices"],
        spec_anchors=["skins", "nodes-and-hierarchy", "instantiation"],
        l3={"primitives": [
            l3_primitive(mesh=skinned_mesh, mesh_name="SkinnedTri", primitive=0, mode=TRIANGLES,
                         positions=TRIANGLE_POSITIONS, joints=_JOINTS, weights=_WEIGHTS,
                         indices=TRIANGLE_INDICES),
            l3_primitive(mesh=static_mesh, mesh_name="StaticTri", primitive=0, mode=TRIANGLES,
                         positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS,
                         indices=TRIANGLE_INDICES),
        ]},
        l4=l4,
    )


FIXTURES = [skin_armature_ancestor, skin_mesh_node_transform, skin_plus_static_mesh]
