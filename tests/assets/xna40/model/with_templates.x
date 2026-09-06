xof 0303txt 0032

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
