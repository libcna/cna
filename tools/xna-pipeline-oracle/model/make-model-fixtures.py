#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xnapipeline_parity.md XNAPP-216, XNAPP-220: the synthetic modelling corpus.

Text `.x` files written here by hand, so every construct the importer must read appears in a file
whose exact content is known: a frame hierarchy with transforms, a mesh with normals, texture
coordinates and vertex colours, a material list with a texture reference, skin weights, and an
animation set. Nothing was downloaded and nothing is third-party content.

The binary `.x` variants are written from the same data by the tokenizer in this file, because the
binary format is the same object model under a different encoding and a corpus that only has text
would leave half the importer untested.

Run:  python3 tools/xna-pipeline-oracle/model/make-model-fixtures.py
"""
import os
import struct
import sys
import zlib

# --- the text corpus ---------------------------------------------------------------------------

HEADER = "xof 0303txt 0032\n"

# One frame, one mesh: a unit quad of two triangles, with normals, UVs, colours and a material.
SIMPLE_MESH = HEADER + """
Frame Root {
  FrameTransformMatrix {
    1.000000, 0.000000, 0.000000, 0.000000,
    0.000000, 1.000000, 0.000000, 0.000000,
    0.000000, 0.000000, 1.000000, 0.000000,
    0.000000, 0.000000, 0.000000, 1.000000;;
  }
  Mesh Quad {
    4;
    -1.000000; -1.000000; 0.000000;,
     1.000000; -1.000000; 0.000000;,
     1.000000;  1.000000; 0.000000;,
    -1.000000;  1.000000; 0.000000;;
    2;
    3; 0, 1, 2;,
    3; 0, 2, 3;;
    MeshNormals {
      4;
      0.000000; 0.000000; 1.000000;,
      0.000000; 0.000000; 1.000000;,
      0.000000; 0.000000; 1.000000;,
      0.000000; 0.000000; 1.000000;;
      2;
      3; 0, 1, 2;,
      3; 0, 2, 3;;
    }
    MeshTextureCoords {
      4;
      0.000000; 1.000000;,
      1.000000; 1.000000;,
      1.000000; 0.000000;,
      0.000000; 0.000000;;
    }
    MeshVertexColors {
      4;
      0; 1.000000; 0.000000; 0.000000; 1.000000;,
      1; 0.000000; 1.000000; 0.000000; 1.000000;,
      2; 0.000000; 0.000000; 1.000000; 1.000000;,
      3; 1.000000; 1.000000; 1.000000; 0.500000;;
    }
    MeshMaterialList {
      1;
      2;
      0,
      0;;
      Material Painted {
        0.800000; 0.700000; 0.600000; 1.000000;;
        24.000000;
        0.100000; 0.200000; 0.300000;;
        0.010000; 0.020000; 0.030000;;
        TextureFilename {
          "surface.png";
        }
      }
    }
  }
}
"""

# Two frames, the child transformed, so a hierarchy and a composed transform are both visible.
HIERARCHY = HEADER + """
Frame World {
  FrameTransformMatrix {
    1.000000, 0.000000, 0.000000, 0.000000,
    0.000000, 1.000000, 0.000000, 0.000000,
    0.000000, 0.000000, 1.000000, 0.000000,
    5.000000, 0.000000, 0.000000, 1.000000;;
  }
  Frame Child {
    FrameTransformMatrix {
      2.000000, 0.000000, 0.000000, 0.000000,
      0.000000, 2.000000, 0.000000, 0.000000,
      0.000000, 0.000000, 2.000000, 0.000000,
      0.000000, 3.000000, 0.000000, 1.000000;;
    }
    Mesh Tri {
      3;
      0.000000; 0.000000; 0.000000;,
      1.000000; 0.000000; 0.000000;,
      0.000000; 1.000000; 0.000000;;
      1;
      3; 0, 1, 2;;
    }
  }
}
"""

# A skinned mesh: two bones, weights on every vertex, and one animation set over both.
SKINNED = HEADER + """
Frame Armature {
  FrameTransformMatrix {
    1.000000, 0.000000, 0.000000, 0.000000,
    0.000000, 1.000000, 0.000000, 0.000000,
    0.000000, 0.000000, 1.000000, 0.000000,
    0.000000, 0.000000, 0.000000, 1.000000;;
  }
  Frame Bone0 {
    FrameTransformMatrix {
      1.000000, 0.000000, 0.000000, 0.000000,
      0.000000, 1.000000, 0.000000, 0.000000,
      0.000000, 0.000000, 1.000000, 0.000000,
      0.000000, 0.000000, 0.000000, 1.000000;;
    }
    Frame Bone1 {
      FrameTransformMatrix {
        1.000000, 0.000000, 0.000000, 0.000000,
        0.000000, 1.000000, 0.000000, 0.000000,
        0.000000, 0.000000, 1.000000, 0.000000,
        0.000000, 2.000000, 0.000000, 1.000000;;
      }
    }
  }
  Mesh Skin {
    4;
    -1.000000; 0.000000; 0.000000;,
     1.000000; 0.000000; 0.000000;,
     1.000000; 2.000000; 0.000000;,
    -1.000000; 2.000000; 0.000000;;
    2;
    3; 0, 1, 2;,
    3; 0, 2, 3;;
    XSkinMeshHeader {
      2;
      2;
      2;
    }
    SkinWeights {
      "Bone0";
      4;
      0, 1, 2, 3;
      1.000000, 1.000000, 0.250000, 0.250000;
      1.000000, 0.000000, 0.000000, 0.000000,
      0.000000, 1.000000, 0.000000, 0.000000,
      0.000000, 0.000000, 1.000000, 0.000000,
      0.000000, 0.000000, 0.000000, 1.000000;;
    }
    SkinWeights {
      "Bone1";
      2;
      2, 3;
      0.750000, 0.750000;
      1.000000, 0.000000, 0.000000, 0.000000,
      0.000000, 1.000000, 0.000000, 0.000000,
      0.000000, 0.000000, 1.000000, 0.000000,
      0.000000, -2.000000, 0.000000, 1.000000;;
    }
  }
}
AnimationSet Wave {
  Animation {
    { Bone1 }
    AnimationKey {
      2;
      3;
      0; 3; 0.000000, 0.000000, 0.000000;;,
      10; 3; 0.000000, 1.000000, 0.000000;;,
      20; 3; 0.000000, 2.000000, 0.000000;;;
    }
    AnimationKey {
      0;
      2;
      0; 4; 1.000000, 0.000000, 0.000000, 0.000000;;,
      20; 4; 0.707107, 0.707107, 0.000000, 0.000000;;;
    }
  }
}
"""

# The same simple mesh with the template declarations a full .x file carries in front of it, which
# an importer must skip rather than read as data.
WITH_TEMPLATES = HEADER + """
template Vector {
 <3d82ab5e-62da-11cf-ab39-0020af71e433>
 FLOAT x;
 FLOAT y;
 FLOAT z;
}
template MeshFace {
 <3d82ab5f-62da-11cf-ab39-0020af71e433>
 DWORD nFaceVertexIndices;
 array DWORD faceVertexIndices[nFaceVertexIndices];
}
template Mesh {
 <3d82ab44-62da-11cf-ab39-0020af71e433>
 DWORD nVertices;
 array Vector vertices[nVertices];
 DWORD nFaces;
 array MeshFace faces[nFaces];
 [...]
}
Frame Root {
  Mesh Tri {
    3;
    0.000000; 0.000000; 0.000000;,
    1.000000; 0.000000; 0.000000;,
    0.000000; 1.000000; 0.000000;;
    1;
    3; 0, 1, 2;;
  }
}
"""

# A mesh with no frame around it at all, which the format allows.
BARE_MESH = HEADER + """
Mesh Loose {
  3;
  0.000000; 0.000000; 0.000000;,
  2.000000; 0.000000; 0.000000;,
  0.000000; 2.000000; 0.000000;;
  1;
  3; 0, 1, 2;;
}
"""


# Normals and positions that are not axis-aligned, so what the importer does to a normal can be
# told apart from what it does to a position: negating every component, negating only Z, or
# nothing at all all give different answers here.
OBLIQUE = HEADER + """
Frame Root {
  Mesh Oblique {
    3;
    1.000000; 2.000000; 3.000000;,
    4.000000; 5.000000; 6.000000;,
    7.000000; 8.000000; 9.000000;;
    1;
    3; 0, 1, 2;;
    MeshNormals {
      3;
      0.600000; 0.000000; 0.800000;,
      0.000000; 0.600000; 0.800000;,
      0.267261; 0.534522; 0.801784;;
      1;
      3; 0, 1, 2;;
    }
    MeshTextureCoords {
      3;
      0.100000; 0.200000;,
      0.300000; 0.400000;,
      0.500000; 0.600000;;
    }
  }
}
"""

# Two materials, assigned per face, so whether the importer splits a mesh into one geometry batch
# per material is visible.
TWO_MATERIALS = HEADER + """
Frame Root {
  Mesh Split {
    4;
    0.000000; 0.000000; 0.000000;,
    1.000000; 0.000000; 0.000000;,
    1.000000; 1.000000; 0.000000;,
    0.000000; 1.000000; 0.000000;;
    2;
    3; 0, 1, 2;,
    3; 0, 2, 3;;
    MeshMaterialList {
      2;
      2;
      0,
      1;;
      Material Red {
        1.000000; 0.000000; 0.000000; 1.000000;;
        1.000000;
        0.000000; 0.000000; 0.000000;;
        0.000000; 0.000000; 0.000000;;
      }
      Material Blue {
        0.000000; 0.000000; 1.000000; 0.500000;;
        2.000000;
        0.000000; 0.000000; 0.000000;;
        0.000000; 0.000000; 0.000000;;
        TextureFilename {
          "blue.dds";
        }
      }
    }
  }
}
"""

# An animation set that names its own tick rate, and a second set, so how a .x tick becomes a
# TimeSpan and where an animation is attached are both measured rather than inferred.
TWO_ANIMATIONS = HEADER + """
Frame Root {
  Frame Joint {
    FrameTransformMatrix {
      1.000000, 0.000000, 0.000000, 0.000000,
      0.000000, 1.000000, 0.000000, 0.000000,
      0.000000, 0.000000, 1.000000, 0.000000,
      0.000000, 0.000000, 0.000000, 1.000000;;
    }
  }
}
AnimTicksPerSecond {
  60;
}
AnimationSet First {
  Animation {
    { Joint }
    AnimationKey {
      2;
      2;
      0; 3; 0.000000, 0.000000, 0.000000;;,
      60; 3; 1.000000, 0.000000, 0.000000;;;
    }
  }
}
AnimationSet Second {
  Animation {
    { Joint }
    AnimationKey {
      1;
      2;
      0; 3; 1.000000, 1.000000, 1.000000;;,
      30; 3; 2.000000, 2.000000, 2.000000;;;
    }
  }
}
"""


# A frame transform with a non-zero Z and off-diagonal terms. Every earlier fixture had z = 0
# everywhere, so what the importer does to a matrix was unmeasured while what it does to a
# position was already known.
TRANSFORM_Z = HEADER + """
Frame Root {
  FrameTransformMatrix {
    1.000000, 2.000000, 3.000000, 0.000000,
    4.000000, 5.000000, 6.000000, 0.000000,
    7.000000, 8.000000, 9.000000, 0.000000,
    10.000000, 11.000000, 12.000000, 1.000000;;
  }
  Mesh Point {
    3;
    0.000000; 0.000000; 5.000000;,
    1.000000; 0.000000; 5.000000;,
    0.000000; 1.000000; 5.000000;;
    1;
    3; 0, 1, 2;;
  }
}
"""

# One animation, one key list, no AnimTicksPerSecond: what a tick is worth by default, and whether
# the duration is the last key's time.
DEFAULT_RATE = HEADER + """
Frame Root {
  Frame Joint {
    FrameTransformMatrix {
      1.000000, 0.000000, 0.000000, 0.000000,
      0.000000, 1.000000, 0.000000, 0.000000,
      0.000000, 0.000000, 1.000000, 0.000000,
      0.000000, 0.000000, 0.000000, 1.000000;;
    }
  }
}
AnimationSet Slide {
  Animation {
    { Joint }
    AnimationKey {
      2;
      2;
      0; 3; 0.000000, 0.000000, 0.000000;;,
      20; 3; 4.000000, 0.000000, 0.000000;;;
    }
  }
}
"""

# Two bones, both animated, in a skinned mesh: where an animation lands when the file has a
# skeleton is the one thing the first skinned fixture left ambiguous.
TWO_BONES_ANIMATED = HEADER + """
Frame Armature {
  Frame Bone0 {
    FrameTransformMatrix {
      1.000000, 0.000000, 0.000000, 0.000000,
      0.000000, 1.000000, 0.000000, 0.000000,
      0.000000, 0.000000, 1.000000, 0.000000,
      0.000000, 0.000000, 0.000000, 1.000000;;
    }
    Frame Bone1 {
      FrameTransformMatrix {
        1.000000, 0.000000, 0.000000, 0.000000,
        0.000000, 1.000000, 0.000000, 0.000000,
        0.000000, 0.000000, 1.000000, 0.000000,
        0.000000, 1.000000, 0.000000, 1.000000;;
      }
    }
  }
  Mesh Skin {
    3;
    0.000000; 0.000000; 0.000000;,
    1.000000; 0.000000; 0.000000;,
    0.000000; 1.000000; 0.000000;;
    1;
    3; 0, 1, 2;;
    SkinWeights {
      "Bone0";
      3;
      0, 1, 2;
      1.000000, 0.500000, 0.000000;
      1.000000, 0.000000, 0.000000, 0.000000,
      0.000000, 1.000000, 0.000000, 0.000000,
      0.000000, 0.000000, 1.000000, 0.000000,
      0.000000, 0.000000, 0.000000, 1.000000;;
    }
    SkinWeights {
      "Bone1";
      2;
      1, 2;
      0.500000, 1.000000;
      1.000000, 0.000000, 0.000000, 0.000000,
      0.000000, 1.000000, 0.000000, 0.000000,
      0.000000, 0.000000, 1.000000, 0.000000,
      0.000000, -1.000000, 0.000000, 1.000000;;
    }
  }
}
AnimationSet Both {
  Animation {
    { Bone0 }
    AnimationKey {
      2;
      2;
      0; 3; 0.000000, 0.000000, 0.000000;;,
      24; 3; 1.000000, 0.000000, 0.000000;;;
    }
  }
  Animation {
    { Bone1 }
    AnimationKey {
      2;
      2;
      0; 3; 0.000000, 1.000000, 0.000000;;,
      24; 3; 0.000000, 3.000000, 0.000000;;;
    }
  }
}
"""


SKINNED_TWO_ANIMATIONS = TWO_BONES_ANIMATED.replace(
    '    SkinWeights {\n      "Bone0";',
    '    XSkinMeshHeader {\n      2;\n      2;\n      2;\n    }\n    SkinWeights {\n      "Bone0";', 1)


# --- the binary encoder ------------------------------------------------------------------------
#
# The binary format is the same object model under a token stream: the tokens are 16-bit, a name
# and a string carry a length and their bytes, and integer and float lists are run-length tokens
# followed by their payload. Writing one from the same data is what puts the binary reader under
# the same expectations as the text one.

TOKEN_NAME, TOKEN_STRING, TOKEN_INTEGER = 1, 2, 3
TOKEN_INTEGER_LIST, TOKEN_FLOAT_LIST = 6, 7
TOKEN_OBRACE, TOKEN_CBRACE = 10, 11
TOKEN_SEMICOLON, TOKEN_COMMA = 20, 19


def name(text):
    return struct.pack("<HI", TOKEN_NAME, len(text)) + text.encode("ascii")


def string(text):
    return (struct.pack("<HI", TOKEN_STRING, len(text)) + text.encode("ascii") +
            struct.pack("<H", TOKEN_SEMICOLON))


def integers(values):
    return struct.pack("<HI", TOKEN_INTEGER_LIST, len(values)) + b"".join(
        struct.pack("<I", v & 0xFFFFFFFF) for v in values)


def floats(values):
    return struct.pack("<HI", TOKEN_FLOAT_LIST, len(values)) + b"".join(
        struct.pack("<f", v) for v in values)


def token(value):
    return struct.pack("<H", value)


def binary_simple_mesh():
    """The `Frame Root { FrameTransformMatrix; Mesh Tri { ... } }` of BARE_MESH's shape."""
    identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
    body = b""
    body += name("Frame") + name("Root") + token(TOKEN_OBRACE)
    body += name("FrameTransformMatrix") + token(TOKEN_OBRACE)
    body += floats(identity)
    body += token(TOKEN_CBRACE)
    body += name("Mesh") + name("Tri") + token(TOKEN_OBRACE)
    body += integers([3])
    body += floats([0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0])
    body += integers([1])
    body += integers([3, 0, 1, 2])
    body += token(TOKEN_CBRACE)
    body += token(TOKEN_CBRACE)
    return b"xof 0303bin 0032" + body


