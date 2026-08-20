# SPDX-License-Identifier: MS-PL
"""Transform fixtures -- owning group ``transforms`` (plans/plan_gltf.md §11.4, §24.2).

These are the L4 ladder's first rungs and the fixtures that prove D1, D2 and D3: CNA's import
data model carries no ``cgltf_node`` and no matrix, so every mesh primitive is emitted in
mesh-local space with an identity bone transform. Each fixture here states one node-graph shape
whose world-space vertex set is exactly computable.

Specification: §3.5.2 ``nodes-and-hierarchy``, §3.5.3 ``transformations``.
"""

from __future__ import annotations

import math

from ..builder import TRIANGLES, UNSIGNED_SHORT, GltfBuilder
from ..manifest import Defect, Fixture, l3_primitive, world_positions
from .common import TRIANGLE_INDICES, TRIANGLE_MAX, TRIANGLE_MIN, TRIANGLE_NORMALS, TRIANGLE_POSITIONS

_SPEC = ["nodes-and-hierarchy", "transformations"]

# The transform tasks that own D1/D2/D3. Listed once so a fixture cannot drift from the plan.
_TRANSFORM_TASKS = ["GLTF-103", "GLTF-113", "GLTF-114", "GLTF-115"]


def _carrier_triangle(builder: GltfBuilder, mesh_name: str, normals=TRIANGLE_NORMALS) -> int:
    """Adds the shared carrier triangle as mesh ``mesh_name`` and returns its mesh index."""
    position = builder.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                           accessor_type="VEC3", with_bounds=True)
    normal = builder.add_packed_accessor(usage="NORMAL", values=normals,
                                         accessor_type="VEC3")
    indices = builder.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                          accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    return builder.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal},
        "indices": indices,
        "mode": TRIANGLES,
    }], name=mesh_name)


def _l3(mesh: int, mesh_name: str, normals=TRIANGLE_NORMALS) -> dict:
    return {"primitives": [l3_primitive(
        mesh=mesh, mesh_name=mesh_name, primitive=0, mode=TRIANGLES,
        positions=TRIANGLE_POSITIONS, normals=normals, indices=TRIANGLE_INDICES)]}


#: A unit normal with two non-zero components, so a normal matrix that is merely the world 3x3
#: gives a visibly different direction from the correct inverse-transpose. An axis-aligned normal
#: would survive both derivations under an axis-aligned scale and prove nothing.
SLANTED_NORMALS = [(0.6, 0.8, 0.0), (0.6, 0.8, 0.0), (0.6, 0.8, 0.0)]


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


def xf_scale_nonuniform() -> Fixture:
    """§11.4's non-uniform-scale rung -- the case that separates a normal matrix from a world one.

    Positions only ever need the world matrix, so every other transform fixture is satisfied by it.
    Normals are not: under a non-uniform scale the correct normal transform is
    ``transpose(inverse(world3x3))`` (§3.7.2.1), and using the world 3x3 directly skews them. The
    carrier triangle's own normals are axis-aligned and would survive both derivations unchanged
    under an axis-aligned scale, so this fixture authors ``(0.6,0.8,0)`` instead: with
    ``scale = [2,3,4]`` the two derivations then disagree far outside any tolerance, and an L6
    capture can tell them apart by number rather than by inspection.
    """
    b = GltfBuilder("xf-scale-nonuniform")
    mesh = _carrier_triangle(b, "SlantedTri", normals=SLANTED_NORMALS)
    node = b.add_node(name="ScaledNode", mesh=mesh, scale=[2, 3, 4])
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="xf-scale-nonuniform", audit_fixture=None, owning_group="transforms",
        description="A single node with the non-uniform scale [2,3,4], carrying a triangle whose "
                    "normals point along (0.6,0.8,0). Positions scale componentwise; normals must "
                    "be transformed by the inverse transpose of the world 3x3, which for this "
                    "scale is diag(0.5, 1/3, 0.25) -- a different direction from the world 3x3's "
                    "own. The fixture exists so the normal matrix has a case that can fail.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["node.scale non-uniform", "normal matrix", "inverse-transpose normal transform"],
        spec_anchors=_SPEC + ["meshes-overview"],
        l3=_l3(mesh, "SlantedTri", normals=SLANTED_NORMALS),
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


