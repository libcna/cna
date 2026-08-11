# SPDX-License-Identifier: MS-PL
"""Scene fixtures -- owning group ``scenes`` (plan_gltf.md §24.2).

Locks behaviour the forensic audit **verified correct**: when ``scene`` names a scene other than
zero, only that scene's nodes are imported.

Specification: §3.5 ``scenes``.
"""

from __future__ import annotations

from ..builder import TRIANGLES, UNSIGNED_SHORT, GltfBuilder
from ..manifest import Fixture, l3_primitive, world_positions
from .common import TRIANGLE_INDICES, TRIANGLE_NORMALS, TRIANGLE_POSITIONS

#: The real mesh sits five units along +Z so a decoy import is impossible to mistake for the
#: intended one at any layer.
_REAL_POSITIONS = [(0.0, 0.0, 5.0), (1.0, 0.0, 5.0), (0.0, 1.0, 5.0)]


def scene_default_selection() -> Fixture:
    """f14 -- two scenes with ``scene: 1``. **Verified correct.**"""
    b = GltfBuilder("scene-default-selection")
    decoy_position = b.add_packed_accessor(usage="POSITION (decoy)", values=TRIANGLE_POSITIONS,
                                           accessor_type="VEC3", with_bounds=True)
    decoy_normal = b.add_packed_accessor(usage="NORMAL (decoy)", values=TRIANGLE_NORMALS,
                                         accessor_type="VEC3")
    decoy_indices = b.add_packed_accessor(usage="indices (decoy)", values=TRIANGLE_INDICES,
                                          accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    real_position = b.add_packed_accessor(usage="POSITION", values=_REAL_POSITIONS,
                                          accessor_type="VEC3", with_bounds=True)
    real_normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS,
                                        accessor_type="VEC3")
    real_indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                         accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    decoy_mesh = b.add_mesh([{
        "attributes": {"POSITION": decoy_position, "NORMAL": decoy_normal},
        "indices": decoy_indices,
        "mode": TRIANGLES,
    }], name="DecoyMesh")
    real_mesh = b.add_mesh([{
        "attributes": {"POSITION": real_position, "NORMAL": real_normal},
        "indices": real_indices,
        "mode": TRIANGLES,
    }], name="RealMesh")
    decoy_node = b.add_node(name="DecoyNode", mesh=decoy_mesh)
    real_node = b.add_node(name="RealNode", mesh=real_mesh)
    b.add_scene([decoy_node], name="DecoyScene")
    b.add_scene([real_node], name="RealScene")
    b.set_default_scene(1)
    return Fixture(
        id="scene-default-selection", audit_fixture="f14", owning_group="scenes",
        description="Two scenes, with 'scene' naming the second. Only RealMesh may be imported; "
                    "DecoyMesh belongs to scene 0 and must be excluded entirely. Verified correct "
                    "on the campaign baseline.",
        builder=b, validated_layers=["L1", "L3", "L4"],
        features=["scene != 0", "unreferenced decoy mesh", "multiple scenes"],
        spec_anchors=["scenes"],
        l3={"primitives": [l3_primitive(
            mesh=real_mesh, mesh_name="RealMesh", primitive=0, mode=TRIANGLES,
            positions=_REAL_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES)],
            "importedMeshes": [real_mesh],
            "excludedMeshes": [decoy_mesh]},
        l4=world_positions(b, {real_mesh: list(_REAL_POSITIONS),
                               decoy_mesh: list(TRIANGLE_POSITIONS)}),
    )


FIXTURES = [scene_default_selection]
