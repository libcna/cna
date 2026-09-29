# Avatar catalog generator

`generate_avatar_catalog.py` writes the original CNA avatar catalog behind the standard XNA
avatar API (`AvatarDescription`, `AvatarAnimation`, `AvatarRenderer`; see `docs/avatars.md`). It
is pure Python with no dependencies, downloads or third-party content, and its output is
byte-for-byte reproducible:

```
python3 tools/avatar_builder/generate_avatar_catalog.py           # regenerate
python3 tools/avatar_builder/generate_avatar_catalog.py --check   # verify (also the ctest
                                                                  # GamerServices_AvatarCatalogUpToDate)
python3 tools/avatar_builder/generate_avatar_catalog.py --out DIR # write elsewhere
```

The default output is `modules/gamer-services/assets/avatars/v1/`, which the build embeds into
the gamer-services library (`cmake/EmbedBinaryFiles.cmake`). A catalog file must never change once
a description can name it: new or changed items go into a new catalog version, distributed by the
CNA service (`docs/gamer-services-server.md`), not into v1.

## Modules

| Module | Contents |
|---|---|
| `cna_avatar/rig.py` | The canonical rig: XNA's 71 slots and parent table, joint names, bind positions per body type, identity bind rotations. |
| `cna_avatar/mesh.py` | Mesh primitives (ellipsoids, tapered capsules, lofts, tubes, boxes) with analytic skin weights. |
| `cna_avatar/body.py` | The two bodies and the face-decal patches. |
| `cna_avatar/wardrobe.py` | Every item a description can name, fitted to both bodies. |
| `cna_avatar/face.py` | The expression atlas: every eye (with a separate iris layer), eyebrow and mouth state. |
| `cna_avatar/posing.py` | Forward kinematics, two-bone IK (hands reach targets, feet stay planted) and finger curl. |
| `cna_avatar/animations.py` | The 31 `AvatarAnimationPreset` clips as key poses with keyed expressions. |
| `cna_avatar/catalog.py` | Assembles the body, item and animation GLBs. |
| `cna_avatar/glb.py`, `png.py` | The glTF 2.0 binary writer (cubic-spline animation, CNA material extras) and a PNG encoder. |
| `cna_avatar/glbread.py` | A small GLB reader for the preview. |

`generate_avatar_catalog.py` itself writes the files and `catalog.json`, which lists every file
with its size and SHA-256.

## Previews

`preview_avatar.py` renders a GLB set to a PNG on the CPU (needs Pillow), optionally posed at a
time in one of the animations:

```
A=modules/gamer-services/assets/avatars/v1
python3 tools/avatar_builder/preview_avatar.py out.png $A/body.male.glb $A/top_tshirt.male.glb \
    --pose $A/animations.glb Wave 1.2
```

It is a development aid; the runtime contract, including everything the C++ reader refuses, is
in `docs/avatars.md`.
