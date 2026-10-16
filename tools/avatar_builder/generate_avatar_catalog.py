#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Generates CNA's original avatar catalog v3 deterministically.

    python3 tools/avatar_builder/generate_avatar_catalog.py            # write the catalog
    python3 tools/avatar_builder/generate_avatar_catalog.py --check    # verify it byte for byte

Everything is procedural and original; nothing is downloaded or derived from third-party or
Xbox avatar content. Output: modules/gamer-services/assets/avatars/v3/, CNA's avatar catalog
(docs/avatars.md), revision CATALOG_REVISION: a revision keeps every id of the previous one in its
kind. The development catalogs v1 and v2 that preceded v3 are not shipped or supported; Git history
keeps them (v1 from this tool's predecessor at commit 6ca06d069, v2 from this tool at commit d1730d4f3).
"""
import argparse
import hashlib
import json
import math
import platform
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from cna_avatar import catalog, face, facecontrols, rig, animations  # noqa: E402

DEFAULT_OUT = HERE.parent.parent / "modules" / "gamer-services" / "assets" / "avatars" / "v3"
CATALOG_VERSION = 3
# Revision 2 (2026-10): the first v3 release's ids keep their kinds, and everything added since is
# named only by format 3 descriptions (docs/avatars.md).
# Revision 3 (2026-10): the same entries, art reworked for visual quality.
# Revision 4 (2026-10): the same entries; sculpted lids, lips and brows, decals on the head's own facets.
CATALOG_REVISION = 4
# The catalog contract level a reader needs: 2 adds shared texture files, face styles, accessories,
# ear shapes, hair covers, anchors and the head surface.
READER = 2


def _round(values, digits=5):
    return [round(v, digits) for v in values]


def head_surface(body_type):
    """The head surface as radii around its centre on a grid (HeadSurface in AvatarAssets.hpp)."""
    from cna_avatar import head
    rings, segments = 32, 48
    radii = []
    for r in range(rings + 1):
        theta = math.pi * r / rings
        for k in range(segments):
            phi = -math.pi + 2.0 * math.pi * k / segments
            p = head.local_point(body_type, theta, phi)
            radii.append(round(math.sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]), 5))
    return {"centre": _round(head.centre(body_type)), "rings": rings, "segments": segments, "radii": radii}


def face_anchors(body_type):
    from cna_avatar import head
    a = head.anchors(body_type)
    return {name: _round(a[name]) for name in ("eyeLeft", "eyeRight", "earLeft", "earRight")}


def _glb_task(task):
    """One GLB of the catalog: ("body", body type) or ("item", body type, id, slot, name)."""
    if task[0] == "body":
        return catalog.body_glb(task[1])
    return catalog.item_glb(*task[1:])


def build(jobs=1):
    from cna_avatar import accessories, head
    files = {}
    textures_used = set()
    items, features = [], []
    tasks = [("body", b) for b in rig.BODY_TYPES]
    for entry_def in catalog.ITEMS + catalog.FEATURE_ITEMS:
        item_id, slot, name = entry_def[:3]
        tasks += [("item", b, item_id, slot, name) for b in rig.BODY_TYPES]
    # Every GLB and atlas tile depends only on its own task, so the output is the same however the
    # work is split.
    pool = None
    if jobs > 1:
        import multiprocessing
        pool = multiprocessing.Pool(jobs)
    try:
        built = pool.map(_glb_task, tasks, chunksize=1) if pool else [_glb_task(task) for task in tasks]
        atlas, layout, styles = face.atlas(pool)
    finally:
        if pool:
            pool.close()
    glbs = {}
    for task, (data, used) in zip(tasks, built):
        name = "body.%s.glb" % task[1] if task[0] == "body" else "%s.%s.glb" % (task[4], task[1])
        glbs[name] = data
        textures_used.update(used)
    for body_type in rig.BODY_TYPES:
        files["body.%s.glb" % body_type] = glbs["body.%s.glb" % body_type]
    for entry_def in catalog.ITEMS + catalog.FEATURE_ITEMS:
        item_id, slot, name, female, male = entry_def[:5]
        extras = entry_def[5] if len(entry_def) > 5 else {}
        entry = {"id": item_id, "slot": slot, "name": name, "random": {"female": female, "male": male},
                 "assets": {b: "%s.%s.glb" % (name, b) for b in rig.BODY_TYPES}}
        for body_type in rig.BODY_TYPES:
            files["%s.%s.glb" % (name, body_type)] = glbs["%s.%s.glb" % (name, body_type)]
        if name in accessories.HAT_COVER:
            entry["hairCover"] = {"line": list(accessories.HAT_COVER[name]),
                                  "lift": round(accessories.UNDER_HAT_LIFT * max(head.SCALE.values()), 5)}
        entry.update(extras)
        if name in catalog.LABELS:
            entry["label"] = catalog.LABELS[name]
        (features if slot in ("facialHair", "ears") else items).append(entry)
    for kind in sorted(textures_used):
        files[catalog.texture_file(kind)] = catalog.texture_bytes(kind)
    files["face_features.png"] = atlas
    files["animations.glb"] = catalog.animations_glb()
    assets = [{"name": name, "sha256": hashlib.sha256(data).hexdigest(), "size": len(data)}
              for name, data in sorted(files.items())]
    manifest = {
        "format": 1, "catalogVersion": CATALOG_VERSION, "revision": CATALOG_REVISION, "reader": READER,
        "rig": "cna-avatar-71",
        "note": "Original CNA avatar assets generated by tools/avatar_builder/generate_avatar_catalog.py.",
        "bodies": {b: {"asset": "body.%s.glb" % b,
                       "authoredHeightMillimeters": int(round(rig.PROPORTIONS[b]["height"] * 1000))}
                   for b in rig.BODY_TYPES},
        "items": items,
        "featureItems": features,
        "faceStyles": [dict(style, label=catalog.LABELS[style["name"]]) if style["name"] in catalog.LABELS else style
                       for style in catalog.face_styles(styles)],
        "outfits": catalog.outfits(),
        "facePresets": catalog.face_presets(),
        "faceControls": {b: facecontrols.controls(b) for b in rig.BODY_TYPES},
        "faceAnchors": {b: face_anchors(b) for b in rig.BODY_TYPES},
        "headSurface": {b: head_surface(b) for b in rig.BODY_TYPES},
        "face": {"asset": "face_features.png", "layout": layout},
        "animations": {"asset": "animations.glb", "presets": animations.PRESETS},
        "assets": assets,
    }
    files["catalog.json"] = (json.dumps(manifest, indent=1, sort_keys=True) + "\n").encode("ascii")
    return files


# plans/plan_apple_m4.md AM4-166: the one part of the released catalog v3 another C library cannot
# reproduce. FemaleShocked places both hands at face(+-0.13, -0.13), a point the head's Newton
# solve (cna_avatar.head._solve) has no solution for: its residual wanders between 0.06 and 0.26 for
# all twelve iterations, and a one-ulp change in the starting guess moves the answer by 11 radians.
# What v3 froze for those six arm rotations is therefore whatever glibc's libm last bits produced
# (catalog v3 is released and frozen, docs/avatars.md, so it is not regenerated); every other byte
# of every file is reproducible anywhere. On another C library the check still requires all of
# that byte for byte, and of these channels exactly what can be required of them: the same
# accessors, holding finite values and unit quaternions. The solver itself belongs to a future
# catalog.
LIBM_SENSITIVE_CLIP = "FemaleShocked"
LIBM_SENSITIVE_JOINTS = frozenset(("ShoulderLeft", "ShoulderRight", "ElbowLeft", "ElbowRight",
                                   "WristLeft", "WristRight"))


def _glb_parts(data):
    json_length = struct.unpack_from("<I", data, 12)[0]
    return json.loads(data[20:20 + json_length]), data[20:20 + json_length], data[20 + json_length + 8:]


def _libm_sensitive_word_ranges(document):
    ranges = []
    for animation in document.get("animations", []):
        if animation.get("name") != LIBM_SENSITIVE_CLIP:
            continue
        for channel in animation["channels"]:
            node = document["nodes"][channel["target"]["node"]].get("name")
            if node not in LIBM_SENSITIVE_JOINTS or channel["target"]["path"] != "rotation":
                continue
            sampler = animation["samplers"][channel["sampler"]]
            accessor = document["accessors"][sampler["output"]]
            view = document["bufferViews"][accessor["bufferView"]]
            start = (view.get("byteOffset", 0) + accessor.get("byteOffset", 0)) // 4
            # A CUBICSPLINE sampler stores (in-tangent, value, out-tangent) per key frame; only the
            # value is a rotation.
            stride = 3 if sampler.get("interpolation") == "CUBICSPLINE" else 1
            ranges.append((start, start + 4 * accessor["count"], stride))
    return ranges


def _differs_only_by_libm(released, generated):
    """Whether two animations.glb files differ only inside the libm-sensitive channels, and those
    still hold finite unit quaternions. Returns the number of differing words, or None."""
    if len(released) != len(generated):
        return None
    document, released_json, released_bin = _glb_parts(released)
    _, generated_json, generated_bin = _glb_parts(generated)
    if released_json != generated_json or released[:20] != generated[:20]:
        return None
    ranges = _libm_sensitive_word_ranges(document)
    if len(ranges) != len(LIBM_SENSITIVE_JOINTS):
        return None
    inside = set()
    for start, end, stride in ranges:
        inside.update(range(start, end))
        for element, quad in enumerate(range(start, end, 4)):
            q = struct.unpack_from("<4f", generated_bin, quad * 4)
            if not all(math.isfinite(c) for c in q):
                return None
            is_value = stride == 1 or element % stride == 1
            if is_value and abs(math.sqrt(sum(c * c for c in q)) - 1.0) > 1e-3:
                return None
    differing = 0
    for word in range(len(released_bin) // 4):
        if released_bin[word * 4:word * 4 + 4] != generated_bin[word * 4:word * 4 + 4]:
            if word not in inside:
                return None
            differing += 1
    return differing


def _manifest_differs_only_in(released, generated, name):
    """Whether two catalog.json manifests are equal apart from asset `name`'s SHA-256."""
    a, b = json.loads(released), json.loads(generated)
    for manifest in (a, b):
        for asset in manifest.get("assets", []):
            if asset["name"] == name:
                asset["sha256"] = None
    return a == b


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--jobs", type=int, default=1, help="build the GLBs in this many processes")
    args = parser.parse_args()
    files = build(args.jobs)
    if args.check:
        stale = [name for name, data in files.items()
                 if not (args.out / name).is_file() or (args.out / name).read_bytes() != data]
        extra = sorted(p.name for p in args.out.iterdir() if p.name not in files) if args.out.is_dir() else []
        if (stale and not extra and platform.libc_ver()[0] != "glibc"
                and set(stale) <= {"animations.glb", "catalog.json"} and "animations.glb" in stale):
            differing = _differs_only_by_libm((args.out / "animations.glb").read_bytes(),
                                              files["animations.glb"])
            if differing is not None and _manifest_differs_only_in(
                    (args.out / "catalog.json").read_bytes(), files["catalog.json"], "animations.glb"):
                print("avatar catalog v%d up to date (%d files, %d bytes); on this C library %d words of "
                      "%s's %d libm-sensitive arm rotations differ from glibc's, as documented"
                      % (CATALOG_VERSION, len(files), sum(len(d) for d in files.values()), differing,
                         LIBM_SENSITIVE_CLIP, len(LIBM_SENSITIVE_JOINTS)))
                return 0
        if stale or extra:
            print("avatar catalog is stale: %s%s" % (", ".join(sorted(stale)), (" extra: " + ", ".join(extra)) if extra else ""))
            return 1
        print("avatar catalog v%d up to date (%d files, %d bytes)" % (CATALOG_VERSION, len(files),
                                                                      sum(len(d) for d in files.values())))
        return 0
    args.out.mkdir(parents=True, exist_ok=True)
    for name, data in files.items():
        (args.out / name).write_bytes(data)
    # Files of an earlier generation that this one no longer has (a catalog directory holds only
    # the generator's output).
    for stale in sorted(p for p in args.out.iterdir() if p.is_file() and p.name not in files and
                        p.suffix in (".glb", ".png", ".json")):
        stale.unlink()
    print("wrote %d files (%d bytes) to %s" % (len(files), sum(len(d) for d in files.values()), args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
