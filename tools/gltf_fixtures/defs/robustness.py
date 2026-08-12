# SPDX-License-Identifier: MS-PL
"""Malformed-input fixtures -- owning group ``robustness`` (plan_gltf.md §24.2).

Each asset here violates a structural constraint whose violation would make decoding read outside
the file's own buffers. They exist to prove the validation pass actually runs: without them,
`cgltf_validate` could be removed again and every other test would stay green.

Specification: §3.6.2 ``accessors``, §3.6.2.4 ``data-alignment``.
"""

from __future__ import annotations

from ..builder import (FLOAT, TRIANGLES, UNSIGNED_BYTE, UNSIGNED_SHORT, GltfBuilder, flatten,
                       pack)
from ..l5 import unsupported as l5_unsupported
from ..manifest import Fixture, l3_primitive, mat_identity, world_positions
from .common import TRIANGLE_INDICES, TRIANGLE_NORMALS, TRIANGLE_POSITIONS


def bad_accessor_out_of_bounds() -> Fixture:
    """A POSITION accessor declaring more elements than its ``bufferView`` can hold.

    Proves **`GLTF-021`**. The accessor says three `VEC3` floats (36 bytes) live in a bufferView
    that is only 24 bytes long, so decoding it reads 12 bytes past the end of the view -- and, for
    the last vertex, past the buffer itself. This is precisely the class of constraint
    `cgltf_validate` exists to catch, and precisely why its failure is a rejection rather than a
    warning: there is no partially-correct import to salvage.
    """
    b = GltfBuilder("bad-accessor-out-of-bounds")
    # Packed honestly, then described dishonestly: the bufferView is deliberately declared two
    # vertices long while the accessor claims three. Authoring the lie in the *description* rather
    # than the data keeps the bytes themselves readable.
    packed = pack(flatten(TRIANGLE_POSITIONS), FLOAT)
    offset = b.append_bytes(packed, alignment=4)
    short_view = b.add_buffer_view(offset, len(packed) - 12)
    position = b.add_accessor(
        usage="POSITION", component_type=FLOAT, accessor_type="VEC3", count=3,
        expected=flatten(TRIANGLE_POSITIONS), buffer_view=short_view,
        min_=[0.0, 0.0, 0.0], max_=[1.0, 1.0, 0.0])
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="TruncatedTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="bad-accessor-out-of-bounds", audit_fixture=None, owning_group="robustness",
        description="A POSITION accessor whose declared element count reaches 12 bytes past the "
                    "end of its bufferView. Decoding it would read outside the file's own buffer, "
                    "so it must be rejected at validation rather than imported with whatever "
                    "happened to be in adjacent memory.",
        builder=b, validated_layers=["L1"],
        features=["accessor beyond bufferView", "structural validation", "import rejection"],
        spec_anchors=["accessors", "data-alignment"],
        # No L3 expectation: there is no correct semantic mesh for a file this malformed, and
        # inventing one would imply a conforming reader could produce something.
        l3={"primitives": []},
        l4={},
        l5=l5_unsupported("The asset is rejected at validation, so no buffers are produced.",
                          ["GLTF-021"]),
        rejection={
            "stage": "validation",
            "task": "GLTF-021",
            "errorContains": ["validation", "bad-accessor-out-of-bounds"],
            "note": "cgltf_validate's own check set is entirely of this kind, which is what makes "
                    "'reject on failure' the whole severity policy rather than one branch of it.",
        },
    )


