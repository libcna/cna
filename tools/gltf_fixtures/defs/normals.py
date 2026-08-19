# SPDX-License-Identifier: MS-PL
"""Normal / tangent fixtures -- owning group ``normals`` (plan_gltf.md §24.2's "Normals / tangents").

Specification: §3.7.2.1 ``meshes-overview`` -- "When normals are not specified, client
implementations MUST calculate flat normals and the provided tangents are ignored."
"""

from __future__ import annotations

import math

from .. import flatnormals
from ..builder import SHORT, TRIANGLES, UNSIGNED_SHORT, GltfBuilder
from ..manifest import Fixture, l3_primitive, world_positions
from ..png import encode_png
from .common import TRIANGLE_INDICES, TRIANGLE_NORMALS, TRIANGLE_POSITIONS

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
    # SHORT NORMAL is not a core glTF 2.0 vertex format. KHR_mesh_quantization both permits it
    # and requires the extension to be listed as required; each VEC3 also needs 4-byte element
    # alignment, hence the 8-byte stride rather than six tightly packed bytes.
    b.declare_extensions(required=["KHR_mesh_quantization"])
    position = b.add_packed_accessor(usage="POSITION", values=_QUANTIZED_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=_QUANTIZED_RAW_SHORTS,
                                   accessor_type="VEC3", component_type=SHORT, normalized=True,
                                   byte_stride=8)
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
        features=["KHR_mesh_quantization", "NORMAL as normalized SHORT",
                  "8-byte aligned VEC3 elements", "§3.6.2.2 normalized decode",
                  "authored normal passed through byte-exact"],
        spec_anchors=["meshes-overview", "accessors"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="QuantizedNormalTri", primitive=0, mode=TRIANGLES,
            positions=_QUANTIZED_POSITIONS, normals=decoded, indices=[0, 1, 2])]},
        l4=l4,
    )


# A canonical UV frame: increasing U follows +X and increasing V follows +Y. On the +Z carrier
# triangle it makes the generated tangent exactly (+X,+1) and the reconstructed bitangent +Y, so
# none of the expectations below depend on decimal approximations.
_FRAME_UVS = [(0.0, 0.0), (1.0, 0.0), (0.0, 1.0)]
_TANGENT_POSITIVE = [(1.0, 0.0, 0.0, 1.0)] * 3
_TANGENT_NEGATIVE = [(1.0, 0.0, 0.0, -1.0)] * 3


def _handedness_normal_map() -> bytes:
    """A constant tangent-space +Y normal whose result changes when ``TANGENT.w`` changes."""
    pixel = (128, 255, 128, 255)
    return encode_png(2, 2, [[pixel, pixel], [pixel, pixel]])


def _add_handedness_material(builder: GltfBuilder, name: str) -> tuple[int, dict]:
    """Adds the discriminating normal map and returns its material plus the L3 expectation."""
    image = builder.add_image(_handedness_normal_map(), name=f"{name}Normal")
    texture = builder.add_texture(source=image, name=f"{name}NormalTexture")
    material = builder.add_material({
        "name": name,
        "normalTexture": {"index": texture},
    })
    expected = {
        "index": material,
        "name": name,
        "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
        "metallicFactor": 1.0,
        "roughnessFactor": 1.0,
        "emissiveFactor": [0.0, 0.0, 0.0],
        "alphaMode": "OPAQUE",
        "alphaCutoff": 0.5,
        "doubleSided": False,
        "hasBaseColorTexture": False,
        "hasNormalTexture": True,
        "hasMetallicRoughnessTexture": False,
        "hasOcclusionTexture": False,
        "hasEmissiveTexture": False,
    }
    return material, expected


