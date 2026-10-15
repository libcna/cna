# SPDX-License-Identifier: MS-PL
"""The catalog's entries and their GLB files.

Catalog v3 revision 2 keeps every id of the first v3 release in its kind (format 1 and 2
descriptions name only those) and adds entries only format 3 descriptions name ("format": 3).
Textures shared by many assets are catalog files of their own (TEXTURES), named by each material.
"""
from . import rig, body, textures
from .glb import GlbBuilder


FEATURE_TINTS = {"eyeLeft": ("none", "eyes"), "eyeRight": ("none", "eyes"), "eyebrowLeft": ("hair",),
                 "eyebrowRight": ("hair",), "mouth": ("none",)}
# Highlight scale per material family: wet eyes and lips, hair and leather sheen, matte cloth.
SPECULAR = {"eyeLeft": (2.5, 3.0), "eyeRight": (2.5, 3.0), "eyebrowLeft": (0.4,), "eyebrowRight": (0.4,), "mouth": (1.6,)}
CLOTH_SPECULAR = {"knit": 0.35, "rib": 0.35, "cable": 0.3, "fleece": 0.25, "denim": 0.45, "canvas": 0.5, "pleat": 0.4,
                  "leather": 2.0, "plain": 1.0, "strands": 2.0}
HAIR_SPECULAR = 2.2
# Makeup decals: (tint, specular, layer).
MAKEUP_DECALS = {"shadowLeft": ("eyeShadow", 0.6, 0), "shadowRight": ("eyeShadow", 0.6, 0), "blushLeft": ("blush", 0.2, 0),
                 "blushRight": ("blush", 0.2, 0), "lips": ("lips", 1.8, 0), "linerLeft": ("none", 1.0, 1),
                 "linerRight": ("none", 1.0, 1)}


def texture_file(kind):
    """The catalog file of a shared texture."""
    return "tex_%s.png" % kind


def texture_bytes(kind):
    if kind == "hair_clump":
        return textures.hair_clump()
    if kind == "hair_cap":
        return textures.hair_cap()
    if kind == "skin_head":
        return textures.skin_head()
    return textures.cloth(kind)


def body_glb(body_type):
    """Body, head and the face-feature and makeup decal patches (drawn with atlas tiles)."""
    from . import face
    skin, head, details = body.build(body_type)
    builder = GlbBuilder({"cna": {"asset": "body", "bodyType": body_type, "rig": "cna-avatar-71",
                                  "authoredHeight": rig.PROPORTIONS[body_type]["height"]}})
    skin_index = builder.add_rig(body_type)
    skin_material = builder.add_material("skin", "skin")
    parts = [(skin, skin_material), (head, builder.add_material("head", "skin", texture_file=texture_file("skin_head"))),
             (details, skin_material)]
    decals = face.build_decals(body_type)
    makeup = face.build_makeup(body_type)

    def decal(name, mesh, tint, specular, layer):
        parts.append((mesh, builder.add_material("%s%d" % (name, layer), tint, specular=specular,
                                                 extras={"cnaFeature": name, "cnaLayer": layer})))
    # Draw order within a layer is this order: makeup under the features it frames.
    for name in ("shadowLeft", "shadowRight", "blushLeft", "blushRight"):
        decal(name, makeup[name], *MAKEUP_DECALS[name])
    for feature in ("eyeLeft", "eyeRight", "eyebrowLeft", "eyebrowRight"):
        for layer, tint in enumerate(FEATURE_TINTS[feature]):
            decal(feature, decals[feature], tint, SPECULAR[feature][layer], layer)
    decal("lips", makeup["lips"], *MAKEUP_DECALS["lips"])
    decal("mouth", decals["mouth"], "none", SPECULAR["mouth"][0], 0)
    for name in ("linerLeft", "linerRight"):
        decal(name, makeup[name], *MAKEUP_DECALS[name])
    builder.add_skinned_mesh("body", parts, skin_index)
    return builder.build(), ["skin_head"]


