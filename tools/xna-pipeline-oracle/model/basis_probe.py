#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-189: which basis matrix the `.x` importer multiplies by.

A converted `FrameTransformMatrix` differs from XNA's only in the sign of its zeros, and a sign of
zero is not something a value can show: `-0 == +0`, and .NET's `ToString` prints both as `0`. What
does show it is the arithmetic that produced it, so this measures the arithmetic. `--write` puts a
corpus of `FrameTransformMatrix` values whose entries are drawn from `{+0, -0, -1.5, +2.5}` in a
directory; run the genuine importer over it with `CNA_MODEL_ORACLE_BITS=1`, and `--fit` searches
every candidate for the basis matrix `B` such that the answer is `B M B`:

  * both orders the two multiplications can be written in, `(B M) B` and `B (M B)`;
  * both shapes a dot product's four terms can be added in, left to right and in pairs;
  * all 4,096 sign assignments of `B`'s twelve off-diagonal zeros.

Every value involved is exactly representable, so Python's own floats are the float32 arithmetic
and no rounding step is needed.

    python3 tools/xna-pipeline-oracle/model/basis_probe.py --write <dir> [--count 48] [--seed N]
    CNA_MODEL_FIXTURES=<dir> CNA_MODEL_ORACLE_BITS=1 \\
        tools/xna-pipeline-oracle/model/run-model-oracle.sh <out>
    python3 tools/xna-pipeline-oracle/model/basis_probe.py --fit <dir> <out>/model-import-oracle.json
"""
from __future__ import annotations

import argparse
import json
import os
import random
import re
import struct
import sys

ALPHABET = [0.0, -0.0, -1.5, 2.5]
OFF = [(i, j) for i in range(4) for j in range(4) if i != j]
DIAGONAL = [1.0, 1.0, -1.0, 1.0]

MESH = """  Mesh Point {
   3;
   0.000000;0.000000;5.000000;,
   1.000000;0.000000;5.000000;,
   0.000000;1.000000;5.000000;;
   1;
   3;0,1,2;;
  }
"""
FILE = """xof 0303txt 0032
// Written by tools/xna-pipeline-oracle/model/basis_probe.py; a probe, not a fixture.

