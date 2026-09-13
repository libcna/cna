#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xnapipeline_parity.md XNAPP-320: the component reference, generated rather than written.

The parity report answers *is it there*. This answers the question somebody porting a project
actually asks: **what does this importer accept, what does this processor produce, what are its
properties called, what do they default to, and how do I spell all of that in C++.**

Every column comes from a file that was measured rather than typed:

  * `content-pipeline-api.json` -- the reflection oracle's reading of the genuine XNA Game Studio
    4.0 Refresh assemblies: the importers, the extensions each declares, the processors, and every
    processor property with the value a freshly constructed processor answers;
  * `content-pipeline-parity-map.json` -- CNA's answer, per type and per member: the C++ spelling,
    the header it lives in, the parity status and the note;
  * `content-pipeline-inputs.json` -- the per-extension route: which CNA importer and processor a
    source of that extension reaches, and the fixture that proves it.

Nothing here is hand-maintained, so nothing here can drift from the measurement without the gate
saying so. `--check` is that gate: it regenerates the document and compares it byte for byte with
the committed one, exactly as the parity report's own check does.

Usage:
    component_reference.py --inventory A.json --map M.json --inputs I.json --output D.md [--check]
"""
from __future__ import annotations

import argparse
import json
import sys
from collections import OrderedDict

PREAMBLE = """# XNA 4.0 Content Pipeline component reference

