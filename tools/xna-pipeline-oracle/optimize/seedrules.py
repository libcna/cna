"""Score candidate restart rules against every seed genuine XNA actually chose.

A seed decision is isolated from the walk by replaying the truth up to it: the state is exactly
what XNA stood in, and the only question is which unused face it opened the next run with.  A rule
is a function from that state to a face; it is scored on how many of the corpus's seeds it
reproduces, and every miss is reported with the candidates it had.  XNASWEEP-149.
"""

from __future__ import annotations

import sys
from typing import Callable, Dict, List, Optional

sys.path.insert(0, __file__.rsplit("/", 1)[0])

import corpus
import mine


class SeedState:
    def __init__(self, walk: mine.Walk):
        self.w = walk

    @property
    def unused(self):
        return [i for i, u in enumerate(self.w.used) if not u]

    def live(self, v):
        return self.w.live[v]

    def minlive_vertices(self):
        live = [n for n in self.w.live.values() if n > 0]
        low = min(live)
        return low, [v for v, n in self.w.live.items() if n == low]


def _faces_of(state: SeedState, vertices):
    return sorted({i for v in vertices for i in state.w.of_vertex[v] if not state.w.used[i]})


def r_highest_face(state):
    return max(state.unused)


def r_lowest_face(state):
    return min(state.unused)


def r_minlive_highest_face(state):
    _, verts = state.minlive_vertices()
    faces = _faces_of(state, verts)
    return max(faces) if faces else max(state.unused)


def r_minlive_lowest_face(state):
    _, verts = state.minlive_vertices()
    faces = _faces_of(state, verts)
    return min(faces) if faces else min(state.unused)


def r_minlive_highest_vertex(state):
    _, verts = state.minlive_vertices()
    v = max(verts)
    return max(i for i in state.w.of_vertex[v] if not state.w.used[i])


def r_minlive_lowest_vertex(state):
    _, verts = state.minlive_vertices()
    v = min(verts)
    return max(i for i in state.w.of_vertex[v] if not state.w.used[i])


def r_minlive_cached_first(state):
    """Minimum live count; among those prefer a vertex still in the cache, oldest first."""
    _, verts = state.minlive_vertices()
    def key(v):
        age = state.w.age(v)
        cached = age != 999
        return (0 if cached else 1, -age if cached else 0, -v)
    v = sorted(verts, key=key)[0]
    return max(i for i in state.w.of_vertex[v] if not state.w.used[i])


def r_minlive_uncached_first(state):
    _, verts = state.minlive_vertices()
    def key(v):
        age = state.w.age(v)
        cached = age != 999
        return (1 if cached else 0, age, -v)
    v = sorted(verts, key=key)[0]
    return max(i for i in state.w.of_vertex[v] if not state.w.used[i])


RULES: Dict[str, Callable] = {
    "highest_face": r_highest_face,
    "lowest_face": r_lowest_face,
    "minlive_highest_face": r_minlive_highest_face,
    "minlive_lowest_face": r_minlive_lowest_face,
    "minlive_highest_vertex": r_minlive_highest_vertex,
    "minlive_lowest_vertex": r_minlive_lowest_vertex,
    "minlive_cached_first": r_minlive_cached_first,
    "minlive_uncached_first": r_minlive_uncached_first,
}


def seed_states(cases=None, cache_size: int = 12):
    """Every (state, chosen seed) pair the corpus contains, replayed from the truth."""
    cases = cases if cases is not None else corpus.load()
    out = []
    for name in sorted(cases):
        case = cases[name]
        if case.order is None:
            continue
        w = mine.Walk(case, cache_size)
        order = w.order
        step = 0
        older = newer = None
        while step < len(order):
            face = order[step]
            if older is None:
                out.append((name, step, SeedState(w), face,
                            {"used": list(w.used), "live": dict(w.live), "cache": list(w.cache)}))
                # snapshot taken before the seed is consumed
                out[-1] = (name, step, _Frozen(w), face, None)
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
                step += 1
                continue
            nxt = w.across(older, newer)
            if nxt is None or nxt != face:
                older = newer = None
                continue
            w.take(nxt)
            step += 1
            triple = w.faces[nxt]
            r = [c for c in triple if c != older and c != newer][0]
            pref = w.across(newer, r)
            swap = w.across(older, r)
            chosen = order[step] if step < len(order) else None
            if chosen is not None and chosen == pref:
                older, newer = newer, r
            elif chosen is not None and chosen == swap:
                older, newer = older, r
            else:
                older = newer = None
    return out


class _Frozen:
    """A copy of the walk state, so a rule can be scored after the replay has moved on."""

    def __init__(self, w: mine.Walk):
        self.used = list(w.used)
        self.live = dict(w.live)
        self.cache = list(w.cache)
        self.of_vertex = w.of_vertex
        self.faces = w.faces

    def age(self, v):
        return len(self.cache) - self.cache.index(v) if v in self.cache else 999


def main() -> int:
    states = seed_states()
    print("%d seed decisions" % len(states))
    for name, rule in RULES.items():
        misses = []
        for case, step, frozen, chosen, _ in states:
            state = SeedState(frozen)
            try:
                got = rule(state)
            except Exception:
                got = None
            if got != chosen:
                misses.append((case, step, chosen, got))
        print("%-24s %4d / %d" % (name, len(states) - len(misses), len(states)))
        if len(sys.argv) > 1 and sys.argv[1] == name:
            for m in misses[:60]:
                print("    %-26s step %3d truth %3d got %s" % m)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
