"""Microsoft DirectXMesh's `OptimizeFaces`, ported to Python for the XNASWEEP-149 experiment.

plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-149`.  Genuine XNA 4.0's
`MeshHelper.OptimizeForCache` is `D3DXOptimizeFaces` on all 616 probes, and Microsoft documents
its open-source DirectXMesh `OptimizeFaces` as the same Hoppe algorithm with the same defaults.
DirectXMesh takes its adjacency as an argument where D3DX computed one internally, so the one
thing the corpus still has to decide is which adjacency reproduces the legacy entry point.  This
module is the fast experiment for that: a line-for-line port of

    DirectXMesh/DirectXMeshOptimizeTVC.cpp   (`mesh_status`, `sim_vcache`,
                                              `VertexCacheStripReorderImpl`)

from microsoft/DirectXMesh, MIT licensed, revision bd17eb215d463d98f2b3a13082ce13979219314f
(tag `oct2025`).  Copyright (c) Microsoft Corporation.  See THIRD_PARTY_NOTICES.md.

`run-dxmesh-oracle.sh` runs the *genuine* compiled library over the same probes; `dxcompare.py`
checks that this port answers exactly what the binary answers, so the sweep below is measured on
Microsoft's algorithm and not on a paraphrase of it.
"""

from __future__ import annotations

from typing import Callable, Dict, List, Optional, Sequence, Tuple

UNUSED32 = 0xFFFFFFFF

OPTFACES_V_DEFAULT = 12
OPTFACES_R_DEFAULT = 7

Face = Tuple[int, int, int]


def find_edge(triple: Sequence[int], search: int) -> int:
    for edge in range(3):
        if triple[edge] == search:
            return edge
    return 3


class MeshStatus:
    """`mesh_status<index_t>`: the physical adjacency, and the buckets `find_initial` reads.

    `mutual` is CNA's own experiment and not DirectXMesh: with it false, a link survives when the
    neighbour carries the shared edge wound the other way even if the neighbour's own adjacency
    names a different face there, and the corner a walk arrives at is read off the shared edge
    rather than out of the neighbour's adjacency.  DirectXMesh's own reading -- the default -- is
    the one where a link is kept only when the two faces name each other.
    """

    def __init__(self, indices: Sequence[int], n_faces: int, adjacency: Sequence[int],
                 mutual: bool = True):
        self.n_faces = n_faces
        self.mutual = mutual
        self.indices = list(indices)
        physical = [UNUSED32] * (n_faces * 3)
        face_offset, face_max = 0, n_faces
        for face in range(n_faces):
            i0, i1, i2 = (self.indices[face * 3], self.indices[face * 3 + 1],
                          self.indices[face * 3 + 2])
            if i0 == -1 or i1 == -1 or i2 == -1 or i0 == i1 or i0 == i2 or i1 == i2:
                # unused and degenerate faces should not have neighbors
                for point in range(3):
                    k = adjacency[face * 3 + point]
                    if k != UNUSED32:
                        for e in range(3):
                            if adjacency[k * 3 + e] == face:
                                physical[k * 3 + e] = UNUSED32
                    physical[face * 3 + point] = UNUSED32
            else:
                for n in range(3):
                    neighbor = adjacency[face * 3 + n]
                    if neighbor != UNUSED32:
                        if (neighbor < face_offset or neighbor >= face_max
                                or neighbor == adjacency[face * 3 + ((n + 1) % 3)]
                                or neighbor == adjacency[face * 3 + ((n + 2) % 3)]):
                            neighbor = UNUSED32
                        elif not mutual:
                            edge_back = 3
                            for e in range(3):
                                if (self.indices[neighbor * 3 + e] == self.indices[face * 3 + ((n + 1) % 3)]
                                        and self.indices[neighbor * 3 + ((e + 1) % 3)]
                                        == self.indices[face * 3 + n]):
                                    edge_back = e
                                    break
                            if edge_back >= 3:
                                neighbor = UNUSED32
                        else:
                            edge_back = find_edge(adjacency[neighbor * 3:neighbor * 3 + 3], face)
                            if edge_back < 3:
                                p1 = self.indices[face * 3 + n]
                                p2 = self.indices[face * 3 + ((n + 1) % 3)]
                                pn1 = self.indices[neighbor * 3 + edge_back]
                                pn2 = self.indices[neighbor * 3 + ((edge_back + 1) % 3)]
                                if p1 != pn2 or p2 != pn1:
                                    neighbor = UNUSED32
                            else:
                                neighbor = UNUSED32
                    physical[face * 3 + n] = neighbor
        self.physical = physical

    def set_subset(self) -> None:
        n = self.n_faces
        self.processed = [False] * n
        self.unprocessed = [0] * n
        self.prev = [UNUSED32] * n
        self.next = [UNUSED32] * n
        self.heads = [UNUSED32] * 4
        for face in range(n):
            i0, i1, i2 = (self.indices[face * 3], self.indices[face * 3 + 1],
                          self.indices[face * 3 + 2])
            if i0 == -1 or i1 == -1 or i2 == -1:
                continue
            count = sum(1 for k in range(3) if self.physical[face * 3 + k] != UNUSED32)
            self.processed[face] = False
            self.unprocessed[face] = count
            self._push_front(face)

    def _push_front(self, face: int) -> None:
        u = self.unprocessed[face]
        head = self.heads[u]
        self.next[face] = head
        if head != UNUSED32:
            self.prev[head] = face
        self.heads[u] = face
        self.prev[face] = UNUSED32

    def _remove(self, face: int) -> None:
        if self.prev[face] != UNUSED32:
            p, nx = self.prev[face], self.next[face]
            self.next[p] = nx
            if nx != UNUSED32:
                self.prev[nx] = p
        else:
            u = self.unprocessed[face]
            self.heads[u] = self.next[face]
            if self.heads[u] != UNUSED32:
                self.prev[self.heads[u]] = UNUSED32
        self.prev[face] = UNUSED32
        self.next[face] = UNUSED32

    def _decrement(self, face: int) -> None:
        self._remove(face)
        self.unprocessed[face] -= 1
        self._push_front(face)

    def isprocessed(self, face: int) -> bool:
        return self.processed[face]

    def find_initial(self) -> int:
        for j in range(4):
            if self.heads[j] != UNUSED32:
                return self.heads[j]
        return UNUSED32

    def mark(self, face: int) -> None:
        self.processed[face] = True
        self._remove(face)
        for n in range(3):
            neighbor = self.physical[face * 3 + n]
            if neighbor != UNUSED32 and not self.processed[neighbor]:
                self._decrement(neighbor)

    def shared_edge_of(self, neighbor: int, face: int, edge: int) -> int:
        """The neighbour's own edge index carrying `face`'s edge `edge`, reversed."""
        a = self.indices[face * 3 + edge]
        b = self.indices[face * 3 + ((edge + 1) % 3)]
        for e in range(3):
            if (self.indices[neighbor * 3 + e] == b
                    and self.indices[neighbor * 3 + ((e + 1) % 3)] == a):
                return e
        return 3

    def get_neighbors(self, face: int, n: int) -> int:
        return self.physical[face * 3 + n]

    def neighbors_of(self, face: int) -> Sequence[int]:
        return self.physical[face * 3:face * 3 + 3]


