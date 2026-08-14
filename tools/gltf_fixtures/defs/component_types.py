# SPDX-License-Identifier: MS-PL
"""Component-type fixtures -- owning group ``component-types`` (plan_gltf.md §24.2).

Both fixtures here lock behaviour the forensic audit **verified correct** (`GLTF-041`). Neither
exposes a defect: they exist so that later index-path and material work cannot be blamed on, or
quietly regress, a decode path that was already proven right.

Specification: §3.6.2.2 ``accessor-data-types``, §3.7.2.1 ``meshes-overview``.
"""

from __future__ import annotations

from ..builder import (BYTE, FLOAT, TRIANGLES, UNSIGNED_BYTE, UNSIGNED_INT,
                       UNSIGNED_SHORT, GltfBuilder)
from ..manifest import Fixture, l3_primitive, world_positions
from .common import TRIANGLE_INDICES, TRIANGLE_NORMALS, TRIANGLE_POSITIONS

#: Raw bytes as authored. Fully saturated primaries plus a mid alpha and a zero alpha, so a
#: decoder that dropped alpha, or normalised it with the wrong divisor, cannot pass by accident.
_COLOR_RAW = [(255, 0, 0, 255), (0, 255, 0, 128), (0, 0, 255, 0)]
#: What §3.6.2.2's `c / 255` rule yields. 128/255 is deliberately not representable as a short
#: decimal, so a decoder using `c / 256` or `(c + 0.5) / 256` diverges immediately.
_COLOR_DECODED = [
    (1.0, 0.0, 0.0, 1.0),
    (0.0, 1.0, 0.0, 128.0 / 255.0),
    (0.0, 0.0, 1.0, 0.0),
]


def u8_idx() -> Fixture:
    """f11 -- an ``UNSIGNED_BYTE`` index accessor. **Verified correct.**"""
    b = GltfBuilder("u8-idx")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_BYTE)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="U8IndexTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="u8-idx", audit_fixture="f11", owning_group="component-types",
        description="A triangle indexed with UNSIGNED_BYTE indices. The narrowest legal index "
                    "component type, and the one most likely to be mis-strided by a hand-rolled "
                    "index reader. Verified correct on the campaign baseline.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        referencing_groups=["accessors"],
        features=["UNSIGNED_BYTE indices"],
        spec_anchors=["accessor-data-types", "meshes-overview"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="U8IndexTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES)]},
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


def normalized_u8_color() -> Fixture:
    """f10 -- a normalized ``UNSIGNED_BYTE`` ``VEC4`` ``COLOR_0``. **Verified correct.**

    Authored without NORMAL on purpose: CNA's colored-unskinned layout is stride 24
    (Position + Color + TextureCoordinate) and has no normal slot at all, so including one would
    make the fixture test two unrelated things and misattribute a documented packing limitation
    (plan_gltf.md §2.3) to the colour decode path.
    """
    b = GltfBuilder("normalized-u8-color")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    color = b.add_packed_accessor(usage="COLOR_0", values=_COLOR_RAW, accessor_type="VEC4",
                                  component_type=UNSIGNED_BYTE, normalized=True)
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_BYTE)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "COLOR_0": color},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="ColoredTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="normalized-u8-color", audit_fixture="f10", owning_group="component-types",
        description="A per-vertex COLOR_0 stored as normalized UNSIGNED_BYTE VEC4, including a "
                    "non-representable mid alpha (128/255) and a fully transparent vertex. "
                    "Verified correct on the campaign baseline: decode and repack round-trip "
                    "byte-exactly.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        referencing_groups=["accessors"],
        features=["normalized UNSIGNED_BYTE", "COLOR_0 VEC4", "vertex colour round-trip"],
        spec_anchors=["accessor-data-types", "meshes-overview"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="ColoredTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, colors=_COLOR_DECODED, indices=TRIANGLE_INDICES)]},
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


def u32_idx() -> Fixture:
    """An ``UNSIGNED_INT`` index accessor. Closes **`GLTF-064`**'s missing third component type.

    `GltfIndexDecodeTests` already asserted all three widths decode exactly, but `u32` only on an
    accessor hand-authored inside the test -- so nothing in the **corpus** carried one, and every
    corpus-wide sweep (L2's decode, L3's index list, L5's packed bytes) ran over `u8` and `u16`
    alone. This is the fixture that puts the third width under all of them.

    A 32-bit index accessor on a three-vertex triangle is entirely legal and is what a real
    exporter emits when it does not narrow: the width is a property of the accessor, not of the
    vertex count. It also makes the L5 golden say something the other two cannot -- CNA chooses its
    **index buffer** width by vertex count rather than by the file's, so this fixture's 32-bit
    source indices must arrive as a 16-bit index buffer, and the golden proves the narrowing.
    """
    b = GltfBuilder("u32-idx")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_INT)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="U32IndexTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="u32-idx", audit_fixture=None, owning_group="component-types",
        description="A triangle indexed with UNSIGNED_INT indices -- the third and widest legal "
                    "index component type, and the one no corpus fixture carried. The vertex count "
                    "is 3, so CNA's own index buffer narrows to 16 bits: the source width and the "
                    "packed width are deliberately different here, which is what makes the L5 "
                    "golden prove the narrowing rather than merely echo the file.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        referencing_groups=["accessors"],
        features=["UNSIGNED_INT indices", "index width narrowing"],
        spec_anchors=["accessor-data-types", "meshes-overview"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="U32IndexTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES)]},
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


def non_indexed_triangles() -> Fixture:
    """A ``TRIANGLES`` primitive with **no** ``indices``. Closes **`GLTF-067`**'s missing fixture.

    §3.7.2.1: when `indices` is undefined the primitive is drawn as an implicit sequential range,
    `0 .. count-1`. CNA synthesises exactly that, and the assertion lived only on a hand-authored
    primitive inside `GltfIndexDecodeTests` -- because the corpus fixture that used to cover it,
    `mode-points`, stopped being able to: `GLTF-071` rejects its `POINTS` mode before the implicit
    range is ever synthesised.

    So the topology here is `TRIANGLES`, deliberately: the fixture has to reach the synthesis, and
    a rejected mode never does. Three vertices, so the synthesised list is `[0, 1, 2]` and the L5
    golden carries it as real index bytes -- a synthesis that produced nothing at all would leave
    an empty index buffer rather than a wrong one, which is a failure no L3 value comparison can
    see.
    """
    b = GltfBuilder("non-indexed-triangles")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal},
        "mode": TRIANGLES,
    }], name="NonIndexedTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="non-indexed-triangles", audit_fixture=None, owning_group="component-types",
        description="A TRIANGLES primitive with no indices accessor at all. §3.7.2.1 makes the "
                    "index list implicit -- 0..count-1 -- and CNA must synthesise it. The corpus "
                    "had no such fixture with an importable topology: the one that used to cover "
                    "it authored POINTS, which GLTF-071 now rejects before synthesis runs.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        referencing_groups=["accessors", "topology"],
        features=["no indices accessor", "implicit index range"],
        spec_anchors=["meshes-overview"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="NonIndexedTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS,
            indices=[0, 1, 2])]},
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


