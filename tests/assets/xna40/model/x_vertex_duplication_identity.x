xof 0303txt 0032
// Written by tools/xna-pipeline-oracle/model/make_degenerate_face_fixtures.py.

template VertexDuplicationIndices {
 <b8d65549-d7c9-4995-89cf-53a9a8b031e3>
 DWORD nIndices;
 DWORD nOriginalVertices;
 array DWORD indices[nIndices];
}

Mesh vertex_duplication_identity {
 6;
  0.000000;0.000000;0.000000;,
  4.000000;0.000000;0.000000;,
  0.000000;4.000000;0.000000;,
  0.000000;0.000000;4.000000;,
  4.000000;4.000000;0.000000;,
  -4.000000;0.000000;0.000000;;
 4;
  3;0,1,2;,
  3;0,4,5;,
  3;1,3,4;,
  3;1,2,3;;
 MeshNormals {
  6;
   1.000000;0.000000;0.000000;,
   0.000000;1.000000;0.000000;,
   0.000000;0.000000;1.000000;,
   1.000000;1.000000;0.000000;,
   0.000000;1.000000;1.000000;,
   1.000000;0.000000;1.000000;;
  4;
   3;0,1,2;,
   3;0,4,5;,
   3;1,3,4;,
   3;1,2,3;;
 }
 VertexDuplicationIndices {
  6;
  6;
  0,1,2,3,4,5;
 }
}
