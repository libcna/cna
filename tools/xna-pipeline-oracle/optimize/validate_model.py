"""Check a built model `.xnb` against its genuine reference, one mesh part at a time.

plans/plan_xna_sample_xnb_sweep.md XNASWEEP-149.  The synthetic probes prove that genuine XNA's
`MeshHelper.OptimizeForCache` is `D3DXOptimizeFaces`; this asks the same question of real corpus
content, and it needs no rebuild of anything.  CNA's stand-in optimiser reverses the triangle list,
so reversing CNA's own output recovers the order its optimiser was given.  Handing *that* to D3DX
predicts what XNA's optimiser would have answered, and the reference says whether it did.

Faces are compared by vertex **position**, not by vertex index, so that a disagreement about a
normal or a tangent cannot be mistaken for a disagreement about order.  A part whose triangle
multiset already differs is reported separately: CNA and XNA disagree about that part's geometry
before any optimiser runs, and it is not this question.

    python3 tools/xna-pipeline-oracle/optimize/validate_model.py <cna.xnb> <reference.xnb>
"""

from __future__ import annotations

import collections
import os
import struct
import sys
from typing import Dict, List, Optional, Tuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import ask
import buffers


def _positions(resource: Dict, offset: int, count: int) -> List[Tuple[float, float, float]]:
    return [struct.unpack_from("<3f", record, 0)
            for record in buffers.vertices(resource)[offset:offset + count]]


def _parts(report: Dict) -> List[Dict]:
    return [part for mesh in report["root"]["meshes"] for part in mesh["parts"]]


def _shape(part: Dict):
    return (part["startIndex"], part["primitiveCount"], part["vertexOffset"], part["numVertices"])


def check(cna_path: str, reference_path: str, tag: str = "validate") -> Optional[Dict]:
    """{'exact', 'ordered', 'geometry', 'parts'} or None when the two files are not comparable."""
    cna = buffers.load(cna_path)
    reference = buffers.load(reference_path)
    if "ModelReader" not in (cna.get("rootReader") or ""):
        return None
    parts = _parts(cna)
    if [_shape(p) for p in parts] != [_shape(p) for p in _parts(reference)]:
        return None

    meshes = []
    held: Dict[int, Dict] = {}
    for k, part in enumerate(parts):
        index_buffer = part["indexBuffer"] - 1
        vertex_buffer = part["vertexBuffer"] - 1
        first = part["startIndex"] // 3
        last = first + part["primitiveCount"]
        ours = buffers.triangles(cna["sharedResources"][index_buffer])[first:last]
        theirs = buffers.triangles(reference["sharedResources"][index_buffer])[first:last]
        our_positions = _positions(cna["sharedResources"][vertex_buffer],
                                   part["vertexOffset"], part["numVertices"])
        their_positions = _positions(reference["sharedResources"][vertex_buffer],
                                     part["vertexOffset"], part["numVertices"])
        ours_by_position = collections.Counter(
            tuple(sorted(our_positions[v] for v in t)) for t in ours)
        theirs_by_position = collections.Counter(
            tuple(sorted(their_positions[v] for v in t)) for t in theirs)
        if ours_by_position != theirs_by_position:
            held[k] = {"geometry": True}
            continue
        before = list(reversed(ours))               # undo CNA's reversing stand-in
        held[k] = {"before": before, "ours": our_positions,
                   "theirs": [tuple(sorted(their_positions[v] for v in t)) for t in theirs]}
        meshes.append(("%s_p%03d" % (tag, k),
                       [(float(i), 0.0, 0.0) for i in range(part["numVertices"])],
                       before, True))

    answers = ask.ask(meshes, tag=tag) if meshes else {}
    exact = ordered = geometry = 0
    detail = []
    for k in range(len(parts)):
        row = held[k]
        if row.get("geometry"):
            geometry += 1
            detail.append((k, "geometry differs"))
            continue
        order = answers.get("%s_p%03d" % (tag, k))
        if order is None:
            detail.append((k, "no answer"))
            continue
        ordered += 1
        predicted = [tuple(sorted(row["ours"][v] for v in row["before"][i])) for i in order]
        if predicted == row["theirs"]:
            exact += 1
        else:
            at = next((i for i in range(min(len(predicted), len(row["theirs"])))
                       if predicted[i] != row["theirs"][i]), None)
            detail.append((k, "order differs at %s of %d" % (at, len(predicted))))
    return {"exact": exact, "ordered": ordered, "geometry": geometry,
            "parts": len(parts), "detail": detail}


def main(argv) -> int:
    if len(argv) < 3:
        print(__doc__)
        return 2
    result = check(argv[1], argv[2], tag=os.path.basename(argv[1]).replace(".xnb", ""))
    if result is None:
        print("not comparable: not a model, or its mesh parts differ")
        return 1
    print("%d mesh parts: %d of %d whose geometry matches reproduce XNA's face order exactly; "
          "%d differ in geometry before any optimiser runs"
          % (result["parts"], result["exact"], result["ordered"], result["geometry"]))
    for k, why in result["detail"][:20]:
        print("   part %-4d %s" % (k, why))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
