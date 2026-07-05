#!/usr/bin/env python3
"""Exports a set of CNA avatar objects (armature + meshes) to a glTF2 `.glb` file via
Blender's built-in glTF2 exporter.

Offline, one-time content-authoring tool — not part of the C++ build, never run by CNA
at runtime. `generate_avatar.py` (Task 11.7) is the only caller; kept in its own module
so the export step's chosen options live in one place rather than being duplicated if a
future script needs to export a different object set.
"""

import sys
from pathlib import Path

try:
    import bpy
except ImportError:
    sys.exit("This script must be run inside Blender: blender --background --python export_gltf.py")


def export_avatar(output_path, objects):
    """Selects exactly `objects` (an iterable of bpy.types.Object — the armature and
    its mesh children) and exports them to `output_path` (a `.glb` file) via Blender's
    glTF2 exporter, with animations (one glTF animation per Action), morph targets
    (shape keys), and skinning included. Returns the resolved output Path."""
    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)

    bpy.ops.export_scene.gltf(
        filepath=str(output_path),
        export_format="GLB",
        use_selection=True,
        export_animation_mode="ACTIONS",
        export_animations=True,
        export_morph=True,
        export_skins=True,
        export_yup=True,
    )
    return output_path
