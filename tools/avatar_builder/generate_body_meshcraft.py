#!/usr/bin/env python3
"""Task 7.4/7.5 (plans/plan_net.md Phase 7): builds CNA's procedural avatar body via the sibling
`../mesh-craft` tool instead of `generate_body.py`'s bpy-primitive construction, per decision 4b
("Body/head ... geometry should be generated as glTF via the sibling ../mesh-craft tool going
forward"). Replaces `generate_body.build_body()` as a drop-in: same signature, same BODY_NAME
contract, same return value, same `fix_automatic_weights` post-process — only how the mesh
*geometry* itself is authored differs.

Data flow (documented once here, per Task 7.4's own requirement):
    generate_skeleton.BONES (Z-up meters)
      -> bones_to_mc3_xml() remaps to mc3's Y-up frame and writes a real .mc3.xml document
         (one <capsule> per limb segment - see "Why capsules, not cylinder+sphere" below)
      -> mc3togltf (external binary, built from ../mesh-craft) evaluates it to a .glb
      -> bpy.ops.import_scene.gltf() imports that .glb into the *same* Blender session
         generate_skeleton.build_skeleton() already populated (Blender's own importer performs
         the standard Y-up -> Z-up remap automatically, landing the geometry back in exactly the
         same frame generate_skeleton.py's BONES table already uses)
      -> imported mesh objects are joined into one BODY_NAME object (matching
         generate_body.build_body()'s own existing contract)
      -> parented to armature_obj with automatic (heat-map) weights, then
         generate_body.fix_automatic_weights() runs unchanged (reused, not reimplemented)

Why capsules, not cylinder+sphere (Task 7.3's own audit finding): the old pipeline built one
cylinder *and one separate joint sphere* per bone, then only `bpy.ops.object.join()`'d them - an
object-datablock merge, not a real weld/boolean-merge, confirmed as the root cause of the
self-intersecting hip/crotch mesh (worst where the most primitives converge). A single `<capsule>`
per bone already has a real, watertight rounded cap built in - no separate sphere, no seam between
a flat cylinder end and a mismatched sphere. mesh-craft's plain `<cone>` primitive (this format
version, 0.3) has no radius1/radius2 frustum form, so true smooth per-segment taper isn't directly
available without CSG (which drops real UVs on its evaluated output - see MC3_FORMAT.md's own CSG
limitations) - properly-sized *untapered* capsules are this pass's deliberate, documented scope
boundary; true taper is a follow-up, not attempted here.

Offline, one-time content-authoring tool - not part of the C++ build, never run by CNA at
runtime. Requires an already-built `mc3togltf` binary (see ../mesh-craft's own README) - path
resolved via the MC3TOGLTF environment variable, falling back to a few conventional build-output
locations relative to this repo's own working copy.
"""

import math
import os
import subprocess
import sys
import tempfile
from pathlib import Path
from xml.sax.saxutils import quoteattr

try:
    import bpy
    import mathutils
except ImportError:
    sys.exit("This script must be run inside Blender: blender --background --python generate_body_meshcraft.py")

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_skeleton  # noqa: E402
import generate_body  # noqa: E402  (reuses fix_automatic_weights, BODY_NAME)

BODY_NAME = generate_body.BODY_NAME

# Task 7.5: thickened/rebalanced relative to generate_body.BONE_RADII's own values - see
# docs/avatar-art-direction.md's proportion table for the reasoning. Arms roughly doubled
# (were 2-3x thinner than legs with zero taper - the confirmed dominant "stick arm" cause);
# legs/torso left close to their original values (already reasonable per Task 7.3's own
# measurements); Head grown from 0.11 to 0.15 (the confirmed "too-small-head" fix - the
# skeleton's own bone *positions* are deliberately left untouched here, to avoid invalidating
# the existing baked animation clips per Task 7.5's own "don't regenerate animations unless a
# specific clip requires new bone layout" scope note).
BONE_RADII = {
    "Hips": 0.15, "Spine": 0.14, "Spine1": 0.14, "Neck": 0.06, "Head": 0.15,
    "Shoulder.L": 0.07, "Shoulder.R": 0.07,
    "UpperArm.L": 0.10, "UpperArm.R": 0.10,
    "LowerArm.L": 0.085, "LowerArm.R": 0.085,
    "Hand.L": 0.06, "Hand.R": 0.06,
    "UpperLeg.L": 0.10, "UpperLeg.R": 0.10,
    "LowerLeg.L": 0.08, "LowerLeg.R": 0.08,
    "Foot.L": 0.06, "Foot.R": 0.06,
}


