#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xnapipeline_parity.md XNAPP-012: join the reflection inventory with the official
IntelliSense XML documentation that ships beside Microsoft.Xna.Framework.Content.Pipeline.dll.

The XML file is public API documentation (summaries, parameter descriptions, return values and
the exceptions the documentation declares). It is the only authority for "declared exceptions",
and it is also the cross-check that says which public types Microsoft documented and which the
metadata exposes without documentation -- both directions are reported, neither is hidden.

Usage: merge_xml_docs.py <content-pipeline-api.json> <Microsoft.Xna.Framework.Content.Pipeline.xml> <output.json>
"""
import json
import re
import sys
import xml.etree.ElementTree as ET


def text_of(node):
    if node is None:
        return None
    parts = []
    for chunk in node.itertext():
        parts.append(chunk)
    text = re.sub(r"\s+", " ", "".join(parts)).strip()
    return text or None


def cref(node):
    return node.get("cref") if node is not None else None


def load_docs(path):
    root = ET.parse(path).getroot()
    docs = {}
    for member in root.iter("member"):
        name = member.get("name")
        if not name:
            continue
        entry = {"summary": text_of(member.find("summary"))}
        params = {}
        for p in member.findall("param"):
            params[p.get("name")] = text_of(p)
        if params:
            entry["params"] = params
        returns = text_of(member.find("returns"))
        if returns:
            entry["returns"] = returns
        remarks = text_of(member.find("remarks"))
        if remarks:
            entry["remarks"] = remarks
        exceptions = []
        for e in member.findall("exception"):
            exceptions.append({"type": (cref(e) or "").replace("T:", ""), "when": text_of(e)})
        if exceptions:
            entry["exceptions"] = exceptions
        docs[name] = entry
    return docs


def clr_member_ids(type_entry):
    """Produce the doc IDs (T:/M:/P:/F:/E:) the XML file uses for this type's members.

    The XML ID grammar spells nested types with '.', generic arity with `N, parameter lists with
    CLR full names and generic parameters as `0/``0 indices. Only the parts needed to match the
    Content Pipeline file are implemented; anything unmatched is reported, not guessed."""
    ids = {}
    type_id = type_entry["fullName"].replace("+", ".")
    ids["T:" + type_id] = ("type", type_entry["displayName"])
    for m in type_entry.get("members", []):
        kind = m["kind"]
        if kind in ("property", "indexer"):
            ids["P:" + type_id + "." + m["name"]] = ("member", m["signature"])
        elif kind in ("field", "constant"):
            ids["F:" + type_id + "." + m["name"]] = ("member", m["signature"])
        elif kind == "event":
            ids["E:" + type_id + "." + m["name"]] = ("member", m["signature"])
        elif kind in ("method", "operator", "constructor"):
            name = "#ctor" if kind == "constructor" else m["name"]
            ids["M:" + type_id + "." + name] = ("member-prefix", m["signature"])
    return ids


def main(argv):
    if len(argv) != 4:
        sys.stderr.write(__doc__)
        return 2
    api = json.load(open(argv[1], encoding="utf-8"))
    docs = load_docs(argv[2])

    documented_types = sorted(k[2:] for k in docs if k.startswith("T:"))
    public_types = sorted(t["fullName"].replace("+", ".") for t in api["types"])
    documented_set = set(documented_types)
    public_set = set(public_types)

    per_type = []
    matched_members = 0
    unmatched_members = []
    for t in api["types"]:
        type_id = t["fullName"].replace("+", ".")
        entry = {"type": t["displayName"], "docId": "T:" + type_id}
        tdoc = docs.get("T:" + type_id)
        entry["documented"] = tdoc is not None
        if tdoc:
            entry["summary"] = tdoc.get("summary")
        members = []
        for m in t.get("members", []):
            kind = m["kind"]
            if kind in ("property", "indexer"):
                prefix = "P:" + type_id + "." + m["name"]
            elif kind in ("field", "constant"):
                prefix = "F:" + type_id + "." + m["name"]
            elif kind == "event":
                prefix = "E:" + type_id + "." + m["name"]
            else:
                name = "#ctor" if kind == "constructor" else m["name"]
                prefix = "M:" + type_id + "." + name
            candidates = [k for k in docs if k == prefix or k.startswith(prefix + "(") or k.startswith(prefix + "``")]
            if kind in ("property", "indexer", "field", "constant", "event"):
                candidates = [k for k in candidates if k == prefix or k.startswith(prefix + "(")]
            doc = None
            if len(candidates) == 1:
                doc = docs[candidates[0]]
            elif len(candidates) > 1:
                # Overloads: match by parameter count when the XML spells parameter lists.
                want = len(m.get("parameters", []))
                narrowed = []
                for k in candidates:
                    inner = k[k.find("(") + 1:-1] if "(" in k else ""
                    count = 0 if not inner else len(re.findall(r",(?![^{]*})", inner)) + 1
                    if count == want:
                        narrowed.append(k)
                if len(narrowed) == 1:
                    doc = docs[narrowed[0]]
                elif narrowed:
                    doc = {"ambiguous": narrowed}
            record = {"signature": m["signature"], "kind": kind, "documented": doc is not None}
            if doc:
                matched_members += 1
                for key in ("summary", "params", "returns", "remarks", "exceptions", "ambiguous"):
                    if key in doc:
                        record[key] = doc[key]
            else:
                unmatched_members.append(t["displayName"] + "::" + m["signature"])
            members.append(record)
        entry["members"] = members
        per_type.append(entry)

    out = {
        "schema": "cna.xna40.content-pipeline-api-docs/1",
        "generator": "tools/xna-pipeline-oracle/merge_xml_docs.py",
        "source": "Microsoft.Xna.Framework.Content.Pipeline.xml (IntelliSense documentation shipped with XNA Game Studio 4.0 Refresh)",
        "counts": {
            "documentedTypes": len(documented_types),
            "publicTypesInInventory": len(public_types),
            "publicTypesDocumented": len(public_set & documented_set),
            "publicTypesUndocumented": len(public_set - documented_set),
            "documentedTypesNotInInventory": len(documented_set - public_set),
            "membersDocumented": matched_members,
            "membersUndocumented": len(unmatched_members),
        },
        "publicTypesUndocumented": sorted(public_set - documented_set),
        "documentedTypesNotInInventory": sorted(documented_set - public_set),
        "membersUndocumented": sorted(unmatched_members),
        "types": per_type,
    }
    with open(argv[3], "w", encoding="utf-8") as f:
        json.dump(out, f, indent=1, sort_keys=False, ensure_ascii=True)
        f.write("\n")
    print(json.dumps(out["counts"], indent=1))
    print("undocumented public types:", out["publicTypesUndocumented"])
    print("documented types missing from inventory:", out["documentedTypesNotInInventory"])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