class SimVCache:
    """`sim_vcache`: a fixed-size FIFO of vertex indices; a hit does not refresh the entry."""

    def __init__(self, size: int):
        self.size = size
        self.fifo = [UNUSED32] * size
        self.tail = 0

    def clear(self) -> None:
        self.fifo = [UNUSED32] * self.size
        self.tail = 0

    def access(self, vertex: int) -> bool:
        for entry in self.fifo:
            if entry == vertex:
                return True
        self.fifo[self.tail] = vertex
        self.tail = (self.tail + 1) % self.size
        return False


def _ccw_corner(corner: Tuple[int, int], status: MeshStatus) -> Tuple[int, int]:
    edge = (corner[1] + 2) % 3
    neighbor = status.get_neighbors(corner[0], edge)
    if neighbor == UNUSED32:
        return (neighbor, UNUSED32)
    if status.mutual:
        return (neighbor, find_edge(status.neighbors_of(neighbor), corner[0]))
    return (neighbor, status.shared_edge_of(neighbor, corner[0], edge))


def optimize_faces(indices: Sequence[int], n_faces: int, adjacency: Sequence[int],
                   vertex_cache: int = OPTFACES_V_DEFAULT,
                   restart: int = OPTFACES_R_DEFAULT, mutual: bool = True,
                   physical: Optional[Sequence[int]] = None) -> List[int]:
    """`DirectX::OptimizeFaces`.  Returns `faceRemap` with `faceRemap[newLoc] = oldLoc`.

    `physical` replaces the adjacency `mesh_status::initialize` would derive, so that a candidate
    adjacency can be scored on its own without a rule that produces it.  It is an analysis hook;
    DirectXMesh has no such argument.
    """
    status = MeshStatus(indices, n_faces, adjacency, mutual)
    if physical is not None:
        status.physical = list(physical)
    vcache = SimVCache(vertex_cache)
    remap_inverse = [UNUSED32] * n_faces
    desired = vertex_cache - restart

    status.set_subset()
    vcache.clear()
    locnext = 0
    next_corner = [UNUSED32, UNUSED32]
    cur_corner = [UNUSED32, UNUSED32]
    curface = 0

    while True:
        cur_corner[0] = status.find_initial()
        if cur_corner[0] == UNUSED32:
            break
        n0 = status.get_neighbors(cur_corner[0], 0)
        if n0 != UNUSED32 and not status.isprocessed(n0):
            cur_corner[1] = 1
        else:
            n1 = status.get_neighbors(cur_corner[0], 1)
            if n1 != UNUSED32 and not status.isprocessed(n1):
                cur_corner[1] = 2
            else:
                cur_corner[1] = 0

        striprestart = False
        while True:
            # Decision: either add a ring of faces or restart the strip
            if next_corner[0] != UNUSED32:
                nf = 0
                temp = (cur_corner[0], cur_corner[1])
                while True:
                    nxt = _ccw_corner(temp, status)
                    if nxt[0] == UNUSED32 or status.isprocessed(nxt[0]):
                        break
                    nf += 1
                    temp = nxt
                if locnext + nf > desired:
                    if not status.isprocessed(next_corner[0]):
                        cur_corner = [next_corner[0], next_corner[1]]
                    next_corner[0] = UNUSED32

            while True:
                status.mark(cur_corner[0])
                remap_inverse[cur_corner[0]] = curface
                curface += 1

                for k in range(3):
                    if not vcache.access(indices[cur_corner[0] * 3 + k]):
                        locnext += 1

                int_corner = _ccw_corner((cur_corner[0], cur_corner[1]), status)
                interiornei = int_corner[0] != UNUSED32 and not status.isprocessed(int_corner[0])

                ext_corner = _ccw_corner((cur_corner[0], (cur_corner[1] + 2) % 3), status)
                exteriornei = ext_corner[0] != UNUSED32 and not status.isprocessed(ext_corner[0])

                if interiornei:
                    if exteriornei:
                        if next_corner[0] == UNUSED32:
                            next_corner = [ext_corner[0], ext_corner[1]]
                            locnext = 0
                    cur_corner = [int_corner[0], int_corner[1]]
                elif exteriornei:
                    cur_corner = [ext_corner[0], ext_corner[1]]
                    break
                else:
                    cur_corner = [next_corner[0], next_corner[1]]
                    next_corner[0] = UNUSED32
                    if cur_corner[0] == UNUSED32 or status.isprocessed(cur_corner[0]):
                        striprestart = True
                    break

            if striprestart:
                break

    face_remap = [UNUSED32] * n_faces
    for j in range(n_faces):
        f = remap_inverse[j]
        if f < n_faces:
            face_remap[f] = j
    return face_remap