# --- the remaining §24.2 component-type rungs (`GLTF-399`) ---------------------------------------

#: A `COLOR_0` as 16-bit normalized integers. 65535 -> 1.0 exactly, and 21845/65535 is deliberately
#: not a short decimal, so a decoder dividing by 65536 diverges in the fourth digit rather than
#: rounding to the same answer.
_U16_COLOR_RAW = [(65535, 0, 0, 65535), (0, 65535, 0, 21845), (0, 0, 65535, 0)]
_U16_COLOR_DECODED = [
    (1.0, 0.0, 0.0, 1.0),
    (0.0, 1.0, 0.0, 21845.0 / 65535.0),
    (0.0, 0.0, 1.0, 0.0),
]

#: A `COLOR_0` stored as plain floats, including a value ABOVE 1. §3.9.2 does not clamp a float
#: vertex colour, and CNA repacks to bytes -- so the fixture states the specification's value and
#: the L3 comparison applies the round-trip, which is where a missing clamp would show.
_FLOAT_COLOR = [(0.25, 0.5, 0.75, 1.0), (1.0, 0.0, 0.5, 0.5), (0.0, 1.0, 0.25, 0.0)]

#: A `NORMAL` as signed normalized bytes. `-128` is the value §3.6.2.2's `max(c/127, -1)` rule
#: exists for: divided by 127 it is -1.0079, which is not a unit vector, and the vendored cgltf does
#: not clamp it (known_bugs.md, `GLTF-056`).
#:
#: These are **axis-aligned on purpose**, which is the opposite of this corpus's usual rule, and the
#: reason is arithmetic rather than laziness: 127 is prime, so no off-axis 8-bit triple decodes to
#: unit length -- the closest are ~1.002 long, which is a real quantisation artefact but would fail
#: the corpus's own "every normal is unit length" invariant and drown this fixture's actual subject.
#: That subject is the clamp, and `-128` versus `-1.0079` is what discriminates: an unclamped
#: decoder produces a 1.0079-long normal here, which that same invariant then reports.
_I8_NORMAL_RAW = [(-128, 0, 0), (0, 127, 0), (0, 0, -128)]


