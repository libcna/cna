#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""CNA-FIX-XVERT-01: which declared normal a corner gets when its triangle names one vertex twice.

`x_face_degenerate_normals.x` keeps its face `(3,4,3)` with the two normals at vertex 3 exchanged.
Each probe here is one polygon whose corner normals are all different unit directions, so every
corner the genuine importer answers can be told apart by its position and its normal:

  `x_corner_normals_pair01.x`    one triangle naming its vertex at corners 0 and 1
  `x_corner_normals_pair02.x`    the same at corners 0 and 2, the shape of the original fixture
  `x_corner_normals_pair12.x`    the same at corners 1 and 2; its two normal indices are in
                                 ascending table order but descending value order, so the
                                 exchange cannot be a sort by either
  `x_corner_normals_quad.x`      a quad naming one vertex at corners 0 and 2: both fan triangles
                                 hold the pair
  `x_corner_normals_heptagon.x`  a heptagon naming one vertex at corners 0, 2, 4 and 6, so each of
                                 its five fan triangles holds a different pair of those four
                                 normals: an exchange per triangle, not a permutation of the four
  `x_corner_normals_one_vertex.x` a quad naming one vertex four times with four normals: a
                                 triangle whose three corners name one vertex is discarded

    python3 tools/xna-pipeline-oracle/model/make_corner_normal_probes.py tests/assets/xna40/model
"""
from __future__ import annotations

import os
import sys

# Distinct, exactly representable directions in the file's own (left-handed) space.
NORMALS = [(0, 1, 0), (1, 0, 0), (0, 0, 1), (-1, 0, 0), (0, -1, 0), (0, 0, -1), (1, 1, 1)]
POSITIONS = [(1, 0, 0), (0, 2, 0), (3, 0, 0), (0, 0, 4), (5, 5, 0), (0, 6, 6), (7, 0, 7)]

# file stem: (vertex index per corner, normal index per corner)
PROBES = {
    "x_corner_normals_pair01": ([0, 0, 1], [2, 0, 1]),
    "x_corner_normals_pair02": ([0, 1, 0], [2, 1, 0]),
    "x_corner_normals_pair12": ([1, 0, 0], [2, 1, 0]),
    "x_corner_normals_quad": ([0, 1, 0, 2], [3, 1, 0, 4]),
    "x_corner_normals_heptagon": ([0, 1, 0, 2, 0, 3, 0], [2, 4, 0, 5, 3, 6, 1]),
    "x_corner_normals_one_vertex": ([0, 0, 0, 0], [0, 2, 3, 1]),
}


def rows(values):
    out = []
    for i, value in enumerate(values):
        out.append(" " + ";".join("%.6f" % c for c in value) + ";" + (";" if i == len(values) - 1 else ","))
    return out


def write(directory: str, name: str, corners, normals) -> None:
    lines = ["xof 0303txt 0032",
             "// Written by tools/xna-pipeline-oracle/model/make_corner_normal_probes.py.",
             "Mesh %s {" % name[2:], " %d;" % len(POSITIONS)]
    lines += rows(POSITIONS)
    lines += [" 1;", " %d;%s;;" % (len(corners), ",".join(map(str, corners))),
              " MeshNormals {", " %d;" % len(NORMALS)]
    lines += rows(NORMALS)
    lines += [" 1;", " %d;%s;;" % (len(normals), ",".join(map(str, normals))), " }", "}"]
    with open(os.path.join(directory, name + ".x"), "w", encoding="ascii", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")


def main() -> int:
    directory = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(directory, exist_ok=True)
    for name, (corners, normals) in PROBES.items():
        assert len(corners) == len(normals) and len(set(normals)) == len(normals), name
        write(directory, name, corners, normals)
    print("wrote %d probes to %s" % (len(PROBES), directory))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
