# SPDX-License-Identifier: MS-PL
"""Pose toolkit for authoring the preset animations on the 71-slot rig.

A Pose holds local rotations (bind rotations are identity) and a root offset. Helpers solve
two-bone IK for arms and legs so key poses are authored as hand/foot targets, and feet stay
planted while the body moves.
"""
import math

from . import rig
from .mathutil import (add, sub, mul, dot, cross, normalize, length, quat_mul, quat_normalize, quat_rotate,
                       quat_axis_angle, quat_euler)

I = rig.INDEX
IDENTITY = (0.0, 0.0, 0.0, 1.0)


def quat_inverse(q):
    return (-q[0], -q[1], -q[2], q[3])


def from_to(u, v):
    u, v = normalize(u), normalize(v)
    d = dot(u, v)
    if d > 0.999999:
        return IDENTITY
    if d < -0.999999:
        axis = cross(u, (1.0, 0.0, 0.0))
        if length(axis) < 1e-6:
            axis = cross(u, (0.0, 1.0, 0.0))
        return quat_axis_angle(axis, 180.0)
    c = cross(u, v)
    return quat_normalize((c[0], c[1], c[2], 1.0 + d))


def mirror(euler, sign):
    """Euler (x, y, z) authored for the left side, mirrored for the right (sign -1)."""
    x, y, z = euler
    return (x, y * sign, z * sign)


