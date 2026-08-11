# SPDX-License-Identifier: MS-PL
"""Primitive-mode fixtures -- owning group ``topology`` (plan_gltf.md §24.2).

All seven glTF modes, one fixture each. Two of them prove **D5**: CNA never read
``primitive.mode``. Every primitive, whatever its declared topology, was emitted as an index list
that the downstream packing layer divided by three and drew as a triangle list. A strip lost its
later triangles; a point cloud became one triangle.

`GLTF-071` closed the reading half: the mode is classified, carried on ``MeshOut``, and never
assumed. `GLTF-072` closed the conversion half for the topologies that have an exact triangle-list
equivalent -- ``TRIANGLE_STRIP`` and ``TRIANGLE_FAN`` are rewritten at import with winding
preserved, so no renderer needs a new topology and the whole GPU packing layer is unchanged.

The line and point modes decode perfectly well and still do not import: what they lack is a draw
path, because every loader computes a triangle-list primitive count. `GLTF-073` (a real
``PrimitiveType`` on ``ModelMeshPart``) and `GLTF-078` (a topology-aware count) give them one, and
`GLTF-077` decides what a point list becomes. Their fixtures therefore stay open with their
expectations unchanged: what a conforming importer must eventually produce has not moved.

Specification: §3.7.2.1 ``meshes-overview``.
"""

from __future__ import annotations

from ..builder import (LINE_LOOP, LINE_STRIP, LINES, MODE_NAMES, POINTS, TRIANGLE_FAN,
                       TRIANGLE_STRIP, TRIANGLES, UNSIGNED_SHORT, GltfBuilder)
from ..l5 import unsupported as l5_unsupported
from ..manifest import Defect, Fixture, l3_primitive, world_positions
from .common import QUAD_STRIP_POSITIONS

#: D5's owning tasks, in the order they land. Both are closed; what remains for the line and point
#: modes is a draw path, which is a different problem owned by different tasks.
_TASKS = ["GLTF-071", "GLTF-072"]
_REMAINING = ["GLTF-073", "GLTF-077", "GLTF-078"]

#: A rejected import produces no semantic mesh at L3 and therefore no world geometry at L4 either.
#: ``import`` is the field name the conformance suite checks first: it means "this primitive does
#: not come out of the import path at all", which makes every other field of that layer moot.
_DIVERGENT_L3 = ["import", "mode", "modeName", "triangles"]
_DIVERGENT_L4 = ["worldPositions", "worldBounds"]

#: The fan's own geometry. A fan is authored around vertex 0, so reusing the strip quad would make
#: the two conversions produce the same triangles and neither fixture would catch the other's rule
#: being applied by mistake.
FAN_POSITIONS = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (1.0, 1.0, 0.0), (0.0, 1.0, 0.0)]

#: The line ladder's own geometry: an open polyline whose closing segment is visibly absent, so
#: LINE_LOOP and LINE_STRIP cannot be confused for one another.
LINE_POSITIONS = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (1.0, 1.0, 0.0)]


def _rejected_actual(mode: int, note: str) -> dict:
    """What CNA produces for a topology it classifies but has no draw path for."""
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


def _no_draw_path_defect(mode: int, summary: str, note: str, prior: dict | None = None) -> Defect:
    """The D5 record for a mode that classifies correctly but cannot yet be drawn."""
    return Defect(
        id="D5", owner="GLTF-MESH", first_divergent_layer="L3", summary=summary,
        owning_tasks=_TASKS, closed_tasks=list(_TASKS), remaining_tasks=_REMAINING,
        status="partially-remediated",
        divergent_fields=_DIVERGENT_L3, also_divergent={"L4": _DIVERGENT_L4},
        current_actual=_rejected_actual(mode, note), prior_actual=prior,
    )


