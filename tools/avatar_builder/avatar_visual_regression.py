#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Avatar v3 visual regression: a small checked-in set of representative avatars and camera views,
drawn by the real AvatarRenderer (cna_avatar_review) and compared with golden images captured on the
canonical renderer, SOFTWARE.

    P=cmake-build-multi/cna_avatar_review
    R=tools/platform/run_gpu_tests_private.sh
    $R --exec python3 tools/avatar_builder/avatar_visual_regression.py check --program $P [--renderer OPENGL33]
    $R --exec python3 tools/avatar_builder/avatar_visual_regression.py update --program $P     # SOFTWARE only
    python3 tools/avatar_builder/avatar_visual_regression.py manifest --program $P             # rewrite manifest.json
    python3 tools/avatar_builder/avatar_visual_regression.py manifest --check --program $P     # is it reproducible?

`regression/manifest.json` is a cna_avatar_review job list generated from AVATARS and VIEWS below
(descriptions as bytes, cameras resolved) and the newest catalog, whose version, revision and
catalog.json SHA-256 it records. `manifest --check` regenerates it and fails unless the result is
byte for byte the checked-in file. `check` first verifies the manifest against its sources (the
catalog it names is the current one, every view of VIEWS is a job of that name, the goldens are
exactly the jobs' and of the manifest's size, it covers REQUIRED), and by default regenerates it
too (numpy; `--no-reproduce` leaves that to a separate `manifest --check`); it fails when the
program fails, when any job is missing, or when a render differs from its golden by more than the
renderer's tolerance: SOFTWARE is compared strictly, a GPU renderer within the differences
rasterizers and drivers legitimately have. A failure writes golden | render | difference sheets
and report.json to --out.

After an intentional art change (a catalog revision), regenerate the manifest if items or framing
changed, run `update` on SOFTWARE, look at every changed golden, and commit them with the change.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
REGRESSION = HERE / "regression"
MANIFEST = REGRESSION / "manifest.json"
GOLDEN = REGRESSION / "golden"
CATALOGS = HERE.parent.parent / "modules" / "gamer-services" / "assets" / "avatars"
CANONICAL = "SOFTWARE"
SIZE = 256
# Tolerances per renderer, from measurement (plans/plan_avatar_acceptance.md AVA-04): mean absolute
# difference (0-255); block, the worst 8x8 block's mean of the per-pixel largest channel difference
# (a ring, a stud, a changed ear); silhouette, intersection over union of the pixels that differ from
# the background; area, the pixels of a solid patch (every pixel of its 3x3 neighbourhood too) that
# differ by more than STRONG -- a moved, missing or recoloured part, never the one-pixel edge
# differences between rasterizers. Measured against SOFTWARE's goldens, the GL renderers, SDL_GPU,
# FNA3D and Mesa llvmpipe stay within mean 0.08, block 1, silhouette 0.9985 and area 0; WEBGPU's
# skinned shading differs on posed frames, up to block 21. Removing a hoop, a ring or a watch,
# swapping an ear shape or shoes, 2 cm of height or a 20-level hair colour change fails on every
# renderer; a thin eyeliner only on SOFTWARE.
STRONG = 48
TOLERANCES = {
    CANONICAL: {"mean": 0.05, "block": 3, "silhouette": 0.9995, "area": 2},
    "WEBGPU": {"mean": 0.75, "block": 28, "silhouette": 0.997, "area": 4},
    "default": {"mean": 0.6, "block": 12, "silhouette": 0.997, "area": 4},
}
# What the manifest must keep covering (the representative matrix of docs/avatars.md, "Visual regression").
REQUIRED = {
    "body:female", "body:male", "height:short", "height:tall", "build:slim", "build:broad", "proportion:compact",
    "proportion:long", "skin:light", "skin:dark", "face:presets>=6", "hair:short", "hair:long", "hair:curly", "hair:tied",
    "facialHair", "glasses", "sunglasses", "headwear", "headwear:over-long-hair", "makeup:eyes", "makeup:lips", "makeup:blush",
    "earrings", "ears:shapes>=3", "wristwear", "rings", "gloves", "outfit", "shoes:open", "random", "expression",
    "animation:arms-raised", "animation:hands", "animation:head-turn", "animation:crouch",
    "view:front", "view:three-quarter", "view:side", "view:back", "view:head", "view:profile", "view:ear", "view:hand",
    "view:feet", "view:waist-side", "view:waist-back",
}

