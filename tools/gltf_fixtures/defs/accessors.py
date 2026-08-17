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
from ..l5 import unsupported as l5_unsupported
from ..manifest import Defect, Fixture, l3_primitive, world_positions
from .common import TRIANGLE_INDICES, TRIANGLE_NORMALS, TRIANGLE_POSITIONS

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
        },
        # GLTF-066's control: with no base bufferView the base array is all zeros, so a decoder
        # that ignored the sparse block would collapse two of the three vertices onto the origin.
        base_values_if_sparse_ignored=[0.0] * 9)
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
        },
        # GLTF-066's control, and the sharpest of the three: the base array's last element is 0, so
        # a decoder that ignored the override produces the degenerate triangle (0,2,0) -- which is
        # a quad missing half of itself, not an error anything downstream would notice.
        base_values_if_sparse_ignored=_SPARSE_INDEX_BASE)
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
        },
        # GLTF-066's control: the interleaved base array, which is what a decoder that ignored the
        # sparse block would produce -- three of the four vertices differ from the effective value.
        base_values_if_sparse_ignored=flatten(_SPARSE_INTERLEAVED_BASE))
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


# --- §24.2's remaining accessor rungs (`GLTF-399`) -----------------------------------------------

_UV = [(0.0, 0.0), (0.75, 0.125), (0.25, 0.875)]

# Raw and specification-decoded COLOR_0 values for the one accessor fixture where a single
# interleaved vertex record mixes component widths.  The alpha bytes are deliberately neither all
# zero nor all saturated: 64/255 and 128/255 expose both a decoder using the FLOAT component size
# to advance COLOR_0 and one normalising by 256.
_MIXED_COLOR_RAW = [(255, 32, 0, 255), (0, 255, 64, 128), (16, 0, 255, 64)]
_MIXED_COLOR_DECODED = [
    tuple(component / 255.0 for component in color) for color in _MIXED_COLOR_RAW
]


