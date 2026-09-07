#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-042: explain every pair the sweep found differing.

The sweep answers "are the bytes the same". This answers "why not", and it does it in the order
that makes the cheapest true statement first:

  1. **The container's payload is identical.** An `.xnb` XNA compressed carries an LZX stream, and
     two conforming LZX encoders do not agree on a byte -- the match finder's choices are the
     encoder's. Where the *decompressed* bodies are equal, everything a runtime ever sees is
     equal, and the difference is the encoder's alone.
  2. **The normalized semantics are identical.** The independent parser reads both and the
     comparison of `tools/xna-pipeline-oracle/differential/compare.py` finds nothing.
  3. **Otherwise**, the differences it does find, as the classification's evidence.

Reads the sweep's own results; runs no build.

Usage:
    classify.py --map <map.json> --results <results.json> --out <classified.json> [--jobs N]
"""
from __future__ import annotations

import argparse
import collections
import concurrent.futures
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools", "xnb"))
sys.path.insert(0, os.path.join(REPO, "tools", "xna-pipeline-oracle", "differential"))
import xnb_conformance as X  # noqa: E402
import compare as differential  # noqa: E402


def slug(text):
    return re.sub(r"[^A-Za-z0-9._-]+", "_", text).strip("_")


def body(path):
    """The container's payload, decompressed when the header says it is compressed."""
    with open(path, "rb") as handle:
        data = handle.read()
    if len(data) < 14:
        return data
    flags = data[5]
    declared = struct.unpack("<I", data[10:14])[0]
    if flags & 0x80:
        return X.lzx_decompress(data[14:], declared, path)[0]
    if flags & 0x40:
        return X.lz4_block_decompress(data[14:], declared, path)
    return data[10:]


_NUMBERS = re.compile(r"^(.*): (-?[0-9][0-9.eE+-]*) vs (-?[0-9][0-9.eE+-]*)$")


def float_distance(found):
    """The worst distance over a difference list, or None if any entry is not numeric.

    Relative alone is the wrong measure near zero: a bounding sphere whose centre is on the axis
    comes out as `-2.1e-07` on one side and `1.4e-07` on the other, which is a *relative* distance
    of 1.67 and an absolute one of 3.5e-07. Both are the same rounding. So the two are combined the
    way a float comparison normally is -- the difference against the larger of the two magnitudes
    and one -- which keeps a genuine disagreement between small numbers visible while not calling
    two ways of computing zero a difference.
    """
    worst = 0.0
    for entry in found:
        match = _NUMBERS.match(entry)
        if match is None:
            return None
        try:
            left, right = float(match.group(2)), float(match.group(3))
        except ValueError:
            return None
        scale = max(abs(left), abs(right), 1.0)
        worst = max(worst, abs(left - right) / scale)
    return worst


def explain(job):
    reference, mine = job
    answer = {"reference": reference, "cna": mine}
    try:
        left, right = body(reference), body(mine)
    except Exception as error:  # noqa: BLE001 - a payload that will not decode is itself the answer
        answer["classification"] = "payload-unreadable"
        answer["detail"] = "%s: %s" % (type(error).__name__, error)
        return answer
    with open(reference, "rb") as handle:
        leftHeader = handle.read(6)
    with open(mine, "rb") as handle:
        rightHeader = handle.read(6)
    if leftHeader != rightHeader:
        answer["headerDiffers"] = "%s vs %s" % (leftHeader.hex(), rightHeader.hex())
    if left == right and leftHeader[:6] == rightHeader[:6]:
        answer["classification"] = "payload-identical"
        answer["detail"] = "the container's decompressed payload and header are byte for byte equal"
        return answer
    try:
        parsedLeft = X.parse(reference)
        parsedRight = X.parse(mine)
    except X.XnbError as error:
        answer["classification"] = "unparsed"
        answer["detail"] = str(error)[:400]
        return answer
    except Exception as error:  # noqa: BLE001
        answer["classification"] = "unparsed"
        answer["detail"] = "%s: %s" % (type(error).__name__, error)
        return answer
    found = differential.differences(parsedLeft, parsedRight)
    if not found:
        answer["classification"] = "semantically-identical"
        answer["detail"] = "the independent parser reads both to the same values"
        return answer
    # A difference every one of whose numbers agrees to within a few parts in ten million is one
    # side's float arithmetic taking a different order, not a different answer: a bounding sphere
    # computed over the same positions in a different order lands within a couple of ULPs. It is
    # reported as its own class rather than hidden -- the numbers and the worst relative distance
    # are in the record -- because "the same value" and "a value near it" are not the same claim.
    worst = float_distance(found)
    if worst is not None and worst < 1.0e-6:
        answer["classification"] = "float-tolerance"
        answer["detail"] = "every differing number agrees to %.3g of the larger magnitude" % worst
        answer["differences"] = found[:12]
        answer["differenceCount"] = len(found)
        answer["worstRelative"] = worst
        return answer
    answer["classification"] = "differs"
    answer["differences"] = found[:12]
    answer["differenceCount"] = len(found)
    return answer


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--map", required=True)
    parser.add_argument("--results", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--units", default=os.path.join(REPO, "build", "xna-sample-sweep", "out",
                                                        "units"))
    parser.add_argument("--jobs", type=int, default=3)
    args = parser.parse_args(argv)

    with open(args.map, encoding="utf-8") as handle:
        mapping = json.load(handle)
    with open(args.results, encoding="utf-8") as handle:
        results = json.load(handle)
    root = mapping["root"]

    jobs, keys = [], []
    for unit in results["results"]:
        for row in unit["rows"]:
            if row["result"] != "differs":
                continue
            relative = row["reference"][len(unit["outputRoot"]) + 1:]
            jobs.append((os.path.join(root, row["reference"]),
                         os.path.join(args.units, slug(unit["outputRoot"]),
                                      relative.replace("/", os.sep))))
            keys.append(row["reference"])

    answers = {}
    with concurrent.futures.ProcessPoolExecutor(max_workers=args.jobs) as pool:
        for key, answer in zip(keys, pool.map(explain, jobs, chunksize=4)):
            answers[key] = answer

    counts = collections.Counter(a["classification"] for a in answers.values())
    print(json.dumps(dict(counts), indent=1))
    shapes = collections.Counter()
    for answer in answers.values():
        if answer["classification"] != "differs":
            continue
        for difference in answer.get("differences", []):
            shapes[difference.split(":")[0]] += 1
    print("\nmost common differing fields:")
    for name, count in shapes.most_common(30):
        print("  %6d  %s" % (count, name))
    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump({"counts": dict(counts), "answers": answers}, handle, indent=1, sort_keys=True)
        handle.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
