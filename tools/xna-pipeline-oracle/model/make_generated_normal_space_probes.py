#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""CNA-FIX-L49: in which space a generated `.x` normal is computed.

A mesh with no `MeshNormals` has its normals generated. Generating them from the imported
(right-handed, Z-reflected) positions and generating them in the file's own left-handed space with
the winding reversed give the same magnitudes, because the reflection and the negation are exact;
only the sign of a zero and a final normalization can tell them apart. Each file is one or two
triangles chosen so that one candidate differs from the others:

  `x_generated_normal_space_source.x`   face normal (0, -1, 0): only source-space generation
                                        followed by the basis change answers `-0` in Z.
  `x_generated_normal_space_seed.x`     face normal (0, +1, 0): an accumulator starting at `-0`
                                        would keep a `-0` that the genuine importer does not.
  `x_generated_normal_space_control.x`  face normal (0, 0, -1): every candidate agrees.
  `x_generated_normal_space_rounding.x` two oblique triangles sharing two corners: the shared
                                        corners answer the basis change of the twice-normalized
                                        sum, one ulp away from a basis change normalized again.

    python3 tools/xna-pipeline-oracle/model/make_generated_normal_space_probes.py <outdir>
"""
from __future__ import annotations

import os
import sys

PROBES = {
    "x_generated_normal_space_source": ([(0, 0, 0), (1, 0, 0), (0, 0, 1)], [(0, 1, 2)]),
    "x_generated_normal_space_seed": ([(0, 0, 0), (1, 0, 0), (2, 0, -1)], [(0, 1, 2)]),
    "x_generated_normal_space_control": ([(0, 0, 0), (1, 0, 0), (0, 1, 0)], [(0, 1, 2)]),
    "x_generated_normal_space_rounding": (
        [(0, 0, 0), (15, -7, -36), (-9, 38, 24), (37, 9, -36)], [(0, 1, 2), (0, 2, 3)]),
}


def write(directory: str, name: str, points, faces) -> None:
    lines = ["xof 0303txt 0032",
             "// Written by tools/xna-pipeline-oracle/model/make_generated_normal_space_probes.py.",
             "Mesh %s {" % name, " %d;" % len(points)]
    for i, point in enumerate(points):
        lines.append(" " + ";".join("%.1f" % c for c in point) + ";" +
                     ("," if i + 1 < len(points) else ";"))
    lines.append(" %d;" % len(faces))
    for i, face in enumerate(faces):
        lines.append(" 3;" + ",".join(map(str, face)) + ";" + ("," if i + 1 < len(faces) else ";"))
    lines.append("}")
    with open(os.path.join(directory, name + ".x"), "w", encoding="ascii", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")


def main() -> int:
    directory = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(directory, exist_ok=True)
    for name, (points, faces) in PROBES.items():
        write(directory, name, points, faces)
    print("wrote %d probes to %s" % (len(PROBES), directory))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
