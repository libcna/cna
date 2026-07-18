#!/usr/bin/env python3
"""Builds CNA's procedural avatar body — a stylized, low-poly humanoid mesh generated
directly from the Task 11.1 skeleton's own bone positions (see generate_skeleton.py /
README.md), then parented to that skeleton with Blender's automatic (heat-map) vertex
weights.

One primitive-derived "flesh" shape is created per bone (a cylinder along the bone's
head->tail axis, plus a small joint sphere at non-Head bones' head end; the Head bone
gets a single sphere instead of a cylinder), then all parts are joined into a single
mesh object. Building geometry directly from the bone list guarantees every deforming
bone has nearby mesh to receive automatic weights, and keeps body shape and skeleton
in lock-step without any manual authoring step.

Automatic weights are a starting point, not a finished result: expect visible bending
artifacts at elbows/knees/shoulders until a manual weight-painting correction pass is
done — do not treat a clean run of this script alone as proof the rig looks right in
motion. That visual check needs Task 11.6's test animations and is deferred until they
exist (see README.md's Status section).

Offline, one-time content-authoring tool — not part of the C++ build, never run by CNA
at runtime. Run headless via Blender:

    blender --background --python generate_body.py

`generate_avatar.py` (Task 11.7) will import build_body() from this module and call it
in the same Blender process as generate_skeleton.build_skeleton(), rather than
re-deriving the body shape.
"""

import sys
from pathlib import Path

try:
    import bpy
    import mathutils
except ImportError:
    sys.exit("This script must be run inside Blender: blender --background --python generate_body.py")

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_skeleton  # noqa: E402  (bpy path setup must happen first)

BODY_NAME = "CNAAvatarBody"

# Approximate limb radius (meters) per bone, used for both the cylinder "flesh"
# segment along the bone and the joint sphere at its head end. Purely a visual
# starting point for a stylized low-poly body, not anatomically precise.
BONE_RADII = {
    "Hips": 0.14, "Spine": 0.13, "Spine1": 0.13, "Neck": 0.05, "Head": 0.11,
    "Shoulder.L": 0.05, "Shoulder.R": 0.05,
    "UpperArm.L": 0.06, "UpperArm.R": 0.06,
    "LowerArm.L": 0.05, "LowerArm.R": 0.05,
    "Hand.L": 0.04, "Hand.R": 0.04,
    "UpperLeg.L": 0.09, "UpperLeg.R": 0.09,
    "LowerLeg.L": 0.07, "LowerLeg.R": 0.07,
    "Foot.L": 0.05, "Foot.R": 0.05,
}


def add_cylinder_segment(name, head, tail, radius):
    """Adds a cylinder running from world point `head` to `tail` with the given radius,
    oriented along that axis. Public so generate_hair.py/generate_clothes.py (Task 11.5)
    can build their own bone-aligned primitives with the same technique as the body."""
    head_v = mathutils.Vector(head)
    tail_v = mathutils.Vector(tail)
    direction = tail_v - head_v
    bpy.ops.mesh.primitive_cylinder_add(
        radius=radius, depth=direction.length, location=(head_v + tail_v) / 2, vertices=8,
    )
    obj = bpy.context.object
    obj.name = name
    obj.rotation_mode = "QUATERNION"
    obj.rotation_quaternion = mathutils.Vector((0.0, 0.0, 1.0)).rotation_difference(direction)
    return obj


def add_joint_sphere(name, location, radius):
    """Adds a low-poly UV sphere at `location`. Public for the same reason as
    add_cylinder_segment() above."""
    bpy.ops.mesh.primitive_uv_sphere_add(
        radius=radius, location=location, segments=8, ring_count=6,
    )
    obj = bpy.context.object
    obj.name = name
    return obj


# Task 11.20: (parent, child) bone pairs whose cylinder segments are only visually
# adjacent, not welded topology -- automatic (heat-map) weighting assigns most vertices
# on each side rigidly to their own segment's bone, so bending tears the seam open (the
# confirmed elbow/sleeve tear, Task 11.6/11.15/11.18). Shoulder/UpperArm is included
# defensively even though no current animation rotates Shoulder relative to its parent.
# Hips/Spine and Spine/Spine1 were added after Task 11.23b's FemaleIdleFixShoe (a 35 deg
# Spine forward bend) revealed the same class of tear at the torso, confirming this
# isn't an arms/legs-only problem -- any animated joint needs to be in this list.
# audit_net.md remediation (2026-07-18): LowerLeg/Foot was missing from this list --
# the ankle joint never got the smoothstep blend fix, so it kept automatic weighting's
# near-binary per-vertex tear even at rest pose (confirmed: the feet/shoes were the
# single darkest, most pose-independent artifact of everything the audit reported,
# consistent with a static unfixed tear rather than a pose-dependent skinning issue).
BEND_JOINTS = [
    ("Hips", "Spine"), ("Spine", "Spine1"),
    ("Shoulder.L", "UpperArm.L"), ("Shoulder.R", "UpperArm.R"),
    ("UpperArm.L", "LowerArm.L"), ("UpperArm.R", "LowerArm.R"),
    ("UpperLeg.L", "LowerLeg.L"), ("UpperLeg.R", "LowerLeg.R"),
    ("LowerLeg.L", "Foot.L"), ("LowerLeg.R", "Foot.R"),
]


