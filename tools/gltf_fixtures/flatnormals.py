# SPDX-License-Identifier: MS-PL
"""§3.7.2.1's flat normals, and the vertex split they force (`GLTF-461`).

Specification, §3.7.2.1 ``meshes-overview``: "When normals are not specified, client
implementations **MUST** calculate flat normals and the provided tangents (if present) **MUST** be
ignored." §3.7.2.2 ``morph-targets`` extends it: "When the base mesh primitive does not specify
normals, client implementations **MUST** calculate flat normals for each morph target; the provided
tangents and their displacements (if present) **MUST** be ignored."

Flat shading gives a vertex one normal **per face**. A vertex shared between faces of different
orientation therefore has no single correct value, and the only conforming answer is to duplicate it
once per orientation -- which renumbers every per-vertex stream. This module states that
transformation independently of the importer, so a golden buffer is a second opinion rather than a
restatement of whatever CNA produced.

Two policies, and the difference is not an optimisation:

* **minimal** -- faces are grouped per vertex, so a mesh whose author already split its edges, and a
  mesh whose shared vertices lie on coplanar faces, keep their vertex count exactly.
* **per corner** -- every triangle corner becomes its own vertex. Required whenever the primitive
  carries morph targets: a ``POSITION`` delta can rotate a face, so two faces coplanar at rest need
  not stay coplanar, and only a fully split primitive can carry an exact per-face normal at every
  reachable pose.

Two conventions here are CNA's ABI rather than the specification's, and both are what make a byte
comparison meaningful at all:

* new vertices are handed out in **source-vertex order** (all of vertex 0's copies, then all of
  vertex 1's, ...), so a primitive that does not split keeps its own numbering and its bytes are
  unchanged; and
* faces whose unit normals agree to within :data:`PARALLEL_FACE_DOT` share a group, and that
  group's normal is their area-weighted sum. The tolerance exists so that a pair of mathematically
  coplanar triangles whose cross products differ in the last bits cannot change the vertex *count*
  -- topology decided by float noise would make this corpus non-reproducible.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Any, Sequence

#: ~0.081 degrees. A reproducibility floor, not a smoothing threshold -- see the module docstring.
PARALLEL_FACE_DOT = 1.0 - 1e-6
#: Below this the cross product states no orientation at all, and the face is degenerate.
DEGENERATE_FACE_LENGTH = 1e-12
#: The normal a vertex no face reaches receives. glTF states none, so this is CNA's own placeholder.
PLACEHOLDER_NORMAL = (0.0, 0.0, 1.0)


@dataclass
class FlatNormalSplit:
    """The result of applying §3.7.2.1 to one primitive."""

    #: New vertex index -> the source vertex it is a copy of. Identity when nothing split.
    source_vertex: list[int] = field(default_factory=list)
    #: The triangle list rewritten onto the new numbering. Same length as the input.
    indices: list[int] = field(default_factory=list)
    #: One flat normal per new vertex.
    normals: list[tuple[float, float, float]] = field(default_factory=list)
    #: New vertices beyond the source count.
    duplicated: int = 0
    #: New vertices whose normal averages faces parallel only within the tolerance.
    merged: int = 0

    @property
    def vertex_count(self) -> int:
        return len(self.source_vertex)

    @property
    def split(self) -> bool:
        return self.duplicated > 0


def _cross(a: Sequence[float], b: Sequence[float], c: Sequence[float]) -> tuple[float, float, float]:
    e1 = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
    e2 = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
    return (e1[1] * e2[2] - e1[2] * e2[1],
            e1[2] * e2[0] - e1[0] * e2[2],
            e1[0] * e2[1] - e1[1] * e2[0])


def _length(v: Sequence[float]) -> float:
    return math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])


def compute(positions: Sequence[Sequence[float]], indices: Sequence[int],
            *, per_corner: bool = False) -> FlatNormalSplit:
    """Applies §3.7.2.1's flat normals to a triangle list.

    :param positions: one XYZ per source vertex.
    :param indices: a triangle list in source numbering; out-of-range triples are skipped, exactly
        as the importer skips them after its own bounds check has already refused the file.
    :param per_corner: use the per-corner policy instead of the minimal one.
    """
    vertex_count = len(positions)
    # groups[v] is a list of [unit, accumulated, faces, oriented, merged].
    groups: list[list[list[Any]]] = [[] for _ in range(vertex_count)]
    corner_group = [0] * len(indices)

    for f in range(0, len(indices) - 2, 3):
        corners = (indices[f], indices[f + 1], indices[f + 2])
        if any(c >= vertex_count for c in corners):
            continue
        weighted = _cross(positions[corners[0]], positions[corners[1]], positions[corners[2]])
        length = _length(weighted)
        oriented = length > DEGENERATE_FACE_LENGTH
        unit = ((weighted[0] / length, weighted[1] / length, weighted[2] / length)
                if oriented else (0.0, 0.0, 0.0))

        for k, vertex in enumerate(corners):
            vertex_groups = groups[vertex]
            chosen = len(vertex_groups)
            if not oriented:
                # A degenerate face has no orientation to disagree with, so it never forces a copy.
                chosen = len(vertex_groups) if not vertex_groups else 0
            elif not per_corner:
                for g, group in enumerate(vertex_groups):
                    if not group[3]:
                        chosen = g
                        break
                    dot = sum(group[0][i] * unit[i] for i in range(3))
                    if dot >= PARALLEL_FACE_DOT:
                        chosen = g
                        break
            if chosen == len(vertex_groups):
                vertex_groups.append([(0.0, 0.0, 0.0), (0.0, 0.0, 0.0), 0, False, False])
            group = vertex_groups[chosen]
            if oriented and group[3] and group[0] != unit:
                group[4] = True
            if oriented and not group[3]:
                group[0] = unit
                group[3] = True
            group[1] = (group[1][0] + weighted[0], group[1][1] + weighted[1],
                        group[1][2] + weighted[2])
            group[2] += 1
            corner_group[f + k] = chosen

    result = FlatNormalSplit()
    group_vertex: list[list[int]] = [[] for _ in range(vertex_count)]
    for v in range(vertex_count):
        # A vertex no face touches still occupies a slot: the file declared it, and dropping it
        # would renumber every index for no gain.
        for _g in range(max(len(groups[v]), 1)):
            group_vertex[v].append(len(result.source_vertex))
            result.source_vertex.append(v)
    result.duplicated = len(result.source_vertex) - vertex_count

    result.normals = [PLACEHOLDER_NORMAL] * len(result.source_vertex)
    for v in range(vertex_count):
        for g, new_index in enumerate(group_vertex[v]):
            if g >= len(groups[v]):
                continue
            accumulated = groups[v][g][1]
            length = _length(accumulated)
            if length > DEGENERATE_FACE_LENGTH:
                result.normals[new_index] = (accumulated[0] / length, accumulated[1] / length,
                                             accumulated[2] / length)
            if groups[v][g][4]:
                result.merged += 1

    result.indices = list(indices)
    for f in range(0, len(indices) - 2, 3):
        for k in range(3):
            source = indices[f + k]
            if source >= vertex_count:
                continue
            result.indices[f + k] = group_vertex[source][corner_group[f + k]]
    return result


def gather(stream: Sequence[Sequence[float]] | None,
           source_vertex: Sequence[int]) -> list[list[float]]:
    """Renumbers a per-vertex stream onto a split's new numbering; ``None``/empty passes through."""
    if not stream:
        return []
    return [list(stream[v]) for v in source_vertex]
