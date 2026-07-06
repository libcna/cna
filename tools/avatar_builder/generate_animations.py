#!/usr/bin/env python3
"""Builds CNA's placeholder avatar animations as Blender Actions keyframed directly on
the Task 11.1 `CNAAvatarSkeleton` bones. Simple bone rotations only: no motion capture,
no external clip source. Because this script shares the Task 11.1 bone names by
construction (it poses `generate_skeleton.BONES` directly), there is no retargeting
step — every keyframe just names a bone from that list.

Task 11.6 authored `Stand0` (idle) and `Wave`. Task 11.15 adds three more, working
toward covering more of `AvatarAnimationPreset`'s 31 values (still a small fraction —
these remain placeholder motion, not a claim of full coverage): `Stand1` (a second,
differently-shaped idle), `Clap`, and `Celebrate`.

Action names must match `AvatarAnimationPreset` enumerator names exactly (see
`AvatarAnimationPresetToClipNameEXT` in
include/Microsoft/Xna/Framework/GamerServices/AvatarAnimationPresetNamesEXT.hpp) since
that is the lookup key `AvatarRenderer::DrawRealEXT` uses against a loaded
`SkinnedModelEXT`'s clips — `Stand0`/`Stand1`/`Wave`/`Clap`/`Celebrate` already are exact
matches.

**A real, non-obvious empirical finding from Task 11.15 (verified by rendering, not
derived analytically — see `_raise_upper_arm`/`_fold_lower_arm` below):** mirroring an
arm gesture from the `.R` bones (as Task 11.6 authored for `Wave`) onto the matching
`.L` bones is NOT just "reuse the same signed angle" for every bone, and worse, testing
each joint *in isolation* gives a wrong answer for one of the two joints involved.
`UpperArm.L/R`'s "raise the arm up and back" axis (local Z) needs an **opposite-signed**
angle between `.L` and `.R` for the same physical motion (`UpperArm.R` raise = `Z(-A)`,
`UpperArm.L` raise = `Z(+A)`), confirmed consistently whether tested alone or combined
with a forearm fold. `LowerArm.L/R`'s "fold the elbow" axis (local X) tests as
opposite-signed too **if posed alone** (T-pose otherwise) — but once the matching
`UpperArm` is actually raised (the real context this rig is posed in), both sides need
the **same** sign instead. The reason: a child bone's local rotation composes with its
parent's *current* world transform, not its rest transform, so the same local angle can
swing a different way in world space once the parent has already rotated — an isolated
single-bone test silently assumes the parent is still at rest, which does not hold once
an arm is actually raised. Always re-verify empirically in the actual combined pose a
new call site uses, not bone-by-bone in isolation, and don't assume this pattern
generalizes to bones this script hasn't yet mirrored (`UpperLeg`/`LowerLeg`, etc.).

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


def _raise_upper_arm(armature_obj, side, frame, angle_rad):
    """Keyframes UpperArm.<side>'s local-Z rotation to swing the arm up/back from its
    T-pose rest, the same axis Task 11.6's Wave uses for UpperArm.R. Empirically
    confirmed (Task 11.15, not derived analytically): `.L` needs the OPPOSITE sign from
    `.R` for the same physical raise, since the two bones' local frames mirror rather
    than match — `side="R"` keyframes `Z(-angle_rad)`, `side="L"` keyframes
    `Z(+angle_rad)`. Pass a positive `angle_rad` for both sides; the sign flip is
    internal."""
    signed = -angle_rad if side == "R" else angle_rad
    _keyframe_euler(armature_obj, f"UpperArm.{side}", frame, (0.0, 0.0, signed))


def _fold_lower_arm(armature_obj, side, frame, angle_rad):
    """Keyframes LowerArm.<side>'s local-X rotation to fold the elbow, the same axis
    Task 11.6's Wave uses for LowerArm.R. Empirically confirmed (Task 11.15): unlike
    _raise_upper_arm's Z axis, this one does NOT mirror between `.L` and `.R` — both
    sides need the SAME sign for the hand to fold up toward the head the same physical
    way. This was actually verified twice, with two different results: posing each
    side's LowerArm alone (T-pose otherwise) at the same angle showed the hand swinging
    in opposite absolute directions, suggesting a mirrored sign — but posing each side's
    LowerArm at that same angle *with its own UpperArm already raised* (the actual
    context this function is used in) showed both sides needing the SAME sign instead.
    The plain-T-pose probe doesn't predict the answer because a child bone's local
    rotation composes with its parent's CURRENT (already-rotated) world transform, not
    its rest transform — the same local angle produces a different world-space swing
    once the parent has moved. Always re-verify empirically in the actual pose a new
    call site uses, not in isolation. `side="R"` and `side="L"` both keyframe
    `X(+angle_rad)`."""
    _keyframe_euler(armature_obj, f"LowerArm.{side}", frame, (angle_rad, 0.0, 0.0))


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


def build_stand1(armature_obj):
    """A second idle, shaped differently from Stand0 on purpose so the two are never
    confusable: a slow side-to-side weight shift (Hips sways in X, not Stand0's
    up/down Z bob) with a counter-rotating torso twist (Spine1 rotates around its own
    local Y — its length axis, a genuine twist for this vertical bone — not Stand0's
    local-X front/back rock). Loops seamlessly over 100 frames (frame 1 and frame 100
    are identical rest poses)."""
    _reset_pose(armature_obj)
    action = _create_action(armature_obj, "Stand1")

    sway = 0.02  # meters
    for frame, x in ((1, 0.0), (25, sway), (50, 0.0), (75, -sway), (100, 0.0)):
        _keyframe_location(armature_obj, "Hips", frame, (x, 0.0, 0.0))

    twist = math.radians(3.0)
    for frame, angle in ((1, 0.0), (25, -twist), (50, 0.0), (75, twist), (100, 0.0)):
        _keyframe_euler(armature_obj, "Spine1", frame, (0.0, angle, 0.0))

    return action


def build_stand2(armature_obj):
    """A third idle: Neck tilts side to side (local Z) with Head counter-tilting a
    smaller amount the opposite way (also local Z) — a "curious head tilt", distinct
    from Stand0 (Hips Z-bob + Spine1 X-rock) and Stand1 (Hips X-sway + Spine1 Y-twist)
    by using the Neck/Head pair instead of Hips/Spine1. Loops seamlessly over 110 frames."""
    _reset_pose(armature_obj)
    action = _create_action(armature_obj, "Stand2")

    tilt = math.radians(6.0)
    for frame, angle in ((1, 0.0), (28, tilt), (55, 0.0), (83, -tilt), (110, 0.0)):
        _keyframe_euler(armature_obj, "Neck", frame, (0.0, 0.0, angle))
        _keyframe_euler(armature_obj, "Head", frame, (0.0, 0.0, -angle * 0.4))

    return action


def build_stand3(armature_obj):
    """A fourth idle: a slow torso twist on Spine (not Spine1, which Stand1 already
    uses) — turns the whole upper body (chest, shoulders, arms, head) left and right
    around its own vertical axis. Loops seamlessly over 120 frames."""
    _reset_pose(armature_obj)
    action = _create_action(armature_obj, "Stand3")

    twist = math.radians(4.0)
    for frame, angle in ((1, 0.0), (30, twist), (60, 0.0), (90, -twist), (120, 0.0)):
        _keyframe_euler(armature_obj, "Spine", frame, (0.0, angle, 0.0))

    return action


def build_stand4(armature_obj):
    """A fifth idle: Stand0's own Hips Z-bob, combined with a Neck forward nod (local
    X) timed to dip at the bottom of each bob — a "tired nod", distinct from Stand0
    (bob alone) by the added, synchronized neck motion. Loops seamlessly over 90 frames
    (same timing as Stand0's bob, reused deliberately so the two read as a family)."""
    _reset_pose(armature_obj)
    action = _create_action(armature_obj, "Stand4")

    bob = 0.01
    nod = math.radians(5.0)
    for frame, z, angle in ((1, 0.0, 0.0), (45, bob, nod), (90, 0.0, 0.0)):
        _keyframe_location(armature_obj, "Hips", frame, (0.0, 0.0, z))
        _keyframe_euler(armature_obj, "Neck", frame, (angle, 0.0, 0.0))

    return action


def build_stand5(armature_obj):
    """A sixth idle: a slow, single-bone Neck nod (local X, "looking down and back up"),
    larger amplitude and slower than Stand4's synchronized nod, with no other bone
    involved — the simplest of the eight Stand variants. Loops seamlessly over 130
    frames."""
    _reset_pose(armature_obj)
    action = _create_action(armature_obj, "Stand5")

    nod = math.radians(8.0)
    for frame, angle in ((1, 0.0), (65, nod), (130, 0.0)):
        _keyframe_euler(armature_obj, "Neck", frame, (angle, 0.0, 0.0))

    return action


def build_stand6(armature_obj):
    """A seventh idle: Spine1 leans (local Z, not Stand0's local-X rock or Stand1's
    local-Y twist — the third rotation axis on this bone) while Hips sway in X the
    opposite phase, like a slow metronome. Loops seamlessly over 100 frames."""
    _reset_pose(armature_obj)
    action = _create_action(armature_obj, "Stand6")

    lean = math.radians(3.0)
    sway = 0.015
    for frame, angle, x in ((1, 0.0, 0.0), (25, lean, -sway), (50, 0.0, 0.0), (75, -lean, sway), (100, 0.0, 0.0)):
        _keyframe_euler(armature_obj, "Spine1", frame, (0.0, 0.0, angle))
        _keyframe_location(armature_obj, "Hips", frame, (x, 0.0, 0.0))

    return action


def build_stand7(armature_obj):
    """An eighth idle: Hips slowly turn in place (local Y) while Spine counter-rotates
    the same amount the opposite way, keeping the chest/shoulders facing forward even as
    the hips shift — a subtle "contrapposto" weight-shift stance. Loops seamlessly over
    140 frames, the slowest of the eight Stand variants."""
    _reset_pose(armature_obj)
    action = _create_action(armature_obj, "Stand7")

    turn = math.radians(5.0)
    for frame, angle in ((1, 0.0), (35, turn), (70, 0.0), (105, -turn), (140, 0.0)):
        _keyframe_euler(armature_obj, "Hips", frame, (0.0, angle, 0.0))
        _keyframe_euler(armature_obj, "Spine", frame, (0.0, -angle, 0.0))

    return action


def build_female_idle_check_nails(armature_obj):
    """FemaleIdleCheckNails: head/neck tilt down and slightly to the side, held, as if
    looking down at one's own hand — Neck local-X nod plus a small local-Z tilt, held
    at the bottom rather than continuously oscillating like the Stand* idles. 70 frames,
    plays once and returns to rest."""
    _reset_pose(armature_obj)
    action = _create_action(armature_obj, "FemaleIdleCheckNails")

    nod, tilt = math.radians(18.0), math.radians(8.0)
    for frame, n, t in ((1, 0.0, 0.0), (15, nod, tilt), (55, nod, tilt), (70, 0.0, 0.0)):
        _keyframe_euler(armature_obj, "Neck", frame, (n, 0.0, t))

    return action


def build_female_idle_look_around(armature_obj):
    """FemaleIdleLookAround: Neck turns to look left, holds, then right, holds, then
    back to center — local-Y twist, distinct timing/holds from Stand2/Stand7's
    continuous sway. 130 frames, plays once."""
    _reset_pose(armature_obj)
    action = _create_action(armature_obj, "FemaleIdleLookAround")

    turn = math.radians(35.0)
    keys = ((1, 0.0), (20, turn), (45, turn), (65, 0.0), (85, -turn), (110, -turn), (130, 0.0))
    for frame, angle in keys:
        _keyframe_euler(armature_obj, "Neck", frame, (0.0, angle, 0.0))

    return action


def build_female_idle_shift_weight(armature_obj):
    """FemaleIdleShiftWeight: Hips sway in X while Spine1 twists (local Y) the opposite
    phase — a slower, larger-amplitude combination than Stand1/Stand6's own Hips+Spine1
    pairings, timed differently so it doesn't read as a duplicate. Loops seamlessly over
    120 frames."""
    _reset_pose(armature_obj)
    action = _create_action(armature_obj, "FemaleIdleShiftWeight")

    sway, twist = 0.025, math.radians(5.0)
    for frame, x, angle in ((1, 0.0, 0.0), (30, sway, -twist), (60, 0.0, 0.0), (90, -sway, twist), (120, 0.0, 0.0)):
        _keyframe_location(armature_obj, "Hips", frame, (x, 0.0, 0.0))
        _keyframe_euler(armature_obj, "Spine1", frame, (0.0, angle, 0.0))

    return action


def build_female_idle_fix_shoe(armature_obj):
    """FemaleIdleFixShoe: torso bends forward at Spine (local X) as if reaching down
    toward a shoe, holds, then straightens back up — approximated entirely on the torso
    (no leg bones), since this rig has no IK to plant a foot while bending. 80 frames,
    plays once."""
    _reset_pose(armature_obj)
    action = _create_action(armature_obj, "FemaleIdleFixShoe")

    bend = math.radians(35.0)
    for frame, angle in ((1, 0.0), (20, bend), (55, bend), (80, 0.0)):
        _keyframe_euler(armature_obj, "Spine", frame, (angle, 0.0, 0.0))

    return action


def build_female_angry(armature_obj):
    """FemaleAngry: a quick, sharp, short-amplitude Spine twist repeated a few times —
    an indignant "huff" shake — plus a short Hips jerk in sync, faster and snappier than
    any Stand* idle's slow sway. 40 frames, plays once."""
    _reset_pose(armature_obj)
    action = _create_action(armature_obj, "FemaleAngry")

    twist = math.radians(10.0)
    jerk = 0.01
    keys = (
        (1, 0.0, 0.0), (6, -twist, jerk), (12, twist, jerk), (18, -twist, jerk),
        (24, twist, jerk), (30, 0.0, 0.0), (40, 0.0, 0.0),
    )
    for frame, angle, z in keys:
        _keyframe_euler(armature_obj, "Spine", frame, (0.0, angle, 0.0))
        _keyframe_location(armature_obj, "Hips", frame, (0.0, 0.0, z))

    return action


def build_female_confused(armature_obj):
    """FemaleConfused: a single head tilt that holds (not oscillating back and forth
    like Stand2's continuous curious tilt) combined with a small Neck twist — a
    "puzzled" tilt-and-hold. 90 frames, plays once and returns to rest."""
    _reset_pose(armature_obj)
    action = _create_action(armature_obj, "FemaleConfused")

    tilt, twist = math.radians(14.0), math.radians(10.0)
    for frame, t, y in ((1, 0.0, 0.0), (20, tilt, twist), (65, tilt, twist), (90, 0.0, 0.0)):
        _keyframe_euler(armature_obj, "Head", frame, (0.0, y, t))

    return action


def build_female_laugh(armature_obj):
    """FemaleLaugh: Spine1 bounces quickly on its local-Z axis (not Stand0's slower
    Hips Z-bob) in sync with a small Head bob — a "shaking with laughter" motion. 40
    frames, plays once."""
    _reset_pose(armature_obj)
    action = _create_action(armature_obj, "FemaleLaugh")

    lean = math.radians(6.0)
    keys = ((1, 0.0), (6, lean), (12, -lean), (18, lean), (24, -lean), (30, lean), (40, 0.0))
    for frame, angle in keys:
        _keyframe_euler(armature_obj, "Spine1", frame, (0.0, 0.0, angle))
        _keyframe_euler(armature_obj, "Head", frame, (0.0, 0.0, angle * 0.5))

    return action


def build_female_cry(armature_obj):
    """FemaleCry: Neck droops forward and stays down (not returning to neutral until
    the very end, unlike every oscillating Stand* idle) while Spine slouches forward a
    little — a sad slump. 100 frames, plays once."""
    _reset_pose(armature_obj)
    action = _create_action(armature_obj, "FemaleCry")

    droop, slouch = math.radians(22.0), math.radians(10.0)
    for frame, n, s in ((1, 0.0, 0.0), (25, droop, slouch), (80, droop, slouch), (100, 0.0, 0.0)):
        _keyframe_euler(armature_obj, "Neck", frame, (n, 0.0, 0.0))
        _keyframe_euler(armature_obj, "Spine", frame, (s, 0.0, 0.0))

    return action


def build_female_shocked(armature_obj):
    """FemaleShocked: a sharp backward Spine lean (negative local X) with the head
    snapping back too, held briefly, then a slower return to rest — a startled recoil.
    50 frames, plays once."""
    _reset_pose(armature_obj)
    action = _create_action(armature_obj, "FemaleShocked")

    lean = math.radians(-12.0)
    for frame, angle in ((1, 0.0), (6, lean), (20, lean), (50, 0.0)):
        _keyframe_euler(armature_obj, "Spine", frame, (angle, 0.0, 0.0))
        _keyframe_euler(armature_obj, "Neck", frame, (angle, 0.0, 0.0))

    return action


def build_female_yawn(armature_obj):
    """FemaleYawn: head/neck tilt back (positive local X, "looking up") together with a
    small Spine1 backward stretch, slow to rise and slow to release. 110 frames, plays
    once."""
    _reset_pose(armature_obj)
    action = _create_action(armature_obj, "FemaleYawn")

    tilt, stretch = math.radians(20.0), math.radians(6.0)
    for frame, n, s in ((1, 0.0, 0.0), (40, tilt, stretch), (70, tilt, stretch), (110, 0.0, 0.0)):
        _keyframe_euler(armature_obj, "Neck", frame, (-n, 0.0, 0.0))
        _keyframe_euler(armature_obj, "Spine1", frame, (-s, 0.0, 0.0))

    return action


def build_clap(armature_obj):
    """Both arms raise together to roughly chest height (UpperArm.L/R, via
    _raise_upper_arm) and hold, while both forearms (LowerArm.L/R, via
    _fold_lower_arm) oscillate their fold angle four times in sync — a crude, symmetric
    approximation of clapping (arms coming up in front of the body and pulsing
    together), not a literal hands-meet-at-center gesture; this cylinder-and-bone rig
    has no IK to aim the hands at each other. 48 frames, plays once and returns to
    rest."""
    _reset_pose(armature_obj)
    action = _create_action(armature_obj, "Clap")

    raise_angle = math.radians(70.0)
    for frame, angle in ((1, 0.0), (8, raise_angle), (40, raise_angle), (48, 0.0)):
        _raise_upper_arm(armature_obj, "R", frame, angle)
        _raise_upper_arm(armature_obj, "L", frame, angle)

    fold_low, fold_high = math.radians(20.0), math.radians(80.0)
    clap_beats = (
        (1, 0.0), (8, fold_low),
        (14, fold_high), (20, fold_low), (26, fold_high), (32, fold_low), (38, fold_high),
        (40, fold_low), (48, 0.0),
    )
    for frame, angle in clap_beats:
        _fold_lower_arm(armature_obj, "R", frame, angle)
        _fold_lower_arm(armature_obj, "L", frame, angle)

    return action


def build_celebrate(armature_obj):
    """Both arms raise together (UpperArm.L/R, via _raise_upper_arm, reusing Wave's own
    empirically-verified 80° raise magnitude rather than an untested new angle — a
    bigger angle was tried first and rendered wrong, see README.md) and hold, while Hips
    bounces up twice (a bigger, faster echo of Stand0's own idle bob) — a triumphant
    "arms up" pose with a little pump to it. 60 frames, plays once and returns to
    rest."""
    _reset_pose(armature_obj)
    action = _create_action(armature_obj, "Celebrate")

    raise_angle = math.radians(80.0)
    for frame, angle in ((1, 0.0), (15, raise_angle), (45, raise_angle), (60, 0.0)):
        _raise_upper_arm(armature_obj, "R", frame, angle)
        _raise_upper_arm(armature_obj, "L", frame, angle)

    bounce = 0.03  # meters
    for frame, z in ((15, 0.0), (22, bounce), (30, 0.0), (37, bounce), (45, 0.0)):
        _keyframe_location(armature_obj, "Hips", frame, (0.0, 0.0, z))

    return action


_GENERIC_BUILDERS = {
    "Stand0": build_stand0,
    "Stand1": build_stand1,
    "Stand2": build_stand2,
    "Stand3": build_stand3,
    "Stand4": build_stand4,
    "Stand5": build_stand5,
    "Stand6": build_stand6,
    "Stand7": build_stand7,
    "Wave": build_wave,
    "Clap": build_clap,
    "Celebrate": build_celebrate,
}

_FEMALE_BUILDERS = {
    "FemaleIdleCheckNails": build_female_idle_check_nails,
    "FemaleIdleLookAround": build_female_idle_look_around,
    "FemaleIdleShiftWeight": build_female_idle_shift_weight,
    "FemaleIdleFixShoe": build_female_idle_fix_shoe,
    "FemaleAngry": build_female_angry,
    "FemaleConfused": build_female_confused,
    "FemaleLaugh": build_female_laugh,
    "FemaleCry": build_female_cry,
    "FemaleShocked": build_female_shocked,
    "FemaleYawn": build_female_yawn,
}

# Populated by Task 11.23c.
_MALE_BUILDERS = {}


def build_animations(armature_obj, gender=None):
    """Builds the generic Stand0-Stand7/Wave/Clap/Celebrate actions (always), plus
    `gender`'s own gendered `AvatarAnimationPreset` subset when `gender` is "female" or
    "male" (Task 11.23b/11.23c) — `FemaleAngry`/`MaleAngry` etc. are gender-specific
    presets in the real enum, so they are only baked into that gender's content, not
    both. `gender=None` (the default) builds only the generic set, e.g. for this
    module's own __main__ smoke test. Returns a {name: action} dict. Safe to call
    repeatedly in the same Blender session — replaces existing actions of the same name
    rather than stacking duplicates. Leaves `armature_obj.animation_data.action` set to
    whichever was built last; callers that need a specific one active should set it
    explicitly."""
    builders = dict(_GENERIC_BUILDERS)
    if gender == "female":
        builders.update(_FEMALE_BUILDERS)
    elif gender == "male":
        builders.update(_MALE_BUILDERS)
    return {name: fn(armature_obj) for name, fn in builders.items()}


if __name__ == "__main__":
    armature_obj = generate_skeleton.build_skeleton()
    actions = {}
    actions.update(build_animations(armature_obj, gender="female"))
    actions.update(build_animations(armature_obj, gender="male"))
    required = set(_GENERIC_BUILDERS) | set(_FEMALE_BUILDERS) | set(_MALE_BUILDERS)

    print(f"Built {len(actions)} actions: {sorted(actions)}")
    for name, action in actions.items():
        frame_start, frame_end = action.frame_range
        print(f"  {name}: frame_range = ({frame_start:.1f}, {frame_end:.1f})")

    assert required.issubset(actions), f"missing required actions: {sorted(required - set(actions))}"
    for name, action in actions.items():
        frame_start, frame_end = action.frame_range
        assert frame_end > frame_start, f"{name} has a zero/negative frame range"
    print(f"OK: all {len(required)} generic+gendered actions exist with a nonzero frame range.")
