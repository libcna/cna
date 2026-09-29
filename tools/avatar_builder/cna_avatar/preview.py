# SPDX-License-Identifier: MS-PL
"""Texture-aware CPU preview of CNA avatars (needs numpy and Pillow).

Mirrors the runtime closely enough to judge art: the description is assembled the way
AvatarModel.cpp does it (items, build, height, tints), bones are sampled the way
AvatarAnimation/AvatarRenderer do, base-color textures and the expression atlas are sampled
with their UVs and alpha, and pixels are lit like SkinnedEffect (one directional light,
ambient, the renderer's faint specular). It is a development aid; the real renderer captures
(cna_avatar_review) are the authority.
"""
import hashlib
import io
import json
import math
from pathlib import Path

import numpy as np
from PIL import Image

from . import glbread, rig
from .description import decode, SLOTS

BUILD_AXES = {1: 5, 5: 14, 2: 6, 3: 8, 6: 11, 8: 15, 12: 20, 16: 22, 20: 25, 22: 28, 25: 33, 28: 36}
FEATURES = ("eyeLeft", "eyeRight", "eyebrowLeft", "eyebrowRight", "mouth")
EYES = ["Neutral", "Sad", "Angry", "Confused", "Laughing", "Shocked", "Happy", "Yawning", "Sleeping", "LookUp",
        "LookDown", "LookLeft", "LookRight", "Blink"]
EYEBROWS = ["Neutral", "Sad", "Angry", "Confused", "Raised"]
MOUTHS = ["Neutral", "Sad", "Angry", "Confused", "Laughing", "Shocked", "Happy", "PhoneticO", "PhoneticAi",
          "PhoneticEe", "PhoneticFv", "PhoneticW", "PhoneticL", "PhoneticDth"]
TINTS = ("skin", "hair", "eyes", "top", "bottom", "shoes", "accessory")


# ----- catalogs ---------------------------------------------------------------------------------

class Catalogs:
    """Catalog directories under one root (v1/, v2/, ...), resolved by exact version."""

    def __init__(self, root):
        self.root = Path(root)
        self._manifests, self._files, self._glbs, self._tiles = {}, {}, {}, {}

    def versions(self):
        return sorted(int(p.name[1:]) for p in self.root.glob("v*") if (p / "catalog.json").is_file())

    def manifest(self, version):
        if version not in self._manifests:
            self._manifests[version] = json.loads((self.root / ("v%d" % version) / "catalog.json").read_text())
        return self._manifests[version]

    def file(self, version, name):
        key = (version, name)
        if key not in self._files:
            listed = {a["name"]: a for a in self.manifest(version)["assets"]}[name]
            data = (self.root / ("v%d" % version) / name).read_bytes()
            assert hashlib.sha256(data).hexdigest() == listed["sha256"], "%s does not match catalog v%d" % (name, version)
            self._files[key] = data
        return self._files[key]

    def glb(self, version, name):
        key = (version, name)
        if key not in self._glbs:
            self._glbs[key] = read_avatar_glb(self.file(version, name))
        return self._glbs[key]

    def face_tiles(self, version):
        if version not in self._tiles:
            face = self.manifest(version)["face"]
            layout = face["layout"]
            atlas = premultiplied(Image.open(io.BytesIO(self.file(version, face["asset"]))))
            size, columns = layout["tileSize"], layout["columns"]
            rows = atlas.shape[0] // size
            self._tiles[version] = ([atlas[r * size:(r + 1) * size, c * size:(c + 1) * size]
                                     for r in range(rows) for c in range(columns)], layout)
        return self._tiles[version]


def premultiplied(image):
    rgba = np.asarray(image.convert("RGBA"), dtype=np.float32) / 255.0
    rgba[..., :3] *= rgba[..., 3:4]
    return rgba


