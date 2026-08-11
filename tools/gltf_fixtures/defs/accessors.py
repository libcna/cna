# SPDX-License-Identifier: MS-PL
"""bufferView / accessor fixtures -- owning group ``accessors`` (plan_gltf.md §24.2).

Two of these lock behaviour the forensic audit **verified correct** (`GLTF-041`): interleaving with
non-zero offsets on both the bufferView and the accessor, and a sparse *attribute* accessor with no
base bufferView. The third proves **D4**: a sparse *index* accessor decodes to all zeros, because
`cgltf_accessor_read_index` silently returns 0 for a sparse or bufferView-less accessor and CNA
checks neither condition.

Specification: §3.6.2 ``accessors``, §3.6.2.3 ``sparse-accessors``, §3.6.2.4 ``data-alignment``.
"""

from __future__ import annotations

from ..builder import (FLOAT, TRIANGLES, UNSIGNED_SHORT, GltfBuilder, flatten, pack)
from ..manifest import Defect, Fixture, l3_primitive, world_positions

_INTERLEAVED_POSITIONS = [(0.0, 0.0, 0.0), (2.0, 0.0, 0.0), (0.0, 3.0, 0.0)]
_INTERLEAVED_NORMALS = [(0.0, 0.0, 1.0), (0.0, 0.0, 1.0), (0.0, 0.0, 1.0)]

_SPARSE_POSITIONS = [(0.0, 0.0, 0.0), (5.0, 0.0, 0.0), (0.0, 7.0, 0.0)]

_QUAD_POSITIONS = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (1.0, 1.0, 0.0), (0.0, 1.0, 0.0)]
#: What a conforming reader must decode from the sparse index accessor: the base array's last
#: element is overridden from 0 to 3, turning a degenerate second triangle into a real one.
_SPARSE_INDEX_BASE = [0, 1, 2, 0, 2, 0]
_SPARSE_INDEX_EXPECTED = [0, 1, 2, 0, 2, 3]


def interleaved_position_normal() -> Fixture:
    """f5 -- POSITION and NORMAL interleaved in one strided bufferView. **Verified correct.**

    Exercises all three address terms of §8.1's effective-address equation at once with distinct
    non-zero values: ``bufferView.byteOffset = 16``, ``byteStride = 24``, and
    ``accessor.byteOffset = 12`` for NORMAL. A decoder that ignored any one of them would produce
    visibly wrong positions.
    """
    b = GltfBuilder("interleaved-position-normal")
    # 16 bytes of leading padding, so bufferView.byteOffset is genuinely non-zero.
    b.pad(16)
    interleaved = b"".join(
        pack(list(p) + list(n), FLOAT)
        for p, n in zip(_INTERLEAVED_POSITIONS, _INTERLEAVED_NORMALS))
    offset = b.append_bytes(interleaved, alignment=4)
    view = b.add_buffer_view(offset, len(interleaved), byte_stride=24)
    position = b.add_accessor(usage="POSITION", component_type=FLOAT, accessor_type="VEC3",
                              count=3, expected=flatten(_INTERLEAVED_POSITIONS),
                              buffer_view=view, byte_offset=0,
                              min_=[0.0, 0.0, 0.0], max_=[2.0, 3.0, 0.0])
    normal = b.add_accessor(usage="NORMAL", component_type=FLOAT, accessor_type="VEC3",
                            count=3, expected=flatten(_INTERLEAVED_NORMALS),
                            buffer_view=view, byte_offset=12)
    indices = b.add_packed_accessor(usage="indices", values=[0, 1, 2], accessor_type="SCALAR",
                                    component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="InterleavedTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="interleaved-position-normal", audit_fixture="f5", owning_group="accessors",
        description="POSITION and NORMAL interleaved at byteStride 24 in a bufferView at "
                    "byteOffset 16, with NORMAL at accessor byteOffset 12. Verified correct on the "
                    "campaign baseline -- GLTF-041 locks it so remediation cannot regress it.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        features=["bufferView.byteStride", "bufferView.byteOffset", "accessor.byteOffset",
                  "interleaved attributes"],
        spec_anchors=["accessors", "data-alignment"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="InterleavedTri", primitive=0, mode=TRIANGLES,
            positions=_INTERLEAVED_POSITIONS, normals=_INTERLEAVED_NORMALS, indices=[0, 1, 2])]},
        l4=world_positions(b, {mesh: list(_INTERLEAVED_POSITIONS)}),
    )