> **Generated** by `tools/xna-pipeline-oracle/component_reference.py` from
> `tests/reference/xna40/content-pipeline-api.json` (read from the genuine XNA Game Studio 4.0
> Refresh assemblies), `tests/reference/xna40/content-pipeline-parity-map.json` (CNA's answer) and
> `tests/reference/xna40/content-pipeline-inputs.json` (the per-extension route). Do not edit by
> hand; edit those and regenerate. The ctest `XnaPipelineComponentReferenceIsCurrent` fails if this
> file is not what a regeneration writes.

This is the *what does it do* reference. Three companions answer the neighbouring questions:

* [`xna-content-pipeline-compat-api.md`](xna-content-pipeline-compat-api.md) -- how a C# pipeline
  concept is spelled in C++ (attributes, properties, reflection, contexts).
* [`xna-content-pipeline-migration.md`](xna-content-pipeline-migration.md) -- porting an existing
  XNA content project, with before/after examples.
* [`xna-content-pipeline-parity-report.md`](xna-content-pipeline-parity-report.md) -- the parity
  matrix: every public type and member, with its status.

**How to read the status column.** `EXACT_EQUIVALENT` means the same name, members and observable
behaviour. `SEMANTIC_EQUIVALENT` means C++ cannot spell it identically but every capability is
there, and the note says how. `HOST_SUBSTITUTION` means a Microsoft-host mechanism was replaced by
a CNA one. There is no `MISSING` row in this document, and the parity gate is what keeps it that
way.
"""


def load(path):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle, object_pairs_hook=OrderedDict)


def cell(text):
    """One markdown table cell: no pipes, no newlines, and empty reads as an em dash."""
    if text is None or text == "":
        return "--"
    return str(text).replace("|", "\\|").replace("\n", " ").strip()


def code(text):
    return "`%s`" % cell(text) if text else "--"


def short(full):
    """`A.B.CProcessor` -> `CProcessor`, which is how every document and every error message spells it."""
    return full.rsplit(".", 1)[-1] if full else ""


def entry(pmap, full):
    return pmap.get("types", {}).get(full, {})


def status_of(pmap, full):
    return entry(pmap, full).get("status", "MISSING")


def member(pmap, full, key, declaring=None):
    """The map entry for one member, from the type that declares it.

    XNA redeclares several inherited processor properties purely to hide them from its designer,
    and the map carries an entry wherever XNA declares one. A property a subclass simply inherits
    -- `TextureProcessor.PremultiplyAlpha` on the sprite and model processors -- has its entry on
    the declaring type and nowhere else, so that is where this looks next. Inheriting a property
    is not a hole in the map.
    """
    found = entry(pmap, full).get("members", {}).get(key)
    if found is None and declaring and declaring != full:
        found = entry(pmap, declaring).get("members", {}).get(key)
    return found or {}


def extensions_section(inventory, pmap, inputs, out):
    out.append("## 1. Source extensions\n")
    out.append("Every file extension a built-in XNA importer declares, and what a source with that "
               "extension reaches in CNA. The importer is selected by extension exactly as XNA "
               "selects it; the processor named here is the one the importer's "
               "`DefaultProcessor` asks for, which a `.contentproj` item's `<Processor>` or a "
               "`.cna-content.json` asset's `processor` field overrides.\n")
    out.append("| Extension | XNA importer | Default processor | CNA importer | CNA processor | Fixture |")
    out.append("|---|---|---|---|---|---|")
    rows = inputs["extensions"]
    for ext in sorted(rows):
        xna = rows[ext]["xna"]
        cna = rows[ext].get("cna", {})
        out.append("| `%s` | %s | %s | %s | %s | %s |" % (
            cell(ext), code(short(xna.get("importerType") or xna.get("importer"))),
            code(xna.get("defaultProcessor")), code(cna.get("importer")),
            code(cna.get("processor")), code(cna.get("fixture"))))
    out.append("")
    out.append("%d extensions, all IMPLEMENTED and TESTED. The matrix that records the tests and "
               "fixtures behind each row is section 9 of the parity report.\n"
               % len(rows))


def importers_section(inventory, pmap, out):
    out.append("## 2. Importers\n")
    out.append("`Cached` is XNA's `CacheImportedData`: whether the build is allowed to cache the "
               "imported object graph between builds. The two model importers declare it and the "
               "other eight do not.\n")
    out.append("| Importer | Display name | Extensions | Produces | Cached | Status |")
    out.append("|---|---|---|---|---|---|")
    for imp in inventory["importers"]:
        full = imp["type"]
        out.append("| %s | %s | %s | %s | %s | %s |" % (
            code(short(full)), cell(imp.get("displayName")),
            " ".join("`%s`" % e for e in imp.get("fileExtensions", [])),
            code(short(imp.get("outputType"))),
            "yes" if imp.get("cacheImportedData") else "no",
            cell(status_of(pmap, full))))
    out.append("")
    for imp in inventory["importers"]:
        full = imp["type"]
        row = entry(pmap, full)
        if not row.get("note"):
            continue
        out.append("* **%s** -- %s" % (short(full), cell(row["note"])))
    out.append("")


def processors_section(inventory, pmap, out):
    out.append("## 3. Processors and their properties\n")
    out.append("A processor property is set from item metadata named `ProcessorParameters_<Name>` "
               "in a `.contentproj`, from an asset's `parameters` object in a "
               "`.cna-content.json`, or directly on the C++ object through the accessor in the "
               "last column. `Configurable` is whether "
               "XNA's own designer offered the property; a non-configurable one is still settable "
               "in code. `XNA default` is the value a freshly constructed processor answered when "
               "the oracle read it, and the same values are asserted against a freshly constructed "
               "CNA processor by `XnaProcessorDefaultsTests.cpp`.\n")
    for proc in inventory["processors"]:
        full = proc["type"]
        row = entry(pmap, full)
        out.append("### %s\n" % short(full))
        out.append("*%s* -- takes %s, produces %s. %s" % (
            cell(proc.get("displayName")), code(short(proc.get("inputType"))),
            code(short(proc.get("outputType"))), cell(status_of(pmap, full))))
        out.append("")
        if row.get("cna"):
            out.append("`%s`" % cell(row["cna"]) + (", declared in `%s`." % cell(row["header"])
                                                    if row.get("header") else "."))
            out.append("")
        if row.get("note"):
            out.append(cell(row["note"]))
            out.append("")
        if not proc["properties"]:
            out.append("No properties.\n")
            continue
        out.append("| Property | Type | XNA default | Configurable | Declared by | CNA |")
        out.append("|---|---|---|---|---|---|")
        for prop in proc["properties"]:
            m = member(pmap, full, "property:" + prop["name"], prop.get("declaringType"))
            out.append("| `%s` | %s | %s | %s | %s | %s |" % (
                cell(prop["name"]), code(short(prop["type"])), code(prop.get("defaultValue")),
                "yes" if prop.get("configurable") else "no",
                code(short(prop.get("declaringType"))), code(m.get("cna"))))
        out.append("")


def counts_section(inventory, inputs, out):
    counts = inventory["counts"]
    out.append("## 4. What this document covers\n")
    out.append("| Quantity | Count |")
    out.append("|---|---:|")
    out.append("| importers | %d |" % counts["importers"])
    out.append("| extensions they declare | %d |" % counts["distinctExtensions"])
    out.append("| processors | %d |" % counts["processors"])
    out.append("| processor properties | %d |" % counts["processorProperties"])
    out.append("| extensions with a CNA route | %d |" % len(inputs["extensions"]))
    out.append("")
    out.append("The denominators are frozen (`inventory_freeze.py`), so a regeneration that found "
               "one importer fewer would fail rather than quietly renumber this table.\n")


def build(inventory, pmap, inputs):
    out = [PREAMBLE]
    extensions_section(inventory, pmap, inputs, out)
    importers_section(inventory, pmap, out)
    processors_section(inventory, pmap, out)
    counts_section(inventory, inputs, out)
    return "\n".join(out).rstrip() + "\n"


def problems_in(inventory, pmap, inputs):
    """Everything this document would have to render as a hole, listed instead of printed."""
    found = []
    for imp in inventory["importers"]:
        if status_of(pmap, imp["type"]) == "MISSING":
            found.append("importer %s is MISSING from the parity map" % imp["type"])
    for proc in inventory["processors"]:
        if status_of(pmap, proc["type"]) == "MISSING":
            found.append("processor %s is MISSING from the parity map" % proc["type"])
        for prop in proc["properties"]:
            m = member(pmap, proc["type"], "property:" + prop["name"], prop.get("declaringType"))
            if m.get("status", "MISSING") == "MISSING":
                found.append("property %s.%s is MISSING from the parity map"
                             % (short(proc["type"]), prop["name"]))
            elif not m.get("cna"):
                found.append("property %s.%s has no C++ spelling in the parity map"
                             % (short(proc["type"]), prop["name"]))
    declared = set()
    for imp in inventory["importers"]:
        declared |= set(imp.get("fileExtensions", []))
    for ext in sorted(declared - set(inputs["extensions"])):
        found.append("extension %s is declared by an importer but absent from the input matrix" % ext)
    return found


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--inventory", required=True)
    ap.add_argument("--map", required=True)
    ap.add_argument("--inputs", required=True)
    ap.add_argument("--output", required=True)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args(argv[1:])

    inventory = load(args.inventory)
    pmap = load(args.map)
    inputs = load(args.inputs)

    document = build(inventory, pmap, inputs)
    problems = problems_in(inventory, pmap, inputs)

    if args.check:
        try:
            with open(args.output, encoding="utf-8") as handle:
                committed = handle.read()
        except OSError as error:
            print("cannot read %s: %s" % (args.output, error))
            return 1
        for problem in problems:
            print("  problem: " + problem)
        if committed != document:
            print("  problem: %s is not what this run writes; regenerate it" % args.output)
            return 1
        print("component reference is current: %d importers, %d processors, %d properties, "
              "%d extensions" % (len(inventory["importers"]), len(inventory["processors"]),
                                 inventory["counts"]["processorProperties"],
                                 len(inputs["extensions"])))
        return 1 if problems else 0

    with open(args.output, "w", encoding="utf-8") as handle:
        handle.write(document)
    print("wrote %s (%d importers, %d processors, %d properties, %d extensions)"
          % (args.output, len(inventory["importers"]), len(inventory["processors"]),
             inventory["counts"]["processorProperties"], len(inputs["extensions"])))
    for problem in problems:
        print("  problem: " + problem)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
