#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xnapipeline_parity.md XNAPP-014: the parity report and gate.

Joins the authoritative inventory (`content-pipeline-api.json`, written by the reflection oracle)
with CNA's hand-maintained answer (`content-pipeline-parity-map.json`) and, optionally, the
input-format matrix (`content-pipeline-inputs.json`), and writes the generated coverage report
`docs/xna-content-pipeline-parity-report.md`.

Every percentage in the plan comes from here. The denominator is the inventory; the map may not
add to it, and nothing in the inventory may be silently absent from the map.

Statuses (plan section 4):
  EXACT_EQUIVALENT     same name, members and observable behaviour
  SEMANTIC_EQUIVALENT  C++ cannot spell it identically; every capability exists (note says how)
  HOST_SUBSTITUTION    a Microsoft-host mechanism replaced by a CNA mechanism (note says how)
  EXTERNAL_BLOCKED     needs a legally/physically unavailable component (note names it)
  MISSING              not there -- zero allowed at completion

Two mechanical rules the report states rather than applies silently:
  * delegate types count as one item, their Invoke signature; the CLR plumbing (.ctor(Object,
    IntPtr), BeginInvoke, EndInvoke) is listed as CLR_PLUMBING and not counted;
  * the two exception types' System.Runtime.Serialization members are HOST_SUBSTITUTION by rule.

Usage:
  parity_report.py --inventory A.json --map M.json [--inputs I.json] --output report.md [--gate] [--init]
    --init  adds every inventory type/member missing from the map as MISSING (never overwrites)
    --gate  exit 1 on any MISSING, unknown map entry, or status without its required annotation
    --check verify the committed report is exactly what this run writes and that the map has no
            gate problem, without writing anything and without failing on MISSING -- MISSING is
            what the report exists to publish. This is the ctest: it makes the report a
            regenerated artefact rather than a document somebody edits, so a status that changes
            without the report changing cannot happen.
