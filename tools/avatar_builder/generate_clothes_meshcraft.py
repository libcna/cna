#!/usr/bin/env python3
"""Task 7.6 (plans/plan_net.md Phase 7): builds CNA's placeholder clothes (Shirt/Pants/Shoes) via
mesh-craft CSG union, exactly mirroring generate_body_meshcraft.py's own fix for the body -
same root cause (generate_clothes.py's `_build_garment` only `bpy.ops.object.join()`s separate
cylinder+joint-sphere primitives per bone, never a real weld), same fix (one real, watertight
`<union>`-merged mesh per garment instead).

Drop-in replacement for generate_clothes.build_clothes() - same signature/contract/return value
(a {slot: mesh_object} dict), same GARMENT_STYLES/DEFAULT_STYLES tables reused unchanged (which
bones a style covers doesn't need to change, only how each covered bone's shell is authored).

Offline, one-time content-authoring tool - not part of the C++ build, never run by CNA at
runtime. Requires an already-built `mc3togltf` binary (see generate_body_meshcraft.py's own
_locate_mc3togltf for path resolution)."""

import sys
import tempfile
from pathlib import Path
from xml.sax.saxutils import quoteattr

try:
    import bpy
    import mathutils
except ImportError:
    sys.exit("This script must be run inside Blender: blender --background --python generate_clothes_meshcraft.py")

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_skeleton  # noqa: E402
import generate_body  # noqa: E402  (BONE_RADII, fix_automatic_weights - the *original*, bpy-only
# module: garment shell radii still key off its BONE_RADII table, independent of whichever body
# generator produced the skin underneath a given shell)
import generate_body_meshcraft  # noqa: E402  (_mc3_position, _capsule_transform, _locate_mc3togltf)
import generate_clothes  # noqa: E402  (GARMENT_STYLES, DEFAULT_STYLES, NAME_PREFIX - reused as-is)
import generate_materials  # noqa: E402

import subprocess  # noqa: E402

# Re-exported so generate_avatar.py's `import generate_clothes_meshcraft as generate_clothes`
# alias (matching generate_body_meshcraft's own aliasing convention) can keep referencing
# generate_clothes.GARMENT_STYLES/DEFAULT_STYLES for its --shirt-style/--pants-style argparse
# choices without any further changes - which bones/styles exist doesn't change between the two
# generators, only how a covered bone's shell geometry gets authored.
GARMENT_STYLES = generate_clothes.GARMENT_STYLES
DEFAULT_STYLES = generate_clothes.DEFAULT_STYLES
NAME_PREFIX = generate_clothes.NAME_PREFIX


def _garment_to_mc3_xml(garment_name, bone_names, padding, bones_by_name, height_scale):
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<mc3 version="0.3" model={quoteattr(garment_name)} unit="meter" rotation_units="degrees" euler_order="XYZ">',
        '  <materials>',
        '    <material id="cloth" roughness="0.9" metallic="0.0"><base_color>0.5 0.5 0.6 1.0</base_color></material>',
        '  </materials>',
        '  <objects>',
        f'    <union name={quoteattr(garment_name)} material="cloth">',
    ]
    for bone_name in bone_names:
        head, tail = bones_by_name[bone_name]
        # Task 7.6 fix-up: sized against the *new*, thicker generate_body_meshcraft.BONE_RADII
        # (not the original, thinner generate_body.BONE_RADII a garment shell used to layer over)
        # - the body underneath was substantially thickened by Task 7.5's own proportion fix, and
        # a garment sized against the old, thinner radii table would undersize relative to it,
        # letting the body poke through instead of the garment properly covering it.
        # GARMENT_STYLES's own padding constants (~0.02m) were tuned against the *old*, thinner
        # body - applied verbatim against the new, thicker radii they read almost invisibly thin
        # (confirmed by direct visual inspection: the shirt became a barely-visible sliver at the
        # collar instead of a clearly readable layer). Scaled up here so a garment's *relative*
        # size-over-the-body stays about what it always was, not just technically non-overlapping.
        radius = generate_body_meshcraft.BONE_RADII[bone_name] * height_scale + padding * 1.8
        head_mc3 = generate_body_meshcraft._mc3_position(head)
        tail_mc3 = generate_body_meshcraft._mc3_position(tail)
        center, height, rot = generate_body_meshcraft._capsule_transform(head_mc3, tail_mc3)
        lines.append(
            f'      <capsule name={quoteattr(bone_name + "_shell")} radius="{radius:.6f}" height="{height:.6f}" '
            f'segments="12" axis="y" '
            f'position="{center[0]:.6f} {center[1]:.6f} {center[2]:.6f}" '
            f'rotation="{rot[0]:.4f} {rot[1]:.4f} {rot[2]:.4f}"/>'
        )
    lines.append('    </union>')
    lines.append('  </objects>')
    lines.append('</mc3>')
    return "\n".join(lines) + "\n"


