#!/usr/bin/env python3
"""Builds CNA's procedural avatar skeleton — a new, CNA-original ~19-bone biped
armature (see README.md for the full bone list/hierarchy, the source of truth other
Phase 11 scripts key off of).

This is NOT the real Xbox 71-bone hierarchy used by the faithful XNA Avatar port
(Microsoft::Xna::Framework::GamerServices::AvatarRenderer::ParentBones, Phase 8) and NOT
Mixamo/Rigify naming — it exists purely so that this same pipeline's own animation
generator (generate_animations.py, Task 11.6) can author clips directly on these bone
names, with zero retargeting.

Offline, one-time content-authoring tool — not part of the C++ build, never run by CNA
at runtime. Run headless via Blender:

    blender --background --python generate_skeleton.py

Other Phase 11 scripts import build_skeleton()/BONES from this module (run inside the
same Blender process via generate_avatar.py, Task 11.7) rather than re-deriving the
bone list. BONES/ARMATURE_NAME are plain data with no bpy dependency, so
validate_gltf.py (Task 11.8) can `import generate_skeleton` from ordinary `python3`
(outside Blender) to check exported joint names against this same source of truth —
only build_skeleton() itself needs to run inside Blender.
"""

import sys

ARMATURE_NAME = "CNAAvatarSkeleton"

# Each entry: (bone name, parent name or None, head (x, y, z), tail (x, y, z), connected).
# `connected` may only be True when head == parent's tail (Blender requirement).
# Coordinates are in meters, Z-up, for a roughly 1.8m-tall standing rest pose centered
# on the X=0 sagittal plane (+X = the figure's left, matching the ".L"/".R" suffixes).
BONES = [
    ("Hips",         None,           (0.0,  0.0, 1.00), (0.0,  0.0, 1.10), False),
    ("Spine",        "Hips",         (0.0,  0.0, 1.10), (0.0,  0.0, 1.25), True),
    ("Spine1",       "Spine",        (0.0,  0.0, 1.25), (0.0,  0.0, 1.40), True),
    ("Neck",         "Spine1",       (0.0,  0.0, 1.40), (0.0,  0.0, 1.50), True),
    ("Head",         "Neck",         (0.0,  0.0, 1.50), (0.0,  0.0, 1.70), True),

    ("Shoulder.L",   "Spine1",       (0.0,  0.0, 1.40), (0.18, 0.0, 1.40), True),
    ("Shoulder.R",   "Spine1",       (0.0,  0.0, 1.40), (-0.18, 0.0, 1.40), True),
    ("UpperArm.L",   "Shoulder.L",   (0.18, 0.0, 1.40), (0.50, 0.0, 1.40), True),
    ("UpperArm.R",   "Shoulder.R",   (-0.18, 0.0, 1.40), (-0.50, 0.0, 1.40), True),
    ("LowerArm.L",   "UpperArm.L",   (0.50, 0.0, 1.40), (0.75, 0.0, 1.40), True),
    ("LowerArm.R",   "UpperArm.R",   (-0.50, 0.0, 1.40), (-0.75, 0.0, 1.40), True),
    ("Hand.L",       "LowerArm.L",   (0.75, 0.0, 1.40), (0.85, 0.0, 1.40), True),
    ("Hand.R",       "LowerArm.R",   (-0.75, 0.0, 1.40), (-0.85, 0.0, 1.40), True),

    ("UpperLeg.L",   "Hips",         (0.10, 0.0, 1.00), (0.10, 0.0, 0.55), False),
    ("UpperLeg.R",   "Hips",         (-0.10, 0.0, 1.00), (-0.10, 0.0, 0.55), False),
    ("LowerLeg.L",   "UpperLeg.L",   (0.10, 0.0, 0.55), (0.10, 0.0, 0.10), True),
    ("LowerLeg.R",   "UpperLeg.R",   (-0.10, 0.0, 0.55), (-0.10, 0.0, 0.10), True),
    ("Foot.L",       "LowerLeg.L",   (0.10, 0.0, 0.10), (0.10, 0.15, 0.02), True),
    ("Foot.R",       "LowerLeg.R",   (-0.10, 0.0, 0.10), (-0.10, 0.15, 0.02), True),
]


def build_skeleton():
    """Creates the CNAAvatarSkeleton armature object in the current Blender scene
    and returns it. Removes any pre-existing object of the same name first, so this
    is safe to call repeatedly in the same Blender session (e.g. from generate_avatar.py)."""
    try:
        import bpy
    except ImportError:
        sys.exit("This script must be run inside Blender: blender --background --python generate_skeleton.py")

    existing = bpy.data.objects.get(ARMATURE_NAME)
    if existing is not None:
        bpy.data.armatures.remove(existing.data, do_unlink=True)

    armature_data = bpy.data.armatures.new(ARMATURE_NAME)
    armature_obj = bpy.data.objects.new(ARMATURE_NAME, armature_data)
    bpy.context.collection.objects.link(armature_obj)

    bpy.context.view_layer.objects.active = armature_obj
    bpy.ops.object.mode_set(mode="EDIT")

    edit_bones = armature_data.edit_bones
    for name, _parent, head, tail, _connected in BONES:
        eb = edit_bones.new(name)
        eb.head = head
        eb.tail = tail

    for name, parent, _head, _tail, connected in BONES:
        if parent is None:
            continue
        eb = edit_bones[name]
        eb.parent = edit_bones[parent]
        eb.use_connect = connected

    bpy.ops.object.mode_set(mode="OBJECT")
    return armature_obj


def _bone_names_in_hierarchy_order():
    return [name for name, *_rest in BONES]


if __name__ == "__main__":
    armature_obj = build_skeleton()
    bones = armature_obj.data.bones
    print(f"Built armature '{armature_obj.name}' with {len(bones)} bones:")
    for name in _bone_names_in_hierarchy_order():
        bone = bones[name]
        parent_name = bone.parent.name if bone.parent else "(none)"
        print(f"  {name:<12} parent={parent_name:<12} connected={bone.use_connect}")

    assert len(bones) == len(BONES), "bone count mismatch"
    for name, parent, *_rest in BONES:
        actual_parent = bones[name].parent.name if bones[name].parent else None
        assert actual_parent == parent, f"{name}: expected parent {parent}, got {actual_parent}"
    print("OK: bone names and parents match the documented hierarchy.")
