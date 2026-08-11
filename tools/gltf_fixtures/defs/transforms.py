# SPDX-License-Identifier: MS-PL
"""Transform fixtures -- owning group ``transforms`` (plan_gltf.md §11.4, §24.2).

These are the L4 ladder's first rungs and the fixtures that prove D1, D2 and D3: CNA's import
data model carries no ``cgltf_node`` and no matrix, so every mesh primitive is emitted in
mesh-local space with an identity bone transform. Each fixture here states one node-graph shape
whose world-space vertex set is exactly computable.

Specification: §3.5.2 ``nodes-and-hierarchy``, §3.5.3 ``transformations``.
"""

from __future__ import annotations

from ..builder import TRIANGLES, UNSIGNED_SHORT, GltfBuilder
from ..manifest import Defect, Fixture, l3_primitive, world_positions
from .common import TRIANGLE_INDICES, TRIANGLE_MAX, TRIANGLE_MIN, TRIANGLE_NORMALS, TRIANGLE_POSITIONS

_SPEC = ["nodes-and-hierarchy", "transformations"]

# The transform tasks that own D1/D2/D3. Listed once so a fixture cannot drift from the plan.
_TRANSFORM_TASKS = ["GLTF-103", "GLTF-113", "GLTF-114", "GLTF-115"]


def _carrier_triangle(builder: GltfBuilder, mesh_name: str) -> int:
    """Adds the shared carrier triangle as mesh ``mesh_name`` and returns its mesh index."""
    position = builder.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                           accessor_type="VEC3", with_bounds=True)
    normal = builder.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS,
                                         accessor_type="VEC3")
    indices = builder.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                          accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    return builder.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal},
        "indices": indices,
        "mode": TRIANGLES,
    }], name=mesh_name)


def _l3(mesh: int, mesh_name: str) -> dict:
    return {"primitives": [l3_primitive(
        mesh=mesh, mesh_name=mesh_name, primitive=0, mode=TRIANGLES,
        positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES)]}


