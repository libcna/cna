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
from urllib.parse import unquote

from . import GENERATOR_VERSION, SPEC_PIN, l5
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


#: Every state a defect record may be in. A record is **never deleted** -- a remediated defect
#: stays in the corpus as the regression witness that proves it has not come back.
#:
#: ``known-failing``        no owning task has landed; CNA is wrong in exactly the recorded way and
#:                          a ``GltfKnownDefect`` test asserts that.
#: ``partially-remediated`` one owning task landed and changed the behaviour, but the defect is not
#:                          fully fixed. ``closed_tasks`` names what landed, ``remaining_tasks``
#:                          what is left, and ``current_actual`` describes the *new* behaviour.
#: ``fixed``                every owning task landed. ``divergent_fields`` is empty, so the
#:                          conformance suite asserts the layer in full and the defect reappearing
#:                          fails an ordinary green test.
DEFECT_STATUSES = ("known-failing", "partially-remediated", "fixed")

#: The statuses under which a defect still suppresses conformance assertions.
OPEN_DEFECT_STATUSES = ("known-failing", "partially-remediated")


@dataclass
class Defect:
    """A proven CNA defect this fixture exposes, with the evidence recorded separately from truth.

    ``current_actual`` is what CNA produces **today**. It is never an expectation: it is dated
    evidence that lets a later test assert "still broken in exactly the documented way", so the
    remediation task that fixes it fails loudly here instead of passing silently. When a task lands
    and changes the behaviour, ``current_actual`` is updated to the new behaviour and the value it
    replaced moves to ``prior_actual`` -- the history is kept, never overwritten.
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
    #: Fields this defect breaks at layers *other* than ``first_divergent_layer``, keyed by layer.
    #: A defect usually diverges at exactly one layer, but not always: an import CNA rejects
    #: outright produces no semantic mesh at L3 *and* no world geometry at L4.
    also_divergent: dict[str, list[str]] = field(default_factory=dict)
    status: str = "known-failing"
    #: Owning tasks that have landed. Empty while the defect is untouched.
    closed_tasks: list[str] = field(default_factory=list)
    #: Owning tasks still to land before the defect can be marked ``fixed``.
    remaining_tasks: list[str] = field(default_factory=list)
    #: What CNA produced before the most recent ``closed_tasks`` entry landed. Dated evidence: it
    #: keeps the original forensic measurement readable after the behaviour has moved on.
    prior_actual: dict[str, Any] | None = None

    def divergent_by_layer(self) -> dict[str, list[str]]:
        """Every layer this defect breaks, mapped to the fields it breaks there."""
        out: dict[str, list[str]] = {}
        if self.divergent_fields:
            out[self.first_divergent_layer] = list(self.divergent_fields)
        for layer, fields in self.also_divergent.items():
            merged = out.setdefault(layer, [])
            merged.extend(f for f in fields if f not in merged)
        return {layer: out[layer] for layer in sorted(out)}

    def record(self) -> dict[str, Any]:
        """The emitted ``defects[]`` entry."""
        if self.status not in DEFECT_STATUSES:
            raise ValueError(f"{self.id}: unknown defect status {self.status!r}")
        if self.status == "fixed" and self.divergent_by_layer():
            raise ValueError(
                f"{self.id}: a fixed defect may not still declare divergent fields -- a fixed "
                "defect suppresses nothing, which is what makes it a regression witness")
        if self.status == "known-failing" and not self.divergent_by_layer():
            raise ValueError(
                f"{self.id}: an open defect must name the fields it breaks, or the conformance "
                "suite would assert them and fail")
        # A `partially-remediated` defect may legitimately declare nothing on a fixture the landed
        # tasks fully resolved -- that is what "partial" means once a defect spans several
        # fixtures. GLTF-072 converts mode-triangle-strip correctly while mode-points still has no
        # draw path, so D5 bites one of its two fixtures and not the other. The invariant that
        # actually matters is corpus-level and lives in corpus.py: a partially-remediated defect
        # must still be divergent on at least one fixture, or it is simply fixed.
        entry: dict[str, Any] = {
            "id": self.id,
            "summary": self.summary,
            "firstDivergentLayer": self.first_divergent_layer,
            "divergentFields": list(self.divergent_fields),
            "divergentFieldsByLayer": self.divergent_by_layer(),
            "owner": self.owner,
            "owningTasks": list(self.owning_tasks),
            "closedTasks": list(self.closed_tasks),
            "remainingTasks": list(self.remaining_tasks),
            "status": self.status,
            "currentActual": self.current_actual,
        }
        if self.prior_actual is not None:
            entry["priorActual"] = self.prior_actual
        return entry


@dataclass
class GltfEmission:
    """How a fixture's text container differs from the self-contained default.

    ``None`` as ``buffer_uri`` means the builder's ordinary base64 data URI. Image overrides are
    applied only to the `.gltf`; the `.glb` twin retains the builder-authored image source. Every
    external URI is accompanied by a flat sidecar entry, emitted and hashed with the corpus.

    A fixture sets this record even when it wants the defaults if the source shape itself is what
    the fixture proves. That makes :meth:`Fixture.container_expectation` appear only for the seven
    named container fixtures instead of churning every existing expectation manifest.
    """

    buffer_uri: str | None = None
    image_uri_overrides: dict[int, str] = field(default_factory=dict)
    sidecars: dict[str, bytes] = field(default_factory=dict)


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
    #: Explicit text-container source form and any files it references. ``None`` retains the
    #: historical self-contained emission without adding a container contract to the manifest.
    gltf_emission: GltfEmission | None = None
    #: Why this asset is allowed past the per-asset size budget (`GLTF-419`), or ``None``.
    #: A corpus fixture is meant to be small enough to read; one that cannot be needs a stated
    #: reason rather than an exemption list in a test, which is how a budget quietly stops binding.
    size_exemption: str | None = None
    #: Distinct severity-0 codes the pinned Khronos Validator must report for each container.
    #: Empty means the asset must have zero validation errors. A non-empty list is reserved for
    #: an intentionally malformed robustness witness and requires a human-readable reason; this
    #: turns a validator failure into an explicit oracle instead of an allow-list wildcard.
    validator_expected_errors: list[str] = field(default_factory=list)
    validator_exception_reason: str | None = None
    l3: dict[str, Any] = field(default_factory=dict)
    l4: dict[str, Any] = field(default_factory=dict)
    #: The L5 golden expectation (`GLTF-007`). Left ``None`` it is derived from the fixture's own
    #: L3 primitives, which is right for every asset CNA imports; a fixture CNA rejects sets it
    #: explicitly to ``l5.unsupported(...)`` naming what is blocking it.
    l5: dict[str, Any] | None = None
    #: For a fixture the importer must **refuse**: which stage rejects it and what the diagnostic
    #: has to name (`GLTF-021` … `GLTF-023`). A rejection fixture asserts the error, not the
    #: geometry -- "it failed to load" is worthless unless the message says why.
    rejection: dict[str, Any] | None = None
    defects: list[Defect] = field(default_factory=list)

    @staticmethod
    def _uri_source(uri: str) -> dict[str, Any]:
        """A compact manifest record for a URI without copying a large base64 payload into it."""
        if uri.startswith("data:"):
            prefix = uri.split(",", 1)[0] + ("," if "," in uri else "")
            return {"source": "data-uri", "uriPrefix": prefix}
        return {"source": "external", "uri": uri}

    def container_expectation(self) -> dict[str, Any] | None:
        """Derives the container/source contract from the exact objects the emitter consumes."""
        emission = self.gltf_emission
        if emission is None:
            return None

        buffer_uri = emission.buffer_uri
        if buffer_uri is None:
            buffer_uri = "data:application/octet-stream;base64,"
        gltf_images: list[dict[str, Any]] = []
        glb_images: list[dict[str, Any]] = []
        referenced_sidecars: set[str] = set()
        if not buffer_uri.startswith("data:"):
            referenced_sidecars.add(unquote(buffer_uri))
        for index, image in enumerate(self.builder.document.get("images", [])):
            authored_uri = str(image.get("uri", ""))
            text_uri = emission.image_uri_overrides.get(index, authored_uri)
            if text_uri and not text_uri.startswith("data:"):
                referenced_sidecars.add(unquote(text_uri))
            gltf_images.append({"index": index, **self._uri_source(text_uri)})
            glb_images.append({"index": index, **self._uri_source(authored_uri)})

        emitted_sidecars = set(emission.sidecars)
        if referenced_sidecars != emitted_sidecars:
            missing = sorted(referenced_sidecars - emitted_sidecars)
            unreferenced = sorted(emitted_sidecars - referenced_sidecars)
            raise ValueError(
                f"{self.id}: text-container sidecars disagree with external URIs "
                f"(missing={missing}, unreferenced={unreferenced})")

        payload_bytes = len(self.builder.buffer_bytes)
        return {
            "gltf": {
                "buffer": self._uri_source(buffer_uri),
                "images": gltf_images,
            },
            "glb": {
                "buffer": {
                    "source": "BIN",
                    "payloadBytes": payload_bytes,
                    "paddingBytes": (-payload_bytes) % 4,
                },
                "images": glb_images,
            },
            "sidecars": [
                {"path": path, "bytes": len(data)}
                for path, data in sorted(emission.sidecars.items())
            ],
        }

    def l5_expectation(self) -> tuple[dict[str, Any], dict[str, bytes]]:
        """The ``l5`` block and the golden buffer files it references."""
        if self.l5 is not None:
            return self.l5, {}
        primitives = self.l3.get("primitives", [])
        if not primitives:
            return l5.unsupported("the fixture declares no importable primitive", []), {}
        return l5.buffers(self.id, primitives)

    def inventory(self) -> dict[str, Any]:
        """The §24.1 inventory record: one canonical id, exactly one owning group."""
        record: dict[str, Any] = {
            "id": self.id,
            "owningGroup": self.owning_group,
            "referencingGroups": list(self.referencing_groups),
            "validatedLayers": list(self.validated_layers),
            "features": list(self.features),
            "auditFixture": self.audit_fixture,
        }
        # Emitted only by an asset that actually needs one, so adding the field churned exactly one
        # fixture's expectation instead of all 71 (`GLTF-419`).
        if self.size_exemption is not None:
            record["sizeExemptionReason"] = self.size_exemption
        if self.validator_expected_errors:
            if not self.validator_exception_reason:
                raise ValueError(
                    f"{self.id}: expected Validator errors need a stated exception reason")
            if len(set(self.validator_expected_errors)) != len(self.validator_expected_errors):
                raise ValueError(f"{self.id}: duplicate expected Validator error code")
            record["validatorExpectedErrorCodes"] = sorted(self.validator_expected_errors)
            record["validatorExceptionReason"] = self.validator_exception_reason
        elif self.validator_exception_reason is not None:
            raise ValueError(
                f"{self.id}: Validator exception reason exists without an expected error")
        return record

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
            "l5": self.l5_expectation()[0],
            "rejection": self.rejection,
            "defects": [d.record() for d in self.defects],
        }
        container = self.container_expectation()
        if container is not None:
            # Conditional by design: only a fixture whose subject is its source/container form
            # carries this block, keeping all unrelated generated files byte-identical.
            doc["container"] = container
        return _clean(doc)


def scene_mesh_instances(builder: GltfBuilder) -> list[MeshInstance]:
    """Every mesh instance reachable from the default scene, with its composed world matrix.

    This is the generator-side L4 oracle: it walks the authored node graph exactly as §3.5.3
    prescribes and never consults CNA.
    """
    doc = builder.document
    nodes = doc.get("nodes", [])
    scenes = doc.get("scenes", [])
    default_scene = doc.get("scene", 0)
    if not scenes:
        # §3.5 permits a file with no `scenes` at all and defines it as "nothing is REQUIRED to be
        # rendered" -- which is not "nothing may be". CNA's decision is to import every root node,
        # the reading every viewer takes, and the oracle mirrors it deliberately rather than
        # returning nothing: an expectation of zero instances would make `scene-no-scenes` assert
        # that CNA does the opposite of what it documents.
        child_indices = {c for node in nodes for c in node.get("children", [])}
        roots = [i for i in range(len(nodes)) if i not in child_indices]
        scenes = [{"nodes": roots}]
        default_scene = 0
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


def world_positions(
        builder: GltfBuilder,
        mesh_local_positions: dict[int | tuple[int, int], list[list[float]]]) -> dict[str, Any]:
    """Builds the L4 expectation from the node graph and each primitive's local positions.

    A key ``mesh`` is the concise form for primitive zero and keeps every single-primitive fixture
    readable. A multi-primitive mesh uses ``(mesh, primitive)`` keys. The emitted L4 unit is one
    ``(node, mesh, primitive)`` placement, matching the importer and preventing a second primitive
    from being hidden inside the first one's concatenated position array.
    """
    instances = scene_mesh_instances(builder)
    nodes = builder.document.get("nodes", [])
    meshes = builder.document.get("meshes", [])
    records: list[dict[str, Any]] = []
    all_points: list[list[float]] = []
    for inst in instances:
        # A SKINNED mesh is not placed by its own node. Specification §3.7.3: the joints place it,
        # and the joint matrix carries inverse(globalTransform(meshNode)) precisely so that node's
        # transform cancels out. Reporting the node-placed positions here would make the L4
        # expectation contradict the skin block computed alongside it, and would ask CNA to apply a
        # transform the specification says to ignore. The skinned result lives under l4.skin
        # instead (plans/plan_gltf.md GLTF-247).
        skinned = "skin" in nodes[inst.node] if inst.node < len(nodes) else False
        placement = mat_identity() if skinned else inst.world_matrix
        primitive_count = len(meshes[inst.mesh].get("primitives", []))
        for primitive in range(primitive_count):
            local = mesh_local_positions.get((inst.mesh, primitive))
            if local is None and primitive == 0:
                local = mesh_local_positions.get(inst.mesh, [])
            if local is None:
                local = []
            transformed = [transform_point(placement, p) for p in local]
            all_points.extend(transformed)
            record = {
                "node": inst.node,
                "nodeName": inst.node_name,
                "mesh": inst.mesh,
                "parentNodePath": inst.parent_path,
                "worldMatrixColumnMajor": placement,
                "worldPositions": transformed,
            }
            # Preserve the compact schema for 126 existing single-primitive fixtures while making
            # the discriminator explicit wherever it matters.
            if primitive_count > 1:
                record["primitive"] = primitive
            records.append(record)
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
                 tangents: Sequence[Sequence[float]] | None = None,
                 generated_tangents: Sequence[Sequence[float]] | None = None,
                 texcoords: Sequence[Sequence[float]] | None = None,
                 texcoords1: Sequence[Sequence[float]] | None = None,
                 colors: Sequence[Sequence[float]] | None = None,
                 joints: Sequence[Sequence[float]] | None = None,
                 authored_joints: Sequence[Sequence[float]] | None = None,
                 weights: Sequence[Sequence[float]] | None = None,
                 indices: Sequence[int] | None = None,
                 material: dict[str, Any] | None = None,
                 flat_normals: str | None = None,
                 dropped_attributes: Sequence[str] | None = None,
                 dropped_reason: str | None = None) -> dict[str, Any]:
    """One primitive's spec-correct semantic mesh record -- the L3 expectation.

    ``indices`` is the resolved index list a conforming reader must produce (``None`` for a
    non-indexed primitive, whose implicit indices are ``[0, count)``). ``triangles`` is derived
    from ``mode`` per §3.7.2.1 and is empty for the point and line modes.

    ``dropped_attributes`` names streams a conforming reader decodes and CNA's chosen vertex
    layout has no slot for -- a documented limitation (``GLTF-086``/``GLTF-241``), not a defect and
    not a licence to ignore them. They stay stated at full value here, because that is what the
    file means; the conformance comparison skips exactly those fields and asserts they came back
    EMPTY, so "dropped" cannot quietly become "present and wrong".

    ``flat_normals`` opts the primitive into §3.7.2.1's flat-normal computation (``GLTF-461``):
    ``"minimal"`` for a primitive that authors no ``NORMAL``, ``"per-corner"`` when it also carries
    morph targets (§3.7.2.2 requires flat normals *per target*, and a ``POSITION`` delta can rotate a
    face). Every per-vertex stream passed here is then renumbered onto the split, ``normals`` becomes
    the computed per-face values, and ``importPolicy.flatNormalSplit`` records the remap so a reader
    can check it rather than infer it from the output. Pass the AUTHORED streams; the split is
    applied here exactly once.

    ``importPolicy`` is the one part of this record that is **not** spec-derived: it states what
    CNA's own documented policies must turn the primitive into. A strip or fan is converted to a
    triangle list (plans/plan_gltf.md §10.1, ``GLTF-072``), and a non-topological skin remaps authored
    ``JOINTS_0`` indices onto its parent-before-child GPU palette (``GLTF-252``). `joints` states
    the palette indices present in the imported L3 mesh; `authored_joints`, when supplied, records
    the source accessor under `importPolicy.authoredJoints` so the remap can be checked rather than
    inferred from its output.
    """
    from .builder import (LINE_LOOP, LINE_STRIP, MODE_NAMES, TRIANGLES, expand_to_triangles,
                          primitive_count_for_mode, produces_triangles)

    resolved = list(indices) if indices is not None else list(range(len(positions)))
    triangles = expand_to_triangles(resolved, mode)
    # What CNA's documented per-mode policy (plans/plan_gltf.md §10.1) turns the primitive into. Three
    # distinct outcomes, and the manifest states which applies rather than leaving a reader to
    # infer it: a triangle mode converts to a triangle list (GLTF-072); a LINE_LOOP becomes a
    # LINE_STRIP carrying the closing segment glTF leaves implicit in the mode (GLTF-076); every
    # other mode is already exactly what its own draw consumes and passes through untouched.
    if produces_triangles(mode):
        imported_mode = TRIANGLES
        imported_indices = [i for tri in triangles for i in tri]
    elif mode == LINE_LOOP:
        imported_mode = LINE_STRIP
        imported_indices = resolved + resolved[:1] if len(resolved) >= 2 else list(resolved)
    else:
        imported_mode = mode
        imported_indices = list(resolved)
    # GLTF-461: §3.7.2.1's flat normals, applied to the CONVERTED triangle list -- the importer
    # splits after the topology conversion, because a strip's adjacent triangles are exactly the
    # shared-vertex case flat shading has to resolve.
    flat_split = None
    if flat_normals is not None:
        from . import flatnormals
        if flat_normals not in ("minimal", "per-corner"):
            raise ValueError(f"unknown flat_normals policy {flat_normals!r}")
        if normals:
            raise ValueError(
                "a primitive whose flat normals are computed must not also state authored ones -- "
                "§3.7.2.1 applies only when NORMAL is absent")
        flat_split = flatnormals.compute(positions, imported_indices,
                                         per_corner=(flat_normals == "per-corner"))
        imported_indices = flat_split.indices
        positions = flatnormals.gather(positions, flat_split.source_vertex)
        normals = flat_split.normals
        tangents = flatnormals.gather(tangents, flat_split.source_vertex)
        texcoords = flatnormals.gather(texcoords, flat_split.source_vertex)
        if texcoords1 is not None:
            texcoords1 = flatnormals.gather(texcoords1, flat_split.source_vertex)
        colors = flatnormals.gather(colors, flat_split.source_vertex)
        joints = flatnormals.gather(joints, flat_split.source_vertex)
        weights = flatnormals.gather(weights, flat_split.source_vertex)
    import_policy: dict[str, Any] = {
        "imported": True,
        "topologyMode": imported_mode,
        "topologyName": MODE_NAMES[imported_mode],
        "indices": imported_indices,
        "converted": imported_mode != mode or imported_indices != resolved,
        # §12.3's draw-call count for the topology the buffer ends up in (GLTF-078). Stated here as
        # well as in l5 so an L3 comparison can catch a count that no longer follows the topology.
        "primitiveCount": primitive_count_for_mode(imported_mode, len(imported_indices)),
    }
    if flat_split is not None:
        import_policy["flatNormalSplit"] = {
            "policy": flat_normals,
            "sourceVertex": list(flat_split.source_vertex),
            "duplicatedVertices": flat_split.duplicated,
            "mergedVertices": flat_split.merged,
            "rule": "§3.7.2.1 requires flat normals when NORMAL is absent, and flat shading gives "
                    "a vertex one normal PER FACE -- so a vertex shared between differently "
                    "oriented faces must be duplicated once per orientation, which renumbers every "
                    "per-vertex stream. New vertices are handed out in source-vertex order, so a "
                    "primitive that does not split keeps its own numbering.",
            "perCornerRule": "§3.7.2.2 requires flat normals for each morph target. A POSITION "
                             "delta can rotate a face, so two faces coplanar at rest need not stay "
                             "coplanar and a rest-pose split cannot serve every pose; only a fully "
                             "split primitive can carry an exact per-face normal at any weight.",
        }
    if dropped_attributes:
        import_policy["droppedAttributes"] = list(dropped_attributes)
        import_policy["droppedReason"] = dropped_reason or ""
    if generated_tangents:
        # Unlike `tangents` below, this is not something the glTF asset authors. It is CNA's
        # documented fallback policy for a primitive with UVs but no TANGENT, so keep it under
        # importPolicy. The L5 packer may consume an exactly-solvable basis without pretending it
        # came from §3.7.2.1, and a production-path test still has to prove CNA generated it.
        import_policy["generatedTangents"] = [list(t) for t in generated_tangents]
    if authored_joints is not None:
        # JOINTS_0 indexes skin.joints[] in the file and the GPU palette after import. Those are
        # intentionally different spaces when BuildSkeleton topologically reorders a skin.
        import_policy["authoredJoints"] = [list(j) for j in authored_joints]
    record = {
        "mesh": mesh,
        "meshName": mesh_name,
        "primitive": primitive,
        "mode": mode,
        "modeName": MODE_NAMES[mode],
        "indexed": indices is not None,
        "vertexCount": len(positions),
        "positions": [list(p) for p in positions],
        "normals": [list(n) for n in (normals or [])],
        # AUTHORED tangents only. A generated tangent basis is CNA's own algorithm rather than
        # anything §3.7.2.1 prescribes, so stating one here would promote an implementation choice
        # to a conformance expectation -- the opposite of what this manifest is for.
        "tangents": [list(t) for t in (tangents or [])],
        "texcoords": [list(t) for t in (texcoords or [])],
        "colors": [list(c) for c in (colors or [])],
        "joints": [list(j) for j in (joints or [])],
        "weights": [list(w) for w in (weights or [])],
        "indices": resolved,
        "triangles": triangles,
        "importPolicy": import_policy,
        "material": material,
    }
    if texcoords1 is not None:
        record["texcoords1"] = [list(t) for t in texcoords1]
    return record