def tangent_handedness() -> Fixture:
    """Two adjacent primitives whose only tangent-basis difference is ``TANGENT.w``."""
    b = GltfBuilder("tangent-handedness")
    material, expected_material = _add_handedness_material(b, "OppositeHandedness")
    left_positions = [(-1.0, 0.0, 0.0), (0.0, 0.0, 0.0), (-1.0, 1.0, 0.0)]
    right_positions = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (1.0, 1.0, 0.0)]
    left_position = b.add_packed_accessor(
        usage="POSITION (positive handedness)", values=left_positions,
        accessor_type="VEC3", with_bounds=True)
    right_position = b.add_packed_accessor(
        usage="POSITION (negative handedness)", values=right_positions,
        accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(
        usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    positive = b.add_packed_accessor(
        usage="TANGENT (positive handedness)", values=_TANGENT_POSITIVE,
        accessor_type="VEC4")
    negative = b.add_packed_accessor(
        usage="TANGENT (negative handedness)", values=_TANGENT_NEGATIVE,
        accessor_type="VEC4")
    texcoord = b.add_packed_accessor(
        usage="TEXCOORD_0", values=_FRAME_UVS, accessor_type="VEC2")
    indices = b.add_packed_accessor(
        usage="indices", values=TRIANGLE_INDICES, accessor_type="SCALAR",
        component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([
        {"attributes": {"POSITION": left_position, "NORMAL": normal,
                        "TANGENT": positive, "TEXCOORD_0": texcoord},
         "indices": indices, "material": material, "mode": TRIANGLES},
        {"attributes": {"POSITION": right_position, "NORMAL": normal,
                        "TANGENT": negative, "TEXCOORD_0": texcoord},
         "indices": indices, "material": material, "mode": TRIANGLES},
    ], name="OppositeHandednessQuad")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    l4 = world_positions(b, {(mesh, 0): left_positions, (mesh, 1): right_positions})
    l4["tangentFrames"] = [
        {"mesh": mesh, "primitive": 0, "normal": [0.0, 0.0, 1.0],
         "tangent": [1.0, 0.0, 0.0, 1.0], "reconstructedBitangent": [0.0, 1.0, 0.0]},
        {"mesh": mesh, "primitive": 1, "normal": [0.0, 0.0, 1.0],
         "tangent": [1.0, 0.0, 0.0, -1.0], "reconstructedBitangent": [0.0, -1.0, 0.0]},
    ]
    return Fixture(
        id="tangent-handedness", audit_fixture=None, owning_group="normals",
        description="Two adjacent primitives author the same +Z normal and +X tangent direction, "
                    "but opposite TANGENT.w signs. Reconstructing B=cross(N,T)*w therefore gives "
                    "+Y on one half and -Y on the other. A non-flat tangent-space +Y normal map "
                    "makes dropping or defaulting the sign visibly wrong rather than merely "
                    "different in an unused vertex field.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4", "L5"],
        features=["opposite TANGENT.w signs", "bitangent reconstruction",
                  "two primitives in one mesh", "byte-exact tangent stream",
                  "normal map with non-zero tangent-space Y"],
        spec_anchors=["meshes-overview"],
        l3={"primitives": [
            l3_primitive(
                mesh=mesh, mesh_name="OppositeHandednessQuad", primitive=0, mode=TRIANGLES,
                positions=left_positions, normals=TRIANGLE_NORMALS,
                tangents=_TANGENT_POSITIVE, texcoords=_FRAME_UVS, indices=TRIANGLE_INDICES,
                material=expected_material),
            l3_primitive(
                mesh=mesh, mesh_name="OppositeHandednessQuad", primitive=1, mode=TRIANGLES,
                positions=right_positions, normals=TRIANGLE_NORMALS,
                tangents=_TANGENT_NEGATIVE, texcoords=_FRAME_UVS, indices=TRIANGLE_INDICES,
                material=expected_material),
        ]},
        l4=l4,
    )


def tangent_absent_generated() -> Fixture:
    """A UV-mapped PBR triangle with no authored tangent and an exactly solvable fallback basis."""
    b = GltfBuilder("tangent-absent-generated")
    position = b.add_packed_accessor(
        usage="POSITION", values=TRIANGLE_POSITIONS, accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(
        usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    texcoord = b.add_packed_accessor(
        usage="TEXCOORD_0", values=_FRAME_UVS, accessor_type="VEC2")
    indices = b.add_packed_accessor(
        usage="indices", values=TRIANGLE_INDICES, accessor_type="SCALAR",
        component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal, "TEXCOORD_0": texcoord},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="GeneratedTangentTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["generatedTangentBasis"] = {
        "algorithm": "angle-weighted UV gradients followed by Gram-Schmidt",
        "tangents": [list(t) for t in _TANGENT_POSITIVE],
        "unitLength": True,
        "perpendicularToNormal": True,
    }
    return Fixture(
        id="tangent-absent-generated", audit_fixture=None, owning_group="normals",
        description="A +Z triangle whose UV axes exactly match its +X/+Y geometry, with no "
                    "TANGENT accessor. CNA's documented fallback has the closed-form result "
                    "(+X,+1) at every vertex; direction, unit length and handedness are all "
                    "independently observable.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4", "L5"],
        features=["absent TANGENT", "angle-weighted tangent generation", "Gram-Schmidt",
                  "unit generated tangent", "generated handedness +1"],
        spec_anchors=["meshes-overview"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="GeneratedTangentTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS,
            generated_tangents=_TANGENT_POSITIVE, texcoords=_FRAME_UVS,
            indices=TRIANGLE_INDICES)]},
        l4=l4,
    )