def xf_negative_scale() -> Fixture:
    """§11.4's mirroring rung -- a node whose transform has a negative determinant.

    A negative scale is the one transform that changes something no world matrix multiplication
    expresses: the triangle's *facing*. Positions mirror the way any other affine transform moves
    them, so an importer that only ever asks "where does this vertex land" passes every position
    assertion here while rendering the model inside-out. §3.7.4 requires the winding order to be
    reversed when the node's global transform determinant is negative; the transform is not baked
    into the vertices (GLTF-103 Option A), so the reversal cannot live in the index buffer of a
    mesh that may also be instanced unmirrored -- it is a per-draw raster decision.
    """
    b = GltfBuilder("xf-negative-scale")
    mesh = _carrier_triangle(b, "MirroredTri")
    node = b.add_node(name="MirroredNode", mesh=mesh, scale=[-1, 1, 1])
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="xf-negative-scale", audit_fixture=None, owning_group="transforms",
        description="A single node scaled [-1,1,1], carrying the carrier triangle. Positions "
                    "mirror across the YZ plane; the determinant of the world 3x3 is -1, which "
                    "per §3.7.4 means the triangle's winding must be reversed at draw time for "
                    "its front face to stay front-facing.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["node.scale negative", "mirroring", "winding order"], spec_anchors=_SPEC,
        l3=_l3(mesh, "MirroredTri"),
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