def _locate_mc3togltf():
    """Resolves the mc3togltf binary: $MC3TOGLTF env var first, then a few conventional
    ../mesh-craft build-output locations (release build preferred over debug)."""
    env = os.environ.get("MC3TOGLTF")
    if env and Path(env).is_file():
        return env
    repo_root = Path(__file__).resolve().parents[2]
    mesh_craft_root = repo_root.parent / "mesh-craft"
    candidates = [
        mesh_craft_root / "b-release" / "mc3togltf" / "mc3togltf",
        mesh_craft_root / "cmake-build-debug" / "mc3togltf" / "mc3togltf",
    ]
    for c in candidates:
        if c.is_file():
            return str(c)
    raise FileNotFoundError(
        "mc3togltf binary not found - set MC3TOGLTF to its path, or build ../mesh-craft "
        f"(looked for: {', '.join(str(c) for c in candidates)})"
    )


def _mc3_position(point):
    """Z-up meters (generate_skeleton.py's own frame) -> mc3's Y-up frame: (x, z, y)."""
    x, y, z = point
    return (x, z, y)


def _recalculate_smooth_normals(obj):
    """Post-plans/plan_net.md remediation (2026-07-18, independent post-completion audit): mc3togltf's
    CSG evaluator exports *flat* per-triangle normals (`mc3togltf/src/CsgEvaluator.cpp`'s own
    comment: "Convert Manifold -> MeshData (flat face normals, no UVs)"), with duplicated
    coincident vertices at every merge seam (one vertex copy per adjacent flat-shaded triangle).
    On a rounded, capsule-based body this is not just a "faceted low-poly look" (this module's
    own earlier docstring assumed) - at CSG union seams, some of those flat per-triangle normals
    end up pointing at a steep/grazing angle relative to the single fixed directional light this
    project's avatar demos use, rendering as near-black patches (confirmed by direct fresh
    screenshot inspection at the neck, shoulder-to-arm junctions, groin, and - worst - a large
    dark mass across the chest during the Wave animation).

    **Correction (2026-07-18, second-round independent audit):** a first version of this function
    called `remove_doubles` + `shade_smooth()` and was verified only from a single, narrow camera
    angle - a second, wider-angle/different-pose check (`--yaw 25` during `Wave`) found the
    artifacts were still substantially present. Root cause of *that*: Blender's glTF importer
    loads mc3togltf's exported NORMAL accessor as **custom split normals** (confirmed directly:
    `mesh.has_custom_normals` reads `True` after import), which silently override the per-polygon
    smooth-shading flag `shade_smooth()` sets - both in Blender's own viewport/render and in
    whatever gets re-exported afterward. `shade_smooth()` alone was therefore close to a no-op on
    the actual rendered/exported result; welding coincident vertices was real (confirmed: ~5900
    removed) but bought little visually since the stale flat custom normals persisted regardless
    of the shared-vertex topology underneath them. Fixed by explicitly clearing the custom split
    normals (`mesh.customdata_custom_splitnormals_clear` before the final smooth-shading pass), so
    Blender actually computes and exports fresh smooth normals from the (now welded) topology.
    """
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.remove_doubles(threshold=0.0001)
    bpy.ops.mesh.normals_make_consistent(inside=False)
    bpy.ops.object.mode_set(mode="OBJECT")
    # Must run *after* leaving Edit Mode (it operates on the mesh's own custom-normals data
    # layer, not the BMesh edit-mode representation) and *before* shade_smooth(), which is
    # otherwise silently overridden by whatever custom split normals mc3togltf's own flat-normal
    # glTF export left behind on import - confirmed directly via mesh.has_custom_normals.
    # (Mesh.calc_normals_split() was removed in this Blender version's split-normals API rework -
    # not needed anyway, since the custom-normals layer is already populated from glTF import.)
    bpy.ops.mesh.customdata_custom_splitnormals_clear()
    bpy.ops.object.shade_smooth()
    assert not obj.data.has_custom_normals, (
        f"{obj.name}: custom split normals still present after customdata_custom_splitnormals_clear() - "
        "shade_smooth() below would still be silently overridden."
    )


