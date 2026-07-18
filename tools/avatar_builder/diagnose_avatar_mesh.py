#!/usr/bin/env python3
"""Mesh-level diagnostics for a generated avatar .glb - the tooling that actually located the
avatar's rendering defects during the audit_net.md remediation rounds, packaged so the next
investigation does not have to rebuild it.

Deliberately mesh-level rather than a GPU "debug render" (unlit / part-ID / normal-colour /
weight-heatmap passes). Those visualise a symptom and still need a human to judge it; these
measure the *cause* numerically and print exact vertex counts, penetration depths and bone
names, which is what let each defect be pinned down and verified. Where a picture genuinely
helps, `scripts/avatar_visual_regression_check.py` already renders through the real pipeline
and reports a per-region structural metric.

Three checks:

  crossings  For every ordered mesh pair, how many of A's vertices lie strictly INSIDE B
             (signed distance to B's nearest surface, signed by B's face normal there), and
             which bone each buried vertex is nearest. A garment that is fully outside the body
             is fine; one fully inside is merely hidden; one that CROSSES is the defect - the
             renderer alternates between the two surfaces along the intersection curve, which on
             low-poly meshes reads as a jagged interleaved seam.

  normals    Vertices sharing (near-)coincident positions whose loop normals diverge widely -
             the CSG "normal singularity" signature, e.g. where several capsule surfaces meet at
             one symmetric point and no single normal is correct.

  weights    Per-bone skin-weight summary per mesh: how many vertices each bone influences, and
             how many vertices are blended across 2+ bones. A garment whose blend profile at a
             joint differs from the body's will deform differently there and cross it under
             animation - that mismatch (body blend_radius 0.1474 vs garments' 0.08 default) was
             a real defect found this way.

Usage (must run inside Blender):
    blender --background --python tools/avatar_builder/diagnose_avatar_mesh.py -- <avatar.glb> [checks...]

`checks` defaults to all three; pass any subset of: crossings normals weights
"""
import collections
import math
import sys

try:
    import bpy
    import mathutils
    from mathutils.bvhtree import BVHTree
except ImportError:
    sys.exit("This script must be run inside Blender: "
             "blender --background --python diagnose_avatar_mesh.py -- <avatar.glb>")


def _argv_after_ddash():
    return sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []


def _load(glb_path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=glb_path)
    meshes = {o.name: o for o in bpy.data.objects if o.type == "MESH"}
    armature = next((o for o in bpy.data.objects if o.type == "ARMATURE"), None)
    return meshes, armature


def _bvh(obj):
    verts = [obj.matrix_world @ v.co for v in obj.data.vertices]
    polys = [list(p.vertices) for p in obj.data.polygons]
    m3 = obj.matrix_world.to_3x3()
    normals = [(m3 @ p.normal).normalized() for p in obj.data.polygons]
    return BVHTree.FromPolygons(verts, polys), normals


def check_crossings(meshes, armature):
    print("\n===== crossings =====")
    # glTF only stores joint head positions reliably (Blender synthesises bone tails on import),
    # so buried vertices are attributed to the nearest bone HEAD, not to a bone segment.
    heads = {}
    if armature is not None:
        heads = {b.name: (armature.matrix_world @ b.head_local) for b in armature.data.bones}

    trees = {n: _bvh(o) for n, o in meshes.items()}
    for a in sorted(meshes):
        for b in sorted(meshes):
            if a == b:
                continue
            bvh, bnormals = trees[b]
            inside = []
            total = 0
            for v in meshes[a].data.vertices:
                p = meshes[a].matrix_world @ v.co
                loc, _n, idx, dist = bvh.find_nearest(p)
                if loc is None:
                    continue
                total += 1
                if (p - loc).dot(bnormals[idx]) < 0.0:
                    inside.append((p, dist))
            if total == 0 or not inside:
                continue
            if len(inside) == total:
                print(f"{a} vs {b}: fully inside ({total} verts) - hidden, not a defect")
                continue
            print(f"{a} vs {b}: *** CROSSES *** {len(inside)}/{total} verts inside, "
                  f"worst {-max(d for _p, d in inside):.4f}m")
            if heads:
                per_bone = collections.Counter()
                worst = {}
                for p, d in inside:
                    bone = min(heads.items(), key=lambda kv: (p - kv[1]).length)[0]
                    per_bone[bone] += 1
                    worst[bone] = min(worst.get(bone, 0.0), -d)
                for bone, n in per_bone.most_common(6):
                    print(f"      near {bone:14s} n={n:4d} worst={worst[bone]:+.4f}m")


