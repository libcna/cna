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
from ..l5 import unsupported as l5_unsupported
from ..manifest import (Defect, Fixture, l3_primitive, mat_identity, mat_scale, mat_translation,
                        mat_mul, world_positions)
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


#: The parent above the skinned mesh node, for the fixture that separates a node's OWN transform
#: from its composed one. On +X so it cannot be confused with the mesh node's own +Z.
_MESH_PARENT_TRANSLATION = [30.0, 0.0, 0.0]


def skin_mesh_node_parent_transform() -> Fixture:
    """A skinned mesh node under a transformed **parent** -- `GLTF-270`.

    `skin-mesh-node-transform` proves the cancellation uses the mesh node's transform. This proves
    it uses the node's **composed** one: §3.7.3 says a skinned mesh is placed by its joints alone,
    which means the whole world transform of the instancing node is cancelled, ancestors included.

    An implementation that cancelled only the node's *local* transform passes the earlier fixture
    exactly and fails here by the parent's 30 units -- and it fails in the direction that looks
    like a rigging problem, because the mesh lands somewhere plausible and the skeleton does not.
    """
    b = GltfBuilder("skin-mesh-node-parent-transform")
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
    parent_node = b.add_node(name="MeshParent", children=[mesh_node],
                             translation=_MESH_PARENT_TRANSLATION)
    b.add_skin({"name": "Skin", "joints": [joint_node], "inverseBindMatrices": ibm})
    b.add_scene([joint_node, parent_node], name="Scene")
    b.set_default_scene(0)

    composed = [_MESH_PARENT_TRANSLATION[i] + _MESH_NODE_TRANSLATION[i] for i in range(3)]
    mesh_node_world = mat_translation(composed)
    mesh_node_inverse = mat_translation([-c for c in composed])
    joint_global = mat_identity()
    joint_matrix = mat_mul(mesh_node_inverse, mat_mul(joint_global, inverse_bind))
    skinned = [[p[0] - composed[0], p[1] - composed[1], p[2] - composed[2]]
               for p in TRIANGLE_POSITIONS]

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
        "note": "globalTransform(meshNode) is T(30,0,0) * T(0,0,50) -- the parent's transform is "
                "part of it. Cancelling only the node's own local transform leaves the mesh 30 "
                "units along +X, which looks like a rigging problem rather than a space error.",
    }
    return Fixture(
        id="skin-mesh-node-parent-transform", owning_group="skinning",
        description="A skinned mesh node translated [0,0,50] under a parent translated [30,0,0]. "
                    "The cancellation term is the node's COMPOSED world transform, so an "
                    "implementation that used only its local one is off by the parent's 30 units.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        referencing_groups=["transforms"],
        features=["skinned mesh node transform", "mesh-space cancellation",
                  "transformed ancestor above the mesh node", "JOINTS_0 / WEIGHTS_0"],
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


#: `skin-skeleton-hint`'s chain. Two transforms, on opposite sides of the declared root, so a walk
#: that stopped at `skin.skeleton` would keep the inner one and lose the outer one -- and lose it
#: by 100 units, the same magnitude D8 displaced every vertex by.
_HINT_OUTER_TRANSLATION = [0.0, 100.0, 0.0]
_HINT_DECLARED_TRANSLATION = [0.0, 0.0, 7.0]
_HINT_MID_TRANSLATION = [3.0, 0.0, 0.0]


def skin_skeleton_hint() -> Fixture:
    """``skin.skeleton`` declared above a transform it must not swallow. Owns **GLTF-249**.

    §15.1.1 states the rule twice over, and this fixture is built so both halves can fail
    separately:

    * **A transform-bearing ancestor sits ABOVE the declared root.** ``Outer`` carries
      ``T(0,100,0)`` and is the parent of ``Declared``. An implementation that walked joint
      ancestry only up to ``skin.skeleton`` would keep ``Declared``'s and ``Mid``'s transforms and
      drop ``Outer``'s -- recreating D8 exactly, down to the 100-unit displacement.
    * **The declared root is not the joints' natural common ancestor.** Both joints hang off
      ``Mid``; ``skin.skeleton`` names ``Declared``, one level higher. So "the declared root" and
      "the node a reader would have inferred" are different nodes, and code that quietly
      substitutes one for the other is visible.

    The inverse bind matrices are the true inverses of the joints' full global transforms, which
    makes every joint matrix exactly the identity: a correct reader moves no vertex at all. Joint 1
    carries no weight and exists solely to make ``Mid`` the common ancestor.
    """
    b = GltfBuilder("skin-skeleton-hint")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    joints = b.add_packed_accessor(usage="JOINTS_0", values=_JOINTS, accessor_type="VEC4",
                                   component_type=UNSIGNED_BYTE)
    weights = b.add_packed_accessor(usage="WEIGHTS_0", values=_WEIGHTS, accessor_type="VEC4")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)

    # globalTransform(joint) = Outer * Declared * Mid, for both joints (neither has a local
    # transform of its own), so the true inverse bind matrix is that product's inverse.
    joint_global_translation = [
        _HINT_OUTER_TRANSLATION[i] + _HINT_DECLARED_TRANSLATION[i] + _HINT_MID_TRANSLATION[i]
        for i in range(3)]
    joint_global = mat_translation(joint_global_translation)
    inverse_bind = mat_translation([-c for c in joint_global_translation])
    ibm_offset = b.append_bytes(pack(list(inverse_bind) + list(inverse_bind), FLOAT), alignment=4)
    ibm = b.add_accessor(usage="inverseBindMatrices", component_type=FLOAT, accessor_type="MAT4",
                         count=2, expected=list(inverse_bind) + list(inverse_bind),
                         buffer_view=b.add_buffer_view(ibm_offset, 128))

    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "JOINTS_0": joints, "WEIGHTS_0": weights},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="SkinnedTri")

    joint0 = b.add_node(name="Joint0")
    joint1 = b.add_node(name="Joint1")
    mid_node = b.add_node(name="Mid", children=[joint0, joint1],
                          translation=_HINT_MID_TRANSLATION)
    declared_node = b.add_node(name="DeclaredRoot", children=[mid_node],
                               translation=_HINT_DECLARED_TRANSLATION)
    outer_node = b.add_node(name="Outer", children=[declared_node],
                            translation=_HINT_OUTER_TRANSLATION)
    mesh_node = b.add_node(name="SkinnedMeshNode", mesh=mesh, skin=0)
    b.add_skin({"name": "Skin", "joints": [joint0, joint1], "skeleton": declared_node,
                "inverseBindMatrices": ibm})
    b.add_scene([outer_node, mesh_node], name="Scene")
    b.set_default_scene(0)

    joint_matrix = mat_mul(joint_global, inverse_bind)  # meshNodeWorld is the identity here
    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["skin"] = {
        "jointCount": 2,
        "meshNodeWorldColumnMajor": mat_identity(),
        "declaredSkeletonRoot": {
            "node": declared_node,
            "nodeName": "DeclaredRoot",
            "isTheJointsCommonAncestor": False,
            "commonAncestorNodeName": "Mid",
            "rule": "A hint only. It names the rig's semantic root and must never bound the "
                    "ancestry walk: Outer sits ABOVE it and its T(0,100,0) still contributes to "
                    "globalTransform(joint). Stopping here would reproduce D8 exactly.",
        },
        "joints": [
            {
                "joint": 0,
                "node": joint0,
                "nodeName": "Joint0",
                "parentJoint": -1,
                "jointGlobalColumnMajor": joint_global,
                "inverseBindMatrixColumnMajor": inverse_bind,
                "jointMatrixColumnMajor": joint_matrix,
            },
            {
                "joint": 1,
                "node": joint1,
                "nodeName": "Joint1",
                "parentJoint": -1,
                "jointGlobalColumnMajor": joint_global,
                "inverseBindMatrixColumnMajor": inverse_bind,
                "jointMatrixColumnMajor": joint_matrix,
            },
        ],
        "skinnedPositions": [list(p) for p in TRIANGLE_POSITIONS],
        "truncatedAtDeclaredRoot": {
            "jointMatrixColumnMajor": mat_mul(
                mat_mul(mat_translation(_HINT_DECLARED_TRANSLATION),
                        mat_translation(_HINT_MID_TRANSLATION)),
                inverse_bind),
            "skinnedPositions": [[p[0], p[1] - _HINT_OUTER_TRANSLATION[1], p[2]]
                                 for p in TRIANGLE_POSITIONS],
            "note": "What a reader that stopped its ancestry walk at skin.skeleton would produce: "
                    "Outer's T(0,100,0) dropped, so every skinned vertex sits 100 units low in Y. "
                    "Stated so the WRONG answer is a named value a test can assert against, not "
                    "merely something that differs from the right one.",
        },
        "note": "Every joint matrix is exactly the identity, because each inverse bind matrix is "
                "the true inverse of its joint's FULL global transform. A correct reader moves no "
                "vertex; any displacement is a dropped ancestor.",
    }
    return Fixture(
        id="skin-skeleton-hint", audit_fixture=None, owning_group="skinning",
        description="A skin whose declared skeleton root has a transform-bearing ancestor above it "
                    "and is not the joints' natural common ancestor. skin.skeleton must be honoured "
                    "as a semantic hint and must never truncate the ancestry walk -- doing so drops "
                    "Outer's T(0,100,0) and reproduces D8.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["skin.skeleton", "declared root below a transform-bearing ancestor",
                  "declared root above the joints' common ancestor", "two joints"],
        spec_anchors=["skins", "joint-hierarchy", "skinned-mesh-attributes"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="SkinnedTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, joints=_JOINTS, weights=_WEIGHTS,
            indices=TRIANGLE_INDICES)]},
        l4=l4,
    )


