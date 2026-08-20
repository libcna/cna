# SPDX-License-Identifier: MS-PL
"""Draco fixtures -- owning group ``draco`` (plans/plan_gltf.md §20, §24.2).

The four bitstreams below are immutable output of google/draco 1.5.7 at CNA's pinned commit
``8786740086a9f4d83f44aa83badfbea4dce7a1b5``. They use ``MESH_SEQUENTIAL_ENCODING``, speed 5/5,
no quantisation, and explicit attribute unique IDs in the semantic order each fixture records.
``GltfDracoCorpusTests.cpp`` reconstructs the source meshes with that vendored encoder and requires
byte identity, so these are reproducible generated inputs rather than opaque captured blobs. The
Python generator remains standard-library-only and does not need to compile Draco to regenerate an
otherwise unchanged corpus.
"""

from __future__ import annotations

from ..builder import FLOAT, TRIANGLES, UNSIGNED_BYTE, UNSIGNED_SHORT, GltfBuilder, flatten
from ..manifest import Fixture, l3_primitive, mat_identity, world_positions
from .common import TRIANGLE_INDICES, TRIANGLE_MAX, TRIANGLE_MIN, TRIANGLE_NORMALS, TRIANGLE_POSITIONS

_KHR_DRACO = "KHR_draco_mesh_compression"

_POSITIONS = TRIANGLE_POSITIONS
_NORMALS = TRIANGLE_NORMALS
_TANGENTS = [(1.0, 0.0, 0.0, 1.0)] * 3
_TEXCOORDS = [(0.0, 0.0), (1.0, 0.0), (0.0, 1.0)]
_COLOR_RAW = [(255, 0, 0, 255), (0, 255, 0, 128), (0, 0, 255, 64)]
_COLORS = [(1.0, 0.0, 0.0, 1.0),
           (0.0, 1.0, 0.0, 128.0 / 255.0),
           (0.0, 0.0, 1.0, 64.0 / 255.0)]
_JOINTS = [(0, 1, 0, 0), (1, 0, 0, 0), (0, 1, 0, 0)]
_WEIGHTS = [(0.75, 0.25, 0.0, 0.0),
            (1.0, 0.0, 0.0, 0.0),
            (0.5, 0.5, 0.0, 0.0)]

_BASIC_STREAM = bytes.fromhex(
    "445241434f020201000000010301000102010300090300000109030001030902000200000000000000"
    "00000000000000000000803f0000000000000000000000000000803f000000000000000000000000"
    "0000803f00000000000000000000803f00000000000000000000803f00000000000000000000803f"
    "00000000000000000000803f")
_PBR_STREAM = bytes.fromhex(
    "445241434f020201000000010301000102010400090300000109030001040904000203090200030000"
    "00000000000000000000000000000000803f0000000000000000000000000000803f000000000000"
    "0000000000000000803f00000000000000000000803f00000000000000000000803f0000803f0000"
    "0000000000000000803f0000803f00000000000000000000803f0000803f00000000000000000000"
    "803f00000000000000000000803f00000000000000000000803f")
_COLOR_STREAM = bytes.fromhex(
    "445241434f020201000000010301000102010300090300000309020001020204010200000100000000"
    "00000000000000000000803f0000000000000000000000000000803f000000000000000000000000"
    "0000803f00000000000000000000803f000101000903551513551559150310b086291000d00f1004fe"
    "00000000ff000000")
_SKINNED_STREAM = bytes.fromhex(
    "445241434f020201000000010301000102010600090300000109030001040904000203090200030404"
    "04000404090400050000000001000000000000000000000000000000803f00000000000000000000"
    "00000000803f0000000000000000000000000000803f00000000000000000000803f000000000000"
    "00000000803f0000803f00000000000000000000803f0000803f00000000000000000000803f0000"
    "803f00000000000000000000803f00000000000000000000803f00000000000000000000803f0001"
    "0100020301400100320300000000010000000000403f0000803e00000000000000000000803f000000"
    "0000000000000000000000003f0000003f0000000000000000")


def _compressed_accessor(b: GltfBuilder, *, usage: str, values, accessor_type: str,
                         component_type: int = FLOAT, normalized: bool = False,
                         min_=None, max_=None) -> int:
    """Adds the metadata-only accessor KHR_draco_mesh_compression requires."""
    expected = flatten(values)
    if normalized:
        divisor = 255.0 if component_type == UNSIGNED_BYTE else 65535.0
        expected = [float(v) / divisor for v in expected]
    return b.add_accessor(
        usage=usage, component_type=component_type, accessor_type=accessor_type,
        count=len(values), expected=expected, buffer_view=None, normalized=normalized,
        min_=min_, max_=max_, encoded_with=_KHR_DRACO)


