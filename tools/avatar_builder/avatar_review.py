#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Deterministic avatar review: one job list, rendered by the CPU preview or by the real
AvatarRenderer (cna_avatar_review), then laid out as contact sheets.

    A=modules/gamer-services/assets/avatars
    python3 tools/avatar_builder/avatar_review.py jobs --catalogs $A --version 3 --out jobs.json
    python3 tools/avatar_builder/avatar_review.py preview jobs.json OUT/preview --catalogs $A
    cna_avatar_review jobs.json OUT/renderer                  # the real renderer, same jobs
    python3 tools/avatar_builder/avatar_review.py sheets jobs.json OUT/renderer OUT/sheets
    python3 tools/avatar_builder/avatar_review.py compare jobs.json OUT/OPENGL33 OUT/SOFTWARE   # renderer vs renderer

Sheets: `views` (front, three-quarter, profile, back, head, hand from the front and the back, feet of a female and a male
avatar), `diverse` (seeded random avatars), `faces` (every face control at both ends), `hair` and `hairback`,
`wardrobe`, `headwear`, `facial`, `ears`, `accessories`, `makeup`, `animations` (key frames of every preset) and
`expressions` (every eye, eyebrow and mouth state, and independent left/right states).
"""
import argparse
import json
import random
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from cna_avatar import preview, rig  # noqa: E402
from cna_avatar.description import Descriptor, use_catalog  # noqa: E402

SKIN = [(255, 224, 196), (241, 194, 160), (224, 172, 128), (198, 134, 90), (160, 104, 68), (120, 78, 52),
        (92, 58, 40), (255, 210, 180)]
HAIR = [(36, 28, 24), (74, 48, 30), (120, 78, 40), (176, 122, 62), (226, 188, 116), (160, 60, 36), (200, 200, 196),
        (60, 64, 120)]
EYES = [(70, 46, 30), (110, 72, 40), (60, 110, 160), (70, 130, 90), (120, 120, 130), (40, 40, 44)]
CLOTH = [(220, 60, 56), (240, 150, 40), (250, 210, 60), (90, 180, 80), (50, 150, 200), (60, 80, 180), (130, 80, 170),
         (230, 120, 170), (240, 240, 236), (60, 62, 70), (120, 90, 60), (40, 110, 110)]
REVIEW_LIGHT = {"direction": [0.35, -0.45, -0.82], "color": [0.72, 0.70, 0.66], "ambient": [0.36, 0.38, 0.42]}
BACKGROUND = [54, 60, 72]
KEY_FRACTIONS = (0.0, 0.3, 0.55, 0.8)


def items_by_slot(manifest):
    out = {}
    for item in manifest["items"]:
        out.setdefault(item["slot"], []).append(item["id"])
    return out


def hero(manifest, body_type, version):
    slots = items_by_slot(manifest)
    pick = lambda slot, k: slots[slot][k % len(slots[slot])]
    if body_type == 0:
        items = {"hair": pick("hair", 2), "top": pick("top", 0), "bottom": pick("bottom", 0),
                 "shoes": pick("shoes", 0), "glasses": 0, "hat": 0}
        colors = {"skin": SKIN[1], "hair": HAIR[2], "eyes": EYES[3], "top": CLOTH[7], "bottom": CLOTH[5],
                  "shoes": CLOTH[8], "accessory": CLOTH[0]}
        return Descriptor(0, 1680, 128, version, colors, items)
    items = {"hair": pick("hair", 0), "top": pick("top", 2), "bottom": pick("bottom", 0), "shoes": pick("shoes", 0),
             "glasses": 0, "hat": 0}
    colors = {"skin": SKIN[3], "hair": HAIR[0], "eyes": EYES[0], "top": CLOTH[4], "bottom": CLOTH[9],
              "shoes": CLOTH[8], "accessory": CLOTH[0]}
    return Descriptor(1, 1800, 128, version, colors, items)


def seeded(manifest, seed, version):
    r = random.Random(seed)
    body = r.randrange(2)
    name = "male" if body else "female"
    authored = manifest["bodies"][name]["authoredHeightMillimeters"]

    def pick(slot, pool="items"):
        entries = [i for i in manifest.get(pool, []) if i["slot"] == slot and i.get("random", {}).get(name, 1.0) > 0]
        return r.choices([i["id"] for i in entries], [i.get("random", {}).get(name, 1.0) for i in entries])[0]
    items = {slot: pick(slot) for slot in ("hair", "top", "bottom", "shoes")}
    items["glasses"] = pick("glasses") if r.random() < 0.3 else 0
    items["hat"] = pick("hat") if r.random() < 0.3 else 0
    colors = {"skin": r.choice(SKIN), "hair": r.choice(HAIR), "eyes": r.choice(EYES)}
    for slot in ("top", "bottom", "shoes", "accessory"):
        colors[slot] = r.choice(CLOTH)
    facial, face = 0, None
    if "faceControls" in manifest:
        face = [max(0, min(255, 128 + int(round((r.random() + r.random() - 1.0) * 120)))) for _ in range(16)]
        if body and r.random() < 0.35:
            facial = pick("facialHair", "featureItems")
    return Descriptor(body, r.randint(authored - 110, authored + 110), r.randint(72, 184), version, colors, items, facial, face)


def posed_joint(catalogs, library, description, animation, time, name, yaw=0.0):
    model = preview.assemble(catalogs, description)
    rotations, root, _ = library.sample(animation, time)
    skins = preview.skin_matrices(model, rotations, root, library.bind)
    p = np.append(model["bind_positions"][rig.INDEX[name]], 1.0)
    return (preview.yaw_matrix(yaw) @ skins[rig.INDEX[name]] @ p)[:3], model["height"]


PRESETS = []


def head_centre(catalogs, library, description, yaw=0.0):
    """Where the posed head's centre is seen from a camera on +z with the avatar turned by yaw:
    close-ups aim there, so a pose that carries the head forward does not push it out of frame."""
    model = preview.assemble(catalogs, description)
    rotations, root, _ = library.sample("Stand0", 0.0)
    skins = preview.skin_matrices(model, rotations, root, library.bind)
    head = rig.INDEX["Head"]
    bind = model["bind_positions"][head]
    # The head's centre sits above its joint by the same share of the head in every catalog.
    centre = np.array([0.0, bind[1] + 0.46 * (model["height"] - bind[1]), 0.0, 1.0])
    return (preview.yaw_matrix(yaw) @ skins[head] @ centre)[:3]


def head_height(catalogs, library, description):
    """(height to frame the face at, the head's span from its joint to the top): close-ups keep
    their distance in proportion to the head, so heads of every size are framed alike."""
    joint, height = posed_joint(catalogs, library, description, "Stand0", 0.0, "Head")
    return joint[1] + 0.52 * (height - joint[1]), height - joint[1]


# The close-ups' reference head span (Head joint to the top of a 1.80 m male), measured on an
# earlier development catalog and kept so that framing does not change.
REFERENCE_SPAN = 0.36


def job(name, sheet, description, eye, target, fov, yaw=0.0, animation="Stand0", time=0.0, expression=None, label=None):
    return {"name": name, "sheet": sheet, "label": label or name, "description": description.hex(),
            "preset": PRESETS.index(animation),
            "camera": {"eye": [round(c, 5) for c in eye], "target": [round(c, 5) for c in target], "fov": fov},
            "yaw": yaw, "animation": animation, "time": round(time, 4), "expression": expression, "light": REVIEW_LIGHT}


def full_body(height):
    return (0.0, height * 0.54, height * 1.85), (0.0, height * 0.5, 0.0), 0.62


def build_jobs(catalogs, version):
    manifest = catalogs.manifest(version)
    use_catalog(manifest)
    library = preview.ClipLibrary(catalogs.glb(version, manifest["animations"]["asset"]))
    PRESETS[:] = manifest["animations"]["presets"]
    jobs = []
    heroes = [hero(manifest, 0, version), hero(manifest, 1, version)]
    for d in heroes:
        body = "female" if d.body_type == 0 else "male"
        data = d.encode()
        h = d.height_mm / 1000.0
        eye, target, fov = full_body(h)
        for view, yaw in (("front", 0.0), ("three-quarter", -35.0), ("profile", -90.0), ("back", 180.0)):
            jobs.append(job("views-%s-%s" % (body, view), "views", d, eye, target, fov, yaw, label="%s %s" % (body, view)))
        head, span = head_height(catalogs, library, data)
        k = span / REFERENCE_SPAN
        jobs.append(job("views-%s-head" % body, "views", d, (0.16 * k, head + 0.03 * k, 0.84 * k), (0.0, head - 0.01 * k, 0.0), 0.5,
                        label="%s head" % body))
        wrist, _ = posed_joint(catalogs, library, data, "Stand0", 0.0, "WristRight")
        tip, _ = posed_joint(catalogs, library, data, "Stand0", 0.0, "FingerMiddle3Right")
        hand = (wrist + tip) / 2.0
        jobs.append(job("views-%s-hands" % body, "views", d, hand + np.array([-0.06, 0.03, 0.42]), hand, 0.45,
                        label="%s hand" % body))
        # The back of the (right) hand, from outside: where the fingers show.
        jobs.append(job("views-%s-hands-back" % body, "views", d, hand + np.array([-0.40, 0.05, 0.12]), hand, 0.42,
                        label="%s hand, back" % body))
        ankle, _ = posed_joint(catalogs, library, data, "Stand0", 0.0, "AnkleLeft")
        feet = np.array([0.0, 0.05, ankle[2] + 0.06])
        jobs.append(job("views-%s-feet" % body, "views", d, feet + np.array([0.30, 0.34, 0.62]), feet, 0.55,
                        label="%s feet" % body))
    for seed in range(24):
        d = seeded(manifest, seed, version)
        eye, target, fov = full_body(1.9)
        jobs.append(job("diverse-%02d" % seed, "diverse", d, eye, target, fov, -20.0, label="seed %d" % seed))
    if "faceControls" in manifest:
        d = heroes[1]
        head, span = head_height(catalogs, library, d.encode())
        k = span / REFERENCE_SPAN
        close = ((0.0, head - 0.02 * k, 0.86 * k), (0.0, head - 0.05 * k, 0.0), 0.5)
        names = [c["name"] for c in manifest["faceControls"]["male"]]
        for index, name in enumerate(names):
            for value in (0, 255):
                face = [128] * len(names)
                face[index] = value
                v = Descriptor(d.body_type, d.height_mm, d.build, version, d.colors, d.items, 0, face)
                jobs.append(job("faces-%02d-%d" % (index, value), "faces", v, *close, yaw=-15.0, label="%s %s" % (name, "-" if value == 0 else "+")))
        for seed in range(16):
            v = seeded(manifest, 100 + seed, version)
            h, span = head_height(catalogs, library, v.encode())
            k = span / REFERENCE_SPAN
            jobs.append(job("faces-random-%02d" % seed, "faces", v, (0.0, h - 0.02 * k, 0.86 * k), (0.0, h - 0.05 * k, 0.0), 0.5, yaw=-15.0,
                            label="random face %d" % seed))
    # Every catalog entry on the avatar it suits best: hairstyles and facial hair as head close-ups,
    # wardrobe items full length, accessories close to where they are worn.
    jobs += catalog_jobs(catalogs, library, manifest, version, heroes)
    jobs += quality_jobs(catalogs, library, manifest, version)
    presets = manifest["animations"]["presets"]
    for index, preset in enumerate(presets):
        d = heroes[0 if preset.startswith("Female") else 1] if preset.startswith(("Female", "Male")) else heroes[index % 2]
        duration = library.clips[preset]["duration"]
        h = d.height_mm / 1000.0
        eye, target, fov = full_body(h)
        for k, fraction in enumerate(KEY_FRACTIONS):
            jobs.append(job("animations-%02d-%d" % (index, k), "animations", d, eye, target, fov, -20.0, preset,
                            duration * fraction, label="%s %.2fs" % (preset, duration * fraction)))
    d = heroes[1]
    head, span = head_height(catalogs, library, d.encode())
    k = span / REFERENCE_SPAN
    close = ((0.0, head + 0.01 * k, 0.72 * k), (0.0, head - 0.02 * k, 0.0), 0.42)
    for state, name in enumerate(preview.MOUTHS):
        jobs.append(job("expressions-mouth-%02d" % state, "expressions", d, *close, expression=[state, 0, 0, 0, 0],
                        label="mouth %s" % name))
    for state, name in enumerate(preview.EYES):
        jobs.append(job("expressions-eyes-%02d" % state, "expressions", d, *close, expression=[0, state, state, 0, 0],
                        label="eyes %s" % name))
    for state, name in enumerate(preview.EYEBROWS):
        jobs.append(job("expressions-brows-%02d" % state, "expressions", d, *close, expression=[0, 0, 0, state, state],
                        label="brows %s" % name))
    for index, (expression, label) in enumerate((
            ([0, 13, 0, 0, 0], "wink (left eye blink)"), ([0, 0, 13, 0, 0], "wink (right eye blink)"),
            ([0, 0, 0, 4, 0], "left brow raised"), ([0, 0, 0, 0, 4], "right brow raised"),
            ([3, 3, 3, 3, 3], "confused"), ([4, 4, 4, 4, 4], "laughing"), ([5, 5, 5, 4, 4], "shocked"),
            ([1, 1, 1, 1, 1], "sad"), ([2, 2, 2, 2, 2], "angry"), ([6, 6, 6, 0, 0], "happy"),
            ([7, 7, 7, 0, 0], "yawning"), ([0, 8, 8, 0, 0], "sleeping"))):
        jobs.append(job("expressions-combo-%02d" % index, "expressions", d, *close, expression=expression, label=label))
    return {"size": 320, "background": BACKGROUND, "catalogVersion": version, "jobs": jobs}


def catalog_jobs(catalogs, library, manifest, version, heroes):
    jobs = []
    fmt = {e["id"]: e.get("format", 1) for key in ("items", "featureItems") for e in manifest.get(key, [])}

    def variant(d, **changes):
        v = Descriptor(d.body_type, d.height_mm, d.build, version, dict(d.colors), dict(d.items), d.facial_hair, list(d.face),
                       item_formats=fmt)
        for key, value in changes.items():
            if key in v.items:
                v.items[key] = value
            else:
                setattr(v, key, value)
        return v
    for item in manifest["items"]:
        slot = item["slot"]
        body = 1 if item["random"]["male"] > item["random"]["female"] else 0
        d = variant(heroes[body], **{slot: item["id"]})
        head, span = head_height(catalogs, library, d.encode())
        k = span / REFERENCE_SPAN
        h = d.height_mm / 1000.0
        if slot in ("hair", "hat", "glasses"):
            sheet = "hair" if slot == "hair" else "headwear"
            jobs.append(job("%s-%03d" % (sheet, item["id"]), sheet, d, (0.30 * k, head + 0.02 * k, 0.88 * k), (0.0, head - 0.05 * k, 0.0),
                            0.55, yaw=-20.0, label=item["name"]))
            if slot == "hair":
                # And from behind, where gaps between hair and scalp show.
                jobs.append(job("hairback-%03d" % item["id"], "hairback", d, (0.30 * k, head + 0.02 * k, 0.88 * k),
                                (0.0, head - 0.05 * k, 0.0), 0.55, yaw=150.0, label=item["name"]))
        elif slot in ("top", "bottom", "shoes"):
            eye, target, fov = full_body(h)
            jobs.append(job("wardrobe-%03d" % item["id"], "wardrobe", d, eye, target, fov, -25.0, label=item["name"]))
    for item in manifest["items"]:
        slot = item["slot"]
        if slot not in ("earrings", "wristwear", "gloves", "rings"):
            continue
        body = 1 if item["random"]["male"] > item["random"]["female"] else 0
        d = variant(heroes[body], accessories={slot: item["id"]})
        data = d.encode()
        if slot == "earrings":
            head, span = head_height(catalogs, library, data)
            k = span / REFERENCE_SPAN
            jobs.append(job("accessories-%03d" % item["id"], "accessories", d, (0.62 * k, head - 0.06 * k, 0.30 * k),
                            (0.0, head - 0.08 * k, 0.0), 0.5, label=item["name"]))
        else:
            wrist, _ = posed_joint(catalogs, library, data, "Stand0", 0.0, "WristRight")
            tip, _ = posed_joint(catalogs, library, data, "Stand0", 0.0, "FingerMiddle3Right")
            # A ring sits on the finger: framed lower and closer than a watch or a glove.
            ring = slot == "rings"
            hand = wrist * 0.45 + tip * 0.55 if ring else wrist * 0.85 + tip * 0.15
            jobs.append(job("accessories-%03d" % item["id"], "accessories", d, hand + np.array([-0.30, 0.06, 0.30]) * (0.6 if ring else 1.0),
                            hand, 0.45, label=item["name"]))
    makeup_colors = {"eyeShadow": (150, 96, 140), "eyeliner": (0, 0, 0), "blush": (228, 120, 120), "lips": (190, 50, 70)}
    for item in manifest.get("faceStyles", []):
        slot = item["slot"]
        body = 0 if slot != "eyebrows" or item["random"]["female"] >= item["random"]["male"] else 1
        # Hair pulled back from the face, so brows and makeup show.
        pulled = {"hair": next((i["id"] for i in manifest["items"] if i["name"] == "hair_ponytail"), heroes[body].items["hair"])}
        if slot == "eyebrows":
            d = variant(heroes[body], eyebrows=item["id"], **pulled)
        else:
            d = variant(heroes[body], makeup={slot: item["id"]}, **pulled)
            if slot in ("eyeShadow", "blush", "lips"):
                d.colors[slot] = makeup_colors[slot]
        head, span = head_height(catalogs, library, d.encode())
        k = span / REFERENCE_SPAN
        jobs.append(job("makeup-%03d" % item["id"], "makeup", d, (0.0, head - 0.03 * k, 0.62 * k), (0.0, head - 0.06 * k, 0.0), 0.5,
                        yaw=-10.0, label=item["name"]))
    for body in (0, 1):
        # Every ear shape in profile, hair pulled back so the ear shows.
        pulled = {"hair": next((i["id"] for i in manifest["items"] if i["name"] == "hair_ponytail"), heroes[body].items["hair"])}
        for shape in (i for i in manifest.get("featureItems", []) if i["slot"] == "ears"):
            d = variant(heroes[body], ears=shape["id"], **pulled)
            _, span = head_height(catalogs, library, d.encode())
            k = span / REFERENCE_SPAN
            ear = manifest["faceAnchors"][("female", "male")[body]]["earLeft"][1]
            jobs.append(job("ears-%d-%03d" % (body, shape["id"]), "ears", d, (0.0, ear, 0.70 * k), (0.0, ear - 0.01 * k, 0.0),
                            0.5, yaw=-90.0, label="%s %s" % (shape["label"], ("female", "male")[body])))
    for item in manifest.get("featureItems", []):
        if item["slot"] != "facialHair":
            continue
        d = variant(heroes[1], facial_hair=item["id"])
        head, span = head_height(catalogs, library, d.encode())
        k = span / REFERENCE_SPAN
        jobs.append(job("facial-%03d" % item["id"], "facial", d, (0.30 * k, head - 0.02 * k, 0.86 * k), (0.0, head - 0.06 * k, 0.0),
                        0.5, yaw=-20.0, label=item["name"]))
    return jobs


PRESET_STRENGTH = 1.35
TONES = [(249, 214, 187), (222, 170, 132), (176, 120, 82), (118, 76, 50), (82, 52, 36)]
# Portrait avatars: (body, skin, hair colour, eye colour, hair, face preset, extras) -- a spread of
# bodies, skin tones, hair lengths and textures, facial hair, glasses, headwear, makeup and earrings.
PORTRAITS = [
    (0, 0, (120, 78, 40), (70, 130, 90), "hair_wavy", "Oval", {}),
    (1, 2, (36, 28, 24), (70, 46, 30), "hair_short", "Balanced", {}),
    (0, 4, (28, 24, 22), (58, 38, 28), "hair_longcurly", "Heart",
     {"accessories": {"earrings": 512}, "makeup": {"lips": 671, "eyeliner": 650}, "lips": (156, 66, 74)}),
    (1, 3, (28, 24, 22), (58, 38, 28), "hair_fade", "Square", {"facial_hair": 482}),
    (1, 0, (160, 118, 74), (72, 112, 158), "hair_sidepart", "Long", {"glasses": 81, "facial_hair": 480}),
    (0, 1, (70, 48, 34), (88, 56, 34), "hair_bobbangs", "Round", {"glasses": 80, "makeup": {"blush": 660}, "blush": (226, 128, 124)}),
    (1, 1, (98, 66, 42), (94, 122, 72), "hair_shag", "Angular", {"hat": 100}),
    (0, 2, (196, 156, 98), (104, 146, 190), "hair_long", "Youthful", {"hat": 101}),
    (1, 4, (28, 24, 22), (44, 42, 46), "hair_afro", "Broad", {"facial_hair": 122}),
    (0, 3, (46, 34, 28), (122, 84, 46), "hair_highponytail", "Angular",
     {"makeup": {"eyeShadow": 640, "eyeliner": 651}, "eyeShadow": (138, 108, 98), "accessories": {"earrings": 510}}),
    (1, 2, (142, 138, 134), (118, 126, 138), "hair_balding", "Mature", {"facial_hair": 122, "glasses": 83}),
    (0, 0, (146, 66, 38), (94, 122, 72), "hair_bun", "Soft", {}),
]
# Head views: (label, yaw, camera height offset in head spans, distance factor).
HEAD_VIEWS = (("front", 0.0, 0.0), ("3/4", -35.0, 0.0), ("profile", -90.0, 0.0), ("above", -25.0, 0.55), ("below", -20.0, -0.45))


def quality_jobs(catalogs, library, manifest, version):
    """The art review: portraits from five angles, every face preset on both bodies, and body
    builds full length from three sides."""
    jobs = []
    names = {i["name"]: i["id"] for i in manifest["items"]}
    controls = [c["name"] for c in manifest["faceControls"]["male"]]
    presets = {(p["group"], p["name"]): p["values"] for p in manifest.get("facePresets", [])}
    fmt = {e["id"]: e.get("format", 1) for key in ("items", "featureItems", "faceStyles") for e in manifest.get(key, [])}

    def shaped(values):
        face = [128] * len(controls)
        for key, value in values.items():
            if key in controls:
                # As the editor applies a preset (AvatarEditor.cpp PresetStrength).
                value = max(-1.0, min(1.0, value * PRESET_STRENGTH))
                face[controls.index(key)] = max(0, min(255, int(round(128 + value * 127))))
        return face

    def avatar(body, skin, hair_colour, eyes, hair, preset, extras, top=None, bottom=None, group="face"):
        authored = manifest["bodies"][("female", "male")[body]]["authoredHeightMillimeters"]
        colors = {"skin": TONES[skin], "hair": hair_colour, "eyes": eyes, "top": CLOTH[(skin * 3 + body) % len(CLOTH)],
                  "bottom": CLOTH[9 if body else 5], "shoes": CLOTH[8], "accessory": CLOTH[0]}
        for key in ("lips", "eyeShadow", "blush"):
            if key in extras:
                colors[key] = extras[key]
        if "accessories" in extras:
            colors["jewelry"] = (212, 175, 90)
        items = {"hair": names[hair], "top": top or names["top_tshirt"], "bottom": bottom or names["bottom_jeans"],
                 "shoes": names["shoes_sneakers"], "glasses": extras.get("glasses", 0), "hat": extras.get("hat", 0)}
        d = Descriptor(body, authored, 128, version, colors, items, extras.get("facial_hair", 0),
                       shaped(presets.get((group, preset), {})), accessories=extras.get("accessories"),
                       makeup=extras.get("makeup"), item_formats=fmt)
        return d
    for index, spec in enumerate(PORTRAITS):
        d = avatar(*spec)
        head, span = head_height(catalogs, library, d.encode())
        k = span / REFERENCE_SPAN
        for label, yaw, lift in HEAD_VIEWS:
            c = head_centre(catalogs, library, d.encode(), yaw)
            jobs.append(job("portraits-%02d-%s" % (index, label.replace("/", "")), "portraits", d,
                            (c[0], head + lift * 0.72 * k, c[2] + 1.85 * k), (c[0], head - 0.04 * k, c[2]), 0.28, yaw=yaw,
                            label="%s %s %s" % (spec[5], spec[4][5:], label)))
    for body in (0, 1):
        for (group, name), values in presets.items():
            if group != "face":
                continue
            d = avatar(body, 1 + body, (70, 48, 34), (88, 56, 34), "hair_ponytail" if body == 0 else "hair_crew", name, {})
            head, span = head_height(catalogs, library, d.encode())
            k = span / REFERENCE_SPAN
            for label, yaw in (("front", 0.0), ("3/4", -30.0), ("profile", -90.0)):
                c = head_centre(catalogs, library, d.encode(), yaw)
                jobs.append(job("presets-%d-%s-%s" % (body, name.lower(), label.replace("/", "")), "presets", d,
                                (c[0], head, c[2] + 1.85 * k), (c[0], head - 0.04 * k, c[2]), 0.28, yaw=yaw,
                                label="%s %s %s" % (("female", "male")[body], name, label)))
    for body in (0, 1):
        authored = manifest["bodies"][("female", "male")[body]]["authoredHeightMillimeters"]
        for label, height, build, proportion in (("petite slim", -0.6, -0.6, -0.3), ("average", 0.0, 0.0, 0.0),
                                                 ("tall athletic", 0.6, 0.3, 0.4), ("heavy", 0.0, 0.95, 0.0),
                                                 ("youthful", -0.75, -0.2, -0.9)):
            d = avatar(body, 2, (70, 48, 34), (88, 56, 34), "hair_lob" if body == 0 else "hair_crop", "Balanced", {},
                       top=names["top_longsleeve"], bottom=names["bottom_chinos"])
            d.height_mm = int(round(authored + height * 110))
            d.build = max(0, min(255, int(round(128 + build * 127))))
            d.proportion = max(0, min(255, int(round(128 + proportion * 127))))
            h = d.height_mm / 1000.0
            eye, target, fov = full_body(1.9)
            for view, yaw in (("front", 0.0), ("3/4", -35.0), ("side", -90.0)):
                jobs.append(job("bodies-%d-%s-%s" % (body, label.replace(" ", ""), view.replace("/", "")), "bodies", d, eye, target,
                                fov, yaw, label="%s %s %s" % (("female", "male")[body], label, view)))
    return jobs + focus_jobs(catalogs, library, manifest, version, avatar)


def posed_point(catalogs, library, description, point, yaw=0.0, animation="Stand0", time=0.0, bone="Head"):
    """Where a bind-pose model point carried by `bone` is in a posed avatar turned by yaw."""
    model = preview.assemble(catalogs, description)
    rotations, root, _ = library.sample(animation, time)
    skins = preview.skin_matrices(model, rotations, root, library.bind)
    return (preview.yaw_matrix(yaw) @ skins[rig.INDEX[bone]] @ np.append(np.asarray(point, dtype=float), 1.0))[:3]


# Focus avatars: (body, skin, hair colour, eye colour, hair, face preset, extras) -- light, medium and dark
# skin on both bodies, with and without makeup, a beard and glasses.
FOCUS = [
    (0, 0, (120, 78, 40), (70, 130, 90), "hair_ponytail", "Balanced", {}),
    (1, 1, (74, 48, 30), (70, 46, 30), "hair_crew", "Balanced", {}),
    (0, 2, (36, 28, 24), (88, 56, 34), "hair_bun", "Heart",
     {"makeup": {"lips": 671, "eyeliner": 650, "eyeShadow": 640}, "lips": (166, 70, 80), "eyeShadow": (138, 108, 98)}),
    (1, 3, (28, 24, 22), (58, 38, 28), "hair_fade", "Square", {"facial_hair": 482}),
    (0, 4, (28, 24, 22), (58, 38, 28), "hair_highponytail", "Oval", {}),
    (1, 0, (160, 118, 74), (72, 112, 158), "hair_sidepart", "Long", {"glasses": 81}),
]


def focus_jobs(catalogs, library, manifest, version, avatar):
    """Close focus sheets for the face's details: `eyes` and `mouth` (front, three-quarter, profile, above,
    below), `temples` (every hairstyle beside the ear, wearing hoops), `profiles` (both sides, head and
    shoulders), `hands` (close, posed and gloved) and `presetgroups` (the eye, brow, nose, mouth and ear
    presets)."""
    jobs = []
    names = {i["name"]: i["id"] for i in manifest["items"]}
    views = (("front", 0.0, 0.0), ("3/4", -35.0, 0.0), ("profile", -90.0, 0.0), ("above", -25.0, 0.45), ("below", -20.0, -0.4))
    for index, spec in enumerate(FOCUS):
        d = avatar(*spec)
        data = d.encode()
        body = ("female", "male")[spec[0]]
        eyes = [np.array(manifest["faceAnchors"][body][k]) for k in ("eyeLeft", "eyeRight")]
        mid = (eyes[0] + eyes[1]) / 2.0
        mouth = mid + np.array([0.0, -0.071 * (1.08 if spec[0] == 0 else 1.12), 0.004])
        for sheet, centre, span in (("eyes", mid + np.array([0.0, 0.012, 0.0]), 0.18), ("mouth", mouth, 0.11)):
            for label, yaw, lift in views:
                # A three-quarter or profile view looks at the eye toward the camera.
                if sheet == "eyes" and yaw <= -60.0:
                    posed = max((posed_point(catalogs, library, data, e + np.array([0.0, 0.012, 0.0]), yaw) for e in eyes),
                                key=lambda q: q[2])
                else:
                    posed = posed_point(catalogs, library, data, centre, yaw)
                distance = 0.5 * span / np.tan(0.14)
                eye = posed + np.array([0.0, lift * span, distance])
                jobs.append(job("%s-%d-%s" % (sheet, index, label.replace("/", "")), sheet, d, eye, posed, 0.28, yaw=yaw,
                                label="%s %s %s" % (body, spec[5], label)))
    # Every hairstyle beside the ear (the side it suits), with hoops: temples, sideburns, the hairline
    # over the ear and what of the ear and the earring the hair lets show.
    for item in (i for i in manifest["items"] if i["slot"] == "hair"):
        b = 1 if item["random"]["male"] > item["random"]["female"] else 0
        d = avatar(b, 1, (120, 78, 40), (70, 130, 90), item["name"], "Balanced", {"accessories": {"earrings": 512}})
        data = d.encode()
        body = ("female", "male")[b]
        ear_point = np.array(manifest["faceAnchors"][body]["earLeft"])
        for label, yaw in (("front-side", -50.0), ("side", -90.0), ("back-side", -130.0)):
            posed = posed_point(catalogs, library, data, ear_point + np.array([0.0, 0.02, 0.03]), yaw)
            span = 0.26
            jobs.append(job("temples-%03d-%s" % (item["id"], label), "temples", d, posed + np.array([0.0, 0.0, 0.5 * span / np.tan(0.14)]),
                            posed, 0.28, yaw=yaw, label="%s %s" % (item["name"][5:], label)))
    # Both profiles, head and shoulders.
    for index, spec in enumerate(PORTRAITS[:8]):
        d = avatar(*spec)
        data = d.encode()
        head, span = head_height(catalogs, library, data)
        k = span / REFERENCE_SPAN
        for label, yaw in (("left", -90.0), ("right", 90.0)):
            c = head_centre(catalogs, library, data, yaw)
            jobs.append(job("profiles-%02d-%s" % (index, label), "profiles", d, (c[0], head - 0.06 * k, c[2] + 2.3 * k),
                            (c[0], head - 0.10 * k, c[2]), 0.28, yaw=yaw, label="%s %s %s" % (spec[5], spec[4][5:], label)))
    # Hands: close from the palm side and the back, standing, swinging, waving and clapping; gloves and rings.
    hand_cases = []
    for b in (0, 1):
        for skin in (0, 3):
            hand_cases.append((b, skin, {}, "bare"))
    hand_cases += [(0, 2, {"accessories": {"gloves": 550}}, "knit"), (1, 1, {"accessories": {"gloves": 551}}, "fingerless"),
                   (1, 4, {"accessories": {"gloves": 552}}, "work"), (0, 1, {"accessories": {"gloves": 553}}, "formal"),
                   (0, 4, {"accessories": {"rings": 562}}, "gem ring"), (1, 2, {"accessories": {"rings": 561}}, "signet")]
    poses = (("palm", "Stand0", 0.0, np.array([-0.06, 0.03, 0.42])), ("back", "Stand0", 0.0, np.array([-0.40, 0.05, 0.12])),
             ("side", "Stand0", 0.0, np.array([0.0, 0.02, -0.42])))
    for index, (b, skin, extras, label) in enumerate(hand_cases):
        d = avatar(b, skin, (70, 48, 34), (88, 56, 34), "hair_ponytail" if b == 0 else "hair_crew", "Balanced", extras)
        data = d.encode()
        for view, animation, time, offset in poses:
            wrist, _ = posed_joint(catalogs, library, data, animation, time, "WristRight")
            tip, _ = posed_joint(catalogs, library, data, animation, time, "FingerMiddle3Right")
            hand = (wrist + tip) / 2.0
            jobs.append(job("hands-%02d-%s" % (index, view), "hands", d, hand + offset, hand, 0.45, animation=animation, time=time,
                            label="%s %s %s" % (("female", "male")[b], label, view)))
    library_clips = library.clips
    for index, (animation, fraction) in enumerate((("Wave", 0.35), ("Clap", 0.45), ("Celebrate", 0.5), ("Stand3", 0.5))):
        b = index % 2
        d = avatar(b, 2, (70, 48, 34), (88, 56, 34), "hair_ponytail" if b == 0 else "hair_crew", "Balanced", {})
        data = d.encode()
        time = library_clips[animation]["duration"] * fraction
        wrist, _ = posed_joint(catalogs, library, data, animation, time, "WristRight")
        tip, _ = posed_joint(catalogs, library, data, animation, time, "FingerMiddle3Right")
        hand = (wrist + tip) / 2.0
        jobs.append(job("hands-anim-%d" % index, "hands", d, hand + np.array([-0.10, 0.04, 0.50]), hand, 0.5, animation=animation,
                        time=time, label="%s %s" % (("female", "male")[b], animation)))
    # The eye, brow, nose, mouth and ear presets on both bodies.
    presets = [(p["group"], p["name"]) for p in manifest.get("facePresets", []) if p["group"] in ("eyes", "brows", "nose", "mouth", "ears")]
    for b in (0, 1):
        for group, name in presets:
            d = avatar(b, 1 + b, (70, 48, 34), (88, 56, 34), "hair_ponytail" if b == 0 else "hair_crew", name, {}, group=group)
            data = d.encode()
            head, span = head_height(catalogs, library, data)
            k = span / REFERENCE_SPAN
            yaw = -90.0 if group == "ears" else (-35.0 if group == "nose" else -15.0)
            c = head_centre(catalogs, library, data, yaw)
            jobs.append(job("presetgroups-%d-%s-%s" % (b, group, name.lower().replace(" ", "")), "presetgroups", d,
                            (c[0], head - 0.02 * k, c[2] + 1.3 * k), (c[0], head - 0.05 * k, c[2]), 0.28, yaw=yaw,
                            label="%s %s %s" % (("female", "male")[b], group, name)))
    return jobs


def random_jobs(catalogs, version, descriptions):
    """Review jobs of CreateRandom() avatars (cna_avatar_review --random): each full length and
    its face close."""
    manifest = catalogs.manifest(version)
    use_catalog(manifest)
    library = preview.ClipLibrary(catalogs.glb(version, manifest["animations"]["asset"]))
    PRESETS[:] = manifest["animations"]["presets"]
    jobs = []
    for index, hexed in enumerate(descriptions):
        data = bytes.fromhex(hexed)
        d = preview.decode(data)
        eye, target, fov = full_body(1.9)
        jobs.append({**job("random-%03d" % index, "random", d, eye, target, fov, -20.0, label="random %d" % index),
                     "description": hexed})
        head, span = head_height(catalogs, library, data)
        k = span / REFERENCE_SPAN
        c = head_centre(catalogs, library, data, -25.0)
        jobs.append({**job("randomface-%03d" % index, "randomface", d, (c[0], head, c[2] + 1.85 * k), (c[0], head - 0.04 * k, c[2]),
                           0.28, yaw=-25.0, label="random %d" % index), "description": hexed})
    return jobs


def cmd_jobs(args):
    catalogs = preview.Catalogs(args.catalogs)
    if args.random:
        descriptions = [line.strip() for line in args.random.read_text().split() if line.strip()]
        data = {"size": 320, "background": BACKGROUND, "catalogVersion": args.version,
                "jobs": random_jobs(catalogs, args.version, descriptions)}
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(data, indent=1) + "\n")
        print("%d jobs -> %s" % (len(data["jobs"]), args.out))
        return
    data = build_jobs(catalogs, args.version)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(data, indent=1) + "\n")
    print("%d jobs -> %s" % (len(data["jobs"]), args.out))


def cmd_preview(args):
    catalogs = preview.Catalogs(args.catalogs)
    data = json.loads(args.jobs.read_text())
    version = data["catalogVersion"]
    manifest = catalogs.manifest(max(catalogs.versions()))
    library = preview.ClipLibrary(catalogs.glb(max(catalogs.versions()), manifest["animations"]["asset"]))
    args.out.mkdir(parents=True, exist_ok=True)
    selected = [j for j in data["jobs"] if not args.only or j["sheet"] in args.only]
    for index, j in enumerate(selected):
        image = preview.render_job(catalogs, library, j, data["size"], data["background"], args.supersample)
        image.save(args.out / (j["name"] + ".png"))
        if index % 20 == 0:
            print("%d/%d %s" % (index + 1, len(selected), j["name"]), flush=True)
    print("rendered %d jobs of catalog v%d" % (len(selected), version))


def cmd_sheets(args):
    from PIL import Image, ImageDraw
    data = json.loads(args.jobs.read_text())
    args.out.mkdir(parents=True, exist_ok=True)
    columns = {"views": 8, "diverse": 8, "animations": 8, "expressions": 8, "faces": 8, "hair": 8, "hairback": 8, "headwear": 8,
               "wardrobe": 8, "facial": 8, "accessories": 8, "makeup": 8, "portraits": 10, "presets": 9, "bodies": 6, "random": 10, "randomface": 10,
               "eyes": 10, "mouth": 10, "temples": 9, "profiles": 8, "hands": 9, "presetgroups": 10}
    cell = args.cell
    sheets = {}
    for j in data["jobs"]:
        sheets.setdefault(j["sheet"], []).append(j)
    for sheet, jobs in sheets.items():
        cols = columns.get(sheet, 8)
        rows = (len(jobs) + cols - 1) // cols
        image = Image.new("RGB", (cols * cell, rows * (cell + 16) + 24), (24, 26, 30))
        draw = ImageDraw.Draw(image)
        draw.text((6, 5), "%s -- %s -- catalog v%d" % (sheet, args.title or args.renders.name, data["catalogVersion"]),
                  fill=(230, 230, 230))
        for index, j in enumerate(jobs):
            x, y = (index % cols) * cell, 24 + (index // cols) * (cell + 16)
            path = args.renders / (j["name"] + ".png")
            if path.is_file():
                image.paste(Image.open(path).convert("RGB").resize((cell, cell), Image.LANCZOS), (x, y))
            else:
                draw.text((x + 6, y + cell // 2), "missing", fill=(255, 80, 80))
            draw.text((x + 4, y + cell + 2), j["label"][:40], fill=(200, 200, 200))
        image.save(args.out / ("%s.png" % sheet))
        print("sheet %s: %d cells" % (sheet, len(jobs)))


def cmd_compare(args):
    """Each job's renders against the reference renderer's: the mean absolute difference (0-255)
    and how much of the avatar's silhouette both agree on (pixels that differ from the background)."""
    from PIL import Image
    data = json.loads(args.jobs.read_text())
    background = np.array(data["background"][:3], dtype=np.float32)
    worst_diff, worst_overlap, compared, missing = (-1.0, ""), (2.0, ""), 0, []
    sheets = {}
    for j in data["jobs"]:
        a, b = args.reference / (j["name"] + ".png"), args.renders / (j["name"] + ".png")
        if not a.is_file() or not b.is_file():
            missing.append(j["name"])
            continue
        x = np.asarray(Image.open(a).convert("RGB"), dtype=np.float32)
        y = np.asarray(Image.open(b).convert("RGB"), dtype=np.float32)
        diff = float(np.abs(x - y).mean())
        sx, sy = np.abs(x - background).max(axis=2) > 12, np.abs(y - background).max(axis=2) > 12
        overlap = float((sx & sy).sum()) / max(1.0, float((sx | sy).sum()))
        worst_diff = max(worst_diff, (diff, j["name"]))
        worst_overlap = min(worst_overlap, (overlap, j["name"]))
        sheet = sheets.setdefault(j["sheet"], [0.0, 1.0])
        sheet[0], sheet[1] = max(sheet[0], diff), min(sheet[1], overlap)
        compared += 1
    for sheet, (diff, overlap) in sorted(sheets.items()):
        print("%-12s worst mean difference %.2f/255, worst silhouette overlap %.4f" % (sheet, diff, overlap))
    print("%d jobs compared, %d missing; worst %.2f/255 (%s), worst overlap %.4f (%s)" %
          (compared, len(missing), worst_diff[0], worst_diff[1], worst_overlap[0], worst_overlap[1]))
    return 1 if missing else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("jobs")
    p.add_argument("--catalogs", type=Path, required=True)
    p.add_argument("--version", type=int, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--random", type=Path, help="jobs for these CreateRandom() descriptions (cna_avatar_review --random N)")
    p.set_defaults(fn=cmd_jobs)
    p = sub.add_parser("preview")
    p.add_argument("jobs", type=Path)
    p.add_argument("out", type=Path)
    p.add_argument("--catalogs", type=Path, required=True)
    p.add_argument("--supersample", type=int, default=2)
    p.add_argument("--only", nargs="*")
    p.set_defaults(fn=cmd_preview)
    p = sub.add_parser("sheets")
    p.add_argument("jobs", type=Path)
    p.add_argument("renders", type=Path)
    p.add_argument("out", type=Path)
    p.add_argument("--cell", type=int, default=200)
    p.add_argument("--title")
    p.set_defaults(fn=cmd_sheets)
    p = sub.add_parser("compare")
    p.add_argument("jobs", type=Path)
    p.add_argument("reference", type=Path)
    p.add_argument("renders", type=Path)
    p.set_defaults(fn=cmd_compare)
    args = parser.parse_args()
    sys.exit(args.fn(args) or 0)


if __name__ == "__main__":
    main()
