# SPDX-License-Identifier: MS-PL
"""The 31 AvatarAnimationPreset clips: original CNA motion authored as key poses.

Every clip starts and ends in (or close to) the relaxed stance so presets can be looped and
chained. Expressions are step keys (mouth, left eye, right eye, left eyebrow, right eyebrow).
"""
import math

from . import rig
from .posing import Pose
from .mathutil import add, lerp

PRESETS = ["Stand0", "Stand1", "Stand2", "Stand3", "Stand4", "Stand5", "Stand6", "Stand7", "Clap", "Wave",
           "Celebrate", "FemaleIdleCheckNails", "FemaleIdleLookAround", "FemaleIdleShiftWeight", "FemaleIdleFixShoe",
           "FemaleAngry", "FemaleConfused", "FemaleLaugh", "FemaleCry", "FemaleShocked", "FemaleYawn",
           "MaleIdleLookAround", "MaleIdleStretch", "MaleIdleShiftWeight", "MaleIdleCheckHand", "MaleAngry",
           "MaleConfused", "MaleLaugh", "MaleCry", "MaleSurprised", "MaleYawn"]
EYES = ["Neutral", "Sad", "Angry", "Confused", "Laughing", "Shocked", "Happy", "Yawning", "Sleeping", "LookUp",
        "LookDown", "LookLeft", "LookRight", "Blink"]
MOUTHS = ["Neutral", "Sad", "Angry", "Confused", "Laughing", "Shocked", "Happy", "PhoneticO", "PhoneticAi",
          "PhoneticEe", "PhoneticFv", "PhoneticW", "PhoneticL", "PhoneticDth"]
BROWS = ["Neutral", "Sad", "Angry", "Confused", "Raised"]


class Clip:
    def __init__(self, name, duration, body="male", loop=False):
        self.name, self.duration, self.body, self.loop = name, duration, body, loop
        self.keys = []
        self.expressions = []

    def key(self, t, pose):
        self.keys.append((t, pose))
        return self

    def face(self, t, mouth="Neutral", eyes="Neutral", brows="Neutral", right_eye=None, right_brow=None):
        self.expressions.append((t, MOUTHS.index(mouth), EYES.index(eyes), EYES.index(right_eye or eyes),
                                 BROWS.index(brows), BROWS.index(right_brow or brows)))
        return self

    def blinks(self, times, mouth="Neutral", eyes="Neutral", brows="Neutral"):
        for t in times:
            self.face(t, mouth, "Blink", brows)
            self.face(t + 0.14, mouth, eyes, brows)
        return self


def stance(body, root=(0.0, 0.0, 0.0), bend=0.0, twist=0.0, lean=0.0, nod=0.0, turn=0.0, tilt=0.0, breath=0.0,
           relaxed=True):
    p = Pose(body)
    p.root = root
    p.spine(bend + breath * 1.2, twist, lean)
    p.rotate("BackUpper", -breath * 1.6)
    for side, _ in rig.SIDES:
        p.shrug(side, breath * 1.5)
    p.head(nod - breath * 0.4, turn, tilt)
    if relaxed:
        p.relaxed_arms()
    return p


def finish(p, lift=None, forward=None):
    p.plant_feet(lift, forward)
    return p


def breathing(clip, count, amplitude=1.0, **stance_args):
    """Uniform idle: `count` breaths over the clip, looping exactly."""
    steps = count * 4
    for k in range(steps + 1):
        t = clip.duration * k / steps
        b = amplitude * math.sin(2 * math.pi * k / 4)
        clip.key(t, finish(stance(clip.body, breath=b, **stance_args)))


