"""Probes for the one thing DirectXMesh's `OptimizeFaces` cannot be given: a non-manifold edge.

plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-149`.  With Microsoft's DirectXMesh algorithm and an
adjacency of *the first other face in input order on each edge, kept only when it is wound the
other way*, all 449 probes of the 616 whose every edge carries at most two faces come back exactly
as genuine XNA and genuine D3DX answer them, and 131 of the 167 that carry an edge with three or
more do.  `D3DXOptimizeFaces` built its own adjacency, so what those 36 measure is the legacy
entry point's rule for an edge more than two faces meet on.  This generator isolates it.

    python3 tools/xna-pipeline-oracle/optimize/nonmanifold.py <out.txt> <out.json>
"""

from __future__ import annotations

import itertools
import json
import sys
from typing import Dict, List, Sequence, Tuple

Face = Tuple[int, int, int]


def _probe(name: str, faces: Sequence[Face], positions: int) -> Dict:
    return {"name": name, "shared": True,
            "positions": [[float(i), float(i * i % 7), 0.0] for i in range(positions)],
            "faces": [list(f) for f in faces]}


def build() -> List[Dict]:
    out: List[Dict] = []

    # A. k faces meeting on the single edge (0, 1), every winding, every input order.
    for k in (3, 4):
        apexes = list(range(2, 2 + k))
        for winding in itertools.product((0, 1), repeat=k):
            for order in itertools.permutations(range(k)):
                faces = []
                for slot in order:
                    p = apexes[slot]
                    faces.append((0, 1, p) if winding[slot] == 0 else (1, 0, p))
                name = "nm%d_w%s_o%s" % (k, "".join(str(w) for w in winding),
                                         "".join(str(o) for o in order))
                out.append(_probe(name, faces, 2 + k))

    # B. the same fan, with a manifold strip hanging off the *first* apex, so the walk has
    #    somewhere to go after it crosses -- which is what makes the crossing observable.
    for k in (3,):
        apexes = list(range(2, 2 + k))
        tail_base = 2 + k
        for winding in itertools.product((0, 1), repeat=k):
            for order in itertools.permutations(range(k)):
                for tail_at in (0, 1, 2):
                    faces = []
                    for slot in order:
                        p = apexes[slot]
                        faces.append((0, 1, p) if winding[slot] == 0 else (1, 0, p))
                    # a quad's worth of strip glued to edge (1, apex[tail_at]) of that face
                    a = apexes[tail_at]
                    faces.append((a, 1, tail_base))
                    faces.append((1, tail_base + 1, tail_base))
                    name = "nmt_w%s_o%s_t%d" % ("".join(str(w) for w in winding),
                                                "".join(str(o) for o in order), tail_at)
                    out.append(_probe(name, faces, tail_base + 2))

    # C. two separate non-manifold edges in one mesh, so that a rule that only ever sees one is
    #    not mistaken for the general one.
    for winding in itertools.product((0, 1), repeat=3):
        faces = []
        for i, p in enumerate((2, 3, 4)):
            faces.append((0, 1, p) if winding[i] == 0 else (1, 0, p))
        for i, p in enumerate((7, 8, 9)):
            faces.append((5, 6, p) if winding[i] == 0 else (6, 5, p))
        out.append(_probe("nm2x_w%s" % "".join(str(w) for w in winding), faces, 10))

    # D. a closed strip of quads where one interior edge carries a third face, swept over which
    #    edge that is -- the shape real content produces.
    for extra_at in range(0, 10, 2):
        faces = []
        for q in range(6):
            a, b = 2 * q, 2 * q + 1
            faces.append((a, b, a + 2))
            faces.append((b, a + 3, a + 2))
        faces.append((extra_at, extra_at + 1, 14))
        out.append(_probe("nmq_at%d" % extra_at, faces, 15))
        faces2 = list(faces[:-1]) + [(extra_at + 1, extra_at, 14)]
        out.append(_probe("nmq_rev_at%d" % extra_at, faces2, 15))
    return out


def write(probes: Sequence[Dict], txt_path: str, json_path: str) -> None:
    with open(txt_path, "w") as fh:
        for p in probes:
            fh.write("P %s %d %d %d\n" % (p["name"], 1 if p["shared"] else 0,
                                          len(p["positions"]), len(p["faces"])))
            for v in p["positions"]:
                fh.write("V %s %s %s\n" % tuple(repr(float(c)) for c in v))
            for f in p["faces"]:
                fh.write("F %d %d %d\n" % tuple(f))
    with open(json_path, "w") as fh:
        json.dump({"probes": list(probes)}, fh, indent=1)


if __name__ == "__main__":
    probes = build()
    write(probes, sys.argv[1], sys.argv[2])
    print("nonmanifold: %d probes" % len(probes))
