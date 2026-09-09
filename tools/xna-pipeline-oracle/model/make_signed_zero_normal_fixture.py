#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-181: the sign of a zero in a `.x` normal.

`XNASWEEP-170` established that the `.x` basis change is a transform applied as
`((x*M1j) + (y*M2j)) + (z*M3j)` and that `M13` is a negative zero, but `x_normal_rules.x` writes
every zero as `+0` and therefore cannot see three of the six off-diagonal signs at all: a term
`z * M31` only shows its sign when the accumulation before it is already a negative zero, which
needs an `x` written `-0.000000`. SAMPLE-028's `Car.x` has eighty-seven such normals.

These normals separate the remaining candidates. Six of the eight sign patterns of an all-zero
vector are here as well, because the genuine importer answers `(+0, +0, +0)` for every one of them
rather than the accumulation's own signs. The last two entries are the folding question in one
place: `(-0, -1, 1)` and `(+0, -1, 1)` are equal as values and answer *differently*, so a file
carrying both says whether `XNASWEEP-148`'s fold onto the first entry holding the same three
floats collapses them.

    python3 tools/xna-pipeline-oracle/model/make_signed_zero_normal_fixture.py <directory>
"""
from __future__ import annotations

import os
import sys

NORMALS = [
    (-0.0, -0.697342, 0.716738),   # Car.x's own: the only shape that answers -0 in X
    (0.0, -0.697342, 0.716738),    # its positive-zero twin, which answers +0
    (-0.0, 1.0, 0.0),
    (-0.0, -1.0, -0.0),
    (0.0, -1.0, -0.0),
    (-0.0, -0.000001, -1.0),
    (0.0, -0.000001, -1.0),
    (0.0, -1.0, 0.0),              # XNASWEEP-170's own case: -0 in Z
    (-0.0, -0.0, -0.0),            # zero length, every sign negative
    (0.0, -0.0, 0.0),              # zero length, mixed
    (-0.0, -1.0, 1.0),             # the folding pair: this one answers -0 in X
    (0.0, -1.0, 1.0),              # and this one +0, from the same three float values
]

HEADER = '''xof 0303txt 0032
// Written by tools/xna-pipeline-oracle/model/make_signed_zero_normal_fixture.py for this repository.

Mesh normal_signed_zero {
'''


def main() -> int:
    directory = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(directory, exist_ok=True)
    count = len(NORMALS)
    faces = [(i, i + 1, i + 2) for i in range(0, count, 3)]
    text = HEADER
    text += " %d;\n" % count
    # One position per corner, all distinct, so that no two corners merge into one vertex and each
    # normal's answer is separately visible.
    for index in range(count):
        end = ";" if index + 1 < count else ";"
        text += "  %f;%f;%f;%s\n" % (float(index), float(index * 2), 0.0,
                                     "," if index + 1 < count else ";")
    text += " %d;\n" % len(faces)
    for at, face in enumerate(faces):
        text += "  3;%d,%d,%d;%s\n" % (face[0], face[1], face[2],
                                       "," if at + 1 < len(faces) else ";")
    text += " MeshNormals {\n"
    text += "  %d;\n" % count
    for index, normal in enumerate(NORMALS):
        text += "  %f;%f;%f;%s\n" % (normal[0], normal[1], normal[2],
                                     "," if index + 1 < count else ";")
    text += "  %d;\n" % len(faces)
    for at, face in enumerate(faces):
        text += "  3;%d,%d,%d;%s\n" % (face[0], face[1], face[2],
                                       "," if at + 1 < len(faces) else ";")
    text += " }\n}\n"
    path = os.path.join(directory, "x_normal_signed_zero.x")
    with open(path, "w", newline="\n") as handle:
        handle.write(text)
    print("wrote %s (%d bytes)" % (path, os.path.getsize(path)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
