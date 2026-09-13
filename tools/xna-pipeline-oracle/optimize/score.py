"""Score every candidate seed rule against the recorded genuine-XNA probe answers."""

from __future__ import annotations

import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])

import oracle
import probes
import simulate


def truth_orders():
    answers = oracle.load()
    out = {}
    for case, probe in probes.PROBES.items():
        answer = answers.get(case)
        if answer is None:
            continue
        order = oracle.face_order(answer, probe.faces)
        if order is None:
            continue
        out[case] = order
    return out


def first_divergence(a, b):
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return i
    if len(a) != len(b):
        return min(len(a), len(b))
    return None


def main() -> int:
    truth = truth_orders()
    names = sys.argv[1:] or list(simulate.SEED_RULES)
    rows = []
    for rule_name in names:
        rule = simulate.SEED_RULES[rule_name]
        exact = 0
        detail = []
        for case in sorted(truth):
            probe = probes.PROBES[case]
            mesh = simulate.mesh_of(probe)
            trace = simulate.walk(mesh, rule)
            div = first_divergence(trace.order, truth[case])
            if div is None:
                exact += 1
                detail.append((case, "exact", len(probe.faces)))
            else:
                detail.append((case, "diverges at %d/%d" % (div, len(probe.faces)),
                               len(probe.faces)))
        rows.append((rule_name, exact, detail))
    rows.sort(key=lambda r: -r[1])
    for rule_name, exact, detail in rows:
        print("== %-26s %2d / %d exact" % (rule_name, exact, len(truth)))
        for case, verdict, count in detail:
            if verdict != "exact":
                print("     %-52s %s" % (case, verdict))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
