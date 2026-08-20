# SPDX-License-Identifier: MS-PL
"""Primitive-mode fixtures -- owning group ``topology`` (plans/plan_gltf.md §24.2).

All seven glTF modes, one fixture each. Two of them prove **D5**: CNA never read
``primitive.mode``. Every primitive, whatever its declared topology, was emitted as an index list
that the downstream packing layer divided by three and drew as a triangle list. A strip lost its
later triangles; a point cloud became one triangle.

`GLTF-071` closed the reading half: the mode is classified, carried on ``MeshOut``, and never
assumed. `GLTF-072` closed the conversion half for the topologies that have an exact triangle-list
equivalent -- ``TRIANGLE_STRIP`` and ``TRIANGLE_FAN`` are rewritten at import with winding
preserved, so no renderer needs a new topology and the whole GPU packing layer is unchanged.

`GLTF-073`/`GLTF-076`/`GLTF-078` then gave the remaining four a draw path: the part carries a real
XNA ``PrimitiveType``, the primitive count follows §12.3's table instead of ``numIndices / 3``, and a
``LINE_LOOP`` becomes a ``LINE_STRIP`` with the closing segment glTF leaves implicit in the mode.
All seven modes now import, so **D5 is fixed** and every fixture here is an ordinary conformance
asset whose expectation is asserted in full.

Specification: §3.7.2.1 ``meshes-overview``.
"""

from __future__ import annotations

from ..builder import (LINE_LOOP, LINE_STRIP, LINES, MODE_NAMES, POINTS, TRIANGLE_FAN,
                       TRIANGLE_STRIP, TRIANGLES, UNSIGNED_SHORT, GltfBuilder)
from ..manifest import Defect, Fixture, l3_primitive, world_positions
from .. import flatnormals
from .common import QUAD_STRIP_POSITIONS

#: D5's owning tasks, in the order they landed: read the mode, convert what converts, then give the
#: rest somewhere to be drawn.
_TASKS = ["GLTF-071", "GLTF-072", "GLTF-073", "GLTF-076", "GLTF-078"]

#: The fan's own geometry. A fan is authored around vertex 0, so reusing the strip quad would make
#: the two conversions produce the same triangles and neither fixture would catch the other's rule
#: being applied by mistake.
FAN_POSITIONS = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (1.0, 1.0, 0.0), (0.0, 1.0, 0.0)]

#: The line ladder's own geometry: an open polyline whose closing segment is visibly absent, so
#: LINE_LOOP and LINE_STRIP cannot be confused for one another.
LINE_POSITIONS = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (1.0, 1.0, 0.0)]


def _line_or_point_fixture(*, fixture_id: str, mode: int, mesh_name: str, node_name: str,
                           positions: list, indices: list[int] | None, description: str,
                           features: list[str], defects: list[Defect] | None = None,
                           audit_fixture: str | None = None) -> Fixture:
    """One fixture for a topology that describes no triangles.

    All four import as themselves since `GLTF-073`/`GLTF-076`/`GLTF-078`: the part carries a real
    XNA ``PrimitiveType``, the primitive count follows §12.3 rather than ``numIndices / 3``, and a
    ``LINE_LOOP`` arrives as a ``LINE_STRIP`` with its closing segment appended.
    """
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
        description=description, builder=b, validated_layers=["L1", "L2", "L3", "L4", "L5"],
        features=features, spec_anchors=["meshes-overview"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name=mesh_name, primitive=0, mode=mode,
            positions=positions, indices=indices)]},
        l4=world_positions(b, {mesh: list(positions)}),
        defects=list(defects or []),
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
            summary="primitive.mode was never read, so every topology was decoded into a flat "
                    "index list that all three loaders divided by three and drew as a triangle "
                    "list: a TRIANGLE_STRIP lost every triangle after the first, a point cloud "
                    "became one arbitrary triangle, and neither warned. GLTF-071 reads and "
                    "classifies the mode; GLTF-072 converts a strip or fan to a triangle list with "
                    "winding preserved; GLTF-073/GLTF-076/GLTF-078 give the four topologies that "
                    "describe no triangles a real draw path -- a PrimitiveType on ModelMeshPart, a "
                    "§12.3 primitive count, and a LINE_LOOP's implicit closing segment.",
            owning_tasks=_TASKS, closed_tasks=list(_TASKS), remaining_tasks=[],
            status="fixed", divergent_fields=[],
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
                        "fails an ordinary green test rather than needing an inverted one. All "
                        "seven modes now import -- the four that describe no triangles as "
                        "themselves, with a real PrimitiveType on the part and a §12.3 primitive "
                        "count (GLTF-073/GLTF-076/GLTF-078) -- so D5 is fixed.",
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
        description="A non-indexed POINTS primitive with four vertices, whose implicit indices are "
                    "[0,count). A conforming importer draws four points and no triangles at all; "
                    "read as a triangle list those four implicit indices became one triangle.",
        features=["primitive.mode = POINTS", "non-indexed primitive", "implicit index range"])