Frame Root {
  FrameTransformMatrix {
%s;;
  }
%s}
"""


def bits(value: float) -> int:
    return struct.unpack("<I", struct.pack("<f", value))[0]


def from_bits(raw: int) -> float:
    return struct.unpack("<f", struct.pack("<I", raw))[0]


def basis(mask: int):
    """The candidate basis matrix: `diag(1, 1, -1, 1)` with one sign per off-diagonal zero."""
    matrix = [[0.0] * 4 for _ in range(4)]
    for i in range(4):
        matrix[i][i] = DIAGONAL[i]
    for bit, (i, j) in enumerate(OFF):
        matrix[i][j] = -0.0 if (mask >> bit) & 1 else 0.0
    return matrix


def multiply_sequential(a, b):
    out = [[0.0] * 4 for _ in range(4)]
    for i in range(4):
        for j in range(4):
            out[i][j] = ((a[i][0] * b[0][j] + a[i][1] * b[1][j]) + a[i][2] * b[2][j]) \
                + a[i][3] * b[3][j]
    return out


def multiply_paired(a, b):
    out = [[0.0] * 4 for _ in range(4)]
    for i in range(4):
        for j in range(4):
            out[i][j] = (a[i][0] * b[0][j] + a[i][1] * b[1][j]) \
                + (a[i][2] * b[2][j] + a[i][3] * b[3][j])
    return out


SHAPES = (("sequential", multiply_sequential), ("paired", multiply_paired))


def write(directory: str, count: int, seed: int) -> int:
    os.makedirs(directory, exist_ok=True)
    rng = random.Random(seed)
    manifest = []
    for index in range(count):
        values = [rng.choice(ALPHABET) if rng.random() < 0.72 else rng.choice([-1.5, 2.5])
                  for _ in range(16)]
        name = "basis_%03d" % index
        rows = ",\n".join("   " + ", ".join("%f" % values[r * 4 + c] for c in range(4))
                          for r in range(4))
        with open(os.path.join(directory, name + ".x"), "w", newline="\n") as handle:
            handle.write(FILE % (rows, MESH))
        manifest.append((name, values))
    with open(os.path.join(directory, "manifest.txt"), "w") as handle:
        for name, values in manifest:
            handle.write(name + " " + " ".join("0x%08X" % bits(v) for v in values) + "\n")
    print("wrote %d probes to %s" % (count, directory))
    return 0


def read_manifest(path: str):
    out = {}
    for line in open(path):
        parts = line.split()
        out[parts[0]] = [from_bits(int(p, 16)) for p in parts[1:]]
    return out


def read_oracle(path: str):
    out = {}
    for case in json.load(open(path))["cases"]:
        name = os.path.basename(case["case"])
        if not name.endswith(".x"):
            continue
        found = re.search(r"^/\w+ type=NodeContent transform=\[([^\]]+)\]",
                          case["result"], re.M)
        if found is None:
            continue
        raw = found.group(1).split()
        if not raw[0].startswith("0x"):
            raise SystemExit("the oracle was not run with CNA_MODEL_ORACLE_BITS=1")
        out[name[:-2]] = [from_bits(int(t, 16)) for t in raw]
    return out


def fit(directory: str, oracle: str) -> int:
    source = read_manifest(os.path.join(directory, "manifest.txt"))
    answered = read_oracle(oracle)
    names = sorted(n for n in source if n in answered)
    if not names:
        raise SystemExit("no probe in %s was answered in %s" % (directory, oracle))
    cases = [([[source[n][r * 4 + c] for c in range(4)] for r in range(4)],
              [[answered[n][r * 4 + c] for c in range(4)] for r in range(4)]) for n in names]
    print("%d matrices, %d entries" % (len(cases), len(cases) * 16))
    solutions = []
    for shape, multiply in SHAPES:
        for order in ("(B M) B", "B (M B)"):
            for mask in range(1 << 12):
                candidate = basis(mask)
                if all(all(bits(got[i][j]) == bits(want[i][j])
                           for i in range(4) for j in range(4))
                       for got, want in
                       (((multiply(multiply(candidate, m), candidate) if order == "(B M) B"
                          else multiply(candidate, multiply(m, candidate))), want)
                        for m, want in cases)):
                    solutions.append((shape, order, mask))
    print("solutions: %d" % len(solutions))
    for shape, order, mask in solutions:
        print("  %s, %s, mask 0x%03X" % (order, shape, mask))
        for row in basis(mask):
            print("      " + " ".join(("-0" if bits(v) == 0x80000000 else
                                       ("+0" if v == 0 else "%+g" % v)) for v in row))
    for solution in solutions[:1]:
        mask = solution[2]
        free = [(i, j) for bit, (i, j) in enumerate(OFF)
                if all(all(bits((multiply_paired(multiply_paired(basis(mask ^ (1 << bit)), m),
                                                 basis(mask ^ (1 << bit))))[i2][j2])
                           == bits(want[i2][j2]) for i2 in range(4) for j2 in range(4))
                       for m, want in cases)]
        print("signs no measured matrix constrains: %s" % (free or "none"))
    return 0 if solutions else 1


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--write", metavar="DIR")
    parser.add_argument("--count", type=int, default=48)
    parser.add_argument("--seed", type=int, default=20260909)
    parser.add_argument("--fit", nargs=2, metavar=("DIR", "ORACLE_JSON"))
    args = parser.parse_args(argv)
    if args.write:
        return write(args.write, args.count, args.seed)
    if args.fit:
        return fit(args.fit[0], args.fit[1])
    parser.error("one of --write or --fit is required")
    return 2


if __name__ == "__main__":
    sys.exit(main())