def accessor_count_mismatch() -> Fixture:
    """A primitive whose ``NORMAL`` accessor is shorter than its ``POSITION``. Proves **`GLTF-060`**.

    §3.7.2.1 requires every attribute of a primitive to have the same count, and CNA depends on it
    absolutely: ``POSITION``'s count drives the loop that indexes every other decoded stream, so a
    ``NORMAL`` one element short is read past the end of its own vector. Nothing checked it, so a
    malformed file of this shape was undefined behaviour rather than an error.

    Both accessors are *honestly* described here -- each really does hold what it claims. The file
    is malformed because the two disagree with each other, which is a different failure from
    `bad-accessor-out-of-bounds`, where one accessor lies about its own bufferView.

    It is refused **twice**, and both refusals matter. ``cgltf_validate`` catches the disagreement,
    so both loaders reject the file before extraction is reached. But ``ExtractMesh`` is also called
    directly, without validation -- the L3 oracle does exactly that -- and had no check of its own,
    so a short ``NORMAL`` was simply read past the end of its vector.
    """
    b = GltfBuilder("accessor-count-mismatch")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    # Two normals for three positions. Truthfully packed and truthfully described -- the lie is
    # only in the relationship between the two accessors.
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS[:2],
                                   accessor_type="VEC3")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="MismatchedTri")
    node = b.add_node(name="MeshNode", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)
    return Fixture(
        id="accessor-count-mismatch", audit_fixture=None, owning_group="robustness",
        description="A primitive whose NORMAL accessor holds two elements while its POSITION holds "
                    "three. Every accessor is individually valid; the file is malformed because "
                    "they disagree. Refused by structural validation and, independently, by "
                    "extraction -- which matters because extraction is also called directly.",
        builder=b, validated_layers=["L1", "L2"],
        features=["attribute count mismatch", "per-primitive attribute agreement",
                  "import rejection"],
        spec_anchors=["meshes-overview", "accessors"],
        l3={"primitives": []},
        l4={},
        l5=l5_unsupported("The primitive is refused at extraction, so no buffers are produced.",
                          ["GLTF-060"]),
        rejection={
            "stage": "validation",
            "task": "GLTF-060",
            "errorContains": ["structural validation"],
            "alsoRefusedAtExtraction": True,
            "extractionErrorContains": ["NORMAL", "same count"],
            "note": "Refused TWICE, and both matter. cgltf_validate catches the disagreement "
                    "(code 4), so every path that validates -- both loaders -- rejects the file "
                    "before extraction is reached. But ExtractMesh is also called DIRECTLY, "
                    "without validation, by the L3 oracle itself, and it had no check of its own: "
                    "POSITION's count drives the loop that indexes every other decoded stream, so "
                    "a short NORMAL was read past the end of its vector. GLTF-060's check turns "
                    "that undefined behaviour into a named error for any caller, which is why it "
                    "is not redundant with the validation cgltf already performs.",
        },
    )


#: `skin-joint-index-out-of-range`: vertex 1's second influence names joint 5 in a two-joint skin,
#: and carries real weight. Vertices 0 and 2 name joint 9 in slots that carry NO weight -- the
#: universal exporter padding pattern, which must survive.
_STRAY_JOINTS = [(0, 9, 0, 0), (0, 5, 0, 0), (1, 9, 0, 0)]
_STRAY_WEIGHTS = [(1.0, 0.0, 0.0, 0.0), (0.6, 0.4, 0.0, 0.0), (1.0, 0.0, 0.0, 0.0)]

#: The same file with vertex 1's stray influence de-weighted. Every out-of-range index is now
#: unweighted, so nothing is refused and the mesh imports.
_PADDED_JOINTS = [(0, 9, 0, 0), (0, 5, 0, 0), (1, 9, 0, 0)]
_PADDED_WEIGHTS = [(1.0, 0.0, 0.0, 0.0), (1.0, 0.0, 0.0, 0.0), (1.0, 0.0, 0.0, 0.0)]
#: What CNA imports: every out-of-range index clamped to joint 0. Stated separately from the
#: authored values for the same reason `skin-unnormalized` states its renormalised weights -- L2
#: holds what the accessor decodes to, L3 holds what the importer understood the mesh to be.
_PADDED_JOINTS_IMPORTED = [(0, 0, 0, 0), (0, 0, 0, 0), (1, 0, 0, 0)]


def _stray_joint_builder(name: str, joints, weights) -> tuple:
    """Shared body of the two joint-index fixtures: one two-joint skin over one triangle."""
    b = GltfBuilder(name)
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    joints_acc = b.add_packed_accessor(usage="JOINTS_0", values=joints,
                                       accessor_type="VEC4", component_type=UNSIGNED_BYTE)
    weights_acc = b.add_packed_accessor(usage="WEIGHTS_0", values=weights, accessor_type="VEC4")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)

    inverse_bind = mat_identity()
    ibm_offset = b.append_bytes(pack(list(inverse_bind) * 2, FLOAT), alignment=4)
    ibm = b.add_accessor(usage="inverseBindMatrices", component_type=FLOAT, accessor_type="MAT4",
                         count=2, expected=list(inverse_bind) * 2,
                         buffer_view=b.add_buffer_view(ibm_offset, 128))

    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "JOINTS_0": joints_acc, "WEIGHTS_0": weights_acc},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="StrayJointTri")
    joint0 = b.add_node(name="Joint0")
    joint1 = b.add_node(name="Joint1")
    mesh_node = b.add_node(name="SkinnedMeshNode", mesh=mesh, skin=0)
    b.add_skin({"name": "Skin", "joints": [joint0, joint1], "inverseBindMatrices": ibm})
    b.add_scene([joint0, joint1, mesh_node], name="Scene")
    b.set_default_scene(0)
    return b, mesh


