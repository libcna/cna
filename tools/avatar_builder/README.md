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

## Review

`avatar_review.py` renders one deterministic job list two ways, so art changes are judged on the
real renderer and iterated quickly on the CPU:

```
A=modules/gamer-services/assets/avatars; R=/rv/tmp/avatar-polish/review
python3 tools/avatar_builder/avatar_review.py jobs --catalogs $A --version 1 --out $R/jobs.json
python3 tools/avatar_builder/avatar_review.py preview $R/jobs.json $R/preview --catalogs $A   # numpy + Pillow
tools/platform/run_gpu_tests_private.sh --exec $PWD/cmake-build-opengl33/cna_avatar_review $R/jobs.json $R/gpu
python3 tools/avatar_builder/avatar_review.py sheets $R/jobs.json $R/gpu $R/sheets
```

The jobs cover a female and a male avatar from the front, three-quarter, profile and back with
head, hand and feet close-ups; 24 seeded random avatars; four key frames of every preset; and every
eye, eyebrow and mouth state plus independent left/right combinations. `cna_avatar_review`
(`modules/gamer-services/examples/avatar_review/`) draws them through the standard XNA avatar API
only. The CPU preview (`cna_avatar/preview.py`) assembles descriptions, samples animations,
textures and the expression atlas, and lights pixels the way the runtime does; it is a
development aid and the renderer captures are the reference.
