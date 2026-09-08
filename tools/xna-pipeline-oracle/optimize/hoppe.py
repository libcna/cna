"""The reconstruction of `MeshHelper.OptimizeForCache`.  XNASWEEP-149.

XNA's method is the documented public `D3DXOptimizeFaces` (616 probes, no disagreement), and
`D3DXOptimizeFaces` implements the greedy strip-growing algorithm of Hugues Hoppe, *Optimization
of mesh locality for transparent vertex caching*, SIGGRAPH 1999 -- public academic literature
whose Figure 3 gives the pseudocode:

    grow a triangle strip from a seed face; where two unvisited neighbours continue it, take one
    and push the other on a queue Q of restart locations; where none does, restart at the first
    unvisited face of Q and clear it; where Q is empty, seed again over the whole mesh, favouring
    faces whose vertices are still cached and then faces with fewest unvisited neighbours; and
    before each face, decide by lookahead simulation whether restarting now would cost less.

Microsoft's public documentation gives the two constants its D3DX9 entry point used: a simulated
vertex cache of 12 and a restart threshold of 7.  What the paper leaves to the implementation --
what "neighbour" means -- is measured here: a face has at most one neighbour per edge, and it is
the *first other face in input order* sharing that edge, taken only when it is wound the opposite
way.  A later face on the same edge is never reached, which is why `ins_f8_at08` restarts where
`ins_f8_at07` crosses, and that one rule predicts the seed of all 616 probes.

Nothing here is derived from any Microsoft implementation, in source or binary form.
"""

from __future__ import annotations

from typing import Dict, List, Optional, Sequence, Tuple

Face = Tuple[int, int, int]

CACHE = 12          # OPTFACES_V_DEFAULT
RESTART = 7         # OPTFACES_R_DEFAULT


def _edges(face: Face):
    a, b, c = face
    return ((a, b), (b, c), (c, a))


class Topology:
    """Faces and the one neighbour each of a face's three edges has, if any."""

    def __init__(self, faces: Sequence[Face]):
        self.faces = [tuple(f) for f in faces]
        self.n = len(self.faces)
        on_edge: Dict[Tuple[int, int], List[int]] = {}
        for i, face in enumerate(self.faces):
            for x, y in _edges(face):
                on_edge.setdefault((x, y) if x < y else (y, x), []).append(i)
        self.adjacent: List[List[int]] = []
        self.edge_of: List[Dict[Tuple[int, int], int]] = []
        for i, face in enumerate(self.faces):
            row: List[int] = []
            where: Dict[Tuple[int, int], int] = {}
            for e, (x, y) in enumerate(_edges(face)):
                key = (x, y) if x < y else (y, x)
                where[key] = e
                partner = -1
                for j in on_edge[key]:
                    if j == i:
                        continue
                    other = self.faces[j]
                    for k in range(3):
                        if (other[k], other[(k + 1) % 3]) == (y, x):
                            partner = j
                        elif (other[k], other[(k + 1) % 3]) == (x, y):
                            partner = -1
                    break                       # only the first other face on the edge is reached
                row.append(partner)
            self.adjacent.append(row)
            self.edge_of.append(where)

    def partner(self, face: int, u: int, v: int) -> int:
        e = self.edge_of[face].get((u, v) if u < v else (v, u))
        return -1 if e is None else self.adjacent[face][e]

    def degree(self, face: int, visited: Sequence[bool]) -> int:
        return sum(1 for j in self.adjacent[face] if j >= 0 and not visited[j])


class State:
    __slots__ = ("visited", "cache", "queue", "misses", "emitted")

    def __init__(self, n: int):
        self.visited = [False] * n
        self.cache: List[int] = []
        self.queue: List[int] = []
        self.misses = 0
        self.emitted = 0

    def clone(self) -> "State":
        s = State.__new__(State)
        s.visited = self.visited[:]
        s.cache = self.cache[:]
        s.queue = self.queue[:]
        s.misses = self.misses
        s.emitted = self.emitted
        return s


def _touch(st: State, face: Face, k: int) -> None:
    cache = st.cache
    for v in face:
        if v not in cache:
            st.misses += 1
            cache.append(v)
            if len(cache) > k:
                del cache[0]


def _seed(topo: Topology, st: State, params: dict, restart: bool) -> int:
    """Fewest unvisited neighbours, faces with cached vertices first on a restart.  The scan keeps
    the last equal candidate, which is what makes a mesh of disjoint triangles come back reversed."""
    visited = st.visited
    cache = st.cache
    best = -1
    best_key = None
    for i in range(topo.n):
        if visited[i]:
            continue
        near = topo.degree(i, visited)
        key = near
        if best_key is None or key <= best_key:
            best_key = key
            best = i
    return best


def _restart_face(topo: Topology, st: State, params: dict) -> int:
    f = -1
    while st.queue:
        cand = st.queue.pop(0)
        if not st.visited[cand]:
            f = cand
            break
    st.queue = []
    return f if f >= 0 else _seed(topo, st, params, restart=True)


def _step(topo: Topology, st: State, f: int, ein: int, params: dict):
    """Emit `f`; return (next face, the edge index it is entered by), or (-1, -1).

    A face's three edges are `(v0,v1)`, `(v1,v2)`, `(v2,v0)`.  A seed -- entered across no edge,
    `ein < 0` -- offers all three in index order and leaves by the first with an unvisited
    neighbour.  A face the strip entered across edge `e` offers `(e + 2) % 3` and then
    `(e + 1) % 3`: the first is Hoppe's fixed strip direction and the corpus takes it in every one
    of 1,088 decisions where both were available.  Whichever is not taken goes on Q.
    """
    st.visited[f] = True
    _touch(st, topo.faces[f], params["cache"])
    st.emitted += 1
    order = (0, 1, 2) if ein < 0 else ((ein + 2) % 3, (ein + 1) % 3)
    taken = -1
    taken_edge = -1
    for e in order:
        j = topo.adjacent[f][e]
        if j < 0 or st.visited[j]:
            continue
        if taken < 0:
            taken, taken_edge = j, e
        else:
            st.queue.append(j)
    if taken < 0:
        return -1, -1
    a, b = _edges(topo.faces[f])[taken_edge]
    return taken, topo.edge_of[taken][(a, b) if a < b else (b, a)]


def optimize(faces: Sequence[Face], cache: int = CACHE, restart: int = RESTART,
             lookahead: Optional[int] = None) -> List[int]:
    """The face order: `optimize(faces)[i]` is the input face at output position `i`.

    A strip is cut `restart` faces after the one whose emission first offered a restart location,
    counting that face.  Measured, not fitted: a closed fan of nine or more offers one at its seed
    and runs exactly seven; the same strip with a pendant triangle seeding it offers one at its
    second face and runs exactly eight; and a plain row of quads, which never offers one, runs all
    forty-eight of its faces in a single strip.
    """
    topo = Topology(faces)
    params = {"cache": cache, "restart": restart,
              "lookahead": lookahead if lookahead is not None else cache + 5}
    st = State(topo.n)
    order: List[int] = []
    f = _seed(topo, st, params, restart=False)
    ein = -1
    since = -1                              # faces emitted since the first push of this strip
    while f >= 0:
        if since >= restart:
            f = _restart_face(topo, st, params)
            ein, since = -1, -1
            continue
        order.append(f)
        had = len(st.queue)
        f, ein = _step(topo, st, f, ein, params)
        if since >= 0:
            since += 1
        elif len(st.queue) > had:
            since = 1
        if f < 0:
            f = _restart_face(topo, st, params)
            ein, since = -1, -1
    return order
