#!/usr/bin/env python3
"""Exports ONE hair style or ONE clothing variant (Task 11.14) as its own standalone,
attachable `.glb` — just that piece's mesh plus a copy of the CNAAvatarSkeleton armature
(needed for its vertex-group skinning data) at rest pose, independent of any specific
full-avatar export.

Complements generate_avatar.py, which always bakes hair + all three clothing slots into
one combined avatar `.glb`; this script is for producing a single swappable wardrobe
piece on its own, so it can be converted independently — CNA's existing
tools/avatar_asset_pipeline/convert_avatar.py already converts any GLB with one skin and
one-or-more meshes into a `.skinnedmodel.json` with one part per mesh, so an
armature-plus-single-mesh GLB from this script converts unmodified, no code changes
needed there.

No new C++ engine support is required or added by this script. AvatarRenderer/
SkinnedModelEXT currently load and draw exactly one model at a time (see
plans/plan_net.md Task 11.14's notes) — actually attaching a separately-converted wardrobe
piece onto a running avatar at draw time, alongside its body, is future, out-of-scope
engine work, not part of this task.

Offline, one-time content-authoring tool — not part of the C++ build, never run by CNA
at runtime. Run headless via Blender, with this script's own arguments after `--`:

    blender --background --python generate_wardrobe.py -- --piece hair --style Ponytail --out /tmp/hair_ponytail.glb
    blender --background --python generate_wardrobe.py -- --piece shirt --style LongSleeve --out /tmp/shirt_longsleeve.glb
"""

import argparse
import sys
from pathlib import Path

try:
    import bpy
except ImportError:
    sys.exit("This script must be run inside Blender: blender --background --python "
              "generate_wardrobe.py -- --piece hair --style Cap --out FILE.glb")

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_skeleton  # noqa: E402  (bpy path setup must happen first)
import generate_materials  # noqa: E402
import generate_clothes_meshcraft as generate_clothes  # noqa: E402  (Task 7.6, plans/plan_net.md
# Phase 7: matches generate_avatar.py's own aliasing - a standalone wardrobe clothing piece
# needs the same CSG-union fix as a full avatar's clothes, same root cause.)
import generate_hair  # noqa: E402
import export_gltf  # noqa: E402
from generate_avatar import GENDER_PRESETS  # noqa: E402

# CLI "piece" name -> generate_clothes.GARMENT_STYLES slot key. "hair" is handled
# separately (generate_hair.HAIRSTYLES has no slot concept, just one hair object).
_CLOTHING_SLOTS = {"shirt": "Shirt", "pants": "Pants", "shoes": "Shoes"}


def build_piece(piece, style, gender="male", height_scale=None, shoulder_width_scale=None, head_scale=None):
    """Clears the scene, builds a fresh skeleton at the given proportions (same
    GENDER_PRESETS/override semantics as generate_avatar.build_avatar(), Task 11.13),
    and builds exactly one hair style or clothing variant object on it — parented and
    auto-weighted the same way generate_avatar.py's full build does. Returns
    (armature_obj, piece_obj)."""
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
    materials = generate_materials.build_materials()

    if piece == "hair":
        piece_obj = generate_hair.build_hair(
            armature_obj, materials, bones=bones, head_scale=head_scale, style=style)
    elif piece in _CLOTHING_SLOTS:
        slot = _CLOTHING_SLOTS[piece]
        garments = generate_clothes.build_clothes(
            armature_obj, materials, bones=bones, height_scale=height_scale, styles={slot: style})
        piece_obj = garments[slot]
    else:
        raise ValueError(f"unknown piece {piece!r}; choices: hair, {', '.join(_CLOTHING_SLOTS)}")

    return armature_obj, piece_obj


def _parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Export one hair/clothing variant as a standalone attachable GLB (Task 11.14).")
    parser.add_argument("--piece", required=True, choices=["hair", *_CLOTHING_SLOTS],
                         help="Which wardrobe piece to build and export.")
    parser.add_argument("--style", required=True,
                         help="Style name within --piece's own registry "
                              "(generate_hair.HAIRSTYLES, or generate_clothes.GARMENT_STYLES[slot] "
                              "for shirt/pants/shoes).")
    parser.add_argument("--gender", choices=["male", "female"], default="male",
                         help="GENDER_PRESETS starting point for the skeleton this piece is fit to.")
    parser.add_argument("--out", required=True, help="Output .glb path.")
    parser.add_argument("--height-scale", type=float, default=None,
                         help="Override --gender's preset height scale (Task 11.13).")
    parser.add_argument("--shoulder-width-scale", type=float, default=None,
                         help="Override --gender's preset shoulder-width scale (Task 11.13).")
    parser.add_argument("--head-scale", type=float, default=None,
                         help="Override --gender's preset head scale (Task 11.13).")
    return parser.parse_args(argv)


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    args = _parse_args(argv)

    armature_obj, piece_obj = build_piece(
        args.piece, args.style, args.gender, args.height_scale, args.shoulder_width_scale, args.head_scale)
    output_path = export_gltf.export_avatar(args.out, [armature_obj, piece_obj])

    print(f"Built and exported '{args.piece}' style '{args.style}' ({piece_obj.name}, "
          f"{len(piece_obj.data.vertices)} vertices) to {output_path}")
    assert output_path.exists() and output_path.stat().st_size > 0, "export produced an empty/missing file"
    assert len(piece_obj.data.vertices) > 0, "piece mesh is empty"
    assert piece_obj.vertex_groups, "piece has no vertex groups — automatic weights did not run"
    print(f"OK: '{args.piece}' style '{args.style}' built, weighted, and exported as a "
          f"standalone armature+piece GLB, independent of any full avatar export.")