#: `skin-unnormalized`'s weights. The first vertex sums to 0.75, which is H12's exact shape: the
#: skin equation applies 0.75 of the transform, dragging the vertex a quarter of the way toward the
#: joint's origin. The second is within float error of 1 and must NOT be counted as malformed. The
#: third sums to zero -- an unweighted vertex, where 0/0 is not a normalisation.
_UNNORMALIZED_WEIGHTS = [
    (0.5, 0.25, 0.0, 0.0),
    (0.60001, 0.39999, 0.0, 0.0),
    (0.0, 0.0, 0.0, 0.0),
]
_TWO_JOINT_INDICES = [(0, 1, 0, 0)] * 3

#: The same weights after CNA's documented policy: vertex 0 renormalised, vertex 1 left alone
#: (within float error of 1), vertex 2 left alone (zero sum -- 0/0 is not a normalisation).
_RENORMALIZED_WEIGHTS = [
    (2.0 / 3.0, 1.0 / 3.0, 0.0, 0.0),
    _UNNORMALIZED_WEIGHTS[1],
    _UNNORMALIZED_WEIGHTS[2],
]


def skin_unnormalized() -> Fixture:
    """Joint weights that do not sum to 1. Owns **GLTF-256**, the audit's **H12**.

    §3.7.3.3 requires a vertex's weights to sum to 1, but a file is not guaranteed to honour it and
    CNA never checked. The failure is not cosmetic: the skin equation is a weighted sum of joint
    matrices, so weights summing to 0.75 apply 0.75 of the vertex's transform -- which for a joint
    near the origin drags the vertex three-quarters of the way toward it. An independent collapse
    mechanism, wearing the same clothes as D8.

    Three vertices, three cases, so the policy can fail in three distinguishable ways: one clearly
    short (0.75), one within float error of 1 (must be left alone, or every quantised exporter's
    output is reported as broken), and one summing to zero (unweighted -- ``0/0`` is not a
    normalisation, so it must be left alone too rather than assigned to an arbitrary joint).
    """
    b = GltfBuilder("skin-unnormalized")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    joints = b.add_packed_accessor(usage="JOINTS_0", values=_TWO_JOINT_INDICES,
                                   accessor_type="VEC4", component_type=UNSIGNED_BYTE)
    weights = b.add_packed_accessor(usage="WEIGHTS_0", values=_UNNORMALIZED_WEIGHTS,
                                    accessor_type="VEC4")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)

    inverse_bind = mat_identity()
    ibm_offset = b.append_bytes(pack(list(inverse_bind) + list(inverse_bind), FLOAT), alignment=4)
    ibm = b.add_accessor(usage="inverseBindMatrices", component_type=FLOAT, accessor_type="MAT4",
                         count=2, expected=list(inverse_bind) + list(inverse_bind),
                         buffer_view=b.add_buffer_view(ibm_offset, 128))

    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "JOINTS_0": joints, "WEIGHTS_0": weights},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="SkinnedTri")

    joint0 = b.add_node(name="Joint0")
    joint1 = b.add_node(name="Joint1")
    mesh_node = b.add_node(name="SkinnedMeshNode", mesh=mesh, skin=0)
    joint_root = b.add_node(name="JointRoot", children=[joint0, joint1])
    b.add_skin({"name": "Skin", "joints": [joint0, joint1], "inverseBindMatrices": ibm})
    b.add_scene([joint_root, mesh_node], name="Scene")
    b.set_default_scene(0)

    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["skin"] = {
        "jointCount": 2,
        "meshNodeWorldColumnMajor": mat_identity(),
        "policy": "RENORMALISE, and report. A slightly-off sum is what quantised exporters "
                  "routinely emit, so refusing those files would be useless; but a sum far from 1 "
                  "is a broken file, and the deviation is what tells the two apart.",
        "weightsAsAuthored": [list(w) for w in _UNNORMALIZED_WEIGHTS],
        "weightsAfterPolicy": [list(w) for w in _RENORMALIZED_WEIGHTS],
        "renormalisedVertexCount": 1,
        "zeroWeightVertexCount": 1,
        "toleranceRule": "A sum within 1e-4 of 1 is float error, not a malformed file, and is left "
                         "untouched and uncounted -- vertex 1 (sum 1.0) is exactly that case.",
        "zeroWeightRule": "A vertex whose weights sum to zero is unweighted. 0/0 is not a "
                          "normalisation, so it is counted and left alone rather than assigned to "
                          "an arbitrary joint.",
    }
    return Fixture(
        id="skin-unnormalized", audit_fixture=None, owning_group="skinning",
        validator_expected_errors=["ACCESSOR_WEIGHTS_NON_NORMALIZED"],
        validator_exception_reason="CNA's explicit repair-and-report policy is exercised on "
                                   "malformed non-normalized and all-zero weights; this fixture "
                                   "pre-dates the bad-* naming rule.",
        description="Three vertices with weights summing to 0.75, to 1 within float error, and to "
                    "zero. The first is H12 -- a weighted sum applying three-quarters of the "
                    "transform drags the vertex toward the joint's origin -- and the other two are "
                    "the cases a renormalisation policy must NOT touch.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        features=["unnormalized WEIGHTS_0", "zero-weight vertex", "renormalisation policy"],
        spec_anchors=["skins", "skinned-mesh-attributes"],
        # L2 holds the AUTHORED weights (that is what the accessor decodes to); L3 holds the
        # renormalised ones, because L3 is what the importer understood the mesh to be after its
        # own documented policy ran. Stating the authored values only here would make the two
        # layers contradict each other.
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="SkinnedTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, joints=_TWO_JOINT_INDICES,
            weights=_RENORMALIZED_WEIGHTS, indices=TRIANGLE_INDICES)]},
        l4=l4,
    )