# (id, slot, name, CreateRandom weight for female, male[, extras]). Ids of the first v3 release
# (1-123) keep their kind; everything since needs format 3.
ITEMS = [
    (1, "hair", "hair_short", 0.6, 1.4), (2, "hair", "hair_spiky", 0.4, 1.0), (3, "hair", "hair_bob", 1.4, 0.4),
    (4, "hair", "hair_ponytail", 1.4, 0.3), (5, "hair", "hair_buzz", 0.3, 1.2), (6, "hair", "hair_long", 1.4, 0.3),
    (7, "hair", "hair_curly", 1.0, 1.0), (8, "hair", "hair_sidepart", 0.6, 1.2), (9, "hair", "hair_bun", 1.2, 0.2),
    (10, "hair", "hair_pigtails", 1.0, 0.1),
    (20, "top", "top_tshirt", 1.0, 1.0), (21, "top", "top_longsleeve", 1.0, 1.0), (22, "top", "top_hoodie", 1.0, 1.0),
    (23, "top", "top_tank", 1.0, 0.6), (24, "top", "top_shirt", 0.8, 1.0), (25, "top", "top_sweater", 1.0, 1.0),
    (26, "top", "top_jacket", 1.0, 1.0), (27, "top", "top_polo", 0.7, 1.0), (28, "top", "top_turtleneck", 1.0, 0.7),
    (40, "bottom", "bottom_jeans", 1.0, 1.4), (41, "bottom", "bottom_shorts", 0.8, 1.0), (42, "bottom", "bottom_skirt", 1.2, 0.05),
    (43, "bottom", "bottom_cargo", 0.6, 1.2), (44, "bottom", "bottom_joggers", 1.0, 1.0),
    (45, "bottom", "bottom_longskirt", 0.8, 0.02),
    (60, "shoes", "shoes_sneakers", 1.2, 1.2), (61, "shoes", "shoes_boots", 1.0, 1.0), (62, "shoes", "shoes_hightops", 1.0, 1.0),
    (63, "shoes", "shoes_dress", 0.6, 1.0), (64, "shoes", "shoes_flats", 1.0, 0.05),
    (80, "glasses", "glasses_round", 1.0, 1.0, {"follow": "eyes"}), (81, "glasses", "glasses_square", 1.0, 1.0, {"follow": "eyes"}),
    (82, "glasses", "glasses_aviator", 1.0, 1.0, {"follow": "eyes"}),
    (83, "glasses", "glasses_browline", 1.0, 1.0, {"follow": "eyes"}),
    (100, "hat", "hat_cap", 1.0, 1.0), (101, "hat", "hat_beanie", 1.0, 1.0), (102, "hat", "hat_bucket", 1.0, 1.0),
    (103, "hat", "hat_fedora", 0.8, 1.0), (104, "hat", "hat_headband", 1.0, 0.3),
]
# Personal features: facial hair (format 2) and ear shapes (format 3).
FEATURE_ITEMS = [
    (120, "facialHair", "facial_mustache", 0.0, 1.0), (121, "facialHair", "facial_goatee", 0.0, 1.0),
    (122, "facialHair", "facial_beard", 0.0, 1.0), (123, "facialHair", "facial_chinstrap", 0.0, 1.0),
] + [(500 + k, "ears", "ears_" + shape.lower(), 1.0, 1.0) for k, shape in enumerate(
    ("round", "small", "large", "longLobe", "attached", "protruding"))]
EAR_SHAPE_OF = {"ears_" + shape.lower(): shape for shape in ("round", "small", "large", "longLobe", "attached", "protruding")}


def face_styles(styles):
    """Face style entries from the atlas's style tiles: eyebrows, eye shadow, eyeliner, blush, lips."""
    from . import face
    out = []
    brow_ids = {name: 600 + k for k, name in enumerate(n for n in face.BROW_STYLES if n != "classic")}
    brow_weights = {"natural": (1.4, 1.2), "straight": (0.6, 1.2), "softArch": (1.2, 0.5), "highArch": (0.8, 0.2),
                    "thin": (0.8, 0.3), "thick": (0.6, 1.2), "short": (0.5, 0.6), "long": (0.6, 0.5), "angled": (0.5, 0.5),
                    "rounded": (0.8, 0.5), "bushy": (0.2, 0.9), "lowFlat": (0.4, 1.0)}
    for name, id_ in brow_ids.items():
        out.append({"id": id_, "slot": "eyebrows", "name": "brows_" + name.lower(), "tiles": styles[("eyebrows", name)],
                    "random": dict(zip(("female", "male"), brow_weights[name])), "format": 3})
    for k, name in enumerate(face.SHADOW_STYLES):
        out.append({"id": 640 + k, "slot": "eyeShadow", "name": "shadow_" + name, "tiles": styles[("eyeShadow", name)],
                    "random": {"female": 1.0, "male": 0.1}, "format": 3, "opacity": 0.85})
    for k, name in enumerate(face.LINER_STYLES):
        out.append({"id": 650 + k, "slot": "eyeliner", "name": "liner_" + name, "tiles": styles[("eyeliner", name)],
                    "random": {"female": 1.0, "male": 0.1}, "format": 3})
    for k, name in enumerate(face.BLUSH_STYLES):
        out.append({"id": 660 + k, "slot": "blush", "name": "blush_" + name, "tiles": styles[("blush", name)],
                    "random": {"female": 1.0, "male": 0.1}, "format": 3, "opacity": 0.75})
    for k, (name, opacity) in enumerate((("tint", 0.45), ("color", 0.75), ("bold", 0.95))):
        out.append({"id": 670 + k, "slot": "lips", "name": "lips_" + name, "tiles": styles[("lips", "mask")],
                    "random": {"female": 1.0, "male": 0.05}, "format": 3, "opacity": opacity})
    return out


