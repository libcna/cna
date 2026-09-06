#!/usr/bin/env python3
"""plans/plan_xnapipeline_parity.md XNAPP-266 (§24): compare XNA's `.xnb` with CNA's, semantically.

XNAPP-265 established that both builds accept and refuse the same sources. That is the coarsest
question the corpus can answer. This one asks the next: given that both produced a file, do the two
files *mean* the same thing -- the same root reader, the same type-reader table, the same object
graph, the same texture format and pixels, the same audio format block and loop region, the same
shared resources, the same external references.

Both sides are read by `tools/xnb/xnb_conformance.py`, which is an independent parser: it shares no
code with CNA's writer or with CNA's reader, so agreeing with it is evidence rather than a tautology.

A difference is one of three things, and the third is the only one that is a problem:

  * **accepted** -- listed in `decisions.json` with a reason. A deliberate divergence, or something
    the environment cannot settle. The reason is the record; this tool only checks that one exists.
  * **absent** -- one side has no file, because the corpus case is one it refuses. Reported, not
    compared.
  * **open** -- everything else. These are the rows that fail the run.

Usage:
    compare.py --xna <dir> --cna <dir> [--decisions <file>] [--json <file>]
"""
from __future__ import annotations

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "xnb"))
import xnb_conformance  # noqa: E402  (path is set above on purpose)


# What §24 says to compare, in the order a reader would look at it. A key absent from a report is
# compared as absent on both sides rather than skipped, so a field that stops being written is a
# difference rather than a silence.
COMPARED = (
    "platform",
    "version",
    "graphicsProfile",
    "compression",
    "rootReader",
    "typeReaders",
    "sharedResourceCount",
    "root",
    "sharedResources",
)


def load(path):
    """Parses one `.xnb`, or answers the failure as a string."""
    try:
        return xnb_conformance.parse(path), None
    except xnb_conformance.XnbError as error:
        return None, str(error)


def differences(left, right, where=""):
    """Every place two parsed reports disagree, as `path: xna != cna` strings."""
    if isinstance(left, dict) and isinstance(right, dict):
        found = []
        for key in sorted(set(left) | set(right)):
            if key in ("path", "status", "totalLength"):
                continue
            found += differences(left.get(key, "<absent>"), right.get(key, "<absent>"),
                                 where + "/" + key if where else key)
        return found
    if isinstance(left, list) and isinstance(right, list):
        if len(left) != len(right):
            return ["%s: %d element(s) vs %d" % (where, len(left), len(right))]
        found = []
        for index, (one, other) in enumerate(zip(left, right)):
            found += differences(one, other, "%s[%d]" % (where, index))
        return found
    if left != right:
        return ["%s: %r vs %r" % (where, left, right)]
    return []


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--xna", required=True, help="directory of .xnb files XNA produced")
    parser.add_argument("--cna", required=True, help="directory of .xnb files CNA produced")
    parser.add_argument("--decisions", default=os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "decisions.json"))
    parser.add_argument("--json", help="write the full report here")
    arguments = parser.parse_args(argv[1:])

    decisions = {}
    if os.path.exists(arguments.decisions):
        with open(arguments.decisions, "r", encoding="utf-8") as handle:
            document = json.load(handle)
        decisions = {row["case"]: row for row in document.get("accepted", [])}

    report = {"cases": [], "accepted": 0, "absent": 0, "open": 0, "identical": 0}
    names = sorted(name for name in os.listdir(arguments.xna) if name.endswith(".xnb"))
    for name in names:
        case = name[:-4]
        xna_path = os.path.join(arguments.xna, name)
        cna_path = os.path.join(arguments.cna, name)
        row = {"case": case}
        if not os.path.exists(cna_path):
            # A case CNA has no route for at all is still a difference, and a recorded reason
            # covers it the same way it covers a difference in the bytes.
            if case in decisions:
                row["outcome"] = "accepted"
                row["reason"] = decisions[case]["reason"]
                row["detail"] = "CNA produced no file for this case"
                report["accepted"] += 1
            else:
                row["outcome"] = "absent"
                row["detail"] = "CNA produced no file for this case"
                report["absent"] += 1
            report["cases"].append(row)
            continue

        xna, xna_error = load(xna_path)
        cna, cna_error = load(cna_path)
        if xna_error or cna_error:
            row["outcome"] = "open"
            row["detail"] = "unreadable: " + (xna_error or "") + (cna_error or "")
            report["open"] += 1
            report["cases"].append(row)
            continue

        found = []
        for key in COMPARED:
            found += differences(xna.get(key, "<absent>"), cna.get(key, "<absent>"), key)
        if not found:
            row["outcome"] = "identical"
            report["identical"] += 1
        elif case in decisions:
            row["outcome"] = "accepted"
            row["reason"] = decisions[case]["reason"]
            row["differences"] = found
            report["accepted"] += 1
        else:
            row["outcome"] = "open"
            row["differences"] = found
            report["open"] += 1
        report["cases"].append(row)

    # A decision for a case that no longer differs is a decision that has outlived its reason.
    compared = {row["case"] for row in report["cases"]}
    for case, decision in sorted(decisions.items()):
        if case not in compared:
            report["cases"].append({"case": case, "outcome": "open",
                                    "detail": "accepted in decisions.json but not in the corpus"})
            report["open"] += 1
        elif next(r for r in report["cases"] if r["case"] == case)["outcome"] == "identical":
            report["cases"].append({"case": case, "outcome": "open",
                                    "detail": "accepted in decisions.json but the two now agree; "
                                              "remove the decision"})
            report["open"] += 1
        del decision

    for row in report["cases"]:
        mark = {"identical": "same    ", "accepted": "accepted", "absent": "absent  ",
                "open": "OPEN    "}[row["outcome"]]
        print("%s %s" % (mark, row["case"]))
        for line in row.get("differences", [])[:8]:
            print("             " + line)
        if row.get("detail"):
            print("             " + row["detail"])
    print("identical %d, accepted %d, absent %d, open %d" %
          (report["identical"], report["accepted"], report["absent"], report["open"]))

    if arguments.json:
        with open(arguments.json, "w", encoding="utf-8") as handle:
            json.dump(report, handle, indent=2, sort_keys=True)
            handle.write("\n")
    return 1 if report["open"] else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