TONES = [(249, 214, 187), (222, 170, 132), (176, 120, 82), (118, 76, 50), (82, 52, 36)]
LIGHT_SKIN, DARK_SKIN = 0, 4
PRESET_STRENGTH = 1.35          # as the editor applies a preset (AvatarEditor.cpp PresetStrength)

# Representative avatars. Items by catalog name; colours RGB; heights in millimetres; build and
# proportion 0-255 (128 as authored); sides 0 both, 1 left, 2 right.
AVATARS = {
    "female-casual": dict(
        body=0, height=1680, skin=1, hair="hair_wavy", hair_colour=(120, 78, 40), eyes=(70, 130, 90), face="Oval",
        outfit="Casual", accessories={"earrings": ("earrings_hoop", 0)}, jewelry=(212, 175, 90),
        makeup={"lips": "lips_tint", "eyeliner": "liner_thin"}, lips=(170, 74, 84)),
    "male-tall-broad": dict(
        body=1, height=2050, build=220, proportion=200, skin=DARK_SKIN, hair="hair_afro", hair_colour=(28, 24, 22),
        eyes=(58, 38, 28), face="Broad", facial_hair="facial_beard", glasses="glasses_thick",
        items={"top": "top_hoodie", "bottom": "bottom_cargo", "shoes": "shoes_boots"},
        colors={"top": (60, 80, 180), "bottom": (120, 90, 60), "shoes": (60, 62, 70)}),
    "female-petite-compact": dict(
        body=0, height=1450, build=60, proportion=20, skin=2, hair="hair_highponytail", hair_colour=(46, 34, 28),
        eyes=(122, 84, 46), face="Heart", hat="hat_beanie", brows="brows_softarch",
        items={"top": "top_cardigan", "bottom": "bottom_midiskirt", "shoes": "shoes_sandals"},
        colors={"top": (230, 120, 170), "bottom": (60, 80, 180), "shoes": (120, 90, 60), "accessory": (240, 150, 40)},
        makeup={"eyeShadow": "shadow_smoky", "blush": "blush_round"}, eye_shadow=(138, 108, 98), blush=(226, 128, 124)),
    "male-average-cap": dict(
        body=1, height=1800, skin=LIGHT_SKIN, hair="hair_short", hair_colour=(160, 118, 74), eyes=(72, 112, 158),
        face="Square", hat="hat_cap", facial_hair="facial_stubble", ears="ears_large",
        items={"top": "top_polo", "bottom": "bottom_chinos", "shoes": "shoes_loafers"},
        colors={"top": (40, 110, 110), "bottom": (240, 240, 236), "shoes": (120, 90, 60), "accessory": (220, 60, 56)},
        accessories={"wristwear": ("wrist_watch", 2), "rings": ("ring_signet", 2)}, jewelry=(200, 200, 196)),
    "female-tall-formal": dict(
        body=0, height=1950, build=100, proportion=220, skin=3, hair="hair_long", hair_colour=(196, 156, 98),
        eyes=(104, 146, 190), face="Long", hat="hat_sunhat", glasses="sunglasses_square", outfit="Formal",
        accessories={"gloves": ("gloves_formal", 0)}, gloves=(36, 32, 30)),
    "male-compact-heavy": dict(
        body=1, height=1600, build=250, proportion=40, skin=1, hair="hair_balding", hair_colour=(142, 138, 134),
        eyes=(118, 126, 138), face="Mature", facial_hair="facial_handlebar", glasses="glasses_round",
        items={"top": "top_sweater", "bottom": "bottom_jeans", "shoes": "shoes_hiking"},
        colors={"top": (130, 80, 170), "bottom": (36, 52, 92), "shoes": (120, 90, 60)}),
    "female-curls-earrings": dict(
        body=0, height=1720, skin=DARK_SKIN, hair="hair_longcurly", hair_colour=(28, 24, 22), eyes=(58, 38, 28),
        face="Round", ears="ears_protruding", outfit="Summer",
        accessories={"earrings": ("earrings_bighoop", 0)}, jewelry=(212, 175, 90),
        makeup={"lips": "lips_bold"}, lips=(150, 40, 60)),
    # A tied tail under a wide brim (plans/plan_avatar_acceptance.md AVA-09: it came through the brim).
    "female-ponytail-sunhat": dict(
        body=0, height=1700, skin=2, hair="hair_highponytail", hair_colour=(150, 70, 40), eyes=(70, 130, 90), face="Soft",
        hat="hat_sunhat", outfit="Smart casual"),
    "male-longlobe-stud": dict(
        body=1, height=1760, skin=2, hair="hair_crop", hair_colour=(36, 28, 24), eyes=(70, 46, 30), face="Angular",
        ears="ears_longlobe", outfit="Sport", accessories={"earrings": ("earrings_stud", 1)}, jewelry=(200, 200, 196)),
}
# Three avatars exactly as AvatarDescription.CreateRandom() makes them, from fixed seeds.
RANDOM_SEED, RANDOM_COUNT = 2026, 3

