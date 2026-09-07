#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-040: rebuild every mapped build unit and compare.

One unit is one `.contentproj` built into one output root, with the target read off the reference
headers rather than assumed. The build goes through `cna-content` -- the product front end, the same
one a game's own build uses -- and nothing here reaches inside the pipeline.

Comparison is per file and by bytes first. Every genuine reference under the unit's output root is
looked for at the same relative path in CNA's output; a reference CNA did not produce is `missing`,
a file CNA produced that the reference set has not got is `extra`, and everything else is
`identical` or `differs`. Side outputs -- the textures a `ModelProcessor` builds for a model's
materials, which no project item names -- are compared exactly like the rest, because they are
output at a path and that is all the comparison needs.

Usage:
    sweep.py --map <map.json> --out <results.json> [--outdir <dir>] [--jobs N] [--only <substring>]
"""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
SWEEP = os.path.join(REPO, "build", "xna-sample-sweep")

PLATFORM_OPTION = {"w": "windows", "m": "windowsphone", "x": "xbox360"}

FONT_DIRECTORY = ("/rv/tmp/samples/SAMPLE-140-RedistributableTTFs_ARCHIVE_3_1/original/"
                  "RedistributableTTFs")
FXC = ("/rv/tmp/samples/_tools/directx-sdk-june-2010/extract/DXSDK/Utilities/bin/x86/fxc.exe")
WINEPREFIX = os.path.expanduser("~/.wine-cna-xna40")


def slug(text):
    return re.sub(r"[^A-Za-z0-9._-]+", "_", text).strip("_")


def digest(path):
    with open(path, "rb") as handle:
        return hashlib.sha256(handle.read()).hexdigest()


def first_difference(left, right):
    """(offset of the first differing byte, differing byte count) for two files."""
    with open(left, "rb") as handle:
        a = handle.read()
    with open(right, "rb") as handle:
        b = handle.read()
    limit = min(len(a), len(b))
    first = None
    differing = abs(len(a) - len(b))
    for index in range(limit):
        if a[index] != b[index]:
            if first is None:
                first = index
            differing += 1
    if first is None and len(a) != len(b):
        first = limit
    return first, differing


def build_one(job):
    unit, root, tool, outdir, timeout = job
    output = os.path.join(outdir, slug(unit["outputRoot"]))
    shutil.rmtree(output, ignore_errors=True)
    os.makedirs(output, exist_ok=True)
    project = os.path.join(root, unit["project"])
    arguments = [
        tool, "build", project, "-o", output,
        "--format", "xnb",
        "--xnb-platform", PLATFORM_OPTION.get(unit["platform"], "windows"),
        "--xnb-profile", "hidef" if unit["hiDef"] else "reach",
        "--xnb-compress", "lzx" if unit["compressed"] else "none",
        "--font-directory", FONT_DIRECTORY,
        "--fx-compiler", FXC,
        "--fx-compiler-launcher", "wine",
    ]
    if unit["platform"] == "x":
        arguments.append("--xnb-allow-unverified-xbox")
    environment = dict(os.environ)
    environment.pop("WAYLAND_DISPLAY", None)
    environment["WINEPREFIX"] = WINEPREFIX
    environment["WINEDEBUG"] = "-all"
    environment["TMPDIR"] = os.path.join(SWEEP, "tmp", "build")
    os.makedirs(environment["TMPDIR"], exist_ok=True)
    started = time.time()
    try:
        finished = subprocess.run(arguments, capture_output=True, text=True, timeout=timeout,
                                  env=environment, cwd=REPO)
        status, out, err = finished.returncode, finished.stdout, finished.stderr
    except subprocess.TimeoutExpired:
        status, out, err = 124, "", "timed out after %d s" % timeout
    return {
        "outputRoot": unit["outputRoot"],
        "output": output,
        "status": status,
        "seconds": round(time.time() - started, 2),
        "stdout": out,
        "stderr": err,
        "command": arguments,
    }


def compare_unit(unit, root, build, references):
    """Every reference under this unit's root against what CNA wrote at the same relative path."""
    output = build["output"]
    prefix = unit["outputRoot"] + "/"
    rows = []
    produced = set()
    for directory, _, files in os.walk(output):
        for name in files:
            if name.lower().endswith(".xnb"):
                produced.add(os.path.relpath(os.path.join(directory, name), output)
                             .replace(os.sep, "/"))
    for reference in references:
        relative = reference["reference"][len(prefix):]
        mine = os.path.join(output, relative.replace("/", os.sep))
        row = {
            "reference": reference["reference"],
            "asset": relative[:-len(".xnb")],
            "referenceSize": reference.get("size"),
            "referenceSha256": reference.get("sha256"),
            "rootReader": reference.get("rootReader"),
        }
        if not os.path.isfile(mine):
            row["result"] = "missing"
            rows.append(row)
            continue
        produced.discard(relative)
        row["cnaSize"] = os.path.getsize(mine)
        row["cnaSha256"] = digest(mine)
        if row["cnaSha256"] == row["referenceSha256"]:
            row["result"] = "identical"
        else:
            row["result"] = "differs"
            offset, count = first_difference(os.path.join(root, reference["reference"]), mine)
            row["firstDifference"] = offset
            row["differingBytes"] = count
        rows.append(row)
    for relative in sorted(produced):
        rows.append({
            "reference": None,
            "asset": relative[:-len(".xnb")],
            "result": "extra",
            "cnaSize": os.path.getsize(os.path.join(output, relative.replace("/", os.sep))),
        })
    return rows


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--map", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--outdir", default=os.path.join(SWEEP, "out", "units"))
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--only", default=None, help="build only units whose root contains this")
    parser.add_argument("--timeout", type=int, default=1800)
    parser.add_argument("--tool", default=os.path.join(REPO, "cmake-build-debug", "cna-content"))
    args = parser.parse_args(argv)

    with open(args.map, encoding="utf-8") as handle:
        document = json.load(handle)
    root = document["root"]
    units = document["buildUnits"]
    if args.only:
        units = [u for u in units if args.only in u["outputRoot"]]
    by_root = {}
    for mapping in document["mappings"]:
        for unit in units:
            if mapping["reference"].startswith(unit["outputRoot"] + "/"):
                by_root.setdefault(unit["outputRoot"], []).append(mapping)
                break

    os.makedirs(args.outdir, exist_ok=True)
    results = []
    jobs = [(unit, root, args.tool, args.outdir, args.timeout) for unit in units]
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for unit, build in zip(units, pool.map(build_one, jobs)):
            rows = compare_unit(unit, root, build, by_root.get(unit["outputRoot"], []))
            counts = {}
            for row in rows:
                counts[row["result"]] = counts.get(row["result"], 0) + 1
            results.append({
                "sample": unit["sample"],
                "outputRoot": unit["outputRoot"],
                "project": unit["project"],
                "platform": unit["platform"],
                "hiDef": unit["hiDef"],
                "compressed": unit["compressed"],
                "buildStatus": build["status"],
                "seconds": build["seconds"],
                "counts": counts,
                "rows": rows,
                "stdoutTail": build["stdout"][-4000:],
                "stderrTail": build["stderr"][-8000:],
            })
            print("%-70s status=%-3d %s" % (unit["outputRoot"][:70], build["status"],
                                            " ".join("%s=%d" % kv for kv in sorted(counts.items()))),
                  flush=True)

    totals = {}
    for result in results:
        for key, value in result["counts"].items():
            totals[key] = totals.get(key, 0) + value
    document = {
        "generator": "tools/xna-sample-sweep/sweep.py",
        "root": root,
        "units": len(results),
        "unitsBuilt": sum(1 for r in results if r["buildStatus"] == 0),
        "totals": totals,
        "results": results,
    }
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump(document, handle, indent=1, sort_keys=True)
        handle.write("\n")
    print(json.dumps({"units": document["units"], "unitsBuilt": document["unitsBuilt"],
                      "totals": totals}, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
