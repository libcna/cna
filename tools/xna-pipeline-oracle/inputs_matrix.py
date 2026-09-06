#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xnapipeline_parity.md XNAPP-021: the source-extension parity matrix and its gate.

The input-parity denominator is not a list anybody types. It is
`content-pipeline-api.json`'s `extensionIndex`, read from the `ContentImporterAttribute` metadata
of the genuine XNA Game Studio 4.0 assemblies. This tool keeps
`tests/reference/xna40/content-pipeline-inputs.json` joined to it, so an extension cannot leave
the denominator because nobody remembered to list it.

Each extension's entry has two halves:

  "xna"  regenerated from the inventory on every `sync`, never edited: the importer type and its
         assembly, the declared display name, the default processor, cacheImportedData, and the
         importer's output type.
  "cna"  CNA's answer, hand-maintained: the C++ importer, the processor CNA routes it to, the
         fixture, the six tests the plan requires, per-target applicability, a status word and a
         note. `sync` adds a MISSING skeleton for an extension the file lacks and never
         overwrites an answer that is already there.

Subcommands:
  sync   rewrite every "xna" half from the inventory; add skeletons for new extensions
  check  fail if an extension is absent, an answer names an extension the inventory lacks, a
         status is outside the vocabulary, a named test does not exist in the tree, a named
         fixture does not exist on disk, or the file is not what `sync` would write

Test names are checked by finding `TEST(Suite, Name)` or `TEST_F(Suite, Name)` in the tree, which
is what makes "the test exists" mechanical rather than a claim in a table.
"""
import argparse
import json
import os
import re
import sys
from collections import OrderedDict

# The six roles the plan requires per extension, plus the differential evidence row. A role whose
# value is the empty string is "not done yet" and counts against the extension.
TEST_ROLES = ("importer", "processor", "sourceToXnb", "sourceToCnb", "malformed", "target")

STATUSES = (
    "IMPLEMENTED+TESTED",   # importer, fixture and all six tests present
    "IMPLEMENTED",          # the route exists, some required test does not
    "EXTERNAL_BLOCKED",     # note names the component that is unavailable
    "MISSING",              # no route
)

TARGET_STATES = ("SUPPORTED", "UNSUPPORTED", "UNVERIFIED", "N/A")

TEST_PATTERN = re.compile(r"^\s*TEST(?:_F|_P)?\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)",
                          re.MULTILINE)


def load(path):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle, object_pairs_hook=OrderedDict)


def dump(path, document):
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(document, handle, indent=1, ensure_ascii=False)
        handle.write("\n")


def importers_by_type(inventory):
    return OrderedDict((i["type"], i) for i in inventory["importers"])


def xna_half(inventory, extension):
    """The half of an extension's row that belongs to Microsoft, read from the inventory."""
    owners = inventory["extensionIndex"][extension]
    if len(owners) != 1:
        raise SystemExit("inputs_matrix: %s is claimed by %d importers; the matrix assumes one"
                         % (extension, len(owners)))
    importer = importers_by_type(inventory)[owners[0]]
    return OrderedDict([
        ("importer", importer["type"].rsplit(".", 1)[-1]),
        ("importerType", importer["type"]),
        ("assembly", importer["assembly"]),
        ("displayName", importer["displayName"]),
        ("defaultProcessor", importer["defaultProcessor"]),
        ("cacheImportedData", importer["cacheImportedData"]),
        ("outputType", importer["outputType"]),
    ])


def skeleton_cna():
    return OrderedDict([
        ("importer", ""),
        ("processor", ""),
        ("fixture", ""),
        ("tests", OrderedDict((role, "") for role in TEST_ROLES)),
        ("differentialEvidence", ""),
        ("windows", "UNVERIFIED"),
        ("windowsPhone", "UNVERIFIED"),
        ("xbox360", "UNVERIFIED"),
        ("status", "MISSING"),
        ("note", ""),
    ])


def sync(inventory, existing):
    document = OrderedDict()
    document["schema"] = "cna.xna40.content-pipeline-inputs/1"
    document["generator"] = ("tools/xna-pipeline-oracle/inputs_matrix.py; the xna half of every "
                             "entry is regenerated from tests/reference/xna40/content-pipeline-api.json "
                             "and must not be edited by hand")
    document["denominator"] = OrderedDict([
        ("source", "ContentImporterAttribute metadata of the XNA Game Studio 4.0 Refresh assemblies"),
        ("extensions", len(inventory["extensionIndex"])),
        ("importers", len(inventory["importers"])),
    ])
    previous = existing.get("extensions", OrderedDict()) if existing else OrderedDict()
    extensions = OrderedDict()
    for extension in sorted(inventory["extensionIndex"]):
        entry = OrderedDict()
        entry["xna"] = xna_half(inventory, extension)
        old = previous.get(extension)
        cna = old.get("cna") if old and "cna" in old else skeleton_cna()
        # Keep the ordering and fill in any role a newer schema added.
        merged = skeleton_cna()
        for key in merged:
            if key == "tests":
                for role in TEST_ROLES:
                    merged["tests"][role] = cna.get("tests", {}).get(role, "")
            elif key in cna:
                merged[key] = cna[key]
        entry["cna"] = merged
        extensions[extension] = entry
    document["extensions"] = extensions
    return document