def mode_lines() -> Fixture:
    """``mode: 1`` (LINES) -- an independent segment per index pair."""
    return _line_or_point_fixture(
        fixture_id="mode-lines", mode=LINES, mesh_name="LineList", node_name="MeshNode",
        positions=list(LINE_POSITIONS), indices=[0, 1, 1, 2],
        description="An indexed LINES primitive: two independent segments sharing a vertex. XNA "
                    "has PrimitiveType::LineList, so this mode needs no index conversion at all "
                    "-- only a ModelMeshPart able to carry a primitive type, which GLTF-073 added. "
                    "Four indices describe two segments; read as a triangle list they described "
                    "one triangle and vertex 3 was unreachable.",
        features=["primitive.mode = LINES", "line topology"])


def mode_line_strip() -> Fixture:
    """``mode: 3`` (LINE_STRIP) -- a connected, open polyline."""
    return _line_or_point_fixture(
        fixture_id="mode-line-strip", mode=LINE_STRIP, mesh_name="LineStrip", node_name="MeshNode",
        positions=list(LINE_POSITIONS), indices=[0, 1, 2],
        description="An indexed LINE_STRIP: one open polyline through three vertices, two "
                    "segments. Its twin mode-line-loop authors the identical index list and "
                    "differs only by the closing segment, so a reader that confused the two would "
                    "produce a visibly different segment count.",
        features=["primitive.mode = LINE_STRIP", "line topology"])


def mode_line_loop() -> Fixture:
    """``mode: 2`` (LINE_LOOP) -- a polyline closed by a segment back to its first vertex."""
    return _line_or_point_fixture(
        fixture_id="mode-line-loop", mode=LINE_LOOP, mesh_name="LineLoop", node_name="MeshNode",
        positions=list(LINE_POSITIONS), indices=[0, 1, 2],
        description="An indexed LINE_LOOP over three vertices: three segments, the last closing "
                    "back to vertex 0. XNA has no LineLoop, so GLTF-076 converts it to a "
                    "LINE_STRIP with that closing index appended -- the one line mode that is a "
                    "real conversion rather than a direct mapping, and the one where dropping the "
                    "mode lost a whole segment before it lost the topology.",
        features=["primitive.mode = LINE_LOOP", "line topology", "implicit closing segment"])


#: `mode-triangle-strip-morph`'s morph deltas, one per strip vertex and all four distinct. Distinct
#: is the point: a delta array read at the wrong index still produces a morphed mesh, so identical
#: deltas would make a reordering indistinguishable from correctness.
_STRIP_MORPH_DELTAS = [(0.0, 0.0, 1.0), (0.0, 0.0, 2.0), (0.0, 0.0, 3.0), (0.0, 0.0, 4.0)]


