"""Replay the recorded genuine answer through the strip model and record every decision.

The walk model has one free choice per step -- continue the strip (preferring the pair
`(newer, r)`, falling back to `(older, r)`) or end it and restart -- and one free choice per
restart, the seed.  This replays the *truth* rather than a candidate: at each step it reports what
the model could have done and what genuine XNA actually did, so that the decisions where the two
part company can be counted and inspected rather than guessed at.  XNASWEEP-149.
"""

from __future__ import annotations

import sys
from typing import Dict, List, Optional, Tuple

sys.path.insert(0, __file__.rsplit("/", 1)[0])

import oracle
import probes
import simulate


def edge_map(faces):
    out: Dict[frozenset, List[int]] = {}
    for i, face in enumerate(faces):
        for a, b in ((face[0], face[1]), (face[1], face[2]), (face[2], face[0])):
            out.setdefault(frozenset((a, b)), []).append(i)
    return out


def replay(case: str, verbose: bool = True):
    probe = probes.PROBES[case]
    mesh = simulate.mesh_of(probe)
    answers = oracle.load()
    truth = oracle.face_order(answers[case], probe.faces)
    edges = edge_map(mesh.faces)
    used = [False] * len(mesh.faces)
    live = {v: len(f) for v, f in mesh.faces_of_vertex.items()}

    def unused_across(a, b):
        for i in edges.get(frozenset((a, b)), ()):
            if not used[i]:
                return i
        return None

    def take(i):
        used[i] = True
        for v in set(mesh.faces[i]):
            live[v] -= 1

    events = []
    step = 0
    strips: List[List[int]] = []
    while step < len(truth):
        seed = truth[step]
        before_live = dict(live)
        low = min(v for v in live.values() if v > 0)
        seed_candidates = sorted(v for v, n in live.items() if n == low)
        events.append(("restart", step, seed, low, seed_candidates))
        strip = [seed]
        take(seed)
        step += 1
        # The seed's opening pair is the edge opposite its "pivot" vertex; read the pivot off the
        # truth's own next face where there is one.
        face = mesh.faces[seed]
        if step < len(truth) and len(set(mesh.faces[truth[step]]) & set(face)) == 2:
            shared = set(mesh.faces[truth[step]]) & set(face)
            pivot = [v for v in face if v not in shared][0]
            k = face.index(pivot)
            older, newer = face[(k + 1) % 3], face[(k + 2) % 3]
        else:
            pivot = face[2]
            older, newer = face[0], face[1]
        events.append(("seedpivot", step - 1, seed, pivot, list(face)))
        while step < len(truth):
            nxt = unused_across(older, newer)
            if nxt is None:
                events.append(("dead", step, None, None, None))
                break
            if nxt != truth[step]:
                events.append(("declined", step, nxt, truth[step], (older, newer)))
                break
            take(nxt)
            strip.append(nxt)
            step += 1
            triple = mesh.faces[nxt]
            r = [c for c in triple if c != older and c != newer][0]
            pref = unused_across(newer, r)
            swap = unused_across(older, r)
            if step < len(truth) and pref is not None and pref == truth[step]:
                older, newer = newer, r
            elif step < len(truth) and swap is not None and swap == truth[step]:
                events.append(("swap", step, swap, (older, r), (newer, r, pref)))
                older, newer = older, r
            else:
                events.append(("stop", step, pref, swap, (older, newer, r)))
                break
        strips.append(strip)
    return mesh, truth, strips, events


def main() -> int:
    cases = sys.argv[1:] or sorted(probes.PROBES)
    for case in cases:
        if not case.startswith("meshhelper/"):
            case = "meshhelper/" + case
        if case not in probes.PROBES:
            continue
        answers = oracle.load()
        if case not in answers:
            continue
        mesh, truth, strips, events = replay(case)
        print("=== %-46s faces=%d strips=%s" % (case, len(truth), [len(s) for s in strips]))
        for kind, step, a, b, c in events:
            if kind == "stop":
                print("    stop  at %3d: preferred=%s swap=%s pair=%s  truth next=%s"
                      % (step, a, b, c, truth[step] if step < len(truth) else None))
            elif kind == "declined":
                print("    DECL  at %3d: model would take %s, truth took %s across %s"
                      % (step, a, b, c))
            elif kind == "restart":
                print("    seed  at %3d: %s  (min live %d over vertices %s)" % (step, a, b, c))
            elif kind == "swap":
                print("    swap  at %3d: to %s across %s (preferred %s was %s)"
                      % (step, a, b, c[:2], c[2]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
