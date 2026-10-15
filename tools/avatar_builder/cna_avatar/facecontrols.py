# SPDX-License-Identifier: MS-PL
"""The catalog's face-shape controls (catalog v3): what each byte of a format 2 description's face block does.

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
              "eyeTilt", "browHeight", "noseSize", "noseWidth", "noseHeight", "mouthWidth", "mouthHeight", "earSize",
              # Format 3's second block.
              "foreheadHeight", "cheekboneWidth", "jawTaper", "chinHeight", "chinProjection", "browSpacing", "browAngle",
              "browThickness", "noseBridge", "noseTip", "upperLip", "lowerLip", "earHeight", "earAngle", "jawAngle", "eyeShape"]


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
        ("head", [op(c, whole, 0.8, scale=(1.14, 1.0, 1.0))]),
        ("head", [op(add(c, (0.0, -0.17 * s, 0.0)), whole, 0.8, scale=(1.0, 1.10, 1.0))]),
        ("face", [op(local(0.0, -0.10, 0.03), radii(0.17, 0.09, 0.17), 0.3, scale=(1.30, 1.0, 1.03))]),
        ("face", [op(a["chin"], radii(0.06, 0.05, 0.06), 0.3, scale=(1.30, 1.30, 1.30))]),
        ("face", [op(local(side * 0.072, -0.050, 0.060), radii(0.058, 0.050, 0.058), 0.2, move=(side * 0.013 * s, 0.0, 0.007 * s))
                  for side in (1.0, -1.0)]),
        ("face", [op(e, radii(0.050, 0.050, 0.030), 0.72, scale=(1.24, 1.24, 1.0)) for e, _ in eyes]),
        ("face", [op(e, radii(0.052, 0.052, 0.032), 0.72, move=(side * 0.009 * s, 0.0, 0.0)) for e, side in eyes]),
        ("face", [op(e, radii(0.052, 0.058, 0.032), 0.72, move=(0.0, 0.009 * s, 0.0)) for e, _ in eyes + brows]),
        ("face", [op(e, radii(0.050, 0.050, 0.032), 0.72, rotate=(0.0, 0.0, 1.0), degrees=side * 14.0) for e, side in eyes]),
        ("face", [op(b, radii(0.050, 0.030, 0.030), 0.70, move=(0.0, 0.0095 * s, 0.0)) for b, _ in brows]),
        ("face", [op(nose, radii(0.040, 0.050, 0.050), 0.55, scale=(1.32, 1.32, 1.32))]),
        ("face", [op(nose, radii(0.040, 0.050, 0.050), 0.55, scale=(1.38, 1.0, 1.0))]),
        ("face", [op(nose, radii(0.034, 0.040, 0.050), 0.55, move=(0.0, 0.0080 * s, 0.0))]),
        ("face", [op(mouth, radii(0.064, 0.034, 0.032), 0.70, scale=(1.30, 1.0, 1.0))]),
        ("face", [op(mouth, radii(0.064, 0.036, 0.034), 0.70, move=(0.0, 0.0085 * s, 0.0))]),
        ("face", [op(e, radii(0.040, 0.050, 0.040), 0.60, scale=(1.40, 1.40, 1.40)) for e, _ in ears]),
        # Forehead: the crown rises or falls, the brows stay (hair and hats follow it).
        ("head", [op(local(0.0, 0.17, -0.01), radii(0.30, 0.20, 0.30), 0.45, move=(0.0, 0.011 * s, 0.0))]),
        ("face", [op(local(0.0, -0.035, 0.05), radii(0.17, 0.055, 0.12), 0.35, scale=(1.17, 1.0, 1.0))]),
        ("face", [op(local(0.0, -0.135, 0.06), radii(0.14, 0.055, 0.12), 0.30, scale=(0.75, 1.0, 1.0))]),
        ("face", [op(a["chin"], radii(0.07, 0.05, 0.07), 0.35, move=(0.0, -0.010 * s, 0.0))]),
        ("face", [op(a["chin"], radii(0.07, 0.05, 0.07), 0.35, move=(0.0, 0.0, 0.010 * s))]),
        ("face", [op(b, radii(0.045, 0.026, 0.028), 0.70, move=(side * 0.0065 * s, 0.0, 0.0)) for b, side in brows]),
        ("face", [op(b, radii(0.048, 0.026, 0.028), 0.72, rotate=(0.0, 0.0, 1.0), degrees=side * 13.0) for b, side in brows]),
        ("face", [op(b, radii(0.050, 0.020, 0.026), 0.65, scale=(1.0, 1.40, 1.0)) for b, side in brows]),
        ("face", [op(a["noseRoot"], radii(0.026, 0.034, 0.040), 0.45, scale=(1.40, 1.0, 1.20))]),
        ("face", [op(a["noseTip"], radii(0.022, 0.020, 0.026), 0.45, move=(0.0, 0.0042 * s, 0.0026 * s))]),
        ("face", [op(add(mouth, (0.0, 0.010 * s, 0.0)), radii(0.060, 0.016, 0.030), 0.55, scale=(1.0, 1.45, 1.0))]),
        ("face", [op(add(mouth, (0.0, -0.012 * s, 0.0)), radii(0.060, 0.018, 0.030), 0.55, scale=(1.0, 1.45, 1.0))]),
        ("face", [op(e, radii(0.040, 0.050, 0.040), 0.60, move=(0.0, 0.011 * s, 0.0)) for e, _ in ears]),
        ("face", [op(e, radii(0.040, 0.050, 0.040), 0.60, rotate=(0.0, 1.0, 0.0), degrees=-side * 18.0) for e, side in ears]),
        ("face", [op(local(side * 0.105, -0.110, -0.01), radii(0.065, 0.075, 0.085), 0.30, move=(side * 0.014 * s, -0.003 * s, 0.0))
                  for side in (1.0, -1.0)]),
        ("face", [op(e, radii(0.046, 0.044, 0.030), 0.72, scale=(1.0, 1.26, 1.0)) for e, _ in eyes]),
    ]
    assert len(out) == len(PARAMETERS)
    return [{"parameter": index, "name": PARAMETERS[index], "scope": scope, "ops": ops} for index, (scope, ops) in enumerate(out)]
