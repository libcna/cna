xof 0303txt 0032
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