def _capsule_transform(head_mc3, tail_mc3):
    """Given a bone's head/tail already in mc3's Y-up frame, returns (center, height,
    rotation_xyz_degrees) for a <capsule axis="y"> (mesh-craft's default capsule long-axis)
    aligned along head->tail. Rotation computed via shortest-arc quaternion (mirrors
    generate_body.add_cylinder_segment()'s own `rotation_difference` approach exactly, just
    operating in mc3's Y-up frame instead of Blender's Z-up one - Blender's glTF importer
    re-applies the same Y-up -> Z-up basis change uniformly to whatever rotation lands here, so
    computing it in mc3's own frame is what makes the imported result end up correctly aligned
    back in generate_skeleton.py's Z-up frame)."""
    head_v = mathutils.Vector(head_mc3)
    tail_v = mathutils.Vector(tail_mc3)
    direction = tail_v - head_v
    height = direction.length
    center = (head_v + tail_v) / 2
    default_axis = mathutils.Vector((0.0, 1.0, 0.0))  # capsule axis="y" default
    quat = default_axis.rotation_difference(direction)
    euler = quat.to_euler('XYZ')
    rotation_deg = (math.degrees(euler.x), math.degrees(euler.y), math.degrees(euler.z))
    return tuple(center), height, rotation_deg


def bones_to_mc3_xml(bones, height_scale=1.0, head_scale=1.0):
    """Returns a complete .mc3.xml document (str) authoring the body as a single real,
    watertight mesh: one <capsule> per non-Head bone (Head gets a <sphere>), all merged via a
    top-level CSG <union> instead of left as separate sibling objects.

    Task 7.5's own real finding: separate-but-visually-overlapping primitives (even properly
    rounded capsules, not the old cylinder+mismatched-sphere combo) still show dark
    self-intersection artifacts at every joint where 2+ segments converge - confirmed by direct
    visual inspection of the capsule-only prototype's own screenshots. A real CSG union (evaluated
    by mesh-craft's Manifold-backed exporter) produces one genuinely merged, watertight surface
    instead - the only way to eliminate this class of defect outright, not just shrink it.

    CSG's own documented cost - "UV coordinates are not real" on the evaluated result (see
    MC3_FORMAT.md) - is a non-issue for this specific asset: `CNAAvatarBody.png` is a solid 4x4
    white placeholder (Task 11.19), confirmed by direct pixel inspection to carry zero real
    per-pixel detail already; the avatar's actual skin color comes from a runtime tint
    (`AvatarAppearanceEXT::setSkinColorProperty`), not texture sampling, so losing real UVs here
    costs nothing visually.

    **Correction (2026-07-18, independent post-completion audit):** this docstring previously
    claimed CSG's flat, recomputed-per-face normals "suit this style" (a toy-like low-poly look).
    That was wrong in practice, not just in framing - flat per-triangle normals at CSG union seams
    render as near-black patches under this project's single fixed-direction lighting, not a cute
    faceted look. `build_body()` below now calls `_recalculate_smooth_normals()` on the imported
    mesh to fix this - see that function's own docstring for the full root-cause writeup."""
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<mc3 version="0.3" model="CNAAvatarBody" unit="meter" rotation_units="degrees" euler_order="XYZ">',
        '  <materials>',
        '    <material id="skin" roughness="0.85" metallic="0.0"><base_color>0.8 0.62 0.5 1.0</base_color></material>',
        '  </materials>',
        '  <objects>',
        '    <union name="CNAAvatarBody" material="skin">',
    ]
    for name, _parent, head, tail, _connected in bones:
        radius = BONE_RADII[name] * (head_scale if name == "Head" else height_scale)
        head_mc3 = _mc3_position(head)
        tail_mc3 = _mc3_position(tail)
        if name == "Head":
            center = tuple((mathutils.Vector(head_mc3) + mathutils.Vector(tail_mc3)) / 2)
            lines.append(
                f'      <sphere name={quoteattr(name + "_flesh")} radius="{radius:.6f}" '
                f'segments="16" position="{center[0]:.6f} {center[1]:.6f} {center[2]:.6f}"/>'
            )
            continue
        center, height, rot = _capsule_transform(head_mc3, tail_mc3)
        # Capsule height is the *cylindrical* portion length; the bone's own head->tail span
        # already includes the rounded end caps' worth of visual overlap at each joint -
        # subtracting 2*radius here would under-cover the joint, so deliberately left as the
        # full bone length (a real, deliberate overlap at each joint for the union below to
        # merge, not a gap).
        lines.append(
            f'      <capsule name={quoteattr(name + "_flesh")} radius="{radius:.6f}" height="{height:.6f}" '
            f'segments="12" axis="y" '
            f'position="{center[0]:.6f} {center[1]:.6f} {center[2]:.6f}" '
            f'rotation="{rot[0]:.4f} {rot[1]:.4f} {rot[2]:.4f}"/>'
        )
    lines.append('    </union>')
    lines.append('  </objects>')
    lines.append('</mc3>')
    return "\n".join(lines) + "\n"


