#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-198: the faces the genuine XImporter discards.

SAMPLE-141's `racer.x` declares 2,447 faces in its chassis and XNA answers 2,445; its `target.x`
declares 180 and XNA answers 170. The two are different rules and these fixtures separate them.

A triangle two of whose corners are the **same corner** is discarded, and the rest of its polygon is
not. What makes two corners the same is the vertex rather than the index, which is why
`x_face_degenerate_normals.x` -- the same repeated index carrying two different normals -- keeps its
face. `VertexDuplicationIndices` is the second rule: two corners whose vertices share an entry are
one corner even where their positions and their normals differ.

    python3 tools/xna-pipeline-oracle/model/make_degenerate_face_fixtures.py <directory>
"""
from __future__ import annotations

import os
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))

TEMPLATE = """template VertexDuplicationIndices {
 <b8d65549-d7c9-4995-89cf-53a9a8b031e3>
 DWORD nIndices;
 DWORD nOriginalVertices;
 array DWORD indices[nIndices];
}
"""

# Six positions and six normals, all different, so nothing merges by value and every discard the
# oracle records is the rule under test rather than a coincidence of the numbers.
POSITIONS = [(0.0, 0.0, 0.0), (4.0, 0.0, 0.0), (0.0, 4.0, 0.0),
             (0.0, 0.0, 4.0), (4.0, 4.0, 0.0), (-4.0, 0.0, 0.0)]
NORMALS = [(1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0),
           (1.0, 1.0, 0.0), (0.0, 1.0, 1.0), (1.0, 0.0, 1.0)]


def mesh(name, faces, normals=NORMALS, normalFaces=None, materials=None, materialPerFace=None,
         duplication=None, positions=POSITIONS):
    """One `.x` file's text."""
    out = ["xof 0303txt 0032",
           "// Written by tools/xna-pipeline-oracle/model/make_degenerate_face_fixtures.py.", ""]
    if duplication is not None:
        out.append(TEMPLATE)
    out.append("Mesh %s {" % name)
    out.append(" %d;" % len(positions))
    for index, position in enumerate(positions):
        out.append("  %f;%f;%f;%s" % (position[0], position[1], position[2],
                                      ";" if index == len(positions) - 1 else ","))
    out.append(" %d;" % len(faces))
    for index, face in enumerate(faces):
        out.append("  %d;%s;%s" % (len(face), ",".join(str(corner) for corner in face),
                                   ";" if index == len(faces) - 1 else ","))
    if normals is not None:
        out.append(" MeshNormals {")
        out.append("  %d;" % len(normals))
        for index, normal in enumerate(normals):
            out.append("   %f;%f;%f;%s" % (normal[0], normal[1], normal[2],
                                           ";" if index == len(normals) - 1 else ","))
        rows = faces if normalFaces is None else normalFaces
        out.append("  %d;" % len(rows))
        for index, face in enumerate(rows):
            out.append("   %d;%s;%s" % (len(face), ",".join(str(corner) for corner in face),
                                        ";" if index == len(rows) - 1 else ","))
        out.append(" }")
    if materials is not None:
        out.append(" MeshMaterialList {")
        out.append("  %d;" % materials)
        out.append("  %d;" % len(materialPerFace))
        for index, material in enumerate(materialPerFace):
            out.append("  %d%s" % (material, ";;" if index == len(materialPerFace) - 1 else ","))
        for material in range(materials):
            out.append("  Material {")
            out.append("   %f;%f;%f;%f;;" % (0.1 * material, 0.2, 0.3, 1.0))
            out.append("   %f;" % (4.0 + material))
            out.append("   0.000000;0.000000;0.000000;;")
            out.append("   0.000000;0.000000;0.000000;;")
            out.append("  }")
        out.append(" }")
    if duplication is not None:
        out.append(" VertexDuplicationIndices {")
        out.append("  %d;" % len(duplication))
        out.append("  %d;" % len(set(duplication)))
        out.append("  %s;" % ",".join(str(value) for value in duplication))
        out.append(" }")
    out.append("}")
    return "\n".join(out) + "\n"


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    out = argv[0] if argv else os.path.join(REPO, "tests", "assets", "xna40", "model")
    os.makedirs(out, exist_ok=True)
    files = {
        # One face repeats a corner, one repeats all three, and a clean face stands between them.
        # Vertex 5 is named by the all-same face alone, so its position must not reach the mesh.
        "x_face_degenerate.x": mesh(
            "face_degenerate", [(0, 1, 2), (3, 4, 3), (1, 2, 4), (5, 5, 5), (0, 3, 1)]),
        # The same repeated index, with two different normals on it: two vertices, so the face
        # stays and nothing about it is degenerate.
        "x_face_degenerate_normals.x": mesh(
            "face_degenerate_normals", [(0, 1, 2), (3, 4, 3), (1, 2, 4)],
            normalFaces=[(5, 0, 1), (0, 0, 1), (2, 3, 4)]),
        # An n-gon loses the fan triangles that are degenerate and keeps the rest: `(0,1,2,0)` is
        # `(0,1,2)` and `(0,2,0)`, and `(0,1,1,2)` is `(0,1,1)` and `(0,1,2)`.
        "x_face_degenerate_quad.x": mesh(
            "face_degenerate_quad", [(0, 1, 2, 0), (0, 1, 1, 2), (1, 2, 4)]),
        # Nothing survives, and the mesh is then not a node at all.
        "x_face_degenerate_all.x": mesh("face_degenerate_all", [(0, 1, 0), (2, 2, 3)]),
        # A mesh that declares no faces answers the same nothing.
        "x_no_faces.x": mesh("no_faces", []),
        # A material whose faces all go produces no geometry, and the ones that stay keep theirs.
        "x_face_degenerate_materials.x": mesh(
            "face_degenerate_materials", [(0, 1, 2), (3, 4, 3), (1, 2, 4)],
            materials=3, materialPerFace=[0, 1, 2]),
        # Vertices 4 and 5 are duplicates of 3, so a face naming two of the three is one corner
        # twice even though its positions and its normals differ.
        "x_vertex_duplication.x": mesh(
            "vertex_duplication", [(0, 1, 2), (0, 4, 5), (1, 3, 4), (1, 2, 3)],
            duplication=[0, 1, 2, 3, 3, 3]),
        # The same file with the identity map: the block is present and discards nothing.
        "x_vertex_duplication_identity.x": mesh(
            "vertex_duplication_identity", [(0, 1, 2), (0, 4, 5), (1, 3, 4), (1, 2, 3)],
            duplication=[0, 1, 2, 3, 4, 5]),
    }
    for name, text in sorted(files.items()):
        with open(os.path.join(out, name), "w", newline="\n") as handle:
            handle.write(text)
        print("make_degenerate_face_fixtures: %s" % name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