def read_avatar_glb(data):
    """{'bind': (71,3), 'primitives': [...]} of a CNA avatar GLB."""
    gltf, blob = glbread.read(data)
    skin = gltf["skins"][0]
    slot_of_joint = [rig.INDEX[gltf["nodes"][n]["name"]] for n in skin["joints"]]
    bind = np.zeros((rig.BONE_COUNT, 3))
    for joint, node in zip(slot_of_joint, skin["joints"]):
        bind[joint] = gltf["nodes"][node].get("translation", (0.0, 0.0, 0.0))
    primitives = []
    for mesh in gltf.get("meshes", []):
        for prim in mesh["primitives"]:
            a = prim["attributes"]
            material = gltf["materials"][prim["material"]]
            extras = material.get("extras", {})
            pbr = material["pbrMetallicRoughness"]
            texture = None
            if "baseColorTexture" in pbr:
                image = gltf["textures"][pbr["baseColorTexture"]["index"]]["source"]
                texture = premultiplied(Image.open(io.BytesIO(glbread.image_bytes(gltf, blob, image))))
            count = gltf["accessors"][a["POSITION"]]["count"]
            joints = np.array(glbread.accessor(gltf, blob, a["JOINTS_0"]), dtype=np.int64)
            weights = np.array(glbread.accessor(gltf, blob, a["WEIGHTS_0"]), dtype=np.float64)
            weights /= weights.sum(axis=1, keepdims=True)
            primitives.append({
                "positions": np.array(glbread.accessor(gltf, blob, a["POSITION"]), dtype=np.float64),
                "normals": np.array(glbread.accessor(gltf, blob, a["NORMAL"]), dtype=np.float64),
                "uvs": np.array(glbread.accessor(gltf, blob, a["TEXCOORD_0"]), dtype=np.float64)
                if "TEXCOORD_0" in a else np.zeros((count, 2)),
                "joints": np.array(slot_of_joint)[joints],
                "weights": weights,
                "indices": np.array(glbread.accessor(gltf, blob, prim["indices"]), dtype=np.int64).reshape(-1, 3),
                "color": np.clip(np.array(pbr.get("baseColorFactor", [1, 1, 1, 1])[:3]), 0.0, 1.0),
                "tint": extras.get("cnaTint", "none"),
                "feature": extras.get("cnaFeature"),
                "layer": extras.get("cnaLayer", 0),
                "texture": texture,
            })
    return {"bind": bind, "primitives": primitives, "gltf": gltf, "blob": blob}


# ----- assembly (AvatarModel.cpp) ---------------------------------------------------------------

def world_positions(local):
    out = np.zeros_like(local)
    for i in range(rig.BONE_COUNT):
        parent = rig.PARENTS[i]
        out[i] = local[i] if parent < 0 else out[parent] + local[i]
    return out


def apply_build(positions, joints, weights, authored, factor):
    if abs(factor - 1.0) < 1e-4:
        return positions
    out = positions.copy()
    dominant = joints[np.arange(len(joints)), np.argmax(weights, axis=1)]
    for bone, toward in BUILD_AXES.items():
        sel = dominant == bone
        if not sel.any():
            continue
        origin = authored[bone]
        direction = authored[toward] - origin
        direction /= np.linalg.norm(direction)
        offset = positions[sel] - origin
        along = np.outer(offset @ direction, direction)
        out[sel] = origin + along + (offset - along) * factor
    return out