def fix_automatic_weights(obj, bones, joint_pairs=None, blend_radius=0.08):
    """Post-processes `obj`'s automatic (heat-map) vertex weights, already assigned by
    a preceding `bpy.ops.object.parent_set(type="ARMATURE_AUTO")` call. Fixes three
    confirmed, real defects (Task 11.20; see tools/avatar_builder/README.md's "Bend-
    artifact check" and "Confirmed, not-fixed findings" for the original reports):

    1. Zero-weight vertices: automatic weighting can leave a handful of vertices with no
       weight in any group at all. Each is assigned to its nearest bone segment (by
       point-to-segment distance in world space) at weight 1.0.
    2. Over-4-influence vertices: glTF's hard 4-influence-per-vertex limit means the
       exporter silently trims/renormalizes any vertex automatic weighting gave more
       than 4 groups to. Explicitly capped here via `vertex_group_limit_total` instead,
       so the trimmed result is deterministic and produced by this pipeline, not by
       whatever the exporter's own trim heuristic happens to pick.
    3. The elbow/sleeve tear itself: for each (parent, child) pair in `joint_pairs`,
       every vertex within `blend_radius` of the joint (the child bone's head) gets its
       parent/child weights forced to a smoothstep blend by signed distance along the
       parent bone's axis, instead of automatic weighting's near-binary per-vertex
       assignment. This does not weld the segments into one continuous surface (they
       remain topologically separate cylinders) — it makes both sides rotate partway
       toward each other's transform near the joint, which shrinks the visible gap at
       the bend angles this rig's animations actually use, rather than eliminating the
       underlying topology gap outright.

    `bones` must be the same (name, parent, head, tail, connected) table passed to
    generate_skeleton.build_skeleton() for this object's armature, needed for #1/#3's
    positions. Returns (zero_weight_fixed, joint_blended) vertex counts, for callers'
    own verification/reporting.
    """
    if joint_pairs is None:
        joint_pairs = BEND_JOINTS
    bone_segments = {name: (mathutils.Vector(head), mathutils.Vector(tail))
                      for name, _parent, head, tail, _connected in bones}
    mesh = obj.data

    def _closest_point_on_segment(p, a, b):
        ab = b - a
        denom = ab.length_squared
        t = 0.0 if denom == 0.0 else max(0.0, min(1.0, (p - a).dot(ab) / denom))
        return a + ab * t

    # --- 1: zero-weight vertices -> nearest bone segment, weight 1.0 ---
    zero_fixed = 0
    for v in mesh.vertices:
        if sum(g.weight for g in v.groups) > 1e-6:
            continue
        world_co = obj.matrix_world @ v.co
        best_name, best_dist_sq = None, None
        for name, (head, tail) in bone_segments.items():
            closest = _closest_point_on_segment(world_co, head, tail)
            dist_sq = (world_co - closest).length_squared
            if best_dist_sq is None or dist_sq < best_dist_sq:
                best_name, best_dist_sq = name, dist_sq
        group = obj.vertex_groups.get(best_name) or obj.vertex_groups.new(name=best_name)
        group.add([v.index], 1.0, "REPLACE")
        zero_fixed += 1

    # --- 3: smooth parent/child weight blend across each bend joint ---
    # (done before the influence cap below, since blending can add a group to a vertex
    # that didn't already have one)
    blended = 0
    for parent_name, child_name in joint_pairs:
        parent_group = obj.vertex_groups.get(parent_name)
        child_group = obj.vertex_groups.get(child_name)
        if parent_group is None or child_group is None:
            continue
        joint_pos, child_tail = bone_segments[child_name]
        axis = (child_tail - joint_pos).normalized()
        for v in mesh.vertices:
            offset = obj.matrix_world @ v.co - joint_pos
            signed_dist = offset.dot(axis)
            if abs(signed_dist) > blend_radius:
                continue
            # audit_net.md remediation (2026-07-18, fifth round): the axial test alone selects an
            # INFINITE SLAB perpendicular to the bone axis, so it forced parent/child weights onto
            # vertices arbitrarily far from the joint sideways. For a laterally-pointing arm bone
            # that slab sweeps straight down through the torso, hips and legs - which is how
            # CNAAvatarPants ended up weighted to Shoulder.L/Shoulder.R (measured: 108 and 107
            # vertices, via tools/avatar_builder/diagnose_avatar_mesh.py's `weights` check). Those
            # spurious weights make hips/leg geometry follow the shoulders during arm animations
            # like `Wave`, deforming garments away from the body they cover. Gate on perpendicular
            # distance too, turning the region into a bounded cylinder around the joint axis
            # instead of a slab.
            perpendicular = (offset - axis * signed_dist).length
            if perpendicular > blend_radius:
                continue
            t = max(0.0, min(1.0, (signed_dist / blend_radius + 1.0) * 0.5))
            t = t * t * (3.0 - 2.0 * t)  # smoothstep
            parent_group.add([v.index], 1.0 - t, "REPLACE")
            child_group.add([v.index], t, "REPLACE")
            blended += 1

    # --- 2: cap every vertex to <=4 influences (glTF's hard limit) ---
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.vertex_group_limit_total(group_select_mode="ALL", limit=4)
    bpy.ops.object.vertex_group_normalize_all(group_select_mode="ALL", lock_active=False)

    return zero_fixed, blended