def skin_joint_index_out_of_range() -> Fixture:
    """A **weighted** influence naming a joint the skin does not declare. Proves **`GLTF-254`**.

    §3.7.3.3 makes a ``JOINTS_0`` value an index into the skin's own ``joints`` array. CNA read one
    out of range and fell back to joint **0** -- binding the vertex to the root and dragging it
    there, which is the exact collapse this campaign exists to remove, arriving from a malformed
    file rather than a bug. ``cgltf_validate`` does not check it.

    The refusal is deliberately conditional on the weight, which is why this fixture has a twin.
    Here vertex 1's stray influence carries weight 0.4 and genuinely changes the vertex's position,
    so the file is refused. In `skin-joint-index-padding` the same stray indices carry no weight at
    all and the file imports, because that is the universal exporter padding pattern and refusing it
    would reject a large share of real assets to prevent nothing.
    """
    b, mesh = _stray_joint_builder("skin-joint-index-out-of-range", _STRAY_JOINTS, _STRAY_WEIGHTS)
    return Fixture(
        id="skin-joint-index-out-of-range", audit_fixture=None, owning_group="robustness",
        description="A skinned triangle whose vertex 1 is weighted 0.4 to joint 5 in a two-joint "
                    "skin. Binding it to the root instead -- what CNA used to do -- drags the "
                    "vertex to the origin, so the file is refused. Its twin proves the refusal is "
                    "conditional on the weight, not on the index alone.",
        builder=b, validated_layers=["L1", "L2"],
        features=["out-of-range JOINTS_0 index", "weighted stray influence", "import rejection"],
        spec_anchors=["skins", "skinned-mesh-attributes"],
        l3={"primitives": []},
        l4={},
        l5=l5_unsupported("The primitive is refused at extraction, so no buffers are produced.",
                          ["GLTF-254"]),
        rejection={
            "stage": "extraction",
            "task": "GLTF-254",
            "errorContains": ["vertex 1", "joint 5", "only 2 joints"],
            "note": "cgltf_validate does not check that a JOINTS_0 value indexes the skin's own "
                    "joints array, so this file passes structural validation and is refused during "
                    "extraction, where the skin's joint count is known. The refusal names the "
                    "vertex, the weight and the offending index, because 'invalid joint index' "
                    "alone does not tell an author which vertex to fix.",
        },
    )


def skin_joint_index_padding() -> Fixture:
    """The same stray indices, all **unweighted**. The control for **`GLTF-254`**'s refusal.

    Exporters routinely fill a vertex's unused influence slots with an arbitrary joint index and a
    zero weight -- the slot contributes nothing to the skin equation, so the index is never read as
    a joint. A check that refused every out-of-range index would reject those files for no gain, and
    this fixture is what stops that check from ever being written that way.
    """
    b, mesh = _stray_joint_builder("skin-joint-index-padding", _PADDED_JOINTS, _PADDED_WEIGHTS)
    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["skin"] = {
        "jointCount": 2,
        "meshNodeWorldColumnMajor": mat_identity(),
        "jointsAsAuthored": [list(j) for j in _PADDED_JOINTS],
        "jointsAfterPolicy": [list(j) for j in _PADDED_JOINTS_IMPORTED],
        "policy": "An out-of-range joint index carrying ZERO weight is clamped to joint 0 and "
                  "imported. It contributes nothing to the skin equation, so nothing is lost, and "
                  "refusing it would reject the padding pattern nearly every exporter emits.",
    }
    return Fixture(
        id="skin-joint-index-padding", audit_fixture=None, owning_group="robustness",
        description="The same out-of-range joint indices as skin-joint-index-out-of-range, with "
                    "every one of them carrying zero weight. Imports cleanly. Without this fixture "
                    "GLTF-254's check could be tightened into one that rejects real assets and "
                    "every other test would stay green.",
        builder=b, validated_layers=["L1", "L2", "L3"],
        features=["out-of-range JOINTS_0 index", "zero-weight padding slot"],
        spec_anchors=["skins", "skinned-mesh-attributes"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="StrayJointTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, joints=_PADDED_JOINTS_IMPORTED,
            weights=_PADDED_WEIGHTS, indices=TRIANGLE_INDICES)]},
        l4=l4,
    )