RECORD = []


def write(path, data):
    mode = "wb" if isinstance(data, bytes) else "w"
    with open(path, mode, newline="" if mode == "w" else None) as handle:
        handle.write(data)
    RECORD.append((os.path.basename(path), len(data)))


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.abspath(os.path.join(here, "..", "..", ".."))
    out = os.path.join(repo, "tests/assets/xna40/model")
    os.makedirs(out, exist_ok=True)

    write(os.path.join(out, "quad_textured.x"), SIMPLE_MESH)
    write(os.path.join(out, "hierarchy.x"), HIERARCHY)
    write(os.path.join(out, "skinned_animated.x"), SKINNED)
    write(os.path.join(out, "with_templates.x"), WITH_TEMPLATES)
    write(os.path.join(out, "bare_mesh.x"), BARE_MESH)
    write(os.path.join(out, "oblique_normals.x"), OBLIQUE)
    write(os.path.join(out, "two_materials.x"), TWO_MATERIALS)
    write(os.path.join(out, "two_animations.x"), TWO_ANIMATIONS)
    write(os.path.join(out, "transform_z.x"), TRANSFORM_Z)
    write(os.path.join(out, "anim_default_rate.x"), DEFAULT_RATE)
    write(os.path.join(out, "two_bones_animated.x"), TWO_BONES_ANIMATED)
    write(os.path.join(out, "skinned_two_animations.x"), SKINNED_TWO_ANIMATIONS)
    write(os.path.join(out, "binary_mesh.x"), binary_simple_mesh())

    # The refusal corpus.
    write(os.path.join(out, "empty.x"), b"")
    write(os.path.join(out, "not_x.x"), b"this file has no xof header at all\n")
    write(os.path.join(out, "truncated.x"), SIMPLE_MESH[:120].encode("ascii"))
    write(os.path.join(out, "bad_version.x"), "xof 9999txt 0032\nFrame Root { }\n")
    # A mesh whose face names a vertex it does not have.
    write(os.path.join(out, "index_out_of_range.x"), HEADER + """
Mesh Broken {
  3;
  0.000000; 0.000000; 0.000000;,
  1.000000; 0.000000; 0.000000;,
  0.000000; 1.000000; 0.000000;;
  1;
  3; 0, 1, 9;;
}
""")

    with open(os.path.join(out, "PROVENANCE.md"), "w", encoding="utf-8") as handle:
        handle.write("# DirectX `.x` corpus (authored)\n\n")
        handle.write("Written by `tools/xna-pipeline-oracle/model/make-model-fixtures.py` for this\n")
        handle.write("repository. Every construct the importer must read appears in a file whose\n")
        handle.write("exact content is known: a frame hierarchy with transforms, a mesh with\n")
        handle.write("normals, texture coordinates and vertex colours, a material list with a\n")
        handle.write("texture reference, skin weights over two bones, and an animation set. The\n")
        handle.write("binary file is the same object model written through this script's own\n")
        handle.write("tokenizer. Nothing here was downloaded and nothing is third-party content.\n\n")
        handle.write("| File | Bytes |\n|---|---:|\n")
        for one, size in sorted(RECORD):
            handle.write("| `%s` | %d |\n" % (one, size))
    print("make-model-fixtures: wrote %d files to %s" % (len(RECORD), out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