_NONUNIFORM_NORMAL = (0.6, 0.8, 0.0)
_NONUNIFORM_NORMALS = [_NONUNIFORM_NORMAL] * 3
# Exact Rz(90deg)*S(2,3,4), column-major. Using a matrix rather than a quaternion keeps the
# independent normal-matrix expectation free of tiny sin/cos round-off values.
_NONUNIFORM_WORLD = [
    0.0, 2.0, 0.0, 0.0,
    -3.0, 0.0, 0.0, 0.0,
    0.0, 0.0, 4.0, 0.0,
    0.0, 0.0, 0.0, 1.0,
]
_NONUNIFORM_NORMAL_MATRIX = [
    0.0, 0.5, 0.0,
    -1.0 / 3.0, 0.0, 0.0,
    0.0, 0.0, 0.25,
]
_NONUNIFORM_WORLD_NORMAL_RAW = (-0.8 / 3.0, 0.6 / 2.0, 0.0)
_NONUNIFORM_WORLD_NORMAL_LENGTH = math.sqrt(sum(v * v for v in _NONUNIFORM_WORLD_NORMAL_RAW))
_NONUNIFORM_WORLD_NORMAL = tuple(
    v / _NONUNIFORM_WORLD_NORMAL_LENGTH for v in _NONUNIFORM_WORLD_NORMAL_RAW)


def normal_nonuniform_scale() -> Fixture:
    """A slanted authored normal under an asymmetric rotated non-uniform scale."""
    b = GltfBuilder("normal-nonuniform-scale")
    position = b.add_packed_accessor(
        usage="POSITION", values=TRIANGLE_POSITIONS, accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(
        usage="NORMAL", values=_NONUNIFORM_NORMALS, accessor_type="VEC3")
    indices = b.add_packed_accessor(
        usage="indices", values=TRIANGLE_INDICES, accessor_type="SCALAR",
        component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="NonUniformNormalTri")
    node = b.add_node(name="RotatedScaledNode", mesh=mesh, matrix=_NONUNIFORM_WORLD)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["normalTransform"] = {
        "node": node,
        "mesh": mesh,
        "sourceNormal": list(_NONUNIFORM_NORMAL),
        "normalMatrixColumnMajor": list(_NONUNIFORM_NORMAL_MATRIX),
        "worldNormal": list(_NONUNIFORM_WORLD_NORMAL),
        "naiveWorld3x3Normal": [-2.4 / math.sqrt(7.2), 1.2 / math.sqrt(7.2), 0.0],
    }
    return Fixture(
        id="normal-nonuniform-scale", audit_fixture=None, owning_group="normals",
        description="A (0.6,0.8,0) authored normal under exact Rz90*S(2,3,4). The correct "
                    "inverse-transpose sends it toward (-0.2667,0.3,0), while the naive world "
                    "3x3 sends it toward (-2.4,1.2,0); rotation makes matrix transposition errors "
                    "visible as well as the non-uniform scale error.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4", "L5"],
        referencing_groups=["transforms"],
        features=["rotated non-uniform scale", "inverse-transpose normal matrix",
                  "slanted authored normal", "normal renormalisation after transform"],
        spec_anchors=["meshes-overview", "transformations"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="NonUniformNormalTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=_NONUNIFORM_NORMALS,
            indices=TRIANGLE_INDICES)]},
        l4=l4,
    )


