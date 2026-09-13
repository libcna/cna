#!/usr/bin/env python3
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-118: a member XNA lets a game override, CNA must too.

XNA's content pipeline is extended by derivation. A game writes `class MyProcessor : ModelProcessor`
and overrides the pieces it wants to change -- `Process`, `ConvertMaterial`, `ProcessVertexChannel`,
and, just as often, a *property*: `public override bool GenerateTangentFrames { get { return true; } }`
is how the Normal Mapping sample forces tangent frames on. Twenty-six build units in the sample
corpus derive from a built-in processor, so this is the ordinary way the pipeline is used rather
than an exotic corner of it.

A C++ member that is not declared `virtual` cannot be overridden, and the failure is silent: the
derived class compiles, its function is never called, and the content is built with the base's
answer. So the reference measurement is used as the authority here too. `content-pipeline-api.json`
records, for every public and protected member of Microsoft's own assemblies, whether the CLR marks
it virtual -- read from assembly metadata, never from a method body -- and this gate checks that
every one of them is `virtual` (or `override`, which is also virtual) where CNA declares the
counterpart.

Two lists are reported separately, because they are different findings:

  * NOT VIRTUAL -- CNA has the member and a game cannot override it. Always a defect.
  * ABSENT      -- CNA does not declare the member at all. A gap in the surface, not in its
                   virtuality, and tracked as its own thing.

Both have an exemption table below, and every entry in it carries the reason it is not a defect.

Usage:
    virtual_members_gate.py --inventory <content-pipeline-api.json> --include <dir> [--include <dir>]
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import re
import sys

# Members the CLR marks virtual that C++ cannot or should not mirror. Each entry says why; an
# entry is removed by making the member virtual, not by widening the reason.
EXEMPT_NOT_VIRTUAL = {
    ("VertexChannel", "ReadConvertedContent"):
        "a member function template; C++ has no virtual templates, and the conversion it dispatches "
        "is reached through the non-template VertexChannel base instead",
}

EXEMPT_ABSENT = {
    ("AudioContent", "Finalize"):
        "C# finalizer; the C++ counterpart is the destructor",
    ("InvalidContentException", "GetObjectData"):
        ".NET binary serialization of an exception, which C++ has no counterpart for",
    ("PixelBitmapContent", "ToString"):
        "CNA does not implement ToString on PixelBitmapContent<T>; an absent member rather than a "
        "non-overridable one, tracked with the rest of the surface in the parity map",
}


def strip_comments(source):
    """Removes block and line comments, so a member named in prose is not mistaken for a declaration."""
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    return re.sub(r"//[^\n]*", "", source)


def declarations(source):
    """Splits a header into declarator fragments: the text up to each `;`, `{` or `}`."""
    return re.split(r"[;{}]", strip_comments(source).replace("\n", " "))


def load_headers(directories):
    """Reads every header under the given directories, returning {path: (source, fragments)}."""
    headers = {}
    for directory in directories:
        for path in glob.glob(os.path.join(directory, "**", "*.hpp"), recursive=True):
            source = open(path, encoding="utf-8").read()
            headers[path] = (strip_comments(source), declarations(source))
    return headers


def cpp_names(member):
    """The C++ spellings of one CLR member: a property is a getter and, when it has one, a setter."""
    if member["kind"] != "property":
        return [member["name"]]
    names = ["get%sProperty" % member["name"]]
    if member.get("set"):
        names.append("set%sProperty" % member["name"])
    return names


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--inventory", required=True)
    parser.add_argument("--include", action="append", required=True)
    parser.add_argument("--verbose", action="store_true")
    arguments = parser.parse_args(argv)

    inventory = json.load(open(arguments.inventory, encoding="utf-8"))
    headers = load_headers(arguments.include)

    checked = 0
    not_virtual = []
    absent = []
    for entry in inventory["types"]:
        short = entry["fullName"].split(".")[-1].split("`")[0]
        if "+" in short:
            # A nested type: its members are reached through the type that encloses it.
            continue
        if entry.get("sealed"):
            # Nothing can derive from a sealed type, so whether its members are virtual is not
            # observable by a game. `ContentWriter` is the case in point: sealed in XNA, `final` in
            # CNA, and its `Dispose(bool)` carries the virtual marker only because it overrides
            # `BinaryWriter`'s.
            continue
        overridable = [m for m in entry.get("members", [])
                       if m.get("virtual") and not m.get("abstract")]
        if not overridable:
            continue
        declaring = [path for path, (source, _) in headers.items()
                     if re.search(r"^\s*(class|struct)\s+%s\b" % re.escape(short), source, re.M)]
        if not declaring:
            for member in overridable:
                for name in cpp_names(member):
                    absent.append((short, name))
            continue
        fragments = [f for path in declaring for f in headers[path][1]]
        for member in overridable:
            for name in cpp_names(member):
                found = [f for f in fragments if re.search(r"\b%s\s*\(" % re.escape(name), f)]
                if not found:
                    absent.append((short, name))
                elif any("virtual" in f or "override" in f for f in found):
                    checked += 1
                else:
                    not_virtual.append((short, name))

    not_virtual = [row for row in not_virtual if row not in EXEMPT_NOT_VIRTUAL]
    absent = [row for row in absent if row not in EXEMPT_ABSENT]

    if arguments.verbose:
        print("%d overridable members declared virtual in CNA" % checked)
        print("%d exemptions: %d not-virtual, %d absent"
              % (len(EXEMPT_NOT_VIRTUAL) + len(EXEMPT_ABSENT), len(EXEMPT_NOT_VIRTUAL),
                 len(EXEMPT_ABSENT)))

    if not_virtual:
        print("XNA declares these virtual; CNA's counterpart cannot be overridden:", file=sys.stderr)
        for type_name, member in sorted(not_virtual):
            print("  %s::%s" % (type_name, member), file=sys.stderr)
    if absent:
        print("XNA declares these virtual; CNA does not declare them at all:", file=sys.stderr)
        for type_name, member in sorted(absent):
            print("  %s::%s" % (type_name, member), file=sys.stderr)
    if not_virtual or absent:
        return 1

    print("%d overridable members checked; every one CNA declares is virtual." % checked)
    return 0


if __name__ == "__main__":
    sys.exit(main())