def collect_test_names(roots):
    names = set()
    for root in roots:
        for directory, _, files in os.walk(root):
            for name in files:
                if not name.endswith((".cpp", ".cc", ".cxx")):
                    continue
                path = os.path.join(directory, name)
                try:
                    with open(path, encoding="utf-8", errors="replace") as handle:
                        text = handle.read()
                except OSError:
                    continue
                for suite, test in TEST_PATTERN.findall(text):
                    names.add(suite + "." + test)
    return names


def check(document, inventory, repo):
    problems = []
    expected = sync(inventory, document)
    if json.dumps(document, indent=1, ensure_ascii=False) != json.dumps(expected, indent=1, ensure_ascii=False):
        # Say which extension drifted rather than only that something did.
        have = document.get("extensions", {})
        want = expected["extensions"]
        for extension in want:
            if extension not in have:
                problems.append("%s is in the inventory and not in the matrix; run `inputs_matrix.py sync`" % extension)
            elif json.dumps(have[extension].get("xna"), sort_keys=True) != json.dumps(want[extension]["xna"], sort_keys=True):
                problems.append("%s: the xna half is stale; run `inputs_matrix.py sync`" % extension)
        for extension in have:
            if extension not in want:
                problems.append("%s is in the matrix and not in the inventory; the denominator does not have it" % extension)
        if not problems:
            problems.append("the matrix is not what `inputs_matrix.py sync` writes (header or ordering); re-run sync")

    names = collect_test_names([os.path.join(repo, "modules"), os.path.join(repo, "tests")])
    for extension, entry in document.get("extensions", {}).items():
        cna = entry.get("cna", {})
        status = cna.get("status")
        if status not in STATUSES:
            problems.append("%s: status %r is outside the vocabulary %s" % (extension, status, list(STATUSES)))
            continue
        for target in ("windows", "windowsPhone", "xbox360"):
            if cna.get(target) not in TARGET_STATES:
                problems.append("%s: %s is %r, outside %s" % (extension, target, cna.get(target), list(TARGET_STATES)))
        if status in ("EXTERNAL_BLOCKED", "IMPLEMENTED") and not cna.get("note"):
            problems.append("%s: status %s needs a note saying what is missing" % (extension, status))
        for role, test in cna.get("tests", {}).items():
            if not test:
                if status == "IMPLEMENTED+TESTED":
                    problems.append("%s: status IMPLEMENTED+TESTED but the %s test is not named" % (extension, role))
                continue
            for one in [t.strip() for t in test.split(",") if t.strip()]:
                if one not in names:
                    problems.append("%s: the %s test names %s, which is not in the tree" % (extension, role, one))
        fixture = cna.get("fixture")
        if fixture:
            for one in [f.strip() for f in fixture.split(",") if f.strip()]:
                if not os.path.exists(os.path.join(repo, one)):
                    problems.append("%s: fixture %s does not exist" % (extension, one))
        elif status.startswith("IMPLEMENTED"):
            problems.append("%s: status %s but no fixture is named" % (extension, status))
        if status.startswith("IMPLEMENTED") and not cna.get("importer"):
            problems.append("%s: status %s but no CNA importer is named" % (extension, status))
    return problems


def summarize(document):
    extensions = document.get("extensions", {})
    counts = OrderedDict((status, 0) for status in STATUSES)
    for entry in extensions.values():
        counts[entry["cna"]["status"]] = counts.get(entry["cna"]["status"], 0) + 1
    return counts, len(extensions)


def main(argv):
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.abspath(os.path.join(here, "..", ".."))
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=("sync", "check"))
    parser.add_argument("--inventory",
                        default=os.path.join(repo, "tests/reference/xna40/content-pipeline-api.json"))
    parser.add_argument("--matrix",
                        default=os.path.join(repo, "tests/reference/xna40/content-pipeline-inputs.json"))
    parser.add_argument("--repo", default=repo)
    args = parser.parse_args(argv)

    inventory = load(args.inventory)
    existing = load(args.matrix) if os.path.exists(args.matrix) else None

    if args.command == "sync":
        dump(args.matrix, sync(inventory, existing))
        counts, total = summarize(load(args.matrix))
        print("inputs_matrix: %d extensions; %s" % (total, ", ".join("%s %d" % kv for kv in counts.items())))
        return 0

    if existing is None:
        print("inputs_matrix: %s does not exist; run `inputs_matrix.py sync`" % args.matrix)
        return 1
    problems = check(existing, inventory, args.repo)
    counts, total = summarize(existing)
    print("inputs_matrix: %d extensions; %s" % (total, ", ".join("%s %d" % kv for kv in counts.items())))
    for problem in problems:
        print("  problem: " + problem)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
