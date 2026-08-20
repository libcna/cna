#!/usr/bin/env python3
"""Builds CNA's minimal placeholder avatar materials — one simple base-color Principled
BSDF material per body part (skin, hair, shirt, pants, shoes). No texture painting at
this stage; each material is a flat, project-chosen base color, meant to be replaced or
texture-painted in a later iteration (see plans/plan_net.md Phase 11c, Task 11.13+).

Only the `Skin` material is assigned anywhere right now — `CNAAvatarBody` (the only mesh
that exists so far, Task 11.2). `Hair`/`Shirt`/`Pants`/`Shoes` are created and returned
for `generate_hair.py`/`generate_clothes.py` (Task 11.5) to assign to the mesh objects
they create; there is no hair/clothing geometry to assign them to yet.

Offline, one-time content-authoring tool — not part of the C++ build, never run by CNA
at runtime. Run headless via Blender:

    blender --background --python generate_materials.py

`generate_avatar.py` (Task 11.7) will import build_materials()/assign_body_material()
from this module and call them in the same Blender process as
generate_skeleton.build_skeleton() and generate_body.build_body(), rather than
re-deriving the materials.
"""

import sys
from pathlib import Path

try:
    import bpy
except ImportError:
    sys.exit("This script must be run inside Blender: blender --background --python generate_materials.py")

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_skeleton  # noqa: E402  (bpy path setup must happen first)
import generate_body  # noqa: E402

NAME_PREFIX = "CNAAvatar"

# Part name -> flat placeholder base color (RGBA, linear). Deliberately simple, no
# texture maps — swap for real textures/tints in a later iteration.
MATERIAL_COLORS = {
    "Skin": (0.80, 0.60, 0.46, 1.0),
    "Hair": (0.25, 0.15, 0.08, 1.0),
    "Shirt": (0.20, 0.35, 0.60, 1.0),
    "Pants": (0.15, 0.15, 0.20, 1.0),
    # audit_net.md remediation (2026-07-18): was (0.05, 0.05, 0.05) - dark enough that no
    # amount of ambient/diffuse light could make joint/seam shading read as anything but
    # a featureless black blob (0.05 base albedo * any realistic light contribution stays
    # under ~15/255). Confirmed via direct pixel sampling that neither the AvatarRenderer
    # ambient-light fix nor the LowerLeg/Foot bend-joint weight fix changed a single foot
    # pixel - this flat base color was the actual, sole cause of "shoes" reading as solid
    # black regardless of lighting or skinning changes. Raised enough to still read as a
    # dark shoe, clearly darker than Pants, while no longer crushing shading detail to 0.
    "Shoes": (0.14, 0.14, 0.16, 1.0),
}


def build_materials():
    """Creates (or reuses, if already present in bpy.data) one Principled-BSDF material
    per part in MATERIAL_COLORS and returns a {part_name: bpy.types.Material} dict. Safe
    to call repeatedly in the same Blender session."""
    materials = {}
    for part, color in MATERIAL_COLORS.items():
        name = f"{NAME_PREFIX}{part}"
        mat = bpy.data.materials.get(name)
        if mat is None:
            mat = bpy.data.materials.new(name)
        mat.use_nodes = True
        mat.diffuse_color = color
        bsdf = mat.node_tree.nodes.get("Principled BSDF")
        if bsdf is not None:
            bsdf.inputs["Base Color"].default_value = color
        materials[part] = mat
    return materials


def assign_body_material(body_obj, materials):
    """Assigns the `Skin` material as the body mesh's sole material slot."""
    body_obj.data.materials.clear()
    body_obj.data.materials.append(materials["Skin"])
    for polygon in body_obj.data.polygons:
        polygon.material_index = 0


if __name__ == "__main__":
    armature_obj = generate_skeleton.build_skeleton()
    body_obj = generate_body.build_body(armature_obj)
    materials = build_materials()
    assign_body_material(body_obj, materials)

    missing = set(MATERIAL_COLORS) - set(materials)
    print(f"Built {len(materials)} materials: {sorted(materials)}")
    print(f"Body mesh material slots: {[m.name for m in body_obj.data.materials]}")
    assert not missing, f"failed to create materials: {sorted(missing)}"
    assert len(body_obj.data.materials) == 1
    assert body_obj.data.materials[0].name == materials["Skin"].name
    print("OK: all 5 placeholder materials exist and Skin is assigned to the body mesh.")
