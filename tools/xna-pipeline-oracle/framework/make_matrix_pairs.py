#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-172: the matrix cases the Matrix oracle runs.

A bone's transform in a built model is a product of matrices, and the corpus's remaining model
differences are in entries a rotation leaves near zero. Whether those entries come out as zero or
as 3.5e-15 is decided by where the dot product rounds, so the cases here are the shapes that
separate the candidates:

  * `random/*`      -- ordinary products, where any reasonable arithmetic agrees;
  * `cancel/*`      -- products whose entries cancel to a residue: a rotation composed with its own
                       inverse, and the conjugation `A^-1 T A` a scene transform performs, which is
                       the shape SAMPLE-041's `terrain.fbx` actually has;
  * `rotation/*`    -- `CreateRotationX/Y/Z` of the angles a quarter and a half turn reach through
                       `MathHelper.ToRadians`, where cos is a residue rather than zero;
  * `toradians/*`   -- the conversion itself, which decides what those angles are;
  * `invert/*`      -- the inverse a scene transform takes before it conjugates;
  * `transform/*`   -- a position and a normal through a matrix, which is how a mesh moves.

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


def words(matrix):
    return " ".join(word(v) for v in matrix)


def identity():
    return [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0]


def rotation_x(radians):
    m = identity()
    c, s = f32(math.cos(radians)), f32(math.sin(radians))
    m[5], m[6], m[9], m[10] = c, s, -s, c
    return m


def rotation_y(radians):
    m = identity()
    c, s = f32(math.cos(radians)), f32(math.sin(radians))
    m[0], m[2], m[8], m[10] = c, -s, s, c
    return m


def rotation_z(radians):
    m = identity()
    c, s = f32(math.cos(radians)), f32(math.sin(radians))
    m[0], m[1], m[4], m[5] = c, s, -s, c
    return m


def scale(sx, sy, sz):
    m = identity()
    m[0], m[5], m[10] = f32(sx), f32(sy), f32(sz)
    return m


def multiply(a, b):
    """A single-precision product, only to build further inputs -- never an expected answer."""
    out = [0.0] * 16
    for i in range(4):
        for j in range(4):
            total = 0.0
            for k in range(4):
                total = f32(total + f32(a[i * 4 + k] * b[k * 4 + j]))
            out[i * 4 + j] = total
    return out


def to_radians(degrees):
    return f32(degrees * f32(0.017453292519943295))


def cases():
    random.seed(20260909)
    out = []

    for index in range(120):
        a = [f32(random.uniform(-10, 10)) for _ in range(16)]
        b = [f32(random.uniform(-10, 10)) for _ in range(16)]
        out.append(("random/%03d" % index, "multiply", a + b))

    # Entries that cancel. A rotation against its own inverse leaves the residue on the diagonal;
    # the conjugation a scene transform performs leaves it wherever the two do not commute.
    builders = [("x", rotation_x), ("y", rotation_y), ("z", rotation_z)]
    turns = [("quarter", 90.0), ("half", 180.0), ("negquarter", -90.0), ("third", 120.0)]
    index = 0
    for axis, build in builders:
        for turn, degrees in turns:
            radians = to_radians(degrees)
            forward = build(radians)
            back = build(to_radians(-degrees))
            out.append(("cancel/%s_%s_inverse" % (axis, turn), "multiply", forward + back))
            unit = scale(2.54, 2.54, 2.54)
            body = multiply(forward, unit)
            out.append(("cancel/%s_%s_conjugate" % (axis, turn), "multiply",
                        multiply(back, body) + forward))
            index += 1

    # A rotation of an angle that is not a nice number, so nothing cancels exactly.
    for index in range(24):
        degrees = f32(random.uniform(-360, 360))
        radians = to_radians(degrees)
        out.append(("cancel/random_%02d" % index, "multiply",
                    rotation_x(radians) + rotation_z(to_radians(f32(random.uniform(-360, 360))))))

    for axis, _ in builders:
        for turn, degrees in turns + [("zero", 0.0), ("full", 360.0)]:
            out.append(("rotation/%s_%s" % (axis, turn), "rotation" + axis, [to_radians(degrees)]))
    for index in range(24):
        degrees = f32(random.uniform(-360, 360))
        out.append(("rotation/x_random_%02d" % index, "rotationx", [to_radians(degrees)]))

    # The inverse a scene transform takes, and the two ways a vector goes through a matrix.
    for index in range(40):
        m = multiply(rotation_x(to_radians(f32(random.uniform(-360, 360)))),
                     scale(f32(random.uniform(0.01, 100)), f32(random.uniform(0.01, 100)),
                           f32(random.uniform(0.01, 100))))
        out.append(("invert/%02d" % index, "invert", m))
    for axis, build in builders:
        for turn, degrees in turns:
            out.append(("invert/%s_%s" % (axis, turn), "invert",
                        multiply(build(to_radians(degrees)), scale(2.54, 2.54, 2.54))))

    for index in range(60):
        v = [f32(random.uniform(-100, 100)) for _ in range(3)] + [0.0] * 13
        m = multiply(rotation_y(to_radians(f32(random.uniform(-360, 360)))),
                     scale(f32(random.uniform(0.01, 100)), f32(random.uniform(0.01, 100)),
                           f32(random.uniform(0.01, 100))))
        m[12], m[13], m[14] = f32(random.uniform(-50, 50)), f32(random.uniform(-50, 50)), f32(random.uniform(-50, 50))
        out.append(("transform/%03d" % index, "transform3", v + m))
        out.append(("transformnormal/%03d" % index, "transformnormal3", v + m))

    for degrees in (0.0, 1.0, 45.0, 90.0, 180.0, -90.0, -180.0, 360.0, 0.1, 1e-7, 1e7):
        out.append(("toradians/%s" % repr(degrees).replace(".", "p").replace("-", "neg"),
                    "toradians", [f32(degrees)]))
        out.append(("todegrees/%s" % repr(degrees).replace(".", "p").replace("-", "neg"),
                    "todegrees", [f32(degrees)]))
    for index in range(20):
        angle = f32(random.uniform(-720, 720))
        out.append(("toradians/random_%02d" % index, "toradians", [angle]))
        out.append(("todegrees/random_%02d" % index, "todegrees", [angle]))
    return out


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", required=True)
    arguments = parser.parse_args(argv)
    lines = []
    for name, op, values in cases():
        lines.append("case " + name)
        lines.append("op " + op)
        for start in range(0, len(values), 16):
            lines.append("values " + words(values[start:start + 16]))
    with open(arguments.out, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")
    print("%s: %d cases" % (arguments.out, sum(1 for line in lines if line.startswith("case "))))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
