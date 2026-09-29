#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Deterministic avatar review: one job list, rendered by the CPU preview or by the real
AvatarRenderer (cna_avatar_review), then laid out as contact sheets.

    A=modules/gamer-services/assets/avatars
    python3 tools/avatar_builder/avatar_review.py jobs --catalogs $A --version 1 --out jobs.json
    python3 tools/avatar_builder/avatar_review.py preview jobs.json OUT/preview --catalogs $A
    cna_avatar_review jobs.json OUT/renderer                  # the real renderer, same jobs
    python3 tools/avatar_builder/avatar_review.py sheets jobs.json OUT/renderer OUT/sheets

Sheets: `views` (front, three-quarter, profile, back, head, hands, feet of a female and a male
avatar), `diverse` (seeded random avatars), `animations` (key frames of every preset) and
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
from cna_avatar.description import Descriptor  # noqa: E402

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


def head_height(catalogs, library, description):
    """Height to frame the face at: between the Head joint and the top of the head."""
    joint, height = posed_joint(catalogs, library, description, "Stand0", 0.0, "Head")
    return joint[1] + 0.52 * (height - joint[1])


def job(name, sheet, description, eye, target, fov, yaw=0.0, animation="Stand0", time=0.0, expression=None, label=None):
    return {"name": name, "sheet": sheet, "label": label or name, "description": description.hex(),
            "preset": PRESETS.index(animation),
            "camera": {"eye": [round(c, 5) for c in eye], "target": [round(c, 5) for c in target], "fov": fov},
            "yaw": yaw, "animation": animation, "time": round(time, 4), "expression": expression, "light": REVIEW_LIGHT}


def full_body(height):
    return (0.0, height * 0.54, height * 1.85), (0.0, height * 0.5, 0.0), 0.62


def build_jobs(catalogs, version):
    manifest = catalogs.manifest(version)
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
        head = head_height(catalogs, library, data)
        jobs.append(job("views-%s-head" % body, "views", d, (0.16, head + 0.03, 0.84), (0.0, head - 0.01, 0.0), 0.5,
                        label="%s head" % body))
        wrist, _ = posed_joint(catalogs, library, data, "Stand0", 0.0, "WristRight")
        tip, _ = posed_joint(catalogs, library, data, "Stand0", 0.0, "FingerMiddle3Right")
        hand = (wrist + tip) / 2.0
        jobs.append(job("views-%s-hands" % body, "views", d, hand + np.array([-0.06, 0.03, 0.42]), hand, 0.45,
                        label="%s hand" % body))
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
        head = head_height(catalogs, library, d.encode())
        close = ((0.0, head + 0.01, 0.80), (0.0, head - 0.02, 0.0), 0.5)
        names = [c["name"] for c in manifest["faceControls"]["male"]]
        for index, name in enumerate(names):
            for value in (0, 255):
                face = [128] * 16
                face[index] = value
                v = Descriptor(d.body_type, d.height_mm, d.build, version, d.colors, d.items, 0, face)
                jobs.append(job("faces-%02d-%d" % (index, value), "faces", v, *close, yaw=-15.0, label="%s %s" % (name, "-" if value == 0 else "+")))
        for seed in range(16):
            v = seeded(manifest, 100 + seed, version)
            h = head_height(catalogs, library, v.encode())
            jobs.append(job("faces-random-%02d" % seed, "faces", v, (0.0, h + 0.01, 0.80), (0.0, h - 0.02, 0.0), 0.5, yaw=-15.0,
                            label="random face %d" % seed))
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
    head = head_height(catalogs, library, d.encode())
    close = ((0.0, head + 0.01, 0.72), (0.0, head - 0.02, 0.0), 0.42)
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


def cmd_jobs(args):
    catalogs = preview.Catalogs(args.catalogs)
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
    columns = {"views": 7, "diverse": 8, "animations": 8, "expressions": 8, "faces": 8}
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


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("jobs")
    p.add_argument("--catalogs", type=Path, required=True)
    p.add_argument("--version", type=int, required=True)
    p.add_argument("--out", type=Path, required=True)
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
    args = parser.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
