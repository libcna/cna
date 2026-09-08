#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-134: the point sets the BoundingSphere oracle runs.

`BoundingSphere.CreateFromPoints` is an iterative sphere-growing pass, so the only way to pin what
it does is to hand the genuine framework point sets whose shapes separate the candidate rules and
compare bit for bit. Four families do that:

  * `seed/*`   -- two points, so the answer is the seed and nothing else, except where a rounding
                  ulp puts one of the two outside its own sphere and the growth pass runs anyway;
  * `grow/*`   -- three points whose third is far outside, so the seed is the widest pair of the
                  three and one growth follows;
  * `step/*`   -- an exact seed (-1024,0,0)/(1024,0,0), centre and radius representable without
                  rounding, and one point outside it, so a single growth step is isolated;
  * `box/*`    -- a box whose corners are listed with duplicates, the shape a mesh's control-point
                  list actually has, over growing prefixes: a point that lands exactly on the
                  sphere either grows it or does not, and the corpus's models are decided by it.

Deterministic: the same seed produces the same file. Written by hand into the repository, not taken
from any sample's content.
"""
from __future__ import annotations

import argparse
import math
import random
import struct


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def word(value):
    return "%08X" % struct.unpack("<I", struct.pack("<f", value))[0]


def cases():
    random.seed(20260908)
    out = []

    for index in range(200):
        a = tuple(f32(random.uniform(-50, 50)) for _ in range(3))
        b = tuple(f32(random.uniform(-50, 50)) for _ in range(3))
        out.append(("seed/%03d" % index, [a, b]))

    for index in range(60):
        a = (f32(-random.uniform(1, 5)), f32(random.uniform(-1, 1)), f32(random.uniform(-1, 1)))
        b = (f32(random.uniform(1, 5)), f32(random.uniform(-1, 1)), f32(random.uniform(-1, 1)))
        c = (f32(random.uniform(-1, 1)), f32(random.uniform(5, 20)), f32(random.uniform(-9, 9)))
        out.append(("grow/%02d" % index, [a, b, c]))

    index = 0
    while index < 150:
        c = tuple(f32(random.uniform(-4000, 4000)) for _ in range(3))
        if math.sqrt(sum(x * x for x in c)) <= 1100:
            continue
        if max(abs(x) for x in c) > 1024:
            continue
        out.append(("step/%03d" % index, [(-1024.0, 0.0, 0.0), (1024.0, 0.0, 0.0), c]))
        index += 1

    # A box whose two x faces are a hair apart, listed the way an exporter lists polygon corners:
    # every corner repeated, so the growth pass meets points already on the sphere.
    lowX, highX = f32(-20.29377937), f32(20.29377555)
    lowY, highY = f32(-0.41667652), f32(2.66667652)
    lowZ, highZ = f32(-20.37752342), f32(20.37752342)
    corners = []
    for z in (highZ, lowZ):
        for x in (lowX, highX):
            for y in (highY, lowY):
                corners.append((x, y, z))
    duplicated = []
    for step in range(6):
        duplicated.extend(corners[(step * 2) % 8:(step * 2) % 8 + 4])
    for length in range(2, len(duplicated) + 1):
        out.append(("box/%02d" % length, duplicated[:length]))
    return out


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", required=True)
    arguments = parser.parse_args(argv)
    lines = []
    for name, points in cases():
        lines.append("case " + name)
        for point in points:
            lines.append("point " + " ".join(word(value) for value in point))
    with open(arguments.out, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")
    print("%s: %d cases" % (arguments.out, sum(1 for line in lines if line.startswith("case "))))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
