"""A global greedy over a vertex cache: the shape XNA's measured order actually has.

At every step the next triangle is the unused one that scores best on a short lexicographic key
read off a simulated post-transform vertex cache and the remaining triangle counts.  The key, the
cache size and the cache's replacement policy are all parameters, because which of them XNA uses is
exactly what the corpus is being asked.  XNASWEEP-149.
"""

from __future__ import annotations

import itertools
import sys
from typing import Dict, List, Optional, Sequence, Tuple

sys.path.insert(0, __file__.rsplit("/", 1)[0])

import corpus
import families

KEYS = ["misses", "hits", "minlive", "sumlive", "maxlive", "index", "negindex",
        "sumage", "minage", "maxage", "negmaxvertex", "maxvertex"]


def run(faces, key: Sequence[str], cache_size: int = 16, policy: str = "lru"):
    n = len(faces)
    of_vertex = families.vertex_faces(faces)
    live = {v: len(f) for v, f in of_vertex.items()}
    used = [False] * n
    cache: List[int] = []
    order: List[int] = []

    def age(v):
        try:
            return len(cache) - cache.index(v)
        except ValueError:
            return 999

    for _ in range(n):
        best = None
        best_key = None
        for i in range(n):
            if used[i]:
                continue
            face = faces[i]
            lives = [live[v] for v in face]
            ages = [age(v) for v in face]
            hits = sum(1 for a in ages if a != 999)
            values = {"misses": 3 - hits, "hits": -hits,
                      "minlive": min(lives), "sumlive": sum(lives), "maxlive": max(lives),
                      "index": i, "negindex": -i,
                      "sumage": sum(ages), "minage": min(ages), "maxage": max(ages),
                      "negmaxvertex": -max(face), "maxvertex": max(face)}
            k = tuple(values[name] for name in key)
            if best_key is None or k < best_key:
                best, best_key = i, k
        used[best] = True
        order.append(best)
        for v in set(faces[best]):
            live[v] -= 1
        for v in faces[best]:
            if policy == "lru":
                if v in cache:
                    cache.remove(v)
                cache.append(v)
            else:  # fifo: a vertex already resident keeps its place
                if v not in cache:
                    cache.append(v)
            while len(cache) > cache_size:
                cache.pop(0)
    return order


def score(cases, key, cache_size, policy):
    exact = 0
    partial = 0
    total = 0
    misses = []
    for name in sorted(cases):
        case = cases[name]
        if case.order is None:
            continue
        faces = families.to_vertex_space(case)
        got = run(faces, key, cache_size, policy)
        total += 1
        if got == case.order:
            exact += 1
        else:
            div = next((i for i, (a, b) in enumerate(zip(got, case.order)) if a != b), 0)
            partial += div
            misses.append((name, div, len(faces)))
    return exact, total, partial, misses


def main() -> int:
    cases = corpus.load()
    candidates = [
        ("misses", "minlive", "negindex"),
        ("misses", "minlive", "index"),
        ("misses", "negindex"),
        ("misses", "sumlive", "negindex"),
        ("misses", "sumage", "negindex"),
        ("misses", "minlive", "sumage", "negindex"),
        ("sumage", "minlive", "negindex"),
        ("misses", "maxlive", "negindex"),
        ("misses", "minlive", "negmaxvertex"),
    ]
    for policy in ("lru", "fifo"):
        for cache_size in (8, 10, 12, 16, 24, 32):
            for key in candidates:
                exact, total, partial, _ = score(cases, key, cache_size, policy)
                if exact >= 40:
                    print("%-5s cache %2d  %-46s %3d / %d  (sum first-divergence %d)"
                          % (policy, cache_size, " > ".join(key), exact, total, partial))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
