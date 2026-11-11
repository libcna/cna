xof 0303txt 0032
// Written by tools/xna-pipeline-oracle/model/make_vertex_duplication_probes.py (T01_002).
template VertexDuplicationIndices {
 <b8d65549-d7c9-4995-89cf-53a9a8b031e3>
 DWORD nIndices;
 DWORD nOriginalVertices;
 array DWORD indices[nIndices];
}

Mesh dup_pair {
 3;
  1.000000;0.000000;0.000000;,
  0.000000;2.000000;0.000000;,
  3.000000;0.000000;0.000000;;
 1;
  3;0,1,2;;
 MeshNormals {
  3;
  0.000000;1.000000;0.000000;,
  1.000000;0.000000;0.000000;,
  0.000000;0.000000;1.000000;;
  1;
  3;0,1,2;;
 }
 VertexDuplicationIndices {
  3;
  3;
  0,0,2;
 }
}