def sparse_position() -> Fixture:
    """f6 -- a sparse POSITION accessor with **no** base bufferView. **Verified correct.**

    Per §3.6.2.3 the base array initialises to zeros when ``accessor.bufferView`` is undefined, and
    only the listed elements are displaced. Element 0 is deliberately left un-displaced so the
    zero-initialisation itself is part of the expectation.
    """
    b = GltfBuilder("sparse-position")
    sparse_indices = [1, 2]
    sparse_values = [_SPARSE_POSITIONS[1], _SPARSE_POSITIONS[2]]
    idx_offset = b.append_bytes(pack(sparse_indices, UNSIGNED_SHORT), alignment=4)
    idx_view = b.add_buffer_view(idx_offset, len(sparse_indices) * 2)
    val_offset = b.append_bytes(pack(flatten(sparse_values), FLOAT), alignment=4)
    val_view = b.add_buffer_view(val_offset, len(flatten(sparse_values)) * 4)
    position = b.add_accessor(
        usage="POSITION", component_type=FLOAT, accessor_type="VEC3", count=3,
        expected=flatten(_SPARSE_POSITIONS), buffer_view=None,
        min_=[0.0, 0.0, 0.0], max_=[5.0, 7.0, 0.0],
        sparse={
            "count": len(sparse_indices),
            "indices": {"bufferView": idx_view, "byteOffset": 0, "componentType": UNSIGNED_SHORT},
            "values": {"bufferView": val_view, "byteOffset": 0},
        })
    indices = b.add_packed_accessor(usage="indices", values=[0, 1, 2], accessor_type="SCALAR",
                                    component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="SparseTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="sparse-position", audit_fixture="f6", owning_group="accessors",
        description="A sparse POSITION accessor with an absent base bufferView: the base array is "
                    "zeros and two of three elements are displaced. Verified correct on the "
                    "campaign baseline -- the attribute path resolves sparse data properly, which "
                    "is exactly what makes D4 an index-path defect rather than an accessor one.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        features=["accessor.sparse", "absent base bufferView", "zero-initialised base array"],
        spec_anchors=["sparse-accessors"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="SparseTri", primitive=0, mode=TRIANGLES,
            positions=_SPARSE_POSITIONS, indices=[0, 1, 2])]},
        l4=world_positions(b, {mesh: list(_SPARSE_POSITIONS)}),
    )


def sparse_indices() -> Fixture:
    """f3 -- a sparse **index** accessor. Proves **D4**.

    The base array already forms a valid first triangle; the sparse override replaces the last
    element so the second triangle is non-degenerate. A conforming reader produces
    ``[0,1,2,0,2,3]``. CNA reads indices through ``cgltf_accessor_read_index``, which returns 0 for
    every element of a sparse accessor with no error channel, collapsing the quad to a point.
    """
    b = GltfBuilder("sparse-indices")
    position = b.add_packed_accessor(usage="POSITION", values=_QUAD_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    base_offset = b.append_bytes(pack(_SPARSE_INDEX_BASE, UNSIGNED_SHORT), alignment=4)
    base_view = b.add_buffer_view(base_offset, len(_SPARSE_INDEX_BASE) * 2)
    sidx_offset = b.append_bytes(pack([5], UNSIGNED_SHORT), alignment=4)
    sidx_view = b.add_buffer_view(sidx_offset, 2)
    sval_offset = b.append_bytes(pack([3], UNSIGNED_SHORT), alignment=4)
    sval_view = b.add_buffer_view(sval_offset, 2)
    indices = b.add_accessor(
        usage="indices", component_type=UNSIGNED_SHORT, accessor_type="SCALAR", count=6,
        expected=_SPARSE_INDEX_EXPECTED, buffer_view=base_view,
        sparse={
            "count": 1,
            "indices": {"bufferView": sidx_view, "byteOffset": 0, "componentType": UNSIGNED_SHORT},
            "values": {"bufferView": sval_view, "byteOffset": 0},
        })
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="SparseIndexQuad")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="sparse-indices", audit_fixture="f3", owning_group="accessors",
        description="A sparse index accessor whose override turns a degenerate second triangle "
                    "into a real one. L2 passes (the data is readable and cgltf resolves it); L3 "
                    "fails, which localises D4 precisely to CNA's index-reading call.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        referencing_groups=["topology"],
        features=["accessor.sparse on indices", "UNSIGNED_SHORT indices"],
        spec_anchors=["sparse-accessors", "meshes-overview"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="SparseIndexQuad", primitive=0, mode=TRIANGLES,
            positions=_QUAD_POSITIONS, indices=_SPARSE_INDEX_EXPECTED)]},
        l4=world_positions(b, {mesh: list(_QUAD_POSITIONS)}),
        defects=[Defect(
            id="D4", owner="GLTF-ACCESSOR", first_divergent_layer="L3",
            summary="A sparse index accessor decodes to all zeros. cgltf_accessor_read_index "
                    "returns 0 when accessor->is_sparse or accessor->buffer_view is null, with no "
                    "error channel, and ExtractMesh checks neither.",
            owning_tasks=["GLTF-063"],
            divergent_fields=["indices", "triangles"],
            current_actual={
                "indices": [0, 0, 0, 0, 0, 0],
                "note": "Every index reads back as 0, so the quad collapses to a single "
                        "degenerate point. The L2 dump of the same accessor is correct, which is "
                        "the evidence that the accessor layer is not at fault.",
            },
        )],
    )


FIXTURES = [interleaved_position_normal, sparse_position, sparse_indices]
