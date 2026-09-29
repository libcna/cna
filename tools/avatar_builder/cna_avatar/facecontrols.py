# SPDX-License-Identifier: MS-PL
"""The catalog's face-shape controls: what each byte of a format 2 description's face block does.

A control holds deformers, each a scale, move or rotation about a centre, weighted 1 inside an
ellipsoid's `inner` fraction and fading to 0 at its surface (smoothstep). A parameter byte b gives
t = clamp((b - 128) / 127, -1, 1): a scale factor f becomes f**t, a move or angle is multiplied by
t. Displacements from every deformer are summed from the undeformed position and applied in
proportion to the vertex's Head weight. Scope "face" deforms the body's head, nose, ears, face
decals and facial hair; "head" also hair, hats and glasses. Coordinates are the body's authored
model space. The runtime (AvatarModel.cpp) and the preview (preview.py) implement exactly this.
"""
from . import head
from .mathutil import add, sub, mul

PARAMETERS = ["headWidth", "headHeight", "jawWidth", "chinSize", "cheekFullness", "eyeSize", "eyeSpacing", "eyeHeight",
              "eyeTilt", "browHeight", "noseSize", "noseWidth", "noseHeight", "mouthWidth", "mouthHeight", "earSize"]


def _r(v):
    return [round(c, 5) for c in v]


def controls(body):
    s = head.SCALE[body]
    c = head.centre(body)
    a = head.anchors(body)
    local = lambda x, y, z: add(c, mul((x, y, z), s))
    radii = lambda x, y, z: mul((x, y, z), s)

    def op(centre, rad, inner, **kind):
        entry = {"centre": _r(centre), "radii": _r(rad), "inner": inner}
        for key, value in kind.items():
            entry[key] = _r(value) if isinstance(value, (tuple, list)) else round(value, 4)
        return entry

    eyes = [(a["eyeLeft"], 1.0), (a["eyeRight"], -1.0)]
    brows = [(a["browLeft"], 1.0), (a["browRight"], -1.0)]
    ears = [(a["earLeft"], 1.0), (a["earRight"], -1.0)]
    nose = mul(add(a["noseTip"], a["noseRoot"]), 0.5)
    mouth = a["mouth"]
    whole = radii(0.34, 0.34, 0.34)
    out = [
        ("head", [op(c, whole, 0.8, scale=(1.08, 1.0, 1.0))]),
        ("head", [op(add(c, (0.0, -0.17 * s, 0.0)), whole, 0.8, scale=(1.0, 1.06, 1.0))]),
        ("face", [op(local(0.0, -0.10, 0.03), radii(0.17, 0.09, 0.17), 0.3, scale=(1.14, 1.0, 1.02))]),
        ("face", [op(a["chin"], radii(0.06, 0.05, 0.06), 0.3, scale=(1.18, 1.18, 1.18))]),
        ("face", [op(local(side * 0.072, -0.040, 0.060), radii(0.055, 0.045, 0.055), 0.2, move=(side * 0.006 * s, 0.0, 0.004 * s))
                  for side in (1.0, -1.0)]),
        ("face", [op(e, radii(0.050, 0.050, 0.030), 0.72, scale=(1.16, 1.16, 1.0)) for e, _ in eyes]),
        ("face", [op(e, radii(0.052, 0.052, 0.032), 0.72, move=(side * 0.0065 * s, 0.0, 0.0)) for e, side in eyes]),
        ("face", [op(e, radii(0.052, 0.058, 0.032), 0.72, move=(0.0, 0.0065 * s, 0.0)) for e, _ in eyes + brows]),
        ("face", [op(e, radii(0.050, 0.050, 0.032), 0.72, rotate=(0.0, 0.0, 1.0), degrees=side * 9.0) for e, side in eyes]),
        ("face", [op(b, radii(0.050, 0.030, 0.030), 0.70, move=(0.0, 0.0065 * s, 0.0)) for b, _ in brows]),
        ("face", [op(nose, radii(0.040, 0.050, 0.050), 0.55, scale=(1.20, 1.20, 1.20))]),
        ("face", [op(nose, radii(0.040, 0.050, 0.050), 0.55, scale=(1.25, 1.0, 1.0))]),
        ("face", [op(nose, radii(0.034, 0.040, 0.050), 0.55, move=(0.0, 0.0055 * s, 0.0))]),
        ("face", [op(mouth, radii(0.064, 0.034, 0.032), 0.70, scale=(1.20, 1.0, 1.0))]),
        ("face", [op(mouth, radii(0.064, 0.036, 0.034), 0.70, move=(0.0, 0.0060 * s, 0.0))]),
        ("face", [op(e, radii(0.040, 0.050, 0.040), 0.60, scale=(1.25, 1.25, 1.25)) for e, _ in ears]),
    ]
    assert len(out) == len(PARAMETERS)
    return [{"parameter": index, "name": PARAMETERS[index], "scope": scope, "ops": ops} for index, (scope, ops) in enumerate(out)]