# (avatar, view, animation, fraction of the clip, expression).
VIEWS = [
    ("female-casual", "front", "Stand0", 0.0, None),
    ("female-casual", "three-quarter", "Stand0", 0.0, None),
    ("female-casual", "head", "Stand0", 0.0, None),
    ("female-casual", "ear", "Stand0", 0.0, None),
    ("female-casual", "three-quarter", "Wave", 0.35, None),
    ("male-tall-broad", "front", "Stand0", 0.0, None),
    ("male-tall-broad", "head", "Stand0", 0.0, None),
    ("male-tall-broad", "profile", "Stand0", 0.0, None),
    ("male-tall-broad", "three-quarter", "Celebrate", 0.5, None),
    ("female-petite-compact", "front", "Stand0", 0.0, None),
    ("female-petite-compact", "head", "Stand0", 0.0, None),
    ("female-petite-compact", "back", "Stand0", 0.0, None),
    ("female-petite-compact", "feet", "Stand0", 0.0, None),
    ("male-average-cap", "three-quarter", "Stand0", 0.0, None),
    ("male-average-cap", "head", "Stand0", 0.0, [4, 4, 4, 4, 4]),
    ("male-average-cap", "hand", "Stand0", 0.0, None),
    ("male-average-cap", "hand", "MaleIdleCheckHand", 0.5, None),
    ("female-tall-formal", "front", "Stand0", 0.0, None),
    ("female-tall-formal", "head", "Stand0", 0.0, None),
    ("female-tall-formal", "hand", "Clap", 0.45, None),
    ("female-casual", "side", "FemaleIdleFixShoe", "crouch", None),
    ("female-casual", "waist-side", "FemaleIdleFixShoe", "crouch", None),
    ("female-casual", "waist-back", "FemaleIdleFixShoe", "crouch", None),
    ("male-compact-heavy", "front", "Stand0", 0.0, None),
    ("male-compact-heavy", "profile", "Stand0", 0.0, None),
    ("male-compact-heavy", "head", "MaleIdleLookAround", 0.3, None),
    ("female-curls-earrings", "ear", "Stand0", 0.0, None),
    ("female-curls-earrings", "three-quarter", "Stand0", 0.0, None),
    ("male-longlobe-stud", "ear", "Stand0", 0.0, None),
    ("female-ponytail-sunhat", "head-back", "Stand0", 0.0, None),
] + [("random-%d" % k, view, "Stand0", 0.0, None) for k in range(RANDOM_COUNT) for view in ("three-quarter", "head")]

ANIMATION_COVERS = {"Wave": "animation:arms-raised", "Celebrate": "animation:arms-raised", "MaleIdleCheckHand": "animation:hands",
                    "Clap": "animation:hands", "MaleIdleLookAround": "animation:head-turn", "FemaleIdleFixShoe": "animation:crouch"}
HAIR_CLASS = {"hair_wavy": "hair:long", "hair_afro": "hair:curly", "hair_highponytail": "hair:tied", "hair_short": "hair:short",
              "hair_long": "hair:long", "hair_balding": "hair:short", "hair_longcurly": "hair:curly", "hair_crop": "hair:short",
}


# ----- the manifest --------------------------------------------------------------------------------

