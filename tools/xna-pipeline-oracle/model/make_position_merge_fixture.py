#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-173: what a `.x` mesh's position list holds.

Spacewar's models declare more vertices than they have different positions -- `p1_bfg.x` declares
5,299 and holds 3,815 -- and the genuine importer answers the smaller number, so the list is the
*distinct* positions and a vertex names one of them. Nothing about the vertex buffer depends on it;
the mesh's bounding sphere does, because it is computed over that list and a position listed twice
moves the growth pass through the same point twice.

The fixture declares eight vertices over five different positions, arranged so that the answer says
more than the count: the repeats are not adjacent, one of them is the first position repeated last,
two vertices that share a position carry *different* normals (so a merged position must not merge
the vertices), and two more share both. A face names every vertex, so nothing is dropped for being
unreferenced.

    python3 tools/xna-pipeline-oracle/model/make_position_merge_fixture.py <directory>
"""
from __future__ import annotations

import os
import sys

# Eight vertices, five distinct positions: 0 and 5 are the same, 1 and 3 are the same, and 7
# repeats the first again.
POSITIONS = [
    (0.0, 0.0, 0.0),
    (10.0, 0.0, 0.0),
    (0.0, 10.0, 0.0),
    (10.0, 0.0, 0.0),
    (0.0, 0.0, 10.0),
    (0.0, 0.0, 0.0),
    (-6.0, -7.0, -8.0),
    (0.0, 0.0, 0.0),
]
# Vertices 0 and 5 share a position and carry different normals; 1 and 3 share both.
NORMALS = [
    (1.0, 0.0, 0.0),
    (0.0, 1.0, 0.0),
    (0.0, 0.0, 1.0),
    (0.0, 1.0, 0.0),
    (-1.0, 0.0, 0.0),
    (0.0, -1.0, 0.0),
    (0.0, 0.0, -1.0),
    (1.0, 0.0, 0.0),
]
FACES = [(0, 1, 2), (3, 4, 5), (6, 7, 0)]

HEADER = '''xof 0303txt 0032
// Written by tools/xna-pipeline-oracle/model/make_position_merge_fixture.py for this repository.

Mesh position_merge {
'''


def main() -> int:
    directory = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(directory, exist_ok=True)
    text = HEADER
    text += " %d;\n" % len(POSITIONS)
    for i, p in enumerate(POSITIONS):
        text += "  %f;%f;%f;%s\n" % (p[0], p[1], p[2], ";" if i + 1 == len(POSITIONS) else ",")
    text += " %d;\n" % len(FACES)
    for k, face in enumerate(FACES):
        text += "  3;%d,%d,%d;%s\n" % (face + ((";" if k + 1 == len(FACES) else ","),))
    text += " MeshNormals {\n  %d;\n" % len(NORMALS)
    for i, n in enumerate(NORMALS):
        text += "  %f;%f;%f;%s\n" % (n[0], n[1], n[2], ";" if i + 1 == len(NORMALS) else ",")
    text += "  %d;\n" % len(FACES)
    for k, face in enumerate(FACES):
        text += "  3;%d,%d,%d;%s\n" % (face + ((";" if k + 1 == len(FACES) else ","),))
    text += " }\n}\n"
    path = os.path.join(directory, "x_position_merge.x")
    with open(path, "w", newline="\n") as handle:
        handle.write(text)
    print("wrote %s (%d vertices, %d different positions)"
          % (path, len(POSITIONS), len(set(POSITIONS))))

    # The order the list is built in. Two materials, and the faces of the *second* one name the
    # lower-numbered vertices: a mesh whose positions were deduplicated in file order would answer
    # them in the file's order, and the genuine importer answers the first material's first --
    # each batch adding what its own faces name, in ascending file order. SAMPLE-014's `p2_dual.x`
    # is this shape at scale, and its list is a permutation of the file's with two descents.
    batched = [
        (0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0),      # named by the second material
        (5.0, 0.0, 0.0), (6.0, 0.0, 0.0), (5.0, 1.0, 0.0),      # named by the first
    ]
    text = HEADER.replace("Mesh position_merge", "Mesh position_batches")
    text += " %d;\n" % len(batched)
    for i, p in enumerate(batched):
        text += "  %f;%f;%f;%s\n" % (p[0], p[1], p[2], ";" if i + 1 == len(batched) else ",")
    text += " 2;\n  3;3,4,5;,\n  3;0,1,2;;\n"
    text += " MeshMaterialList {\n  2;\n  2;\n  0,\n  1;;\n"
    for name, colour in (("First", 0.25), ("Second", 0.75)):
        text += ("  Material %s {\n   %f;%f;%f;1.000000;;\n   8.000000;\n"
                 "   0.100000;0.200000;0.300000;;\n   0.010000;0.020000;0.030000;;\n  }\n"
                 % (name, colour, colour, colour))
    text += " }\n"
    text += " MeshNormals {\n  %d;\n" % len(batched)
    for i in range(len(batched)):
        text += "  0.000000;0.000000;1.000000;%s\n" % (";" if i + 1 == len(batched) else ",")
    text += " 2;\n  3;3,4,5;,\n  3;0,1,2;;\n }\n}\n"
    path = os.path.join(directory, "x_position_batches.x")
    with open(path, "w", newline="\n") as handle:
        handle.write(text)
    print("wrote %s (two materials, the second naming the lower vertices)" % path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
