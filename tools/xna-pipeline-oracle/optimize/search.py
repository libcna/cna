"""Search simple lexicographic rules against every decision genuine XNA made.

Each step of each measured answer becomes a table: one row per unused face carrying the features a
rule might read, with the face XNA took marked.  A candidate rule is an ordered list of feature
names, each read as "smaller is better"; it is scored teacher-forced, handed the truth's own
history, so that a wrong answer means the rule is wrong rather than that an earlier mistake put the
state somewhere XNA was never in.  Rules that survive are re-checked by full simulation with
`model.py`.  XNASWEEP-149.
"""

from __future__ import annotations

import itertools
import sys
from typing import Dict, List, Optional, Tuple

sys.path.insert(0, __file__.rsplit("/", 1)[0])

import corpus
import mine

FEATURES = [
    "not_pref", "not_swap", "not_cont", "minlive", "sumlive", "maxlive", "neglive",
    "index", "negindex", "misses", "hits", "minage", "maxage", "sumage", "negminage",
    "negmaxvertex", "maxvertex", "neighbours", "negneighbours",
]


def _row(w: mine.Walk, i: int, pref, swap):
    face = w.faces[i]
    lives = [w.live[v] for v in face]
    ages = [w.age(v) for v in face]
    hits = sum(1 for a in ages if a != 999)
    nb = sum(1 for a, b in ((face[0], face[1]), (face[1], face[2]), (face[2], face[0]))
             if w.across(a, b) is not None)
    return {
        "not_pref": 0 if (pref is not None and i == pref) else 1,
        "not_swap": 0 if (swap is not None and i == swap) else 1,
        "not_cont": 0 if i in (pref, swap) else 1,
        "minlive": min(lives), "sumlive": sum(lives), "maxlive": max(lives),
        "neglive": -min(lives),
        "index": i, "negindex": -i,
        "misses": 3 - hits, "hits": -hits,
        "minage": min(ages), "maxage": max(ages), "sumage": sum(ages),
        "negminage": -min(ages),
        "negmaxvertex": -max(face), "maxvertex": max(face),
        "neighbours": nb, "negneighbours": -nb,
    }


def decisions(cases=None, cache_size: int = 32):
    """Every step, as (case, step, candidate table, chosen face, is_seed)."""
    cases = cases if cases is not None else corpus.load()
    out = []
    for name in sorted(cases):
        case = cases[name]
        if case.order is None:
            continue
        w = mine.Walk(case, cache_size)
        order = case.order
        pending: Optional[Tuple[int, int, Optional[int]]] = None
        for step, chosen in enumerate(order):
            if pending is None or pending[2] is None:
                pref = swap = None
                if pending is not None:
                    pref = w.across(pending[0], pending[1])
            else:
                older, newer, r = pending
                pref = w.across(newer, r)
                swap = w.across(older, r)
            rows = {i: _row(w, i, pref, swap) for i in range(w.n) if not w.used[i]}
            out.append((name, step, rows, chosen, pending is None))
            w.take(chosen)
            triple = w.faces[chosen]
            if pending is None:
                nxt = order[step + 1] if step + 1 < len(order) else None
                if nxt is not None and len(set(w.faces[nxt]) & set(triple)) == 2:
                    shared = set(w.faces[nxt]) & set(triple)
                    pivot = [v for v in triple if v not in shared][0]
                else:
                    pivot = triple[2]
                k = triple.index(pivot)
                pending = (triple[(k + 1) % 3], triple[(k + 2) % 3], None)
                continue
            older, newer, r = pending
            if older in triple and newer in triple:
                rnew = [c for c in triple if c != older and c != newer][0]
                if r is None or chosen == pref:
                    pending = (newer, rnew, rnew)
                elif chosen == swap:
                    pending = (older, rnew, rnew)
                else:
                    pending = None
            else:
                pending = None
    return out


def evaluate(rows, rule):
    best = None
    best_key = None
    for i, features in rows.items():
        key = tuple(features[f] for f in rule)
        if best_key is None or key < best_key:
            best, best_key = i, key
    return best


def score(data, rule):
    return sum(1 for _, _, rows, chosen, _ in data if evaluate(rows, rule) == chosen)


def main() -> int:
    data = decisions()
    print("%d decisions" % len(data))
    depth = int(sys.argv[1]) if len(sys.argv) > 1 else 2
    results = sorted(((score(data, combo), combo)
                      for combo in itertools.permutations(FEATURES, depth)), reverse=True)
    for good, combo in results[:20]:
        print("%5d / %d   %s" % (good, len(data), " > ".join(combo)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