def build_body(armature_obj, bones=None, height_scale=1.0, head_scale=1.0, mc3togltf_path=None):
    """Drop-in replacement for generate_body.build_body() - same signature/contract/return
    value, geometry authored via mesh-craft instead of bpy primitives (see module docstring)."""
    if bones is None:
        bones = generate_skeleton.BONES
    if mc3togltf_path is None:
        mc3togltf_path = _locate_mc3togltf()

    xml_doc = bones_to_mc3_xml(bones, height_scale=height_scale, head_scale=head_scale)

    with tempfile.TemporaryDirectory(prefix="cna_avatar_meshcraft_") as tmpdir:
        xml_path = Path(tmpdir) / "body.mc3.xml"
        glb_path = Path(tmpdir) / "body.glb"
        xml_path.write_text(xml_doc, encoding="utf-8")

        result = subprocess.run(
            [mc3togltf_path, str(xml_path), str(glb_path)],
            capture_output=True, text=True,
        )
        if result.returncode != 0 or not glb_path.is_file():
            raise RuntimeError(
                f"mc3togltf failed (exit {result.returncode}):\nstdout: {result.stdout}\nstderr: {result.stderr}"
            )

        existing = bpy.data.objects.get(BODY_NAME)
        if existing is not None:
            bpy.data.meshes.remove(existing.data, do_unlink=True)

        pre_import_objects = set(bpy.data.objects.keys())
        bpy.ops.import_scene.gltf(filepath=str(glb_path))
        imported = [obj for obj in bpy.data.objects if obj.name not in pre_import_objects and obj.type == 'MESH']

    if not imported:
        raise RuntimeError("mc3togltf export imported no mesh objects")

    bpy.ops.object.select_all(action="DESELECT")
    for obj in imported:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = imported[0]
    if len(imported) > 1:
        bpy.ops.object.join()
    body_obj = bpy.context.object
    body_obj.name = BODY_NAME
    body_obj.data.name = BODY_NAME
    # mc3togltf's exported glTF nodes carry their own position (already baked into vertex
    # positions on import by Blender's own glTF importer's convention for non-skinned meshes) -
    # any residual object-level transform should be identity; apply defensively so armature
    # parenting below isn't affected by an unexpected residual transform.
    bpy.ops.object.select_all(action="DESELECT")
    body_obj.select_set(True)
    bpy.context.view_layer.objects.active = body_obj
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

    # Weld CSG's duplicated coincident vertices and recompute smooth normals - see this
    # function's own docstring for why (flat per-triangle normals at union seams render as
    # near-black patches, not a toy-like faceted look).
    _recalculate_smooth_normals(body_obj)

    bpy.ops.object.select_all(action="DESELECT")
    body_obj.select_set(True)
    armature_obj.select_set(True)
    bpy.context.view_layer.objects.active = armature_obj
    bpy.ops.object.parent_set(type="ARMATURE_AUTO")
    # Task 7.5: blend_radius widened from generate_body.py's flat 0.08 (confirmed, via the Task
    # 7.1 Wave-pose screenshot, insufficient against ring artifacts at every joint) to a
    # per-joint radius derived from the *thicker* new BONE_RADII above, so the blend region
    # scales with how fat the limb actually is at that joint instead of a one-size-fits-all
    # constant.
    avg_radius = sum(BONE_RADII.values()) / len(BONE_RADII)
    generate_body.fix_automatic_weights(body_obj, bones, blend_radius=avg_radius * 1.6)

    return body_obj


if __name__ == "__main__":
    armature_obj = generate_skeleton.build_skeleton()
    body_obj = build_body(armature_obj)

    bone_names = {name for name, *_rest in generate_skeleton.BONES}
    group_names = {g.name for g in body_obj.vertex_groups}
    missing = bone_names - group_names

    print(f"Built body '{body_obj.name}' with {len(body_obj.data.vertices)} vertices, "
          f"{len(body_obj.vertex_groups)} vertex groups.")
    if missing:
        print(f"WARNING: no vertex group for bones: {sorted(missing)}")
    assert not missing, f"automatic weights produced no vertex group for: {sorted(missing)}"
    print("OK: every skeleton bone has a corresponding vertex group on the body mesh.")

    zero_weight = [v.index for v in body_obj.data.vertices if sum(g.weight for g in v.groups) < 1e-6]
    over_limit = [v.index for v in body_obj.data.vertices if len(v.groups) > 4]
    print(f"Zero-weight vertices: {len(zero_weight)}. Over-4-influence vertices: {len(over_limit)}.")
    assert not zero_weight, f"zero-weight vertices remain: {zero_weight[:10]}"
    assert not over_limit, f"over-4-influence vertices remain: {over_limit[:10]}"
    print("OK: no zero-weight or over-4-influence vertices remain on the body mesh.")