def u16_idx() -> Fixture:
    """An ``UNSIGNED_SHORT`` index accessor, named as its own rung."""
    b = GltfBuilder("u16-idx")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="U16IndexTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="u16-idx", audit_fixture=None, owning_group="component-types",
        description="A triangle indexed with UNSIGNED_SHORT indices -- the middle width, and the "
                    "one CNA's own index buffer uses whenever the vertex count fits. It is the "
                    "rung where the source width and the packed width coincide, which is what "
                    "makes u8-idx and u32-idx's narrowing and widening legible as changes.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        referencing_groups=["accessors"],
        features=["UNSIGNED_SHORT indices"],
        spec_anchors=["accessor-data-types", "meshes-overview"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="U16IndexTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES)]},
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


def normalized_u16_color() -> Fixture:
    """A 16-bit normalized ``COLOR_0`` -- the divisor is 65535, never 65536."""
    b = GltfBuilder("normalized-u16-color")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    color = b.add_packed_accessor(usage="COLOR_0", values=_U16_COLOR_RAW, accessor_type="VEC4",
                                  component_type=UNSIGNED_SHORT, normalized=True)
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "COLOR_0": color},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="U16ColoredTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="normalized-u16-color", audit_fixture=None, owning_group="component-types",
        description="COLOR_0 as normalized UNSIGNED_SHORT. §3.6.2.2 divides by 65535, and one "
                    "alpha (21845) is deliberately not a short decimal in that scale -- a decoder "
                    "dividing by 65536 diverges in the fourth digit instead of rounding to the "
                    "same answer.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        referencing_groups=["accessors"],
        features=["normalized UNSIGNED_SHORT", "COLOR_0 VEC4", "65535 divisor"],
        spec_anchors=["accessor-data-types"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="U16ColoredTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, colors=_U16_COLOR_DECODED, indices=TRIANGLE_INDICES)]},
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


def float_color() -> Fixture:
    """A float ``COLOR_0``, which the specification does not normalise at all."""
    b = GltfBuilder("float-color")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    color = b.add_packed_accessor(usage="COLOR_0", values=_FLOAT_COLOR, accessor_type="VEC4",
                                  component_type=FLOAT)
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "COLOR_0": color},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="FloatColoredTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="float-color", audit_fixture=None, owning_group="component-types",
        description="COLOR_0 stored as plain FLOAT VEC4 -- the third of the three forms §3.6.2.2 "
                    "allows, and the one with no normalisation step to get wrong. CNA repacks it "
                    "to bytes, so the fixture states the specification's float and the comparison "
                    "applies that round-trip, which is where a decoder that normalised an "
                    "already-normalised value would fail.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        referencing_groups=["accessors"],
        features=["FLOAT COLOR_0", "no normalisation"],
        spec_anchors=["accessor-data-types"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="FloatColoredTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, colors=_FLOAT_COLOR, indices=TRIANGLE_INDICES)]},
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


def normalized_i8_normal() -> Fixture:
    """A signed normalized ``NORMAL`` -- the accessor form §3.6.2.2's clamp exists for."""
    decoded = [tuple(max(c / 127.0, -1.0) for c in v) for v in _I8_NORMAL_RAW]
    b = GltfBuilder("normalized-i8-normal")
    # BYTE NORMAL is enabled by KHR_mesh_quantization, not core glTF. The extension is mandatory
    # for this source form and requires each VEC3 element to start on a four-byte boundary.
    b.declare_extensions(required=["KHR_mesh_quantization"])
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=_I8_NORMAL_RAW, accessor_type="VEC3",
                                   component_type=BYTE, normalized=True, byte_stride=4)
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="QuantisedNormalTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="normalized-i8-normal", audit_fixture=None, owning_group="component-types",
        description="NORMAL as signed normalized BYTE, with -128 in two of the three vertices. "
                    "That is the value §3.6.2.2's max(c/127, -1) rule exists for: divided by 127 "
                    "it is -1.0079, which is not a unit vector, and the vendored cgltf does not "
                    "clamp it -- so this fixture is the corpus's own witness for the "
                    "ClampNormalizedSigned workaround (known_bugs.md, GLTF-056). The vectors are "
                    "axis-aligned because 127 is prime and no off-axis 8-bit triple decodes to "
                    "unit length; an unclamped decode is 1.0079 long, which the corpus's "
                    "unit-length invariant reports on its own.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        referencing_groups=["accessors", "normals"],
        features=["KHR_mesh_quantization", "normalized BYTE NORMAL",
                  "4-byte aligned VEC3 elements", "§3.6.2.2 signed clamp",
                  "cgltf workaround witness"],
        spec_anchors=["accessor-data-types"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="QuantisedNormalTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=decoded, indices=TRIANGLE_INDICES)]},
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


FIXTURES = [u8_idx, u16_idx, u32_idx, non_indexed_triangles, normalized_u8_color,
            normalized_u16_color, float_color, normalized_i8_normal]