def _draco_primitive(b: GltfBuilder, stream: bytes, attributes: list[tuple],
                     *, targets=None) -> dict:
    """Adds metadata accessors plus one compressed view; list order is the Draco unique ID."""
    gltf_attributes = {}
    unique_ids = {}
    for unique_id, spec in enumerate(attributes):
        semantic, values, accessor_type, component_type, normalized = spec
        gltf_attributes[semantic] = _compressed_accessor(
            b, usage=semantic, values=values, accessor_type=accessor_type,
            component_type=component_type, normalized=normalized,
            min_=TRIANGLE_MIN if semantic == "POSITION" else None,
            max_=TRIANGLE_MAX if semantic == "POSITION" else None)
        unique_ids[semantic] = unique_id
    offset = b.append_bytes(stream, alignment=4)
    view = b.add_buffer_view(offset, len(stream))
    primitive = {
        "attributes": gltf_attributes,
        "mode": TRIANGLES,
        "extensions": {_KHR_DRACO: {"bufferView": view, "attributes": unique_ids}},
    }
    if targets is not None:
        primitive["targets"] = targets
    return primitive


def _regular_primitive(b: GltfBuilder, attributes: list[tuple], *, targets=None) -> dict:
    gltf_attributes = {}
    for semantic, values, accessor_type, component_type, normalized in attributes:
        gltf_attributes[semantic] = b.add_packed_accessor(
            usage=semantic, values=values, accessor_type=accessor_type,
            component_type=component_type, normalized=normalized,
            with_bounds=(semantic == "POSITION"))
    indices = b.add_packed_accessor(
        usage="indices", values=TRIANGLE_INDICES, accessor_type="SCALAR",
        component_type=UNSIGNED_SHORT)
    primitive = {"attributes": gltf_attributes, "indices": indices, "mode": TRIANGLES}
    if targets is not None:
        primitive["targets"] = targets
    return primitive


_BASIC_ATTRIBUTES = [
    ("POSITION", _POSITIONS, "VEC3", FLOAT, False),
    ("NORMAL", _NORMALS, "VEC3", FLOAT, False),
    ("TEXCOORD_0", _TEXCOORDS, "VEC2", FLOAT, False),
]
_PBR_ATTRIBUTES = [
    ("POSITION", _POSITIONS, "VEC3", FLOAT, False),
    ("NORMAL", _NORMALS, "VEC3", FLOAT, False),
    ("TANGENT", _TANGENTS, "VEC4", FLOAT, False),
    ("TEXCOORD_0", _TEXCOORDS, "VEC2", FLOAT, False),
]
_COLOR_ATTRIBUTES = [
    ("POSITION", _POSITIONS, "VEC3", FLOAT, False),
    ("TEXCOORD_0", _TEXCOORDS, "VEC2", FLOAT, False),
    ("COLOR_0", _COLOR_RAW, "VEC4", UNSIGNED_BYTE, True),
]
_SKINNED_ATTRIBUTES = _PBR_ATTRIBUTES + [
    ("JOINTS_0", _JOINTS, "VEC4", UNSIGNED_SHORT, False),
    ("WEIGHTS_0", _WEIGHTS, "VEC4", FLOAT, False),
]


def draco_triangle() -> Fixture:
    """Smallest real compressed primitive, including generated-tangent and face-list paths."""
    b = GltfBuilder("draco-triangle")
    primitive = _draco_primitive(b, _BASIC_STREAM, _BASIC_ATTRIBUTES)
    mesh = b.add_mesh([primitive], name="DracoTriangle")
    node = b.add_node(name="DracoTriangleNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    b.declare_extensions(required=[_KHR_DRACO])
    return Fixture(
        id="draco-triangle", owning_group="draco",
        description="A losslessly Draco-compressed triangle whose position, normal, UV and face "
                    "list are decoded from the extension bufferView. The tangent is generated "
                    "after decoding, proving the compressed path reaches the shared mesh logic.",
        builder=b, validated_layers=["L1", "L3", "L4", "L5"],
        features=["KHR_draco_mesh_compression", "decoded face connectivity",
                  "generated tangent after decompression"],
        spec_anchors=["KHR_draco_mesh_compression", "meshes-overview"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="DracoTriangle", primitive=0, mode=TRIANGLES,
            positions=_POSITIONS, normals=_NORMALS, texcoords=_TEXCOORDS,
            generated_tangents=_TANGENTS, indices=TRIANGLE_INDICES)]},
        l4=world_positions(b, {mesh: list(_POSITIONS)}),
    )


