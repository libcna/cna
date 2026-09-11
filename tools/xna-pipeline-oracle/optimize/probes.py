"""The OptimizeForCache probe topologies, rebuilt exactly as the graphics oracle builds them.

`tools/xna-pipeline-oracle/graphics/GraphicsContentOracle.cs` hands genuine XNA 4.0's
`MeshHelper.OptimizeForCache` a set of small meshes and records what comes back.  This module
reproduces the *inputs* of those cases in Python, keyed by the oracle's own case name, so that a
candidate reconstruction of the method can be scored against the recorded answers without
re-running Wine.  XNASWEEP-149.

A probe is a pair `(positions, faces)`, `faces` being triples of **position** indices in the order
the mesh hands them over.  Two shapes of mesh appear: a *soup*, one vertex per corner, and a
*shared* mesh, one vertex per position, which is what an importer actually produces.  The
distinction matters to the answer and is carried in `Probe.shared`.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, List, Sequence, Tuple

Face = Tuple[int, int, int]


@dataclass(frozen=True)
class Probe:
    """One probe: the mesh handed to `OptimizeForCache`, named as the oracle names it."""

    case: str
    positions: Tuple[Tuple[float, float, float], ...]
    faces: Tuple[Face, ...]
    shared: bool

    @property
    def face_count(self) -> int:
        return len(self.faces)


def strip_triangles(count: int) -> List[Face]:
    faces: List[Face] = []
    for i in range(count):
        a, b, c, d = 2 * i, 2 * i + 1, 2 * i + 2, 2 * i + 3
        faces.append((a, b, d))
        faces.append((a, d, c))
    return faces


def strip_positions(count: int) -> List[Tuple[float, float, float]]:
    out: List[Tuple[float, float, float]] = []
    for i in range(count + 1):
        out.append((i, 0, 0))
        out.append((i, 1, 0))
    return out


def grid_positions(side: int) -> List[Tuple[float, float, float]]:
    return [(x, y, 0) for y in range(side + 1) for x in range(side + 1)]


def grid_triangles(side: int) -> List[Face]:
    faces: List[Face] = []
    for y in range(side):
        for x in range(side):
            a = y * (side + 1) + x
            b = a + 1
            c = a + side + 1
            e = c + 1
            faces.append((a, b, e))
            faces.append((a, e, c))
    return faces


def sphere_triangles(rings: int, segments: int):
    points: List[Tuple[float, float, float]] = []
    for ring in range(rings):
        for segment in range(segments):
            points.append((segment, ring, 0))
    south = len(points)
    points.append((-1, -1, 0))
    north = len(points)
    points.append((-1, rings, 0))
    faces: List[Face] = []
    for ring in range(rings - 1):
        for segment in range(segments):
            a = ring * segments + segment
            b = ring * segments + (segment + 1) % segments
            c = (ring + 1) * segments + segment
            d = (ring + 1) * segments + (segment + 1) % segments
            faces.append((a, b, d))
            faces.append((a, d, c))
    for segment in range(segments):
        faces.append((segment, (segment + 1) % segments, south))
    for segment in range(segments):
        b = (rings - 1) * segments
        faces.append((b + segment, b + (segment + 1) % segments, north))
    return points, faces


def quads_positions_and_faces(count: int = 6):
    positions: List[Tuple[float, float, float]] = []
    faces: List[Face] = []
    for q in range(count):
        a = len(positions)
        positions.append((2 * q, 0, 0))
        positions.append((2 * q + 1, 0, 0))
        positions.append((2 * q, 1, 0))
        positions.append((2 * q + 1, 1, 0))
        faces.append((a, a + 1, a + 3))
        faces.append((a, a + 3, a + 2))
    return positions, faces


def _register(out: Dict[str, Probe], probe: Probe) -> None:
    out[probe.case] = probe


def build_probes() -> Dict[str, Probe]:
    """Every recorded `OptimizeForCache` case, keyed by the oracle's case name."""
    probes: Dict[str, Probe] = {}

    # --- soup meshes (one vertex per corner) -------------------------------------------------
    for count in (2, 4, 8, 20):
        _register(probes, Probe("meshhelper/optimize_strip_%d" % count,
                                tuple(strip_positions(count)),
                                tuple(strip_triangles(count)), False))
    for side in (1, 2, 4, 6):
        _register(probes, Probe("meshhelper/optimize_grid_%d" % side,
                                tuple(grid_positions(side)),
                                tuple(grid_triangles(side)), False))
    for rings, segments in ((2, 4), (3, 6), (5, 12), (6, 12)):
        points, faces = sphere_triangles(rings, segments)
        _register(probes, Probe("meshhelper/optimize_sphere_%dx%d" % (rings, segments),
                                tuple(points), tuple(faces), False))
    _register(probes, Probe("meshhelper/optimize_for_cache_grid",
                            tuple(grid_positions(3)), tuple(grid_triangles(3)), False))
    shuffled_order = [5, 0, 11, 3, 8, 14, 1, 17, 6, 12, 2, 9, 16, 4, 10, 7, 15, 13]
    plain3 = grid_triangles(3)
    _register(probes, Probe("meshhelper/optimize_for_cache_shuffled",
                            tuple(grid_positions(3)),
                            tuple(plain3[i] for i in shuffled_order), False))
    _register(probes, Probe("meshhelper/optimize_for_cache",
                            ((0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)),
                            ((0, 1, 2), (0, 2, 3)), True))

    # --- shared meshes (one vertex per position) ---------------------------------------------
    for count in (2, 8, 20):
        _register(probes, Probe("meshhelper/optimize_shared_strip_%d" % count,
                                tuple(strip_positions(count)),
                                tuple(strip_triangles(count)), True))
    for side in (2, 3, 4, 5, 6):
        _register(probes, Probe("meshhelper/optimize_shared_grid_%d" % side,
                                tuple(grid_positions(side)),
                                tuple(grid_triangles(side)), True))
    for rings, segments in ((2, 4), (3, 6), (5, 12), (6, 12)):
        points, faces = sphere_triangles(rings, segments)
        _register(probes, Probe("meshhelper/optimize_shared_sphere_%dx%d" % (rings, segments),
                                tuple(points), tuple(faces), True))

    quad_positions, quad_faces = quads_positions_and_faces(6)
    _register(probes, Probe("meshhelper/optimize_quads_6",
                            tuple(quad_positions), tuple(quad_faces), True))
    _register(probes, Probe("meshhelper/optimize_quads_6_permuted",
                            tuple(quad_positions),
                            tuple(quad_faces[(5 * i) % len(quad_faces)]
                                  for i in range(len(quad_faces))), True))

    plain4 = grid_triangles(4)
    _register(probes, Probe("meshhelper/optimize_shared_grid_4_permuted",
                            tuple(grid_positions(4)),
                            tuple(plain4[(7 * i) % len(plain4)] for i in range(len(plain4))), True))
    _register(probes, Probe("meshhelper/optimize_shared_grid_4_reversed",
                            tuple(grid_positions(4)),
                            tuple(reversed(plain4)), True))

    plain_pos4 = grid_positions(4)
    n = len(plain_pos4)
    flipped_positions = [None] * n
    for i, p in enumerate(plain_pos4):
        flipped_positions[n - 1 - i] = p
    _register(probes, Probe("meshhelper/optimize_shared_grid_4_vertexflip",
                            tuple(flipped_positions),
                            tuple((n - 1 - t[0], n - 1 - t[1], n - 1 - t[2]) for t in plain4),
                            True))

    fan_positions = [(0, 0, 0)] + [(i, 1, 0) for i in range(8)]
    fan_faces = [(0, 1 + i, 1 + (i + 1) % 8) for i in range(8)]
    _register(probes, Probe("meshhelper/optimize_fan_8",
                            tuple(fan_positions), tuple(fan_faces), True))

    one = grid_positions(3)
    tri = grid_triangles(3)
    two_positions = list(one) + [(v[0] + 10, v[1], v[2]) for v in one]
    two_faces = [tuple(t) for t in tri] + [tuple(c + len(one) for c in t) for t in tri]
    _register(probes, Probe("meshhelper/optimize_two_grids_3",
                            tuple(two_positions), tuple(two_faces), True))

    points, faces = sphere_triangles(2, 4)
    _register(probes, Probe("meshhelper/optimize_shared_sphere_2x4_permuted",
                            tuple(points),
                            tuple(faces[(7 * i) % len(faces)] for i in range(len(faces))), True))

    return probes


PROBES = build_probes()
