#!/usr/bin/env python3
"""Converts a MakeHuman body export + Mixamo animation clips into CNA's
.skinnedmodel.json / .skeleton.bin / .clip.bin content format for the
AvatarRenderer real-rendering extension (see docs/avatar-real-rendering-ext.md).

This is an offline, one-time asset-preparation tool — it is not part of the C++ build and is
never run by CNA itself at runtime.

Pipeline (see README.md for the manual steps this depends on):
  1. Export a MakeHuman body with the built-in "Mixamo" skeleton preset to FBX.
  2. Download the Mixamo animation clips listed in README.md's substitution table, each as FBX,
     retargeted onto a MakeHuman-compatible skeleton.
  3. Convert every FBX to glTF2 with the assimp CLI (normalizes FBX-exporter differences):
       assimp export body.fbx body.glb -f gltf2
       assimp export Wave.fbx Wave.glb -f gltf2
  4. Run this script:
       python3 convert_avatar.py --body body.glb --out content/avatar/male \\
           --clip Wave.glb Wave --clip Clap.glb Clap ...

Requires: pygltflib (pip install pygltflib). Does NOT require assimp's Python bindings —
glTF2 parsing is done directly via pygltflib, which has better animation/skin support than
assimp's own Python wrapper.
"""

import argparse
import struct
import sys
from pathlib import Path

try:
    import pygltflib
except ImportError:
    sys.exit("This script requires pygltflib: pip install pygltflib")

# glTF accessor componentType -> (struct format char, byte size)
_COMPONENT_TYPES = {
    5120: ("b", 1),  # BYTE
    5121: ("B", 1),  # UNSIGNED_BYTE
    5122: ("h", 2),  # SHORT
    5123: ("H", 2),  # UNSIGNED_SHORT
    5125: ("I", 4),  # UNSIGNED_INT
    5126: ("f", 4),  # FLOAT
}

# glTF accessor type -> component count
_TYPE_COUNTS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def read_accessor(gltf, blob, accessor_index):
    """Decodes a glTF accessor into a flat list of tuples (one tuple per element).

    pygltflib has no built-in accessor decoder (unlike higher-level glTF libraries in other
    languages) — this is the standard manual approach: locate the accessor's bufferView,
    compute the byte offset/stride, and unpack with the `struct` module.
    """
    accessor = gltf.accessors[accessor_index]
    view = gltf.bufferViews[accessor.bufferView]
    fmt_char, comp_size = _COMPONENT_TYPES[accessor.componentType]
    comp_count = _TYPE_COUNTS[accessor.type]
    element_size = comp_size * comp_count
    stride = view.byteStride or element_size
    base_offset = (view.byteOffset or 0) + (accessor.byteOffset or 0)

    results = []
    for i in range(accessor.count):
        offset = base_offset + i * stride
        raw = blob[offset:offset + element_size]
        values = struct.unpack("<" + fmt_char * comp_count, raw)
        results.append(values)
    return results


def load_gltf(path):
    gltf = pygltflib.GLTF2().load(str(path))
    blob = gltf.binary_blob()
    return gltf, blob


def build_node_hierarchy(gltf, skin):
    """Returns (joint_names, parent_indices) for a skin's joints, in the skin's own joint
    order. Bones are reordered into breadth-first (topological) order so that
    SkinnedModelEXT::ComputeBoneTransformsEXT can assume parent[i] < i.
    """
    joints = list(skin.joints)  # node indices, in skin-declared order
    joint_set = set(joints)

    # node index -> parent node index, restricted to nodes that are joints
    node_parent = {}
    for node_idx, node in enumerate(gltf.nodes):
        for child_idx in (node.children or []):
            if child_idx in joint_set:
                node_parent[child_idx] = node_idx if node_idx in joint_set else -1

    roots = [j for j in joints if node_parent.get(j, -1) not in joint_set]
    order = []
    frontier = list(roots)
    while frontier:
        n = frontier.pop(0)
        order.append(n)
        node = gltf.nodes[n]
        for child_idx in (node.children or []):
            if child_idx in joint_set:
                frontier.append(child_idx)

    node_to_bone = {node_idx: i for i, node_idx in enumerate(order)}
    parent_indices = []
    names = []
    for node_idx in order:
        parent_node = node_parent.get(node_idx, -1)
        parent_indices.append(node_to_bone.get(parent_node, -1))
        names.append(gltf.nodes[node_idx].name or f"bone{node_idx}")
    return names, parent_indices, node_to_bone


def write_skeleton_bin(path, parent_indices, bind_pose_local, inverse_bind_global):
    with open(path, "wb") as f:
        f.write(struct.pack("<i", len(parent_indices)))
        for p in parent_indices:
            f.write(struct.pack("<i", p))
        for m in bind_pose_local:
            f.write(struct.pack("<16f", *m))
        for m in inverse_bind_global:
            f.write(struct.pack("<16f", *m))