def _line_or_point_fixture(*, fixture_id: str, mode: int, mesh_name: str, node_name: str,
                           positions: list, indices: list[int] | None, description: str,
                           features: list[str], summary: str, note: str,
                           audit_fixture: str | None = None, prior: dict | None = None,
                           blocked_by: list[str]) -> Fixture:
    """One fixture for a topology that decodes correctly but has no draw path yet."""
    b = GltfBuilder(fixture_id)
    position = b.add_packed_accessor(usage="POSITION", values=positions, accessor_type="VEC3",
                                     with_bounds=True)
    primitive: dict = {"attributes": {"POSITION": position}, "mode": mode}
    if indices is not None:
        primitive["indices"] = b.add_packed_accessor(usage="indices", values=indices,
                                                     accessor_type="SCALAR",
                                                     component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([primitive], name=mesh_name)
    node = b.add_node(name=node_name, mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id=fixture_id, audit_fixture=audit_fixture, owning_group="topology",
        description=description, builder=b, validated_layers=["L1", "L2", "L3"],
        features=features, spec_anchors=["meshes-overview"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name=mesh_name, primitive=0, mode=mode,
            positions=positions, indices=indices)]},
        l4=world_positions(b, {mesh: list(positions)}),
        l5=l5_unsupported(
            f"A {MODE_NAMES[mode]} primitive classifies correctly but does not import, so no "
            "vertex or index buffer is produced at all. The golden arrives with the draw path.",
            blocked_by),
        defects=[_no_draw_path_defect(mode, summary, note, prior)],
    )


def mode_triangles() -> Fixture:
    """``mode: 4`` -- the control case, and the only mode CNA ever handled correctly."""
    b = GltfBuilder("mode-triangles")
    position = b.add_packed_accessor(usage="POSITION", values=QUAD_STRIP_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    indices = b.add_packed_accessor(usage="indices", values=[0, 1, 2, 2, 1, 3],
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="ListQuad")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="mode-triangles", audit_fixture=None, owning_group="topology",
        description="An explicit TRIANGLES quad whose index list is exactly what the strip and fan "
                    "fixtures must convert to. It is the control for GLTF-072: a conversion that "
                    "produced this list by accident, from the wrong rule, would still have to "
                    "agree with a file that authored it directly.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4", "L5"],
        features=["primitive.mode = TRIANGLES", "explicit mode key"],
        spec_anchors=["meshes-overview"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="ListQuad", primitive=0, mode=TRIANGLES,
            positions=QUAD_STRIP_POSITIONS, indices=[0, 1, 2, 2, 1, 3])]},
        l4=world_positions(b, {mesh: list(QUAD_STRIP_POSITIONS)}),
    )