def check_normals(meshes):
    print("\n===== normals (divergence at coincident positions) =====")
    for name in sorted(meshes):
        obj = meshes[name]
        mesh = obj.data
        world = obj.matrix_world
        buckets = collections.defaultdict(list)
        for poly in mesh.polygons:
            for li in poly.loop_indices:
                vi = mesh.loops[li].vertex_index
                wco = world @ mesh.vertices[vi].co
                key = (round(wco.x, 2), round(wco.y, 2), round(wco.z, 2))
                buckets[key].append(mathutils.Vector(mesh.loops[li].normal))
        spreads = []
        for key, normals in buckets.items():
            if len(normals) < 2:
                continue
            worst = 0.0
            for i in range(len(normals)):
                for j in range(i + 1, len(normals)):
                    d = max(-1.0, min(1.0, normals[i].dot(normals[j])))
                    worst = max(worst, math.degrees(math.acos(d)))
            spreads.append((worst, key, len(normals)))
        if not spreads:
            continue
        spreads.sort(reverse=True)
        bad = sum(1 for w, _k, _n in spreads if w > 60.0)
        print(f"{name}: {len(spreads)} shared-position buckets, {bad} with >60deg divergence")
        for worst, key, n in spreads[:5]:
            if worst <= 60.0:
                break
            print(f"      pos={key} loops={n} max_divergence={worst:.1f}deg")


def check_weights(meshes, armature):
    print("\n===== weights =====")
    if armature is None:
        print("(no armature in file)")
        return
    for name in sorted(meshes):
        obj = meshes[name]
        groups = {g.index: g.name for g in obj.vertex_groups}
        if not groups:
            print(f"{name}: no vertex groups (unskinned)")
            continue
        per_bone = collections.Counter()
        influence_hist = collections.Counter()
        for v in obj.data.vertices:
            contributing = [g for g in v.groups if g.weight > 1e-4]
            influence_hist[len(contributing)] += 1
            for g in contributing:
                per_bone[groups.get(g.group, f"#{g.group}")] += 1
        blended = sum(n for k, n in influence_hist.items() if k >= 2)
        print(f"{name}: {len(obj.data.vertices)} verts, {len(groups)} groups, "
              f"{blended} blended across 2+ bones "
              f"({100.0 * blended / max(1, len(obj.data.vertices)):.0f}%)")
        print("      influences/vertex: " +
              ", ".join(f"{k}:{influence_hist[k]}" for k in sorted(influence_hist)))
        for bone, n in per_bone.most_common(6):
            print(f"      {bone:14s} influences {n:4d} verts")


def main():
    args = _argv_after_ddash()
    if not args:
        sys.exit("usage: ... -- <avatar.glb> [crossings] [normals] [weights]")
    glb = args[0]
    checks = args[1:] or ["crossings", "normals", "weights"]
    meshes, armature = _load(glb)
    print(f"Loaded {glb}: {len(meshes)} meshes, armature={'yes' if armature else 'no'}")
    if "crossings" in checks:
        check_crossings(meshes, armature)
    if "normals" in checks:
        check_normals(meshes)
    if "weights" in checks:
        check_weights(meshes, armature)


if __name__ == "__main__":
    main()