def _build_garment_meshcraft(garment_name, bone_names, padding, bones_by_name, height_scale, mc3togltf_path):
    xml_doc = _garment_to_mc3_xml(garment_name, bone_names, padding, bones_by_name, height_scale)

    with tempfile.TemporaryDirectory(prefix="cna_avatar_meshcraft_") as tmpdir:
        xml_path = Path(tmpdir) / "garment.mc3.xml"
        glb_path = Path(tmpdir) / "garment.glb"
        xml_path.write_text(xml_doc, encoding="utf-8")

        result = subprocess.run([mc3togltf_path, str(xml_path), str(glb_path)], capture_output=True, text=True)
        if result.returncode != 0 or not glb_path.is_file():
            raise RuntimeError(f"mc3togltf failed for {garment_name} (exit {result.returncode}):\n{result.stderr}")

        pre_import = set(bpy.data.objects.keys())
        bpy.ops.import_scene.gltf(filepath=str(glb_path))
        imported = [o for o in bpy.data.objects if o.name not in pre_import and o.type == 'MESH']

    if not imported:
        raise RuntimeError(f"mc3togltf export imported no mesh objects for {garment_name}")

    bpy.ops.object.select_all(action="DESELECT")
    for obj in imported:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = imported[0]
    if len(imported) > 1:
        bpy.ops.object.join()
    obj = bpy.context.object
    obj.name = garment_name
    obj.data.name = garment_name
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    # Post-plans/plan_net.md remediation (2026-07-18): same flat-CSG-normal fix as
    # generate_body_meshcraft.py's own build_body() - see
    # generate_body_meshcraft._recalculate_smooth_normals's docstring for the root cause.
    generate_body_meshcraft._recalculate_smooth_normals(obj)
    return obj


def build_clothes(armature_obj, materials, bones=None, height_scale=1.0, styles=None, mc3togltf_path=None):
    """Drop-in replacement for generate_clothes.build_clothes() - see module docstring."""
    if bones is None:
        bones = generate_skeleton.BONES
    if mc3togltf_path is None:
        mc3togltf_path = generate_body_meshcraft._locate_mc3togltf()
    bones_by_name = {name: (head, tail) for name, _parent, head, tail, _connected in bones}
    resolved_styles = {**generate_clothes.DEFAULT_STYLES, **(styles or {})}

    # Derived exactly the same way generate_body_meshcraft.build_body derives its own, so the
    # two cannot drift apart again - see the fix_automatic_weights call below for why they must
    # match.
    _radii = generate_body_meshcraft.BONE_RADII
    body_blend_radius = (sum(_radii.values()) / len(_radii)) * 1.6

    garment_objs = {}
    for garment_slot, style_name in resolved_styles.items():
        bone_names, padding, material_part = generate_clothes.GARMENT_STYLES[garment_slot][style_name]
        full_name = f"{generate_clothes.NAME_PREFIX}{garment_slot}"

        existing = bpy.data.objects.get(full_name)
        if existing is not None:
            bpy.data.meshes.remove(existing.data, do_unlink=True)

        obj = _build_garment_meshcraft(full_name, bone_names, padding, bones_by_name, height_scale, mc3togltf_path)

        obj.data.materials.clear()
        obj.data.materials.append(materials[material_part])

        bpy.ops.object.select_all(action="DESELECT")
        obj.select_set(True)
        armature_obj.select_set(True)
        bpy.context.view_layer.objects.active = armature_obj
        bpy.ops.object.parent_set(type="ARMATURE_AUTO")
        # audit_net.md remediation (2026-07-18, fourth round): pass the SAME blend_radius the
        # body itself is skinned with (generate_body_meshcraft.build_body's own
        # `avg_radius * 1.6`), instead of silently taking fix_automatic_weights' 0.08 default.
        # A garment skinned with a different joint-blend width than the body it covers deforms
        # differently from that body at every animated joint - so under a pose like `Wave` the
        # two surfaces cross, and the render alternates between garment and skin along a
        # low-poly intersection curve. That is the real source of the "dark blotches" the audit
        # reported across the Wave-pose torso/shoulder: measured here as body=0.1474 vs
        # garment=0.08, an ~84% mismatch, which is largest exactly at the wide-radius torso and
        # shoulder joints where the artifact was worst.
        generate_body.fix_automatic_weights(obj, bones, blend_radius=body_blend_radius)

        garment_objs[garment_slot] = obj

    return garment_objs


if __name__ == "__main__":
    armature_obj = generate_skeleton.build_skeleton()
    body_obj = generate_body_meshcraft.build_body(armature_obj)
    materials = generate_materials.build_materials()
    generate_materials.assign_body_material(body_obj, materials)
    garments = build_clothes(armature_obj, materials)

    for slot, obj in garments.items():
        bone_names, _padding, material_part = generate_clothes.GARMENT_STYLES[slot][generate_clothes.DEFAULT_STYLES[slot]]
        covered_bones = set(bone_names)
        group_names = {g.name for g in obj.vertex_groups}
        missing = covered_bones - group_names
        print(f"Built '{obj.name}' with {len(obj.data.vertices)} vertices, "
              f"{len(obj.vertex_groups)} vertex groups, material "
              f"{obj.data.materials[0].name if obj.data.materials else '(none)'}.")
        if missing:
            print(f"WARNING: no vertex group for bones: {sorted(missing)}")
        assert not missing, f"{slot}: automatic weights missing groups for {sorted(missing)}"
        assert obj.data.materials and obj.data.materials[0].name == materials[material_part].name

        zero_weight = [v.index for v in obj.data.vertices if sum(g.weight for g in v.groups) < 1e-6]
        over_limit = [v.index for v in obj.data.vertices if len(v.groups) > 4]
        assert not zero_weight, f"{slot}: zero-weight vertices remain: {zero_weight[:10]}"
        assert not over_limit, f"{slot}: over-4-influence vertices remain: {over_limit[:10]}"

    print(f"OK: {', '.join(garments)} exist, each vertex-grouped to its covered bones "
          f"with the correct material assigned, no zero-weight or over-4-influence vertices.")
