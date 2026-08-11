# SPDX-License-Identifier: MS-PL
"""Primitive-mode fixtures -- owning group ``topology`` (plan_gltf.md §24.2).

Both prove **D5**: CNA never read ``primitive.mode``. Every primitive, whatever its declared
topology, was emitted as an index list that the downstream packing layer divided by three and drew
as a triangle list. A strip lost its later triangles; a point cloud became one triangle.

`GLTF-071` closed the reading half: the mode is now classified, carried on ``MeshOut``, and a
topology CNA cannot yet honour is **rejected with its real mode named** instead of reinterpreted.
The conversion half -- strip and fan to a triangle list, the line topologies carried, points
decided per renderer -- is `GLTF-072`, so these fixtures stay open and their expectations
unchanged: what a conforming importer must eventually produce has not moved.

Specification: §3.7.2.1 ``meshes-overview``.
"""

from __future__ import annotations

from ..builder import MODE_NAMES, POINTS, TRIANGLE_STRIP, UNSIGNED_SHORT, GltfBuilder
from ..manifest import Defect, Fixture, l3_primitive, world_positions
from .common import QUAD_STRIP_POSITIONS

#: Every owning task, in the order they land. GLTF-071 is closed; GLTF-072 is not.
_TASKS = ["GLTF-071", "GLTF-072"]
_CLOSED = ["GLTF-071"]
_REMAINING = ["GLTF-072"]

#: A rejected import produces no semantic mesh at L3 and therefore no world geometry at L4 either.
#: ``import`` is the field name the conformance suite checks first: it means "this primitive does
#: not come out of the import path at all", which makes every other field of that layer moot.
_DIVERGENT_L3 = ["import", "mode", "modeName", "triangles"]
_DIVERGENT_L4 = ["worldPositions", "worldBounds"]


def _rejected_actual(mode: int, note: str) -> dict:
    """What CNA produces for a non-TRIANGLES primitive after `GLTF-071`."""
    return {
        "importRejected": True,
        "topologyCarried": True,
        "classifiedMode": mode,
        "classifiedModeName": MODE_NAMES[mode],
        "errorContains": [MODE_NAMES[mode], f"mode {mode}"],
        "indices": [],
        "triangles": [],
        "note": note,
    }


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
            summary="primitive.mode was never read, so a TRIANGLE_STRIP was silently reinterpreted "
                    "as a triangle list. GLTF-071 reads and classifies the mode and rejects the "
                    "topologies CNA cannot yet honour; GLTF-072 still owns converting a strip to a "
                    "triangle list, so the fixture does not import.",
            owning_tasks=_TASKS, closed_tasks=_CLOSED, remaining_tasks=_REMAINING,
            status="partially-remediated",
            divergent_fields=_DIVERGENT_L3,
            also_divergent={"L4": _DIVERGENT_L4},
            current_actual=_rejected_actual(
                TRIANGLE_STRIP,
                "ExtractMesh classifies mode 5 and throws with TRIANGLE_STRIP named. No index list "
                "reaches the numIndices/3 triangle-list path any more -- an explicit, tracked "
                "limitation instead of one silently wrong triangle. GLTF-072 converts the strip to "
                "[0,1,2] and [2,1,3], at which point this record becomes fixed."),
            prior_actual={
                "indices": [0, 1, 2, 3],
                "topologyCarried": False,
                "triangles": [[0, 1, 2]],
                "measuredOn": "fb3728267e8f2179d43b96357ff372ae712b7e7f",
                "note": "What the forensic audit measured before GLTF-071: the index list survived "
                        "intact and the topology was lost. Read as TRIANGLES, four indices yielded "
                        "one triangle and vertex 3 was unreachable.",
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
            summary="primitive.mode was never read, so a POINTS primitive became a triangle list "
                    "built from its own implicit index range. GLTF-071 reads and classifies the "
                    "mode and rejects it explicitly; GLTF-072 still owns deciding what a point "
                    "primitive becomes, so the fixture does not import.",
            owning_tasks=_TASKS, closed_tasks=_CLOSED, remaining_tasks=_REMAINING,
            status="partially-remediated",
            divergent_fields=_DIVERGENT_L3,
            also_divergent={"L4": _DIVERGENT_L4},
            current_actual=_rejected_actual(
                POINTS,
                "ExtractMesh classifies mode 0 and throws with POINTS named, before the implicit "
                "index range is even synthesised. GLTF-077 decides whether a point list becomes a "
                "CNAEXT point topology or a documented per-renderer rejection; either way the "
                "expected output stays four points and zero triangles."),
            prior_actual={
                "indices": [0, 1, 2, 3],
                "topologyCarried": False,
                "triangles": [[0, 1, 2]],
                "measuredOn": "fb3728267e8f2179d43b96357ff372ae712b7e7f",
                "note": "What the forensic audit measured before GLTF-071: implicit indices "
                        "[0,1,2,3] were generated correctly for the non-indexed primitive and then "
                        "interpreted as TRIANGLES, yielding one triangle where the expected output "
                        "is four points and zero triangles.",
            },
        )],
    )


FIXTURES = [mode_triangle_strip, mode_points]
