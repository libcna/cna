#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-190/203: the `REFERENCE_REMOVED` class, itemised.

A frozen reference that is no longer on disk is not a comparison that passed and not a comparison
that failed, and the difference between "the bytes are still built and still compared somewhere
else" and "this reference is gone" is not something a count can carry. This prints one row per
removed reference with the four facts the class needs:

  * what it was -- sample, path, size and the `sha256` the corpus froze;
  * whether a **byte-identical twin** is still on disk *and* is itself a frozen reference of this
    corpus, which is what decides whether the same bytes are still compared;
  * what that twin's own verdict is, so a twin nobody compares cannot stand in for one;
  * whether the directory it was in still exists, which is what says a prune removed a subtree
    rather than a file being edited away.

A removed reference with no twin would be lost coverage and is reported as such. Nothing here
writes to the read-only tree.

    python3 tools/xna-sample-sweep/removed_references.py [--corpus <corpus.json>]
                                                         [--taxonomy <taxonomy.json>]
                                                         [--json <out.json>]
"""
from __future__ import annotations

import argparse
import collections
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default="/rv/tmp/samples")
    parser.add_argument("--corpus", default=os.path.join(
        REPO, "build", "xna-sample-sweep", "manifest", "sample-xnb-corpus.json"))
    parser.add_argument("--taxonomy", default=None)
    parser.add_argument("--json", default=None)
    args = parser.parse_args(argv)

    with open(args.corpus, encoding="utf-8") as handle:
        corpus = json.load(handle)
    entries = corpus["entries"]
    verdicts = {}
    if args.taxonomy:
        with open(args.taxonomy, encoding="utf-8") as handle:
            verdicts = json.load(handle).get("verdicts", {})

    bySha = collections.defaultdict(list)
    for entry in entries:
        bySha[entry.get("sha256")].append(entry["relative"])

    rows = []
    for entry in entries:
        path = os.path.join(args.root, entry["relative"])
        if os.path.exists(path):
            continue
        twins = []
        for other in bySha[entry.get("sha256")]:
            if other == entry["relative"]:
                continue
            if os.path.exists(os.path.join(args.root, other)):
                twins.append({"reference": other,
                              "verdict": verdicts.get(other, {}).get("class")})
        rows.append({
            "reference": entry["relative"],
            "sample": entry["relative"].split("/")[0],
            "size": entry.get("size"),
            "sha256": entry.get("sha256"),
            "directoryStillThere": os.path.isdir(os.path.dirname(path)),
            "byteIdenticalTwinsOnDisk": twins,
            "coverageLost": not twins,
        })

    rows.sort(key=lambda row: row["reference"])
    lost = [row for row in rows if row["coverageLost"]]
    uncompared = [row for row in rows
                  if row["byteIdenticalTwinsOnDisk"] and
                  not any(twin["verdict"] in ("IDENTICAL", "SEMANTICALLY_IDENTICAL",
                                              "ACCEPTED_DIFFERENCE", "UNEXPLAINED")
                          for twin in row["byteIdenticalTwinsOnDisk"])]
    print("=== %d frozen reference(s) are no longer on disk ===" % len(rows))
    for row in rows:
        twins = ", ".join("%s [%s]" % (twin["reference"], twin["verdict"] or "unclassified")
                          for twin in row["byteIdenticalTwinsOnDisk"])
        print("  %-96s dir=%s twin=%s"
              % (row["reference"][-96:], "yes" if row["directoryStillThere"] else "no",
                 twins if twins else "NONE -- coverage lost"))
    print()
    print("  with a byte-identical twin still on disk: %d" % (len(rows) - len(lost)))
    print("  with no twin at all (coverage lost):      %d" % len(lost))
    print("  whose every twin is itself uncompared:    %d" % len(uncompared))
    if args.json:
        os.makedirs(os.path.dirname(os.path.abspath(args.json)), exist_ok=True)
        with open(args.json, "w", encoding="utf-8") as handle:
            json.dump({"generator": "tools/xna-sample-sweep/removed_references.py",
                       "root": args.root, "removed": rows,
                       "counts": {"removed": len(rows), "coverageLost": len(lost),
                                  "twinsUncompared": len(uncompared)}},
                      handle, indent=1, sort_keys=True)
    return 1 if lost else 0


if __name__ == "__main__":
    raise SystemExit(main())
