# DirectX `.x` corpus (authored)

Written by `tools/xna-pipeline-oracle/model/make-model-fixtures.py` for this
repository. Every construct the importer must read appears in a file whose
exact content is known: a frame hierarchy with transforms, a mesh with
normals, texture coordinates and vertex colours, a material list with a
texture reference, skin weights over two bones, and an animation set. The
binary file is the same object model written through this script's own
tokenizer. Nothing here was downloaded and nothing is third-party content.

| File | Bytes |
|---|---:|
| `anim_default_rate.x` | 458 |
| `bad_version.x` | 32 |
| `bare_mesh.x` | 157 |
| `binary_mesh.x` | 248 |
| `empty.x` | 0 |
| `hierarchy.x` | 647 |
| `index_out_of_range.x` | 158 |
| `not_x.x` | 35 |
| `oblique_normals.x` | 482 |
| `quad_textured.x` | 1372 |
| `skinned_animated.x` | 1952 |
| `skinned_two_animations.x` | 1644 |
| `transform_z.x` | 397 |
| `truncated.x` | 120 |
| `two_animations.x` | 675 |
| `two_bones_animated.x` | 1589 |
| `two_materials.x` | 713 |
| `with_templates.x` | 581 |
