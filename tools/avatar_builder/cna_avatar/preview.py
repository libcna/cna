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
from .description import decode, SLOTS, ACCESSORIES, MAKEUP, EXTENDED_COLORS

BUILD_AXES = {1: 5, 5: 14, 2: 6, 3: 8, 6: 11, 8: 15, 12: 20, 16: 22, 20: 25, 22: 28, 25: 33, 28: 36}
FEATURES = ("eyeLeft", "eyeRight", "eyebrowLeft", "eyebrowRight", "mouth")
# Makeup decals of the body: (makeup kind, side index).
MAKEUP_FEATURES = {"lips": ("lips", 0), "shadowLeft": ("eyeShadow", 0), "shadowRight": ("eyeShadow", 1),
                   "linerLeft": ("eyeliner", 0), "linerRight": ("eyeliner", 1), "blushLeft": ("blush", 0),
                   "blushRight": ("blush", 1)}
CLOSED_EYES = (4, 7, 8, 13)
EYES = ["Neutral", "Sad", "Angry", "Confused", "Laughing", "Shocked", "Happy", "Yawning", "Sleeping", "LookUp",
        "LookDown", "LookLeft", "LookRight", "Blink"]
EYEBROWS = ["Neutral", "Sad", "Angry", "Confused", "Raised"]
MOUTHS = ["Neutral", "Sad", "Angry", "Confused", "Laughing", "Shocked", "Happy", "PhoneticO", "PhoneticAi",
          "PhoneticEe", "PhoneticFv", "PhoneticW", "PhoneticL", "PhoneticDth"]
TINTS = ("skin", "hair", "eyes", "top", "bottom", "shoes", "accessory", "lips", "eyeShadow", "blush", "jewelry", "gloves")
# The first colour of each format 3 palette (AvatarDescriptionCodec.cpp), drawn for an unset (black) slot.
DEFAULT_COLORS = {"lips": (176, 86, 96), "eyeShadow": (138, 108, 98), "blush": (226, 128, 124), "jewelry": (212, 176, 98),
                  "gloves": (220, 60, 56)}


# ----- catalogs ---------------------------------------------------------------------------------

class Catalogs:
    """Catalog directories under one root (v3/, ...), resolved by exact version."""

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
            self._glbs[key] = read_avatar_glb(self.file(version, name),
                                              lambda texture: premultiplied(Image.open(io.BytesIO(self.file(version, texture)))))
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


def read_avatar_glb(data, shared_texture=None):
    """{'bind': (71,3), 'primitives': [...]} of a CNA avatar GLB; shared_texture(name) loads a
    catalog texture file a material names (cnaTexture)."""
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
            elif "cnaTexture" in extras and shared_texture:
                texture = shared_texture(extras["cnaTexture"])
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
                "specular": extras.get("cnaSpecular", 1.0), "shininess": extras.get("cnaShininess", 24.0),
                "side": {"left": 1, "right": 2}.get(extras.get("cnaSide"), 0),
                "blend": extras.get("cnaBlend") == "alpha",
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


def _hairline(line, phi):
    a = abs((phi + math.pi) % (2.0 * math.pi) - math.pi)
    sm = lambda e0, e1, x: float(_smoothstep(e0, e1, x))
    if a < 0.30 * math.pi:
        return math.pi * (line[0] + (line[1] - line[0]) * sm(0.0, 0.30 * math.pi, a))
    if a < 0.55 * math.pi:
        return math.pi * (line[1] + (line[2] - line[1]) * sm(0.30 * math.pi, 0.55 * math.pi, a))
    return math.pi * (line[2] + (line[3] - line[2]) * sm(0.55 * math.pi, math.pi, a))


def _surface_radius(surface, theta, phi):
    rings, segments = surface["rings"], surface["segments"]
    radii = np.array(surface["radii"]).reshape(rings + 1, segments)
    row = np.clip(theta / math.pi, 0.0, 1.0) * rings
    column = ((phi + math.pi) / (2.0 * math.pi) * segments) % segments
    r0 = np.minimum(row.astype(np.int64), rings - 1)
    c0 = np.minimum(column.astype(np.int64), segments - 1)
    c1 = (c0 + 1) % segments
    fr, fc = row - r0, column - c0
    top = radii[r0, c0] + (radii[r0, c1] - radii[r0, c0]) * fc
    bottom = radii[r0 + 1, c0] + (radii[r0 + 1, c1] - radii[r0 + 1, c0]) * fc
    return top + (bottom - top) * fr


