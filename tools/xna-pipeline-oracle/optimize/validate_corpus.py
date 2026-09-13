"""Run `validate_model.py`'s question over every model the sweep built, and sort the answers.

plans/plan_xna_sample_xnb_sweep.md XNASWEEP-149.  A differing model `.xnb` can differ for two
reasons that the sweep's own diff cannot tell apart: CNA put the right triangles in a different
order, which is `XNASWEEP-149`, or it did not have the right triangles at all, which is a defect of
its own.  This asks each one and counts them, so a session can see at a glance whether any model
still has the second kind.

    python3 tools/xna-pipeline-oracle/optimize/validate_corpus.py <sweep-results.json> <outdir>
"""

from __future__ import annotations

import collections
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import buffers
import validate_model

ROOT = "/rv/tmp/samples"


def main(argv) -> int:
    if len(argv) < 3:
        print(__doc__)
        return 2
    results = json.load(open(argv[1]))
    outdir = argv[2]
    totals = collections.Counter()
    per_file = []
    for unit in results["results"]:
        folder = os.path.join(outdir, unit["outputRoot"].replace("/", "_"))
        if not os.path.isdir(folder):
            continue
        for row in unit.get("rows", []):
            if row.get("result") != "differs":
                continue
            reference = os.path.join(ROOT, row["reference"])
            built = os.path.join(folder, row["asset"] + ".xnb")
            if not (os.path.exists(built) and os.path.exists(reference)):
                continue
            try:
                if "ModelReader" not in (buffers.load(built).get("rootReader") or ""):
                    continue
            except Exception:
                continue
            tag = ("v" + str(len(per_file)))
            try:
                answer = validate_model.check(built, reference, tag=tag)
            except Exception as error:
                totals["error"] += 1
                per_file.append((row["reference"], "error: %s" % error))
                continue
            if answer is None:
                totals["parts differ"] += 1
                per_file.append((row["reference"], "mesh parts differ"))
                continue
            totals["models"] += 1
            totals["parts"] += answer["parts"]
            totals["exact"] += answer["exact"]
            totals["ordered"] += answer["ordered"]
            totals["geometry"] += answer["geometry"]
            if answer["geometry"] or answer["exact"] != answer["ordered"]:
                per_file.append((row["reference"],
                                 "%d/%d exact, %d geometry" % (answer["exact"], answer["ordered"],
                                                               answer["geometry"])))
    print("models compared: %d   mesh parts: %d" % (totals["models"], totals["parts"]))
    print("  parts whose geometry matches and whose order D3DX reproduces: %d of %d"
          % (totals["exact"], totals["ordered"]))
    print("  parts whose geometry differs before any optimiser runs:       %d" % totals["geometry"])
    print("  models whose mesh parts do not line up at all:                %d" % totals["parts differ"])
    print("  models the comparison could not read:                         %d" % totals["error"])
    if per_file:
        print("\nmodels with something other than a pure ordering difference:")
        for name, why in per_file[:40]:
            print("   %-72s %s" % (name[-72:], why))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