def _smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def face_deform(positions, joints, weights, controls, face, face_scope):
    """AvatarModel.cpp's face-shape controls (see facecontrols.py) on Head-weighted vertices."""
    head_w = (weights * (joints == rig.INDEX["Head"])).sum(axis=1)
    mask = head_w > 0.0
    if not mask.any() or not controls:
        return positions
    p = positions[mask]
    disp = np.zeros_like(p)
    for control in controls:
        t = float(np.clip((face[control["parameter"]] - 128) / 127.0, -1.0, 1.0))
        if t == 0.0 or (control["scope"] == "face" and not face_scope):
            continue
        for op in control["ops"]:
            q = p - np.array(op["centre"])
            d = np.sqrt((((q / np.array(op["radii"])) ** 2).sum(axis=1)))
            w = (1.0 - _smoothstep(op["inner"], 1.0, d))[:, None]
            if "scale" in op:
                disp += w * q * (np.array(op["scale"]) ** t - 1.0)
            elif "move" in op:
                disp += w * t * np.array(op["move"])
            else:
                axis = np.array(op["rotate"]) / np.linalg.norm(op["rotate"])
                angle = math.radians(op["degrees"] * t)
                rq = q * math.cos(angle) + np.cross(axis, q) * math.sin(angle) + np.outer(q @ axis, axis) * (1.0 - math.cos(angle))
                disp += w * (rq - q)
    out = positions.copy()
    out[mask] = p + head_w[mask][:, None] * disp
    return out


def assemble(catalogs, description_bytes):
    """The avatar a description renders as, or None when it is not a readable CNA avatar."""
    d = decode(description_bytes)
    if d is None:
        return None
    version = d.catalog_version
    if version not in catalogs.versions():
        return None
    manifest = catalogs.manifest(version)
    body = "male" if d.body_type == 1 else "female"
    body_glb = catalogs.glb(version, manifest["bodies"][body]["asset"])
    items = {item["id"]: item for item in manifest["items"]}
    features = {item["id"]: item for item in manifest.get("featureItems", [])}
    assets = [(body_glb, True)]
    substituted = []
    hat = items.get(d.items["hat"])
    under_hat = bool(hat and hat.get("coversHair"))
    for slot in SLOTS:
        item_id = d.items[slot]
        item = items.get(item_id)
        if item_id and item and item["slot"] == slot:
            files = item.get("hatAssets") if slot == "hair" and under_hat and "hatAssets" in item else item["assets"]
            assets.append((catalogs.glb(version, files[body]), False))
        elif item_id:
            substituted.append(item_id)
            fallback = next((i for i in manifest["items"] if i["slot"] == slot), None)
            if slot not in ("glasses", "hat") and fallback:
                assets.append((catalogs.glb(version, fallback["assets"][body]), False))
    feature = features.get(d.facial_hair)
    if d.facial_hair and feature:
        assets.append((catalogs.glb(version, feature["assets"][body]), True))
    elif d.facial_hair:
        substituted.append(d.facial_hair)
    authored_height = manifest["bodies"][body]["authoredHeightMillimeters"] / 1000.0
    scale = (d.height_mm / 1000.0) / authored_height
    bind = body_glb["bind"]
    authored = world_positions(bind)
    factor = 1.0 + (d.build - 128.0) / 127.0 * 0.18
    colors = {name: np.array(d.colors[name]) / 255.0 for name in TINTS}
    opaque, decals = [], []
    controls = manifest.get("faceControls", {}).get(body, []) if d.format == 2 else []
    for glb, face_scope in assets:
        for prim in glb["primitives"]:
            positions = face_deform(prim["positions"], prim["joints"], prim["weights"], controls, d.face, face_scope)
            positions = apply_build(positions, prim["joints"], prim["weights"], authored, factor) * scale
            tint = colors.get(prim["tint"], np.ones(3))
            part = dict(prim, positions=positions, color=prim["color"] * tint)
            (decals if prim["feature"] else opaque).append(part)
    decals.sort(key=lambda p: p["layer"])
    tiles, layout = catalogs.face_tiles(version)
    return {"height": d.height_mm / 1000.0, "bind_translations": bind * scale,
            "bind_positions": world_positions(bind * scale), "parts": opaque + decals, "tiles": tiles,
            "layout": layout, "substituted": substituted}


# ----- animation (AvatarAnimation.cpp / AvatarClips.cpp) ----------------------------------------

