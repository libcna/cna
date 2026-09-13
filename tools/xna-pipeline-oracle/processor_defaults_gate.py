#!/usr/bin/env python3
"""plans/plan_xnapipeline_parity.md XNAPP-023: the defaults test says what it was measured against.

`XnaProcessorDefaultsTests.cpp` constructs each XNA processor CNA provides and checks every property
against the value Microsoft's own class answered. That is only worth something if the table it
checks against is the measurement rather than a copy of it that has since drifted, and if it covers
every property rather than the ones somebody remembered.

So each assertion in that file carries a comment naming what it is asserting:

    // XNA-DEFAULT: TextureProcessor GenerateMipmaps False

and this reads them back out and compares them with `content-pipeline-api.json`'s own
`defaultValue`, failing on a value that differs, a property the inventory does not have, and any of
the inventory's properties the test does not cover.

Usage:
    processor_defaults_gate.py --inventory <api.json> --test <XnaProcessorDefaultsTests.cpp>
"""
from __future__ import annotations

import argparse
import json
import re
import sys

MARKER = re.compile(r"^\s*//\s*XNA-DEFAULT:\s*(\S+)\s+(\S+)\s+(.*?)\s*$")


def normalize(value):
    """The two sides spell the same value differently in exactly one place, the space character.

    The oracle writes a char as `'\\u0020' (32)` because that is unambiguous in a JSON file; a C++
    comment writes `' ' (32)`, because a literal escape in a comment reads as the escape. Nothing
    else is normalized: a default that differs must differ visibly.
    """
    if value is None:
        return "None"
    return str(value).replace("'\\u0020'", "' '")


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--inventory", required=True)
    parser.add_argument("--test", required=True)
    arguments = parser.parse_args(argv[1:])

    with open(arguments.inventory, encoding="utf-8") as handle:
        inventory = json.load(handle)
    measured = {}
    for processor in inventory["processors"]:
        short = processor["type"].rsplit(".", 1)[-1]
        for prop in processor["properties"]:
            measured[(short, prop["name"])] = normalize(prop.get("defaultValue"))

    asserted = {}
    duplicates = []
    with open(arguments.test, encoding="utf-8") as handle:
        for line in handle:
            found = MARKER.match(line)
            if not found:
                continue
            key = (found.group(1), found.group(2))
            if key in asserted:
                duplicates.append(key)
            asserted[key] = found.group(3)

    problems = []
    for key in sorted(duplicates):
        problems.append("%s.%s is asserted twice" % key)
    for key, value in sorted(asserted.items()):
        if key not in measured:
            problems.append("%s.%s is asserted and the inventory has no such property" % key)
        elif measured[key] != value:
            problems.append("%s.%s is asserted as %r; the oracle measured %r"
                            % (key[0], key[1], value, measured[key]))
    for key in sorted(measured):
        if key not in asserted:
            problems.append("%s.%s was measured and nothing asserts it" % key)

    for problem in problems:
        print("processor_defaults_gate: " + problem)
    print("processor_defaults_gate: %d measured, %d asserted; %d problem(s)"
          % (len(measured), len(asserted), len(problems)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
