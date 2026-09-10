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
import collections
import concurrent.futures
import hashlib
import json
import os
import posixpath
import re
import shutil
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
SWEEP = os.path.join(REPO, "build", "xna-sample-sweep")

PLATFORM_OPTION = {"w": "windows", "m": "windowsphone", "x": "xbox360"}

WINEPREFIX = os.path.expanduser("~/.wine-cna-xna40")

# The fonts the reference build itself saw, in the order it would have found them: the Wine
# prefix's own `Fonts` directory is what the genuine `FontDescriptionProcessor` resolved a family
# through, and the XNA redistributable pack beside it carries the families that prefix has not got
# (Pericles Light, Pescadero, Segoe Keycaps, Segoe Print, Wasco Sans, News Gothic, OCR A Extended,
# Jing Jing, Andy). Both are read, never written.
FONT_DIRECTORIES = (
    os.path.join(WINEPREFIX, "drive_c", "windows", "Fonts"),
    "/rv/tmp/samples/SAMPLE-140-RedistributableTTFs_ARCHIVE_3_1/original/RedistributableTTFs",
)
FXC = ("/rv/tmp/samples/_tools/directx-sdk-june-2010/extract/DXSDK/Utilities/bin/x86/fxc.exe")


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


MSBUILD_NS = "http://schemas.microsoft.com/developer/msbuild/2003"


def reconstruct_project(project, runner, staged):
    """The project as the sample's runner actually built it, beside a copy of its sources.

    Most of these runners hand-list their assets and pass no `ProcessorParameters` at all, so the
    reference carries the processor's own defaults whatever the project declares. Building the
    project as written would then compare a correct build against a reference nobody produced from
    it. The reconstruction is the project with exactly the parameters the runner set -- none, for
    almost all of them -- and it needs the sources beside it because the sample tree is read-only.

    @param project The original `.contentproj`.
    @param runner The sample's entry in `runner-provenance.json`.
    @param staged Directory to build the reconstruction in.
    @return The reconstructed project's path, or None when the project needs no reconstruction.
    """
    overrides = {posixpath.basename(k): v for k, v in runner.get("parameterOverrides", {}).items()}
    ET.register_namespace("", MSBUILD_NS)
    tree = ET.parse(project)
    changed = False
    for item in tree.getroot().iter("{%s}Compile" % MSBUILD_NS):
        include = (item.get("Include") or "").replace("\\", "/")
        wanted = overrides.get(posixpath.basename(include), {})
        for child in list(item):
            local = child.tag.rsplit("}", 1)[-1]
            if local.lower().startswith("processorparameters"):
                item.remove(child)
                changed = True
        for name in sorted(wanted):
            element = ET.SubElement(item, "{%s}ProcessorParameters_%s" % (MSBUILD_NS, name))
            element.text = wanted[name]
            changed = True
    if not changed:
        return None
    source = os.path.dirname(project)
    if not os.path.isdir(staged):
        shutil.copytree(source, staged,
                        ignore=shutil.ignore_patterns("bin", "obj", "*.xnb"))
    written = os.path.join(staged, os.path.basename(project))
    tree.write(written, encoding="utf-8", xml_declaration=True)
    return written