def build_manifest(program):
    import numpy as np
    import avatar_review as review
    from cna_avatar import preview
    from cna_avatar.description import Descriptor, decode, use_catalog
    catalogs = preview.Catalogs(CATALOGS)
    version = max(catalogs.versions())
    manifest = catalogs.manifest(version)
    use_catalog(manifest)
    library = preview.ClipLibrary(catalogs.glb(version, manifest["animations"]["asset"]))
    review.PRESETS[:] = manifest["animations"]["presets"]
    ids = {e["name"]: e["id"] for key in ("items", "featureItems", "faceStyles") for e in manifest[key]}
    names = {value: key for key, value in ids.items()}
    outfits = {o["name"]: o for o in manifest["outfits"]}
    controls = [c["name"] for c in manifest["faceControls"]["male"]]
    presets = {p["name"]: p["values"] for p in manifest["facePresets"] if p["group"] == "face"}

    def shaped(name):
        face = [128] * len(controls)
        for key, value in presets[name].items():
            value = max(-1.0, min(1.0, value * PRESET_STRENGTH))
            face[controls.index(key)] = max(0, min(255, int(round(128 + value * 127))))
        return face

    def describe(spec):
        colors = {"skin": TONES[spec["skin"]], "hair": spec["hair_colour"], "eyes": spec["eyes"], "top": (220, 60, 56),
                  "bottom": (60, 62, 70), "shoes": (240, 240, 236), "accessory": (50, 150, 200)}
        items = {"hair": ids[spec["hair"]], "top": ids["top_tshirt"], "bottom": ids["bottom_jeans"], "shoes": ids["shoes_sneakers"],
                 "glasses": 0, "hat": 0}
        accessories, sides = {}, {}
        if "outfit" in spec:
            outfit = outfits[spec["outfit"]]
            for slot, item in outfit["items"].items():
                if slot in items:
                    items[slot] = item
                elif item:
                    accessories[slot] = item
            colors.update({slot: tuple(rgb) for slot, rgb in outfit.get("colors", {}).items()})
        items.update({slot: ids[name] for slot, name in spec.get("items", {}).items()})
        colors.update(spec.get("colors", {}))
        if "glasses" in spec:
            items["glasses"] = ids[spec["glasses"]]
        if "hat" in spec:
            items["hat"] = ids[spec["hat"]]
        for slot, (name, side) in spec.get("accessories", {}).items():
            accessories[slot], sides[slot] = ids[name], side
        for key, slot in (("lips", "lips"), ("eye_shadow", "eyeShadow"), ("blush", "blush"), ("jewelry", "jewelry"), ("gloves", "gloves")):
            if key in spec:
                colors[slot] = spec[key]
        d = Descriptor(spec["body"], spec["height"], spec.get("build", 128), version, colors, items,
                       ids[spec["facial_hair"]] if "facial_hair" in spec else 0, shaped(spec["face"]),
                       eyebrows=ids[spec["brows"]] if "brows" in spec else 0, ears=ids[spec["ears"]] if "ears" in spec else 0,
                       accessories=accessories, sides=sides,
                       makeup={slot: ids[name] for slot, name in spec.get("makeup", {}).items()},
                       proportion=spec.get("proportion", 128))
        return d

    def covers(spec, d):
        authored = manifest["bodies"][("female", "male")[d.body_type]]["authoredHeightMillimeters"]
        out = {"body:" + ("female", "male")[d.body_type], "face:" + spec["face"]}
        out |= {"height:short"} if d.height_mm <= authored - 150 else {"height:tall"} if d.height_mm >= authored + 150 else set()
        out |= {"build:slim"} if d.build <= 80 else {"build:broad"} if d.build >= 200 else set()
        out |= {"proportion:compact"} if d.proportion <= 60 else {"proportion:long"} if d.proportion >= 196 else set()
        out |= {"skin:light"} if spec["skin"] == LIGHT_SKIN else {"skin:dark"} if spec["skin"] == DARK_SKIN else set()
        out.add(HAIR_CLASS[spec["hair"]])
        if d.facial_hair:
            out.add("facialHair")
        if d.items["glasses"]:
            out.add("sunglasses" if names[d.items["glasses"]].startswith("sunglasses_") else "glasses")
        if d.items["hat"]:
            out.add("headwear")
            if HAIR_CLASS[spec["hair"]] in ("hair:long", "hair:tied", "hair:curly"):
                out.add("headwear:over-long-hair")
        for slot, value in d.accessories.items():
            if value:
                out.add(slot)
        out |= {{"eyeShadow": "makeup:eyes", "eyeliner": "makeup:eyes", "lips": "makeup:lips", "blush": "makeup:blush"}[slot]
                for slot, value in d.makeup.items() if value}
        if d.ears:
            out.add("ears:" + spec["ears"])
        if "outfit" in spec:
            out.add("outfit")
        if d.items["shoes"] == ids["shoes_sandals"]:
            out.add("shoes:open")
        return out

    avatars = {name: (spec, describe(spec)) for name, spec in AVATARS.items()}
    randoms = subprocess.run([str(program), "--random", str(RANDOM_COUNT), "--seed", str(RANDOM_SEED)], check=True,
                             capture_output=True, text=True).stdout.split()
    if len(randoms) != RANDOM_COUNT:
        sys.exit("%s --random %d --seed %d printed %d descriptions" % (program, RANDOM_COUNT, RANDOM_SEED, len(randoms)))
    for k, hexed in enumerate(randoms):
        avatars["random-%d" % k] = (None, hexed)

    def crouch_time(clip):
        duration = library.clips[clip]["duration"]
        times = np.linspace(0.0, duration, 241)
        return float(times[int(np.argmin([library.sample(clip, t)[1][1] for t in times]))])

    jobs = []
    for name, view, animation, fraction, expression in VIEWS:
        spec, d = avatars[name]
        hexed = d if spec is None else d.hex()
        data = bytes.fromhex(hexed)
        body = decode(data)
        h = body.height_mm / 1000.0
        duration = library.clips[animation]["duration"]
        time = crouch_time(animation) if fraction == "crouch" else duration * fraction
        head, span = review.head_height(catalogs, library, data)
        k = span / review.REFERENCE_SPAN
        yaw = {"front": 0.0, "three-quarter": -35.0, "side": -90.0, "back": 180.0, "head": -30.0, "profile": -90.0,
               "head-back": 150.0}.get(view, 0.0)
        if view in ("front", "three-quarter", "side", "back"):
            eye, target, fov = review.full_body(h)
        elif view in ("head", "profile", "head-back"):
            # Framed as the portraits sheet frames a head, and moved with it where the clip carries it.
            c = review.head_centre(catalogs, library, data, yaw)
            moved, _ = review.posed_joint(catalogs, library, data, animation, time, "Head", yaw)
            rest, _ = review.posed_joint(catalogs, library, data, "Stand0", 0.0, "Head", yaw)
            c, lift = c + (moved - rest), (moved - rest)[1]
            eye, target, fov = (c[0], head + lift, c[2] + 1.85 * k), (c[0], head + lift - 0.04 * k, c[2]), 0.28
        elif view == "ear":
            side = "earRight" if spec and spec.get("accessories", {}).get("earrings", ("", 0))[1] == 2 else "earLeft"
            yaw = -90.0 if side == "earLeft" else 90.0
            anchor = np.array(manifest["faceAnchors"][("female", "male")[body.body_type]][side])
            point = review.posed_point(catalogs, library, data, anchor + np.array([0.0, 0.02, 0.03]), yaw)
            eye, target, fov = point + np.array([0.0, 0.0, 0.5 * 0.26 / np.tan(0.14)]), point, 0.28
        elif view == "hand":
            wrist, _ = review.posed_joint(catalogs, library, data, animation, time, "WristRight")
            tip, _ = review.posed_joint(catalogs, library, data, animation, time, "FingerMiddle3Right")
            hand = (wrist + tip) / 2.0
            eye, target, fov = hand + np.array([-0.40, 0.05, 0.12]), hand, 0.42
        elif view == "feet":
            ankle, _ = review.posed_joint(catalogs, library, data, "Stand0", 0.0, "AnkleLeft")
            feet = np.array([0.0, 0.05, ankle[2] + 0.06])
            eye, target, fov = feet + np.array([0.30, 0.34, 0.62]), feet, 0.55
        elif view.startswith("waist"):
            # Where a top's hem meets the waistband: from the side and from behind.
            yaw = -90.0 if view == "waist-side" else 145.0
            hips = [review.posed_joint(catalogs, library, data, animation, time, joint, yaw)[0] for joint in ("HipLeft", "HipRight")]
            waist = (hips[0] + hips[1]) / 2.0 + np.array([0.0, 0.06, 0.0])
            eye, target, fov = waist + np.array([0.0, 0.08, 1.10]), waist, 0.45
        else:
            raise ValueError(view)
        label = "%s %s" % (name, view) + ("" if animation == "Stand0" else " %s %.2fs" % (animation, time))
        # Named by what it shows, so adding a job renames no golden.
        stable = "%s-%s" % (name, view) + ("" if animation == "Stand0" else "-" + animation.lower()) + ("-expression" if expression else "")
        if any(j["name"] == stable for j in jobs):
            raise ValueError("two jobs named " + stable)
        j = review.job(stable, "regression", body, eye, target, fov, yaw, animation, time, expression, label)
        j["description"] = hexed
        tags = {"view:" + view} | (covers(spec, d) if spec else {"random"})
        if animation in ANIMATION_COVERS:
            tags.add(ANIMATION_COVERS[animation])
        if expression:
            tags.add("expression")
        j["covers"] = sorted(tags)
        jobs.append(j)
    return {"size": SIZE, "background": review.BACKGROUND, "catalogVersion": version, "catalogRevision": manifest["revision"],
            "catalogSha256": catalog_digest(version), "goldenRenderer": CANONICAL, "jobs": jobs}


