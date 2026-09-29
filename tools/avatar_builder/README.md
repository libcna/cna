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

It writes catalog v3 to `modules/gamer-services/assets/avatars/v3/`; every `v<N>/` directory there
is embedded into the gamer-services library (`cmake/EmbedBinaryFiles.cmake`). A catalog must never
change once descriptions can name it: catalog v1 is frozen (its generator is in Git history,
commit 6ca06d069), so is catalog v2 (this generator at commit d1730d4f3), and changed or new items
go into a new version, distributed by the CNA service
(`docs/gamer-services-server.md`) to builds that do not embed it.

## Modules

| Module | Contents |
|---|---|
| `cna_avatar/rig.py` | The canonical rig: XNA's 71 slots and parent table, joint names, bind positions per body type, identity bind rotations. |
| `cna_avatar/mesh.py` | Parametric grids (ellipsoids, capsules, lofts, tubes) with analytic skin weights. |
| `cna_avatar/head.py` | The parametric head surface, nose, ears, feature anchors and conforming decal patches. |
| `cna_avatar/body.py` | The two bodies: superellipse torso, single-loft arms and legs, sculpted hands. |
| `cna_avatar/face.py` | The expression atlas (anti-aliased signed-distance shapes) and the decal placement. |
| `cna_avatar/facecontrols.py` | What each face-shape byte of a format 2 description does. |
| `cna_avatar/hair.py` | Scalp caps, locks, the ten styles and their under-hat variants. |
| `cna_avatar/garments.py` | Tops, bottoms and shoes as offset lofts over the body. |
| `cna_avatar/accessories.py` | Glasses, hats and facial hair. |
| `cna_avatar/textures.py` | Procedural texture multipliers (skin, hair, fabrics). |
| `cna_avatar/posing.py` | Forward kinematics, two-bone IK (hands reach targets, feet stay planted) and finger curl. |
| `cna_avatar/animations.py` | The 31 `AvatarAnimationPreset` clips as key poses with keyed expressions. |
| `cna_avatar/catalog.py` | The item table and GLB assembly. |
| `cna_avatar/glb.py`, `png.py` | The glTF 2.0 binary writer (cubic-spline animation, CNA material extras) and a PNG encoder. |
| `cna_avatar/glbread.py` | A small GLB reader for the preview. |
| `cna_avatar/description.py` | CNA descriptions (formats 1 and 2) in Python, for the review tools. |
| `cna_avatar/preview.py` | The texture-aware CPU preview. |

`generate_avatar_catalog.py` itself writes the files and `catalog.json`, which lists every file
with its size and SHA-256.

## Review

`avatar_review.py` renders one deterministic job list two ways, so art changes are judged on the
real renderer and iterated quickly on the CPU:

```
A=modules/gamer-services/assets/avatars; R=/rv/tmp/avatar-polish/review
python3 tools/avatar_builder/avatar_review.py jobs --catalogs $A --version 3 --out $R/jobs.json
python3 tools/avatar_builder/avatar_review.py preview $R/jobs.json $R/preview --catalogs $A   # numpy + Pillow
tools/platform/run_gpu_tests_private.sh --exec $PWD/cmake-build-opengl33/cna_avatar_review $R/jobs.json $R/gpu
python3 tools/avatar_builder/avatar_review.py sheets $R/jobs.json $R/gpu $R/sheets
```

The jobs cover a female and a male avatar from the front, three-quarter, profile and back with
head, hand and feet close-ups (close-ups keep their distance in proportion to the avatar's own head,
so catalogs with different head sizes are framed alike); 24 seeded random avatars; four key frames of every preset; and every
eye, eyebrow and mouth state plus independent left/right combinations. `cna_avatar_review`
(`modules/gamer-services/examples/avatar_review/`) draws them through the standard XNA avatar API
only. The CPU preview (`cna_avatar/preview.py`) assembles descriptions, samples animations,
textures and the expression atlas, and lights pixels the way the runtime does; it is a
development aid and the renderer captures are the reference.
