#!/usr/bin/env python3
"""Orchestrates Tasks 11.1-11.6 (skeleton, body, materials, morphs, hair/clothes,
animations) into one built avatar and exports it to a glTF2 `.glb` file.

Offline, one-time content-authoring tool — not part of the C++ build, never run by CNA
at runtime. Run headless via Blender, with this script's own arguments after `--` (so
Blender itself doesn't try to parse them):

    blender --background --python generate_avatar.py -- --gender male --out /tmp/male_avatar.glb
    blender --background --python generate_avatar.py -- --gender female --out /tmp/female_avatar.glb

Task 11.13: `--gender` selects a GENDER_PRESETS entry (height/shoulder-width/head-size
scale), and `--height-scale`/`--shoulder-width-scale`/`--head-scale` override any of the
three individually — e.g. `--gender female --head-scale 1.1` starts from the female
preset but with a bigger head. These are real, independent parameters applied at
geometry-generation time (via generate_skeleton.build_bones()), not the coarse, uniform,
whole-armature post-scale this script used before Task 11.13 — conceptually echoing
AvatarDescription's customization intent (height, build) without attempting to
reconstruct its real, undocumented byte format.

Task 11.14: `--hair-style`/`--shirt-style`/`--pants-style` select a variant from
generate_hair.HAIRSTYLES / generate_clothes.GARMENT_STYLES, baked into this same combined
export (defaults reproduce this script's pre-Task-11.14 output exactly). To export a
single hair/clothing variant on its own, as a standalone attachable `.glb` instead, use
generate_wardrobe.py.
"""

import argparse
import sys
from pathlib import Path

try:
    import bpy
except ImportError:
    sys.exit("This script must be run inside Blender: blender --background --python generate_avatar.py -- --out FILE")

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_skeleton  # noqa: E402  (bpy path setup must happen first)
import generate_body_meshcraft as generate_body  # noqa: E402  (Task 7.4/7.5, plans/plan_net.md Phase 7:
# decision 4b - "Body/head ... geometry should be generated as glTF via the sibling ../mesh-craft
# tool going forward" - aliased under the old module's own name so build_avatar() below needs no
# further changes; generate_body.py itself is untouched and still importable/runnable standalone
# for anyone comparing the two approaches, it's just no longer what this orchestrator calls.
import generate_materials  # noqa: E402
import generate_morphs  # noqa: E402
import generate_clothes_meshcraft as generate_clothes  # noqa: E402  (Task 7.6, plans/plan_net.md
# Phase 7: same CSG-union fix as generate_body_meshcraft, for the same root cause - see that
# module's own docstring. generate_clothes.py itself is untouched/still runnable standalone.)
import generate_hair  # noqa: E402
import generate_animations  # noqa: E402
import export_gltf  # noqa: E402

# Coarse starting points, not measured/researched proportions — a placeholder female
# silhouette (shorter, narrower shoulders, very slightly smaller head) distinct from a
# unisex default, overridable per-parameter via the CLI below.
GENDER_PRESETS = {
    "male": {"height_scale": 1.0, "shoulder_width_scale": 1.0, "head_scale": 1.0},
    "female": {"height_scale": 0.93, "shoulder_width_scale": 0.85, "head_scale": 0.97},
}


def build_avatar(gender, height_scale=None, shoulder_width_scale=None, head_scale=None,
                  hair_style="Cap", garment_styles=None):
    """Builds one full avatar (skeleton, body, materials, morphs, clothes, hair,
    animations) in a clean scene and returns (armature_obj, [armature_obj, body_obj,
    hair_obj, *garment_objs]) — the object list export_gltf.export_avatar() expects.

    `gender` selects a GENDER_PRESETS entry as the starting point for the three scale
    parameters; passing height_scale/shoulder_width_scale/head_scale explicitly
    overrides that preset's corresponding value (Task 11.13).

    `hair_style` selects a generate_hair.HAIRSTYLES entry and `garment_styles`
    optionally overrides generate_clothes.DEFAULT_STYLES per slot (Task 11.14) — both
    default to this script's pre-Task-11.14 behavior."""
    preset = GENDER_PRESETS[gender]
    if height_scale is None:
        height_scale = preset["height_scale"]
    if shoulder_width_scale is None:
        shoulder_width_scale = preset["shoulder_width_scale"]
    if head_scale is None:
        head_scale = preset["head_scale"]

    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)

    bones = generate_skeleton.build_bones(height_scale=height_scale, shoulder_width_scale=shoulder_width_scale)
    armature_obj = generate_skeleton.build_skeleton(bones)
    body_obj = generate_body.build_body(armature_obj, bones=bones, height_scale=height_scale, head_scale=head_scale)
    materials = generate_materials.build_materials()
    generate_materials.assign_body_material(body_obj, materials)
    generate_morphs.build_morphs(body_obj, bones=bones, head_scale=head_scale)
    garments = generate_clothes.build_clothes(
        armature_obj, materials, bones=bones, height_scale=height_scale, styles=garment_styles)
    hair_obj = generate_hair.build_hair(
        armature_obj, materials, bones=bones, head_scale=head_scale, style=hair_style)
    generate_animations.build_animations(armature_obj, gender=gender)

    objects = [armature_obj, body_obj, hair_obj, *garments.values()]
    return armature_obj, objects


def _parse_args(argv):
    parser = argparse.ArgumentParser(description="Build and export a CNA procedural avatar.")
    parser.add_argument("--gender", choices=["male", "female"], default="male",
                         help="Selects a GENDER_PRESETS starting point for the scale parameters below.")
    parser.add_argument("--out", required=True, help="Output .glb path.")
    parser.add_argument("--height-scale", type=float, default=None,
                         help="Override --gender's preset height scale (Task 11.13).")
    parser.add_argument("--shoulder-width-scale", type=float, default=None,
                         help="Override --gender's preset shoulder-width scale (Task 11.13).")
    parser.add_argument("--head-scale", type=float, default=None,
                         help="Override --gender's preset head scale (Task 11.13).")
    parser.add_argument("--hair-style", choices=sorted(generate_hair.HAIRSTYLES), default="Cap",
                         help="Hair style variant (Task 11.14).")
    parser.add_argument("--shirt-style", choices=sorted(generate_clothes.GARMENT_STYLES["Shirt"]),
                         default=generate_clothes.DEFAULT_STYLES["Shirt"],
                         help="Shirt style variant (Task 11.14).")
    parser.add_argument("--pants-style", choices=sorted(generate_clothes.GARMENT_STYLES["Pants"]),
                         default=generate_clothes.DEFAULT_STYLES["Pants"],
                         help="Pants style variant (Task 11.14).")
    return parser.parse_args(argv)


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    args = _parse_args(argv)

    armature_obj, objects = build_avatar(
        args.gender, args.height_scale, args.shoulder_width_scale, args.head_scale,
        hair_style=args.hair_style,
        garment_styles={"Shirt": args.shirt_style, "Pants": args.pants_style})
    output_path = export_gltf.export_avatar(args.out, objects)

    print(f"Built and exported '{args.gender}' avatar ({len(objects)} objects: "
          f"{[o.name for o in objects]}) to {output_path}")
    assert output_path.exists() and output_path.stat().st_size > 0, "export produced an empty/missing file"
    print("OK: export file exists and is non-empty.")