def mode_triangle_strip_morph() -> Fixture:
    """A ``TRIANGLE_STRIP`` primitive that also carries a morph target. Owns **GLTF-081**.

    Converting a strip to a triangle list rewrites the **index** list -- ``[0,1,2,3]`` becomes
    ``[0,1,2, 2,1,3]`` -- and morph deltas are addressed **per vertex**, by position in the target
    accessor. So the conversion is only safe if it leaves the vertex order completely alone, and
    this fixture is where that becomes an assertion rather than an assumption.

    The failure it guards against is not a crash. A conversion that also compacted or reordered
    vertices -- de-duplicating the strip's shared corners is the obvious temptation, since vertices
    1 and 2 appear in both triangles -- would still produce a mesh, still produce a morph, and
    apply every delta to the wrong vertex. The result deforms plausibly and wrongly.

    Each delta moves its vertex a different distance along +Z, so the morphed shape is a staircase
    whose steps say which vertex received which delta. A permutation of them is visible as a
    different staircase, not as a subtly different surface.
    """
    b = GltfBuilder("mode-triangle-strip-morph")
    position = b.add_packed_accessor(usage="POSITION", values=QUAD_STRIP_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    target_position = b.add_packed_accessor(usage="morph POSITION delta",
                                            values=_STRIP_MORPH_DELTAS,
                                            accessor_type="VEC3", with_bounds=True)
    indices = b.add_packed_accessor(usage="indices", values=[0, 1, 2, 3], accessor_type="SCALAR",
                                    component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position},
        "indices": indices,
        "targets": [{"POSITION": target_position}],
        "mode": TRIANGLE_STRIP,
    }], name="MorphedStripQuad", weights=[1.0])
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)

    # GLTF-461: the primitive authors no NORMAL and carries a morph target, so §3.7.2.2 requires
    # flat normals PER TARGET and every corner becomes its own vertex. The remap is stated
    # independently here so the delta pairing can be checked against it rather than against CNA.
    split = flatnormals.compute(QUAD_STRIP_POSITIONS, [0, 1, 2, 2, 1, 3], per_corner=True)
    split_positions = flatnormals.gather(QUAD_STRIP_POSITIONS, split.source_vertex)
    split_deltas = flatnormals.gather(_STRIP_MORPH_DELTAS, split.source_vertex)
    fully_morphed = [
        [p[0] + d[0], p[1] + d[1], p[2] + d[2]]
        for p, d in zip(split_positions, split_deltas)
    ]
    # l4.worldPositions stays in AUTHORED numbering: it is the spec's own reading of the file, and
    # the CNA comparison maps through `importPolicy.flatNormalSplit.sourceVertex` rather than being
    # handed a pre-split answer. Stating the split here as well would make the comparison agree with
    # itself.
    l4 = world_positions(b, {mesh: list(QUAD_STRIP_POSITIONS)})
    l4["topologyMorph"] = {
        "sourceTopology": "TRIANGLE_STRIP",
        "importedTopology": "TRIANGLES",
        "authoredIndices": [0, 1, 2, 3],
        "convertedIndices": [0, 1, 2, 2, 1, 3],
        "splitIndices": list(split.indices),
        "sourceVertex": list(split.source_vertex),
        "vertexCount": split.vertex_count,
        "authoredVertexCount": len(QUAD_STRIP_POSITIONS),
        "vertexOrderUnchanged": False,
        "authoredMorphDeltas": [list(d) for d in _STRIP_MORPH_DELTAS],
        "morphDeltas": [list(d) for d in split_deltas],
        "fullyMorphedPositions": fully_morphed,
        "rule": "Converting a strip rewrites the INDEX list and must not renumber vertices on its "
                "own -- morph deltas are addressed per vertex by position in the target accessor, "
                "and de-duplicating the strip's shared corners would put every delta on the wrong "
                "one. What DOES renumber here is §3.7.2.1's flat-normal split (GLTF-461): the "
                "primitive authors no NORMAL, so each of the two triangles needs its own normal at "
                "every morph weight, and every delta must follow its source vertex through the "
                "split. `sourceVertex` states that mapping, so the pairing is checkable rather "
                "than assumed.",
        "discriminationRule": "The four authored deltas are all different, so a permutation is a "
                              "different staircase rather than a subtly different surface. They "
                              "also make the two triangles NON-COPLANAR once applied, which is "
                              "exactly why a rest-pose split cannot serve every pose and the "
                              "per-corner policy is required.",
    }
    return Fixture(
        id="mode-triangle-strip-morph", audit_fixture=None, owning_group="topology",
        description="A TRIANGLE_STRIP quad carrying a morph target with a distinct delta per "
                    "vertex. The strip-to-list conversion rewrites the index list; the vertex "
                    "order it must not touch is exactly what the per-vertex deltas are addressed "
                    "by.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4", "L5"],
        features=["primitive.mode = TRIANGLE_STRIP", "morph target", "strip -> list conversion",
                  "per-vertex delta addressing"],
        spec_anchors=["meshes-overview", "morph-targets"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="MorphedStripQuad", primitive=0, mode=TRIANGLE_STRIP,
            positions=QUAD_STRIP_POSITIONS, indices=[0, 1, 2, 3],
            flat_normals="per-corner")]},
        l4=l4,
    )


FIXTURES = [mode_points, mode_lines, mode_line_loop, mode_line_strip, mode_triangles,
            mode_triangle_strip, mode_triangle_strip_morph, mode_triangle_fan]