# ---------------------------------------------------------------------------------------------
# Adjacency constructions.  D3DXOptimizeFaces built its own; these are the candidates the corpus
# scores.  Each returns the flat `adjacency[face * 3 + edge]` array DirectXMesh expects.
# ---------------------------------------------------------------------------------------------

def _edge_key(a: int, b: int) -> Tuple[int, int]:
    return (a, b) if a < b else (b, a)


def adjacency_first_other(faces: Sequence[Face]) -> List[int]:
    """The first *other* face in input order carrying the same undirected edge."""
    on_edge: Dict[Tuple[int, int], List[int]] = {}
    for i, f in enumerate(faces):
        for n in range(3):
            on_edge.setdefault(_edge_key(f[n], f[(n + 1) % 3]), []).append(i)
    adjacency = [UNUSED32] * (len(faces) * 3)
    for i, f in enumerate(faces):
        for n in range(3):
            for j in on_edge[_edge_key(f[n], f[(n + 1) % 3])]:
                if j != i:
                    adjacency[i * 3 + n] = j
                    break
    return adjacency


def adjacency_first_opposite(faces: Sequence[Face]) -> List[int]:
    """The first other face in input order whose own winding on the edge is the reverse."""
    directed: Dict[Tuple[int, int], List[int]] = {}
    for i, f in enumerate(faces):
        for n in range(3):
            directed.setdefault((f[n], f[(n + 1) % 3]), []).append(i)
    adjacency = [UNUSED32] * (len(faces) * 3)
    for i, f in enumerate(faces):
        for n in range(3):
            for j in directed.get((f[(n + 1) % 3], f[n]), ()):
                if j != i:
                    adjacency[i * 3 + n] = j
                    break
    return adjacency


