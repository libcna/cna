# SPDX-License-Identifier: MS-PL
"""Assembles catalog assets into GLB files."""
from . import rig, body, png
from .glb import GlbBuilder


def head_texture():
    """Skin-tint multiplier for the head: white with soft cheek blush (u=0.5 is the face)."""
    w, h = 128, 64
    pixels = bytearray()
    for y in range(h):
        v = (y + 0.5) / h
        for x in range(w):
            u = (x + 0.5) / w
            blush = 0.0
            for cu in (0.5 - 0.085, 0.5 + 0.085):
                d = ((u - cu) / 0.035) ** 2 + ((v - 0.585) / 0.05) ** 2
                blush = max(blush, max(0.0, 1.0 - d) ** 2)
            pixels += bytes((255, int(round(255 - 30 * blush)), int(round(255 - 34 * blush)), 255))
    return png.encode_rgba(w, h, pixels)


FEATURE_TINTS = {"eyeLeft": ("none", "eyes"), "eyeRight": ("none", "eyes"), "eyebrowLeft": ("hair",),
                 "eyebrowRight": ("hair",), "mouth": ("none",)}


def body_glb(body_type):
    """Body, head and the face-feature decal patches (drawn with the expression's atlas tiles)."""
    from . import face
    skin, head = body.build(body_type)
    builder = GlbBuilder({"cna": {"asset": "body", "bodyType": body_type, "rig": "cna-avatar-71",
                                  "authoredHeight": rig.PROPORTIONS[body_type]["height"]}})
    skin_index = builder.add_rig(body_type)
    head_tex = builder.add_image("head", head_texture())
    parts = [(skin, builder.add_material("skin", "skin")), (head, builder.add_material("head", "skin", texture=head_tex))]
    for feature, mesh in face.build_decals(body_type).items():
        for layer, tint in enumerate(FEATURE_TINTS[feature]):
            parts.append((mesh, builder.add_material("%s%d" % (feature, layer), tint,
                                                     extras={"cnaFeature": feature, "cnaLayer": layer})))
    builder.add_skinned_mesh("body", parts, skin_index)
    return builder.build()


from . import wardrobe  # noqa: E402

ITEMS = [  # (id, slot, name) -- must match catalogItems() in AvatarDescriptionCodec.cpp
    (1, "hair", "hair_short"), (2, "hair", "hair_spiky"), (3, "hair", "hair_bob"), (4, "hair", "hair_ponytail"),
    (5, "hair", "hair_buzz"),
    (20, "top", "top_tshirt"), (21, "top", "top_longsleeve"), (22, "top", "top_hoodie"), (23, "top", "top_tank"),
    (40, "bottom", "bottom_jeans"), (41, "bottom", "bottom_shorts"), (42, "bottom", "bottom_skirt"),
    (60, "shoes", "shoes_sneakers"), (61, "shoes", "shoes_boots"),
    (80, "glasses", "glasses_round"), (81, "glasses", "glasses_square"),
    (100, "hat", "hat_cap"), (101, "hat", "hat_beanie"),
]
TINT_FOR_SLOT = {"hair": "hair", "top": "top", "bottom": "bottom", "shoes": "shoes", "glasses": "accessory",
                 "hat": "accessory"}


def item_glb(body_type, item_id, slot, name):
    builder = GlbBuilder({"cna": {"asset": "item", "id": item_id, "slot": slot, "name": name, "bodyType": body_type,
                                  "rig": "cna-avatar-71"}})
    skin_index = builder.add_rig(body_type)
    parts = []
    if slot == "shoes":
        upper, sole = wardrobe.shoes(body_type, name)
        parts.append((upper, builder.add_material(name, "shoes")))
        parts.append((sole, builder.add_material(name + "_sole", "none", color=(0.93, 0.93, 0.9))))
    else:
        mesh = {"hair": wardrobe.hair, "top": wardrobe.top, "bottom": wardrobe.bottom, "glasses": wardrobe.glasses,
                "hat": wardrobe.hat}[slot](body_type, name)
        parts.append((mesh, builder.add_material(name, TINT_FOR_SLOT[slot])))
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