def build_body(armature_obj, bones=None, height_scale=1.0, head_scale=1.0):
    """Builds the procedural low-poly body mesh, joins every part into a single mesh
    object, and parents it to `armature_obj` with automatic (heat-map) vertex weights.
    Returns the body mesh object. Safe to call repeatedly in the same Blender session
    (e.g. from generate_avatar.py) — removes any pre-existing body object first.

    `bones` optionally overrides the canonical `generate_skeleton.BONES` table (e.g.
    with `generate_skeleton.build_bones(...)`'s output, Task 11.13) — must be the SAME
    bone list passed to `generate_skeleton.build_skeleton()`, so the body's geometry
    lines up with the actual armature. `height_scale` scales every non-Head bone's
    flesh radius to match (Task 11.13's height parameter, applied here since bone
    *positions* already carry it via `bones`, but radius does not scale automatically).
    `head_scale` scales the Head bone's own radius independently of height (Task 11.13's
    head-size parameter — deliberately decoupled from height_scale, e.g. for a
    disproportionately large/small "chibi"-style head)."""
    if bones is None:
        bones = generate_skeleton.BONES

    existing = bpy.data.objects.get(BODY_NAME)
    if existing is not None:
        bpy.data.meshes.remove(existing.data, do_unlink=True)

    parts = []
    for name, _parent, head, tail, _connected in bones:
        radius = BONE_RADII[name] * (head_scale if name == "Head" else height_scale)
        if name == "Head":
            center = tuple((mathutils.Vector(head) + mathutils.Vector(tail)) / 2)
            parts.append(add_joint_sphere(f"{name}_flesh", center, radius))
        else:
            parts.append(add_cylinder_segment(f"{name}_flesh", head, tail, radius))
            parts.append(add_joint_sphere(f"{name}_joint", head, radius))

    bpy.ops.object.select_all(action="DESELECT")
    for part in parts:
        part.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()

    body_obj = bpy.context.object
    body_obj.name = BODY_NAME
    body_obj.data.name = BODY_NAME

    bpy.ops.object.select_all(action="DESELECT")
    body_obj.select_set(True)
    armature_obj.select_set(True)
    bpy.context.view_layer.objects.active = armature_obj
    bpy.ops.object.parent_set(type="ARMATURE_AUTO")
    fix_automatic_weights(body_obj, bones)

    return body_obj


if __name__ == "__main__":
    armature_obj = generate_skeleton.build_skeleton()
    body_obj = build_body(armature_obj)

    bone_names = {name for name, *_rest in generate_skeleton.BONES}
    group_names = {g.name for g in body_obj.vertex_groups}
    missing = bone_names - group_names

    print(f"Built body '{body_obj.name}' with {len(body_obj.data.vertices)} vertices, "
          f"{len(body_obj.vertex_groups)} vertex groups.")
    if missing:
        print(f"WARNING: no vertex group for bones: {sorted(missing)}")
    assert not missing, f"automatic weights produced no vertex group for: {sorted(missing)}"
    print("OK: every skeleton bone has a corresponding vertex group on the body mesh.")

    # Task 11.20: independently re-check fix_automatic_weights' two hard guarantees by
    # direct per-vertex inspection, rather than trusting its own return value.
    zero_weight = [v.index for v in body_obj.data.vertices if sum(g.weight for g in v.groups) < 1e-6]
    over_limit = [v.index for v in body_obj.data.vertices if len(v.groups) > 4]
    print(f"Zero-weight vertices: {len(zero_weight)}. Over-4-influence vertices: {len(over_limit)}.")
    assert not zero_weight, f"zero-weight vertices remain: {zero_weight[:10]}"
    assert not over_limit, f"over-4-influence vertices remain: {over_limit[:10]}"
    print("OK: no zero-weight or over-4-influence vertices remain on the body mesh.")
