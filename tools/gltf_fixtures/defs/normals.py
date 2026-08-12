# SPDX-License-Identifier: MS-PL
"""Normal / tangent fixtures -- owning group ``normals`` (plan_gltf.md §24.2's "Normals / tangents").

Specification: §3.7.2.1 ``meshes-overview`` -- "When normals are not specified, client
implementations MUST calculate flat normals and the provided tangents are ignored."
"""

from __future__ import annotations

import math

from ..builder import SHORT, TRIANGLES, UNSIGNED_SHORT, GltfBuilder
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


#: `normal-quantized`'s raw SHORT components, chosen so every decoded value is exact.
#:
#: §3.6.2.2 divides a normalized SHORT by 32767, so a raw 32767 is exactly 1.0 and a raw 0 is
#: exactly 0.0 -- and those are the only two magnitudes an arbitrary unit vector shares with a
#: 16-bit grid. Using them keeps the L5 goldens byte-exact without asking the manifest to state a
#: rounding, which would be testing the packer's arithmetic rather than the importer's decode.
#:
#: The three normals point along three DIFFERENT axes, which is the discrimination that matters:
#: a decoder that read the wrong component, or divided by 32768, or treated the accessor as
#: un-normalized raw integers, produces a different answer on each vertex rather than a uniformly
#: wrong one that could be mistaken for a coordinate-system difference.
_QUANTIZED_RAW_SHORTS = [(32767, 0, 0), (0, 32767, 0), (0, 0, 32767)]
_QUANTIZED_POSITIONS = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)]


def normal_quantized() -> Fixture:
    """A ``NORMAL`` accessor stored as **normalized SHORT**. Owns `GLTF-084`'s decode half.

    §3.7.2.1 allows ``NORMAL`` as ``float``, ``byte normalized`` or ``short normalized``, and every
    other normal in the corpus is a plain float -- so two of the three legal storage forms had no
    asset behind them at all. That is not a hypothetical gap: quantized normals are what a
    size-conscious exporter emits, and the failure mode of reading one as raw integers is a normal
    of length 32767, which no later layer rejects and which lights as pure white.

    Authored normals are passed through **byte-exact** (``docs/gltf-conventions.md``: CNA does not
    renormalise what the file promised), so the L5 golden is the decode itself and a byte
    comparison is a decode comparison. The three raw values are 32767 and 0 exclusively, which
    §3.6.2.2 maps to exactly 1.0 and 0.0 -- so the expectation states no rounding, and the test is
    about the importer rather than about the fixture generator's own arithmetic.
    """
    b = GltfBuilder("normal-quantized")
    position = b.add_packed_accessor(usage="POSITION", values=_QUANTIZED_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=_QUANTIZED_RAW_SHORTS,
                                   accessor_type="VEC3", component_type=SHORT, normalized=True)
    indices = b.add_packed_accessor(usage="indices", values=[0, 1, 2], accessor_type="SCALAR",
                                    component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="QuantizedNormalTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)

    decoded = [(x / 32767.0, y / 32767.0, z / 32767.0) for x, y, z in _QUANTIZED_RAW_SHORTS]
    l4 = world_positions(b, {mesh: list(_QUANTIZED_POSITIONS)})
    l4["quantizedNormals"] = {
        "componentType": "SHORT",
        "normalized": True,
        "rawComponents": [list(v) for v in _QUANTIZED_RAW_SHORTS],
        "decodedNormals": [list(v) for v in decoded],
        "divisor": 32767,
        "decodeRule": "§3.6.2.2 divides a normalized SHORT by 32767, not 32768. Only 32767 and 0 "
                      "are used, so every decoded value is exactly 1.0 or 0.0 and the expectation "
                      "states no rounding -- the assertion is about the importer's decode, not "
                      "about the generator's arithmetic.",
        "passthroughRule": "An authored normal is passed through byte-exact: CNA does not "
                           "renormalise what the file promised to be unit length, so the L5 "
                           "golden IS the decode and a byte comparison is a decode comparison.",
        "discriminationRule": "The three normals point along three different axes, so a decoder "
                              "reading the wrong component, dividing by 32768, or treating the "
                              "accessor as raw integers is wrong differently on each vertex -- "
                              "rather than uniformly wrong in a way that reads as a "
                              "coordinate-system difference.",
    }
    return Fixture(
        id="normal-quantized", audit_fixture=None, owning_group="normals",
        description="A NORMAL accessor stored as normalized SHORT, the storage form a "
                    "size-conscious exporter emits and the one no corpus asset used. Read as raw "
                    "integers it gives a normal of length 32767, which nothing downstream rejects "
                    "and which lights as pure white.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4", "L5"],
        features=["NORMAL as normalized SHORT", "§3.6.2.2 normalized decode",
                  "authored normal passed through byte-exact"],
        spec_anchors=["meshes-overview", "accessors"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="QuantizedNormalTri", primitive=0, mode=TRIANGLES,
            positions=_QUANTIZED_POSITIONS, normals=decoded, indices=[0, 1, 2])]},
        l4=l4,
    )


FIXTURES = [normal_absent, normal_quantized]
