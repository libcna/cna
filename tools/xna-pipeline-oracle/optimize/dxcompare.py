"""Score Microsoft DirectXMesh's `OptimizeFaces` against genuine XNA and genuine D3DX9.

plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-149`.  Three implementations answer the same probes:

  A. genuine Microsoft XNA 4.0 `MeshHelper.OptimizeForCache`   (OptimizeForCacheOracle.cs)
  B. genuine Microsoft D3DX9 `D3DXOptimizeFaces`               (D3dxOptimizeOracle.c)
  C. Microsoft DirectXMesh `DirectX::OptimizeFaces`            (DxMeshOptimizeOracle.cpp, and the
                                                                dxmesh.py port of the same code)

`D3DXOptimizeFaces` built its own adjacency and `DirectX::OptimizeFaces` is given one, so C is
scored once per candidate adjacency construction.

    python3 tools/xna-pipeline-oracle/optimize/dxcompare.py            # score every construction
    python3 tools/xna-pipeline-oracle/optimize/dxcompare.py --verify <answers.txt>
                                                                      # port == genuine binary?
    python3 tools/xna-pipeline-oracle/optimize/dxcompare.py --diff <name> [construction]
"""

from __future__ import annotations

import sys
import time
from typing import Dict, List, Optional, Sequence

sys.path.insert(0, __file__.rsplit("/", 1)[0])

import corpus
import d3dx
import dxmesh


def designed() -> Dict[str, corpus.Case]:
    everything = {}
    everything.update(corpus.load())
    for n in ("2", "3", "4", "5", "6", "7", "8"):
        everything.update(corpus.load("build/xna-sample-sweep/optimize/probes%s.json" % n,
                                      "build/xna-sample-sweep/optimize/answers%s.txt" % n))
    return everything


def heldout() -> Dict[str, corpus.Case]:
    return corpus.load("build/xna-sample-sweep/optimize/heldout.json",
                       "build/xna-sample-sweep/optimize/heldout-xna.txt")


def vertex_faces(case: corpus.Case):
    """The face list as D3DX sees it: over vertex indices, not position indices.  An unshared mesh
    gives every corner its own vertex, and so has no shared edges at all."""
    verts: Dict[int, int] = {}
    out = []
    used = 0
    for f in case.faces:
        t = []
        for cornr in f:
            if case.shared:
                if cornr not in verts:
                    verts[cornr] = used
                    used += 1
                t.append(verts[cornr])
            else:
                t.append(used)
                used += 1
        out.append(tuple(t))
    return out


def score(cases: Dict[str, corpus.Case], construction: str, **kw):
    ok, total, misses = 0, 0, []
    for name, c in sorted(cases.items()):
        if c.order is None:
            continue
        total += 1
        if dxmesh.order_of(vertex_faces(c), construction, **kw) == list(c.order):
            ok += 1
        else:
            misses.append(name)
    return ok, total, misses


def verify(path: str, cases: Dict[str, corpus.Case], construction: str) -> None:
    """The port answers what the compiled DirectXMesh answers, probe for probe."""
    remaps = d3dx.load_remaps(path)
    same, differ, missing = 0, [], 0
    for name, c in sorted(cases.items()):
        entry = remaps.get(name)
        if entry is None or entry[0] is None:
            missing += 1
            continue
        mine = dxmesh.order_of(vertex_faces(c), construction)
        if list(entry[0]) == mine:
            same += 1
        else:
            differ.append(name)
    print("port == genuine DirectXMesh binary: %d same, %d differ, %d not measured"
          % (same, len(differ), missing))
    if differ:
        print("  differing:", ", ".join(differ[:12]))


def diff(name: str, construction: str) -> None:
    cases = designed()
    cases.update(heldout())
    c = cases[name]
    faces = vertex_faces(c)
    mine = dxmesh.order_of(faces, construction)
    print("probe   %s  (%d faces, shared=%s)" % (name, len(faces), c.shared))
    print("faces   %s" % (faces,))
    adjacency = dxmesh.ADJACENCY[construction](faces)
    print("adj     %s" % ([("-" if a == dxmesh.UNUSED32 else a) for a in adjacency],))
    status = dxmesh.MeshStatus([i for f in faces for i in f], len(faces), adjacency)
    print("phys    %s" % ([("-" if a == dxmesh.UNUSED32 else a) for a in status.physical],))
    print("XNA     %s" % (list(c.order),))
    print("DXMesh  %s" % (mine,))
    for i, (a, b) in enumerate(zip(list(c.order), mine)):
        if a != b:
            print("first difference at output position %d: XNA %d, DirectXMesh %d" % (i, a, b))
            break


def main() -> None:
    if "--diff" in sys.argv:
        i = sys.argv.index("--diff")
        diff(sys.argv[i + 1], sys.argv[i + 2] if len(sys.argv) > i + 2 else "first_other")
        return
    cases = designed()
    held = heldout()
    if "--verify" in sys.argv:
        i = sys.argv.index("--verify")
        construction = sys.argv[i + 2] if len(sys.argv) > i + 2 else "first_other"
        which = dict(cases)
        which.update(held)
        verify(sys.argv[i + 1], which, construction)
        return
    only = [a for a in sys.argv[1:] if not a.startswith("-")]
    for construction in (only or sorted(dxmesh.ADJACENCY)):
        line = "%-20s" % construction
        for label, cs in (("designed", cases), ("held-out", held)):
            t = time.time()
            ok, total, misses = score(cs, construction)
            line += "  %s %4d/%-4d (%4.1fs)" % (label, ok, total, time.time() - t)
        print(line)


if __name__ == "__main__":
    main()
