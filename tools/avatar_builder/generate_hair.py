#!/usr/bin/env python3
"""Builds CNA's placeholder hair, selectable between two styles (Task 11.14):

- `Cap` — a simple helmet-like cap covering the upper half of the Task 11.2 head sphere.
  No strand geometry, no card-based hair cards, just a single open-bottomed hemisphere
  shell slightly larger than the head.
- `Ponytail` — the same cap, plus a tapered cone "tail" hanging off the back of the head,
  drooping down and backward — a simple, obviously-distinct silhouette from `Cap`.

Both are a single mesh object parented to the skeleton with automatic weights (which,
being close only to the `Head` bone, ends up rigidly following it in practice).

Explicitly expected to still look crude, not like real hair strands — a known, accepted
limitation of this iteration (see plans/plan_net.md Phase 11's own framing of this exact
caveat), not a bug to chase down yet. A later iteration can replace either style with
actual hair-strand geometry or hair cards.

Offline, one-time content-authoring tool — not part of the C++ build, never run by CNA
at runtime. Run headless via Blender:

    blender --background --python generate_hair.py

`generate_avatar.py` (Task 11.7) will import build_hair() from this module and call it
in the same Blender process as the skeleton/body/material builders, rather than
re-deriving the hair mesh. `generate_wardrobe.py` (Task 11.14) can also export any one
hair style on its own, as a standalone attachable `.glb`.
"""

import sys
from pathlib import Path

try:
    import bpy
    import bmesh
    import mathutils
except ImportError:
    sys.exit("This script must be run inside Blender: blender --background --python generate_hair.py")

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_skeleton  # noqa: E402  (bpy path setup must happen first)
import generate_body  # noqa: E402
import generate_materials  # noqa: E402

HAIR_NAME = "CNAAvatarHair"

# Outward padding (meters) added to the head radius so the cap sits just outside the
# head surface rather than z-fighting with it.
HAIR_PADDING = 0.015


def _head_center_and_radius(bones, head_scale):
    """Head sphere placement, mirrored from generate_body.py's Head handling: center is
    the midpoint of the Head bone's head/tail (from `bones`, Task 11.13's optionally-
    scaled bone list), radius from BONE_RADII["Head"] * head_scale (independent of
    height_scale — see generate_body.build_body()'s docstring for why)."""
    head_head, head_tail = next(
        (head, tail) for name, _parent, head, tail, _connected in bones if name == "Head"
    )
    center = mathutils.Vector(head_head).lerp(mathutils.Vector(head_tail), 0.5)
    radius = generate_body.BONE_RADII["Head"] * head_scale + HAIR_PADDING
    return center, radius


def _build_cap_mesh(radius):
    """Returns a new mesh: the upper hemisphere (z >= 0, in the mesh's own local space)
    of a UV sphere, open at the bottom — a basic cap/helmet shape."""
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=8, v_segments=6, radius=radius)
    lower_verts = [v for v in bm.verts if v.co.z < -1e-4]
    bmesh.ops.delete(bm, geom=lower_verts, context="VERTS")

    mesh = bpy.data.meshes.new(HAIR_NAME)
    bm.to_mesh(mesh)
    bm.free()
    return mesh


def _build_ponytail_mesh(radius):
    """Returns a new mesh: the same open-bottomed cap hemisphere as `_build_cap_mesh()`,
    plus a tapered cone "tail" hanging off the back of the head (-Y, matching the
    skeleton's own +Y-forward convention), drooping down and backward — a simple,
    obviously-distinct silhouette from the plain `Cap` style, built entirely with bmesh
    ops so it stays a single mesh/object like `Cap` (no join step needed)."""
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=8, v_segments=6, radius=radius)
    lower_verts = [v for v in bm.verts if v.co.z < -1e-4]
    bmesh.ops.delete(bm, geom=lower_verts, context="VERTS")

    droop_direction = mathutils.Vector((0.0, -0.5, -0.85)).normalized()
    rotation = mathutils.Vector((0.0, 0.0, 1.0)).rotation_difference(droop_direction).to_matrix().to_4x4()
    translation = mathutils.Matrix.Translation((0.0, -radius * 0.75, radius * 0.15))
    bmesh.ops.create_cone(
        bm, cap_ends=True, segments=10,
        radius1=radius * 0.35, radius2=radius * 0.08, depth=radius * 2.0,
        matrix=translation @ rotation,
    )

    mesh = bpy.data.meshes.new(HAIR_NAME)
    bm.to_mesh(mesh)
    bm.free()
    return mesh


# Style name -> mesh-builder function (radius -> bpy.types.Mesh). "Cap" is the original
# Task 11.5 style and every build_hair() call site's default, so nothing that doesn't
# ask for "Ponytail" changes behavior (Task 11.14).
HAIRSTYLES = {
    "Cap": _build_cap_mesh,
    "Ponytail": _build_ponytail_mesh,
}


def build_hair(armature_obj, materials, bones=None, head_scale=1.0, style="Cap"):
    """Builds the placeholder hair mesh object for the given `style` (a HAIRSTYLES key),
    parents it to `armature_obj` with automatic (heat-map) vertex weights, and assigns
    the `Hair` material from `materials` (as returned by
    generate_materials.build_materials()). Returns the hair mesh object. Safe to call
    repeatedly in the same Blender session — removes any pre-existing hair object first.

    `bones` optionally overrides the canonical `generate_skeleton.BONES` table (Task
    11.13) — must be the same bone list passed to `generate_skeleton.build_skeleton()`.
    `head_scale` scales the style's geometry to match the (possibly
    independently-scaled) head. `style` selects a HAIRSTYLES entry (Task 11.14);
    defaults to `"Cap"`, matching every call site's behavior before Task 11.14."""
    if bones is None:
        bones = generate_skeleton.BONES
    if style not in HAIRSTYLES:
        raise ValueError(f"unknown hair style {style!r}; choices: {sorted(HAIRSTYLES)}")
    head_center, hair_radius = _head_center_and_radius(bones, head_scale)

    existing = bpy.data.objects.get(HAIR_NAME)
    if existing is not None:
        bpy.data.meshes.remove(existing.data, do_unlink=True)

    mesh = HAIRSTYLES[style](hair_radius)
    obj = bpy.data.objects.new(HAIR_NAME, mesh)
    bpy.context.collection.objects.link(obj)
    obj.location = head_center

    obj.data.materials.clear()
    obj.data.materials.append(materials["Hair"])

    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    armature_obj.select_set(True)
    bpy.context.view_layer.objects.active = armature_obj
    bpy.ops.object.parent_set(type="ARMATURE_AUTO")
    generate_body.fix_automatic_weights(obj, bones)

    return obj


if __name__ == "__main__":
    armature_obj = generate_skeleton.build_skeleton()
    body_obj = generate_body.build_body(armature_obj)
    materials = generate_materials.build_materials()
    generate_materials.assign_body_material(body_obj, materials)
    hair_obj = build_hair(armature_obj, materials)

    group_names = {g.name for g in hair_obj.vertex_groups}
    print(f"Built '{hair_obj.name}' with {len(hair_obj.data.vertices)} vertices, "
          f"{len(hair_obj.vertex_groups)} vertex groups, material "
          f"{hair_obj.data.materials[0].name if hair_obj.data.materials else '(none)'}.")

    assert "Head" in group_names, "hair mesh got no vertex group for the Head bone"
    assert hair_obj.data.materials and hair_obj.data.materials[0].name == materials["Hair"].name
    assert len(hair_obj.data.vertices) > 0, "hair cap mesh is empty"
    print("OK: hair cap exists, is vertex-grouped to Head, and has the Hair material assigned.")