def skin_73_joints() -> Fixture:
    """A rig one joint past the 72-entry GPU palette. Owns **GLTF-261**, the audit's **H6**.

    The palette is ``MaxBones`` (72) ``mat4`` values -- a real XNA constant that every renderer's
    uniform array is sized by, so raising it is not a local change. The file is therefore refused,
    and refusing rather than truncating is the whole point: truncating leaves the joints past the
    limit at the identity, so every vertex bound to them collapses toward the origin.

    Exactly 73 joints, so the fixture sits one past the boundary rather than far beyond it -- an
    off-by-one in the check is as much a failure as no check at all.
    """
    joint_count = 73
    b = GltfBuilder("skin-73-joints")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    # Every vertex is bound to the LAST joint, the one a truncating reader would drop.
    joints = b.add_packed_accessor(usage="JOINTS_0",
                                   values=[(joint_count - 1, 0, 0, 0)] * 3,
                                   accessor_type="VEC4", component_type=UNSIGNED_BYTE)
    weights = b.add_packed_accessor(usage="WEIGHTS_0", values=_WEIGHTS, accessor_type="VEC4")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)

    identity = mat_identity()
    ibm_offset = b.append_bytes(pack(list(identity) * joint_count, FLOAT), alignment=4)
    ibm = b.add_accessor(usage="inverseBindMatrices", component_type=FLOAT, accessor_type="MAT4",
                         count=joint_count, expected=list(identity) * joint_count,
                         buffer_view=b.add_buffer_view(ibm_offset, 64 * joint_count))

    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "JOINTS_0": joints, "WEIGHTS_0": weights},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="SkinnedTri")

    joint_nodes = [b.add_node(name="Joint" + str(i)) for i in range(joint_count)]
    mesh_node = b.add_node(name="SkinnedMeshNode", mesh=mesh, skin=0)
    b.add_skin({"name": "BigRig", "joints": joint_nodes, "inverseBindMatrices": ibm})
    b.add_scene(joint_nodes + [mesh_node], name="Scene")
    b.set_default_scene(0)

    return Fixture(
        id="skin-73-joints", audit_fixture=None, owning_group="skinning",
        size_exemption="73 joints require 73 nodes and 73 inverse bind matrices -- 1168 floats of "
                       "authored data before anything else. The fixture exists to sit exactly one "
                       "past the 72-entry palette, so it cannot be made smaller without ceasing to "
                       "test the boundary at all.",
        description="A skin with 73 joints, one past the 72-entry GPU bone palette, with every "
                    "vertex bound to the last one. Refused rather than truncated: truncating "
                    "leaves those joints at the identity and collapses their vertices toward the "
                    "origin.",
        builder=b, validated_layers=["L1", "L2"],
        features=["skin.joints beyond MaxBones", "palette limit", "import rejection"],
        spec_anchors=["skins", "joint-hierarchy"],
        l3={"primitives": []},
        l4={},
        l5=l5_unsupported("The skin is refused at extraction, so no buffers are produced.",
                          ["GLTF-261"]),
        rejection={
            "stage": "extraction",
            "task": "GLTF-261",
            "errorContains": ["73 joints", "72"],
            "note": "Refused at EXTRACTION, not validation: the file is perfectly well-formed "
                    "glTF and cgltf_validate has nothing to object to. The limit is CNA's own GPU "
                    "palette size, which is why the diagnostic names both numbers rather than "
                    "citing the specification.",
        },
    )


#: `skin-eight-influences`' two influence sets. Set 0 keeps four joints and set 1 adds four more;
#: each vertex sums to exactly 1 ACROSS BOTH sets, which is what §3.7.3.3 actually requires.
#: The three vertices are chosen so the dropped share is unmistakable, borderline, and nil in turn:
#: vertex 0 loses 40% of its influence, vertex 1 loses 1% (exporter noise), vertex 2 loses nothing
#: because its set-1 weights are all zero.
_EIGHT_JOINTS_SET0 = [(0, 1, 2, 3), (0, 1, 2, 3), (0, 1, 2, 3)]
_EIGHT_JOINTS_SET1 = [(4, 5, 6, 7), (4, 5, 6, 7), (4, 5, 6, 7)]
_EIGHT_WEIGHTS_SET0 = [(0.3, 0.2, 0.05, 0.05), (0.6, 0.2, 0.1, 0.09), (0.5, 0.3, 0.15, 0.05)]
_EIGHT_WEIGHTS_SET1 = [(0.2, 0.1, 0.05, 0.05), (0.01, 0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 0.0)]
#: What CNA imports: set 0 only, renormalised to sum to 1 by GLTF-256. Vertex 0's authored set-0
#: weights sum to 0.6, so each is divided by 0.6; vertex 1's sum to 0.99; vertex 2's already sum to
#: 1 and are left exactly alone.
_EIGHT_WEIGHTS_IMPORTED = [
    (0.3 / 0.6, 0.2 / 0.6, 0.05 / 0.6, 0.05 / 0.6),
    (0.6 / 0.99, 0.2 / 0.99, 0.1 / 0.99, 0.09 / 0.99),
    (0.5, 0.3, 0.15, 0.05),
]