def write_clip_bin(path, duration_seconds, tracks):
    """tracks: list of (bone_index, keys) where keys is a list of
    (time, tx, ty, tz, qx, qy, qz, qw, sx, sy, sz)."""
    with open(path, "wb") as f:
        f.write(struct.pack("<d", duration_seconds))
        f.write(struct.pack("<i", len(tracks)))
        for bone_index, keys in tracks:
            f.write(struct.pack("<i", bone_index))
            f.write(struct.pack("<i", len(keys)))
            for key in keys:
                f.write(struct.pack("<d10f", *key))


def convert_body(body_path, out_dir):
    """Converts the base MakeHuman body glTF into a .skinnedmodel.json + .skeleton.bin +
    per-part vertex/index binary blobs. Returns bone_names (list, index = bone index) for
    reuse by convert_clip, so animation tracks can be retargeted by joint *name* onto the
    same bone indices this function assigned.
    """
    gltf, blob = load_gltf(body_path)
    if not gltf.skins:
        sys.exit(f"{body_path}: no skin found — expected a rigged export")
    skin = gltf.skins[0]

    names, parent_indices, node_to_bone = build_node_hierarchy(gltf, skin)
    bone_count = len(names)

    inverse_bind = read_accessor(gltf, blob, skin.inverseBindMatrices)
    # glTF stores matrices column-major; CNA's Matrix(m11..m44) constructor is row-major —
    # transpose on write.
    inverse_bind_global = [_transpose4x4(m) for m in inverse_bind]
    # Bind-pose *local* transform isn't directly given by glTF's skin data (glTF only gives
    # the inverse bind *global* matrix); derive it from each joint node's own local TRS, which
    # is what MakeHuman/Mixamo actually author into the node hierarchy. bone_to_node inverts
    # node_to_bone so bind_pose_local[i] lines up with bone index i, not node index.
    bone_to_node = {bone_idx: node_idx for node_idx, bone_idx in node_to_bone.items()}
    bind_pose_local = [_node_local_matrix(gltf.nodes[bone_to_node[i]]) for i in range(bone_count)]

    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    write_skeleton_bin(out_dir / "skeleton.bin", parent_indices, bind_pose_local, inverse_bind_global)

    parts = []
    for mesh_i, mesh in enumerate(gltf.meshes):
        for prim_i, prim in enumerate(mesh.primitives):
            part_name = mesh.name or f"part{mesh_i}_{prim_i}"
            verts_path = out_dir / f"{part_name}.verts.bin"
            idx_path = out_dir / f"{part_name}.idx.bin"
            _write_skinned_vertex_buffer(gltf, blob, prim, verts_path)
            _write_index_buffer(gltf, blob, prim, idx_path)
            parts.append({
                "name": part_name,
                "vertices": verts_path.name,
                "indices": idx_path.name,
                "vertexStride": 52,
            })

    manifest = {
        "skeleton": "skeleton.bin",
        "parts": parts,
        "animations": [],
    }
    _write_json(out_dir / "avatar.skinnedmodel.json", manifest)
    print(f"Wrote {out_dir / 'avatar.skinnedmodel.json'} ({bone_count} bones, {len(parts)} parts)")
    return names


def convert_clip(clip_path, clip_name, bone_names, out_dir):
    """Converts one Mixamo animation glTF into a .clip.bin, retargeted by joint *name* onto
    the base skeleton's bone indices (bone_names, produced by convert_body) — matching by
    name rather than index, since the clip file's own node indices have no relationship to
    the base skeleton file's node indices; only the joint *names* are expected to match
    (guaranteed by using MakeHuman's "Mixamo" rig preset for the base body, per README.md).
    """
    gltf, blob = load_gltf(clip_path)
    if not gltf.animations:
        sys.exit(f"{clip_path}: no animation found")
    anim = gltf.animations[0]
    name_to_bone_idx = {name: i for i, name in enumerate(bone_names)}

    duration = 0.0
    tracks = []
    channels_by_node = {}
    for ch in anim.channels:
        channels_by_node.setdefault(ch.target.node, []).append(ch)

    skipped = []
    for node_idx, channels in channels_by_node.items():
        joint_name = gltf.nodes[node_idx].name
        base_bone_idx = name_to_bone_idx.get(joint_name)
        if base_bone_idx is None:
            skipped.append(joint_name)
            continue  # joint not present in the base skeleton (e.g. an IK helper bone)

        keys_by_time = {}
        for ch in channels:
            sampler = anim.samplers[ch.sampler]
            times = [t[0] for t in read_accessor(gltf, blob, sampler.input)]
            values = read_accessor(gltf, blob, sampler.output)
            duration = max([duration] + list(times))
            for t, v in zip(times, values):
                entry = keys_by_time.setdefault(t, {"t": (0, 0, 0), "r": (0, 0, 0, 1), "s": (1, 1, 1)})
                if ch.target.path == "translation":
                    entry["t"] = v
                elif ch.target.path == "rotation":
                    entry["r"] = v
                elif ch.target.path == "scale":
                    entry["s"] = v

        keys = []
        for t in sorted(keys_by_time):
            e = keys_by_time[t]
            keys.append((t, *e["t"], *e["r"], *e["s"]))
        tracks.append((base_bone_idx, keys))

    if skipped:
        print(f"  ({clip_name}: {len(skipped)} joint(s) in the clip had no matching base bone "
              f"by name, skipped: {', '.join(skipped[:5])}{'...' if len(skipped) > 5 else ''})")

    out_path = Path(out_dir) / "clips" / f"{clip_name}.clip.bin"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    write_clip_bin(out_path, duration, tracks)
    print(f"Wrote {out_path} ({len(tracks)} tracks, {duration:.2f}s)")
    return out_path.name