def catalog_digest(version):
    return hashlib.sha256((CATALOGS / ("v%d" % version) / "catalog.json").read_bytes()).hexdigest()


def stale(data):
    """How the checked-in manifest and goldens disagree with their sources (empty when they agree)."""
    import avatar_review as review
    from PIL import Image
    problems = []
    versions = sorted(int(p.name[1:]) for p in CATALOGS.glob("v*") if (p / "catalog.json").is_file())
    newest = versions[-1] if versions else None
    catalog = json.loads((CATALOGS / ("v%d" % newest) / "catalog.json").read_text()) if newest else {}
    made_for = (data.get("catalogVersion"), data.get("catalogRevision"), data.get("catalogSha256"))
    current = (newest, catalog.get("revision"), catalog_digest(newest) if newest else None)
    if made_for != current:
        problems.append("the manifest was made for catalog v%s revision %s (catalog.json %s), the checkout has v%s revision %s (%s): "
                        "regenerate it (manifest), then update and review the goldens" % (
                            made_for[0], made_for[1], str(made_for[2])[:12], current[0], current[1], str(current[2])[:12]))
    if (data.get("size"), data.get("background"), data.get("goldenRenderer")) != (SIZE, review.BACKGROUND, CANONICAL):
        problems.append("the manifest's size, background or golden renderer (%s, %s, %s) is not this script's (%s, %s, %s)" % (
            data.get("size"), data.get("background"), data.get("goldenRenderer"), SIZE, review.BACKGROUND, CANONICAL))
    jobs = [j["name"] for j in data["jobs"]]
    if len(set(jobs)) != len(jobs):
        problems.append("the manifest names a job twice: %s" % sorted({n for n in jobs if jobs.count(n) > 1}))
    if len(jobs) != len(VIEWS):
        problems.append("the manifest has %d jobs, VIEWS %d views" % (len(jobs), len(VIEWS)))
    for j in data["jobs"]:
        if not j.get("covers") or not j.get("description"):
            problems.append("%s states no coverage or no description" % j["name"])
        elif newest and j["description"][16:20] != newest.to_bytes(2, "little").hex():
            # Bytes 8-9 of a description name its catalog version (little-endian).
            problems.append("%s describes an avatar of another catalog version" % j["name"])
    goldens = {g.stem for g in GOLDEN.glob("*.png")}
    for name in sorted(goldens - set(jobs)):
        problems.append("golden/%s.png belongs to no job (a renamed or removed view)" % name)
    for name in sorted(set(jobs) - goldens):
        problems.append("%s has no golden" % name)
    for name in sorted(goldens & set(jobs)):
        with Image.open(GOLDEN / (name + ".png")) as image:
            if image.size != (SIZE, SIZE):
                problems.append("golden/%s.png is %dx%d, the manifest's size %d" % (name, image.width, image.height, SIZE))
    missing = coverage(data["jobs"])
    if missing:
        problems.append("the manifest no longer covers: " + ", ".join(sorted(missing)))
    return problems