def skin_eight_influences() -> Fixture:
    """Two `JOINTS_n`/`WEIGHTS_n` sets — eight influences per vertex. Owns **GLTF-095**/**GLTF-257**.

    glTF puts no limit on influence sets; XNA's ``BlendIndices``/``BlendWeight`` carry exactly four.
    Every set past the first is therefore dropped, and this fixture is what makes the drop
    *measurable* rather than merely known: each vertex's weights sum to 1 **across both sets**, so
    the share lost is exactly the set-1 sum and is different for all three vertices.

    The interaction with `GLTF-256` is the part worth pinning. Truncation alone would leave vertex 0
    with weights summing to 0.6, which is `H12` — three-fifths of the transform, dragging the vertex
    toward the origin. Renormalisation of the retained four is what turns that into a *coarser*
    skin instead of a collapsed one, and the L3 expectation states the renormalised values so a
    regression in either half fails here.
    """
    b = GltfBuilder("skin-eight-influences")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    joints0 = b.add_packed_accessor(usage="JOINTS_0", values=_EIGHT_JOINTS_SET0,
                                    accessor_type="VEC4", component_type=UNSIGNED_BYTE)
    weights0 = b.add_packed_accessor(usage="WEIGHTS_0", values=_EIGHT_WEIGHTS_SET0,
                                     accessor_type="VEC4")
    joints1 = b.add_packed_accessor(usage="JOINTS_1", values=_EIGHT_JOINTS_SET1,
                                    accessor_type="VEC4", component_type=UNSIGNED_BYTE)
    weights1 = b.add_packed_accessor(usage="WEIGHTS_1", values=_EIGHT_WEIGHTS_SET1,
                                     accessor_type="VEC4")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)

    inverse_bind = mat_identity()
    ibm_offset = b.append_bytes(pack(list(inverse_bind) * 8, FLOAT), alignment=4)
    ibm = b.add_accessor(usage="inverseBindMatrices", component_type=FLOAT, accessor_type="MAT4",
                         count=8, expected=list(inverse_bind) * 8,
                         buffer_view=b.add_buffer_view(ibm_offset, 64 * 8))

    mesh = b.add_mesh([{
        "attributes": {"POSITION": position,
                       "JOINTS_0": joints0, "WEIGHTS_0": weights0,
                       "JOINTS_1": joints1, "WEIGHTS_1": weights1},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="EightInfluenceTri")

    joint_nodes = [b.add_node(name=f"Joint{i}") for i in range(8)]
    mesh_node = b.add_node(name="SkinnedMeshNode", mesh=mesh, skin=0)
    joint_root = b.add_node(name="JointRoot", children=joint_nodes)
    b.add_skin({"name": "Skin", "joints": joint_nodes, "inverseBindMatrices": ibm})
    b.add_scene([joint_root, mesh_node], name="Scene")
    b.set_default_scene(0)

    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["skin"] = {
        "jointCount": 8,
        "meshNodeWorldColumnMajor": mat_identity(),
        "policy": "IMPORT SET 0, RENORMALISE IT, AND REPORT. XNA's BlendIndices/BlendWeight carry "
                  "exactly four influences and no CNA vertex layout or shader has room for more, "
                  "so a second set cannot be carried -- but it must not be dropped in silence, and "
                  "the share lost is what says whether the loss matters.",
        "influenceSets": 2,
        "extraInfluenceSets": 1,
        "weightsAsAuthoredSet0": [list(w) for w in _EIGHT_WEIGHTS_SET0],
        "weightsAsAuthoredSet1": [list(w) for w in _EIGHT_WEIGHTS_SET1],
        "weightsAfterPolicy": [list(w) for w in _EIGHT_WEIGHTS_IMPORTED],
        "droppedInfluencePerVertex": [0.4, 0.01, 0.0],
        "worstDroppedInfluence": 0.4,
        "renormalisationRule": "Measured BEFORE renormalisation, because that is the number that "
                               "distinguishes exporter noise from a different pose. Renormalising "
                               "the retained four is then what makes the result a coarser skin "
                               "rather than a collapsed one: without it vertex 0's weights would "
                               "sum to 0.6 and drag the vertex toward the origin, which is H12.",
    }
    return Fixture(
        id="skin-eight-influences", audit_fixture=None, owning_group="skinning",
        description="Two influence sets, eight joints per vertex, each vertex summing to 1 across "
                    "both. The dropped share differs per vertex (40%, 1%, none) so a report that "
                    "merely counted sets could not pass. Owns the decision that a second set is "
                    "reported rather than carried, and pins its interaction with GLTF-256's "
                    "renormalisation.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        features=["JOINTS_1/WEIGHTS_1", "eight influences per vertex",
                  "influence-set truncation", "renormalisation after truncation"],
        spec_anchors=["skins", "skinned-mesh-attributes"],
        # L2 holds every accessor as authored, including set 1 -- it decodes perfectly well and
        # saying otherwise would be false. L3 holds what the importer understood the mesh to be
        # after its documented policy ran, which is set 0 renormalised.
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="EightInfluenceTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, joints=_EIGHT_JOINTS_SET0,
            weights=_EIGHT_WEIGHTS_IMPORTED, indices=TRIANGLE_INDICES)]},
        l4=l4,
    )


#: A UV set and a vertex colour for the two stride fixtures below, chosen so every component is
#: distinct: a mis-offset TextureCoordinate or Color slot then produces visibly wrong bytes rather
#: than three copies of the same default.
_STRIDE_TEXCOORDS = [(0.0, 0.0), (0.75, 0.125), (0.25, 0.875)]
_STRIDE_COLORS = [(1.0, 0.0, 0.25, 1.0), (0.0, 0.5, 1.0, 0.75), (0.25, 0.75, 0.0, 0.5)]


def _single_joint_skin(builder: GltfBuilder) -> tuple[int, int]:
    """One identity-bound joint plus its inverse-bind accessor -- the minimum a skin needs.

    The stride fixtures below are about the vertex LAYOUT, not about skinning arithmetic, so their
    skin is deliberately the simplest one that makes `ExtractMesh` take the skinned branch.

    :param builder: the fixture's builder.
    :return: the joint node index and the inverseBindMatrices accessor index.
    """
    inverse_bind = mat_identity()
    ibm_offset = builder.append_bytes(pack(list(inverse_bind), FLOAT), alignment=4)
    ibm = builder.add_accessor(usage="inverseBindMatrices", component_type=FLOAT,
                               accessor_type="MAT4", count=1, expected=list(inverse_bind),
                               buffer_view=builder.add_buffer_view(ibm_offset, 64))
    return builder.add_node(name="Joint0"), ibm


def skin_unlit() -> Fixture:
    """A skinned primitive with a non-PBR material -- the fixture that reaches vertex stride 52.

    plan_gltf.md `GLTF-149`. A skinned primitive lands on stride 68 (the skinned PBR layout)
    whenever its material uses the metallic-roughness model, which is glTF's default in two
    separate ways -- so stride 52, the skinned layout with no tangent slot, was unreachable by
    every fixture in the corpus and had no golden bytes at all.
    """
    b = GltfBuilder("skin-unlit")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    texcoord = b.add_packed_accessor(usage="TEXCOORD_0", values=_STRIDE_TEXCOORDS,
                                     accessor_type="VEC2")
    joints = b.add_packed_accessor(usage="JOINTS_0", values=_JOINTS, accessor_type="VEC4",
                                   component_type=UNSIGNED_BYTE)
    weights = b.add_packed_accessor(usage="WEIGHTS_0", values=_WEIGHTS, accessor_type="VEC4")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    joint0, ibm = _single_joint_skin(b)
    material = b.add_material({
        "name": "UnlitSkin",
        "pbrMetallicRoughness": {"baseColorFactor": [0.9, 0.4, 0.1, 1.0]},
        "extensions": {"KHR_materials_unlit": {}},
    })
    b.declare_extensions(used=["KHR_materials_unlit"])
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal, "TEXCOORD_0": texcoord,
                       "JOINTS_0": joints, "WEIGHTS_0": weights},
        "indices": indices,
        "material": material,
        "mode": TRIANGLES,
    }], name="UnlitSkinnedTri")
    mesh_node = b.add_node(name="SkinnedMeshNode", mesh=mesh, skin=0)
    b.add_skin({"name": "Skin", "joints": [joint0], "inverseBindMatrices": ibm})
    b.add_scene([joint0, mesh_node], name="Scene")
    b.set_default_scene(0)

    expected_material = {
        "index": material,
        "name": "UnlitSkin",
        "model": "unlit",
        "baseColorFactor": [0.9, 0.4, 0.1, 1.0],
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
        id="skin-unlit", audit_fixture=None, owning_group="skinning",
        description="A skinned triangle whose material declares KHR_materials_unlit, so it imports "
                    "through SkinnedEffect on the stride-52 layout (Position+Normal+"
                    "TextureCoordinate+BlendWeight+BlendIndices) rather than the skinned PBR one.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4", "L5"],
        features=["KHR_materials_unlit", "JOINTS_0 / WEIGHTS_0", "vertex stride 52"],
        spec_anchors=["skins", "skinned-mesh-attributes"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="UnlitSkinnedTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, texcoords=_STRIDE_TEXCOORDS,
            joints=_JOINTS, weights=_WEIGHTS, indices=TRIANGLE_INDICES,
            material=expected_material)]},
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


