#!/usr/bin/env python3
"""Adds CNA's placeholder facial shape keys — `Smile` and `Blink` — to the Task 11.2
body mesh (`CNAAvatarBody`).

The current body has no separate eye/mouth geometry (Task 11.2's head is a single
low-poly UV sphere), so vertex selection for each morph is done purely by world-space
position relative to the head sphere's own center/radius (see `HEAD_CENTER`/`HEAD_RADIUS`
below, kept in sync with generate_body.py's Head placement) — a "mouth band" and an "eye
band" picked by height (Z) and how far forward (+Y) a vertex is. This is a crude,
explicitly placeholder approximation, not a modeled face; see the "Adding more morphs"
section in README.md for how a later iteration with real facial geometry should replace
this vertex-selection approach entirely rather than trying to refine it in place.

Offline, one-time content-authoring tool — not part of the C++ build, never run by CNA
at runtime. Run headless via Blender:

    blender --background --python generate_morphs.py

`generate_avatar.py` (Task 11.7) will import build_morphs() from this module and call it
in the same Blender process as the skeleton/body/material builders, rather than
re-deriving the shape keys.
"""

import sys
from pathlib import Path

try:
    import bpy
    import mathutils
except ImportError:
    sys.exit("This script must be run inside Blender: blender --background --python generate_morphs.py")

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_skeleton  # noqa: E402  (bpy path setup must happen first)
import generate_body  # noqa: E402
import generate_materials  # noqa: E402

# Head sphere placement, mirrored from generate_body.py's Head handling (center is the
# midpoint of the Head bone's head/tail, radius from BONE_RADII["Head"]).
_HEAD_HEAD, _HEAD_TAIL = next(
    (head, tail) for name, _parent, head, tail, _connected in generate_skeleton.BONES if name == "Head"
)
HEAD_CENTER = mathutils.Vector(_HEAD_HEAD).lerp(mathutils.Vector(_HEAD_TAIL), 0.5)
HEAD_RADIUS = generate_body.BONE_RADII["Head"]


def _select_by_world_position(body_obj, predicate):
    matrix = body_obj.matrix_world
    return [v.index for v in body_obj.data.vertices if predicate(matrix @ v.co)]


def _ensure_basis(body_obj):
    if body_obj.data.shape_keys is None:
        body_obj.shape_key_add(name="Basis", from_mix=False)


def _add_shape_key(body_obj, name, indices, displacement_fn):
    """Adds (or replaces) a shape key called `name`, applying displacement_fn(local_offset)
    -> Vector to every vertex in `indices`, where local_offset is that vertex's rest
    position relative to HEAD_CENTER (in world axes)."""
    existing = body_obj.data.shape_keys.key_blocks.get(name) if body_obj.data.shape_keys else None
    if existing is not None:
        body_obj.shape_key_remove(existing)

    key = body_obj.shape_key_add(name=name, from_mix=False)
    matrix = body_obj.matrix_world
    matrix_inv = matrix.inverted()
    for i in indices:
        world_co = matrix @ key.data[i].co
        offset = world_co - HEAD_CENTER
        key.data[i].co = matrix_inv @ (world_co + displacement_fn(offset))
    return key


def build_morphs(body_obj):
    """Adds `Smile` and `Blink` shape keys to `body_obj` (in addition to the implicit
    `Basis`). Safe to call repeatedly in the same Blender session — replaces existing
    shape keys of the same name rather than stacking duplicates."""
    _ensure_basis(body_obj)

    # The head is a UV sphere (generate_body.py, segments=8/ring_count=6), so its
    # vertices sit on a fixed set of latitude rings at z/radius in {0, +-0.5, +-0.866,
    # +-1.0} regardless of the actual radius. Select by ratio (with tolerance), not an
    # absolute z range, so this keeps working if HEAD_RADIUS ever changes.
    def _at_latitude_ratio(offset, ratio, tolerance=0.15):
        return abs((offset.z / HEAD_RADIUS) - ratio) < tolerance

    # Mouth band: the front-facing vertices of the first ring below the equator.
    mouth_indices = _select_by_world_position(
        body_obj,
        lambda co: (co - HEAD_CENTER).y > HEAD_RADIUS * 0.3
        and _at_latitude_ratio(co - HEAD_CENTER, -0.5),
    )

    def _smile_displacement(offset):
        # Corners (large |x|) lift and pull back further than the center, approximating
        # the corners-up shape of a smile on this single-sphere head.
        lift = HEAD_RADIUS * 0.25 * (abs(offset.x) / HEAD_RADIUS)
        return mathutils.Vector((0.0, -HEAD_RADIUS * 0.10, lift))

    smile = _add_shape_key(body_obj, "Smile", mouth_indices, _smile_displacement)

    # Eye band: the front-facing vertices of the first ring above the equator.
    eye_indices = _select_by_world_position(
        body_obj,
        lambda co: (co - HEAD_CENTER).y > HEAD_RADIUS * 0.3
        and _at_latitude_ratio(co - HEAD_CENTER, 0.5),
    )

    def _blink_displacement(_offset):
        # Flattens the eye band downward/inward, approximating closed eyelids.
        return mathutils.Vector((0.0, -HEAD_RADIUS * 0.08, -HEAD_RADIUS * 0.30))

    blink = _add_shape_key(body_obj, "Blink", eye_indices, _blink_displacement)

    return {"Smile": smile, "Blink": blink}


def _max_displacement(key, basis):
    return max((key.data[i].co - basis.data[i].co).length for i in range(len(key.data)))


if __name__ == "__main__":
    armature_obj = generate_skeleton.build_skeleton()
    body_obj = generate_body.build_body(armature_obj)
    materials = generate_materials.build_materials()
    generate_materials.assign_body_material(body_obj, materials)
    morphs = build_morphs(body_obj)

    basis = body_obj.data.shape_keys.key_blocks["Basis"]
    key_names = {kb.name for kb in body_obj.data.shape_keys.key_blocks}
    print(f"Shape keys on '{body_obj.name}': {sorted(key_names)}")
    for name, key in morphs.items():
        print(f"  {name}: max vertex displacement from Basis = {_max_displacement(key, basis):.4f} m")

    assert {"Smile", "Blink"}.issubset(key_names), "missing required shape keys"
    for name, key in morphs.items():
        assert _max_displacement(key, basis) > 1e-4, f"{name} shape key has no effective displacement"
    print("OK: Smile and Blink shape keys exist and displace at least one vertex.")