"""
import argparse
import json
import sys
from collections import OrderedDict

IMPLEMENTED = ("EXACT_EQUIVALENT", "SEMANTIC_EQUIVALENT", "HOST_SUBSTITUTION")
ALL_STATUSES = IMPLEMENTED + ("EXTERNAL_BLOCKED", "MISSING")
NEEDS_NOTE = ("SEMANTIC_EQUIVALENT", "HOST_SUBSTITUTION", "EXTERNAL_BLOCKED")
NEEDS_CNA = ("EXACT_EQUIVALENT", "SEMANTIC_EQUIVALENT")

SERIALIZATION_PLUMBING = (
    "GetObjectData(System.Runtime.Serialization.SerializationInfo, System.Runtime.Serialization.StreamingContext)",
    ".ctor(System.Runtime.Serialization.SerializationInfo, System.Runtime.Serialization.StreamingContext)",
)
SERIALIZATION_NOTE = (".NET binary serialization of exceptions has no C++ counterpart; the exception's "
                      "developer-visible contract (message, ContentIdentity, inner exception) is provided in full.")


def member_key(m):
    return m["kind"] + ":" + m["signature"]


def is_delegate_plumbing(m):
    if m["kind"] == "constructor" and m["signature"] == ".ctor(System.Object, System.IntPtr)":
        return True
    return m["kind"] == "method" and (m["signature"].startswith("BeginInvoke(") or m["signature"].startswith("EndInvoke("))


def load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f, object_pairs_hook=OrderedDict)


def save(path, data):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1, ensure_ascii=True)
        f.write("\n")


def effective_members(t):
    """Members that count, with their automatic statuses (or None when the map decides)."""
    out = []
    for m in t.get("members", []):
        auto = None
        if t["kind"] == "delegate":
            if is_delegate_plumbing(m):
                auto = ("CLR_PLUMBING", "delegate plumbing; the type maps to one C++ callable whose shape is Invoke")
            # Invoke itself is decided by the map.
        elif t["kind"] in ("class", "struct") and m["signature"] in SERIALIZATION_PLUMBING:
            auto = ("HOST_SUBSTITUTION", SERIALIZATION_NOTE)
        out.append((m, auto))
    return out


def init_map(inventory, pmap):
    types = pmap.setdefault("types", OrderedDict())
    added_types = added_members = 0
    for t in inventory["types"]:
        entry = types.get(t["fullName"])
        if entry is None:
            entry = OrderedDict([("status", "MISSING"), ("cna", ""), ("header", ""), ("note", ""), ("members", OrderedDict())])
            types[t["fullName"]] = entry
            added_types += 1
        members = entry.setdefault("members", OrderedDict())
        for m, auto in effective_members(t):
            if auto is not None:
                continue
            k = member_key(m)
            if k not in members:
                members[k] = OrderedDict([("status", "MISSING"), ("cna", ""), ("note", "")])
                added_members += 1
        if t["kind"] == "enum":
            values = entry.setdefault("enumValues", OrderedDict())
            for v in t["values"]:
                if v["name"] not in values:
                    values[v["name"]] = OrderedDict([("status", "MISSING"), ("cna", "")])
                    added_members += 1
    # Deterministic order: inventory order for types (already sorted by fullName).
    ordered = OrderedDict()
    for t in inventory["types"]:
        ordered[t["fullName"]] = types[t["fullName"]]
    for k in types:
        if k not in ordered:
            ordered[k] = types[k]
    pmap["types"] = ordered
    return added_types, added_members


def check_annotation(status, entry, where, problems):
    if status not in ALL_STATUSES:
        problems.append("%s: unknown status %r" % (where, status))
        return
    if status in NEEDS_CNA and not entry.get("cna"):
        problems.append("%s: status %s requires a `cna` spelling" % (where, status))
    if status in NEEDS_NOTE and not entry.get("note"):
        problems.append("%s: status %s requires a `note` explaining the substitution/blocker" % (where, status))


def build_report(inventory, pmap, inputs):
    problems = []
    types_map = pmap.get("types", OrderedDict())
    inv_types = OrderedDict((t["fullName"], t) for t in inventory["types"])

    for name in types_map:
        if name not in inv_types:
            problems.append("map names a type the inventory does not have: " + name)

    type_rows = []
    member_rows = []
    type_counts = OrderedDict((s, 0) for s in ALL_STATUSES)
    member_counts = OrderedDict((s, 0) for s in ALL_STATUSES)
    plumbing = []
    enum_value_counts = OrderedDict((s, 0) for s in ALL_STATUSES)
    per_namespace = OrderedDict()

    for full, t in inv_types.items():
        entry = types_map.get(full)
        status = entry["status"] if entry else "MISSING"
        if entry:
            check_annotation(status, entry, "type " + full, problems)
        else:
            problems.append("inventory type absent from map: " + full)
        type_counts[status] = type_counts.get(status, 0) + 1
        ns = t["namespace"]
        nsrec = per_namespace.setdefault(ns, OrderedDict((s, 0) for s in ALL_STATUSES))
        nsrec[status] = nsrec.get(status, 0) + 1
        type_rows.append((ns, t["displayName"], t["kind"], status, (entry or {}).get("cna", ""), (entry or {}).get("note", "")))

        mapped_members = (entry or {}).get("members", {})
        known_keys = set()
        counted = 0
        implemented = 0
        for m, auto in effective_members(t):
            k = member_key(m)
            known_keys.add(k)
            if auto is not None:
                mstatus, mnote = auto
                if mstatus == "CLR_PLUMBING":
                    plumbing.append((t["displayName"], m["signature"], mnote))
                    continue
                mcna = ""
            else:
                me = mapped_members.get(k)
                if me is None:
                    mstatus, mcna, mnote = "MISSING", "", ""
                    if entry is not None:
                        problems.append("member absent from map: %s :: %s" % (full, k))
                else:
                    mstatus, mcna, mnote = me.get("status", "MISSING"), me.get("cna", ""), me.get("note", "")
                    check_annotation(mstatus, me, "member %s :: %s" % (full, k), problems)
            member_counts[mstatus] = member_counts.get(mstatus, 0) + 1
            counted += 1
            if mstatus in IMPLEMENTED:
                implemented += 1
            member_rows.append((t["displayName"], m["kind"], m["signature"], mstatus, mcna, mnote))
        for k in mapped_members:
            if k not in known_keys:
                problems.append("map names a member the inventory does not have: %s :: %s" % (full, k))
        if t["kind"] == "enum":
            values_map = (entry or {}).get("enumValues", {})
            for v in t["values"]:
                ve = values_map.get(v["name"])
                vstatus = ve.get("status", "MISSING") if ve else "MISSING"
                if ve is None and entry is not None:
                    problems.append("enum value absent from map: %s.%s" % (full, v["name"]))
                elif ve is not None:
                    check_annotation(vstatus, ve, "enum value %s.%s" % (full, v["name"]), problems)
                enum_value_counts[vstatus] = enum_value_counts.get(vstatus, 0) + 1
                member_rows.append((t["displayName"], "enum value", "%s = %d" % (v["name"], v["value"]), vstatus, (ve or {}).get("cna", ""), (ve or {}).get("note", "")))
            for name in values_map:
                if name not in {v["name"] for v in t["values"]}:
                    problems.append("map names an enum value the inventory does not have: %s.%s" % (full, name))

    # Importers / processors / properties from the inventory, statuses from the type/member map.
    importer_rows = []
    for imp in inventory["importers"]:
        full = next((t["fullName"] for t in inventory["types"] if t["displayName"] == imp["type"]), imp["type"])
        e = types_map.get(full, {})
        importer_rows.append((imp["type"].split(".")[-1], ", ".join(imp["fileExtensions"]), imp["defaultProcessor"] or "-", e.get("status", "MISSING"), e.get("cna", ""), e.get("note", "")))
    processor_rows = []
    property_rows = []
    for p in inventory["processors"]:
        full = next((t["fullName"] for t in inventory["types"] if t["displayName"] == p["type"]), p["type"])
        e = types_map.get(full, {})
        processor_rows.append((p["type"].split(".")[-1], (p["inputType"] or "").split(".")[-1], (p["outputType"] or "").split(".")[-1], len(p["properties"]), e.get("status", "MISSING"), e.get("cna", "")))
        for prop in p["properties"]:
            # The property is declared on some type in the inventory (possibly a base); find its map entry there.
            decl = next((t for t in inventory["types"] if t["displayName"] == prop["declaringType"]), None)
            pstatus, pcna = "MISSING", ""
            if decl is not None:
                de = types_map.get(decl["fullName"], {}).get("members", {})
                me = de.get("property:" + prop["name"])
                if me:
                    pstatus, pcna = me.get("status", "MISSING"), me.get("cna", "")
            property_rows.append((p["type"].split(".")[-1], prop["name"], prop["type"].split(".")[-1], str(prop.get("defaultValue", prop.get("defaultValueError", ""))), pstatus, pcna))

    def pct(num, den):
        return "%d/%d (%.1f%%)" % (num, den, (100.0 * num / den) if den else 0.0)

    def implemented_count(counts):
        return sum(counts.get(s, 0) for s in IMPLEMENTED)

    total_types = len(inv_types)
    total_members = sum(member_counts.values())
    total_values = sum(enum_value_counts.values())
    importer_impl = sum(1 for r in importer_rows if r[3] in IMPLEMENTED)
    processor_impl = sum(1 for r in processor_rows if r[4] in IMPLEMENTED)
    property_impl = sum(1 for r in property_rows if r[4] in IMPLEMENTED)

    lines = []
    w = lines.append
    w("# XNA 4.0 Content Pipeline parity report")
    w("")
    w("> **Generated** by `tools/xna-pipeline-oracle/parity_report.py` from")
    w("> `tests/reference/xna40/content-pipeline-api.json` (the denominator, read from the genuine")
    w("> XNA Game Studio 4.0 Refresh assemblies) and `tests/reference/xna40/content-pipeline-parity-map.json`")
    w("> (CNA's answer). Do not edit by hand; edit the map and regenerate. Task log and decisions:")
    w("> `plans/plan_xnapipeline_parity.md`.")
    w("")
    w("## 1. Coverage summary")
    w("")
    w("| Quantity | Implemented (EXACT + SEMANTIC + HOST_SUBSTITUTION) | EXTERNAL_BLOCKED | MISSING |")
    w("|---|---|---:|---:|")
    w("| public/protected types | %s | %d | %d |" % (pct(implemented_count(type_counts), total_types), type_counts["EXTERNAL_BLOCKED"], type_counts["MISSING"]))
    w("| public/protected members | %s | %d | %d |" % (pct(implemented_count(member_counts), total_members), member_counts["EXTERNAL_BLOCKED"], member_counts["MISSING"]))
    w("| enum values | %s | %d | %d |" % (pct(implemented_count(enum_value_counts), total_values), enum_value_counts["EXTERNAL_BLOCKED"], enum_value_counts["MISSING"]))
    w("| built-in importers | %s | %d | %d |" % (pct(importer_impl, len(importer_rows)), sum(1 for r in importer_rows if r[3] == "EXTERNAL_BLOCKED"), sum(1 for r in importer_rows if r[3] == "MISSING")))
    w("| built-in processors | %s | %d | %d |" % (pct(processor_impl, len(processor_rows)), sum(1 for r in processor_rows if r[4] == "EXTERNAL_BLOCKED"), sum(1 for r in processor_rows if r[4] == "MISSING")))
    w("| processor properties | %s | %d | %d |" % (pct(property_impl, len(property_rows)), sum(1 for r in property_rows if r[4] == "EXTERNAL_BLOCKED"), sum(1 for r in property_rows if r[4] == "MISSING")))
    if inputs is not None:
        ext = inputs.get("extensions", OrderedDict())
        done = sum(1 for e in ext.values() if e.get("status") == "IMPLEMENTED+TESTED")
        blocked = sum(1 for e in ext.values() if e.get("status") == "EXTERNAL_BLOCKED")
        w("| source extensions IMPLEMENTED+TESTED | %s | %d | %d |" % (pct(done, len(ext)), blocked, len(ext) - done - blocked))
    w("")
    w("Status vocabulary: EXACT_EQUIVALENT, SEMANTIC_EQUIVALENT (spelling differs, capability identical; note says how),")
    w("HOST_SUBSTITUTION (Microsoft-host mechanism replaced; note says how), EXTERNAL_BLOCKED (note names the")
    w("unavailable component), MISSING. Type status by value: " + ", ".join("%s %d" % (s, type_counts[s]) for s in ALL_STATUSES) + ".")
    w("Member status by value: " + ", ".join("%s %d" % (s, member_counts[s]) for s in ALL_STATUSES) + ".")
    w("")
    w("Rules applied mechanically: %d delegate plumbing members are listed in section 7 and not counted; %d exception" % (len(plumbing), sum(1 for r in member_rows if r[3] == "HOST_SUBSTITUTION" and r[5] == SERIALIZATION_NOTE)))
    w("serialization members are HOST_SUBSTITUTION by rule (System.Runtime.Serialization has no C++ counterpart).")
    w("")
    w("## 2. Types by namespace")
    w("")
    w("| Namespace | Types | Implemented | Blocked | Missing |")
    w("|---|---:|---:|---:|---:|")
    for ns, rec in per_namespace.items():
        w("| `%s` | %d | %d | %d | %d |" % (ns, sum(rec.values()), implemented_count(rec), rec["EXTERNAL_BLOCKED"], rec["MISSING"]))
    w("")
    w("## 3. Type matrix")
    w("")
    w("| Namespace | XNA type | Kind | Status | CNA type | Note |")
    w("|---|---|---|---|---|---|")
    for ns, disp, kind, status, cna, note in type_rows:
        w("| `%s` | `%s` | %s | %s | %s | %s |" % (ns.replace("Microsoft.Xna.Framework.Content.Pipeline", "…"), disp.split(".")[-1] if "<" not in disp else disp.rsplit(".", 1)[-1], kind, status, ("`%s`" % cna) if cna else "", note.replace("|", "\\|")))
    w("")
    w("## 4. Importers")
    w("")
    w("| XNA importer | Extensions | Default processor | Status | CNA type | Note |")
    w("|---|---|---|---|---|---|")
    for r in importer_rows:
        w("| `%s` | %s | `%s` | %s | %s | %s |" % (r[0], r[1], r[2], r[3], ("`%s`" % r[4]) if r[4] else "", r[5].replace("|", "\\|")))
    w("")
    w("## 5. Processors and properties")
    w("")
    w("| XNA processor | Input | Output | Properties | Status | CNA type |")
    w("|---|---|---|---:|---|---|")
    for r in processor_rows:
        w("| `%s` | `%s` | `%s` | %d | %s | %s |" % (r[0], r[1], r[2], r[3], r[4], ("`%s`" % r[5]) if r[5] else ""))
    w("")
    w("| Processor | Property | Type | XNA default (black-box) | Status | CNA |")
    w("|---|---|---|---|---|---|")
    for r in property_rows:
        w("| `%s` | `%s` | `%s` | `%s` | %s | %s |" % (r[0], r[1], r[2], r[3], r[4], ("`%s`" % r[5]) if r[5] else ""))
    w("")
    if inputs is not None:
        w("## 6. Source extensions")
        w("")
        w("| Extension | XNA importer | CNA importer | Processor | Windows | Phone | Xbox | Tests | Status | Note |")
        w("|---|---|---|---|---|---|---|---|---|---|")
        for extname, e in inputs.get("extensions", {}).items():
            tests = e.get("tests", {})
            w("| `%s` | `%s` | %s | `%s` | %s | %s | %s | %d/%d | %s | %s |" % (
                extname, e.get("xnaImporter", ""), ("`%s`" % e["cnaImporter"]) if e.get("cnaImporter") else "",
                e.get("processor", ""), e.get("windows", ""), e.get("windowsPhone", ""), e.get("xbox360", ""),
                sum(1 for v in tests.values() if v), len(tests), e.get("status", "MISSING"), e.get("note", "").replace("|", "\\|")))
        w("")
    w("## 7. Members")
    w("")
    w("| XNA type | Kind | Signature | Status | CNA | Note |")
    w("|---|---|---|---|---|---|")
    for disp, kind, sig, status, cna, note in member_rows:
        w("| `%s` | %s | `%s` | %s | %s | %s |" % (disp.rsplit(".", 1)[-1], kind, sig.replace("|", "\\|"), status, ("`%s`" % cna) if cna else "", note.replace("|", "\\|")))
    w("")
    w("### 7.1 CLR plumbing not counted")
    w("")
    w("| XNA type | Signature | Why |")
    w("|---|---|---|")
    for disp, sig, why in plumbing:
        w("| `%s` | `%s` | %s |" % (disp.rsplit(".", 1)[-1], sig, why))
    w("")
    if problems:
        w("## 8. Gate problems")
        w("")
        for p in problems:
            w("- " + p)
        w("")
    summary = OrderedDict([
        ("types", (implemented_count(type_counts), total_types)),
        ("members", (implemented_count(member_counts), total_members)),
        ("enumValues", (implemented_count(enum_value_counts), total_values)),
        ("importers", (importer_impl, len(importer_rows))),
        ("processors", (processor_impl, len(processor_rows))),
        ("properties", (property_impl, len(property_rows))),
        ("missingTypes", type_counts["MISSING"]),
        ("missingMembers", member_counts["MISSING"]),
        ("missingEnumValues", enum_value_counts["MISSING"]),
    ])
    while lines and lines[-1] == "":
        lines.pop()
    return "\n".join(lines) + "\n", problems, summary


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--inventory", required=True)
    ap.add_argument("--map", required=True)
    ap.add_argument("--inputs")
    ap.add_argument("--output", required=True)
    ap.add_argument("--gate", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--init", action="store_true")
    ap.add_argument("--summary-json", help="also write the summary counts as JSON")
    args = ap.parse_args(argv[1:])

    inventory = load(args.inventory)
    try:
        pmap = load(args.map)
    except FileNotFoundError:
        pmap = OrderedDict([("schema", "cna.xna40.content-pipeline-parity-map/1"), ("types", OrderedDict())])
    inputs = load(args.inputs) if args.inputs else None

    if args.init:
        added_types, added_members = init_map(inventory, pmap)
        save(args.map, pmap)
        print("init: added %d types and %d members/values as MISSING" % (added_types, added_members))

    report, problems, summary = build_report(inventory, pmap, inputs)
    if args.check:
        try:
            with open(args.output, encoding="utf-8") as f:
                committed = f.read()
        except OSError as error:
            print("cannot read %s: %s" % (args.output, error))
            return 1
        for key, value in summary.items():
            print("%s: %s" % (key, "%d/%d" % value if isinstance(value, tuple) else value))
        for problem in problems:
            print("  problem: " + problem)
        if committed != report:
            print("  problem: %s is not what this run writes; regenerate it" % args.output)
        return 1 if problems or committed != report else 0
    with open(args.output, "w", encoding="utf-8") as f:
        f.write(report)
    if args.summary_json:
        save(args.summary_json, summary)
    for key, value in summary.items():
        print("%s: %s" % (key, "%d/%d" % value if isinstance(value, tuple) else value))
    if problems:
        print("%d gate problem(s); first: %s" % (len(problems), problems[0]))
    if args.gate and (problems or summary["missingTypes"] or summary["missingMembers"] or summary["missingEnumValues"]):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