def skin_vertex_color() -> Fixture:
    """A skinned, vertex-coloured primitive -- the fixture that reaches vertex stride 56.

    plan_gltf.md `GLTF-149`. Stride 56 is the skinned layout with a packed Color appended, and it
    is the one stride with no C++ stream struct behind it (`GLTF-156`): the importer writes those
    bytes itself. That makes a golden more valuable here than anywhere else, not less -- there is
    no `offsetof` to catch a mistake in it.
    """
    b = GltfBuilder("skin-vertex-color")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    texcoord = b.add_packed_accessor(usage="TEXCOORD_0", values=_STRIDE_TEXCOORDS,
                                     accessor_type="VEC2")
    color = b.add_packed_accessor(usage="COLOR_0", values=_STRIDE_COLORS, accessor_type="VEC4")
    joints = b.add_packed_accessor(usage="JOINTS_0", values=_JOINTS, accessor_type="VEC4",
                                   component_type=UNSIGNED_BYTE)
    weights = b.add_packed_accessor(usage="WEIGHTS_0", values=_WEIGHTS, accessor_type="VEC4")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    joint0, ibm = _single_joint_skin(b)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal, "TEXCOORD_0": texcoord,
                       "COLOR_0": color, "JOINTS_0": joints, "WEIGHTS_0": weights},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="ColoredSkinnedTri")
    mesh_node = b.add_node(name="SkinnedMeshNode", mesh=mesh, skin=0)
    b.add_skin({"name": "Skin", "joints": [joint0], "inverseBindMatrices": ibm})
    b.add_scene([joint0, mesh_node], name="Scene")
    b.set_default_scene(0)

    return Fixture(
        id="skin-vertex-color", audit_fixture=None, owning_group="skinning",
        description="A skinned triangle carrying COLOR_0, which selects the stride-56 layout: the "
                    "skinned stride-52 record with a packed Color appended. Every colour component "
                    "is distinct so a mis-offset Color slot cannot pass.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4", "L5"],
        features=["COLOR_0 on a skinned mesh", "JOINTS_0 / WEIGHTS_0", "vertex stride 56"],
        spec_anchors=["skins", "skinned-mesh-attributes"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="ColoredSkinnedTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, texcoords=_STRIDE_TEXCOORDS,
            colors=_STRIDE_COLORS, joints=_JOINTS, weights=_WEIGHTS, indices=TRIANGLE_INDICES)]},
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


# --- §15.4's arithmetic ladder ------------------------------------------------------------------
#
# The rungs above own a defect each. These own the skin equation itself: one property per fixture,
# each authored so that the plausible wrong answer is a DIFFERENT answer rather than a slightly
# worse one. `GltfSkinLadder` evaluates every one of them through the real loader's bone palette,
# so none of these expectations is prose.


def _skinned_triangle(b: GltfBuilder, *, joint_indices, joint_weights, inverse_binds,
                      joints_component_type: int = UNSIGNED_BYTE,
                      normals=None, mesh_name: str = "SkinnedTri") -> tuple[int, int | None]:
    """Packs the carrier triangle with a skin's attributes. Returns ``(mesh, ibmAccessor)``.

    ``inverse_binds`` is a list of 16-float column-major matrices, or ``None`` for a skin that
    authors no ``inverseBindMatrices`` at all -- which §3.7.3 says means identity for every joint.
    """
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    joints = b.add_packed_accessor(usage="JOINTS_0", values=joint_indices, accessor_type="VEC4",
                                   component_type=joints_component_type)
    weights = b.add_packed_accessor(usage="WEIGHTS_0", values=joint_weights, accessor_type="VEC4")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    attributes = {"POSITION": position, "JOINTS_0": joints, "WEIGHTS_0": weights}
    if normals is not None:
        attributes["NORMAL"] = b.add_packed_accessor(usage="NORMAL", values=normals,
                                                     accessor_type="VEC3")

    ibm = None
    if inverse_binds is not None:
        flat = [c for matrix in inverse_binds for c in matrix]
        ibm_offset = b.append_bytes(pack(flat, FLOAT), alignment=4)
        ibm = b.add_accessor(usage="inverseBindMatrices", component_type=FLOAT,
                             accessor_type="MAT4", count=len(inverse_binds), expected=flat,
                             buffer_view=b.add_buffer_view(ibm_offset, 64 * len(inverse_binds)))

    mesh = b.add_mesh([{"attributes": attributes, "indices": indices, "mode": TRIANGLES}],
                      name=mesh_name)
    return mesh, ibm


def _blend(joint_globals, joint_indices, joint_weights):
    """The skin equation of §3.7.3.3, evaluated on the carrier triangle.

    Identity inverse bind matrices and an identity mesh-node transform are assumed, which is true
    of every fixture below that uses it -- so ``jointMatrix[j]`` is ``joint_globals[j]`` and the
    expected value stays readable as arithmetic rather than as a matrix product.
    """
    out = []
    for vertex, (js, ws) in enumerate(zip(joint_indices, joint_weights)):
        p = TRIANGLE_POSITIONS[vertex]
        acc = [0.0, 0.0, 0.0]
        for j, w in zip(js, ws):
            if w == 0.0:
                continue
            m = joint_globals[j]
            for row in range(3):
                acc[row] += w * (m[0 * 4 + row] * p[0] + m[1 * 4 + row] * p[1] +
                                 m[2 * 4 + row] * p[2] + m[3 * 4 + row])
        out.append(acc)
    return out


def _skin_block(*, joint_nodes, joint_names, joint_globals, joint_indices, joint_weights,
                note: str, parents=None, inverse_binds=None):
    """The ``l4.skin`` expectation `GltfSkinLadder` reads: joint matrices and skinned positions."""
    binds = inverse_binds if inverse_binds is not None else [mat_identity()] * len(joint_nodes)
    return {
        "jointCount": len(joint_nodes),
        "meshNodeWorldColumnMajor": mat_identity(),
        "joints": [{
            "joint": i,
            "node": joint_nodes[i],
            "nodeName": joint_names[i],
            "parentJoint": -1 if parents is None else parents[i],
            "jointGlobalColumnMajor": joint_globals[i],
            "inverseBindMatrixColumnMajor": binds[i],
            "jointMatrixColumnMajor": mat_mul(joint_globals[i], binds[i]),
        } for i in range(len(joint_nodes))],
        "skinnedPositions": _blend(joint_globals, joint_indices, joint_weights),
        "note": note,
    }


