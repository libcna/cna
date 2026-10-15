#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Audit of an avatar catalog's assets and body proportions (numpy).

    python3 tools/avatar_builder/avatar_audit.py modules/gamer-services/assets/avatars --version 3

Prints, per kind: how many entries, triangles, vertices and bytes their models take; per wearable
model: how much of the body under it pokes out through it in the bind pose (skin showing through
a garment or hair); and per body: its proportions in head heights and against its height. Exit
status 1 when a model lets more than --through percent, and more than --vertices vertices, of the
skin under it show through.
"""
import argparse
import sys
from collections import defaultdict
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cna_avatar import preview, rig  # noqa: E402


def surface(glb, skin_only=False):
    """Positions, normals and Head weights of a model's opaque, non-decal parts (a body's skin only)."""
    positions, normals, head = [], [], []
    for prim in glb["primitives"]:
        if prim["feature"] or prim["blend"] or (skin_only and prim["tint"] != "skin"):
            continue
        positions.append(prim["positions"])
        normals.append(prim["normals"])
        head.append(np.where(prim["joints"] == rig.INDEX["Head"], prim["weights"], 0.0).sum(axis=1))
    return np.concatenate(positions), np.concatenate(normals), np.concatenate(head)


def nearest(points, others, chunk=1024):
    """Index of the nearest of `others` for every point, and its distance."""
    index, distance = [], []
    for start in range(0, len(points), chunk):
        d = ((points[start:start + chunk, None, :] - others[None, :, :]) ** 2).sum(axis=2)
        index.append(d.argmin(axis=1))
        distance.append(np.sqrt(d.min(axis=1)))
    return np.concatenate(index), np.concatenate(distance)


def through(model_points, model_normals, body_points, body_normals, radius=0.004, depth=0.0015, chunk=256):
    """Share of the skin a model covers that pokes out through it: along the skin's normal, a
    surface of the model facing the same way lies between `depth` and 8 mm under the skin and none
    lies in front of it. Inner linings and tucked rims face the body, and soles lie centimetres
    under the foot, so neither counts; skin next to an open shoe or under a fringe has nothing of
    the model along its normal and is simply uncovered."""
    covered = poking = 0
    # Returns (share, vertices).
    for start in range(0, len(body_points), chunk):
        b, n = body_points[start:start + chunk], body_normals[start:start + chunk]
        d = model_points[None, :, :] - b[:, None, :]
        along = (d * n[:, None, :]).sum(axis=2)
        radial = np.linalg.norm(d - along[:, :, None] * n[:, None, :], axis=2)
        facing = (model_normals[None, :, :] * n[:, None, :]).sum(axis=2) > 0.3
        near = (radial < radius) & facing
        front = (near & (along >= -depth) & (along < 0.04)).any(axis=1)
        behind = (near & (along < -depth) & (along > -0.008)).any(axis=1)
        covered += int((front | behind).sum())
        poking += int((behind & ~front).sum())
    return (poking / covered if covered else 0.0), poking


def proportions(glb):
    """Measured from the body's skin and its joints (metres and head heights)."""
    joints = preview.world_positions(glb["bind"])
    skin, _, head_weight = surface(glb, skin_only=True)
    height = skin[:, 1].max()
    at = lambda name: joints[rig.INDEX[name]]
    # The head is what the Head joint alone moves; its lowest point in front is the chin.
    face = skin[(head_weight > 0.99) & (skin[:, 2] > 0.03)]
    chin = face[:, 1].min()
    head = height - chin
    head_width = np.ptp(skin[(skin[:, 1] > chin + 0.4 * head) & (skin[:, 1] < chin + 0.8 * head)][:, 0])
    shoulders = np.ptp(skin[(abs(skin[:, 1] - at("ShoulderLeft")[1]) < 0.03)][:, 0])
    arm = np.linalg.norm(at("ShoulderLeft") - at("ElbowLeft")) + np.linalg.norm(at("ElbowLeft") - at("WristLeft"))
    leg = at("HipLeft")[1] - at("AnkleLeft")[1]
    hand = np.linalg.norm(at("WristLeft") - at("FingerMiddle3Left")) + 0.02
    feet = skin[skin[:, 1] < 0.12]
    foot = np.ptp(feet[feet[:, 0] > 0.02][:, 2])
    neck = chin - at("ShoulderLeft")[1]
    return [("height", height, height / head), ("head (chin to crown)", head, 1.0), ("head width", head_width, head_width / head),
            ("shoulder width", shoulders, shoulders / head), ("arm, shoulder to wrist", arm, arm / head),
            ("leg, hip to ankle", leg, leg / head), ("hand", hand, hand / head), ("foot", foot, foot / head),
            ("chin above shoulders", neck, neck / head), ("leg / height", leg / height, None),
            ("arm span / height", (2 * arm + shoulders) / height, None)]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("catalogs", type=Path)
    parser.add_argument("--version", type=int, default=3)
    parser.add_argument("--through", type=float, default=2.0, help="percent of the skin under a model allowed to show through it")
    parser.add_argument("--vertices", type=int, default=30, help="skin vertices allowed to show through a model")
    args = parser.parse_args()
    catalogs = preview.Catalogs(args.catalogs)
    manifest = catalogs.manifest(args.version)
    sizes = {a["name"]: a["size"] for a in manifest["assets"]}
    bodies = {}
    for index, body in enumerate(("female", "male")):
        glb = catalogs.glb(args.version, manifest["bodies"][body]["asset"])
        points, normals, _ = surface(glb, skin_only=True)
        bodies[body] = (glb, points, normals)
        print("%s body: %d triangles" % (body, sum(len(p["indices"]) for p in glb["primitives"])))
        for name, metres, heads in proportions(glb):
            print("  %-24s %6.3f%s" % (name, metres, "" if heads is None else "  (%.2f heads)" % heads))
    budget = defaultdict(lambda: [0, [], [], 0])
    worst = []
    for entry in manifest["items"] + manifest.get("featureItems", []):
        for body in ("female", "male"):
            name = entry["assets"][body]
            glb = catalogs.glb(args.version, name)
            kind = budget[entry["slot"]]
            kind[0] += 1
            kind[1].append(sum(len(p["indices"]) for p in glb["primitives"]))
            kind[2].append(sum(len(p["positions"]) for p in glb["primitives"]))
            kind[3] += sizes[name]
            if entry["slot"] in ("top", "bottom", "shoes", "hair", "hat", "gloves"):
                points, normals, _ = surface(glb)
                share, count = through(points, normals, bodies[body][1], bodies[body][2])
                worst.append((100.0 * share, count, name))
    print("\nkind         models  triangles min/mean/max     vertices mean   bytes")
    for slot, (count, tris, verts, size) in sorted(budget.items()):
        print("%-12s %6d  %6d / %6d / %6d   %8d   %10d" % (slot, count, min(tris), int(np.mean(tris)), max(tris), int(np.mean(verts)), size))
    worst.sort(reverse=True)
    print("\nwhere the body shows through most (bind pose, share of the covered skin poking out):")
    for share, count, name in worst[:12]:
        print("  %5.1f%%  %4d vertices  %s" % (share, count, name))
    # A handful of vertices at a waistband, or at a cap's rim tucked under hanging hair, is not a
    # garment the body shows through.
    failed = [name for share, count, name in worst if share > args.through and count > args.vertices]
    if failed:
        print("\n%d models exceed %.1f%%: %s" % (len(failed), args.through, ", ".join(failed)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
