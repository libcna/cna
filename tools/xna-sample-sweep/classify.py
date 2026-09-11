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


_SHARED_DIGEST = re.compile(r"^sharedResources\[(\d+)\]/digest: ")

_VERTEX_USAGE = {0: "POSITION", 1: "COLOR", 2: "TEXCOORD", 3: "NORMAL", 4: "BINORMAL",
                 5: "TANGENT", 6: "BLENDINDICES", 7: "BLENDWEIGHT", 8: "DEPTH", 9: "FOG",
                 10: "POINTSIZE", 11: "SAMPLE", 12: "TESSELLATEFACTOR"}
# Vertex element formats that are a run of 32-bit floats, and how many.
_VERTEX_FLOATS = {0: 1, 1: 2, 2: 3, 3: 4}


def buffer_differences(reference, mine, found):
    """Two differing buffer digests, reopened as the numbers they are.

    A vertex buffer reaches the report as one digest, so a model whose generated normals differ in
    their last bits is indistinguishable from one whose geometry is wrong: both say `digest: 'a' vs
    'b'` and nothing else. Nine references sat in `UNEXPLAINED` for that reason alone, and reading
    them showed every one to be a normal off in the mantissa -- worst 0.00073 of a degree -- which
    is what a sum of face normals taken in a different order does
    (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-210`).

    Only when *every* difference is a buffer digest, because a report that also disagrees about a
    bone or a bounding sphere is answered by those and not by this.

    @param reference The genuine container.
    @param mine CNA's.
    @param found The difference list the parses produced.
    @return A per-element difference list, or None when this does not apply.
    """
    indices = []
    for entry in found:
        match = _SHARED_DIGEST.match(entry)
        if match is None:
            return None
        indices.append(int(match.group(1)))
    try:
        left = X.parse(reference, keep_payloads=True)
        right = X.parse(mine, keep_payloads=True)
    except Exception:  # noqa: BLE001 - a payload that will not decode leaves the digests as found
        return None
    # The Xbox 360 target writes its containers big-endian, so a decoder that assumes little
    # will read a normal as a number with a huge exponent and report the position and texture
    # coordinate beside it as differing too. Two references read that way and looked like a
    # mechanism of their own; read the right way round they are the same one-to-two-ulp normal
    # rounding as their Windows twins (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-219`).
    order = ">" if left.get("platform") == "x" else "<"
    expanded = []
    for index in indices:
        try:
            one, two = left["sharedResources"][index], right["sharedResources"][index]
        except (KeyError, IndexError):
            return None
        if "payload" not in one or "payload" not in two:
            return None
        if one["reader"] != two["reader"] or len(one["payload"]) != len(two["payload"]):
            return None
        if "VertexBufferReader" in one["reader"]:
            if one["declaration"] != two["declaration"]:
                return None
            stride = one["declaration"]["stride"]
            for element in one["declaration"]["elements"]:
                count = _VERTEX_FLOATS.get(element["format"])
                if count is None:
                    # A packed element -- a colour, a normalized short. Reading it as a number
                    # would invent a scale it has not got, so the digest stands.
                    if _element_differs(one["payload"], two["payload"], stride, element, 4):
                        return None
                    continue
                name = "%s%d" % (_VERTEX_USAGE.get(element["usage"], str(element["usage"])),
                                 element["usageIndex"])
                width = 4 * count
                for vertex in range(one["vertexCount"]):
                    at = stride * vertex + element["offset"]
                    a = struct.unpack("%s%df" % (order, count), one["payload"][at:at + width])
                    b = struct.unpack("%s%df" % (order, count), two["payload"][at:at + width])
                    for lane in range(count):
                        if a[lane] != b[lane]:
                            expanded.append("sharedResources[%d]/vertices[%d]/%s[%d]: %r vs %r"
                                            % (index, vertex, name, lane, a[lane], b[lane]))
        elif "IndexBufferReader" in one["reader"]:
            width = one["indexElementSize"]
            code = ("%sH" if width == 2 else "%sI") % order
            for position in range(one["indexCount"]):
                at = width * position
                a = struct.unpack(code, one["payload"][at:at + width])[0]
                b = struct.unpack(code, two["payload"][at:at + width])[0]
                if a != b:
                    expanded.append("sharedResources[%d]/indices[%d]: %d vs %d"
                                    % (index, position, a, b))
        else:
            return None
    return expanded or None


