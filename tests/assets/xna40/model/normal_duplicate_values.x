xof 0303txt 0032
// Written for this repository, plans/plan_xna_sample_xnb_sweep.md XNASWEEP-148.
// Two triangles sharing an edge, and a MeshNormals list that writes the same normal six times
// under six different indices: the distinct (position, normal value) pairs are four, the distinct
// (position, normal index) pairs are six.
Mesh Doubled {
 4;
 0.000000;0.000000;0.000000;,
 1.000000;0.000000;0.000000;,
 1.000000;1.000000;0.000000;,
 0.000000;1.000000;0.000000;;
 2;
 3;0,1,2;,
 3;0,2,3;;
 MeshNormals {
  6;
  0.000000;0.000000;1.000000;,
  0.000000;0.000000;1.000000;,
  0.000000;0.000000;1.000000;,
  0.000000;0.000000;1.000000;,
  0.000000;0.000000;1.000000;,
  0.000000;0.000000;1.000000;;
  2;
  3;0,1,2;,
  3;3,4,5;;
 }
}