class ClipLibrary:
    def __init__(self, glb):
        self.bind = glb["bind"]
        gltf, blob = glb["gltf"], glb["blob"]
        node_slot = {n: rig.INDEX[gltf["nodes"][n]["name"]] for n in gltf["skins"][0]["joints"]}
        self.clips = {}
        for animation in gltf["animations"]:
            tracks = []
            for channel in animation["channels"]:
                sampler = animation["samplers"][channel["sampler"]]
                tracks.append((node_slot[channel["target"]["node"]], channel["target"]["path"],
                               np.array(glbread.accessor(gltf, blob, sampler["input"]), dtype=np.float64),
                               np.array(glbread.accessor(gltf, blob, sampler["output"]), dtype=np.float64)))
            extras = animation.get("extras", {})
            duration = max(t[2][-1] for t in tracks) if tracks else 0.0
            self.clips[animation["name"]] = {"tracks": tracks, "duration": duration,
                                             "expressions": extras.get("cnaExpressions", [])}

    @staticmethod
    def _evaluate(times, values, seconds):
        value = lambda k: values[k * 3 + 1]
        if len(times) == 1 or seconds <= times[0]:
            return value(0)
        if seconds >= times[-1]:
            return value(len(times) - 1)
        k = int(np.searchsorted(times, seconds, side="right")) - 1
        td = times[k + 1] - times[k]
        s = (seconds - times[k]) / td if td > 0 else 0.0
        s2, s3 = s * s, s * s * s
        return (value(k) * (2 * s3 - 3 * s2 + 1) + values[k * 3 + 2] * (td * (s3 - 2 * s2 + s)) +
                value(k + 1) * (-2 * s3 + 3 * s2) + values[(k + 1) * 3] * (td * (s3 - s2)))

    def sample(self, name, seconds):
        """(71 local rotations as xyzw, root offset, expression [mouth, leftEye, rightEye, leftBrow, rightBrow])."""
        rotations = np.tile([0.0, 0.0, 0.0, 1.0], (rig.BONE_COUNT, 1))
        root = np.zeros(3)
        clip = self.clips.get(name)
        expression = [0, 0, 0, 0, 0]
        if clip is None:
            return rotations, root, expression
        for bone, path, times, values in clip["tracks"]:
            v = self._evaluate(times, values, seconds)
            if path == "translation":
                root = v[:3]
            else:
                n = np.linalg.norm(v)
                rotations[bone] = v / n if n > 1e-6 else (0.0, 0.0, 0.0, 1.0)
        for key in clip["expressions"]:
            if key[0] > seconds + 1e-6:
                break
            expression = list(key[1:])
        return rotations, root, expression


def quat_matrix(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def skin_matrices(model, rotations, root_offset, library_bind):
    """AvatarRenderer::DrawAvatar: each bone's rotation from the animation, translations from the
    avatar's own bind pose (the root keeps the animation's)."""
    world = [None] * rig.BONE_COUNT
    skins = np.zeros((rig.BONE_COUNT, 4, 4))
    for bone in range(rig.BONE_COUNT):
        t = library_bind[0] + root_offset if bone == 0 else model["bind_translations"][bone]
        local = np.eye(4)
        local[:3, :3] = quat_matrix(rotations[bone])
        local[:3, 3] = t
        parent = rig.PARENTS[bone]
        world[bone] = local if parent < 0 else world[parent] @ local
        unbind = np.eye(4)
        unbind[:3, 3] = -model["bind_positions"][bone]
        skins[bone] = world[bone] @ unbind
    return skins


def skin_part(part, skins, world):
    m = np.einsum("nk,nkij->nij", part["weights"], skins[part["joints"]])
    m = np.einsum("ij,njk->nik", world, m)
    p = np.einsum("nij,nj->ni", m[:, :3, :3], part["positions"]) + m[:, :3, 3]
    n = np.einsum("nij,nj->ni", m[:, :3, :3], part["normals"])
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)
    return p, n