def tangent_mirrored() -> Fixture:
    """One tangent-bearing mesh placed once normally and once by a negative determinant."""
    b = GltfBuilder("tangent-mirrored")
    material, expected_material = _add_handedness_material(b, "MirroredHandedness")
    position = b.add_packed_accessor(
        usage="POSITION", values=TRIANGLE_POSITIONS, accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(
        usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    tangent = b.add_packed_accessor(
        usage="TANGENT", values=_TANGENT_POSITIVE, accessor_type="VEC4")
    texcoord = b.add_packed_accessor(
        usage="TEXCOORD_0", values=_FRAME_UVS, accessor_type="VEC2")
    indices = b.add_packed_accessor(
        usage="indices", values=TRIANGLE_INDICES, accessor_type="SCALAR",
        component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal, "TANGENT": tangent,
                       "TEXCOORD_0": texcoord},
        "indices": indices,
        "material": material,
        "mode": TRIANGLES,
    }], name="SharedTangentTri")
    ordinary = b.add_node(name="Ordinary", mesh=mesh)
    mirrored = b.add_node(name="Mirrored", mesh=mesh, translation=[3.0, 0.0, 0.0],
                          scale=[-1.0, 1.0, 1.0])
    b.add_scene([ordinary, mirrored], name="Scene")
    b.set_default_scene(0)
    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["tangentPlacements"] = [
        {"node": ordinary, "mirrored": False, "worldNormal": [0.0, 0.0, 1.0],
         "worldTangent": [1.0, 0.0, 0.0], "worldHandedness": 1.0,
         "worldBitangent": [0.0, 1.0, 0.0]},
        {"node": mirrored, "mirrored": True, "worldNormal": [0.0, 0.0, 1.0],
         "worldTangent": [-1.0, 0.0, 0.0], "worldHandedness": -1.0,
         "worldBitangent": [0.0, 1.0, 0.0]},
    ]
    return Fixture(
        id="tangent-mirrored", audit_fixture=None, owning_group="normals",
        description="One mesh with authored (+X,+1) tangents is instanced normally and through "
                    "T(3,0,0)*S(-1,1,1). The shared vertex buffer must stay local, while the "
                    "mirrored draw transforms T to -X and multiplies w by sign(det(world))=-1 so "
                    "the reconstructed world bitangent remains +Y. A tangent-space +Y normal map "
                    "makes that per-draw determinant correction visible.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4", "L5"],
        referencing_groups=["transforms", "scenes"],
        features=["negative-determinant placement", "shared tangent vertex buffer",
                  "per-draw handedness sign", "mirrored tangent direction",
                  "normal map with non-zero tangent-space Y"],
        spec_anchors=["meshes-overview", "transformations"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="SharedTangentTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS,
            tangents=_TANGENT_POSITIVE, texcoords=_FRAME_UVS,
            indices=TRIANGLE_INDICES, material=expected_material)]},
        l4=l4,
    )




#: `tangent-without-normal`'s tilted triangle -- the same geometry `normal-absent` uses, for the
#: same reason: its computed flat normal `(0, -1/sqrt2, 1/sqrt2)` is not `(0,0,1)`.
_TANGENT_NO_NORMAL_POSITIONS = _TILTED_POSITIONS
#: A UV set whose gradient runs along +X, so the tangent §3.7.2.1 requires a reader to GENERATE is
#: `(1,0,0)` with `w = +1`.
_TANGENT_NO_NORMAL_UVS = [(0.0, 0.0), (1.0, 0.0), (0.0, 1.0)]
#: The authored tangent, chosen to share NO component with the generated one. `(0,1,0,+1)` is not
#: even perpendicular to the flat normal above, so a reader that keeps it hands the shader a skewed
#: frame -- and the two answers cannot be confused for a rounding difference.
_AUTHORED_TANGENT = [(0.0, 1.0, 0.0, 1.0)] * 3
_GENERATED_TANGENT = [(1.0, 0.0, 0.0, 1.0)] * 3