def reproduced(program):
    """Differences between the checked-in manifest and the one AVATARS, VIEWS and the catalog generate."""
    data = build_manifest(program)
    text = json.dumps(data, indent=1) + "\n"
    if text == MANIFEST.read_text():
        return []
    old = json.loads(MANIFEST.read_text())
    changed = [key for key in data if key != "jobs" and data[key] != old.get(key)]
    jobs = {j["name"]: j for j in old["jobs"]}
    changed += ["job %s" % j["name"] for j in data["jobs"] if jobs.get(j["name"]) != j]
    changed += ["job %s (gone)" % name for name in jobs if name not in {j["name"] for j in data["jobs"]}]
    return ["regenerating the manifest does not reproduce the checked-in file: %s" % (", ".join(changed[:12]) or "formatting")]


def coverage(jobs):
    """REQUIRED entries the jobs do not cover."""
    tags = set().union(*(set(j.get("covers", [])) for j in jobs))
    missing = set()
    for need in REQUIRED:
        if ">=" in need:
            prefix, count = need.split(">=")
            group = prefix.split(":")[0]
            if len({t for t in tags if t.startswith(group + ":")}) < int(count):
                missing.add(need)
        elif need not in tags:
            missing.add(need)
    return missing


# ----- rendering and comparison --------------------------------------------------------------------