#: Two joints, one at the origin and one 10 units up, and a vertex split evenly between them.
_TWO_WEIGHTED_JOINTS = [(0, 1, 0, 0)] * 3
_TWO_WEIGHTED_WEIGHTS = [(0.5, 0.5, 0.0, 0.0)] * 3


def skin_two_weighted() -> Fixture:
    """§15.4's blending rung: `w = [0.5, 0.5]` across two joints 10 units apart.

    The skin equation is a weighted sum of joint matrices, and this is the smallest asset on which
    a reader that is not summing produces a *different* answer rather than a slightly worse one:
    taking the highest weight lands the vertex at one joint or the other (a 5-unit error either
    way, and ties are decided by whichever comparison the reader happens to use), while summing
    correctly lands it exactly halfway.
    """
    b = GltfBuilder("skin-two-weighted")
    mesh, ibm = _skinned_triangle(b, joint_indices=_TWO_WEIGHTED_JOINTS,
                                  joint_weights=_TWO_WEIGHTED_WEIGHTS,
                                  inverse_binds=[mat_identity()] * 2)
    joint0 = b.add_node(name="Joint0")
    joint1 = b.add_node(name="Joint1", translation=[0.0, 10.0, 0.0])
    mesh_node = b.add_node(name="SkinnedMeshNode", mesh=mesh, skin=0)
    joint_root = b.add_node(name="JointRoot", children=[joint0, joint1])
    b.add_skin({"name": "Skin", "joints": [joint0, joint1], "inverseBindMatrices": ibm})
    b.add_scene([joint_root, mesh_node], name="Scene")
    b.set_default_scene(0)

    globals_ = [mat_identity(), mat_translation([0.0, 10.0, 0.0])]
    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["skin"] = _skin_block(
        joint_nodes=[joint0, joint1], joint_names=["Joint0", "Joint1"], joint_globals=globals_,
        joint_indices=_TWO_WEIGHTED_JOINTS, joint_weights=_TWO_WEIGHTED_WEIGHTS,
        note="Both inverse bind matrices are the identity, so each joint matrix is that joint's "
             "own global transform and the blend is readable directly: the vertex at (1,0,0) "
             "lands at (1,5,0), exactly halfway between the two joints' results.")
    return Fixture(
        id="skin-two-weighted", audit_fixture=None, owning_group="skinning",
        description="A vertex weighted 0.5/0.5 between a joint at the origin and one 10 units up. "
                    "Summing lands it at the midpoint; picking the dominant weight lands it on a "
                    "joint, 5 units away, with the tie broken by an implementation accident.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4", "L5"],
        features=["two-joint weight blending", "JOINTS_0 / WEIGHTS_0", "identity inverse binds"],
        spec_anchors=["skins", "skinned-mesh-attributes"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="SkinnedTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, joints=_TWO_WEIGHTED_JOINTS,
            weights=_TWO_WEIGHTED_WEIGHTS, indices=TRIANGLE_INDICES)]},
        l4=l4,
    )


#: Four influences, each on its own axis or magnitude, so no permutation of the weight vector
#: produces the same point: X is joint 0's alone, Y is joint 1's alone, and joints 2 and 3 pull
#: against each other along Z.
_FOUR_WEIGHTED_JOINTS = [(0, 1, 2, 3)] * 3
_FOUR_WEIGHTED_WEIGHTS = [(0.4, 0.3, 0.2, 0.1)] * 3


def skin_four_weighted() -> Fixture:
    """§15.4's four-influence rung. Every slot of `JOINTS_0`/`WEIGHTS_0` carries real work.

    XNA's ``BlendIndices``/``BlendWeight`` are four-wide and glTF's first influence set is too, so
    four is the case an importer must get exactly right -- and a reader that drops the fourth
    influence, or reads the four in the wrong order, is the failure this rung is shaped to catch.
    The joints are placed so that each weight is recoverable from the result on its own: X comes
    only from joint 0, Y only from joint 1, and joints 2 and 3 pull in opposite directions along Z
    so swapping their weights flips the sign of the Z displacement.
    """
    b = GltfBuilder("skin-four-weighted")
    mesh, ibm = _skinned_triangle(b, joint_indices=_FOUR_WEIGHTED_JOINTS,
                                  joint_weights=_FOUR_WEIGHTED_WEIGHTS,
                                  inverse_binds=[mat_identity()] * 4)
    translations = [[100.0, 0.0, 0.0], [0.0, 10.0, 0.0], [0.0, 0.0, 1.0], [0.0, 0.0, -1.0]]
    joint_nodes = [b.add_node(name="Joint" + str(i), translation=t)
                   for i, t in enumerate(translations)]
    mesh_node = b.add_node(name="SkinnedMeshNode", mesh=mesh, skin=0)
    joint_root = b.add_node(name="JointRoot", children=joint_nodes)
    b.add_skin({"name": "Skin", "joints": joint_nodes, "inverseBindMatrices": ibm})
    b.add_scene([joint_root, mesh_node], name="Scene")
    b.set_default_scene(0)

    globals_ = [mat_translation(t) for t in translations]
    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["skin"] = _skin_block(
        joint_nodes=joint_nodes, joint_names=["Joint" + str(i) for i in range(4)],
        joint_globals=globals_, joint_indices=_FOUR_WEIGHTED_JOINTS,
        joint_weights=_FOUR_WEIGHTED_WEIGHTS,
        note="The displacement is 0.4*(100,0,0) + 0.3*(0,10,0) + 0.2*(0,0,1) + 0.1*(0,0,-1) = "
             "(40,3,0.1). Dropping the fourth influence gives Z=0.2 rather than 0.1, and swapping "
             "the last two weights gives -0.1: both are visible without a tolerance argument.")
    return Fixture(
        id="skin-four-weighted", audit_fixture=None, owning_group="skinning",
        description="Four influences with weights 0.4/0.3/0.2/0.1 on joints placed so that each "
                    "weight is separately recoverable from the result. A dropped or reordered "
                    "influence moves the vertex to a different point, not merely a worse one.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4", "L5"],
        features=["four-influence weight blending", "JOINTS_0 / WEIGHTS_0"],
        spec_anchors=["skins", "skinned-mesh-attributes"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="SkinnedTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, joints=_FOUR_WEIGHTED_JOINTS,
            weights=_FOUR_WEIGHTED_WEIGHTS, indices=TRIANGLE_INDICES)]},
        l4=l4,
    )


