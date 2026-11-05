#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Compare focused GTest registration and isolated outcomes with the aggregate.

The oracle is binary equivalence, never a golden generated from CNA. Missing,
failed, crashed, timed-out or empty runs cannot qualify as parity. Skips are
compared as skips, so a skipped aggregate cannot hide a focused failure.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET


def run(binary, arguments, root, environment):
    result = subprocess.run([str(binary), *arguments], cwd=root, env=environment,
                            text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=120)
    if result.returncode:
        raise RuntimeError(f"{binary.name} exited {result.returncode}:\n{result.stdout}")
    return result.stdout


def inventory(binary, root, environment):
    output = run(binary, ["--gtest_list_tests"], root, environment)
    suite = None
    names = set()
    for line in output.splitlines():
        token = line.split("#", 1)[0].strip()
        if not token:
            continue
        if not line[0].isspace() and token.endswith("."):
            suite = token
        elif line[0].isspace() and suite and " " not in token:
            name = suite + token
            if name in names:
                raise RuntimeError(f"duplicate registration in {binary.name}: {name}")
            names.add(name)
        else:
            suite = None  # diagnostic output is not a suite or a test
    if not names:
        raise RuntimeError(f"{binary.name} registered no tests")
    return names


def outcome(binary, name, root, environment, artifacts):
    xml = artifacts / (binary.name + ".xml")
    xml.unlink(missing_ok=True)
    run(binary, [f"--gtest_filter={name}", "--gtest_also_run_disabled_tests",
                 f"--gtest_output=xml:{xml}"], root, environment)
    cases = ET.parse(xml).getroot().findall(".//testcase")
    if len(cases) != 1:
        raise RuntimeError(f"{binary.name}: expected exactly one case for {name}, got {len(cases)}")
    case = cases[0]
    actual = case.attrib["classname"] + "." + case.attrib["name"]
    if actual != name or case.attrib.get("status") != "run" or case.find("failure") is not None:
        raise RuntimeError(f"{binary.name}: missing, unexecuted or failed case {name}")
    if case.find("skipped") is not None:
        return "SKIP"
    if case.attrib.get("result") != "completed":
        raise RuntimeError(f"{binary.name}: incomplete case {name}")
    return "PASS"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--aggregate", type=Path, required=True)
    parser.add_argument("--focused", type=Path, action="append", required=True)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--case", action="append", default=[])
    parser.add_argument("--complete-inventory", action="store_true")
    parser.add_argument("--allow-unbuilt", action="store_true")
    parser.add_argument("--without-driver", action="store_true")
    args = parser.parse_args()
    binaries = [args.aggregate.resolve(), *(p.resolve() for p in args.focused)]
    missing = [str(p) for p in binaries if not p.is_file()]
    if missing:
        print("Unbuilt parity prerequisites: " + ", ".join(missing))
        return 77 if args.allow_unbuilt else 1
    environment = dict(os.environ)
    # A caller's GTest flags must not turn discovery or one-case execution into an empty,
    # sharded, repeated or differently filtered run. CLI arguments also pin filter/output.
    environment = {k: v for k, v in environment.items() if not k.startswith("GTEST_")}
    if args.without_driver:
        environment.pop("FNA3D_FORCE_DRIVER", None)
    try:
        inventories = {p: inventory(p, args.source_root, environment) for p in binaries}
        aggregate = inventories[binaries[0]]
        union = set()
        for binary in binaries[1:]:
            extra = inventories[binary] - aggregate
            if extra:
                raise RuntimeError(f"{binary.name}: absent from aggregate: {sorted(extra)}")
            union.update(inventories[binary])
        if args.complete_inventory and union != aggregate:
            raise RuntimeError(f"uncovered aggregate registrations: {sorted(aggregate - union)}")
        with tempfile.TemporaryDirectory(prefix="cna-focused-parity-") as directory:
            artifacts = Path(directory)
            for name in args.case:
                owners = [p for p in binaries[1:] if name in inventories[p]]
                if name not in aggregate or not owners:
                    raise RuntimeError(f"parity case has no aggregate/focused registration: {name}")
                expected = outcome(binaries[0], name, args.source_root, environment, artifacts)
                for binary in owners:
                    actual = outcome(binary, name, args.source_root, environment, artifacts)
                    if actual != expected:
                        raise RuntimeError(f"{name}: aggregate={expected}, {binary.name}={actual}")
                    print(f"{name}: aggregate and {binary.name} {actual}")
        print(f"Registration parity: {len(aggregate)} aggregate, {len(union)} focused names")
        return 0
    except (RuntimeError, OSError, subprocess.TimeoutExpired, ET.ParseError, KeyError) as error:
        print(error)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
