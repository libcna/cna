"""Candidate algorithm families, each implemented from its public description, scored on the corpus.

Phase 3 of XNASWEEP-149: XNA's observable order is compared against the published families of
vertex-cache optimisation -- a global greedy over a cache-and-valence score, a fanning-vertex walk,
and the strip walk this campaign measured -- each written here from the algorithm's description
rather than from anyone's implementation.  A family that cannot reproduce the corpus is discarded
with its score recorded; nothing is kept because it optimises well.
"""

from __future__ import annotations

import math
import sys
from typing import Callable, Dict, List, Optional, Tuple

sys.path.insert(0, __file__.rsplit("/", 1)[0])

import corpus


def vertex_faces(faces):
    out: Dict[int, List[int]] = {}
    for i, face in enumerate(faces):
        for v in set(face):
            out.setdefault(v, []).append(i)
    return out


def to_vertex_space(case: corpus.Case):
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
        return faces
    return [(3 * i, 3 * i + 1, 3 * i + 2) for i in range(len(case.faces))]


# --------------------------------------------------------------------------------------------
# a global greedy over a cache-position-and-valence score (the Forsyth family)


def greedy_score(faces, cache_size=32, last_bonus=0.75, valence_boost=2.0, power=1.5,
                 tie="highest"):
    n = len(faces)
    of_vertex = vertex_faces(faces)
    live = {v: len(f) for v, f in of_vertex.items()}
    position = {v: -1 for v in of_vertex}
    cache: List[int] = []
    used = [False] * n
    order: List[int] = []

    def vertex_score(v):
        if live[v] <= 0:
            return -1.0
        score = 0.0
        p = position[v]
        if p >= 0:
            if p < 3:
                score = last_bonus
            else:
                score = math.pow(1.0 - (p - 3) / float(cache_size - 3), power)
        return score + valence_boost * math.pow(live[v], -0.5)

    for _ in range(n):
        best = None
        best_score = None
        for i in range(n):
            if used[i]:
                continue
            s = sum(vertex_score(v) for v in faces[i])
            if best_score is None or s > best_score + 1e-12 or (
                    abs(s - best_score) <= 1e-12 and (i > best if tie == "highest" else False)):
                best, best_score = i, s
        used[best] = True
        order.append(best)
        for v in set(faces[best]):
            live[v] -= 1
        for v in faces[best]:
            if v in cache:
                cache.remove(v)
            cache.insert(0, v)
        del cache[cache_size:]
        for v in of_vertex:
            position[v] = cache.index(v) if v in cache else -1
    return order


# --------------------------------------------------------------------------------------------
# a fanning-vertex walk (the Tipsify family)


def fanning(faces, k=12, next_rule="cachepos", dead_end="stack", seed_rule="cursor"):
    n = len(faces)
    of_vertex = vertex_faces(faces)
    live = {v: len(f) for v, f in of_vertex.items()}
    stamp = {v: 0 for v in of_vertex}
    used = [False] * n
    order: List[int] = []
    stack: List[int] = []
    cursor = 0
    s = k + 1
    vertices = sorted(of_vertex)
    fan = vertices[0] if vertices else None

    def emit(i):
        nonlocal s
        used[i] = True
        order.append(i)
        for v in faces[i]:
            stack.append(v)
            live[v] -= 1
            if s - stamp[v] > k:
                stamp[v] = s
                s += 1

    while fan is not None:
        ring = set()
        for i in list(of_vertex[fan]):
            if used[i]:
                continue
            emit(i)
            ring.update(faces[i])
        best = None
        best_priority = -1
        for v in ring:
            if live[v] <= 0:
                continue
            priority = 0
            if next_rule == "cachepos":
                if 2 * live[v] + (s - stamp[v]) <= k:
                    priority = s - stamp[v]
            elif next_rule == "minlive":
                priority = 1000 - live[v]
            if priority > best_priority:
                best, best_priority = v, priority
        if best is None and dead_end == "stack":
            while stack:
                v = stack.pop()
                if live[v] > 0:
                    best = v
                    break
        if best is None:
            while cursor < len(vertices) and live[vertices[cursor]] <= 0:
                cursor += 1
            best = vertices[cursor] if cursor < len(vertices) else None
        fan = best
        if fan is None and len(order) < n:
            for i in range(n):
                if not used[i]:
                    fan = faces[i][0]
                    break
    return order


def score(runner, cases=None, **kwargs):
    cases = cases if cases is not None else corpus.load()
    exact = 0
    total = 0
    firsts = []
    for name in sorted(cases):
        case = cases[name]
        if case.order is None:
            continue
        faces = to_vertex_space(case)
        got = runner(faces, **kwargs)
        total += 1
        if got == case.order:
            exact += 1
        else:
            div = next((i for i, (a, b) in enumerate(zip(got, case.order)) if a != b),
                       min(len(got), len(case.order)))
            firsts.append((name, div, len(faces)))
    return exact, total, firsts


def main() -> int:
    cases = corpus.load()
    print("== global greedy over a cache-and-valence score")
    for cache_size in (8, 12, 16, 24, 32):
        exact, total, _ = score(greedy_score, cases, cache_size=cache_size)
        print("   cache %2d : %3d / %d" % (cache_size, exact, total))
    print("== fanning-vertex walk")
    for k in (6, 8, 10, 12, 16, 24, 32):
        for rule in ("cachepos", "minlive"):
            exact, total, _ = score(fanning, cases, k=k, next_rule=rule)
            print("   k %2d %-9s: %3d / %d" % (k, rule, exact, total))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
