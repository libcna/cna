# SPDX-License-Identifier: MS-PL
"""Component-type fixtures -- owning group ``component-types`` (plan_gltf.md §24.2).

Both fixtures here lock behaviour the forensic audit **verified correct** (`GLTF-041`). Neither
exposes a defect: they exist so that later index-path and material work cannot be blamed on, or
quietly regress, a decode path that was already proven right.

Specification: §3.6.2.2 ``accessor-data-types``, §3.7.2.1 ``meshes-overview``.
"""

from __future__ import annotations

from ..builder import TRIANGLES, UNSIGNED_BYTE, UNSIGNED_INT, GltfBuilder
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


FIXTURES = [u8_idx, u32_idx, non_indexed_triangles, normalized_u8_color]