def render(program, renderer, manifest_path, out):
    """Runs cna_avatar_review: (exit status, log, the renderer it reports drawing with)."""
    env = dict(os.environ)
    if renderer:
        env["CNA_GRAPHICS_RENDERER"] = renderer
    env.setdefault("SDL_AUDIODRIVER", "dummy")
    out.mkdir(parents=True, exist_ok=True)
    result = subprocess.run([str(program), str(manifest_path), str(out)], env=env, capture_output=True, text=True, timeout=1800)
    log = result.stdout + result.stderr
    (out / "cna_avatar_review.log").write_text(log)
    drew = re.search(r"graphics renderer: ([A-Z0-9_]+)", log)
    return result.returncode, log, drew.group(1) if drew else None


def load(path):
    from PIL import Image
    return Image.open(path).convert("RGB")


def peak(a, b):
    """Per pixel, the largest channel difference between two images."""
    from PIL import ImageChops
    r, g, b_ = ImageChops.difference(a, b).split()
    return ImageChops.lighter(ImageChops.lighter(r, g), b_)


def metrics(golden, image, background):
    from PIL import Image, ImageChops, ImageFilter, ImageStat
    mean = sum(ImageStat.Stat(ImageChops.difference(golden, image)).mean) / 3.0
    # A patch, not an edge: the strongly differing pixels whose whole 3x3 neighbourhood differs too.
    area = peak(golden, image).point(lambda v: 255 if v > STRONG else 0).filter(ImageFilter.MinFilter(3)).histogram()[255]
    # A small part changed (a ring, a liner, a stud): the worst 8x8 block's mean difference.
    block = peak(golden, image).reduce(8).getextrema()[1]
    back = Image.new("RGB", golden.size, tuple(background))
    a, b = (peak(i, back).point(lambda v: 255 if v > 12 else 0) for i in (golden, image))
    both, either = ImageChops.darker(a, b).histogram()[255], ImageChops.lighter(a, b).histogram()[255]
    return {"mean": round(mean, 4), "block": int(block), "silhouette": round(both / either if either else 1.0, 5), "area": int(area)}


def sheet(golden, image, path):
    """golden | render | difference (four times as bright)."""
    from PIL import Image
    heat = peak(golden, image).point(lambda v: min(255, v * 4))
    black = Image.new("L", golden.size, 0)
    out = Image.new("RGB", (golden.width * 3, golden.height))
    for index, picture in enumerate((golden, image, Image.merge("RGB", (heat, black, black)))):
        out.paste(picture, (golden.width * index, 0))
    out.save(path)


def cmd_manifest(args):
    if args.check:
        problems = reproduced(args.program) + stale(json.loads(MANIFEST.read_text()))
        for problem in problems:
            print("  - " + problem)
        print("manifest %s" % ("FAILED" if problems else "reproduced byte for byte, consistent with the catalog and the goldens"))
        return 1 if problems else 0
    data = build_manifest(args.program)
    missing = coverage(data["jobs"])
    if missing:
        sys.exit("the manifest would not cover: " + ", ".join(sorted(missing)))
    MANIFEST.parent.mkdir(parents=True, exist_ok=True)
    MANIFEST.write_text(json.dumps(data, indent=1) + "\n")
    print("%d jobs -> %s" % (len(data["jobs"]), MANIFEST))


