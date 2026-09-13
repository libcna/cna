#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xnapipeline_parity.md XNAPP-014: apply a parity decision to the map.

The map (`tests/reference/xna40/content-pipeline-parity-map.json`) is the hand-maintained record of
CNA's answer per XNA type and member. Editing 700 JSON entries by hand invites typos, so this tool
applies one *decision document* -- a small JSON file -- to the map, spelling the CNA member names
mechanically from the XNA ones unless the decision overrides them:

    property `Foo`            -> `getFooProperty()` / `setFooProperty()` (as the accessors exist)
    indexer  `Item[K]`        -> `operator[]`
    method   `Bar(...)`       -> `Bar(...)`
    constructor `.ctor(...)`  -> `<TypeName>(...)`
    field/constant `Baz`      -> `Baz`
    enum value `V`            -> `<TypeName>::V`

Decision document shape:

    {
      "Microsoft.Xna.Framework.Content.Pipeline.ContentIdentity": {
        "status": "EXACT_EQUIVALENT",
        "cna": "Microsoft::Xna::Framework::Content::Pipeline::ContentIdentity",
        "header": "modules/content-pipeline/include/Microsoft/Xna/Framework/Content/Pipeline/ContentIdentity.hpp",
        "note": "",
        "members": {
          "*": {"status": "EXACT_EQUIVALENT"},
          "constructor:.ctor()": {"status": "EXACT_EQUIVALENT", "cna": "ContentIdentity()"},
          "method:Foo(System.String)": {"status": "SEMANTIC_EQUIVALENT", "note": "why"}
        },
        "enumValues": {"*": {"status": "EXACT_EQUIVALENT"}}
      }
    }

`"*"` applies to every member (or enum value) the decision does not name explicitly. A member the
decision names but the inventory lacks is an error, so a typo cannot create phantom coverage.

Usage: parity_map_edit.py --inventory A.json --map M.json --apply decision.json
"""
import argparse
import json
import re
import sys
from collections import OrderedDict

STATUSES = ("EXACT_EQUIVALENT", "SEMANTIC_EQUIVALENT", "HOST_SUBSTITUTION", "EXTERNAL_BLOCKED", "MISSING")


def load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f, object_pairs_hook=OrderedDict)


def save(path, data):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1, ensure_ascii=True)
        f.write("\n")


def short_type_name(display_name):
    name = display_name.rsplit(".", 1)[-1]
    if "<" in name:
        name = name.split("<", 1)[0]
    return name


def auto_cna(type_entry, member):
    kind = member["kind"]
    if kind == "property":
        parts = []
        if member.get("get"):
            parts.append("get%sProperty()" % member["name"])
        if member.get("set"):
            parts.append("set%sProperty()" % member["name"])
        return " / ".join(parts)
    if kind == "indexer":
        return "operator[]"
    if kind == "constructor":
        return short_type_name(type_entry["displayName"]) + member["signature"][len(".ctor"):]
    if kind in ("method", "operator", "field", "constant", "event"):
        return member["signature"]
    return member["name"]


def apply(inventory, pmap, decisions):
    inv = OrderedDict((t["fullName"], t) for t in inventory["types"])
    problems = []
    for full, decision in decisions.items():
        t = inv.get(full)
        if t is None:
            problems.append("decision names a type the inventory lacks: " + full)
            continue
        entry = pmap["types"].setdefault(full, OrderedDict([("status", "MISSING"), ("cna", ""), ("header", ""), ("note", ""), ("members", OrderedDict())]))
        for key in ("status", "cna", "header", "note"):
            if key in decision:
                entry[key] = decision[key]
        if entry["status"] not in STATUSES:
            problems.append("%s: unknown status %s" % (full, entry["status"]))
        members_decision = decision.get("members", {})
        wildcard = members_decision.get("*")
        members = entry.setdefault("members", OrderedDict())
        inventory_keys = set()
        for m in t.get("members", []):
            key = m["kind"] + ":" + m["signature"]
            inventory_keys.add(key)
            specific = members_decision.get(key)
            chosen = specific if specific is not None else wildcard
            if chosen is None:
                continue
            record = members.setdefault(key, OrderedDict([("status", "MISSING"), ("cna", ""), ("note", "")]))
            record["status"] = chosen.get("status", record["status"])
            if "cna" in chosen:
                record["cna"] = chosen["cna"]
            elif not record.get("cna") and record["status"] in ("EXACT_EQUIVALENT", "SEMANTIC_EQUIVALENT"):
                record["cna"] = auto_cna(t, m)
            if "note" in chosen:
                record["note"] = chosen["note"]
            elif specific is None and wildcard is not None and "note" in wildcard:
                record["note"] = wildcard["note"]
        for key in members_decision:
            if key != "*" and key not in inventory_keys:
                problems.append("%s: decision names a member the inventory lacks: %s" % (full, key))
        if t["kind"] == "enum":
            values_decision = decision.get("enumValues", {})
            vwild = values_decision.get("*")
            values = entry.setdefault("enumValues", OrderedDict())
            for v in t["values"]:
                chosen = values_decision.get(v["name"], vwild)
                if chosen is None:
                    continue
                record = values.setdefault(v["name"], OrderedDict([("status", "MISSING"), ("cna", "")]))
                record["status"] = chosen.get("status", record["status"])
                record["cna"] = chosen.get("cna", record.get("cna") or (short_type_name(t["displayName"]) + "::" + v["name"]))
                if "note" in chosen:
                    record["note"] = chosen["note"]
            for name in values_decision:
                if name != "*" and name not in {v["name"] for v in t["values"]}:
                    problems.append("%s: decision names an enum value the inventory lacks: %s" % (full, name))
    return problems


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--inventory", required=True)
    ap.add_argument("--map", required=True)
    ap.add_argument("--apply", required=True, help="decision document (JSON)")
    args = ap.parse_args(argv[1:])
    inventory = load(args.inventory)
    pmap = load(args.map)
    decisions = load(args.apply)
    problems = apply(inventory, pmap, decisions)
    if problems:
        for p in problems:
            print("error: " + p, file=sys.stderr)
        return 1
    save(args.map, pmap)
    print("applied %d type decision(s)" % len(decisions))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
