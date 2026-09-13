"""Score the reconstruction against both Microsoft oracles.  XNASWEEP-149."""
from __future__ import annotations
import sys, itertools, time
sys.path.insert(0, __file__.rsplit("/", 1)[0])
import corpus, hoppe


def designed():
    everything = {}
    everything.update(corpus.load())
    for n in ("2", "3", "4", "5", "6", "7", "8"):
        everything.update(corpus.load("build/xna-sample-sweep/optimize/probes%s.json" % n,
                                      "build/xna-sample-sweep/optimize/answers%s.txt" % n))
    return everything


def heldout():
    return corpus.load("build/xna-sample-sweep/optimize/heldout.json",
                       "build/xna-sample-sweep/optimize/heldout-xna.txt")


def vertex_faces(case):
    """The face list as D3DX sees it: over vertex indices, not position indices.  An unshared mesh
    gives every corner its own vertex, and so has no shared edges at all."""
    verts = {}
    out = []
    used = 0
    for f in case.faces:
        t = []
        for corner in f:
            if case.shared:
                if corner not in verts:
                    verts[corner] = used
                    used += 1
                t.append(verts[corner])
            else:
                t.append(used)
                used += 1
        out.append(tuple(t))
    return out


def score(cases, limit_faces=None, **kw):
    ok = 0
    total = 0
    misses = []
    for name, c in sorted(cases.items()):
        if c.order is None:
            continue
        if limit_faces is not None and len(c.faces) > limit_faces:
            continue
        total += 1
        if hoppe.optimize(vertex_faces(c), **kw) == list(c.order):
            ok += 1
        else:
            misses.append(name)
    return ok, total, misses


if __name__ == "__main__":
    cases = designed()
    small = {n: c for n, c in cases.items() if len(c.faces) <= 40}
    print("searching on %d designed probes of <= 40 faces" % len(small))
    designed_cases = designed()
    held = heldout()
    for label, cs in (("designed", designed_cases), ("held-out", held)):
        t = time.time()
        ok, total, misses = score(cs)
        print("  %-10s %4d / %d   (%.1fs)" % (label, ok, total, time.time() - t))
        if misses:
            print("     misses: %s%s" % (", ".join(misses[:10]),
                                         " ..." if len(misses) > 10 else ""))