def flatten_under_hat(prim, surface, cover):
    """AvatarModel.cpp's flattenUnderHat: hair a covering hat hides lies flat under it."""
    centre = np.array(surface["centre"])
    local = prim["positions"] - centre
    r = np.linalg.norm(local, axis=1)
    d = local / np.maximum(r, 1e-9)[:, None]
    theta = np.arccos(np.clip(d[:, 1], -1.0, 1.0))
    phi = np.arctan2(d[:, 0], d[:, 2])
    line = np.array([_hairline(cover["line"], p) for p in phi])
    covered = theta < line - 0.05 * math.pi
    amount = 1.0 - _smoothstep(line - 0.03 * math.pi, line + 0.02 * math.pi, theta)
    limit = _surface_radius(surface, theta, phi) + cover["lift"]
    positions = centre + d * (r + (np.minimum(r, limit) - r) * amount)[:, None]
    keep = ~covered[prim["indices"]].all(axis=1)
    return dict(prim, positions=positions, indices=prim["indices"][keep])


def face_displacement(points, controls, face):
    """Every face control's displacement of authored points (full weight)."""
    fake_joints = np.full((len(points), 4), rig.INDEX["Head"])
    weights = np.tile([1.0, 0.0, 0.0, 0.0], (len(points), 1))
    return face_deform(points, fake_joints, weights, controls, face, True) - points


PROPORTION_BONES = None


def proportions(authored, local, value):
    """AvatarModel.cpp's proportionsFor: (morphed joint positions, stretch per bone, axis per bone, head scale)."""
    t = max(-1.0, min(1.0, (value - 128.0) / 127.0))
    stretch = np.ones(rig.BONE_COUNT)
    axes = np.zeros((rig.BONE_COUNT, 3))
    if t == 0.0:
        return authored.copy(), stretch, axes, 1.0
    at = lambda compact, tall: 1.0 + (-t * (compact - 1.0) if t < 0 else t * (tall - 1.0))
    legs, arms, torso, neck, shoulders = at(0.84, 1.07), at(0.88, 1.05), at(0.90, 1.05), at(0.80, 1.06), at(0.88, 1.03)
    head_scale = at(1.10, 0.96)
    factor = np.ones((rig.BONE_COUNT, 3))
    for side in ("Left", "Right"):
        factor[rig.INDEX["Hip" + side]] = (1, legs, 1)
        factor[rig.INDEX["Knee" + side]] = (legs, legs, legs)
        factor[rig.INDEX["Ankle" + side]] = (legs, legs, legs)
        factor[rig.INDEX["Collar" + side]] = (shoulders, torso, 1)
        factor[rig.INDEX["Shoulder" + side]] = (shoulders, 1, 1)
        factor[rig.INDEX["Elbow" + side]] = (arms, arms, arms)
        factor[rig.INDEX["Wrist" + side]] = (arms, arms, arms)
    factor[rig.INDEX["BackUpper"]] = (1, torso, 1)
    factor[rig.INDEX["Neck"]] = (1, torso, 1)
    factor[rig.INDEX["Head"]] = (1, neck, 1)
    for bone in range(rig.BONE_COUNT):
        parent = rig.PARENTS[bone]
        if rig.NAMES[bone].startswith("Slot") and parent > 0:
            name = rig.NAMES[parent]
            if name.startswith(("Hip", "Knee")):
                factor[bone] = (legs, legs, legs)
            elif name.startswith(("Shoulder", "Elbow")):
                factor[bone] = (arms, arms, arms)
    out = authored.copy()
    hip_height = authored[rig.INDEX["HipLeft"]][1]
    for bone in range(1, rig.BONE_COUNT):
        parent = rig.PARENTS[bone]
        offset = local[bone].copy()
        if bone == rig.INDEX["BackLower"] or (parent == 0 and rig.NAMES[bone].startswith("Slot")):
            offset[1] = min(offset[1], hip_height) * legs + max(offset[1] - hip_height, 0.0) * torso
        else:
            offset = offset * factor[bone]
        out[bone] = out[parent] + offset
    for bone, toward in BUILD_AXES.items():
        direction = authored[toward] - authored[bone]
        before = np.linalg.norm(direction)
        axes[bone] = direction / before
        stretch[bone] = np.linalg.norm(out[toward] - out[bone]) / before if before > 1e-4 else 1.0
    return out, stretch, axes, head_scale


