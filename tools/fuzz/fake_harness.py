#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""A stand-in for a seeded harness, speaking tools/fuzz/seeded_harness.hpp's protocol.

Used only by test_run_long_campaign.py. FAKE_BEHAVIOUR picks what goes wrong and FAKE_SEED the
seed it goes wrong on; FAKE_FIXED=1 is the "repaired" build in which nothing does. A candidate is
its corpus entry followed by ``|seed=<seed>``, so a saved input carries the seed that made it and
``--replay-one`` fails exactly where ``--seeded`` did.
"""

import json
import os
import signal
import sys
import time
from pathlib import Path

BEHAVIOUR = os.environ.get("FAKE_BEHAVIOUR", "ok")
TRIGGER = int(os.environ.get("FAKE_SEED", "-1"))
FIXED = os.environ.get("FAKE_FIXED") == "1"
DELAY = float(os.environ.get("FAKE_DELAY", "0.002"))
stop = False
operations = applies = 0


def on_stop(_number, _frame):
    global stop
    stop = True


def emit(line):
    sys.stdout.write(line + "\n")
    sys.stdout.flush()


def candidate(corpus, seed):
    return corpus[seed % len(corpus)] + b"|seed=%d" % seed


def triggered(data):
    if FIXED:
        return False
    if BEHAVIOUR == "every-crash":
        return True
    if BEHAVIOUR == "every-fifth":
        return int(data.rsplit(b"|seed=", 1)[1]) % 5 == 0
    return data.endswith(b"|seed=%d" % TRIGGER)


def run(data):
    global operations, applies
    time.sleep(DELAY)
    if triggered(data):
        if BEHAVIOUR in ("crash", "every-crash", "every-fifth"):
            # every-crash fails differently each time; every-fifth is one defect met repeatedly.
            where = data.rsplit(b"|", 1)[1].decode() if BEHAVIOUR == "every-crash" else "run"
            sys.stderr.write(f"SUMMARY: AddressSanitizer: heap-buffer-overflow fake_harness.py in {where}\n")
            sys.stderr.flush()
            os.abort()
        if BEHAVIOUR == "timeout":
            time.sleep(600)
        if BEHAVIOUR == "rss":
            hoard = bytearray(b"\x01") * (256 << 20)
            time.sleep(600)
            del hoard
    if BEHAVIOUR != "zero-ops":
        operations += 3
    if BEHAVIOUR not in ("zero-ops", "no-applies"):
        applies += 1


def main(argv):
    mode = argv[1] if len(argv) > 1 else ""
    if BEHAVIOUR == "startup-fail":
        sys.stderr.write("fake: cannot open corpus\n")
        return 2
    if BEHAVIOUR == "capability":
        sys.stderr.write("fake: not qualified: FakeCapability unavailable on renderer STUB\n")
        return 3
    if mode == "--emit":
        seed, output, files = int(argv[2], 0), argv[3], argv[4:]
        corpus = [Path(path).read_bytes() for path in files]
        data = candidate(corpus, seed)
        Path(output).write_bytes(data)
        emit("cna_fuzz_emit " + json.dumps({"seed": seed, "corpus_index": seed % len(corpus), "bytes": len(data)}))
        return 0
    signal.signal(signal.SIGTERM, on_stop)
    signal.signal(signal.SIGINT, on_stop)
    if mode == "--seeded":
        first, count = int(argv[2], 0), int(argv[3], 0)
        corpus = [Path(path).read_bytes() for path in argv[4:]]
        seeds = range(first, first + count)
    elif mode == "--replay-one":
        corpus, seeds = [Path(argv[2]).read_bytes()], [None]
        first, count = 0, 1
    else:
        return 2
    capability = "SomethingElse" if BEHAVIOUR == "wrong-capability" else "FakeCapability on FAKE"
    emit("cna_fuzz_start " + json.dumps({"harness": "fake", "capability": capability}))
    attempts = 0
    for seed in seeds:
        if stop:
            break
        emit(f"cna_fuzz_begin {seed if seed is not None else 0}")
        run(corpus[0] if seed is None else candidate(corpus, seed))
        attempts += 1
        emit("cna_fuzz_progress " + json.dumps({"attempts": attempts,
                                                "counters": {"operations": operations, "applies": applies}}))
    reported = attempts - 1 if BEHAVIOUR == "mismatch" and attempts else attempts
    emit("cna_fuzz_result " + json.dumps({"attempts": reported, "stopped": attempts < count,
                                          "counters": {"operations": operations, "applies": applies}}))
    leaked = (triggered(corpus[0]) if mode == "--replay-one"
              else not FIXED and first <= TRIGGER < first + attempts)
    if (BEHAVIOUR == "leak" and leaked) or (BEHAVIOUR == "leak-always" and not FIXED):
        sys.stderr.write("==1==ERROR: LeakSanitizer: detected memory leaks\n"
                         "SUMMARY: AddressSanitizer: 8 byte(s) leaked in 1 allocation(s).\n")
        return 23
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
