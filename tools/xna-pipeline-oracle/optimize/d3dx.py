"""Load the D3DX9 oracle's answers and score them against genuine XNA's, probe for probe.

plans/plan_xna_sample_xnb_sweep.md XNASWEEP-149.  `D3dxOptimizeOracle.c` records the face remap
`D3DXOptimizeFaces` returns, verbatim.  Which way round a remap reads is not documented sharply
enough to assume, so both readings are decoded here and both are scored; the corpus decides.
"""

from __future__ import annotations

import sys
from typing import Dict, List, Optional, Sequence

sys.path.insert(0, __file__.rsplit("/", 1)[0])

import corpus

DEFAULT_ANSWERS = "build/xna-sample-sweep/optimize/d3dx-answers.txt"


def load_remaps(path: str = DEFAULT_ANSWERS):
    """name -> (faceRemap, vertexRemap), each a list of ints, or None where the call failed."""
    out: Dict[str, tuple] = {}
    for line in open(path):
        line = line.strip()
        if not line:
            continue
        name, _, rest = line.partition("|")
        faces_text, _, vertices_text = rest.partition("|")
        if faces_text.startswith("ERROR") or faces_text == "TOOBIG":
            out[name] = (None, None)
            continue
        faces = [int(v) for v in faces_text.split(",") if v != ""]
        vertices = [int(v) for v in vertices_text.split(",") if v != ""]
        out[name] = (faces, vertices)
    return out


def order_destination(remap: Sequence[int]) -> Optional[List[int]]:
    """Read remap[i] as *the new position of old face i*: order[remap[i]] = i."""
    n = len(remap)
    order = [None] * n
    for i, dest in enumerate(remap):
        if not (0 <= dest < n) or order[dest] is not None:
            return None
        order[dest] = i
    return order


def order_source(remap: Sequence[int]) -> Optional[List[int]]:
    """Read remap[i] as *the old face now at position i*: order = remap."""
    n = len(remap)
    if sorted(remap) != list(range(n)):
        return None
    return list(remap)


def load_cases():
    everything = {}
    everything.update(corpus.load())
    for n in ("2", "3", "4", "5", "6", "7", "8"):
        everything.update(corpus.load("build/xna-sample-sweep/optimize/probes%s.json" % n,
                                      "build/xna-sample-sweep/optimize/answers%s.txt" % n))
    return everything


def main():
    cases = load_cases()
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    remaps = load_remaps(args[0] if args else DEFAULT_ANSWERS)
    for label, decode in (("remap[i] = new position of face i", order_destination),
                          ("remap[i] = face now at position i", order_source)):
        exact = 0
        missing = 0
        undecodable = 0
        differing: List[str] = []
        for name, case in sorted(cases.items()):
            if case.order is None:
                continue
            entry = remaps.get(name)
            if entry is None or entry[0] is None:
                missing += 1
                continue
            order = decode(entry[0])
            if order is None:
                undecodable += 1
                continue
            if order == list(case.order):
                exact += 1
            else:
                differing.append(name)
        total = sum(1 for c in cases.values() if c.order is not None)
        print("%-38s %4d / %d exact   (%d not measured, %d undecodable)"
              % (label, exact, total, missing, undecodable))
        if differing and "-v" in sys.argv:
            print("   first differing:", ", ".join(differing[:12]))


if __name__ == "__main__":
    main()