def adjacency_last_opposite(faces: Sequence[Face]) -> List[int]:
    """The last other face in input order whose winding on the edge is the reverse."""
    directed: Dict[Tuple[int, int], List[int]] = {}
    for i, f in enumerate(faces):
        for n in range(3):
            directed.setdefault((f[n], f[(n + 1) % 3]), []).append(i)
    adjacency = [UNUSED32] * (len(faces) * 3)
    for i, f in enumerate(faces):
        for n in range(3):
            for j in reversed(directed.get((f[(n + 1) % 3], f[n]), ())):
                if j != i:
                    adjacency[i * 3 + n] = j
                    break
    return adjacency


def adjacency_pairwise(faces: Sequence[Face]) -> List[int]:
    """Manifold pairing: an undirected edge shared by exactly two faces links them, and an edge
    with three or more faces links none.  This is what a hash of `edge -> (first, second)` gives
    when a third face makes the entry ambiguous."""
    on_edge: Dict[Tuple[int, int], List[int]] = {}
    for i, f in enumerate(faces):
        for n in range(3):
            on_edge.setdefault(_edge_key(f[n], f[(n + 1) % 3]), []).append(i)
    adjacency = [UNUSED32] * (len(faces) * 3)
    for i, f in enumerate(faces):
        for n in range(3):
            bucket = on_edge[_edge_key(f[n], f[(n + 1) % 3])]
            if len(bucket) == 2:
                adjacency[i * 3 + n] = bucket[0] if bucket[1] == i else bucket[1]
    return adjacency


def adjacency_pair_last(faces: Sequence[Face]) -> List[int]:
    """The legacy `D3DXOptimizeFaces` adjacency, measured.

    An undirected edge holds the faces that carry it, in input order.  Walking those in input
    order, a face that is not yet paired takes the **last** still-unpaired face on the edge whose
    own winding on it is the reverse, and the two are paired and removed from consideration.  A
    face left over gets no neighbour on that edge.

    On an edge with two faces this is just "link them if they are wound the other way", which is
    why every probe whose edges carry at most two faces answers the same under every reading.  The
    corpus decides it on the ones that carry three or more: 594 designed non-manifold probes and
    the 167 of the original 616 that hold such an edge.
    """
    on_edge: Dict[Tuple[int, int], List[Tuple[int, int]]] = {}
    for i, f in enumerate(faces):
        for n in range(3):
            on_edge.setdefault(_edge_key(f[n], f[(n + 1) % 3]), []).append((i, n))
    adjacency = [UNUSED32] * (len(faces) * 3)
    for slots in on_edge.values():
        paired = [False] * len(slots)
        for p, (i, n) in enumerate(slots):
            if paired[p]:
                continue
            a, b = faces[i][n], faces[i][(n + 1) % 3]
            for q in range(len(slots) - 1, -1, -1):
                if q == p or paired[q]:
                    continue
                j, m = slots[q]
                if j == i:
                    continue
                if faces[j][m] == b and faces[j][(m + 1) % 3] == a:
                    paired[p] = paired[q] = True
                    adjacency[i * 3 + n] = j
                    adjacency[j * 3 + m] = i
                    break
    return adjacency


def adjacency_mutual_first_other(faces: Sequence[Face]) -> List[int]:
    """`adjacency_first_other`, then dropped wherever the choice is not mutual.  Written out so a
    reading that differs only in mutuality can be scored on its own."""
    adjacency = adjacency_first_other(faces)
    out = list(adjacency)
    for i in range(len(faces)):
        for n in range(3):
            j = adjacency[i * 3 + n]
            if j != UNUSED32 and find_edge(adjacency[j * 3:j * 3 + 3], i) >= 3:
                out[i * 3 + n] = UNUSED32
    return out


ADJACENCY: Dict[str, Callable[[Sequence[Face]], List[int]]] = {
    "pair_last": adjacency_pair_last,
    "first_other": adjacency_first_other,
    "first_opposite": adjacency_first_opposite,
    "last_opposite": adjacency_last_opposite,
    "pairwise": adjacency_pairwise,
    "mutual_first_other": adjacency_mutual_first_other,
}


def order_of(faces: Sequence[Face], adjacency_name: str = "pair_last",
             vertex_cache: int = OPTFACES_V_DEFAULT,
             restart: int = OPTFACES_R_DEFAULT, mutual: bool = True) -> List[int]:
    """`order_of(faces)[i]` is the input face at output position `i`."""
    indices: List[int] = []
    for f in faces:
        indices.extend(f)
    adjacency = ADJACENCY[adjacency_name](faces)
    remap = optimize_faces(indices, len(faces), adjacency, vertex_cache, restart, mutual)
    return [r for r in remap]
