#!/usr/bin/env python3
"""Builds CNA's placeholder hair — a simple helmet-like cap covering the upper half of
the Task 11.2 head sphere. No strand geometry, no card-based hair cards, just a single
open-bottomed hemisphere shell slightly larger than the head, parented to the skeleton
with automatic weights (which, being close only to the `Head` bone, ends up rigidly
following it in practice).

Explicitly expected to look like a helmet, not hair — a known, accepted limitation of
this first pass (see plan_net.md Phase 11's own framing of this exact caveat), not a bug
to chase down yet. A later iteration (plan_net.md Phase 11c/Task 11.14) can replace this
with actual hair-strand geometry or hair cards.

Offline, one-time content-authoring tool — not part of the C++ build, never run by CNA
at runtime. Run headless via Blender:

    blender --background --python generate_hair.py

`generate_avatar.py` (Task 11.7) will import build_hair() from this module and call it
in the same Blender process as the skeleton/body/material builders, rather than
re-deriving the hair mesh.
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


def build_hair(armature_obj, materials, bones=None, head_scale=1.0):
    """Builds the placeholder hair cap mesh object, parents it to `armature_obj` with
    automatic (heat-map) vertex weights, and assigns the `Hair` material from
    `materials` (as returned by generate_materials.build_materials()). Returns the hair
    mesh object. Safe to call repeatedly in the same Blender session — removes any
    pre-existing hair object first.

    `bones` optionally overrides the canonical `generate_skeleton.BONES` table (Task
    11.13) — must be the same bone list passed to `generate_skeleton.build_skeleton()`.
    `head_scale` scales the cap to match the (possibly independently-scaled) head."""
    if bones is None:
        bones = generate_skeleton.BONES
    head_center, hair_radius = _head_center_and_radius(bones, head_scale)

    existing = bpy.data.objects.get(HAIR_NAME)
    if existing is not None:
        bpy.data.meshes.remove(existing.data, do_unlink=True)

    mesh = _build_cap_mesh(hair_radius)
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
