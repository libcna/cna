#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-134/XNASWEEP-168: the point sets the BoundingSphere oracle runs.

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
                  sphere either grows it or does not, and the corpus's models are decided by it;
  * `span/*`   -- two axes whose extents have the same length and different squared lengths, which
                  is the only shape that says whether the widest axis is chosen by `Distance` or by
                  `DistanceSquared` (`XNASWEEP-168`).

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

    # `span/*` -- two axes whose extents are the same length and not the same squared length.
    #
    # The seed of the sphere is the widest axis, and "widest" can be read as the distance between
    # that axis's two extreme points or as the square of it. The two readings differ only where a
    # squared span rounds above another's and the square root of both rounds to the same float, and
    # a cylinder in the corpus does exactly that: its Y extremes are 6.2500005 apart squared and
    # its Z extremes 6.25, and 2.5 is the single-precision root of both. Each case here is that
    # shape deliberately -- a pair whose squared span is one ulp over L*L and whose length is
    # exactly L, and a second pair, on the later axis, whose span is exactly L both ways. Reading
    # the span squared seeds from the first pair; reading it as a length ties, and a tie goes to
    # the later axis, which seeds from the second. The two seeds are placed far enough apart that
    # the answers cannot be confused.
    for index, (dx, dy, dz, length) in enumerate([
            (1.5, 2.0, 0.000699999975040555, 2.5),
            (2.494291067123413, 0.1636705994606018, 0.041525036096572876, 2.5),
            (1.5381286144256592, 1.9704573154449463, 0.03819317743182182, 2.5),
            (9.201226234436035, 3.9115512371063232, 0.1928943544626236, 10.0),
            (7.428255081176758, 6.6943254470825195, 0.08389818668365479, 10.0),
            (39.24144744873047, 7.711873531341553, 0.7974483370780945, 40.0),
            (6.785238742828369, 39.418663024902344, 0.36000341176986694, 40.0)]):
        dx, dy, dz = f32(dx), f32(dy), f32(dz)
        wide = float(dx) * dx + float(dy) * dy + float(dz) * dz
        assert f32(wide) > f32(length * length), "the squared span must round above L*L"
        assert f32(math.sqrt(wide)) == f32(length), "and its root must be exactly L"
        near = [(f32(-dx / 2), f32(-dy / 2), f32(-dz / 2)), (f32(dx / 2), f32(dy / 2), f32(dz / 2))]
        # The exact pair sits on z, inside the near pair on x and y and outside it on z, and its
        # midpoint is nowhere near the near pair's, which is the origin.
        qx, qy = f32(dx / 4), f32(dy / 4)
        exact = [(qx, qy, f32(-length / 2 + length / 8)), (qx, qy, f32(length / 2 + length / 8))]
        assert f32(exact[1][2] - exact[0][2]) == f32(length)
        assert abs(exact[0][2]) > abs(near[0][2]) and abs(exact[1][2]) > abs(near[1][2])
        assert near[0][0] < qx < near[1][0] and near[0][1] < qy < near[1][1]
        out.append(("span/%02d" % index, near + exact))
        out.append(("span/%02d_reordered" % index, exact + near))
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
