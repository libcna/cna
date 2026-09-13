"""Compare one probe's genuine answer with a candidate walk, decision by decision."""

from __future__ import annotations

import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])

import oracle
import probes
import simulate


def main() -> int:
    case = sys.argv[1]
    rule_name = sys.argv[2] if len(sys.argv) > 2 else "minval_highest_vertex"
    if not case.startswith("meshhelper/"):
        case = "meshhelper/" + case
    probe = probes.PROBES[case]
    answers = oracle.load()
    truth = oracle.face_order(answers[case], probe.faces)
    mesh = simulate.mesh_of(probe)
    trace = simulate.walk(mesh, simulate.SEED_RULES[rule_name])

    print("case      : %s (%d faces, %s)" % (case, probe.face_count,
                                             "shared" if probe.shared else "soup"))
    print("faces(v)  : %s" % " ".join("%d:%s" % (i, ",".join(map(str, f)))
                                      for i, f in enumerate(mesh.faces)))
    print("truth     : %s" % " ".join(map(str, truth)))
    print("model     : %s" % " ".join(map(str, trace.order)))
    print("model seed: %s" % " ".join(map(str, trace.seeds)))
    print("model strp: %s" % " | ".join(" ".join(map(str, s)) for s in trace.strips))

    # The truth's own strip decomposition, read off adjacency in vertex space.
    adjacent = lambda a, b: len(set(mesh.faces[a]) & set(mesh.faces[b])) >= 2
    strips = []
    for face in truth:
        if strips and adjacent(strips[-1][-1], face):
            strips[-1].append(face)
        else:
            strips.append([face])
    print("truth strp: %s" % " | ".join(" ".join(map(str, s)) for s in strips))
    print("truth seed: %s   lengths %s" % (" ".join(str(s[0]) for s in strips),
                                           [len(s) for s in strips]))
    for i, (a, b) in enumerate(zip(trace.order, truth)):
        if a != b:
            print("FIRST DIVERGENCE at %d: model %d (%s), truth %d (%s)"
                  % (i, a, mesh.faces[a], b, mesh.faces[b]))
            break
    else:
        print("EXACT" if len(trace.order) == len(truth) else "LENGTH MISMATCH")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
