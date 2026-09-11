"""Every step of every recorded answer, as a decision with its state.

At each step the walk stands on a face entered across an ordered pair `(older, newer)` whose third
vertex is `r`, and has at most three things it can do: cross to the face sharing `(newer, r)`,
cross to the face sharing `(older, r)`, or end the strip and seed a new one.  This dumps one row
per step -- what was available, what genuine XNA did, and the live counts and cache state both
options stood in -- so that the rule choosing between them can be looked for in data rather than
guessed.  XNASWEEP-149.
"""

from __future__ import annotations

import sys
from typing import Dict, List, Optional

sys.path.insert(0, __file__.rsplit("/", 1)[0])

import oracle
import probes
import simulate


class Walker:
    def __init__(self, case: str, cache_size: int = 8):
        self.case = case
        self.probe = probes.PROBES[case]
        self.mesh = simulate.mesh_of(self.probe)
        self.truth = oracle.face_order(oracle.load()[case], self.probe.faces)
        self.cache_size = cache_size
        self.edges: Dict[frozenset, List[int]] = {}
        for i, face in enumerate(self.mesh.faces):
            for a, b in ((face[0], face[1]), (face[1], face[2]), (face[2], face[0])):
                self.edges.setdefault(frozenset((a, b)), []).append(i)
        self.used = [False] * len(self.mesh.faces)
        self.live = {v: len(f) for v, f in self.mesh.faces_of_vertex.items()}
        self.cache: List[int] = []

    def across(self, a, b):
        for i in self.edges.get(frozenset((a, b)), ()):
            if not self.used[i]:
                return i
        return None

    def take(self, i):
        self.used[i] = True
        for v in set(self.mesh.faces[i]):
            self.live[v] -= 1
        for v in self.mesh.faces[i]:
            if v in self.cache:
                self.cache.remove(v)
            self.cache.append(v)
            while len(self.cache) > self.cache_size:
                self.cache.pop(0)

    def face_features(self, i):
        if i is None:
            return None
        face = self.mesh.faces[i]
        lives = [self.live[v] for v in face]
        hits = sum(1 for v in face if v in self.cache)
        ages = [len(self.cache) - self.cache.index(v) if v in self.cache else 99 for v in face]
        return {"face": i, "minlive": min(lives), "sumlive": sum(lives), "hits": hits,
                "age": min(ages), "lives": lives}

    def seed_candidates(self):
        low = min((n for n in self.live.values() if n > 0), default=None)
        if low is None:
            return low, []
        verts = sorted(v for v, n in self.live.items() if n == low)
        faces = sorted({i for v in verts for i in self.mesh.faces_of_vertex[v] if not self.used[i]})
        return low, faces


def rows(case: str, cache_size: int = 8):
    w = Walker(case, cache_size)
    truth = w.truth
    out = []
    step = 0
    strip_length = 0
    older = newer = None
    while step < len(truth):
        face = truth[step]
        if older is None:
            low, seeds = w.seed_candidates()
            out.append({"case": case, "step": step, "kind": "seed", "truth": face,
                        "minlive": low, "seedfaces": seeds, "striplen": 0})
            w.take(face)
            triple = w.mesh.faces[face]
            if step + 1 < len(truth) and len(set(w.mesh.faces[truth[step + 1]]) & set(triple)) == 2:
                shared = set(w.mesh.faces[truth[step + 1]]) & set(triple)
                pivot = [v for v in triple if v not in shared][0]
            else:
                pivot = triple[2]
            k = triple.index(pivot)
            older, newer = triple[(k + 1) % 3], triple[(k + 2) % 3]
            out[-1]["pivot"] = pivot
            strip_length = 1
            step += 1
            continue
        nxt = w.across(older, newer)
        if nxt is None or nxt != face:
            older = newer = None
            continue
        # emit and then record the choice made *after* it
        w.take(nxt)
        strip_length += 1
        step += 1
        triple = w.mesh.faces[nxt]
        r = [c for c in triple if c != older and c != newer][0]
        pref = w.across(newer, r)
        swap = w.across(older, r)
        low, seeds = w.seed_candidates()
        chosen = truth[step] if step < len(truth) else None
        kind = ("pref" if chosen == pref and pref is not None else
                "swap" if chosen == swap and swap is not None else "restart")
        out.append({"case": case, "step": step, "kind": kind, "truth": chosen,
                    "striplen": strip_length,
                    "pref": w.face_features(pref), "swap": w.face_features(swap),
                    "pivot_pref_live": w.live[newer], "pivot_swap_live": w.live[older],
                    "r_live": w.live[r], "minlive": low, "seedfaces": seeds,
                    "remaining": sum(1 for u in w.used if not u)})
        if kind == "pref":
            older, newer = newer, r
        elif kind == "swap":
            older, newer = older, r
        else:
            older = newer = None
    return out


def main() -> int:
    cases = sys.argv[1:] or sorted(probes.PROBES)
    answers = oracle.load()
    for case in cases:
        if not case.startswith("meshhelper/"):
            case = "meshhelper/" + case
        if case not in probes.PROBES or case not in answers:
            continue
        print("=== %s" % case)
        for row in rows(case):
            if row["kind"] == "seed":
                print("  %3d seed   -> %-3s  minlive=%s pivot=%s seedfaces=%s"
                      % (row["step"], row["truth"], row["minlive"], row.get("pivot"),
                         row["seedfaces"][:12]))
            else:
                def fmt(f):
                    if f is None:
                        return "-"
                    return "f%d(ml=%d,sl=%d,h=%d,a=%d)" % (f["face"], f["minlive"], f["sumlive"],
                                                           f["hits"], f["age"])
                print("  %3d %-7s-> %-4s len=%2d pref=%-24s swap=%-24s pivL=%d pivS=%d "
                      "minlive=%s seeds=%s"
                      % (row["step"], row["kind"], row["truth"], row["striplen"],
                         fmt(row["pref"]), fmt(row["swap"]), row["pivot_pref_live"],
                         row["pivot_swap_live"], row["minlive"], row["seedfaces"][:8]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
