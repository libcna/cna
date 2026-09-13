"""A candidate reconstruction of XNA 4.0's `MeshHelper.OptimizeForCache` face ordering.

The walk itself is settled by measurement (XNASWEEP-149): the answer is a generalized triangle
strip over the input faces, every emitted triangle carrying its input corner order unrotated.  A
strip holds an ordered pair of vertices `(older, newer)`; the next face is the unused one carrying
both; on emitting it with third vertex `r` the pair becomes `(newer, r)`, and where that is a dead
end `(older, r)` is tried instead.  A strip's seed opens with the pair `(a, b)` of its own corners.

What is *not* settled is the seed, and that is what this module makes pluggable: `SEED_RULES` holds
one callable per candidate restart rule, and `score` reports, per probe, whether the rule
reproduces the recorded genuine answer and where it first diverges.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional, Sequence, Set, Tuple

Face = Tuple[int, int, int]


@dataclass
class Mesh:
    """The optimizer's view of a probe.

    The optimizer works over the geometry's *vertices*, not its positions: a soup mesh has one
    vertex per corner even where two corners name one position, and two of its faces that appear to
    share a position share no vertex at all.  `faces` is therefore in vertex space, and
    `positions` keeps the position triple of each face so that an answer can be matched back.
    """

    faces: Tuple[Face, ...]
    positions: Tuple[Face, ...]

    faces_of_vertex: Dict[int, List[int]] = field(init=False)

    def __post_init__(self) -> None:
        by_vertex: Dict[int, List[int]] = {}
        for i, face in enumerate(self.faces):
            for corner in set(face):
                by_vertex.setdefault(corner, []).append(i)
        self.faces_of_vertex = by_vertex


def mesh_of(probe) -> Mesh:
    """Build the vertex-space mesh a probe hands the optimizer."""
    if probe.shared:
        vertex_of: Dict[int, int] = {}
        faces = []
        for face in probe.faces:
            triple = []
            for corner in face:
                if corner not in vertex_of:
                    vertex_of[corner] = len(vertex_of)
                triple.append(vertex_of[corner])
            faces.append(tuple(triple))
    else:
        faces = [(3 * i, 3 * i + 1, 3 * i + 2) for i in range(len(probe.faces))]
    return Mesh(tuple(faces), tuple(probe.faces))


@dataclass
class State:
    """Everything a seed rule may look at."""

    mesh: Mesh
    used: List[bool]
    remaining_valence: Dict[int, int]
    cache: List[int]
    emitted: List[int]

    def candidates(self) -> List[int]:
        return [i for i, u in enumerate(self.used) if not u]


SeedRule = Callable[[State], int]


def _min_valence_vertices(state: State) -> List[int]:
    live = {p: n for p, n in state.remaining_valence.items() if n > 0}
    if not live:
        return []
    low = min(live.values())
    return [p for p, n in live.items() if n == low]


def seed_highest_face(state: State) -> int:
    """The highest-index unused face, with no reference to valence at all."""
    return max(state.candidates())


def seed_lowest_face(state: State) -> int:
    return min(state.candidates())


def seed_minvalence_highest_face(state: State) -> int:
    """Highest-index unused face containing a vertex of minimum remaining valence."""
    low = set(_min_valence_vertices(state))
    best = -1
    for i, used in enumerate(state.used):
        if used:
            continue
        if low.intersection(state.mesh.faces[i]):
            best = max(best, i)
    return best if best >= 0 else max(state.candidates())


def seed_minvalence_lowest_face(state: State) -> int:
    low = set(_min_valence_vertices(state))
    best = None
    for i, used in enumerate(state.used):
        if used:
            continue
        if low.intersection(state.mesh.faces[i]):
            best = i if best is None else min(best, i)
    return best if best is not None else min(state.candidates())


def seed_minvalence_highest_vertex(state: State) -> int:
    """The highest-numbered vertex of minimum remaining valence, then its highest face."""
    low = _min_valence_vertices(state)
    if not low:
        return max(state.candidates())
    vertex = max(low)
    faces = [i for i in state.mesh.faces_of_vertex[vertex] if not state.used[i]]
    return max(faces)


def seed_minvalence_lowest_vertex(state: State) -> int:
    low = _min_valence_vertices(state)
    if not low:
        return min(state.candidates())
    vertex = min(low)
    faces = [i for i in state.mesh.faces_of_vertex[vertex] if not state.used[i]]
    return max(faces)


SEED_RULES: Dict[str, SeedRule] = {
    "highest_face": seed_highest_face,
    "lowest_face": seed_lowest_face,
    "minval_highest_face": seed_minvalence_highest_face,
    "minval_lowest_face": seed_minvalence_lowest_face,
    "minval_highest_vertex": seed_minvalence_highest_vertex,
    "minval_lowest_vertex": seed_minvalence_lowest_vertex,
}


@dataclass
class Trace:
    order: List[int]
    seeds: List[int]
    strips: List[List[int]]
    restart_states: List[dict]


def walk(mesh: Mesh, seed_rule: SeedRule, cache_size: int = 32,
         strip_cap: Optional[int] = None, allow_swap: bool = True) -> Trace:
    """Run the strip walk over `mesh`, choosing every seed with `seed_rule`."""
    n = len(mesh.faces)
    used = [False] * n
    remaining = {v: len(f) for v, f in mesh.faces_of_vertex.items()}
    cache: List[int] = []
    order: List[int] = []
    seeds: List[int] = []
    strips: List[List[int]] = []
    restart_states: List[dict] = []

    edge_faces: Dict[frozenset, List[int]] = {}
    for i, face in enumerate(mesh.faces):
        for a, b in ((face[0], face[1]), (face[1], face[2]), (face[2], face[0])):
            edge_faces.setdefault(frozenset((a, b)), []).append(i)

    def touch(face_index: int) -> None:
        used[face_index] = True
        order.append(face_index)
        for corner in set(mesh.faces[face_index]):
            remaining[corner] -= 1
        for corner in mesh.faces[face_index]:
            if corner in cache:
                cache.remove(corner)
            cache.append(corner)
            while len(cache) > cache_size:
                cache.pop(0)

    def unused_across(a: int, b: int) -> Optional[int]:
        for i in edge_faces.get(frozenset((a, b)), ()):  # at most two in a manifold
            if not used[i]:
                return i
        return None

    while sum(used) < n:
        state = State(mesh, used, dict(remaining), list(cache), list(order))
        restart_states.append({
            "before": len(order),
            "candidates": state.candidates(),
            "remaining_valence": {p: v for p, v in remaining.items() if v > 0},
            "cache": list(cache),
        })
        seed = seed_rule(state)
        seeds.append(seed)
        strip = [seed]
        face = mesh.faces[seed]
        touch(seed)
        older, newer = face[0], face[1]
        while True:
            if strip_cap is not None and len(strip) >= strip_cap:
                break
            nxt = unused_across(older, newer)
            if nxt is None:
                break
            triple = mesh.faces[nxt]
            r = [c for c in triple if c != older and c != newer]
            if not r:
                break
            r = r[0]
            touch(nxt)
            strip.append(nxt)
            following = unused_across(newer, r)
            if following is not None:
                older, newer = newer, r
            elif allow_swap and unused_across(older, r) is not None:
                older, newer = older, r
            else:
                older, newer = newer, r  # dead either way; the next probe ends the strip
        strips.append(strip)
    return Trace(order, seeds, strips, restart_states)
