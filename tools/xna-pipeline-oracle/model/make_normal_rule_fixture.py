#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-170: what a `.x` `MeshNormals` entry becomes.

One mesh, one normal per vertex, chosen so that every part of the rule is separately observable:
the six axis directions expose the sign of a zero the basis change leaves behind, the non-unit
vectors expose whether the importer normalizes and in which direction, and the near-unit ones --
taken from SAMPLE-014's own models -- expose the precision the normalization is done in, which is
where a float sum of squares and a double one part company.

    python3 tools/xna-pipeline-oracle/model/make_normal_rule_fixture.py <directory>
"""
from __future__ import annotations

import os
import sys

NORMALS = [
    (1.0, 0.0, 0.0),            # axis: the zero columns
    (-1.0, 0.0, 0.0),
    (0.0, 1.0, 0.0),
    (0.0, -1.0, 0.0),           # the one case that comes back with a negative zero
    (0.0, 0.0, 1.0),
    (0.0, 0.0, -1.0),
    (2.0, 0.0, 0.0),            # non-unit, twice as long
    (0.5, 0.0, 0.0),            # non-unit, half as long
    (0.6, 0.0, 0.8),            # unit, and exactly so
    (0.855686, 0.0, 0.517496),  # SAMPLE-014's own: 4.2e-7 longer than unit
    (0.855685, 0.0, -0.517497), # its sibling: 4.5e-7 shorter
    (1.0, 1.0, 1.0),            # oblique and non-unit
    (0.0, 0.0, 0.0),            # zero length
    (-0.9724069, 0.2307520, 0.0343190),   # a real one whose float and double sums differ
]

HEADER = '''xof 0303txt 0032
// Written by tools/xna-pipeline-oracle/model/make_normal_rule_fixture.py for this repository.

Mesh normal_rules {
'''


def main() -> int:
    directory = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(directory, exist_ok=True)
    count = len(NORMALS)
    faces = [(i, (i + 1) % count, (i + 2) % count) for i in range(0, count, 3)]
    text = HEADER
    text += " %d;\n" % count
    # The positions are the same fourteen vectors, so the same fixture answers the same question
    # of the *position* conversion: a position at `(0, -1, 0)` is where a plain negation and a
    # matrix multiply part company on the sign of the zero it leaves in Z.
    for i, n in enumerate(NORMALS):
        text += "  %f;%f;%f;%s\n" % (n[0], n[1], n[2], ";" if i + 1 == count else ",")
    text += " %d;\n" % len(faces)
    for k, face in enumerate(faces):
        text += "  3;%d,%d,%d;%s\n" % (face[0], face[1], face[2],
                                       ";" if k + 1 == len(faces) else ",")
    text += " MeshNormals {\n"
    text += "  %d;\n" % count
    for i, n in enumerate(NORMALS):
        text += "  %f;%f;%f;%s\n" % (n[0], n[1], n[2], ";" if i + 1 == count else ",")
    text += "  %d;\n" % len(faces)
    for k, face in enumerate(faces):
        text += "  3;%d,%d,%d;%s\n" % (face[0], face[1], face[2],
                                       ";" if k + 1 == len(faces) else ",")
    text += " }\n"
    text += "}\n"
    path = os.path.join(directory, "x_normal_rules.x")
    with open(path, "w", newline="\n") as handle:
        handle.write(text)
    print("wrote %s (%d normals, %d faces)" % (path, count, len(faces)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