def tangent_without_normal() -> Fixture:
    """``TANGENT`` authored, ``NORMAL`` absent -- the tangent MUST be ignored. Owns **`GLTF-461`**.

    §3.7.2.1 does not stop at "MUST calculate flat normals". It continues "and the provided tangents
    (if present) **MUST** be ignored", and the reason is not arbitrary: an authored tangent basis was
    built against the normals the file then failed to supply, so keeping it pairs a tangent with a
    normal it is not orthogonal to and normal mapping lights the surface through a skewed frame.

    The discrimination is that the authored and the required answers share no component. Authored is
    ``(0,1,0,+1)``; regenerated from the UVs is ``(1,0,0,+1)``. A reader that honours the authored
    tangent, and one that follows the specification, cannot produce values close enough to be read as
    the same result with different rounding.

    Promoted from an inline test document by `GLTF-464`: it is a conformance statement about the
    file format, which `docs/gltf-conformance.md` §3.8 puts in this corpus rather than in a test.
    """
    b = GltfBuilder("tangent-without-normal")
    position = b.add_packed_accessor(usage="POSITION", values=_TANGENT_NO_NORMAL_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    tangent = b.add_packed_accessor(usage="TANGENT", values=_AUTHORED_TANGENT,
                                    accessor_type="VEC4")
    uv = b.add_packed_accessor(usage="TEXCOORD_0", values=_TANGENT_NO_NORMAL_UVS,
                               accessor_type="VEC2")
    indices = b.add_packed_accessor(usage="indices", values=[0, 1, 2], accessor_type="SCALAR",
                                    component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "TANGENT": tangent, "TEXCOORD_0": uv},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="TangentNoNormal")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)

    l4 = world_positions(b, {mesh: list(_TANGENT_NO_NORMAL_POSITIONS)})
    l4["ignoredTangent"] = {
        "authoredTangents": [list(t) for t in _AUTHORED_TANGENT],
        "requiredTangents": [list(t) for t in _GENERATED_TANGENT],
        "rule": "§3.7.2.1: when NORMAL is absent the provided tangents MUST be ignored, and the "
                "basis is regenerated from the positions and UVs instead.",
        "discriminationRule": "The authored and required tangents share no component, so honouring "
                              "the authored one is a different vector rather than a near miss.",
    }
    return Fixture(
        id="tangent-without-normal", audit_fixture=None, owning_group="normals",
        description="A tilted triangle authoring TANGENT and TEXCOORD_0 but no NORMAL. §3.7.2.1 "
                    "requires the flat normal to be computed AND the authored tangent to be "
                    "ignored; the authored (0,1,0,+1) and the regenerated (1,0,0,+1) share no "
                    "component, so the two behaviours cannot be confused.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4"],
        features=["absent NORMAL", "authored TANGENT ignored", "regenerated tangent basis"],
        spec_anchors=["meshes-overview"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="TangentNoNormal", primitive=0, mode=TRIANGLES,
            positions=_TANGENT_NO_NORMAL_POSITIONS, normals=_EXPECTED_NORMALS,
            generated_tangents=_GENERATED_TANGENT, texcoords=_TANGENT_NO_NORMAL_UVS,
            indices=[0, 1, 2])]},
        l4=l4,
    )


#: `morph-normalless-quad`'s four corners: a unit quad in the XY plane as two triangles.
_MORPH_QUAD_POSITIONS = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (1.0, 1.0, 0.0), (0.0, 1.0, 0.0)]
#: Triangles (0,1,2) and (0,2,3). Vertices 0 and 2 are shared, and vertex 2 is the one the target
#: lifts -- so once morphed the two faces genuinely disagree.
_MORPH_QUAD_INDICES = [0, 1, 2, 0, 2, 3]
#: The target lifts ONLY the shared vertex. A delta on all four would translate the quad and leave
#: its normals alone, which is exactly the case a reader that never recomputes still passes.
_MORPH_QUAD_DELTAS = [(0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 1.0), (0.0, 0.0, 0.0)]