def accessor_offset() -> Fixture:
    """A non-zero ``accessor.byteOffset`` into a view whose own offset is zero."""
    b = GltfBuilder("accessor-offset")
    # Two vertex sets in one tightly packed view; the accessor reads the SECOND, so its own
    # byteOffset is the only term that selects it. A decoder ignoring it reads the decoy.
    decoy = [(-9.0, -9.0, -9.0)] * 3
    packed = pack(flatten(decoy) + flatten(TRIANGLE_POSITIONS), FLOAT)
    offset = b.append_bytes(packed, alignment=4)
    view = b.add_buffer_view(offset, len(packed))
    position = b.add_accessor(usage="POSITION", component_type=FLOAT, accessor_type="VEC3",
                              count=3, expected=flatten(TRIANGLE_POSITIONS), buffer_view=view,
                              byte_offset=36, min_=[0.0, 0.0, 0.0], max_=[1.0, 1.0, 0.0])
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{"attributes": {"POSITION": position, "NORMAL": normal},
                        "indices": indices, "mode": TRIANGLES}], name="OffsetTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="accessor-offset", audit_fixture=None, owning_group="accessors",
        description="POSITION read at accessor.byteOffset 36 into a view that starts at 0, with a "
                    "decoy vertex set occupying the first 36 bytes. The decoy is what makes the "
                    "fixture discriminate: a decoder ignoring accessor.byteOffset reads three "
                    "(-9,-9,-9) vertices rather than something merely shifted.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        features=["accessor.byteOffset", "decoy data before the accessor"],
        spec_anchors=["accessors", "data-alignment"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="OffsetTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES)]},
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


def bufferview_offset() -> Fixture:
    """The other address term: a non-zero ``bufferView.byteOffset``, accessor offset zero."""
    b = GltfBuilder("bufferview-offset")
    b.pad(64)
    packed = pack(flatten(TRIANGLE_POSITIONS), FLOAT)
    offset = b.append_bytes(packed, alignment=4)
    view = b.add_buffer_view(offset, len(packed))
    position = b.add_accessor(usage="POSITION", component_type=FLOAT, accessor_type="VEC3",
                              count=3, expected=flatten(TRIANGLE_POSITIONS), buffer_view=view,
                              min_=[0.0, 0.0, 0.0], max_=[1.0, 1.0, 0.0])
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{"attributes": {"POSITION": position, "NORMAL": normal},
                        "indices": indices, "mode": TRIANGLES}], name="ViewOffsetTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="bufferview-offset", audit_fixture=None, owning_group="accessors",
        description="POSITION in a bufferView that starts 64 bytes into the buffer, with the "
                    "accessor's own offset zero. The pair with accessor-offset: §8.1 adds both "
                    "terms, and a decoder that implements only one passes exactly one of these "
                    "two fixtures.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        features=["bufferView.byteOffset", "leading buffer padding"],
        spec_anchors=["accessors", "buffers-and-buffer-views"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="ViewOffsetTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES)]},
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


def bufferview_stride_tight() -> Fixture:
    """A view declaring a ``byteStride`` exactly equal to the element size."""
    b = GltfBuilder("bufferview-stride-tight")
    packed = pack(flatten(TRIANGLE_POSITIONS), FLOAT)
    offset = b.append_bytes(packed, alignment=4)
    # byteStride 12 == the element size. Legal, redundant, and emitted by real exporters -- and a
    # reader that treats "has a byteStride" as "is interleaved" has an extra code path to get
    # wrong here, on data that is not interleaved at all.
    view = b.add_buffer_view(offset, len(packed), byte_stride=12)
    position = b.add_accessor(usage="POSITION", component_type=FLOAT, accessor_type="VEC3",
                              count=3, expected=flatten(TRIANGLE_POSITIONS), buffer_view=view,
                              min_=[0.0, 0.0, 0.0], max_=[1.0, 1.0, 0.0])
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{"attributes": {"POSITION": position, "NORMAL": normal},
                        "indices": indices, "mode": TRIANGLES}], name="TightStrideTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="bufferview-stride-tight", audit_fixture=None, owning_group="accessors",
        description="A bufferView declaring byteStride 12 for VEC3<float> data -- exactly the "
                    "element size. Legal and redundant, which is why exporters emit it and why a "
                    "reader that branches on 'has a byteStride' rather than on the value takes its "
                    "interleaved path over data that is not interleaved.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        features=["byteStride equal to element size", "redundant stride"],
        spec_anchors=["buffers-and-buffer-views", "accessors"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="TightStrideTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES)]},
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


def interleaved_pos_nrm_uv() -> Fixture:
    """Three attributes in one strided view, each at its own accessor offset."""
    b = GltfBuilder("interleaved-pos-nrm-uv")
    interleaved = b"".join(
        pack(list(p) + list(n) + list(uv), FLOAT)
        for p, n, uv in zip(TRIANGLE_POSITIONS, TRIANGLE_NORMALS, _UV))
    offset = b.append_bytes(interleaved, alignment=4)
    view = b.add_buffer_view(offset, len(interleaved), byte_stride=32)
    position = b.add_accessor(usage="POSITION", component_type=FLOAT, accessor_type="VEC3",
                              count=3, expected=flatten(TRIANGLE_POSITIONS), buffer_view=view,
                              byte_offset=0, min_=[0.0, 0.0, 0.0], max_=[1.0, 1.0, 0.0])
    normal = b.add_accessor(usage="NORMAL", component_type=FLOAT, accessor_type="VEC3", count=3,
                            expected=flatten(TRIANGLE_NORMALS), buffer_view=view, byte_offset=12)
    uv = b.add_accessor(usage="TEXCOORD_0", component_type=FLOAT, accessor_type="VEC2", count=3,
                        expected=flatten(_UV), buffer_view=view, byte_offset=24)
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{"attributes": {"POSITION": position, "NORMAL": normal, "TEXCOORD_0": uv},
                        "indices": indices, "mode": TRIANGLES}], name="InterleavedThreeTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="interleaved-pos-nrm-uv", audit_fixture=None, owning_group="accessors",
        description="POSITION, NORMAL and TEXCOORD_0 interleaved at stride 32 with offsets 0, 12 "
                    "and 24. Three accessors over one view -- the layout every real exporter "
                    "produces, and the one where an off-by-one in the offset table gives each "
                    "attribute its neighbour's data rather than nothing.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        features=["three interleaved attributes", "per-accessor byteOffset", "byteStride 32"],
        spec_anchors=["accessors", "buffers-and-buffer-views"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="InterleavedThreeTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, texcoords=_UV,
            indices=TRIANGLE_INDICES)]},
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
        # No L5 golden, deliberately and with the reason recorded: this primitive authors UVs and
        # no TANGENT, so CNA generates a tangent basis with GLTF-180's angle-weighted algorithm.
        # Reproducing that algorithm in the generator is GLTF-149's work, and emitting a golden
        # nobody has checked would be worse than having none. The fixture's subject is the
        # interleaved address arithmetic, which L2 and L3 assert in full.
        l5=l5_unsupported("the primitive authors UVs and no TANGENT, so its packed bytes contain a "
                          "generated tangent basis this generator does not reimplement",
                          ["GLTF-149", "GLTF-180"]),
    )


