#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-227: is the float tolerance narrow enough to see a
difference that is not a float's last bits?

`classify.py` calls a pair `float-tolerance` when every number it reads differs by less than 1e-6
of the larger magnitude. That threshold is only worth having if a *real* difference cannot hide
under it. This takes every reference the run classified that way, copies CNA's file, changes one of
the very floats the classifier compared -- by a relative 1e-5, ten times the threshold, and by a
relative 1e-2 -- and asks the classifier again. A mutation that still comes back `float-tolerance`
is a hole.

    tolerance_mutation.py --classified <classified.json> [--limit N]
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import classify  # noqa: E402  (path is set above on purpose)


def floats_in(path):
    """Every offset in the file that reads as a plausible normal-sized float."""
    with open(path, "rb") as handle:
        data = handle.read()
    out = []
    for at in range(0, len(data) - 4):
        value = struct.unpack_from("<f", data, at)[0]
        if value == value and abs(value) != float("inf") and 1e-3 < abs(value) < 1e4:
            out.append((at, value))
    return data, out


def mutate(path, scale):
    """A copy of `path` with one float scaled, or None when there is nothing to scale."""
    data, candidates = floats_in(path)
    if not candidates:
        return None, None
    at, value = candidates[len(candidates) // 2]
    changed = bytearray(data)
    struct.pack_into("<f", changed, at, value * scale)
    handle = tempfile.NamedTemporaryFile(suffix=".xnb", delete=False)
    handle.write(bytes(changed))
    handle.close()
    return handle.name, (at, value, value * scale)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--classified", required=True)
    parser.add_argument("--limit", type=int, default=0)
    arguments = parser.parse_args(argv)

    with open(arguments.classified, "r", encoding="utf-8") as handle:
        document = json.load(handle)
    rows = [(key, value) for key, value in sorted(document["answers"].items())
            if value.get("classification") == "float-tolerance"]
    if arguments.limit:
        rows = rows[:arguments.limit]
    holes = 0
    for key, value in rows:
        reference, mine = value["reference"], value["cna"]
        if not (os.path.exists(reference) and os.path.exists(mine)):
            print("%-64s skipped: a file is missing" % key[-64:])
            continue
        line = "%-64s" % key[-64:]
        for scale in (1.00001, 1.01):
            path, what = mutate(mine, scale)
            if path is None:
                line += "  no float to change"
                continue
            try:
                answer = classify.explain((reference, path))
            finally:
                os.unlink(path)
            got = answer.get("classification")
            line += "  x%-8g -> %-18s" % (scale, got)
            if got == "float-tolerance":
                holes += 1
        print(line)
    print("%d reference(s) tested, %d mutation(s) still inside the tolerance" % (len(rows), holes))
    return 1 if holes else 0


if __name__ == "__main__":
    sys.exit(main())
