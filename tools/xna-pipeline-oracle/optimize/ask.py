"""Ask the D3DX oracle about a batch of meshes, in one Wine start.

plans/plan_xna_sample_xnb_sweep.md XNASWEEP-149.  The measurement loop for the reconstruction:
build meshes in Python, get the face order genuine `D3DXOptimizeFaces` answers, in bulk.
`XNASWEEP-149` proved that order is genuine XNA's `MeshHelper.OptimizeForCache` on 616 probes, so
this is the fast stand-in for the .NET oracle during inference; a conclusion is confirmed against
the XNA oracle itself before it is acted on.
"""

from __future__ import annotations

import os
import subprocess
import sys
from typing import Dict, List, Sequence, Tuple

Face = Tuple[int, int, int]

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
WORK = os.path.join(REPO, "build/xna-sample-sweep/optimize/ask")


def ask(meshes: Sequence[Tuple[str, Sequence[Sequence[float]], Sequence[Face], bool]],
        tag: str = "ask") -> Dict[str, List[int]]:
    """[(name, positions, faces, shared)] -> {name: face order}, straight from D3DX."""
    os.makedirs(WORK, exist_ok=True)
    probes = os.path.join(WORK, tag + ".txt")
    answers = os.path.join(WORK, tag + "-d3dx.txt")
    with open(probes, "w") as out:
        for name, positions, faces, shared in meshes:
            out.write("P %s %d %d %d\n" % (name, 1 if shared else 0, len(positions), len(faces)))
            for p in positions:
                out.write("V %r %r %r\n" % (float(p[0]), float(p[1]), float(p[2])))
            for a, b, c in faces:
                out.write("F %d %d %d\n" % (a, b, c))
    subprocess.run(["bash", os.path.join(REPO, "tools/xna-pipeline-oracle/optimize/run-d3dx-oracle.sh"),
                    probes, answers], check=True, capture_output=True)
    out: Dict[str, List[int]] = {}
    for line in open(answers):
        line = line.strip()
        if not line:
            continue
        name, _, rest = line.partition("|")
        faces_text, _, _ = rest.partition("|")
        if faces_text.startswith("ERROR") or faces_text == "TOOBIG":
            out[name] = None
            continue
        out[name] = [int(v) for v in faces_text.split(",") if v != ""]
    return out


def ask_faces(faces: Sequence[Face], shared: bool = True) -> List[int]:
    """One mesh, positions invented (they are ignored -- XNASWEEP-149 measured that)."""
    n = 1 + max(max(f) for f in faces)
    positions = [(float(i), 0.0, 0.0) for i in range(n)]
    return ask([("one", positions, faces, shared)], tag="one")["one"]
