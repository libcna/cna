# SPDX-License-Identifier: MS-PL
"""Fixture records, expectation manifests, and the L4 world-transform oracle (GLTF-003/GLTF-006).

The matrix helpers here implement glTF's own convention exactly as specified in §3.5.3
(``transformations``): a flat 16-float **column-major** array with the column-vector convention
``v' = M * v``, composed as ``local = T * R * S`` and ``world(node) = world(parent) * local(node)``.

They are the generator half of the L4 oracle. ``EvaluateWorldPositionsEXT`` on the C++ side
recomputes the same quantities independently, so a disagreement between the two is itself a signal
rather than something that can silently cancel out.
"""

from __future__ import annotations

import json
import math
from dataclasses import dataclass, field
from typing import Any, Sequence

from . import GENERATOR_VERSION, SPEC_PIN
from .builder import GltfBuilder

Matrix = list[float]


def mat_identity() -> Matrix:
    """The 4x4 identity, in glTF column-major order."""
    return [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0]


def mat_translation(t: Sequence[float]) -> Matrix:
    """Translation matrix, glTF column-major."""
    m = mat_identity()
    m[12], m[13], m[14] = float(t[0]), float(t[1]), float(t[2])
    return m


def mat_scale(s: Sequence[float]) -> Matrix:
    """Scale matrix, glTF column-major."""
    m = mat_identity()
    m[0], m[5], m[10] = float(s[0]), float(s[1]), float(s[2])
    return m


def mat_rotation(q: Sequence[float]) -> Matrix:
    """Rotation matrix from a glTF ``(x, y, z, w)`` unit quaternion, glTF column-major."""
    x, y, z, w = (float(c) for c in q)
    return [
        1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0.0,
        2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0.0,
        2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0.0,
        0.0, 0.0, 0.0, 1.0,
    ]


def mat_mul(a: Matrix, b: Matrix) -> Matrix:
    """``a * b`` for column-major, column-vector matrices: ``(a*b)v == a(bv)``."""
    out = [0.0] * 16
    for col in range(4):
        for row in range(4):
            out[col * 4 + row] = sum(a[k * 4 + row] * b[col * 4 + k] for k in range(4))
    return out


def mat_from_trs(translation: Sequence[float] | None, rotation: Sequence[float] | None,
                 scale: Sequence[float] | None) -> Matrix:
    """``T * R * S`` from the TRS properties, applying their glTF defaults when absent (§3.5.3)."""
    t = mat_translation(translation) if translation is not None else mat_identity()
    r = mat_rotation(rotation) if rotation is not None else mat_identity()
    s = mat_scale(scale) if scale is not None else mat_identity()
    return mat_mul(mat_mul(t, r), s)


def node_local_matrix(node: dict[str, Any]) -> Matrix:
    """A node's local transform: its ``matrix`` if present, otherwise ``T * R * S`` (§3.5.3)."""
    if "matrix" in node:
        return [float(v) for v in node["matrix"]]
    return mat_from_trs(node.get("translation"), node.get("rotation"), node.get("scale"))


def transform_point(m: Matrix, p: Sequence[float]) -> list[float]:
    """Transforms the point ``p`` by ``m`` (column-vector convention, implicit ``w = 1``)."""
    x, y, z = (float(c) for c in p)
    return [
        m[0] * x + m[4] * y + m[8] * z + m[12],
        m[1] * x + m[5] * y + m[9] * z + m[13],
        m[2] * x + m[6] * y + m[10] * z + m[14],
    ]


def _clean(value: Any) -> Any:
    """Normalises ``-0.0`` to ``0.0`` so manifests stay readable; otherwise value-preserving."""
    if isinstance(value, float):
        return 0.0 if value == 0.0 else value
    if isinstance(value, list):
        return [_clean(v) for v in value]
    if isinstance(value, dict):
        return {k: _clean(v) for k, v in value.items()}
    return value


@dataclass
class MeshInstance:
    """One (node, mesh) pair reachable from the default scene -- the unit the L4 oracle works in."""

    node: int
    node_name: str
    mesh: int
    world_matrix: Matrix
    parent_path: list[int]


@dataclass
class Defect:
    """A proven CNA defect this fixture exposes, with the evidence recorded separately from truth.

    ``current_actual`` is what CNA produces **today**. It is never an expectation: it is dated
    evidence that lets a later test assert "still broken in exactly the documented way", so the
    remediation task that fixes it fails loudly here instead of passing silently.
    """

    id: str
    summary: str
    first_divergent_layer: str
    owner: str
    owning_tasks: list[str]
    current_actual: dict[str, Any]
    #: Exactly which fields of ``first_divergent_layer`` this defect breaks. The conformance tests
    #: skip these and only these, so a defect confined to one field (D7 loses the material and
    #: nothing else) never suppresses checking of the fields that are correct.
    divergent_fields: list[str] = field(default_factory=list)
    status: str = "known-failing"


