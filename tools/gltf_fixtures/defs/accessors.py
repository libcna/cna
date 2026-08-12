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

from ..builder import (FLOAT, TRIANGLES, UNSIGNED_BYTE, UNSIGNED_SHORT, GltfBuilder, flatten,
                       pack)
from ..manifest import Defect, Fixture, l3_primitive, world_positions

_INTERLEAVED_POSITIONS = [(0.0, 0.0, 0.0), (2.0, 0.0, 0.0), (0.0, 3.0, 0.0)]
_INTERLEAVED_NORMALS = [(0.0, 0.0, 1.0), (0.0, 0.0, 1.0), (0.0, 0.0, 1.0)]

_SPARSE_POSITIONS = [(0.0, 0.0, 0.0), (5.0, 0.0, 0.0), (0.0, 7.0, 0.0)]

_QUAD_POSITIONS = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (1.0, 1.0, 0.0), (0.0, 1.0, 0.0)]
#: What a conforming reader must decode from the sparse index accessor: the base array's last
#: element is overridden from 0 to 3, turning a degenerate second triangle into a real one.
_SPARSE_INDEX_BASE = [0, 1, 2, 0, 2, 0]
_SPARSE_INDEX_EXPECTED = [0, 1, 2, 0, 2, 3]

#: The base array of ``sparse-interleaved-base``, stored interleaved with NORMAL at byteStride 24.
#: Everything except element 0 is a sentinel that the sparse block must displace; the sentinel is
#: far outside the accessor's authored bounds so a reader that misses an override produces a
#: visibly broken quad rather than a subtly wrong one.
_SPARSE_INTERLEAVED_BASE = [(0.0, 0.0, 0.0), (-9.0, -9.0, -9.0), (-9.0, -9.0, -9.0),
                            (-9.0, -9.0, -9.0)]
