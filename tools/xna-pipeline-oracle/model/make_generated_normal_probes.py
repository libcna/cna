#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-231: the stages of a generated `.x` normal.

A mesh with no `MeshNormals` has its normals generated, and the arithmetic that generates them
rounds in three places -- the edge subtraction, the cross product, and the normalization. These two
files each hold isolated triangles that leave exactly one of those places rounding, so a candidate
can be scored against the genuine importer one stage at a time.

  `x_generated_normals_exact.x`  every triangle is the origin and two small integer vectors, so the
                                 subtraction and the cross product are exact and only the
                                 normalization rounds.
  `x_generated_normals_wide.x`   integer coordinates up to 20,000, so every difference is still
                                 exact and every product in the cross rounds.

    python3 tools/xna-pipeline-oracle/model/make_generated_normal_probes.py [outdir]
"""
from __future__ import annotations

import os
import random
import sys

HEADER = "xof 0303txt 0032\n\n// Written by tools/xna-pipeline-oracle/model/make_generated_normal_probes.py.\n\n"


def mesh(name, triangles):
    verts = []
    faces = []
    for triangle in triangles:
        base = len(verts)
        verts += list(triangle)
        faces.append((base, base + 1, base + 2))
    lines = [HEADER + "Mesh %s {" % name, " %d;" % len(verts)]
    for index, point in enumerate(verts):
        lines.append("  %d.0;%d.0;%d.0;%s"
                     % (point[0], point[1], point[2], "," if index + 1 < len(verts) else ";"))
    lines.append(" %d;" % len(faces))
    for index, face in enumerate(faces):
        lines.append("  3;%d,%d,%d;%s" % (face + ("," if index + 1 < len(faces) else ";",)))
    lines.append("}")
    return "\n".join(lines) + "\n"


def triangles(count, span, from_origin, seed):
    random.seed(seed)
    out = []
    seen = set()
    while len(out) < count:
        if from_origin:
            points = [(0, 0, 0),
                      tuple(random.randint(-span, span) for _ in range(3)),
                      tuple(random.randint(-span, span) for _ in range(3))]
        else:
            points = [tuple(random.randint(-span, span) for _ in range(3)) for _ in range(3)]
        u = tuple(points[1][i] - points[0][i] for i in range(3))
        v = tuple(points[2][i] - points[0][i] for i in range(3))
        normal = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        if normal == (0, 0, 0) or normal in seen:
            continue
        seen.add(normal)
        out.append(points)
    return out


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..")),
        "tests/assets/xna40/model")
    os.makedirs(out, exist_ok=True)
    # The origin is one vertex of every triangle on purpose: a position the whole mesh shares is
    # also how the accumulation-by-position is observable in one file.
    with open(os.path.join(out, "x_generated_normals_exact.x"), "w", newline="\n") as handle:
        handle.write(mesh("generated_exact", triangles(24, 40, True, 19901)))
    with open(os.path.join(out, "x_generated_normals_wide.x"), "w", newline="\n") as handle:
        handle.write(mesh("generated_wide", triangles(24, 20000, False, 1992)))
    print("wrote 2 files to %s" % out)


if __name__ == "__main__":
    main()