def xf_identity() -> Fixture:
    """One node, no transform -- the L4 ladder's zero point and the oracle's own self-test.

    CNA already produces the right answer here, because "mesh-local with an identity bone" and
    "world space" coincide when the node transform is the identity. That is exactly what makes it
    the control: it proves the L4 oracle agrees with CNA when there is nothing to get wrong, so a
    divergence on any other transform fixture is the transform, not the harness.
    """
    b = GltfBuilder("xf-identity")
    mesh = _carrier_triangle(b, "Tri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="xf-identity", audit_fixture=None, owning_group="transforms",
        description="A single untransformed node instancing one triangle. The L4 oracle's control "
                    "case: expected world positions equal the mesh-local positions.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["node without transform", "single scene root"], spec_anchors=_SPEC,
        l3=_l3(mesh, "Tri"),
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


def xf_shared_mesh() -> Fixture:
    """f1 -- one mesh instanced by two nodes, one of them translated. Proves **D1**."""
    b = GltfBuilder("xf-shared-mesh")
    mesh = _carrier_triangle(b, "SharedTri")
    at_origin = b.add_node(name="InstanceAtOrigin", mesh=mesh, translation=[0, 0, 0])
    translated = b.add_node(name="InstanceTranslatedX10", mesh=mesh, translation=[10, 0, 0])
    b.add_scene([at_origin, translated], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="xf-shared-mesh", audit_fixture="f1", owning_group="transforms",
        description="One mesh instanced by two nodes, the second translated by [10,0,0]. A "
                    "conforming importer places the two instances 10 units apart; CNA emits both "
                    "in mesh-local space, superimposed at the origin.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        referencing_groups=["scenes"],
        features=["node.translation", "mesh instancing", "two scene roots"], spec_anchors=_SPEC,
        l3=_l3(mesh, "SharedTri"),
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
        defects=[Defect(
            id="D1", owner="GLTF-TRANSFORM", first_divergent_layer="L4",
            summary="glTF node TRS was discarded for every mesh instance -- MeshGroup carried no "
                    "cgltf_node and no matrix, so every primitive was emitted mesh-local with an "
                    "identity bone transform. GLTF-113 replaced MeshGroup::meshes with real "
                    "MeshInstanceOut placements, and GLTF-114 gave both loaders a ModelBone per "
                    "scene node so the instancing node's transform reaches the model.",
            owning_tasks=_TRANSFORM_TASKS, closed_tasks=_TRANSFORM_TASKS, status="fixed",
            divergent_fields=[],
            current_actual={
                "worldBounds": {"min": [0.0, 0.0, 0.0], "max": [11.0, 1.0, 0.0]},
                "instanceCount": 2,
                "instanceWorldMatricesAreAllIdentity": False,
                "note": "The two instances are placed 10 units apart, so the union spans the "
                        "spec-derived X range [0,11]. Vertex positions stay mesh-local, which is "
                        "the point of the bone-hierarchy architecture (GLTF-103 Option A): the "
                        "shared mesh is instanced, not duplicated. Nothing is suppressed any more "
                        "-- GltfConformanceL4 asserts this fixture in full.",
            },
            prior_actual={
                "worldBounds": {"min": [0.0, 0.0, 0.0], "max": [1.0, 1.0, 0.0]},
                "instanceCount": 2,
                "instanceWorldMatricesAreAllIdentity": True,
                "measuredOn": "fb3728267e8f2179d43b96357ff372ae712b7e7f",
                "note": "What the forensic audit measured before GLTF-113/114: both instances "
                        "decoded to the mesh-local X range [0,1] against an expected union of "
                        "[0,11]. CollectMeshGroups recorded the mesh twice but no node, so the "
                        "second instance was an exact duplicate of the first.",
            },
        )],
    )


def xf_parent_child() -> Fixture:
    """f2 -- a scaled parent with a translated child. Proves **D2**."""
    b = GltfBuilder("xf-parent-child")
    mesh = _carrier_triangle(b, "ChildTri")
    child = b.add_node(name="Child", mesh=mesh, translation=[0, 3, 0])
    parent = b.add_node(name="Parent", children=[child], scale=[2, 2, 2])
    b.add_scene([parent], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="xf-parent-child", audit_fixture="f2", owning_group="transforms",
        description="Parent scale [2,2,2] composed with child translation [0,3,0]. world = "
                    "parentWorld * childLocal, so the child's local origin lands at Y=6. CNA "
                    "composes nothing and emits the child in mesh-local space.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["node.scale", "node.translation", "parent-child composition"], spec_anchors=_SPEC,
        l3=_l3(mesh, "ChildTri"),
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
        defects=[Defect(
            id="D2", owner="GLTF-TRANSFORM", first_divergent_layer="L4",
            summary="Parent-to-child transform composition was discarded: no ancestry was walked "
                    "for a mesh instance at all. GLTF-113's BuildSceneGraph composes "
                    "local * parentWorld parent-before-child, and GLTF-114 mirrors that chain as "
                    "ModelBone parents so Model::CopyAbsoluteBoneTransformsTo recomposes it.",
            owning_tasks=_TRANSFORM_TASKS, closed_tasks=_TRANSFORM_TASKS, status="fixed",
            divergent_fields=[],
            current_actual={
                "worldBounds": {"min": [0.0, 6.0, 0.0], "max": [2.0, 8.0, 0.0]},
                "instanceCount": 1,
                "instanceWorldMatricesAreAllIdentity": False,
                "note": "The composed world Y range is the spec-derived [6,8].",
            },
            prior_actual={
                "worldBounds": {"min": [0.0, 0.0, 0.0], "max": [1.0, 1.0, 0.0]},
                "instanceCount": 1,
                "instanceWorldMatricesAreAllIdentity": True,
                "measuredOn": "fb3728267e8f2179d43b96357ff372ae712b7e7f",
                "note": "Decoded Y range was the mesh-local [0,1] against an expected [6,8].",
            },
        )],
    )


def xf_matrix_node() -> Fixture:
    """f13 -- a node whose transform is authored as ``matrix``. Proves **D3**."""
    b = GltfBuilder("xf-matrix-node")
    mesh = _carrier_triangle(b, "MatrixTri")
    # Column-major translate(4,5,6): the translation occupies the last column (§3.5.3).
    node = b.add_node(name="MatrixNode", mesh=mesh,
                      matrix=[1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 4, 5, 6, 1])
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="xf-matrix-node", audit_fixture="f13", owning_group="transforms",
        description="A node transform authored as a column-major 'matrix' rather than TRS. A "
                    "conforming importer must honour it exactly as it honours TRS.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["node.matrix", "column-major matrix layout"], spec_anchors=_SPEC,
        l3=_l3(mesh, "MatrixTri"),
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
        defects=[Defect(
            id="D3", owner="GLTF-TRANSFORM", first_divergent_layer="L4",
            summary="node.matrix was discarded, by the same mechanism as D1/D2 -- the import data "
                    "model had nowhere to put it. BuildSceneGraph reads the node's local transform "
                    "through cgltf_node_transform_local, which already applies the spec's own "
                    "'matrix, or else TRS' exclusivity rule, so both authorings now compose alike.",
            owning_tasks=_TRANSFORM_TASKS, closed_tasks=_TRANSFORM_TASKS, status="fixed",
            divergent_fields=[],
            current_actual={
                "worldBounds": {"min": [4.0, 5.0, 6.0], "max": [5.0, 6.0, 6.0]},
                "instanceCount": 1,
                "instanceWorldMatricesAreAllIdentity": False,
                "note": "The composed world X range is the spec-derived [4,5].",
            },
            prior_actual={
                "worldBounds": {"min": [0.0, 0.0, 0.0], "max": [1.0, 1.0, 0.0]},
                "instanceCount": 1,
                "instanceWorldMatricesAreAllIdentity": True,
                "measuredOn": "fb3728267e8f2179d43b96357ff372ae712b7e7f",
                "note": "Decoded X range was the mesh-local [0,1] against an expected [4,5].",
            },
        )],
    )


FIXTURES = [xf_identity, xf_shared_mesh, xf_parent_child, xf_matrix_node]
