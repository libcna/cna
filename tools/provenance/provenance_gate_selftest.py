#!/usr/bin/env python3
"""plans/plan_xnapipeline_parity.md XNAPP-008: does the provenance gate actually catch anything?

A gate that has never failed is a gate nobody has tested. This builds a throwaway Git repository,
plants one of each thing the gate exists to find, and checks that it finds exactly that -- so the
green run on the real tree means the tree is clean rather than that the gate is asleep.

Usage:
    provenance_gate_selftest.py [--gate <path to provenance_gate.py>]
"""
from __future__ import annotations

import argparse
import json
import os
import struct
import subprocess
import sys
import tempfile


def git(repo, *arguments):
    subprocess.run(["git", "-C", repo] + list(arguments), check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def write(repo, name, data):
    path = os.path.join(repo, name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as handle:
        handle.write(data if isinstance(data, bytes) else data.encode("utf-8"))


def font_with_family(family):
    """The smallest TrueType file whose `name` table says one family name."""
    value = family.encode("utf-16-be")
    # One name record: platform 3, encoding 1, language 0x409, name id 1 (family).
    records = struct.pack(">HHHHHH", 3, 1, 0x0409, 1, len(value), 0)
    name_table = struct.pack(">HHH", 0, 1, 6 + 12) + records + value
    # One table directory entry for `name`.
    directory = struct.pack(">IHHHH", 0x00010000, 1, 16, 0, 0)
    entry_offset = len(directory) + 16
    entry = b"name" + struct.pack(">III", 0, entry_offset, len(name_table))
    return directory + entry + name_table


def run(gate, repo):
    finished = subprocess.run([sys.executable, gate, "--repo", repo, "--spdx-baseline", "100000",
                               "--json", os.path.join(repo, "report.json")],
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    with open(os.path.join(repo, "report.json"), encoding="utf-8") as handle:
        return finished.returncode, json.load(handle), finished.stdout.decode("utf-8", "replace")


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--gate", default=os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                       "provenance_gate.py"))
    arguments = parser.parse_args(argv[1:])

    failures = []
    with tempfile.TemporaryDirectory(prefix="cna-provenance-selftest-") as repo:
        git(repo, "init", "-q")
        git(repo, "config", "user.email", "selftest@example.invalid")
        git(repo, "config", "user.name", "selftest")
        write(repo, ".gitignore", "build/\n")
        write(repo, "tools/provenance/third-party.json",
              json.dumps({"vendored": []}))
        # Check 6's clean case: a source adapted from properly licensed third-party code, with the
        # row that allows it. It must NOT be reported, or the gate makes the exception useless.
        write(repo, "tools/provenance/derived-sources.json",
              json.dumps({"derived": [
                  {"path": "modules/core/src/Adapted.cpp", "upstreamProject": "example/upstream",
                   "upstreamUrl": "https://example.invalid/upstream",
                   "upstreamRevision": "0123456789abcdef", "license": "MIT",
                   "licenseFile": "THIRD_PARTY_NOTICES.md", "spdx": "MIT",
                   "whatWasTaken": "one function", "whatWasChanged": "nothing"}]}))
        write(repo, "THIRD_PARTY_NOTICES.md", "MIT, Copyright (c) Microsoft Corporation\n")
        write(repo, "modules/core/src/Adapted.cpp",
              "// SPDX-License-Identifier: MIT\n"
              "// Copyright (c) Microsoft Corporation.\n"
              "int adapted();\n")
        write(repo, "modules/core/src/Clean.cpp", "// SPDX-License-Identifier: MS-PL\nint clean();\n")
        write(repo, "tests/assets/xna40/source/probe.xml", "<XnaContent />\n")
        write(repo, "tests/assets/xna40/source/PROVENANCE.json",
              json.dumps({"files": [{"file": "probe.xml", "origin": "authored",
                                     "thirdParty": False}]}))
        git(repo, "add", "-A")
        git(repo, "commit", "-qm", "clean")

        code, report, output = run(arguments.gate, repo)
        if code != 0 or report["findings"]:
            failures.append("a clean tree was reported dirty: %s" % output)

        # One of each thing the gate is for.
        write(repo, "tools/oracle/Microsoft.Xna.Framework.dll", b"MZ\x90\x00" + b"\0" * 64)
        write(repo, "tests/assets/fonts/anonymous.ttf", font_with_family("Segoe UI"))
        write(repo, "tests/assets/fonts/PROVENANCE.json",
              json.dumps({"file": "anonymous.ttf", "license": "unknown"}))
        write(repo, "modules/core/src/Borrowed.cpp",
              "// SPDX-License-Identifier: MS-PL\n"
              "// Copyright (c) The MonoGame Team\n"
              "int borrowed();\n")
        write(repo, "modules/core/src/monogame_helpers.cpp",
              "// SPDX-License-Identifier: MS-PL\nint helper();\n")
        write(repo, "modules/core/src/Foreign.cpp",
              "// SPDX-License-Identifier: GPL-3.0-only\nint foreign();\n")
        os.makedirs(os.path.join(repo, "vendor", "undeclared"), exist_ok=True)
        write(repo, "vendor/undeclared/README.md", "undeclared\n")
        write(repo, "tests/assets/xna40/source/unexplained.xml", "<XnaContent />\n")
        # Check 6, the three ways a derived-source row and its file can come apart.
        write(repo, "tools/provenance/derived-sources.json",
              json.dumps({"derived": [
                  {"path": "modules/core/src/Adapted.cpp", "upstreamProject": "example/upstream",
                   "upstreamUrl": "https://example.invalid/upstream",
                   "upstreamRevision": "0123456789abcdef", "license": "MIT",
                   "licenseFile": "THIRD_PARTY_NOTICES.md", "spdx": "MS-PL",
                   "whatWasTaken": "one function", "whatWasChanged": "nothing"},
                  {"path": "modules/core/src/Vanished.cpp", "upstreamProject": "example/gone",
                   "upstreamUrl": "https://example.invalid/gone",
                   "upstreamRevision": "cafebabe", "license": "MIT",
                   "licenseFile": "THIRD_PARTY_NOTICES.md", "spdx": "MIT",
                   "whatWasTaken": "one function", "whatWasChanged": "nothing"},
                  {"path": "modules/core/src/Unattributed.cpp", "upstreamProject": "example/other",
                   "upstreamUrl": "https://example.invalid/other",
                   "upstreamRevision": "d00d", "license": "MIT",
                   "licenseFile": "THIRD_PARTY_NOTICES.md", "spdx": "MIT",
                   "whatWasTaken": "one function", "whatWasChanged": "nothing"}]}))
        write(repo, "modules/core/src/Unattributed.cpp",
              "// SPDX-License-Identifier: MIT\nint unattributed();\n")
        git(repo, "add", "-A")
        git(repo, "commit", "-qm", "planted")

        code, report, output = run(arguments.gate, repo)
        if code == 0:
            failures.append("the gate passed a tree with a planted Microsoft assembly in it")
        found = {(row["check"], row["path"]) for row in report["findings"]}
        expected = [
            ("binary", "tools/oracle/Microsoft.Xna.Framework.dll"),
            ("font", "tests/assets/fonts/anonymous.ttf"),
            ("source", "modules/core/src/Borrowed.cpp"),
            ("source", "modules/core/src/monogame_helpers.cpp"),
            ("source", "modules/core/src/Foreign.cpp"),
            ("vendor", "vendor/undeclared"),
            ("corpus", "tests/assets/xna40/source/unexplained.xml"),
            ("derived", "modules/core/src/Adapted.cpp"),
            ("derived", "modules/core/src/Vanished.cpp"),
            ("derived", "modules/core/src/Unattributed.cpp"),
        ]
        for row in expected:
            if row not in found:
                failures.append("the gate did not find %s: %s" % (row, output))

        # And the ignore rule: an oracle copy that could be committed is a finding.
        write(repo, ".gitignore", "# nothing ignored\n")
        git(repo, "add", "-A")
        git(repo, "commit", "-qm", "unignored")
        code, report, output = run(arguments.gate, repo)
        if not any(row["check"] == "ignored" for row in report["findings"]):
            failures.append("the gate did not notice that the oracle's copy path is committable")

    for failure in failures:
        print("provenance_gate_selftest: " + failure)
    print("provenance_gate_selftest: %d failure(s)" % len(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
