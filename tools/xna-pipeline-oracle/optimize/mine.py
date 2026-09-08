"""Mine every walk decision out of the measured corpus, with the state it was made in.

The walk model is settled: a face is emitted with its input corner order, and from a face entered
across the ordered pair `(older, newer)` with third vertex `r` the walk may cross to the unused
face carrying `(newer, r)`, to the one carrying `(older, r)`, or end the run.  What is not settled
is which, so this labels every decision with what genuine XNA did and with the features any
candidate rule might read.  XNASWEEP-149.
"""

from __future__ import annotations

import sys
from typing import Dict, List, Optional, Tuple

sys.path.insert(0, __file__.rsplit("/", 1)[0])

import corpus


class Walk:
    """Replays one measured answer, exposing the state at every decision."""

    def __init__(self, case: corpus.Case, cache_size: int = 12):
        self.case = case
        self.order = case.order
        self.cache_size = cache_size
        if case.shared:
            vertex_of: Dict[int, int] = {}
            faces = []
            for face in case.faces:
                triple = []
                for corner in face:
                    if corner not in vertex_of:
                        vertex_of[corner] = len(vertex_of)
                    triple.append(vertex_of[corner])
                faces.append(tuple(triple))
        else:
            faces = [(3 * i, 3 * i + 1, 3 * i + 2) for i in range(len(case.faces))]
        self.faces = faces
        self.n = len(faces)
        self.of_vertex: Dict[int, List[int]] = {}
        for i, face in enumerate(faces):
            for v in set(face):
                self.of_vertex.setdefault(v, []).append(i)
        self.edges: Dict[frozenset, List[int]] = {}
        for i, face in enumerate(faces):
            for a, b in ((face[0], face[1]), (face[1], face[2]), (face[2], face[0])):
                self.edges.setdefault(frozenset((a, b)), []).append(i)
        self.used = [False] * len(faces)
        self.live = {v: len(f) for v, f in self.of_vertex.items()}
        self.cache: List[int] = []

    def across(self, a, b):
        for i in self.edges.get(frozenset((a, b)), ()):
            if not self.used[i]:
                return i
        return None

    def take(self, i):
        self.used[i] = True
        for v in set(self.faces[i]):
            self.live[v] -= 1
        for v in self.faces[i]:
            if v in self.cache:
                self.cache.remove(v)
            self.cache.append(v)
            while len(self.cache) > self.cache_size:
                self.cache.pop(0)

    def age(self, v):
        return len(self.cache) - self.cache.index(v) if v in self.cache else 999

    def features(self, i):
        if i is None:
            return None
        face = self.faces[i]
        lives = [self.live[v] for v in face]
        return {"face": i, "minlive": min(lives), "sumlive": sum(lives),
                "hits": sum(1 for v in face if v in self.cache),
                "minage": min(self.age(v) for v in face),
                "maxage": max(self.age(v) for v in face)}

    def global_best(self):
        live = [n for n in self.live.values() if n > 0]
        if not live:
            return None, [], None
        low = min(live)
        verts = [v for v, n in self.live.items() if n == low]
        faces = sorted({i for v in verts for i in self.of_vertex[v] if not self.used[i]})
        return low, faces, (max(faces) if faces else None)


def decisions(case: corpus.Case, cache_size: int = 12):
    w = Walk(case, cache_size)
    order = w.order
    out = []
    step = 0
    older = newer = None
    run = 0
    while step < len(order):
        face = order[step]
        if older is None:
            low, gfaces, gbest = w.global_best()
            out.append({"case": case.name, "step": step, "label": "seed", "chosen": face,
                        "run": 0, "globallow": low, "globalfaces": gfaces,
                        "seed_is_globalmax": face == gbest,
                        "seed_in_global": face in gfaces})
            w.take(face)
            triple = w.faces[face]
            nxt = order[step + 1] if step + 1 < len(order) else None
            if nxt is not None and len(set(w.faces[nxt]) & set(triple)) == 2:
                shared = set(w.faces[nxt]) & set(triple)
                pivot = [v for v in triple if v not in shared][0]
            else:
                pivot = triple[2]
            k = triple.index(pivot)
            older, newer = triple[(k + 1) % 3], triple[(k + 2) % 3]
            out[-1]["pivot_slot"] = k
            run = 1
            step += 1
            continue
        nxt = w.across(older, newer)
        if nxt is None or nxt != face:
            older = newer = None
            continue
        w.take(nxt)
        run += 1
        step += 1
        triple = w.faces[nxt]
        r = [c for c in triple if c != older and c != newer][0]
        pref = w.across(newer, r)
        swap = w.across(older, r)
        low, gfaces, gbest = w.global_best()
        chosen = order[step] if step < len(order) else None
        if chosen is not None and chosen == pref:
            label = "pref"
        elif chosen is not None and chosen == swap:
            label = "swap"
        else:
            label = "restart" if chosen is not None else "end"
        row = {"case": case.name, "step": step, "label": label, "chosen": chosen, "run": run,
               "pref": w.features(pref), "swap": w.features(swap),
               "globallow": low, "globalbest": gbest, "globalfaces": gfaces,
               "live_older": w.live[older], "live_newer": w.live[newer], "live_r": w.live[r],
               "age_older": w.age(older), "age_newer": w.age(newer), "age_r": w.age(r),
               "remaining": sum(1 for u in w.used if not u)}
        out.append(row)
        if label == "pref":
            older, newer = newer, r
        elif label == "swap":
            older, newer = older, r
        else:
            older = newer = None
    return out


def all_decisions(cases=None, cache_size: int = 12):
    cases = cases if cases is not None else corpus.load()
    rows = []
    for name in sorted(cases):
        case = cases[name]
        if case.order is None:
            continue
        rows.extend(decisions(case, cache_size))
    return rows
