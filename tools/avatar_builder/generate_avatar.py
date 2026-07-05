#!/usr/bin/env python3
"""Orchestrates Tasks 11.1-11.6 (skeleton, body, materials, morphs, hair/clothes,
animations) into one built avatar and exports it to a glTF2 `.glb` file.

Offline, one-time content-authoring tool — not part of the C++ build, never run by CNA
at runtime. Run headless via Blender, with this script's own arguments after `--` (so
Blender itself doesn't try to parse them):

    blender --background --python generate_avatar.py -- --gender male --out /tmp/male_avatar.glb
    blender --background --python generate_avatar.py -- --gender female --out /tmp/female_avatar.glb

Female uses the identical skeleton/rig and geometry-generation code as male, scaled down
by FEMALE_SCALE as a whole — a coarse, explicitly placeholder stand-in for real
proportion differentiation (different shoulder/hip width ratios, height, etc.), which is
deliberately deferred to a later, lower-priority iteration (plan_net.md Task 11.13,
"Parametric body variation") rather than attempted here.
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
import generate_body  # noqa: E402
import generate_materials  # noqa: E402
import generate_morphs  # noqa: E402
import generate_clothes  # noqa: E402
import generate_hair  # noqa: E402
import generate_animations  # noqa: E402
import export_gltf  # noqa: E402

# Coarse overall scale for the female variant. Not real proportion differentiation
# (shoulder/hip width ratio, head size, etc.) — see this module's docstring.
FEMALE_SCALE = 0.93


def build_avatar(gender):
    """Builds one full avatar (skeleton, body, materials, morphs, clothes, hair,
    animations) in a clean scene and returns (armature_obj, [armature_obj, body_obj,
    hair_obj, *garment_objs]) — the object list export_gltf.export_avatar() expects.
    `gender` is "male" or "female"."""
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)

    armature_obj = generate_skeleton.build_skeleton()
    body_obj = generate_body.build_body(armature_obj)
    materials = generate_materials.build_materials()
    generate_materials.assign_body_material(body_obj, materials)
    generate_morphs.build_morphs(body_obj)
    garments = generate_clothes.build_clothes(armature_obj, materials)
    hair_obj = generate_hair.build_hair(armature_obj, materials)
    generate_animations.build_animations(armature_obj)

    if gender == "female":
        armature_obj.scale = (FEMALE_SCALE, FEMALE_SCALE, FEMALE_SCALE)

    objects = [armature_obj, body_obj, hair_obj, *garments.values()]
    return armature_obj, objects


def _parse_args(argv):
    parser = argparse.ArgumentParser(description="Build and export a CNA procedural avatar.")
    parser.add_argument("--gender", choices=["male", "female"], default="male")
    parser.add_argument("--out", required=True, help="Output .glb path.")
    return parser.parse_args(argv)


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    args = _parse_args(argv)

    armature_obj, objects = build_avatar(args.gender)
    output_path = export_gltf.export_avatar(args.out, objects)

    print(f"Built and exported '{args.gender}' avatar ({len(objects)} objects: "
          f"{[o.name for o in objects]}) to {output_path}")
    assert output_path.exists() and output_path.stat().st_size > 0, "export produced an empty/missing file"
    print("OK: export file exists and is non-empty.")
