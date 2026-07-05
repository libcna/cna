#!/usr/bin/env python3
"""Sanity-checks a CNA procedural avatar `.glb` (as exported by generate_avatar.py, Task
11.7) using `pygltflib` — non-Blender, plain `python3` (this is the automated
"does the export actually contain what it should" gate; nothing here launches Blender).

Checks, all of which must pass — fails loudly (nonzero exit, clear message) on any
miss, rather than silently accepting a hollow/broken export:

  1. At least one non-empty mesh (a POSITION accessor with a nonzero vertex count).
  2. A skin whose joints include every one of Task 11.1's 19 canonical bone names
     (imported from generate_skeleton.BONES, not duplicated here — see that module's
     docstring for why plain `python3` can import it). Extra joints beyond those 19
     (e.g. the `neutral_bone` Blender's exporter adds for zero-weight vertices — see
     README.md) are allowed and reported, not treated as a failure.
  3. `Stand0`/`Wave` (Task 11.6) and `Stand1`/`Clap`/`Celebrate` (Task 11.15) animations
     are all present.
  4. Both the `Smile` and `Blink` shape keys (glTF morph targets) are present on some
     mesh (Task 11.4) — checked via that mesh's `extras["targetNames"]`, which is where
     Blender's glTF exporter records shape-key names for a mesh's morph targets.

Usage:

    python3 validate_gltf.py path/to/avatar.glb

Requires: pygltflib (pip install pygltflib) — already a project dependency (see
tools/avatar_asset_pipeline/convert_avatar.py for prior usage).
"""

import sys
from pathlib import Path

try:
    import pygltflib
except ImportError:
    sys.exit("This script requires pygltflib: pip install pygltflib")

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_skeleton  # noqa: E402  (bpy-free: see that module's docstring)

REQUIRED_BONE_NAMES = {name for name, *_rest in generate_skeleton.BONES}
REQUIRED_ANIMATIONS = {"Stand0", "Wave", "Stand1", "Clap", "Celebrate"}
REQUIRED_SHAPE_KEYS = {"Smile", "Blink"}


class ValidationError(Exception):
    pass


def _check_nonempty_mesh(gltf):
    for mesh in gltf.meshes:
        for primitive in mesh.primitives:
            if primitive.attributes.POSITION is None:
                continue
            accessor = gltf.accessors[primitive.attributes.POSITION]
            if accessor.count > 0:
                return
    raise ValidationError("no mesh with a non-empty POSITION accessor found")


def _check_skin_joints(gltf):
    if not gltf.skins:
        raise ValidationError("no skin found in the file")

    joint_names = set()
    for skin in gltf.skins:
        for joint_index in skin.joints:
            joint_names.add(gltf.nodes[joint_index].name)

    missing = REQUIRED_BONE_NAMES - joint_names
    if missing:
        raise ValidationError(f"skin is missing joints for bones: {sorted(missing)}")

    extra = joint_names - REQUIRED_BONE_NAMES
    if extra:
        print(f"  (info) extra joints beyond the 19 canonical bones: {sorted(extra)} "
              f"(e.g. a synthetic neutral_bone for zero-weight vertices — expected, see README.md)")


def _check_animations(gltf):
    names = {a.name for a in gltf.animations}
    missing = REQUIRED_ANIMATIONS - names
    if missing:
        raise ValidationError(f"missing animations: {sorted(missing)} (found: {sorted(names)})")


def _check_shape_keys(gltf):
    all_target_names = set()
    for mesh in gltf.meshes:
        target_names = (mesh.extras or {}).get("targetNames", [])
        all_target_names.update(target_names)

    missing = REQUIRED_SHAPE_KEYS - all_target_names
    if missing:
        raise ValidationError(f"missing shape keys (morph targets): {sorted(missing)}")


CHECKS = (
    ("non-empty mesh", _check_nonempty_mesh),
    ("skin/joint names", _check_skin_joints),
    ("Stand0/Stand1/Wave/Clap/Celebrate animations", _check_animations),
    ("Smile/Blink shape keys", _check_shape_keys),
)


def validate(path):
    """Runs all checks against the glTF/GLB file at `path`. Raises ValidationError (with
    a message naming exactly what's missing) on the first failing check, including if
    the file itself isn't a loadable glTF/GLB at all; prints a one line per-check
    summary as it goes."""
    try:
        gltf = pygltflib.GLTF2().load(str(path))
    except Exception as e:
        raise ValidationError(f"not a loadable glTF/GLB file: {e}") from e

    for label, check in CHECKS:
        check(gltf)
        print(f"  [OK] {label}")
    return gltf


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(f"Usage: python3 {Path(__file__).name} path/to/avatar.glb")

    path = Path(sys.argv[1])
    if not path.is_file():
        sys.exit(f"File not found: {path}")

    print(f"Validating {path} ...")
    try:
        gltf = validate(path)
    except ValidationError as e:
        sys.exit(f"FAIL: {e}")

    bone_count = len({gltf.nodes[j].name for s in gltf.skins for j in s.joints} & REQUIRED_BONE_NAMES)
    print(f"OK: {path.name} — {bone_count}/{len(REQUIRED_BONE_NAMES)} canonical bones, "
          f"animations {sorted({a.name for a in gltf.animations})}, "
          f"shape keys {sorted(REQUIRED_SHAPE_KEYS)}.")