def mode_triangle_strip() -> Fixture:
    """f4 -- ``mode: 5`` (TRIANGLE_STRIP) with four indices. Proved **D5**; fixed by `GLTF-072`.

    Four strip indices describe two triangles, the second with its first two corners swapped so the
    winding is preserved. Read as a triangle list they describe one triangle and vertex 3 is never
    drawn at all -- which is exactly what CNA did before the mode was read.
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
                    "vertex 3 is dropped. GLTF-072 converts it at import, so the emitted index "
                    "list is the expansion and the winding survives.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4", "L5"],
        features=["primitive.mode = TRIANGLE_STRIP", "strip winding", "strip -> list conversion"],
        spec_anchors=["meshes-overview"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="StripQuad", primitive=0, mode=TRIANGLE_STRIP,
            positions=QUAD_STRIP_POSITIONS, indices=[0, 1, 2, 3])]},
        l4=world_positions(b, {mesh: list(QUAD_STRIP_POSITIONS)}),
        defects=[Defect(
            id="D5", owner="GLTF-MESH", first_divergent_layer="L3",
            summary="primitive.mode was never read, so a TRIANGLE_STRIP was silently reinterpreted "
                    "as a triangle list: four indices yielded one triangle and vertex 3 was "
                    "unreachable. GLTF-071 reads and classifies the mode; GLTF-072 converts the "
                    "strip to [0,1,2] and [2,1,3] at import with the odd triangle's winding "
                    "corrected, so the primitive now imports and its index list is the expansion.",
            owning_tasks=_TASKS, closed_tasks=list(_TASKS), remaining_tasks=_REMAINING,
            status="partially-remediated", divergent_fields=[],
            current_actual={
                "importRejected": False,
                "topologyCarried": True,
                "sourceMode": TRIANGLE_STRIP,
                "sourceModeName": MODE_NAMES[TRIANGLE_STRIP],
                "importedMode": TRIANGLES,
                "importedModeName": MODE_NAMES[TRIANGLES],
                "indices": [0, 1, 2, 2, 1, 3],
                "triangles": [[0, 1, 2], [2, 1, 3]],
                "note": "The strip is converted at import and nothing is suppressed any more: "
                        "GltfConformanceL3 asserts the converted index list and GltfConformanceL5 "
                        "the resulting index buffer bytes, so the reinterpretation reappearing "
                        "fails an ordinary green test. D5 stays partially-remediated because it "
                        "also owns mode-points, whose topology still has no draw path.",
            },
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


def mode_triangle_fan() -> Fixture:
    """``mode: 6`` (TRIANGLE_FAN) -- the second conversion `GLTF-072` owns."""
    b = GltfBuilder("mode-triangle-fan")
    position = b.add_packed_accessor(usage="POSITION", values=FAN_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    indices = b.add_packed_accessor(usage="indices", values=[0, 1, 2, 3], accessor_type="SCALAR",
                                    component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position},
        "indices": indices,
        "mode": TRIANGLE_FAN,
    }], name="FanQuad")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="mode-triangle-fan", audit_fixture=None, owning_group="topology",
        description="An indexed TRIANGLE_FAN quad. Four fan indices describe two triangles around "
                    "vertex 0 -- [0,1,2] and [0,2,3] -- with no winding flip anywhere, which is "
                    "what separates the fan rule from the strip rule on identical index input.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4", "L5"],
        features=["primitive.mode = TRIANGLE_FAN", "fan -> list conversion"],
        spec_anchors=["meshes-overview"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="FanQuad", primitive=0, mode=TRIANGLE_FAN,
            positions=FAN_POSITIONS, indices=[0, 1, 2, 3])]},
        l4=world_positions(b, {mesh: list(FAN_POSITIONS)}),
    )


def mode_points() -> Fixture:
    """f12 -- ``mode: 0`` (POINTS), non-indexed. Proves **D5** on the non-indexed path."""
    return _line_or_point_fixture(
        fixture_id="mode-points", mode=POINTS, mesh_name="PointCloud", node_name="MeshNode",
        positions=list(QUAD_STRIP_POSITIONS), indices=None, audit_fixture="f12",
        description="A non-indexed POINTS primitive with four vertices. A conforming importer "
                    "draws four points and no triangles at all.",
        features=["primitive.mode = POINTS", "non-indexed primitive", "implicit index range"],
        summary="primitive.mode was never read, so a POINTS primitive became a triangle list "
                "built from its own implicit index range. GLTF-071 reads and classifies the mode "
                "and rejects it explicitly rather than reinterpreting it; a point list has no "
                "triangle-list equivalent, so what remains is a draw path, not a conversion.",
        note="ExtractMesh classifies mode 0 and throws with POINTS named, before the implicit "
             "index range is even synthesised. GLTF-077 decides whether a point list becomes a "
             "CNAEXT point topology or a documented per-renderer rejection; either way the "
             "expected output stays four points and zero triangles.",
        prior={
            "indices": [0, 1, 2, 3],
            "topologyCarried": False,
            "triangles": [[0, 1, 2]],
            "measuredOn": "fb3728267e8f2179d43b96357ff372ae712b7e7f",
            "note": "What the forensic audit measured before GLTF-071: implicit indices [0,1,2,3] "
                    "were generated correctly for the non-indexed primitive and then interpreted "
                    "as TRIANGLES, yielding one triangle where the expected output is four points "
                    "and zero triangles.",
        },
        blocked_by=["GLTF-077", "GLTF-073", "GLTF-078"])


def mode_lines() -> Fixture:
    """``mode: 1`` (LINES) -- an independent segment per index pair."""
    return _line_or_point_fixture(
        fixture_id="mode-lines", mode=LINES, mesh_name="LineList", node_name="MeshNode",
        positions=list(LINE_POSITIONS), indices=[0, 1, 1, 2],
        description="An indexed LINES primitive: two independent segments sharing a vertex. XNA "
                    "has PrimitiveType::LineList, so this mode needs no conversion at all -- only "
                    "a ModelMeshPart that can carry a primitive type.",
        features=["primitive.mode = LINES", "line topology"],
        summary="primitive.mode was never read, so a LINES primitive was drawn as a triangle "
                "list. GLTF-071 classifies and rejects it. LINES maps directly onto XNA's own "
                "PrimitiveType::LineList and needs no index conversion; what it needs is "
                "GLTF-073's primitive type on ModelMeshPart and GLTF-078's topology-aware count.",
        note="ExtractMesh classifies mode 1 and throws with LINES named. The index list is "
             "already exactly what a LineList draw would consume, which is why this is a draw-path "
             "gap and not a decoding one.",
        blocked_by=["GLTF-073", "GLTF-078"])


def mode_line_strip() -> Fixture:
    """``mode: 3`` (LINE_STRIP) -- a connected, open polyline."""
    return _line_or_point_fixture(
        fixture_id="mode-line-strip", mode=LINE_STRIP, mesh_name="LineStrip", node_name="MeshNode",
        positions=list(LINE_POSITIONS), indices=[0, 1, 2],
        description="An indexed LINE_STRIP: one open polyline through three vertices, two "
                    "segments. Its twin mode-line-loop authors the identical index list and "
                    "differs only by the closing segment, so a reader that confused the two would "
                    "produce a visibly different segment count.",
        features=["primitive.mode = LINE_STRIP", "line topology"],
        summary="primitive.mode was never read, so a LINE_STRIP was drawn as a triangle list. "
                "GLTF-071 classifies and rejects it. LINE_STRIP maps directly onto XNA's own "
                "PrimitiveType::LineStrip and needs no index conversion, only GLTF-073's "
                "primitive type on ModelMeshPart and GLTF-078's topology-aware count.",
        note="ExtractMesh classifies mode 3 and throws with LINE_STRIP named. Three indices "
             "describe two segments; read as a triangle list they described one triangle.",
        blocked_by=["GLTF-073", "GLTF-078"])


def mode_line_loop() -> Fixture:
    """``mode: 2`` (LINE_LOOP) -- a polyline closed by a segment back to its first vertex."""
    return _line_or_point_fixture(
        fixture_id="mode-line-loop", mode=LINE_LOOP, mesh_name="LineLoop", node_name="MeshNode",
        positions=list(LINE_POSITIONS), indices=[0, 1, 2],
        description="An indexed LINE_LOOP over three vertices: three segments, the last closing "
                    "back to vertex 0. XNA has no LineLoop, so GLTF-076 converts it to a "
                    "LINE_STRIP plus that closing segment -- the one line mode that is a real "
                    "conversion rather than a direct mapping.",
        features=["primitive.mode = LINE_LOOP", "line topology", "implicit closing segment"],
        summary="primitive.mode was never read, so a LINE_LOOP was drawn as a triangle list. "
                "GLTF-071 classifies and rejects it. LINE_LOOP has no XNA equivalent: GLTF-076 "
                "converts it to a LINE_STRIP with the closing segment appended, which needs "
                "GLTF-073's primitive type on ModelMeshPart to have anywhere to go.",
        note="ExtractMesh classifies mode 2 and throws with LINE_LOOP named. The closing segment "
             "is implicit in the mode itself, so a reader that dropped the mode lost a segment "
             "even before it lost the topology.",
        blocked_by=["GLTF-073", "GLTF-076", "GLTF-078"])


FIXTURES = [mode_points, mode_lines, mode_line_loop, mode_line_strip, mode_triangles,
            mode_triangle_strip, mode_triangle_fan]