def draco_vs_uncompressed_pair() -> Fixture:
    """Two discriminating compressed/ordinary pairs covering the five rigid streams."""
    b = GltfBuilder("draco-vs-uncompressed-pair")
    primitives = [
        _draco_primitive(b, _PBR_STREAM, _PBR_ATTRIBUTES),
        _regular_primitive(b, _PBR_ATTRIBUTES),
        _draco_primitive(b, _COLOR_STREAM, _COLOR_ATTRIBUTES),
        _regular_primitive(b, _COLOR_ATTRIBUTES),
    ]
    mesh = b.add_mesh(primitives, name="DracoParityPairs")
    node = b.add_node(name="ParityNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    b.declare_extensions(required=[_KHR_DRACO])
    expected = [
        l3_primitive(mesh=mesh, mesh_name="DracoParityPairs", primitive=0, mode=TRIANGLES,
                     positions=_POSITIONS, normals=_NORMALS, tangents=_TANGENTS,
                     texcoords=_TEXCOORDS, indices=TRIANGLE_INDICES),
        l3_primitive(mesh=mesh, mesh_name="DracoParityPairs", primitive=1, mode=TRIANGLES,
                     positions=_POSITIONS, normals=_NORMALS, tangents=_TANGENTS,
                     texcoords=_TEXCOORDS, indices=TRIANGLE_INDICES),
        # plans/plan_gltf.md GLTF-462: the two vertex-colour primitives keep their metallic-roughness
        # material now, so they take the rigid PBR layout and need the generated tangent basis
        # stated. It is exactly solvable for this carrier: the UV axes match its +X/+Y geometry, so
        # the closed-form result is (+X, +1) at every vertex -- the same value `tangent-absent-
        # generated` pins independently.
        l3_primitive(mesh=mesh, mesh_name="DracoParityPairs", primitive=2, mode=TRIANGLES,
                     positions=_POSITIONS, texcoords=_TEXCOORDS, colors=_COLORS,
                     generated_tangents=_TANGENTS, indices=TRIANGLE_INDICES),
        l3_primitive(mesh=mesh, mesh_name="DracoParityPairs", primitive=3, mode=TRIANGLES,
                     positions=_POSITIONS, texcoords=_TEXCOORDS, colors=_COLORS,
                     generated_tangents=_TANGENTS, indices=TRIANGLE_INDICES),
    ]
    return Fixture(
        id="draco-vs-uncompressed-pair", owning_group="draco",
        description="A PBR pair and a vertex-colour pair, each authored once through Draco and "
                    "once through ordinary accessors. Together they compare POSITION, NORMAL, "
                    "TANGENT, TEXCOORD_0, COLOR_0 and connectivity without a layout silently "
                    "dropping the attribute under comparison.",
        builder=b, validated_layers=["L1", "L3", "L4", "L5"],
        features=["compressed/uncompressed L3 parity", "all rigid vertex streams",
                  "sequential point ordering"],
        spec_anchors=["KHR_draco_mesh_compression", "meshes-overview"],
        l3={"primitives": expected},
        l4=world_positions(b, {(mesh, i): list(_POSITIONS) for i in range(4)}),
    )


def draco_skinned() -> Fixture:
    """Compressed and ordinary skinned PBR primitives sharing one two-joint skin."""
    b = GltfBuilder("draco-skinned")
    primitives = [
        _draco_primitive(b, _SKINNED_STREAM, _SKINNED_ATTRIBUTES),
        _regular_primitive(b, _SKINNED_ATTRIBUTES),
    ]
    mesh = b.add_mesh(primitives, name="DracoSkinnedPair")
    joint1 = b.add_node(name="Joint1")
    joint0 = b.add_node(name="Joint0", children=[joint1])
    identity = mat_identity()
    ibm = b.add_packed_accessor(
        usage="inverseBindMatrices", values=[identity, identity], accessor_type="MAT4")
    b.add_skin({"name": "TwoJointSkin", "joints": [joint0, joint1],
                "inverseBindMatrices": ibm})
    mesh_node = b.add_node(name="SkinnedPairNode", mesh=mesh, skin=0)
    b.add_scene([joint0, mesh_node], name="Scene")
    b.set_default_scene(0)
    b.declare_extensions(required=[_KHR_DRACO])
    expected = [l3_primitive(
        mesh=mesh, mesh_name="DracoSkinnedPair", primitive=i, mode=TRIANGLES,
        positions=_POSITIONS, normals=_NORMALS, tangents=_TANGENTS,
        texcoords=_TEXCOORDS, joints=_JOINTS, weights=_WEIGHTS,
        indices=TRIANGLE_INDICES) for i in range(2)]
    return Fixture(
        id="draco-skinned", owning_group="draco",
        description="A two-joint, losslessly compressed skinned PBR triangle beside its ordinary "
                    "accessor twin. Non-trivial JOINTS_0 and WEIGHTS_0 prove both generic Draco "
                    "attributes reach the shared palette remap and stride-68 packer.",
        builder=b, validated_layers=["L1", "L3", "L4", "L5"],
        referencing_groups=["skinning"],
        features=["Draco with JOINTS_0 / WEIGHTS_0", "two-joint skin",
                  "compressed/uncompressed skin parity"],
        spec_anchors=["KHR_draco_mesh_compression", "skins", "skinned-mesh-attributes"],
        l3={"primitives": expected},
        l4=world_positions(b, {(mesh, i): list(_POSITIONS) for i in range(2)}),
    )


def draco_morph() -> Fixture:
    """A Draco base mesh and ordinary twin carrying the same three morph-delta streams."""
    b = GltfBuilder("draco-morph")
    position_deltas = [(0.25, 0.0, 0.0), (0.0, 0.5, 0.0), (0.0, 0.0, 0.75)]
    normal_deltas = [(0.0, 0.25, -0.25)] * 3
    tangent_deltas = [(0.0, 0.5, 0.0)] * 3
    targets = [{
        "POSITION": b.add_packed_accessor(
            usage="morph POSITION delta 0", values=position_deltas, accessor_type="VEC3",
            with_bounds=True),
        "NORMAL": b.add_packed_accessor(
            usage="morph NORMAL delta 0", values=normal_deltas, accessor_type="VEC3"),
        "TANGENT": b.add_packed_accessor(
            usage="morph TANGENT delta 0", values=tangent_deltas, accessor_type="VEC3"),
    }]
    primitives = [
        _draco_primitive(b, _PBR_STREAM, _PBR_ATTRIBUTES, targets=targets),
        _regular_primitive(b, _PBR_ATTRIBUTES, targets=targets),
    ]
    mesh = b.add_mesh(primitives, name="DracoMorphPair", weights=[0.5])
    node = b.add_node(name="MorphPairNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    b.declare_extensions(required=[_KHR_DRACO])
    expected = [l3_primitive(
        mesh=mesh, mesh_name="DracoMorphPair", primitive=i, mode=TRIANGLES,
        positions=_POSITIONS, normals=_NORMALS, tangents=_TANGENTS,
        texcoords=_TEXCOORDS, indices=TRIANGLE_INDICES) for i in range(2)]
    l4 = world_positions(b, {(mesh, i): list(_POSITIONS) for i in range(2)})
    l4["morph"] = {
        "targetCount": 1,
        "meshWeights": [0.5],
        "effectiveWeights": [0.5],
        "positionDeltas": [list(v) for v in position_deltas],
        "normalDeltas": [list(v) for v in normal_deltas],
        "tangentDeltas": [list(v) for v in tangent_deltas],
        "blendRule": "Both base encodings share the same ordinary morph accessors; parity "
                     "requires identical base buffers and identical three delta arrays.",
    }
    return Fixture(
        id="draco-morph", owning_group="draco",
        description="A losslessly compressed PBR base mesh beside its ordinary accessor twin, "
                    "both carrying the same asymmetric POSITION, NORMAL and TANGENT morph target. "
                    "The extension compresses primitive attributes, not morph target accessors.",
        builder=b, validated_layers=["L1", "L3", "L4", "L5"],
        referencing_groups=["animation"],
        features=["Draco base with morph target", "POSITION/NORMAL/TANGENT deltas",
                  "compressed/uncompressed morph parity"],
        spec_anchors=["KHR_draco_mesh_compression", "morph-targets"],
        l3={"primitives": expected}, l4=l4,
    )


FIXTURES = [draco_triangle, draco_vs_uncompressed_pair, draco_skinned, draco_morph]