#: What a conforming reader must decode. The three overridden elements are pairwise distinct on
#: purpose: a reader that walks the values array at the wrong stride lands on a *different
#: override*, and that has to be distinguishable from landing on the right one.
_SPARSE_INTERLEAVED_POSITIONS = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (2.0, 3.0, 0.0), (0.0, 4.0, 0.0)]
_SPARSE_INTERLEAVED_NORMALS = [(0.0, 0.0, 1.0)] * 4


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
    """f3 -- a sparse **index** accessor. Proved **D4**; the regression witness for `GLTF-063`.

    The base array already forms a valid first triangle; the sparse override replaces the last
    element so the second triangle is non-degenerate. A conforming reader produces
    ``[0,1,2,0,2,3]``. CNA read indices through ``cgltf_accessor_read_index``, which returns 0 for
    every element of a sparse accessor with no error channel, collapsing the quad to a point;
    `GLTF-063`'s own reader resolves the override, so this now asserts as ordinary conformance.
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
                    "into a real one. L2 always passed (the data is readable and cgltf resolves "
                    "it) while L3 did not, which localised D4 precisely to CNA's index-reading "
                    "call; GLTF-063 replaced that call and the fixture now passes at every layer.",
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
            summary="A sparse index accessor decoded to all zeros. cgltf_accessor_read_index "
                    "returns 0 when accessor->is_sparse or accessor->buffer_view is null, with no "
                    "error channel, and ExtractMesh checked neither. GLTF-063 replaced that call "
                    "with a CNA-side sparse-aware, bounds-checked index reader.",
            owning_tasks=["GLTF-063"],
            closed_tasks=["GLTF-063"],
            status="fixed",
            divergent_fields=[],
            current_actual={
                "indices": list(_SPARSE_INDEX_EXPECTED),
                "note": "The decoded indices are the spec-derived ones. Nothing is suppressed any "
                        "more: GltfConformanceL3 asserts this fixture's index list in full, so D4 "
                        "reappearing fails an ordinary green test rather than needing a "
                        "known-defect test of its own.",
            },
            prior_actual={
                "indices": [0, 0, 0, 0, 0, 0],
                "measuredOn": "fb3728267e8f2179d43b96357ff372ae712b7e7f",
                "note": "What the forensic audit measured before GLTF-063: every index read back "
                        "as 0, so the quad collapsed to a single degenerate point. The L2 dump of "
                        "the same accessor was already correct, which is the evidence that "
                        "localised the defect to the index-reading call rather than the accessor "
                        "layer.",
            },
        )],
    )


def sparse_interleaved_base() -> Fixture:
    """`GLTF-062` -- a sparse accessor whose **base** bufferView is interleaved.

    §3.6.2.3 defines two independent strides. The *base* array is addressed with the base
    bufferView's ``byteStride``; the sparse ``values`` array is its own bufferView and is always
    **tightly packed**, so its element `i` sits at `i * elementSize`. An accessor that is both
    sparse *and* strided is the only place those two numbers differ, which is why nothing else in
    the corpus can catch a reader that confuses them.

    The fixture is built so the confusion is unmissable: three overrides, base stride 24, element
    size 12. Element 0 lands correctly under either rule; elements 1 and 2 do not.
    """
    b = GltfBuilder("sparse-interleaved-base")
    override_indices = [1, 2, 3]
    override_values = [_SPARSE_INTERLEAVED_POSITIONS[i] for i in override_indices]
    idx_offset = b.append_bytes(pack(override_indices, UNSIGNED_SHORT), alignment=4)
    idx_view = b.add_buffer_view(idx_offset, len(override_indices) * 2)
    # Tightly packed, exactly as the specification requires -- 3 x VEC3<float> = 36 bytes, with no
    # byteStride on the view. The interleaved base data is appended *after* it on purpose: a reader
    # that walked this array at the base accessor's stride instead would run off the end of a
    # trailing view, and the corpus should exercise the defect, not an allocator.
    val_offset = b.append_bytes(pack(flatten(override_values), FLOAT), alignment=4)
    val_view = b.add_buffer_view(val_offset, len(flatten(override_values)) * 4)

    interleaved = b"".join(
        pack(list(p) + list(n), FLOAT)
        for p, n in zip(_SPARSE_INTERLEAVED_BASE, _SPARSE_INTERLEAVED_NORMALS))
    base_offset = b.append_bytes(interleaved, alignment=4)
    base_view = b.add_buffer_view(base_offset, len(interleaved), byte_stride=24)

    position = b.add_accessor(
        usage="POSITION", component_type=FLOAT, accessor_type="VEC3", count=4,
        expected=flatten(_SPARSE_INTERLEAVED_POSITIONS), buffer_view=base_view, byte_offset=0,
        min_=[0.0, 0.0, 0.0], max_=[2.0, 4.0, 0.0],
        sparse={
            "count": len(override_indices),
            "indices": {"bufferView": idx_view, "byteOffset": 0, "componentType": UNSIGNED_SHORT},
            "values": {"bufferView": val_view, "byteOffset": 0},
        })
    normal = b.add_accessor(usage="NORMAL", component_type=FLOAT, accessor_type="VEC3", count=4,
                            expected=flatten(_SPARSE_INTERLEAVED_NORMALS),
                            buffer_view=base_view, byte_offset=12)
    indices = b.add_packed_accessor(usage="indices", values=[0, 1, 2, 0, 2, 3],
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="SparseInterleavedQuad")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="sparse-interleaved-base", owning_group="accessors",
        description="A sparse POSITION accessor over an interleaved base bufferView (byteStride "
                    "24, element size 12), with three overrides. The sparse values array is "
                    "tightly packed per the specification, so a reader that addresses it at the "
                    "base accessor's stride misplaces every override after the first. This is the "
                    "regression witness for the vendored cgltf sparse-values stride bug that "
                    "GLTF-062 works around; the base positions are deliberately far out of the "
                    "authored bounds so a missed override cannot look plausible.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["accessor.sparse", "bufferView.byteStride", "interleaved base array",
                  "tightly packed sparse values"],
        spec_anchors=["sparse-accessors", "accessors", "data-alignment"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="SparseInterleavedQuad", primitive=0, mode=TRIANGLES,
            positions=_SPARSE_INTERLEAVED_POSITIONS, normals=_SPARSE_INTERLEAVED_NORMALS,
            indices=[0, 1, 2, 0, 2, 3])]},
        l4=world_positions(b, {mesh: list(_SPARSE_INTERLEAVED_POSITIONS)}),
    )


#: The 3x3 matrix `mat3-padded` stores, in glTF's column-major order. Nine distinct non-zero values,
#: because the failure this fixture exists to catch reads the PADDING as data: a reader that ignores
#: §3.6.2.4's column alignment produces `1,2,3,0,4,5,6,0,7` from these bytes -- so every authored
#: value must differ from 0 and from its neighbours for the wrong answer to be a *different* answer.
_MAT3_VALUES = [1, 2, 3, 4, 5, 6, 7, 8, 9]


def mat3_padded() -> Fixture:
    """A ``MAT3`` accessor of ``UNSIGNED_BYTE`` with §3.6.2.4 column padding. Owns **`GLTF-058`**.

    §3.6.2.4: each **column** of a matrix accessor starts on a 4-byte boundary, so a `MAT3` of
    single-byte components stores 3 bytes of data plus 1 byte of padding per column -- 12 bytes per
    element, not 9. Get that wrong and the decoded matrix is silently shifted by one component per
    column, which is a plausible-looking matrix rather than an obvious error.

    Nothing in CNA reads a `MAT2`/`MAT3` accessor today: the only matrix accessor the importer
    consumes is `inverseBindMatrices`, which is `MAT4<FLOAT>` and needs no padding. That is exactly
    why the fixture exists -- an unexercised rule is one that regresses without anyone noticing, and
    the L2 sweep decodes **every** accessor a fixture declares, referenced or not.

    The primitive beside it is an ordinary triangle so the asset still imports; the matrix accessor
    is deliberately unreferenced, which is legal and is what keeps this fixture about the decode.
    """
    b = GltfBuilder("mat3-padded")
    position = b.add_packed_accessor(usage="POSITION", values=_INTERLEAVED_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=_INTERLEAVED_NORMALS,
                                   accessor_type="VEC3")
    indices = b.add_packed_accessor(usage="indices", values=[0, 1, 2],
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)

    # Authored byte for byte rather than through a packing helper, because the padding IS the
    # subject: three data bytes then one zero, per column. A helper that emitted the padding for us
    # would be asserting its own arithmetic.
    padded = bytes([_MAT3_VALUES[0], _MAT3_VALUES[1], _MAT3_VALUES[2], 0,
                    _MAT3_VALUES[3], _MAT3_VALUES[4], _MAT3_VALUES[5], 0,
                    _MAT3_VALUES[6], _MAT3_VALUES[7], _MAT3_VALUES[8], 0])
    offset = b.append_bytes(padded, alignment=4)
    view = b.add_buffer_view(offset, len(padded))
    b.add_accessor(usage="unreferenced MAT3 (column padding)", component_type=UNSIGNED_BYTE,
                   accessor_type="MAT3", count=1,
                   expected=[float(v) for v in _MAT3_VALUES], buffer_view=view)

    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="Mat3PaddedTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="mat3-padded", audit_fixture=None, owning_group="accessors",
        description="A MAT3 accessor of UNSIGNED_BYTE, stored with §3.6.2.4's column padding: 3 "
                    "data bytes plus 1 pad byte per column, 12 bytes per element rather than 9. No "
                    "CNA path reads a MAT2/MAT3 accessor today -- inverseBindMatrices is "
                    "MAT4<FLOAT> and needs no padding -- which is precisely why the rule needs a "
                    "fixture: an unexercised rule regresses unnoticed, and a reader that ignored "
                    "the padding would produce 1,2,3,0,4,5,6,0,7 from these bytes.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        features=["MAT3 accessor", "§3.6.2.4 column padding", "unreferenced accessor"],
        spec_anchors=["data-alignment", "accessor-data-types"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="Mat3PaddedTri", primitive=0, mode=TRIANGLES,
            positions=_INTERLEAVED_POSITIONS, normals=_INTERLEAVED_NORMALS, indices=[0, 1, 2])]},
        l4=world_positions(b, {mesh: list(_INTERLEAVED_POSITIONS)}),
    )


FIXTURES = [interleaved_position_normal, sparse_position, sparse_indices, sparse_interleaved_base,
            mat3_padded]