def skin_no_ibm() -> Fixture:
    """§15.4's absent-`inverseBindMatrices` rung. §3.7.3: absent means identity, not absent joints.

    The property is undefined-by-omission, which is the kind an importer gets wrong in two opposite
    directions: skipping the joint entirely (the vertex never moves) or leaving the bind matrix
    uninitialised (it moves somewhere unrepeatable). The single joint is translated 7 units up and
    its inverse bind matrix is *not* authored, so the correct joint matrix is that translation
    itself -- and "the vertex did not move" is exactly the wrong answer, distinguishable from the
    right one at a glance.
    """
    b = GltfBuilder("skin-no-ibm")
    mesh, ibm = _skinned_triangle(b, joint_indices=_JOINTS, joint_weights=_WEIGHTS,
                                  inverse_binds=None)
    assert ibm is None
    joint0 = b.add_node(name="Joint0", translation=[0.0, 7.0, 0.0])
    mesh_node = b.add_node(name="SkinnedMeshNode", mesh=mesh, skin=0)
    # No `inverseBindMatrices` member at all -- the case under test.
    b.add_skin({"name": "Skin", "joints": [joint0]})
    b.add_scene([joint0, mesh_node], name="Scene")
    b.set_default_scene(0)

    globals_ = [mat_translation([0.0, 7.0, 0.0])]
    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["skin"] = _skin_block(
        joint_nodes=[joint0], joint_names=["Joint0"], joint_globals=globals_,
        joint_indices=_JOINTS, joint_weights=_WEIGHTS,
        note="§3.7.3: when inverseBindMatrices is undefined every matrix is the identity. The "
             "joint matrix is therefore the joint's global transform, T(0,7,0), and every skinned "
             "vertex rises by 7. A vertex that stays put means the joint was skipped for lacking "
             "a bind matrix.")
    return Fixture(
        id="skin-no-ibm", audit_fixture=None, owning_group="skinning",
        description="A skin with no `inverseBindMatrices` at all and a joint translated 7 units "
                    "up. §3.7.3 makes the missing matrices identity, so the skinned vertices rise "
                    "by 7; an importer that skips a joint without a bind matrix leaves them put.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4", "L5"],
        features=["skin without inverseBindMatrices", "identity bind pose by omission"],
        spec_anchors=["skins", "skinned-mesh-attributes"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="SkinnedTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, joints=_JOINTS, weights=_WEIGHTS,
            indices=TRIANGLE_INDICES)]},
        l4=l4,
    )


#: A normal deliberately off every axis, so a non-uniform scale changes its DIRECTION rather than
#: only its length -- the difference between the correct inverse-transpose and the joint matrix
#: applied directly is then a visible rotation instead of a renormalisation no-op.
_TILTED_NORMALS = [(0.0, 0.6, 0.8)] * 3


def skin_nonuniform_joint_scale() -> Fixture:
    """§15.4's non-uniform joint scale rung: positions **and** what happens to normals.

    A joint scaled `[1,2,1]` doubles Y for positions. Normals are the interesting half: they
    transform by the inverse transpose, so the same joint *halves* their Y before renormalisation.
    The authored normal is tilted off every axis on purpose -- an axis-aligned one is an
    eigenvector of a diagonal scale, so it comes back unchanged whichever rule was applied, and the
    fixture would prove nothing.

    The manifest states both answers and which is correct. Positions are asserted at L4 by
    `GltfSkinLadder`; the normal transform is **not**, and the manifest says so rather than
    implying coverage -- skinned normals are computed in each renderer's vertex shader, which is L7
    ground, and a CPU assertion here would only be re-checking arithmetic this file wrote itself.
    """
    b = GltfBuilder("skin-nonuniform-joint-scale")
    mesh, ibm = _skinned_triangle(b, joint_indices=_JOINTS, joint_weights=_WEIGHTS,
                                  inverse_binds=[mat_identity()], normals=_TILTED_NORMALS)
    joint0 = b.add_node(name="Joint0", scale=[1.0, 2.0, 1.0])
    mesh_node = b.add_node(name="SkinnedMeshNode", mesh=mesh, skin=0)
    b.add_skin({"name": "Skin", "joints": [joint0], "inverseBindMatrices": ibm})
    b.add_scene([joint0, mesh_node], name="Scene")
    b.set_default_scene(0)

    globals_ = [mat_scale([1.0, 2.0, 1.0])]
    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["skin"] = _skin_block(
        joint_nodes=[joint0], joint_names=["Joint0"], joint_globals=globals_,
        joint_indices=_JOINTS, joint_weights=_WEIGHTS,
        note="The joint matrix is S(1,2,1), so the vertex at (0,1,0) skins to (0,2,0) while the "
             "one at (1,0,0) does not move at all -- a uniform scale mistaken for this one would "
             "move both.")
    # Stated, and stated as unasserted. inverse-transpose(S(1,2,1)) halves Y: (0,0.6,0.8) becomes
    # (0,0.3,0.8), which normalises to (0,0.351123,0.936329). Applying the joint matrix directly
    # instead gives (0,1.2,0.8) -> (0,0.832050,0.554700): a 30-degree error, not a scale.
    l4["skin"]["normals"] = {
        "authored": [list(n) for n in _TILTED_NORMALS],
        "correctSkinnedNormals": [[0.0, 0.3511234, 0.9363292]] * 3,
        "wrongIfJointMatrixAppliedDirectly": [[0.0, 0.8320503, 0.5547002]] * 3,
        "rule": "A normal transforms by the inverse transpose of the joint matrix, then is "
                "renormalised. For a diagonal scale that means dividing each component by its own "
                "scale factor rather than multiplying.",
        "assertedAtL4": False,
        "whyNot": "Skinned normals are produced by each renderer's vertex shader, so the claim "
                  "belongs to L7. Asserting it on the CPU here would check arithmetic this "
                  "manifest wrote itself against nothing.",
    }
    return Fixture(
        id="skin-nonuniform-joint-scale", audit_fixture=None, owning_group="skinning",
        description="A joint scaled [1,2,1] with a normal tilted off every axis. Positions double "
                    "in Y; the normal's Y is HALVED before renormalisation, so the two rules give "
                    "visibly different directions rather than the same one at a different length.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4", "L5"],
        features=["non-uniform joint scale", "normal inverse-transpose rule",
                  "JOINTS_0 / WEIGHTS_0"],
        spec_anchors=["skins", "skinned-mesh-attributes"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="SkinnedTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=_TILTED_NORMALS, joints=_JOINTS,
            weights=_WEIGHTS, indices=TRIANGLE_INDICES)]},
        l4=l4,
    )


#: The file deliberately lists the CHILD before the PARENT in ``skin.joints[]``. One vertex per
#: joint plus one shared between them makes both the topological reorder and the chain observable:
#: vertex 0 rides file-local joint 1 (the parent), vertex 1 joint 0 (the child), vertex 2 both.
_PARENTED_JOINTS_AUTHORED = [(1, 0, 0, 0), (0, 0, 0, 0), (1, 0, 0, 0)]
#: What ExtractMesh must hand to the GPU after old file-local indices [child,parent] are remapped
#: onto the parent-before-child palette. Every valid component is remapped, including zero-weight
#: padding: authored joint 0 is the child and therefore becomes palette slot 1. L3/L5 describe
#: these imported values, not raw accessor values.
_PARENTED_JOINTS_IMPORTED = [(0, 1, 1, 1), (1, 1, 1, 1), (0, 1, 1, 1)]
_PARENTED_WEIGHTS = [(1.0, 0.0, 0.0, 0.0), (1.0, 0.0, 0.0, 0.0), (0.5, 0.5, 0.0, 0.0)]


