#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-236: can a real difference hide inside
`SEMANTICALLY_IDENTICAL`?

`taxonomy.py` puts a reference there for one of three reasons, and each is a different claim:

  * `payload-identical` -- the containers differ but their decompressed payloads and headers are
    byte for byte equal. 729 references.
  * `semantically-identical` -- the payloads differ and the independent parser reads both to the
    same values. 5 references.
  * `float-tolerance` -- the parser reads different numbers and every one of them agrees to less
    than 1e-6 of the larger magnitude. 18 references, and `tolerance_mutation.py` is that claim's
    own test, for the reason recorded beside `KINDS` below.

The first two had no test at all until `XNASWEEP-236` found two references sitting in
`ACCEPTED_DIFFERENCE` with nothing behind them, which is what an unaudited class costs. This is
the test: change one byte of CNA's side and ask the classifier again. A mutation that still comes
back under the same claim is a hole.

`payload-identical` is also *re-derived* rather than only mutated, because it is the strongest of
the three claims and the cheapest to check independently: both sides are decompressed here and
compared byte for byte, without going through `explain()` at all. A claim that the tool makes and
the tool alone can check is not a measurement.

    semantic_mutation.py --classified <classified.json> [--limit N] [--kinds a,b]
"""
from __future__ import annotations

import argparse
import collections
import concurrent.futures
import json
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import classify  # noqa: E402  (path is set above on purpose)

# The two claims a changed byte can falsify. `float-tolerance` is deliberately *not* here, and
# the measurement that settled it is worth keeping: flipping byte 13,546 of SAMPLE-055's
# `baseballbat.xnb` leaves it `float-tolerance`, correctly -- `0x7f ^ 0xff` is `0x80`, one unit of
# last place in that normal's mantissa, and the worst relative distance moves from 1.19e-07 to
# 1.59e-07, which is still what the class claims. A byte flip's strength depends on the byte it
# lands on, so it cannot test a threshold; `tolerance_mutation.py` changes the *number* by a
# relative 1e-5 and 1e-2 instead, which is the right instrument and is that claim's own test.
# Pass `--kinds float-tolerance` to run it here anyway; what it measures is coverage, not a hole.
KINDS = ("payload-identical", "semantically-identical")


def mutate(data, at):
    """One byte changed, never to itself."""
    out = bytearray(data)
    out[at] ^= 0xFF
    return bytes(out)


def positions(size):
    """Where to change a byte: after the 14-byte header, spread across the payload.

    The header is left alone deliberately. A header change is seen by a comparison that never
    looks at the payload at all, so it would pass this test without proving anything about it.
    """
    span = size - 14
    if span <= 0:
        return []
    return sorted({14 + (span * fraction) // 8 for fraction in (1, 3, 5, 7)} - {size - 1})


def reclassify(reference, path):
    """`classify.py`'s own answer for one pair, with nothing else changed."""
    return classify.explain((reference, path)).get("classification")


def payload_equal(reference, mine):
    """Decompress both sides here and compare, rather than believing the classifier."""
    try:
        return classify.body(reference) == classify.body(mine)
    except Exception as error:  # noqa: BLE001 - a payload that will not decode is the answer
        return "unreadable: %s" % type(error).__name__


def one_reference(job):
    """Every mutation of one reference, as (mutations, holes, payloads-that-are-not-equal)."""
    kind, reference, mine = job
    if not os.path.exists(mine) or not os.path.exists(reference):
        return 0, [(kind, reference, "one side is not on disk")], []
    wrong = []
    if kind == "payload-identical":
        verdict = payload_equal(reference, mine)
        if verdict is not True:
            wrong.append((reference, verdict))
    with open(mine, "rb") as handle:
        original = handle.read()
    scratch = tempfile.mkdtemp(prefix="xnasweep-semantic-")
    holes, done = [], 0
    try:
        copy = os.path.join(scratch, "mutated.xnb")
        for at in positions(len(original)):
            with open(copy, "wb") as handle:
                handle.write(mutate(original, at))
            done += 1
            if reclassify(reference, copy) == kind:
                holes.append((kind, reference, "byte %d changed and it is still %s" % (at, kind)))
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    return done, holes, wrong


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--classified", required=True)
    parser.add_argument("--limit", type=int, default=12,
                        help="references per kind (0 for all)")
    parser.add_argument("--kinds", default=",".join(KINDS))
    parser.add_argument("--jobs", type=int, default=8)
    args = parser.parse_args(argv)

    wanted = tuple(one.strip() for one in args.kinds.split(",") if one.strip())
    with open(args.classified, encoding="utf-8") as handle:
        answers = json.load(handle)["answers"]

    # The dictionary is keyed by the reference's path *relative to the corpus root*; the absolute
    # one the classifier actually opened is in the answer, along with the CNA output it was
    # compared with. Use those, so this tool reads exactly the pair `classify.py` read.
    by_kind = collections.defaultdict(list)
    for answer in sorted(answers.values(), key=lambda one: one.get("reference", "")):
        if answer.get("classification") in wanted:
            by_kind[answer["classification"]].append((answer["reference"], answer["cna"]))

    mutations = detected = 0
    holes, unequal = [], []
    for kind in wanted:
        chosen = by_kind.get(kind, [])
        if args.limit:
            chosen = chosen[:args.limit]
        print("=== %s: %d of %d reference(s) ===" % (kind, len(chosen), len(by_kind.get(kind, []))),
              flush=True)
        jobs = [(kind, reference, mine) for reference, mine in chosen]
        with concurrent.futures.ProcessPoolExecutor(max_workers=args.jobs) as pool:
            for done, bad, wrong in pool.map(one_reference, jobs, chunksize=2):
                mutations += done
                detected += done - len(bad)
                holes += bad
                unequal += wrong
        print("  %d mutation(s) so far, %d seen" % (mutations, detected), flush=True)

    print()
    print("=== %d mutation(s), %d seen, %d hole(s) ===" % (mutations, detected, len(holes)))
    for kind, reference, why in holes[:20]:
        print("  %-24s %s: %s" % (kind, reference.split("/rv/tmp/samples/")[-1], why))
    if unequal:
        print("=== %d payload-identical reference(s) whose payloads are NOT equal ===" % len(unequal))
        for reference, verdict in unequal[:20]:
            print("  %s: %s" % (reference.split("/rv/tmp/samples/")[-1], verdict))
    return 1 if (holes or unequal) else 0


if __name__ == "__main__":
    raise SystemExit(main())