def interleaved_mixed_widths() -> Fixture:
    """FLOAT positions and normalized U8 colours share one 16-byte vertex record."""
    b = GltfBuilder("interleaved-mixed-widths")
    # Every record is 12 bytes of VEC3<FLOAT> followed immediately by 4 bytes of
    # VEC4<UNSIGNED_BYTE>.  There is no padding to disguise a component-size error: using the
    # position accessor's four-byte component width for COLOR_0 walks into the next vertex, while
    # treating the whole record as bytes destroys POSITION after its first component.
    interleaved = b"".join(
        pack(p, FLOAT) + pack(color, UNSIGNED_BYTE)
        for p, color in zip(TRIANGLE_POSITIONS, _MIXED_COLOR_RAW))
    offset = b.append_bytes(interleaved, alignment=4)
    view = b.add_buffer_view(offset, len(interleaved), byte_stride=16)
    position = b.add_accessor(
        usage="POSITION", component_type=FLOAT, accessor_type="VEC3", count=3,
        expected=flatten(TRIANGLE_POSITIONS), buffer_view=view, byte_offset=0,
        min_=[0.0, 0.0, 0.0], max_=[1.0, 1.0, 0.0])
    color = b.add_accessor(
        usage="COLOR_0", component_type=UNSIGNED_BYTE, accessor_type="VEC4", count=3,
        expected=flatten(_MIXED_COLOR_DECODED), buffer_view=view, byte_offset=12,
        normalized=True)
    indices = b.add_packed_accessor(
        usage="indices", values=TRIANGLE_INDICES, accessor_type="SCALAR",
        component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "COLOR_0": color},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="MixedWidthTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="interleaved-mixed-widths", audit_fixture=None, owning_group="accessors",
        description="POSITION as VEC3<FLOAT> and COLOR_0 as normalized "
                    "VEC4<UNSIGNED_BYTE> occupy the same 16-byte interleaved record at offsets 0 "
                    "and 12. The record has no padding, so advancing either accessor with the "
                    "other one's component width immediately reads a neighbouring attribute or "
                    "vertex.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        features=["mixed FLOAT and UNSIGNED_BYTE attributes", "byteStride 16",
                  "normalized interleaved COLOR_0", "no inter-attribute padding"],
        spec_anchors=["accessors", "accessor-data-types", "data-alignment"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="MixedWidthTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, colors=_MIXED_COLOR_DECODED,
            indices=TRIANGLE_INDICES)]},
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


def stride_padded() -> Fixture:
    """A stride larger than the attributes it carries -- padding a reader must skip."""
    b = GltfBuilder("stride-padded")
    # 24 bytes of data (POSITION + NORMAL) in a 40-byte stride: 16 bytes of padding per vertex,
    # filled with a sentinel a reader would decode as garbage if it walked tightly.
    interleaved = b"".join(
        pack(list(p) + list(n), FLOAT) + pack([-9.0, -9.0, -9.0, -9.0], FLOAT)
        for p, n in zip(TRIANGLE_POSITIONS, TRIANGLE_NORMALS))
    offset = b.append_bytes(interleaved, alignment=4)
    view = b.add_buffer_view(offset, len(interleaved), byte_stride=40)
    position = b.add_accessor(usage="POSITION", component_type=FLOAT, accessor_type="VEC3",
                              count=3, expected=flatten(TRIANGLE_POSITIONS), buffer_view=view,
                              min_=[0.0, 0.0, 0.0], max_=[1.0, 1.0, 0.0])
    normal = b.add_accessor(usage="NORMAL", component_type=FLOAT, accessor_type="VEC3", count=3,
                            expected=flatten(TRIANGLE_NORMALS), buffer_view=view, byte_offset=12)
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{"attributes": {"POSITION": position, "NORMAL": normal},
                        "indices": indices, "mode": TRIANGLES}], name="PaddedStrideTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="stride-padded", audit_fixture=None, owning_group="accessors",
        description="24 bytes of attributes in a 40-byte stride, with the 16 bytes of padding "
                    "filled with a -9 sentinel. A reader that walks tightly rather than by stride "
                    "decodes the padding as its second vertex, which is a visibly broken triangle "
                    "rather than a subtly wrong one.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        features=["stride larger than the data", "inter-vertex padding", "sentinel padding"],
        spec_anchors=["buffers-and-buffer-views"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="PaddedStrideTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES)]},
        l4=world_positions(b, {mesh: list(TRIANGLE_POSITIONS)}),
    )


