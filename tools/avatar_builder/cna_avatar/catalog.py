# SPDX-License-Identifier: MS-PL
"""Assembles catalog v2 assets into GLB files."""
from . import rig, body, textures
from .glb import GlbBuilder


FEATURE_TINTS = {"eyeLeft": ("none", "eyes"), "eyeRight": ("none", "eyes"), "eyebrowLeft": ("hair",),
                 "eyebrowRight": ("hair",), "mouth": ("none",)}


def body_glb(body_type):
    """Body, head and the face-feature decal patches (drawn with the expression's atlas tiles)."""
    from . import face
    skin, head, details = body.build(body_type)
    builder = GlbBuilder({"cna": {"asset": "body", "bodyType": body_type, "rig": "cna-avatar-71",
                                  "authoredHeight": rig.PROPORTIONS[body_type]["height"]}})
    skin_index = builder.add_rig(body_type)
    head_tex = builder.add_image("head", textures.skin_head())
    skin_material = builder.add_material("skin", "skin")
    parts = [(skin, skin_material), (head, builder.add_material("head", "skin", texture=head_tex)), (details, skin_material)]
    for feature, mesh in face.build_decals(body_type).items():
        for layer, tint in enumerate(FEATURE_TINTS[feature]):
            parts.append((mesh, builder.add_material("%s%d" % (feature, layer), tint,
                                                     extras={"cnaFeature": feature, "cnaLayer": layer})))
    builder.add_skinned_mesh("body", parts, skin_index)
    return builder.build()


# (id, slot, name, CreateRandom weight for female, male). Ids 1-101 are catalog v1's items in
# their v2 form (a catalog keeps every earlier id and slot); the rest are new in v2.
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
    (80, "glasses", "glasses_round", 1.0, 1.0), (81, "glasses", "glasses_square", 1.0, 1.0),
    (82, "glasses", "glasses_aviator", 1.0, 1.0), (83, "glasses", "glasses_browline", 1.0, 1.0),
    (100, "hat", "hat_cap", 1.0, 1.0), (101, "hat", "hat_beanie", 1.0, 1.0), (102, "hat", "hat_bucket", 1.0, 1.0),
    (103, "hat", "hat_fedora", 0.8, 1.0), (104, "hat", "hat_headband", 1.0, 0.3),
]
# Personal features a CNA format 2 description can name (facial hair).
FEATURE_ITEMS = [
    (120, "facialHair", "facial_mustache", 0.0, 1.0), (121, "facialHair", "facial_goatee", 0.0, 1.0),
    (122, "facialHair", "facial_beard", 0.0, 1.0), (123, "facialHair", "facial_chinstrap", 0.0, 1.0),
]
# Hats that cover the hair: hair styles get a variant flattened under them.
COVERING_HATS = ("hat_cap", "hat_beanie", "hat_bucket", "hat_fedora")


def item_glb(body_type, item_id, slot, name, under_hat=False):
    """One item fitted to one body; `under_hat` gives a hair style's variant for covering hats."""
    from . import hair, garments, accessories
    builder = GlbBuilder({"cna": {"asset": "item", "id": item_id, "slot": slot, "name": name, "bodyType": body_type,
                                  "rig": "cna-avatar-71", "underHat": under_hat}})
    skin_index = builder.add_rig(body_type)
    parts = []
    if slot == "hair":
        locks, scalp = hair.build(body_type, name)
        if under_hat:
            locks, scalp = hair.under_hat(body_type, locks), hair.under_hat(body_type, scalp)
        if scalp.positions:
            parts.append((scalp, builder.add_material(name + "_scalp", "hair", texture=builder.add_image("scalp", textures.hair_cap()))))
        if locks.positions:
            parts.append((locks, builder.add_material(name, "hair", texture=builder.add_image("strands", textures.hair_clump()))))
    else:
        built = {"top": garments.top, "bottom": garments.bottom, "shoes": garments.shoes, "glasses": accessories.glasses,
                 "hat": accessories.hat, "facialHair": accessories.facial_hair}[slot](body_type, name)
        images = {}
        for index, (mesh, tint, kind, colour) in enumerate(built):
            if kind not in images:
                images[kind] = builder.add_image(kind, textures.hair_clump() if kind == "strands" else textures.cloth(kind))
            parts.append((mesh, builder.add_material("%s_%d" % (name, index), tint, color=colour, texture=images[kind])))
    builder.add_skinned_mesh(name, parts, skin_index)
    return builder.build()


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


def animations_glb():
    clips = animations.build_clips()
    builder = GlbBuilder({"cna": {"asset": "animations", "rig": "cna-avatar-71", "presets": animations.PRESETS}})
    builder.add_rig("male")
    for index, clip in enumerate(clips):
        times = [t for t, _ in clip.keys]
        assert times[0] == 0.0 and abs(times[-1] - clip.duration) < 1e-9 and times == sorted(times)
        channels = []
        for slot in range(rig.BONE_COUNT):
            values = [pose.local[slot] for _, pose in clip.keys]
            if all(max(abs(v[0]), abs(v[1]), abs(v[2])) < 1e-6 for v in values):
                continue
            assert slot not in rig.HELPERS, "helper slots are never animated"
            continuous = [values[0]]
            for v in values[1:]:
                previous = continuous[-1]
                continuous.append(tuple(-c for c in v) if sum(a * b for a, b in zip(v, previous)) < 0 else v)
            if all(max(abs(a - b) for a, b in zip(v, continuous[0])) < 1e-6 for v in continuous):
                channels.append((slot, "rotation", [((0.0,) * 4, continuous[0], (0.0,) * 4)]))
                continue
            tangents = _tangents(times, continuous, clip.loop)
            channels.append((slot, "rotation", [(t, v, t) for v, t in zip(continuous, tangents)]))
        roots = [pose.root for _, pose in clip.keys]
        if any(max(abs(c) for c in r) > 1e-7 for r in roots):
            tangents = _tangents(times, roots, clip.loop)
            channels.append((0, "translation", [(t, v, t) for v, t in zip(roots, tangents)]))
        extras = {"cnaPreset": index, "cnaLoop": clip.loop,
                  "cnaExpressions": [[round(e[0], 4)] + list(e[1:]) for e in sorted(clip.expressions)]}
        builder.add_animation(clip.name, times, channels, extras)
    return builder.build()
