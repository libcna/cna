# SPDX-License-Identifier: MS-PL
"""Primitive-mode fixtures -- owning group ``topology`` (plan_gltf.md §24.2).

Both prove **D5**: CNA never reads ``primitive.mode``. Every primitive, whatever its declared
topology, is emitted as an index list that the downstream packing layer divides by three and draws
as a triangle list. A strip loses its later triangles; a point cloud becomes one triangle.

Specification: §3.7.2.1 ``meshes-overview``.
"""

from __future__ import annotations

from ..builder import POINTS, TRIANGLE_STRIP, UNSIGNED_SHORT, GltfBuilder
from ..manifest import Defect, Fixture, l3_primitive, world_positions
from .common import QUAD_STRIP_POSITIONS

_TASKS = ["GLTF-071"]


def mode_triangle_strip() -> Fixture:
    """f4 -- ``mode: 5`` (TRIANGLE_STRIP) with four indices. Proves **D5**.

    Four strip indices describe two triangles, the second with its first two corners swapped so the
    winding is preserved. Read as a triangle list they describe one triangle and vertex 3 is never
    drawn at all.
    """
    b = GltfBuilder("mode-triangle-strip")
    position = b.add_packed_accessor(usage="POSITION", values=QUAD_STRIP_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    indices = b.add_packed_accessor(usage="indices", values=[0, 1, 2, 3], accessor_type="SCALAR",
                                    component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position},
        "indices": indices,
        "mode": TRIANGLE_STRIP,
    }], name="StripQuad")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="mode-triangle-strip", audit_fixture="f4", owning_group="topology",
        description="An indexed TRIANGLE_STRIP quad. The spec-correct expansion is two triangles, "
                    "[0,1,2] and [2,1,3]; reinterpreted as a triangle list it is one triangle and "
                    "vertex 3 is dropped.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        features=["primitive.mode = TRIANGLE_STRIP", "strip winding"],
        spec_anchors=["meshes-overview"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="StripQuad", primitive=0, mode=TRIANGLE_STRIP,
            positions=QUAD_STRIP_POSITIONS, indices=[0, 1, 2, 3])]},
        l4=world_positions(b, {mesh: list(QUAD_STRIP_POSITIONS)}),
        defects=[Defect(
            id="D5", owner="GLTF-MESH", first_divergent_layer="L3",
            summary="primitive.mode is never read. MeshOut carries no topology field at all, so a "
                    "TRIANGLE_STRIP is silently reinterpreted as a triangle list.",
            owning_tasks=_TASKS,
            divergent_fields=["mode", "modeName", "topologyCarried", "triangles"],
            current_actual={
                "indices": [0, 1, 2, 3],
                "topologyCarried": False,
                "triangles": [[0, 1, 2]],
                "note": "The index list itself survives intact -- the loss is the topology. Read "
                        "as TRIANGLES, four indices yield one triangle and vertex 3 is unreachable; "
                        "the expected expansion is two triangles.",
            },
        )],
    )


def mode_points() -> Fixture:
    """f12 -- ``mode: 0`` (POINTS), non-indexed. Proves **D5** on the non-indexed path.

    A non-indexed primitive's implicit indices are ``[0, count)``. With the mode ignored, those
    four implicit indices are read as a triangle list: one triangle where four points were meant.
    """
    b = GltfBuilder("mode-points")
    position = b.add_packed_accessor(usage="POSITION", values=QUAD_STRIP_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position},
        "mode": POINTS,
    }], name="PointCloud")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="mode-points", audit_fixture="f12", owning_group="topology",
        description="A non-indexed POINTS primitive with four vertices. A conforming importer "
                    "draws four points and no triangles at all.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        features=["primitive.mode = POINTS", "non-indexed primitive", "implicit index range"],
        spec_anchors=["meshes-overview"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="PointCloud", primitive=0, mode=POINTS,
            positions=QUAD_STRIP_POSITIONS, indices=None)]},
        l4=world_positions(b, {mesh: list(QUAD_STRIP_POSITIONS)}),
        defects=[Defect(
            id="D5", owner="GLTF-MESH", first_divergent_layer="L3",
            summary="primitive.mode is never read, so a POINTS primitive becomes a triangle list "
                    "built from its own implicit index range.",
            owning_tasks=_TASKS,
            divergent_fields=["mode", "modeName", "topologyCarried", "triangles"],
            current_actual={
                "indices": [0, 1, 2, 3],
                "topologyCarried": False,
                "triangles": [[0, 1, 2]],
                "note": "Implicit indices [0,1,2,3] are generated correctly for the non-indexed "
                        "primitive; the defect is that they are then interpreted as TRIANGLES, "
                        "yielding one triangle where the expected output is four points and zero "
                        "triangles.",
            },
        )],
    )


FIXTURES = [mode_triangle_strip, mode_points]
