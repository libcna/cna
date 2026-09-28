#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Development preview of generated avatar GLBs (needs Pillow): CPU skinning + painter's raster.

    python3 tools/avatar_builder/preview_avatar.py OUT.png BODY.glb [ITEM.glb ...] [--pose ANIM.glb NAME TIME]
"""
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from PIL import Image, ImageDraw  # noqa: E402
from cna_avatar import glbread, rig  # noqa: E402

TINTS = {"skin": (224, 172, 128), "hair": (74, 48, 30), "top": (50, 150, 200), "bottom": (60, 80, 180),
         "shoes": (240, 240, 236), "accessory": (220, 60, 56), "eyes": (60, 110, 160), "none": (255, 255, 255)}


def mat_mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def quat_matrix(q, t):
    x, y, z, w = q
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), t[0]],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), t[1]],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y), t[2]], [0, 0, 0, 1]]


def sample(gltf, blob, name, time):
    """Local rotations (and root translation) of animation `name` at `time` (cubic spline)."""
    anim = next(a for a in gltf["animations"] if a["name"] == name)
    pose = {}
    for channel in anim["channels"]:
        s = anim["samplers"][channel["sampler"]]
        times = glbread.accessor(gltf, blob, s["input"])
        out = glbread.accessor(gltf, blob, s["output"])
        t = min(max(time, times[0]), times[-1])
        k = max(i for i in range(len(times)) if times[i] <= t) if t > times[0] else 0
        k = min(k, len(times) - 2) if len(times) > 1 else 0
        if len(times) == 1:
            value = out[1]
        else:
            td = times[k + 1] - times[k]
            u = (t - times[k]) / td
            v0, b0, a1, v1 = out[3 * k + 1], out[3 * k + 2], out[3 * (k + 1)], out[3 * (k + 1) + 1]
            h = (2 * u ** 3 - 3 * u ** 2 + 1, u ** 3 - 2 * u ** 2 + u, -2 * u ** 3 + 3 * u ** 2, u ** 3 - u ** 2)
            value = tuple(h[0] * v0[i] + td * h[1] * b0[i] + h[2] * v1[i] + td * h[3] * a1[i] for i in range(len(v0)))
        node = channel["target"]["node"]
        if channel["target"]["path"] == "rotation":
            n = math.sqrt(sum(c * c for c in value))
            value = tuple(c / n for c in value)
        pose.setdefault(node, {})[channel["target"]["path"]] = value
    return pose


def main():
    args = sys.argv[1:]
    pose_spec = None
    if "--pose" in args:
        i = args.index("--pose")
        pose_spec = (args[i + 1], args[i + 2], float(args[i + 3]))
        args = args[:i] + args[i + 4:]
    focus = None
    if "--focus" in args:
        i = args.index("--focus")
        focus = (float(args[i + 1]), float(args[i + 2]), float(args[i + 3]))
        args = args[:i] + args[i + 4:]
    out, files = args[0], args[1:]
    first, _ = glbread.read(Path(files[0]).read_bytes())
    translations = [tuple(first["nodes"][i]["translation"]) for i in range(rig.BONE_COUNT)]
    pose = {}
    if pose_spec:
        agltf, ablob = glbread.read(Path(pose_spec[0]).read_bytes())
        pose = sample(agltf, ablob, pose_spec[1], pose_spec[2])
    world, bind = [None] * rig.BONE_COUNT, [None] * rig.BONE_COUNT
    for i in range(rig.BONE_COUNT):
        entry = pose.get(i, {})
        local = quat_matrix(entry.get("rotation", (0, 0, 0, 1)), entry.get("translation", translations[i]))
        world[i] = local if rig.PARENTS[i] < 0 else mat_mul(world[rig.PARENTS[i]], local)
        b = [[1, 0, 0, translations[i][0]], [0, 1, 0, translations[i][1]], [0, 0, 1, translations[i][2]], [0, 0, 0, 1]]
        bind[i] = b if rig.PARENTS[i] < 0 else mat_mul(bind[rig.PARENTS[i]], b)
    skin = [mat_mul(world[i], [[1, 0, 0, -bind[i][0][3]], [0, 1, 0, -bind[i][1][3]], [0, 0, 1, -bind[i][2][3]],
                               [0, 0, 0, 1]]) for i in range(rig.BONE_COUNT)]
    tris = []
    light = (0.35, 0.45, 0.82)
    for path in files:
        gltf, blob = glbread.read(Path(path).read_bytes())
        for mesh in gltf.get("meshes", []):
            for prim in mesh["primitives"]:
                material = gltf["materials"][prim["material"]]
                tint = TINTS.get(material["extras"].get("cnaTint", "none"), (255, 255, 255))
                a = prim["attributes"]
                P = glbread.accessor(gltf, blob, a["POSITION"])
                N = glbread.accessor(gltf, blob, a["NORMAL"])
                J = glbread.accessor(gltf, blob, a["JOINTS_0"])
                W = glbread.accessor(gltf, blob, a["WEIGHTS_0"])
                idx = glbread.accessor(gltf, blob, prim["indices"])
                SP, SN = [], []
                for p, n, j, w in zip(P, N, J, W):
                    sp, sn = [0.0, 0.0, 0.0], [0.0, 0.0, 0.0]
                    for bone, weight in zip(j, w):
                        if weight <= 0:
                            continue
                        m = skin[bone]
                        for r in range(3):
                            sp[r] += weight * (m[r][0] * p[0] + m[r][1] * p[1] + m[r][2] * p[2] + m[r][3])
                            sn[r] += weight * (m[r][0] * n[0] + m[r][1] * n[1] + m[r][2] * n[2])
                    SP.append(sp)
                    SN.append(sn)
                for t in range(0, len(idx), 3):
                    tris.append((tint, [SP[idx[t + k]] for k in range(3)], [SN[idx[t + k]] for k in range(3)]))
    views = [((1, 0, 0), (0, 0, 1)), ((0, 0, -1), (1, 0, 0)), ((math.cos(0.6), 0, -math.sin(0.6)),
                                                                 (math.sin(0.6), 0, math.cos(0.6)))]
    size = 420
    image = Image.new("RGB", (size * len(views), size), (40, 44, 52))
    draw = ImageDraw.Draw(image)
    scale = size / 2.05
    cx, cy = 0.0, 1.0
    if focus:
        cx, cy, half = focus
        scale = size / (2 * half)
    for vi, (right, toward) in enumerate(views):
        projected = []
        for tint, ps, ns in tris:
            sx = [vi * size + size / 2 + ((p[0] * right[0] + p[2] * right[2]) - cx) * scale for p in ps]
            sy = [size / 2 - (p[1] - cy) * scale for p in ps] if focus else [size - 12 - p[1] * scale for p in ps]
            area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0])
            if area >= 0:
                continue
            depth = sum(p[0] * toward[0] + p[2] * toward[2] for p in ps) / 3
            n = [sum(c) / 3 for c in zip(*ns)]
            ln = math.sqrt(sum(c * c for c in n)) or 1
            lx = light[0] * right[0] + light[2] * toward[0]
            lz = light[0] * right[2] + light[2] * toward[2]
            diffuse = max(0.0, (n[0] * lx + n[1] * light[1] + n[2] * lz) / ln)
            shade = 0.35 + 0.65 * diffuse
            projected.append((depth, list(zip(sx, sy)), tuple(int(c * shade) for c in tint)))
        projected.sort(key=lambda e: e[0])
        for _, poly, color in projected:
            draw.polygon(poly, fill=color)
    image.save(out)


if __name__ == "__main__":
    main()