def bad_animation_input_order() -> Fixture:
    """An animation sampler whose input times go **backwards**. Proves **`GLTF-313`**.

    §3.11 requires sampler input to be strictly increasing, and every reader here takes that on
    trust: ``FindBracket`` walks the array once looking for the first pair straddling ``t``, and
    the track builder merges channel times with a sort-then-unique that assumes its inputs were
    already ordered. Times ``0, 2, 1`` break both -- the curve doubles back, so a time inside the
    reversed span has two authored values.

    Refused rather than sorted, and which of the two is not arbitrary. Sorting would silently
    re-pair each time with a different value than the exporter wrote, turning a broken file into a
    plausible-looking wrong animation that plays; a named failure at import is recoverable, and a
    quietly wrong animation is the failure mode this whole corpus exists to prevent. The **equal**
    case is answered the other way -- see ``anim-repeated-time`` -- because equal times are what an
    exporter emits for a hard cut and they read correctly.

    Refused at *extraction*, not at validation: ``cgltf_validate`` does not check sampler input
    ordering, so this is CNA's own check and the only thing standing between the file and an
    animation whose values are paired with the wrong times.
    """
    b = GltfBuilder("bad-animation-input-order")
    position = b.add_packed_accessor(usage="POSITION", values=TRIANGLE_POSITIONS,
                                     accessor_type="VEC3", with_bounds=True)
    normal = b.add_packed_accessor(usage="NORMAL", values=TRIANGLE_NORMALS, accessor_type="VEC3")
    indices = b.add_packed_accessor(usage="indices", values=TRIANGLE_INDICES,
                                    accessor_type="SCALAR", component_type=UNSIGNED_SHORT)
    mesh = b.add_mesh([{
        "attributes": {"POSITION": position, "NORMAL": normal},
        "indices": indices,
        "mode": TRIANGLES,
    }], name="BackwardsTri")
    node = b.add_node(name="Backwards", mesh=mesh)
    b.add_scene([node], name="Scene")
    b.set_default_scene(0)

    # Both accessors are honestly packed and honestly described; the file is malformed only in the
    # ORDER of the input values, which no structural check looks at.
    times = b.add_packed_accessor(usage="animation input (time)", values=[0.0, 2.0, 1.0],
                                  accessor_type="SCALAR", component_type=FLOAT)
    offsets = b.add_packed_accessor(
        usage="animation output (translation)",
        values=[(0.0, 0.0, 0.0), (4.0, 0.0, 0.0), (8.0, 0.0, 0.0)], accessor_type="VEC3",
        component_type=FLOAT)
    b.add_animation({
        "name": "Backwards",
        "samplers": [{"input": times, "output": offsets, "interpolation": "LINEAR"}],
        "channels": [{"sampler": 0, "target": {"node": node, "path": "translation"}}],
    })

    l4 = world_positions(b, {mesh: list(TRIANGLE_POSITIONS)})
    l4["animation"] = {
        "animationCount": 1,
        "authoredTimes": [0.0, 2.0, 1.0],
        "decreasingAtSample": 2,
        "importable": False,
    }
    return Fixture(
        id="bad-animation-input-order", audit_fixture=None, owning_group="robustness",
        description="An animation sampler whose input times are 0, 2, 1. Every accessor is "
                    "individually valid and cgltf_validate does not look at input ordering, so "
                    "this is CNA's own check -- and it refuses rather than sorting, because "
                    "sorting would re-pair each time with a value the exporter did not write.",
        builder=b, validated_layers=["L1", "L2"],
        features=["non-monotonic sampler input", "animation input ordering", "import rejection"],
        spec_anchors=["animations"],
        l3={"primitives": [l3_primitive(
            mesh=mesh, mesh_name="BackwardsTri", primitive=0, mode=TRIANGLES,
            positions=TRIANGLE_POSITIONS, normals=TRIANGLE_NORMALS, indices=TRIANGLE_INDICES)]},
        l4=l4,
        rejection={
            "stage": "extraction",
            "task": "GLTF-313",
            "errorContains": ["not ascending", "strictly increasing"],
            "alsoRefusedAtExtraction": True,
            "extractionErrorContains": ["not ascending", "strictly increasing"],
            "note": "cgltf_validate has no rule about sampler input ordering, so structural "
                    "validation passes and the mesh itself imports fine -- the refusal comes from "
                    "clip extraction. Equal adjacent times are deliberately NOT refused "
                    "(anim-repeated-time): they are what an exporter writes for a hard cut and "
                    "they read correctly, whereas a decreasing step has two authored values for "
                    "one time and no defensible reading at all.",
        },
    )


FIXTURES = [bad_accessor_out_of_bounds, accessor_count_mismatch, skin_joint_index_out_of_range,
            skin_joint_index_padding, bad_animation_input_order]