def build_clips():
    clips = []
    c = lambda *a, **k: clips.append(Clip(*a, **k)) or clips[-1]

    # --- Stand0: relaxed idle with a small sway -------------------------------------------------
    clip = c("Stand0", 6.0, loop=True)
    for k in range(9):
        t = 6.0 * k / 8
        phase = 2 * math.pi * k / 8
        clip.key(t, finish(stance("male", root=(0.008 * math.sin(phase), 0.0, 0.0), breath=math.sin(2 * phase),
                                  lean=-0.8 * math.sin(phase), tilt=0.8 * math.sin(phase))))
    clip.face(0.0).blinks([1.4, 4.2])

    # --- Stand1: hands clasped behind the back --------------------------------------------------
    clip = c("Stand1", 7.0, loop=True)
    for k in range(9):
        t = 7.0 * k / 8
        b = math.sin(2 * math.pi * k / 4)
        p = stance("male", breath=b, nod=-2.0, relaxed=False)
        back = p.point("BackLower", (0.0, 0.02, -0.19))
        p.reach("Left", add(back, (0.035, 0.0, 0.0)), pole=(1.0, 0.0, -0.3))
        p.reach("Right", add(back, (-0.035, 0.0, 0.0)), pole=(-1.0, 0.0, -0.3))
        p.fingers("Left", 0.5).fingers("Right", 0.5)
        clip.key(t, finish(p))
    clip.face(0.0).blinks([2.0, 5.1])

    # --- Stand2: arms folded across the chest ---------------------------------------------------
    clip = c("Stand2", 6.5, loop=True)
    for k in range(9):
        t = 6.5 * k / 8
        b = math.sin(2 * math.pi * k / 4)
        p = stance("male", breath=b, relaxed=False, tilt=1.5 * math.sin(2 * math.pi * k / 8))
        p.reach("Left", p.chest((-0.10, -0.06, 0.07)), pole=(1.0, -0.4, 0.2))
        p.reach("Right", p.chest((0.10, -0.02, 0.1)), pole=(-1.0, -0.4, 0.2))
        p.fingers("Left", 0.35).fingers("Right", 0.35)
        clip.key(t, finish(p))
    clip.face(0.0).blinks([1.1, 3.9, 5.6])

    # --- Stand3: hands on hips ------------------------------------------------------------------
    clip = c("Stand3", 6.0, loop=True)
    for k in range(9):
        t = 6.0 * k / 8
        b = math.sin(2 * math.pi * k / 4)
        p = stance("male", breath=b, relaxed=False, turn=4.0 * math.sin(2 * math.pi * k / 8))
        for side, sign in rig.SIDES:
            p.reach(side, p.point("BackLower", (sign * 0.19, 0.0, 0.02)), pole=(sign * 1.0, 0.1, -0.6))
            p.fingers(side, 0.2, thumb=0.0)
        clip.key(t, finish(p))
    clip.face(0.0).blinks([2.3, 4.8])

    # --- Stand4: looking around ----------------------------------------------------------------
    clip = c("Stand4", 8.0, loop=True)
    turns = [0, 0, 35, 35, 0, -30, -30, 0, 0]
    nods = [0, 0, -6, -6, 0, 4, 4, 0, 0]
    for k in range(9):
        t = 8.0 * k / 8
        clip.key(t, finish(stance("male", breath=math.sin(2 * math.pi * k / 4), turn=turns[k], nod=nods[k],
                                  twist=turns[k] * 0.15)))
    clip.face(0.0).face(1.7, eyes="LookLeft").face(3.4).face(4.7, eyes="LookRight").face(6.3).blinks([7.2])

    # --- Stand5: weight on one leg, tapping foot -------------------------------------------------
    clip = c("Stand5", 7.0, loop=True)
    for k in range(15):
        t = 7.0 * k / 14
        shift = 0.035 * (0.5 - 0.5 * math.cos(2 * math.pi * k / 14))
        tap = 0.03 if k in (4, 6, 8) else 0.0
        p = stance("male", root=(shift, -0.01 * shift / 0.035, 0.0), lean=-3.0 * shift / 0.035,
                   tilt=2.5 * shift / 0.035, breath=math.sin(2 * math.pi * k / 7))
        clip.key(t, finish(p, lift={"Right": (0.0, tap, 0.02)}, forward={"Right": (-8.0 * tap / 0.03, 0, 0)}))
    clip.face(0.0).blinks([1.0, 5.5])

    # --- Stand6: hands at the front of the thighs, rocking on the heels -------------------------
    clip = c("Stand6", 6.0, loop=True)
    for k in range(9):
        t = 6.0 * k / 8
        rock = math.sin(2 * math.pi * k / 8)
        p = stance("male", root=(0.0, 0.0, -0.012 * rock), bend=2.0 * rock, breath=math.sin(2 * math.pi * k / 4),
                   relaxed=False)
        for side, sign in rig.SIDES:
            p.reach(side, p.point("Hip" + side, (sign * 0.06, -0.10, 0.08)), pole=(sign * 1.0, 0.0, -0.4))
            p.fingers(side, 0.55)
        clip.key(t, finish(p))
    clip.face(0.0, mouth="Happy").blinks([2.6], mouth="Happy")

    # --- Stand7: rubbing hands in front ----------------------------------------------------------
    clip = c("Stand7", 7.0, loop=True)
    for k in range(15):
        t = 7.0 * k / 14
        rub = 0.02 * math.sin(2 * math.pi * k * 3 / 14)
        p = stance("male", breath=math.sin(2 * math.pi * k / 7), nod=6.0, relaxed=False)
        base = p.chest((0.0, -0.22, 0.12))
        p.reach("Left", add(base, (0.045, rub, 0.0)), pole=(1.0, -0.5, -0.2))
        p.reach("Right", add(base, (-0.045, -rub, 0.0)), pole=(-1.0, -0.5, -0.2))
        p.fingers("Left", 0.15).fingers("Right", 0.15)
        clip.key(t, finish(p))
    clip.face(0.0, mouth="Happy", eyes="LookDown").blinks([3.3], mouth="Happy", eyes="LookDown")

    # --- Clap ---------------------------------------------------------------------------------
    clip = c("Clap", 3.2)
    clip.key(0.0, finish(stance("male")))
    t = 0.45
    for n in range(5):
        for apart in (0.07, 0.0):
            p = stance("male", nod=-3.0, bend=-2.0, relaxed=False)
            front = p.chest((0.0, -0.08, 0.22))
            p.reach("Left", add(front, (apart + 0.03, 0.0, 0.0)), pole=(1.0, -0.8, -0.2))
            p.reach("Right", add(front, (-apart - 0.03, 0.0, 0.0)), pole=(-1.0, -0.8, -0.2))
            p.fingers("Left", 0.05).fingers("Right", 0.05)
            clip.key(t, finish(p))
            t += 0.21
    clip.key(3.2, finish(stance("male")))
    clip.face(0.0, mouth="Happy", eyes="Happy").face(0.45, mouth="Laughing", eyes="Laughing").face(2.6, mouth="Happy",
                                                                                                  eyes="Happy")

    # --- Wave ---------------------------------------------------------------------------------
    clip = c("Wave", 3.0)
    clip.key(0.0, finish(stance("male")))
    t = 0.5
    for n in range(7):
        p = stance("male", tilt=-4.0, lean=2.0)
        swing = 18.0 if n % 2 == 0 else -18.0
        p.arm("Right", shoulder=(-10.0, 0.0, 105.0), elbow=(0.0, 0.0, 55.0 + swing), wrist=(0.0, 0.0, 10.0 + swing * 0.5))
        p.fingers("Right", 0.0, spread=1.0)
        clip.key(t, finish(p))
        t += 0.3
    clip.key(3.0, finish(stance("male")))
    clip.face(0.0, mouth="Happy", eyes="Happy").face(0.6, mouth="PhoneticAi", eyes="Happy").face(1.1, mouth="Happy",
                                                                                                eyes="Happy")

    # --- Celebrate ------------------------------------------------------------------------------
    clip = c("Celebrate", 3.4)
    clip.key(0.0, finish(stance("male")))
    crouch = stance("male", root=(0.0, -0.08, 0.0), bend=10.0, relaxed=False)
    crouch.arm("Left", shoulder=(20.0, 0.0, -5.0), elbow=(-30.0, 0.0, 0.0)).arm("Right", shoulder=(20.0, 0.0, -5.0),
                                                                              elbow=(-30.0, 0.0, 0.0))
    clip.key(0.45, finish(crouch))
    for t, height in ((0.75, 0.09), (1.05, 0.0), (1.4, 0.07), (1.7, 0.0)):
        p = stance("male", root=(0.0, height, 0.0), bend=-6.0, nod=-12.0, relaxed=False)
        p.arm("Left", shoulder=(0.0, 0.0, 145.0), elbow=(0.0, 0.0, 10.0)).arm("Right", shoulder=(0.0, 0.0, 145.0),
                                                                            elbow=(0.0, 0.0, 10.0))
        p.fingers("Left", 0.9).fingers("Right", 0.9)
        clip.key(t, finish(p, lift={"Left": (0, height, 0), "Right": (0, height, 0)}) if height else finish(p))
    p = stance("male", nod=-8.0, relaxed=False)
    p.arm("Left", shoulder=(-30.0, 0.0, 60.0), elbow=(-80.0, 0.0, 0.0)).arm("Right", shoulder=(-30.0, 0.0, 60.0),
                                                                          elbow=(-80.0, 0.0, 0.0))
    p.fingers("Left", 0.9).fingers("Right", 0.9)
    clip.key(2.3, finish(p))
    clip.key(3.4, finish(stance("male")))
    clip.face(0.0, mouth="Happy").face(0.4, mouth="PhoneticO", eyes="Shocked", brows="Raised").face(
        0.75, mouth="Laughing", eyes="Laughing", brows="Raised").face(2.6, mouth="Happy", eyes="Happy")

    # --- female idles -----------------------------------------------------------------------------
    clip = c("FemaleIdleCheckNails", 6.0, body="female")
    clip.key(0.0, finish(stance("female")))
    for t, turn in ((0.9, 0.0), (2.2, 25.0), (3.4, 25.0), (4.6, 0.0)):
        p = stance("female", nod=18.0, tilt=-4.0, relaxed=True)
        p.reach("Left", p.chest((0.03, 0.02, 0.18)), pole=(1.0, -0.8, 0.0))
        p.rotate("WristLeft", 0.0, -60.0 - turn, 20.0)
        p.fingers("Left", 0.05)
        clip.key(t, finish(p))
    clip.key(6.0, finish(stance("female")))
    clip.face(0.0).face(0.8, eyes="LookDown").face(4.8).blinks([3.0], eyes="LookDown")

    clip = c("FemaleIdleLookAround", 7.0, body="female")
    for t, turn, nod in ((0.0, 0, 0), (1.2, 30, -4), (2.4, 30, -4), (3.4, -32, 2), (4.8, -32, 2), (5.9, 0, -10),
                         (7.0, 0, 0)):
        clip.key(t, finish(stance("female", turn=turn, nod=nod, twist=turn * 0.2, tilt=turn * 0.08)))
    clip.face(0.0).face(1.1, eyes="LookLeft").face(3.3, eyes="LookRight").face(5.6, eyes="LookUp").face(6.6)

    clip = c("FemaleIdleShiftWeight", 6.0, body="female")
    for t, shift in ((0.0, 0.0), (1.2, 0.04), (2.6, 0.04), (3.8, -0.04), (5.0, -0.04), (6.0, 0.0)):
        p = stance("female", root=(shift, -abs(shift) * 0.25, 0.0), lean=-60 * shift, tilt=70 * shift)
        clip.key(t, finish(p))
    clip.face(0.0).blinks([2.0, 4.4])

    clip = c("FemaleIdleFixShoe", 6.5, body="female")
    clip.key(0.0, finish(stance("female")))
    for t in (1.3, 2.4, 3.6, 4.6):
        p = stance("female", bend=38.0, nod=10.0, relaxed=True)
        p.arm("Left", shoulder=(-10.0, 0.0, 25.0), elbow=(-10.0, 0.0, 0.0))
        foot = add(p.bind[rig.INDEX["AnkleRight"]], (0.0, 0.28, -0.12))
        p.reach("Right", add(foot, (0.0, 0.03 + (0.015 if t in (2.4, 4.6) else 0.0), 0.03)), pole=(-0.5, -0.2, 1.0))
        p.fingers("Right", 0.4)
        clip.key(t, finish(p, lift={"Right": (0.0, 0.28, -0.12)}, forward={"Right": (35.0, 0.0, 0.0)}))
    clip.key(6.5, finish(stance("female")))
    clip.face(0.0).face(1.0, eyes="LookDown").face(5.4)

    # --- female emotes ----------------------------------------------------------------------------
    clip = c("FemaleAngry", 4.0, body="female")
    clip.key(0.0, finish(stance("female")))
    for t, shake, stomp in ((0.5, 0, 0.0), (1.0, 12, 0.06), (1.3, -12, 0.0), (1.6, 12, 0.06), (1.9, -12, 0.0),
                            (2.8, 0, 0.0)):
        p = stance("female", bend=6.0, turn=shake, nod=-4.0, relaxed=False)
        for side, sign in rig.SIDES:
            p.reach(side, p.point("BackLower", (sign * 0.18, 0.0, 0.03)), pole=(sign * 1.0, 0.1, -0.6))
            p.fingers(side, 0.9)
        clip.key(t, finish(p, lift={"Right": (0.0, stomp, 0.03)}))
    clip.key(4.0, finish(stance("female")))
    clip.face(0.0, mouth="Angry", eyes="Angry", brows="Angry").face(3.6)

    clip = c("FemaleConfused", 4.0, body="female")
    clip.key(0.0, finish(stance("female")))
    for t, scratch in ((0.8, 0.0), (1.2, 0.02), (1.6, 0.0), (2.0, 0.02), (2.4, 0.0), (2.9, 0.0)):
        p = stance("female", tilt=12.0, turn=-6.0, relaxed=True)
        p.reach("Right", p.face((-0.16, 0.06 + scratch, -0.2)), pole=(-1.0, 0.3, -0.2))
        p.fingers("Right", 0.35)
        clip.key(t, finish(p))
    clip.key(4.0, finish(stance("female")))
    clip.face(0.0).face(0.5, mouth="Confused", eyes="Confused", brows="Confused").face(3.5)

    clip = c("FemaleLaugh", 4.0, body="female")
    clip.key(0.0, finish(stance("female")))
    for n, t in enumerate((0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0, 2.25, 2.6)):
        up = 1.0 if n % 2 else 0.0
        p = stance("female", bend=8.0 + 4.0 * up, nod=-8.0 + 6.0 * up, relaxed=True)
        p.shrug("Left", 6.0 * up).shrug("Right", 6.0 * up)
        p.reach("Right", p.face((-0.02, -0.08, -0.05)), pole=(-1.0, -0.6, -0.2))
        p.fingers("Right", 0.2)
        clip.key(t, finish(p))
    clip.key(4.0, finish(stance("female")))
    clip.face(0.0, mouth="Happy").face(0.4, mouth="Laughing", eyes="Laughing").face(3.2, mouth="Happy", eyes="Happy")

    clip = c("FemaleCry", 5.0, body="female")
    clip.key(0.0, finish(stance("female")))
    for n, t in enumerate((0.9, 1.3, 1.7, 2.1, 2.5, 2.9, 3.4)):
        sob = 1.0 if n % 2 else 0.0
        p = stance("female", bend=12.0, nod=20.0 + 4.0 * sob, relaxed=False)
        p.shrug("Left", 5.0 * sob).shrug("Right", 5.0 * sob)
        p.reach("Left", p.face((0.05, 0.0, -0.09)), pole=(1.0, -1.0, 0.0))
        p.reach("Right", p.face((-0.05, 0.0, -0.09)), pole=(-1.0, -1.0, 0.0))
        p.fingers("Left", 0.3).fingers("Right", 0.3)
        clip.key(t, finish(p))
    clip.key(5.0, finish(stance("female")))
    clip.face(0.0, mouth="Sad", eyes="Sad", brows="Sad").face(4.6)

    clip = c("FemaleShocked", 3.5, body="female")
    clip.key(0.0, finish(stance("female")))
    for t, back in ((0.35, 0.04), (1.0, 0.05), (2.2, 0.05)):
        p = stance("female", root=(0.0, 0.0, -back), bend=-8.0, nod=-6.0, relaxed=False)
        p.reach("Left", p.face((0.1, -0.06, -0.12)), pole=(1.0, -1.0, 0.0))
        p.reach("Right", p.face((-0.1, -0.06, -0.12)), pole=(-1.0, -1.0, 0.0))
        p.fingers("Left", 0.1, spread=1.0).fingers("Right", 0.1, spread=1.0)
        clip.key(t, finish(p))
    clip.key(3.5, finish(stance("female")))
    clip.face(0.0).face(0.3, mouth="Shocked", eyes="Shocked", brows="Raised").face(3.0)

    clip = c("FemaleYawn", 5.0, body="female")
    clip.key(0.0, finish(stance("female")))
    for t, open_ in ((1.0, 0.5), (1.8, 1.0), (2.8, 1.0), (3.6, 0.3)):
        p = stance("female", bend=-6.0 * open_, nod=-12.0 * open_, relaxed=True)
        p.reach("Right", p.face((-0.01, -0.09, -0.06)), pole=(-1.0, -0.6, -0.2))
        p.fingers("Right", 0.1)
        p.arm("Left", shoulder=(0.0, 0.0, 30.0 * open_), elbow=(-20.0 * open_, 0.0, 0.0))
        clip.key(t, finish(p))
    clip.key(5.0, finish(stance("female")))
    clip.face(0.0).face(0.8, mouth="PhoneticO", eyes="Yawning").face(1.6, mouth="Shocked", eyes="Sleeping").face(
        3.4, mouth="PhoneticW", eyes="Yawning").face(4.4)

    # --- male idles -----------------------------------------------------------------------------
    clip = c("MaleIdleLookAround", 7.0)
    for t, turn, nod in ((0.0, 0, 0), (1.3, -34, -2), (2.5, -34, -2), (3.6, 30, 3), (5.0, 30, 3), (6.0, 0, 8),
                         (7.0, 0, 0)):
        clip.key(t, finish(stance("male", turn=turn, nod=nod, twist=turn * 0.25)))
    clip.face(0.0).face(1.2, eyes="LookRight").face(3.5, eyes="LookLeft").face(5.8, eyes="LookDown").face(6.7)

    clip = c("MaleIdleStretch", 6.5)
    clip.key(0.0, finish(stance("male")))
    for t, twist in ((1.2, 0.0), (2.2, 18.0), (3.2, -18.0), (4.2, 0.0)):
        p = stance("male", bend=-10.0, twist=twist, nod=-15.0, relaxed=False)
        p.arm("Left", shoulder=(-15.0, 0.0, 150.0), elbow=(0.0, 0.0, 12.0))
        p.arm("Right", shoulder=(-15.0, 0.0, 150.0), elbow=(0.0, 0.0, 12.0))
        p.fingers("Left", 0.6).fingers("Right", 0.6)
        clip.key(t, finish(p))
    clip.key(6.5, finish(stance("male")))
    clip.face(0.0).face(1.0, mouth="PhoneticO", eyes="Sleeping").face(4.4)

    clip = c("MaleIdleShiftWeight", 6.0)
    for t, shift in ((0.0, 0.0), (1.3, -0.035), (2.7, -0.035), (3.9, 0.035), (5.1, 0.035), (6.0, 0.0)):
        clip.key(t, finish(stance("male", root=(shift, -abs(shift) * 0.2, 0.0), lean=-50 * shift, tilt=40 * shift)))
    clip.face(0.0).blinks([1.7, 4.5])

    clip = c("MaleIdleCheckHand", 5.5)
    clip.key(0.0, finish(stance("male")))
    for t, flip, curl in ((0.9, 0.0, 0.1), (2.0, 0.0, 0.1), (2.7, 150.0, 0.1), (3.5, 150.0, 0.9), (4.3, 0.0, 0.2)):
        p = stance("male", nod=16.0, turn=-6.0, relaxed=True)
        p.reach("Right", p.chest((-0.05, 0.05, 0.2)), pole=(-1.0, -0.8, 0.0))
        p.rotate("WristRight", 0.0, 50.0 - flip * 0.6, 0.0)
        p.fingers("Right", curl)
        clip.key(t, finish(p))
    clip.key(5.5, finish(stance("male")))
    clip.face(0.0).face(0.8, eyes="LookDown").face(4.6)

    # --- male emotes ----------------------------------------------------------------------------
    clip = c("MaleAngry", 4.0)
    clip.key(0.0, finish(stance("male")))
    for n, t in enumerate((0.5, 0.8, 1.1, 1.4, 1.7, 2.0, 2.6)):
        shake = 0.025 if n % 2 else -0.025
        p = stance("male", bend=12.0, nod=-8.0, relaxed=False)
        p.reach("Left", p.chest((0.12, -0.02 + shake, 0.2)), pole=(1.0, -1.0, -0.2))
        p.reach("Right", p.chest((-0.12, -0.02 - shake, 0.2)), pole=(-1.0, -1.0, -0.2))
        p.fingers("Left", 1.0).fingers("Right", 1.0)
        clip.key(t, finish(p))
    clip.key(4.0, finish(stance("male")))
    clip.face(0.0, mouth="Angry", eyes="Angry", brows="Angry").face(3.6)

    clip = c("MaleConfused", 4.0)
    clip.key(0.0, finish(stance("male")))
    for t in (0.7, 1.4, 2.4):
        p = stance("male", tilt=-10.0, relaxed=False)
        p.shrug("Left", 9.0).shrug("Right", 9.0)
        p.arm("Left", shoulder=(-10.0, 0.0, 22.0), elbow=(-85.0, -40.0, 0.0), wrist=(0.0, 60.0, 0.0))
        p.arm("Right", shoulder=(-10.0, 0.0, 22.0), elbow=(-85.0, -40.0, 0.0), wrist=(0.0, 60.0, 0.0))
        p.fingers("Left", 0.1).fingers("Right", 0.1)
        clip.key(t, finish(p))
    clip.key(4.0, finish(stance("male")))
    clip.face(0.0).face(0.5, mouth="Confused", eyes="Confused", brows="Confused").face(3.4)

    clip = c("MaleLaugh", 4.0)
    clip.key(0.0, finish(stance("male")))
    for n, t in enumerate((0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0, 2.3, 2.7)):
        up = 1.0 if n % 2 else 0.0
        p = stance("male", bend=-8.0 - 4.0 * up, nod=-12.0, relaxed=False)
        belly = p.point("BackLower", (0.0, 0.08, 0.16))
        p.reach("Left", add(belly, (0.05, 0.0, 0.0)), pole=(1.0, -0.2, -0.2))
        p.reach("Right", add(belly, (-0.05, 0.02, 0.0)), pole=(-1.0, -0.2, -0.2))
        p.shrug("Left", 5.0 * up).shrug("Right", 5.0 * up)
        p.fingers("Left", 0.2).fingers("Right", 0.2)
        clip.key(t, finish(p))
    clip.key(4.0, finish(stance("male")))
    clip.face(0.0, mouth="Happy").face(0.4, mouth="Laughing", eyes="Laughing").face(3.2, mouth="Happy", eyes="Happy")

    clip = c("MaleCry", 5.0)
    clip.key(0.0, finish(stance("male")))
    for n, t in enumerate((0.9, 1.35, 1.8, 2.25, 2.7, 3.2)):
        sob = 1.0 if n % 2 else 0.0
        p = stance("male", bend=10.0, nod=22.0 + 3.0 * sob, relaxed=True)
        p.shrug("Left", 4.0 * sob).shrug("Right", 4.0 * sob)
        p.reach("Right", p.face((0.0, 0.03, -0.08)), pole=(-1.0, -0.4, 0.2))
        p.fingers("Right", 0.3)
        clip.key(t, finish(p))
    clip.key(5.0, finish(stance("male")))
    clip.face(0.0, mouth="Sad", eyes="Sad", brows="Sad").face(4.6)

    clip = c("MaleSurprised", 3.5)
    clip.key(0.0, finish(stance("male")))
    for t, back in ((0.3, 0.06), (0.9, 0.07), (2.2, 0.07)):
        p = stance("male", root=(0.0, 0.0, -back), bend=-10.0, nod=-8.0, relaxed=False)
        p.arm("Left", shoulder=(-40.0, 0.0, 30.0), elbow=(-90.0, 0.0, 0.0), wrist=(40.0, 0.0, 0.0))
        p.arm("Right", shoulder=(-40.0, 0.0, 30.0), elbow=(-90.0, 0.0, 0.0), wrist=(40.0, 0.0, 0.0))
        p.fingers("Left", 0.0, spread=1.0).fingers("Right", 0.0, spread=1.0)
        clip.key(t, finish(p))
    clip.key(3.5, finish(stance("male")))
    clip.face(0.0).face(0.25, mouth="Shocked", eyes="Shocked", brows="Raised").face(3.0)

    clip = c("MaleYawn", 5.0)
    clip.key(0.0, finish(stance("male")))
    for t, open_ in ((1.0, 0.5), (1.8, 1.0), (2.8, 1.0), (3.6, 0.3)):
        p = stance("male", bend=-8.0 * open_, nod=-14.0 * open_, relaxed=True)
        p.reach("Right", p.face((0.0, -0.1, -0.07)), pole=(-1.0, -0.6, -0.2))
        p.fingers("Right", 0.8)
        p.arm("Left", shoulder=(0.0, 0.0, 40.0 * open_), elbow=(-25.0 * open_, 0.0, 0.0))
        clip.key(t, finish(p))
    clip.key(5.0, finish(stance("male")))
    clip.face(0.0).face(0.8, mouth="PhoneticO", eyes="Yawning").face(1.6, mouth="Shocked", eyes="Sleeping").face(
        3.4, mouth="PhoneticW", eyes="Yawning").face(4.4)

    assert [clip.name for clip in clips] == PRESETS, [clip.name for clip in clips]
    return clips