# ----- rasterization ------------------------------------------------------------------------------

class Camera:
    def __init__(self, eye, target, fov, width, height, up=(0.0, 1.0, 0.0)):
        self.eye = np.array(eye, dtype=np.float64)
        z = self.eye - np.array(target, dtype=np.float64)
        z /= np.linalg.norm(z)
        x = np.cross(up, z)
        x /= np.linalg.norm(x)
        self.axes = np.stack([x, np.cross(z, x), z])
        self.f = 1.0 / math.tan(fov / 2.0)
        self.width, self.height = width, height

    def project(self, points):
        v = (points - self.eye) @ self.axes.T
        w = -v[:, 2]
        aspect = self.width / self.height
        sx = (self.f / aspect * v[:, 0] / w * 0.5 + 0.5) * self.width
        sy = (0.5 - self.f * v[:, 1] / w * 0.5) * self.height
        return sx, sy, w


def fragments(sx, sy, w, tris, width, height, cull=True):
    """Covered pixel centres of every triangle: (pixel, depth, triangle, perspective barycentrics)."""
    x = sx[tris]
    y = sy[tris]
    ww = w[tris]
    area = (x[:, 1] - x[:, 0]) * (y[:, 2] - y[:, 0]) - (x[:, 2] - x[:, 0]) * (y[:, 1] - y[:, 0])
    ok = (ww > 0.01).all(axis=1) & (np.abs(area) > 1e-12)
    if cull:
        ok &= area < 0  # glTF counter-clockwise front faces appear clockwise with y pointing down
    xmin = np.clip(np.floor(x.min(axis=1) - 0.5), 0, width - 1).astype(np.int64)
    xmax = np.clip(np.ceil(x.max(axis=1) - 0.5), 0, width - 1).astype(np.int64)
    ymin = np.clip(np.floor(y.min(axis=1) - 0.5), 0, height - 1).astype(np.int64)
    ymax = np.clip(np.ceil(y.max(axis=1) - 0.5), 0, height - 1).astype(np.int64)
    ok &= (x.max(axis=1) >= 0) & (x.min(axis=1) < width) & (y.max(axis=1) >= 0) & (y.min(axis=1) < height)
    span = np.maximum(xmax - xmin, ymax - ymin) + 1
    out = [[], [], [], []]
    lower = 0
    for size in (2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096):
        chosen = np.nonzero(ok & (span > lower) & (span <= size))[0]
        lower = size
        if not len(chosen):
            continue
        oy, ox = np.divmod(np.arange(size * size), size)
        per = max(1, 4_000_000 // (size * size))
        for start in range(0, len(chosen), per):
            t = chosen[start:start + per]
            px = xmin[t, None] + ox[None, :]
            py = ymin[t, None] + oy[None, :]
            cx, cy = px + 0.5, py + 0.5
            x0, x1, x2 = (x[t, k][:, None] for k in range(3))
            y0, y1, y2 = (y[t, k][:, None] for k in range(3))
            a = area[t][:, None]
            b0 = ((x1 - cx) * (y2 - cy) - (x2 - cx) * (y1 - cy)) / a
            b1 = ((x2 - cx) * (y0 - cy) - (x0 - cx) * (y2 - cy)) / a
            b2 = 1.0 - b0 - b1
            inside = (b0 >= -1e-9) & (b1 >= -1e-9) & (b2 >= -1e-9) & (px <= xmax[t, None]) & (py <= ymax[t, None])
            rows, cols = np.nonzero(inside)
            tri = t[rows]
            q = np.stack([b0[rows, cols], b1[rows, cols], b2[rows, cols]], axis=1) / ww[tri]
            s = q.sum(axis=1)
            out[0].append(py[rows, cols] * width + px[rows, cols])
            out[1].append(1.0 / s)
            out[2].append(tri)
            out[3].append(q / s[:, None])
    if not out[0]:
        return np.zeros(0, np.int64), np.zeros(0), np.zeros(0, np.int64), np.zeros((0, 3))
    return tuple(np.concatenate(c) for c in out)


def nearest(pix, depth):
    """Index of the nearest fragment per pixel."""
    order = np.lexsort((depth, pix))
    _, first = np.unique(pix[order], return_index=True)
    return order[first]


def sample(image, uv):
    """Bilinear, clamped, premultiplied RGBA at texture coordinates (N, 2)."""
    h, w = image.shape[:2]
    fx = np.clip(uv[:, 0] * w - 0.5, 0, w - 1)
    fy = np.clip(uv[:, 1] * h - 0.5, 0, h - 1)
    x0, y0 = np.floor(fx).astype(np.int64), np.floor(fy).astype(np.int64)
    x1, y1 = np.minimum(x0 + 1, w - 1), np.minimum(y0 + 1, h - 1)
    ax, ay = (fx - x0)[:, None], (fy - y0)[:, None]
    top = image[y0, x0] * (1 - ax) + image[y0, x1] * ax
    bottom = image[y1, x0] * (1 - ax) + image[y1, x1] * ax
    return top * (1 - ay) + bottom * ay


class Lighting:
    def __init__(self, direction=(0.0, 0.0, -1.0), color=(0.65, 0.65, 0.65), ambient=(0.35, 0.35, 0.35)):
        d = np.array(direction, dtype=np.float64)
        self.direction = d / np.linalg.norm(d)
        self.color = np.array(color, dtype=np.float64)
        self.ambient = np.array(ambient, dtype=np.float64)

    def shade(self, rgba, diffuse, normal, position, eye):
        """SkinnedEffect with one directional light: texture * diffuse * (ambient + N.L) + specular."""
        n = normal / np.maximum(np.linalg.norm(normal, axis=1, keepdims=True), 1e-9)
        dot_l = n @ -self.direction
        lit = (dot_l > 0)[:, None]
        e = eye - position
        e /= np.maximum(np.linalg.norm(e, axis=1, keepdims=True), 1e-9)
        h = e - self.direction
        h /= np.maximum(np.linalg.norm(h, axis=1, keepdims=True), 1e-9)
        spec = np.power(np.maximum(np.einsum("ij,ij->i", n, h), 0.0)[:, None] * lit, 24.0) * self.color * 0.12
        light = self.ambient + self.color * np.maximum(dot_l, 0.0)[:, None]
        rgb = rgba[:, :3] * diffuse * light + spec * rgba[:, 3:4]
        return np.concatenate([rgb, rgba[:, 3:4]], axis=1)


def render(model, camera, bones, expression, lighting=None, world=None, background=(54, 60, 72)):
    """RGBA float image of the avatar posed by `bones` (skin matrices) with `expression`
    [mouth, leftEye, rightEye, leftBrow, rightBrow]."""
    lighting = lighting or Lighting()
    world = np.eye(4) if world is None else world
    width, height = camera.width, camera.height
    color = np.tile(np.append(np.array(background, dtype=np.float64) / 255.0, 1.0), (width * height, 1))
    depth = np.full(width * height, np.inf)
    posed = [skin_part(part, bones, world) for part in model["parts"]]
    opaque = [i for i, part in enumerate(model["parts"]) if not part["feature"]]
    decals = [i for i, part in enumerate(model["parts"]) if part["feature"]]
    # Opaque parts: nearest fragment per pixel over all parts, then shade once.
    frags = []
    for i in opaque:
        p, _ = posed[i]
        sx, sy, w = camera.project(p)
        pix, z, tri, bary = fragments(sx, sy, w, model["parts"][i]["indices"], width, height)
        frags.append((i, pix, z, tri, bary))
    if frags:
        pix = np.concatenate([f[1] for f in frags])
        z = np.concatenate([f[2] for f in frags])
        which = np.concatenate([np.full(len(f[1]), f[0]) for f in frags])
        tri = np.concatenate([f[3] for f in frags])
        bary = np.concatenate([f[4] for f in frags])
        win = nearest(pix, z)
        depth[pix[win]] = z[win]
        for i in opaque:
            sel = win[which[win] == i]
            if len(sel):
                color[pix[sel]] = _shade(model, i, posed[i], tri[sel], bary[sel], lighting, camera, None)
    layout = model["layout"]
    for i in decals:
        part = model["parts"][i]
        tile = _tile(layout, part, expression)
        p, _ = posed[i]
        sx, sy, w = camera.project(p)
        pix, z, tri, bary = fragments(sx, sy, w, part["indices"], width, height)
        keep = z <= depth[pix] + 1e-4
        pix, z, tri, bary = pix[keep], z[keep], tri[keep], bary[keep]
        if not len(pix):
            continue
        win = nearest(pix, z)
        src = _shade(model, i, posed[i], tri[win], bary[win], lighting, camera, model["tiles"][tile])
        dst = color[pix[win]]
        color[pix[win]] = src + dst * (1.0 - src[:, 3:4])
    color[:, 3] = 1.0
    return np.clip(color.reshape(height, width, 4), 0.0, 1.0)


def _tile(layout, part, expression):
    mouth, left_eye, right_eye, left_brow, right_brow = expression
    feature = part["feature"]
    if feature == "eyeLeft":
        return layout["eyes"][EYES[left_eye]]["left"][part["layer"]]
    if feature == "eyeRight":
        return layout["eyes"][EYES[right_eye]]["right"][part["layer"]]
    if feature == "eyebrowLeft":
        return layout["eyebrows"][EYEBROWS[left_brow]]["left"]
    if feature == "eyebrowRight":
        return layout["eyebrows"][EYEBROWS[right_brow]]["right"]
    return layout["mouths"][MOUTHS[mouth]]


def _shade(model, index, posed, tri, bary, lighting, camera, texture):
    part = model["parts"][index]
    p, n = posed
    corners = part["indices"][tri]
    position = np.einsum("nk,nkj->nj", bary, p[corners])
    normal = np.einsum("nk,nkj->nj", bary, n[corners])
    image = texture if texture is not None else part["texture"]
    if image is not None:
        uv = np.einsum("nk,nkj->nj", bary, part["uvs"][corners])
        rgba = sample(image, uv)
    else:
        rgba = np.ones((len(tri), 4))
    return lighting.shade(rgba, part["color"], normal, position, camera.eye)


def yaw_matrix(degrees):
    m = np.eye(4)
    c, s = math.cos(math.radians(degrees)), math.sin(math.radians(degrees))
    m[0, 0], m[0, 2], m[2, 0], m[2, 2] = c, s, -s, c
    return m


def render_job(catalogs, library, job, size, background, supersample=2):
    """One review job (see avatar_review.py) as a PIL image."""
    model = assemble(catalogs, bytes.fromhex(job["description"]))
    if model is None:
        return Image.new("RGB", (size, size), tuple(background))
    rotations, root, expression = library.sample(job.get("animation", "Stand0"), job.get("time", 0.0))
    if job.get("expression"):
        expression = job["expression"]
    bones = skin_matrices(model, rotations, root, library.bind)
    cam = job["camera"]
    camera = Camera(cam["eye"], cam["target"], cam["fov"], size * supersample, size * supersample)
    light = job.get("light", {})
    lighting = Lighting(light.get("direction", (0.0, 0.0, -1.0)), light.get("color", (0.65,) * 3),
                        light.get("ambient", (0.35,) * 3))
    image = render(model, camera, bones, expression, lighting, yaw_matrix(job.get("yaw", 0.0)), background)
    out = Image.fromarray((image[..., :3] * 255.0 + 0.5).astype(np.uint8), "RGB")
    return out.resize((size, size), Image.LANCZOS) if supersample > 1 else out
