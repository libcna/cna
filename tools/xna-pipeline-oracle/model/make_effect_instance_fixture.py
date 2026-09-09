#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-174: what a `.x` `EffectInstance` becomes.

SAMPLE-028's `Car.x` gives one of its materials an `EffectInstance` naming `ReplaceColor.fx`, and
XNA's build answers an `EffectMaterialReader` where CNA answered a `BasicEffectReader`. The fixture
carries one of each parameter template the format defines -- `EffectParamString`, `EffectParamFloats`
and `EffectParamDWord` -- so that what each becomes is separately observable, and the material's own
colours are set to values that would be visible if they survived (they do not).

An `EffectParamFloats` is typed by its *count* rather than by anything it declares, so all five
counts the pipeline reads are here: one float is a `Single`, two a `Vector2`, three a `Vector3`,
four a `Vector4` and sixteen a `Matrix`. The parameters are declared out of alphabetical order,
because the built `.xnb` writes the table sorted and the file's own order is what would show if it
did not. Counts the genuine importer answers as `System.Single[]` are deliberately absent: CNA has
no array-valued content object and refuses such a file rather than dropping the parameter.

    python3 tools/xna-pipeline-oracle/model/make_effect_instance_fixture.py <directory>
"""
from __future__ import annotations

import os
import sys

TEXT = '''xof 0303txt 0032
// Written by tools/xna-pipeline-oracle/model/make_effect_instance_fixture.py for this repository.

template EffectInstance {
 <e331f7e4-0559-4cc2-8e99-1cec1657928f>
 STRING EffectFilename;
 [...]
}

template EffectParamFloats {
 <3014b9a0-62f5-478c-9b86-e4ac9f4e418b>
 STRING ParamName;
 DWORD nFloats;
 array FLOAT Floats[nFloats];
}

template EffectParamString {
 <1dbc4c88-94c1-46ee-9076-2c28818c9481>
 STRING ParamName;
 STRING Value;
}

template EffectParamDWord {
 <e13963bc-ae51-4c5d-b00f-cfa3a9d97ce5>
 STRING ParamName;
 DWORD Value;
}

Mesh effect_instance {
 4;
 0.000000;0.000000;0.000000;,
 10.000000;0.000000;0.000000;,
 0.000000;10.000000;0.000000;,
 10.000000;10.000000;0.000000;;
 2;
 3;0,1,2;,
 3;1,3,2;;
 MeshMaterialList {
  1;
  2;
  0,
  0;;
  Material Painted {
   0.400000;0.500000;0.600000;1.000000;;
   24.000000;
   0.100000;0.200000;0.300000;;
   0.010000;0.020000;0.030000;;
   EffectInstance {
    "x_effect_instance.fx";
    EffectParamString {
     "DiffuseTexture";
     "fbx_texture_b.tga";
    }
    EffectParamFloats {
     "Tint";
     3;
     0.250000,0.500000,0.750000;;
    }
    EffectParamFloats {
     "Strength";
     1;
     0.125000;;
    }
    EffectParamFloats {
     "Offset";
     2;
     0.375000,0.625000;;
    }
    EffectParamFloats {
     "Corners";
     4;
     0.100000,0.200000,0.300000,0.400000;;
    }
    EffectParamFloats {
     "World";
     16;
     1.000000,0.000000,0.000000,0.000000,0.000000,1.000000,0.000000,0.000000,0.000000,0.000000,1.000000,0.000000,7.000000,8.000000,9.000000,1.000000;;
    }
    EffectParamDWord {
     "Passes";
     2;
    }
   }
  }
 }
 MeshNormals {
  4;
  0.000000;0.000000;1.000000;,
  0.000000;0.000000;1.000000;,
  0.000000;0.000000;1.000000;,
  0.000000;0.000000;1.000000;;
  2;
  3;0,1,2;,
  3;1,3,2;;
 }
}
'''

EFFECT = '''// SPDX-License-Identifier: MS-PL
// The effect x_effect_instance.x names. Nothing about the importer's answer depends on what it
// compiles to; it has to exist because the reference is resolved against the file beside it.
float3 Tint;
float Strength;
float2 Offset;
float4 Corners;
float4x4 World;
int Passes;
texture DiffuseTexture;
technique Replace
{
    pass Single
    {
    }
}
'''


def main() -> int:
    directory = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(directory, exist_ok=True)
    path = os.path.join(directory, "x_effect_instance.x")
    with open(path, "w", newline="\n") as handle:
        handle.write(TEXT)
    effect = os.path.join(directory, "x_effect_instance.fx")
    with open(effect, "w", newline="\n") as handle:
        handle.write(EFFECT)
    print("wrote %s and %s" % (path, effect))
    return 0


if __name__ == "__main__":
    sys.exit(main())
