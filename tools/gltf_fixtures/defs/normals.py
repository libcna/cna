# SPDX-License-Identifier: MS-PL
"""Normal / tangent fixtures -- owning group ``normals`` (plan_gltf.md §24.2's "Normals / tangents").

Specification: §3.7.2.1 ``meshes-overview`` -- "When normals are not specified, client
implementations MUST calculate flat normals and the provided tangents are ignored."
"""

from __future__ import annotations

import math

from ..builder import TRIANGLES, UNSIGNED_SHORT, GltfBuilder
from ..manifest import Fixture, l3_primitive, world_positions

#: A triangle deliberately NOT in the XY plane. Every other normal-less fixture in the corpus is
#: planar and CCW, so its computed face normal is exactly the fabricated ``(0,0,1)`` CNA used to
#: write -- which means none of them can tell the two apart. This one can: the cross product of
#: ``(1,0,0)`` and ``(0,1,1)`` is ``(0,-1,1)``.
_TILTED_POSITIONS = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 1.0)]
_INV_SQRT2 = 1.0 / math.sqrt(2.0)
#: What §3.7.2.1 requires a reader to produce for `_TILTED_POSITIONS`: the normalized face normal,
#: identical on all three vertices because a single triangle shares no vertex with anything.
_FLAT_NORMAL = (0.0, -_INV_SQRT2, _INV_SQRT2)
_EXPECTED_NORMALS = [_FLAT_NORMAL] * 3


def normal_absent() -> Fixture:
    """A primitive with no ``NORMAL``. Owns **`GLTF-173`**.

    §3.7.2.1 makes calculating flat normals a **MUST**, and CNA wrote a fabricated ``(0,0,1)`` on
    every vertex instead -- a surface facing +Z regardless of where it actually points, so a model
    lit from any other direction was uniformly and silently wrong.

    The tilt is the whole design. A planar CCW triangle's own face normal *is* ``(0,0,1)``, so every
    other normal-less asset in this corpus passes identically under both behaviours and none of them
    could ever have caught this. Here the correct answer is ``(0, -1/√2, 1/√2)``, which the old
    behaviour cannot produce by accident.

    One triangle, so no vertex is shared between faces: the area-weighted vertex normal and the flat
    normal coincide exactly, and the fixture asserts the *exact* spec answer rather than an
    approximation of it.
    """
    b = GltfBuilder("normal-absent")
    position = b.add_packed_accessor(usage="POSITION", values=_TILTED_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    indices = b.add_packed_accessor(usage="indices", values=[0, 1, 2], accessor_type="SCALAR",
                                    component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="TiltedTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="normal-absent", audit_fixture=None, owning_group="normals",
        description="A triangle with no NORMAL attribute, tilted out of the XY plane so its "
                    "computed flat normal (0, -1/sqrt2, 1/sqrt2) cannot be confused with the "
                    "fabricated (0,0,1) CNA used to write. The one fixture in the corpus that can "
                    "tell §3.7.2.1's MUST from the old default at all.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["absent NORMAL", "computed flat normals", "non-planar triangle"],
        spec_anchors=["meshes-overview"],
        # L2 lists only what the file authors -- there is no NORMAL accessor to dump. L3 states the
        # normals the importer must DERIVE, which is exactly the layer where a derived value
        # belongs: L2 is what the bytes say, L3 is what the mesh means.
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="TiltedTri", primitive=0, mode=TRIANGLES,
            positions=_TILTED_POSITIONS, normals=_EXPECTED_NORMALS, indices=[0, 1, 2])]},
        l4=world_positions(b, {mesh: list(_TILTED_POSITIONS)}),
    )


FIXTURES = [normal_absent]
