#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md §11: the counts, printed from the tools rather than typed.

§11 says nothing in it is hand-counted, and this is what makes that true: it reads the taxonomy
and the sweep results of one or more runs and prints the two tables in the file's own Markdown, so
a run's numbers are copied rather than transcribed.

    python3 tools/xna-sample-sweep/status_table.py run42 run44 run47
"""
from __future__ import annotations

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
MANIFEST = os.path.join(REPO, "build", "xna-sample-sweep", "manifest")

ORDER = ["IDENTICAL", "SEMANTICALLY_IDENTICAL", "ACCEPTED_DIFFERENCE", "CUSTOM_PIPELINE_GAP",
         "ENVIRONMENT_GAP", "CORPUS_GAP", "REFERENCE_REMOVED", "UNEXPLAINED"]


def read(name, *patterns):
    """The first of these files that exists, read as JSON.

    Two spellings, because the taxonomy of the earlier runs was written as `taxonomy-runNN.json`
    and the later ones as `sweep-taxonomy-runNN.json`. A table that silently drops a run it cannot
    find its file for is worse than one that says so, which is why the caller checks for None.
    """
    for pattern in patterns:
        for candidate in (pattern % name, pattern % name.replace("run", "")):
            path = os.path.join(MANIFEST, candidate)
            if os.path.exists(path):
                with open(path, encoding="utf-8") as handle:
                    return json.load(handle)
    return None


def main(argv=None):
    runs = list(sys.argv[1:] if argv is None else argv)
    if not runs:
        print(__doc__.strip().splitlines()[-1].strip(), file=sys.stderr)
        return 2
    taxonomies = {run: read(run, "sweep-taxonomy-%s.json", "taxonomy-%s.json") for run in runs}
    results = {run: read(run, "sweep-results-%s.json") for run in runs}
    missing = [run for run in runs if taxonomies[run] is None]
    if missing:
        print("status_table: no taxonomy for %s" % ", ".join(missing), file=sys.stderr)
        return 2

    print("| | %s |" % " | ".join(runs))
    print("|---|%s" % ("---:|" * len(runs)))
    for row, key in (("byte-identical", "identical"), ("differing", "differs"),
                     ("missing (CNA produced nothing)", "missing"),
                     ("build units that finished", None)):
        cells = []
        for run in runs:
            answer = results.get(run)
            if answer is None:
                cells.append("--")
            elif key is None:
                cells.append(str(answer.get("unitsBuilt", "--")))
            else:
                cells.append(str(answer.get("totals", {}).get(key, "--")))
        print("| %s | %s |" % (row, " | ".join(cells)))
    print()
    print("| class | %s |" % " | ".join(runs))
    print("|---|%s" % ("---:|" * len(runs)))
    for name in ORDER:
        cells = [str(taxonomies[run]["counts"].get(name, 0)) for run in runs]
        print("| `%s` | %s |" % (name, " | ".join(cells)))
    print("| **total** | %s |"
          % " | ".join(str(sum(taxonomies[run]["counts"].values())) for run in runs))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