def build_one(job):
    unit, root, tool, outdir, timeout, provenance, staging = job
    # A root written by more than one `BuildContent` call gets one output directory per call, and
    # each compares only the references its own project names -- otherwise the second pass wipes
    # the first's output and reports the whole root missing
    # (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-163`).
    output = os.path.join(outdir, slug(unit["outputRoot"]) +
                          ("__" + slug(os.path.basename(unit["project"])) if unit.get("secondPass")
                           else ""))
    shutil.rmtree(output, ignore_errors=True)
    os.makedirs(output, exist_ok=True)
    project = os.path.join(root, unit["project"])
    runner = provenance.get(unit["sample"], {})
    reconstructed = None
    if runner.get("kind") in ("explicit", "enumerated"):
        staged = os.path.join(staging, slug(unit["project"]).replace(".contentproj", ""))
        try:
            reconstructed = reconstruct_project(project, runner, staged)
        except Exception as error:  # noqa: BLE001 - a reconstruction that fails is a finding
            reconstructed = None
            print("reconstruct failed for %s: %s" % (unit["project"], error), flush=True)
    if reconstructed:
        project = reconstructed
    # The configuration the sample's own runner handed `BuildContent`, because
    # `EffectProcessor.DebugMode` defaults to `Auto` and follows it: 101 of these runners pass
    # `Debug` and 12 pass `Release`, and a content project's own `Configuration` -- which defaults
    # to `Debug` and is what a build reads when nobody says otherwise -- is not what the runner
    # used. Without this every effect of those twelve samples was compiled `/Zi /Od` against a
    # reference compiled optimized, and came out at twice the length
    # (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-200`).
    configuration = runner.get("buildConfiguration")
    arguments = [
        tool, "build", project, "-o", output,
        "--format", "xnb",
        "--xnb-platform", PLATFORM_OPTION.get(unit["platform"], "windows"),
        "--xnb-profile", "hidef" if unit["hiDef"] else "reach",
        "--xnb-compress", "lzx" if unit["compressed"] else "none",
        "--fx-compiler", FXC,
        "--fx-compiler-launcher", "wine",
    ]
    if configuration:
        arguments += ["--build-configuration", configuration]
    for directory in FONT_DIRECTORIES:
        arguments += ["--font-directory", directory]
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
        "reconstructedProject": reconstructed,
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
    # The same output path without regard to case, because the reference tree's own filesystem was
    # case-insensitive: Spacewar's project items go to `Textures/` and its models' nested textures
    # to `textures/`, and the corpus has one `textures/` holding both. A folded name with two
    # different CNA outputs under it is left alone rather than guessed at
    # (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-184`).
    folded = collections.defaultdict(list)
    for directory, _, files in os.walk(output):
        for name in files:
            if name.lower().endswith(".xnb"):
                relative = os.path.relpath(os.path.join(directory, name), output)
                relative = relative.replace(os.sep, "/")
                produced.add(relative)
                folded[relative.lower()].append(relative)
    for reference in references:
        relative = reference["reference"][len(prefix):]
        mine = os.path.join(output, relative.replace("/", os.sep))
        if not os.path.isfile(mine):
            candidates = folded.get(relative.lower(), [])
            if len(candidates) == 1 and candidates[0] != relative:
                relative = candidates[0]
                mine = os.path.join(output, relative.replace("/", os.sep))
        row = {
            "reference": reference["reference"],
            "asset": relative[:-len(".xnb")],
            "referenceSize": reference.get("size"),
            "referenceSha256": reference.get("sha256"),
            "rootReader": reference.get("rootReader"),
        }
        # The reference itself may be gone: `/rv/tmp/samples` is not this campaign's tree and
        # another session's `prune-completed-sample.sh` removed 21 of the frozen references on
        # 2026-09-09. A reference that is no longer on disk is a *finding* rather than a crash,
        # and it is not CNA's: the sweep says so and carries on
        # (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-190`, §11.4).
        if not os.path.isfile(os.path.join(root, reference["reference"])):
            row["result"] = "reference-removed"
            produced.discard(relative)
            rows.append(row)
            continue
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
    parser.add_argument("--provenance",
                        default=os.path.join(SWEEP, "manifest", "runner-provenance.json"))
    parser.add_argument("--staging", default=os.path.join(SWEEP, "staged"))
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
                if unit.get("secondPass") and mapping.get("project") == unit["project"]:
                    by_root.setdefault(unit["outputRoot"] + "\x00" + unit["project"],
                                       []).append(mapping)
                break

    provenance = {}
    if os.path.exists(args.provenance):
        # A run's whole answer for a sample can turn on this file: it decides whether the sample's
        # project parameters are used or thrown away, and a classifier that has been corrected
        # since it was written silently keeps the old answer. Run 47 built 78 of SAMPLE-014's
        # textures against a reference that used `PremultiplyAlpha False` because this manifest
        # predated the fix that reads its runner correctly
        # (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-208`).
        generator = os.path.join(HERE, "runner_provenance.py")
        if (os.path.exists(generator)
                and os.path.getmtime(generator) > os.path.getmtime(args.provenance)):
            print("sweep: %s is older than %s; regenerate it before sweeping." %
                  (os.path.relpath(args.provenance, REPO), os.path.relpath(generator, REPO)),
                  file=sys.stderr)
            return 2
        with open(args.provenance, encoding="utf-8") as handle:
            provenance = json.load(handle).get("samples", {})
    os.makedirs(args.outdir, exist_ok=True)
    os.makedirs(args.staging, exist_ok=True)
    results = []
    jobs = [(unit, root, args.tool, args.outdir, args.timeout, provenance, args.staging)
            for unit in units]
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for unit, build in zip(units, pool.map(build_one, jobs)):
            key = (unit["outputRoot"] + "\x00" + unit["project"]) if unit.get("secondPass") \
                else unit["outputRoot"]
            rows = compare_unit(unit, root, build, by_root.get(key, []))
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
                "reconstructedProject": build.get("reconstructedProject"),
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
