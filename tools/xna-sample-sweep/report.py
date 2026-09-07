#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-041: what the sweep found, grouped so it can be acted on.

Joins the sweep's per-file results with the source mapping, so every row carries the source
extension, the importer and processor the project named, and the root reader the reference
declares. Prints the aggregate the plan reports and the breakdowns that say where to look next.

Usage:
    report.py --map <map.json> --results <results.json> [--json <out.json>] [--group ext|processor|reader|sample]
"""
from __future__ import annotations

import argparse
import collections
import json
import os


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--map", required=True)
    parser.add_argument("--results", required=True)
    parser.add_argument("--json", default=None)
    parser.add_argument("--top", type=int, default=25)
    args = parser.parse_args(argv)

    with open(args.map, encoding="utf-8") as handle:
        mapping = json.load(handle)
    with open(args.results, encoding="utf-8") as handle:
        results = json.load(handle)

    by_reference = {m["reference"]: m for m in mapping["mappings"]}
    units = {u["outputRoot"]: u for u in mapping["buildUnits"]}

    rows = []
    for unit in results["results"]:
        # A root holding far fewer references than its project has items is a *selective* build --
        # a diagnostic variant that kept one asset -- so what CNA produced beyond it is not a
        # finding about CNA. Counted separately rather than reported as a difference.
        declared = units.get(unit["outputRoot"], {}).get("items", 0)
        references = units.get(unit["outputRoot"], {}).get("referencesUnderRoot", 0)
        selective = declared > 0 and references * 2 < declared
        for row in unit["rows"]:
            entry = {
                "sample": unit["sample"],
                "outputRoot": unit["outputRoot"],
                "result": row["result"],
                "asset": row["asset"],
                "reference": row.get("reference"),
                "rootReader": (row.get("rootReader") or "").rsplit(".", 1)[-1] or None,
                "platform": unit["platform"],
                "hiDef": unit["hiDef"],
                "compressed": unit["compressed"],
                "selectiveRoot": selective,
                "firstDifference": row.get("firstDifference"),
                "differingBytes": row.get("differingBytes"),
                "referenceSize": row.get("referenceSize"),
                "cnaSize": row.get("cnaSize"),
            }
            source = by_reference.get(row.get("reference") or "", {})
            entry["source"] = source.get("source")
            entry["extension"] = (os.path.splitext(source["source"])[1].lower()
                                  if source.get("source") else None)
            entry["importer"] = source.get("importer")
            entry["processor"] = source.get("processor")
            entry["mappingStatus"] = source.get("status")
            rows.append(entry)

    counted = [r for r in rows if not (r["result"] == "extra" and r["selectiveRoot"])]
    totals = collections.Counter(r["result"] for r in counted)
    print("=== totals (%d rows, %d suppressed as a selective root's extras) ==="
          % (len(counted), len(rows) - len(counted)))
    for key in ("identical", "differs", "missing", "extra"):
        print("  %-10s %6d" % (key, totals.get(key, 0)))

    def table(title, key, statuses=("identical", "differs", "missing")):
        print("\n=== %s ===" % title)
        buckets = collections.defaultdict(collections.Counter)
        for row in counted:
            if row["result"] not in statuses:
                continue
            buckets[row[key]][row["result"]] += 1
        ordered = sorted(buckets.items(), key=lambda kv: -sum(kv[1].values()))
        print("  %-34s %8s %8s %8s   %s" % (key, "ident", "differ", "missing", "identical %"))
        for name, counts in ordered[:args.top]:
            total = sum(counts.values())
            print("  %-34s %8d %8d %8d   %5.1f%%" % (
                str(name)[:34], counts["identical"], counts["differs"], counts["missing"],
                100.0 * counts["identical"] / total if total else 0.0))

    table("by source extension", "extension")
    table("by processor", "processor")
    table("by reference root reader", "rootReader")
    table("by target platform", "platform")

    print("\n=== samples with the most differing or missing ===")
    worst = collections.Counter()
    for row in counted:
        if row["result"] in ("differs", "missing"):
            worst[row["sample"]] += 1
    for name, count in worst.most_common(args.top):
        print("  %-52s %5d" % (name, count))

    if args.json:
        os.makedirs(os.path.dirname(os.path.abspath(args.json)), exist_ok=True)
        with open(args.json, "w", encoding="utf-8") as handle:
            json.dump({"totals": dict(totals), "rows": rows}, handle, indent=1, sort_keys=True)
            handle.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
