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

It writes catalog v3, CNA's baseline avatar catalog, to `modules/gamer-services/assets/avatars/v3/`,
which the gamer-services library embeds (`CNA_AVATAR_CATALOG_VERSIONS` in
`modules/gamer-services/CMakeLists.txt`, `cmake/EmbedBinaryFiles.cmake`), deflated. It writes
revision 2 of catalog v3: a revision may improve every item's art and add entries, but every id
keeps its kind and description format, and every added entry is `"format": 3`
(`AvatarCatalogTest.CatalogV3Revision2KeepsTheFirstReleaseAndAddsOnlyFormat3Entries`; the rules
and how to add an entry are in `docs/avatars.md`, "Assets"). The generator removes catalog files it
no longer writes. The development catalogs v1 and v2 that preceded v3 are not shipped or
supported; Git history keeps them.

## Modules

| Module | Contents |
|---|---|
| `cna_avatar/rig.py` | The canonical rig: XNA's 71 slots and parent table, joint names, bind positions per body type, identity bind rotations. |
| `cna_avatar/mesh.py` | Parametric grids (ellipsoids, capsules, lofts, tubes) with analytic skin weights. |
| `cna_avatar/head.py` | The parametric head surface, nose, the six ear shapes, feature anchors, the head surface radii and conforming decal patches. |
| `cna_avatar/body.py` | The two bodies: superellipse torso, single-loft arms and legs, sculpted hands. |
| `cna_avatar/face.py` | The expression atlas (anti-aliased signed-distance shapes): eyes, mouths, the eyebrow styles and makeup tiles, and the decal placement. |
| `cna_avatar/facecontrols.py` | What each of the 32 face-shape bytes (formats 2 and 3) does, and the named face presets. |
| `cna_avatar/hair.py` | Scalp caps, clumps and shells: the 37 hairstyles (hats fit them at run time from `hairCover`). |
| `cna_avatar/garments.py` | Tops, bottoms and shoes as offset lofts over the body, with fabrics. |
| `cna_avatar/accessories.py` | Glasses, headwear, facial hair, earrings, wristwear, gloves and rings. |
| `cna_avatar/textures.py` | Procedural texture multipliers (skin, hair, fabrics). |
| `cna_avatar/posing.py` | Forward kinematics, two-bone IK (hands reach targets, feet stay planted) and finger curl. |
| `cna_avatar/animations.py` | The 31 `AvatarAnimationPreset` clips as key poses with keyed expressions. |
| `cna_avatar/catalog.py` | The entry tables (ids, labels, random weights, formats, rules), outfits and GLB assembly. |
| `cna_avatar/glb.py`, `png.py` | The glTF 2.0 binary writer (cubic-spline animation, CNA material extras) and a PNG encoder. |
| `cna_avatar/glbread.py` | A small GLB reader for the preview. |
| `cna_avatar/description.py` | CNA descriptions (formats 1 to 3) in Python, for the review tools. |
| `cna_avatar/preview.py` | The texture-aware CPU preview. |

`generate_avatar_catalog.py` itself writes the files and `catalog.json`, which lists every file
with its size and SHA-256.

## Audit

`avatar_audit.py <catalogs> --version 3` measures what review sheets cannot: each body's proportions
(recorded in `docs/avatar-art-direction.md`), each kind's triangle, vertex and byte budget, and for
each garment, shoe, hat, glove and hairstyle how much skin pokes out through it in the bind pose
(exit status 1 past a threshold; a few vertices at a waistband or a tucked rim are allowed). The
content tests (`AvatarCatalogContentTests`) check the rest: sound geometry, every reference, and every
model skinned through every animation preset.

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
head, hand (front and back) and feet close-ups (close-ups keep their distance in proportion to the avatar's own head,
so catalogs with different head sizes are framed alike); 24 seeded random avatars; every face
control at both ends; every hairstyle, top, bottom, shoe, headwear, facial-hair style, accessory
and makeup style on the body it is weighted for, every hairstyle also from behind; every ear shape
in profile on both bodies; four
key frames of every preset; and every eye, eyebrow and mouth
state plus independent left/right combinations. `cna_avatar_review`
(`modules/gamer-services/examples/avatar_review/`) draws them through the standard XNA avatar API
only. The CPU preview (`cna_avatar/preview.py`) assembles descriptions, samples animations,
textures and the expression atlas, and lights pixels the way the runtime does; it is a
development aid and the renderer captures are the reference.