class Pose:
    def __init__(self, body):
        self.body = body
        self.local = [IDENTITY] * rig.BONE_COUNT
        self.root = (0.0, 0.0, 0.0)
        self.translations = rig.local_translations(body)
        self.bind = rig.joint_positions(body)
        self.feet = (None, None)

    def copy(self):
        p = Pose(self.body)
        p.local = list(self.local)
        p.root = self.root
        p.feet = self.feet
        return p

    # ----- forward kinematics ------------------------------------------------------------------
    def world(self):
        rot = [None] * rig.BONE_COUNT
        pos = [None] * rig.BONE_COUNT
        for i in range(rig.BONE_COUNT):
            parent = rig.PARENTS[i]
            if parent < 0:
                rot[i] = self.local[i]
                pos[i] = add(self.translations[i], self.root)
            else:
                rot[i] = quat_normalize(quat_mul(rot[parent], self.local[i]))
                pos[i] = add(pos[parent], quat_rotate(rot[parent], self.translations[i]))
        return rot, pos

    def set_world_rotation(self, bone, world_rotation, rot=None):
        rot = rot or self.world()[0]
        parent = rig.PARENTS[bone]
        self.local[bone] = quat_normalize(quat_mul(quat_inverse(rot[parent]), world_rotation))

    def rotate(self, name, x=0.0, y=0.0, z=0.0):
        """Adds a local rotation (degrees, bind axes) on top of the current one."""
        bone = I[name]
        self.local[bone] = quat_normalize(quat_mul(self.local[bone], quat_euler(x, y, z)))
        return self

    def rotate_side(self, name, side, euler):
        sign = 1.0 if side == "Left" else -1.0
        return self.rotate(name + side, *mirror(euler, sign))

    # ----- body-level helpers ------------------------------------------------------------------
    def spine(self, bend=0.0, twist=0.0, lean=0.0):
        """bend > 0 forward, twist > 0 toward the avatar's left, lean > 0 toward its left."""
        self.rotate("BackLower", bend * 0.45, twist * 0.4, -lean * 0.5)
        self.rotate("BackUpper", bend * 0.55, twist * 0.6, -lean * 0.5)
        return self

    def head(self, nod=0.0, turn=0.0, tilt=0.0):
        """nod > 0 looks down, turn > 0 looks to the avatar's left, tilt > 0 toward its left shoulder."""
        self.rotate("Neck", nod * 0.4, turn * 0.4, -tilt * 0.4)
        self.rotate("Head", nod * 0.6, turn * 0.6, -tilt * 0.6)
        return self

    def shrug(self, side, amount):
        """Raise (amount > 0) a collar, in degrees."""
        return self.rotate_side("Collar", side, (0.0, 0.0, amount))

    def arm(self, side, shoulder=(0.0, 0.0, 0.0), elbow=(0.0, 0.0, 0.0), wrist=(0.0, 0.0, 0.0)):
        """Euler rotations authored for the left arm (mirrored for the right)."""
        self.rotate_side("Shoulder", side, shoulder)
        self.rotate_side("Elbow", side, elbow)
        self.rotate_side("Wrist", side, wrist)
        return self

    def relaxed_arms(self):
        # Not a mirror image: one arm hangs a little further forward and more bent.
        self.arm("Left", shoulder=(-6.0, 0.0, -8.0), elbow=(-17.0, 0.0, 0.0))
        self.fingers("Left", 0.36)
        self.arm("Right", shoulder=(-2.5, 0.0, -10.5), elbow=(-10.0, 0.0, 0.0))
        self.fingers("Right", 0.26)
        return self

    def fingers(self, side, curl, thumb=None, spread=0.0):
        """Curl 0 = straight, 1 = fist."""
        sign = 1.0 if side == "Left" else -1.0
        along, normal, _ = rig.hand_frame(sign, rig.PROPORTIONS[self.body])
        axis = cross(along, normal)
        for finger in rig.FINGERS:
            amounts = (55.0, 80.0, 60.0)
            names = ("Finger%s%s", "Finger%s2%s", "Finger%s3%s")
            for amount, name in zip(amounts, names):
                bone = I[name % (finger, side)]
                self.local[bone] = quat_axis_angle(axis, curl * amount)
            if spread:
                fan = {"Index": 1.0, "Middle": 0.3, "Ring": -0.3, "Small": -1.0}[finger]
                bone = I["Finger%s%s" % (finger, side)]
                self.local[bone] = quat_normalize(quat_mul(quat_axis_angle(normal, spread * fan * 8.0), self.local[bone]))
        t = curl if thumb is None else thumb
        thumb_axis = cross(rig.thumb_direction(sign, rig.PROPORTIONS[self.body]), normal)
        for amount, name in zip((25.0, 35.0, 30.0), ("FingerThumb%s", "FingerThumb2%s", "FingerThumb3%s")):
            self.local[I[name % side]] = quat_axis_angle(thumb_axis, t * amount)
        return self

    # ----- inverse kinematics ------------------------------------------------------------------
    def _two_bone(self, upper, middle, end, target, pole):
        rot, pos = self.world()
        s = pos[upper]
        a = length(self.translations[middle])
        b = length(self.translations[end])
        to_target = sub(target, s)
        d = min(max(length(to_target), abs(a - b) + 1e-4), a + b - 1e-4)
        direction = normalize(to_target)
        cos_alpha = (a * a + d * d - b * b) / (2 * a * d)
        sin_alpha = math.sqrt(max(0.0, 1.0 - cos_alpha * cos_alpha))
        bend = normalize(sub(pole, mul(direction, dot(pole, direction))))
        elbow = add(s, add(mul(direction, a * cos_alpha), mul(bend, a * sin_alpha)))
        wrist = add(s, mul(direction, d))
        parent_rot = rot[rig.PARENTS[upper]]
        # Upper bone: carry its bind direction (as currently oriented by the parent) onto the new one.
        current_upper = quat_rotate(parent_rot, self.translations[middle])
        upper_world = quat_mul(from_to(current_upper, sub(elbow, s)), parent_rot)
        self.local[upper] = quat_normalize(quat_mul(quat_inverse(parent_rot), upper_world))
        current_lower = quat_rotate(upper_world, self.translations[end])
        lower_world = quat_mul(from_to(current_lower, sub(wrist, elbow)), upper_world)
        self.local[middle] = quat_normalize(quat_mul(quat_inverse(upper_world), lower_world))
        return lower_world

    def reach(self, side, target, pole=None, hand=None):
        """Put a wrist at `target` (model space). `hand` optionally gives the wrist's world rotation
        as Euler degrees authored for the left hand (mirrored for the right)."""
        sign = 1.0 if side == "Left" else -1.0
        pole = pole or (sign * 0.6, -0.2, -0.8)
        self._two_bone(I["Shoulder" + side], I["Elbow" + side], I["Wrist" + side], target, pole)
        if hand is not None:
            rot, _ = self.world()
            self.set_world_rotation(I["Wrist" + side], quat_euler(*mirror(hand, sign)), rot)
        else:
            self.local[I["Wrist" + side]] = IDENTITY
        return self

    def plant_feet(self, lift=None, forward=None, knee_out=0.0):
        """Solve both legs so ankles return to their bind positions (plus optional per-side offsets)
        and feet stay level. The targets are kept, so frames between keys can be solved again."""
        self.feet = (lift, forward)
        for side, sign in rig.SIDES:
            target = self.bind[I["Ankle" + side]]
            if lift and side in lift:
                target = add(target, lift[side])
            pole = (sign * knee_out, 0.0, 1.0)
            self._two_bone(I["Hip" + side], I["Knee" + side], I["Ankle" + side], target, pole)
            rot, _ = self.world()
            flat = IDENTITY if not (forward and side in forward) else quat_euler(*forward[side])
            self.set_world_rotation(I["Ankle" + side], flat, rot)
        return self

    # ----- measurements used as targets ---------------------------------------------------------
    def point(self, name, offset=(0.0, 0.0, 0.0)):
        """Current world position of a joint plus a model-space offset."""
        return add(self.world()[1][I[name]], offset)

    def chest(self, offset=(0.0, 0.0, 0.0)):
        """A point on the front of the chest, following the spine."""
        rot, pos = self.world()
        return add(pos[I["BackUpper"]], quat_rotate(rot[I["BackUpper"]], add((0.0, 0.08, 0.13), offset)))

    def face(self, x=0.0, y=0.0, out=0.03):
        """A point `out` metres in front of the face at head-local (x, y) (catalog head units,
        scaled to this body's head), following the head."""
        from . import head
        s = head.SCALE[self.body]
        on_face = add(head.front_point(self.body, x * s, y * s)[0], (0.0, 0.0, out))
        return self._on_head(on_face)

    def beside_head(self, side, y=0.0, z=0.0, out=0.05):
        """A point `out` metres off the side of the head (`side` "Left"/"Right") at head-local
        (y, z), following the head."""
        from . import head
        s = head.SCALE[self.body]
        sign = 1.0 if side == "Left" else -1.0
        point = add(head.side_point(self.body, sign, y * s, z * s)[0], (sign * out, 0.0, 0.0))
        return self._on_head(point)

    def _on_head(self, bind_point):
        rot, pos = self.world()
        return add(pos[I["Head"]], quat_rotate(rot[I["Head"]], sub(bind_point, self.bind[I["Head"]])))
