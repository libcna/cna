#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-180: a `.x` object's type name and its case.

An object's type in a `.x` file is a *template* name, and the genuine reader resolves it against
the template set without regard to case. SAMPLE-028's `Car.x` is the corpus file that needs it --
it writes `TextureFileName` where every other `.x` in the corpus writes `TextureFilename`, and an
exact comparison loses every texture the model has.

The fixture writes one object of each type the importer reads in a spelling no `.x` in the corpus
uses, so a single graph answers for all of them at once: upper case for the frame, its transform,
the mesh, its normals and its texture coordinates, lower case for the material list, and the two
mixed spellings for the material and its texture file name. It also carries an object of a type no
template defines, which the genuine reader accepts and ignores.

    python3 tools/xna-pipeline-oracle/model/make_type_case_fixture.py <directory>
"""
from __future__ import annotations

import os
import sys

TEXT = '''xof 0303txt 0032
// Written by tools/xna-pipeline-oracle/model/make_type_case_fixture.py for this repository.

FRAME outer {
 FRAMETRANSFORMMATRIX {
  2.000000,0.000000,0.000000,0.000000,0.000000,2.000000,0.000000,0.000000,0.000000,0.000000,2.000000,0.000000,3.000000,4.000000,5.000000,1.000000;;
 }
 MESH shape {
  3;
  0.000000;0.000000;0.000000;,
  1.000000;0.000000;0.000000;,
  0.000000;1.000000;0.000000;;
  1;
  3;0,1,2;;
  MESHNORMALS {
   1;
   0.000000;0.000000;1.000000;;
   1;
   3;0,0,0;;
  }
  MESHTEXTURECOORDS {
   3;
   0.100000;0.200000;,
   0.300000;0.400000;,
   0.500000;0.600000;;
  }
  meshmateriallist {
   1;
   1;
   0;;
   MaTeRiAl painted {
    0.400000;0.500000;0.600000;1.000000;;
    24.000000;
    0.100000;0.200000;0.300000;;
    0.010000;0.020000;0.030000;;
    TextureFileName {
     "fbx_texture_b.tga";
    }
   }
  }
  NoSuchTemplate {
   1;
  }
 }
}
'''


def main() -> int:
    directory = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(directory, exist_ok=True)
    path = os.path.join(directory, "x_type_name_case.x")
    with open(path, "w", newline="\n") as handle:
        handle.write(TEXT)
    print("wrote %s (%d bytes)" % (path, os.path.getsize(path)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