def _element_differs(left, right, stride, element, width):
    """Whether one vertex element's bytes differ anywhere in two equal-length buffers."""
    for at in range(element["offset"], len(left), stride):
        if left[at:at + width] != right[at:at + width]:
            return True
    return False


def unit_directory(units, unit):
    """The directory `sweep.py` wrote this unit's output into.

    A root written by more than one `BuildContent` call gets one directory per call, the later
    ones suffixed with the project that made them (`XNASWEEP-163`). Deriving the name here from
    the output root alone dropped that suffix, so SAMPLE-031's two diagnostic fonts were looked
    for in the *other* unit's directory, came back `payload-unreadable` -- CNA's file not found --
    and were then handed the sprite-font accepted reason by `taxonomy.py` without any comparison
    having happened (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-236`).

    `sweep.py` records the directory it used, so no rule is re-derived. The search below is only
    for results written before it did; it tries the two names that rule can produce and nothing
    else, and a run whose results carry `outputDir` never reaches it.
    """
    named = unit.get("outputDir")
    if named:
        return os.path.join(units, named)
    plain = os.path.join(units, slug(unit["outputRoot"]))
    if _holds_any(plain, unit):
        return plain
    suffixed = plain + "__" + slug(os.path.basename(unit["project"]))
    return suffixed if _holds_any(suffixed, unit) else plain


def _holds_any(directory, unit):
    """Whether `directory` holds an output for any asset this unit's rows name."""
    if not os.path.isdir(directory):
        return False
    for row in unit["rows"]:
        asset = row.get("asset")
        if not asset:
            continue
        if os.path.exists(os.path.join(directory, (asset + ".xnb").replace("/", os.sep))):
            return True
    return False


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
    # A buffer reaches the parse as a digest; when that is the only thing that differs, it is
    # reopened as the numbers it holds so the tolerance below can judge them.
    expanded = buffer_differences(reference, mine, found)
    if expanded is not None:
        found = expanded
    worst = float_distance(found)
    if worst is not None and worst < 1.0e-6:
        answer["classification"] = "float-tolerance"
        answer["detail"] = "every differing number agrees to %.3g of the larger magnitude" % worst
        answer["differences"] = found[:12]
        answer["differenceCount"] = len(found)
        answer["differenceFields"] = difference_fields(found)
        answer["worstRelative"] = worst
        return answer
    answer["classification"] = "differs"
    answer["differences"] = found[:12]
    answer["differenceCount"] = len(found)
    answer["differenceFields"] = difference_fields(found)
    return answer



def difference_fields(found):
    """The distinct field paths a difference list touches, with indices stripped.

    The listed differences are capped at twelve, which is enough to read and not enough to prove
    anything: a gate asking "does everything that differs belong to the reason this was accepted
    under" cannot answer from a truncated list. This is the whole set, and it stays small because
    an index is not a field -- 501 differing glyph rectangles are one path
    (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-204`).
    """
    fields = set()
    for one in found:
        path = one.split(":", 1)[0].strip()
        # A level digest keeps its index, because *which* level differs is the whole of what
        # separates a mip filter's dither from a base image that is simply not the same.
        if "levelDigests[" in path:
            fields.add(re.sub(r"(?<!levelDigests)\[\d+\]", "[]", path))
        else:
            fields.add(re.sub(r"\[\d+\]", "[]", path))
    return sorted(fields)


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
            # `sweep.py` already resolved which of CNA's outputs this reference was compared
            # with, and records it in `asset` -- which is not always the reference's own spelling.
            # The corpus has one `bin/Content/textures/` where CNA writes a `Textures/` the project
            # authored and a `textures/` a model's nested output authored, and deriving the path
            # from the reference again here looked in the wrong one of the two: 60 of Spacewar's
            # references came back `payload-unreadable` and were counted `UNEXPLAINED`
            # (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-184`, `XNASWEEP-191`).
            relative = row.get("asset")
            relative = (relative + ".xnb") if relative else \
                row["reference"][len(unit["outputRoot"]) + 1:]
            jobs.append((os.path.join(root, row["reference"]),
                         os.path.join(unit_directory(args.units, unit),
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