def _node_local_matrix(node):
    if node.matrix:
        return tuple(node.matrix)
    # glTF TRS -> 4x4 column-major, matching inverseBindMatrices' own convention.
    tx, ty, tz = node.translation or (0, 0, 0)
    qx, qy, qz, qw = node.rotation or (0, 0, 0, 1)
    sx, sy, sz = node.scale or (1, 1, 1)
    # Standard quaternion-to-matrix (column-major, matches glTF).
    xx, yy, zz = qx * qx, qy * qy, qz * qz
    xy, xz, yz = qx * qy, qx * qz, qy * qz
    wx, wy, wz = qw * qx, qw * qy, qw * qz
    r = (
        1 - 2 * (yy + zz), 2 * (xy + wz), 2 * (xz - wy), 0,
        2 * (xy - wz), 1 - 2 * (xx + zz), 2 * (yz + wx), 0,
        2 * (xz + wy), 2 * (yz - wx), 1 - 2 * (xx + yy), 0,
        0, 0, 0, 1,
    )
    scale = (sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, sz, 0, 0, 0, 0, 1)
    m = _mat_mul(scale, r)
    m = list(m)
    m[12], m[13], m[14] = tx, ty, tz
    return tuple(m)


def _mat_mul(a, b):
    result = [0.0] * 16
    for row in range(4):
        for col in range(4):
            result[col * 4 + row] = sum(a[k * 4 + row] * b[col * 4 + k] for k in range(4))
    return tuple(result)


def _transpose4x4(m):
    return tuple(m[r + c * 4] for r in range(4) for c in range(4))


def _write_skinned_vertex_buffer(gltf, blob, prim, out_path):
    positions = read_accessor(gltf, blob, prim.attributes.POSITION)
    normals = (read_accessor(gltf, blob, prim.attributes.NORMAL)
               if prim.attributes.NORMAL is not None else [(0, 0, 1)] * len(positions))
    uvs = (read_accessor(gltf, blob, prim.attributes.TEXCOORD_0)
           if prim.attributes.TEXCOORD_0 is not None else [(0, 0)] * len(positions))
    weights = (read_accessor(gltf, blob, prim.attributes.WEIGHTS_0)
               if prim.attributes.WEIGHTS_0 is not None else [(1, 0, 0, 0)] * len(positions))
    joints = (read_accessor(gltf, blob, prim.attributes.JOINTS_0)
              if prim.attributes.JOINTS_0 is not None else [(0, 0, 0, 0)] * len(positions))

    with open(out_path, "wb") as f:
        for pos, nrm, uv, w, j in zip(positions, normals, uvs, weights, joints):
            f.write(struct.pack("<3f3f2f4f4B", *pos, *nrm, *uv, *w, *[int(x) for x in j]))


def _write_index_buffer(gltf, blob, prim, out_path):
    indices = read_accessor(gltf, blob, prim.indices)
    with open(out_path, "wb") as f:
        for (i,) in indices:
            f.write(struct.pack("<H", i))


def _write_json(path, data):
    import json
    with open(path, "w") as f:
        json.dump(data, f, indent=2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--body", required=True, help="Base MakeHuman body .glb")
    parser.add_argument("--out", required=True, help="Output content directory")
    parser.add_argument("--clip", nargs=2, action="append", default=[],
                         metavar=("GLB", "PRESET_NAME"),
                         help="Mixamo animation .glb + AvatarAnimationPreset name, repeatable")
    args = parser.parse_args()

    bone_names = convert_body(args.body, args.out)

    clip_entries = []
    for clip_glb, preset_name in args.clip:
        clip_file = convert_clip(clip_glb, preset_name, bone_names, args.out)
        clip_entries.append({"name": preset_name, "clip": f"clips/{clip_file}"})

    if clip_entries:
        import json
        manifest_path = Path(args.out) / "avatar.skinnedmodel.json"
        manifest = json.loads(manifest_path.read_text())
        manifest["animations"] = clip_entries
        _write_json(manifest_path, manifest)
        print(f"Updated {manifest_path} with {len(clip_entries)} animation(s)")


if __name__ == "__main__":
    main()
