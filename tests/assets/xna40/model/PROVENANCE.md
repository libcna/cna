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

## The texture a fixture names

`quad_textured.x` names `surface.png`, and `surface.png` beside it is that file: the 4x4 probe the
texture corpus uses (`tests/assets/xna40/texture/probe_4x4.png`, byte for byte). Four by four
rather than the 2x2 everything else uses, because `ModelProcessor` builds a model's textures with
`TextureFormat` defaulting to `DxtCompressed`, and XNA refuses to DXT-compress a texture whose
dimensions are not multiples of four -- measured, `texture/png_texture_dxt` and
`model/x_textured` in the differential corpus. A 2x2 here would make this fixture one XNA itself
cannot build. It is here
so the model is a complete asset -- the source-to-output legs build it through the real
coordinator, which imports, processes and publishes the texture as its own output before the model
can refer to it, exactly as XNA's `MaterialProcessor` does. `two_materials.x` names `blue.dds`,
which is deliberately *not* committed: that fixture measures the importer, and a model whose
texture is missing is one of the things a build has to report rather than skip.
