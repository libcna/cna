#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-225: which bytes of an effect blob are the
compiler's own and which are not reproducible at all.

Two things in a compiled `fx_2_0` container are not a function of the source:

* the compiler's version string, which names the D3DX9 that produced it;
* the bytes between a string's null terminator and the four-byte boundary the container aligns to,
  which `d3dx9` leaves as whatever its heap held.

The second is not even a function of the *compiler*: the same source through the same compiler in
two runs answers different padding. This module finds both, so a comparison can require equality
everywhere else instead of accepting the blob whole.

    effect_mask.py <a.xnb> <b.xnb> [...]      print the bytes that differ and where they fall
    effect_mask.py --self-test <a> <b>        mutation-test the mask on one pair
"""
from __future__ import annotations

import os
import re
import sys

# `fx_2_0` little-endian, as the Windows compiler writes it, and the same word big-endian, which
# is what an Xbox 360 effect carries inside its own wrapper.
SIGNATURES = (b"\x01\x09\xff\xfe", b"\xfe\xff\x09\x01")
# Every compiler stamps its own name: D3DX9 writes the sentence, the Xbox compiler writes a bare
# version. Both are the compiler's identity rather than the source's.
VERSION = re.compile(rb"Microsoft \(R\) HLSL Shader Compiler [0-9][0-9.]*|[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+")


def blob(path):
    """The compiled effect inside an `.xnb`, and where it starts."""
    with open(path, "rb") as handle:
        data = handle.read()
    at = min((data.find(signature) for signature in SIGNATURES
              if data.find(signature) >= 0), default=-1)
    if at < 0:
        raise ValueError("%s holds no fx_2_0 container" % path)
    return data[at:], at


def version_spans(data):
    """Every compiler version string, as half-open byte ranges."""
    return [(match.start(), match.end()) for match in VERSION.finditer(data)]


def padding_spans(data):
    """The bytes between a printable string's null terminator and the next four-byte boundary.

    A container's strings are stored with a length and padded to four; the padding is what `d3dx9`
    does not clear. The scan is deliberately structural rather than a parse: a run of printable
    bytes followed by a null, with fewer than four bytes to the boundary after it.
    """
    spans = []
    at = 0
    while at < len(data):
        if 32 <= data[at] < 127:
            start = at
            while at < len(data) and 32 <= data[at] < 127:
                at += 1
            if at < len(data) and data[at] == 0 and at - start >= 3:
                end = at + 1
                aligned = (end + 3) & ~3
                if aligned > end:
                    spans.append((end, aligned))
                at = aligned
                continue
        at += 1
    return spans


def covered(spans, index):
    return any(start <= index < end for start, end in spans)


def compare(left_path, right_path):
    try:
        left, _ = blob(left_path)
        right, _ = blob(right_path)
    except ValueError as error:
        # An Xbox 360 effect is a different container entirely; the mask has nothing to say about
        # it and says so rather than pretending.
        return None, str(error)
    if len(left) != len(right):
        return None, "lengths differ: %d against %d" % (len(left), len(right))
    version = version_spans(left) + version_spans(right)
    padding = padding_spans(left) + padding_spans(right)
    out = {"differing": 0, "inVersion": 0, "inPadding": 0, "elsewhere": []}
    for index, (a, b) in enumerate(zip(left, right)):
        if a == b:
            continue
        out["differing"] += 1
        if covered(version, index):
            out["inVersion"] += 1
        elif covered(padding, index):
            out["inPadding"] += 1
        else:
            out["elsewhere"].append(index)
    return out, None


def self_test(left_path, right_path):
    """Mutation test: a byte changed *outside* the mask has to be seen.

    The mask is only worth having if it is narrow. This flips one byte of the blob at every
    position the mask does not cover -- one run per position, up to a bound -- and fails if any of
    them is still reported as agreeing.
    """
    import tempfile
    left, _ = blob(left_path)
    right, start = blob(right_path)
    if len(left) != len(right):
        return "lengths differ, so the mask cannot be tested on this pair"
    spans = version_spans(left) + version_spans(right) + padding_spans(left) + padding_spans(right)
    with open(right_path, "rb") as handle:
        whole = bytearray(handle.read())
    missed = []
    tested = 0
    for index in range(len(right)):
        if covered(spans, index):
            continue
        tested += 1
        if tested % 7 != 1:                     # every seventh position, so the run stays short
            continue
        mutated = bytearray(whole)
        mutated[start + index] ^= 0xFF
        with tempfile.NamedTemporaryFile(suffix=".xnb", delete=False) as handle:
            handle.write(bytes(mutated))
            path = handle.name
        try:
            result, error = compare(left_path, path)
            if error is None and not result["elsewhere"]:
                missed.append(index)
        finally:
            os.unlink(path)
    if missed:
        return "%d of %d mutations outside the mask were not seen: %s" % (
            len(missed), tested, missed[:8])
    return "ok: %d positions outside the mask, every mutation tried was seen" % tested


def main(argv):
    if len(argv) == 4 and argv[1] == "--self-test":
        message = self_test(argv[2], argv[3])
        print("%-44s %s" % (argv[2].split("/")[-1], message))
        return 0 if message.startswith("ok") else 1
    if len(argv) < 3:
        print(__doc__.strip())
        return 2
    for index in range(1, len(argv) - 1, 2):
        left, right = argv[index], argv[index + 1]
        result, error = compare(left, right)
        if error:
            print("%s vs %s: %s" % (left, right, error))
            continue
        print("%-44s %d differing, %d in the version string, %d in padding, %d elsewhere %s"
              % (left.split("/")[-1], result["differing"], result["inVersion"],
                 result["inPadding"], len(result["elsewhere"]),
                 result["elsewhere"][:12] if result["elsewhere"] else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