def skin_parented_joints() -> Fixture:
    """§15.4's joint-chain rung: joint B is a child of joint A, and both are transformed.

    A joint's matrix is built from its **global** transform, so the child's must include its
    parent's. An importer using node-local transforms produces a child matrix of T(2,0,0) instead
    of T(2,3,0) -- and this fixture binds one vertex to each joint plus one to both, so that error
    shows up on the child's vertex and half of it on the shared one, while the parent's vertex
    stays correct. A single-joint fixture cannot tell the two rules apart at all.
    """
    b = GltfBuilder("skin-parented-joints")
    mesh, ibm = _skinned_triangle(b, joint_indices=_PARENTED_JOINTS_AUTHORED,
                                  joint_weights=_PARENTED_WEIGHTS,
                                  inverse_binds=[mat_identity()] * 2)
    # Declared child-before-parent. BuildSkeleton must reorder this to parent-before-child without
    # touching the scene graph's stable order, and ExtractMesh must remap the authored JOINTS_0
    # indices into that palette. GLTF-252 tests all three index spaces explicitly.
    child = b.add_node(name="JointB", translation=[2.0, 0.0, 0.0])
    parent = b.add_node(name="JointA", translation=[0.0, 3.0, 0.0], children=[child])
    mesh_node = b.add_node(name="SkinnedMeshNode", mesh=mesh, skin=0)
    b.add_skin({"name": "Skin", "joints": [child, parent], "inverseBindMatrices": ibm})
    b.add_scene([parent, mesh_node], name="Scene")
    b.set_default_scene(0)

    parent_global = mat_translation([0.0, 3.0, 0.0])
    child_global = mat_mul(parent_global, mat_translation([2.0, 0.0, 0.0]))
    globals_ = [child_global, parent_global]
    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["skin"] = _skin_block(
        joint_nodes=[child, parent], joint_names=["JointB", "JointA"], joint_globals=globals_,
        joint_indices=_PARENTED_JOINTS_AUTHORED, joint_weights=_PARENTED_WEIGHTS, parents=[1, -1],
        note="JointB's global transform is T(0,3,0) * T(2,0,0) = T(2,3,0), so the vertex bound to "
             "it lands at (3,3,0). A reader using node-local transforms puts it at (3,0,0) and "
             "moves the shared vertex half as far wrong, while the parent's vertex stays right -- "
             "which is what makes the failure attributable to the chain rather than to the skin.")
    return Fixture(
        id="skin-parented-joints", audit_fixture=None, owning_group="skinning",
        description="Joint B parented to joint A, both translated, with one vertex on each and "
                    "one shared. The child's joint matrix must carry its parent's transform; a "
                    "node-local reading is wrong on two of the three vertices and right on one.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4", "L5"],
        features=["joint parented to joint", "non-topological skin.joints order",
                  "global joint transform", "per-vertex joint binding"],
        spec_anchors=["skins", "joint-hierarchy", "skinned-mesh-attributes"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="SkinnedTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, joints=_PARENTED_JOINTS_IMPORTED,
            authored_joints=_PARENTED_JOINTS_AUTHORED, weights=_PARENTED_WEIGHTS,
            indices=TRIANGLE_INDICES)]},
        l4=l4,
    )


#: Joints 1 and 2, never 0, and in that order: as UNSIGNED_SHORT this is `01 00 02 00`, which a
#: reader that mistakes the stream for UNSIGNED_BYTE reads as joints (1,0,2,0) -- the same first
#: index, a spurious joint 0, and joint 2 landing in the slot weight 0 belongs to.
_USHORT_JOINTS = [(1, 2, 0, 0)] * 3
_USHORT_WEIGHTS = [(0.5, 0.5, 0.0, 0.0)] * 3


def skin_ushort_joint_indices() -> Fixture:
    """`JOINTS_0` stored as `UNSIGNED_SHORT`, which §3.7.3.3 permits alongside `UNSIGNED_BYTE`.

    XNA's ``BlendIndices`` is four bytes, so a wide index stream must be *converted* rather than
    copied, and the conversion is where a reader silently reinterprets the bytes. The three joints
    are given distinct translations and every vertex is split evenly between joints 1 and 2, so a
    byte-wise reading blends joints 1 and 0 instead: (2.5,3,0) rather than (0,3,3.5), which no
    tolerance can absorb.

    This rung replaces §15.4's `skin-256-joints`, whose stated point -- that a 256-joint rig must
    not wrap the index byte -- is unreachable: `GLTF-261` refuses any skin past the 72-joint GPU
    palette long before its indices are decoded, and `skin-73-joints` already owns that refusal
    one joint past the boundary. The reachable half of the same hazard is the component type, and
    that is what this fixture tests.
    """
    b = GltfBuilder("skin-ushort-joint-indices")
    mesh, ibm = _skinned_triangle(b, joint_indices=_USHORT_JOINTS, joint_weights=_USHORT_WEIGHTS,
                                  inverse_binds=[mat_identity()] * 3,
                                  joints_component_type=UNSIGNED_SHORT)
    translations = [[5.0, 0.0, 0.0], [0.0, 6.0, 0.0], [0.0, 0.0, 7.0]]
    joint_nodes = [b.add_node(name="Joint" + str(i), translation=t)
                   for i, t in enumerate(translations)]
    mesh_node = b.add_node(name="SkinnedMeshNode", mesh=mesh, skin=0)
    b.add_skin({"name": "Skin", "joints": joint_nodes, "inverseBindMatrices": ibm})
    b.add_scene(joint_nodes + [mesh_node], name="Scene")
    b.set_default_scene(0)

    globals_ = [mat_translation(t) for t in translations]
    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["skin"] = _skin_block(
        joint_nodes=joint_nodes, joint_names=["Joint0", "Joint1", "Joint2"],
        joint_globals=globals_, joint_indices=_USHORT_JOINTS, joint_weights=_USHORT_WEIGHTS,
        note="Every vertex is split evenly between joints 1 and 2, displacing it by "
             "0.5*(0,6,0) + 0.5*(0,0,7) = (0,3,3.5). A reader treating the UNSIGNED_SHORT stream "
             "as bytes blends joints 1 and 0 instead and displaces it by (2.5,3,0).")
    l4["skin"]["jointIndexComponentType"] = "UNSIGNED_SHORT"
    return Fixture(
        id="skin-ushort-joint-indices", audit_fixture=None, owning_group="skinning",
        description="JOINTS_0 stored as UNSIGNED_SHORT rather than UNSIGNED_BYTE, with the two "
                    "influencing joints chosen so a byte-wise misreading blends a different pair "
                    "of joints and lands the vertex somewhere else entirely.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4", "L5"],
        features=["UNSIGNED_SHORT JOINTS_0", "joint index component conversion"],
        spec_anchors=["skins", "skinned-mesh-attributes", "accessor-data-types"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="SkinnedTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, joints=_USHORT_JOINTS, weights=_USHORT_WEIGHTS,
            indices=TRIANGLE_INDICES)]},
        l4=l4,
    )


FIXTURES = [skin_armature_ancestor, skin_mesh_node_transform, skin_plus_static_mesh,
            skin_unlit, skin_vertex_color, skin_mesh_node_parent_transform,
            skin_skeleton_hint, skin_unnormalized, skin_73_joints, skin_eight_influences,
            skin_two_weighted, skin_four_weighted, skin_no_ibm, skin_nonuniform_joint_scale,
            skin_parented_joints, skin_ushort_joint_indices]