def morph_normalless_quad() -> Fixture:
    """A normal-less quad whose morph target lifts the vertex both triangles share. Owns **`GLTF-461`**.

    §3.7.2.2: "When the base mesh primitive does not specify normals, client implementations **MUST**
    calculate flat normals for each morph target." Every part of this fixture is load-bearing:

    * **no NORMAL** makes §3.7.2.1's flat normals required in the first place;
    * **a morph target** makes §3.7.2.2's *per target* clause apply, which forces the per-corner
      split policy -- two faces coplanar at rest need not stay coplanar, so a rest-pose split cannot
      serve every reachable pose;
    * the delta lifts **only the vertex both triangles share**, so at four vertices there is no value
      that is both faces' normal, and a reader that computed once at rest is wrong at every non-zero
      weight while still looking plausible at weight 0; and
    * because the delta is on a *shared* vertex, **both** of its copies must receive it -- a gather
      that dropped one would tear the surface along the diagonal.

    Promoted from an inline test document by `GLTF-464`, for `docs/gltf-conformance.md` §3.8's rule:
    this is a statement about the format, not about loader machinery.
    """
    b = GltfBuilder("morph-normalless-quad")
    position = b.add_packed_accessor(usage="POSITION", values=_MORPH_QUAD_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    delta = b.add_packed_accessor(usage="morph POSITION delta 0", values=_MORPH_QUAD_DELTAS,
                                  accessor_type="VEC3", with_bounds=True)
    indices = b.add_packed_accessor(usage="indices", values=_MORPH_QUAD_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position},
        "indices": indices,
        "targets": [{"POSITION": delta}],
        "mode": TRIANGLES,
    }], name="NormallessMorphQuad", weights=[1.0])
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)

    # Stated from flatnormals.py rather than from CNA, so the golden is a second opinion.
    split = flatnormals.compute(_MORPH_QUAD_POSITIONS, _MORPH_QUAD_INDICES, per_corner=True)
    split_positions = flatnormals.gather(_MORPH_QUAD_POSITIONS, split.source_vertex)
    split_deltas = flatnormals.gather(_MORPH_QUAD_DELTAS, split.source_vertex)
    morphed = [[p[0] + d[0], p[1] + d[1], p[2] + d[2]]
               for p, d in zip(split_positions, split_deltas)]

    l4 = world_positions(b, {mesh: list(_MORPH_QUAD_POSITIONS)})
    # The standard morph block, so this fixture answers the corpus-wide `morph-*` family sweep like
    # every other member rather than opting out of it. `blendedPositions` is stated in the SPLIT
    # numbering, because that sweep compares against the IMPORTED buffer and §3.7.2.2 forces this
    # primitive to be split -- and that is what makes the comparison a real check rather than a
    # self-agreement: the split comes from `flatnormals.py`, which is an independent implementation
    # of §3.7.2.1, not a restatement of what CNA produced.
    l4["morph"] = {
        "targetCount": 1,
        "meshWeights": [1.0],
        "effectiveWeights": [1.0],
        "targetsWithoutPositions": 0,
        "targetsWithoutNormals": 1,
        "targetsWithoutTangents": 1,
        "blendedPositions": morphed,
    }
    l4["morphedFlatNormals"] = {
        "targetCount": 1,
        "meshWeights": [1.0],
        "effectiveWeights": [1.0],
        "authoredVertexCount": len(_MORPH_QUAD_POSITIONS),
        "vertexCount": split.vertex_count,
        "sourceVertex": list(split.source_vertex),
        "splitIndices": list(split.indices),
        "authoredMorphDeltas": [list(d) for d in _MORPH_QUAD_DELTAS],
        "morphDeltas": [list(d) for d in split_deltas],
        "restFlatNormals": [list(n) for n in split.normals],
        "fullyMorphedPositions": morphed,
        # The two faces at weight 1, from the morphed positions rather than restated: (0,1,2) rises
        # on one diagonal and (0,2,3) on the other, so they disagree exactly as the design intends.
        "fullyMorphedFaceNormals": [
            list(n) for n in flatnormals.compute(
                [tuple(p) for p in morphed], list(split.indices), per_corner=True).normals],
        "rule": "§3.7.2.2 requires flat normals to be recomputed for EACH morph target, so the "
                "normal of a normal-less primitive is a function of the WEIGHTS and no buffer can "
                "hold it. The per-corner split is what makes an exact per-face normal expressible "
                "at every reachable pose.",
        "discriminationRule": "Only the shared vertex moves. A delta applied to all four corners "
                              "would translate the quad and leave both face normals unchanged -- "
                              "which is precisely the case a reader that never recomputes passes.",
    }
    return Fixture(
        id="morph-normalless-quad", audit_fixture=None, owning_group="normals",
        description="A quad as two triangles with no NORMAL and a morph target that lifts the one "
                    "vertex both triangles share. §3.7.2.2 requires flat normals per target, which "
                    "forces the per-corner split (4 source vertices become 6) and makes the "
                    "normals a function of the weights rather than of the file.",
        builder=b, validated_layers=["L1", "L2", "L3", "L4", "L5"],
        features=["absent NORMAL", "morph target", "per-corner flat-normal split",
                  "delta on a shared vertex"],
        spec_anchors=["meshes-overview", "morph-targets"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="NormallessMorphQuad", primitive=0, mode=TRIANGLES,
            positions=_MORPH_QUAD_POSITIONS, indices=_MORPH_QUAD_INDICES,
            flat_normals="per-corner")]},
        l4=l4,
    )


FIXTURES = [normal_absent, normal_quantized, tangent_handedness,
            tangent_absent_generated, normal_nonuniform_scale, tangent_mirrored,
            # GLTF-464: promoted from inline test documents.
            tangent_without_normal, morph_normalless_quad]