def apply_proportions(positions, joints, weights, authored, morph):
    moved_joints, stretch, axes, head_scale = morph
    out = np.zeros_like(positions)
    head = rig.INDEX["Head"]
    for k in range(4):
        bone = joints[:, k]
        offset = positions - authored[bone]
        scale_head = (bone == head)[:, None]
        along = (offset * axes[bone]).sum(axis=1, keepdims=True) * axes[bone] * (stretch[bone][:, None] - 1.0)
        offset = np.where(scale_head, offset * head_scale, offset + along)
        out += weights[:, k:k + 1] * (moved_joints[bone] + offset)
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
    entries = {e["id"]: e for key in ("items", "featureItems", "faceStyles") for e in manifest.get(key, [])}

    def worn(item_id, slot):
        e = entries.get(item_id)
        return e if item_id and e and e["slot"] == slot else None
    pieces = [(body_glb, "face", None, 0, None)]
    substituted = []
    hidden = set()
    for slot in SLOTS:
        e = worn(d.items[slot], slot)
        if e:
            hidden.update(e.get("hides", []))
    for name in ACCESSORIES:
        e = worn(d.accessories.get(name, 0), name)
        if e:
            hidden.update(e.get("hides", []))
    hat = worn(d.items["hat"], "hat")
    cover = hat.get("hairCover") if hat else None
    ears = worn(d.ears, "ears") or next((e for e in manifest.get("featureItems", []) if e["slot"] == "ears"), None)
    if ears:
        pieces.append((catalogs.glb(version, ears["assets"][body]), "face", None, 0, None))
    for slot in SLOTS:
        item_id = d.items[slot]
        item = worn(item_id, slot)
        if item:
            scope = "head" if slot in ("hair", "hat") else None
            follow = item.get("follow")
            if slot == "glasses" and not follow:
                scope = "head"
            pieces.append((catalogs.glb(version, item["assets"][body]), scope, follow, 0, cover if slot == "hair" else None))
        elif item_id:
            substituted.append(item_id)
            fallback = next((i for i in manifest["items"] if i["slot"] == slot), None)
            if slot not in ("glasses", "hat") and fallback:
                pieces.append((catalogs.glb(version, fallback["assets"][body]), None, None, 0, None))
    feature = worn(d.facial_hair, "facialHair")
    if d.facial_hair and feature:
        pieces.append((catalogs.glb(version, feature["assets"][body]), "face", None, 0, None))
    elif d.facial_hair:
        substituted.append(d.facial_hair)
    for name in ACCESSORIES:
        item = worn(d.accessories.get(name, 0), name)
        if item and name not in hidden:
            pieces.append((catalogs.glb(version, item["assets"][body]), None, item.get("follow"), d.sides.get(name, 0), None))
    authored_height = manifest["bodies"][body]["authoredHeightMillimeters"] / 1000.0
    bind = body_glb["bind"]
    authored = world_positions(bind)
    morph = proportions(authored, bind, d.proportion)
    head = rig.INDEX["Head"]
    morphed_height = morph[0][head][1] + (authored_height - authored[head][1]) * morph[3]
    scale = (d.height_mm / 1000.0) / morphed_height
    factor = 1.0 + (d.build - 128.0) / 127.0 * 0.18
    colors = {name: np.array(d.colors[name] if d.colors.get(name, (0, 0, 0)) != (0, 0, 0) or name not in DEFAULT_COLORS
                             else DEFAULT_COLORS[name]) / 255.0 for name in TINTS}
    controls = manifest.get("faceControls", {}).get(body, [])
    shaped = bool(controls) and any(v != 128 for v in d.face)
    anchors = manifest.get("faceAnchors", {}).get(body)
    eye_move, eye_stretch, eye_centre, ear_move = np.zeros(3), 1.0, np.zeros(3), np.zeros((2, 3))
    # Earrings are made for the first ear shape's lobes and carried to the worn shape's (AvatarModel.cpp).
    first_ears = next((e for e in manifest.get("featureItems", []) if e["slot"] == "ears"), None)
    lobes = None
    if ears and first_ears and "lobes" in ears and "lobes" in first_ears:
        lobes = np.array(ears["lobes"][body])
        ear_move = lobes - np.array(first_ears["lobes"][body])
    if shaped and anchors:
        pts = np.array([anchors["eyeLeft"], anchors["eyeRight"], anchors["earLeft"], anchors["earRight"]])
        if lobes is not None:
            pts[2:] = lobes
        moved = pts + face_displacement(pts, controls, d.face)
        eye_centre = (pts[0] + pts[1]) / 2.0
        eye_move = (moved[0] + moved[1]) / 2.0 - eye_centre
        eye_stretch = abs(moved[0][0] - moved[1][0]) / max(abs(pts[0][0] - pts[1][0]), 1e-4)
        ear_move = ear_move + moved[2:] - pts[2:]
    brows = worn(d.eyebrows, "eyebrows")
    makeup = {kind: worn(d.makeup.get(kind, 0), kind) for kind in MAKEUP}
    opaque, decals = [], []
    for glb, scope, follow, sides, hair_cover in pieces:
        for prim in glb["primitives"]:
            if prim["side"] and sides and prim["side"] != sides:
                continue
            extra = {}
            if prim["feature"] in MAKEUP_FEATURES:
                kind, side = MAKEUP_FEATURES[prim["feature"]]
                style = makeup[kind]
                if not style:
                    continue
                extra = {"style_tiles": style["tiles"], "alpha": style.get("opacity", 1.0), "makeup_side": side}
            if hair_cover:
                prim = flatten_under_hat(prim, manifest["headSurface"][body], hair_cover)
            positions = prim["positions"]
            if shaped and scope:
                positions = face_deform(positions, prim["joints"], prim["weights"], controls, d.face, scope == "face")
            if shaped and follow == "eyes":
                positions = positions.copy()
                positions[:, 0] = eye_centre[0] + (positions[:, 0] - eye_centre[0]) * eye_stretch
                positions = positions + eye_move
            elif follow == "ears":
                positions = positions + np.where((positions[:, 0] >= 0.0)[:, None], ear_move[0], ear_move[1])
            positions = apply_proportions(positions, prim["joints"], prim["weights"], authored, morph)
            positions = apply_build(positions, prim["joints"], prim["weights"], morph[0], factor) * scale
            tint = colors.get(prim["tint"], np.ones(3))
            part = dict(prim, positions=positions, color=prim["color"] * tint, **extra)
            (decals if prim["feature"] or prim["blend"] else opaque).append(part)
    decals.sort(key=lambda p: (bool(p["feature"]), p["layer"]))
    tiles, layout = catalogs.face_tiles(version)
    eyebrow_tiles = brows["tiles"] if brows else None
    bind_positions = morph[0] * scale
    translations = bind_positions.copy()
    for bone in range(1, rig.BONE_COUNT):
        translations[bone] = bind_positions[bone] - bind_positions[rig.PARENTS[bone]]
    return {"height": d.height_mm / 1000.0, "bind_translations": translations,
            "bind_positions": bind_positions, "parts": opaque + decals, "tiles": tiles,
            "layout": layout, "substituted": substituted, "eyebrow_tiles": eyebrow_tiles}


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

    def shade(self, rgba, diffuse, normal, position, eye, specular=1.0, shininess=24.0):
        """SkinnedEffect with one directional light: texture * diffuse * (ambient + N.L) + specular."""
        n = normal / np.maximum(np.linalg.norm(normal, axis=1, keepdims=True), 1e-9)
        dot_l = n @ -self.direction
        lit = (dot_l > 0)[:, None]
        e = eye - position
        e /= np.maximum(np.linalg.norm(e, axis=1, keepdims=True), 1e-9)
        h = e - self.direction
        h /= np.maximum(np.linalg.norm(h, axis=1, keepdims=True), 1e-9)
        spec = np.power(np.maximum(np.einsum("ij,ij->i", n, h), 0.0)[:, None] * lit, shininess) * self.color * 0.12 * specular
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
    opaque = [i for i, part in enumerate(model["parts"]) if not part["feature"] and not part.get("blend")]
    decals = [i for i, part in enumerate(model["parts"]) if part["feature"] or part.get("blend")]
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
        tile = _tile(model, part, expression) if part["feature"] else None
        p, _ = posed[i]
        sx, sy, w = camera.project(p)
        pix, z, tri, bary = fragments(sx, sy, w, part["indices"], width, height)
        keep = z <= depth[pix] + 1e-4
        pix, z, tri, bary = pix[keep], z[keep], tri[keep], bary[keep]
        if not len(pix):
            continue
        win = nearest(pix, z)
        src = _shade(model, i, posed[i], tri[win], bary[win], lighting, camera, model["tiles"][tile] if tile is not None else None)
        src = src * part.get("alpha", 1.0)
        dst = color[pix[win]]
        color[pix[win]] = src + dst * (1.0 - src[:, 3:4])
    color[:, 3] = 1.0
    return np.clip(color.reshape(height, width, 4), 0.0, 1.0)


def _tile(model, part, expression):
    layout = model["layout"]
    mouth, left_eye, right_eye, left_brow, right_brow = expression
    feature = part["feature"]
    if "style_tiles" in part:
        tiles = part["style_tiles"]
        if feature == "lips":
            return tiles[mouth]
        side = part["makeup_side"]
        if len(tiles) == 4 and (left_eye if side == 0 else right_eye) in CLOSED_EYES:
            return tiles[2 + side]
        return tiles[side]
    if feature == "eyeLeft":
        return layout["eyes"][EYES[left_eye]]["left"][part["layer"]]
    if feature == "eyeRight":
        return layout["eyes"][EYES[right_eye]]["right"][part["layer"]]
    if feature in ("eyebrowLeft", "eyebrowRight"):
        side, state = (0, left_brow) if feature == "eyebrowLeft" else (1, right_brow)
        if model.get("eyebrow_tiles"):
            return model["eyebrow_tiles"][state * 2 + side]
        return layout["eyebrows"][EYEBROWS[state]]["left" if side == 0 else "right"]
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
    return lighting.shade(rgba, part["color"], normal, position, camera.eye, part.get("specular", 1.0), part.get("shininess", 24.0))


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
