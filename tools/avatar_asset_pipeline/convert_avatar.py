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

Requires: pygltflib (pip install pygltflib) and Pillow (pip install Pillow, Task 11.19's
placeholder texture output). Does NOT require assimp's Python bindings — glTF2 parsing is
done directly via pygltflib, which has better animation/skin support than assimp's own
Python wrapper.
"""

import argparse
import struct
import sys
from pathlib import Path

try:
    import pygltflib
except ImportError:
    sys.exit("This script requires pygltflib: pip install pygltflib")

try:
    from PIL import Image
except ImportError:
    sys.exit("This script requires Pillow: pip install Pillow")

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


def _write_placeholder_texture(out_dir, part_name, size=4):
    """Writes a tiny neutral-white RGBA PNG for `part_name` and returns its Path.

    Task 11.19: before this, ContentManager could already load a per-part texture (see
    ContentManager.cpp's "texture" JSON field handling) but nothing ever emitted one, so
    AvatarRenderer.PartTintEXT's per-part tint (Task 11.17) was the only color signal
    that ever reached the GPU. This texture is intentionally neutral (white), not a
    painted per-material color: AvatarAppearanceEXT remains the sole color-customization
    authority (texture * tint == tint, no double-application of color). Painted surface
    detail is future work (see plans/plan_net.md Task 11.25), not this task — this task makes
    the texture *pipeline* itself real, end-to-end.
    """
    tex_path = out_dir / f"{part_name}.png"
    Image.new("RGBA", (size, size), (255, 255, 255, 255)).save(tex_path)
    return tex_path


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

    # build_node_hierarchy reorders bones into topological (BFS) order, which generally
    # differs from skin.joints' own declared order. inverseBindMatrices and every vertex's
    # JOINTS_0 indices are given in that *original* skin.joints order, though — remap both
    # to the new order, or bones end up skinned by the wrong bind pose/vertex entirely.
    # A second real bug found the same way as the bind_pose_local transpose above: caught
    # by actually rendering real content (Task 11.11), not by static review.
    joint_index_remap = [node_to_bone[node_idx] for node_idx in skin.joints]

    inverse_bind = read_accessor(gltf, blob, skin.inverseBindMatrices)
    # glTF's column-major storage for a column-vector transform (v'=Av) and XNA/CNA's
    # row-major storage for the *same* transform expressed in row-vector form
    # (v'=v*(A^T)) are byte-for-byte IDENTICAL — transposing the matrix and swapping
    # major order are inverse operations that cancel out. So this is a straight copy,
    # NOT a transpose.
    # Confirmed empirically (Task 11.11): an earlier version of this code DID transpose
    # here, which corrupted every bind-pose-local matrix, moving translation from row 4
    # (M41/M42/M43, where CNA's Matrix/BinReaderEXT::ReadMatrix expects it) into
    # column 4 — rendering a real avatar as a huge, nonsensical close-up instead of a
    # recognizable standing figure. A forced-identity-bones diagnostic render (bypassing
    # this code path entirely) proved the camera/mesh/shader path was already correct,
    # isolating the bug to exactly this matrix convention question.
    # Reordered via joint_index_remap for the same reason as above (topological reorder).
    inverse_bind_global = [None] * bone_count
    for original_idx, m in enumerate(inverse_bind):
        inverse_bind_global[joint_index_remap[original_idx]] = m
    # Bind-pose *local* transform isn't directly given by glTF's skin data (glTF only gives
    # the inverse bind *global* matrix) — derive it purely from inverse_bind_global (already
    # remapped/verified-correct above) via matrix inversion, rather than hand-deriving it a
    # second, independent way from each joint node's own TRS. An earlier version did the
    # latter (see _node_local_matrix, now removed) via hand-rolled quaternion-to-matrix math;
    # it produced a non-identity result even at the exact rest pose (confirmed by dumping
    # ComputeBoneTransformsEXT's own output — every bone should reduce to identity there,
    # by definition, since bind pose composed with its own inverse must cancel out), meaning
    # that independent derivation didn't actually agree with inverse_bind_global's convention
    # somewhere. Deriving bind_pose_local FROM inverse_bind_global instead sidesteps needing
    # to find that exact bug: it's correct by construction, since
    # bind_pose_global[i] * inverse(bind_pose_global[i]) is trivially identity, matching
    # ComputeBoneTransformsEXT's own worldTransforms[i] * InverseBindPoseGlobal[i] formula.
    bind_pose_global = [_invert4x4(m) for m in inverse_bind_global]
    bind_pose_local = []
    for i in range(bone_count):
        parent = parent_indices[i]
        if parent < 0:
            bind_pose_local.append(bind_pose_global[i])
        else:
            bind_pose_local.append(_mat_mul_rowmajor(bind_pose_global[i], _invert4x4(bind_pose_global[parent])))

    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    write_skeleton_bin(out_dir / "skeleton.bin", parent_indices, bind_pose_local, inverse_bind_global)

    parts = []
    for mesh_i, mesh in enumerate(gltf.meshes):
        for prim_i, prim in enumerate(mesh.primitives):
            part_name = mesh.name or f"part{mesh_i}_{prim_i}"
            verts_path = out_dir / f"{part_name}.verts.bin"
            idx_path = out_dir / f"{part_name}.idx.bin"
            _write_skinned_vertex_buffer(gltf, blob, prim, verts_path, joint_index_remap)
            _write_index_buffer(gltf, blob, prim, idx_path)
            tex_path = _write_placeholder_texture(out_dir, part_name)
            parts.append({
                "name": part_name,
                "vertices": verts_path.name,
                "indices": idx_path.name,
                "vertexStride": 52,
                "texture": tex_path.name,
            })

    manifest = {
        "skeleton": "skeleton.bin",
        "parts": parts,
        "animations": [],
    }
    _write_json(out_dir / "avatar.skinnedmodel.json", manifest)
    print(f"Wrote {out_dir / 'avatar.skinnedmodel.json'} ({bone_count} bones, {len(parts)} parts)")
    return names


def _tracks_from_animation(gltf, blob, anim, bone_names, clip_label):
    """Builds (duration, tracks) for one glTF animation, retargeted by joint *name* onto
    the base skeleton's bone indices (bone_names, produced by convert_body) — matching by
    name rather than index, since an animation's own node indices have no relationship to
    the base skeleton's node indices; only the joint *names* are expected to match
    (guaranteed either by MakeHuman's "Mixamo" rig preset per README.md, or by
    tools/avatar_builder/generate_skeleton.py's own bone names for CNA's own procedural
    pipeline, whose clips are embedded in the body file itself — see convert_embedded_clip).
    """
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
        print(f"  ({clip_label}: {len(skipped)} joint(s) in the clip had no matching base bone "
              f"by name, skipped: {', '.join(skipped[:5])}{'...' if len(skipped) > 5 else ''})")

    return duration, tracks


def _write_clip(out_dir, clip_name, duration, tracks):
    out_path = Path(out_dir) / "clips" / f"{clip_name}.clip.bin"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    write_clip_bin(out_path, duration, tracks)
    print(f"Wrote {out_path} ({len(tracks)} tracks, {duration:.2f}s)")
    return out_path.name


def convert_clip(clip_path, clip_name, bone_names, out_dir):
    """Converts one standalone Mixamo animation glTF (its own file, one animation) into a
    .clip.bin. See _tracks_from_animation for the retargeting-by-name approach."""
    gltf, blob = load_gltf(clip_path)
    if not gltf.animations:
        sys.exit(f"{clip_path}: no animation found")
    anim = gltf.animations[0]
    duration, tracks = _tracks_from_animation(gltf, blob, anim, bone_names, clip_name)
    return _write_clip(out_dir, clip_name, duration, tracks)


def convert_embedded_clip(gltf, blob, anim, bone_names, out_dir):
    """Converts one animation that's already embedded in an already-loaded glTF (as
    opposed to convert_clip's standalone-file case) into a .clip.bin, using the
    animation's own `name` as the clip name. This is CNA's own
    tools/avatar_builder/generate_avatar.py output's shape: body + skeleton + every clip
    bundled in one .glb, unlike the MakeHuman/Mixamo workflow's separate body-file-plus-
    per-clip-file layout convert_clip was originally written for."""
    duration, tracks = _tracks_from_animation(gltf, blob, anim, bone_names, anim.name)
    return _write_clip(out_dir, anim.name, duration, tracks)


def _mat_mul_rowmajor(a, b):
    """4x4 matrix multiply; a, b, and the result are all flat 16-tuples with the same
    indexing convention (m[i*4+j] = row i, col j) — result = a @ b in the usual sense.
    Convention-agnostic: works correctly whether that indexing is "really" row-major or
    column-major, as long as every matrix passed through this file uses the same one
    (see the note in convert_body about glTF/CNA's byte layout being interchangeable)."""
    result = [0.0] * 16
    for i in range(4):
        for j in range(4):
            result[i * 4 + j] = sum(a[i * 4 + k] * b[k * 4 + j] for k in range(4))
    return tuple(result)


def _invert4x4(m):
    """General 4x4 matrix inverse via Gauss-Jordan elimination (no numpy dependency —
    pygltflib is this script's only requirement). m is a flat 16-tuple, m[i*4+j] = row i
    col j; the result uses the same convention. Raises ValueError if m is singular."""
    aug = [list(m[r * 4:r * 4 + 4]) + [1.0 if c == r else 0.0 for c in range(4)] for r in range(4)]
    for col in range(4):
        pivot_row = max(range(col, 4), key=lambda r: abs(aug[r][col]))
        if abs(aug[pivot_row][col]) < 1e-9:
            raise ValueError("matrix is singular, cannot invert")
        aug[col], aug[pivot_row] = aug[pivot_row], aug[col]
        pivot = aug[col][col]
        aug[col] = [x / pivot for x in aug[col]]
        for r in range(4):
            if r != col:
                factor = aug[r][col]
                aug[r] = [x - factor * y for x, y in zip(aug[r], aug[col])]
    return tuple(aug[r][4 + c] for r in range(4) for c in range(4))


def _write_skinned_vertex_buffer(gltf, blob, prim, out_path, joint_index_remap):
    """joint_index_remap[original skin.joints index] -> bone index in skeleton.bin's
    topological order (see build_node_hierarchy) — every vertex's raw JOINTS_0 value is
    an index into skin.joints, not already a skeleton.bin bone index, so it must be
    remapped the same way inverse_bind_global is in convert_body."""
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
            remapped_j = [joint_index_remap[int(x)] for x in j]
            f.write(struct.pack("<3f3f2f4f4B", *pos, *nrm, *uv, *w, *remapped_j))


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
    parser.add_argument("--body", required=True,
                         help="Base body .glb — a MakeHuman export, or CNA's own "
                              "tools/avatar_builder/generate_avatar.py output")
    parser.add_argument("--out", required=True, help="Output content directory")
    parser.add_argument("--clip", nargs=2, action="append", default=[],
                         metavar=("GLB", "PRESET_NAME"),
                         help="Mixamo animation .glb + AvatarAnimationPreset name, repeatable")
    parser.add_argument("--embedded-clips", action="store_true",
                         help="Also convert every animation already embedded in --body "
                              "itself, named to match its own AvatarAnimationPreset name "
                              "(e.g. CNA's own generate_avatar.py output, which bundles "
                              "body+skeleton+clips in one .glb instead of the MakeHuman/"
                              "Mixamo workflow's separate per-clip files --clip expects)")
    args = parser.parse_args()

    bone_names = convert_body(args.body, args.out)

    clip_entries = []
    for clip_glb, preset_name in args.clip:
        clip_file = convert_clip(clip_glb, preset_name, bone_names, args.out)
        clip_entries.append({"name": preset_name, "clip": f"clips/{clip_file}"})

    if args.embedded_clips:
        gltf, blob = load_gltf(args.body)
        for anim in gltf.animations:
            if not anim.name:
                sys.exit(f"{args.body}: an embedded animation has no name — "
                         f"can't derive an AvatarAnimationPreset name for it")
            clip_file = convert_embedded_clip(gltf, blob, anim, bone_names, args.out)
            clip_entries.append({"name": anim.name, "clip": f"clips/{clip_file}"})

    if clip_entries:
        import json
        manifest_path = Path(args.out) / "avatar.skinnedmodel.json"
        manifest = json.loads(manifest_path.read_text())
        manifest["animations"] = clip_entries
        _write_json(manifest_path, manifest)
        print(f"Updated {manifest_path} with {len(clip_entries)} animation(s)")


if __name__ == "__main__":
    main()