def cmd_update(args):
    problems = stale(json.loads(MANIFEST.read_text()))
    if any(p.startswith("the manifest") for p in problems):
        sys.exit("regenerate the manifest before capturing goldens:\n  " + "\n  ".join(problems))
    out = Path(args.out)
    code, log, drew = render(args.program, args.renderer or CANONICAL, MANIFEST, out)
    if code != 0:
        sys.exit("cna_avatar_review failed (exit %d):\n%s" % (code, log[-2000:]))
    if drew != CANONICAL:
        sys.exit("goldens are captured on %s; cna_avatar_review drew with %s" % (CANONICAL, drew))
    renderer = drew
    GOLDEN.mkdir(parents=True, exist_ok=True)
    from PIL import Image
    data = json.loads(MANIFEST.read_text())
    names = {j["name"] for j in data["jobs"]}
    for stale in GOLDEN.glob("*.png"):
        if stale.stem not in names:
            stale.unlink()
    for j in data["jobs"]:
        Image.open(out / (j["name"] + ".png")).convert("RGB").save(GOLDEN / (j["name"] + ".png"), optimize=True)
    print("%d goldens written to %s from %s" % (len(names), GOLDEN, renderer))


def cmd_check(args):
    data = json.loads(MANIFEST.read_text())
    out = Path(args.out) / (args.renderer or "default")
    failures = stale(data) + ([] if args.no_reproduce else reproduced(args.program))
    code, log, renderer = render(args.program, args.renderer, MANIFEST, out)
    if code != 0:
        failures.append("cna_avatar_review exited %d: %s" % (code, " | ".join(l for l in log.splitlines() if "[FAIL]" in l)[:600]))
    if renderer is None or (args.renderer and renderer != args.renderer):
        failures.append("cna_avatar_review did not report drawing with %s" % (args.renderer or "a renderer"))
    tolerance = TOLERANCES.get(renderer, TOLERANCES["default"])
    background = data["background"][:3]
    report, worst = [], {"mean": 0.0, "block": 0, "silhouette": 1.0, "area": 0}
    for j in data["jobs"]:
        golden, image = GOLDEN / (j["name"] + ".png"), out / (j["name"] + ".png")
        if not golden.is_file() or not image.is_file():
            failures.append("%s: %s is missing" % (j["name"], "the golden" if not golden.is_file() else "the render"))
            continue
        g, i = load(golden), load(image)
        if g.size != i.size:
            failures.append("%s: %s render against a %s golden" % (j["name"], i.size, g.size))
            continue
        m = metrics(g, i, background)
        worst = {"mean": max(worst["mean"], m["mean"]), "block": max(worst["block"], m["block"]),
                 "silhouette": min(worst["silhouette"], m["silhouette"]), "area": max(worst["area"], m["area"])}
        bad = [key for key in ("mean", "block", "area") if m[key] > tolerance[key]] + (
            ["silhouette"] if m["silhouette"] < tolerance["silhouette"] else [])
        report.append({"job": j["name"], "label": j["label"], **m, "failed": bad})
        if bad:
            (out / "failures").mkdir(exist_ok=True)
            sheet(g, i, out / "failures" / (j["name"] + ".png"))
            failures.append("%s (%s): %s" % (j["name"], j["label"], ", ".join("%s %s" % (key, m[key]) for key in bad)))
    (out / "report.json").write_text(json.dumps({"renderer": renderer, "tolerance": tolerance, "worst": worst, "jobs": report,
                                                 "failures": failures}, indent=1) + "\n")
    print("avatar visual regression on %s: %d/%d jobs compared, worst mean %.3f/255, block %d, silhouette %.4f, area %d px "
          "(tolerance %s)" % (renderer, len(report), len(data["jobs"]), worst["mean"], worst["block"], worst["silhouette"], worst["area"],
                              tolerance))
    if failures:
        print("FAILED (%d); renders, report.json and failure sheets in %s" % (len(failures), out))
        for failure in failures:
            print("  - " + failure)
        return 1
    print("PASSED")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    for name, fn in (("check", cmd_check), ("update", cmd_update), ("manifest", cmd_manifest)):
        p = sub.add_parser(name)
        p.add_argument("--program", type=Path, required=True, help="cna_avatar_review")
        if name != "manifest":
            p.add_argument("--renderer", help="CNA_GRAPHICS_RENDERER for a multi-renderer build (default: its default)")
            p.add_argument("--out", default="avatar-visual-regression", help="where renders, report and failure sheets go")
        if name == "check":
            p.add_argument("--no-reproduce", action="store_true", help="do not regenerate the manifest (a separate manifest --check does)")
        if name == "manifest":
            p.add_argument("--check", action="store_true", help="regenerate and compare with the checked-in manifest; write nothing")
        p.set_defaults(fn=fn)
    args = parser.parse_args()
    sys.exit(args.fn(args) or 0)


if __name__ == "__main__":
    main()