def item_glb(body_type, item_id, slot, name):
    """One item fitted to one body. Returns (GLB bytes, shared texture kinds it names)."""
    from . import hair, garments, accessories, head
    builder = GlbBuilder({"cna": {"asset": "item", "id": item_id, "slot": slot, "name": name, "bodyType": body_type,
                                  "rig": "cna-avatar-71"}})
    skin_index = builder.add_rig(body_type)
    parts = []
    used = []

    def material(part_name, tint, kind, colour=(1.0, 1.0, 1.0), specular=None, extras=None):
        if kind not in used:
            used.append(kind)
        return builder.add_material(part_name, tint, color=colour, texture_file=texture_file(kind), specular=specular, extras=extras)
    if slot == "hair":
        locks, scalp = hair.build(body_type, name)
        if scalp.positions:
            parts.append((scalp, material(name + "_scalp", "hair", "hair_cap", specular=HAIR_SPECULAR)))
        if locks.positions:
            parts.append((locks, material(name, "hair", "hair_clump", specular=HAIR_SPECULAR)))
    elif slot == "ears":
        parts.append((head.ear_meshes(body_type, EAR_SHAPE_OF[name]), builder.add_material(name, "skin")))
    else:
        built = {"top": garments.top, "bottom": garments.bottom, "shoes": garments.shoes, "glasses": accessories.glasses,
                 "hat": accessories.hat, "facialHair": accessories.facial_hair}[slot](body_type, name)
        for index, part in enumerate(built):
            mesh, tint, kind, colour = part[:4]
            extras = part[4] if len(part) > 4 else None
            texture = "hair_clump" if kind == "strands" else kind
            specular = 2.5 if slot == "glasses" else CLOTH_SPECULAR.get(kind, 1.0)
            parts.append((mesh, material("%s_%d" % (name, index), tint, texture, colour, specular, extras)))
    builder.add_skinned_mesh(name, parts, skin_index)
    return builder.build(), used


from . import animations  # noqa: E402


def _tangents(times, values, loop):
    """Catmull-Rom derivatives (per second); zero at the ends of a one-shot clip."""
    n = len(values)
    size = len(values[0])
    out = []
    for k in range(n):
        if 0 < k < n - 1:
            prev, nxt, dt = values[k - 1], values[k + 1], times[k + 1] - times[k - 1]
        elif loop and n > 2:
            # The first and last keys are the same pose; look across the seam.
            prev, nxt = values[n - 2], values[1]
            dt = (times[n - 1] - times[n - 2]) + (times[1] - times[0])
        else:
            out.append(tuple(0.0 for _ in range(size)))
            continue
        out.append(tuple((nxt[i] - prev[i]) / dt for i in range(size)))
    return out


def _hermite(times, triples, t):
    """The runtime's cubic spline (AvatarClips.cpp) at time t."""
    value = lambda k: triples[k][1]
    if len(times) == 1 or t <= times[0]:
        return value(0)
    if t >= times[-1]:
        return value(len(times) - 1)
    k = max(i for i in range(len(times) - 1) if times[i] <= t)
    td = times[k + 1] - times[k]
    s = (t - times[k]) / td
    s2, s3 = s * s, s * s * s
    v0, b0, a1, v1 = value(k), triples[k][2], triples[k + 1][0], value(k + 1)
    return tuple(v0[i] * (2 * s3 - 3 * s2 + 1) + b0[i] * td * (s3 - 2 * s2 + s) + v1[i] * (-2 * s3 + 3 * s2) +
                 a1[i] * td * (s3 - s2) for i in range(len(v0)))