@dataclass
class Fixture:
    """One corpus asset: the builder that produced it plus every layer's expectation."""

    id: str
    description: str
    owning_group: str
    builder: GltfBuilder
    validated_layers: list[str]
    features: list[str] = field(default_factory=list)
    referencing_groups: list[str] = field(default_factory=list)
    spec_anchors: list[str] = field(default_factory=list)
    audit_fixture: str | None = None
    l3: dict[str, Any] = field(default_factory=dict)
    l4: dict[str, Any] = field(default_factory=dict)
    defects: list[Defect] = field(default_factory=list)

    def inventory(self) -> dict[str, Any]:
        """The §24.1 inventory record: one canonical id, exactly one owning group."""
        return {
            "id": self.id,
            "owningGroup": self.owning_group,
            "referencingGroups": list(self.referencing_groups),
            "validatedLayers": list(self.validated_layers),
            "features": list(self.features),
            "auditFixture": self.audit_fixture,
        }

    def expectation(self) -> dict[str, Any]:
        """The complete ``<id>.expected.json`` document."""
        doc: dict[str, Any] = {
            "id": self.id,
            "description": self.description,
            "generator": {
                "tool": "tools/gltf_fixtures",
                "version": GENERATOR_VERSION,
                "task": "GLTF-003",
            },
            "specPin": dict(SPEC_PIN),
            "specAnchors": list(self.spec_anchors),
            "inventory": self.inventory(),
            "l1": self.builder.l1_expectation(),
            "l2": {"accessors": self.builder.accessor_records},
            "l3": self.l3,
            "l4": self.l4,
            "defects": [
                {
                    "id": d.id,
                    "summary": d.summary,
                    "firstDivergentLayer": d.first_divergent_layer,
                    "divergentFields": list(d.divergent_fields),
                    "owner": d.owner,
                    "owningTasks": list(d.owning_tasks),
                    "status": d.status,
                    "currentActual": d.current_actual,
                }
                for d in self.defects
            ],
        }
        return _clean(doc)


def scene_mesh_instances(builder: GltfBuilder) -> list[MeshInstance]:
    """Every mesh instance reachable from the default scene, with its composed world matrix.

    This is the generator-side L4 oracle: it walks the authored node graph exactly as §3.5.3
    prescribes and never consults CNA.
    """
    doc = builder.document
    nodes = doc.get("nodes", [])
    scenes = doc.get("scenes", [])
    if not scenes:
        return []
    default_scene = doc.get("scene", 0)
    instances: list[MeshInstance] = []

    def visit(index: int, parent_world: Matrix, path: list[int]) -> None:
        node = nodes[index]
        world = mat_mul(parent_world, node_local_matrix(node))
        if "mesh" in node:
            instances.append(MeshInstance(
                node=index,
                node_name=node.get("name", f"node{index}"),
                mesh=node["mesh"],
                world_matrix=world,
                parent_path=list(path),
            ))
        for child in node.get("children", []):
            visit(child, world, path + [index])

    for root in scenes[default_scene].get("nodes", []):
        visit(root, mat_identity(), [])
    return instances


def world_positions(builder: GltfBuilder, mesh_local_positions: dict[int, list[list[float]]]
                    ) -> dict[str, Any]:
    """Builds the L4 expectation from the node graph and each mesh's local positions.

    ``mesh_local_positions`` maps a mesh index to its concatenated per-primitive positions.
    """
    instances = scene_mesh_instances(builder)
    records: list[dict[str, Any]] = []
    all_points: list[list[float]] = []
    for inst in instances:
        local = mesh_local_positions.get(inst.mesh, [])
        transformed = [transform_point(inst.world_matrix, p) for p in local]
        all_points.extend(transformed)
        records.append({
            "node": inst.node,
            "nodeName": inst.node_name,
            "mesh": inst.mesh,
            "parentNodePath": inst.parent_path,
            "worldMatrixColumnMajor": inst.world_matrix,
            "worldPositions": transformed,
        })
    bounds: dict[str, Any] = {}
    if all_points:
        bounds = {
            "min": [min(p[c] for p in all_points) for c in range(3)],
            "max": [max(p[c] for p in all_points) for c in range(3)],
        }
    return {"instances": records, "worldBounds": bounds}


def dumps(document: Any) -> str:
    """Deterministic JSON serialisation used for every emitted manifest."""
    return json.dumps(document, indent=2, ensure_ascii=True, allow_nan=False) + "\n"


def assert_finite(values: Sequence[float], context: str) -> None:
    """Guards a generated expectation against NaN/inf, which would make a manifest unusable."""
    for v in values:
        if not math.isfinite(v):
            raise ValueError(f"{context}: non-finite generated value {v!r}")


def l3_primitive(*, mesh: int, mesh_name: str, primitive: int, mode: int,
                 positions: Sequence[Sequence[float]],
                 normals: Sequence[Sequence[float]] | None = None,
                 texcoords: Sequence[Sequence[float]] | None = None,
                 colors: Sequence[Sequence[float]] | None = None,
                 joints: Sequence[Sequence[float]] | None = None,
                 weights: Sequence[Sequence[float]] | None = None,
                 indices: Sequence[int] | None = None,
                 material: dict[str, Any] | None = None) -> dict[str, Any]:
    """One primitive's spec-correct semantic mesh record -- the L3 expectation.

    ``indices`` is the resolved index list a conforming reader must produce (``None`` for a
    non-indexed primitive, whose implicit indices are ``[0, count)``). ``triangles`` is derived
    from ``mode`` per §3.7.2.1 and is empty for the point and line modes.
    """
    from .builder import MODE_NAMES, expand_to_triangles

    resolved = list(indices) if indices is not None else list(range(len(positions)))
    return {
        "mesh": mesh,
        "meshName": mesh_name,
        "primitive": primitive,
        "mode": mode,
        "modeName": MODE_NAMES[mode],
        "indexed": indices is not None,
        "vertexCount": len(positions),
        "positions": [list(p) for p in positions],
        "normals": [list(n) for n in (normals or [])],
        "texcoords": [list(t) for t in (texcoords or [])],
        "colors": [list(c) for c in (colors or [])],
        "joints": [list(j) for j in (joints or [])],
        "weights": [list(w) for w in (weights or [])],
        "indices": resolved,
        "triangles": expand_to_triangles(resolved, mode),
        "material": material,
    }
