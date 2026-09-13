#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-168: what a mesh's bounding sphere is computed over.

CNA's `BoundingSphere::CreateFromPoints` reproduces the genuine method exactly over 560 measured
point sets (`XNASWEEP-134`), and a model's sphere still comes out different, so what differs is the
*input*, not the algorithm -- and once the input was pinned, what was left was the seed. These
probes vary one thing at a time. The first group asks what the sphere is computed over: the order
the same points arrive in, whether a position that two vertices share is counted twice, and whether
a position no triangle names is counted at all. The second asks how the seed is chosen: which of
several points sharing an extreme is kept, which axis a tie goes to, and -- the one that was wrong
-- whether the axes are compared by the distance between their extreme points or by the square of
it. Each is measured through `tools/xna-pipeline-oracle/modelroot/run-modelroot-oracle.sh`, which
runs the genuine `ModelProcessor` and prints every mesh's sphere at round-trip precision.

    python3 tools/xna-pipeline-oracle/model/make_sphere_probes.py <directory>
"""
from __future__ import annotations

import os
import struct
import sys


def value(number):
    """A coordinate written so that the `.x` parser reads back the same float.

    Fixed notation, not exponential: the genuine reader answers a mesh of NaNs for `4.37e-08`.
    """
    text = "%.20f" % number
    assert struct.pack("<f", float(text)) == struct.pack("<f", number), text
    return text

HEADER = '''xof 0303txt 0032
// Written by tools/xna-pipeline-oracle/model/make_sphere_probes.py for this repository.

Mesh %s {
'''


def mesh(name, positions, faces, normals, normal_faces):
    text = HEADER % name
    text += " %d;\n" % len(positions)
    for i, p in enumerate(positions):
        text += "  %s;%s;%s;%s\n" % (value(p[0]), value(p[1]), value(p[2]),
                                      ";" if i + 1 == len(positions) else ",")
    text += " %d;\n" % len(faces)
    for i, face in enumerate(faces):
        text += "  3;%d,%d,%d;%s\n" % (face[0], face[1], face[2],
                                       ";" if i + 1 == len(faces) else ",")
    text += " MeshNormals {\n  %d;\n" % len(normals)
    for i, n in enumerate(normals):
        text += "  %s;%s;%s;%s\n" % (value(n[0]), value(n[1]), value(n[2]),
                                      ";" if i + 1 == len(normals) else ",")
    text += "  %d;\n" % len(normal_faces)
    for i, face in enumerate(normal_faces):
        text += "  3;%d,%d,%d;%s\n" % (face[0], face[1], face[2],
                                       ";" if i + 1 == len(normal_faces) else ",")
    text += " }\n}\n"
    return text


P = [(0.0, 0.0, 0.0), (4.0, 0.0, 0.0), (0.0, 3.0, 0.0), (1.0, 1.0, 7.0),
     (-2.0, -5.0, 1.0), (6.0, 2.0, -3.0)]
N = [(1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0),
     (0.577350, 0.577350, 0.577350), (-1.0, 0.0, 0.0), (0.0, -1.0, 0.0)]

CASES = {}
# the same six points, in four different orders
CASES["sph_order0"] = ([0, 1, 2, 3, 4, 5], [(0, 1, 2), (3, 4, 5)])
CASES["sph_order1"] = ([5, 4, 3, 2, 1, 0], [(0, 1, 2), (3, 4, 5)])
CASES["sph_order2"] = ([3, 0, 5, 1, 4, 2], [(0, 1, 2), (3, 4, 5)])
CASES["sph_order3"] = ([1, 2, 0, 4, 5, 3], [(0, 1, 2), (3, 4, 5)])
# the same six points, but only the first triangle is declared: three positions no face names
CASES["sph_unused"] = ([0, 1, 2, 3, 4, 5], [(0, 1, 2)])


def main() -> int:
    directory = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(directory, exist_ok=True)
    for name, (order, faces) in sorted(CASES.items()):
        positions = [P[i] for i in order]
        normals = [N[i] for i in order]
        open(os.path.join(directory, name + ".x"), "w", newline="\n").write(
            mesh(name, positions, faces, normals, faces))
        print("wrote %s (%d positions, %d faces)" % (name, len(positions), len(faces)))
    # Which of several points sharing an extreme coordinate is kept. X is the widest axis here by
    # construction, two points share its minimum and two its maximum, and the four possible
    # midpoints are far enough apart to be told apart in the answer.
    tie = [(-1.0, 0.0, 0.0), (-1.0, 0.1, 0.0), (1.0, 0.0, 0.0), (1.0, 0.2, 0.0),
           (0.0, 0.05, 0.0), (0.0, -0.05, 0.0)]
    open(os.path.join(directory, "sph_extreme_tie.x"), "w", newline="\n").write(
        mesh("sph_extreme_tie", tie, [(0, 1, 2), (3, 4, 5)], N, [(0, 1, 2), (3, 4, 5)]))
    print("wrote sph_extreme_tie (6 positions, two sharing each X extreme)")

    # Six points whose sphere genuinely depends on the order they arrive in: CNA's algorithm gives
    # four different answers over their 720 permutations. Two orders that give different answers
    # are written out, which is what makes "does XNA's sphere depend on the order" answerable.
    D = [(-1.762, -3.492, 1.509), (-4.276, 0.359, -1.343), (-4.420, 0.074, -4.625),
         (-0.664, -4.301, -4.093), (-0.755, 3.269, -3.762), (-2.768, 1.274, 4.477)]
    for label, order in (("a", (0, 1, 2, 3, 4, 5)), ("b", (0, 1, 2, 4, 3, 5))):
        pts = [(D[i][0], D[i][1], -D[i][2]) for i in order]   # the importer negates Z back
        open(os.path.join(directory, "sph_seq_%s.x" % label), "w", newline="\n").write(
            mesh("sph_seq_%s" % label, pts, [(0, 1, 2), (3, 4, 5)], N, [(0, 1, 2), (3, 4, 5)]))
        print("wrote sph_seq_%s" % label)

    # How the seed pair is chosen. Each of these is a four-point set whose two candidate seeds are
    # far enough apart that the answer says which rule produced it.
    #
    #   tie_xy/tie_xz/tie_yz -- two axes whose extents are exactly equal, so the answer says which
    #                           axis a tie goes to (measured: the later one);
    #   ulp_xy/ulp_yz        -- two axes whose extents are the same length and *not* the same
    #                           squared length, which is the only shape that separates comparing
    #                           `Distance` from comparing `DistanceSquared` (measured: `Distance`);
    #   minrun/maxrun        -- two points sharing the minimum, and two sharing the maximum, on the
    #                           axis that wins, so the answer says which one is kept (the first).
    #
    # `dz` below is chosen so that (8, dz) has a squared length one ulp over 100 and a length of
    # exactly 10 in single precision; `spanprec` is the same question carrying `Cylinder.fbx`'s own
    # Y-extreme pair, which is where the corpus met it.
    dz = 6.000000476837158
    designed = {
        "sph_tie_xy": [(-5, 0, 0), (5, 0, 0), (1, -4, 0), (1, 4, 6)],
        "sph_tie_yz": [(4, -4, 0), (4, 4, 6), (0, 0, -2), (0, 0, 8)],
        "sph_tie_xz": [(-5, 0, 0), (5, 0, 0), (0, 3, -4), (0, 3, 6)],
        "sph_ulp_xy": [(-5, 0, 0), (5, 0, 0), (1, -4, 0), (1, 4, dz)],
        "sph_ulp_yz": [(4, -4, 0), (4, 4, dz), (0, 0, -2), (0, 0, 8)],
        "sph_minrun": [(-5, 0, 0), (-5, 4, 3), (5, 0, 0), (0, -1, 1)],
        "sph_maxrun": [(-5, 0, 0), (5, 0, 0), (5, 4, 3), (0, -1, 1)],
        "sph_spanprec": [(0.6067627668380737, -1.0000001192092896, 0.44083893299102783),
                         (-0.606762707233429, 1.0000001192092896, -0.4408387243747711),
                         (0.5, 0.5, 1.0), (0.5, 0.5, -1.5)],
    }
    for name, pts in sorted(designed.items()):
        faces = [(0, 1, 2), (1, 2, 3)]
        open(os.path.join(directory, name + ".x"), "w", newline="\n").write(
            mesh(name, [(p[0], p[1], -p[2]) for p in pts], faces, N[:4], faces))
        print("wrote %s (4 positions, two candidate seeds)" % name)

    # a position two vertices share, split by different normals: six vertices over four positions
    positions = [P[0], P[1], P[2], P[3]]
    faces = [(0, 1, 2), (0, 1, 3)]
    normals = [N[0], N[1], N[2], N[3], N[4], N[5]]
    normal_faces = [(0, 1, 2), (3, 4, 5)]
    open(os.path.join(directory, "sph_split.x"), "w", newline="\n").write(
        mesh("sph_split", positions, faces, normals, normal_faces))
    print("wrote sph_split (4 positions, 6 vertices)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
