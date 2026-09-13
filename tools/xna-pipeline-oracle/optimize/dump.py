"""Print every recorded `OptimizeForCache` probe as input faces, answer faces and permutation."""

from __future__ import annotations

import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])

import oracle
import probes


def main() -> int:
    answers = oracle.load(sys.argv[1] if len(sys.argv) > 1 else oracle.DEFAULT_ORACLE)
    for case in sorted(probes.PROBES):
        probe = probes.PROBES[case]
        answer = answers.get(case)
        if answer is None:
            print("%-52s  NO ANSWER" % case)
            continue
        order = oracle.face_order(answer, probe.faces)
        print("=== %s  (%d faces, %s)" % (case, probe.face_count,
                                          "shared" if probe.shared else "soup"))
        print("  in   : %s" % " ".join("%d:%s" % (i, ",".join(map(str, f)))
                                       for i, f in enumerate(probe.faces)))
        print("  out  : %s" % " ".join(",".join(map(str, f)) for f in answer.faces))
        if order is None:
            print("  order: <faces do not correspond>")
        else:
            rotations = [oracle.face_rotation(answer.faces[i], probe.faces[order[i]])
                         for i in range(len(order))]
            print("  order: %s" % " ".join(map(str, order)))
            print("  rot  : %s" % " ".join("R" if r is None else str(r) for r in rotations))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
