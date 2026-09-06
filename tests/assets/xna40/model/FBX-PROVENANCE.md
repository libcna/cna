# FBX corpus (authored)

Written by `tools/xna-pipeline-oracle/model/make_fbx_fixtures.py`, in FBX 6.1
ASCII. That version is chosen for a measured reason: XNA's importer carries FBX
SDK 2011.3.1, and an FBX written by a current tool is version 7400 or 7500, which
that SDK refuses -- `Error code: 0 encountered when importing the scene`, recorded
in the oracle for an Assimp-written file. Nothing here was downloaded and nothing
is third-party content.

| File | Bytes |
|---|---:|
| `fbx_bare_mesh.fbx` | 1270 |
| `fbx_empty.fbx` | 0 |
| `fbx_hierarchy.fbx` | 2058 |
| `fbx_not_fbx.fbx` | 31 |
| `fbx_not_fbx_large.fbx` | 1024 |
| `fbx_oblique.fbx` | 1787 |
| `fbx_quad_polygon.fbx` | 1288 |
| `fbx_quad_textured.fbx` | 2531 |
| `fbx_truncated.fbx` | 400 |
| `fbx_two_materials.fbx` | 2484 |