def xf_mirror_child() -> Fixture:
    """Mirroring composed through a hierarchy -- two mirrors cancel, one does not.

    The file carries three placements of one mesh so a single fixture states the whole rule: an
    unmirrored root, a mirrored child of it, and a mirrored child of *that*. The determinant sign
    is multiplicative, so the third placement is unmirrored again. An implementation that tested
    only the instancing node's own scale -- the obvious shortcut -- gets the deepest one wrong,
    and gets it wrong in the direction that renders geometry inside-out with no other symptom.
    """
    b = GltfBuilder("xf-mirror-child")
    mesh = _carrier_triangle(b, "HandednessTri")
    grandchild = b.add_node(name="MirroredTwice", mesh=mesh, scale=[-1, 1, 1],
                            translation=[0, 4, 0])
    child = b.add_node(name="MirroredOnce", mesh=mesh, scale=[-1, 1, 1], translation=[0, 2, 0],
                       children=[grandchild])
    root = b.add_node(name="Unmirrored", mesh=mesh, children=[child])
    b.add_scene([root], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="xf-mirror-child", audit_fixture=None, owning_group="transforms",
        description="One mesh instanced three times down a chain: an unmirrored root, a child "
                    "scaled [-1,1,1], and a grandchild scaled [-1,1,1] again. The composed "
                    "determinants are +1, -1 and +1 -- mirroring is a property of the composed "
                    "world transform, never of the instancing node's own scale.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        referencing_groups=["scenes"],
        features=["mirroring", "hierarchical composition", "mesh instancing"], spec_anchors=_SPEC,
        l3=_l3(mesh, "HandednessTri"),
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


# --- §11.4's remaining ladder rungs (`GLTF-399`) -------------------------------------------------
#
# One node-graph shape each, with an exactly computable world-space vertex set. `world_positions`
# derives the expectation from the builder's own node graph and cross-checks it against cgltf, so
# these state the SHAPE and the oracle states the numbers -- which is what keeps a fixture from
# asserting a value someone typed twice.

#: sin(45°) = cos(45°) = √½, the quaternion component a 90° turn about one axis uses for both its
#: vector part and its scalar part.
_HALF_SQRT2 = math.sqrt(0.5)


def _single_rotation(name: str, rotation, description: str, features) -> Fixture:
    """A quarter turn about one axis, on a triangle whose vertices lie on two of the three axes."""
    b = GltfBuilder(name)
    mesh = _carrier_triangle(b, "Tri")
    node = b.add_node(name="MeshNode", mesh=mesh, rotation=list(rotation))
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id=name, audit_fixture=None, owning_group="transforms",
        description=description, builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=list(features), spec_anchors=_SPEC,
        l3=_l3(mesh, "Tri"),
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


def xf_translation() -> Fixture:
    """`T=[3,4,5]`. The rung that separates "the transform arrived" from "a transform arrived"."""
    b = GltfBuilder("xf-translation")
    mesh = _carrier_triangle(b, "Tri")
    node = b.add_node(name="MeshNode", mesh=mesh, translation=[3.0, 4.0, 5.0])
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="xf-translation", audit_fixture=None, owning_group="transforms",
        description="A single node translated by (3,4,5). Three different components, so a "
                    "translation applied on the wrong axis is a different point rather than a "
                    "plausible one.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["node translation"], spec_anchors=_SPEC,
        l3=_l3(mesh, "Tri"),
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


def xf_scale_uniform() -> Fixture:
    """`S=[2,2,2]` -- the case under which the normal matrix and the world 3x3 agree."""
    b = GltfBuilder("xf-scale-uniform")
    mesh = _carrier_triangle(b, "Tri", normals=SLANTED_NORMALS)
    node = b.add_node(name="MeshNode", mesh=mesh, scale=[2.0, 2.0, 2.0])
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="xf-scale-uniform", audit_fixture=None, owning_group="transforms",
        description="A uniformly scaled node. Deliberately paired with xf-scale-nonuniform: under "
                    "uniform scale the inverse-transpose normal matrix and the world 3x3 point the "
                    "same way, so this is the case that must NOT discriminate -- and a normal "
                    "matrix that got it wrong here would be wrong about something else entirely.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["uniform node scale", "normal matrix agrees with the world 3x3"],
        spec_anchors=_SPEC,
        l3=_l3(mesh, "Tri", normals=SLANTED_NORMALS),
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


def xf_rot_x90() -> Fixture:
    """A quarter turn about +X: the triangle's `(0,1,0)` vertex must land on `(0,0,1)`."""
    return _single_rotation(
        "xf-rot-x90", [_HALF_SQRT2, 0.0, 0.0, _HALF_SQRT2],
        "A 90° rotation about +X. The vertex on +X is invariant and the vertex on +Y moves to +Z, "
        "so the fixture distinguishes 'rotated' from 'rotated the other way' and from 'not "
        "rotated' with the same three vertices.",
        ["node rotation", "quaternion about +X"])


def xf_rot_y90() -> Fixture:
    """A quarter turn about +Y: `(1,0,0)` must land on `(0,0,-1)`."""
    return _single_rotation(
        "xf-rot-y90", [0.0, _HALF_SQRT2, 0.0, _HALF_SQRT2],
        "A 90° rotation about +Y, which sends +X to -Z. The sign is the point: a rotation built "
        "with the opposite handedness sends it to +Z, and every length in the model stays right.",
        ["node rotation", "quaternion about +Y", "handedness"])


def xf_rot_z90() -> Fixture:
    """A quarter turn about +Z: `(1,0,0)` must land on `(0,1,0)`."""
    return _single_rotation(
        "xf-rot-z90", [0.0, 0.0, _HALF_SQRT2, _HALF_SQRT2],
        "A 90° rotation about +Z. This is the rotation Matrix::Decompose lost entirely until "
        "known_bugs.md's negative-zero fix -- a quarter turn has exact zeros in every row, which "
        "is exactly where std::signbit disagrees with Math.Sign.",
        ["node rotation", "quaternion about +Z", "axis-aligned quarter turn"])


def xf_trs_order() -> Fixture:
    """`T`, `R` and `S` at once -- §3.5.3's own order, which is the only thing this proves."""
    b = GltfBuilder("xf-trs-order")
    mesh = _carrier_triangle(b, "Tri")
    node = b.add_node(name="MeshNode", mesh=mesh, translation=[1.0, 0.0, 0.0],
                      rotation=[0.0, 0.0, _HALF_SQRT2, _HALF_SQRT2], scale=[2.0, 2.0, 2.0])
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="xf-trs-order", audit_fixture=None, owning_group="transforms",
        description="One node with all three of T=[1,0,0], R=z90 and S=[2,2,2]. §3.5.3 composes "
                    "scale, then rotation, then translation, and every other order puts the "
                    "triangle somewhere else -- which is why all three are non-trivial here rather "
                    "than one being a token value.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["TRS composition order", "scale then rotate then translate"], spec_anchors=_SPEC,
        l3=_l3(mesh, "Tri"),
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


def xf_matrix_vs_trs() -> Fixture:
    """The same transform authored both ways, in one file, so the two must agree exactly."""
    b = GltfBuilder("xf-matrix-vs-trs")
    mesh = _carrier_triangle(b, "Tri")
    # translate(1,0,0) * rotate_z(90°) * scale(2) as a column-major matrix -- the composition
    # §3.5.3 defines, written out rather than composed by the builder, so the fixture compares two
    # authorings rather than one authoring against itself.
    matrix = [0.0, 2.0, 0.0, 0.0,
              -2.0, 0.0, 0.0, 0.0,
              0.0, 0.0, 2.0, 0.0,
              1.0, 0.0, 0.0, 1.0]
    node_matrix = b.add_node(name="AuthoredAsMatrix", mesh=mesh, matrix=matrix)
    node_trs = b.add_node(name="AuthoredAsTrs", mesh=mesh, translation=[1.0, 0.0, 0.0],
                          rotation=[0.0, 0.0, _HALF_SQRT2, _HALF_SQRT2], scale=[2.0, 2.0, 2.0])
    b.add_scene([node_matrix, node_trs], name="Scene")
    b.set_default_scene(0)
    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["equivalentInstances"] = {
        "rule": "The two nodes author the same transform, one as `matrix` and one as TRS. Their "
                "world positions must be identical to the last bit the float format allows -- an "
                "importer that composes TRS in a different order, or transposes the matrix, "
                "diverges here while each node on its own still looks reasonable.",
        "nodes": [node_matrix, node_trs],
    }
    return Fixture(
        id="xf-matrix-vs-trs", audit_fixture=None, owning_group="transforms",
        description="One transform authored twice in one file -- as `matrix` on one node and as "
                    "TRS on another. Neither node proves anything alone; the pair proves the two "
                    "paths agree, which is the only way to catch a transposed matrix or a "
                    "reordered composition that is self-consistent.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["matrix vs TRS equivalence", "two authorings of one transform"],
        spec_anchors=_SPEC,
        l3=_l3(mesh, "Tri"),
        l4=l4,
    )


def xf_deep_chain() -> Fixture:
    """Five nested nodes, each translating by 1 -- composition that accumulates."""
    b = GltfBuilder("xf-deep-chain")
    mesh = _carrier_triangle(b, "Tri")
    leaf = b.add_node(name="Depth5", mesh=mesh, translation=[1.0, 0.0, 0.0])
    child = leaf
    for depth in range(4, 0, -1):
        child = b.add_node(name=f"Depth{depth}", translation=[1.0, 0.0, 0.0], children=[child])
    b.add_scene([child], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="xf-deep-chain", audit_fixture=None, owning_group="transforms",
        description="Five nested nodes each translated by (1,0,0), so the leaf mesh sits at x=5 "
                    "plus its own vertex. A composition applied once, or applied at the wrong "
                    "depth, lands on a different integer -- which is why every step is the same "
                    "size and the total is not.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["five-deep node chain", "accumulating composition"], spec_anchors=_SPEC,
        l3=_l3(mesh, "Tri"),
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


def xf_transform_only() -> Fixture:
    """A node with a transform and no mesh, whose child carries one."""
    b = GltfBuilder("xf-transform-only")
    mesh = _carrier_triangle(b, "Tri")
    child = b.add_node(name="MeshChild", mesh=mesh)
    parent = b.add_node(name="TransformOnly", translation=[0.0, 7.0, 0.0], scale=[3.0, 3.0, 3.0],
                        children=[child])
    b.add_scene([parent], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="xf-transform-only", audit_fixture=None, owning_group="transforms",
        description="A transform-only parent (no mesh of its own) with a mesh-bearing child. The "
                    "shape every exporter produces for a group or a pivot, and the one an importer "
                    "that only walks mesh nodes silently places at the origin.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["transform-only node", "mesh on a child"], spec_anchors=_SPEC,
        l3=_l3(mesh, "Tri"),
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


def xf_multi_root() -> Fixture:
    """Three root nodes in one scene, each with the same mesh at a different place."""
    b = GltfBuilder("xf-multi-root")
    mesh = _carrier_triangle(b, "Tri")
    roots = [
        b.add_node(name="RootA", mesh=mesh),
        b.add_node(name="RootB", mesh=mesh, translation=[10.0, 0.0, 0.0]),
        b.add_node(name="RootC", mesh=mesh, translation=[0.0, -20.0, 0.0]),
    ]
    b.add_scene(roots, name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="xf-multi-root", audit_fixture=None, owning_group="transforms",
        description="Three root nodes in one scene, spread far apart. §3.8 allows any number of "
                    "roots, and an importer that took scene.nodes[0] would produce a model missing "
                    "two thirds of itself with nothing to indicate it.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["three scene roots", "multi-root scene"], spec_anchors=_SPEC,
        l3=_l3(mesh, "Tri"),
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


FIXTURES = [xf_identity, xf_translation, xf_scale_uniform, xf_scale_nonuniform, xf_rot_x90,
            xf_rot_y90, xf_rot_z90, xf_trs_order, xf_matrix_node, xf_matrix_vs_trs,
            xf_parent_child, xf_deep_chain, xf_negative_scale, xf_mirror_child,
            xf_shared_mesh, xf_transform_only, xf_multi_root]