def two_primitives_one_buffer() -> Fixture:
    """Two primitives whose accessors read different windows of one bufferView."""
    b = GltfBuilder("two-primitives-one-buffer")
    second = [(2.0, 0.0, 0.0), (3.0, 0.0, 0.0), (2.0, 1.0, 0.0)]
    packed = pack(flatten(TRIANGLE_POSITIONS) + flatten(second), FLOAT)
    offset = b.append_bytes(packed, alignment=4)
    # Multiple vertex accessors sharing one bufferView require byteStride even when each accessor
    # selects a non-overlapping window. Twelve is also the natural VEC3<float> element stride.
    view = b.add_buffer_view(offset, len(packed), byte_stride=12)
    first_pos = b.add_accessor(usage="POSITION (primitive 0)", component_type=FLOAT,
                               accessor_type="VEC3", count=3,
                               expected=flatten(TRIANGLE_POSITIONS), buffer_view=view,
                               min_=[0.0, 0.0, 0.0], max_=[1.0, 1.0, 0.0])
    second_pos = b.add_accessor(usage="POSITION (primitive 1)", component_type=FLOAT,
                                accessor_type="VEC3", count=3, expected=flatten(second),
                                buffer_view=view, byte_offset=36,
                                min_=[2.0, 0.0, 0.0], max_=[3.0, 1.0, 0.0])
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    # One real multi-primitive mesh. This used to be reshaped as two meshes solely because the L4
    # oracle concatenated all primitive positions into one node-level record while CNA emitted one
    # record per primitive. The oracle now uses the same (node, mesh, primitive) unit, so the asset
    # can finally exercise the cache boundary it names: the primitive changes while the bufferView,
    # NORMAL accessor and index accessor stay identical.
    mesh = b.add_mesh([
        {"attributes": {"POSITION": first_pos, "NORMAL": normal},
         "indices": indices, "mode": TRIANGLES},
        {"attributes": {"POSITION": second_pos, "NORMAL": normal},
         "indices": indices, "mode": TRIANGLES},
    ], name="TwoWindowMesh")
    node = b.add_node(name="TwoWindows", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="two-primitives-one-buffer", audit_fixture=None, owning_group="accessors",
        description="Two primitives of one mesh whose POSITION accessors are different 36-byte "
                    "windows of ONE bufferView, sharing a single NORMAL accessor and a single "
                    "index accessor. Sharing is the point: a reader that caches decoded data by "
                    "bufferView rather than by accessor gives both primitives the same vertices, "
                    "and one that re-decodes the shared accessors per primitive is merely slow.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["shared bufferView", "shared normal and index accessors",
                  "two primitives in one mesh", "two windows of one view"],
        spec_anchors=["meshes-overview", "buffers-and-buffer-views"],
        l3={"primitives": [
            l3_primitive(mesh=mesh, mesh_name="TwoWindowMesh", primitive=0, mode=TRIANGLES,
                         positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS,
                         indices=TRIANGLE_INDICES),
            l3_primitive(mesh=mesh, mesh_name="TwoWindowMesh", primitive=1, mode=TRIANGLES,
                         positions=second, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES),
        ]},
        l4=world_positions(b, {
            (mesh, 0): list(TRIANGLE_POSITIONS),
            (mesh, 1): list(second),
        }),
    )


def accessor_minmax() -> Fixture:
    """An accessor whose declared ``min``/``max`` are the tight bounds of its data."""
    b = GltfBuilder("accessor-minmax")
    spread = [(-2.0, 0.5, -3.0), (4.0, -1.5, 0.0), (1.0, 6.0, 2.0)]
    position = b.add_packed_accessor(usage="POSITION", values=spread, accessor_type="VEC3",
                                     with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{"attributes": {"POSITION": position, "NORMAL": normal},
                        "indices": indices, "mode": TRIANGLES}], name="BoundedTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="accessor-minmax", audit_fixture=None, owning_group="accessors",
        description="A POSITION accessor whose min/max are the tight per-component bounds of data "
                    "spread over both signs on every axis. §3.6.1 requires the bounds on a "
                    "POSITION accessor, and they are the one piece of redundancy glTF gives a "
                    "reader -- GLTF-061 cross-checks every decoded value against them, and a "
                    "decoder that is wrong in a way the bounds can see is caught before any "
                    "geometry is compared.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        features=["declared min/max", "tight bounds", "values on both signs"],
        spec_anchors=["accessors"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="BoundedTri", primitive=0, mode=TRIANGLES,
            positions=spread, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES)]},
        l4=world_positions(b, {mesh: list(spread)}),
    )


FIXTURES = [accessor_offset, bufferview_offset, bufferview_stride_tight,
            interleaved_position_normal, interleaved_pos_nrm_uv, interleaved_mixed_widths,
            stride_padded,
            two_primitives_one_buffer, sparse_position, sparse_indices,
            sparse_interleaved_base, accessor_minmax, mat3_padded]
