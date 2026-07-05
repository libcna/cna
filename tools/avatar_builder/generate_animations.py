#!/usr/bin/env python3
"""Builds CNA's two placeholder avatar animations — `Stand0` (idle) and `Wave` — as
Blender Actions keyframed directly on the Task 11.1 `CNAAvatarSkeleton` bones. Simple
bone rotations only: no motion capture, no external clip source. Because this script
shares the Task 11.1 bone names by construction (it poses `generate_skeleton.BONES`
directly), there is no retargeting step — every keyframe just names a bone from that
list.

Action names must match `AvatarAnimationPreset` enumerator names exactly (see
`AvatarAnimationPresetToClipNameEXT` in
include/Microsoft/Xna/Framework/GamerServices/AvatarAnimationPresetNamesEXT.hpp) since
that is the lookup key `AvatarRenderer::DrawRealEXT` uses against a loaded
`SkinnedModelEXT`'s clips — `Stand0` and `Wave` already are exact matches.

Offline, one-time content-authoring tool — not part of the C++ build, never run by CNA
at runtime. Run headless via Blender:

    blender --background --python generate_animations.py

`generate_avatar.py` (Task 11.7) will import build_animations() from this module and
call it in the same Blender process as the skeleton/body/etc. builders, rather than
re-deriving the animations.
"""

import math
import sys
from pathlib import Path

try:
    import bpy
except ImportError:
    sys.exit("This script must be run inside Blender: blender --background --python generate_animations.py")

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_skeleton  # noqa: E402  (bpy path setup must happen first)


def _create_action(armature_obj, name):
    if armature_obj.animation_data is None:
        armature_obj.animation_data_create()
    existing = bpy.data.actions.get(name)
    if existing is not None:
        bpy.data.actions.remove(existing, do_unlink=True)
    action = bpy.data.actions.new(name)
    action.use_fake_user = True
    armature_obj.animation_data.action = action
    return action


def _reset_pose(armature_obj):
    for pb in armature_obj.pose.bones:
        pb.rotation_mode = "XYZ"
        pb.rotation_euler = (0.0, 0.0, 0.0)
        pb.location = (0.0, 0.0, 0.0)


def _keyframe_euler(armature_obj, bone_name, frame, euler_xyz):
    pb = armature_obj.pose.bones[bone_name]
    pb.rotation_mode = "XYZ"
    pb.rotation_euler = euler_xyz
    pb.keyframe_insert(data_path="rotation_euler", frame=frame)


def _keyframe_location(armature_obj, bone_name, frame, location_xyz):
    pb = armature_obj.pose.bones[bone_name]
    pb.location = location_xyz
    pb.keyframe_insert(data_path="location", frame=frame)


def build_stand0(armature_obj):
    """A subtle idle: Hips bob slightly and Spine1 rocks a couple of degrees, looping
    seamlessly over 90 frames (frame 1 and frame 90 are identical rest poses) — 3.75s at
    Blender's default scene fps (24), confirmed via the exported clip's own duration
    field, not assumed."""
    _reset_pose(armature_obj)
    action = _create_action(armature_obj, "Stand0")

    bob = 0.01  # meters
    for frame, z in ((1, 0.0), (45, bob), (90, 0.0)):
        _keyframe_location(armature_obj, "Hips", frame, (0.0, 0.0, z))

    rock = math.radians(2.0)
    for frame, angle in ((1, 0.0), (45, rock), (90, 0.0)):
        _keyframe_euler(armature_obj, "Spine1", frame, (angle, 0.0, 0.0))

    return action


def build_wave(armature_obj):
    """Raises the right arm (Shoulder.R/UpperArm.R chain) and oscillates the forearm
    (LowerArm.R) side to side a few times, then lowers back to rest. 60 frames — 2.5s at
    Blender's default scene fps (24). Rotation axes were picked empirically (see
    README.md) to lift the arm up/forward and
    swing the forearm side to side given this skeleton's bone roll — not derived from a
    generic formula, since Blender's automatic bone roll for a horizontal bone isn't the
    same for every axis convention."""
    _reset_pose(armature_obj)
    action = _create_action(armature_obj, "Wave")

    raise_angle = math.radians(-80.0)
    for frame, angle in ((1, 0.0), (10, raise_angle), (50, raise_angle), (60, 0.0)):
        _keyframe_euler(armature_obj, "UpperArm.R", frame, (0.0, 0.0, angle))

    # Rotating LowerArm.R around its own length axis (local Y, euler (0, angle, 0)) is
    # an invisible twist on a round cylinder — verified empirically by rendering it and
    # seeing no silhouette change. Local X instead folds the elbow, swinging the
    # forearm/hand up and down near the head, which reads as a recognizable wave.
    fold_low, fold_high = math.radians(20.0), math.radians(70.0)
    swing_frames = (
        (1, 0.0), (10, 0.0),
        (18, fold_high), (26, fold_low), (34, fold_high), (42, fold_low),
        (50, 0.0), (60, 0.0),
    )
    for frame, angle in swing_frames:
        _keyframe_euler(armature_obj, "LowerArm.R", frame, (angle, 0.0, 0.0))

    return action


def build_animations(armature_obj):
    """Builds both Stand0 and Wave actions and returns a {name: action} dict. Safe to
    call repeatedly in the same Blender session — replaces existing actions of the same
    name rather than stacking duplicates. Leaves `armature_obj.animation_data.action`
    set to whichever was built last; callers that need a specific one active should set
    it explicitly."""
    return {
        "Stand0": build_stand0(armature_obj),
        "Wave": build_wave(armature_obj),
    }


if __name__ == "__main__":
    armature_obj = generate_skeleton.build_skeleton()
    actions = build_animations(armature_obj)

    print(f"Built {len(actions)} actions: {sorted(actions)}")
    for name, action in actions.items():
        frame_start, frame_end = action.frame_range
        print(f"  {name}: frame_range = ({frame_start:.1f}, {frame_end:.1f})")

    assert {"Stand0", "Wave"}.issubset(actions), "missing required actions"
    for name, action in actions.items():
        frame_start, frame_end = action.frame_range
        assert frame_end > frame_start, f"{name} has a zero/negative frame range"
    print("OK: Stand0 and Wave actions exist with a nonzero frame range.")
