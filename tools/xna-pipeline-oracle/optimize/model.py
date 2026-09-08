"""The reconstruction of `MeshHelper.OptimizeForCache` under test, and its score on the corpus.

The walk is settled by measurement: a face is emitted with its input corner order untouched; a run
holds an ordered vertex pair `(older, newer)` and crosses to the unused face carrying `(newer, r)`
or, failing that, `(older, r)`; a run's seed opens on the edge opposite a chosen pivot vertex.  The
two rules still under test are which face seeds a run and when a run ends, and they are the
parameters here.  XNASWEEP-149.
"""

from __future__ import annotations

import sys
from typing import Callable, Dict, List, Optional, Sequence, Tuple

sys.path.insert(0, __file__.rsplit("/", 1)[0])

import corpus
import families


class Mesh:
    def __init__(self, faces):
        self.faces = faces
        self.n = len(faces)
        self.of_vertex = families.vertex_faces(faces)
        self.edges: Dict[frozenset, List[int]] = {}
        for i, face in enumerate(faces):
            for a, b in ((face[0], face[1]), (face[1], face[2]), (face[2], face[0])):
                self.edges.setdefault(frozenset((a, b)), []).append(i)


def walk(faces, seed_rule="minlive_highest_face", stop_rule="minlive_tie",
         cache_size=32, run_cap=None, pivot_rule="first"):
    m = Mesh(faces)
    used = [False] * m.n
    live = {v: len(f) for v, f in m.of_vertex.items()}
    cache: List[int] = []
    order: List[int] = []

    def across(a, b):
        """The face the walk crosses to over edge (a, b), by the rule batch eight measured.

        An edge's faces are looked up in input order and the **first** one is the candidate: where
        it is already used, or is wound the same way as the face the walk stands on -- which no
        strip walk can enter -- the run ends, rather than the second candidate being tried
        (`ins_f8_at07` against `ins_f8_at08`). Reading it that way scores 179 of 372 where skipping
        the current face scores 160, and where taking the first *unused* face scores 135.
        """
        for i in m.edges.get(frozenset((a, b)), ()):
            if used[i]:
                return None
            face = faces[i]
            for x, y in ((face[0], face[1]), (face[1], face[2]), (face[2], face[0])):
                if (x, y) == (b, a):
                    return i
            return None
        return None

    def age(v):
        try:
            return len(cache) - cache.index(v)
        except ValueError:
            return 999

    def take(i):
        used[i] = True
        order.append(i)
        for v in set(faces[i]):
            live[v] -= 1
        for v in faces[i]:
            if v in cache:
                cache.remove(v)
            cache.append(v)
            while len(cache) > cache_size:
                cache.pop(0)

    def global_low():
        values = [n for n in live.values() if n > 0]
        return min(values) if values else None

    def seed():
        low = global_low()
        verts = [v for v, n in live.items() if n == low]
        faces_of = sorted({i for v in verts for i in m.of_vertex[v] if not used[i]})
        if not faces_of:
            faces_of = [i for i in range(m.n) if not used[i]]
        if seed_rule == "minlive_highest_face":
            chosen = max(faces_of)
        elif seed_rule == "minlive_lowest_face":
            chosen = min(faces_of)
        elif seed_rule == "minlive_cached_first":
            def key(v):
                a = age(v)
                return (0 if a != 999 else 1, -a if a != 999 else 0, -v)
            v = sorted(verts, key=key)[0]
            chosen = max(i for i in m.of_vertex[v] if not used[i])
        else:
            raise SystemExit("unknown seed rule " + seed_rule)
        # the pivot is the minimal-live vertex the seed was chosen for
        triple = faces[chosen]
        pivots = [v for v in triple if live[v] == low]
        if not pivots:
            pivot = triple[2]
        elif pivot_rule == "last":
            pivot = pivots[-1]
        elif pivot_rule == "lowest":
            pivot = min(pivots)
        elif pivot_rule == "highest":
            pivot = max(pivots)
        else:
            pivot = pivots[0]
        k = triple.index(pivot)
        return chosen, (triple[(k + 1) % 3], triple[(k + 2) % 3])

    while len(order) < m.n:
        chosen, (older, newer) = seed()
        take(chosen)
        run = 1
        while True:
            if run_cap is not None and run >= run_cap:
                break
            nxt = across(older, newer)
            if nxt is None:
                break
            take(nxt)
            run += 1
            triple = faces[nxt]
            r = [c for c in triple if c != older and c != newer][0]
            pref = across(newer, r)
            swap = across(older, r)
            candidate = pref if pref is not None else swap
            if candidate is None:
                break
            if stop_rule == "fresh_vertex":
                fresh = [v for v in faces[candidate] if v != older and v != newer and v != r]
                fresh = fresh[0] if fresh else None
                if fresh is not None and fresh < max(older, newer, r):
                    break
            elif stop_rule == "fresh_vertex_pair":
                fresh = [v for v in faces[candidate] if v not in (newer, r)] if pref is not None \
                    else [v for v in faces[candidate] if v not in (older, r)]
                fresh = fresh[0] if fresh else None
                if fresh is not None and fresh < max(newer, r):
                    break
            elif stop_rule == "minlive_tie":
                low = global_low()
                if low is not None and min(live[v] for v in faces[candidate]) > low:
                    break
            elif stop_rule == "never":
                pass
            if pref is not None:
                older, newer = newer, r
            else:
                older, newer = older, r
    return order


def score(cases, **kwargs):
    exact = 0
    total = 0
    misses = []
    for name in sorted(cases):
        case = cases[name]
        if case.order is None:
            continue
        faces = families.to_vertex_space(case)
        got = walk(faces, **kwargs)
        total += 1
        if got == case.order:
            exact += 1
        else:
            div = next((i for i, (a, b) in enumerate(zip(got, case.order)) if a != b),
                       min(len(got), len(case.order)))
            misses.append((name, div, len(faces)))
    return exact, total, misses


def main() -> int:
    everything = {}
    everything.update(corpus.load())
    for n in ("2", "3", "4", "5", "6", "7"):
        everything.update(corpus.load("build/xna-sample-sweep/optimize/probes%s.json" % n,
                                      "build/xna-sample-sweep/optimize/answers%s.txt" % n))
    print("%d measured probes" % len(everything))
    for seed_rule in ("minlive_highest_face", "minlive_cached_first"):
        for stop_rule in ("minlive_tie", "never"):
            for pivot_rule in ("first", "last", "lowest", "highest"):
                exact, total, misses = score(everything, seed_rule=seed_rule,
                                             stop_rule=stop_rule, pivot_rule=pivot_rule)
                print("%-22s %-12s pivot=%-8s %3d / %d"
                      % (seed_rule, stop_rule, pivot_rule, exact, total))
    if len(sys.argv) > 1:
        exact, total, misses = score(everything, seed_rule=sys.argv[1], stop_rule=sys.argv[2],
                                     pivot_rule=sys.argv[3])
        for name, div, n in misses:
            print("   %-28s diverges at %d/%d" % (name, div, n))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