def _channels(times, keys, loop):
    """Cubic-spline channels for every animated slot of a list of poses at `times`."""
    channels = []
    for slot in range(rig.BONE_COUNT):
        values = [pose.local[slot] for pose in keys]
        if all(max(abs(v[0]), abs(v[1]), abs(v[2])) < 1e-6 for v in values):
            continue
        assert slot not in rig.HELPERS, "helper slots are never animated"
        continuous = [values[0]]
        for v in values[1:]:
            previous = continuous[-1]
            continuous.append(tuple(-c for c in v) if sum(a * b for a, b in zip(v, previous)) < 0 else v)
        if all(max(abs(a - b) for a, b in zip(v, continuous[0])) < 1e-6 for v in continuous):
            channels.append((slot, "rotation", [((0.0,) * 4, continuous[0], (0.0,) * 4)], None))
            continue
        tangents = _tangents(times, continuous, loop)
        channels.append((slot, "rotation", [(t, v, t) for v, t in zip(continuous, tangents)], None))
    roots = [pose.root for pose in keys]
    if any(max(abs(c) for c in r) > 1e-7 for r in roots):
        tangents = _tangents(times, roots, loop)
        channels.append((0, "translation", [(t, v, t) for v, t in zip(roots, tangents)], None))
    return channels


LEGS = [rig.INDEX[n + side] for side in ("Left", "Right") for n in ("Hip", "Knee", "Ankle")]


def _feet_between(keys, times, t):
    """Foot targets at t: each key's lift/forward offsets eased between neighbouring keys."""
    k = max(i for i in range(len(times)) if times[i] <= t + 1e-9)
    if k == len(times) - 1:
        return keys[k].feet
    s = (t - times[k]) / (times[k + 1] - times[k])
    s = s * s * (3.0 - 2.0 * s)
    out = []
    for part in range(2):
        a, b = keys[k].feet[part] or {}, keys[k + 1].feet[part] or {}
        merged = {}
        for side in set(a) | set(b):
            va, vb = a.get(side, (0.0, 0.0, 0.0)), b.get(side, (0.0, 0.0, 0.0))
            merged[side] = tuple(x + (y - x) * s for x, y in zip(va, vb))
        out.append(merged or None)
    return tuple(out)


def _refine_feet(clip, times, keys, channels, step=1.0 / 12.0, tolerance=0.004):
    """Keys are IK-solved only where they were authored; in between, interpolated rotations can let
    a planted foot slide or sink. Where it would move more than `tolerance`, the legs are solved
    again at `step` and their channels replaced by the denser keys."""
    from .posing import Pose
    by_slot = {(slot, path): (tr, tt) for slot, path, tr, tt in channels}
    dense = sorted(set([round(i * step, 6) for i in range(int(clip.duration / step) + 1)] + list(times)))
    if dense[-1] < clip.duration - 1e-9:
        dense.append(clip.duration)
    worst = 0.0
    solved = []
    for t in dense:
        pose = Pose(clip.body)
        for (slot, path), (triples, _) in by_slot.items():
            value = _hermite(times if len(triples) > 1 else [0.0], triples, t)
            if path == "rotation":
                n = math.sqrt(sum(c * c for c in value))
                pose.local[slot] = tuple(c / n for c in value)
            else:
                pose.root = value
        lift, forward = _feet_between(keys, times, t)
        _, pos = pose.world()
        for side, sign in rig.SIDES:
            target = pose.bind[rig.INDEX["Ankle" + side]]
            if lift and side in lift:
                target = tuple(a + b for a, b in zip(target, lift[side]))
            worst = max(worst, math.sqrt(sum((a - b) ** 2 for a, b in zip(pos[rig.INDEX["Ankle" + side]], target))))
        pose.plant_feet(lift, forward)
        solved.append(pose)
    if worst <= tolerance:
        return channels
    kept = [c for c in channels if c[0] not in LEGS or c[1] != "rotation"]
    for slot in LEGS:
        values = [pose.local[slot] for pose in solved]
        continuous = [values[0]]
        for v in values[1:]:
            continuous.append(tuple(-c for c in v) if sum(a * b for a, b in zip(v, continuous[-1])) < 0 else v)
        tangents = _tangents(dense, continuous, clip.loop)
        kept.append((slot, "rotation", [(tg, v, tg) for v, tg in zip(continuous, tangents)], dense))
    return kept


import math  # noqa: E402


def animations_glb():
    clips = animations.build_clips()
    builder = GlbBuilder({"cna": {"asset": "animations", "rig": "cna-avatar-71", "presets": animations.PRESETS}})
    builder.add_rig("male")
    for index, clip in enumerate(clips):
        times = [t for t, _ in clip.keys]
        keys = [pose for _, pose in clip.keys]
        assert times[0] == 0.0 and abs(times[-1] - clip.duration) < 1e-9 and times == sorted(times)
        channels = _refine_feet(clip, times, keys, _channels(times, keys, clip.loop))
        extras = {"cnaPreset": index, "cnaLoop": clip.loop,
                  "cnaExpressions": [[round(e[0], 4)] + list(e[1:]) for e in sorted(clip.expressions)]}
        builder.add_animation(clip.name, times, channels, extras)
    return builder.build()
